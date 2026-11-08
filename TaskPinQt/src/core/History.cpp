#include "core/History.h"

#include "core/Settings.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace History {

QString dir() { return Settings::dataDir() + QStringLiteral("/history/snip"); }

QString save(const QImage &image)
{
    if (image.isNull()) return {};
    QDir().mkpath(dir());
    const QString name =
        QStringLiteral("snip-%1.png").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"));
    const QString path = QDir(dir()).filePath(name);
    if (!image.save(path, "PNG")) {
        qWarning() << "history save failed:" << path;
        return {};
    }
    trim();
    return path;
}

QStringList recent()
{
    QDir d(dir());
    if (!d.exists()) return {};
    QFileInfoList entries = d.entryInfoList({QStringLiteral("*.png")}, QDir::Files, QDir::Time | QDir::Reversed);
    QStringList out;
    out.reserve(entries.size());
    for (const auto &fi : entries) out.append(fi.absoluteFilePath());
    // QDir::Time sorts newest first; Reversed would flip it, so sort explicitly to stay predictable
    std::sort(out.begin(), out.end(), [](const QString &a, const QString &b) {
        return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
    });
    return out;
}

void trim()
{
    const int limit = qMax(5, Settings::instance().historyLimit);
    const QStringList files = recent();
    for (int i = limit; i < files.size(); ++i) QFile::remove(files.at(i));
}

}   // namespace History
