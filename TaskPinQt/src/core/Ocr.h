#pragma once

#include <QImage>
#include <QObject>
#include <QString>

class QProcess;
class QTimer;

/// Offline OCR through the Windows.Media.Ocr runtime (zh-Hans works out of the box).
/// The WinRT call runs in a short-lived PowerShell helper; results are read back as JSON.
class Ocr : public QObject {
    Q_OBJECT
public:
    explicit Ocr(QObject *parent = nullptr);
    ~Ocr() override;

    bool busy() const { return m_process != nullptr; }
    /// True when the WinRT OCR stack is likely usable on this machine.
    static bool available();

    void recognize(const QImage &image);

signals:
    void finished(const QString &text);
    void failed(const QString &reason);

private:
    bool writeHelper();
    void finishWithError(const QString &reason);
    void cleanup();

    QProcess *m_process = nullptr;
    QTimer *m_timeout = nullptr;
    QString m_imagePath;
    QString m_resultPath;
};
