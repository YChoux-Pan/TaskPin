#include "core/InstanceNames.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

namespace {
QString resolvedDirectory(const QString &directory, int remainingAliases = 64)
{
    QString ancestor = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    QStringList components;
    // Resolve parents too: Qt's Windows canonicalFilePath preserves junction names.
    // Walking from the root also keeps missing suffixes stable across mkdir().
    for (;;) {
        const QFileInfo info(ancestor);
        const QString parent = info.absolutePath();
        if (parent == ancestor) break;
        components.prepend(info.fileName());
        ancestor = parent;
    }
    QString resolved = ancestor;
    for (const QString &component : components) {
        const QFileInfo info(QDir(resolved).filePath(component));
        const QString target = info.isJunction() ? info.junctionTarget() : info.symLinkTarget();
        if (!target.isEmpty() && remainingAliases > 0) {
            resolved = resolvedDirectory(target, --remainingAliases);
        } else {
            const QString canonical = info.canonicalFilePath();
            resolved = canonical.isEmpty() ? info.absoluteFilePath() : canonical;
        }
    }
    return QDir::cleanPath(QDir::fromNativeSeparators(resolved));
}
}

InstanceNames instanceNamesFor(const QString &customDataDirectory)
{
    QString suffix;
    if (!customDataDirectory.isEmpty()) {
        QString path = resolvedDirectory(QDir::cleanPath(QDir::fromNativeSeparators(customDataDirectory)));
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        suffix = QLatin1Char('.') + QString::fromLatin1(
            QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    return {QStringLiteral("TaskPinQt.Cmd") + suffix, QStringLiteral("TaskPinQt.SingleInstance") + suffix};
}

bool taskPinAutomation() { return qEnvironmentVariable("TASKPIN_AUTOMATION") == QLatin1String("1"); }
