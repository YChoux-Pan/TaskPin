#pragma once

#include <QString>
#include <Qt>

/// Parses / formats Windows hotkey chords such as "Ctrl+Shift+F1" and records Qt key events.
namespace HotkeyText {

struct Chord {
    unsigned mods = 0;   // MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN
    unsigned vk = 0;     // virtual-key code
    QString text;
    bool valid() const { return vk != 0; }
};

/// Invalid when the last token is not a known key.
Chord parse(const QString &text);
QString format(unsigned mods, unsigned vk);
QString keyName(unsigned vk);

/// Maps a Qt key press to a chord; modifier-only presses produce an invalid chord.
Chord fromQtKey(int qtKey, Qt::KeyboardModifiers modifiers);

}   // namespace HotkeyText
