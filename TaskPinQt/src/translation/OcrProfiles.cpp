#include "translation/OcrProfiles.h"
#include "core/Settings.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <cmath>

namespace Translation {
namespace {
bool reject(QString *error, const QString &message)
{
    if(error)*error=message;
    return false;
}
bool knownKeys(const QJsonObject &object,const QStringList &allowed)
{
    for(const auto &key:object.keys())if(!allowed.contains(key))return false;
    return true;
}
bool integerInRange(const QJsonValue &value,int low,int high)
{
    const double number=value.toDouble(-1);
    return value.isDouble() && std::isfinite(number) && number==std::floor(number) && number>=low && number<=high;
}
}
QString OcrProfiles::settingsPath(){return Settings::dataDir()+"/ocr.json";}
OcrProfiles OcrProfiles::load(QString *error)
{
    if(error)error->clear();
    OcrProfiles selection;
    QFile file(settingsPath());
    if(!file.exists())return selection;
    if(!file.open(QIODevice::ReadOnly) || file.size()>65536){selection.profile.clear();reject(error,QStringLiteral("无法读取本地 OCR 配置，请重新选择模型。"));return selection;}
    QJsonParseError parse;
    const auto document=QJsonDocument::fromJson(file.readAll(),&parse);
    const auto object=document.object();
    selection.profile=object.value("profile").toString();
    selection.customProfilePath=object.value("customProfilePath").toString();
    if(parse.error!=QJsonParseError::NoError || !document.isObject() || !knownKeys(object,{"schemaVersion","profile","customProfilePath"}) ||
       !integerInRange(object.value("schemaVersion"),1,1) || !object.value("profile").isString() ||
       (object.contains("customProfilePath") && !object.value("customProfilePath").isString()) ||
       (selection.profile!="builtin-ppocrv5-mobile" && selection.profile!="custom")) {
        selection.profile.clear();reject(error,QStringLiteral("本地 OCR 配置无效或版本不受支持，请重新选择模型。"));
    }
    return selection;
}
QString OcrProfiles::profilePath() const
{
    if(profile!="custom")return {};
    const QFileInfo path(customProfilePath.trimmed());
    return path.isAbsolute()?path.absoluteFilePath():QFileInfo(settingsPath()).dir().absoluteFilePath(path.filePath());
}
bool OcrProfiles::validate(QString *error,bool verifyHashes) const
{
    if(error)error->clear();
    if(profile=="builtin-ppocrv5-mobile")return true;
    if(profile!="custom")return reject(error,QStringLiteral("本地 OCR 配置无效，请重新选择模型。"));
    if(customProfilePath.trimmed().isEmpty())return reject(error,QStringLiteral("请选择本地 OCR 模型配置 JSON 文件。"));
    const QFileInfo configFile(profilePath());QFile file(configFile.absoluteFilePath());
    if(!file.open(QIODevice::ReadOnly))return reject(error,QStringLiteral("无法读取所选 OCR 模型配置文件。"));
    if(file.size()>65536)return reject(error,QStringLiteral("OCR 模型配置文件过大。"));
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(file.readAll(),&parse);const auto object=document.object();
    const QString invalid=QStringLiteral("OCR 模型配置 JSON 无效，或包含不支持的字段。请查看本地模型配置说明。");
    if(parse.error!=QJsonParseError::NoError || !document.isObject() || !integerInRange(object.value("schemaVersion"),1,1) ||
       !knownKeys(object,{"schemaVersion","id","modelVersion","language","det","rec","cpu"}) ||
       !QRegularExpression("\\A[A-Za-z0-9][A-Za-z0-9._-]{0,63}\\z").match(object.value("id").toString()).hasMatch() ||
       !object.value("modelVersion").isString() || object.value("modelVersion").toString().trimmed().isEmpty() || object.value("modelVersion").toString().size()>128)
        return reject(error,invalid);
    if(!QStringList{"multi","zh","en","zh+en"}.contains(object.value("language").toString()))return reject(error,QStringLiteral("OCR 配置的语言元数据不受支持。"));
    if(object.contains("cpu")) {
        if(!object.value("cpu").isObject())return reject(error,invalid);
        const auto cpu=object.value("cpu").toObject();
        if(!knownKeys(cpu,{"intraOpThreads","interOpThreads"}))return reject(error,invalid);
        if((cpu.contains("intraOpThreads") && !integerInRange(cpu.value("intraOpThreads"),1,8)) ||
           (cpu.contains("interOpThreads") && !integerInRange(cpu.value("interOpThreads"),1,4)))
            return reject(error,QStringLiteral("OCR CPU 线程选项不受支持：intraOpThreads 为 1–8，interOpThreads 为 1–4。"));
    }
    // Validate every option before reading large model files.
    for(const auto &task:QStringList{"det","rec"}) {
        if(!object.value(task).isObject())return reject(error,invalid);
        const auto model=object.value(task).toObject();
        if(model.size()!=4 || !knownKeys(model,{"path","sha256","ocrVersion","modelType"}) ||
           !model.value("path").isString() || model.value("path").toString().isEmpty() ||
           !QRegularExpression("\\A[a-fA-F0-9]{64}\\z").match(model.value("sha256").toString()).hasMatch())return reject(error,invalid);
        if(!QStringList{"PP-OCRv4","PP-OCRv5"}.contains(model.value("ocrVersion").toString()) ||
           !QStringList{"mobile","server"}.contains(model.value("modelType").toString()))
            return reject(error,QStringLiteral("OCR 模型家族或类型不受支持；当前支持 PP-OCRv4/v5 的 mobile/server ONNX。"));
    }
    for(const auto &task:QStringList{"det","rec"}) {
        const auto model=object.value(task).toObject();
        const QFileInfo weight(configFile.dir().absoluteFilePath(model.value("path").toString()));
        if(weight.suffix().compare("onnx",Qt::CaseInsensitive)!=0)return reject(error,QStringLiteral("OCR 模型必须为本地 ONNX 文件。"));
        // The GUI checks syntax only. Even opening a model on a slow/UNC disk
        // belongs to the cancellable helper along with hash/model validation.
        if(!verifyHashes)continue;
        QFile bytes(weight.absoluteFilePath());
        if(!weight.isFile() || !bytes.open(QIODevice::ReadOnly))return reject(error,QStringLiteral("所选 OCR 模型文件不存在或无法读取（%1）。").arg(task));
        if(verifyHashes) {
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if(!hash.addData(&bytes) || QString::fromLatin1(hash.result().toHex())!=model.value("sha256").toString().toLower())
                return reject(error,QStringLiteral("所选 OCR 模型 SHA256 校验失败（%1），请确认模型与配置来自同一版本。").arg(task));
        }
    }
    return true;
}
bool OcrProfiles::save(QString *error,bool verifyHashes) const
{
    if(!validate(error,verifyHashes))return false;
    QSaveFile file(settingsPath());
    const auto bytes=QJsonDocument(QJsonObject{{"schemaVersion",1},{"profile",profile},{"customProfilePath",profile=="custom"?profilePath():customProfilePath.trimmed()}}).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())return reject(error,QStringLiteral("无法保存本地 OCR 模型配置。"));
    return true;
}
}
