#include "core/InstanceNames.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>

InstanceNames instanceNamesFor(const QString &customDataDirectory)
{
    QString suffix;
    if (!customDataDirectory.isEmpty()) {
        const QFileInfo directory(QDir::cleanPath(QDir::fromNativeSeparators(customDataDirectory)));
        QString path = directory.canonicalFilePath();
        if (path.isEmpty()) path = directory.absoluteFilePath();
        path = QDir::cleanPath(QDir::fromNativeSeparators(path));
#ifdef Q_OS_WIN
        path = path.toCaseFolded();
#endif
        suffix = QLatin1Char('.') + QString::fromLatin1(
            QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha256).toHex());
    }
    return {QStringLiteral("TaskPinQt.Cmd") + suffix, QStringLiteral("TaskPinQt.SingleInstance") + suffix};
}

bool taskPinAutomation() { return qEnvironmentVariable("TASKPIN_AUTOMATION") == QLatin1String("1"); }
