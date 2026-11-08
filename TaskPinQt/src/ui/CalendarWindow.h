#pragma once

#include "core/Store.h"

#include <QDate>
#include <QMap>
#include <QVector>
#include <QWidget>

/// Month calendar over task_day: every task that existed on a day stays visible later (K1–K4).
class CalendarWindow : public QWidget {
    Q_OBJECT
public:
    explicit CalendarWindow(Store *store, QWidget *parent = nullptr);

    void setSelectedDay(const QDate &day);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    void reload();
    void drawHeader(QPainter &p);
    void drawGrid(QPainter &p);
    void drawDayPanel(QPainter &p);
    void goMonth(int delta);
    void goToday();

    Store *m_store;
    QDate m_month;      // first day of the displayed month
    QDate m_selected;
    QMap<QString, DayStat> m_stats;
    QList<DayTask> m_dayTasks;
    QVector<QPair<QRect, QDate>> m_cells;
    QRect m_prevRect;
    QRect m_nextRect;
    QRect m_todayRect;
};
