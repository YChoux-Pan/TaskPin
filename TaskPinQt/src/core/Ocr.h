#pragma once
#include <QImage>
#include <QObject>
#include <QString>
namespace Translation {class Ocr;}
// Both task recognition and screenshot translation share the local PaddleOCR adapter.
class Ocr : public QObject {
    Q_OBJECT
public:
    explicit Ocr(QObject *parent=nullptr);
    ~Ocr() override;
    bool busy() const;
    static bool available();
    void recognize(const QImage &image);
signals:
    void finished(const QString &text);
    void failed(const QString &reason);
private:
    Translation::Ocr *m_engine;
    QString m_taskId;
};
