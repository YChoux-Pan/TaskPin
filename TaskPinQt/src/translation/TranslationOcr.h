#pragma once
#include "translation/TranslationTypes.h"
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>

namespace Translation {
class OcrWorker;
class Ocr : public QObject {
    Q_OBJECT
public:
    explicit Ocr(QObject *parent = nullptr);
    ~Ocr() override;
    void recognize(const QString &taskId, const QImage &image);
    void cancel();
    bool busy() const { return !m_requestId.isEmpty(); }
    static bool available();
    static void prewarm();
    static QString runtimeDirectory();
    static bool decode(const QByteArray &json, const QSize &imageSize, OcrResult *result, QString *error);
signals:
    void finished(const QString &taskId, const Translation::OcrResult &result);
    void failed(const QString &taskId, const QString &message);
private:
    friend class OcrWorker;
    void accept(const QString &requestId,const QByteArray &json,const QSize &size,const QJsonObject &bridgeTimings);
    void reject(const QString &requestId,const QString &message);
    QString m_taskId;
    QString m_requestId;
    QElapsedTimer m_started;
};
}
