#include "ui/PinWindow.h"

#include "core/FileNaming.h"
#include "core/Settings.h"
#include "core/InstanceNames.h"
#include "platform/WindowFx.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QTextDocument>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr int kPad = 6;   // transparent padding that carries the drop shadow

QList<PinWindow *> &registry()
{
    static QList<PinWindow *> pins;
    return pins;
}

QStringList &groupList()
{
    static QStringList groups{QStringLiteral("默认")};
    return groups;
}

QString &currentGroupRef()
{
    static QString current = QStringLiteral("默认");
    return current;
}
}   // namespace

PinWindow::PinWindow(const QImage &image, const QString &group, QWidget *parent, const QRect &sourceRegion)
    : QWidget(parent), m_image(image),
      m_logicalImageSize(sourceRegion.isValid() ? sourceRegion.size() : image.size()), m_group(group)
{
    setWindowFlags((taskPinAutomation() ? Qt::Window : Qt::Tool) | Qt::FramelessWindowHint |
                   Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("TaskPin 贴图"));

    const auto &s = Settings::instance();
    m_opacity = qBound(0.1, s.pinOpacity, 1.0);
    m_shadowStrength = s.pinShadowStrength;
    m_borderWidth = s.pinBorderWidth;
    m_borderColor = s.pinBorderColor;
    rebuildPreview();
    m_zoomAnimation.setDuration(110);
    m_zoomAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&m_zoomAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        displayZoom(value.toReal(), m_zoomCenter);
    });
    m_qualityTimer.setSingleShot(true);
    m_qualityTimer.setInterval(150);
    connect(&m_qualityTimer, &QTimer::timeout, this, &PinWindow::rebuildCache);
    if (m_group.trimmed().isEmpty()) m_group = currentGroupRef();
    ensureGroup(m_group);

    relayout();

    // appear near the cursor but fully inside the working area
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const QPoint cursor = QCursor::pos();
    const int minX = area.left() + qMin(8, qMax(0, area.width() - width()));
    const int minY = area.top() + qMin(8, qMax(0, area.height() - height()));
    int x = qBound(minX, cursor.x() + 16, qMax(minX, area.right() - width() - 8));
    int y = qBound(minY, cursor.y() + 16, qMax(minY, area.bottom() - height() - 8));
    // Direct captures overlay the original content exactly, even at a monitor edge.
    // Only the transparent shadow padding can extend beyond the captured monitor.
    move(sourceRegion.isValid() ? sourceRegion.topLeft() - QPoint(padding(), padding()) : QPoint(x, y));

    registry().append(this);
    setPinOpacity(m_opacity);
    qInfo("pin created %dx%d group=%s", m_image.width(), m_image.height(), qUtf8Printable(m_group));
}

PinWindow::~PinWindow()
{
    registry().removeAll(this);
    qInfo("pin closed, %d remaining", int(registry().size()));
}

const QList<PinWindow *> &PinWindow::all() { return registry(); }

// ---------------------------------------------------------------- rendering

void PinWindow::relayout()
{
    m_cache = {};
    const int w = qMax(1, qRound(m_logicalImageSize.width() * m_zoom)) + padding() * 2;
    const int h = qMax(1, qRound(m_logicalImageSize.height() * m_zoom)) + padding() * 2;
    resize(w, h);
    update();
}

bool PinWindow::event(QEvent *event)
{
    if (event->type() == QEvent::ScreenChangeInternal) {
        m_cache = {};
        m_qualityTimer.start();
        update();
    }
    return QWidget::event(event);
}

void PinWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // Clear the backing surface as it shrinks; stale translucent pixels looked like a second edge.
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.fillRect(rect(), Qt::transparent);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const bool moving = m_zoomAnimation.state() == QAbstractAnimation::Running || m_qualityTimer.isActive();
    p.setRenderHint(QPainter::SmoothPixmapTransform, !moving);

    const QRect target(padding(), padding(), qMax(1, qRound(m_logicalImageSize.width() * m_zoom)),
                       qMax(1, qRound(m_logicalImageSize.height() * m_zoom)));

    // A single fading contour, rather than two hard offset copies.
    for (int spread = 6; m_shadowStrength > 0 && spread >= 1; --spread) {
        p.setPen(QPen(QColor(0, 0, 0, qRound(m_shadowStrength * (7 - spread) / 10.0)), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRect(target.adjusted(-spread, -spread, spread - 1, spread - 1));
    }
    const QImage &frame = moving ? m_preview : m_cache.isNull() ? m_image : m_cache;
    p.drawImage(target, frame);
    p.setBrush(Qt::NoBrush);
    if (m_borderWidth > 0) {
        p.setPen(QPen(m_borderColor, m_borderWidth));
        const qreal inset = m_borderWidth / 2.0;
        p.drawRect(QRectF(target).adjusted(inset, inset, -inset, -inset));
    }
}

int PinWindow::padding() const { return m_shadowStrength > 0 ? kPad : 0; }

void PinWindow::applyAppearance(int shadowStrength, int borderWidth, const QColor &color)
{
    const QPoint origin = contentGeometry().topLeft();
    m_shadowStrength = qBound(0, shadowStrength, 100);
    m_borderWidth = qBound(0, borderWidth, 8);
    if (color.isValid()) m_borderColor = color;
    relayout();
    move(origin - QPoint(padding(), padding()));
    m_qualityTimer.start();
}

void PinWindow::rebuildCache()
{
    const QSize pixels = (contentGeometry().size() * devicePixelRatioF());
    // Bound the cache; a 4x enlargement of a multi-monitor screenshot can otherwise exceed 1 GB.
    if (qint64(pixels.width()) * pixels.height() <= 16000000) {
        m_cache = m_image.scaled(pixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        m_cache.setDevicePixelRatio(devicePixelRatioF());
    }
    update();
}

void PinWindow::rebuildPreview()
{
    m_preview = m_image.width() > 2000 || m_image.height() > 2000
        ? m_image.scaled(QSize(2000, 2000), Qt::KeepAspectRatio, Qt::FastTransformation) : m_image;
}
void PinWindow::replaceFrame(const QImage &image, const QSize &logicalSize, bool resetView)
{
    if(image.isNull() || logicalSize.isEmpty())return;
    const QPoint origin=contentGeometry().topLeft();
    m_zoomAnimation.stop();
    if(resetView){m_frameTransform.reset();m_zoom=1.;}
    m_targetZoom=m_zoom;
    m_image=m_frameTransform.isIdentity()?image:image.transformed(m_frameTransform);
    m_logicalImageSize=m_frameTransform.mapRect(QRectF(QPointF(),logicalSize)).size().toSize();
    rebuildPreview();relayout();move(origin-QPoint(padding(),padding()));m_qualityTimer.start();
}

QRect PinWindow::contentGeometry() const
{
    return QRect(mapToGlobal(QPoint(padding(), padding())), size() - QSize(padding() * 2, padding() * 2));
}

void PinWindow::setZoom(qreal zoom)
{
    const qreal next = qBound(0.25, zoom, 4.0);
    m_zoomAnimation.stop();
    m_targetZoom = next;
    displayZoom(next, QRectF(geometry()).center());
    m_qualityTimer.start();
}

void PinWindow::displayZoom(qreal zoom, const QPointF &center)
{
    m_zoom = zoom;
    m_cache = {};
    const QSize next(qMax(1, qRound(m_logicalImageSize.width() * zoom)) + 2 * padding(),
                     qMax(1, qRound(m_logicalImageSize.height() * zoom)) + 2 * padding());
    setGeometry(QRect((center - QPointF(next.width() / 2.0, next.height() / 2.0)).toPoint(), next));
    update();
}

void PinWindow::setPinOpacity(qreal opacity)
{
    m_opacity = qBound(0.1, opacity, 1.0);
    setWindowOpacity(m_opacity);
}

void PinWindow::resetDisplay()
{
    setZoom(1.0);
    setPinOpacity(1.0);
    if (m_clickThrough) toggleClickThrough();
}

void PinWindow::flash()
{
    // two quick blinks: fade out then back to the configured opacity
    auto *timer = new QTimer(this);
    timer->setInterval(140);
    connect(timer, &QTimer::timeout, this, [this, timer] {
        ++m_flashTicks;
        setWindowOpacity(m_flashTicks % 2 ? 0.25 : m_opacity);
        if (m_flashTicks >= 6) {
            timer->stop();
            timer->deleteLater();
            setWindowOpacity(m_opacity);
        }
    });
    timer->start();
}

// ---------------------------------------------------------------- input

void PinWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        m_zoomAnimation.stop();
        m_targetZoom = m_zoom;
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
        raise();
    }
}

void PinWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) move(e->globalPosition().toPoint() - m_dragOffset);
}

void PinWindow::mouseReleaseEvent(QMouseEvent *)
{
    m_dragging = false;
}

void PinWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) close();
}

void PinWindow::wheelEvent(QWheelEvent *e)
{
    const qreal steps = !e->pixelDelta().isNull() ? e->pixelDelta().y() / 120.0 : e->angleDelta().y() / 120.0;
    if (qFuzzyIsNull(steps)) { e->accept(); return; }
    if (e->modifiers() & Qt::ControlModifier) {
        setPinOpacity(m_opacity + steps * 0.05);
        e->accept();
        return;
    }
    if (m_zoomAnimation.state() != QAbstractAnimation::Running) m_zoomCenter = QRectF(geometry()).center();
    m_zoomAnimation.stop();
    m_targetZoom = qBound(.25, m_targetZoom * std::pow(1.1, steps), 4.0);
    m_zoomAnimation.setStartValue(m_zoom);
    m_zoomAnimation.setEndValue(m_targetZoom);
    m_zoomAnimation.start();
    m_qualityTimer.start();
    e->accept();
}

void PinWindow::keyPressEvent(QKeyEvent *e)
{
    const int step = (e->modifiers() & Qt::ShiftModifier) ? 10 : 1;
    switch (e->key()) {
    case Qt::Key_Escape:
    case Qt::Key_Delete:
        close();
        return;
    case Qt::Key_Left: move(x() - step, y()); return;
    case Qt::Key_Right: move(x() + step, y()); return;
    case Qt::Key_Up: move(x(), y() - step); return;
    case Qt::Key_Down: move(x(), y() + step); return;
    case Qt::Key_Plus:
    case Qt::Key_Equal:
        setZoom(m_zoom * 1.1);
        return;
    case Qt::Key_Minus:
        setZoom(m_zoom / 1.1);
        return;
    case Qt::Key_0:
        if (e->modifiers() & Qt::ControlModifier) {
            if (e->modifiers() & Qt::ShiftModifier) resetDisplay();
            else setZoom(1.0);
        }
        return;
    case Qt::Key_C:
        if (e->modifiers() & Qt::ControlModifier) QApplication::clipboard()->setImage(m_image);
        return;
    case Qt::Key_S:
        if (e->modifiers() & Qt::ControlModifier) saveImage();
        return;
    case Qt::Key_R:
        if (e->modifiers() & Qt::ControlModifier) emit recognizeRequested(m_image);
        return;
    default:
        break;
    }
    QWidget::keyPressEvent(e);
}

void PinWindow::contextMenuEvent(QContextMenuEvent *e)
{
    if (m_menuOpen) return;   // nested popup loops used to overflow the stack
    m_menuOpen = true;

    auto *menu=new QMenu(this);
    buildMenu(menu);
    connect(menu,&QMenu::aboutToHide,this,[this,menu] {m_menuOpen=false;menu->deleteLater();});

    // a non-activatable window cannot host a usable popup: make this one activatable for the duration
    WindowFx::setNoActivate(this, false);
    WindowFx::keepOnTop(this);
    menu->popup(e->globalPos());
}

void PinWindow::buildMenu(QMenu *menu)
{
    extendMenu(menu);
    menu->addAction(QStringLiteral("识别为任务\tCtrl+R"), this, [this] { emit recognizeRequested(m_image); });
    menu->addSeparator();

    auto *appearance = menu->addMenu(QStringLiteral("边框与阴影"));
    appearance->addAction(QStringLiteral("无边框与阴影"), this, [this] {
        applyAppearance(0, 0, m_borderColor);
    });
    auto *shadow = appearance->addMenu(QStringLiteral("阴影强度"));
    for (int value : {0, 18, 35, 60, 100}) {
        auto *a = shadow->addAction(value == 0 ? QStringLiteral("关闭阴影") : QStringLiteral("%1%").arg(value));
        a->setCheckable(true); a->setChecked(m_shadowStrength == value);
        connect(a, &QAction::triggered, this, [this, value] { applyAppearance(value, m_borderWidth, m_borderColor); });
    }
    auto *border = appearance->addMenu(QStringLiteral("边框宽度"));
    for (int value : {0, 1, 2, 4, 8}) {
        auto *a = border->addAction(value == 0 ? QStringLiteral("关闭边框") : QStringLiteral("%1 px").arg(value));
        a->setCheckable(true); a->setChecked(m_borderWidth == value);
        connect(a, &QAction::triggered, this, [this, value] { applyAppearance(m_shadowStrength, value, m_borderColor); });
    }
    appearance->addAction(QStringLiteral("边框颜色…"), this, [this] {
        const QPointer<PinWindow> alive=this;
        const auto c = QColorDialog::getColor(m_borderColor, this, QStringLiteral("贴图边框颜色"));
        if (alive && c.isValid()) applyAppearance(m_shadowStrength, m_borderWidth, c);
    });
    appearance->addAction(QStringLiteral("恢复设置中的外观"), this, [this] {
        const auto &s = Settings::instance();
        applyAppearance(s.pinShadowStrength, s.pinBorderWidth, s.pinBorderColor);
    });
    menu->addAction(QStringLiteral("复制图像\tCtrl+C"), this,
                    [this] { QApplication::clipboard()->setImage(m_image); });
    menu->addAction(QStringLiteral("保存图片…\tCtrl+S"), this, [this] { saveImage(); });
    if(allowsClipboardReplacement())menu->addAction(QStringLiteral("以剪贴板内容替换"), this, [this] { replaceFromClipboard(); });
    menu->addSeparator();

    QMenu *opacity = menu->addMenu(QStringLiteral("不透明度"));
    opacity->menuAction()->setToolTip(QStringLiteral("Ctrl+滚轮微调不透明度"));
    opacity->addAction(QStringLiteral("当前：%1%").arg(qRound(m_opacity * 100)))->setEnabled(false);
    opacity->addSeparator();
    auto *opacityGroup = new QActionGroup(opacity);
    for (int pct : {100, 90, 80, 70, 60, 50, 40, 30, 20, 10}) {
        QAction *a = opacity->addAction(QStringLiteral("%1%").arg(pct));
        a->setCheckable(true);
        a->setChecked(qAbs(m_opacity - pct / 100.0) < 0.001);
        opacityGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, pct] { setPinOpacity(pct / 100.0); });
    }

    QMenu *zoom = menu->addMenu(QStringLiteral("缩放"));
    zoom->menuAction()->setToolTip(QStringLiteral("滚轮或 + / - 微调缩放"));
    zoom->addAction(QStringLiteral("当前：%1%").arg(qRound(m_zoom * 100)))->setEnabled(false);
    zoom->addSeparator();
    auto *zoomGroup = new QActionGroup(zoom);
    for (int pct : {25, 50, 75, 100, 150, 200, 300, 400}) {
        QAction *a = zoom->addAction(QStringLiteral("%1%").arg(pct));
        a->setCheckable(true);
        a->setChecked(qAbs(m_zoom - pct / 100.0) < 0.001);
        zoomGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, pct] { setZoom(pct / 100.0); });
    }
    menu->addAction(QStringLiteral("原始大小\tCtrl+0"), this, [this] { setZoom(1.0); });
    auto *reset = menu->addAction(QStringLiteral("重置显示\tCtrl+Shift+0"), this, [this] { resetDisplay(); });
    reset->setToolTip(QStringLiteral("恢复原始大小、100% 不透明度，并取消点击穿透"));

    QMenu *rotate = menu->addMenu(QStringLiteral("旋转 / 翻转"));
    rotate->addAction(QStringLiteral("顺时针 90°"), this, [this] {
        m_image = m_image.transformed(QTransform().rotate(90));
        m_frameTransform *= QTransform().rotate(90);
        m_logicalImageSize.transpose();
        rebuildPreview();
        relayout();
    });
    rotate->addAction(QStringLiteral("逆时针 90°"), this, [this] {
        m_image = m_image.transformed(QTransform().rotate(-90));
        m_frameTransform *= QTransform().rotate(-90);
        m_logicalImageSize.transpose();
        rebuildPreview();
        relayout();
    });
    rotate->addAction(QStringLiteral("水平翻转"), this, [this] {
        m_image = m_image.mirrored(true, false);
        m_frameTransform *= QTransform().scale(-1,1);
        rebuildPreview();
        relayout();
    });

    QAction *clickThrough = menu->addAction(QStringLiteral("点击穿透"), this, [this] { toggleClickThrough(); });
    clickThrough->setCheckable(true);
    clickThrough->setChecked(m_clickThrough);
    clickThrough->setToolTip(QStringLiteral("可通过托盘菜单取消全部贴图点击穿透"));
    menu->addSeparator();

    QMenu *groups = menu->addMenu(QStringLiteral("贴图分组（当前：%1）").arg(m_group));
    for (const QString &g : groupList()) {
        QAction *a = groups->addAction(g);
        a->setCheckable(true);
        a->setChecked(g == m_group);
        connect(a, &QAction::triggered, this, [this, g] { applyGroup(g); });
    }
    groups->addSeparator();
    groups->addAction(QStringLiteral("新建分组…"), this, [this] {
        const QPointer<PinWindow> alive=this;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("新建分组"), QStringLiteral("分组名称："),
                                                   QLineEdit::Normal,
                                                   QStringLiteral("分组%1").arg(groupList().size() + 1), &ok)
                                 .trimmed();
        if (alive && ok && !name.isEmpty()) {
            ensureGroup(name);
            applyGroup(name);
        }
    });
    groups->addAction(QStringLiteral("切换到该分组（闪烁定位）"), this, [this] {
        setCurrentGroup(m_group);
        switchGroup(m_group);
        flashAll();
    });
    groups->addAction(QStringLiteral("关闭该分组的全部贴图"), this, [this] { closeGroup(m_group); });

    menu->addSeparator();
    menu->addAction(QStringLiteral("关闭\tEsc / Delete"), this, [this] { close(); });
    menu->addAction(QStringLiteral("关闭全部贴图"), this, [] { closeAll(); });
}

void PinWindow::toggleClickThrough()
{
    m_clickThrough = !m_clickThrough;
    WindowFx::setClickThrough(this, m_clickThrough);
    qInfo("pin click-through=%d", int(m_clickThrough));
}

void PinWindow::replaceFromClipboard()
{
    const QImage img = QApplication::clipboard()->image();
    if (img.isNull()) return;
    m_image = img;
    m_frameTransform.reset();
    m_logicalImageSize = img.size();
    rebuildPreview();
    relayout();
}

void PinWindow::saveImage()
{
    const QPointer<PinWindow> alive=this;
    const QImage snapshot=m_image;
    const QString def = Settings::instance().resolveAutoSaveDir() + QStringLiteral("/") +
                        FileNaming::expand(Settings::instance().namePattern, QDateTime::currentDateTime());
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存贴图"), def,
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (!alive || path.isEmpty()) return;
    if (!snapshot.save(path, "PNG")) {setToolTip(QStringLiteral("图片保存失败，请检查目录权限。"));return;}
    QApplication::clipboard()->setText(QDir::toNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

void PinWindow::applyGroup(const QString &group)
{
    m_group = ensureGroup(group);
    setCurrentGroup(m_group);
}

// ---------------------------------------------------------------- statics

void PinWindow::closeAll()
{
    const auto copy = registry();
    for (auto *p : copy) p->close();
}

void PinWindow::closeGroup(const QString &group)
{
    const auto copy = registry();
    for (auto *p : copy)
        if (p->group() == group) p->close();
}

void PinWindow::switchGroup(const QString &group)
{
    setCurrentGroup(group);
    for (auto *p : registry()) {
        if (p->group() == group) {
            p->show();
            WindowFx::keepOnTop(p);
            p->flash();
        } else {
            p->hide();
        }
    }
}

void PinWindow::flashAll()
{
    for (auto *p : registry())
        if (p->isVisible()) p->flash();
}

bool PinWindow::toggleAll()
{
    QList<PinWindow *> visible;
    for (auto *p : registry())
        if (p->isVisible()) visible.append(p);
    if (!visible.isEmpty()) {
        for (auto *p : visible) p->hide();
        return false;
    }
    bool anyInGroup = false;
    for (auto *p : registry())
        if (p->group() == currentGroupRef()) anyInGroup = true;
    for (auto *p : registry()) {
        if (anyInGroup && p->group() != currentGroupRef()) continue;
        p->show();
        WindowFx::keepOnTop(p);
    }
    return true;
}

void PinWindow::clearClickThrough()
{
    for (auto *p : registry()) {
        if (p->m_clickThrough || WindowFx::isClickThrough(p)) {
            p->m_clickThrough = false;
            WindowFx::setClickThrough(p, false);
        }
    }
    qInfo("cleared click-through on all pins");
}

QStringList PinWindow::groups() { return groupList(); }

QString PinWindow::currentGroup() { return currentGroupRef(); }

void PinWindow::setCurrentGroup(const QString &group)
{
    ensureGroup(group);
    currentGroupRef() = group;
    Settings::instance().pinGroup = group;
}

QString PinWindow::ensureGroup(const QString &name)
{
    const QString cleaned = name.trimmed().isEmpty() ? QStringLiteral("默认") : name.trimmed();
    if (!groupList().contains(cleaned)) groupList().append(cleaned);
    return cleaned;
}

QImage PinWindow::renderTextImage(const QString &text)
{
    QFont font(QStringLiteral("Microsoft YaHei UI"));
    font.setPixelSize(15);

    QTextDocument doc;
    doc.setDefaultFont(font);
    doc.setDocumentMargin(0);
    doc.setPlainText(text);

    const int maxTextWidth = 520;
    doc.setTextWidth(maxTextWidth);

    const int pad = 16;
    const int w = int(doc.idealWidth()) + pad * 2;
    const int h = int(doc.size().height()) + pad * 2;

    QImage image(qMax(80, w), qMax(40, h), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setBrush(QColor(0x12, 0x17, 0x1F, 245));
    p.setPen(QPen(QColor(255, 255, 255, 36), 1));
    p.drawRoundedRect(QRect(0, 0, image.width() - 1, image.height() - 1), 10, 10);
    p.setPen(QColor(0xE8, 0xEC, 0xF2));
    p.translate(pad, pad);
    doc.drawContents(&p);
    p.end();
    return image;
}

PinWindow *PinWindow::createTextPin(const QString &text)
{
    if (text.trimmed().isEmpty()) return nullptr;
    return new PinWindow(renderTextImage(text.trimmed()));
}
