#pragma once

#include <QColor>
#include <QList>
#include <QMap>
#include <QPair>
#include <QRect>
#include <QString>
#include <QStringList>

/// JSON settings, kept next to the sqlite database in %LOCALAPPDATA%\TaskPin\TaskPinQt.
/// A file that cannot be parsed is copied to settings.json.broken instead of being overwritten.
class Settings {
public:
    static Settings &instance();

    void load();
    void save();
    void resetToDefaults();

    static QString dataDir();
    static QString dbPath();
    static QString settingsPath();
    static QString logPath();

    // ---------------------------------------------------------------- hotkeys
    /// (action id, 中文名) in display order; the id is also the settings.json key.
    static const QList<QPair<QString, QString>> &hotkeyActions();
    static QString defaultChord(const QString &actionId);
    /// Tried when the configured chord is already taken by another program.
    static QString fallbackChord(const QString &actionId);

    QString chordFor(const QString &actionId) const;
    void setChord(const QString &actionId, const QString &chord);

    // ---------------------------------------------------------------- card
    QString cardPosition = QStringLiteral("TopRight");   // TopRight | TopLeft | BottomRight | BottomLeft
    int cardWidth = 340;
    int visibleRows = 6;
    double opacity = 0.94;
    bool alwaysOnTop = true;
    bool showCarryMark = true;
    QColor cardBg{0x12, 0x17, 0x1F};
    QColor cardFg{0xE8, 0xEC, 0xF2};
    /// Geometry remembered from the last drag; -1 means "place automatically".
    int cardX = -1;
    int cardY = -1;

    // ---------------------------------------------------------------- snip
    double snipDelay = 0.0;                       // seconds before the frozen frame is taken
    QColor annotationColor{0xE5, 0x3B, 0x3B};
    int annotationWidth = 3;
    bool autoSave = false;
    QString autoSaveDir;                          // empty -> Pictures/TaskPin
    QString namePattern = QStringLiteral("TaskPin-{yyyy}{MM}{dd}-{HH}{mm}{ss}");
    int historyLimit = 50;
    QString lastRegion;                           // "x,y,w,h" in logical coordinates

    // ---------------------------------------------------------------- pins
    double pinOpacity = 1.0;
    QStringList pinGroups{QStringLiteral("默认")};
    QString pinGroup = QStringLiteral("默认");

    // ---------------------------------------------------------------- ai
    bool aiEnabled = false;
    QString aiBaseUrl = QStringLiteral("https://api.deepseek.com/v1");
    QString aiApiKey;
    QString aiModel = QStringLiteral("deepseek-chat");

    // ---------------------------------------------------------------- misc
    bool autostart = false;

    /// Directory for auto-saved captures (created on demand).
    QString resolveAutoSaveDir() const;
    /// "x,y,w,h" -> rect; null rect when unset/invalid.
    QRect lastRegionRect() const;
    void setLastRegion(const QRect &logicalRect);

private:
    QMap<QString, QString> m_hotkeys;
    static QMap<QString, QString> defaultHotkeys();
};
