#include "translation/TranslationConfig.h"
#include "core/Settings.h"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace Translation {
Config Config::load()
{
    Config config;
    config.endpoint = Settings::instance().aiBaseUrl;
    config.model = Settings::instance().aiModel;
    QFile file(Settings::dataDir()+"/translation.json");
    if (!file.open(QIODevice::ReadOnly)) return config;
    const auto object = QJsonDocument::fromJson(file.readAll()).object();
    config.mode = object.value("mode").toString("local");
    if (config.mode != "mock" && config.mode != "cloud") config.mode = "local";
    config.consentEndpoint = object.value("consentEndpoint").toString();
    config.timeoutMs = qBound(1000,object.value("timeoutMs").toInt(60000),120000);
    return config;
}
bool Config::save(QString *error) const
{
    QSaveFile file(Settings::dataDir()+"/translation.json");
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(QJsonObject{
            {"version",2},{"connectionSource","shared-ai"},{"mode",mode},
            {"consentEndpoint",consentEndpoint},{"timeoutMs",timeoutMs}}).toJson()) < 0 || !file.commit()) {
        if (error) *error = QStringLiteral("无法保存翻译设置。"); return false;
    }
    return true;
}
QString Config::apiKey() const { return Settings::instance().aiApiKey.trimmed(); }
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
}
