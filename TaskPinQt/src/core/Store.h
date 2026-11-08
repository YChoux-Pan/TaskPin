#pragma once

#include "core/Task.h"

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSqlDatabase>
#include <QString>

/// Aggregated task_day counters for one calendar day.
struct DayStat {
    int pending = 0;   // task_day.state = 'open'
    int done = 0;      // task_day.state = 'done'
    int carried = 0;   // task_day.state = 'carried'
};

/// One task as it appeared on a given day (used by the calendar's right-hand panel).
struct DayTask {
    QString id;
    QString title;
    QString state;   // open | done | carried | dropped
};

/// SQLite storage, schema-compatible with the C# build so data can be shared later.
class Store : public QObject {
    Q_OBJECT
public:
    explicit Store(QObject *parent = nullptr);
    ~Store() override;

    bool open(const QString &path);
    bool isOpen() const { return m_open; }

    QList<Task> openTasks() const;
    QList<Task> deletedTasks() const;
    int openCount() const;
    int countByState(const QString &state) const;

    QString addTask(const QString &title, const QString &source = QStringLiteral("manual"),
                    const QString &attachment = QString(), const QDateTime &due = QDateTime(),
                    int priority = -1, const QString &ocrText = QString());
    void setDone(const QString &id, bool done);
    void setPriority(const QString &id, int priority);
    void setDue(const QString &id, const QDateTime &due);
    void postpone(const QString &id);
    void drop(const QString &id);
    void restore(const QString &id);
    void purge(const QString &id);

    /// Tasks that existed on one day, ordered carried -> open -> done.
    QList<DayTask> dayTasks(const QDate &day) const;
    /// Per-day counters between two dates (inclusive); key is "yyyy-MM-dd".
    QMap<QString, DayStat> dayStats(const QDate &from, const QDate &to) const;

    /// Marks yesterday's unfinished tasks as carried over today; returns how many moved.
    int rollover();

signals:
    void changed();

private:
    void ensureSchema();
    QList<Task> query(const QString &sql, const QVariantList &args = {}) const;
    qint64 nextSortKey() const;

    QSqlDatabase m_db;
    bool m_open = false;
};
