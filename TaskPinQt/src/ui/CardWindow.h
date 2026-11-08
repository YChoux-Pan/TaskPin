#pragma once

#include "core/Task.h"

#include <QList>
#include <QWidget>

class Store;

/// Frameless desktop card: today's tasks, drag to move, wheel to scroll, right-click for actions.
class CardWindow : public QWidget {
    Q_OBJECT
public:
    explicit CardWindow(Store *store, QWidget *parent = nullptr);

    void refresh();
    void placeOnScreen();
    /// Re-applies settings.json (row count, width, colours, top-most flag).
    void applySettings();

signals:
    void quickAddRequested();
    void settingsRequested();
    void recycleBinRequested();
    void calendarRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    int rowAt(const QPoint &p) const;
    void relayout();

    Store *m_store;
    QList<Task> m_tasks;
    int m_scroll = 0;
    int m_hoverRow = -1;
    bool m_dragging = false;
    QPoint m_dragOffset;

    QRect m_dateRect;
    QRect m_plusRect;
    int m_rowTop = 0;
    int m_rowHeight = 0;
};
