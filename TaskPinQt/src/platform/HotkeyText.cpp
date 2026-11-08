#include "platform/HotkeyText.h"

#include <QCoreApplication>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {

struct NamedKey {
    const char *name;
    unsigned vk;
};

// Sorted by nothing in particular; lookup walks the table (it is tiny).
const NamedKey kNamed[] = {
    {"Backspace", 0x08}, {"Tab", 0x09},      {"Enter", 0x0D},   {"Esc", 0x1B},
    {"Space", 0x20},     {"PageUp", 0x21},   {"PageDown", 0x22}, {"End", 0x23},
    {"Home", 0x24},      {"Left", 0x25},     {"Up", 0x26},      {"Right", 0x27},
    {"Down", 0x28},      {"Insert", 0x2D},   {"Delete", 0x2E},
    {"`", 0xC0},         {"-", 0xBD},        {"=", 0xBB},       {"[", 0xDB},
    {"]", 0xDD},         {"\\", 0xDC},       {";", 0xBA},       {"'", 0xDE},
    {",", 0xBC},         {".", 0xBE},        {"/", 0xBF},
};

unsigned vkForName(const QString &token)
{
    const QString t = token.trimmed();
    if (t.isEmpty()) return 0;

    if (t.size() >= 2 && (t.at(0) == QLatin1Char('F') || t.at(0) == QLatin1Char('f'))) {
        bool ok = false;
        const int n = t.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24) return 0x70u + unsigned(n - 1);
    }
    if (t.size() == 1) {
        const QChar c = t.at(0).toUpper();
        if ((c >= QLatin1Char('A') && c <= QLatin1Char('Z')) || (c >= QLatin1Char('0') && c <= QLatin1Char('9')))
            return c.unicode();
        if (c == QLatin1Char('`')) return 0xC0;
    }
    for (const auto &nk : kNamed)
        if (t.compare(QString::fromLatin1(nk.name), Qt::CaseInsensitive) == 0) return nk.vk;
    return 0;
}

}   // namespace

namespace HotkeyText {

QString keyName(unsigned vk)
{
    if (vk >= 0x70 && vk <= 0x87) return QStringLiteral("F%1").arg(int(vk) - 0x70 + 1);
    if (vk >= 'A' && vk <= 'Z') return QString(QChar(int(vk)));
    if (vk >= '0' && vk <= '9') return QString(QChar(int(vk)));
    for (const auto &nk : kNamed)
        if (nk.vk == vk) return QString::fromLatin1(nk.name);
    return QStringLiteral("0x%1").arg(vk, 2, 16, QChar('0')).toUpper();
}

QString format(unsigned mods, unsigned vk)
{
    QString out;
#ifdef Q_OS_WIN
    if (mods & MOD_CONTROL) out += QStringLiteral("Ctrl+");
    if (mods & MOD_ALT) out += QStringLiteral("Alt+");
    if (mods & MOD_SHIFT) out += QStringLiteral("Shift+");
    if (mods & MOD_WIN) out += QStringLiteral("Win+");
#else
    Q_UNUSED(mods)
#endif
    return vk ? out + keyName(vk) : QString();
}

Chord parse(const QString &text)
{
    Chord c;
    const QString raw = text.trimmed();
    if (raw.isEmpty()) return c;

    const QStringList parts = raw.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return c;

    for (int i = 0; i < parts.size() - 1; ++i) {
        const QString m = parts.at(i).trimmed().toLower();
        if (m == QLatin1String("ctrl") || m == QLatin1String("control")) c.mods |= 0x0002;
        else if (m == QLatin1String("alt")) c.mods |= 0x0001;
        else if (m == QLatin1String("shift")) c.mods |= 0x0004;
        else if (m == QLatin1String("win") || m == QLatin1String("windows")) c.mods |= 0x0008;
        else return Chord();   // unknown modifier: refuse rather than guess
    }

    c.vk = vkForName(parts.last());
    if (c.vk) c.text = format(c.mods, c.vk);
    return c;
}

Chord fromQtKey(int qtKey, Qt::KeyboardModifiers modifiers)
{
    Chord c;
#ifdef Q_OS_WIN
    if (modifiers & Qt::ControlModifier) c.mods |= MOD_CONTROL;
    if (modifiers & Qt::AltModifier) c.mods |= MOD_ALT;
    if (modifiers & Qt::ShiftModifier) c.mods |= MOD_SHIFT;
    if (modifiers & Qt::MetaModifier) c.mods |= MOD_WIN;
#endif

    unsigned vk = 0;
    if (qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z) vk = unsigned(qtKey);
    else if (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9) vk = unsigned(qtKey);
    else if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24) vk = 0x70u + unsigned(qtKey - Qt::Key_F1);
    else {
        switch (qtKey) {
        case Qt::Key_Space: vk = 0x20; break;
        case Qt::Key_Return:
        case Qt::Key_Enter: vk = 0x0D; break;
        case Qt::Key_Escape: vk = 0x1B; break;
        case Qt::Key_Tab: vk = 0x09; break;
        case Qt::Key_Backspace: vk = 0x08; break;
        case Qt::Key_Insert: vk = 0x2D; break;
        case Qt::Key_Delete: vk = 0x2E; break;
        case Qt::Key_Home: vk = 0x24; break;
        case Qt::Key_End: vk = 0x23; break;
        case Qt::Key_PageUp: vk = 0x21; break;
        case Qt::Key_PageDown: vk = 0x22; break;
        case Qt::Key_Left: vk = 0x25; break;
        case Qt::Key_Up: vk = 0x26; break;
        case Qt::Key_Right: vk = 0x27; break;
        case Qt::Key_Down: vk = 0x28; break;
        case Qt::Key_Semicolon: vk = 0xBA; break;
        case Qt::Key_Equal: vk = 0xBB; break;
        case Qt::Key_Comma: vk = 0xBC; break;
        case Qt::Key_Minus: vk = 0xBD; break;
        case Qt::Key_Period: vk = 0xBE; break;
        case Qt::Key_Slash: vk = 0xBF; break;
        case Qt::Key_QuoteLeft: vk = 0xC0; break;
        case Qt::Key_BracketLeft: vk = 0xDB; break;
        case Qt::Key_Backslash: vk = 0xDC; break;
        case Qt::Key_BracketRight: vk = 0xDD; break;
        case Qt::Key_Apostrophe: vk = 0xDE; break;
        default: return Chord();   // modifier-only or unsupported key
        }
    }
    c.vk = vk;
    if (vk) c.text = format(c.mods, vk);
    return c;
}

}   // namespace HotkeyText
