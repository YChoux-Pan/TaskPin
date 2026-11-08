#include "platform/WindowPicker.h"

#include <QDebug>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

namespace WindowPicker {

#ifdef Q_OS_WIN

namespace {

struct Context {
    POINT point{};
    Hit hit;
};

bool windowBounds(HWND hwnd, RECT &out)
{
    // DWMWA_EXTENDED_FRAME_BOUNDS excludes the invisible resize border Windows 10+ adds
    constexpr DWORD kExtendedFrameBounds = 9;
    HRESULT hr = DwmGetWindowAttribute(hwnd, kExtendedFrameBounds, &out, sizeof(out));
    if (SUCCEEDED(hr) && out.right > out.left && out.bottom > out.top) return true;
    return GetWindowRect(hwnd, &out) != FALSE && out.right > out.left && out.bottom > out.top;
}

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp)
{
    auto *ctx = reinterpret_cast<Context *>(lp);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId()) return TRUE;   // never snap to our own overlay/card/pins

    const LONG ex = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (ex & WS_EX_TRANSPARENT) return TRUE;         // click-through layers are not real targets

    wchar_t cls[128]{};
    GetClassNameW(hwnd, cls, 128);
    if (wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0 ||
        wcscmp(cls, L"Shell_TrayWnd") == 0 || wcscmp(cls, L"Shell_SecondaryTrayWnd") == 0)
        return TRUE;

    BOOL cloaked = FALSE;
    constexpr DWORD kCloaked = 14;   // DWMWA_CLOAKED
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, kCloaked, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;

    RECT r{};
    if (!windowBounds(hwnd, r)) return TRUE;
    if (!PtInRect(&r, ctx->point)) return TRUE;

    wchar_t title[256]{};
    GetWindowTextW(hwnd, title, 256);

    ctx->hit.valid = true;
    ctx->hit.physicalRect = QRect(int(r.left), int(r.top), int(r.right - r.left), int(r.bottom - r.top));
    ctx->hit.title = QString::fromWCharArray(title);
    ctx->hit.className = QString::fromWCharArray(cls);
    return FALSE;   // top-most hit wins; stop enumerating
}

}   // namespace

Hit atPhysicalPoint(int x, int y)
{
    Context ctx;
    ctx.point.x = x;
    ctx.point.y = y;
    // EnumWindows walks top-level windows from the top of the Z order down, which is exactly
    // what "the window under the cursor" means once our own overlays are skipped.
    EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hit;
}

#else

Hit atPhysicalPoint(int x, int y)
{
    Q_UNUSED(x) Q_UNUSED(y)
    return {};
}

#endif

}   // namespace WindowPicker
