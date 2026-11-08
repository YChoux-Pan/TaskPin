#include "platform/WindowFx.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace WindowFx {

#ifdef Q_OS_WIN

namespace {
HWND handleOf(QWidget *w)
{
    return w ? reinterpret_cast<HWND>(w->winId()) : nullptr;
}
}   // namespace

void setClickThrough(QWidget *window, bool on)
{
    HWND hwnd = handleOf(window);
    if (!hwnd) return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR next = on ? (ex | WS_EX_TRANSPARENT | WS_EX_LAYERED) : (ex & ~LONG_PTR(WS_EX_TRANSPARENT));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
}

bool isClickThrough(QWidget *window)
{
    HWND hwnd = handleOf(window);
    if (!hwnd) return false;
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0;
}

void setNoActivate(QWidget *window, bool on)
{
    HWND hwnd = handleOf(window);
    if (!hwnd) return;
    const LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    const LONG_PTR next = on ? (ex | WS_EX_NOACTIVATE) : (ex & ~LONG_PTR(WS_EX_NOACTIVATE));
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, next);
}

void keepOnTop(QWidget *window)
{
    HWND hwnd = handleOf(window);
    if (!hwnd) return;
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

bool forceForeground(QWidget *window)
{
    HWND hwnd = handleOf(window);
    if (!hwnd) return false;

    const HWND fg = GetForegroundWindow();
    const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const DWORD myThread = GetCurrentThreadId();
    bool attached = false;
    if (fgThread && fgThread != myThread) attached = AttachThreadInput(myThread, fgThread, TRUE);

    AllowSetForegroundWindow(ASFW_ANY);
    BringWindowToTop(hwnd);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    if (attached) AttachThreadInput(myThread, fgThread, FALSE);
    return GetForegroundWindow() == hwnd;
}

#else

void setClickThrough(QWidget *, bool) {}
bool isClickThrough(QWidget *) { return false; }
void setNoActivate(QWidget *, bool) {}
void keepOnTop(QWidget *) {}
bool forceForeground(QWidget *) { return false; }

#endif

}   // namespace WindowFx
