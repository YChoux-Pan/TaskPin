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
    bool useCache = true; // transient request policy; connection tests always bypass it
    static Config load();
    bool save(QString *error) const;
    QUrl requestUrl() const;
    bool authorized() const;
    QString apiKey() const;
};
}
