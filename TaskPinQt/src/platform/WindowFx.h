#pragma once

class QWidget;

/// Small helpers around Win32 extended window styles. All native calls stay in platform/.
namespace WindowFx {

/// WS_EX_TRANSPARENT: mouse events fall through to whatever is underneath.
void setClickThrough(QWidget *window, bool on);
bool isClickThrough(QWidget *window);

/// WS_EX_NOACTIVATE: the window never steals focus (used by the desktop card).
void setNoActivate(QWidget *window, bool on);

/// Re-assert HWND_TOPMOST without moving, resizing or activating.
void keepOnTop(QWidget *window);

/// Best-effort foreground grab; needed for dialogs opened from a non-activatable card.
bool forceForeground(QWidget *window);

/// WS_EX_TOPMOST on/off, without moving, resizing or activating.
void setTopMost(QWidget *window, bool on);
bool isTopMost(QWidget *window);

/// Drop the window to the very bottom of the z-order: above the wallpaper, below every normal
/// window (the "desktop layer" the C# build called 桌面层).
void sendToBottom(QWidget *window);

/// Hook for WM_WINDOWPOSCHANGING while a window lives on the desktop layer: any attempt to raise
/// it (a click, show(), or another app calling SetWindowPos) is clamped back to HWND_BOTTOM.
/// Pass the native message Qt hands to QWidget::nativeEvent; safe to call for every message.
void clampToDesktopLayer(void *nativeMessage);

}   // namespace WindowFx
