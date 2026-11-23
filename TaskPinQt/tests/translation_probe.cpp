#include "translation/TranslationOcr.h"
#include "translation/OcrProfiles.h"
#include "translation/TranslationText.h"
#include "translation/TranslationClient.h"
#include "ui/TranslationWindow.h"
#include "ui/TranslationCanvas.h"
#include "ui/SnipOverlay.h"
#include "core/Settings.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QBuffer>
#include <QProcess>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>
#include <QtTest>

static int benchmark(QApplication &app,const QString &output,const Translation::Config &config,int samples,bool oneShot)
{
    Translation::Ocr ocr;Translation::Client client;QProcess legacy;QImage image;QJsonArray rows;
    QElapsedTimer total,stage;int index=0;double ocrMs=0;Translation::OcrResult observed;
    QList<Translation::Block> blocks;
    std::function<void()> next=[&]{
        if(index==samples*2){
            QFile f(output+"/benchmark.json");if(!f.open(QIODevice::WriteOnly)){app.exit(6);return;}
            f.write(QJsonDocument(QJsonObject{{"fixtureOnly",true},{"cloudCalled",config.mode=="cloud"},
                {"samples",rows},{"ocrMode",oneShot?"one-shot":"shared-worker"},{"clock","QElapsedTimer monotonic"}}).toJson(QJsonDocument::Indented));f.close();
            app.exit(0);return;
        }
        const bool paragraph=index>=samples;
        image=QImage(1100,paragraph?850:280,QImage::Format_RGB32);image.fill(Qt::white);
        QPainter p(&image);QFont font=app.font();font.setPixelSize(27);p.setFont(font);p.setPen(Qt::black);
        const QStringList lines={"The operator segments regions using a watershed.","Keep the original image and review every technical symbol.",
            "The function multiplies two images and clips overflow values.","Each input image must have the same width and height.",
            "Several image pairs can be processed in the same call.","The result preserves the original pixel coordinates.",
            "Use a smaller region when the connection is slow.","Changing settings is applied only after pressing Save.",
            "Cancel keeps the previous configuration intact.","OCR and network translation are measured separately."};
        const int count=paragraph?lines.size():2;
        for(int i=0;i<count;++i)p.drawText(24,55+i*70,lines[i]);p.end();
        total.start();stage.start();
        if(oneShot){
            const auto runtime=Translation::Ocr::runtimeDirectory();
            legacy.setProgram(runtime+"/python.exe");legacy.setWorkingDirectory(runtime);
            legacy.setArguments({"-I","-B",runtime+"/helper.py"});legacy.start();
        }else ocr.recognize(QString::number(index),image);
    };
    QObject::connect(&ocr,&Translation::Ocr::failed,&app,[&](const QString &,const QString &){app.exit(5);});
    QObject::connect(&client,&Translation::Client::failed,&app,[&](const QString &,const QString &){app.exit(5);});
    auto recognized=[&](const Translation::OcrResult &result){
        ocrMs=stage.elapsed();observed=result;blocks=Translation::buildBlocks(result);
        for(auto &b:blocks)b=Translation::protect(b,QString::number(index));
        auto requestConfig=config;requestConfig.useCache=false;stage.restart();
        client.start(QString::number(index),blocks,requestConfig,config.mode=="cloud"?config.apiKey():QString());
    };
    QObject::connect(&ocr,&Translation::Ocr::finished,&app,[&](const QString &,const Translation::OcrResult &result){recognized(result);});
    QObject::connect(&legacy,&QProcess::started,&app,[&]{
        QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
        const auto selection=Translation::OcrProfiles::load();
        QJsonObject payload{{"image",QString::fromLatin1(png.toBase64())}};
        if(selection.profile=="custom")payload.insert("profilePath",selection.profilePath());
        else payload.insert("profileId",selection.profile);
        legacy.write(QJsonDocument(payload).toJson(QJsonDocument::Compact));legacy.closeWriteChannel();
    });
    QObject::connect(&legacy,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&app,[&](int code,QProcess::ExitStatus status){
        Translation::OcrResult result;QString error;
        if(code!=0||status!=QProcess::NormalExit||!Translation::Ocr::decode(legacy.readAllStandardOutput(),image.size(),&result,&error)){app.exit(5);return;}
        recognized(result);
    });
    QObject::connect(&legacy,&QProcess::errorOccurred,&app,[&](QProcess::ProcessError){app.exit(5);});
    QObject::connect(&client,&Translation::Client::finished,&app,[&](const QString &,const QList<Translation::Result> &results){
        const double translateMs=stage.elapsed();stage.restart();
        TranslationCanvas canvas;canvas.setContent(image,blocks,results);const auto rendered=canvas.renderedImage();
        if(rendered.isNull()){app.exit(5);return;}
        bool ok=true;for(const auto &r:results)ok&=r.status=="ok"||r.status=="mock"||r.status=="untranslated";
        if(!ok){app.exit(5);return;}
        rows.append(QJsonObject{{"fixture",index>=samples?"paragraph":"small"},{"sample",index%samples+1},
            {"ocrMs",ocrMs},{"translationMs",translateMs},{"renderMs",double(stage.elapsed())},{"totalMs",double(total.elapsed())},
            {"characters",Translation::sourceText(blocks).size()},{"ocrEngine",observed.engine},{"modelVersion",observed.modelVersion},
            {"ocrStageMs",observed.stageMs},{"workerReused",observed.workerReused},{"profileId",observed.profileId}});
        ++index;QTimer::singleShot(0,&app,next);
    });
    QTimer::singleShot(0,&app,next);QTimer::singleShot(600000,&app,[&]{app.exit(5);});return app.exec();
}

int main(int argc,char **argv)
{
    QApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()<2)return 2;
    const bool cloud=args.contains("--cloud"),hold=args.contains("--hold"),compare=args.contains("--comparison");
    QJsonObject shared;
    if(cloud){QFile f(qEnvironmentVariable("LOCALAPPDATA")+"/TaskPin/TaskPinQt/settings.json");if(!f.open(QIODevice::ReadOnly))return 8;shared=QJsonDocument::fromJson(f.readAll()).object();}
    const QString output=QDir(args[1]).absolutePath();QDir().mkpath(output);
    qputenv("TASKPIN_DATA",(output+"/isolated-data").toUtf8());qputenv("TASKPIN_AUTOMATION","1");
    const int fontId=QFontDatabase::addApplicationFont(qEnvironmentVariable("TASKPIN_TEST_FONT","C:/Windows/Fonts/msyh.ttc"));
    if(fontId<0)return 3;
    app.setFont(QFont(QFontDatabase::applicationFontFamilies(fontId).first()));
    Translation::Config config;config.mode=cloud?"cloud":"mock";
    if(cloud){auto &s=Settings::instance();s.aiBaseUrl=shared["aiBaseUrl"].toString();s.aiModel=shared["aiModel"].toString();s.aiApiKey=shared["aiApiKey"].toString();
        config.endpoint=s.aiBaseUrl;config.model=s.aiModel;config.consentEndpoint=config.requestUrl().toString(QUrl::FullyEncoded);
        if(config.requestUrl()!=QUrl("https://api.deepseek.com/v1/chat/completions") || s.aiApiKey.isEmpty())return 9;}
    QString error;if(!config.save(&error))return 4;
    if(args.contains("--lightweight")) {
        Translation::OcrProfiles profile;profile.profile="builtin-ppocrv4-mobile";
        if(!profile.save(&error))return 14;
    }
    if(args.contains("--benchmark"))return benchmark(app,output,config,args.contains("--single-sample")?1:3,args.contains("--oneshot-ocr"));
    const bool paragraph=args.contains("--paragraph");
    QImage image(1100,paragraph?850:280,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);
    QFont font(QFontDatabase::applicationFontFamilies(fontId).first());font.setPixelSize(30);painter.setFont(font);painter.setPen(Qt::black);
    painter.drawText(24,55,QStringLiteral("The operator segments regions by a watershed."));
    painter.drawText(24,120,QStringLiteral("inner_circle (Region, Row, Column, Radius)"));
    painter.drawText(24,185,QStringLiteral("max(W-B1, W-B2) < Threshold"));
    if(paragraph){
        const QStringList lines={"The function multiplies two images and clips overflow values.",
            "Each input image must have the same width and height.",
            "Several image pairs may be processed in the same call.",
            "The result preserves the original pixel coordinates.",
            "Use a small region when the network connection is slow.",
            "Review the original image before using technical symbols.",
            "Changes to the settings are applied after pressing Save.",
            "Cancel keeps the previous configuration and image intact."};
        for(int i=0;i<lines.size();++i)painter.drawText(24,260+i*65,lines[i]);
    }
    painter.end();QElapsedTimer elapsed;elapsed.start();qint64 ocrMs=-1,translationMs=-1;
    TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
    Translation::OcrResult observedOcr;
    QObject::connect(window.findChild<Translation::Ocr *>(),&Translation::Ocr::finished,&app,[&](const QString &,const Translation::OcrResult &result){observedOcr=result;ocrMs=elapsed.elapsed();});
    QObject::connect(window.findChild<Translation::Client *>(),&Translation::Client::finished,&app,[&](const QString &,const QList<Translation::Result> &){translationMs=elapsed.elapsed()-ocrMs;});
    const QRect sourceRegion(200,200,880,paragraph?680:224);
    SnipOverlay editor({QPixmap::fromImage(image),sourceRegion,1.25,"fixture"});editor.setAttribute(Qt::WA_DeleteOnClose,false);
    QObject::connect(&editor,&SnipOverlay::translationRequested,&window,&TranslationWindow::setCapture);
    editor.setPreselect(sourceRegion);editor.show();QTest::keyClick(&editor,Qt::Key_T,Qt::ControlModifier);
    QTest::keyClick(&window,Qt::Key_Plus);window.move(180,120);
    const QPoint expectedOrigin=window.contentGeometry().topLeft();const qreal expectedZoom=window.zoom();
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&app,[&] {

        if(window.state()=="FAILED" || elapsed.elapsed()>180000){app.exit(5);return;}
        if(window.state()!="PRESENTING" || window.sourceText().isEmpty() || window.targetText().isEmpty())return;
        poll.stop();
        if(window.contentGeometry().topLeft()!=expectedOrigin || window.zoom()!=expectedZoom || !window.windowFlags().testFlag(Qt::FramelessWindowHint)) {app.exit(11);return;}
        window.grab().save(output+(cloud?"/translation-pin-cloud.png":"/translation-pin-mock.png"));
        window.translatedImage().save(output+"/translated-image.png");image.save(output+"/original-fixture.png");
        QFile report(output+"/native-probe.json");if(!report.open(QIODevice::WriteOnly)){app.exit(6);return;}
        QJsonArray confidences;for(const auto &line:observedOcr.lines)confidences.append(line.recognitionConfidence?QJsonValue(*line.recognitionConfidence):QJsonValue());
        const bool defaultComparison=bool(window.findChild<QPlainTextEdit *>("translationSource"));
        QJsonObject comparisonUi;
        if(compare){
            window.showComparison();QCoreApplication::processEvents();
            auto *dialog=window.findChild<QDialog *>("translationComparison");
            auto *source=window.findChild<QPlainTextEdit *>("translationSource");
            auto *target=window.findChild<QPlainTextEdit *>("translationTarget");
            if(!dialog||!source||!target){app.exit(12);return;}
            const auto screenshot=dialog->grab();screenshot.save(output+"/comparison.png");
            comparisonUi={{"opaqueBackground",dialog->windowOpacity()==1. && screenshot.toImage().pixelColor(3,3).alpha()==255},
                {"textHeightRatio",double(qMin(source->height(),target->height()))/dialog->height()},
                {"paneWidthRatio",double(source->width())/target->width()},
                {"width",dialog->width()},{"height",dialog->height()}};
        }
        report.write(QJsonDocument(QJsonObject{{"fixtureOnly",true},{"cloudCalled",cloud},{"elapsedMs",double(elapsed.elapsed())},
            {"fixture",paragraph?"paragraph":"small"},{"ocrMs",double(ocrMs)},{"translationMs",double(translationMs)},
            {"physicalPixels",image.width()*image.height()},{"sourceCharacters",window.sourceText().size()},
            {"ocrStageMs",observedOcr.stageMs},{"workerReused",observedOcr.workerReused},{"profileId",observedOcr.profileId},
            {"ocr",window.sourceText()},{"result",window.targetText()},{"state",window.state()},
            {"ocrEngine",observedOcr.engine},{"ocrModelVersion",observedOcr.modelVersion},{"recognitionScores",confidences},{"editorEntry","F1 toolbar translation"},{"framelessPin",true},{"positionAndZoomPreserved",true},{"comparisonCreated",defaultComparison},{"comparisonUi",comparisonUi}}).toJson(QJsonDocument::Indented));report.close();
        if(!hold){window.cancel();window.close();app.exit(0);}
    });poll.start(50);
    return app.exec();
}
