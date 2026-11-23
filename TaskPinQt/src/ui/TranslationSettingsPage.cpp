#include "ui/TranslationSettingsPage.h"
#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include <QBuffer>
#include <QCryptographicHash>
#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDirIterator>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QScrollArea>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardItemModel>
#include <QSignalBlocker>
#include <QDesktopServices>
#include <QTimer>
#include <QVBoxLayout>

namespace {
constexpr qsizetype MaxValidationReplyBytes=1024*1024;
const QSize ValidationImageSize(128,64);
}

TranslationSettingsPage::TranslationSettingsPage(QWidget *parent):QWidget(parent)
{
    setObjectName("translationSettingsPage");
    const auto config=Translation::Config::load();m_authorizedEndpoint=config.consentEndpoint;
    auto *outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto *scroll=new QScrollArea(this);scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    auto *content=new QWidget(scroll);scroll->setWidget(content);outer->addWidget(scroll);
    auto *box=new QVBoxLayout(content);
    auto *intro=new QLabel(QStringLiteral("与 AI 页共用接口、模型和 API Key，无需重复填写。便签 AI 归纳开关与截图翻译分别控制。"),this);
    intro->setWordWrap(true);box->addWidget(intro);
    auto *form=new QFormLayout;
    m_mode=new QComboBox(this);m_mode->setObjectName("translationMode");
    m_mode->addItem(QStringLiteral("云端翻译 · 使用 AI 页连接"),"cloud");
    m_mode->addItem(QStringLiteral("仅本地 OCR · 不翻译"),"local");
    m_mode->addItem(QStringLiteral("离线 Mock · 仅供流程测试"),"mock");
    m_mode->setCurrentIndex(qMax(0,m_mode->findData(config.mode)));form->addRow(QStringLiteral("模式"),m_mode);
    m_timeout=new QSpinBox(this);m_timeout->setRange(5,120);m_timeout->setSuffix(QStringLiteral(" 秒"));m_timeout->setValue(config.timeoutMs/1000);
    form->addRow(QStringLiteral("请求超时"),m_timeout);
    QString ocrError;const auto ocr=Translation::OcrProfiles::load(&ocrError);
    m_ocrProfile=new QComboBox(this);m_ocrProfile->setObjectName("translationOcrProfile");
    m_ocrProfile->addItem(QStringLiteral("PP-OCRv5 mobile · 默认 · 20.5 MiB"),"builtin-ppocrv5-mobile");
    m_ocrProfile->addItem(QStringLiteral("PP-OCRv4 mobile · 轻量 · 14.9 MiB"),"builtin-ppocrv4-mobile");
    m_ocrProfile->addItem(QStringLiteral("PP-OCRv6 tiny · 960 像素 · 4 线程"),"builtin-ppocrv6-tiny");
    m_ocrProfile->addItem(QStringLiteral("自定义本地模型配置"),"custom");
    if(m_ocrProfile->findData(ocr.profile)<0 && !ocr.profile.isEmpty()) {
        m_ocrProfile->addItem(QStringLiteral("不可用 · %1").arg(ocr.profile),ocr.profile);
        qobject_cast<QStandardItemModel *>(m_ocrProfile->model())->item(m_ocrProfile->count()-1)->setEnabled(false);
    }
    m_ocrProfile->setCurrentIndex(m_ocrProfile->findData(ocr.profile));form->addRow(QStringLiteral("本地 OCR · 便签与翻译共用"),m_ocrProfile);
    auto *ocrRow=new QHBoxLayout;m_ocrPath=new QLineEdit(ocr.customProfilePath,this);m_ocrPath->setObjectName("translationOcrProfilePath");
    m_ocrPath->setPlaceholderText(QStringLiteral("选择包含 ONNX 路径与 SHA256 的 JSON 配置"));
    auto *browse=new QPushButton(QStringLiteral("选择文件…"),this);browse->setObjectName("translationOcrProfileBrowse");
    ocrRow->addWidget(m_ocrPath,1);ocrRow->addWidget(browse);form->addRow(QStringLiteral("模型配置"),ocrRow);
    auto *catalogRow=new QHBoxLayout;
    auto *openFolder=new QPushButton(QStringLiteral("打开模型文件夹"),this);openFolder->setObjectName("translationOcrOpenFolder");
    m_ocrRefresh=new QPushButton(QStringLiteral("刷新模型"),this);m_ocrRefresh->setObjectName("translationOcrRefresh");
    catalogRow->addWidget(openFolder);catalogRow->addWidget(m_ocrRefresh);form->addRow(QStringLiteral("文件夹模型"),catalogRow);
    connect(openFolder,&QPushButton::clicked,this,[] {
        const auto directory=Translation::OcrProfiles::modelDirectory();
        if(QDir().mkpath(directory))QDesktopServices::openUrl(QUrl::fromLocalFile(directory));
    });
    connect(m_ocrRefresh,&QPushButton::clicked,this,&TranslationSettingsPage::refreshOcrCatalog);
    auto updateOcr=[this,browse] {
        const bool custom=m_ocrProfile->currentData().toString()=="custom";m_ocrPath->setEnabled(custom);browse->setEnabled(custom);
        const auto id=m_ocrProfile->currentData().toString();
        if(Translation::OcrProfiles::isLocalProfile(id)) {
            const auto entry=m_catalogEntries.value(id);
            m_ocrStatus->setText(entry.value("enabled").toBool()?entry.value("description").toString():
                QStringLiteral("所选文件夹模型不可用：%1").arg(entry.value("error").toString(QStringLiteral("尚未发现模型，请刷新或检查模型文件夹。"))));return;
        }
        if(id=="builtin-ppocrv6-tiny") {
            m_ocrStatus->setText(QStringLiteral("随应用提供的 PP-OCRv6 tiny，最大边 960 像素，4 个推理线程。本地识别后仅文字按上方模式发送翻译。"));return;
        }
        m_ocrStatus->setText(custom?QStringLiteral("保存时校验本地文件、模型家族和 SHA256。配置错误会显示失败，不会自动下载或改用默认模型。"):
            m_ocrProfile->currentData().toString()=="builtin-ppocrv4-mobile"?
            QStringLiteral("轻量模型已随应用提供，无需另行下载。权重约 14.9 MiB，适合常见中英文截图；复杂文字建议与默认 v5 对比。速度因图片与设备而异。"):
            QStringLiteral("使用随应用提供的 PP-OCRv5 mobile，权重约 20.5 MiB。本地识别后，仅文字可按上方模式发送翻译。"));
    };
    m_ocrStatus=new QLabel(this);m_ocrStatus->setObjectName("translationOcrStatus");m_ocrStatus->setWordWrap(true);m_ocrStatus->setTextFormat(Qt::PlainText);
    form->addRow(QString(),m_ocrStatus);updateOcr();if(!ocrError.isEmpty())m_ocrStatus->setText(ocrError);
    connect(m_ocrProfile,&QComboBox::currentIndexChanged,this,updateOcr);
    connect(browse,&QPushButton::clicked,this,[this] {
        const QPointer<TranslationSettingsPage> guard(this);
        const QString chosen=QFileDialog::getOpenFileName(this,QStringLiteral("选择本地 OCR 模型配置"),m_ocrPath->text(),QStringLiteral("JSON 配置 (*.json);;所有文件 (*)"));
        if(guard && !chosen.isEmpty())guard->m_ocrPath->setText(chosen);
    });
    box->addLayout(form);
    m_connection=new QLabel(this);m_connection->setObjectName("translationSharedConnection");m_connection->setWordWrap(true);m_connection->setTextFormat(Qt::PlainText);box->addWidget(m_connection);
    auto *edit=new QPushButton(QStringLiteral("前往 AI 页编辑连接 / 密钥"),this);edit->setObjectName("translationEditAi");
    connect(edit,&QPushButton::clicked,this,&TranslationSettingsPage::editAiRequested);box->addWidget(edit);
    m_consent=new QCheckBox(QStringLiteral("允许主动截图后，将识别文字发送到上述接口翻译（不发送图片）"),this);
    m_consent->setObjectName("translationConsent");box->addWidget(m_consent);
    auto *hint=new QLabel(QStringLiteral("在 F1 框选工具栏点击“翻译”，结果直接贴到桌面。拖动移动、滚轮缩放，右键可切换原图或查看文字对照。复杂背景使用译文底板；代码与公式保留。取消设置不会保存更改，取消请求不能撤回已发送文字。"),this);
    hint->setWordWrap(true);box->addWidget(hint);
    auto *row=new QHBoxLayout;
    m_test=new QPushButton(QStringLiteral("测试连接 · 仅发送测试短句"),this);m_test->setObjectName("translationTestConnection");
    m_cancel=new QPushButton(QStringLiteral("取消测试"),this);m_cancel->setEnabled(false);
    row->addWidget(m_test);row->addWidget(m_cancel);box->addLayout(row);
    m_status=new QLabel(this);m_status->setObjectName("translationConnectionStatus");m_status->setWordWrap(true);m_status->setTextFormat(Qt::PlainText);box->addWidget(m_status);box->addStretch();
    m_client=new Translation::Client(this);
    connect(m_test,&QPushButton::clicked,this,&TranslationSettingsPage::testConnection);
    connect(m_cancel,&QPushButton::clicked,this,[this] {cancelTest();m_status->setText(QStringLiteral("已取消连接测试。"));});
    connect(m_client,&Translation::Client::finished,this,[this](const QString &,const QList<Translation::Result> &results) {
        m_test->setEnabled(true);m_cancel->setEnabled(false);
        bool ok=results.size()==1 && results.first().status=="ok";
        m_status->setText(ok?QStringLiteral("连接成功 · 已收到真实译文：%1").arg(results.first().text):QStringLiteral("连接成功，但测试译文未通过完整性校验，请检查模型。"));
    });
    connect(m_client,&Translation::Client::failed,this,[this](const QString &,const QString &error) {m_test->setEnabled(true);m_cancel->setEnabled(false);m_status->setText(error);});
    m_ocrValidationTimeout=new QTimer(this);m_ocrValidationTimeout->setSingleShot(true);
    connect(m_ocrValidationTimeout,&QTimer::timeout,this,[this] {failOcrValidation(QStringLiteral("本地 OCR 模型校验超时，请检查模型文件后重试。"));});
    connect(m_mode,&QComboBox::currentIndexChanged,this,&TranslationSettingsPage::invalidateOcrValidation);
    connect(m_timeout,&QSpinBox::valueChanged,this,&TranslationSettingsPage::invalidateOcrValidation);
    connect(m_consent,&QCheckBox::toggled,this,&TranslationSettingsPage::invalidateOcrValidation);
    connect(m_ocrProfile,&QComboBox::currentIndexChanged,this,&TranslationSettingsPage::invalidateOcrValidation);
    connect(m_ocrPath,&QLineEdit::textChanged,this,&TranslationSettingsPage::invalidateOcrValidation);
    m_catalogTimeout=new QTimer(this);m_catalogTimeout->setSingleShot(true);
    connect(m_catalogTimeout,&QTimer::timeout,this,[this]{cancelOcrCatalog();finishOcrCatalog({},QStringLiteral("模型发现超时，请重试。"));});
    refreshOcrCatalog();
}
TranslationSettingsPage::~TranslationSettingsPage(){cancelOcrCatalog();cancelOcrValidation(false);}
void TranslationSettingsPage::setConnection(const QString &endpoint,const QString &model,const QString &key)
{
    const QPointer<TranslationSettingsPage> guard(this);
    const bool changed=m_endpoint!=endpoint.trimmed() || m_model!=model.trimmed() || m_key!=key.trimmed();
    if(changed && m_client->busy())cancelTest();
    if(!guard)return;
    if(changed)invalidateOcrValidation();
    if(!guard)return;
    Translation::Config before;before.endpoint=m_endpoint;
    m_endpoint=endpoint.trimmed();m_model=model.trimmed();m_key=key.trimmed();
    Translation::Config cfg;cfg.endpoint=m_endpoint;
    if(!m_initialized)m_consent->setChecked(m_authorizedEndpoint==cfg.requestUrl().toString(QUrl::FullyEncoded));
    else if(before.requestUrl()!=cfg.requestUrl())m_consent->setChecked(false);
    m_initialized=true;
    m_connection->setText(QStringLiteral("共享接口：%1\n模型：%2\nAPI Key：%3").arg(cfg.requestUrl().toDisplayString(),m_model,m_key.isEmpty()?QStringLiteral("尚未配置，请到 AI 页填写"):QStringLiteral("已配置 · 与 AI 页共用")));
    if(changed)m_status->clear();
}
Translation::Config TranslationSettingsPage::configuration() const
{
    Translation::Config cfg;cfg.mode=m_mode->currentData().toString();cfg.endpoint=m_endpoint;cfg.model=m_model;
    cfg.timeoutMs=m_timeout->value()*1000;
    cfg.consentEndpoint=m_consent->isChecked()?cfg.requestUrl().toString(QUrl::FullyEncoded):QString();return cfg;
}
bool TranslationSettingsPage::save()
{
    const auto cfg=configuration();
    const auto ocr=ocrConfiguration();QString error;
    // Only the bounded JSON and supported option/path syntax are read here.
    // Model hashes and actual ONNX compatibility run in a cancellable process.
    if(!ocr.validate(&error,false)){m_ocrStatus->setText(error);invalidateOcrValidation();return false;}
    if(Translation::OcrProfiles::isLocalProfile(ocr.profile) &&
       (m_ocrCatalog || !m_catalogEntries.value(ocr.profile).value("enabled").toBool())) {
        m_ocrStatus->setText(QStringLiteral("所选文件夹模型不可用或仍在刷新，请检查模型文件夹后重试。"));return false;
    }
    if(cfg.mode=="cloud" && (!cfg.authorized() || m_model.isEmpty() || m_key.isEmpty())) {
        m_status->setText(QStringLiteral("请在 AI 页配置有效的 HTTPS 接口、模型和密钥，并勾选文字翻译授权。"));return false;
    }
    if(ocr.profile=="custom" || Translation::OcrProfiles::isLocalProfile(ocr.profile)) {
        const auto draftKey=ocrDraftKey(ocr);
        if(m_validatedDraft.isEmpty() || m_validatedDraft!=draftKey) {
            if(m_ocrValidation && m_validationDraft==draftKey)return false;
            const QPointer<TranslationSettingsPage> guard(this);cancelOcrValidation(true);
            if(guard)guard->beginOcrValidation(ocr,draftKey);
            return false;
        }
        // The successful validation authorizes exactly one save of this draft.
        m_validatedDraft.clear();
    }
    const QString translationPath=QFileInfo(Translation::OcrProfiles::settingsPath()).dir().filePath("translation.json");
    QFile prior(translationPath);const bool existed=prior.exists();QByteArray previous;
    if(existed){if(!prior.open(QIODevice::ReadOnly)){m_status->setText(QStringLiteral("无法读取现有翻译设置，更改未保存。"));return false;}previous=prior.readAll();prior.close();}
    if(!cfg.save(&error)){m_status->setText(error);return false;}
    if(!ocr.save(&error,false)) {
        bool restored;
        if(existed){QSaveFile restore(translationPath);restored=restore.open(QIODevice::WriteOnly) && restore.write(previous)==previous.size() && restore.commit();}
        else restored=QFile::remove(translationPath);
        m_ocrStatus->setText(error+(restored?QStringLiteral("；更改未保存。"):QStringLiteral("；翻译配置回滚失败，请检查磁盘后重新确认。")));return false;
    }
    cancelTest();return true;
}
Translation::OcrProfiles TranslationSettingsPage::ocrConfiguration() const
{
    Translation::OcrProfiles cfg;cfg.profile=m_ocrProfile->currentData().toString();cfg.customProfilePath=m_ocrPath->text().trimmed();return cfg;
}
QByteArray TranslationSettingsPage::ocrDraftKey(const Translation::OcrProfiles &ocr) const
{
    const auto cfg=configuration();
    QJsonObject draft{{"profile",ocr.profile},{"path",ocr.profilePath()},{"mode",cfg.mode},
        {"endpoint",m_endpoint},{"model",m_model},{"key",m_key},{"timeoutMs",cfg.timeoutMs},
        {"consentEndpoint",cfg.consentEndpoint},{"runtime",Translation::Ocr::runtimeDirectory()}};
    draft.insert("modelDirectory",Translation::OcrProfiles::modelDirectory());
    QStringList paths;
    QFile profile(ocr.profilePath());QByteArray bytes;
    if(ocr.profile=="custom" && profile.open(QIODevice::ReadOnly) && profile.size()<=65536)bytes=profile.read(65537);
    draft.insert("profileBytes",QString::fromLatin1(bytes.toBase64()));
    const auto object=QJsonDocument::fromJson(bytes).object();
    QJsonArray weights;
    if(Translation::OcrProfiles::isLocalProfile(ocr.profile)) {
        for(const auto &value:m_catalogEntries.value(ocr.profile).value("files").toArray())paths.append(value.toString());
        if(!paths.isEmpty()) {
            QString packagePath=QFileInfo(paths.first()).absolutePath();
            for(const auto &path:paths)if(QFileInfo(path).fileName()=="model.json")packagePath=QFileInfo(path).absolutePath();
            const QFileInfo package(packagePath);
            draft.insert("packageDirectory",QJsonObject{{"path",package.absoluteFilePath()},{"canonicalPath",package.canonicalFilePath()},
                {"exists",package.isDir()},{"modified",QString::number(package.lastModified().toMSecsSinceEpoch())},
                {"created",QString::number(package.birthTime().toMSecsSinceEpoch())}});
            // Package interpretation also depends on its immediate members:
            // an extra ONNX can make a previously valid pair ambiguous. Match
            // the helper's 64-entry bound, retaining one overflow entry, and
            // never recurse or open/hash model files on the GUI thread.
            QDirIterator entries(packagePath,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDirIterator::NoIteratorFlags);
            QMap<QString,QJsonObject> members;
            while(members.size()<65 && entries.hasNext()) {
                entries.next();const auto member=entries.fileInfo();
                members.insert(member.fileName(),QJsonObject{{"name",member.fileName()},{"canonicalPath",member.canonicalFilePath()},
                    {"file",member.isFile()},{"directory",member.isDir()},{"symlink",member.isSymLink()},
                    {"size",QString::number(member.size())},{"modified",QString::number(member.lastModified().toMSecsSinceEpoch())},
                    {"created",QString::number(member.birthTime().toMSecsSinceEpoch())}});
            }
            QJsonArray membership;for(const auto &member:members)membership.append(member);
            draft.insert("packageMembers",membership);draft.insert("packageTooManyEntries",members.size()>64);
            // A descriptor can be introduced after filename-only discovery.
            // Include its missing-file stat so that this also invalidates a
            // successful load without opening or hashing any model weights.
            const auto descriptor=QFileInfo(paths.first()).dir().absoluteFilePath("model.json");
            if(!paths.contains(descriptor))paths.append(descriptor);
        }
    } else for(const auto &task:QStringList{"det","rec"})
        paths.append(QFileInfo(ocr.profilePath()).dir().absoluteFilePath(object.value(task).toObject().value("path").toString()));
    for(const auto &path:paths) {
        const QFileInfo weight(path);
        // Stat identity detects ordinary replacement without hashing/opening
        // potentially large model files on the GUI thread.
        weights.append(QJsonObject{{"path",weight.absoluteFilePath()},{"canonicalPath",weight.canonicalFilePath()},
            {"exists",weight.isFile()},{"size",QString::number(weight.size())},
            {"modified",QString::number(weight.lastModified().toMSecsSinceEpoch())},
            {"created",QString::number(weight.birthTime().toMSecsSinceEpoch())}});
    }
    draft.insert("weights",weights);
    return QCryptographicHash::hash(QJsonDocument(draft).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
void TranslationSettingsPage::beginOcrValidation(const Translation::OcrProfiles &ocr,const QByteArray &draftKey)
{
    if(!Translation::Ocr::available()) {
        m_ocrStatus->setText(QStringLiteral("PaddleOCR 本地运行环境未完整部署，请解压完整发行包后重试。"));return;
    }
    QString expectedId=ocr.profile;
    if(ocr.profile=="custom") {
        QFile profile(ocr.profilePath());
        if(!profile.open(QIODevice::ReadOnly) || profile.size()>65536) {
            m_ocrStatus->setText(QStringLiteral("无法读取所选 OCR 模型配置文件。"));return;
        }
        expectedId=QJsonDocument::fromJson(profile.readAll()).object().value("id").toString();
    }
    const QString runtime=Translation::Ocr::runtimeDirectory();
    auto *process=new QProcess(this);m_ocrValidation=process;m_validationDraft=draftKey;m_validationOutput.clear();
    const auto generation=++m_validationGeneration;
    process->setProgram(runtime+"/python.exe");process->setWorkingDirectory(runtime);
    process->setArguments({"-I","-B",runtime+"/helper.py"});
    const auto current=[guard=QPointer<TranslationSettingsPage>(this),process,generation] {
        return guard && guard->m_ocrValidation==process && guard->m_validationGeneration==generation;
    };
    connect(process,&QProcess::started,this,[this,process,current,ocr] {
        if(!current())return;
        QImage image(ValidationImageSize,QImage::Format_RGB32);image.fill(Qt::white);
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);
        if(!image.save(&buffer,"PNG")){failOcrValidation(QStringLiteral("无法准备本地 OCR 模型校验。"));return;}
        QJsonObject payload{{"image",QString::fromLatin1(png.toBase64())}};
        if(ocr.profile=="custom")payload.insert("profilePath",ocr.profilePath());
        else {payload.insert("profileId",ocr.profile);payload.insert("modelDirectory",Translation::OcrProfiles::modelDirectory());}
        const auto request=QJsonDocument(payload).toJson(QJsonDocument::Compact);
        if(process->write(request)!=request.size()){failOcrValidation(QStringLiteral("无法发送本地 OCR 模型校验。"));return;}
        process->closeWriteChannel();
    });
    const auto readOutput=[this,process,current] {
        if(!current())return;
        process->setReadChannel(QProcess::StandardOutput);
        m_validationOutput+=process->read(MaxValidationReplyBytes+1-m_validationOutput.size());
        if(m_validationOutput.size()>MaxValidationReplyBytes)failOcrValidation(QStringLiteral("本地 OCR 模型校验响应超过大小限制。"));
    };
    connect(process,&QProcess::readyReadStandardOutput,this,readOutput);
    connect(process,&QProcess::readyReadStandardError,this,[process] {process->readAllStandardError();});
    connect(process,&QProcess::errorOccurred,this,[this,current](QProcess::ProcessError error) {
        if(current() && error==QProcess::FailedToStart)failOcrValidation(QStringLiteral("无法启动本地 OCR 运行环境。"));
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this,process,current,readOutput,expectedId,draftKey](int exitCode,QProcess::ExitStatus exitStatus) {
        if(!current())return;
        readOutput();if(!current())return;
        Translation::OcrResult result;QString error;
        const bool decoded=Translation::Ocr::decode(m_validationOutput,ValidationImageSize,&result,&error);
        if(!decoded || exitStatus!=QProcess::NormalExit || exitCode!=0) {
            failOcrValidation(error.isEmpty()?QStringLiteral("所选 OCR 模型无法在本地引擎中加载。请检查 ONNX 模型与字符字典。"):error);return;
        }
        if(result.profileId!=expectedId) {failOcrValidation(QStringLiteral("本地 OCR 模型校验返回了不同配置，未保存更改。"));return;}
        if(draftKey!=ocrDraftKey(ocrConfiguration())) {failOcrValidation(QStringLiteral("模型配置或设置已更改，请重新保存以校验当前选择。"));return;}
        m_ocrValidationTimeout->stop();m_ocrValidation.clear();m_validationDraft.clear();m_validationOutput.clear();
        ++m_validationGeneration;process->deleteLater();m_validatedDraft=draftKey;
        m_ocrStatus->setText(QStringLiteral("本地 OCR 模型校验成功，正在保存设置…"));
        const QPointer<TranslationSettingsPage> guard(this);
        emit validationBusyChanged(false);
        if(guard && guard->m_validatedDraft==draftKey)emit guard->saveReady();
    });
    m_ocrStatus->setText(QStringLiteral("正在本地校验 SHA256 并加载 OCR 模型…可取消设置，原图与模型不会上传。"));
    m_ocrValidationTimeout->start(90000);
    const QPointer<TranslationSettingsPage> guard(this);emit validationBusyChanged(true);
    if(guard && current())process->start();
}
void TranslationSettingsPage::cancelOcrValidation(bool notify)
{
    ++m_validationGeneration;m_ocrValidationTimeout->stop();m_validatedDraft.clear();m_validationDraft.clear();m_validationOutput.clear();
    if(!m_ocrValidation)return;
    auto *process=m_ocrValidation.data();m_ocrValidation.clear();
    disconnect(process,nullptr,this,nullptr);process->setParent(nullptr);
    if(process->state()!=QProcess::NotRunning) {
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
        connect(process,&QProcess::errorOccurred,process,[process](QProcess::ProcessError error) {if(error==QProcess::FailedToStart)process->deleteLater();});
        process->kill();
    } else process->deleteLater();
    if(notify)emit validationBusyChanged(false);
}
void TranslationSettingsPage::failOcrValidation(const QString &message)
{
    m_ocrStatus->setText(message);cancelOcrValidation(true);
}
void TranslationSettingsPage::invalidateOcrValidation()
{
    if(m_ocrValidation)m_ocrStatus->setText(QStringLiteral("设置已更改，请保存以校验当前本地模型。"));
    cancelOcrValidation(true);
}
void TranslationSettingsPage::refreshOcrCatalog()
{
    cancelOcrCatalog();invalidateOcrValidation();
    m_ocrRefresh->setEnabled(false);
    const auto runtime=Translation::Ocr::runtimeDirectory();
    auto *process=new QProcess(this);m_ocrCatalog=process;m_catalogOutput.clear();
    const auto generation=++m_catalogGeneration;
    const auto current=[guard=QPointer<TranslationSettingsPage>(this),process,generation] {
        return guard && guard->m_ocrCatalog==process && guard->m_catalogGeneration==generation;
    };
    const auto fail=[this,current](const QString &error) {
        if(!current())return;
        cancelOcrCatalog();finishOcrCatalog({},error);
    };
    const auto read=[this,process,current,fail] {
        if(!current())return;
        process->setReadChannel(QProcess::StandardOutput);
        m_catalogOutput+=process->read(MaxValidationReplyBytes+1-m_catalogOutput.size());
        if(m_catalogOutput.size()>MaxValidationReplyBytes)fail(QStringLiteral("模型发现响应超过大小限制。"));
    };
    connect(process,&QProcess::readyReadStandardOutput,this,read);
    connect(process,&QProcess::readyReadStandardError,this,[process]{process->readAllStandardError();});
    connect(process,&QProcess::errorOccurred,this,[fail](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)fail(QStringLiteral("无法启动模型发现，请检查本地 OCR 运行环境。"));
    });
    connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,
        [this,current,read,fail](int code,QProcess::ExitStatus status) {
        if(!current())return;read();if(!current())return;
        QJsonParseError parse;const auto document=QJsonDocument::fromJson(m_catalogOutput,&parse);
        const auto object=document.object();const auto models=object.value("models").toArray();
        if(code!=0 || status!=QProcess::NormalExit || parse.error!=QJsonParseError::NoError || !document.isObject() ||
           !object.value("ok").toBool() || !object.value("models").isArray() || models.size()>131) {
            fail(QStringLiteral("模型发现失败：%1").arg(object.value("error").toString(QStringLiteral("请检查本地 OCR 运行环境与模型文件夹。"))).left(8192));return;
        }
        QSet<QString> seen;
        for(const auto &value:models) {
            const auto entry=value.toObject();const auto id=entry.value("id").toString();
            const bool knownBuiltin=QStringList{"builtin-ppocrv4-mobile","builtin-ppocrv5-mobile","builtin-ppocrv6-tiny"}.contains(id);
            const auto files=entry.value("files").toArray();
            if(!value.isObject() || (!knownBuiltin && !Translation::OcrProfiles::isLocalProfile(id)) || seen.contains(id) ||
               !entry.value("name").isString() || entry.value("name").toString().isEmpty() || entry.value("name").toString().size()>1024 ||
               !entry.value("description").isString() || entry.value("description").toString().size()>8192 ||
               !entry.value("enabled").isBool() || !entry.value("files").isArray() || files.size()>3 ||
               entry.value("source").toString()!=(knownBuiltin?QStringLiteral("builtin"):QStringLiteral("folder")) ||
               (!knownBuiltin && entry.value("enabled").toBool() && files.size()<2)) {
                fail(QStringLiteral("模型发现返回了无效条目，请检查本地 OCR 运行环境。"));return;
            }
            for(const auto &file:files)if(!file.isString() || file.toString().size()>32768 || !QFileInfo(file.toString()).isAbsolute()) {
                fail(QStringLiteral("模型发现返回了无效文件路径。"));return;
            }
            seen.insert(id);
        }
        cancelOcrCatalog();finishOcrCatalog(models);
    });
    process->setProgram(runtime+"/python.exe");process->setWorkingDirectory(runtime);
    process->setArguments({"-I","-B",runtime+"/helper.py","--catalog","--model-dir",Translation::OcrProfiles::modelDirectory()});
    m_catalogTimeout->start(30000);process->start();process->closeWriteChannel();
}
void TranslationSettingsPage::finishOcrCatalog(const QJsonArray &models,const QString &error)
{
    const auto selected=m_ocrProfile->currentData().toString();
    {
        const QSignalBlocker blocker(m_ocrProfile);m_catalogEntries.clear();
        for(int i=m_ocrProfile->count()-1;i>=0;--i)
            if(Translation::OcrProfiles::isLocalProfile(m_ocrProfile->itemData(i).toString()))m_ocrProfile->removeItem(i);
        auto *model=qobject_cast<QStandardItemModel *>(m_ocrProfile->model());
        for(const auto &value:models) {
            const auto entry=value.toObject();const auto id=entry.value("id").toString();
            if(!Translation::OcrProfiles::isLocalProfile(id))continue;
            m_catalogEntries.insert(id,entry);const bool enabled=entry.value("enabled").toBool();
            m_ocrProfile->addItem((enabled?QString():QStringLiteral("不可用 · "))+entry.value("name").toString(),id);
            auto *item=model->item(m_ocrProfile->count()-1);item->setEnabled(enabled);
            item->setToolTip("<p>"+(enabled?entry.value("description").toString():entry.value("error").toString()).toHtmlEscaped()+"</p>");
        }
        if(!selected.isEmpty() && m_ocrProfile->findData(selected)<0) {
            m_ocrProfile->addItem(QStringLiteral("不可用 · %1").arg(selected),selected);
            model->item(m_ocrProfile->count()-1)->setEnabled(false);
        }
        m_ocrProfile->setCurrentIndex(m_ocrProfile->findData(selected));
    }
    m_ocrRefresh->setEnabled(true);
    if(!error.isEmpty())m_ocrStatus->setText(error);
    else if(Translation::OcrProfiles::isLocalProfile(selected)) {
        const auto entry=m_catalogEntries.value(selected);
        m_ocrStatus->setText(entry.value("enabled").toBool()?entry.value("description").toString():
            QStringLiteral("所选文件夹模型不可用：%1").arg(entry.value("error").toString(QStringLiteral("文件夹已移除或改名，请重新选择模型。"))));
    }
}
void TranslationSettingsPage::cancelOcrCatalog()
{
    ++m_catalogGeneration;m_catalogTimeout->stop();m_catalogOutput.clear();m_ocrRefresh->setEnabled(true);
    if(!m_ocrCatalog)return;
    auto *process=m_ocrCatalog.data();m_ocrCatalog.clear();disconnect(process,nullptr,this,nullptr);process->setParent(nullptr);
    if(process->state()!=QProcess::NotRunning) {
        connect(process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),process,&QObject::deleteLater);
        connect(process,&QProcess::errorOccurred,process,[process](QProcess::ProcessError error){if(error==QProcess::FailedToStart)process->deleteLater();});
        process->kill();
    } else process->deleteLater();
}
void TranslationSettingsPage::cancelTest(){m_client->cancel();m_test->setEnabled(true);m_cancel->setEnabled(false);cancelOcrCatalog();cancelOcrValidation(true);}
void TranslationSettingsPage::testConnection()
{
    auto cfg=configuration();cfg.mode="cloud";
    cfg.useCache=false;
    // This button authorizes only the fixed synthetic request. It does not save screenshot consent.
    cfg.consentEndpoint=cfg.requestUrl().toString(QUrl::FullyEncoded);
    if(!cfg.authorized() || m_model.isEmpty() || m_key.isEmpty()){m_status->setText(QStringLiteral("请先在 AI 页填写有效的 HTTPS 接口、模型和密钥。"));return;}
    Translation::Block block;block.id="connection-test";block.kind="paragraph";block.source="The default value is 10.";
    block=Translation::protect(block,"connection");
    m_test->setEnabled(false);m_cancel->setEnabled(true);m_status->setText(QStringLiteral("正在连接并翻译固定测试短句…"));
    m_client->start("settings-test",{block},cfg,m_key);
}
