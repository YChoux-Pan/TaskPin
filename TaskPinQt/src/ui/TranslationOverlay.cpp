#include "ui/TranslationOverlay.h"
#include "core/InstanceNames.h"
#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QtMath>

TranslationOverlay::TranslationOverlay(const ScreenCapture &shot,QWidget *parent) : QWidget(parent), m_shot(shot)
{
    setWindowTitle(QStringLiteral("TaskPin 截图翻译 · 框选"));
    setObjectName("translationOverlay");
    setWindowFlags(Qt::FramelessWindowHint|Qt::WindowStaysOnTopHint|(taskPinAutomation()?Qt::Window:Qt::Tool));
    setAttribute(Qt::WA_DeleteOnClose); setGeometry(shot.logicalRect);
    setCursor(Qt::CrossCursor); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus);
    m_start = m_end = QCursor::pos()-shot.logicalRect.topLeft();
    m_hint = QStringLiteral("截图翻译 · 拖动框选，松开识别 · Esc 取消 · 仅当前显示器");
}
QRect TranslationOverlay::pixelRect(const QRectF &selection,const QSize &logicalSize,const QSize &pixelSize)
{
    if (logicalSize.isEmpty() || pixelSize.isEmpty()) return {};
    const auto normalized = selection.normalized().intersected(QRectF(QPointF(),logicalSize));
    if (normalized.width()<3 || normalized.height()<3) return {};
    const double sx = double(pixelSize.width())/logicalSize.width(), sy = double(pixelSize.height())/logicalSize.height();
    return QRect(QPoint(qFloor(normalized.left()*sx),qFloor(normalized.top()*sy)),
                 QPoint(qCeil(normalized.right()*sx)-1,qCeil(normalized.bottom()*sy)-1)).intersected(QRect(QPoint(),pixelSize));
}
void TranslationOverlay::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.drawPixmap(rect(),m_shot.pixmap);
    painter.fillRect(rect(),QColor(0,0,0,100));
    const QRectF selection = QRectF(m_start,m_end).normalized().intersected(rect());
    if (m_dragging && selection.width()>0 && selection.height()>0) {
        painter.save(); painter.setClipRect(selection); painter.drawPixmap(rect(),m_shot.pixmap); painter.restore();
        painter.setPen(QPen(QColor("#4C9AFF"),2)); painter.setBrush(Qt::NoBrush); painter.drawRect(selection);
    }
    painter.setPen(Qt::white);
    painter.fillRect(QRect(20,20,qMin(width()-40,580),40),QColor("#12171F"));
    painter.drawText(QRect(32,20,qMin(width()-64,556),40),Qt::AlignVCenter,
                     m_hint);
}
void TranslationOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) { emit cancelled(); close(); return; }
    if (event->button() == Qt::LeftButton) { m_start = m_end = event->position(); m_dragging = true; update(); }
}
void TranslationOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) { m_end = QPointF(qBound(0.0,event->position().x(),double(width())),qBound(0.0,event->position().y(),double(height()))); update(); }
}
void TranslationOverlay::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button()!=Qt::LeftButton || !m_dragging) return;
    m_end = event->position();
    const QRect pixels = pixelRect(QRectF(m_start,m_end),size(),m_shot.pixmap.size());
    if (pixels.isEmpty()) { m_hint = QStringLiteral("选区过小，请重新框选 · Esc 取消"); m_dragging = false; update(); return; }
    if (qint64(pixels.width())*pixels.height()>16000000) { m_hint = QStringLiteral("选区过大，请缩小到 1600 万像素内 · Esc 取消"); m_dragging=false; update(); return; }
    QImage image = m_shot.pixmap.toImage().copy(pixels); image.setDevicePixelRatio(1);
    const QRect global = QRectF(m_start,m_end).normalized().intersected(rect()).toAlignedRect().translated(m_shot.logicalRect.topLeft());
    hide(); emit selected(image,global); close();
}
void TranslationOverlay::keyPressEvent(QKeyEvent *event)
{
    if (event->key()==Qt::Key_Escape) { emit cancelled(); close(); } else QWidget::keyPressEvent(event);
}
