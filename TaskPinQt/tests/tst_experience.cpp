#include "app/AppHost.h"
#include "core/AiClient.h"
#include "core/Ocr.h"
#include "core/Settings.h"
#include "core/Store.h"
#include "ui/CardWindow.h"
#include "ui/CalendarWindow.h"
#include "ui/PinWindow.h"
#include "ui/SettingsDialog.h"
#include "ui/SnipOverlay.h"
#include <QColorDialog>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QJsonDocument>
#include <QListWidget>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSlider>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QWheelEvent>
#include <QtTest>
#include <QFontDatabase>
#include <QPainter>
#include <algorithm>

namespace {
void wheel(QWidget &w, int angle, QPoint pixels = {})
{
    QWheelEvent e(QPointF(60, 50), w.mapToGlobal(QPoint(60, 50)), pixels, {0, angle},
                  Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(&w, &e);
}
QAction *action(QMenu *menu, const QString &name)
{
    for (auto *a : menu->actions()) {
        if (a->text().section('\t', 0, 0) == name) return a;
        if (a->menu()) if (auto *found = action(a->menu(), name)) return found;
    }
    return nullptr;
}
void menuAction(QWidget &widget, const QString &name, bool *found)
{
    *found = false;
    QTimer::singleShot(0, &widget, [&widget, name, found] {
        auto *menu = widget.findChild<QMenu *>();
        if (menu) {
            if (auto *a = action(menu, name)) { *found = true; a->trigger(); }
            menu->close();
        }
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, {15, 15}, widget.mapToGlobal(QPoint(15, 15)));
    QApplication::sendEvent(&widget, &event);
    QCoreApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}
QPushButton *button(QWidget *widget, const QString &label)
{
    for (auto *b : widget->findChildren<QPushButton *>()) if (b->text() == label) return b;
    return nullptr;
}

// Only the remote HTTP boundary is controlled. The real AiClient and host handle the response.
void replyAfter(QTcpServer &server, int delay, QByteArray body, int code = 200)
{
    QObject::connect(&server, &QTcpServer::newConnection, &server, [&server, delay, body, code] {
        while (auto *socket = server.nextPendingConnection()) {
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, delay, body, code] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("scheduled").toBool()) return;
                socket->setProperty("scheduled", true);
                QTimer::singleShot(delay, socket, [socket, body, code] {
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + " Test\r\nContent-Type: application/json\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        }
    });
}
}

class ExperienceTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
private slots:
    void initTestCase()
    {
        QVERIFY(data.isValid());
        qputenv("TASKPIN_AUTOMATION", "1");
        qputenv("TASKPIN_DATA", data.path().toUtf8());
        const int font = QFontDatabase::addApplicationFont(qEnvironmentVariable("TASKPIN_TEST_FONT", "C:/Windows/Fonts/msyh.ttc"));
        if (font >= 0) QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(font).first()));
    }
    void init()
    {
        Settings::instance().resetToDefaults();
        qputenv("TASKPIN_DATA", (data.path() + "/" + QTest::currentTestFunction() + "-" + QTest::currentDataTag()).toUtf8());
        QDir().mkpath(Settings::dataDir());
    }
    void cleanup()
    {
        // AppHost's production windows are top-level widgets, not children of its QObject.
        QList<QPointer<QWidget>> windows;
        for (auto *w : QApplication::topLevelWidgets()) windows.append(w);
        for (auto w : windows) if (w) delete w;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QSqlDatabase::removeDatabase("taskpin");
    }
    void wheelHonorsSmallDeltasAndReverses()
    {
        PinWindow pin(QImage(200, 100, QImage::Format_RGB32));
        for (int i = 0; i < 4; ++i) wheel(pin, 30);
        QTRY_VERIFY(qAbs(pin.zoom() - 1.1) < .0001);
        for (int i = 0; i < 4; ++i) wheel(pin, -30);
        QTRY_VERIFY(qAbs(pin.zoom() - 1.0) < .0001);
        wheel(pin, 0);
        QTest::qWait(150);
        QCOMPARE(pin.zoom(), 1.0);
        wheel(pin, 0, {0, 15});
        QTRY_VERIFY(pin.zoom() > 1.0 && pin.zoom() < 1.1);
    }
    void plainPinHasExactContentAndNoHalo()
    {
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(QColor("#2070b0"));
        PinWindow pin(image, {}, nullptr, QRect(-180, 100, 200, 100));
        const QPoint origin = pin.contentGeometry().topLeft();
        bool found;
        menuAction(pin, QStringLiteral("无边框与阴影"), &found);
        QVERIFY(found);
        QCOMPARE(pin.contentGeometry().topLeft(), origin);
        QCOMPARE(pin.size(), QSize(200, 100));
        QCOMPARE(pin.grab().toImage().pixelColor(0, 0), QColor("#2070b0"));
        QCOMPARE(pin.grab().toImage().pixelColor(199, 99), QColor("#2070b0"));
    }
    void magnifierFollowsCursorBeforeFirstMove()
    {
        const QPoint previous = QCursor::pos();
        const auto restore = qScopeGuard([previous] { QCursor::setPos(previous); });
        QCursor::setPos(240, 180);
        QPixmap pixels(800, 600); pixels.fill(Qt::white);
        SnipOverlay overlay({pixels, QRect(0, 0, 800, 600), 1, "synthetic"});
        const QImage frame = overlay.grab().toImage();
        // Interior of the magnifier must be undimmed white on the very first paint.
        QCOMPARE(frame.pixelColor(274, 214), QColor(Qt::white));
        QVERIFY(frame.pixelColor(34, 34) != QColor(Qt::white));
    }
    void textOpensFullTaskWithoutCompleting()
    {
        Store store; QVERIFY(store.open(data.path() + "/long.db"));
        const QString title = QStringLiteral("需要核对的长任务内容，含完整交付要求。").repeated(30);
        const QString id = store.addTask(title);
        CardWindow card(&store); card.grab();
        QTest::mouseClick(&card, Qt::LeftButton, {}, {120, 69});
        QCOMPARE(store.openCount(), 1);
        auto *detail = card.findChild<QPlainTextEdit *>();
        QVERIFY(detail);
        QVERIFY(detail->toPlainText().contains(title));
        QTest::mouseDClick(&card, Qt::LeftButton, {}, {120, 69});
        QCOMPARE(store.openCount(), 1);
        QVERIFY(!id.isEmpty());
    }
    void completedListRestoresTaskAndDailyCounts()
    {
        Store store; QVERIFY(store.open(data.path() + "/restore.db"));
        const QString id = store.addTask(QStringLiteral("误完成后应恢复"));
        store.setDone(id, true);
        CardWindow card(&store);
        bool found;
        menuAction(card, QStringLiteral("已完成…"), &found);
        QVERIFY(found);
        auto *list = card.findChild<QListWidget *>();
        QVERIFY(list); QCOMPARE(list->count(), 1);
        list->setCurrentRow(0);
        auto *restore = button(list->window(), QStringLiteral("恢复待办"));
        QVERIFY(restore); restore->click();
        QCOMPARE(store.openCount(), 1);
        QCOMPARE(store.openTasks().first().id, id);
        QVERIFY(store.openTasks().first().completed.isEmpty());
        const auto stats = store.dayStats(QDate::currentDate(), QDate::currentDate()).first();
        QCOMPARE(stats.done, 0); QCOMPARE(stats.pending, 1);
    }
    void doubleClickCompletionDoesNotCompleteNextTask()
    {
        Store store; QVERIFY(store.open(data.path() + "/double.db"));
        store.addTask("first"); store.addTask("second");
        CardWindow card(&store); card.grab();
        QTest::mouseClick(&card, Qt::LeftButton, {}, {29, 69});
        QCOMPARE(store.openCount(), 1);
        QTest::mouseDClick(&card, Qt::LeftButton, {}, {29, 69});
        QCOMPARE(store.openCount(), 1);
    }
    void liveColorPreviewCancelAndSave()
    {
        AppHost host; host.start();
        CardWindow *card = nullptr;
        for (auto *w : QApplication::topLevelWidgets()) if (auto *c = qobject_cast<CardWindow *>(w)) card = c;
        QVERIFY(card);
        const QColor old = Settings::instance().cardBg;
        const QImage baseline = card->grab().toImage();
        host.showSettings();
        SettingsDialog *dialog = nullptr;
        for (auto *w : QApplication::topLevelWidgets()) if (auto *d = qobject_cast<SettingsDialog *>(w)) dialog = d;
        QVERIFY(dialog);
        QPushButton *bg = nullptr;
        for (auto *b : dialog->findChildren<QPushButton *>()) if (b->text() == old.name().toUpper()) bg = b;
        QVERIFY(bg);
        bool pickerFound = false, changed = false;
        QTimer::singleShot(0, dialog, [&] {
            auto *picker = dialog->findChild<QColorDialog *>();
            if (!picker) return;
            pickerFound = true;
            picker->setCurrentColor(QColor("#bd452d"));
            changed = card->grab().toImage().pixelColor(30, 100) != baseline.pixelColor(30, 100);
            picker->accept();
        });
        bg->click();
        QVERIFY(pickerFound); QVERIFY(changed);
        QCOMPARE(Settings::instance().cardBg, old); // preview must never write live settings
        dialog->reject();
        QCOMPARE(card->grab().toImage().pixelColor(30, 100), baseline.pixelColor(30, 100));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        host.showSettings();
        for (auto *w : QApplication::topLevelWidgets()) if (auto *d = qobject_cast<SettingsDialog *>(w)) dialog = d;
        bg = nullptr;
        for (auto *b : dialog->findChildren<QPushButton *>()) if (b->text() == old.name().toUpper()) bg = b;
        QVERIFY(bg);
        QTimer::singleShot(0, dialog, [&] {
            auto *picker = dialog->findChild<QColorDialog *>();
            QVERIFY(picker); picker->setCurrentColor(QColor("#457850")); picker->reject();
        });
        bg->click();
        QCOMPARE(card->grab().toImage().pixelColor(30, 100), baseline.pixelColor(30, 100));
        QTimer::singleShot(0, dialog, [&] {
            auto *picker = dialog->findChild<QColorDialog *>();
            QVERIFY(picker); picker->setCurrentColor(QColor("#bd452d")); picker->accept();
        });
        bg->click();
        Settings::instance().cardX = 75; Settings::instance().cardY = 130;
        auto *save = button(dialog, QStringLiteral("保存")); QVERIFY(save); save->click();
        QCOMPARE(Settings::instance().cardBg, QColor("#bd452d"));
        QCOMPARE(Settings::instance().cardX, 75); QCOMPARE(Settings::instance().cardY, 130);
        Settings persisted; persisted.load();
        QCOMPARE(persisted.cardBg, QColor("#bd452d"));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(card->grab().toImage().pixelColor(30, 100) != baseline.pixelColor(30, 100));
    }
    void quickUndoRestoresTheCompletedTask()
    {
        Store store; QVERIFY(store.open(data.path() + "/undo.db"));
        const QString id = store.addTask("undo this task"); store.addTask("keep the next task");
        CardWindow card(&store); card.grab();
        QTest::mouseClick(&card, Qt::LeftButton, {}, {29, 69});
        QCOMPARE(store.openCount(), 1);
        QTest::keyClick(&card, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(store.openCount(), 2);
        QVERIFY(!store.task(id).isDone()); QVERIFY(store.task(id).completed.isEmpty());
    }
    void largePinContinuousZoomDrawCost()
    {
        QImage image(3840, 2160, QImage::Format_RGB32);
        QPainter painter(&image);
        for (int y = 0; y < 2160; y += 4) painter.fillRect(0, y, 3840, 4, QColor::fromHsv(y % 360, 180, 210));
        painter.end();
        PinWindow pin(image);
        QImage canvas(1280, 720, QImage::Format_ARGB32_Premultiplied);
        QList<qint64> timings;
        for (int i = 0; i < 24; ++i) {
            wheel(pin, 15);
            QTest::qWait(16);
            QElapsedTimer elapsed; elapsed.start();
            pin.render(&canvas, {}, QRegion(QRect(0, 0, 1280, 720)));
            timings.append(elapsed.nsecsElapsed());
        }
        QTRY_VERIFY(qAbs(pin.zoom() - 1.331) < .0001);
        std::sort(timings.begin(), timings.end());
        qInfo("4K pin clipped draw ms: median=%.3f p95=%.3f max=%.3f samples=%d",
              timings[12] / 1e6, timings[22] / 1e6, timings.last() / 1e6, int(timings.size()));
        QCOMPARE(pin.image(), image);
    }
    void slowAiKeepsOneRecoverableTaskAndReplacesTitle()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        replyAfter(server, 300, R"({"choices":[{"message":{"content":"condensed title"}}]})");
        auto &s = Settings::instance();
        s.aiEnabled = true; s.aiApiKey = "fixture-key";
        s.aiBaseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        AppHost host; host.start();
        auto *ocr = host.findChild<Ocr *>(); auto *store = host.findChild<Store *>();
        QVERIFY(ocr && store);
        const QString original = "A very long task with the entire original OCR text kept intact";
        QVERIFY(QMetaObject::invokeMethod(ocr, "finished", Qt::DirectConnection, Q_ARG(QString, original)));
        QCOMPARE(store->openCount(), 1);
        const auto before = store->openTasks().first();
        QCOMPARE(before.aiState, QString("pending"));
        QCOMPARE(before.ocrText, original);
        QTRY_COMPARE(store->openTasks().first().title, QString("condensed title"));
        QCOMPARE(store->openTasks().first().id, before.id);
        QCOMPARE(store->openTasks().first().sortKey, before.sortKey);
        QCOMPARE(store->openTasks().first().ocrText, original);
        QCOMPARE(store->openCount(), 1);
    }
    void aiFailureKeepsLocalTask_data()
    {
        QTest::addColumn<QByteArray>("body"); QTest::addColumn<int>("code");
        QTest::newRow("http-error") << QByteArray("{}") << 500;
        QTest::newRow("malformed") << QByteArray("not-json") << 200;
        QTest::newRow("empty") << QByteArray(R"({"choices":[{"message":{"content":""}}]})") << 200;
    }
    void aiFailureKeepsLocalTask()
    {
        QFETCH(QByteArray, body); QFETCH(int, code);
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost)); replyAfter(server, 70, body, code);
        auto &s = Settings::instance(); s.aiEnabled = true; s.aiApiKey = "fixture-key";
        s.aiBaseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        AppHost host; host.start(); auto *store = host.findChild<Store *>(); auto *ocr = host.findChild<Ocr *>();
        const QString original = "local fallback title";
        QVERIFY(QMetaObject::invokeMethod(ocr, "finished", Qt::DirectConnection, Q_ARG(QString, original)));
        QCOMPARE(store->openCount(), 1);
        QTRY_COMPARE(store->openTasks().first().aiState, QString("failed"));
        QCOMPARE(store->openTasks().first().title, original);
        QCOMPARE(store->openTasks().first().ocrText, original);
        QCOMPARE(store->openCount(), 1);
    }
    void aiTimeoutEmitsOneFailure()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        replyAfter(server, 400, R"({"choices":[{"message":{"content":"late"}}]})");
        auto &s = Settings::instance(); s.aiEnabled = true; s.aiApiKey = "fixture-key";
        s.aiBaseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        AiClient client; QSignalSpy failed(&client, &AiClient::failed), finished(&client, &AiClient::finished);
        client.summarize("test timeout");
        auto *timer = client.findChild<QTimer *>(); QVERIFY(timer); timer->start(50);
        QTRY_COMPARE(failed.count(), 1);
        QTest::qWait(500);
        QCOMPARE(failed.count(), 1); QCOMPARE(finished.count(), 0); QVERIFY(!client.busy());
    }
    void captureWhileAiWaitsIsQueued()
    {
        QTcpServer server; QVERIFY(server.listen(QHostAddress::LocalHost));
        replyAfter(server, 500, R"({"choices":[{"message":{"content":"first request"}}]})");
        auto &s = Settings::instance(); s.aiEnabled = true; s.aiApiKey = "fixture-key";
        s.aiBaseUrl = QString("http://127.0.0.1:%1/v1").arg(server.serverPort());
        AppHost host; host.start(); auto *ocr = host.findChild<Ocr *>(); auto *store = host.findChild<Store *>();
        const QString original = "first original OCR text";
        QVERIFY(QMetaObject::invokeMethod(ocr, "finished", Qt::DirectConnection, Q_ARG(QString, original)));
        QImage image(60, 40, QImage::Format_RGB32); image.fill(Qt::white);
        QApplication::clipboard()->setImage(image); host.pinFromClipboard();
        auto *pin = PinWindow::all().last();
        QVERIFY(QMetaObject::invokeMethod(pin, "recognizeRequested", Qt::DirectConnection, Q_ARG(QImage, image)));
        QVERIFY(!ocr->busy());
        QCOMPARE(host.dumpState()["ocr"].toObject()["queued"].toInt(), 1);
        QCOMPARE(store->openTasks().first().ocrText, original);
        QTRY_COMPARE(store->openTasks().first().title, QString("first request"));
        QCOMPARE(store->openTasks().first().ocrText, original);
        QTRY_VERIFY(ocr->busy()); // The queued OCR starts only after the first AI request has finished.
        PinWindow::closeAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
    void restartRecoversPendingLocalTitle()
    {
        const QString file = data.path() + "/recover.db";
        QString id;
        {
            Store store; QVERIFY(store.open(file)); id = store.addTask("survives restart", "ocr");
            QSqlQuery query(QSqlDatabase::database("taskpin"));
            QVERIFY(query.exec("UPDATE task SET ai_state='pending'"));
        }
        QSqlDatabase::removeDatabase("taskpin");
        Store store; QVERIFY(store.open(file));
        QCOMPARE(store.openTasks().first().id, id);
        QCOMPARE(store.openTasks().first().title, QString("survives restart"));
        QCOMPARE(store.openTasks().first().aiState, QString("failed"));
    }
    void ocrTimeoutEmitsOnlyOneFailure()
    {
        Ocr ocr;
        QSignalSpy failed(&ocr, &Ocr::failed);
        QImage image(100, 60, QImage::Format_RGB32); image.fill(Qt::white);
        ocr.recognize(image);
        auto *timer = ocr.findChild<QTimer *>(); QVERIFY(timer); timer->start(100);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(!ocr.busy());
        QTest::qWait(100);
        QCOMPARE(failed.count(), 1);
    }
    void calendarTaskOpensDetailsAndCanRestore()
    {
        Store store; QVERIFY(store.open(data.path() + "/calendar.db"));
        const QString id = store.addTask(QStringLiteral("日历任务详情").repeated(30));
        store.setDone(id, true);
        CalendarWindow calendar(&store); calendar.grab();
        QTest::mouseClick(&calendar, Qt::LeftButton, {}, {640, 151});
        auto *text = calendar.findChild<QPlainTextEdit *>(); QVERIFY(text);
        auto *restore = button(text->window(), QStringLiteral("恢复待办"));
        QVERIFY(restore); restore->click();
        QCOMPARE(store.openCount(), 1);
        QCOMPARE(store.openTasks().first().id, id);
    }
};
QTEST_MAIN(ExperienceTests)
#include "tst_experience.moc"
