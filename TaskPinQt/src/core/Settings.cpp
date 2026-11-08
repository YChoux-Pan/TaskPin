#include "core/Settings.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

Settings &Settings::instance()
{
    static Settings s;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        s.load();
    }
    return s;
}

QString Settings::dataDir()
{
    QString base = qEnvironmentVariable("TASKPIN_DATA");
    if (base.isEmpty()) base = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(base);
    return base;
}

QString Settings::dbPath() { return dataDir() + QStringLiteral("/taskpin.db"); }
QString Settings::settingsPath() { return dataDir() + QStringLiteral("/settings.json"); }
QString Settings::logPath() { return dataDir() + QStringLiteral("/taskpinqt.log"); }

const QList<QPair<QString, QString>> &Settings::hotkeyActions()
{
    static const QList<QPair<QString, QString>> list{
        {QStringLiteral("snip"), QStringLiteral("区域截图")},
        {QStringLiteral("full"), QStringLiteral("全屏截图并复制")},
        {QStringLiteral("last"), QStringLiteral("上一次区域截图")},
        {QStringLiteral("pin"), QStringLiteral("贴住剪贴板图片")},
        {QStringLiteral("pins"), QStringLiteral("显示 / 隐藏全部贴图")},
        {QStringLiteral("quick"), QStringLiteral("速记")},
        {QStringLiteral("toggle"), QStringLiteral("显示 / 隐藏卡片")},
        {QStringLiteral("history"), QStringLiteral("截屏历史")},
    };
    return list;
}

QMap<QString, QString> Settings::defaultHotkeys()
{
    return {
        {QStringLiteral("snip"), QStringLiteral("Ctrl+Alt+A")},
        {QStringLiteral("full"), QStringLiteral("Ctrl+Alt+F")},
        {QStringLiteral("last"), QStringLiteral("Ctrl+Alt+R")},
        {QStringLiteral("pin"), QStringLiteral("Ctrl+Alt+V")},
        {QStringLiteral("pins"), QStringLiteral("Ctrl+Alt+H")},
        {QStringLiteral("quick"), QStringLiteral("Ctrl+Alt+Space")},
        {QStringLiteral("toggle"), QStringLiteral("Ctrl+Alt+D")},
        {QStringLiteral("history"), QStringLiteral("Ctrl+Alt+G")},
    };
}

QString Settings::defaultChord(const QString &actionId)
{
    return defaultHotkeys().value(actionId);
}

QString Settings::fallbackChord(const QString &actionId)
{
    static const QMap<QString, QString> fallbacks{
        {QStringLiteral("snip"), QStringLiteral("Ctrl+Shift+F9")},
        {QStringLiteral("full"), QStringLiteral("Ctrl+Shift+F10")},
        {QStringLiteral("last"), QStringLiteral("Ctrl+Shift+F11")},
        {QStringLiteral("pin"), QStringLiteral("Ctrl+Shift+F12")},
        {QStringLiteral("pins"), QStringLiteral("Ctrl+Shift+F8")},
        {QStringLiteral("quick"), QStringLiteral("Ctrl+Shift+F7")},
        {QStringLiteral("toggle"), QStringLiteral("Ctrl+Shift+F6")},
        {QStringLiteral("history"), QStringLiteral("Ctrl+Shift+F5")},
    };
    return fallbacks.value(actionId);
}

QString Settings::chordFor(const QString &actionId) const
{
    const QString configured = m_hotkeys.value(actionId);
    return configured.trimmed().isEmpty() ? defaultChord(actionId) : configured;
}

void Settings::setChord(const QString &actionId, const QString &chord)
{
    if (chord.trimmed().isEmpty()) m_hotkeys.remove(actionId);
    else m_hotkeys[actionId] = chord.trimmed();
}

QString Settings::resolveAutoSaveDir() const
{
    QString dir = autoSaveDir.trimmed();
    if (dir.isEmpty()) dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + QStringLiteral("/TaskPin");
    QDir().mkpath(dir);
    return dir;
}

QRect Settings::lastRegionRect() const
{
    const QStringList parts = lastRegion.split(',');
    if (parts.size() != 4) return {};
    bool ok = true;
    const int x = parts[0].toInt(&ok);
    if (!ok) return {};
    const int y = parts[1].toInt(&ok);
    if (!ok) return {};
    const int w = parts[2].toInt(&ok);
    if (!ok) return {};
    const int h = parts[3].toInt(&ok);
    if (!ok || w <= 2 || h <= 2) return {};
    return QRect(x, y, w, h);
}

void Settings::setLastRegion(const QRect &logicalRect)
{
    lastRegion = QStringLiteral("%1,%2,%3,%4")
                     .arg(logicalRect.x())
                     .arg(logicalRect.y())
                     .arg(logicalRect.width())
                     .arg(logicalRect.height());
}

static QString colorToHex(const QColor &c) { return c.name(QColor::HexRgb); }

static QColor colorFrom(const QJsonObject &o, const char *key, const QColor &fallback)
{
    const QString hex = o.value(QLatin1String(key)).toString().trimmed();
    if (hex.isEmpty()) return fallback;
    const QColor c(hex);
    return c.isValid() ? c : fallback;
}

void Settings::load()
{
    const QString path = settingsPath();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QByteArray bytes = f.readAll();
    f.close();

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        // never silently overwrite a broken file: keep a copy the user can inspect
        qWarning() << "settings.json is unreadable:" << err.errorString();
        if (!bytes.trimmed().isEmpty()) {
            const QString backup = path + QStringLiteral(".broken");
            QFile::remove(backup);
            QFile::copy(path, backup);
            qWarning() << "settings.json backed up to" << backup;
        }
        return;
    }
    const auto o = doc.object();

    cardPosition = o.value(QStringLiteral("cardPosition")).toString(cardPosition);
    cardWidth = o.value(QStringLiteral("cardWidth")).toInt(cardWidth);
    visibleRows = o.value(QStringLiteral("visibleRows")).toInt(visibleRows);
    opacity = o.value(QStringLiteral("opacity")).toDouble(opacity);
    alwaysOnTop = o.value(QStringLiteral("alwaysOnTop")).toBool(alwaysOnTop);
    showCarryMark = o.value(QStringLiteral("showCarryMark")).toBool(showCarryMark);
    cardBg = colorFrom(o, "cardBg", cardBg);
    cardFg = colorFrom(o, "cardFg", cardFg);
    cardX = o.value(QStringLiteral("cardX")).toInt(cardX);
    cardY = o.value(QStringLiteral("cardY")).toInt(cardY);

    snipDelay = o.value(QStringLiteral("snipDelay")).toDouble(snipDelay);
    annotationColor = colorFrom(o, "annotationColor", annotationColor);
    annotationWidth = o.value(QStringLiteral("annotationWidth")).toInt(annotationWidth);
    autoSave = o.value(QStringLiteral("autoSave")).toBool(autoSave);
    autoSaveDir = o.value(QStringLiteral("autoSaveDir")).toString(autoSaveDir);
    namePattern = o.value(QStringLiteral("namePattern")).toString(namePattern);
    historyLimit = o.value(QStringLiteral("historyLimit")).toInt(historyLimit);
    lastRegion = o.value(QStringLiteral("lastRegion")).toString(lastRegion);

    pinOpacity = o.value(QStringLiteral("pinOpacity")).toDouble(pinOpacity);
    pinGroup = o.value(QStringLiteral("pinGroup")).toString(pinGroup);
    const auto groups = o.value(QStringLiteral("pinGroups")).toArray();
    if (!groups.isEmpty()) {
        pinGroups.clear();
        for (const auto &g : groups) {
            const QString name = g.toString().trimmed();
            if (!name.isEmpty() && !pinGroups.contains(name)) pinGroups.append(name);
        }
        if (pinGroups.isEmpty()) pinGroups.append(QStringLiteral("默认"));
    }

    aiEnabled = o.value(QStringLiteral("aiEnabled")).toBool(aiEnabled);
    aiBaseUrl = o.value(QStringLiteral("aiBaseUrl")).toString(aiBaseUrl);
    aiApiKey = o.value(QStringLiteral("aiApiKey")).toString(aiApiKey);
    aiModel = o.value(QStringLiteral("aiModel")).toString(aiModel);

    autostart = o.value(QStringLiteral("autostart")).toBool(autostart);

    // hotkeys: object form; fall back to the legacy flat keys once so old installs keep working
    m_hotkeys.clear();
    const auto hk = o.value(QStringLiteral("hotkeys")).toObject();
    if (!hk.isEmpty()) {
        for (auto it = hk.begin(); it != hk.end(); ++it) {
            const QString chord = it.value().toString().trimmed();
            if (!chord.isEmpty()) m_hotkeys[it.key()] = chord;
        }
    } else {
        if (o.contains(QStringLiteral("snipHotkey"))) m_hotkeys[QStringLiteral("snip")] = o.value(QStringLiteral("snipHotkey")).toString();
        if (o.contains(QStringLiteral("quickHotkey"))) m_hotkeys[QStringLiteral("quick")] = o.value(QStringLiteral("quickHotkey")).toString();
        if (o.contains(QStringLiteral("toggleHotkey"))) m_hotkeys[QStringLiteral("toggle")] = o.value(QStringLiteral("toggleHotkey")).toString();
    }
}

void Settings::save()
{
    QJsonObject o;
    o[QStringLiteral("version")] = 2;
    o[QStringLiteral("cardPosition")] = cardPosition;
    o[QStringLiteral("cardWidth")] = cardWidth;
    o[QStringLiteral("visibleRows")] = visibleRows;
    o[QStringLiteral("opacity")] = opacity;
    o[QStringLiteral("alwaysOnTop")] = alwaysOnTop;
    o[QStringLiteral("showCarryMark")] = showCarryMark;
    o[QStringLiteral("cardBg")] = colorToHex(cardBg);
    o[QStringLiteral("cardFg")] = colorToHex(cardFg);
    o[QStringLiteral("cardX")] = cardX;
    o[QStringLiteral("cardY")] = cardY;

    o[QStringLiteral("snipDelay")] = snipDelay;
    o[QStringLiteral("annotationColor")] = colorToHex(annotationColor);
    o[QStringLiteral("annotationWidth")] = annotationWidth;
    o[QStringLiteral("autoSave")] = autoSave;
    o[QStringLiteral("autoSaveDir")] = autoSaveDir;
    o[QStringLiteral("namePattern")] = namePattern;
    o[QStringLiteral("historyLimit")] = historyLimit;
    o[QStringLiteral("lastRegion")] = lastRegion;

    o[QStringLiteral("pinOpacity")] = pinOpacity;
    QJsonArray groups;
    for (const auto &g : pinGroups) groups.append(g);
    o[QStringLiteral("pinGroups")] = groups;
    o[QStringLiteral("pinGroup")] = pinGroup;

    o[QStringLiteral("aiEnabled")] = aiEnabled;
    o[QStringLiteral("aiBaseUrl")] = aiBaseUrl;
    o[QStringLiteral("aiApiKey")] = aiApiKey;
    o[QStringLiteral("aiModel")] = aiModel;

    o[QStringLiteral("autostart")] = autostart;

    QJsonObject hk;
    for (const auto &pair : hotkeyActions()) hk[pair.first] = chordFor(pair.first);
    o[QStringLiteral("hotkeys")] = hk;

    // atomic write: a crash mid-save must never truncate the previous settings
    QSaveFile f(settingsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "settings save failed:" << f.errorString();
        return;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    f.commit();
}

void Settings::resetToDefaults()
{
    const Settings fresh;
    *this = fresh;
}
