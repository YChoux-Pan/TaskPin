#pragma once
#include "translation/TranslationTypes.h"
#include <QObject>
#include <QPointer>

class QProcess;
class QTimer;
namespace Translation {
class Ocr : public QObject {
    Q_OBJECT
public:
    explicit Ocr(QObject *parent = nullptr);
    ~Ocr() override;
    void recognize(const QString &taskId, const QImage &image);
    void cancel();
    bool busy() const { return !m_process.isNull(); }
    static bool decode(const QByteArray &json, const QSize &imageSize, OcrResult *result, QString *error);
signals:
    void finished(const QString &taskId, const Translation::OcrResult &result);
    void failed(const QString &taskId, const QString &message);
private:
    void fail(const QString &message);
    QPointer<QProcess> m_process;
    QTimer *m_timer;
    QString m_taskId;
    QSize m_size;
    QByteArray m_output;
    quint64 m_generation = 0;
};
}
