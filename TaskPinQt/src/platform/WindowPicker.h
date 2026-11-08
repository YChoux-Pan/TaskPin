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

}   // namespace WindowPicker
