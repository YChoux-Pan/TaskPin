#pragma once

#include "platform/ScreenCapture.h"

#include <QColor>
#include <QImage>
#include <QWidget>

/// Full-screen eyedropper: 8x magnifier, hex + RGB readout, click to pick (C# ColorPickerOverlay).
class ColorPickerOverlay : public QWidget {
    Q_OBJECT
public:
    explicit ColorPickerOverlay(const ScreenCapture &shot, QWidget *parent = nullptr);

signals:
    void picked(const QColor &color);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;

private:
    QColor colorAt(const QPoint &logical) const;
    void drawMagnifier(QPainter &p);

    ScreenCapture m_shot;
    QImage m_base;
    QPoint m_cursor;
    int m_zoom = 10;
};
