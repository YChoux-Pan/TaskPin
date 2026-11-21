#pragma once

#include <QColor>
#include <QDialog>
#include <QMap>
#include <QString>
#include "ui/CardAppearance.h"

class HotkeyEdit;
class QTabWidget;
class TranslationSettingsPage;
class QLabel;

/// Tabbed settings: card / hotkeys / snip & pins / AI / misc (S6).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;
    void showTranslationPage();

signals:
    void saved();
    void cardPreviewChanged(const CardAppearance &appearance);

private:
    QWidget *buildCardPage();
    QWidget *buildHotkeyPage();
    QWidget *buildSnipPage();
    QWidget *buildAiPage();
    QWidget *buildMiscPage();

    bool apply();
    void emitPreview();
    void chooseColor(class QPushButton *button, QColor &value, bool preview);

    QMap<QString, HotkeyEdit *> m_hotkeyEdits;
    QColor m_cardBg;
    QColor m_cardFg;
    QColor m_annotationColor;
    QColor m_pinBorderColor;
    bool m_resetPosition = false;
    QTabWidget *m_tabs = nullptr;
    TranslationSettingsPage *m_translation = nullptr;
    QLabel *m_saveError = nullptr;

    class Ui;
    Ui *m_ui = nullptr;
};
