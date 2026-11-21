#pragma once
#include "translation/TranslationConfig.h"
#include "translation/TranslationTypes.h"
#include <QElapsedTimer>
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QQueue>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
namespace Translation {
class Client : public QObject {
    Q_OBJECT
public:
    explicit Client(QObject *parent = nullptr);
    ~Client() override;
    void start(const QString &taskId, const QList<Block> &blocks, const Config &config, const QString &key);
    void cancel();
    bool busy() const { return m_busy; }
    // Only a controlled localhost test server may use cleartext HTTP.
    void allowLoopbackForTests(bool value) { m_testLoopback = value; }
signals:
    void updated(const QString &taskId, const QList<Translation::Result> &results, int complete, int total);
    void finished(const QString &taskId, const QList<Translation::Result> &results);
    void failed(const QString &taskId, const QString &message);
private:
    void next();
    void fail(const QString &message);
    void detachReply();
    void acceptResponse(const QByteArray &body);
    bool endpointAllowed() const;
    QString cacheKey(const Block &block) const;
    QNetworkAccessManager *m_net;
    QPointer<QNetworkReply> m_reply;
    QTimer *m_timer;
    bool m_busy = false, m_testLoopback = false;
    quint64 m_generation = 0;
    QString m_taskId, m_key;
    Config m_config;
    QList<Block> m_blocks, m_current;
    QList<Result> m_results;
    QQueue<QList<Block>> m_queue;
    QHash<QString,int> m_attempts;
    QHash<QString,QString> m_cache;
    QQueue<QString> m_cacheOrder;
    QElapsedTimer m_budget;
};
}
