#pragma once

#include <QIcon>
#include <QString>

// One Canva-derived brand asset for application windows, the tray and the calendar.
inline QIcon taskpinIcon()
{
    static const QIcon icon = [] {
        QIcon result;
        for (int size : {16, 20, 24, 32, 40, 48, 64, 128, 256})
            result.addFile(QStringLiteral(":/brand/taskpin-%1.png").arg(size), QSize(size, size));
        return result;
    }();
    return icon;
}
