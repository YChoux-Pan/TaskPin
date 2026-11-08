#pragma once

#include "platform/ScreenCapture.h"
#include "ui/Annotation.h"

#include <QColor>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QRect>
#include <QVector>
#include <QWidget>

/// Frozen-screen editor: select a region (freely, snapped to a window, or full screen),
/// annotate it, then copy / save / pin / recognise / print.
class SnipOverlay : public QWidget {
    Q_OBJECT
public:
    explicit SnipOverlay(const ScreenCapture &shot, QWidget *parent = nullptr);
    ~SnipOverlay() override;

    /// Pre-selects a rectangle (used by "capture last region").
    void setPreselect(const QRect &logicalRect);

    QRect selection() const;

signals:
    void copied(const QImage &image);
    void saved(const QString &path);
    void pinned(const QImage &image);
    void recognizeRequested(const QImage &image);
    void printed(const QImage &image);
    /// Emitted once for every successful output so the host can keep history + auto save.
    void captured(const QImage &image, const QRect &logicalRegion);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    enum class Tool {
        Select, Rect, Ellipse, Arrow, Line, Pen, Highlighter, Text, Number, Mosaic, Blur, Eraser
    };
    enum class Mode { Idle, SelectRect, Draw, MoveAnnotation, ResizeSelection, Erase };

    void drawToolbar(QPainter &p);
    void drawSelection(QPainter &p);
    void drawMagnifier(QPainter &p);
    void drawHints(QPainter &p);
    void drawHoverWindow(QPainter &p);

    QString hitToolbar(const QPoint &pos) const;
    void handleAction(const QString &id);
    void setTool(Tool t);

    int selectionHandleAt(const QPoint &pos) const;
    void drawHandle(QPainter &p, const QRect &sel) const;

    QImage compose() const;
    void beginText(const QPoint &at);
    void commitText();
    void cancelText();
    void undo();
    void redo();
    void eraseAt(const QPoint &pos);
    void updateHoverWindow(const QPoint &pos);
    void nudgeSelection(Qt::KeyboardModifiers mods, const QPoint &delta);
    void saveQuick();
    void emitOutput(const QImage &img, const QString &action);

    ScreenCapture m_shot;
    QImage m_base;          // frozen desktop in device pixels (cached for mosaic/blur)

    QRect m_sel;
    bool m_hasSel = false;
    QPoint m_dragStart;
    QPoint m_dragLast;
    QPoint m_cursor;
    Mode m_mode = Mode::Idle;
    int m_resizeHandle = -1;
    QRect m_selAtPress;
    bool m_dragMoved = false;

    QRect m_hoverWindow;
    QString m_hoverTitle;
    QPoint m_lastPickPos;
    QElapsedTimer m_pickClock;

    Tool m_tool = Tool::Select;
    QColor m_color{0xE5, 0x3B, 0x3B};
    int m_width = 3;
    int m_fontSize = 18;
    int m_block = 14;
    int m_nextNumber = 1;

    QVector<Annotation> m_items;
    QVector<Annotation> m_redoStack;
    Annotation m_current;
    bool m_drawing = false;
    int m_selected = -1;

    QLineEdit *m_textEdit = nullptr;
    QPoint m_textPos;

    struct Hot {
        QRect rect;
        QString id;
    };
    QVector<Hot> m_hot;
    QRect m_toolbarRect;
};
