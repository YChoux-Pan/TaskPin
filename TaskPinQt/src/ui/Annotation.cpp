#include "ui/Annotation.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QPainterPathStroker>
#include <cmath>

namespace {
struct NumberShape {
    QPainterPath glyph;
    QPainterPath badge;
};

NumberShape numberShape(const Annotation &a)
{
    const qreal radius = 11 + a.width;
    QFont font;
    font.setPixelSize(int(radius) + 4);
    font.setBold(true);
    NumberShape shape;
    shape.glyph.addText(QPointF(), font, a.numberLabel());
    const QRectF ink = shape.glyph.boundingRect();
    shape.glyph.translate(QPointF(a.pos) - ink.center());

    const qreal height = qMax(2 * radius, ink.height() + 6);
    const qreal width = qMax(height, ink.width() + radius);
    QRectF box(0, 0, width, height);
    box.moveCenter(a.pos);
    shape.badge.addRoundedRect(box, height / 2, height / 2);
    return shape;
}

QPainterPath arrowShape(const Annotation &a)
{
    if (a.points.size() < 2) return {};
    const QPointF tail = a.points.first();
    const QPointF delta = QPointF(a.points.last()) - tail;
    const qreal length = std::hypot(delta.x(), delta.y());
    if (length == 0) return {};

    using Style = Annotation::ArrowStyle;
    const qreal stroke = qMax(1, a.width);
    const bool doubleHead = a.arrowStyle == Style::Double;
    const qreal head = qMin((8 + 3 * stroke) * 0.90, length * (doubleHead ? 0.44 : 0.8));
    const qreal wing = head * 0.48;
    QPainterPath shape;
    if (a.arrowStyle == Style::Tapered) {
        const qreal neck = qMin(stroke, wing * 0.58);
        shape.moveTo(0, 0);
        shape.lineTo(length - head, -neck);
        shape.lineTo(length - head, -wing);
        shape.lineTo(length, 0);
        shape.lineTo(length - head, wing);
        shape.lineTo(length - head, neck);
        shape.closeSubpath();
    } else {
        QPainterPathStroker stroker;
        stroker.setWidth(stroke);
        stroker.setCapStyle(Qt::RoundCap);
        stroker.setJoinStyle(Qt::RoundJoin);
        QPainterPath shaft;
        shaft.moveTo(0, 0);
        shaft.lineTo(length, 0);
        auto makeHead = [&](qreal tip, qreal back) {
            QPainterPath path;
            path.moveTo(back, -wing);
            path.lineTo(tip, 0);
            path.lineTo(back, wing);
            if (a.arrowStyle == Style::Open) return stroker.createStroke(path);
            path.closeSubpath();
            return path.united(stroker.createStroke(path));
        };
        shape = stroker.createStroke(shaft).united(makeHead(length, length - head));
        if (doubleHead) shape = shape.united(makeHead(0, head));
    }
    const QPointF axis = delta / length;
    const QTransform toCanvas(axis.x(), axis.y(), -axis.y(), axis.x(), tail.x(), tail.y());
    return toCanvas.map(shape);
}
}

QString Annotation::numberLabel() const
{
    if (number < 1) return QString::number(number);
    if (numberFormat == NumberFormat::Padded)
        return QString::number(number).rightJustified(2, QLatin1Char('0'));
    if (numberFormat == NumberFormat::UpperAlpha || numberFormat == NumberFormat::LowerAlpha) {
        const char first = numberFormat == NumberFormat::UpperAlpha ? 'A' : 'a';
        QString label;
        for (int n = number; n > 0; n /= 26) {
            --n;
            label.prepend(QLatin1Char(char(first + n % 26)));
        }
        return label;
    }
    if ((numberFormat == NumberFormat::UpperRoman || numberFormat == NumberFormat::LowerRoman)
        && number <= 3999) {
        static const struct { int value; const char *digits; } roman[] = {
            {1000, "M"}, {900, "CM"}, {500, "D"}, {400, "CD"}, {100, "C"}, {90, "XC"},
            {50, "L"}, {40, "XL"}, {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}
        };
        QString label;
        int remaining = number;
        for (const auto &part : roman) {
            while (remaining >= part.value) {
                label += QLatin1String(part.digits);
                remaining -= part.value;
            }
        }
        return numberFormat == NumberFormat::UpperRoman ? label : label.toLower();
    }
    return QString::number(number);
}

void Annotation::paint(QPainter &p, const QImage *base, qreal dpr) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    switch (kind) {
    case Rect:
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect);
        break;
    case Ellipse:
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(rect);
        break;
    case Arrow:
        p.fillPath(arrowShape(*this), color);
        break;
    case Line:
        if (points.size() >= 2) {
            p.setPen(pen);
            p.drawLine(points.first(), points.last());
        }
        break;
    case Pen:
        if (points.size() >= 2) {
            p.setPen(pen);
            p.drawPolyline(points.constData(), points.size());
        }
        break;
    case Highlighter: {
        if (points.size() < 2) break;
        QColor c = color;
        c.setAlpha(110);
        QPen hi(c, qMax(6, width * 4), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p.setPen(hi);
        p.drawPolyline(points.constData(), points.size());
        break;
    }
    case Text: {
        QFont f;
        f.setPixelSize(fontSize);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QRect box(pos.x() - 5, pos.y() - fm.ascent() - 3, fm.horizontalAdvance(text) + 10, fm.height() + 6);
        if (textBackground) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(16, 20, 26, 200));
            p.drawRoundedRect(box, 5, 5);
        }
        p.setPen(color);
        p.drawText(pos, text);
        break;
    }
    case Number: {
        auto shape = numberShape(*this);
        switch (numberFill) {
        case NumberFill::Badge:
            p.fillPath(shape.badge, color);
            p.fillPath(shape.glyph, Qt::white);
            break;
        case NumberFill::Ink:
            p.fillPath(shape.glyph, color);
            break;
        case NumberFill::Cutout:
            // Leave the glyph untouched on any destination, including earlier annotations.
            shape.badge.addPath(shape.glyph);
            shape.badge.setFillRule(Qt::OddEvenFill);
            p.fillPath(shape.badge, color);
            break;
        }
        break;
    }
    case Pixelate: {
        const QRect r = rect.normalized();
        if (r.width() < 2 || r.height() < 2) break;
        if (!base || base->isNull()) {   // no pixels available: keep a visible marker
            p.setPen(QPen(color, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(r);
            break;
        }
        QRect phys(qRound(r.x() * dpr), qRound(r.y() * dpr), qRound(r.width() * dpr), qRound(r.height() * dpr));
        phys = phys.intersected(base->rect());
        if (phys.isEmpty()) break;

        const QImage region = base->copy(phys);
        const int b = qMax(2, int(qMax(4, block) * dpr));
        const QSize small(qMax(1, region.width() / b), qMax(1, region.height() / b));
        const QImage reduced = region.scaled(small, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        const QImage out = reduced.scaled(region.size(), Qt::IgnoreAspectRatio,
                                          blur ? Qt::SmoothTransformation : Qt::FastTransformation);
        p.drawImage(r, out);
        break;
    }
    }
    p.restore();
}

QRect Annotation::bounds() const
{
    QRect r;
    switch (kind) {
    case Rect:
    case Ellipse:
    case Pixelate:
        r = rect.normalized();
        break;
    case Arrow:
        r = arrowShape(*this).boundingRect().toAlignedRect();
        break;
    case Line:
    case Pen:
    case Highlighter:
        if (!points.isEmpty()) {
            int minX = points.first().x(), maxX = minX, minY = points.first().y(), maxY = minY;
            for (const auto &pt : points) {
                minX = qMin(minX, pt.x());
                maxX = qMax(maxX, pt.x());
                minY = qMin(minY, pt.y());
                maxY = qMax(maxY, pt.y());
            }
            r = QRect(QPoint(minX, minY), QPoint(maxX, maxY));
        }
        break;
    case Text: {
        QFont f;
        f.setPixelSize(fontSize);
        const QFontMetrics fm(f);
        return QRect(pos.x()-5,pos.y()-fm.ascent()-3,fm.horizontalAdvance(text)+10,fm.height()+6);
        break;
    }
    case Number: {
        const auto shape = numberShape(*this);
        r = (numberFill == NumberFill::Ink ? shape.glyph : shape.badge).boundingRect().toAlignedRect();
        break;
    }
    }
    if (!r.isValid()) return r;
    const int pad = qMax(4, width + 3);
    return r.adjusted(-pad, -pad, pad, pad);
}

bool Annotation::hitTest(const QPoint &p) const
{
    const int tolerance = qMax(6, width + 4);
    const auto nearSegment = [&](const QPoint &a, const QPoint &b) {
        const double dx = b.x() - a.x(), dy = b.y() - a.y();
        const double len2 = dx * dx + dy * dy;
        double t = len2 > 0 ? ((p.x() - a.x()) * dx + (p.y() - a.y()) * dy) / len2 : 0.0;
        t = qBound(0.0, t, 1.0);
        const double px = a.x() + t * dx, py = a.y() + t * dy;
        const double ddx = p.x() - px, ddy = p.y() - py;
        return std::sqrt(ddx * ddx + ddy * ddy) <= tolerance;
    };

    switch (kind) {
    case Rect:
    case Ellipse:
    case Pixelate: {
        const QRect r = rect.normalized();
        if (kind == Pixelate) return r.contains(p);
        return r.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(p);
    }
    case Arrow: {
        const QPainterPath shape = arrowShape(*this);
        return shape.intersects(QRectF(p.x() - 4, p.y() - 4, 8, 8));
    }
    case Line:
        return points.size() >= 2 && nearSegment(points.first(), points.last());
    case Pen:
    case Highlighter:
        for (int i = 1; i < points.size(); ++i)
            if (nearSegment(points.at(i - 1), points.at(i))) return true;
        return false;
    case Text:
        return bounds().contains(p);
    case Number: {
        const auto shape = numberShape(*this);
        if (numberFill == NumberFill::Ink)
            return shape.glyph.boundingRect().adjusted(-4, -4, 4, 4).contains(p);
        return shape.badge.intersects(QRectF(p.x() - 4, p.y() - 4, 8, 8));
    }
    }
    return false;
}

void Annotation::translate(const QPoint &delta)
{
    rect.translate(delta);
    pos += delta;
    for (auto &pt : points) pt += delta;
}
