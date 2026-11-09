#include "app/AppHost.h"

#include "core/AiClient.h"
#include "core/FileNaming.h"
#include "core/History.h"
#include "core/Ocr.h"
#include "core/Settings.h"
#include "core/Store.h"
#include "core/TimeParser.h"
#include "platform/Autostart.h"
#include "platform/Hotkeys.h"
#include "platform/ScreenCapture.h"
#include "platform/WindowFx.h"
#include "ui/CalendarWindow.h"
#include "ui/CardWindow.h"
#include "ui/ColorPickerOverlay.h"
#include "ui/HistoryDialog.h"
#include "ui/PinWindow.h"
#include "ui/QuickAddDialog.h"
#include "ui/SettingsDialog.h"
#include "ui/SnipOverlay.h"
#include "ui/WhiteboardWindow.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifndef TASKPIN_VERSION
#define TASKPIN_VERSION "0.2.0"
#endif

namespace {

QIcon makeTrayIcon()
{
    QPixmap pm(32, 32);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(QRectF(3, 3, 26, 26), 8, 8);
    p.fillPath(path, QColor(0x4C, 0x9A, 0xFF));
    p.setPen(QPen(Qt::white, 2.4, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(10, 12), QPointF(22, 12));
    p.drawLine(QPointF(10, 18), QPointF(18, 18));
    p.end();
    return QIcon(pm);
}

/// action id -> hotkey action id
const QList<QPair<QString, int>> &hotkeyActions()
{
    static const QList<QPair<QString, int>> list{
        {QStringLiteral("snip"), Hotkeys::Snip},     {QStringLiteral("full"), Hotkeys::Full},
        {QStringLiteral("last"), Hotkeys::Last},     {QStringLiteral("pin"), Hotkeys::Pin},
        {QStringLiteral("pins"), Hotkeys::Pins},     {QStringLiteral("quick"), Hotkeys::QuickAdd},
        {QStringLiteral("toggle"), Hotkeys::ToggleCard}, {QStringLiteral("history"), Hotkeys::History},
    };
    return list;
}

QString firstLineTitle(const QString &text)
{
    QString t = text;
    const int nl = t.indexOf(QLatin1Char('\n'));
    if (nl > 0) t = t.left(nl);
    t = t.trimmed();
    if (t.size() > 40) t = t.left(40);
    return t;
}

class RecycleDialog : public QDialog {
public:
    explicit RecycleDialog(Store *store, QWidget *parent = nullptr) : QDialog(parent), m_store(store)
    {
        setWindowTitle(QStringLiteral("TaskPin 回收站"));
        resize(560, 380);
        auto *lay = new QVBoxLayout(this);
        m_list = new QListWidget(this);
        lay->addWidget(m_list);
        auto *buttons = new QDialogButtonBox(this);
        auto *restore = buttons->addButton(QStringLiteral("恢复"), QDialogButtonBox::ActionRole);
        auto *purge = buttons->addButton(QStringLiteral("彻底删除"), QDialogButtonBox::DestructiveRole);
        buttons->addButton(QStringLiteral("关闭"), QDialogButtonBox::RejectRole);
        lay->addWidget(buttons);
        connect(restore, &QPushButton::clicked, this, &RecycleDialog::restoreSelected);
        connect(purge, &QPushButton::clicked, this, &RecycleDialog::purgeSelected);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        reload();
    }

private:
    void reload()
    {
        m_list->clear();
        m_items = m_store->deletedTasks();
        for (const auto &t : m_items) m_list->addItem(QStringLiteral("%1    %2").arg(t.title, t.updated.left(16)));
    }
    void restoreSelected()
    {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_items.size()) return;
        m_store->restore(m_items.at(row).id);
        reload();
    }
    void purgeSelected()
    {
        const int row = m_list->currentRow();
        if (row < 0 || row >= m_items.size()) return;
        if (QMessageBox::question(this, QStringLiteral("TaskPin"),
                                  QStringLiteral("彻底删除「%1」？不可恢复。").arg(m_items.at(row).title)) !=
            QMessageBox::Yes)
            return;
        m_store->purge(m_items.at(row).id);
        reload();
    }

    Store *m_store;
    QListWidget *m_list = nullptr;
    QList<Task> m_items;
};

}   // namespace

AppHost::AppHost(QObject *parent) : QObject(parent) {}

AppHost::~AppHost() = default;

void AppHost::start(const QString &initialCommand)
{
    m_store = new Store(this);
    if (!m_store->open(Settings::dbPath())) {
        QMessageBox::critical(nullptr, QStringLiteral("TaskPin"), QStringLiteral("无法打开数据库"));
        qApp->quit();
        return;
    }
    const int moved = m_store->rollover();
    qInfo("TaskPin Qt %s started, rolled over %d task(s)", TASKPIN_VERSION, moved);

    // keep the logon entry in sync with settings.json (S5)
    Autostart::setEnabled(Settings::instance().autostart, QCoreApplication::applicationFilePath());

    m_card = new CardWindow(m_store);
    m_card->placeOnScreen();
    m_card->show();

    connect(m_card, &CardWindow::quickAddRequested, this, &AppHost::quickAdd);
    connect(m_card, &CardWindow::recycleBinRequested, this, &AppHost::showRecycleBin);
    connect(m_card, &CardWindow::calendarRequested, this, &AppHost::showCalendar);
    connect(m_card, &CardWindow::settingsRequested, this, &AppHost::showSettings);
    connect(m_card, &CardWindow::zOrderChanged, this, &AppHost::syncCardTopAction);

    m_ocr = new Ocr(this);
    connect(m_ocr, &Ocr::finished, this, &AppHost::onOcrFinished);
    connect(m_ocr, &Ocr::failed, this, &AppHost::onOcrFailed);

    m_ai = new AiClient(this);
    connect(m_ai, &AiClient::finished, this, &AppHost::onAiFinished);
    connect(m_ai, &AiClient::failed, this, [this](const QString &reason) {
        qWarning("ai summarize failed: %s", qUtf8Printable(reason));
        if (!m_pendingOcrText.isEmpty()) createTaskFromCapture(m_pendingLocalTitle);
    });

    m_hotkeys = new Hotkeys(this);
    registerHotkeys();
    connect(m_hotkeys, &Hotkeys::triggered, this, &AppHost::onHotkey);

    buildTray();

    // command channel: TaskPinQt.exe --snip / --full / --last / --pin / --pins / --quick /
    // --toggle / --recycle / --history / --calendar / --settings / --quit (also used by tests)
    m_cmd = new QLocalServer(this);
    QLocalServer::removeServer(QStringLiteral("TaskPinQt.Cmd"));
    if (!m_cmd->listen(QStringLiteral("TaskPinQt.Cmd"))) qWarning("cmd channel listen failed");
    connect(m_cmd, &QLocalServer::newConnection, this, [this] {
        while (auto *sock = m_cmd->nextPendingConnection()) {
            connect(sock, &QLocalSocket::readyRead, this, [this, sock] {
                const QString cmd = QString::fromUtf8(sock->readAll()).trimmed();
                qInfo("cmd: %s", qUtf8Printable(cmd));
                if (cmd.startsWith(QLatin1String("dump "))) {
                    // --dump asks the running instance for a snapshot instead of starting another one,
                    // which would otherwise steal the named pipe from this process.
                    const QString path = cmd.mid(5).trimmed();
                    const bool ok = writeDump(path);
                    sock->write(ok ? "ok\n" : "error\n");
                    sock->flush();
                    sock->waitForBytesWritten(500);
                } else {
                    handleCommand(cmd);
                }
                sock->disconnectFromServer();
            });
            connect(sock, &QLocalSocket::disconnected, sock, &QObject::deleteLater);
        }
    });

    if (!initialCommand.isEmpty()) QTimer::singleShot(0, this, [this, initialCommand] { handleCommand(initialCommand); });
}

void AppHost::handleCommand(const QString &command)
{
    if (command == QLatin1String("snip")) startSnip();
    else if (command == QLatin1String("full")) captureFull();
    else if (command == QLatin1String("last")) startLastRegion();
    else if (command == QLatin1String("pin")) pinFromClipboard();
    else if (command == QLatin1String("pins")) togglePins();
    else if (command == QLatin1String("color")) pickColor();
    else if (command == QLatin1String("board")) showWhiteboard();
    else if (command == QLatin1String("board-transparent")) showTransparentBoard();
    else if (command == QLatin1String("quick")) quickAdd();
    else if (command == QLatin1String("toggle")) toggleCard();
    else if (command == QLatin1String("card-top")) setCardOnTop(true);
    else if (command == QLatin1String("card-desktop")) setCardOnTop(false);
    else if (command == QLatin1String("card-layer")) toggleCardLayer();
    else if (command == QLatin1String("recycle")) showRecycleBin();
    else if (command == QLatin1String("history")) showHistory();
    else if (command == QLatin1String("calendar")) showCalendar();
    else if (command == QLatin1String("settings")) showSettings();
    else if (command == QLatin1String("quit")) quitApp();
    else qWarning("unknown command: %s", qUtf8Printable(command));
}

void AppHost::buildTray()
{
    m_tray = new QSystemTrayIcon(makeTrayIcon(), this);
    m_tray->setToolTip(QStringLiteral("TaskPin %1").arg(QStringLiteral(TASKPIN_VERSION)));

    auto *menu = new QMenu();
    menu->addAction(QStringLiteral("区域截图"), this, &AppHost::startSnip);
    menu->addAction(QStringLiteral("全屏截图并复制"), this, &AppHost::captureFull);
    menu->addAction(QStringLiteral("上一次区域"), this, &AppHost::startLastRegion);
    menu->addSeparator();
    menu->addAction(QStringLiteral("新建任务"), this, &AppHost::quickAdd);
    menu->addAction(QStringLiteral("贴住剪贴板内容"), this, &AppHost::pinFromClipboard);
    menu->addAction(QStringLiteral("显示 / 隐藏全部贴图"), this, &AppHost::togglePins);
    menu->addAction(QStringLiteral("取消全部贴图点击穿透"), [] {
        PinWindow::clearClickThrough();
        PinWindow::flashAll();
    });
    menu->addSeparator();
    menu->addAction(QStringLiteral("取色器"), this, &AppHost::pickColor);
    menu->addAction(QStringLiteral("白板"), this, &AppHost::showWhiteboard);
    menu->addAction(QStringLiteral("透明白板"), this, &AppHost::showTransparentBoard);
    menu->addSeparator();
    menu->addAction(QStringLiteral("截屏历史…"), this, &AppHost::showHistory);
    menu->addAction(QStringLiteral("日历…"), this, &AppHost::showCalendar);
    menu->addAction(QStringLiteral("回收站…"), this, &AppHost::showRecycleBin);
    menu->addAction(QStringLiteral("设置…"), this, &AppHost::showSettings);
    menu->addSeparator();
    menu->addAction(QStringLiteral("显示 / 隐藏卡片"), this, &AppHost::toggleCard);
    m_cardTopAction = menu->addAction(QStringLiteral("卡片全局置顶"));
    m_cardTopAction->setCheckable(true);
    m_cardTopAction->setChecked(Settings::instance().alwaysOnTop);
    m_cardTopAction->setToolTip(QStringLiteral("取消勾选后卡片贴在桌面层：壁纸上方，会被普通窗口盖住"));
    connect(m_cardTopAction, &QAction::triggered, this, [this](bool checked) { setCardOnTop(checked); });
    menu->addAction(QStringLiteral("退出"), this, &AppHost::quitApp);

    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::DoubleClick) toggleCard();
    });
    m_tray->show();
}

void AppHost::registerHotkeys()
{
    if (!m_hotkeys) return;
    m_hotkeys->unbindAll();
    int registered = 0;
    for (const auto &pair : hotkeyActions()) {
        const QString wanted = Settings::instance().chordFor(pair.first);
        bool ok = m_hotkeys->bind(pair.second, wanted);
        QString actual = wanted;
        if (!ok) {
            const QString fallback = Settings::fallbackChord(pair.first);
            if (!fallback.isEmpty()) {
                ok = m_hotkeys->bind(pair.second, fallback);
                if (ok) actual = fallback;
            }
        }
        if (ok) ++registered;
        qInfo("hotkey %s = %s %s%s", qUtf8Printable(pair.first), qUtf8Printable(actual),
              ok ? "(registered)" : "(FAILED)", ok && actual != wanted ? " [degraded]" : "");
    }
    qInfo("hotkeys: %d/%d registered", registered, int(hotkeyActions().size()));
}

void AppHost::onHotkey(int id)
{
    qInfo("onHotkey id=%d", id);
    switch (id) {
    case Hotkeys::Snip: startSnip(); break;
    case Hotkeys::Full: captureFull(); break;
    case Hotkeys::Last: startLastRegion(); break;
    case Hotkeys::Pin: pinFromClipboard(); break;
    case Hotkeys::Pins: togglePins(); break;
    case Hotkeys::QuickAdd: quickAdd(); break;
    case Hotkeys::ToggleCard: toggleCard(); break;
    case Hotkeys::History: showHistory(); break;
    default: break;
    }
}

// ---------------------------------------------------------------- snipping

void AppHost::hideOverlaysForCapture()
{
    m_hiddenPins.clear();
    for (auto *pin : PinWindow::all()) {
        if (pin->isVisible()) {
            pin->hide();
            m_hiddenPins.append(pin);
        }
    }
    if (m_card) m_card->hide();
    QGuiApplication::processEvents();
}

void AppHost::restoreAfterCapture()
{
    if (m_card) m_card->show();
    for (auto &pin : m_hiddenPins)
        if (pin) pin->show();
    m_hiddenPins.clear();
}

void AppHost::startSnip()
{
    if (m_snip) {
        m_snip->raise();
        m_snip->activateWindow();
        return;
    }
    const double delay = Settings::instance().snipDelay;
    if (delay > 0.0) {
        qInfo("startSnip: delayed %.1fs", delay);
        toast(QStringLiteral("%1 秒后截图…").arg(delay, 0, 'f', 1));
        QTimer::singleShot(qRound(delay * 1000.0), this, [this] { captureAndEdit(false); });
        return;
    }
    captureAndEdit(false);
}

void AppHost::startLastRegion()
{
    if (m_snip) {
        m_snip->raise();
        return;
    }
    if (!Settings::instance().lastRegionRect().isValid()) {
        qInfo("startLastRegion: no region yet, falling back to a fresh snip");
        toast(QStringLiteral("还没有上一次选区，先框选一次"));
        captureAndEdit(false);
        return;
    }
    captureAndEdit(true);
}

void AppHost::captureAndEdit(bool preselectLast)
{
    if (m_snip) return;

    hideOverlaysForCapture();
    const ScreenCapture shot = captureVirtualDesktop();
    if (!shot.isValid()) {
        restoreAfterCapture();
        qWarning("screen capture failed");
        return;
    }
    qInfo("startSnip: capture ok %dx%d dpr=%.2f", shot.logicalRect.width(), shot.logicalRect.height(), shot.dpr);

    auto *overlay = new SnipOverlay(shot);
    m_snip = overlay;
    if (preselectLast) {
        const QRect last = Settings::instance().lastRegionRect();
        if (last.isValid()) overlay->setPreselect(last);
    }

    connect(overlay, &SnipOverlay::captured, this, &AppHost::recordCapture);
    connect(overlay, &SnipOverlay::saved, this, [this](const QString &path) {
        toast(QStringLiteral("已保存：%1").arg(QDir::toNativeSeparators(path)));
    });
    connect(overlay, &SnipOverlay::copied, this, [this](const QImage &image) {
        toast(QStringLiteral("已复制 %1 × %2").arg(image.width()).arg(image.height()));
    });
    connect(overlay, &SnipOverlay::pinned, this, &AppHost::pinImage);
    connect(overlay, &SnipOverlay::recognizeRequested, this, &AppHost::recognizeImage);
    connect(overlay, &SnipOverlay::printed, this, &AppHost::printImage);
    connect(overlay, &QObject::destroyed, this, [this] {
        m_snip = nullptr;
        restoreAfterCapture();
    });

    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    WindowFx::forceForeground(overlay);
}

void AppHost::captureFull()
{
    if (m_snip) return;
    hideOverlaysForCapture();
    const ScreenCapture shot = captureVirtualDesktop();
    if (!shot.isValid()) {
        restoreAfterCapture();
        qWarning("full capture failed");
        return;
    }
    const QRect sel = shot.logicalRect;
    const qreal dpr = shot.dpr;
    const QRect src(qRound(sel.x() * dpr), qRound(sel.y() * dpr), qRound(sel.width() * dpr),
                    qRound(sel.height() * dpr));
    QImage image(src.size(), QImage::Format_ARGB32_Premultiplied);
    {
        QPainter p(&image);
        p.drawPixmap(image.rect(), shot.pixmap, src);
    }
    QString path;
    if (Settings::instance().autoSave) {
        const auto &s = Settings::instance();
        path = FileNaming::uniquePath(s.resolveAutoSaveDir(), s.namePattern, QDateTime::currentDateTime());
        if (!image.save(path, "PNG")) path.clear();
    }

    QApplication::clipboard()->setImage(image);
    recordCapture(image, sel);
    restoreAfterCapture();
    qInfo("full capture copied %dx%d", image.width(), image.height());
    toast(QStringLiteral("已截图并复制 %1 × %2").arg(image.width()).arg(image.height()));
}

void AppHost::recordCapture(const QImage &image, const QRect &logicalRegion)
{
    if (image.isNull()) return;
    const QString history = History::save(image);
    auto &s = Settings::instance();
    if (s.autoSave) {
        const QString path = FileNaming::uniquePath(s.resolveAutoSaveDir(), s.namePattern, QDateTime::currentDateTime());
        if (!image.save(path, "PNG")) qWarning() << "auto save failed:" << path;
        else qInfo("auto saved: %s", qUtf8Printable(path));
    }
    if (logicalRegion.isValid()) s.setLastRegion(logicalRegion);
    s.save();
    qInfo("capture recorded %dx%d history=%s region=%s", image.width(), image.height(), qUtf8Printable(history),
          qUtf8Printable(s.lastRegion));
}

// ---------------------------------------------------------------- pins

void AppHost::pinImage(const QImage &image)
{
    if (image.isNull()) return;
    auto *pin = new PinWindow(image);
    connect(pin, &PinWindow::recognizeRequested, this, &AppHost::recognizeImage);
    pin->show();
    WindowFx::keepOnTop(pin);
    toast(QStringLiteral("已贴到桌面 %1 × %2").arg(image.width()).arg(image.height()));
}

void AppHost::pinFromClipboard()
{
    const QImage image = QApplication::clipboard()->image();
    if (!image.isNull()) {
        pinImage(image);
        return;
    }
    const QString text = QApplication::clipboard()->text().trimmed();
    if (text.isEmpty()) {
        toast(QStringLiteral("剪贴板里没有图片或文字"));
        return;
    }
    auto *pin = PinWindow::createTextPin(text);
    if (!pin) return;
    connect(pin, &PinWindow::recognizeRequested, this, &AppHost::recognizeImage);
    pin->show();
    WindowFx::keepOnTop(pin);
    toast(QStringLiteral("已贴出文字卡片"));
}

void AppHost::togglePins()
{
    const bool visible = PinWindow::toggleAll();
    toast(visible ? QStringLiteral("贴图已显示") : QStringLiteral("贴图已隐藏"));
}

void AppHost::pickColor()
{
    if (m_snip) return;
    hideOverlaysForCapture();
    const ScreenCapture shot = captureVirtualDesktop();
    if (!shot.isValid()) {
        restoreAfterCapture();
        qWarning("color picker capture failed");
        return;
    }
    auto *overlay = new ColorPickerOverlay(shot);
    connect(overlay, &ColorPickerOverlay::picked, this, [this](const QColor &color) {
        const QString hex = color.name(QColor::HexRgb).toUpper();
        QApplication::clipboard()->setText(hex);
        qInfo("color picked: %s", qUtf8Printable(hex));
        toast(QStringLiteral("已复制色值 %1").arg(hex));
    });
    connect(overlay, &QObject::destroyed, this, [this] { restoreAfterCapture(); });
    overlay->show();
    overlay->raise();
    overlay->activateWindow();
    overlay->setFocus();
    WindowFx::forceForeground(overlay);
}

void AppHost::showWhiteboard() { captureWhiteboard(false); }

void AppHost::showTransparentBoard() { captureWhiteboard(true); }

void AppHost::captureWhiteboard(bool transparent)
{
    if (m_snip) return;
    hideOverlaysForCapture();
    const ScreenCapture shot = captureVirtualDesktop();
    if (!shot.isValid()) {
        restoreAfterCapture();
        qWarning("whiteboard capture failed");
        return;
    }
    auto *board = new WhiteboardWindow(shot, transparent);
    connect(board, &WhiteboardWindow::copied, this, [this, transparent](const QImage &image) {
        if (!transparent) recordCapture(image, QRect());
        toast(QStringLiteral("白板已复制 %1 × %2").arg(image.width()).arg(image.height()));
    });
    connect(board, &WhiteboardWindow::saved, this, [this](const QString &path) {
        toast(QStringLiteral("已保存：%1").arg(QDir::toNativeSeparators(path)));
    });
    connect(board, &WhiteboardWindow::printRequested, this, &AppHost::printImage);
    connect(board, &QObject::destroyed, this, [this] { restoreAfterCapture(); });
    board->show();
    board->raise();
    board->activateWindow();
    board->setFocus();
    WindowFx::forceForeground(board);
    qInfo("whiteboard opened (transparent=%d)", int(transparent));
}

// ---------------------------------------------------------------- OCR / AI

void AppHost::recognizeImage(const QImage &image)
{
    if (!m_ocr) return;
    if (m_ocr->busy()) {
        toast(QStringLiteral("上一次识别还没结束…"));
        return;
    }
    toast(QStringLiteral("正在识别文字…"));
    m_ocr->recognize(image);
}

void AppHost::onOcrFinished(const QString &text)
{
    m_pendingOcrText = text.trimmed();
    if (m_pendingOcrText.isEmpty()) {
        toast(QStringLiteral("没有识别到文字"));
        return;
    }
    const ParsedTask parsed = TimeParser::parse(m_pendingOcrText);
    m_pendingDue = parsed.due;
    m_pendingPriority = parsed.priority;
    m_pendingLocalTitle = firstLineTitle(parsed.title);
    if (m_pendingLocalTitle.isEmpty()) m_pendingLocalTitle = firstLineTitle(m_pendingOcrText);

    const auto &s = Settings::instance();
    if (s.aiEnabled && !s.aiApiKey.trimmed().isEmpty()) {
        toast(QStringLiteral("识别完成，AI 归纳中…"));
        m_ai->summarize(m_pendingOcrText);
        return;
    }
    createTaskFromCapture(m_pendingLocalTitle);
}

void AppHost::onOcrFailed(const QString &reason)
{
    toast(QStringLiteral("识别失败：%1").arg(reason));
    m_pendingOcrText.clear();
}

void AppHost::onAiFinished(const QString &title)
{
    createTaskFromCapture(title.trimmed().isEmpty() ? m_pendingLocalTitle : title.trimmed());
}

void AppHost::createTaskFromCapture(const QString &title)
{
    if (title.trimmed().isEmpty() || m_pendingOcrText.isEmpty()) return;
    const QString id = m_store->addTask(title.trimmed(), QStringLiteral("ocr"), QString(), m_pendingDue,
                                        m_pendingPriority, m_pendingOcrText);
    qInfo("ocr task created: %s id=%s due=%s", qUtf8Printable(title), qUtf8Printable(id),
          qUtf8Printable(m_pendingDue.isValid() ? m_pendingDue.toString(Qt::ISODate) : QStringLiteral("-")));
    toast(QStringLiteral("已建任务：%1").arg(title.trimmed()));
    m_pendingOcrText.clear();
    m_pendingLocalTitle.clear();
    m_pendingDue = QDateTime();
    m_pendingPriority = 2;
}

// ---------------------------------------------------------------- windows

void AppHost::quickAdd()
{
    auto *dlg = new QuickAddDialog();
    connect(dlg, &QDialog::accepted, this, [this, dlg] {
        const QString raw = dlg->text();
        if (raw.isEmpty()) return;
        const ParsedTask parsed = TimeParser::parse(raw);
        const QString id = m_store->addTask(parsed.title, QStringLiteral("manual"), QString(), parsed.due,
                                            parsed.priority);
        qInfo("task added: %s id=%s due=%s", qUtf8Printable(parsed.title), qUtf8Printable(id),
              qUtf8Printable(parsed.due.isValid() ? parsed.due.toString(Qt::ISODate) : QStringLiteral("-")));
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    WindowFx::forceForeground(dlg);
}

void AppHost::showRecycleBin()
{
    auto *dlg = new RecycleDialog(m_store);
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
    WindowFx::forceForeground(dlg);
}

void AppHost::showHistory()
{
    if (m_history) {
        m_history->raise();
        WindowFx::forceForeground(m_history);
        return;
    }
    auto *dlg = new HistoryDialog();
    m_history = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dlg, &HistoryDialog::pinRequested, this, &AppHost::pinImage);
    connect(dlg, &HistoryDialog::printRequested, this, &AppHost::printImage);
    dlg->show();
    dlg->raise();
    WindowFx::forceForeground(dlg);
}

void AppHost::showCalendar()
{
    if (m_calendar) {
        m_calendar->raise();
        WindowFx::forceForeground(m_calendar);
        return;
    }
    auto *window = new CalendarWindow(m_store);
    m_calendar = window;
    window->setAttribute(Qt::WA_DeleteOnClose, true);
    window->show();
    window->raise();
    WindowFx::forceForeground(window);
}

void AppHost::showSettings()
{
    if (m_settings) {
        m_settings->raise();
        WindowFx::forceForeground(m_settings);
        return;
    }
    auto *dlg = new SettingsDialog();
    m_settings = dlg;
    dlg->setAttribute(Qt::WA_DeleteOnClose, true);
    connect(dlg, &SettingsDialog::saved, this, &AppHost::applySettings);
    dlg->show();
    dlg->raise();
    WindowFx::forceForeground(dlg);
}

void AppHost::applySettings()
{
    qInfo("settings applied");
    registerHotkeys();
    if (m_card) m_card->applySettings();
    syncCardTopAction();
    Autostart::setEnabled(Settings::instance().autostart, QCoreApplication::applicationFilePath());
    toast(QStringLiteral("设置已保存"));
}

void AppHost::setCardOnTop(bool onTop)
{
    if (m_card) {
        m_card->setDesktopLayer(!onTop);
    } else {
        auto &s = Settings::instance();
        s.alwaysOnTop = onTop;
        s.save();
    }
    syncCardTopAction();
    qInfo("card layer set: %s", onTop ? "topmost" : "desktop");
    toast(onTop ? QStringLiteral("卡片已全局置顶")
                : QStringLiteral("卡片已贴到桌面层（壁纸上方，普通窗口会盖住它）"));
}

void AppHost::toggleCardLayer() { setCardOnTop(!Settings::instance().alwaysOnTop); }

void AppHost::syncCardTopAction()
{
    if (!m_cardTopAction) return;
    const bool onTop = m_card ? !m_card->isDesktopLayer() : Settings::instance().alwaysOnTop;
    if (m_cardTopAction->isChecked() == onTop) return;
    const QSignalBlocker blocker(m_cardTopAction);
    m_cardTopAction->setChecked(onTop);
}

void AppHost::toggleCard()
{
    if (!m_card) return;
    if (m_card->isVisible()) {
        m_card->hide();
    } else {
        m_card->show();
        // showEvent 里已经按模式重新排过 z-order；置顶模式再抬一次，桌面层模式保持最底层
        if (!m_card->isDesktopLayer()) m_card->raise();
        else m_card->applyZOrder();
    }
}

void AppHost::printImage(const QImage &image)
{
    if (image.isNull()) return;
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer);
    dialog.setWindowTitle(QStringLiteral("打印截图"));
    if (dialog.exec() != QDialog::Accepted) return;

    QPainter painter(&printer);
    if (!painter.isActive()) {
        qWarning("printer painter inactive");
        return;
    }
    const QRect page = printer.pageRect(QPrinter::DevicePixel).toRect();
    const QSize scaled = image.size().scaled(page.size(), Qt::KeepAspectRatio);
    const QRect target((page.width() - scaled.width()) / 2, (page.height() - scaled.height()) / 2, scaled.width(),
                       scaled.height());
    painter.drawImage(target, image);
}

void AppHost::toast(const QString &message)
{
    qInfo("toast: %s", qUtf8Printable(message));
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(QStringLiteral("TaskPin"), message, QSystemTrayIcon::Information, 2200);
}

void AppHost::quitApp()
{
    if (m_hotkeys) m_hotkeys->unbindAll();
    Settings::instance().save();
    qApp->quit();
}

// ---------------------------------------------------------------- diagnostics

QJsonObject AppHost::dumpState() const
{
    const auto &s = Settings::instance();
    QJsonObject o;
    o["app"] = QStringLiteral("TaskPinQt");
    o["version"] = QStringLiteral(TASKPIN_VERSION);
    o["time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o["dataDir"] = Settings::dataDir();
    o["dbPath"] = Settings::dbPath();
    o["logPath"] = Settings::logPath();
    o["historyDir"] = History::dir();

    QJsonObject cfg;
    cfg["cardPosition"] = s.cardPosition;
    cfg["cardWidth"] = s.cardWidth;
    cfg["visibleRows"] = s.visibleRows;
    cfg["opacity"] = s.opacity;
    cfg["alwaysOnTop"] = s.alwaysOnTop;
    cfg["showCarryMark"] = s.showCarryMark;
    cfg["cardBg"] = s.cardBg.name();
    cfg["cardFg"] = s.cardFg.name();
    cfg["cardX"] = s.cardX;
    cfg["cardY"] = s.cardY;
    cfg["snipDelay"] = s.snipDelay;
    cfg["annotationColor"] = s.annotationColor.name();
    cfg["annotationWidth"] = s.annotationWidth;
    cfg["autoSave"] = s.autoSave;
    cfg["autoSaveDir"] = s.resolveAutoSaveDir();
    cfg["namePattern"] = s.namePattern;
    cfg["historyLimit"] = s.historyLimit;
    cfg["lastRegion"] = s.lastRegion;
    cfg["pinOpacity"] = s.pinOpacity;
    cfg["pinGroup"] = s.pinGroup;
    cfg["aiEnabled"] = s.aiEnabled;
    cfg["aiBaseUrl"] = s.aiBaseUrl;
    cfg["aiModel"] = s.aiModel;
    cfg["autostart"] = Autostart::isEnabled();
    o["settings"] = cfg;

    QJsonObject tasks;
    if (m_store) {
        const auto open = m_store->openTasks();
        int carried = 0;
        for (const auto &t : open) carried += t.carryCount > 0 ? 1 : 0;
        tasks["open"] = open.size();
        tasks["carried"] = carried;
        tasks["done"] = m_store->countByState(QStringLiteral("done"));
        tasks["dropped"] = m_store->countByState(QStringLiteral("dropped"));
    }
    o["tasks"] = tasks;

    QJsonArray hotkeys;
    if (m_hotkeys) {
        for (const auto &pair : hotkeyActions()) {
            QJsonObject item;
            item["action"] = pair.first;
            item["chord"] = m_hotkeys->label(pair.second);
            item["registered"] = !m_hotkeys->label(pair.second).isEmpty();
            hotkeys.append(item);
        }
    }
    o["hotkeys"] = hotkeys;

    if (m_card) {
        QJsonObject card;
        card["x"] = m_card->x();
        card["y"] = m_card->y();
        card["w"] = m_card->width();
        card["h"] = m_card->height();
        card["visible"] = m_card->isVisible();
        card["desktopLayer"] = m_card->isDesktopLayer();
        card["topMost"] = WindowFx::isTopMost(m_card);
        card["zOrder"] = m_card->isDesktopLayer() ? QStringLiteral("desktop") : QStringLiteral("topmost");
        o["card"] = card;
    }

    QJsonObject pins;
    pins["count"] = int(PinWindow::all().size());
    pins["group"] = PinWindow::currentGroup();
    QJsonArray groups;
    for (const QString &g : PinWindow::groups()) groups.append(g);
    pins["groups"] = groups;
    o["pins"] = pins;

    QJsonObject history;
    history["count"] = int(History::recent().size());
    history["limit"] = s.historyLimit;
    o["history"] = history;

    QJsonObject ocr;
    ocr["available"] = Ocr::available();
    ocr["engine"] = QStringLiteral("Windows.Media.Ocr");
    o["ocr"] = ocr;

    QJsonArray screens;
    for (auto *screen : QGuiApplication::screens()) {
        QJsonObject item;
        item["name"] = screen->name();
        const QRect g = screen->geometry();
        item["geometry"] = QStringLiteral("%1,%2,%3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
        item["dpr"] = screen->devicePixelRatio();
        screens.append(item);
    }
    o["screens"] = screens;
    return o;
}

bool AppHost::writeDump(const QString &path) const
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning("dump open failed: %s", qUtf8Printable(path));
        return false;
    }
    const QByteArray bytes = QJsonDocument(dumpState()).toJson(QJsonDocument::Indented);
    f.write(bytes);
    f.close();
    qInfo("dump written: %s (%d bytes)", qUtf8Printable(path), int(bytes.size()));
    return true;
}
