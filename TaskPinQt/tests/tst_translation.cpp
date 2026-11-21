#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include "platform/TranslationCredentials.h"
#include "core/Settings.h"
#include "core/Store.h"
#include "app/AppHost.h"
#include "ui/TranslationOverlay.h"
#include "ui/TranslationWindow.h"
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

using namespace Translation;
class FixtureServer : public QTcpServer {
public:
    int requests=0, delay=0, httpStatus=200;
    QString behavior="ok";
    QList<QJsonObject> payloads;
    FixtureServer() {
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this] {
            while (auto *socket=nextPendingConnection()) {
                connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
                connect(socket,&QTcpSocket::readyRead,socket,[this,socket] {
                    auto data=socket->property("data").toByteArray()+socket->readAll(); socket->setProperty("data",data);
                    const int separator=data.indexOf("\r\n\r\n");
                    if (separator<0 || socket->property("handled").toBool())return;
                    const auto header=data.left(separator).split('\n'); int length=0;
                    for (const auto &line:header) if (line.toLower().startsWith("content-length:")) length=line.mid(15).trimmed().toInt();
                    if (data.size()<separator+4+length)return;
                    socket->setProperty("handled",true);++requests;
                    auto payload=QJsonDocument::fromJson(data.mid(separator+4,length)).object();payloads.append(payload);
                    auto messages=payload["messages"].toArray();
                    auto blocks=QJsonDocument::fromJson(messages.last().toObject()["content"].toString().toUtf8()).object()["blocks"].toArray();
                    QJsonArray output;
                    for (auto item:blocks) {
                        auto b=item.toObject(); QString text=b["text"].toString();
                        if (behavior=="token")text.replace("__KEEP_","__BROKEN_");
                        output.append(QJsonObject{{"block_id",b["block_id"]},{"translated_text",QStringLiteral("译文 ")+text},{"status","ok"}});
                    }
                    if (behavior=="missing" && !output.isEmpty())output.removeLast();
                    if (behavior=="duplicate" && !output.isEmpty())output.append(output.first());
                    const QString content=behavior=="badjson"?"not json":QString::fromUtf8(QJsonDocument(QJsonObject{{"blocks",output}}).toJson(QJsonDocument::Compact));
                    auto body=QJsonDocument(QJsonObject{{"choices",QJsonArray{QJsonObject{{"finish_reason",behavior=="truncated"?"length":"stop"},
                        {"message",QJsonObject{{"content",content}}}}}}}).toJson(QJsonDocument::Compact);
                    QTimer::singleShot(delay,socket,[socket,body,code=httpStatus] {
                        if (socket->state()!=QAbstractSocket::ConnectedState)return;
                        socket->write("HTTP/1.1 "+QByteArray::number(code)+" Fixture\r\nContent-Type: application/json\r\nContent-Length: "+QByteArray::number(body.size())+"\r\nConnection: close\r\n\r\n"+body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });
    }
    Config config() const { Config c;c.mode="cloud";c.endpoint=QString("http://127.0.0.1:%1/v1").arg(serverPort());c.consentEndpoint=c.requestUrl().toString(QUrl::FullyEncoded);return c; }
};
static Block block(QString text="The operator uses Threshold and watersheds_threshold at 12 mm.",QString id="block-1") {
    Block b;b.id=id;b.kind="paragraph";b.source=text;return protect(b,"test");
}
class TranslationTests : public QObject {
    Q_OBJECT
    QTemporaryDir data;
private slots:
    void initTestCase() {
        QVERIFY(data.isValid()); qputenv("TASKPIN_DATA",data.path().toUtf8());qputenv("TASKPIN_AUTOMATION","1");
        qRegisterMetaType<QList<Translation::Result>>();qRegisterMetaType<Translation::OcrResult>();
        Settings::instance().resetToDefaults();
        QVERIFY(QFontDatabase::addApplicationFont(qEnvironmentVariable("TASKPIN_TEST_FONT","C:/Windows/Fonts/msyh.ttc"))>=0);
    }
    void independentDefaultHotkey() {
        QCOMPARE(Settings::defaultChord("snip"),QString("F1"));QCOMPARE(Settings::defaultChord("translate"),QString("Ctrl+Alt+T"));
        QVERIFY(Settings::fallbackChord("translate").isEmpty());
        auto cfg=Config::load();QCOMPARE(cfg.mode,QString("local"));QVERIFY(!cfg.authorized());
    }
    void protectionPreservesTechnicalText() {
        const QString text="watersheds_threshold Image Basins Threshold Row Column Radius HObject HTuple https://example.com/help C:\\demo\\x 24.05 10 mm";
        auto b=block(text);QVERIFY(b.tokens.size()>=13);QString restored;
        QVERIFY(restore(b,QStringLiteral("译文 ")+b.protectedText,&restored));QCOMPARE(restored,QStringLiteral("译文 ")+text);
        QVERIFY(!restore(b,b.protectedText+b.tokens.firstKey(),&restored));
        auto damaged=b.protectedText;damaged.replace(b.tokens.firstKey(),"oops");QVERIFY(!restore(b,damaged,&restored));
        auto prose=block("An image shows a contour and a watershed.");QVERIFY(prose.tokens.isEmpty());
    }
    void placeholderCollisionAndFormula() {
        auto b=block("__KEEP_test_0__ Threshold and max(W-B1, W-B2) < Threshold");
        QVERIFY(!b.tokens.contains("__KEEP_test_0__"));QString result;QVERIFY(restore(b,b.protectedText,&result));QCOMPARE(result,b.source);
        QVERIFY(result.contains("max(W-B1, W-B2) < Threshold"));
    }
    void scientificNumbersKeepSignsAndExponent() {
        auto b=block("The default is -1.5e-3; limits are +2E+6 and -.25 mm, version 24.05.1.");
        const auto values=b.tokens.values();
        QVERIFY(values.contains("-1.5e-3"));QVERIFY(values.contains("+2E+6"));
        QVERIFY(values.contains("-.25 mm"));QVERIFY(values.contains("24.05.1"));
        QString restored;QVERIFY(restore(b,b.protectedText,&restored));QCOMPARE(restored,b.source);
        auto damaged=b.protectedText;damaged.replace(b.tokens.key("-1.5e-3"),"-1.5e3");
        QVERIFY(!restore(b,damaged,&restored));
    }
    void quotedEnumLiteralsRemainExact() {
        auto b=block("Use 'geotukey' or \"nearest_neighbor\"; translate 'a natural sentence' normally.");
        QVERIFY(b.tokens.values().contains("'geotukey'"));
        QVERIFY(b.tokens.values().contains("\"nearest_neighbor\""));
        QVERIFY(b.protectedText.contains("'a natural sentence'"));
        QString restored;QVERIFY(restore(b,b.protectedText,&restored));QCOMPARE(restored,b.source);
    }
    void paragraphsAndCode() {
        OcrResult o;o.imageSize={900,500};o.lines={
            {"l2","separated from each other by a watershed.",{20,45,600,20},{}},
            {"l1","The operator segments regions that are",{20,20,600,20},{}},
            {"l3","inner_circle (Region, Row, Column, Radius)",{20,100,600,20},{}},
            {"l4","max(W-B1, W-B2) < Threshold",{20,140,500,20},{}}};
        QStringList warnings;auto blocks=buildBlocks(o,&warnings);QCOMPARE(blocks.size(),3);QCOMPARE(blocks.first().lineIds,QStringList({"l1","l2"}));
        QVERIFY(blocks[1].bypass());QVERIFY(blocks[2].bypass());QVERIFY(!warnings.isEmpty());
    }
    void uncertainLayoutKeepsAllLines() {
        OcrResult o;o.lines={{"a","Left column",{0,0,100,20},.3},{"b","Right column",{200,0,100,20},std::nullopt}};
        QStringList warning;auto blocks=buildBlocks(o,&warning);QCOMPARE(blocks.size(),2);QCOMPARE(warning.size(),3);
    }
    void strictResponseValidation() {
        auto a=block("A sentence.","a"),b=block("Threshold controls merging.","b");
        auto row=[](const Block &x){return QJsonObject{{"block_id",x.id},{"translated_text",x.protectedText},{"status","ok"}};};
        QList<Result> results;QStringList invalid;QString error;
        QVERIFY(validate({a,b},{row(b),row(a)},&results,&invalid,&error));QCOMPARE(results.size(),2);QVERIFY(invalid.isEmpty());
        results.clear();QVERIFY(!validate({a},{row(a),row(a)},&results,&invalid,&error));
        results.clear();invalid.clear();QVERIFY(validate({a,b},{row(a)},&results,&invalid,&error));QCOMPARE(invalid,QStringList{"b"});
    }
    void cropCoordinates_data() {
        QTest::addColumn<double>("scale");QTest::addColumn<QPoint>("from");QTest::addColumn<QPoint>("to");
        for (double scale:{1.,1.25,1.5,2.}) for(int direction=0;direction<4;++direction)
            QTest::newRow(qPrintable(QString::number(scale)+"-"+QString::number(direction)))<<scale<<QPoint(direction&1?120:20,direction&2?80:10)<<QPoint(direction&1?20:120,direction&2?10:80);
    }
    void cropCoordinates() {
        QFETCH(double,scale);QFETCH(QPoint,from);QFETCH(QPoint,to);
        QCOMPARE(TranslationOverlay::pixelRect(QRectF(from,to),{200,100},{qRound(200*scale),qRound(100*scale)}),QRect(qFloor(20*scale),qFloor(10*scale),qCeil(120*scale)-qFloor(20*scale),qCeil(80*scale)-qFloor(10*scale)));
        QVERIFY(TranslationOverlay::pixelRect({0,0,1,1},{200,100},{400,200}).isEmpty());
    }
    void selectionNoClipboardOrHistory() {
        QImage image(400,200,QImage::Format_RGB32);image.fill(Qt::green);
        ScreenCapture shot{QPixmap::fromImage(image),QRect(-200,50,200,100),2,"fixture"};
        TranslationOverlay w(shot);w.setAttribute(Qt::WA_DeleteOnClose,false);w.show();
        QApplication::clipboard()->setText("unchanged");QSignalSpy selected(&w,&TranslationOverlay::selected);
        QTest::mousePress(&w,Qt::LeftButton,Qt::NoModifier,{120,80});QTest::mouseRelease(&w,Qt::LeftButton,Qt::NoModifier,{20,10});
        QCOMPARE(selected.size(),1);QCOMPARE(qvariant_cast<QImage>(selected[0][0]).size(),QSize(200,140));
        QCOMPARE(selected[0][1].toRect(),QRect(-180,60,100,70));QCOMPARE(QApplication::clipboard()->text(),QString("unchanged"));
        QVERIFY(!QFile::exists(data.path()+"/history/snip"));
    }
    void selectionEscape() {
        QPixmap image(200,100);image.fill(Qt::white);TranslationOverlay w({image,{0,0,200,100},1,"fixture"});
        w.setAttribute(Qt::WA_DeleteOnClose,false);QSignalSpy cancelled(&w,&TranslationOverlay::cancelled);w.show();QTest::keyClick(&w,Qt::Key_Escape);QCOMPARE(cancelled.size(),1);
    }
    void ocrContracts() {
        OcrResult r;QString error;
        QVERIFY(Translation::Ocr::decode(R"({"ok":true,"language":"en-US","lines":[{"text":"Threshold < 10","x":2,"y":3,"width":100,"height":20}]})",{200,100},&r,&error));
        QCOMPARE(r.lines.size(),1);QVERIFY(!r.lines.first().recognitionConfidence);QCOMPARE(r.lines.first().rect,QRectF(2,3,100,20));
        OcrResult invalid;QVERIFY(!Translation::Ocr::decode(R"({"ok":true,"lines":[{"text":"x","x":-1,"y":0,"width":10,"height":10}]})",{200,100},&invalid,&error));
    }
    void localAndUnconsentedNeverRequest() {
        FixtureServer server;Client client;client.allowLoopbackForTests(true);QSignalSpy failed(&client,&Client::failed);
        auto cfg=server.config();cfg.consentEndpoint.clear();client.start("a",{block()},cfg,"fixture-only");QCOMPARE(failed.size(),1);QCOMPARE(server.requests,0);
        cfg.mode="local";client.start("b",{block()},cfg,{});QCOMPARE(failed.size(),2);QCOMPARE(server.requests,0);
    }
    void mockOfflineDoesNotPretendTranslation() {
        Client client;Config cfg;cfg.mode="mock";QSignalSpy finished(&client,&Client::finished);
        client.start("mock",{block()},cfg,{});QTRY_COMPARE(finished.size(),1);
        auto results=qvariant_cast<QList<Result>>(finished[0][1]);QCOMPARE(results.size(),1);QCOMPARE(results[0].status,QString("mock"));QVERIFY(results[0].text.contains(QStringLiteral("非真实译文")));
    }
    void realHttpBoundaryAndCache() {
        FixtureServer server;Client client;client.allowLoopbackForTests(true);QSignalSpy finished(&client,&Client::finished);
        client.start("a",{block()},server.config(),"fixture-only");QTRY_COMPARE(finished.size(),1);QCOMPARE(server.requests,1);
        auto results=qvariant_cast<QList<Result>>(finished[0][1]);QVERIFY(results[0].text.contains("Threshold"));QVERIFY(!results[0].text.contains("__KEEP_"));
        QVERIFY(!QJsonDocument(server.payloads.first()).toJson().contains("image_url"));
        client.start("b",{block()},server.config(),"fixture-only");QTRY_COMPARE(finished.size(),2);QCOMPARE(server.requests,1);
    }
    void responseFailures_data() {
        QTest::addColumn<QString>("behavior");QTest::addColumn<int>("code");QTest::addColumn<bool>("partial");
        QTest::newRow("missing")<<QString("missing")<<200<<true;QTest::newRow("placeholder")<<QString("token")<<200<<true;
        QTest::newRow("duplicate")<<QString("duplicate")<<200<<false;QTest::newRow("json")<<QString("badjson")<<200<<false;
        QTest::newRow("truncated")<<QString("truncated")<<200<<false;QTest::newRow("unauthorized")<<QString("ok")<<401<<false;QTest::newRow("rate-limit")<<QString("ok")<<429<<false;
    }
    void responseFailures() {
        QFETCH(QString,behavior);QFETCH(int,code);QFETCH(bool,partial);FixtureServer server;server.behavior=behavior;server.httpStatus=code;
        Client client;client.allowLoopbackForTests(true);QSignalSpy finished(&client,&Client::finished),failed(&client,&Client::failed);
        client.start("x",{block()},server.config(),"fixture-only");QTRY_VERIFY(!finished.isEmpty()||!failed.isEmpty());
        if(partial){QCOMPARE(finished.size(),1);QCOMPARE(server.requests,2);auto r=qvariant_cast<QList<Result>>(finished[0][1]);QCOMPARE(r[0].status,QString("failed"));QCOMPARE(r[0].text,block().source);}
        else{QCOMPARE(failed.size(),1);QCOMPARE(server.requests,1);}
    }
    void cancellationAndLateReply() {
        FixtureServer server;server.delay=180;Client client;client.allowLoopbackForTests(true);QSignalSpy finished(&client,&Client::finished),failed(&client,&Client::failed);
        client.start("old",{block()},server.config(),"fixture-only");QTRY_COMPARE(server.requests,1);client.cancel();
        server.delay=0;client.start("new",{block("New text.")},server.config(),"fixture-only");QTRY_COMPARE(finished.size(),1);QCOMPARE(finished[0][0].toString(),QString("new"));
        QTest::qWait(230);QCOMPARE(finished.size(),1);QCOMPARE(failed.size(),0);
    }
    void timeoutOnlyFailsOnce() {
        FixtureServer server;server.delay=300;Client client;client.allowLoopbackForTests(true);QSignalSpy failed(&client,&Client::failed);
        auto cfg=server.config();cfg.timeoutMs=40;client.start("timeout",{block()},cfg,"fixture-only");QTRY_COMPARE(failed.size(),1);QTest::qWait(350);QCOMPARE(failed.size(),1);QVERIFY(!client.busy());
    }
    void cancelFromProgressDoesNotSendRequest() {
        FixtureServer server;Client client;client.allowLoopbackForTests(true);QSignalSpy finished(&client,&Client::finished);
        connect(&client,&Client::updated,&client,[&client]{client.cancel();});
        client.start("cancel",{block()},server.config(),"fixture-only");QTest::qWait(80);
        QCOMPARE(server.requests,0);QCOMPARE(finished.size(),0);QVERIFY(!client.busy());
    }
    void configNoPlaintextAndEndpointScope() {
        Config cfg;cfg.mode="cloud";cfg.consentEndpoint=cfg.requestUrl().toString(QUrl::FullyEncoded);QVERIFY(cfg.authorized());
        QString error;QVERIFY(cfg.save(&error));QFile file(data.path()+"/translation.json");QVERIFY(file.open(QIODevice::ReadOnly));auto bytes=file.readAll();
        file.close();QVERIFY(!bytes.contains("apiKey"));QVERIFY(!bytes.contains("secret"));cfg.endpoint="https://other.example/v1";QVERIFY(!cfg.authorized());
        Config local;QVERIFY(local.save(&error));
    }
    void credentialVaultIsolatedRoundTrip() {
        Config cfg;cfg.endpoint="https://fixture.invalid/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
        QString error;const auto target=cfg.credentialTarget();QVERIFY(TranslationCredentials::write(target,"fixture-not-a-real-secret",&error));
        QCOMPARE(TranslationCredentials::read(target),QString("fixture-not-a-real-secret"));QVERIFY(TranslationCredentials::remove(target,&error));QVERIFY(TranslationCredentials::read(target).isEmpty());
    }
    void cancelledWindowRejectsOldOcr() {
        TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
        QImage img(80,30,QImage::Format_RGB32);img.fill(Qt::white);window.setCapture(img,{0,0,80,30});auto id=window.taskId();window.cancel();
        OcrResult r;r.lines={{"line","old text",{0,0,30,20},{}}};window.presentOcr(id,r);
        auto *source=window.findChild<QPlainTextEdit *>("translationSource");QVERIFY(source);QVERIFY(source->toPlainText().isEmpty());QCOMPARE(window.state(),QString("CANCELLED"));
    }
    void resultIsPlainTextAndCopyIsExplicit() {
        TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
        auto *source=window.findChild<QPlainTextEdit *>("translationSource");auto *target=window.findChild<QPlainTextEdit *>("translationTarget");
        QVERIFY(source&&target);source->setPlainText("<script>never execute</script> https://example.invalid");
        QApplication::clipboard()->setText("original");QCOMPARE(QApplication::clipboard()->text(),QString("original"));
        window.findChild<QPushButton *>("translationSourceCopy")->click();QCOMPARE(QApplication::clipboard()->text(),source->toPlainText());
        QVERIFY(target->isReadOnly());QCOMPARE(source->toPlainText(),QString("<script>never execute</script> https://example.invalid"));
    }
    void settingsCancelActiveTranslationBeforeDialog_data() {
        QTest::addColumn<bool>("globalSettings");
        QTest::newRow("tray")<<false;QTest::newRow("global-settings")<<true;
    }
    void settingsCancelActiveTranslationBeforeDialog() {
        QFETCH(bool,globalSettings);
        AppHost host;host.start();host.startTranslation();
        TranslationOverlay *overlay=nullptr;
        QTRY_VERIFY(([&] { for(auto *w:QApplication::topLevelWidgets())if(auto *t=qobject_cast<TranslationOverlay *>(w))overlay=t;return overlay; })());
        QTest::mousePress(overlay,Qt::LeftButton,Qt::NoModifier,{20,20});
        QTest::mouseRelease(overlay,Qt::LeftButton,Qt::NoModifier,{180,90});
        TranslationWindow *window=nullptr;
        for(auto *w:QApplication::topLevelWidgets())if(auto *t=qobject_cast<TranslationWindow *>(w))window=t;
        QVERIFY(window);QVERIFY(window->busy());
        bool cancelledBeforeDialog=false;
        QTimer::singleShot(0,&host,[&] {
            cancelledBeforeDialog=!window->busy() && window->state()=="CANCELLED";
            for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog *>(w))d->reject();
        });
        if (globalSettings) {
            host.showSettings();
            QPushButton *button=nullptr;
            for(auto *w:QApplication::topLevelWidgets())if(auto *b=w->findChild<QPushButton *>("openTranslationSettings"))button=b;
            QVERIFY(button);button->click();
        } else host.showTranslationSettings();
        QVERIFY(cancelledBeforeDialog);QVERIFY(!window->busy());
    }
    void hostEntryIsSeparateFromSnipAndNotes() {
        AppHost host;host.start();const auto before=host.dumpState();
        host.startTranslation();QTRY_VERIFY(host.dumpState()["translation"].toObject()["selecting"].toBool());
        QTRY_VERIFY([] { for(auto *w:QApplication::topLevelWidgets())if(qobject_cast<TranslationOverlay *>(w))return true;return false; }());
        TranslationOverlay *overlay=nullptr;for(auto *w:QApplication::topLevelWidgets())if(auto *t=qobject_cast<TranslationOverlay *>(w))overlay=t;
        QVERIFY(overlay);host.startSnip();QVERIFY(!host.dumpState()["snip"].toObject()["active"].toBool());
        QTest::keyClick(overlay,Qt::Key_Escape);QTRY_VERIFY(!host.dumpState()["translation"].toObject()["selecting"].toBool());
        QCOMPARE(host.dumpState()["tasks"].toObject(),before["tasks"].toObject());
        QCOMPARE(host.dumpState()["settings"].toObject()["lastRegion"],before["settings"].toObject()["lastRegion"]);
        host.startSnip();QTRY_VERIFY(host.dumpState()["snip"].toObject()["active"].toBool());
        host.startTranslation();QVERIFY(!host.dumpState()["translation"].toObject()["selecting"].toBool());
    }
};
QTEST_MAIN(TranslationTests)
#include "tst_translation.moc"
