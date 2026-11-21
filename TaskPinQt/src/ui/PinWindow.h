#pragma once

#include <QImage>
#include <QColor>
#include <QTimer>
#include <QVariantAnimation>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>
#include <QTransform>

class QMenu;

/// A screenshot pinned on top of the desktop: drag to move, wheel to zoom, Ctrl+wheel for
/// opacity, right-click for rotation / groups / click-through.
class PinWindow : public QWidget {
    Q_OBJECT
public:
    /// sourceRegion is the global logical capture rectangle; pixels remain at source resolution.
    explicit PinWindow(const QImage &image, const QString &group = QString(), QWidget *parent = nullptr,
                       const QRect &sourceRegion = QRect());
    ~PinWindow() override;

    static const QList<PinWindow *> &all();

    static void closeAll();
    static void closeGroup(const QString &group);
    static void switchGroup(const QString &group);
    static void flashAll();
    /// Hides everything when any pin is visible, otherwise reveals the current group.
    static bool toggleAll();
    /// Safety hatch: click-through pins cannot be right-clicked any more, so the tray can reset them.
    static void clearClickThrough();

    static QStringList groups();
    static QString currentGroup();
    static void setCurrentGroup(const QString &group);
    static QString ensureGroup(const QString &name);

    /// Renders clipboard text as a dark card and pins it (P10).
    static PinWindow *createTextPin(const QString &text);

    QString group() const { return m_group; }
    QImage image() const { return m_image; }
    qreal zoom() const { return m_zoom; }
    qreal pinOpacity() const { return m_opacity; }
    bool clickThrough() const { return m_clickThrough; }
    QRect contentGeometry() const;
    void applyAppearance(int shadowStrength, int borderWidth, const QColor &borderColor);

signals:
    void recognizeRequested(const QImage &image);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    virtual void extendMenu(QMenu *) {}
    virtual bool allowsClipboardReplacement() const { return true; }
    void replaceFrame(const QImage &image, const QSize &logicalSize, bool resetView = false);

private:
    void relayout();
    void setZoom(qreal zoom);
    void displayZoom(qreal zoom, const QPointF &center);
    void rebuildCache();
    void rebuildPreview();
    int padding() const;
    void setPinOpacity(qreal opacity);
    void resetDisplay();
    void flash();
    void toggleClickThrough();
    void replaceFromClipboard();
    void saveImage();
    void buildMenu(QMenu *menu);
    void applyGroup(const QString &group);
    static QImage renderTextImage(const QString &text);

    QImage m_image;
    QTransform m_frameTransform;
    QSize m_logicalImageSize;
    QString m_group;
    qreal m_zoom = 1.0;
    qreal m_targetZoom = 1.0;
    QPointF m_zoomCenter;
    QVariantAnimation m_zoomAnimation;
    QTimer m_qualityTimer;
    QImage m_preview;
    QImage m_cache;
    int m_shadowStrength = 18;
    int m_borderWidth = 1;
    QColor m_borderColor;
    qreal m_opacity = 1.0;
    bool m_clickThrough = false;
    bool m_menuOpen = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
    int m_flashTicks = 0;
};
