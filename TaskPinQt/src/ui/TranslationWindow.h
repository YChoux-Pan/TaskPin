#pragma once
#include "ui/PinWindow.h"
#include "translation/TranslationTypes.h"
#include <QPointer>
class QDialog;class QPlainTextEdit;class QCloseEvent;class QHideEvent;
class TranslationCanvas;
namespace Translation { class Ocr; class Client; }
// Kept as the task/presentation class name; the presentation is an ordinary desktop pin.
class TranslationWindow : public PinWindow {
    Q_OBJECT
public:
    explicit TranslationWindow(QWidget *parent=nullptr);
    ~TranslationWindow() override;
    void setCapture(const QImage &image,const QRect &region);
    void cancel();
    bool busy() const;
    QString state() const {return m_state;}
    QString taskId() const {return m_taskId;}
    QString sourceText() const {return m_sourceText;}
    QString targetText() const {return m_targetText;}
    QImage translatedImage() const {return m_translatedImage;}
    void presentOcr(const QString &taskId,const Translation::OcrResult &result);
    void showComparison();
signals:
    void recaptureRequested();
    void settingsRequested();
    void processingRequested(TranslationWindow *pin);
protected:
    void paintEvent(QPaintEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void hideEvent(QHideEvent *) override;
    void extendMenu(QMenu *) override;
    bool allowsClipboardReplacement() const override {return false;}
private:
    void translate();
    void setStatus(const QString &state,const QString &label);
    void presentResults(const QList<Translation::Result> &results);
    void refreshFrame();
    void toggleOriginal();
    Translation::Ocr *m_ocr;
    Translation::Client *m_client;
    TranslationCanvas *m_renderer;
    QImage m_originalImage,m_translatedImage;
    QSize m_sourceLogicalSize;
    QList<Translation::Block> m_blocks;
    QString m_taskId,m_state="IDLE",m_message,m_warning,m_sourceText,m_targetText;
    bool m_original=false,m_statusVisible=false,m_closing=false;
    quint64 m_statusRevision=0;
    QPointer<QDialog> m_comparison;
    QPlainTextEdit *m_source=nullptr,*m_target=nullptr;
};
