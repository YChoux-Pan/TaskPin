#pragma once

#include <QColor>
#include <QDialog>
#include <QMap>
#include <QString>

class HotkeyEdit;

/// Tabbed settings: card / hotkeys / snip & pins / AI / misc (S6).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void saved();

private:
    QWidget *buildCardPage();
    QWidget *buildHotkeyPage();
    QWidget *buildSnipPage();
    QWidget *buildAiPage();
    QWidget *buildMiscPage();

    void apply();

    QMap<QString, HotkeyEdit *> m_hotkeyEdits;
    QColor m_cardBg;
    QColor m_cardFg;
    QColor m_annotationColor;

    class Ui;
    Ui *m_ui = nullptr;
};
