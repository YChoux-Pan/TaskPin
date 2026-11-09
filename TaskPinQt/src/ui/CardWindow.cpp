#include "ui/CardWindow.h"

#include "core/Settings.h"
#include "core/Store.h"
#include "platform/WindowFx.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDateTime>
#include <QDebug>
#include <QLocale>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

namespace {
constexpr int kMargin = 10;
constexpr int kPadX = 14;
constexpr int kHeaderH = 26;
}

CardWindow::CardWindow(Store *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    // No Qt::WindowStaysOnTopHint here: the z-order is asserted through Win32 in applyZOrder()
    // so the two modes can be switched at runtime without Qt recreating the window handle.
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    // stable title so the regression harness can find the window among the process' other HWNDs
    setWindowTitle(QStringLiteral("TaskPinCard"));

    m_desktopLayer = !Settings::instance().alwaysOnTop;

    connect(m_store, &Store::changed, this, &CardWindow::refresh);
    relayout();
    refresh();
}

void CardWindow::relayout()
{
    const auto &s = Settings::instance();
    const int visible = qMax(1, s.visibleRows);
    m_rowHeight = 27;
    const int footerH = 24;
    const int h = kMargin * 2 + kHeaderH + 8 + visible * m_rowHeight + footerH;
    const int w = qMax(240, s.cardWidth);
    resize(w, h);
}

void CardWindow::placeOnScreen()
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) return;
    const QRect area = screen->availableGeometry();
    const auto &s = Settings::instance();

    // a position remembered from the last drag wins, as long as it is still on screen
    if (s.cardX >= 0 && s.cardY >= 0) {
        const QRect target(s.cardX, s.cardY, width(), height());
        const QRect visible = target.intersected(area);
        if (visible.width() >= width() / 2 && visible.height() >= height() / 2) {
            move(s.cardX, s.cardY);
            return;
        }
    }

    int x = area.right() - width() - 20;
    int y = area.top() + 30;
    if (s.cardPosition == "TopLeft") { x = area.left() + 20; y = area.top() + 30; }
    else if (s.cardPosition == "BottomRight") { x = area.right() - width() - 20; y = area.bottom() - height() - 20; }
    else if (s.cardPosition == "BottomLeft") { x = area.left() + 20; y = area.bottom() - height() - 20; }
    move(x, y);
}

void CardWindow::applySettings()
{
    relayout();
    if (Settings::instance().cardX < 0) placeOnScreen();
    applyZOrder();
    update();
}

void CardWindow::applyZOrder()
{
    const bool onTop = Settings::instance().alwaysOnTop;
    m_desktopLayer = !onTop;
    if (onTop) {
        WindowFx::setTopMost(this, true);
        if (!WindowFx::isTopMost(this)) {
            // Windows occasionally swallows the first HWND_TOPMOST request right after a burst of
            // z-order changes (e.g. restoring a dozen windows). Verify once more on the next tick.
            QTimer::singleShot(0, this, [this] {
                if (!Settings::instance().alwaysOnTop || WindowFx::isTopMost(this)) return;
                WindowFx::setTopMost(this, true);
                qInfo("card z-order retry: topmost=%d", int(WindowFx::isTopMost(this)));
            });
        }
    } else {
        WindowFx::setTopMost(this, false);
        WindowFx::sendToBottom(this);
    }
    qInfo("card z-order: %s (native topmost=%d)", onTop ? "topmost" : "desktop-layer",
          int(WindowFx::isTopMost(this)));
}

void CardWindow::setDesktopLayer(bool on)
{
    m_desktopLayer = on;
    auto &s = Settings::instance();
    s.alwaysOnTop = !on;
    s.save();
    applyZOrder();
    emit zOrderChanged();
    qInfo("card layer -> %s", on ? "desktop" : "topmost");
}

void CardWindow::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    // show()/raise() can pull the window back up; re-assert the configured layer every time
    applyZOrder();
}

bool CardWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
    if (m_desktopLayer) WindowFx::clampToDesktopLayer(message);
    return QWidget::nativeEvent(eventType, message, result);
}

void CardWindow::refresh()
{
    m_tasks = m_store->openTasks();
    const int maxScroll = qMax(0, m_tasks.size() - qMax(1, Settings::instance().visibleRows));
    m_scroll = qBound(0, m_scroll, maxScroll);
    update();
}

int CardWindow::rowAt(const QPoint &p) const
{
    if (m_rowHeight <= 0 || p.y() < m_rowTop) return -1;
    const int i = (p.y() - m_rowTop) / m_rowHeight;
    const int visible = qMax(1, Settings::instance().visibleRows);
    if (i < 0 || i >= visible) return -1;
    const int real = m_scroll + i;
    return real < m_tasks.size() ? real : -1;
}

void CardWindow::paintEvent(QPaintEvent *)
{
    const auto &s = Settings::instance();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRect card(kMargin, kMargin, width() - kMargin * 2, height() - kMargin * 2);
    QPainterPath path;
    path.addRoundedRect(card, 16, 16);

    QLinearGradient grad(card.topLeft(), card.bottomLeft());
    QColor bg = s.cardBg;
    bg.setAlphaF(qBound(0.35, s.opacity, 1.0));
    QColor bg2 = bg.darker(140);
    grad.setColorAt(0, bg);
    grad.setColorAt(1, bg2);
    p.fillPath(path, grad);

    p.setPen(QPen(QColor(255, 255, 255, 30), 1));
    p.drawPath(path);

    // header
    const QColor fg = s.cardFg;
    QColor dim = fg; dim.setAlphaF(0.55);
    QColor faint = fg; faint.setAlphaF(0.40);

    int y = card.top() + 12;
    QFont titleFont = font();
    titleFont.setPointSizeF(10.8);
    titleFont.setBold(true);
    p.setFont(titleFont);
    p.setPen(fg);
    p.drawText(QPoint(card.left() + kPadX, y + 14), QStringLiteral("今日"));

    const int titleW = p.fontMetrics().horizontalAdvance(QStringLiteral("今日"));
    int pending = 0;
    for (const auto &t : m_tasks) if (!t.isDone()) pending++;

    QFont small = font();
    small.setPointSizeF(7.4);
    p.setFont(small);
    const QString pill = pending > 0 ? QString::number(pending) : QStringLiteral("清空");
    const int pillW = p.fontMetrics().horizontalAdvance(pill) + 10;
    QRect pillRect(card.left() + kPadX + titleW + 6, y + 3, pillW, 16);
    QPainterPath pillPath;
    pillPath.addRoundedRect(pillRect, 7, 7);
    p.fillPath(pillPath, QColor(0x6E, 0x9B, 0xFF, 40));
    p.setPen(QColor(0xA9, 0xC4, 0xFF));
    p.drawText(pillRect, Qt::AlignCenter, pill);

    // "+" button
    const int plusSize = 18;
    m_plusRect = QRect(card.right() - kPadX - plusSize, y + 1, plusSize, plusSize);
    p.setBrush(QColor(255, 255, 255, 28));
    p.setPen(Qt::NoPen);
    p.drawEllipse(m_plusRect);
    p.setPen(QPen(QColor(0xC8, 0xD2, 0xE0), 1.2));
    p.drawLine(m_plusRect.center() - QPoint(4, 0), m_plusRect.center() + QPoint(4, 0));
    p.drawLine(m_plusRect.center() - QPoint(0, 4), m_plusRect.center() + QPoint(0, 4));

    // date
    const QString date = QLocale().toString(QDateTime::currentDateTime(), QStringLiteral("MM/dd ddd"));
    const int dateW = p.fontMetrics().horizontalAdvance(date);
    m_dateRect = QRect(m_plusRect.left() - 8 - dateW, y + 2, dateW, 16);
    p.setPen(faint);
    p.drawText(m_dateRect, Qt::AlignLeft | Qt::AlignVCenter, date);

    y += kHeaderH;
    p.setPen(QPen(QColor(255, 255, 255, 22), 1));
    p.drawLine(card.left() + kPadX, y, card.right() - kPadX, y);
    y += 8;
    m_rowTop = y;

    // rows
    const int visible = qMax(1, s.visibleRows);
    QFont row = font();
    row.setPointSizeF(8.6);
    QFont time = font();
    time.setPointSizeF(7.2);

    if (m_tasks.isEmpty()) {
        p.setFont(small);
        p.setPen(faint);
        p.drawText(QRect(card.left() + kPadX, y, card.width() - kPadX * 2, m_rowHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("还没有任务"));
    }

    for (int i = 0; i < visible; ++i) {
        const int real = m_scroll + i;
        if (real >= m_tasks.size()) break;
        const Task &t = m_tasks.at(real);
        const QRect rowRect(card.left() + kPadX, y + i * m_rowHeight, card.width() - kPadX * 2, m_rowHeight);
        if (real == m_hoverRow) {
            QPainterPath hp;
            hp.addRoundedRect(rowRect.adjusted(-4, 1, 4, -1), 6, 6);
            p.fillPath(hp, QColor(255, 255, 255, 16));
        }

        // circle indicator
        QRect ring(rowRect.left(), rowRect.center().y() - 5, 10, 10);
        p.setPen(QPen(QColor(255, 255, 255, 60), 1.2));
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(ring);
        if (t.isDone()) {
            p.setBrush(QColor(0x3E, 0xC4, 0x6B));
            p.setPen(Qt::NoPen);
            p.drawEllipse(ring.adjusted(2, 2, -2, -2));
        }

        p.setFont(row);
        p.setPen(t.isDone() ? faint : fg);
        QRect textRect = rowRect.adjusted(20, 0, -48, 0);
        const QString elided = p.fontMetrics().elidedText(t.title, Qt::ElideRight, textRect.width());
        p.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, elided);

        p.setFont(time);
        p.setPen(faint);
        QString right;
        if (s.showCarryMark && t.carryCount > 0) right = QStringLiteral("×%1").arg(t.carryCount);
        if (!right.isEmpty()) {
            const int w = p.fontMetrics().horizontalAdvance(right);
            p.drawText(QRect(rowRect.right() - w, rowRect.top(), w, rowRect.height()),
                       Qt::AlignRight | Qt::AlignVCenter, right);
        }
    }

    // footer hint
    int footY = card.bottom() - 8;
    p.setFont(time);
    p.setPen(faint);
    p.drawText(QRect(card.left() + kPadX, footY - 14, card.width() - kPadX * 2, 14),
               Qt::AlignLeft | Qt::AlignVCenter,
               QStringLiteral("%1 截图 · %2 速记").arg(s.chordFor(QStringLiteral("snip")),
                                                      s.chordFor(QStringLiteral("quick"))));
}

void CardWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) {
        if (m_plusRect.contains(e->pos())) { emit quickAddRequested(); return; }
        const int idx = rowAt(e->pos());
        if (idx >= 0) {
            const Task &t = m_tasks.at(idx);
            m_store->setDone(t.id, !t.isDone());
            return;
        }
        m_dragging = true;
        m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
    }
}

void CardWindow::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragging) {
        move(e->globalPosition().toPoint() - m_dragOffset);
        return;
    }
    const int idx = rowAt(e->pos());
    if (idx != m_hoverRow) { m_hoverRow = idx; update(); }
}

void CardWindow::mouseReleaseEvent(QMouseEvent *)
{
    if (!m_dragging) return;
    m_dragging = false;
    // remember where the user put the card (T9 appearance / position)
    auto &s = Settings::instance();
    s.cardX = x();
    s.cardY = y();
    s.save();
}

void CardWindow::mouseDoubleClickEvent(QMouseEvent *e)
{
    qInfo("card double-click at %d,%d dateRect=%d,%d %dx%d", e->pos().x(), e->pos().y(), m_dateRect.x(),
          m_dateRect.y(), m_dateRect.width(), m_dateRect.height());
    if (e->button() == Qt::LeftButton && m_dateRect.contains(e->pos())) {
        emit calendarRequested();   // T8: double-click the date to open the calendar
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

void CardWindow::wheelEvent(QWheelEvent *e)
{
    const int step = e->angleDelta().y() > 0 ? -1 : 1;
    const int maxScroll = qMax(0, m_tasks.size() - qMax(1, Settings::instance().visibleRows));
    const int next = qBound(0, m_scroll + step, maxScroll);
    if (next != m_scroll) { m_scroll = next; m_hoverRow = -1; update(); }
    e->accept();
}

void CardWindow::contextMenuEvent(QContextMenuEvent *e)
{
    QMenu menu(this);
    const int idx = rowAt(e->pos());
    if (idx >= 0) {
        const Task t = m_tasks.at(idx);
        menu.addAction(t.isDone() ? QStringLiteral("标为未完成") : QStringLiteral("✓ 完成"),
                       [this, t] { m_store->setDone(t.id, !t.isDone()); });
        auto *pri = menu.addMenu(QStringLiteral("优先级"));
        const QStringList names{QStringLiteral("P0 关键"), QStringLiteral("P1 高"),
                                QStringLiteral("P2 普通"), QStringLiteral("P3 低")};
        for (int i = 0; i < names.size(); ++i)
            pri->addAction(names[i], [this, t, i] { m_store->setPriority(t.id, i); });
        menu.addAction(QStringLiteral("顺延到明天"), [this, t] { m_store->postpone(t.id); });
        menu.addAction(QStringLiteral("复制文字"), [t] { qApp->clipboard()->setText(t.title); });
        menu.addSeparator();
        menu.addAction(QStringLiteral("删除"), [this, t] { m_store->drop(t.id); });
    } else {
        menu.addAction(QStringLiteral("新建任务"), this, &CardWindow::quickAddRequested);
        menu.addAction(QStringLiteral("回收站…"), this, &CardWindow::recycleBinRequested);
        menu.addAction(QStringLiteral("日历…"), this, &CardWindow::calendarRequested);
        menu.addAction(QStringLiteral("设置…"), this, &CardWindow::settingsRequested);
        menu.addSeparator();
        auto *top = menu.addAction(QStringLiteral("全局置顶显示"));
        top->setCheckable(true);
        top->setChecked(Settings::instance().alwaysOnTop);
        connect(top, &QAction::triggered, this, [this](bool checked) { setDesktopLayer(!checked); });
        menu.addAction(QStringLiteral("隐藏卡片"), this, &QWidget::hide);
    }
    menu.exec(e->globalPos());
}
