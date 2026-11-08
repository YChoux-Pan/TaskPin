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

}   // namespace WindowFx
