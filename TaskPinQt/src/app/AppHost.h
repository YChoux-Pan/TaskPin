#pragma once

#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

class AiClient;
class CalendarWindow;
class CardWindow;
class HistoryDialog;
class Hotkeys;
class Ocr;
class PinWindow;
class QAction;
class QImage;
class QLocalServer;
class QSystemTrayIcon;
class SettingsDialog;
class SnipOverlay;
class Store;

/// Owns the tray icon, global hotkeys, the desktop card and every editor/dialog it spawns.
class AppHost : public QObject {
    Q_OBJECT
public:
    explicit AppHost(QObject *parent = nullptr);
    ~AppHost() override;

    /// `initialCommand` carries a CLI verb when this is the first instance.
    void start(const QString &initialCommand = QString());

    /// Snapshot used by --dump (settings, counts, hotkeys, geometry, pins, screens).
    QJsonObject dumpState() const;
    /// Writes dumpState() to `path` as indented JSON; returns false when the file cannot be written.
    bool writeDump(const QString &path) const;

public slots:
    void quickAdd();
    void startSnip();
    void startLastRegion();
    void captureFull();
    void pinFromClipboard();
    void togglePins();
    void pickColor();
    void showWhiteboard();
    void showTransparentBoard();
    void toggleCard();
    /// true = 全局置顶；false = 桌面层（壁纸上方，被普通窗口盖住）
    void setCardOnTop(bool onTop);
    void toggleCardLayer();
    void showRecycleBin();
    void showHistory();
    void showCalendar();
    void showSettings();
    void quitApp();

private slots:
    void onHotkey(int id);
    void applySettings();

private:
    void buildTray();
    void registerHotkeys();
    void handleCommand(const QString &command);
    void syncCardTopAction();

    void captureAndEdit(bool preselectLast);
    void captureWhiteboard(bool transparent);
    void hideOverlaysForCapture();
    void restoreAfterCapture();

    void recordCapture(const QImage &image, const QRect &logicalRegion);
    void pinImage(const QImage &image);
    void recognizeImage(const QImage &image);
    void onOcrFinished(const QString &text);
    void onOcrFailed(const QString &reason);
    void onAiFinished(const QString &title);
    void createTaskFromCapture(const QString &title);
    void printImage(const QImage &image);
    void toast(const QString &message);

    Store *m_store = nullptr;
    CardWindow *m_card = nullptr;
    Hotkeys *m_hotkeys = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    SnipOverlay *m_snip = nullptr;
    QLocalServer *m_cmd = nullptr;
    Ocr *m_ocr = nullptr;
    AiClient *m_ai = nullptr;
    QAction *m_cardTopAction = nullptr;

    QPointer<HistoryDialog> m_history;
    QPointer<CalendarWindow> m_calendar;
    QPointer<SettingsDialog> m_settings;

    QList<QPointer<PinWindow>> m_hiddenPins;

    // OCR result waiting for either the local parser or the AI summariser
    QString m_pendingOcrText;
    QString m_pendingLocalTitle;
    QDateTime m_pendingDue;
    int m_pendingPriority = 2;
};
