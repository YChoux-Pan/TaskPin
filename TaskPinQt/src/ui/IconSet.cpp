#include "ui/IconSet.h"

#include <QHash>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

struct Entry {
    const char *id;
    const char *body;
};

// Iconify "lucide" 2px-rounded line icons (ISC). "number" is mdi:numeric-1-circle-outline.
const Entry kIcons[] = {
    {"select", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4.037 4.688a.495.495 0 0 1 .651-.651l16 6.5a.5.5 0 0 1-.063.947l-6.124 1.58a2 2 0 0 0-1.438 1.435l-1.579 6.126a.5.5 0 0 1-.947.063z"/>)SVG"},
    {"rect", R"SVG(<rect width="18" height="18" x="3" y="3" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" rx="2"/>)SVG"},
    {"ellipse", R"SVG(<circle cx="12" cy="12" r="10" fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"/>)SVG"},
    {"arrow", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 5h6v6m0-6L5 19"/>)SVG"},
    {"line", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h14"/>)SVG"},
    {"pen", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21.174 6.812a1 1 0 0 0-3.986-3.987L3.842 16.174a2 2 0 0 0-.5.83l-1.321 4.352a.5.5 0 0 0 .623.622l4.353-1.32a2 2 0 0 0 .83-.497zM15 5l4 4"/>)SVG"},
    {"marker", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="m9 11l-6 6v3h9l3-3"/><path d="m22 12l-4.6 4.6a2 2 0 0 1-2.8 0l-5.2-5.2a2 2 0 0 1 0-2.8L14 4"/></g>)SVG"},
    {"text", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 4v16M4 7V5a1 1 0 0 1 1-1h14a1 1 0 0 1 1 1v2M9 20h6"/>)SVG"},
    {"mosaic", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="M12 3v18m-9-9h18"/><rect width="18" height="18" x="3" y="3" rx="2"/></g>)SVG"},
    {"blur", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><circle cx="15" cy="9" r="7"/><circle cx="9" cy="15" r="7"/></g>)SVG"},
    {"number", R"SVG(<path fill="currentColor" d="M10 7h4v10h-2V9h-2zm2-5a10 10 0 0 1 10 10a10 10 0 0 1-10 10A10 10 0 0 1 2 12A10 10 0 0 1 12 2m0 2a8 8 0 0 0-8 8a8 8 0 0 0 8 8a8 8 0 0 0 8-8a8 8 0 0 0-8-8"/>)SVG"},
    {"eraser", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M21 21H8a2 2 0 0 1-1.42-.587l-3.994-3.999a2 2 0 0 1 0-2.828l10-10a2 2 0 0 1 2.829 0l5.999 6a2 2 0 0 1 0 2.828L12.834 21m-7.752-9.91l8.828 8.828"/>)SVG"},
    {"undo", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="M9 14L4 9l5-5"/><path d="M4 9h10.5a5.5 5.5 0 0 1 5.5 5.5a5.5 5.5 0 0 1-5.5 5.5H11"/></g>)SVG"},
    {"redo", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="m15 14l5-5l-5-5"/><path d="M20 9H9.5A5.5 5.5 0 0 0 4 14.5A5.5 5.5 0 0 0 9.5 20H13"/></g>)SVG"},
    {"copy", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><rect width="14" height="14" x="8" y="8" rx="2" ry="2"/><path d="M4 16c-1.1 0-2-.9-2-2V4c0-1.1.9-2 2-2h10c1.1 0 2 .9 2 2"/></g>)SVG"},
    {"save", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7M7 3v4a1 1 0 0 0 1 1h7"/></g>)SVG"},
    {"pin", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 17v5M9 10.76a2 2 0 0 1-1.11 1.79l-1.78.9A2 2 0 0 0 5 15.24V16a1 1 0 0 0 1 1h12a1 1 0 0 0 1-1v-.76a2 2 0 0 0-1.11-1.79l-1.78-.9A2 2 0 0 1 15 10.76V7a1 1 0 0 1 1-1a2 2 0 0 0 0-4H8a2 2 0 0 0 0 4a1 1 0 0 1 1 1z"/>)SVG"},
    {"ocr", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 7V5a2 2 0 0 1 2-2h2m10 0h2a2 2 0 0 1 2 2v2m0 10v2a2 2 0 0 1-2 2h-2M7 21H5a2 2 0 0 1-2-2v-2m4-9h8m-8 4h10M7 16h6"/>)SVG"},
    {"print", R"SVG(<g fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2"><path d="M6 18H4a2 2 0 0 1-2-2v-5a2 2 0 0 1 2-2h16a2 2 0 0 1 2 2v5a2 2 0 0 1-2 2h-2M6 9V3a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v6"/><rect width="12" height="8" x="6" y="14" rx="1"/></g>)SVG"},
    {"close", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M18 6L6 18M6 6l12 12"/>)SVG"},
    {"plus", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M5 12h14m-7-7v14"/>)SVG"},
    {"clear", R"SVG(<path fill="none" stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10 11v6m4-6v6m5-11v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/>)SVG"},
};

const char *bodyFor(const QString &id)
{
    for (const Entry &e : kIcons) {
        if (id == QLatin1String(e.id)) return e.body;
    }
    return nullptr;
}

QPixmap renderIcon(const QString &id, const QColor &color, int px, qreal dpr)
{
    const char *body = bodyFor(id);
    QPixmap pm;
    if (!body) return pm;

    QString svg = QStringLiteral(
                      "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' width='%1' height='%1' "
                      "fill='none' stroke='%2' stroke-width='2' stroke-linecap='round' stroke-linejoin='round'>%3</svg>")
                      .arg(px)
                      .arg(color.name(QColor::HexRgb))
                      .arg(QString::fromUtf8(body).replace(QLatin1String("currentColor"), color.name(QColor::HexRgb)));

    pm = QPixmap(QSize(px, px) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QSvgRenderer renderer(svg.toUtf8());
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    renderer.render(&p, QRectF(0, 0, px, px));
    return pm;
}

}   // namespace

void IconSet::draw(QPainter &p, const QString &id, const QRectF &rect, const QColor &color)
{
    if (!p.isActive() || rect.isEmpty()) return;

    const int px = qMax(8, qRound(rect.width()));
    const qreal dpr = p.device() ? p.device()->devicePixelRatioF() : qreal(1);
    const QString key = id + QLatin1Char('|') + color.name(QColor::HexRgb) + QLatin1Char('|') +
                        QString::number(px) + QLatin1Char('|') + QString::number(dpr, 'f', 2);

    static QHash<QString, QPixmap> cache;
    auto it = cache.constFind(key);
    if (it == cache.constEnd()) {
        it = cache.insert(key, renderIcon(id, color, px, dpr));
    }
    if (it.value().isNull()) return;

    p.drawPixmap(rect.topLeft(), it.value());
}
