#include "ui/WhiteboardWindow.h"

#include "core/FileNaming.h"
#include "core/Settings.h"
#include "ui/IconSet.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QWheelEvent>

namespace {
constexpr int kBtn = 32;
constexpr int kGap = 4;
constexpr int kSwatch = 22;
constexpr int kPad = 8;
constexpr int kSwatchCount = 7;
constexpr int kSwatchesWidth = kSwatchCount * (kSwatch + kGap) - kGap;
const QStringList kActionIds{QStringLiteral("undo"), QStringLiteral("clear"), QStringLiteral("copy"),
                             QStringLiteral("save"), QStringLiteral("print"), QStringLiteral("close")};

const QColor kBar(0xEC, 0xEE, 0xF3, 250);
const QColor kBarEdge(0x1A, 0x1D, 0x22, 30);
const QColor kFg(0x2E, 0x33, 0x3B);
const QColor kToolAccent(0x2F, 0x6F, 0xEB);
const QColor kDanger(0xC0, 0x3B, 0x2E);

QColor swatches[kSwatchCount] = {
    QColor(0xE5, 0x3B, 0x3B), QColor(0xFF, 0xB0, 0x20), QColor(0x3E, 0xC4, 0x6B),
    QColor(0x3B, 0x9C, 0xFF), QColor(0xB0, 0x6B, 0xFF), QColor(0xFF, 0xFF, 0xFF),
    QColor(0x10, 0x14, 0x1A),
};
}   // namespace

WhiteboardWindow::WhiteboardWindow(const ScreenCapture &shot, bool transparent, QWidget *parent)
    : QWidget(parent), m_shot(shot), m_transparent(transparent),
      m_widthSlider(new QSlider(Qt::Horizontal, this)), m_widthValue(new QLabel(this))
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    setAttribute(Qt::WA_DeleteOnClose, true);
    if (m_transparent) setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setGeometry(shot.logicalRect);
    setFocusPolicy(Qt::StrongFocus);
    setWindowTitle(QStringLiteral("TaskPin 白板"));

    m_widthSlider->setRange(1, 40);
    m_widthSlider->setAccessibleName(QStringLiteral("画笔粗细"));
    m_widthSlider->setToolTip(QStringLiteral("画笔粗细"));
    m_widthSlider->setCursor(Qt::ArrowCursor);
    m_widthSlider->setFocusPolicy(Qt::StrongFocus);
    m_widthValue->setAlignment(Qt::AlignCenter);
    m_widthValue->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto palette = m_widthValue->palette();
    palette.setColor(QPalette::WindowText, kFg);
    m_widthValue->setPalette(palette);
    connect(m_widthSlider, &QSlider::valueChanged, this, [this](int width) {
        m_widthValue->setText(QStringLiteral("%1 px").arg(width));
    });
    m_widthSlider->setValue(4);

    m_base = shot.pixmap.toImage();
    m_base.setDevicePixelRatio(1.0);
}

// ---------------------------------------------------------------- painting

void WhiteboardWindow::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    const int valueWidth = m_widthValue->fontMetrics().horizontalAdvance(QStringLiteral("40 px")) + 4;
    const int actionsWidth = int(kActionIds.size()) * (kBtn + kGap) - kGap;
    const int fixedWidth = 2 * kPad + kSwatchesWidth + 10 + kGap + valueWidth + 10 + actionsWidth;
    const int sliderWidth = qBound(48, width() - fixedWidth, 104);
    const int barWidth = fixedWidth + sliderWidth;
    m_toolbarRect = QRect((width() - barWidth) / 2, height() - 92, barWidth, kBtn + 2 * kPad);
    const int sliderX = m_toolbarRect.left() + kPad + kSwatchesWidth + 10;
    const int y = m_toolbarRect.top() + kPad;
    m_widthSlider->setGeometry(sliderX, y, sliderWidth, kBtn);
    m_widthValue->setGeometry(sliderX + sliderWidth + kGap, y, valueWidth, kBtn);
}

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

    const QRect &bar = m_toolbarRect;

    p.setPen(QPen(kBarEdge, 1));
    p.setBrush(kBar);
    p.drawRoundedRect(QRectF(bar).adjusted(0.5, 0.5, -0.5, -0.5), 10, 10);
    p.setPen(Qt::NoPen);

    int cx = bar.left() + kPad;
    const int cy = bar.top() + kPad;
    for (int i = 0; i < kSwatchCount; ++i) {
        const QRect r(cx, cy + (kBtn - kSwatch) / 2, kSwatch, kSwatch);
        m_hot.append({r, QStringLiteral("swatch:%1").arg(i)});
        p.setPen(QPen(m_color == swatches[i] ? kToolAccent : QColor(0, 0, 0, 55), 2));
        p.setBrush(swatches[i]);
        p.drawEllipse(r.adjusted(1, 1, -1, -1));
        cx += kSwatch + kGap;
    }
    cx = m_widthValue->geometry().right() + 1 + 10;
    for (const QString &id : kActionIds) {
        const QRect r(cx, cy, kBtn, kBtn);
        m_hot.append({r, id});
        const QColor tint = id == QStringLiteral("close") ? kDanger : kFg;
        IconSet::draw(p, id, QRectF(r.adjusted(7, 7, -7, -7)), tint);
        cx += kBtn + kGap;
    }

    QFont f = font();
    f.setPixelSize(11);
    p.setFont(f);
    const QString hint = QStringLiteral("手写 · 滚轮或方向键调粗细 · Ctrl+Z 撤销 · Enter 复制 · Esc 退出");
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
    if (m_toolbarRect.contains(e->pos())) return;

    m_drawing = true;
    m_current = Annotation();
    m_current.kind = Annotation::Pen;
    m_current.color = m_color;
    m_current.width = m_widthSlider->value();
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
    const int delta = e->angleDelta().y();
    if (delta != 0) m_widthSlider->setValue(m_widthSlider->value() + (delta > 0 ? 1 : -1));
}

void WhiteboardWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left:
    case Qt::Key_Down: m_widthSlider->triggerAction(QAbstractSlider::SliderSingleStepSub); return;
    case Qt::Key_Right:
    case Qt::Key_Up: m_widthSlider->triggerAction(QAbstractSlider::SliderSingleStepAdd); return;
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
