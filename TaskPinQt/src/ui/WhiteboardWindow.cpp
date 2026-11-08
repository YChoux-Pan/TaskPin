#include "ui/WhiteboardWindow.h"

#include "core/FileNaming.h"
#include "core/Settings.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

namespace {
constexpr int kBtn = 32;
constexpr int kGap = 4;
constexpr int kSwatch = 22;
constexpr int kPad = 8;
constexpr int kSwatchCount = 7;

const QColor kBar(20, 25, 32, 240);
const QColor kFg(0xE6, 0xEC, 0xF3);
const QColor kAccent(0x4C, 0x9A, 0xFF);

QColor swatches[kSwatchCount] = {
    QColor(0xE5, 0x3B, 0x3B), QColor(0xFF, 0xB0, 0x20), QColor(0x3E, 0xC4, 0x6B),
    QColor(0x3B, 0x9C, 0xFF), QColor(0xB0, 0x6B, 0xFF), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x10, 0x14, 0x1A),
};
}   // namespace

WhiteboardWindow::WhiteboardWindow(const ScreenCapture &shot, bool transparent, QWidget *parent)
    : QWidget(parent), m_shot(shot), m_transparent(transparent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    if (m_transparent) setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(shot.logicalRect);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("TaskPin 白板"));

    m_base = shot.pixmap.toImage();
    m_base.setDevicePixelRatio(1.0);
}

// ---------------------------------------------------------------- painting

void WhiteboardWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (m_transparent) {
        // alpha=1 instead of 0: a layered window whose pixels are fully transparent would let the
        // clicks fall through to the desktop, and then you could never draw (see HANDOVER 3.3).
        p.fillRect(rect(), QColor(0, 0, 0, 1));
    } else {
        p.drawPixmap(0, 0, m_shot.pixmap);
    }

    for (const auto &stroke : m_strokes) stroke.paint(p, &m_base, m_shot.dpr);
    if (m_drawing) m_current.paint(p, &m_base, m_shot.dpr);
    drawToolbar(p);
}

void WhiteboardWindow::drawToolbar(QPainter &p)
{
    m_hot.clear();

    const QStringList actionIds{QStringLiteral("undo"), QStringLiteral("clear"), QStringLiteral("copy"),
                                QStringLiteral("save"), QStringLiteral("print"), QStringLiteral("close")};
    const int barWidth = kSwatchCount * (kSwatch + kGap) - kGap + 10 + (2 * 28 + kGap) + 10 +
                         actionIds.size() * (kBtn + kGap) - kGap + kPad * 2;
    const QRect bar((QWidget::width() - barWidth) / 2, height() - 48 - 44, barWidth, kBtn + kPad * 2);

    p.setPen(Qt::NoPen);
    p.setBrush(kBar);
    p.drawRoundedRect(bar, 10, 10);

    int cx = bar.left() + kPad;
    const int cy = bar.top() + kPad;
    for (int i = 0; i < kSwatchCount; ++i) {
        const QRect r(cx, cy + (kBtn - kSwatch) / 2, kSwatch, kSwatch);
        m_hot.append({r, QStringLiteral("swatch:%1").arg(i)});
        p.setPen(QPen(m_color == swatches[i] ? Qt::white : QColor(255, 255, 255, 60), 2));
        p.setBrush(swatches[i]);
        p.drawEllipse(r);
        cx += kSwatch + kGap;
    }
    cx += 10;

    for (const QString &id : {QStringLiteral("width-"), QStringLiteral("width+")}) {
        const QRect r(cx, cy, 28, kBtn);
        m_hot.append({r, id});
        p.setPen(QPen(kFg, 1.8, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(r.center() + QPoint(-6, 0), r.center() + QPoint(6, 0));
        if (id == QStringLiteral("width+")) p.drawLine(r.center() + QPoint(0, -6), r.center() + QPoint(0, 6));
        cx += 28 + kGap;
    }
    cx += 10;

    for (const QString &id : actionIds) {
        const QRect r(cx, cy, kBtn, kBtn);
        m_hot.append({r, id});
        p.setPen(QPen(id == QStringLiteral("close") ? QColor(0xFF, 0x8A, 0x7A) : kFg, 1.6, Qt::SolidLine,
                      Qt::RoundCap));
        p.setBrush(Qt::NoBrush);
        const QPoint c = r.center();
        if (id == QStringLiteral("undo")) {
            QPainterPath path;
            path.arcMoveTo(QRectF(c.x() - 7, c.y() - 6, 14, 12), 20);
            path.arcTo(QRectF(c.x() - 7, c.y() - 6, 14, 12), 20, 140);
            p.drawPath(path);
            p.drawLine(c + QPoint(-7, -6), c + QPoint(-3, -1));
        } else if (id == QStringLiteral("clear")) {
            p.drawLine(c + QPoint(-6, -6), c + QPoint(6, 6));
            p.drawLine(c + QPoint(6, -6), c + QPoint(-6, 6));
            p.drawRect(QRect(c.x() - 7, c.y() + 4, 14, 4));
        } else if (id == QStringLiteral("copy")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 9, 9));
            p.drawRect(QRect(c.x() - 2, c.y() - 2, 9, 9));
        } else if (id == QStringLiteral("save")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 12, 12));
            p.setBrush(kFg);
            p.drawRect(QRect(c.x() - 3, c.y() - 6, 6, 4));
            p.setBrush(Qt::NoBrush);
        } else if (id == QStringLiteral("print")) {
            p.drawRect(QRect(c.x() - 6, c.y() - 6, 12, 6));
            p.drawRect(QRect(c.x() - 6, c.y(), 12, 6));
            p.drawRect(QRect(c.x() - 3, c.y() + 4, 6, 4));
        } else {
            p.drawLine(c + QPoint(-6, -6), c + QPoint(6, 6));
            p.drawLine(c + QPoint(6, -6), c + QPoint(-6, 6));
        }
        cx += kBtn + kGap;
    }

    m_toolbarRect = bar;

    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    const QString hint = QStringLiteral("手写 · 滚轮调粗细 · Ctrl+Z 撤销 · Enter 复制 · Esc 退出");
    p.setPen(QColor(0x9A, 0xA5, 0xB4));
    p.drawText(QRect(bar.left(), bar.top() - 22, bar.width(), 18), Qt::AlignCenter, hint);
}

// ---------------------------------------------------------------- input

void WhiteboardWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::RightButton) {
        close();
        return;
    }
    if (e->button() != Qt::LeftButton) return;

    const QString id = hitToolbar(e->pos());
    if (!id.isEmpty()) {
        handleAction(id);
        return;
    }

    m_drawing = true;
    m_current = Annotation();
    m_current.kind = Annotation::Pen;
    m_current.color = m_color;
    m_current.width = m_width;
    m_current.points = {e->pos()};
    update();
}

void WhiteboardWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drawing) {
        m_current.points.append(e->pos());
        update();
    }
}

void WhiteboardWindow::mouseReleaseEvent(QMouseEvent *e)
{
    if (!m_drawing) return;
    m_drawing = false;
    if (m_current.points.size() > 1) {
        m_strokes.append(m_current);
        m_redo.clear();
    }
    m_current = Annotation();
    update();
    Q_UNUSED(e)
}

void WhiteboardWindow::wheelEvent(QWheelEvent *e)
{
    m_width = qBound(1, m_width + (e->angleDelta().y() > 0 ? 1 : -1), 40);
    update();
}

void WhiteboardWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Escape: close(); return;
    case Qt::Key_Return:
    case Qt::Key_Enter: handleAction(QStringLiteral("copy")); return;
    case Qt::Key_Z:
        if (e->modifiers() & Qt::ControlModifier) undo();
        return;
    case Qt::Key_S:
        if (e->modifiers() & Qt::ControlModifier) handleAction(QStringLiteral("save"));
        return;
    case Qt::Key_P:
        if (e->modifiers() & Qt::ControlModifier) handleAction(QStringLiteral("print"));
        return;
    case Qt::Key_1: m_color = swatches[0]; update(); return;
    case Qt::Key_2: m_color = swatches[1]; update(); return;
    case Qt::Key_3: m_color = swatches[2]; update(); return;
    case Qt::Key_4: m_color = swatches[3]; update(); return;
    case Qt::Key_5: m_color = swatches[4]; update(); return;
    case Qt::Key_6: m_color = swatches[5]; update(); return;
    case Qt::Key_7: m_color = swatches[6]; update(); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

// ---------------------------------------------------------------- actions

QString WhiteboardWindow::hitToolbar(const QPoint &pos) const
{
    for (const auto &h : m_hot)
        if (h.rect.contains(pos)) return h.id;
    return {};
}

void WhiteboardWindow::handleAction(const QString &id)
{
    if (id.startsWith(QStringLiteral("swatch:"))) {
        m_color = swatches[id.mid(7).toInt()];
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
    if (id == QStringLiteral("clear")) {
        clearAll();
        return;
    }
    if (id == QStringLiteral("close")) {
        close();
        return;
    }
    if (id == QStringLiteral("copy")) {
        const QImage image = compose();
        if (image.isNull()) return;
        QApplication::clipboard()->setImage(image);
        emit copied(image);
        close();
        return;
    }
    if (id == QStringLiteral("save")) {
        const QImage image = compose();
        if (image.isNull()) return;
        const auto &s = Settings::instance();
        const QString def = s.resolveAutoSaveDir() + QStringLiteral("/") +
                            FileNaming::expand(s.namePattern, QDateTime::currentDateTime());
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存白板"), def,
                                                          QStringLiteral("PNG 图片 (*.png)"));
        if (path.isEmpty()) return;
        if (!image.save(path, "PNG")) {
            qWarning() << "whiteboard save failed:" << path;
            return;
        }
        emit saved(path);
        close();
        return;
    }
    if (id == QStringLiteral("print")) {
        const QImage image = compose();
        if (image.isNull()) return;
        emit printRequested(image);
        close();
        return;
    }
}

void WhiteboardWindow::undo()
{
    if (m_strokes.isEmpty()) return;
    m_redo.append(m_strokes.takeLast());
    update();
}

void WhiteboardWindow::clearAll()
{
    m_redo = m_strokes;
    m_strokes.clear();
    update();
}

QImage WhiteboardWindow::compose() const
{
    const qreal dpr = m_shot.dpr;
    const QSize size(qMax(1, qRound(width() * dpr)), qMax(1, qRound(height() * dpr)));
    QImage out(size, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing, true);
    if (!m_transparent && !m_shot.pixmap.isNull()) {
        p.drawPixmap(QRect(QPoint(0, 0), size),
                     m_shot.pixmap, QRectF(0, 0, m_shot.pixmap.width(), m_shot.pixmap.height()));
    }
    p.scale(dpr, dpr);
    for (const auto &stroke : m_strokes) stroke.paint(p, &m_base, dpr);
    out.setDevicePixelRatio(1.0);
    return out;
}
