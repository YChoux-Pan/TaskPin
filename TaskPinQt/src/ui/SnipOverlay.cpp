#include "ui/SnipOverlay.h"

#include "core/FileNaming.h"
#include "core/Settings.h"
#include "platform/WindowPicker.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStandardPaths>
#include <QWheelEvent>
#include <cmath>

namespace {
constexpr int kBtn = 32;
constexpr int kGap = 3;
constexpr int kToolbarPad = 8;
constexpr int kSwatchSize = 20;
constexpr int kSwatchCount = 7;

const QColor kBar(20, 25, 32, 240);
const QColor kAccent(0x4C, 0x9A, 0xFF);
const QColor kFg(0xE6, 0xEC, 0xF3);

QColor swatches[kSwatchCount] = {
    QColor(0xE5, 0x3B, 0x3B), QColor(0xFF, 0xB0, 0x20), QColor(0x3E, 0xC4, 0x6B),
    QColor(0x3B, 0x9C, 0xFF), QColor(0xB0, 0x6B, 0xFF), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x10, 0x14, 0x1A),
};
}   // namespace

SnipOverlay::SnipOverlay(const ScreenCapture &shot, QWidget *parent)
    : QWidget(parent), m_shot(shot)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(shot.logicalRect);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("TaskPin 截图"));

    // one device-pixel copy of the frozen frame: mosaic/blur and the magnifier read from here
    m_base = shot.pixmap.toImage();
    m_base.setDevicePixelRatio(1.0);

    const auto &s = Settings::instance();
    m_color = s.annotationColor;
    m_width = qBound(1, s.annotationWidth, 40);
    m_pickClock.start();
}

SnipOverlay::~SnipOverlay() = default;

void SnipOverlay::setPreselect(const QRect &r)
{
    m_sel = r.intersected(m_shot.logicalRect);
    m_hasSel = m_sel.width() > 4 && m_sel.height() > 4;
    update();
}

QRect SnipOverlay::selection() const { return m_sel.normalized(); }

void SnipOverlay::setTool(Tool t)
{
    m_tool = t;
    setCursor(t == Tool::Select ? Qt::ArrowCursor : Qt::CrossCursor);
    if (t != Tool::Select) m_selected = -1;
    update();
}

// ---------------------------------------------------------------- painting

void SnipOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(0, 0, m_shot.pixmap);

    if (!m_hasSel && m_mode != Mode::SelectRect) {
        p.fillRect(rect(), QColor(0, 0, 0, 105));
        drawHoverWindow(p);
        drawHints(p);
        drawMagnifier(p);
        return;
    }

    const QRect sel = selection();
    QRegion outside(rect());
    outside = outside.subtracted(QRegion(sel));
    p.save();
    p.setClipRegion(outside);
    p.fillRect(rect(), QColor(0, 0, 0, 105));
    p.restore();

    drawHoverWindow(p);

    for (int i = 0; i < m_items.size(); ++i) {
        m_items.at(i).paint(p, &m_base, m_shot.dpr);
        if (i == m_selected) {
            p.setPen(QPen(kAccent, 1, Qt::DashLine));
            p.setBrush(Qt::NoBrush);
            p.drawRect(m_items.at(i).bounds());
        }
    }
    if (m_drawing) m_current.paint(p, &m_base, m_shot.dpr);

    if (m_hasSel) drawSelection(p);
    drawToolbar(p);
    if (m_tool == Tool::Select) drawMagnifier(p);
}

void SnipOverlay::drawSelection(QPainter &p)
{
    const QRect sel = selection();
    p.setPen(QPen(kAccent, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(sel);

    p.setBrush(Qt::white);
    p.setPen(QPen(kAccent, 1));
    const QPoint pts[8] = {sel.topLeft(),
                           sel.topRight(),
                           sel.bottomLeft(),
                           sel.bottomRight(),
                           QPoint(sel.center().x(), sel.top()),
                           QPoint(sel.center().x(), sel.bottom()),
                           QPoint(sel.left(), sel.center().y()),
                           QPoint(sel.right(), sel.center().y())};
    for (const QPoint &c : pts) p.drawRect(QRect(c.x() - 3, c.y() - 3, 6, 6));

    // size label
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    QString text = QStringLiteral("%1 × %2").arg(sel.width()).arg(sel.height());
    if (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
        text += QStringLiteral("   %1 %2px").arg(m_tool == Tool::Blur ? QStringLiteral("模糊") : QStringLiteral("马赛克")).arg(m_block);
    const QFontMetrics fm(f);
    const QRect box(sel.left(), sel.top() - 24, fm.horizontalAdvance(text) + 16, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 22, 205));
    p.drawRect(box);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.drawText(box, Qt::AlignCenter, text);
}

void SnipOverlay::drawHoverWindow(QPainter &p)
{
    if (!m_hoverWindow.isValid() || m_hasSel) return;
    p.save();
    p.setBrush(QColor(0x4C, 0x9A, 0xFF, 26));
    p.setPen(QPen(kAccent, 1.6));
    p.drawRect(m_hoverWindow.adjusted(1, 1, -1, -1));
    if (!m_hoverTitle.isEmpty()) {
        QFont f = font();
        f.setPixelSize(12);
        p.setFont(f);
        const QFontMetrics fm(f);
        const QString title = fm.elidedText(m_hoverTitle, Qt::ElideRight, 320);
        const QRect chip(m_hoverWindow.left() + 4, m_hoverWindow.top() + 4, fm.horizontalAdvance(title) + 14, 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(12, 16, 22, 210));
        p.drawRect(chip);
        p.setPen(QColor(0xE8, 0xEC, 0xF2));
        p.drawText(chip, Qt::AlignCenter, title);
    }
    p.restore();
}

void SnipOverlay::drawHints(QPainter &p)
{
    const QString tip = QStringLiteral("拖拽框选 · 单击选窗口 · 空格全屏 · Esc 取消");
    QFont f = font();
    f.setPixelSize(14);
    p.setFont(f);
    const QFontMetrics fm(f);
    const int w = fm.horizontalAdvance(tip) + 28;
    const QRect box(width() / 2 - w / 2, 36, w, 34);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 22, 200));
    p.drawRoundedRect(box, 10, 10);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.drawText(box, Qt::AlignCenter, tip);
}

void SnipOverlay::drawMagnifier(QPainter &p)
{
    if (m_base.isNull() || !rect().contains(m_cursor)) return;

    const int zoom = 8, box = 128;
    const QRect src(m_cursor.x() - box / zoom / 2, m_cursor.y() - box / zoom / 2, box / zoom, box / zoom);
    QRect dest(m_cursor.x() + 24, m_cursor.y() + 24, box, box);
    if (dest.right() > width() - 8) dest.moveLeft(m_cursor.x() - 24 - box);
    if (dest.bottom() > height() - 52) dest.moveTop(m_cursor.y() - 24 - box);

    p.drawPixmap(dest, m_shot.pixmap,
                 QRectF(src.x() * m_shot.dpr, src.y() * m_shot.dpr, src.width() * m_shot.dpr,
                        src.height() * m_shot.dpr));
    p.setPen(QPen(kAccent, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(dest);
    p.drawLine(dest.center().x(), dest.top() + 6, dest.center().x(), dest.bottom() - 6);
    p.drawLine(dest.left() + 6, dest.center().y(), dest.right() - 6, dest.center().y());

    const QColor c = m_base.pixelColor(
        qBound(0, int(m_cursor.x() * m_shot.dpr), m_base.width() - 1),
        qBound(0, int(m_cursor.y() * m_shot.dpr), m_base.height() - 1));

    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    const QString info = QStringLiteral("%1, %2   #%3%4%5")
                             .arg(m_cursor.x() + m_shot.logicalRect.x())
                             .arg(m_cursor.y() + m_shot.logicalRect.y())
                             .arg(c.red(), 2, 16, QChar('0'))
                             .arg(c.green(), 2, 16, QChar('0'))
                             .arg(c.blue(), 2, 16, QChar('0'));
    const QRect infoBox(dest.left(), dest.bottom() + 4, 196, 20);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 22, 210));
    p.drawRect(infoBox);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.drawText(infoBox.adjusted(6, 0, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, info);
}

void SnipOverlay::drawToolbar(QPainter &p)
{
    m_hot.clear();

    const QStringList toolIds{QStringLiteral("select"), QStringLiteral("rect"),   QStringLiteral("ellipse"),
                              QStringLiteral("arrow"),  QStringLiteral("line"),   QStringLiteral("pen"),
                              QStringLiteral("marker"), QStringLiteral("text"),   QStringLiteral("mosaic"),
                              QStringLiteral("blur"),   QStringLiteral("number"), QStringLiteral("eraser")};
    const QStringList actionIds{QStringLiteral("undo"), QStringLiteral("redo"), QStringLiteral("copy"),
                                QStringLiteral("save"), QStringLiteral("pin"),  QStringLiteral("ocr"),
                                QStringLiteral("print"), QStringLiteral("close")};

    const int row1 = toolIds.size() * (kBtn + kGap) - kGap;
    const int row2 = kSwatchCount * (kSwatchSize + kGap) - kGap + 6 + (2 * 26 + kGap) + 8 +
                     actionIds.size() * (kBtn + kGap) - kGap;
    const int w = qMax(row1, row2) + kToolbarPad * 2;

    const QRect sel = selection();
    int x = sel.right() - w;
    int y = sel.bottom() + 12;
    if (x < 8) x = 8;
    if (y + 90 > height()) y = sel.top() - 92;
    if (y < 8) y = qMax(8, sel.bottom() - 92);

    m_toolbarRect = QRect(x - kToolbarPad, y - kToolbarPad, w + kToolbarPad, 2 * kBtn + 6 + kToolbarPad * 2);
    p.setPen(Qt::NoPen);
    p.setBrush(kBar);
    p.drawRoundedRect(m_toolbarRect, 9, 9);

    const auto activeToolId = [this]() -> QString {
        switch (m_tool) {
        case Tool::Select: return QStringLiteral("select");
        case Tool::Rect: return QStringLiteral("rect");
        case Tool::Ellipse: return QStringLiteral("ellipse");
        case Tool::Arrow: return QStringLiteral("arrow");
        case Tool::Line: return QStringLiteral("line");
        case Tool::Pen: return QStringLiteral("pen");
        case Tool::Highlighter: return QStringLiteral("marker");
        case Tool::Text: return QStringLiteral("text");
        case Tool::Mosaic: return QStringLiteral("mosaic");
        case Tool::Blur: return QStringLiteral("blur");
        case Tool::Number: return QStringLiteral("number");
        case Tool::Eraser: return QStringLiteral("eraser");
        }
        return {};
    };
    const QString activeId = activeToolId();

    for (int i = 0; i < toolIds.size(); ++i) {
        const QRect r(x + i * (kBtn + kGap), y, kBtn, kBtn);
        m_hot.append({r, QStringLiteral("tool:") + toolIds.at(i)});
        const bool active = toolIds.at(i) == activeId;
        if (active) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(0x4C, 0x9A, 0xFF, 70));
            p.drawRoundedRect(r, 6, 6);
        }
        p.setPen(QPen(active ? kAccent : kFg, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        const QPoint c = r.center();
        const QString id = toolIds.at(i);
        if (id == QStringLiteral("select")) {
            QPolygon poly;
            poly << c + QPoint(-5, -7) << c + QPoint(6, 0) << c + QPoint(0, 1) << c + QPoint(3, 7);
            p.setBrush(active ? kAccent : kFg);
            p.setPen(Qt::NoPen);
            p.drawPolygon(poly);
        } else if (id == QStringLiteral("rect")) {
            p.drawRect(QRect(c.x() - 7, c.y() - 5, 14, 10));
        } else if (id == QStringLiteral("ellipse")) {
            p.drawEllipse(QRect(c.x() - 7, c.y() - 5, 14, 10));
        } else if (id == QStringLiteral("arrow")) {
            p.drawLine(c + QPoint(-7, 6), c + QPoint(5, -5));
            p.drawLine(c + QPoint(5, -5), c + QPoint(1, -5));
            p.drawLine(c + QPoint(5, -5), c + QPoint(5, -1));
        } else if (id == QStringLiteral("line")) {
            p.drawLine(c + QPoint(-7, 6), c + QPoint(7, -6));
        } else if (id == QStringLiteral("pen")) {
            QPainterPath path;
            path.moveTo(c + QPoint(-7, 5));
            path.cubicTo(c + QPoint(-2, -7), c + QPoint(2, 7), c + QPoint(7, -5));
            p.drawPath(path);
        } else if (id == QStringLiteral("marker")) {
            p.setPen(QPen(active ? kAccent : kFg, 5, Qt::SolidLine, Qt::RoundCap));
            p.drawLine(c + QPoint(-6, 5), c + QPoint(6, -5));
        } else if (id == QStringLiteral("text")) {
            QFont f = p.font();
            f.setPixelSize(15);
            f.setBold(true);
            p.setFont(f);
            p.setPen(active ? kAccent : kFg);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("T"));
        } else if (id == QStringLiteral("mosaic")) {
            for (int gx = 0; gx < 2; ++gx)
                for (int gy = 0; gy < 2; ++gy) {
                    const QRect cell(c.x() - 7 + gx * 7, c.y() - 7 + gy * 7, 7, 7);
                    if ((gx + gy) % 2 == 0) p.fillRect(cell, active ? kAccent : kFg);
                    else p.drawRect(cell);
                }
        } else if (id == QStringLiteral("blur")) {
            QPen dash(active ? kAccent : kFg, 1.5, Qt::DashLine);
            p.setPen(dash);
            p.drawEllipse(QRect(c.x() - 8, c.y() - 6, 16, 12));
            p.drawEllipse(QRect(c.x() - 4, c.y() - 3, 8, 6));
        } else if (id == QStringLiteral("number")) {
            p.drawEllipse(QRect(c.x() - 8, c.y() - 8, 16, 16));
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.setPen(active ? kAccent : kFg);
            p.drawText(QRect(c.x() - 8, c.y() - 8, 16, 16), Qt::AlignCenter, QStringLiteral("1"));
        } else if (id == QStringLiteral("eraser")) {
            QPolygon poly;
            poly << c + QPoint(-7, 3) << c + QPoint(-1, -6) << c + QPoint(7, -1) << c + QPoint(1, 7);
            p.drawPolygon(poly);
        }
    }

    // ---- second row: colours, width, actions
    const int y2 = y + kBtn + 6;
    int cx = x;
    for (int i = 0; i < kSwatchCount; ++i) {
        const QRect r(cx, y2 + (kBtn - kSwatchSize) / 2, kSwatchSize, kSwatchSize);
        m_hot.append({r, QStringLiteral("swatch:%1").arg(i)});
        p.setPen(QPen(m_color == swatches[i] ? Qt::white : QColor(255, 255, 255, 60), 2));
        p.setBrush(swatches[i]);
        p.drawEllipse(r);
        cx += kSwatchSize + kGap;
    }
    cx += 6;
    for (const QString &id : {QStringLiteral("width-"), QStringLiteral("width+")}) {
        const QRect r(cx, y2, 26, kBtn);
        m_hot.append({r, id});
        p.setPen(QPen(kFg, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(r.center() + QPoint(-6, 0), r.center() + QPoint(6, 0));
        if (id == QStringLiteral("width+")) p.drawLine(r.center() + QPoint(0, -6), r.center() + QPoint(0, 6));
        cx += 26 + kGap;
    }
    cx += 8;
    for (int i = 0; i < actionIds.size(); ++i) {
        const QRect r(cx + i * (kBtn + kGap), y2, kBtn, kBtn);
        const QString id = actionIds.at(i);
        m_hot.append({r, id});
        p.setPen(QPen(id == QStringLiteral("close") ? QColor(0xFF, 0x8A, 0x7A) : kFg, 1.6, Qt::SolidLine, Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const QPoint c = r.center();
        if (id == QStringLiteral("undo") || id == QStringLiteral("redo")) {
            const int dir = id == QStringLiteral("undo") ? -1 : 1;
            QPainterPath path;
            path.arcMoveTo(QRectF(c.x() - 7, c.y() - 6, 14, 12), dir < 0 ? 20 : 160);
            path.arcTo(QRectF(c.x() - 7, c.y() - 6, 14, 12), dir < 0 ? 20 : 160, dir < 0 ? 140 : -140);
            p.drawPath(path);
            p.drawLine(c + QPoint(dir * 7, -6), c + QPoint(dir * 3, -1));
        } else if (id == QStringLiteral("copy")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 9, 9));
            p.drawRect(QRect(c.x() - 2, c.y() - 2, 9, 9));
        } else if (id == QStringLiteral("save")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 12, 12));
            p.setBrush(kFg);
            p.drawRect(QRect(c.x() - 3, c.y() - 6, 6, 4));
            p.setBrush(Qt::NoBrush);
        } else if (id == QStringLiteral("pin")) {
            p.drawEllipse(QRect(c.x() - 4, c.y() - 7, 8, 6));
            p.drawLine(c, c + QPoint(0, 7));
        } else if (id == QStringLiteral("ocr")) {
            p.drawLine(c + QPoint(-6, -6), c + QPoint(-2, -6));
            p.drawLine(c + QPoint(-6, -6), c + QPoint(-6, -2));
            p.drawLine(c + QPoint(6, 6), c + QPoint(2, 6));
            p.drawLine(c + QPoint(6, 6), c + QPoint(6, 2));
            QFont f = p.font();
            f.setPixelSize(11);
            f.setBold(true);
            p.setFont(f);
            p.drawText(QRect(c.x() - 6, c.y() - 6, 12, 12), Qt::AlignCenter, QStringLiteral("A"));
        } else if (id == QStringLiteral("print")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 12, 6));
            p.drawRect(QRect(c.x() - 6, c.y(), 12, 6));
            p.drawRect(QRect(c.x() - 3, c.y() + 4, 6, 4));
        } else {
            p.drawLine(c + QPoint(-6, -6), c + QPoint(6, 6));
            p.drawLine(c + QPoint(6, -6), c + QPoint(-6, 6));
        }
    }
}

// ---------------------------------------------------------------- input

void SnipOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        close();
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    m_cursor = e->pos();

    if (m_hasSel) {
        const QString id = hitToolbar(e->pos());
        if (!id.isEmpty()) {
            handleAction(id);
            return;
        }
    }

    if (!m_hasSel) {
        // clicking while the cursor is over a window snaps to that window instead of rubber-banding
        m_mode = Mode::SelectRect;
        m_dragStart = e->pos();
        m_dragLast = e->pos();
        m_dragMoved = false;
        m_sel = QRect(e->pos(), QSize());
        update();
        return;
    }

    switch (m_tool) {
    case Tool::Select: {
        int hit = -1;
        for (int i = m_items.size() - 1; i >= 0; --i)
            if (m_items.at(i).hitTest(e->pos())) {
                hit = i;
                break;
            }
        if (hit >= 0) {
            m_selected = hit;
            m_mode = Mode::MoveAnnotation;
            m_dragLast = e->pos();
            update();
            return;
        }
        const int handle = selectionHandleAt(e->pos());
        if (handle >= 0) {
            m_mode = Mode::ResizeSelection;
            m_resizeHandle = handle;
            m_selAtPress = m_sel;
            m_dragStart = e->pos();
            update();
            return;
        }
        m_selected = -1;
        m_mode = Mode::SelectRect;
        m_dragStart = e->pos();
        m_dragLast = e->pos();
        m_dragMoved = false;
        m_sel = QRect(e->pos(), QSize());
        m_hasSel = false;
        update();
        return;
    }
    case Tool::Text:
        beginText(e->pos());
        break;
    case Tool::Eraser:
        m_mode = Mode::Erase;
        eraseAt(e->pos());
        break;
    case Tool::Number: {
        Annotation a;
        a.kind = Annotation::Number;
        a.color = m_color;
        a.width = m_width;
        a.pos = e->pos();
        a.number = m_nextNumber++;
        m_items.append(a);
        m_redoStack.clear();
        update();
        break;
    }
    default: {
        m_drawing = true;
        m_mode = Mode::Draw;
        m_current = Annotation();
        m_current.color = m_color;
        m_current.width = m_width;
        m_current.rect = QRect(e->pos(), QSize());
        m_current.points = {e->pos()};
        m_current.block = m_block;
        switch (m_tool) {
        case Tool::Rect: m_current.kind = Annotation::Rect; break;
        case Tool::Ellipse: m_current.kind = Annotation::Ellipse; break;
        case Tool::Arrow: m_current.kind = Annotation::Arrow; break;
        case Tool::Line: m_current.kind = Annotation::Line; break;
        case Tool::Pen: m_current.kind = Annotation::Pen; break;
        case Tool::Highlighter: m_current.kind = Annotation::Highlighter; break;
        case Tool::Mosaic: m_current.kind = Annotation::Pixelate; m_current.blur = false; break;
        case Tool::Blur: m_current.kind = Annotation::Pixelate; m_current.blur = true; break;
        default: break;
        }
        update();
        break;
    }
    }
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->pos();

    switch (m_mode) {
    case Mode::SelectRect:
        m_sel = QRect(m_dragStart, e->pos()).normalized();
        if ((e->pos() - m_dragStart).manhattanLength() > 3) m_dragMoved = true;
        update();
        return;
    case Mode::MoveAnnotation:
        if (m_selected >= 0 && m_selected < m_items.size()) {
            m_items[m_selected].translate(e->pos() - m_dragLast);
            m_dragLast = e->pos();
            update();
        }
        return;
    case Mode::ResizeSelection: {
        QRect r = m_selAtPress;
        const QPoint d = e->pos() - m_dragStart;
        switch (m_resizeHandle) {
        case 0: r.setTopLeft(r.topLeft() + d); break;
        case 1: r.setTopRight(r.topRight() + d); break;
        case 2: r.setBottomLeft(r.bottomLeft() + d); break;
        case 3: r.setBottomRight(r.bottomRight() + d); break;
        case 4: r.setTop(r.top() + d.y()); break;
        case 5: r.setBottom(r.bottom() + d.y()); break;
        case 6: r.setLeft(r.left() + d.x()); break;
        case 7: r.setRight(r.right() + d.x()); break;
        default: break;
        }
        m_sel = r.normalized().intersected(rect());
        update();
        return;
    }
    case Mode::Erase:
        eraseAt(e->pos());
        return;
    case Mode::Draw:
        if (m_drawing) {
            switch (m_current.kind) {
            case Annotation::Rect:
            case Annotation::Ellipse:
            case Annotation::Pixelate:
                m_current.rect = QRect(m_current.points.first(), e->pos()).normalized();
                break;
            case Annotation::Arrow:
            case Annotation::Line:
                m_current.points = {m_current.points.first(), e->pos()};
                break;
            default:
                m_current.points.append(e->pos());
                break;
            }
            update();
            return;
        }
        break;
    case Mode::Idle:
        break;
    }

    if (m_tool == Tool::Select && !m_hasSel) updateHoverWindow(e->pos());
    update();
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;

    if (m_mode == Mode::SelectRect) {
        const QRect r = QRect(m_dragStart, e->pos()).normalized();
        m_mode = Mode::Idle;
        if (r.width() <= 3 || r.height() <= 3) {
            if (m_hoverWindow.isValid() && !m_dragMoved) {
                m_sel = m_hoverWindow;
                m_hasSel = true;
            } else {
                m_sel = QRect();
                m_hasSel = false;
            }
        } else {
            m_sel = r;
            m_hasSel = true;
        }
        update();
        return;
    }
    if (m_mode == Mode::Draw && m_drawing) {
        m_drawing = false;
        m_items.append(m_current);
        m_redoStack.clear();
    }
    m_mode = Mode::Idle;
    update();
}

void SnipOverlay::wheelEvent(QWheelEvent *e)
{
    const int dir = e->angleDelta().y() > 0 ? 1 : -1;
    if (m_tool == Tool::Text) m_fontSize = qBound(10, m_fontSize + dir * 2, 96);
    else if (m_tool == Tool::Mosaic || m_tool == Tool::Blur) m_block = qBound(4, m_block + dir * 2, 80);
    else m_width = qBound(1, m_width + dir, 40);
    update();
}

void SnipOverlay::keyPressEvent(QKeyEvent *e)
{
    if (m_textEdit && m_textEdit->hasFocus()) {
        QWidget::keyPressEvent(e);
        return;
    }

    switch (e->key()) {
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_V: setTool(Tool::Select); return;
    case Qt::Key_R:
        if (e->modifiers() & Qt::ControlModifier) {
            handleAction(QStringLiteral("ocr"));
            return;
        }
        setTool(Tool::Rect);
        return;
    case Qt::Key_O: setTool(Tool::Ellipse); return;
    case Qt::Key_A: setTool(Tool::Arrow); return;
    case Qt::Key_L: setTool(Tool::Line); return;
    case Qt::Key_P:
        if (e->modifiers() & Qt::ControlModifier) {
            handleAction(QStringLiteral("pin"));
            return;
        }
        setTool(Tool::Pen);
        return;
    case Qt::Key_H: setTool(Tool::Highlighter); return;
    case Qt::Key_T: setTool(Tool::Text); return;
    case Qt::Key_N: setTool(Tool::Number); return;
    case Qt::Key_M: setTool(Tool::Mosaic); return;
    case Qt::Key_B: setTool(Tool::Blur); return;
    case Qt::Key_E: setTool(Tool::Eraser); return;
    case Qt::Key_Space:
        m_sel = m_shot.logicalRect;
        m_hasSel = true;
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        handleAction(QStringLiteral("copy"));
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selected >= 0 && m_selected < m_items.size()) {
            m_items.removeAt(m_selected);
            m_selected = -1;
            update();
        }
        return;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->modifiers() & Qt::ShiftModifier) redo();
            else undo();
        }
        return;
    case Qt::Key_Y:
        if (e->modifiers() & Qt::ControlModifier) redo();
        return;
    case Qt::Key_S:
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->modifiers() & Qt::ShiftModifier) saveQuick();
            else handleAction(QStringLiteral("save"));
        }
        return;
    case Qt::Key_Left: nudgeSelection(e->modifiers(), QPoint(-1, 0)); return;
    case Qt::Key_Right: nudgeSelection(e->modifiers(), QPoint(1, 0)); return;
    case Qt::Key_Up: nudgeSelection(e->modifiers(), QPoint(0, -1)); return;
    case Qt::Key_Down: nudgeSelection(e->modifiers(), QPoint(0, 1)); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

void SnipOverlay::nudgeSelection(Qt::KeyboardModifiers mods, const QPoint &delta)
{
    if (!m_hasSel) return;
    QRect r = m_sel;
    if (mods & Qt::ShiftModifier) r.adjust(0, 0, delta.x(), delta.y());      // 外扩
    else if (mods & Qt::AltModifier) r.adjust(0, 0, -delta.x(), -delta.y()); // 收窄
    else r.translate(delta);
    if (r.width() < 8 || r.height() < 8) return;
    m_sel = r.intersected(rect());
    m_hasSel = true;
    update();
}

// ---------------------------------------------------------------- helpers

QString SnipOverlay::hitToolbar(const QPoint &pos) const
{
    for (const auto &h : m_hot)
        if (h.rect.contains(pos)) return h.id;
    return {};
}

int SnipOverlay::selectionHandleAt(const QPoint &pos) const
{
    if (!m_hasSel) return -1;
    const QRect s = selection();
    const QPoint pts[8] = {s.topLeft(),
                           s.topRight(),
                           s.bottomLeft(),
                           s.bottomRight(),
                           QPoint(s.center().x(), s.top()),
                           QPoint(s.center().x(), s.bottom()),
                           QPoint(s.left(), s.center().y()),
                           QPoint(s.right(), s.center().y())};
    for (int i = 0; i < 8; ++i)
        if ((pos - pts[i]).manhattanLength() <= 8) return i;
    return -1;
}

void SnipOverlay::updateHoverWindow(const QPoint &pos)
{
    if (m_pickClock.isValid() && m_pickClock.elapsed() < 40 && (pos - m_lastPickPos).manhattanLength() < 4) return;
    m_pickClock.restart();
    m_lastPickPos = pos;

    const int physX = qRound((m_shot.logicalRect.x() + pos.x()) * m_shot.dpr);
    const int physY = qRound((m_shot.logicalRect.y() + pos.y()) * m_shot.dpr);
    const WindowPicker::Hit hit = WindowPicker::atPhysicalPoint(physX, physY);
    if (!hit.valid) {
        if (m_hoverWindow.isValid()) {
            m_hoverWindow = QRect();
            m_hoverTitle.clear();
        }
        return;
    }

    QRect logical(qRound(hit.physicalRect.x() / m_shot.dpr) - m_shot.logicalRect.x(),
                  qRound(hit.physicalRect.y() / m_shot.dpr) - m_shot.logicalRect.y(),
                  qRound(hit.physicalRect.width() / m_shot.dpr), qRound(hit.physicalRect.height() / m_shot.dpr));
    logical = logical.intersected(rect());
    if (logical.width() < 12 || logical.height() < 12) {
        m_hoverWindow = QRect();
        m_hoverTitle.clear();
        return;
    }
    m_hoverWindow = logical;
    m_hoverTitle = hit.title;
}

void SnipOverlay::eraseAt(const QPoint &pos)
{
    for (int i = m_items.size() - 1; i >= 0; --i) {
        if (m_items.at(i).hitTest(pos)) {
            m_items.removeAt(i);
            m_selected = -1;
            update();
            return;
        }
    }
}

// ---------------------------------------------------------------- actions

void SnipOverlay::handleAction(const QString &id)
{
    if (id.startsWith(QStringLiteral("tool:"))) {
        const QString t = id.mid(5);
        if (t == QStringLiteral("select")) setTool(Tool::Select);
        else if (t == QStringLiteral("rect")) setTool(Tool::Rect);
        else if (t == QStringLiteral("ellipse")) setTool(Tool::Ellipse);
        else if (t == QStringLiteral("arrow")) setTool(Tool::Arrow);
        else if (t == QStringLiteral("line")) setTool(Tool::Line);
        else if (t == QStringLiteral("pen")) setTool(Tool::Pen);
        else if (t == QStringLiteral("marker")) setTool(Tool::Highlighter);
        else if (t == QStringLiteral("text")) setTool(Tool::Text);
        else if (t == QStringLiteral("mosaic")) setTool(Tool::Mosaic);
        else if (t == QStringLiteral("blur")) setTool(Tool::Blur);
        else if (t == QStringLiteral("number")) setTool(Tool::Number);
        else if (t == QStringLiteral("eraser")) setTool(Tool::Eraser);
        return;
    }
    if (id.startsWith(QStringLiteral("swatch:"))) {
        const QColor c = swatches[id.mid(7).toInt()];
        if (m_selected >= 0 && m_selected < m_items.size()) m_items[m_selected].color = c;
        m_color = c;
        update();
        return;
    }
    if (id == QStringLiteral("width-")) {
        m_width = qMax(1, m_width - 1);
        update();
        return;
    }
    if (id == QStringLiteral("width+")) {
        m_width = qMin(40, m_width + 1);
        update();
        return;
    }
    if (id == QStringLiteral("undo")) {
        undo();
        return;
    }
    if (id == QStringLiteral("redo")) {
        redo();
        return;
    }
    if (id == QStringLiteral("close")) {
        close();
        return;
    }

    if (id == QStringLiteral("copy")) {
        const QImage img = compose();
        if (img.isNull()) return;
        QApplication::clipboard()->setImage(img);
        emit captured(img, selection());
        emit copied(img);
        close();
        return;
    }
    if (id == QStringLiteral("save")) {
        const QImage img = compose();
        if (img.isNull()) return;
        const QString def = Settings::instance().resolveAutoSaveDir() + QStringLiteral("/") +
                            FileNaming::expand(Settings::instance().namePattern, QDateTime::currentDateTime());
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存截图"), def,
                                                          QStringLiteral("PNG 图片 (*.png)"));
        if (path.isEmpty()) return;
        if (!img.save(path, "PNG")) {
            qWarning() << "save failed:" << path;
            return;
        }
        emit captured(img, selection());
        emit saved(path);
        close();
        return;
    }
    if (id == QStringLiteral("pin")) {
        const QImage img = compose();
        if (img.isNull()) return;
        emit captured(img, selection());
        emit pinned(img);
        close();
        return;
    }
    if (id == QStringLiteral("ocr")) {
        const QImage img = compose();
        if (img.isNull()) return;
        emit captured(img, selection());
        emit recognizeRequested(img);
        close();
        return;
    }
    if (id == QStringLiteral("print")) {
        const QImage img = compose();
        if (img.isNull()) return;
        emit captured(img, selection());
        emit printed(img);
        close();
        return;
    }
}

void SnipOverlay::saveQuick()
{
    const QImage img = compose();
    if (img.isNull()) return;
    const auto &s = Settings::instance();
    const QString dir = s.resolveAutoSaveDir();
    const QString path = FileNaming::uniquePath(dir, s.namePattern, QDateTime::currentDateTime());
    if (!img.save(path, "PNG")) {
        qWarning() << "quick save failed:" << path;
        return;
    }
    qInfo("quick save: %s", qUtf8Printable(path));
    emit captured(img, selection());
    emit saved(path);
    close();
}

QImage SnipOverlay::compose() const
{
    const QRect sel = selection();
    if (sel.width() < 2 || sel.height() < 2) return {};

    const qreal dpr = m_shot.dpr;
    const QRect src(qRound(sel.x() * dpr), qRound(sel.y() * dpr), qRound(sel.width() * dpr),
                    qRound(sel.height() * dpr));

    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawPixmap(QRect(0, 0, src.width(), src.height()), m_shot.pixmap, src);
        p.scale(dpr, dpr);
        p.translate(-sel.topLeft());
        for (const auto &a : m_items) a.paint(p, &m_base, dpr);
    }
    out.setDevicePixelRatio(1.0);
    return out;
}

void SnipOverlay::undo()
{
    if (m_items.isEmpty()) return;
    m_redoStack.append(m_items.takeLast());
    m_selected = -1;
    update();
}

void SnipOverlay::redo()
{
    if (m_redoStack.isEmpty()) return;
    m_items.append(m_redoStack.takeLast());
    update();
}

void SnipOverlay::beginText(const QPoint &at)
{
    commitText();
    m_textPos = at;
    m_textEdit = new QLineEdit(this);
    m_textEdit->setGeometry(at.x(), at.y(), 240, 30);
    m_textEdit->setStyleSheet(
        "QLineEdit { background: rgba(20,25,32,235); border: 1px solid #4C9AFF; color: #E8ECF2; }");
    m_textEdit->installEventFilter(this);
    m_textEdit->show();
    m_textEdit->setFocus();
    // Enter commits (returnPressed) and clicking elsewhere commits too; Esc cancels in eventFilter.
    connect(m_textEdit, &QLineEdit::editingFinished, this, &SnipOverlay::commitText);
}

bool SnipOverlay::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_textEdit && e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        if (key->key() == Qt::Key_Escape) {
            cancelText();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            commitText();
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void SnipOverlay::commitText()
{
    if (!m_textEdit) return;
    QLineEdit *edit = m_textEdit;
    m_textEdit = nullptr;   // drop the pointer first: setFocus() below emits editingFinished again
    const QString text = edit->text().trimmed();
    edit->removeEventFilter(this);
    edit->deleteLater();
    setFocus();
    if (text.isEmpty()) return;

    Annotation a;
    a.kind = Annotation::Text;
    a.color = m_color;
    a.text = text;
    a.pos = m_textPos;
    a.fontSize = m_fontSize;
    m_items.append(a);
    m_redoStack.clear();
    update();
}

void SnipOverlay::cancelText()
{
    if (!m_textEdit) return;
    QLineEdit *edit = m_textEdit;
    m_textEdit = nullptr;
    edit->removeEventFilter(this);
    edit->deleteLater();
    setFocus();
    update();
}
