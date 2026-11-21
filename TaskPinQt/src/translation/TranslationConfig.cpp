#include "translation/TranslationConfig.h"
#include "core/Settings.h"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Translation {
Config Config::load()
{
    Config config;
    QFile file(Settings::dataDir()+"/translation.json");
    if (!file.open(QIODevice::ReadOnly)) return config;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    config.mode = object.value("mode").toString("local");
    if (config.mode != "mock" && config.mode != "cloud") config.mode = "local";
    config.endpoint = object.value("endpoint").toString(config.endpoint);
    config.model = object.value("model").toString(config.model);
    config.consentEndpoint = object.value("consentEndpoint").toString();
    config.timeoutMs = qBound(1000,object.value("timeoutMs").toInt(60000),120000);
    return config;
}
bool Config::save(QString *error) const
{
    QSaveFile file(Settings::dataDir()+"/translation.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject{
            {"version",1},{"mode",mode},{"endpoint",endpoint},{"model",model},
            {"consentEndpoint",consentEndpoint},{"timeoutMs",timeoutMs}}).toJson()) < 0 || !file.commit()) {
        if (error) *error = QStringLiteral("无法保存翻译设置。"); return false;
    }
    return true;
}
QUrl Config::requestUrl() const
{
    QString base = endpoint.trimmed(); while (base.endsWith('/')) base.chop(1);
    if (!base.endsWith("/chat/completions")) base += "/chat/completions";
    return QUrl(base);
}
bool Config::authorized() const
{
    const auto url = requestUrl();
    return url.isValid() && url.scheme() == "https" && !url.host().isEmpty() && url.userInfo().isEmpty() &&
        !url.hasQuery() && !url.hasFragment() && consentEndpoint == url.toString(QUrl::FullyEncoded);
}
QString Config::credentialTarget() const
{
    const auto digest = QCryptographicHash::hash((Settings::dataDir()+"|"+requestUrl().toString()).toUtf8(),QCryptographicHash::Sha256).toHex();
    return "TaskPin/ScreenTranslation/"+QString::fromLatin1(digest);
}
}
