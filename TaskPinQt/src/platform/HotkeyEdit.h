#pragma once

#include <QLineEdit>

/// A read-only line edit that records one chord when you press it (used by the settings dialog).
class HotkeyEdit : public QLineEdit {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget *parent = nullptr);

    void setChord(const QString &chord);
    QString chord() const { return m_chord; }

signals:
    void chordChanged(const QString &chord);

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;

private:
    void refresh();

    QString m_chord;
};
