#include "core/Store.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

static QString nowIso() { return QDateTime::currentDateTime().toString(Qt::ISODate); }
static QString todayKey() { return QDate::currentDate().toString("yyyy-MM-dd"); }

Store::Store(QObject *parent) : QObject(parent) {}

Store::~Store()
{
    if (m_db.isOpen()) m_db.close();
}

bool Store::open(const QString &path)
{
    m_db = QSqlDatabase::addDatabase("QSQLITE", "taskpin");
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "sqlite open failed:" << m_db.lastError().text();
        return false;
    }
    ensureSchema();
    m_open = true;
    return true;
}

void Store::ensureSchema()
{
    QSqlQuery q(m_db);
    q.exec("PRAGMA journal_mode=WAL");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS task (
          id TEXT PRIMARY KEY, title TEXT NOT NULL, note TEXT, ocr_text TEXT, source TEXT,
          tags TEXT, due TEXT, priority INTEGER NOT NULL DEFAULT 2, sort_key INTEGER NOT NULL,
          state TEXT NOT NULL DEFAULT 'open', created TEXT, updated TEXT, completed TEXT,
          origin_date TEXT, last_day TEXT, carry_count INTEGER NOT NULL DEFAULT 0,
          attachment TEXT, ai_state TEXT, ai_summary TEXT, ai_attempts INTEGER DEFAULT 0, ai_next_try TEXT
        ))");
    q.exec(R"(
        CREATE TABLE IF NOT EXISTS task_day (
          task_id TEXT NOT NULL, day TEXT NOT NULL, state TEXT NOT NULL, seen_at TEXT,
          PRIMARY KEY (task_id, day)
        ))");
    q.exec("CREATE INDEX IF NOT EXISTS idx_day ON task_day(day)");
    q.exec("CREATE INDEX IF NOT EXISTS idx_state ON task(state, sort_key)");
}

QList<Task> Store::query(const QString &sql, const QVariantList &args) const
{
    QList<Task> list;
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const auto &a : args) q.addBindValue(a);
    if (!q.exec()) {
        qWarning() << "sql failed:" << q.lastError().text() << sql;
        return list;
    }
    while (q.next()) {
        Task t;
        t.id = q.value("id").toString();
        t.title = q.value("title").toString();
        t.note = q.value("note").toString();
        t.ocrText = q.value("ocr_text").toString();
        t.source = q.value("source").toString();
        t.tags = q.value("tags").toString();
        t.due = q.value("due").toString();
        t.priority = q.value("priority").toInt();
        t.sortKey = q.value("sort_key").toLongLong();
        t.state = q.value("state").toString();
        t.created = q.value("created").toString();
        t.updated = q.value("updated").toString();
        t.completed = q.value("completed").toString();
        t.originDate = q.value("origin_date").toString();
        t.lastDay = q.value("last_day").toString();
        t.carryCount = q.value("carry_count").toInt();
        t.attachment = q.value("attachment").toString();
        t.aiState = q.value("ai_state").toString();
        list.append(t);
    }
    return list;
}

QList<Task> Store::openTasks() const
{
    return query("SELECT * FROM task WHERE state='open' "
                 "ORDER BY priority ASC, COALESCE(due,'9999') ASC, sort_key ASC");
}

QList<Task> Store::deletedTasks() const
{
    return query("SELECT * FROM task WHERE state='dropped' ORDER BY updated DESC LIMIT 200");
}

int Store::openCount() const { return countByState(QStringLiteral("open")); }

int Store::countByState(const QString &state) const
{
    QSqlQuery q(m_db);
    q.prepare("SELECT COUNT(*) FROM task WHERE state=?");
    q.addBindValue(state);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return 0;
}

qint64 Store::nextSortKey() const
{
    QSqlQuery q(m_db);
    if (q.exec("SELECT COALESCE(MAX(sort_key),0)+1 FROM task") && q.next())
        return q.value(0).toLongLong();
    return QDateTime::currentMSecsSinceEpoch();
}

QString Store::addTask(const QString &title, const QString &source, const QString &attachment,
                       const QDateTime &due, int priority, const QString &ocrText)
{
    Task t;
    t.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    t.title = title;
    t.source = source;
    t.attachment = attachment;
    t.ocrText = ocrText;
    t.priority = qBound(0, priority < 0 ? 2 : priority, 3);
    t.sortKey = nextSortKey();
    t.created = nowIso();
    t.updated = t.created;
    t.originDate = todayKey();
    t.lastDay = todayKey();

    QSqlQuery q(m_db);
    q.prepare("INSERT INTO task (id,title,note,ocr_text,source,tags,due,priority,sort_key,state,created,"
              "updated,completed,origin_date,last_day,carry_count,attachment,ai_state,ai_attempts) "
              "VALUES (?,?,?,?,?,?,?,?,?,'open',?,?,'',?,?,0,?,'none',0)");
    q.addBindValue(t.id);
    q.addBindValue(t.title);
    q.addBindValue("");
    q.addBindValue(t.ocrText);
    q.addBindValue(t.source);
    q.addBindValue("");
    q.addBindValue(due.isValid() ? QVariant(due.toString(Qt::ISODate)) : QVariant());
    q.addBindValue(t.priority);
    q.addBindValue(t.sortKey);
    q.addBindValue(t.created);
    q.addBindValue(t.updated);
    q.addBindValue(t.originDate);
    q.addBindValue(t.lastDay);
    q.addBindValue(t.attachment);
    if (!q.exec()) qWarning() << "insert failed:" << q.lastError().text();

    QSqlQuery d(m_db);
    d.prepare("INSERT OR REPLACE INTO task_day (task_id,day,state,seen_at) VALUES (?,?,'open',?)");
    d.addBindValue(t.id);
    d.addBindValue(todayKey());
    d.addBindValue(nowIso());
    d.exec();

    emit changed();
    return t.id;
}

void Store::setDone(const QString &id, bool done)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET state=?, completed=?, updated=? WHERE id=?");
    q.addBindValue(done ? "done" : "open");
    q.addBindValue(done ? nowIso() : QString());
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();

    QSqlQuery d(m_db);
    d.prepare("INSERT OR REPLACE INTO task_day (task_id,day,state,seen_at) VALUES (?,?,?,?)");
    d.addBindValue(id);
    d.addBindValue(todayKey());
    d.addBindValue(done ? "done" : "open");
    d.addBindValue(nowIso());
    d.exec();

    emit changed();
}

void Store::setPriority(const QString &id, int priority)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET priority=?, updated=? WHERE id=?");
    q.addBindValue(qBound(0, priority, 3));
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();
    emit changed();
}

void Store::setDue(const QString &id, const QDateTime &due)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET due=?, updated=? WHERE id=?");
    q.addBindValue(due.isValid() ? QVariant(due.toString(Qt::ISODate)) : QVariant());
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();
    emit changed();
}

void Store::postpone(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET carry_count=carry_count+1, last_day=?, sort_key=?, updated=? WHERE id=?");
    q.addBindValue(todayKey());
    q.addBindValue(nextSortKey());
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();

    QSqlQuery d(m_db);
    d.prepare("INSERT OR REPLACE INTO task_day (task_id,day,state,seen_at) VALUES (?,?,'carried',?)");
    d.addBindValue(id);
    d.addBindValue(todayKey());
    d.addBindValue(nowIso());
    d.exec();
    emit changed();
}

void Store::drop(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET state='dropped', updated=? WHERE id=?");
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();

    QSqlQuery d(m_db);
    d.prepare("INSERT OR REPLACE INTO task_day (task_id,day,state,seen_at) VALUES (?,?,'dropped',?)");
    d.addBindValue(id);
    d.addBindValue(todayKey());
    d.addBindValue(nowIso());
    d.exec();
    emit changed();
}

void Store::restore(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("UPDATE task SET state='open', updated=? WHERE id=?");
    q.addBindValue(nowIso());
    q.addBindValue(id);
    q.exec();
    emit changed();
}

void Store::purge(const QString &id)
{
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM task_day WHERE task_id=?");
    q.addBindValue(id);
    q.exec();
    q.prepare("DELETE FROM task WHERE id=?");
    q.addBindValue(id);
    q.exec();
    emit changed();
}

QList<DayTask> Store::dayTasks(const QDate &day) const
{
    QList<DayTask> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT t.id, t.title, d.state FROM task_day d JOIN task t ON t.id=d.task_id "
              "WHERE d.day=? "
              "ORDER BY CASE d.state WHEN 'carried' THEN 0 WHEN 'open' THEN 1 WHEN 'done' THEN 2 ELSE 3 END, "
              "t.sort_key ASC");
    q.addBindValue(day.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "dayTasks failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) out.append({q.value(0).toString(), q.value(1).toString(), q.value(2).toString()});
    return out;
}

QMap<QString, DayStat> Store::dayStats(const QDate &from, const QDate &to) const
{
    QMap<QString, DayStat> out;
    QSqlQuery q(m_db);
    q.prepare("SELECT day, state, COUNT(*) FROM task_day WHERE day>=? AND day<=? GROUP BY day, state");
    q.addBindValue(from.toString("yyyy-MM-dd"));
    q.addBindValue(to.toString("yyyy-MM-dd"));
    if (!q.exec()) {
        qWarning() << "dayStats failed:" << q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QString day = q.value(0).toString();
        const QString state = q.value(1).toString();
        const int n = q.value(2).toInt();
        DayStat &s = out[day];
        if (state == QStringLiteral("open")) s.pending += n;
        else if (state == QStringLiteral("done")) s.done += n;
        else if (state == QStringLiteral("carried")) s.carried += n;
    }
    return out;
}

int Store::rollover()
{
    const QString today = todayKey();
    QSqlQuery q(m_db);
    q.prepare("SELECT id, last_day FROM task WHERE state='open' AND last_day<>?");
    q.addBindValue(today);
    if (!q.exec()) return 0;

    QStringList ids;
    while (q.next()) ids << q.value(0).toString();
    if (ids.isEmpty()) return 0;

    for (const auto &id : ids) {
        QSqlQuery u(m_db);
        u.prepare("UPDATE task SET last_day=?, carry_count=carry_count+1, updated=? WHERE id=?");
        u.addBindValue(today);
        u.addBindValue(nowIso());
        u.addBindValue(id);
        u.exec();

        QSqlQuery d(m_db);
        d.prepare("INSERT OR REPLACE INTO task_day (task_id,day,state,seen_at) VALUES (?,?,'carried',?)");
        d.addBindValue(id);
        d.addBindValue(today);
        d.addBindValue(nowIso());
        d.exec();
    }
    emit changed();
    return ids.size();
}
