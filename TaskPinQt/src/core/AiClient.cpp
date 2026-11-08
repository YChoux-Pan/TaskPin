#include "core/AiClient.h"

#include "core/Settings.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

AiClient::AiClient(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void AiClient::summarize(const QString &text)
{
    const auto &s = Settings::instance();
    if (!s.aiEnabled || s.aiApiKey.trimmed().isEmpty()) {
        emit failed(QStringLiteral("AI 未启用或缺少 API Key"));
        return;
    }
    if (busy()) {
        emit failed(QStringLiteral("上一次请求还没结束"));
        return;
    }

    QString base = s.aiBaseUrl.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    if (!base.endsWith(QLatin1String("/chat/completions"))) base += QStringLiteral("/chat/completions");

    QJsonObject system;
    system[QStringLiteral("role")] = QStringLiteral("system");
    system[QStringLiteral("content")] =
        QStringLiteral("把用户提供的截图文字压缩成一句不超过 20 个字的中文任务标题，只输出标题本身，不要引号和解释。");
    QJsonObject user;
    user[QStringLiteral("role")] = QStringLiteral("user");
    user[QStringLiteral("content")] = text.left(4000);

    QJsonObject payload;
    payload[QStringLiteral("model")] = s.aiModel;
    payload[QStringLiteral("messages")] = QJsonArray{system, user};
    payload[QStringLiteral("temperature")] = 0.2;
    payload[QStringLiteral("max_tokens")] = 64;

    QNetworkRequest request{QUrl(base)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Authorization", QByteArray("Bearer ") + s.aiApiKey.trimmed().toUtf8());

    m_reply = m_net->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    qInfo("ai request -> %s", qUtf8Printable(base));

    connect(m_reply, &QNetworkReply::finished, this, [this] {
        if (!m_reply) return;
        const QByteArray body = m_reply->readAll();
        const QString error = m_reply->error() == QNetworkReply::NoError
                                  ? QString()
                                  : QStringLiteral("%1 %2").arg(m_reply->errorString(),
                                                                QString::fromUtf8(body).left(160));
        m_reply->deleteLater();
        m_reply = nullptr;
        if (m_timer) {
            m_timer->stop();
            m_timer->deleteLater();
            m_timer = nullptr;
        }
        if (!error.isEmpty()) {
            emit failed(error);
            return;
        }
        const QJsonObject o = QJsonDocument::fromJson(body).object();
        const QJsonArray choices = o.value(QStringLiteral("choices")).toArray();
        if (choices.isEmpty()) {
            emit failed(QStringLiteral("响应里没有 choices"));
            return;
        }
        QString content =
            choices.first().toObject().value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
        content = content.trimmed();
        content.remove(QLatin1Char('\n'));
        content.remove(QLatin1Char('"'));
        content.remove(QChar(0x201C));
        content.remove(QChar(0x201D));
        if (content.isEmpty()) {
            emit failed(QStringLiteral("模型返回空标题"));
            return;
        }
        qInfo("ai title: %s", qUtf8Printable(content));
        emit finished(content.left(40));
    });

    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    connect(m_timer, &QTimer::timeout, this, [this] { finishError(QStringLiteral("AI 请求超时")); });
    m_timer->start(25000);
}

void AiClient::finishError(const QString &reason)
{
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    if (m_timer) {
        m_timer->stop();
        m_timer->deleteLater();
        m_timer = nullptr;
    }
    emit failed(reason);
}
