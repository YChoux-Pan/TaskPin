#include "core/FileNaming.h"

#include <QDir>
#include <QFileInfo>

namespace FileNaming {

QString defaultPattern() { return QStringLiteral("TaskPin-{yyyy}{MM}{dd}-{HH}{mm}{ss}"); }

QString expand(const QString &pattern, const QDateTime &when, const QString &extension)
{
    QString p = pattern.trimmed();
    if (p.isEmpty()) p = defaultPattern();

    p.replace("{yyyy}", when.toString("yyyy"));
    p.replace("{yy}", when.toString("yy"));
    p.replace("{MM}", when.toString("MM"));
    p.replace("{dd}", when.toString("dd"));
    p.replace("{HH}", when.toString("HH"));
    p.replace("{mm}", when.toString("mm"));
    p.replace("{ss}", when.toString("ss"));
    p.replace("{fff}", when.toString("zzz"));
    p.replace("{date}", when.toString("yyyy-MM-dd"));
    p.replace("{time}", when.toString("HH-mm-ss"));

    QString name = sanitize(p);
    if (name.isEmpty()) name = QStringLiteral("TaskPin-") + when.toString("yyyyMMdd-HHmmss");
    if (!name.endsWith(extension, Qt::CaseInsensitive)) name += extension;
    return name;
}

QString sanitize(const QString &name)
{
    if (name.isEmpty()) return {};
    static const QString invalid = QStringLiteral("<>:\"/\\|?*");
    QString out;
    out.reserve(name.size());
    for (const QChar c : name) {
        const bool bad = invalid.contains(c) || c.unicode() < 32;
        out.append(bad ? QChar('_') : c);
    }
    out = out.trimmed();
    while (out.endsWith('.')) out.chop(1);
    if (out.size() > 120) out.truncate(120);
    return out;
}

QString uniquePath(const QString &dir, const QString &pattern, const QDateTime &when, const QString &extension)
{
    const QString name = expand(pattern, when, extension);
    const QString stem = name.left(name.size() - extension.size());
    QString path = QDir(dir).filePath(name);
    int i = 1;
    while (QFileInfo::exists(path)) {
        path = QDir(dir).filePath(QStringLiteral("%1-%2%3").arg(stem).arg(i++).arg(extension));
        if (i > 9999) break;
    }
    return path;
}

}   // namespace FileNaming
