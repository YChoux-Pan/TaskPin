#include "ui/CalendarWindow.h"

#include <QFont>
#include <QKeyEvent>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

namespace {
const QColor kBg(0x12, 0x16, 0x1C);
const QColor kPanel(0x1A, 0x20, 0x28);
const QColor kText(0xE8, 0xEC, 0xF2);
const QColor kDim(0x7F, 0x8B, 0x9B);
const QColor kFaint(0x4A, 0x55, 0x63);
const QColor kAccent(0x4C, 0x9A, 0xFF);
const QColor kDone(0x3E, 0xC4, 0x6B);
const QColor kWarn(0xFF, 0xB0, 0x20);
const QColor kLine(0x25, 0x2E, 0x3A);

QFont yahei(qreal points, bool bold = false)
{
    QFont f(QStringLiteral("Microsoft YaHei UI"));
    f.setPointSizeF(points);
    f.setBold(bold);
    return f;
}

QPainterPath rounded(const QRectF &r, qreal radius)
{
    QPainterPath path;
    path.addRoundedRect(r, radius, radius);
    return path;
}
}   // namespace

CalendarWindow::CalendarWindow(Store *store, QWidget *parent)
    : QWidget(parent), m_store(store)
{
    setWindowTitle(QStringLiteral("TaskPin 日历"));
    setWindowFlag(Qt::WindowStaysOnTopHint, true);
    resize(860, 620);
    setMinimumSize(720, 560);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent, true);

    m_month = QDate(QDate::currentDate().year(), QDate::currentDate().month(), 1);
    m_selected = QDate::currentDate();

    connect(m_store, &Store::changed, this, [this] { reload(); });
    reload();
}

void CalendarWindow::setSelectedDay(const QDate &day)
{
    if (!day.isValid()) return;
    m_selected = day;
    m_month = QDate(day.year(), day.month(), 1);
    reload();
}

void CalendarWindow::reload()
{
    const QDate last = m_month.addMonths(1).addDays(-1);
    m_stats = m_store->dayStats(m_month, last);
    m_dayTasks = m_store->dayTasks(m_selected);
    update();
}

void CalendarWindow::goMonth(int delta)
{
    m_month = m_month.addMonths(delta);
    m_selected = m_month;
    reload();
}

void CalendarWindow::goToday()
{
    const QDate today = QDate::currentDate();
    m_month = QDate(today.year(), today.month(), 1);
    m_selected = today;
    reload();
}

void CalendarWindow::keyPressEvent(QKeyEvent *e)
{
    switch (e->key()) {
    case Qt::Key_Left: goMonth(-1); return;
    case Qt::Key_Right: goMonth(1); return;
    case Qt::Key_Escape: close(); return;
    default: break;
    }
    QWidget::keyPressEvent(e);
}

void CalendarWindow::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const QPoint pos = e->pos();
    if (m_prevRect.contains(pos)) {
        goMonth(-1);
        return;
    }
    if (m_nextRect.contains(pos)) {
        goMonth(1);
        return;
    }
    if (m_todayRect.contains(pos)) {
        goToday();
        return;
    }
    for (const auto &cell : m_cells) {
        if (!cell.first.contains(pos)) continue;
        m_selected = cell.second;
        reload();
        return;
    }
}

void CalendarWindow::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), kBg);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);
    drawHeader(p);
    drawGrid(p);
    drawDayPanel(p);
}

void CalendarWindow::drawHeader(QPainter &p)
{
    const QLocale zh(QLocale::Chinese, QLocale::China);
    p.setFont(yahei(15, true));
    p.setPen(kText);
    p.drawText(QRect(24, 14, width() - 320, 34), Qt::AlignLeft | Qt::AlignVCenter,
               zh.toString(m_month, QStringLiteral("yyyy 年 M 月")));

    m_prevRect = QRect(width() - 250, 20, 32, 28);
    m_nextRect = QRect(width() - 210, 20, 32, 28);
    m_todayRect = QRect(width() - 164, 20, 64, 28);

    p.setPen(QPen(QColor(0x3A, 0x45, 0x55), 1.4));
    p.setBrush(kPanel);
    for (const QRect &r : {m_prevRect, m_nextRect, m_todayRect}) p.drawPath(rounded(r, 7));

    p.setFont(yahei(10));
    p.setPen(kDim);
    p.drawText(m_prevRect, Qt::AlignCenter, QStringLiteral("‹"));
    p.drawText(m_nextRect, Qt::AlignCenter, QStringLiteral("›"));
    p.setFont(yahei(8.5));
    p.drawText(m_todayRect, Qt::AlignCenter, QStringLiteral("今天"));

    p.setPen(QPen(kLine, 1));
    p.drawLine(24, 62, width() - 24, 62);
}

void CalendarWindow::drawGrid(QPainter &p)
{
    m_cells.clear();

    const int left = 24;
    const int top = 96;
    const int right = width() - 300;
    const int bottom = height() - 24;
    const qreal cellW = (right - left) / 7.0;
    const qreal cellH = (bottom - top) / 6.0;

    p.setFont(yahei(8.6));
    p.setPen(kDim);
    const QStringList names{QStringLiteral("一"), QStringLiteral("二"), QStringLiteral("三"), QStringLiteral("四"),
                            QStringLiteral("五"), QStringLiteral("六"), QStringLiteral("日")};
    for (int i = 0; i < 7; ++i) {
        const QRect r(qRound(left + i * cellW), top - 28, qRound(cellW), 20);
        p.drawText(r, Qt::AlignCenter, names.at(i));
    }

    const int offset = m_month.dayOfWeek() - 1;   // Monday first
    const int days = m_month.daysInMonth();
    const QDate today = QDate::currentDate();

    for (int d = 1; d <= days; ++d) {
        const int idx = offset + d - 1;
        const int col = idx % 7;
        const int row = idx / 7;
        const QRect rect(qRound(left + col * cellW) + 2, qRound(top + row * cellH) + 2,
                         qRound(cellW) - 4, qRound(cellH) - 4);
        const QDate day(m_month.year(), m_month.month(), d);
        m_cells.append({rect, day});

        const bool isToday = day == today;
        const bool isSelected = day == m_selected;

        if (isSelected) {
            p.setPen(QPen(kAccent, 1.4));
            p.setBrush(QColor(0x4C, 0x9A, 0xFF, 60));
            p.drawPath(rounded(rect, 8));
        } else if (isToday) {
            p.setPen(Qt::NoPen);
            p.setBrush(kPanel);
            p.drawPath(rounded(rect, 8));
        }

        p.setFont(yahei(10, isToday));
        p.setPen(kText);
        p.drawText(QRect(rect.left() + 8, rect.top() + 4, rect.width() - 12, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QString::number(d));

        const DayStat st = m_stats.value(day.toString("yyyy-MM-dd"));
        int y = rect.top() + 26;
        p.setFont(yahei(7.4));
        if (st.pending > 0) {
            p.setPen(kAccent);
            p.drawText(QRect(rect.left() + 8, y, rect.width() - 12, 12), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 待办").arg(st.pending));
            y += 12;
        }
        if (st.done > 0) {
            p.setPen(kDone);
            p.drawText(QRect(rect.left() + 8, y, rect.width() - 12, 12), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 完成").arg(st.done));
            y += 12;
        }
        if (st.carried > 0) {
            p.setPen(kWarn);
            p.drawText(QRect(rect.left() + 8, y, rect.width() - 12, 12), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("%1 顺延").arg(st.carried));
        }
        if (st.pending == 0 && st.done == 0 && st.carried == 0 &&
            (day.dayOfWeek() == 6 || day.dayOfWeek() == 7)) {
            p.setPen(kFaint);
            p.drawText(QRect(rect.left() + 8, rect.top() + 26, 20, 12), Qt::AlignLeft | Qt::AlignVCenter,
                       QStringLiteral("·"));
        }
    }
}

void CalendarWindow::drawDayPanel(QPainter &p)
{
    const QLocale zh(QLocale::Chinese, QLocale::China);
    const QRect panel(width() - 288, 96, 264, height() - 120);
    p.setPen(Qt::NoPen);
    p.setBrush(kPanel);
    p.drawPath(rounded(panel, 12));

    p.setFont(yahei(11, true));
    p.setPen(kText);
    p.drawText(QRect(panel.left() + 16, panel.top() + 10, panel.width() - 32, 26), Qt::AlignLeft | Qt::AlignVCenter,
               zh.toString(m_selected, QStringLiteral("M月d日 dddd")));

    int y = panel.top() + 46;
    p.setFont(yahei(8));
    if (m_dayTasks.isEmpty()) {
        p.setPen(kDim);
        p.drawText(QRect(panel.left() + 16, y, panel.width() - 32, 20), Qt::AlignLeft | Qt::AlignVCenter,
                   QStringLiteral("这一天没有任务记录"));
        return;
    }

    p.setFont(yahei(9));
    for (const DayTask &t : m_dayTasks) {
        if (y + 20 > panel.bottom() - 30) break;
        QColor color = kAccent;
        QString mark;
        if (t.state == QStringLiteral("done")) {
            color = kDone;
            mark = QStringLiteral("✓ ");
        } else if (t.state == QStringLiteral("carried")) {
            color = kWarn;
            mark = QStringLiteral("→ ");
        }
        p.setPen(Qt::NoPen);
        p.setBrush(color);
        p.drawEllipse(QPoint(panel.left() + 19, y + 9), 3, 3);

        const QString caption = p.fontMetrics().elidedText(mark + t.title, Qt::ElideRight, panel.width() - 48);
        p.setPen(t.state == QStringLiteral("done") ? kDone : kText);
        p.drawText(QRect(panel.left() + 28, y, panel.width() - 44, 20), Qt::AlignLeft | Qt::AlignVCenter, caption);
        y += 24;
    }

    p.setFont(yahei(8));
    p.setPen(kDim);
    p.drawText(QRect(panel.left() + 16, panel.bottom() - 26, panel.width() - 32, 18),
               Qt::AlignLeft | Qt::AlignVCenter, QStringLiteral("共 %1 条").arg(m_dayTasks.size()));
}
