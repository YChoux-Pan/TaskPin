#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include "translation/TranslationClient.h"
#include "ui/TranslationWindow.h"
#include "ui/SnipOverlay.h"
#include "core/Settings.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QtTest>

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<2)return 2;
    const bool cloud=args.contains("--cloud"),hold=args.contains("--hold");
    QJsonObject shared;
    if(cloud){QFile f(qEnvironmentVariable("LOCALAPPDATA")+"/TaskPin/TaskPinQt/settings.json");if(!f.open(QIODevice::ReadOnly))return 8;shared=QJsonDocument::fromJson(f.readAll()).object();}
    const QString output=QDir(args[1]).absolutePath();QDir().mkpath(output);
    qputenv("TASKPIN_DATA",(output+"/isolated-data").toUtf8());qputenv("TASKPIN_AUTOMATION","1");
    const int fontId=QFontDatabase::addApplicationFont(qEnvironmentVariable("TASKPIN_TEST_FONT","C:/Windows/Fonts/msyh.ttc"));
    if(fontId<0)return 3;
    Translation::Config config;config.mode=cloud?"cloud":"mock";
    if(cloud){auto &s=Settings::instance();s.aiBaseUrl=shared["aiBaseUrl"].toString();s.aiModel=shared["aiModel"].toString();s.aiApiKey=shared["aiApiKey"].toString();
        config.endpoint=s.aiBaseUrl;config.model=s.aiModel;config.consentEndpoint=config.requestUrl().toString(QUrl::FullyEncoded);
        if(config.requestUrl()!=QUrl("https://api.deepseek.com/v1/chat/completions") || s.aiApiKey.isEmpty())return 9;}
    QString error;if(!config.save(&error))return 4;
    QImage image(1100,280,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);
    QFont font(QFontDatabase::applicationFontFamilies(fontId).first());font.setPixelSize(30);painter.setFont(font);painter.setPen(Qt::black);
    painter.drawText(24,55,QStringLiteral("The operator segments regions by a watershed."));
    painter.drawText(24,120,QStringLiteral("inner_circle (Region, Row, Column, Radius)"));
    painter.drawText(24,185,QStringLiteral("max(W-B1, W-B2) < Threshold"));painter.end();
    TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
    Translation::OcrResult observedOcr;
    QObject::connect(window.findChild<Translation::Ocr *>(),&Translation::Ocr::finished,&app,[&](const QString &,const Translation::OcrResult &result){observedOcr=result;});
    const QRect sourceRegion(200,200,880,224);
    SnipOverlay editor({QPixmap::fromImage(image),sourceRegion,1.25,"fixture"});editor.setAttribute(Qt::WA_DeleteOnClose,false);
    QObject::connect(&editor,&SnipOverlay::translationRequested,&window,&TranslationWindow::setCapture);
    editor.setPreselect(sourceRegion);editor.show();QTest::keyClick(&editor,Qt::Key_T,Qt::ControlModifier);
    QTest::keyClick(&window,Qt::Key_Plus);window.move(180,120);
    const QPoint expectedOrigin=window.contentGeometry().topLeft();const qreal expectedZoom=window.zoom();
    QElapsedTimer elapsed;elapsed.start();
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&app,[&] {

        if(window.state()=="FAILED" || elapsed.elapsed()>35000){app.exit(5);return;}
        if(window.state()!="PRESENTING" || window.sourceText().isEmpty() || window.targetText().isEmpty())return;
        poll.stop();
        if(window.contentGeometry().topLeft()!=expectedOrigin || window.zoom()!=expectedZoom || !window.windowFlags().testFlag(Qt::FramelessWindowHint)) {app.exit(11);return;}
        window.grab().save(output+(cloud?"/translation-pin-cloud.png":"/translation-pin-mock.png"));
        window.translatedImage().save(output+"/translated-image.png");image.save(output+"/original-fixture.png");
        QFile report(output+"/native-probe.json");if(!report.open(QIODevice::WriteOnly)){app.exit(6);return;}
        QJsonArray confidences;for(const auto &line:observedOcr.lines)confidences.append(line.recognitionConfidence?QJsonValue(*line.recognitionConfidence):QJsonValue());
        report.write(QJsonDocument(QJsonObject{{"fixtureOnly",true},{"cloudCalled",cloud},{"elapsedMs",double(elapsed.elapsed())},
            {"ocr",window.sourceText()},{"result",window.targetText()},{"state",window.state()},
            {"ocrEngine",observedOcr.engine},{"ocrModelVersion",observedOcr.modelVersion},{"recognitionScores",confidences},{"editorEntry","F1 toolbar translation"},{"framelessPin",true},{"positionAndZoomPreserved",true},{"comparisonCreated",bool(window.findChild<QPlainTextEdit *>("translationSource"))}}).toJson(QJsonDocument::Indented));report.close();
        if(!hold){window.cancel();window.close();app.exit(0);}
    });poll.start(50);
    return app.exec();
}
