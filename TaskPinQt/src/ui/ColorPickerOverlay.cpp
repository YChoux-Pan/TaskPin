#include "ui/ColorPickerOverlay.h"

#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace {
constexpr int kMagnifier = 152;   // magnifier box in logical pixels

QString hexOf(const QColor &c)
{
    return QStringLiteral("#%1%2%3")
        .arg(c.red(), 2, 16, QChar('0'))
        .arg(c.green(), 2, 16, QChar('0'))
        .arg(c.blue(), 2, 16, QChar('0'))
        .toUpper();
}
}   // namespace

ColorPickerOverlay::ColorPickerOverlay(const ScreenCapture &shot, QWidget *parent)
    : QWidget(parent), m_shot(shot)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(shot.logicalRect);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("TaskPin 取色"));

    m_base = shot.pixmap.toImage();
    m_base.setDevicePixelRatio(1.0);
    m_cursor = QPoint(width() / 2, height() / 2);
}

QColor ColorPickerOverlay::colorAt(const QPoint &logical) const
{
    if (m_base.isNull()) return {};
    const int x = qBound(0, int(logical.x() * m_shot.dpr), m_base.width() - 1);
    const int y = qBound(0, int(logical.y() * m_shot.dpr), m_base.height() - 1);
    return m_base.pixelColor(x, y);
}

void ColorPickerOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawPixmap(0, 0, m_shot.pixmap);
    p.fillRect(rect(), QColor(0, 0, 0, 40));   // slight dim so the cursor stands out

    drawMagnifier(p);

    QFont f = font();
    f.setPixelSize(13);
    p.setFont(f);
    const QString hint = QStringLiteral("单击复制色值 · Esc 取消");
    const QFontMetrics fm(f);
    const QRect box(width() / 2 - (fm.horizontalAdvance(hint) + 24) / 2, 30, fm.horizontalAdvance(hint) + 24, 30);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 22, 205));
    p.drawRoundedRect(box, 8, 8);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.drawText(box, Qt::AlignCenter, hint);
}

void ColorPickerOverlay::drawMagnifier(QPainter &p)
{
    if (m_base.isNull()) return;

    QRect box(m_cursor.x() + 26, m_cursor.y() + 26, kMagnifier, kMagnifier);
    if (box.right() > width() - 10) box.moveLeft(m_cursor.x() - 26 - kMagnifier);
    if (box.bottom() > height() - 10) box.moveTop(m_cursor.y() - 26 - kMagnifier);

    const int cells = 15;
    const QRect src(m_cursor.x() - cells / 2, m_cursor.y() - cells / 2, cells, cells);
    p.save();
    p.setClipRect(box);
    p.drawPixmap(box, m_shot.pixmap,
                 QRectF(src.x() * m_shot.dpr, src.y() * m_shot.dpr, src.width() * m_shot.dpr,
                        src.height() * m_shot.dpr));
    p.restore();

    // pixel grid + centre crosshair
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    const qreal cell = qreal(kMagnifier) / cells;
    for (int i = 1; i < cells; ++i) {
        p.drawLine(QPointF(box.left() + i * cell, box.top()), QPointF(box.left() + i * cell, box.bottom()));
        p.drawLine(QPointF(box.left(), box.top() + i * cell), QPointF(box.right(), box.top() + i * cell));
    }
    p.setPen(QPen(QColor(0x4C, 0x9A, 0xFF), 2));
    p.setBrush(Qt::NoBrush);
    p.drawRect(box.adjusted(-1, -1, 1, 1));
    p.setPen(QPen(QColor(0x4C, 0x9A, 0xFF), 1.5));
    const QPoint center = box.center();
    p.drawLine(center - QPoint(9, 0), center + QPoint(9, 0));
    p.drawLine(center - QPoint(0, 9), center + QPoint(0, 9));
    p.setPen(QPen(Qt::black, 1));
    p.drawLine(center - QPoint(8, 0), center + QPoint(8, 0));
    p.drawLine(center - QPoint(0, 8), center + QPoint(0, 8));

    // swatch + values
    const QColor c = colorAt(m_cursor);
    const QRect panel(box.left(), box.bottom() + 8, kMagnifier, 46);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(12, 16, 22, 225));
    p.drawRoundedRect(panel, 8, 8);
    p.setBrush(c);
    p.drawRoundedRect(QRect(panel.left() + 8, panel.top() + 8, 30, 30), 6, 6);

    QFont f = font();
    f.setPixelSize(13);
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.drawText(QRect(panel.left() + 46, panel.top() + 6, panel.width() - 50, 16), Qt::AlignLeft | Qt::AlignVCenter,
               hexOf(c));
    f.setBold(false);
    f.setPixelSize(11);
    p.setFont(f);
    p.setPen(QColor(0x9A, 0xA5, 0xB4));
    p.drawText(QRect(panel.left() + 46, panel.top() + 24, panel.width() - 50, 16), Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("R%1 G%2 B%3").arg(c.red()).arg(c.green()).arg(c.blue()));
}

void ColorPickerOverlay::mouseMoveEvent(QMouseEvent *e)
{
    m_cursor = e->pos();
    update();
}

void ColorPickerOverlay::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        close();
        return;
    }
    m_cursor = e->pos();
    emit picked(colorAt(m_cursor));
    close();
}

void ColorPickerOverlay::keyPressEvent(QKeyEvent *e)
{
    if (e->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(e);
}
