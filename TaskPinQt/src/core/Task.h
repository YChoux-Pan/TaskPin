#pragma once

#include <QString>
#include <QStringList>

struct Task {
    QString id;
    QString title;
    QString note;
    QString ocrText;
    QString source = "manual";
    QString tags;
    QString due;          // ISO string or empty
    int priority = 2;     // 0..3
    qint64 sortKey = 0;
    QString state = "open";   // open | done | dropped
    QString created;
    QString updated;
    QString completed;
    QString originDate;
    QString lastDay;
    int carryCount = 0;
    QString attachment;
    QString aiState = "none";

    bool isDone() const { return state == "done"; }
};
