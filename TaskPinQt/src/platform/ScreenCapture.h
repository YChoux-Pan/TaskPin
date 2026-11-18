#pragma once

#include <QPixmap>
#include <QRect>
#include <QString>

/// One frozen snapshot of a single screen.
struct ScreenCapture {
    QPixmap pixmap;      // device pixels of that screen; origin == logicalRect.topLeft()
    QRect logicalRect;   // the captured screen's geometry in global logical coordinates
    qreal dpr = 1.0;
    QString screenName;  // QScreen::name(), used to look up the monitor's physical rect

    bool isValid() const { return !pixmap.isNull() && logicalRect.isValid(); }
};

/// Grabs the screen under the cursor (falls back to the primary screen).
///
/// Capturing one screen at a time keeps the overlay's DPI equal to the capture DPI. Compositing the
/// whole virtual desktop at the primary DPI (the old behaviour) breaks as soon as monitors have
/// different scale factors: Qt then gives the overlay window the other screen's DPR and every
/// selection comes out scaled (e.g. 0.8x on a 100% + 125% setup).
ScreenCapture captureScreenForCursor();
