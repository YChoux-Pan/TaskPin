#include "ui/PinWindow.h"

#include "core/FileNaming.h"
#include "core/Settings.h"
#include "platform/WindowFx.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QFileDialog>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QTextDocument>
#include <QTimer>
#include <QTransform>
#include <QWheelEvent>
#include <algorithm>

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

PinWindow::PinWindow(const QImage &image, const QString &group, QWidget *parent)
    : QWidget(parent), m_image(image), m_group(group)
{
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_DeleteOnClose, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setWindowTitle(QStringLiteral("TaskPin 贴图"));

    const auto &s = Settings::instance();
    m_opacity = qBound(0.1, s.pinOpacity, 1.0);
    if (m_group.trimmed().isEmpty()) m_group = currentGroupRef();
    ensureGroup(m_group);

    relayout();

    // appear near the cursor but fully inside the working area
    QScreen *screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 800, 600);
    const QPoint cursor = QCursor::pos();
    int x = qBound(area.left() + 8, cursor.x() + 16, area.right() - width() - 8);
    int y = qBound(area.top() + 8, cursor.y() + 16, area.bottom() - height() - 8);
    move(x, y);

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
    const int w = qMax(1, qRound(m_image.width() * m_zoom)) + kPad * 2;
    const int h = qMax(1, qRound(m_image.height() * m_zoom)) + kPad * 2;
    resize(w, h);
    update();
}

void PinWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QRect target(kPad, kPad, qMax(1, qRound(m_image.width() * m_zoom)),
                       qMax(1, qRound(m_image.height() * m_zoom)));

    // soft double shadow like the C# build
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, 50));
    p.drawRect(target.adjusted(2, 3, 3, 3));
    p.setBrush(QColor(0, 0, 0, 28));
    p.drawRect(target.adjusted(5, 6, 6, 6));

    p.drawImage(target, m_image);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(255, 255, 255, 60), 1));
    p.drawRect(target.adjusted(0, 0, -1, -1));
}

void PinWindow::setZoom(qreal zoom)
{
    const qreal next = qBound(0.25, zoom, 4.0);
    if (qAbs(next - m_zoom) < 0.001) return;
    // keep the centre stable while zooming
    const QPoint center = geometry().center();
    m_zoom = next;
    relayout();
    move(center - QPoint(width() / 2, height() / 2));
    qInfo("pin zoom %.2f", m_zoom);
}

void PinWindow::setPinOpacity(qreal opacity)
{
    m_opacity = qBound(0.1, opacity, 1.0);
    setWindowOpacity(m_opacity);
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
    const int dir = e->angleDelta().y() > 0 ? 1 : -1;
    if (e->modifiers() & Qt::ControlModifier) {
        setPinOpacity(m_opacity + dir * 0.05);
        return;
    }
    setZoom(m_zoom * (dir > 0 ? 1.1 : 0.9));
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
        if (e->modifiers() & Qt::ControlModifier) setZoom(1.0);
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

    QMenu menu(this);
    buildMenu(&menu);

    // a non-activatable window cannot host a usable popup: make this one activatable for the duration
    WindowFx::setNoActivate(this, false);
    WindowFx::keepOnTop(this);
    menu.exec(e->globalPos());
    m_menuOpen = false;
}

void PinWindow::buildMenu(QMenu *menu)
{
    menu->addAction(QStringLiteral("识别为任务  Ctrl+R"), this, [this] { emit recognizeRequested(m_image); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("复制图像  Ctrl+C"), this,
                    [this] { QApplication::clipboard()->setImage(m_image); });
    menu->addAction(QStringLiteral("保存图片…  Ctrl+S"), this, [this] { saveImage(); });
    menu->addAction(QStringLiteral("以剪贴板内容替换"), this, [this] { replaceFromClipboard(); });
    menu->addSeparator();

    QMenu *opacity = menu->addMenu(QStringLiteral("不透明度"));
    for (int pct : {100, 90, 80, 70, 60, 50, 40, 30}) {
        QAction *a = opacity->addAction(QStringLiteral("%1%").arg(pct));
        connect(a, &QAction::triggered, this, [this, pct] { setPinOpacity(pct / 100.0); });
    }

    const QList<int> presets{25, 50, 75, 100, 150, 200};
    QString zoomLabel = QStringLiteral("缩放：%1%（点击切换）").arg(qRound(m_zoom * 100));
    menu->addAction(zoomLabel, this, [this, presets] {
        for (int p : presets) {
            if (p > qRound(m_zoom * 100) + 1) {
                setZoom(p / 100.0);
                return;
            }
        }
        setZoom(0.25);
    });

    QMenu *rotate = menu->addMenu(QStringLiteral("旋转 / 翻转"));
    rotate->addAction(QStringLiteral("顺时针 90°"), this, [this] {
        m_image = m_image.transformed(QTransform().rotate(90));
        relayout();
    });
    rotate->addAction(QStringLiteral("逆时针 90°"), this, [this] {
        m_image = m_image.transformed(QTransform().rotate(-90));
        relayout();
    });
    rotate->addAction(QStringLiteral("水平翻转"), this, [this] {
        m_image = m_image.mirrored(true, false);
        relayout();
    });

    menu->addAction(m_clickThrough ? QStringLiteral("取消点击穿透") : QStringLiteral("点击穿透"), this,
                    [this] { toggleClickThrough(); });
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
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("新建分组"), QStringLiteral("分组名称："),
                                                   QLineEdit::Normal,
                                                   QStringLiteral("分组%1").arg(groupList().size() + 1), &ok)
                                 .trimmed();
        if (ok && !name.isEmpty()) {
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
    menu->addAction(QStringLiteral("关闭  Esc"), this, [this] { close(); });
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
    relayout();
}

void PinWindow::saveImage()
{
    const QString def = Settings::instance().resolveAutoSaveDir() + QStringLiteral("/") +
                        FileNaming::expand(Settings::instance().namePattern, QDateTime::currentDateTime());
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("保存贴图"), def,
                                                      QStringLiteral("PNG 图片 (*.png)"));
    if (path.isEmpty()) return;
    if (!m_image.save(path, "PNG")) qWarning() << "pin save failed:" << path;
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
        if (WindowFx::isClickThrough(p)) {
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
