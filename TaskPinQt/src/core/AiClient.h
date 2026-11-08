#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/// Optional OpenAI-compatible summariser: turns a long OCR dump into one short task title.
class AiClient : public QObject {
    Q_OBJECT
public:
    explicit AiClient(QObject *parent = nullptr);

    bool busy() const { return m_reply != nullptr; }
    void summarize(const QString &text);

signals:
    void finished(const QString &title);
    void failed(const QString &reason);

private:
    void finishError(const QString &reason);

    QNetworkAccessManager *m_net = nullptr;
    QNetworkReply *m_reply = nullptr;
    QTimer *m_timer = nullptr;
};
