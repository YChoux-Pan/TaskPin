#include "translation/TranslationOcr.h"
#include "translation/OcrProfiles.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTimer>
#include <cmath>
#include <optional>

namespace Translation {
namespace {
constexpr int MaxReplyBytes=1024*1024;
constexpr int MaxQueuedRequests=8;
QPointer<OcrWorker> sharedWorker;
}

// One application-owned worker serializes notes and translation OCR. Owners
// carry request tokens, so cancellation/destruction cannot deliver stale text.
class OcrWorker : public QObject {
public:
    explicit OcrWorker(QObject *parent):QObject(parent)
    {
        setObjectName("paddleOcrWorker");m_timeout.setParent(this);m_idle.setParent(this);
        m_timeout.setObjectName("paddleOcrRequestTimeout");m_idle.setObjectName("paddleOcrIdleTimeout");
        m_timeout.setSingleShot(true);m_idle.setSingleShot(true);
        connect(&m_timeout,&QTimer::timeout,this,[this] {failActive(QStringLiteral("PaddleOCR 本地识别超时，请缩小选区重试。"));});
        connect(&m_idle,&QTimer::timeout,this,[this] {if(!m_active && m_queue.isEmpty())stopProcess();});
    }
    ~OcrWorker() override {stopProcess();}
    void enqueue(Ocr *owner,const QImage &image,const QString &runtime,const QString &profilePath)
    {
        if(m_queue.size()>=MaxQueuedRequests){const QString taskId=owner->m_taskId;emit owner->failed(taskId,QStringLiteral("本地 OCR 等待队列已满，请稍后重试。"));return;}
        Request request;request.owner=owner;request.id=QString::number(++m_sequence);request.image=image;
        request.size=image.size();request.runtime=runtime;request.profilePath=profilePath;
        request.queued.start();request.requestStarted=owner->m_started;
        owner->m_requestId=request.id;m_queue.append(std::move(request));startNext();
    }
    void cancel(Ocr *owner)
    {
        for(qsizetype i=m_queue.size();i>0;--i)if(m_queue[i-1].owner==owner)m_queue.removeAt(i-1);
        if(m_active && m_active->owner==owner){m_active.reset();m_timeout.stop();stopProcess();startNext();}
    }
private:
    struct Request {
        QPointer<Ocr> owner;QString id,runtime,profilePath;QImage image;QSize size;
        QElapsedTimer queued,requestStarted,processStarted;
        qint64 queueMs=0;bool sent=false;
    };
    void stopProcess()
    {
        m_idle.stop();m_output.clear();
        if(!m_process)return;
        auto *process=m_process.data();m_process.clear();
        disconnect(process,nullptr,this,nullptr);process->setParent(nullptr);
        if(process->state()!=QProcess::NotRunning){
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);process->kill();
        } else process->deleteLater();
    }
    void startNext()
    {
        if(m_active)return;
        while(!m_queue.isEmpty() && m_queue.first().owner.isNull())m_queue.removeFirst();
        if(m_queue.isEmpty()){if(m_process)m_idle.start(60000);return;}
        m_idle.stop();m_active=m_queue.takeFirst();m_active->queueMs=m_active->queued.elapsed();m_active->processStarted.start();
        if(m_process && (m_runtime!=m_active->runtime || m_process->state()==QProcess::NotRunning))stopProcess();
        m_timeout.start(90000);
        if(m_process){if(m_process->state()==QProcess::Running)sendActive();return;}
        auto *process=new QProcess(this);m_process=process;m_runtime=m_active->runtime;
        process->setProgram(m_runtime+"/python.exe");process->setWorkingDirectory(m_runtime);
        process->setArguments({"-I","-B",m_runtime+"/helper.py","--worker"});
        connect(process,&QProcess::started,this,[this,process] {if(m_process==process)sendActive();});
        connect(process,&QProcess::readyReadStandardOutput,this,[this,process] {if(m_process==process)readReplies(process);});
        connect(process,&QProcess::readyReadStandardError,this,[process] {process->readAllStandardError();});
        connect(process,&QProcess::errorOccurred,this,[this,process](QProcess::ProcessError error) {
            if(m_process==process && error==QProcess::FailedToStart)failActive(QStringLiteral("无法启动本地 OCR 运行环境。"));
        });
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,process](int,QProcess::ExitStatus) {
            if(m_process!=process)return;
            readReplies(process);if(m_process!=process)return;
            if(m_active)failActive(QStringLiteral("本地 OCR 进程意外退出，请重试。"));else stopProcess();
        });
        process->start();
    }
    void sendActive()
    {
        if(!m_active || !m_process || m_active->sent)return;
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
        if(!m_active->image.save(&buffer,"PNG")){failActive(QStringLiteral("无法准备 OCR 输入。"));return;}
        QJsonObject payload{{"image",QString::fromLatin1(png.toBase64())},{"requestId",m_active->id}};
        if(!m_active->profilePath.isEmpty())payload.insert("profilePath",m_active->profilePath);
        m_active->sent=true;m_active->image=QImage();
        if(m_process->write(QJsonDocument(payload).toJson(QJsonDocument::Compact)+"\n")<0)failActive(QStringLiteral("无法发送本地 OCR 输入。"));
    }
    void readReplies(QProcess *process)
    {
        m_output+=process->readAllStandardOutput();
        while(m_process==process) {
            const auto newline=m_output.indexOf('\n');
            if(newline<0){if(m_output.size()>MaxReplyBytes)failActive(QStringLiteral("OCR 文本超过大小限制。"));return;}
            if(newline>MaxReplyBytes){failActive(QStringLiteral("OCR 文本超过大小限制。"));return;}
            const auto line=m_output.left(newline);m_output.remove(0,newline+1);
            QJsonParseError parse;const auto document=QJsonDocument::fromJson(line,&parse);
            if(parse.error!=QJsonParseError::NoError || !document.isObject()){
                failActive(QStringLiteral("本地 OCR 返回了无效响应，请重试。"));return;
            }
            if(!m_active || document.object().value("requestId").toString()!=m_active->id)continue;
            Request done=std::move(*m_active);m_active.reset();m_timeout.stop();
            const QJsonObject timings{{"queue",double(done.queueMs)},{"process",double(done.processStarted.elapsed())},{"requestTotal",double(done.requestStarted.elapsed())}};
            if(done.owner)done.owner->accept(done.id,line,done.size,timings);
            startNext();
        }
    }
    void failActive(const QString &message)
    {
        auto failed=std::move(m_active);m_active.reset();m_timeout.stop();stopProcess();
        if(failed && failed->owner)failed->owner->reject(failed->id,message);
        startNext();
    }
    QPointer<QProcess> m_process;QString m_runtime;QByteArray m_output;
    QList<Request> m_queue;std::optional<Request> m_active;
    QTimer m_timeout,m_idle;quint64 m_sequence=0;
};

QString Ocr::runtimeDirectory()
{
    const QString configured=qEnvironmentVariable("TASKPIN_OCR_RUNTIME");
    return configured.isEmpty()?QCoreApplication::applicationDirPath()+"/ocr":QDir(configured).absolutePath();
}
bool Ocr::available()
{
    const QString runtime=runtimeDirectory();
    return QFileInfo::exists(runtime+"/python.exe") && QFileInfo::exists(runtime+"/helper.py");
}
Ocr::Ocr(QObject *parent) : QObject(parent) {}
Ocr::~Ocr() { cancel(); }
void Ocr::cancel()
{
    m_requestId.clear();if(sharedWorker)sharedWorker->cancel(this);
}
void Ocr::reject(const QString &requestId,const QString &message)
{
    if(m_requestId!=requestId)return;
    m_requestId.clear();const QString id=m_taskId;emit failed(id,message);
}
void Ocr::accept(const QString &requestId,const QByteArray &json,const QSize &size,const QJsonObject &bridgeTimings)
{
    if(m_requestId!=requestId)return;
    m_requestId.clear();const QString id=m_taskId;OcrResult result;QString error;
    if(!decode(json,size,&result,&error)){emit failed(id,error);return;}
    for(auto it=bridgeTimings.begin();it!=bridgeTimings.end();++it)result.stageMs.insert(it.key(),it.value());
    result.stageMs.insert("bridgeOverhead",qMax(0.0,bridgeTimings.value("process").toDouble()-result.stageMs.value("total").toDouble()));
    emit finished(id,result);
}
void Ocr::recognize(const QString &taskId, const QImage &image)
{
    const QString stableId=taskId;
    cancel(); m_taskId = stableId;m_started.start();
    if (image.isNull() || image.width() < 8 || image.height() < 8 || qint64(image.width())*image.height()>16000000) {
        emit failed(stableId,QStringLiteral("截图为空、过小或超过 1600 万像素，请重新框选。")); return;
    }
    QString error;const auto selection=OcrProfiles::load(&error);
    // Large weight hashing belongs to the cancellable helper, not the GUI thread.
    if(!error.isEmpty() || !selection.validate(&error,false)){emit failed(stableId,error);return;}
    const QString runtime=runtimeDirectory();
    if(!available()){emit failed(stableId,QStringLiteral("PaddleOCR 本地运行环境未完整部署，请解压完整发行包后重试。"));return;}
    if(!sharedWorker)sharedWorker=new OcrWorker(QCoreApplication::instance());
    sharedWorker->enqueue(this,image,runtime,selection.profilePath());
}
bool Ocr::decode(const QByteArray &json, const QSize &imageSize, OcrResult *result, QString *error)
{
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(json.trimmed(),&parse);
    const auto object = document.object();
    *error = QStringLiteral("PaddleOCR 本地识别失败，请检查完整模型文件，或缩小截图范围。原图未上传。");
    if(object.value("errorCode").toString()=="model_hash_mismatch")*error=QStringLiteral("所选 OCR 模型 SHA256 校验失败，请检查模型与配置版本。原图未上传。");
    else if(object.value("errorCode").toString()=="model_missing")*error=QStringLiteral("所选 OCR 模型文件缺失，请检查本地配置中的 ONNX 路径。原图未上传。");
    else if(object.value("errorCode").toString().startsWith("profile_"))*error=QStringLiteral("所选 OCR 模型配置缺失、无效或不受支持，请重新选择本地配置。原图未上传。");
    else if(object.value("errorCode").toString()=="model_incompatible")*error=QStringLiteral("所选 ONNX 模型与本地 OCR 引擎不兼容，请检查模型家族、类型与字符字典。原图未上传。");
    else if(object.value("errorCode").toString()=="network_disabled")*error=QStringLiteral("所选 OCR 模型缺少本地依赖；已阻止自动下载，请提供完整的兼容模型。原图未上传。");
    if (json.size()>MaxReplyBytes || parse.error != QJsonParseError::NoError || !object.value("ok").toBool() || !object.value("lines").isArray()) return false;
    *result=OcrResult{};
    result->imageSize = imageSize; result->language = object.value("language").toString();
    result->engine=object.value("engine").toString();result->modelVersion=object.value("modelVersion").toString();
    result->profileId=object.value("profileId").toString();result->modelIdentity=object.value("modelIdentity").toString();result->workerReused=object.value("workerReused").toBool();
    if(object.contains("stageMs")) {
        if(!object.value("stageMs").isObject())return false;
        const auto stages=object.value("stageMs").toObject();if(stages.size()>32)return false;
        for(auto it=stages.begin();it!=stages.end();++it)if(!it.value().isDouble() || !std::isfinite(it.value().toDouble()) || it.value().toDouble()<0)return false;
        result->stageMs=stages;
    }
    const auto lines = object.value("lines").toArray();
    if (lines.size()>512) { *error = QStringLiteral("OCR 行数过多，请缩小范围。"); return false; }
    int chars = 0;
    for (const auto &value : lines) {
        const auto line = value.toObject();
        QRectF rect(line.value("x").toDouble(-1),line.value("y").toDouble(-1),line.value("width").toDouble(-1),line.value("height").toDouble(-1));
        const QString text = line.value("text").toString(); chars += text.size();
        if (chars > 32000 || !rect.isValid() || rect.left()<0 || rect.top()<0 || rect.right()>imageSize.width()+1 || rect.bottom()>imageSize.height()+1) return false;
        if (text.trimmed().isEmpty()) continue;
        std::optional<double> confidence;
        if(line.contains("confidence")) {
            const auto value=line.value("confidence");const double score=value.toDouble(-1);
            if(!value.isDouble() || !std::isfinite(score) || score<0 || score>1)return false;
            confidence=score;
        }
        result->lines.append({QStringLiteral("line-%1").arg(result->lines.size()+1),text,rect,confidence});
    }
    return true;
}
}
