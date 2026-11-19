#include "ui/PinWindow.h"
#include "core/Settings.h"
#include "core/InstanceNames.h"
#include "app/AppHost.h"
#include "core/Store.h"
#include "ui/CardWindow.h"
#include "ui/SnipOverlay.h"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QAction>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScreen>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <functional>

namespace {
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
