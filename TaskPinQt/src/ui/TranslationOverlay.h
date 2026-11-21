#pragma once
#include "platform/ScreenCapture.h"
#include <QWidget>

class TranslationOverlay : public QWidget {
    Q_OBJECT
public:
    explicit TranslationOverlay(const ScreenCapture &shot, QWidget *parent = nullptr);
    static QRect pixelRect(const QRectF &selection, const QSize &logicalSize, const QSize &pixelSize);
signals:
    void selected(const QImage &image, const QRect &globalLogicalRect);
    void cancelled();
protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
private:
    ScreenCapture m_shot;
    QPointF m_start, m_end;
    bool m_dragging = false;
    QString m_hint;
};
