#include "platform/HotkeyEdit.h"

#include "platform/HotkeyText.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>

HotkeyEdit::HotkeyEdit(QWidget *parent) : QLineEdit(parent)
{
    setReadOnly(true);
    setAlignment(Qt::AlignCenter);
    setCursor(Qt::PointingHandCursor);
    setPlaceholderText(QStringLiteral("未设置"));
    setToolTip(QStringLiteral("点击后直接按下组合键；Backspace 清除"));
    refresh();
}

void HotkeyEdit::setChord(const QString &chord)
{
    m_chord = chord.trimmed();
    refresh();
}

void HotkeyEdit::refresh()
{
    if (hasFocus()) setText(QStringLiteral("按下组合键…"));
    else setText(m_chord.isEmpty() ? QStringLiteral("未设置") : m_chord);
}

void HotkeyEdit::mousePressEvent(QMouseEvent *e)
{
    setFocus(Qt::MouseFocusReason);
    QLineEdit::mousePressEvent(e);
}

void HotkeyEdit::focusInEvent(QFocusEvent *e)
{
    QLineEdit::focusInEvent(e);
    refresh();
    selectAll();
}

void HotkeyEdit::focusOutEvent(QFocusEvent *e)
{
    QLineEdit::focusOutEvent(e);
    refresh();
}

void HotkeyEdit::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        clearFocus();
        return;
    }
    if (e->key() == Qt::Key_Backspace || e->key() == Qt::Key_Delete) {
        m_chord.clear();
        emit chordChanged(m_chord);
        refresh();
        return;
    }
    const HotkeyText::Chord chord = HotkeyText::fromQtKey(e->key(), e->modifiers());
    if (!chord.valid()) return;   // modifier-only press: keep waiting for the real key
    m_chord = chord.text;
    emit chordChanged(m_chord);
    refresh();
}
