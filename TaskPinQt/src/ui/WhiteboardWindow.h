#pragma once

#include "platform/ScreenCapture.h"
#include "ui/Annotation.h"

#include <QColor>
#include <QImage>
#include <QVector>
#include <QWidget>

/// Handwriting board: frozen-screen whiteboard (W1) or ink floating over the desktop (W2).
class WhiteboardWindow : public QWidget {
    Q_OBJECT
public:
    explicit WhiteboardWindow(const ScreenCapture &shot, bool transparent, QWidget *parent = nullptr);

signals:
    void copied(const QImage &image);
    void saved(const QString &path);
    void printRequested(const QImage &image);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    void drawToolbar(QPainter &p);
    QString hitToolbar(const QPoint &pos) const;
    void handleAction(const QString &id);
    QImage compose() const;
    void undo();
    void clearAll();

    ScreenCapture m_shot;
    QImage m_base;
    bool m_transparent = false;

    QColor m_color{0xE5, 0x3B, 0x3B};
    int m_width = 4;

    QVector<Annotation> m_strokes;
    QVector<Annotation> m_redo;
    Annotation m_current;
    bool m_drawing = false;

    struct Hot {
        QRect rect;
        QString id;
    };
    QVector<Hot> m_hot;
    QRect m_toolbarRect;
};
