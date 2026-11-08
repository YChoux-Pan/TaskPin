#include "platform/ScreenCapture.h"

#include <QGuiApplication>
#include <QPainter>
#include <QScreen>

ScreenCapture captureVirtualDesktop()
{
    ScreenCapture out;
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) return out;

    QRect virt;
    for (auto *s : screens) virt = virt.united(s->geometry());
    if (!virt.isValid()) return out;

    QScreen *primary = QGuiApplication::primaryScreen();
    const qreal dpr = primary ? primary->devicePixelRatio() : 1.0;

    QPixmap canvas(QSize(qRound(virt.width() * dpr), qRound(virt.height() * dpr)));
    canvas.fill(Qt::black);
    {
        QPainter p(&canvas);
        for (auto *s : screens) {
            QPixmap shot = s->grabWindow(0);           // device pixels of that screen
            if (shot.isNull()) continue;
            shot.setDevicePixelRatio(1.0);
            const QRect g = s->geometry();
            const QRect target(qRound((g.x() - virt.x()) * dpr), qRound((g.y() - virt.y()) * dpr),
                               qRound(g.width() * dpr), qRound(g.height() * dpr));
            p.drawPixmap(target, shot, shot.rect());
        }
    }
    canvas.setDevicePixelRatio(dpr);

    out.pixmap = canvas;
    out.logicalRect = virt;
    out.dpr = dpr;
    return out;
}
