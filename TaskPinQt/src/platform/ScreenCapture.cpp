#include "platform/ScreenCapture.h"

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

ScreenCapture captureScreenForCursor()
{
    ScreenCapture out;

    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    if (!screen) return out;

    QPixmap shot = screen->grabWindow(0);   // native pixels of that screen
    if (shot.isNull()) return out;
    shot.setDevicePixelRatio(1.0);

    out.pixmap = shot;
    out.logicalRect = screen->geometry();
    out.dpr = screen->devicePixelRatio();
    out.screenName = screen->name();
    return out;
}
