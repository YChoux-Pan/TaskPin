#pragma once

#include "core/Task.h"

#include <QList>
#include <QWidget>

class Store;

/// Frameless desktop card: today's tasks, drag to move, wheel to scroll, right-click for actions.
/// Two z-order modes: always-on-top, or the "desktop layer" (above the wallpaper, below every
/// normal window) — toggled from the tray, the card's own menu, the settings dialog or the CLI.
class CardWindow : public QWidget {
    Q_OBJECT
public:
    explicit CardWindow(Store *store, QWidget *parent = nullptr);

    void refresh();
    void placeOnScreen();
    /// Re-applies settings.json (row count, width, colours, z-order mode).
    void applySettings();

    /// True when the card sits on the desktop layer instead of being topmost.
    bool isDesktopLayer() const { return m_desktopLayer; }
    /// Re-reads settings and re-asserts the native z-order.
    void applyZOrder();

public slots:
    /// Switches between topmost (false) and desktop layer (true) and persists the choice.
    void setDesktopLayer(bool on);

signals:
    void quickAddRequested();
    void settingsRequested();
    void recycleBinRequested();
    void calendarRequested();
    void zOrderChanged();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void showEvent(QShowEvent *e) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    int rowAt(const QPoint &p) const;
    void relayout();

    Store *m_store;
    QList<Task> m_tasks;
    int m_scroll = 0;
    int m_hoverRow = -1;
    bool m_dragging = false;
    QPoint m_dragOffset;
    bool m_desktopLayer = false;

    QRect m_dateRect;
    QRect m_plusRect;
    int m_rowTop = 0;
    int m_rowHeight = 0;
};
