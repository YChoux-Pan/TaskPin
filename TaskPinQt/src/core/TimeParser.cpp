#include "core/TimeParser.h"

#include <QDate>
#include <QRegularExpression>
#include <QTime>

namespace {

bool cnToInt(const QString &s, int &out)
{
    if (s.isEmpty()) return false;
    bool ok = false;
    const int direct = s.toInt(&ok);
    if (ok) {
        out = direct;
        return true;
    }
    auto digit = [](QChar c) -> int {
        switch (c.unicode()) {
        case 0x4E00: return 1;   // 一
        case 0x4E8C: return 2;   // 二
        case 0x4E09: return 3;   // 三
        case 0x56DB: return 4;   // 四
        case 0x4E94: return 5;   // 五
        case 0x516D: return 6;   // 六
        case 0x4E03: return 7;   // 七
        case 0x516B: return 8;   // 八
        case 0x4E5D: return 9;   // 九
        case 0x5341: return 10;  // 十
        case 0x4E24: return 2;   // 两
        default: return -1;
        }
    };
    if (s.size() == 1) {
        const int v = digit(s.at(0));
        if (v <= 0) return false;
        out = v;
        return true;
    }
    if (s.at(0).unicode() == 0x5341) {   // 十 / 十一 / 十二
        out = 10 + qMax(0, digit(s.at(1)));
        return true;
    }
    if (s.size() >= 2 && s.at(1).unicode() == 0x5341) {   // 二十 / 二十三
        out = digit(s.at(0)) * 10 + (s.size() > 2 ? qMax(0, digit(s.at(2))) : 0);
        return true;
    }
    return false;
}

QDate safeDate(int year, int month, int day)
{
    month = qBound(1, month, 12);
    day = qBound(1, day, QDate(year, month, 1).daysInMonth());
    return QDate(year, month, day);
}

QString trimCharSet(QString s, const QString &set)
{
    while (!s.isEmpty() && set.contains(s.at(0))) s.remove(0, 1);
    while (!s.isEmpty() && set.contains(s.at(s.size() - 1))) s.chop(1);
    return s;
}

}   // namespace

namespace TimeParser {

ParsedTask parse(const QString &raw, const QDateTime &now)
{
    const QDate today = now.date();
    QString text = raw.trimmed();
    ParsedTask r;
    r.raw = text;

    // ---- tags  #报价
    static const QRegularExpression tagRe(QStringLiteral("#([^\\s#，,。；;]+)"));
    {
        auto it = tagRe.globalMatch(text);
        while (it.hasNext()) {
            const QString t = it.next().captured(1);
            if (!r.tags.contains(t)) r.tags.append(t);
        }
    }
    text.replace(tagRe, QStringLiteral(" "));

    // ---- priority  ! / ！！
    static const QRegularExpression bangRe(QStringLiteral("[!！]+"));
    {
        const auto m = bangRe.match(text);
        if (m.hasMatch()) {
            r.priority = m.capturedLength() >= 2 ? 0 : 1;
            text.remove(m.capturedStart(), m.capturedLength());
        }
    }

    bool hasDate = false;
    bool hasTime = false;
    QDate date;
    QTime time;

    // ---- day words
    static const QRegularExpression dayRe(QStringLiteral("(大后天|后天|明天|今日|今天|今晚|明早|明晚)"));
    {
        const auto m = dayRe.match(text);
        if (m.hasMatch()) {
            const QString word = m.captured(1);
            int add = 3;
            if (word == QStringLiteral("今天") || word == QStringLiteral("今日") || word == QStringLiteral("今晚")) add = 0;
            else if (word == QStringLiteral("明天") || word == QStringLiteral("明早") || word == QStringLiteral("明晚")) add = 1;
            else if (word == QStringLiteral("后天")) add = 2;
            date = today.addDays(add);
            hasDate = true;
            if (word.endsWith(QStringLiteral("晚"))) {
                time = QTime(20, 0);
                hasTime = true;
            }
            if (word.endsWith(QStringLiteral("早"))) {
                time = QTime(9, 0);
                hasTime = true;
            }
            text.remove(m.capturedStart(), m.capturedLength());
        }
    }

    // ---- 周三 / 下周三 / 周天
    if (!hasDate) {
        static const QRegularExpression weekRe(
            QStringLiteral("(?<next>下)?\\s*(?:周|星期)\\s*(?<d>[一二三四五六日天])"));
        const auto m = weekRe.match(text);
        if (m.hasMatch()) {
            int target = 0;
            const QChar d = m.captured(QStringLiteral("d")).at(0);
            switch (d.unicode()) {
            case 0x4E00: target = 1; break;   // 一
            case 0x4E8C: target = 2; break;   // 二
            case 0x4E09: target = 3; break;   // 三
            case 0x56DB: target = 4; break;   // 四
            case 0x4E94: target = 5; break;   // 五
            case 0x516D: target = 6; break;   // 六
            default: target = 0; break;       // 日 / 天
            }
            const int todayDow = today.dayOfWeek() % 7;   // Sunday = 0, same as the Chinese week index
            int delta = (target - todayDow + 7) % 7;
            if (!m.captured(QStringLiteral("next")).isEmpty()) delta += 7;   // 下周三 = this week's Wednesday + 7
            date = today.addDays(delta);
            hasDate = true;
            text.remove(m.capturedStart(), m.capturedLength());
        }
    }

    // ---- 11月8日 / 8号 / 月底
    if (!hasDate) {
        static const QRegularExpression mdRe(
            QStringLiteral("(?<mo>\\d{1,2})\\s*月\\s*(?<d>\\d{1,2})\\s*[日号]?"));
        static const QRegularExpression dRe(QStringLiteral("(?<!\\d)(?<d>\\d{1,2})\\s*号"));
        static const QRegularExpression monthEndRe(QStringLiteral("月底|月末"));

        const auto md = mdRe.match(text);
        const auto me = monthEndRe.match(text);
        const auto d = dRe.match(text);
        if (md.hasMatch()) {
            const int mo = md.captured(QStringLiteral("mo")).toInt();
            const int day = md.captured(QStringLiteral("d")).toInt();
            date = safeDate(today.year(), mo, day);
            if (date < today) date = safeDate(today.year() + 1, mo, day);
            hasDate = true;
            text.remove(md.capturedStart(), md.capturedLength());
        } else if (me.hasMatch()) {
            date = QDate(today.year(), today.month(), today.daysInMonth());
            hasDate = true;
            text.remove(me.capturedStart(), me.capturedLength());
        } else if (d.hasMatch()) {
            const int day = d.captured(QStringLiteral("d")).toInt();
            if (day >= 1 && day <= 31) {
                date = safeDate(today.year(), today.month(), day);
                if (date < today) {
                    const QDate nextMonth = today.addMonths(1);
                    date = safeDate(nextMonth.year(), nextMonth.month(), day);
                }
                hasDate = true;
                text.remove(d.capturedStart(), d.capturedLength());
            }
        }
    }

    // ---- 15点 / 15:30 / 下午3点 / 下午两点半
    static const QRegularExpression timeRe(QStringLiteral(
        "(?<ampm>凌晨|早上|上午|中午|下午|傍晚|晚上)?\\s*"
        "(?<h>[0-9]{1,2}|[一二三四五六七八九十两]{1,3})\\s*(?:[点时]|[:：])\\s*"
        "(?<m>[0-9]{1,2}|半|一刻|三刻|[一二三四五六七八九十两]{1,3})?\\s*分?"));
    {
        const auto m = timeRe.match(text);
        if (m.hasMatch()) {
            int hh = -1;
            cnToInt(m.captured(QStringLiteral("h")), hh);
            int mi = 0;
            const QString rawM = m.captured(QStringLiteral("m"));
            if (!rawM.isEmpty()) {
                if (rawM == QStringLiteral("半")) mi = 30;
                else if (rawM == QStringLiteral("一刻")) mi = 15;
                else if (rawM == QStringLiteral("三刻")) mi = 45;
                else cnToInt(rawM, mi);
            }
            const QString ampm = m.captured(QStringLiteral("ampm"));
            if ((ampm == QStringLiteral("下午") || ampm == QStringLiteral("傍晚") || ampm == QStringLiteral("晚上")) && hh < 12)
                hh += 12;
            if (ampm == QStringLiteral("中午") && hh < 11) hh += 12;
            if (hh == 24) hh = 0;
            if (hh >= 0 && hh <= 23 && mi >= 0 && mi <= 59) {
                time = QTime(hh, mi);
                hasTime = true;
            }
            text.remove(m.capturedStart(), m.capturedLength());
        }
    }

    // ---- resolve
    if (hasDate || hasTime) {
        const QDate day = hasDate ? date : today;
        const QTime clock = hasTime ? time : QTime(23, 59);
        QDateTime due(day, clock);
        if (!hasDate && due < now) due = due.addDays(1);   // "15点" already past -> tomorrow
        r.due = due;
    }

    // ---- title cleanup
    QString title = text;
    static const QRegularExpression spaces(QStringLiteral("\\s+"));
    title.replace(spaces, QStringLiteral(" "));
    title = title.trimmed();
    title = trimCharSet(title, QStringLiteral("，,。.；;：:-—|/"));
    static const QRegularExpression leadRe(QStringLiteral("^(前|之前|以前|截至|截止)\\s*"));
    static const QRegularExpression tailRe(QStringLiteral("\\s*(前|之前|以前|截至|截止)$"));
    title.replace(leadRe, QString());
    title.replace(tailRe, QString());
    title = title.trimmed();
    r.title = title.isEmpty() ? r.raw : title;
    return r;
}

QString describeDue(const QDateTime &due, const QDateTime &now)
{
    if (!due.isValid()) return {};
    const QDate d = due.date();
    const int delta = static_cast<int>(now.date().daysTo(d));
    QString dayText;
    if (delta == 0) dayText = QStringLiteral("今天");
    else if (delta == 1) dayText = QStringLiteral("明天");
    else if (delta == 2) dayText = QStringLiteral("后天");
    else if (delta == -1) dayText = QStringLiteral("昨天");
    else if (delta < 0) dayText = QStringLiteral("逾期%1天").arg(-delta);
    else if (d.month() == now.date().month()) dayText = QStringLiteral("%1日").arg(d.day());
    else dayText = QStringLiteral("%1/%2").arg(d.month()).arg(d.day());

    if (due.time() == QTime(23, 59)) return dayText;
    return dayText + due.toString("HH:mm");
}

}   // namespace TimeParser
