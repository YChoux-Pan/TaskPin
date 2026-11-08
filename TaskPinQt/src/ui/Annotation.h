#pragma once

#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector>

class QPainter;

/// One annotation. Coordinates are always **logical** (window) coordinates; ::paint scales the
/// frozen desktop image by the device pixel ratio when it needs real pixels (mosaic / blur).
struct Annotation {
    enum Kind { Rect, Ellipse, Arrow, Line, Pen, Highlighter, Text, Number, Pixelate };

    Kind kind = Rect;
    QColor color{0xE5, 0x3B, 0x3B};
    int width = 3;
    QRect rect;
    QVector<QPoint> points;
    QString text;
    QPoint pos;
    int fontSize = 18;
    int number = 1;
    bool blur = false;   // Pixelate: false = mosaic blocks, true = soft blur
    int block = 14;      // Pixelate block size in logical pixels

    /// `base` is the frozen desktop image in device pixels (needed by Pixelate); may be null.
    void paint(QPainter &p, const QImage *base = nullptr, qreal dpr = 1.0) const;

    QRect bounds() const;
    /// True when a click at `p` (logical) should select this annotation.
    bool hitTest(const QPoint &p) const;

    /// Moves the annotation by a delta in logical pixels.
    void translate(const QPoint &delta);
};
