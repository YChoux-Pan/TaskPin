#pragma once

#include <QImage>
#include <QString>
#include <QStringList>

/// Keeps the most recent snips on disk (dataDir/history/snip) so they can be re-used later.
namespace History {

QString dir();

/// Writes "<dir>/snip-yyyyMMdd-HHmmss-zzz.png" and trims to the configured limit. Empty on failure.
QString save(const QImage &image);

/// Newest first.
QStringList recent();

/// Deletes the oldest files beyond the configured limit.
void trim();

}   // namespace History
