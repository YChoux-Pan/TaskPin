#pragma once
#include <QRect>
#include <QString>
#include <QVector>

struct SnipAction {
    QString id;
    QRect rect;
    bool enabled = true;
    QString label;
    QString shortcut;
    int group = 0;
};

namespace SnipToolbar
{
QString label(const QString &id);
QString shortcut(const QString &id);
// Wrap whole buttons into bounded rows, preferring a free side of the selection.
QRect layout(QVector<SnipAction> &actions, const QRect &viewport, const QRect &selection);
}
