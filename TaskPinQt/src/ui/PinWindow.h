#pragma once

#include <QImage>
#include <QList>
#include <QString>
#include <QStringList>
#include <QWidget>

class QMenu;

/// A screenshot pinned on top of the desktop: drag to move, wheel to zoom, Ctrl+wheel for
/// opacity, right-click for rotation / groups / click-through.
class PinWindow : public QWidget {
    Q_OBJECT
public:
    explicit PinWindow(const QImage &image, const QString &group = QString(), QWidget *parent = nullptr);
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

signals:
    void recognizeRequested(const QImage &image);

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    void relayout();
    void setZoom(qreal zoom);
    void setPinOpacity(qreal opacity);
    void flash();
    void toggleClickThrough();
    void replaceFromClipboard();
    void saveImage();
    void buildMenu(QMenu *menu);
    void applyGroup(const QString &group);
    static QImage renderTextImage(const QString &text);

    QImage m_image;
    QString m_group;
    qreal m_zoom = 1.0;
    qreal m_opacity = 1.0;
    bool m_clickThrough = false;
    bool m_menuOpen = false;
    bool m_dragging = false;
    QPoint m_dragOffset;
    int m_flashTicks = 0;
};
