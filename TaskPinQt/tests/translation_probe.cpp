#include "translation/TranslationOcr.h"
#include "translation/TranslationText.h"
#include "translation/TranslationClient.h"
#include "ui/TranslationWindow.h"
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
    if(args.size()!=2)return 2;
    const QString output=QDir(args[1]).absolutePath();QDir().mkpath(output);
    qputenv("TASKPIN_DATA",(output+"/isolated-data").toUtf8());qputenv("TASKPIN_AUTOMATION","1");
    const int fontId=QFontDatabase::addApplicationFont(qEnvironmentVariable("TASKPIN_TEST_FONT","C:/Windows/Fonts/msyh.ttc"));
    if(fontId<0)return 3;
    Translation::Config config;config.mode="mock";QString error;if(!config.save(&error))return 4;
    QImage image(1100,280,QImage::Format_RGB32);image.fill(Qt::white);QPainter painter(&image);
    QFont font(QFontDatabase::applicationFontFamilies(fontId).first());font.setPixelSize(30);painter.setFont(font);painter.setPen(Qt::black);
    painter.drawText(24,55,QStringLiteral("The operator segments regions by a watershed."));
    painter.drawText(24,120,QStringLiteral("inner_circle (Region, Row, Column, Radius)"));
    painter.drawText(24,185,QStringLiteral("max(W-B1, W-B2) < Threshold"));painter.end();
    TranslationWindow window;window.setAttribute(Qt::WA_DeleteOnClose,false);
    window.setCapture(image,{0,0,image.width(),image.height()});
    QElapsedTimer elapsed;elapsed.start();
    QTimer poll;
    QObject::connect(&poll,&QTimer::timeout,&app,[&] {
        auto *source=window.findChild<QPlainTextEdit *>("translationSource");auto *target=window.findChild<QPlainTextEdit *>("translationTarget");
        if(window.state()=="FAILED" || elapsed.elapsed()>35000){app.exit(5);return;}
        if(window.state()!="PRESENTING" || !source || source->toPlainText().isEmpty() || target->toPlainText().isEmpty())return;
        poll.stop();window.resize(980,680);window.grab().save(output+"/translation-window-mock.png");
        QFile report(output+"/native-probe.json");if(!report.open(QIODevice::WriteOnly)){app.exit(6);return;}
        report.write(QJsonDocument(QJsonObject{{"fixtureOnly",true},{"cloudCalled",false},{"elapsedMs",double(elapsed.elapsed())},
            {"ocr",source->toPlainText()},{"mockResult",target->toPlainText()},{"state",window.state()},
            {"systemOcrConfidence","unavailable"}}).toJson(QJsonDocument::Indented));report.close();
        window.cancel();window.close();app.exit(0);
    });poll.start(50);
    return app.exec();
}
