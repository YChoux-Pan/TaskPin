#include "core/SelfTest.h"

#include "core/FileNaming.h"
#include "core/History.h"
#include "core/Ocr.h"
#include "core/Settings.h"
#include "core/Store.h"
#include "core/TimeParser.h"
#include "platform/HotkeyText.h"
#include "ui/Annotation.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <QtMath>

namespace {
int g_pass = 0;
int g_fail = 0;

void check(QTextStream &out, const QString &name, bool ok, const QString &detail = {})
{
    if (ok) ++g_pass;
    else ++g_fail;
    out << (ok ? "PASS  " : "FAIL  ") << name;
    if (!detail.isEmpty()) out << "  -> " << detail;
    out << "\n";
}
}   // namespace

int runSelfTest(const QString &outDir)
{
    QDir().mkpath(outDir);
    // keep every side effect inside the report directory
    qputenv("TASKPIN_DATA", outDir.toUtf8());

    QFile f(outDir + "/selftest.txt");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return 1;
    QTextStream out(&f);
    out << "TaskPinQt self test  " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n\n";

    // ---- store on a throw-away database
    const QString dbPath = outDir + "/selftest.db";
    QFile::remove(dbPath);
    out << "== store ==\n";
    {
        Store store;
        check(out, "open db", store.open(dbPath), dbPath);

        const QString a = store.addTask(QStringLiteral("自检任务 A"));
        const QString b = store.addTask(QStringLiteral("自检任务 B"));
        check(out, "add two tasks", store.openCount() == 2, QStringLiteral("open=%1").arg(store.openCount()));

        store.setDone(a, true);
        check(out, "complete one", store.openCount() == 1, QStringLiteral("open=%1").arg(store.openCount()));

        store.drop(b);
        check(out, "soft delete keeps row", store.deletedTasks().size() == 1,
              QStringLiteral("dropped=%1").arg(store.deletedTasks().size()));
        check(out, "deleted hidden from open list", store.openCount() == 0,
              QStringLiteral("open=%1").arg(store.openCount()));

        const QString droppedId = store.deletedTasks().first().id;
        store.restore(droppedId);
        check(out, "restore brings it back", store.openCount() == 1,
              QStringLiteral("open=%1").arg(store.openCount()));

        store.purge(droppedId);
        check(out, "purge removes row", store.deletedTasks().isEmpty(),
              QStringLiteral("dropped=%1").arg(store.deletedTasks().size()));

        // due + priority + calendar aggregation
        const QDateTime due(QDate::currentDate(), QTime(15, 0));
        const QString urgent = store.addTask(QStringLiteral("带截止时间的任务"), QStringLiteral("manual"), QString(),
                                             due, 0);
        const QString normal = store.addTask(QStringLiteral("普通任务"));
        const auto open = store.openTasks();
        check(out, "priority sorts urgent first", !open.isEmpty() && open.first().id == urgent,
              open.isEmpty() ? QStringLiteral("empty") : open.first().title);
        check(out, "due stored as ISO", open.first().due == due.toString(Qt::ISODate), open.first().due);

        const QDate today = QDate::currentDate();
        const auto dayTasks = store.dayTasks(today);
        check(out, "dayTasks lists every task_day row", dayTasks.size() == 3,
              QStringLiteral("n=%1").arg(dayTasks.size()));
        bool sawDone = false;
        for (const auto &dt : dayTasks) sawDone = sawDone || dt.state == QStringLiteral("done");
        check(out, "dayTasks keeps the done row", sawDone,
              dayTasks.isEmpty() ? QString() : dayTasks.first().state);

        const auto stats = store.dayStats(today, today);
        const DayStat st = stats.value(today.toString("yyyy-MM-dd"));
        check(out, "dayStats counts open rows", st.pending == 2 && st.done == 1,
              QStringLiteral("pending=%1 done=%2").arg(st.pending).arg(st.done));

        store.setPriority(urgent, 3);
        const auto reordered = store.openTasks();
        check(out, "setPriority reorders", !reordered.isEmpty() && reordered.first().id == normal,
              reordered.isEmpty() ? QStringLiteral("empty") : reordered.first().title);
    }

    // ---- DPI math used by the screenshot composer
    out << "\n== dpi math ==\n";
    {
        const qreal dpr = 1.25;
        const int w = qRound(301 * dpr);
        const int h = qRound(141 * dpr);
        check(out, "logical 301x141 -> 376x176", w == 376 && h == 176, QStringLiteral("%1x%2").arg(w).arg(h));
        check(out, "dpr round trip", qRound(376 / dpr) == 301, QStringLiteral("%1").arg(qRound(376 / dpr)));
    }

    // ---- settings defaults are sane
    out << "\n== config ==\n";
    {
        Settings s;
        check(out, "default rows", s.visibleRows == 6, QString::number(s.visibleRows));
        check(out, "default width", s.cardWidth == 340, QString::number(s.cardWidth));
        check(out, "hotkey default has modifier", s.chordFor(QStringLiteral("snip")).contains(QLatin1Char('+')),
              s.chordFor(QStringLiteral("snip")));
        check(out, "every action has a default chord", [&] {
            for (const auto &a : Settings::hotkeyActions())
                if (Settings::defaultChord(a.first).isEmpty()) return false;
            return true;
        }());

        s.setChord(QStringLiteral("snip"), QStringLiteral("Ctrl+Shift+F9"));
        check(out, "setChord overrides", s.chordFor(QStringLiteral("snip")) == QStringLiteral("Ctrl+Shift+F9"),
              s.chordFor(QStringLiteral("snip")));
        s.setChord(QStringLiteral("snip"), QString());
        check(out, "empty chord falls back to default",
              s.chordFor(QStringLiteral("snip")) == Settings::defaultChord(QStringLiteral("snip")),
              s.chordFor(QStringLiteral("snip")));

        s.setLastRegion(QRect(120, 80, 301, 141));
        const QRect back = s.lastRegionRect();
        check(out, "lastRegion round trip", back == QRect(120, 80, 301, 141), s.lastRegion);
        s.lastRegion = QStringLiteral("broken");
        check(out, "broken lastRegion is ignored", !s.lastRegionRect().isValid(), s.lastRegion);
    }

    // ---- file naming
    out << "\n== file naming ==\n";
    {
        const QDateTime when(QDate(2026, 11, 8), QTime(16, 31, 25));
        const QString name = FileNaming::expand(QStringLiteral("TaskPin-{yyyy}{MM}{dd}-{HH}{mm}{ss}"), when);
        check(out, "pattern expands", name == QStringLiteral("TaskPin-20261108-163125.png"), name);
        check(out, "sanitize replaces illegal chars",
              FileNaming::sanitize(QStringLiteral("a/b:c*?.png")) == QStringLiteral("a_b_c__.png"),
              FileNaming::sanitize(QStringLiteral("a/b:c*?.png")));
        const QString unique = FileNaming::uniquePath(outDir, QStringLiteral("uniq-{ss}"), when);
        QFile probe(unique);
        probe.open(QIODevice::WriteOnly);
        probe.write("x");
        probe.close();
        const QString second = FileNaming::uniquePath(outDir, QStringLiteral("uniq-{ss}"), when);
        check(out, "uniquePath avoids collisions", second != unique, second);
        QFile::remove(unique);
        QFile::remove(second);
    }

    // ---- Chinese time parsing
    out << "\n== time parser ==\n";
    {
        const QDateTime now(QDate(2026, 11, 8), QTime(10, 0));   // a Sunday
        const ParsedTask t = TimeParser::parse(QStringLiteral("明天 15:00 前 给王总发报价 #报价"), now);
        check(out, "明天 resolves to +1 day", t.due.isValid() && t.due.date() == QDate(2026, 11, 9),
              t.due.toString(Qt::ISODate));
        check(out, "15:00 parsed", t.due.time() == QTime(15, 0), t.due.time().toString("HH:mm"));
        check(out, "tag extracted", t.tags.contains(QStringLiteral("报价")), t.tags.join(','));
        check(out, "title cleaned", t.title == QStringLiteral("给王总发报价"), t.title);

        const ParsedTask week = TimeParser::parse(QStringLiteral("周五下午两点 开会"), now);
        check(out, "周五下午两点 -> Friday 14:00",
              week.due.isValid() && week.due.date().dayOfWeek() == 5 && week.due.time() == QTime(14, 0),
              week.due.toString(Qt::ISODate));

        const ParsedTask bang = TimeParser::parse(QStringLiteral("！！ 交季度报表"), now);
        check(out, "!! raises priority to P0", bang.priority == 0, QString::number(bang.priority));
        check(out, "plain text keeps no due", !TimeParser::parse(QStringLiteral("随手记一下"), now).due.isValid());

        check(out, "describeDue tomorrow",
              TimeParser::describeDue(QDateTime(QDate(2026, 11, 9), QTime(15, 0)), now) == QStringLiteral("明天15:00"),
              TimeParser::describeDue(QDateTime(QDate(2026, 11, 9), QTime(15, 0)), now));
    }

    // ---- hotkey chord text
    out << "\n== hotkeys ==\n";
    {
        const auto chord = HotkeyText::parse(QStringLiteral("ctrl+alt+a"));
        check(out, "parse Ctrl+Alt+A", chord.valid() && chord.text == QStringLiteral("Ctrl+Alt+A"), chord.text);
        check(out, "parse F1", HotkeyText::parse(QStringLiteral("F1")).valid());
        check(out, "reject unknown modifier", !HotkeyText::parse(QStringLiteral("Hyper+A")).valid());
        const auto recorded = HotkeyText::fromQtKey(Qt::Key_F9, Qt::ControlModifier);
        check(out, "record Ctrl+F9", recorded.valid() && recorded.text == QStringLiteral("Ctrl+F9"), recorded.text);
        check(out, "modifier-only press is not a chord",
              !HotkeyText::fromQtKey(Qt::Key_Control, Qt::ControlModifier).valid());
        check(out, "format round trip",
              HotkeyText::parse(HotkeyText::format(1u | 2u, 'A')).text == QStringLiteral("Ctrl+Alt+A"),
              HotkeyText::format(1u | 2u, 'A'));
    }

    // ---- annotations (mosaic must sample the frozen frame, vector marks must hit-test)
    out << "\n== annotations ==\n";
    {
        QImage base(80, 60, QImage::Format_ARGB32_Premultiplied);
        base.fill(QColor(255, 0, 0));
        QImage canvas(80, 60, QImage::Format_ARGB32_Premultiplied);
        canvas.fill(QColor(0, 0, 255));

        Annotation mosaic;
        mosaic.kind = Annotation::Pixelate;
        mosaic.rect = QRect(10, 10, 40, 30);
        mosaic.block = 8;
        {
            QPainter p(&canvas);
            mosaic.paint(p, &base, 1.0);
        }
        const QColor inside = canvas.pixelColor(20, 20);
        const QColor outside = canvas.pixelColor(70, 50);
        check(out, "mosaic samples the frozen frame", inside.red() > 200 && inside.blue() < 60, inside.name());
        check(out, "mosaic stays inside its rect", outside.blue() > 200, outside.name());

        Annotation arrow;
        arrow.kind = Annotation::Arrow;
        arrow.points = {QPoint(5, 5), QPoint(60, 40)};
        check(out, "arrow hit test on the line", arrow.hitTest(QPoint(32, 22)), QString());
        check(out, "arrow misses far away", !arrow.hitTest(QPoint(2, 55)), QString());
        arrow.translate(QPoint(4, 0));
        check(out, "translate moves points", arrow.points.first() == QPoint(9, 5), QString());
        check(out, "bounds cover the shape", arrow.bounds().contains(QRect(4, 4, 60, 40)), QString());

        Annotation blur;
        blur.kind = Annotation::Pixelate;
        blur.blur = true;
        blur.rect = QRect(0, 0, 80, 60);
        QImage soft = canvas;
        {
            QPainter p(&soft);
            blur.paint(p, &base, 1.0);
        }
        check(out, "blur runs without a source crash", soft.size() == canvas.size(), QString());
    }

    // ---- a broken settings.json is backed up, never silently overwritten
    out << "\n== settings recovery ==\n";
    {
        const QString path = Settings::settingsPath();
        const QString backup = path + QStringLiteral(".broken");
        QFile::remove(backup);
        QFile broken(path);
        broken.open(QIODevice::WriteOnly | QIODevice::Truncate);
        broken.write("{ this is not json");
        broken.close();

        Settings s;
        s.load();
        check(out, "broken settings backed up", QFile::exists(backup), backup);
        check(out, "defaults survive a broken file", s.visibleRows == 6, QString::number(s.visibleRows));
        QFile::remove(path);
        QFile::remove(backup);
    }

    // ---- history keeps the most recent captures
    out << "\n== history ==\n";
    {
        QImage img(40, 20, QImage::Format_ARGB32_Premultiplied);
        img.fill(QColor(16, 20, 26));
        const QString path = History::save(img);
        check(out, "history save writes a png", !path.isEmpty() && QFile::exists(path), path);
        check(out, "history lists it back", History::recent().contains(path),
              QStringLiteral("n=%1").arg(History::recent().size()));
        check(out, "history dir under dataDir", History::dir().startsWith(Settings::dataDir()), History::dir());
    }

    // ---- environment probes (informational, still asserted for the migration checklist)
    out << "\n== environment ==\n";
    {
        check(out, "windows OCR reachable", Ocr::available(), QStringLiteral("Windows.Media.Ocr via PowerShell"));
    }

    out << "\n== summary: " << g_pass << " passed, " << g_fail << " failed ==\n";
    f.close();
    return g_fail == 0 ? 0 : 1;
}
