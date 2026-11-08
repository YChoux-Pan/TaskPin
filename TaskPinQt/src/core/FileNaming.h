#pragma once

#include <QDateTime>
#include <QString>

/// Snipaste-style file name patterns such as "TaskPin-{yyyy}{MM}{dd}-{HH}{mm}{ss}".
namespace FileNaming {

QString defaultPattern();

/// Expands {yyyy} {yy} {MM} {dd} {HH} {mm} {ss} {fff} {date} {time} and sanitises the result.
QString expand(const QString &pattern, const QDateTime &when, const QString &extension = QStringLiteral(".png"));

/// Makes an arbitrary string safe as one NTFS path segment.
QString sanitize(const QString &name);

/// "<dir>/<expanded pattern>.png" with a "-N" suffix when the file already exists.
QString uniquePath(const QString &dir, const QString &pattern, const QDateTime &when,
                   const QString &extension = QStringLiteral(".png"));

}   // namespace FileNaming
