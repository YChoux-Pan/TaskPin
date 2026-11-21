#include "translation/TranslationOcr.h"
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

namespace Translation {
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
Ocr::Ocr(QObject *parent) : QObject(parent), m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    connect(m_timer,&QTimer::timeout,this,[this] { fail(QStringLiteral("PaddleOCR 本地识别超时，请缩小选区重试。")); });
}
Ocr::~Ocr() { cancel(); }
void Ocr::cancel()
{
    ++m_generation; m_timer->stop(); m_output.clear();
    if (m_process) {
        auto *process = m_process.data(); m_process.clear();
        disconnect(process,nullptr,this,nullptr);
        process->setParent(nullptr);
        if (process->state() != QProcess::NotRunning) {
            connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
            process->kill();
        } else process->deleteLater();
    }
}
void Ocr::fail(const QString &message)
{
    const QString id = m_taskId; cancel(); emit failed(id,message);
}
void Ocr::recognize(const QString &taskId, const QImage &image)
{
    cancel(); m_taskId = taskId;
    if (image.isNull() || image.width() < 8 || image.height() < 8 || qint64(image.width())*image.height()>16000000) {
        emit failed(taskId,QStringLiteral("截图为空、过小或超过 1600 万像素，请重新框选。")); return;
    }
    const QString runtime=runtimeDirectory();
    if(!available()){emit failed(taskId,QStringLiteral("PaddleOCR 本地运行环境未完整部署，请解压完整发行包后重试。"));return;}
    m_size=image.size();auto *process=new QProcess(this);m_process=process;
    process->setProgram(runtime+"/python.exe");process->setWorkingDirectory(runtime);
    process->setArguments({"-I","-B",runtime+"/helper.py"});
    const auto generation = m_generation;
    connect(process,&QProcess::started,this,[this,process,image,generation] {
        if (generation != m_generation) return;
        QByteArray png; QBuffer buffer(&png); buffer.open(QIODevice::WriteOnly);
        if (!image.save(&buffer,"PNG")) { fail(QStringLiteral("无法准备 OCR 输入。")); return; }
        process->write(QJsonDocument(QJsonObject{{"image",QString::fromLatin1(png.toBase64())}}).toJson(QJsonDocument::Compact));
        process->closeWriteChannel();
    });
    connect(process,&QProcess::readyReadStandardOutput,this,[this,process,generation] {
        if (generation != m_generation) return;
        m_output += process->readAllStandardOutput();
        if (m_output.size()>1024*1024) fail(QStringLiteral("OCR 文本超过大小限制。"));
    });
    connect(process,&QProcess::readyReadStandardError,this,[process] { process->readAllStandardError(); });
    connect(process,&QProcess::finished,this,[this,process,generation](int code,QProcess::ExitStatus status) {
        if (generation != m_generation || m_process != process) return;
        m_output += process->readAllStandardOutput();
        OcrResult result; QString error;
        const bool decoded = decode(m_output,m_size,&result,&error);
        const bool ok=decoded && code==0 && status==QProcess::NormalExit;
        const QString id = m_taskId; cancel();
        if (ok) emit finished(id,result); else emit failed(id,error);
    });
    connect(process,&QProcess::errorOccurred,this,[this,generation](QProcess::ProcessError error) {
        if (generation == m_generation && error == QProcess::FailedToStart) fail(QStringLiteral("无法启动本地 OCR。"));
    });
    m_timer->start(90000); process->start();
}
bool Ocr::decode(const QByteArray &json, const QSize &imageSize, OcrResult *result, QString *error)
{
    QJsonParseError parse; const auto document = QJsonDocument::fromJson(json.trimmed(),&parse);
    const auto object = document.object();
    *error = QStringLiteral("PaddleOCR 本地识别失败，请检查完整模型文件，或缩小截图范围。原图未上传。");
    if (json.size()>1024*1024 || parse.error != QJsonParseError::NoError || !object.value("ok").toBool() || !object.value("lines").isArray()) return false;
    result->imageSize = imageSize; result->language = object.value("language").toString();
    result->engine=object.value("engine").toString();result->modelVersion=object.value("modelVersion").toString();
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
