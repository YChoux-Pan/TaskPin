#pragma once

#include <QRect>
#include <QString>

/// Finds the top-level window under the cursor so the snip editor can snap to it.
namespace WindowPicker {

struct Hit {
    bool valid = false;
    QRect physicalRect;   // real on-screen bounds in physical pixels
    QString title;
    QString className;
};

/// Top-most window under one physical screen point, ignoring TaskPin itself and the shell desktop.
Hit atPhysicalPoint(int x, int y);

/// Physical rect of the monitor whose GDI device name matches (QScreen::name() == "\\.\DISPLAY1").
/// Returns a null rect when the monitor is unknown (e.g. not on Windows).
QRect monitorRectForDevice(const QString &deviceName);

}   // namespace WindowPicker
