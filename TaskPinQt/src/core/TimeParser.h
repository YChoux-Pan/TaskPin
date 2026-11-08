#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

/// Result of turning "明天15点前 给王总发报价 #报价 ！！" into structured fields.
struct ParsedTask {
    QString title;
    QDateTime due;        // invalid when the text carries no date/time
    int priority = 2;     // 2 normal, 1 for one "!", 0 for "!!"
    QStringList tags;
    QString raw;
};

/// Regex-only, offline, never throws. Always keeps the raw text in ParsedTask::raw.
namespace TimeParser {

ParsedTask parse(const QString &raw, const QDateTime &now = QDateTime::currentDateTime());

/// "今天" / "明天15:00" / "逾期2天" / "11/20" — empty when the due is invalid.
QString describeDue(const QDateTime &due, const QDateTime &now = QDateTime::currentDateTime());

}   // namespace TimeParser
