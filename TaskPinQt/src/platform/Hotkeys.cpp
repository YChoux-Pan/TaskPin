#include "platform/Hotkeys.h"

#include "platform/HotkeyText.h"

#include <QDebug>
#include <QMetaObject>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
const wchar_t *kClassName = L"TaskPinHotkeyWindow";

LRESULT CALLBACK hotkeyProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    Q_UNUSED(lparam)
    if (msg == WM_HOTKEY) {
        qInfo("WndProc WM_HOTKEY %d", int(wparam));
        auto *self = reinterpret_cast<Hotkeys *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (self) QMetaObject::invokeMethod(self, "relay", Qt::QueuedConnection, Q_ARG(int, int(wparam)));
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
#endif
}

Hotkeys::Hotkeys(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = hotkeyProc;
    wc.hInstance = inst;
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);   // already registered is fine

    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, inst, nullptr);
    if (hwnd) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        m_hwnd = hwnd;
    }
#endif
}

Hotkeys::~Hotkeys()
{
    unbindAll();
#ifdef Q_OS_WIN
    if (m_hwnd) DestroyWindow(static_cast<HWND>(m_hwnd));
#endif
}

bool Hotkeys::bind(int id, const QString &chord)
{
#ifdef Q_OS_WIN
    if (!m_hwnd) return false;
    const HotkeyText::Chord parsed = HotkeyText::parse(chord);
    if (!parsed.valid()) {
        qWarning() << "hotkey parse failed:" << chord;
        return false;
    }
    if (!RegisterHotKey(static_cast<HWND>(m_hwnd), id, parsed.mods | MOD_NOREPEAT, parsed.vk)) {
        qWarning() << "hotkey in use:" << parsed.text;
        return false;
    }
    m_bindings.append({id, parsed.mods, parsed.vk, parsed.text});
    return true;
#else
    Q_UNUSED(id) Q_UNUSED(chord)
    return false;
#endif
}

void Hotkeys::unbindAll()
{
#ifdef Q_OS_WIN
    for (const auto &b : qAsConst(m_bindings)) UnregisterHotKey(static_cast<HWND>(m_hwnd), b.id);
#endif
    m_bindings.clear();
}

QString Hotkeys::label(int id) const
{
    for (const auto &b : m_bindings)
        if (b.id == id) return b.chord;
    return {};
}

void Hotkeys::relay(int id)
{
    qInfo("hotkey fired: %d", id);
    emit triggered(id);
}
