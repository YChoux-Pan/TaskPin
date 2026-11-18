#pragma once

#include <QColor>
#include <QRectF>
#include <QString>

class QPainter;

// Iconify line icons, embedded as SVG so the app stays offline.
// Set: lucide (ISC licence), plus mdi for the numbered marker.
// Icons are drawn centred into `rect` and tinted with `color`; the 24x24 grid is kept.
namespace IconSet {
void draw(QPainter &p, const QString &id, const QRectF &rect, const QColor &color);
}   // namespace IconSet
