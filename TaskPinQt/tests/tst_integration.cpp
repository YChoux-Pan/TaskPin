#include "ui/PinWindow.h"
#include "core/Settings.h"
#include "core/InstanceNames.h"
#include "core/Store.h"
#include "ui/CardWindow.h"
#include "ui/SnipOverlay.h"
#include "ui/SettingsDialog.h"
#include "platform/HotkeyEdit.h"
#include "platform/Hotkeys.h"
#include <QCheckBox>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QLabel>
#include <QScreen>
#include <QScopeGuard>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <QtTest>
#include <functional>
// Install a synthetic editor for host routing tests without taking a desktop capture.
#define private public
#include "app/AppHost.h"
#undef private
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

namespace {
#ifdef Q_OS_WIN
struct RunValue {
    LONG status = ERROR_SUCCESS;
    DWORD type = 0;
    QByteArray bytes;
};
RunValue readRunValue()
{
    RunValue result;
    HKEY key = nullptr;
    result.status = RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_QUERY_VALUE, &key);
    if (result.status != ERROR_SUCCESS) return result;
    const auto closeKey = qScopeGuard([key] { RegCloseKey(key); });
    DWORD size = 0;
    result.status = RegQueryValueExW(key, L"TaskPinQt", nullptr, &result.type, nullptr, &size);
    if (result.status != ERROR_SUCCESS) return result;
    result.bytes.resize(size);
    result.status = RegQueryValueExW(key, L"TaskPinQt", nullptr, &result.type,
        reinterpret_cast<LPBYTE>(result.bytes.data()), &size);
    result.bytes.resize(size);
    return result;
}
#endif
void inspectMenu(PinWindow &pin, const std::function<void(QMenu *)> &inspect)
{
    QTest::ignoreMessage(QtWarningMsg, "This plugin does not support raise()");
    QTest::ignoreMessage(QtWarningMsg, "This plugin does not support grabbing the keyboard");
    QTimer::singleShot(0, &pin, [&pin, inspect] {
        auto *menu = pin.findChild<QMenu *>();
        inspect(menu);
        if (menu) menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, {10, 10}, pin.mapToGlobal(QPoint(10, 10)));
    QApplication::sendEvent(&pin, &event);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}
QAction *named(QMenu *menu, const QString &text)
{
    if (!menu) return nullptr;
    for (QAction *a : menu->actions()) {
        if (a->text().section('\t', 0, 0) == text) return a;
        if (a->menu()) if (auto *found = named(a->menu(), text)) return found;
    }
    return nullptr;
}
}

class IntegrationTests : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { QSqlDatabase::removeDatabase(QStringLiteral("taskpin")); }
    void initTestCase()
    {
        QVERIFY(m_data.isValid());
        qputenv("TASKPIN_DATA", m_data.path().toUtf8());
        qputenv("TASKPIN_AUTOMATION", "1");
        Settings::instance().autoSaveDir = m_data.path() + "/images";
    }
    void hotkeySettingsUpgrade_data()
    {
        QTest::addColumn<QByteArray>("json");
        QTest::addColumn<QStringList>("expected");
        QTest::newRow("fresh") << QByteArray("{}")
            << QStringList{"F1", "F3", "Shift+F3"};
        QTest::newRow("old-defaults")
            << QByteArray(R"({"version":2,"hotkeys":{"snip":"Ctrl+Alt+A","pin":"Ctrl+Alt+V","pins":"Ctrl+Alt+H"}})")
            << QStringList{"F1", "F3", "Shift+F3"};
        QTest::newRow("legacy-flat") << QByteArray(R"({"snipHotkey":"Ctrl+Alt+A"})")
            << QStringList{"F1", "F3", "Shift+F3"};
        QTest::newRow("custom")
            << QByteArray(R"({"version":2,"hotkeys":{"snip":"Ctrl+Shift+1","pin":"Ctrl+Shift+2","pins":"Ctrl+Shift+3"}})")
            << QStringList{"Ctrl+Shift+1", "Ctrl+Shift+2", "Ctrl+Shift+3"};
        QTest::newRow("new-schema-preserves-old-chords")
            << QByteArray(R"({"version":3,"hotkeys":{"snip":"Ctrl+Alt+A","pin":"Ctrl+Alt+V","pins":"Ctrl+Alt+H"}})")
            << QStringList{"Ctrl+Alt+A", "Ctrl+Alt+V", "Ctrl+Alt+H"};
        QTest::newRow("mixed")
            << QByteArray(R"({"version":2,"hotkeys":{"snip":"Ctrl+Alt+A","pin":"Ctrl+Shift+2","pins":"Ctrl+Alt+H"}})")
            << QStringList{"F1", "Ctrl+Shift+2", "Shift+F3"};
    }
    void hotkeySettingsUpgrade()
    {
        QFETCH(QByteArray, json);
        QFETCH(QStringList, expected);
        QFile file(Settings::settingsPath());
        const bool existed = file.exists();
        QByteArray original;
        if (existed) { QVERIFY(file.open(QIODevice::ReadOnly)); original = file.readAll(); file.close(); }
        const auto restore = qScopeGuard([&] {
            if (!existed) { file.remove(); return; }
            if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { file.write(original); file.close(); }
        });
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(json), qint64(json.size()));
        file.close();
        Settings settings;
        settings.load();
        const QStringList keys{"snip", "pin", "pins"};
        for (int i = 0; i < keys.size(); ++i) QCOMPARE(settings.chordFor(keys[i]), expected[i]);
        QCOMPARE(settings.chordFor("quick"), QStringLiteral("Ctrl+Alt+Space"));
        settings.save();
        Settings reloaded;
        reloaded.load();
        for (int i = 0; i < keys.size(); ++i) QCOMPARE(reloaded.chordFor(keys[i]), expected[i]);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("version").toInt(), 3);
        file.close();
    }
    void editorTranslationHotkeyPersistsThroughDialog()
    {
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; Settings::instance().save(); });
        Settings::instance().resetToDefaults();
        SettingsDialog dialog;
        auto *translate = dialog.findChild<HotkeyEdit *>("hotkey_editorTranslate");
        QVERIFY(translate);
        QCOMPARE(translate->chord(), QString("Ctrl+T"));
        bool scopedLabel = false;
        for (auto *candidate : dialog.findChildren<QLabel *>())
            scopedLabel |= candidate->text().contains(QStringLiteral("翻译")) && candidate->text().contains(QStringLiteral("F1框选后"));
        QVERIFY(scopedLabel);
        QTest::keyClick(translate, Qt::Key_F8, Qt::AltModifier);
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QSignalSpy saved(&dialog, &SettingsDialog::saved);
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) button->click();
        QCOMPARE(saved.count(), 1);
        Settings reloaded; reloaded.load();
        QCOMPARE(reloaded.chordFor("editorTranslate"), QString("Alt+F8"));
        QCOMPARE(reloaded.chordFor("translate"), QString());
        SettingsDialog reopened;
        auto *again = reopened.findChild<HotkeyEdit *>("hotkey_editorTranslate"); QVERIFY(again);
        QCOMPARE(again->chord(), QString("Alt+F8"));
        QTest::keyClick(again, Qt::Key_F9, Qt::AltModifier);
        reopened.reject();
        Settings afterCancel; afterCancel.load();
        QCOMPARE(afterCancel.chordFor("editorTranslate"), QString("Alt+F8"));
        SettingsDialog cleared;
        auto *clearEdit = cleared.findChild<HotkeyEdit *>("hotkey_editorTranslate"); QVERIFY(clearEdit);
        QTest::keyClick(clearEdit, Qt::Key_Backspace);
        auto *clearButtons = cleared.findChild<QDialogButtonBox *>(); QVERIFY(clearButtons);
        for (auto *button : clearButtons->buttons())
            if (clearButtons->buttonRole(button) == QDialogButtonBox::AcceptRole) button->click();
        Settings afterClear; afterClear.load();
        QCOMPARE(afterClear.chordFor("editorTranslate"), QString("Ctrl+T"));
    }
    void editorTranslationRejectsShortcutCollision_data()
    {
        QTest::addColumn<QString>("chord");
        QTest::newRow("global-pin") << QString("F3");
        QTest::newRow("editor-copy") << QString("Enter");
        QTest::newRow("editor-save") << QString("Ctrl+S");
        QTest::newRow("pin-alias") << QString("Ctrl+P");
        QTest::newRow("pin-fallback") << QString("Ctrl+Shift+F12");
        QTest::newRow("snip-fallback") << QString("Ctrl+Shift+F9");
    }
    void editorTranslationRejectsShortcutCollision()
    {
        QFETCH(QString, chord);
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; Settings::instance().save(); });
        Settings::instance().resetToDefaults();
        QVERIFY(Settings::instance().save());
        SettingsDialog dialog;
        auto *translate = dialog.findChild<HotkeyEdit *>("hotkey_editorTranslate"); QVERIFY(translate);
        translate->setChord(chord);
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QSignalSpy saved(&dialog, &SettingsDialog::saved);
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) button->click();
        QCOMPARE(saved.count(), 0);
        QCOMPARE(Settings::instance().chordFor("editorTranslate"), QString("Ctrl+T"));
        Settings reloaded; reloaded.load();
        QCOMPARE(reloaded.chordFor("editorTranslate"), QString("Ctrl+T"));
        auto *error = dialog.findChild<QLabel *>("settingsSaveError"); QVERIFY(error);
        QVERIFY(!error->text().isEmpty());
    }
    void globalHotkeysProtectEditorCommands_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("chord");
        QTest::addColumn<bool>("accepted");
        QTest::newRow("history-cannot-steal-pin-alias") << QString("history") << QString("Ctrl+P") << false;
        QTest::newRow("quick-cannot-steal-ocr") << QString("quick") << QString("Ctrl+R") << false;
        QTest::newRow("pin-can-use-its-own-alias") << QString("pin") << QString("Ctrl+P") << true;
    }
    void globalHotkeysProtectEditorCommands()
    {
        QFETCH(QString, action); QFETCH(QString, chord); QFETCH(bool, accepted);
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; Settings::instance().save(); });
        Settings::instance().resetToDefaults();
        QVERIFY(Settings::instance().save());
        const QString original = Settings::instance().chordFor(action);
        SettingsDialog dialog;
        auto *edit = dialog.findChild<HotkeyEdit *>("hotkey_" + action); QVERIFY(edit);
        edit->setChord(chord);
        auto *buttons = dialog.findChild<QDialogButtonBox *>(); QVERIFY(buttons);
        QSignalSpy saved(&dialog, &SettingsDialog::saved);
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole) button->click();
        QCOMPARE(saved.count(), accepted ? 1 : 0);
        Settings reloaded; reloaded.load();
        QCOMPARE(reloaded.chordFor(action), accepted ? chord : original);
        auto *error = dialog.findChild<QLabel *>("settingsSaveError"); QVERIFY(error);
        QCOMPARE(error->text().isEmpty(), accepted);
    }
    void pinHotkeyRoutesToEditorInsteadOfClipboard_data()
    {
        QTest::addColumn<bool>("selected");
        QTest::newRow("selected") << true;
        QTest::newRow("no-selection") << false;
    }
    void pinHotkeyRoutesToEditorInsteadOfClipboard()
    {
        QFETCH(bool, selected);
        AppHost host;
        QPixmap pixels(800, 600); pixels.fill(Qt::red);
        host.m_snip = new SnipOverlay({pixels, QRect(-800, 100, 800, 600), 1., "synthetic"});
        host.m_snip->setAttribute(Qt::WA_DeleteOnClose, false);
        if (selected) host.m_snip->setPreselect({-760, 140, 300, 200});
        QSignalSpy pinned(host.m_snip, &SnipOverlay::pinned);
        QImage clipboard(20, 10, QImage::Format_RGB32); clipboard.fill(Qt::blue);
        QApplication::clipboard()->setImage(clipboard);
        const int before = PinWindow::all().size();
        const auto closePins = qScopeGuard([] { PinWindow::closeAll(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        QVERIFY(QMetaObject::invokeMethod(&host, "onHotkey", Qt::DirectConnection, Q_ARG(int, Hotkeys::Pin)));
        QCOMPARE(PinWindow::all().size(), before);
        QCOMPARE(pinned.count(), selected ? 1 : 0);
        if (selected) QCOMPARE(qvariant_cast<QImage>(pinned.first().first()).size(), QSize(300, 200));
        QCOMPARE(QApplication::clipboard()->image(), clipboard);
    }
    void pinHotkeyWithoutEditorKeepsClipboardBehavior()
    {
        AppHost host;
        QImage clipboard(20, 10, QImage::Format_RGB32); clipboard.fill(Qt::blue);
        QApplication::clipboard()->setImage(clipboard);
        const int before = PinWindow::all().size();
        const auto closePins = qScopeGuard([] { PinWindow::closeAll(); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
        QVERIFY(QMetaObject::invokeMethod(&host, "onHotkey", Qt::DirectConnection, Q_ARG(int, Hotkeys::Pin)));
        QCOMPARE(PinWindow::all().size(), before + 1);
        QCOMPARE(PinWindow::all().last()->image(), clipboard);
    }
    void instanceNamespaces()
    {
        QCOMPARE(instanceNamesFor(QString()).command, QStringLiteral("TaskPinQt.Cmd"));
        QCOMPARE(instanceNamesFor(QString()).singleInstance, QStringLiteral("TaskPinQt.SingleInstance"));
        QDir root(m_data.path());
        QVERIFY(root.mkpath("a"));
        QVERIFY(root.mkpath("b"));
        const QString a = root.filePath("a");
        const QString b = root.filePath("b");
        QCOMPARE(instanceNamesFor(a).command, instanceNamesFor(a + "/../a/.").command);
        QCOMPARE(instanceNamesFor(a).singleInstance, instanceNamesFor(QDir::toNativeSeparators(a)).singleInstance);
#ifdef Q_OS_WIN
        QCOMPARE(instanceNamesFor(a).command, instanceNamesFor(a.toUpper()).command);
#endif
        QCOMPARE(instanceNamesFor(a + "/new").command, instanceNamesFor(a + "/new/../new/.").command);
        QVERIFY(instanceNamesFor(a).command != instanceNamesFor(b).command);
        QVERIFY(instanceNamesFor(a).singleInstance != instanceNamesFor(b).singleInstance);
        QVERIFY(instanceNamesFor(a).command != instanceNamesFor(QString()).command);
    }
    void aliasedDataDirectoryNamespaceIsStable()
    {
#ifdef Q_OS_WIN
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const QString target = root.path() + "/target";
        const QString alias = root.path() + "/alias";
        QVERIFY(QDir().mkpath(target));
        QProcess junction;
        junction.start(QStringLiteral("cmd.exe"), {"/c", "mklink", "/J",
            QDir::toNativeSeparators(alias), QDir::toNativeSeparators(target)});
        QVERIFY(junction.waitForFinished(10000));
        if (junction.exitCode() != 0) QSKIP("Temporary junction creation is not supported in this environment");
        const auto removeAlias = qScopeGuard([alias] { QDir().rmdir(alias); });
        QVERIFY(QFileInfo(alias).isJunction());
        const QString throughAlias = alias + "/new/nested/data";
        const QString throughTarget = target + "/new/nested/data";
        const auto aliasBefore = instanceNamesFor(throughAlias);
        const auto targetBefore = instanceNamesFor(throughTarget);
        QCOMPARE(aliasBefore.command, targetBefore.command);
        QCOMPARE(aliasBefore.singleInstance, targetBefore.singleInstance);
        QVERIFY(QDir().mkpath(throughAlias));
        const auto aliasAfter = instanceNamesFor(throughAlias);
        const auto targetAfter = instanceNamesFor(throughTarget);
        QCOMPARE(aliasBefore.command, aliasAfter.command);
        QCOMPARE(aliasBefore.singleInstance, aliasAfter.singleInstance);
        QCOMPARE(aliasAfter.command, targetAfter.command);
        QCOMPARE(aliasAfter.singleInstance, targetAfter.singleInstance);

        // First startup computes names before Settings creates this new directory.
        const QString processAlias = alias + "/process/new-data";
        const QString processTarget = target + "/process/new-data";
        const QString executable = QCoreApplication::applicationDirPath() + "/TaskPinQt.exe";
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("TASKPIN_DATA", processAlias);
        env.insert("TASKPIN_AUTOMATION", "1");
        QProcess owner;
        owner.setProcessEnvironment(env);
        owner.start(executable, QStringList{});
        QVERIFY(owner.waitForStarted(10000));
        const auto ready = [&] {
            QLocalSocket socket;
            socket.connectToServer(instanceNamesFor(processTarget).command);
            return socket.waitForConnected(50);
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(), 15000);
        env.insert("TASKPIN_DATA", processTarget);
        const auto cli = [&](const QStringList &arguments) {
            QProcess process;
            process.setProcessEnvironment(env);
            process.start(executable, arguments);
            return process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(cli({})); // Resolved-path launch must find the same single-instance pipe.
        const QString dumpPath = root.path() + "/alias-process-dump.json";
        QVERIFY(cli({"--dump", dumpPath}));
        QFile dumpFile(dumpPath);
        QVERIFY(dumpFile.open(QIODevice::ReadOnly));
        const auto dump = QJsonDocument::fromJson(dumpFile.readAll()).object();
        QCOMPARE(dump["instance"].toObject()["pid"].toInteger(), owner.processId());
        QVERIFY(cli({"--quit"}));
        QVERIFY(owner.waitForFinished(10000));
#else
        QSKIP("Windows junction regression");
#endif
    }
    void pinPresetsAndReset()
    {
        Settings::instance().pinOpacity = 1.0;
        PinWindow pin(QImage(200, 100, QImage::Format_RGB32));
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        inspectMenu(pin, [](QMenu *menu) {
            auto *zoom = named(menu, QStringLiteral("缩放"));
            QVERIFY(zoom && zoom->menu());
            auto *half = named(zoom->menu(), QStringLiteral("50%"));
            QVERIFY(half && half->isCheckable());
            half->trigger();
        });
        QCOMPARE(pin.size(), QSize(112, 62));
        inspectMenu(pin, [](QMenu *menu) {
            auto *zoom = named(menu, QStringLiteral("缩放"));
            QVERIFY(zoom && zoom->menu());
            QVERIFY(named(zoom->menu(), QStringLiteral("50%"))->isChecked());
            auto *opacity = named(menu, QStringLiteral("不透明度"));
            QVERIFY(opacity && opacity->menu());
            auto *half = named(opacity->menu(), QStringLiteral("50%"));
            QVERIFY(half && half->isCheckable());
            QTest::ignoreMessage(QtWarningMsg, "This plugin does not support setting window opacity");
            half->trigger();
        });
        QCOMPARE(pin.pinOpacity(), 0.5);
        inspectMenu(pin, [](QMenu *menu) {
            auto *opacity = named(menu, QStringLiteral("不透明度"));
            QVERIFY(named(opacity->menu(), QStringLiteral("50%"))->isChecked());
            QVERIFY(!named(opacity->menu(), QStringLiteral("100%"))->isChecked());
            auto *reset = named(menu, QStringLiteral("重置显示"));
            QVERIFY(reset);
            QTest::ignoreMessage(QtWarningMsg, "This plugin does not support setting window opacity");
            reset->trigger();
        });
        QCOMPARE(pin.size(), QSize(212, 112));
        QCOMPARE(pin.pinOpacity(), 1.0);
        QTest::keyClick(&pin, Qt::Key_Minus);
        QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(pin.zoom(), 1.0);
    }
    void automationWindowsRemainSelectable()
    {
        Store store;
        QVERIFY(store.open(m_data.path() + "/window-types.db"));
        QPixmap frame(320, 240);
        frame.fill(Qt::white);
        ScreenCapture shot{frame, QRect(0, 0, 320, 240), 1.0, QStringLiteral("synthetic")};
        for (bool automation : {false, true}) {
            qputenv("TASKPIN_AUTOMATION", automation ? "1" : "0");
            PinWindow pin(QImage(100, 60, QImage::Format_RGB32));
            CardWindow card(&store);
            SnipOverlay overlay(shot);
            const auto pinType = pin.windowType();
            const auto cardType = card.windowType();
            const auto overlayType = overlay.windowType();
            qputenv("TASKPIN_AUTOMATION", "1"); // Restore before any assertion can return.
            const auto expected = automation ? Qt::Window : Qt::Tool;
            QCOMPARE(pinType, expected);
            QCOMPARE(cardType, expected);
            QCOMPARE(overlayType, expected);
        }
    }
    void pinWheelValuesVisible()
    {
        Settings::instance().pinOpacity = 1.0;
        PinWindow pin(QImage(200, 100, QImage::Format_RGB32));
        QWheelEvent zoom(QPointF(10, 10), pin.mapToGlobal(QPoint(10, 10)), {}, {0, 120},
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&pin, &zoom);
        QTRY_COMPARE(pin.zoom(), 1.1);
        QWheelEvent opacity(QPointF(10, 10), pin.mapToGlobal(QPoint(10, 10)), {}, {0, -120},
                            Qt::NoButton, Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&pin, &opacity);
        QCOMPARE(pin.pinOpacity(), 0.95);
        inspectMenu(pin, [](QMenu *menu) {
            auto *zoom = named(menu, QStringLiteral("缩放"));
            auto *opacity = named(menu, QStringLiteral("不透明度"));
            QVERIFY(zoom && zoom->menu());
            QVERIFY(opacity && opacity->menu());
            QVERIFY(named(zoom->menu(), QStringLiteral("当前：110%")));
            QVERIFY(named(opacity->menu(), QStringLiteral("当前：95%")));
            for (auto *submenu : {zoom->menu(), opacity->menu()})
                for (auto *action : submenu->actions())
                    QVERIFY(!action->isChecked());
            QVERIFY(named(menu, QStringLiteral("原始大小"))->text().contains(QStringLiteral("Ctrl+0")));
            QVERIFY(named(menu, QStringLiteral("重置显示"))->text().contains(QStringLiteral("Ctrl+Shift+0")));
        });
    }
    void pinActionsPreserved()
    {
        PinWindow pin(QImage(200, 100, QImage::Format_RGB32));
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        QSignalSpy recognized(&pin, &PinWindow::recognizeRequested);
        inspectMenu(pin, [](QMenu *menu) {
            QVERIFY(named(menu, QStringLiteral("识别为任务")));
            named(menu, QStringLiteral("识别为任务"))->trigger();
            QVERIFY(named(menu, QStringLiteral("旋转 / 翻转")));
            named(menu, QStringLiteral("顺时针 90°"))->trigger();
            QVERIFY(named(menu, QStringLiteral("以剪贴板内容替换")));
            QVERIFY(named(menu, QStringLiteral("关闭全部贴图")));
            named(menu, QStringLiteral("点击穿透"))->trigger();
        });
        QCOMPARE(recognized.count(), 1);
        QCOMPARE(pin.image().size(), QSize(100, 200));
        QVERIFY(pin.clickThrough());
        PinWindow::clearClickThrough();
        QVERIFY(!pin.clickThrough());
        QApplication::clipboard()->setImage(QImage(40, 30, QImage::Format_RGB32));
        inspectMenu(pin, [](QMenu *menu) { named(menu, QStringLiteral("以剪贴板内容替换"))->trigger(); });
        QCOMPARE(pin.image().size(), QSize(40, 30));
    }
    void oversizedPinPlacement()
    {
        PinWindow pin(QImage(6000, 4000, QImage::Format_RGB32));
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        const QRect area = pin.screen()->availableGeometry();
        QVERIFY(pin.x() >= area.left());
        QVERIFY(pin.y() >= area.top());
    }
    void capturePinGeometry_data()
    {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<QRect>("region");
        for (qreal dpr : {1.0, 1.25, 1.5, 2.0}) {
            for (QRect region : {QRect(120, 80, 400, 200), QRect(-1500, -700, 400, 200),
                                 QRect(0, 0, 400, 200), QRect(1520, 880, 400, 200)})
                QTest::newRow(qPrintable(QString("%1-%2-%3").arg(dpr).arg(region.x()).arg(region.y())))
                    << dpr << region;
        }
    }
    void capturePinGeometry()
    {
        QFETCH(qreal, dpr);
        QFETCH(QRect, region);
        QImage image(qRound(region.width() * dpr), qRound(region.height() * dpr), QImage::Format_RGB32);
        image.fill(Qt::green);
        PinWindow pin(image, QString(), nullptr, region);
        pin.setAttribute(Qt::WA_DeleteOnClose, false);
        QCOMPARE(pin.mapToGlobal(QPoint(6, 6)), region.topLeft());
        QCOMPARE(pin.size(), region.size() + QSize(12, 12));
        QCOMPARE(pin.zoom(), 1.0);
        QCOMPARE(pin.image().size(), image.size());
        QTest::keyClick(&pin, Qt::Key_Minus);
        QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
        QCOMPARE(pin.size(), region.size() + QSize(12, 12));
        QCOMPARE(pin.image(), image);
    }
    void automationHostSettingsAndDiagnostics()
    {
        // Compare the actual Run value without ever emitting its contents in test output.
        QSettings run(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
                      QSettings::NativeFormat);
        const QVariant before = run.value(QStringLiteral("TaskPinQt"));
        AppHost host;
        host.start();
        const bool previous = Settings::instance().autostart;
        for (bool enabled : {false, true}) {
            Settings::instance().autostart = enabled;
            QVERIFY(QMetaObject::invokeMethod(&host, "applySettings", Qt::DirectConnection));
            run.sync();
            QVERIFY(run.value(QStringLiteral("TaskPinQt")) == before);
            for (const auto &item : host.dumpState()["hotkeys"].toArray())
                QVERIFY(!item.toObject()["registered"].toBool());
        }
        Settings::instance().autostart = previous;
        QApplication::clipboard()->setImage(QImage(60, 40, QImage::Format_RGB32));
        host.pinFromClipboard();
        const auto dump = host.dumpState();
        QVERIFY(dump["instance"].toObject()["automation"].toBool());
        QCOMPARE(dump["instance"].toObject()["command"].toString(), instanceNamesFor(m_data.path()).command);
        QCOMPARE(dump["pins"].toObject()["items"].toArray().size(), 1);
        QVERIFY(dump["snip"].toObject().contains("actions"));
        PinWindow::closeAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        for (auto *window : QApplication::topLevelWidgets()) window->hide();
    }
    void automationDialogSavePreservesAutostart_data()
    {
        QTest::addColumn<bool>("enabled");
        QTest::newRow("disabled") << false;
        QTest::newRow("enabled") << true;
    }
    void automationDialogSavePreservesAutostart()
    {
#ifdef Q_OS_WIN
        QFETCH(bool, enabled);
        // Read only: retain exact presence, registry type, and raw bytes, without logging contents.
        const RunValue before = readRunValue();
        QVERIFY(before.status == ERROR_SUCCESS || before.status == ERROR_FILE_NOT_FOUND);
        const Settings previous = Settings::instance();
        const auto restoreSettings = qScopeGuard([previous] {
            Settings::instance() = previous;
            Settings::instance().save();
        });
        AppHost host;
        host.start();
        host.showSettings();
        SettingsDialog *dialog = nullptr;
        for (auto *window : QApplication::topLevelWidgets())
            if (auto *settings = qobject_cast<SettingsDialog *>(window)) dialog = settings;
        QVERIFY(dialog);
        const auto closeDialog = qScopeGuard([dialog] {
            dialog->close();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        });
        QCheckBox *autostart = nullptr;
        for (auto *box : dialog->findChildren<QCheckBox *>())
            if (box->text() == QStringLiteral("开机自动启动（当前用户运行项）")) autostart = box;
        QVERIFY(autostart);
        autostart->setChecked(enabled);
        QSpinBox *width = nullptr;
        for (auto *spin : dialog->findChildren<QSpinBox *>())
            if (spin->minimum() == 280 && spin->maximum() == 640) width = spin;
        QVERIFY(width);
        width->setValue(420);
        auto *buttons = dialog->findChild<QDialogButtonBox *>();
        QVERIFY(buttons);
        QPushButton *save = nullptr;
        for (auto *button : buttons->buttons())
            if (buttons->buttonRole(button) == QDialogButtonBox::AcceptRole)
                save = qobject_cast<QPushButton *>(button);
        QVERIFY(save);
        QSignalSpy saved(dialog, &SettingsDialog::saved);
        QSignalSpy accepted(dialog, &QDialog::accepted);
        save->click(); // Exercise the real Save connection, apply(), saved(), and host handler.
        QCOMPARE(saved.count(), 1);
        QCOMPARE(accepted.count(), 1);
        const RunValue after = readRunValue();
        QVERIFY(after.status == before.status);
        QVERIFY(after.type == before.type);
        QVERIFY(after.bytes == before.bytes);
        QCOMPARE(Settings::instance().autostart, enabled);
        QCOMPARE(Settings::instance().cardWidth, 420);
        QFile settingsFile(Settings::settingsPath());
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        const auto persisted = QJsonDocument::fromJson(settingsFile.readAll()).object();
        QCOMPARE(persisted["autostart"].toBool(), enabled);
        QCOMPARE(persisted["cardWidth"].toInt(), 420);
        for (const auto &item : host.dumpState()["hotkeys"].toArray())
            QVERIFY(!item.toObject()["registered"].toBool());
#else
        QSKIP("Windows Run registry regression");
#endif
    }
    void separateProcesses()
    {
        const QString executable = QCoreApplication::applicationDirPath() + "/TaskPinQt.exe";
        QProcess first, second;
        const auto launch = [&](QProcess &process, const QString &directory) {
            QDir().mkpath(directory);
            auto env = QProcessEnvironment::systemEnvironment();
            env.insert("TASKPIN_DATA", directory);
            env.insert("TASKPIN_AUTOMATION", "1");
            process.setProcessEnvironment(env);
            process.start(executable, QStringList{});
            return process.waitForStarted(10000);
        };
        // QProcess destructors stop only these children if any assertion fails.
        const QString a = m_data.path() + "/process-a";
        const QString b = m_data.path() + "/process-b";
        QVERIFY(launch(first, a));
        QVERIFY(launch(second, b));
        const auto ready = [](const QString &directory) {
            QLocalSocket socket;
            socket.connectToServer(instanceNamesFor(directory).command);
            return socket.waitForConnected(50);
        };
        QTRY_VERIFY_WITH_TIMEOUT(ready(a) && ready(b), 15000);
        const auto cli = [&](const QProcess &owner, const QStringList &args) {
            QProcess process;
            process.setProcessEnvironment(owner.processEnvironment());
            process.start(executable, args);
            return process.waitForFinished(10000) && process.exitCode() == 0;
        };
        QVERIFY(cli(first, {})); // The same directory's single-instance probe exits promptly.
        QVERIFY(first.state() == QProcess::Running);
        QVERIFY(second.state() == QProcess::Running);
        for (auto *process : {&first, &second}) {
            const QString directory = process->processEnvironment().value("TASKPIN_DATA");
            const QString path = directory + "/dump.json";
            QVERIFY(cli(*process, {"--dump", path}));
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const auto dump = QJsonDocument::fromJson(file.readAll()).object();
            QCOMPARE(dump["instance"].toObject()["pid"].toInteger(), process->processId());
            QCOMPARE(dump["dataDir"].toString(), directory);
            for (const auto &hotkey : dump["hotkeys"].toArray())
                QVERIFY(!hotkey.toObject()["registered"].toBool());
        }
        QVERIFY(first.processId() != second.processId());
        QVERIFY(cli(first, {"--quit"}));
        QVERIFY(first.waitForFinished(10000));
        QVERIFY(second.state() == QProcess::Running);
        QVERIFY(cli(second, {"--quit"}));
        QVERIFY(second.waitForFinished(10000));
    }
private:
    QTemporaryDir m_data;
};
QTEST_MAIN(IntegrationTests)
#include "tst_integration.moc"
