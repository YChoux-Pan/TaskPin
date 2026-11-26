#include "translation/TranslationClient.h"
#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include "core/Settings.h"
#include "core/Store.h"
#include "app/AppHost.h"
#include "ui/SnipOverlay.h"
#include "ui/TranslationWindow.h"
#include "ui/PinWindow.h"
#include <QClipboard>
#include <QDir>
#include <QDialog>
#include <QTabWidget>
#include <QLineEdit>
#include <QLabel>
#include <QFileInfo>
#include "ui/SettingsDialog.h"
#include "ui/TranslationSettingsPage.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCryptographicHash>
#include <QMenu>
#include <QWheelEvent>
#include <QContextMenuEvent>
#include <QFile>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include <QSignalSpy>
#include <QScopeGuard>
#include <QStandardItemModel>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>
#include <vector>
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
static QString writeDraftOcrProfile(const QString &directory,const QString &name,bool realModels)
{
    QJsonObject profile{{"schemaVersion",1},{"id","fixture-custom"},{"modelVersion","generated fixture"},{"language","multi"}};
    if(realModels) {
        const QString runtime=Translation::Ocr::runtimeDirectory();QFile lock(runtime+"/runtime-lock.json");
        if(!lock.open(QIODevice::ReadOnly))return {};
        for(const auto &value:QJsonDocument::fromJson(lock.readAll()).object().value("models").toArray()) {
            const auto model=value.toObject();
            profile.insert(model.value("task").toString(),QJsonObject{{"path",runtime+"/models/"+model.value("filename").toString()},
                {"sha256",model.value("sha256")},{"ocrVersion","PP-OCRv5"},{"modelType","mobile"}});
        }
    } else {
        const QString modelPath=directory+"/"+name+".onnx";QFile model(modelPath);
        if(!model.open(QIODevice::WriteOnly))return {};model.write("generated-profile-validation-fixture");model.close();
        const QString digest=QString::fromLatin1(QCryptographicHash::hash("generated-profile-validation-fixture",QCryptographicHash::Sha256).toHex());
        const QJsonObject weight{{"path",modelPath},{"sha256",digest},{"ocrVersion","PP-OCRv5"},{"modelType","mobile"}};
        profile.insert("det",weight);profile.insert("rec",weight);
    }
    const QString path=directory+"/"+name+".json";QFile file(path);
    if(!file.open(QIODevice::WriteOnly))return {};
    if(file.write(QJsonDocument(profile).toJson())<0)return {};return path;
}
static bool pinMenuAction(PinWindow &pin,const QString &name) {
    QContextMenuEvent event(QContextMenuEvent::Mouse,{15,15},pin.mapToGlobal(QPoint(15,15)));QApplication::sendEvent(&pin,&event);
    auto *menu=pin.findChild<QMenu *>();if(!menu)return false;
    std::function<QAction *(QMenu *)> find=[&](QMenu *m)->QAction *{for(auto *a:m->actions()){
        if(a->text().section('\t',0,0)==name)return a;if(a->menu())if(auto *nested=find(a->menu()))return nested;}return nullptr;};
    auto *action=find(menu);if(action)action->trigger();menu->close();
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);return action!=nullptr;
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
    void onlyRegionScreenshotEntryRemains() {
        QCOMPARE(Settings::defaultChord("snip"),QString("F1"));QVERIFY(Settings::defaultChord("translate").isEmpty());
        QVERIFY(Settings::fallbackChord("translate").isEmpty());
        auto cfg=Config::load();QCOMPARE(cfg.mode,QString("local"));QVERIFY(!cfg.authorized());
    }
    void translationResultUsesPinAtCaptureOrigin() {
        TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
        QImage image(800,400,QImage::Format_RGB32);image.fill(Qt::white);
        const QRect region(-300,120,400,200);window.setCapture(image,region);window.cancel();
        auto *pin=qobject_cast<PinWindow *>(&window);QVERIFY(pin);
        QVERIFY(window.windowFlags().testFlag(Qt::FramelessWindowHint));
        QCOMPARE(pin->contentGeometry(),region);QVERIFY(PinWindow::all().contains(pin));
        QVERIFY(!window.findChild<QPushButton *>("translationRetry"));
    }
    void translationStatusDrawsRedTextOverCapture() {
        TranslationWindow pin;
        pin.applyAppearance(0, 0, Qt::transparent);
        QImage capture(800, 100, QImage::Format_RGB32);
        capture.fill(Qt::cyan);
        pin.setCapture(capture, capture.rect());
        pin.cancel();

        QImage displayed(pin.size(), QImage::Format_RGB32);
        displayed.fill(Qt::black);
        pin.render(&displayed);
        const QRect besideText(600, 60, 150, 30);
        QCOMPARE(displayed.copy(besideText), capture.copy(besideText));
        bool foundRedText = false;
        for (int y = displayed.height() / 2; y < displayed.height(); ++y) {
            for (int x = 0; x < besideText.left(); ++x) {
                const auto pixel = displayed.pixelColor(x, y);
                if (pixel.red() > 180 && pixel.green() < 80 && pixel.blue() < 80)
                    foundRedText = true;
            }
        }
        QVERIFY(foundRedText);
    }
    void translatedFramePreservesMovedZoomedHiddenPin() {
        Config config;config.mode="mock";QString error;QVERIFY(config.save(&error));
        TranslationWindow pin;pin.setAttribute(Qt::WA_DeleteOnClose,false);
        QImage image(800,400,QImage::Format_RGB32);image.fill(Qt::white);
        pin.setCapture(image,{-300,120,400,200});const auto id=pin.taskId();pin.findChild<Translation::Ocr *>()->cancel();
        QTest::keyClick(&pin,Qt::Key_Plus);pin.move(-190,85);
        const QPoint origin=pin.contentGeometry().topLeft();const auto zoom=pin.zoom();pin.hide();
        OcrResult result;result.imageSize=image.size();result.language="en-US";result.lines={{"a","The region is bright.",{24,30,600,40},{}}};
        pin.presentOcr(id,result);QTRY_COMPARE(pin.state(),QString("PRESENTING"));
        QVERIFY(!pin.isVisible());QCOMPARE(pin.contentGeometry().topLeft(),origin);QCOMPARE(pin.zoom(),zoom);
        QVERIFY(pin.image()!=image);QVERIFY(pin.targetText().contains(QStringLiteral("非真实译文")));
        QVERIFY(pinMenuAction(pin,QStringLiteral("查看原图")));QCOMPARE(pin.image(),image);
        QCOMPARE(pin.contentGeometry().topLeft(),origin);QCOMPARE(pin.zoom(),zoom);
        QVERIFY(pinMenuAction(pin,QStringLiteral("顺时针 90°")));QCOMPARE(pin.image(),image.transformed(QTransform().rotate(90)));
        QVERIFY(pinMenuAction(pin,QStringLiteral("显示中文译图")));
        QCOMPARE(pin.image(),pin.translatedImage().transformed(QTransform().rotate(90)));
        Config local;QVERIFY(local.save(&error));
    }
    void closingPinWithOpenMenuCleansBoth() {
        auto *pin=new TranslationWindow;QPointer<TranslationWindow> alive=pin;
        QContextMenuEvent event(QContextMenuEvent::Mouse,{1,1},{50,50});QApplication::sendEvent(pin,&event);
        QPointer<QMenu> menu=pin->findChild<QMenu *>();QVERIFY(menu);
        pin->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QVERIFY(alive.isNull());QVERIFY(menu.isNull());
    }
    void multipleTranslationPinsHaveOneActiveTaskAndHostOwnership() {
        QPointer<TranslationWindow> first,second;
        {
            AppHost host;host.start();QImage image(300,100,QImage::Format_RGB32);image.fill(Qt::white);
            host.translateImage(image,{20,20,300,100});first=qobject_cast<TranslationWindow *>(PinWindow::all().last());QVERIFY(first->busy());
            host.translateImage(image,{60,60,300,100});second=qobject_cast<TranslationWindow *>(PinWindow::all().last());
            QCOMPARE(PinWindow::all().size(),2);QVERIFY(first!=second);QCOMPARE(first->state(),QString("CANCELLED"));QVERIFY(second->busy());
            first->close();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);QVERIFY(first.isNull());
            QVERIFY(host.dumpState()["translation"].toObject()["busy"].toBool());QVERIFY(second->busy());
        }
        QVERIFY(second.isNull());QVERIFY(PinWindow::all().isEmpty());
    }
    void retryCancelledOcrPinPreservesViewAndCancelsOtherTask() {
        AppHost host;host.start();QImage image(400,200,QImage::Format_RGB32);image.fill(Qt::white);
        host.translateImage(image,{20,20,200,100});auto *first=qobject_cast<TranslationWindow *>(PinWindow::all().last());
        QTest::keyClick(first,Qt::Key_Plus);first->move(80,110);
        const auto origin=first->contentGeometry().topLeft();const auto zoom=first->zoom();
        host.translateImage(image,{40,40,200,100});auto *second=qobject_cast<TranslationWindow *>(PinWindow::all().last());
        QCOMPARE(first->state(),QString("CANCELLED"));QVERIFY(second->busy());
        QVERIFY(pinMenuAction(*first,QStringLiteral("重新翻译")));
        QVERIFY(first->busy());QCOMPARE(second->state(),QString("CANCELLED"));
        QCOMPARE(first->contentGeometry().topLeft(),origin);QCOMPARE(first->zoom(),zoom);
    }
    void settingsHidesAndRestoresAllTranslationPins() {
        AppHost host;host.start();QImage image(200,100,QImage::Format_RGB32);image.fill(Qt::white);
        host.translateImage(image,{20,20,200,100});auto *first=qobject_cast<TranslationWindow *>(PinWindow::all().last());
        host.translateImage(image,{50,50,200,100});auto *second=qobject_cast<TranslationWindow *>(PinWindow::all().last());
        host.showSettings();QVERIFY(!first->isVisible());QVERIFY(!second->isVisible());
        for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<SettingsDialog *>(w))d->reject();
        QVERIFY(first->isVisible());QVERIFY(second->isVisible());QVERIFY(!first->busy());QVERIFY(!second->busy());
    }
    void sharedAiConfigurationIgnoresLegacyTranslationConnection() {
        auto &s=Settings::instance();const auto endpoint=s.aiBaseUrl,model=s.aiModel;
        Config cfg;cfg.endpoint="https://old-translation.invalid/v1";cfg.model="old-model";QString error;QVERIFY(cfg.save(&error));
        s.aiBaseUrl="https://shared-ai.invalid/v1";s.aiModel="working-model";
        auto actual=Config::load();
        const bool shared=actual.endpoint==s.aiBaseUrl && actual.model==s.aiModel;
        s.aiBaseUrl=endpoint;s.aiModel=model;Config local;QVERIFY(local.save(&error));
        QVERIFY(shared);
    }
    void translationSettingsIsOneNonmodalPage() {
        AppHost host;host.start();host.showSettings();
        bool modalObserved=false;
        QTimer watchdog;watchdog.setInterval(20);
        connect(&watchdog,&QTimer::timeout,&host,[&] {for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<QDialog *>(w))if(d->isModal()){modalObserved=true;d->reject();}});
        watchdog.start();host.showTranslationSettings();watchdog.stop();
        int settings=0;bool pageFound=false;
        for(auto *w:QApplication::topLevelWidgets())if(w->windowTitle()==QStringLiteral("TaskPin 设置")) {
            ++settings;auto *tabs=w->findChild<QTabWidget *>();if(tabs)pageFound=tabs->tabText(tabs->currentIndex())==QStringLiteral("截图翻译");
        }
        QVERIFY(!modalObserved);QCOMPARE(settings,1);QVERIFY(pageFound);
    }
    void sharedKeySaveReloadAndCancel() {
        auto &s=Settings::instance();const auto endpoint=s.aiBaseUrl,model=s.aiModel,key=s.aiApiKey;
        Config local;QString error;QVERIFY(local.save(&error));
        {
            SettingsDialog dialog;dialog.findChild<QLineEdit *>("aiApiKey")->setText("fixture-saved-token");
            dialog.findChild<QLineEdit *>("aiModel")->setText("fixture-model");
            for(auto *b:dialog.findChildren<QPushButton *>())if(b->text()==QStringLiteral("保存"))b->click();
            QCOMPARE(dialog.result(),int(QDialog::Accepted));
        }
        s.load();QCOMPARE(s.aiApiKey,QString("fixture-saved-token"));QCOMPARE(Config::load().apiKey(),QString("fixture-saved-token"));
        {
            SettingsDialog dialog;QCOMPARE(dialog.findChild<QLineEdit *>("aiApiKey")->text(),QString("fixture-saved-token"));
            dialog.findChild<QLineEdit *>("aiApiKey")->setText("fixture-must-not-save");dialog.reject();
        }
        s.load();QCOMPARE(s.aiApiKey,QString("fixture-saved-token"));
        s.aiBaseUrl=endpoint;s.aiModel=model;s.aiApiKey=key;s.save();
    }
    void revokedConsentSurvivesDraftKeyAndModelEdits() {
        Config cfg;cfg.mode="cloud";cfg.consentEndpoint=cfg.requestUrl().toString(QUrl::FullyEncoded);QString error;QVERIFY(cfg.save(&error));
        TranslationSettingsPage page;page.setConnection(cfg.endpoint,cfg.model,"fixture-key");
        auto *consent=page.findChild<QCheckBox *>("translationConsent");QVERIFY(consent->isChecked());
        consent->setChecked(false);page.setConnection(cfg.endpoint,"new-model","new-fixture-key");
        const bool stillRevoked=!consent->isChecked();Config local;QVERIFY(local.save(&error));QVERIFY(stillRevoked);
    }
    void failedKeySaveDoesNotClaimSuccessOrChangeSettings_data() {
        QTest::addColumn<bool>("existingOcrSelection");
        QTest::newRow("no-prior-ocr")<<false;QTest::newRow("prior-ocr")<<true;
    }
    void failedKeySaveDoesNotClaimSuccessOrChangeSettings() {
        QFETCH(bool,existingOcrSelection);
        auto &s=Settings::instance();const QString oldKey=s.aiApiKey;
        Config local;QString error;QVERIFY(local.save(&error));s.save();
        QFile ocrSelection(Settings::dataDir()+"/ocr.json");QFile::remove(ocrSelection.fileName());
        const QByteArray ocrBytes="{\n \"schemaVersion\":1, \"profile\":\"builtin-ppocrv5-mobile\", \"customProfilePath\":\"\"\n}\n";
        if(existingOcrSelection){QVERIFY(ocrSelection.open(QIODevice::WriteOnly));ocrSelection.write(ocrBytes);ocrSelection.close();}
        QFile before(Settings::dataDir()+"/translation.json");QVERIFY(before.open(QIODevice::ReadOnly));const auto translationBefore=before.readAll();before.close();
        SettingsDialog dialog;QSignalSpy saved(&dialog,&SettingsDialog::saved);
        dialog.findChild<QLineEdit *>("aiApiKey")->setText("fixture-must-not-commit");
        const QString path=Settings::settingsPath(),backup=path+".test-before";
        QVERIFY(QFile::rename(path,backup));QVERIFY(QDir().mkpath(path));
        for(auto *b:dialog.findChildren<QPushButton *>())if(b->text()==QStringLiteral("保存"))b->click();
        const bool accepted=dialog.result()==QDialog::Accepted;
        const QString afterKey=s.aiApiKey;const int savedCount=saved.size();
        QVERIFY(QDir().rmdir(path));QVERIFY(QFile::rename(backup,path));
        before.setFileName(Settings::dataDir()+"/translation.json");QVERIFY(before.open(QIODevice::ReadOnly));const auto translationAfter=before.readAll();
        s.load();QVERIFY(!accepted);QCOMPARE(savedCount,0);QCOMPARE(afterKey,oldKey);QCOMPARE(translationAfter,translationBefore);
        QCOMPARE(ocrSelection.exists(),existingOcrSelection);
        if(existingOcrSelection){QVERIFY(ocrSelection.open(QIODevice::ReadOnly));QCOMPARE(ocrSelection.readAll(),ocrBytes);ocrSelection.close();}
        QFile::remove(ocrSelection.fileName());
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
    void paddleFragmentsKeepSentenceReadingOrder() {
        // Real PP-OCRv5 detections have slightly different tops within one sentence.
        OcrResult o;o.imageSize={1100,280};
        o.lines={{"a","The",{20,26,64,38},.99905},
                 {"b","operator segments",{77,27,284,40},.98019},
                 {"c","regions",{359,27,118,39},.99914},
                 {"d","by a watershed.",{472,24,238,44},.99114},
                 {"e","inner circle",{21,94,165,32},.99836},
                 {"f","(Region,",{189,92,126,38},.99668},
                 {"g","Row,",{316,94,74,33},.95897},
                 {"h","Column,",{395,94,120,32},.99908},
                 {"i","Radius)",{520,92,110,38},.99947}};
        auto blocks=buildBlocks(o);QCOMPARE(blocks.size(),2);
        QCOMPARE(blocks[0].source,QString("The operator segments regions by a watershed."));
        QCOMPARE(blocks[0].rect,QRectF(20,24,690,44));
        QCOMPARE(blocks[1].source,QString("inner circle (Region, Row, Column, Radius)"));
        QVERIFY(blocks[1].bypass());
    }
    void jitteredWordsDoNotBridgeColumnGap() {
        OcrResult o;o.lines={{"a","Left",{0,0,40,20},.99},
                            {"b","text",{45,5,40,20},.99},
                            {"c","RIGHT",{120,0,80,20},.99}};
        auto blocks=buildBlocks(o);QCOMPARE(blocks.size(),2);
        QCOMPARE(blocks[0].source,QString("Left text"));
        QCOMPARE(blocks[1].source,QString("RIGHT"));
    }
    void strictResponseValidation() {
        auto a=block("A sentence.","a"),b=block("Threshold controls merging.","b");
        auto row=[](const Block &x){return QJsonObject{{"block_id",x.id},{"translated_text",x.protectedText},{"status","ok"}};};
        QList<Result> results;QStringList invalid;QString error;
        QVERIFY(validate({a,b},{row(b),row(a)},&results,&invalid,&error));QCOMPARE(results.size(),2);QVERIFY(invalid.isEmpty());
        results.clear();QVERIFY(!validate({a},{row(a),row(a)},&results,&invalid,&error));
        results.clear();invalid.clear();QVERIFY(validate({a,b},{row(a)},&results,&invalid,&error));QCOMPARE(invalid,QStringList{"b"});
    }
    void ocrContracts() {
        OcrResult r;QString error;
        QVERIFY(Translation::Ocr::decode(R"({"ok":true,"language":"en-US","lines":[{"text":"Threshold < 10","x":2,"y":3,"width":100,"height":20}]})",{200,100},&r,&error));
        QCOMPARE(r.lines.size(),1);QVERIFY(!r.lines.first().recognitionConfidence);QCOMPARE(r.lines.first().rect,QRectF(2,3,100,20));
        OcrResult invalid;QVERIFY(!Translation::Ocr::decode(R"({"ok":true,"lines":[{"text":"x","x":-1,"y":0,"width":10,"height":10}]})",{200,100},&invalid,&error));
    }
    void lightweightOcrSelectionSavesAndCancelKeepsPrevious() {
        Config local;QString error;QVERIFY(local.save(&error));
        const QString selection=Settings::dataDir()+"/ocr.json";QFile::remove(selection);
        {
            TranslationSettingsPage page;
            auto *choice=page.findChild<QComboBox *>("translationOcrProfile");QVERIFY(choice);
            const int lightweight=choice->findData("builtin-ppocrv4-mobile");QVERIFY(lightweight>=0);
            choice->setCurrentIndex(lightweight);
            QVERIFY(!page.findChild<QLineEdit *>("translationOcrProfilePath")->isEnabled());
            QVERIFY(page.save());
        }
        QCOMPARE(OcrProfiles::load(&error).profile,QString("builtin-ppocrv4-mobile"));QVERIFY(error.isEmpty());
        {
            TranslationSettingsPage draft;
            auto *choice=draft.findChild<QComboBox *>("translationOcrProfile");
            QCOMPARE(choice->currentData().toString(),QString("builtin-ppocrv4-mobile"));
            choice->setCurrentIndex(choice->findData("builtin-ppocrv5-mobile"));
        }
        QCOMPARE(OcrProfiles::load(&error).profile,QString("builtin-ppocrv4-mobile"));
        QFile::remove(selection);
    }
    void v6OcrSelectionSavesAndReopens() {
        Config local;QString error;QVERIFY(local.save(&error));QFile::remove(OcrProfiles::settingsPath());
        {
            TranslationSettingsPage page;auto *choice=page.findChild<QComboBox *>("translationOcrProfile");
            const int tiny=choice->findData("builtin-ppocrv6-tiny");QVERIFY(tiny>=0);
            choice->setCurrentIndex(tiny);QVERIFY2(page.save(),qPrintable(page.findChild<QLabel *>("translationOcrStatus")->text()));
        }
        QCOMPARE(OcrProfiles::load(&error).profile,QString("builtin-ppocrv6-tiny"));QVERIFY(error.isEmpty());
        TranslationSettingsPage reopened;QCOMPARE(reopened.findChild<QComboBox *>("translationOcrProfile")->currentData().toString(),QString("builtin-ppocrv6-tiny"));
        QFile::remove(OcrProfiles::settingsPath());
    }
    void ocrFolderRefreshPreservesDraftAndMissingSelection() {
        QTemporaryDir packages;QVERIFY(packages.isValid());const auto previous=qgetenv("TASKPIN_OCR_MODELS");
        qputenv("TASKPIN_OCR_MODELS",packages.path().toUtf8());
        const auto restore=qScopeGuard([previous]{if(previous.isEmpty())qunsetenv("TASKPIN_OCR_MODELS");else qputenv("TASKPIN_OCR_MODELS",previous);});
        QFile::remove(OcrProfiles::settingsPath());
        const QString folder=packages.path()+"/dropped-small";QVERIFY(QDir().mkpath(folder));
        for(const auto &name:{"PP-OCRv6_det_small.onnx","PP-OCRv6_rec_small.onnx"}) {
            QFile file(folder+"/"+name);QVERIFY(file.open(QIODevice::WriteOnly));file.write("catalog metadata fixture");
        }
        QString id;
        {
            TranslationSettingsPage page;auto *choice=page.findChild<QComboBox *>("translationOcrProfile");
            auto *refresh=page.findChild<QPushButton *>("translationOcrRefresh");QVERIFY(refresh);
            QVERIFY(page.findChild<QPushButton *>("translationOcrOpenFolder"));
            auto localIndex=[&]{for(int i=0;i<choice->count();++i)if(choice->itemData(i).toString().startsWith("local-"))return i;return -1;};
            QTRY_VERIFY_WITH_TIMEOUT(localIndex()>=0,30000);id=choice->itemData(localIndex()).toString();
            choice->setCurrentIndex(choice->findData("custom"));page.findChild<QLineEdit *>("translationOcrProfilePath")->setText("unsaved-draft.json");
            refresh->click();QTRY_VERIFY_WITH_TIMEOUT(refresh->isEnabled(),30000);
            QCOMPARE(choice->currentData().toString(),QString("custom"));QCOMPARE(page.findChild<QLineEdit *>("translationOcrProfilePath")->text(),QString("unsaved-draft.json"));
            choice->setCurrentIndex(choice->findData(id));QSignalSpy ready(&page,SIGNAL(saveReady()));QSignalSpy busy(&page,SIGNAL(validationBusyChanged(bool)));
            QVERIFY(!page.save());QTRY_VERIFY_WITH_TIMEOUT(busy.size()>=2,30000);QCOMPARE(ready.size(),0);
            QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
        }
        QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
        OcrProfiles selection;selection.profile=id;QString error;QVERIFY(selection.save(&error,false));
        QVERIFY(QDir(folder).removeRecursively());
        {
            TranslationSettingsPage page;auto *choice=page.findChild<QComboBox *>("translationOcrProfile");
            QTRY_VERIFY_WITH_TIMEOUT(page.findChild<QPushButton *>("translationOcrRefresh")->isEnabled(),30000);
            QCOMPARE(choice->currentData().toString(),id);QVERIFY(choice->currentText().contains(QStringLiteral("不可用")));
            auto *model=qobject_cast<QStandardItemModel *>(choice->model());QVERIFY(model);QVERIFY(!model->item(choice->currentIndex())->isEnabled());
            QVERIFY(!page.save());
        }
        QFile::remove(OcrProfiles::settingsPath());
    }
    void folderOcrLoadsOnSaveAndRoutesSharedWorker() {
        QVERIFY(Translation::Ocr::available());QTemporaryDir packages;QVERIFY(packages.isValid());
        const auto previous=qgetenv("TASKPIN_OCR_MODELS");qputenv("TASKPIN_OCR_MODELS",packages.path().toUtf8());
        const auto restore=qScopeGuard([previous]{QFile::remove(OcrProfiles::settingsPath());if(previous.isEmpty())qunsetenv("TASKPIN_OCR_MODELS");else qputenv("TASKPIN_OCR_MODELS",previous);});
        Config local;QString error;QVERIFY(local.save(&error));QFile::remove(OcrProfiles::settingsPath());
        const QString folder=packages.path()+"/actual-tiny";QVERIFY(QDir().mkpath(folder));
        for(const auto &name:{"PP-OCRv6_det_tiny.onnx","PP-OCRv6_rec_tiny.onnx"})
            QVERIFY(QFile::copy(Translation::Ocr::runtimeDirectory()+"/models/"+name,folder+"/"+name));
        QString selected;
        {
            TranslationSettingsPage page;auto *choice=page.findChild<QComboBox *>("translationOcrProfile");
            auto localIndex=[&]{for(int i=0;i<choice->count();++i)if(choice->itemData(i).toString().startsWith("local-"))return i;return -1;};
            QTRY_VERIFY_WITH_TIMEOUT(localIndex()>=0,30000);selected=choice->itemData(localIndex()).toString();choice->setCurrentIndex(localIndex());
            QSignalSpy ready(&page,SIGNAL(saveReady()));bool heartbeat=false;QTimer::singleShot(0,&page,[&]{heartbeat=true;});
            QElapsedTimer elapsed;elapsed.start();QVERIFY(!page.save());QVERIFY(elapsed.elapsed()<500);QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
            QTRY_VERIFY(heartbeat);page.cancelTest();QTest::qWait(100);QCOMPARE(ready.size(),0);QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
            QVERIFY(!page.save());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,90000);
            // A descriptor added after loading changes model interpretation even
            // when the already-discovered weight files have not changed.
            QFile descriptor(folder+"/model.json");QVERIFY(descriptor.open(QIODevice::WriteOnly));descriptor.write("{invalid}");descriptor.close();
            QVERIFY(!page.save());QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
            page.cancelTest();QVERIFY(QFile::remove(descriptor.fileName()));
            QVERIFY(!page.save());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,90000);QVERIFY(page.save());
        }
        QCOMPARE(OcrProfiles::load(&error).profile,selected);QVERIFY(error.isEmpty());
        {
            TranslationSettingsPage draft;auto *choice=draft.findChild<QComboBox *>("translationOcrProfile");
            QTRY_VERIFY_WITH_TIMEOUT(draft.findChild<QPushButton *>("translationOcrRefresh")->isEnabled(),30000);
            QCOMPARE(choice->currentData().toString(),selected);choice->setCurrentIndex(choice->findData("builtin-ppocrv5-mobile"));
        }
        QCOMPARE(OcrProfiles::load(&error).profile,selected);
        Translation::Ocr recognizer;QSignalSpy finished(&recognizer,&Translation::Ocr::finished),failed(&recognizer,&Translation::Ocr::failed);
        QImage image(300,80,QImage::Format_RGB32);image.fill(Qt::white);recognizer.recognize("folder-routing",image);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(),90000);
        QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first()[1].toString()));
        const auto result=qvariant_cast<OcrResult>(finished.last()[1]);QCOMPARE(result.profileId,selected);QCOMPARE(result.engine,QString("PaddleOCR PP-OCRv6"));
    }
    void folderOcrValidationRejectsPackageMembershipChanges_data() {
        QTest::addColumn<bool>("afterReady");
        QTest::newRow("while-loading")<<false;QTest::newRow("after-loading")<<true;
    }
    void folderOcrValidationRejectsPackageMembershipChanges() {
        QFETCH(bool,afterReady);QVERIFY(Translation::Ocr::available());QTemporaryDir packages;QVERIFY(packages.isValid());
        const auto previous=qgetenv("TASKPIN_OCR_MODELS");qputenv("TASKPIN_OCR_MODELS",packages.path().toUtf8());
        const auto restore=qScopeGuard([previous]{QFile::remove(OcrProfiles::settingsPath());if(previous.isEmpty())qunsetenv("TASKPIN_OCR_MODELS");else qputenv("TASKPIN_OCR_MODELS",previous);});
        Config local;QString error;QVERIFY(local.save(&error));QFile::remove(OcrProfiles::settingsPath());
        QFile translation(Settings::dataDir()+"/translation.json");QVERIFY(translation.open(QIODevice::ReadOnly));const auto before=translation.readAll();translation.close();
        const QString folder=packages.path()+"/changing-tiny";QVERIFY(QDir().mkpath(folder));
        for(const auto &name:{"PP-OCRv6_det_tiny.onnx","PP-OCRv6_rec_tiny.onnx"})
            QVERIFY(QFile::copy(Translation::Ocr::runtimeDirectory()+"/models/"+name,folder+"/"+name));
        TranslationSettingsPage page;auto *choice=page.findChild<QComboBox *>("translationOcrProfile");
        auto localIndex=[&]{for(int i=0;i<choice->count();++i)if(choice->itemData(i).toString().startsWith("local-"))return i;return -1;};
        QTRY_VERIFY_WITH_TIMEOUT(localIndex()>=0,30000);choice->setCurrentIndex(localIndex());
        QSignalSpy ready(&page,SIGNAL(saveReady())),busy(&page,SIGNAL(validationBusyChanged(bool)));
        QVERIFY(!page.save());
        if(afterReady)QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,90000);
        QFile conflict(folder+"/unknown.onnx");QVERIFY(conflict.open(QIODevice::WriteOnly));conflict.write("conflicting package member");conflict.close();
        if(afterReady)QVERIFY(!page.save());
        QTRY_VERIFY_WITH_TIMEOUT(!busy.isEmpty() && !busy.last().first().toBool(),90000);
        QCOMPARE(ready.size(),afterReady?1:0);QVERIFY(!QFileInfo::exists(OcrProfiles::settingsPath()));
        QVERIFY(translation.open(QIODevice::ReadOnly));QCOMPARE(translation.readAll(),before);
    }
    void customV6SchemaAndLocalIdsAreStrict() {
        QString error;const auto path=writeDraftOcrProfile(data.path(),"v6-schema",false);QVERIFY(!path.isEmpty());
        QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));auto object=QJsonDocument::fromJson(file.readAll()).object();file.close();
        object.insert("detector",QJsonObject{{"limitType","max"},{"limitSideLen",960}});
        for(const auto &task:QStringList{"det","rec"}) {auto model=object.value(task).toObject();model.insert("ocrVersion","PP-OCRv6");model.insert("modelType","tiny");model.insert("language","ch");object.insert(task,model);}
        auto write=[&]{if(!file.open(QIODevice::WriteOnly|QIODevice::Truncate))return false;file.write(QJsonDocument(object).toJson());file.close();return true;};
        OcrProfiles selection;selection.profile="custom";selection.customProfilePath=path;QVERIFY(write());QVERIFY2(selection.validate(&error,false),qPrintable(error));
        object.insert("detector",QJsonObject{{"limitType","max"},{"limitSideLen",31}});QVERIFY(write());QVERIFY(!selection.validate(&error,false));
        object.insert("detector",QJsonObject{{"limitType","max"},{"limitSideLen",960}});auto model=object.value("rec").toObject();model.insert("modelType","mobile");object.insert("rec",model);QVERIFY(write());QVERIFY(!selection.validate(&error,false));
        selection.profile="local-"+QString(64,'a');QVERIFY(selection.validate(&error,false));
        for(const auto &id:QStringList{"local-a","local-"+QString(64,'A'),"local-"+QString(65,'a'),"local-../model"}) {selection.profile=id;QVERIFY(!selection.validate(&error,false));}
        const auto configured=qgetenv("TASKPIN_OCR_MODELS");qputenv("TASKPIN_OCR_MODELS",data.path().toUtf8());
        QCOMPARE(OcrProfiles::modelDirectory(),QDir::cleanPath(data.path()));
        if(configured.isEmpty())qunsetenv("TASKPIN_OCR_MODELS");else qputenv("TASKPIN_OCR_MODELS",configured);
    }
    void lightweightOcrActuallySwitchesSharedWorker() {
        QVERIFY2(Translation::Ocr::available(),"Real packaged OCR runtime required");
        QImage image(300,80,QImage::Format_RGB32);image.fill(Qt::white);
        Translation::Ocr recognizer;QSignalSpy finished(&recognizer,&Translation::Ocr::finished),failed(&recognizer,&Translation::Ocr::failed);
        QString error;OcrProfiles selection;
        QString lightweightIdentity;
        for(const QString &profile:QStringList{"builtin-ppocrv4-mobile","builtin-ppocrv4-mobile","builtin-ppocrv5-mobile"}) {
            selection.profile=profile;QVERIFY2(selection.save(&error),qPrintable(error));
            const int before=finished.size();recognizer.recognize(profile,image);
            QTRY_VERIFY_WITH_TIMEOUT(finished.size()>before || !failed.isEmpty(),90000);
            QVERIFY2(failed.isEmpty(),failed.isEmpty()?"":qPrintable(failed.first()[1].toString()));
            const auto result=qvariant_cast<OcrResult>(finished.last()[1]);QCOMPARE(result.profileId,profile);
            if(before==0){QCOMPARE(result.engine,QString("PaddleOCR PP-OCRv4"));lightweightIdentity=result.modelIdentity;}
            else if(before==1){QVERIFY(result.workerReused);QCOMPARE(result.modelIdentity,lightweightIdentity);}
            else {QCOMPARE(result.engine,QString("PaddleOCR PP-OCRv5"));QVERIFY(!result.workerReused);QVERIFY(result.modelIdentity!=lightweightIdentity);}
        }
        QFile::remove(OcrProfiles::settingsPath());
    }
    void ocrProfileSelectionSavesAndCancelKeepsPrevious() {
        if(!Translation::Ocr::available())QSKIP("Real packaged OCR runtime not available");
        Config local;QString error;QVERIFY(local.save(&error));
        const QString selection=Settings::dataDir()+"/ocr.json";
        QFile::remove(selection);
        const QString profilePath=writeDraftOcrProfile(data.path(),"local-profile",true);QVERIFY(!profilePath.isEmpty());
        {
            TranslationSettingsPage draft;
            auto *choice=draft.findChild<QComboBox *>("translationOcrProfile");
            auto *path=draft.findChild<QLineEdit *>("translationOcrProfilePath");
            QVERIFY(choice&&path);QCOMPARE(choice->currentData().toString(),QString("builtin-ppocrv5-mobile"));
            choice->setCurrentIndex(choice->findData("custom"));path->setText(profilePath);
        }
        QVERIFY(!QFileInfo::exists(selection));
        {
            TranslationSettingsPage page;
            auto *choice=page.findChild<QComboBox *>("translationOcrProfile");auto *path=page.findChild<QLineEdit *>("translationOcrProfilePath");
            QVERIFY(choice&&path);choice->setCurrentIndex(choice->findData("custom"));path->setText(profilePath);
            const bool immediate=page.save();QVERIFY(!immediate);QVERIFY(!QFileInfo::exists(selection));
            QSignalSpy ready(&page,SIGNAL(saveReady()));QVERIFY(ready.isValid());QTRY_COMPARE_WITH_TIMEOUT(ready.size(),1,90000);
            // A profile edited on disk after the helper succeeded must not use
            // that staged validation to commit a different configuration.
            QFile edited(profilePath);QVERIFY(edited.open(QIODevice::ReadOnly));auto profile=QJsonDocument::fromJson(edited.readAll()).object();edited.close();
            profile.insert("modelVersion","changed after validation");QVERIFY(edited.open(QIODevice::WriteOnly|QIODevice::Truncate));edited.write(QJsonDocument(profile).toJson());edited.close();
            QVERIFY(!page.save());QVERIFY(!QFileInfo::exists(selection));
            QTRY_COMPARE_WITH_TIMEOUT(ready.size(),2,90000);QVERIFY(page.save());
        }
        QFile saved(selection);QVERIFY(saved.open(QIODevice::ReadOnly));const auto before=saved.readAll();saved.close();
        {
            TranslationSettingsPage page;
            auto *choice=page.findChild<QComboBox *>("translationOcrProfile");auto *path=page.findChild<QLineEdit *>("translationOcrProfilePath");
            QCOMPARE(choice->currentData().toString(),QString("custom"));QCOMPARE(path->text(),profilePath);
            choice->setCurrentIndex(0);path->clear();
        }
        QVERIFY(saved.open(QIODevice::ReadOnly));QCOMPARE(saved.readAll(),before);saved.close();QFile::remove(selection);
    }
    void customOcrValidationKeepsUiResponsiveAndCancelPreservesFiles() {
        if(!Translation::Ocr::available())QSKIP("Real packaged OCR runtime not available");
        Config local;QString error;QVERIFY(local.save(&error));QFile::remove(Settings::dataDir()+"/ocr.json");
        const QString profilePath=writeDraftOcrProfile(data.path(),"slow-profile",true);QVERIFY(!profilePath.isEmpty());
        const QString originalRuntime=Translation::Ocr::runtimeDirectory();
        const QString slowRuntime=data.path()+"/slow-validation-runtime";QVERIFY(QDir().mkpath(slowRuntime));
        for(const auto &name:{"python.exe","python312.dll","python312.zip","vcruntime140.dll","vcruntime140_1.dll"})
            if(QFileInfo::exists(originalRuntime+"/"+name))QVERIFY(QFile::copy(originalRuntime+"/"+name,slowRuntime+"/"+name));
        QFile pth(slowRuntime+"/python312._pth");QVERIFY(pth.open(QIODevice::WriteOnly));pth.write("python312.zip\n.\nimport site\n");pth.close();
        QFile helper(slowRuntime+"/helper.py");QVERIFY(helper.open(QIODevice::WriteOnly));
        const auto command=QJsonDocument(QJsonArray{originalRuntime+"/python.exe","-I","-B",originalRuntime+"/helper.py"}).toJson(QJsonDocument::Compact);
        helper.write("import sys,time,subprocess,os\nrequest=sys.stdin.buffer.read()\ntime.sleep(2)\ncommand="+command+
            "\nresult=subprocess.run(command,input=request,capture_output=True)\nos.write(1,result.stdout)\nraise SystemExit(result.returncode)\n");helper.close();
        const auto previousRuntime=qgetenv("TASKPIN_OCR_RUNTIME");qputenv("TASKPIN_OCR_RUNTIME",slowRuntime.toUtf8());
        QFile translation(Settings::dataDir()+"/translation.json");QVERIFY(translation.open(QIODevice::ReadOnly));const auto before=translation.readAll();translation.close();
        auto *page=new TranslationSettingsPage;
        auto *profileChoice=page->findChild<QComboBox *>("translationOcrProfile");profileChoice->setCurrentIndex(profileChoice->findData("custom"));page->findChild<QLineEdit *>("translationOcrProfilePath")->setText(profilePath);
        bool heartbeat=false;QTimer::singleShot(0,page,[&]{heartbeat=true;});
        QElapsedTimer elapsed;elapsed.start();const bool immediate=page->save();const auto saveMs=elapsed.elapsed();
        QSignalSpy ready(page,SIGNAL(saveReady()));
        QCoreApplication::processEvents();const bool heartbeatBeforeCancel=heartbeat;
        page->cancelTest();QTest::qWait(100);const int readyAfterCancel=ready.size();delete page;QTest::qWait(100);
        if(previousRuntime.isEmpty())qunsetenv("TASKPIN_OCR_RUNTIME");else qputenv("TASKPIN_OCR_RUNTIME",previousRuntime);
        QVERIFY(!immediate);QVERIFY(saveMs<500);QVERIFY(heartbeatBeforeCancel);QCOMPARE(readyAfterCancel,0);QCOMPARE(ready.size(),0);
        QVERIFY(!QFileInfo::exists(Settings::dataDir()+"/ocr.json"));QVERIFY(translation.open(QIODevice::ReadOnly));QCOMPARE(translation.readAll(),before);
    }
    void invalidOcrProfileDoesNotSaveEitherConfiguration() {
        Config local;QString error;QVERIFY(local.save(&error));
        const QString selection=Settings::dataDir()+"/ocr.json";QFile::remove(selection);
        QFile translation(Settings::dataDir()+"/translation.json");QVERIFY(translation.open(QIODevice::ReadOnly));const auto before=translation.readAll();translation.close();
        TranslationSettingsPage page;
        auto *choice=page.findChild<QComboBox *>("translationOcrProfile");auto *path=page.findChild<QLineEdit *>("translationOcrProfilePath");
        QVERIFY(choice&&path);choice->setCurrentIndex(choice->findData("custom"));path->setText(data.path()+"/missing-profile.json");
        page.findChild<QComboBox *>("translationMode")->setCurrentIndex(2);
        QVERIFY(!page.save());QVERIFY(!page.findChild<QLabel *>("translationOcrStatus")->text().isEmpty());QVERIFY(!QFileInfo::exists(selection));
        QVERIFY(translation.open(QIODevice::ReadOnly));QCOMPARE(translation.readAll(),before);
    }
    void invalidSavedOcrSelectionFailsVisiblyBeforeRuntimeFallback() {
        const auto previousRuntime=qgetenv("TASKPIN_OCR_RUNTIME");qputenv("TASKPIN_OCR_RUNTIME",(data.path()+"/missing-runtime").toUtf8());
        QFile selection(Settings::dataDir()+"/ocr.json");QVERIFY(selection.open(QIODevice::WriteOnly));selection.write("{\"schemaVersion\":1,\"profile\":\"unknown-future-model\"}");selection.close();
        Translation::Ocr recognizer;QSignalSpy failed(&recognizer,&Translation::Ocr::failed);
        QImage image(100,50,QImage::Format_RGB32);image.fill(Qt::white);recognizer.recognize("invalid-selection",image);
        const QString failure=failed.isEmpty()?QString():failed[0][1].toString();const bool busy=recognizer.busy();
        QFile::remove(selection.fileName());if(previousRuntime.isEmpty())qunsetenv("TASKPIN_OCR_RUNTIME");else qputenv("TASKPIN_OCR_RUNTIME",previousRuntime);
        QCOMPARE(failed.size(),1);QVERIFY(failure.contains(QStringLiteral("配置")));QVERIFY(!busy);
    }
    void missingOcrRuntimeReportsFailureAndClearsBusy() {
        const auto previousRuntime=qgetenv("TASKPIN_OCR_RUNTIME");qputenv("TASKPIN_OCR_RUNTIME",(data.path()+"/missing-runtime").toUtf8());
        Translation::Ocr recognizer;QSignalSpy failed(&recognizer,&Translation::Ocr::failed);
        QImage image(100,50,QImage::Format_RGB32);image.fill(Qt::white);recognizer.recognize("missing-runtime",image);
        if(previousRuntime.isEmpty())qunsetenv("TASKPIN_OCR_RUNTIME");else qputenv("TASKPIN_OCR_RUNTIME",previousRuntime);
        QCOMPARE(failed.size(),1);QVERIFY(failed[0][1].toString().contains(QStringLiteral("运行环境")));QVERIFY(!recognizer.busy());
    }
    void ocrModelHashFailureExplainsSelectedModelProblem() {
        OcrResult result;QString error;
        QVERIFY(!Translation::Ocr::decode(R"({"ok":false,"errorCode":"model_hash_mismatch"})",{200,100},&result,&error));
        QVERIFY(error.contains("SHA256"));
    }
    void paddleOcrPreservesModelConfidence() {
        OcrResult r;QString error;
        QVERIFY(Translation::Ocr::decode(R"({"ok":true,"engine":"PaddleOCR PP-OCRv5","language":"zh+en","modelVersion":"PP-OCRv5","lines":[{"text":"inner_circle","x":3,"y":4,"width":150,"height":22,"confidence":0.9375}]})",{200,100},&r,&error));
        QCOMPARE(r.lines.size(),1);QVERIFY(r.lines[0].recognitionConfidence.has_value());
        QCOMPARE(*r.lines[0].recognitionConfidence,.9375);
    }
    void ocrTimingMetadataIsValidatedAndPreserved() {
        OcrResult result;QString error;
        QVERIFY(Translation::Ocr::decode(R"({"ok":true,"profileId":"local-v4","modelIdentity":"sha256:fixture","workerReused":true,"stageMs":{"import":2.5,"load":0,"inference":100,"total":104},"lines":[]})",{200,100},&result,&error));
        QCOMPARE(result.profileId,QString("local-v4"));QCOMPARE(result.modelIdentity,QString("sha256:fixture"));QVERIFY(result.workerReused);
        QCOMPARE(result.stageMs.value("load").toDouble(-1),0.0);QCOMPARE(result.stageMs.value("inference").toDouble(),100.0);
        QVERIFY(!Translation::Ocr::decode(R"({"ok":true,"stageMs":{"load":-1},"lines":[]})",{200,100},&result,&error));
        QVERIFY(!Translation::Ocr::decode(R"({"ok":true,"stageMs":{"load":"fast"},"lines":[]})",{200,100},&result,&error));
    }
    void prewarmReusesSelectedModelForFirstScreenshot() {
        if(!Translation::Ocr::available())QSKIP("Real packaged OCR runtime not available");
        QTemporaryDir isolated;QVERIFY(isolated.isValid());
        const auto previousData=qgetenv("TASKPIN_DATA");
        qputenv("TASKPIN_DATA",isolated.path().toUtf8());
        const auto reset=qScopeGuard([&]{qputenv("TASKPIN_DATA",previousData);});
        OcrProfiles profile;profile.profile="builtin-ppocrv6-tiny";QString error;QVERIFY(profile.save(&error));
        Translation::Ocr recognizer;
        QSignalSpy finished(&recognizer,&Translation::Ocr::finished),failed(&recognizer,&Translation::Ocr::failed);
        QImage image(320,100,QImage::Format_RGB32);image.fill(Qt::white);
        Translation::Ocr::prewarm();
        QVERIFY(!recognizer.busy());
        Translation::Ocr::prewarm();
        recognizer.recognize("first-real-screenshot",image);
        QTRY_VERIFY_WITH_TIMEOUT(!finished.isEmpty() || !failed.isEmpty(),20000);
        QCOMPARE(failed.size(),0);QCOMPARE(finished.size(),1);
        QCOMPARE(finished.first().first().toString(),QString("first-real-screenshot"));
        const auto result=qvariant_cast<OcrResult>(finished.first()[1]);
        QCOMPARE(result.profileId,profile.profile);QVERIFY(result.workerReused);
        QCOMPARE(result.stageMs.value("load").toDouble(-1),0.0);
#ifdef Q_OS_WIN
        auto *worker=QCoreApplication::instance()->findChild<QObject *>("paddleOcrWorker");QVERIFY(worker);
        auto *process=worker->findChild<QProcess *>("paddleOcrProcess");QVERIFY(process);
        const HANDLE processHandle=OpenProcess(PROCESS_QUERY_INFORMATION,FALSE,DWORD(process->processId()));QVERIFY(processHandle);
        const DWORD priority=GetPriorityClass(processHandle);CloseHandle(processHandle);
        QCOMPARE(priority,DWORD(NORMAL_PRIORITY_CLASS));
#endif
    }
    void sharedOcrWorkerReusesModelAndHonorsCancelledOwners() {
        if(!Translation::Ocr::available())QSKIP("Real packaged OCR runtime not available");
        Config local;QString error;QVERIFY(local.save(&error));QFile::remove(Settings::dataDir()+"/ocr.json");
        QImage image(320,100,QImage::Format_RGB32);image.fill(Qt::white);
        Translation::Ocr first,queued,next;QSignalSpy firstDone(&first,&Translation::Ocr::finished),queuedDone(&queued,&Translation::Ocr::finished),nextDone(&next,&Translation::Ocr::finished);
        QSignalSpy firstFailed(&first,&Translation::Ocr::failed),queuedFailed(&queued,&Translation::Ocr::failed),nextFailed(&next,&Translation::Ocr::failed);
        first.recognize("first",image);next.recognize("warm",image);queued.recognize("queued-cancel",image);queued.cancel();
        auto *closed=new Translation::Ocr;closed->recognize("closed-owner",image);delete closed;
        QTRY_COMPARE_WITH_TIMEOUT(firstDone.size(),1,90000);QCOMPARE(firstFailed.size(),0);
        QTRY_COMPARE_WITH_TIMEOUT(nextDone.size(),1,90000);QCOMPARE(nextFailed.size(),0);
        const auto warm=qvariant_cast<OcrResult>(nextDone[0][1]);QVERIFY(warm.workerReused);QCOMPARE(warm.stageMs.value("load").toDouble(-1),0.0);
        QVERIFY(warm.stageMs.value("queue").toDouble()>0);QVERIFY(warm.stageMs.value("requestTotal").toDouble()>=warm.stageMs.value("process").toDouble());
        QCOMPARE(queuedDone.size(),0);QCOMPARE(queuedFailed.size(),0);QVERIFY(!queued.busy());
        first.recognize("active-cancel",image);next.recognize("after-cancel",image);first.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(nextDone.size(),2,90000);QCOMPARE(nextDone[1][0].toString(),QString("after-cancel"));
        QTest::qWait(80);QCOMPARE(firstDone.size(),1);QCOMPARE(firstFailed.size(),0);QCOMPARE(nextFailed.size(),0);
        const auto restarted=qvariant_cast<OcrResult>(nextDone[1][1]);QVERIFY(!restarted.workerReused);
        auto *activeOwner=new Translation::Ocr;activeOwner->recognize("closed-active",image);next.recognize("after-close",image);delete activeOwner;
        QTRY_COMPARE_WITH_TIMEOUT(nextDone.size(),3,90000);QCOMPARE(nextDone[2][0].toString(),QString("after-close"));QCOMPARE(nextFailed.size(),0);
    }
    void saturatedOcrQueueKeepsFailureIdDuringImmediateReplacement() {
        if(!Translation::Ocr::available())QSKIP("Real packaged OCR runtime not available");
        QFile::remove(Settings::dataDir()+"/ocr.json");QImage image(100,50,QImage::Format_RGB32);image.fill(Qt::white);
        std::vector<std::unique_ptr<Translation::Ocr>> owners;
        for(int i=0;i<9;++i){owners.emplace_back(std::make_unique<Translation::Ocr>());owners.back()->recognize(QString("queued-%1").arg(i),image);}
        Translation::Ocr overflow;bool replaced=false;
        connect(&overflow,&Translation::Ocr::failed,&overflow,[&](const QString &,const QString &){if(!replaced){replaced=true;overflow.recognize("replacement",image);}});
        QSignalSpy failures(&overflow,&Translation::Ocr::failed);overflow.recognize("outer-request",image);
        for(auto it=owners.rbegin();it!=owners.rend();++it)(*it)->cancel();owners.clear();overflow.cancel();QCOMPARE(failures.size(),2);
        QCOMPARE(failures[0][0].toString(),QString("replacement"));QCOMPARE(failures[1][0].toString(),QString("outer-request"));
    }
    void immediateOcrInputFailureKeepsCallerIdSnapshot() {
        Translation::Ocr recognizer;QString callerId="invalid-input";
        connect(&recognizer,&Translation::Ocr::failed,&recognizer,[&](const QString &,const QString &){callerId="changed-by-slot";});
        QSignalSpy failures(&recognizer,&Translation::Ocr::failed);recognizer.recognize(callerId,QImage());
        QCOMPARE(failures.size(),1);QCOMPARE(failures[0][0].toString(),QString("invalid-input"));
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
    void connectionTestBypassesPriorSuccessfulCache() {
        FixtureServer server;Client client;client.allowLoopbackForTests(true);
        QSignalSpy finished(&client,&Client::finished),failed(&client,&Client::failed);
        client.start("good",{block()},server.config(),"fixture-good");QTRY_COMPARE(finished.size(),1);
        server.httpStatus=401;auto cfg=server.config();cfg.useCache=false;
        client.start("bad",{block()},cfg,"fixture-invalid");
        QTRY_COMPARE(server.requests,2);QTRY_COMPARE(failed.size(),1);QCOMPARE(finished.size(),1);
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
    void cancelledWindowRejectsOldOcr() {
        TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
        QImage img(80,30,QImage::Format_RGB32);img.fill(Qt::white);window.setCapture(img,{0,0,80,30});auto id=window.taskId();window.cancel();
        OcrResult r;r.lines={{"line","old text",{0,0,30,20},{}}};window.presentOcr(id,r);
        QVERIFY(window.sourceText().isEmpty());QCOMPARE(window.state(),QString("CANCELLED"));
    }
    void resultIsPlainTextAndCopyIsExplicit() {
        TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
        QVERIFY(!window.findChild<QPlainTextEdit *>("translationSource"));window.showComparison();
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
        AppHost host;host.start();QImage image(320,140,QImage::Format_RGB32);image.fill(Qt::white);host.translateImage(image,{20,20,160,70});
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
        QCoreApplication::processEvents();
        QVERIFY(cancelledBeforeDialog);QVERIFY(!window->busy());
    }
    void regionEditorTranslationCreatesPinWithoutNotesOrHistory() {
        AppHost host;host.start();const auto before=host.dumpState();host.startSnip();
        SnipOverlay *overlay=nullptr;for(auto *w:QApplication::topLevelWidgets())if(auto *t=qobject_cast<SnipOverlay *>(w))overlay=t;
        QVERIFY(overlay);overlay->setPreselect({40,40,160,80});QTest::keyClick(overlay,Qt::Key_T,Qt::ControlModifier);
        QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
        QCOMPARE(PinWindow::all().size(),1);auto *pin=qobject_cast<TranslationWindow *>(PinWindow::all().first());QVERIFY(pin);
        QCOMPARE(pin->contentGeometry().size(),QSize(160,80));
        QCOMPARE(host.dumpState()["tasks"].toObject(),before["tasks"].toObject());
        QCOMPARE(host.dumpState()["history"].toObject(),before["history"].toObject());
        QCOMPARE(host.dumpState()["settings"].toObject()["lastRegion"],before["settings"].toObject()["lastRegion"]);
        QVERIFY(!host.dumpState()["snip"].toObject()["active"].toBool());
        for(const auto &h:host.dumpState()["hotkeys"].toArray())QVERIFY(h.toObject()["action"]!="translate");
    }
    void topmostTranslationDoesNotCoverSettings() {
        AppHost host;host.start();QImage image(320,140,QImage::Format_RGB32);image.fill(Qt::white);host.translateImage(image,{20,20,160,70});
        TranslationWindow *window=nullptr;for(auto *w:QApplication::topLevelWidgets())if(auto *t=qobject_cast<TranslationWindow *>(w))window=t;
        QVERIFY(window);window->setWindowFlag(Qt::WindowStaysOnTopHint,true);window->show();
        host.showTranslationSettings();
        const bool hidden=!window->isVisible();
        window->show();host.showTranslationSettings();
        const bool reentryHidden=!window->isVisible();
        for(auto *w:QApplication::topLevelWidgets())if(auto *d=qobject_cast<SettingsDialog *>(w))d->reject();
        QVERIFY(hidden);QVERIFY(reentryHidden);QVERIFY(window->isVisible());QVERIFY(window->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
    }
};
QTEST_MAIN(TranslationTests)
#include "tst_translation.moc"
