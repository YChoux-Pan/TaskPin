#include "ui/Annotation.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>
#include <QPolygon>
#include <cmath>

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
    case Arrow: {
        if (points.size() < 2) break;
        p.setPen(pen);
        const QPoint a = points.first(), b = points.last();
        p.drawLine(a, b);
        const double ang = std::atan2(double(b.y() - a.y()), double(b.x() - a.x()));
        const int len = 8 + width * 3;
        const QPoint p1(b.x() - int(len * std::cos(ang - 0.45)), b.y() - int(len * std::sin(ang - 0.45)));
        const QPoint p2(b.x() - int(len * std::cos(ang + 0.45)), b.y() - int(len * std::sin(ang + 0.45)));
        QPolygon head;
        head << b << p1 << p2;
        p.setBrush(color);
        p.drawPolygon(head);
        break;
    }
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
        QFont f = p.font();
        f.setPixelSize(fontSize);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QRect box(pos.x() - 5, pos.y() - 3, fm.horizontalAdvance(text) + 10, fm.height() + 6);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(16, 20, 26, 200));
        p.drawRoundedRect(box, 5, 5);
        p.setPen(color);
        p.drawText(pos, text);
        break;
    }
    case Number: {
        const int r = 11 + width;
        const QRect box(pos.x() - r, pos.y() - r, r * 2, r * 2);
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(box);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(r + 4);
        f.setBold(true);
        p.setFont(f);
        p.drawText(box, Qt::AlignCenter, QString::number(number));
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
        r = QRect(pos, QSize(fm.horizontalAdvance(text), fm.height()));
        break;
    }
    case Number: {
        const int rad = 11 + width;
        r = QRect(pos.x() - rad, pos.y() - rad, rad * 2, rad * 2);
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
    case Arrow:
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
        const QPoint d = p - pos;
        return d.manhattanLength() <= 16 + width;
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
