#pragma once
#include <QString>
#include <QUrl>

namespace Translation {
struct Config {
    QString mode = "local"; // local OCR | mock (not a real translation) | cloud
    QString endpoint = "https://api.deepseek.com/v1";
    QString model = "deepseek-chat";
    QString consentEndpoint;
    int timeoutMs = 60000;
    static Config load();
    bool save(QString *error) const;
    QUrl requestUrl() const;
    bool authorized() const;
    QString credentialTarget() const;
};
}
