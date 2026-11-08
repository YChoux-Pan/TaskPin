#pragma once

#include <QPixmap>
#include <QRect>

/// One frozen snapshot of the whole virtual desktop.
struct ScreenCapture {
    QPixmap pixmap;      // device pixels, devicePixelRatio set
    QRect logicalRect;   // virtual desktop rect in logical coordinates
    qreal dpr = 1.0;

    bool isValid() const { return !pixmap.isNull() && logicalRect.isValid(); }
};

/// Grabs every screen and composites them into one pixmap in logical desktop coordinates.
ScreenCapture captureVirtualDesktop();
