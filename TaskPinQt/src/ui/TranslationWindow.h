#pragma once
#include "translation/TranslationTypes.h"
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QCheckBox;
class QCloseEvent;
class QStackedWidget;
class QScrollArea;
class QComboBox;
class TranslationCanvas;
namespace Translation { class Ocr; class Client; }
class TranslationWindow : public QWidget {
    Q_OBJECT
public:
    explicit TranslationWindow(QWidget *parent = nullptr);
    ~TranslationWindow() override;
    void setCapture(const QImage &image, const QRect &region);
    void cancel();
    bool busy() const;
    QString state() const { return m_state; }
    QString taskId() const { return m_taskId; }
    void presentOcr(const QString &taskId, const Translation::OcrResult &result);
signals:
    void recaptureRequested();
    void settingsRequested();
protected:
    void closeEvent(QCloseEvent *) override;
private:
    void translate();
    void setStatus(const QString &state, const QString &label);
    void settings();
    void presentResults(const QList<Translation::Result> &results);
    void saveTranslatedImage();
    QLabel *m_status, *m_warning;
    QPlainTextEdit *m_source, *m_target;
    QPushButton *m_retry, *m_cancel;
    Translation::Ocr *m_ocr;
    Translation::Client *m_client;
    QImage m_image;
    TranslationCanvas *m_canvas;
    QStackedWidget *m_stack;
    QScrollArea *m_scroll;
    QComboBox *m_view;
    QList<Translation::Block> m_blocks;
    QString m_taskId, m_state = "IDLE";
};
