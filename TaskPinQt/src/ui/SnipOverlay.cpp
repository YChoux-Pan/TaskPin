#include "ui/SnipOverlay.h"

#include "core/FileNaming.h"
#include "core/Settings.h"
#include "core/InstanceNames.h"
#include "platform/WindowPicker.h"
#include "platform/HotkeyText.h"
#include "ui/IconSet.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QCursor>
#include <QShowEvent>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QStandardPaths>
#include <QWheelEvent>
#include <cmath>

namespace
{
constexpr int kSwatchCount = 7;

// Light toolbar (Iconify-lucide line icons), selection accents stay bright for the dimmed backdrop.
const QColor kBar(0xEC, 0xEE, 0xF3, 250);
const QColor kBarEdge(0x1A, 0x1D, 0x22, 30);
const QColor kAccent(0x4C, 0x9A, 0xFF);
const QColor kToolAccent(0x2F, 0x6F, 0xEB);
const QColor kFg(0x2E, 0x33, 0x3B);
const QColor kDanger(0xC0, 0x3B, 0x2E);

QColor swatches[kSwatchCount] = {
    QColor(0xE5, 0x3B, 0x3B),
    QColor(0xFF, 0xB0, 0x20),
    QColor(0x3E, 0xC4, 0x6B),
    QColor(0x3B, 0x9C, 0xFF),
    QColor(0xB0, 0x6B, 0xFF),
    QColor(0xFF, 0xFF, 0xFF),
    QColor(0x10, 0x14, 0x1A),
};
} // namespace

SnipOverlay::SnipOverlay(const ScreenCapture &shot, QWidget *parent)
    : QWidget(parent)
    , m_shot(shot)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                   (taskPinAutomation() ? Qt::Window : Qt::Tool));
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(shot.logicalRect);
    m_cursor = boundedPoint(QCursor::pos() - shot.logicalRect.topLeft());
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("TaskPin 截图"));

    // The overlay covers exactly one screen, so its logical origin is that screen's. The physical
    // origin comes from the same monitor, which is what keeps click-to-snap exact on mixed DPI.
    m_monitor = WindowPicker::monitorRectForDevice(shot.screenName);
    if (!m_monitor.isValid()) {
        m_monitor = QRect(
            QPoint(qRound(shot.logicalRect.x() * shot.dpr), qRound(shot.logicalRect.y() * shot.dpr)),
            QSize(qRound(shot.logicalRect.width() * shot.dpr), qRound(shot.logicalRect.height() * shot.dpr)));
    }

    // one device-pixel copy of the frozen frame: mosaic/blur and the magnifier read from here
    m_base = shot.pixmap.toImage();
    m_base.setDevicePixelRatio(1.0);

    const auto &s = Settings::instance();
    m_color = s.annotationColor;
    m_width = qBound(1, s.annotationWidth, 40);
    m_pinShortcut = s.chordFor("pin");
    m_pickClock.start();
}

SnipOverlay::~SnipOverlay() = default;

void SnipOverlay::pinSelection()
{
    handleAction(QStringLiteral("pin"));
}

void SnipOverlay::setPinShortcut(const QString &chord)
{
    m_pinShortcut = chord.isEmpty() ? Settings::instance().chordFor("pin") : chord;
    update();
}

QString SnipOverlay::actionShortcut(const QString &id) const
{
    return id == "pin" ? m_pinShortcut : SnipToolbar::shortcut(id);
}

bool SnipOverlay::handleOutputShortcut(QKeyEvent *event, bool textInput)
{
    // A plain printable custom key still types into an annotation's text field.
    if (textInput && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        && event->key() >= Qt::Key_Space && event->key() < Qt::Key_Escape)
        return false;
    const auto pressed = HotkeyText::fromQtKey(event->key(), event->modifiers());
    if (!pressed.valid()) return false;
    const auto matches = [&](const QString &chord) {
        const auto wanted = HotkeyText::parse(chord);
        return wanted.valid() && wanted.mods == pressed.mods && wanted.vk == pressed.vk;
    };
    if (matches(actionShortcut("pin")) || matches(QStringLiteral("Ctrl+P"))) {
        pinSelection();
        return true;
    }
    if (matches(actionShortcut("translate"))) {
        handleAction(QStringLiteral("translate"));
        return true;
    }
    return false;
}

void SnipOverlay::showEvent(QShowEvent *event)
{
    m_cursor = boundedPoint(QCursor::pos() - m_shot.logicalRect.topLeft());
    QWidget::showEvent(event);
}

void SnipOverlay::setPreselect(const QRect &r)
{
    m_sel = r.translated(-m_shot.logicalRect.topLeft()).intersected(rect());
    m_hasSel = m_sel.width() > 4 && m_sel.height() > 4;
    updateSelectionCursor();
    update();
}

QRect SnipOverlay::selection() const { return m_sel.normalized(); }

void SnipOverlay::setTool(Tool t)
{
    if (!m_hasSel) return;
    commitText();
    m_tool = t;
    updateSelectionCursor();
    if (t != Tool::Select) m_selected = -1;
    update();
}

// ---------------------------------------------------------------- painting

void SnipOverlay::paintEvent(QPaintEvent *)
{
    m_hot.clear();
    m_actions.clear();
    m_toolbarRect = {};
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(rect(), m_base);

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
    drawHoverTip(p);
    if (m_tool == Tool::Select) drawMagnifier(p);
    if (!m_error.isEmpty()) {
        QFont errorFont = font();
        errorFont.setPixelSize(12);
        p.setFont(errorFont);
        const int textFlags = Qt::AlignVCenter | Qt::TextWordWrap;
        const int errorHeight = qBound(40,
            QFontMetrics(errorFont).boundingRect(QRect(0, 0, width() - 32, height()), textFlags, m_error).height() + 16,
            height() - 16);
        QRect box(8, 8, qMax(20, width() - 16), errorHeight);
        if (box.intersects(m_toolbarRect)) {
            if (m_toolbarRect.bottom() + 8 + errorHeight < height() - 8)
                box.moveTop(m_toolbarRect.bottom() + 8);
            else if (m_toolbarRect.top() - 8 - errorHeight >= 8)
                box.moveTop(m_toolbarRect.top() - 8 - errorHeight);
        }
        // Paint last so a failed output always leaves a readable reason, even
        // when an unusually small viewport cannot fit both floating surfaces.
        p.setPen(Qt::white);
        p.setBrush(QColor(150, 35, 35, 235));
        p.drawRoundedRect(box, 5, 5);
        p.drawText(box.adjusted(8, 0, -8, 0), textFlags, m_error);
    }
}

QString SnipOverlay::hoverHint() const
{
    const auto label = SnipToolbar::label(m_hoverHot);
    const auto chord = actionShortcut(m_hoverHot);
    if (label.isEmpty()) return {};
    if (chord.isEmpty()) return label;
    return label + QStringLiteral("  ") + chord;
}

void SnipOverlay::refreshHover(const QPoint &pos)
{
    QString id;
    QRect r;
    for (const auto &h : m_hot) {
        if (h.rect.contains(pos)) {
            id = h.id;
            r = h.rect;
            break;
        }
    }
    if (id == m_hoverHot) return;
    m_hoverHot = id;
    m_hoverHotRect = r;
    update(); // repaint so the highlight + tooltip follow the cursor
}

void SnipOverlay::drawHoverTip(QPainter &p)
{
    if (m_hoverHot.isEmpty()) return;
    const auto label = SnipToolbar::label(m_hoverHot);
    const auto chord = actionShortcut(m_hoverHot);
    if (label.isEmpty()) return;

    QFont f = font();
    f.setPixelSize(13);
    const QFontMetrics fm(f);
    const int gap = chord.isEmpty() ? 0 : 12;
    const int textW = fm.horizontalAdvance(label) + (chord.isEmpty() ? 0 : gap + fm.horizontalAdvance(chord));
    const int w = textW + 24;
    const int h = fm.height() + 14;

    int x = m_hoverHotRect.center().x() - w / 2;
    int y = m_hoverHotRect.top() - h - 8;
    if (y < 4) y = m_hoverHotRect.bottom() + 8; // no room above: flip below the button
    x = qBound(4, x, qMax(4, width() - w - 4));
    y = qBound(4, y, qMax(4, height() - h - 4));

    // Paint the chip into an ARGB image: the raster engine uses grayscale antialiasing, so the
    // text stays clean instead of picking up ClearType colour fringes on the dark rounded chip.
    const qreal dpr = devicePixelRatioF();
    QImage img(QSize(w, h) * dpr, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    {
        QPainter ip(&img);
        ip.setRenderHint(QPainter::Antialiasing, true);
        ip.setRenderHint(QPainter::TextAntialiasing, true);
        ip.setFont(f);
        ip.setPen(QPen(QColor(255, 255, 255, 30), 1));
        ip.setBrush(QColor(0x23, 0x27, 0x2E, 242));
        ip.drawRoundedRect(QRectF(0.5, 0.5, w - 1.0, h - 1.0), 7, 7);
        const QRect inner(12, 0, w - 24, h);
        ip.setPen(QColor(0xF2, 0xF4, 0xF7));
        ip.drawText(inner, Qt::AlignLeft | Qt::AlignVCenter, label);
        if (!chord.isEmpty()) {
            ip.setPen(QColor(0xA9, 0xB3, 0xC1));
            ip.drawText(inner.adjusted(fm.horizontalAdvance(label) + gap, 0, 0, 0),
                Qt::AlignLeft | Qt::AlignVCenter, chord);
        }
    }
    p.drawImage(QPoint(x, y), img);
}
void SnipOverlay::drawSelection(QPainter &p)
{
    const QRect sel = selection();
    p.setPen(QPen(kAccent, 1.4));
    p.setBrush(Qt::NoBrush);
    p.drawRect(sel);

    p.setBrush(Qt::white);
    p.setPen(QPen(kAccent, 1));
    const QPoint pts[8] = { sel.topLeft(), sel.topRight(), sel.bottomLeft(), sel.bottomRight(),
        QPoint(sel.center().x(), sel.top()), QPoint(sel.center().x(), sel.bottom()),
        QPoint(sel.left(), sel.center().y()), QPoint(sel.right(), sel.center().y()) };
    for (const QPoint &c : pts)
        p.drawRect(QRect(c.x() - 3, c.y() - 3, 6, 6));

    // size label
    QFont f = font();
    f.setPixelSize(12);
    p.setFont(f);
    QString text = QStringLiteral("%1 × %2").arg(sel.width()).arg(sel.height());
    if (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
        text += QStringLiteral("   %1 %2px")
                    .arg(m_tool == Tool::Blur ? QStringLiteral("模糊") : QStringLiteral("马赛克"))
                    .arg(m_block);
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
        const QRect chip(
            m_hoverWindow.left() + 4, m_hoverWindow.top() + 4, fm.horizontalAdvance(title) + 14, 20);
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
    if (m_base.isNull() || !rect().contains(m_cursor) || m_toolbarRect.contains(m_cursor)
        || m_mode == Mode::Idle && m_hasSel)
        return;

    const int zoom = 8, box = 128;
    const QRect src(m_cursor.x() - box / zoom / 2, m_cursor.y() - box / zoom / 2, box / zoom, box / zoom);
    QRect dest(m_cursor.x() + 24, m_cursor.y() + 24, box, box);
    if (dest.right() > width() - 8) dest.moveLeft(m_cursor.x() - 24 - box);
    if (dest.bottom() > height() - 52) dest.moveTop(m_cursor.y() - 24 - box);

    p.drawPixmap(dest, m_shot.pixmap,
        QRectF(
            src.x() * m_shot.dpr, src.y() * m_shot.dpr, src.width() * m_shot.dpr, src.height() * m_shot.dpr));
    p.setPen(QPen(kAccent, 1.5));
    p.setBrush(Qt::NoBrush);
    p.drawRect(dest);
    p.drawLine(dest.center().x(), dest.top() + 6, dest.center().x(), dest.bottom() - 6);
    p.drawLine(dest.left() + 6, dest.center().y(), dest.right() - 6, dest.center().y());

    const QColor c = m_base.pixelColor(qBound(0, int(m_cursor.x() * m_shot.dpr), m_base.width() - 1),
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
    m_actions.clear();
    m_toolbarRect = {};
    if (!m_hasSel || m_mode != Mode::Idle) return;
    const QStringList tools { "select", "rect", "ellipse", "arrow", "line", "pen", "marker", "text", "number",
        "mosaic", "blur", "eraser" };
    const int active = int(m_tool);
    const QStringList enumTools { "select", "rect", "ellipse", "arrow", "line", "pen", "marker", "text",
        "number", "mosaic", "blur", "eraser" };
    auto add = [&](const QString &id, int group, bool enabled = true) {
        m_actions.append({ id, {}, enabled, SnipToolbar::label(id), actionShortcut(id), group });
    };
    for (const auto &id : tools)
        add("tool:" + id, 0);
    add("undo", 1, m_history.canUndo());
    add("redo", 1, m_history.canRedo());
    for (const auto &id : QStringList { "pin", "ocr", "translate", "save", "more", "copy", "close" })
        add(id, 2);
    const bool text = m_tool == Tool::Text;
    const bool pixels = m_tool == Tool::Mosaic || m_tool == Tool::Blur;
    const bool colorTool = (!pixels && m_tool != Tool::Eraser && m_tool != Tool::Select) || m_selected >= 0;
    if (colorTool) {
        for (int i = 0; i < kSwatchCount; ++i)
            add(QStringLiteral("swatch:%1").arg(i), 3);
        add("custom", 3);
    }
    if (text) {
        add("text-background", 3);
        add("font-", 4, m_fontSize > 10);
        add("font-value", 4, false);
        add("font+", 4, m_fontSize < 96);
    } else if (m_tool != Tool::Select && m_tool != Tool::Eraser) {
        add(pixels ? "block-" : "width-", 4);
        add(pixels ? "block+" : "width+", 4);
    }
    m_toolbarRect = SnipToolbar::layout(m_actions, rect(), selection());
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(kBarEdge, 1));
    p.setBrush(kBar);
    p.drawRoundedRect(m_toolbarRect, 9, 9);
    int previous = -1;
    for (const auto &a : m_actions) {
        const QRect r = a.rect;
        m_hot.append({ r, a.id });
        if (previous >= 0 && a.group != previous && r.left() > m_toolbarRect.left() + 8) {
            p.setPen(QColor(0, 0, 0, 22));
            p.drawLine(r.left() - 2, r.top() + 7, r.left() - 2, r.bottom() - 7);
        }
        previous = a.group;
        const bool selected = a.id == "tool:" + enumTools.at(active)
            || (a.id == "text-background" && m_textBackground);
        p.setPen(Qt::NoPen);
        if (a.id == "copy" || selected || a.id == m_hoverHot) {
            p.setBrush(a.id == "copy" ? kToolAccent
                    : selected        ? QColor(47, 111, 235, 30)
                                      : QColor(47, 111, 235, 15));
            p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 6, 6);
        }
        QColor fg = !a.enabled ? QColor(150, 155, 164)
            : a.id == "copy"   ? QColor(Qt::white)
            : selected         ? kToolAccent
            : a.id == "close"  ? kDanger
                               : kFg;
        if (a.id.startsWith("swatch:")) {
            const QColor c = swatches[a.id.mid(7).toInt()];
            p.setPen(QPen(c == m_color ? kToolAccent : QColor(0, 0, 0, 45), 2));
            p.setBrush(c);
            p.drawEllipse(r.adjusted(5, 5, -5, -5));
        } else if (a.id == "custom") {
            p.setBrush(m_color);
            p.setPen(QPen(kFg, 1));
            p.drawRoundedRect(r.adjusted(6, 6, -6, -6), 3, 3);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, "+");
        } else if (a.id == "text-background") {
            p.setPen(QPen(fg, 1.4));
            p.setBrush(Qt::NoBrush);
            const QRect inset = r.adjusted(7, 7, -7, -7);
            p.drawRoundedRect(inset, 2, 2);
            p.drawText(inset, Qt::AlignCenter, "A");
            if (!m_textBackground) p.drawLine(inset.bottomLeft(), inset.topRight());
        } else if (a.group == 4) {
            p.setPen(fg);
            QFont f = font();
            f.setPixelSize(11);
            p.setFont(f);
            if (text) {
                if (a.id == "font-value") p.setPen(kFg);
                p.drawText(r, Qt::AlignCenter, a.id == "font-value" ? QString::number(m_fontSize)
                    : a.id.endsWith('+') ? QStringLiteral("+") : QStringLiteral("−"));
            } else {
                const int value = pixels ? m_block : m_width;
                p.drawText(r, Qt::AlignCenter, QString(a.id.endsWith('+') ? "+%1" : "−%1").arg(value));
            }
        } else if (a.id == "more") {
            p.setPen(fg);
            p.drawText(r, Qt::AlignCenter, QStringLiteral("•••"));
        } else {
            IconSet::draw(
                p, a.id.startsWith("tool:") ? a.id.mid(5) : a.id, QRectF(r.adjusted(6, 6, -6, -6)), fg);
        }
    }
    p.restore();
}
// ---------------------------------------------------------------- input

void SnipOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        close();
        return;
    }
    if (e->button() != Qt::LeftButton || m_completed) return;
    m_cursor = e->pos();
    m_gestureRecorded = false;

    if (m_hasSel) {
        const QString id = hitToolbar(e->pos());
        if (!id.isEmpty()) {
            handleAction(id);
            return;
        }
        // Disabled controls and padding still own their input; do not move a
        // selection underneath the floating bar when a control is unavailable.
        if (m_toolbarRect.contains(e->pos())) return;
    }

    if (!m_hasSel) {
        // clicking while the cursor is over a window snaps to that window instead of rubber-banding
        m_mode = Mode::SelectRect;
        updateSelectionCursor();
        m_dragStart = e->pos();
        m_dragLast = e->pos();
        m_dragMoved = false;
        m_sel = QRect(e->pos(), QSize());
        update();
        return;
    }

    if (m_tool != Tool::Select && !selection().contains(e->pos())) return;
    commitText();
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
        if (m_sel.contains(e->pos())) {
            m_mode = Mode::MoveSelection;
            m_selAtPress = m_sel;
            m_dragStart = e->pos();
            update();
            return;
        }
        m_mode = Mode::SelectRect;
        m_dragStart = e->pos();
        m_dragLast = e->pos();
        m_dragMoved = false;
        m_sel = QRect(e->pos(), QSize());
        m_hasSel = false;
        updateSelectionCursor();
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
        recordMutation();
        m_items.append(a);
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
        m_current.points = { e->pos() };
        m_current.block = m_block;
        switch (m_tool) {
        case Tool::Rect:
            m_current.kind = Annotation::Rect;
            break;
        case Tool::Ellipse:
            m_current.kind = Annotation::Ellipse;
            break;
        case Tool::Arrow:
            m_current.kind = Annotation::Arrow;
            break;
        case Tool::Line:
            m_current.kind = Annotation::Line;
            break;
        case Tool::Pen:
            m_current.kind = Annotation::Pen;
            break;
        case Tool::Highlighter:
            m_current.kind = Annotation::Highlighter;
            break;
        case Tool::Mosaic:
            m_current.kind = Annotation::Pixelate;
            m_current.blur = false;
            break;
        case Tool::Blur:
            m_current.kind = Annotation::Pixelate;
            m_current.blur = true;
            break;
        default:
            break;
        }
        update();
        break;
    }
    }
}

void SnipOverlay::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->pos();
    if (m_mode == Mode::Idle) refreshHover(e->pos());

    switch (m_mode) {
    case Mode::SelectRect:
        m_sel = QRect(m_dragStart, boundedPoint(e->pos())).normalized().intersected(rect());
        if ((e->pos() - m_dragStart).manhattanLength() > 3) m_dragMoved = true;
        update();
        return;
    case Mode::MoveSelection: {
        QRect moved = m_selAtPress.translated(e->pos() - m_dragStart);
        moved.moveLeft(qBound(0, moved.left(), width() - moved.width()));
        moved.moveTop(qBound(0, moved.top(), height() - moved.height()));
        m_sel = moved;
        update();
        return;
    }
    case Mode::MoveAnnotation:
        if (m_selected >= 0 && m_selected < m_items.size()) {
            QPoint delta = boundedPoint(e->pos()) - m_dragLast;
            const QRect b = m_items[m_selected].bounds();
            delta.setX(qBound(-b.left(), delta.x(), qMax(-b.left(), width() - 1 - b.right())));
            delta.setY(qBound(-b.top(), delta.y(), qMax(-b.top(), height() - 1 - b.bottom())));
            if (!delta.isNull()) {
                recordMutation();
                m_items[m_selected].translate(delta);
            }
            m_dragLast = boundedPoint(e->pos());
            update();
        }
        return;
    case Mode::ResizeSelection: {
        QRect r = m_selAtPress;
        const QPoint d = e->pos() - m_dragStart;
        switch (m_resizeHandle) {
        case 0:
            r.setTopLeft(r.topLeft() + d);
            break;
        case 1:
            r.setTopRight(r.topRight() + d);
            break;
        case 2:
            r.setBottomLeft(r.bottomLeft() + d);
            break;
        case 3:
            r.setBottomRight(r.bottomRight() + d);
            break;
        case 4:
            r.setTop(r.top() + d.y());
            break;
        case 5:
            r.setBottom(r.bottom() + d.y());
            break;
        case 6:
            r.setLeft(r.left() + d.x());
            break;
        case 7:
            r.setRight(r.right() + d.x());
            break;
        default:
            break;
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
                m_current.rect = QRect(m_current.points.first(), boundedPoint(e->pos())).normalized();
                break;
            case Annotation::Arrow:
            case Annotation::Line:
                m_current.points = { m_current.points.first(), boundedPoint(e->pos()) };
                break;
            default:
                m_current.points.append(boundedPoint(e->pos()));
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

void SnipOverlay::updateSelectionCursor()
{
    const bool selecting = !m_hasSel || m_mode == Mode::SelectRect;
    if (selecting) setCursor(Qt::CrossCursor);
    else unsetCursor();
}

void SnipOverlay::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    if (m_mode != Mode::Idle && m_mode != Mode::SelectRect) mouseMoveEvent(e);

    if (m_mode == Mode::SelectRect) {
        const QRect r = QRect(m_dragStart, boundedPoint(e->pos())).normalized().intersected(rect());
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
        updateSelectionCursor();
        update();
        return;
    }
    if (m_mode == Mode::Draw && m_drawing) {
        m_drawing = false;
        recordMutation();
        m_items.append(m_current);
    }
    m_mode = Mode::Idle;
    update();
}

void SnipOverlay::wheelEvent(QWheelEvent *e)
{
    if (!e->angleDelta().y()) return;
    const int dir = e->angleDelta().y() > 0 ? 1 : -1;
    if (m_tool == Tool::Text) {
        m_fontSize = qBound(10, m_fontSize + dir * 2, 96);
        updateTextEditor(m_color);
    } else if (m_tool == Tool::Mosaic || m_tool == Tool::Blur)
        m_block = qBound(4, m_block + dir * 2, 80);
    else
        m_width = qBound(1, m_width + dir, 40);
    update();
}

void SnipOverlay::keyPressEvent(QKeyEvent *e)
{
    const bool editingText = m_textEdit && m_textEdit->hasFocus();
    if (handleOutputShortcut(e, editingText)) return;
    if (editingText) {
        QWidget::keyPressEvent(e);
        return;
    }

    switch (e->key()) {
    case Qt::Key_Escape:
        close();
        return;
    case Qt::Key_V:
        setTool(Tool::Select);
        return;
    case Qt::Key_R:
        if (e->modifiers() & Qt::ControlModifier) {
            handleAction(QStringLiteral("ocr"));
            return;
        }
        setTool(Tool::Rect);
        return;
    case Qt::Key_O:
        setTool(Tool::Ellipse);
        return;
    case Qt::Key_A:
        setTool(Tool::Arrow);
        return;
    case Qt::Key_L:
        setTool(Tool::Line);
        return;
    case Qt::Key_P:
        setTool(Tool::Pen);
        return;
    case Qt::Key_H:
        setTool(Tool::Highlighter);
        return;
    case Qt::Key_T:
        setTool(Tool::Text);
        return;
    case Qt::Key_N:
        setTool(Tool::Number);
        return;
    case Qt::Key_M:
        setTool(Tool::Mosaic);
        return;
    case Qt::Key_B:
        setTool(Tool::Blur);
        return;
    case Qt::Key_E:
        setTool(Tool::Eraser);
        return;
    case Qt::Key_Space:
        m_sel = rect();
        m_hasSel = true;
        updateSelectionCursor();
        update();
        return;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        handleAction(QStringLiteral("copy"));
        return;
    case Qt::Key_Delete:
    case Qt::Key_Backspace:
        if (m_selected >= 0 && m_selected < m_items.size()) {
            m_gestureRecorded = false;
            recordMutation();
            m_items.removeAt(m_selected);
            m_selected = -1;
            update();
        }
        return;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->modifiers() & Qt::ShiftModifier)
                redo();
            else
                undo();
        }
        return;
    case Qt::Key_Y:
        if (e->modifiers() & Qt::ControlModifier) redo();
        return;
    case Qt::Key_S:
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->modifiers() & Qt::ShiftModifier)
                saveQuick();
            else
                handleAction(QStringLiteral("save"));
        }
        return;
    case Qt::Key_Left:
        nudgeSelection(e->modifiers(), QPoint(-1, 0));
        return;
    case Qt::Key_Right:
        nudgeSelection(e->modifiers(), QPoint(1, 0));
        return;
    case Qt::Key_Up:
        nudgeSelection(e->modifiers(), QPoint(0, -1));
        return;
    case Qt::Key_Down:
        nudgeSelection(e->modifiers(), QPoint(0, 1));
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void SnipOverlay::nudgeSelection(Qt::KeyboardModifiers mods, const QPoint &delta)
{
    if (!m_hasSel) return;
    QRect r = m_sel;
    if (mods & Qt::ShiftModifier)
        r.adjust(0, 0, delta.x(), delta.y()); // 外扩
    else if (mods & Qt::AltModifier)
        r.adjust(0, 0, -delta.x(), -delta.y()); // 收窄
    else
        r.translate(delta);
    if (r.width() < 8 || r.height() < 8) return;
    m_sel = r.intersected(rect());
    m_hasSel = true;
    update();
}

// ---------------------------------------------------------------- helpers

QString SnipOverlay::hitToolbar(const QPoint &pos) const
{
    for (const auto &h : m_hot)
        if (h.rect.contains(pos)) {
            for (const auto &a : m_actions)
                if (a.id == h.id && !a.enabled) return {};
            return h.id;
        }
    return {};
}

int SnipOverlay::selectionHandleAt(const QPoint &pos) const
{
    if (!m_hasSel) return -1;
    const QRect s = selection();
    const QPoint pts[8] = { s.topLeft(), s.topRight(), s.bottomLeft(), s.bottomRight(),
        QPoint(s.center().x(), s.top()), QPoint(s.center().x(), s.bottom()), QPoint(s.left(), s.center().y()),
        QPoint(s.right(), s.center().y()) };
    for (int i = 0; i < 8; ++i)
        if ((pos - pts[i]).manhattanLength() <= 8) return i;
    return -1;
}

void SnipOverlay::updateHoverWindow(const QPoint &pos)
{
    if (m_pickClock.isValid() && m_pickClock.elapsed() < 40 && (pos - m_lastPickPos).manhattanLength() < 4)
        return;
    m_pickClock.restart();
    m_lastPickPos = pos;

    const int physX = m_monitor.x() + qRound(pos.x() * m_shot.dpr);
    const int physY = m_monitor.y() + qRound(pos.y() * m_shot.dpr);
    const WindowPicker::Hit hit = WindowPicker::atPhysicalPoint(physX, physY);
    if (!hit.valid) {
        if (m_hoverWindow.isValid()) {
            m_hoverWindow = QRect();
            m_hoverTitle.clear();
        }
        return;
    }

    QRect logical(qRound((hit.physicalRect.x() - m_monitor.x()) / m_shot.dpr),
        qRound((hit.physicalRect.y() - m_monitor.y()) / m_shot.dpr),
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
            recordMutation();
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
    if (m_completed) return;
    if (id == "close") {
        cancelText();
        close();
        return;
    }
    if (!m_hasSel) return;
    const bool changesTextStyle = id == "custom" || id == "text-background"
        || id.startsWith("font") || id.startsWith("swatch:");
    if (!changesTextStyle) commitText();
    if (id == "more") {
        QMenu menu(this);
        QAction *quick = menu.addAction(SnipToolbar::label("quick") + "\tCtrl+Shift+S");
        QAction *print = menu.addAction(SnipToolbar::label("print"));
        QAction *chosen = menu.exec(mapToGlobal(m_toolbarRect.bottomLeft()));
        if (chosen == quick)
            saveQuick();
        else if (chosen == print)
            handleAction("print");
        return;
    }
    if (id == "custom") {
        QColorDialog dialog(m_color, this);
        dialog.setWindowTitle(QStringLiteral("自选颜色"));
        connect(&dialog, &QColorDialog::currentColorChanged, this, &SnipOverlay::updateTextEditor);
        const QColor c = dialog.exec() == QDialog::Accepted ? dialog.selectedColor() : QColor();
        if (c.isValid()) {
            if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].color != c) {
                m_gestureRecorded = false;
                recordMutation();
                m_items[m_selected].color = c;
            }
            m_color = c;
            update();
        }
        updateTextEditor(m_color);
        if (m_textEdit) m_textEdit->setFocus();
        return;
    }
    if (id == "text-background") {
        m_textBackground = !m_textBackground;
        updateTextEditor(m_color);
        if (m_textEdit) m_textEdit->setFocus();
        update();
        return;
    }
    if (id.startsWith("font") || id.startsWith("block")) {
        const int d = id.endsWith('+') ? 2 : -2;
        if (id == "font-value") return;
        if (id.startsWith("font")) {
            m_fontSize = qBound(10, m_fontSize + d, 96);
            updateTextEditor(m_color);
            if (m_textEdit) m_textEdit->setFocus();
        } else
            m_block = qBound(4, m_block + d, 80);
        update();
        return;
    }
    if (id.startsWith(QStringLiteral("tool:"))) {
        const QString t = id.mid(5);
        if (t == QStringLiteral("select"))
            setTool(Tool::Select);
        else if (t == QStringLiteral("rect"))
            setTool(Tool::Rect);
        else if (t == QStringLiteral("ellipse"))
            setTool(Tool::Ellipse);
        else if (t == QStringLiteral("arrow"))
            setTool(Tool::Arrow);
        else if (t == QStringLiteral("line"))
            setTool(Tool::Line);
        else if (t == QStringLiteral("pen"))
            setTool(Tool::Pen);
        else if (t == QStringLiteral("marker"))
            setTool(Tool::Highlighter);
        else if (t == QStringLiteral("text"))
            setTool(Tool::Text);
        else if (t == QStringLiteral("mosaic"))
            setTool(Tool::Mosaic);
        else if (t == QStringLiteral("blur"))
            setTool(Tool::Blur);
        else if (t == QStringLiteral("number"))
            setTool(Tool::Number);
        else if (t == QStringLiteral("eraser"))
            setTool(Tool::Eraser);
        return;
    }
    if (id.startsWith(QStringLiteral("swatch:"))) {
        const QColor c = swatches[id.mid(7).toInt()];
        if (m_selected >= 0 && m_selected < m_items.size() && m_items[m_selected].color != c) {
            m_gestureRecorded = false;
            recordMutation();
            m_items[m_selected].color = c;
        }
        m_color = c;
        updateTextEditor(m_color);
        if (m_textEdit) m_textEdit->setFocus();
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
        m_completed = true;
        emit captured(img, selection().translated(m_shot.logicalRect.topLeft()));
        emit copied(img);
        close();
        return;
    }
    if (id == QStringLiteral("save")) {
        const QImage img = compose();
        if (img.isNull()) return;
        const QString def = Settings::instance().resolveAutoSaveDir() + QStringLiteral("/")
            + FileNaming::expand(Settings::instance().namePattern, QDateTime::currentDateTime());
        const QString path = QFileDialog::getSaveFileName(
            this, QStringLiteral("保存截图"), def, QStringLiteral("PNG 图片 (*.png)"));
        if (path.isEmpty()) return;
        saveToPath(path);
        return;
    }
    if (id == QStringLiteral("pin")) {
        const QImage img = compose();
        if (img.isNull()) return;
        m_completed = true;
        emit captured(img, selection().translated(m_shot.logicalRect.topLeft()));
        emit pinned(img);
        close();
        return;
    }
    if (id == QStringLiteral("translate")) {
        const QImage img=compose();if(img.isNull())return;
        m_completed=true;
        emit translationRequested(img,selection().translated(m_shot.logicalRect.topLeft()));
        close();return;
    }
    if (id == QStringLiteral("ocr")) {
        const QImage img = compose();
        if (img.isNull()) return;
        m_completed = true;
        emit captured(img, selection().translated(m_shot.logicalRect.topLeft()));
        emit recognizeRequested(img);
        close();
        return;
    }
    if (id == QStringLiteral("print")) {
        const QImage img = compose();
        if (img.isNull()) return;
        m_completed = true;
        emit captured(img, selection().translated(m_shot.logicalRect.topLeft()));
        emit printed(img);
        close();
        return;
    }
}

void SnipOverlay::saveQuick()
{
    commitText();
    const auto &s = Settings::instance();
    saveToPath(FileNaming::uniquePath(s.resolveAutoSaveDir(), s.namePattern, QDateTime::currentDateTime()));
}

bool SnipOverlay::saveToPath(const QString &path)
{
    if (m_completed) return false;
    commitText();
    const QImage img = compose();
    if (img.isNull()) return false;
    if (!img.save(path, "PNG")) {
        m_error
            = QStringLiteral("无法保存截图：%1。请检查目录与写入权限。").arg(QDir::toNativeSeparators(path));
        update();
        return false;
    }
    m_completed = true;
    emit captured(img, selection().translated(m_shot.logicalRect.topLeft()));
    QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
    emit saved(path);
    close();
    return true;
}
QImage SnipOverlay::compose() const
{
    const QRect sel = selection();
    if (!m_hasSel || sel.width() < 2 || sel.height() < 2) return {};

    const qreal dpr = m_shot.dpr;
    const QRect src(
        qRound(sel.x() * dpr), qRound(sel.y() * dpr), qRound(sel.width() * dpr), qRound(sel.height() * dpr));

    QImage out(src.size(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(QRect(0, 0, src.width(), src.height()), m_base, src);
        p.scale(dpr, dpr);
        p.translate(-sel.topLeft());
        for (const auto &a : m_items)
            a.paint(p, &m_base, dpr);
    }
    out.setDevicePixelRatio(1.0);
    return out;
}

void SnipOverlay::undo()
{
    commitText();
    m_history.undo(m_items);
    m_selected = -1;
    update();
}
void SnipOverlay::redo()
{
    m_history.redo(m_items);
    m_selected = -1;
    update();
}
void SnipOverlay::recordMutation()
{
    if (m_gestureRecorded) return;
    m_history.record(m_items);
    m_gestureRecorded = true;
}
QPoint SnipOverlay::boundedPoint(const QPoint &p) const
{
    const QRect r = m_hasSel ? selection().intersected(rect()) : rect();
    return { qBound(r.left(), p.x(), r.right()), qBound(r.top(), p.y(), r.bottom()) };
}
void SnipOverlay::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton && m_hasSel && selection().contains(e->pos())
        && !m_toolbarRect.contains(e->pos())) {
        m_mode = Mode::Idle;
        handleAction("copy");
    }
}
void SnipOverlay::beginText(const QPoint &at)
{
    commitText();
    m_textAnchor = at;
    m_textEdit = new QLineEdit(this);
    m_textEdit->setCursor(cursor());
    updateTextEditor(m_color);
    m_textEdit->installEventFilter(this);
    m_textEdit->show();
    m_textEdit->setFocus();
}

void SnipOverlay::updateTextEditor(const QColor &color)
{
    if (!m_textEdit) return;
    QFont f = font();
    f.setPixelSize(m_fontSize);
    m_textEdit->setFont(f);
    const QRect area = selection();
    const int boxHeight = QFontMetrics(f).height() + 8;
    const int y = qBound(area.top(), m_textAnchor.y(), qMax(area.top(), area.bottom() - boxHeight + 1));
    m_textEdit->setGeometry(m_textAnchor.x(), y, qMin(420, area.right() - m_textAnchor.x() + 1),
        qMin(boxHeight, area.height()));
    m_textEdit->setStyleSheet(QStringLiteral("QLineEdit { background: %1; color: %2; font-size: %3px; border: 1px solid #4C9AFF; padding: 0px; }")
        .arg(m_textBackground ? QStringLiteral("rgba(16,20,26,200)") : QStringLiteral("transparent"), color.name())
        .arg(m_fontSize));
}

bool SnipOverlay::eventFilter(QObject *obj, QEvent *e)
{
    if (obj == m_textEdit && e->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(e);
        if (handleOutputShortcut(key, true)) return true;
        if (key->key() == Qt::Key_Escape) {
            cancelText();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            commitText();
            return true;
        }
    }
    if (obj == m_textEdit && e->type() == QEvent::Wheel) {
        wheelEvent(static_cast<QWheelEvent *>(e));
        return true;
    }
    return QWidget::eventFilter(obj, e);
}

void SnipOverlay::commitText()
{
    if (!m_textEdit) return;
    QLineEdit *edit = m_textEdit;
    m_textEdit = nullptr;
    const QString text = edit->text().trimmed();
    const QFontMetrics metrics(edit->font());
    const QPoint baseline(edit->x() + 2, edit->y() + (edit->height() - metrics.height()) / 2 + metrics.ascent());
    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();
    setFocus();
    if (text.isEmpty()) return;

    Annotation a;
    a.kind = Annotation::Text;
    a.color = m_color;
    a.text = text;
    a.pos = baseline;
    a.fontSize = m_fontSize;
    a.textBackground = m_textBackground;
    m_gestureRecorded = false;
    recordMutation();
    m_items.append(a);
    update();
}

void SnipOverlay::cancelText()
{
    if (!m_textEdit) return;
    QLineEdit *edit = m_textEdit;
    m_textEdit = nullptr;
    edit->removeEventFilter(this);
    edit->hide();
    edit->deleteLater();
    setFocus();
    update();
}
