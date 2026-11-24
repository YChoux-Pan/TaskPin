#include "ui/SnipOverlay.h"
#include "core/InstanceNames.h"
#include "core/Settings.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QPainter>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

namespace
{
ScreenCapture fixture(qreal dpr = 1, QPoint origin = {}, QSize size = { 800, 600 })
{
    QImage pixels(QSize(qRound(size.width() * dpr), qRound(size.height() * dpr)), QImage::Format_RGB32);
    pixels.fill(Qt::white);
    QPainter p(&pixels);
    p.fillRect(QRect(0, 0, pixels.width() / 2, pixels.height() / 2), Qt::red);
    p.end();
    // ScreenCapture stores raw device pixels (pixmap DPR=1), separately from shot.dpr.
    QPixmap frame = QPixmap::fromImage(pixels);
    return { frame, QRect(origin, size), dpr, QStringLiteral("synthetic") };
}
void ready(SnipOverlay &w, QRect region = { 40, 40, 300, 200 })
{
    w.setAttribute(Qt::WA_DeleteOnClose, false);
    w.setPreselect(region);
    if (!taskPinAutomation())
        QTest::ignoreMessage(QtWarningMsg, "This plugin does not support raise()");
    w.show();
    QCoreApplication::processEvents();
    w.repaint();
}
void drag(QWidget &w, QPoint from, QPoint to)
{
    QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, from);
    QMouseEvent move(QEvent::MouseMove, to, w.mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&w, &move);
    QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, to);
    w.repaint();
}
void rectangle(SnipOverlay &w)
{
    QTest::keyClick(&w, Qt::Key_R);
    drag(w, { 80, 80 }, { 160, 140 });
    QTest::keyClick(&w, Qt::Key_V);
}
QImage output(SnipOverlay &w)
{
    QSignalSpy spy(&w, &SnipOverlay::pinned);
    QTest::keyClick(&w, Qt::Key_P, Qt::ControlModifier);
    return spy.isEmpty() ? QImage() : qvariant_cast<QImage>(spy.first().first());
}
SnipAction action(SnipOverlay &w, const QString &id)
{
    w.repaint();
    for (const auto &a : w.toolbarActions())
        if (a.id == id) return a;
    return {};
}
void clickAction(SnipOverlay &w, const QString &id)
{
    const auto a = action(w, id);
    QVERIFY2(!a.rect.isEmpty(), qPrintable(id));
    QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, a.rect.center());
    w.repaint();
}
void inputChinese(QLineEdit *edit, const QString &text)
{
    QInputMethodEvent commit;
    commit.setCommitString(text);
    QApplication::sendEvent(edit, &commit);
}
}
class ScreenshotTests : public QObject
{
    Q_OBJECT
    QTemporaryDir data;
private slots:
    void initTestCase()
    {
        QVERIFY(data.isValid());
        qputenv("TASKPIN_DATA", data.path().toUtf8());
        const QString fontPath = qEnvironmentVariable("TASKPIN_TEST_FONT", "C:/Windows/Fonts/msyh.ttc");
        const int id = QFontDatabase::addApplicationFont(fontPath);
        QVERIFY2(id >= 0, qPrintable(QStringLiteral("Cannot load Chinese test font: %1").arg(fontPath)));
        QApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first()));
    }
    void interiorMovesSelection()
    {
        SnipOverlay w(fixture());
        ready(w);
        drag(w, { 200, 180 }, { 230, 200 });
        QCOMPARE(w.selection(), QRect(70, 60, 300, 200));
    }
    void regionSelectionOwnsCrosshair()
    {
        SnipOverlay w(fixture());
        ready(w, {});
        QCOMPARE(w.cursor().shape(), Qt::CrossCursor);
        drag(w, { 40, 40 }, { 339, 239 });
        QCOMPARE(w.cursor().shape(), Qt::ArrowCursor);
        clickAction(w, "tool:pen");
        QCOMPARE(w.cursor().shape(), Qt::ArrowCursor);
        clickAction(w, "tool:select");
        QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, { 20, 20 });
        QCOMPARE(w.cursor().shape(), Qt::CrossCursor);
        QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, { 320, 220 });
        QCOMPARE(w.cursor().shape(), Qt::ArrowCursor);
    }
    void textFormattingUpdatesCurrentDraftAndExport()
    {
        SnipOverlay w(fixture());
        ready(w, { 430, 350, 300, 130 });
        clickAction(w, "tool:text");
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 450, 370 });
        auto *edit = w.findChild<QLineEdit *>();
        QVERIFY(edit);
        inputChinese(edit, QStringLiteral("当前文字"));
        QVERIFY(action(w, "font-value").rect.isValid());
        QCOMPARE(action(w, "font-").rect.top(), action(w, "font+").rect.top());
        const int initial = edit->font().pixelSize();
        clickAction(w, "font+");
        QCOMPARE(edit->font().pixelSize(), initial + 2);
        QVERIFY(edit->styleSheet().contains(QStringLiteral("font-size: 20px")));
        clickAction(w, "font-");
        QCOMPARE(edit->font().pixelSize(), initial);
        const auto initialColor = edit->palette().color(QPalette::Text);
        clickAction(w, "swatch:2");
        QVERIFY(edit->palette().color(QPalette::Text) != initialColor);
        clickAction(w, "text-background");
        QCOMPARE(edit->palette().color(QPalette::Base).alpha(), 0);
        QCOMPARE(edit->text(), QStringLiteral("当前文字"));
        QCoreApplication::processEvents();
        QVERIFY(edit->hasFocus());
        const QImage rendered = output(w);
        QVERIFY(!rendered.isNull());
        QCOMPARE(rendered.pixelColor(290, 120), QColor(Qt::white));
        int whitePixels = 0, greenPixels = 0;
        for(int y = 0; y < rendered.height(); ++y)
            for(int x = 0; x < rendered.width(); ++x) {
                const QColor pixel = rendered.pixelColor(x, y);
                whitePixels += pixel == QColor(Qt::white);
                greenPixels += pixel == QColor(0x3E, 0xC4, 0x6B);
            }
        QVERIFY(whitePixels > rendered.width() * rendered.height() * .90);
        QVERIFY(greenPixels > 0);
    }
    void configuredPinExportsAnnotatedSelection_data()
    {
        QTest::addColumn<QString>("chord");
        QTest::addColumn<int>("key");
        QTest::addColumn<Qt::KeyboardModifiers>("modifiers");
        QTest::newRow("default-F3") << QString("F3") << int(Qt::Key_F3) << Qt::KeyboardModifiers{};
        QTest::newRow("custom") << QString("Ctrl+Shift+F6") << int(Qt::Key_F6)
            << Qt::KeyboardModifiers(Qt::ControlModifier | Qt::ShiftModifier);
    }
    void configuredPinExportsAnnotatedSelection()
    {
        QFETCH(QString, chord); QFETCH(int, key); QFETCH(Qt::KeyboardModifiers, modifiers);
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; });
        Settings::instance().setChord("pin", chord);
        SnipOverlay w(fixture(1.5, {-800, 100}));
        ready(w, {-760, 140, 300, 200}); rectangle(w);
        QCOMPARE(action(w, "pin").shortcut, chord);
        QSignalSpy pinned(&w, &SnipOverlay::pinned), captured(&w, &SnipOverlay::captured);
        QApplication::clipboard()->setText("keep-clipboard");
        QTest::keyClick(&w, Qt::Key(key), modifiers);
        QCOMPARE(pinned.count(), 1); QCOMPARE(captured.count(), 1);
        const QImage image = qvariant_cast<QImage>(pinned.first().first());
        QCOMPARE(image.size(), QSize(450, 300));
        QVERIFY(image.pixelColor(90, 60) != QColor(Qt::red));
        QCOMPARE(captured.first().at(1).toRect(), QRect(-760, 140, 300, 200));
        QCOMPARE(QApplication::clipboard()->text(), QString("keep-clipboard"));
        QTest::keyClick(&w, Qt::Key(key), modifiers);
        QCOMPARE(pinned.count(), 1);
    }
    void configuredTranslationReplacesDefaultShortcut()
    {
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; });
        Settings::instance().setChord("editorTranslate", "Alt+F8");
        SnipOverlay w(fixture()); ready(w);
        QCOMPARE(action(w, "translate").shortcut, QString("Alt+F8"));
        QSignalSpy translated(&w, &SnipOverlay::translationRequested);
        QTest::keyClick(&w, Qt::Key_T, Qt::ControlModifier);
        QCOMPARE(translated.count(), 0);
        QTest::keyClick(&w, Qt::Key_F8, Qt::AltModifier);
        QCOMPARE(translated.count(), 1);
    }
    void actualPinFallbackAppearsAndWorksInEditor()
    {
        SnipOverlay w(fixture()); ready(w);
        QVERIFY(QMetaObject::invokeMethod(&w, "setPinShortcut", Qt::DirectConnection,
            Q_ARG(QString, QString("Ctrl+Shift+F12"))));
        QCOMPARE(action(w, "pin").shortcut, QString("Ctrl+Shift+F12"));
        QSignalSpy pinned(&w, &SnipOverlay::pinned);
        QTest::keyClick(&w, Qt::Key_F3);
        QCOMPARE(pinned.count(), 0);
        QTest::keyClick(&w, Qt::Key_F12, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(pinned.count(), 1);
    }
    void bareCustomTranslationDoesNotConsumeTextInput()
    {
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; });
        Settings::instance().setChord("editorTranslate", "X");
        SnipOverlay w(fixture()); ready(w);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, {100, 100});
        auto *edit = w.findChild<QLineEdit *>(); QVERIFY(edit);
        QSignalSpy translated(&w, &SnipOverlay::translationRequested);
        QTest::keyClick(edit, Qt::Key_X);
        QCOMPARE(edit->text(), QString("x")); QCOMPARE(translated.count(), 0);
        QTest::keyClick(edit, Qt::Key_Return);
        QTest::keyClick(&w, Qt::Key_X);
        QCOMPARE(translated.count(), 1);
    }
    void configuredOutputCommitsFocusedText_data()
    {
        QTest::addColumn<bool>("translate");
        QTest::newRow("pin") << false;
        QTest::newRow("translation") << true;
    }
    void configuredOutputCommitsFocusedText()
    {
        QFETCH(bool, translate);
        const Settings previous = Settings::instance();
        const auto restore = qScopeGuard([previous] { Settings::instance() = previous; });
        Settings::instance().setChord(translate ? "editorTranslate" : "pin", "Alt+F8");
        SnipOverlay w(fixture()); ready(w);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, {100, 100});
        auto *edit = w.findChild<QLineEdit *>(); QVERIFY(edit);
        edit->setText(QStringLiteral("中文任务"));
        QSignalSpy pinned(&w, &SnipOverlay::pinned), translated(&w, &SnipOverlay::translationRequested);
        QTest::keyClick(edit, Qt::Key_F8, Qt::AltModifier);
        QCOMPARE(translate ? translated.count() : pinned.count(), 1);
        const QImage image = qvariant_cast<QImage>((translate ? translated : pinned).first().first());
        bool hasAnnotation = false;
        for (int y = 60; y < 110; ++y)
            for (int x = 60; x < 150; ++x)
                hasAnnotation |= image.pixelColor(x, y) != QColor(Qt::red);
        QVERIFY(hasAnnotation);
    }
    void translationActionIsAlongsideTaskRecognition() {
        SnipOverlay w(fixture());ready(w);
        const auto translate=action(w,"translate"),ocr=action(w,"ocr");
        QVERIFY(!translate.rect.isEmpty());QCOMPARE(translate.label,QStringLiteral("翻译"));
        QCOMPARE(translate.group,ocr.group);QCOMPARE(translate.shortcut,QString("Ctrl+T"));
    }
    void translationExportsCurrentSelection_data() {
        QTest::addColumn<bool>("toolbar");QTest::addColumn<double>("dpr");
        QTest::newRow("button-100")<<true<<1.;QTest::newRow("shortcut-100")<<false<<1.;
        QTest::newRow("button-125")<<true<<1.25;QTest::newRow("shortcut-125")<<false<<1.25;
        QTest::newRow("button-150")<<true<<1.5;QTest::newRow("shortcut-150")<<false<<1.5;
        QTest::newRow("button-200")<<true<<2.;QTest::newRow("shortcut-200")<<false<<2.;
    }
    void translationExportsCurrentSelection() {
        QFETCH(bool,toolbar);QFETCH(double,dpr);
        SnipOverlay w(fixture(dpr,{-800,100}));ready(w,{-760,140,300,200});rectangle(w);
        QSignalSpy translated(&w,&SnipOverlay::translationRequested),captured(&w,&SnipOverlay::captured),ocr(&w,&SnipOverlay::recognizeRequested);
        QApplication::clipboard()->setText("translation-does-not-copy");
        if(toolbar)clickAction(w,"translate");else QTest::keyClick(&w,Qt::Key_T,Qt::ControlModifier);
        QCOMPARE(translated.size(),1);QCOMPARE(captured.size(),0);QCOMPARE(ocr.size(),0);
        const auto image=qvariant_cast<QImage>(translated[0][0]);QCOMPARE(image.size(),QSize(qRound(300*dpr),qRound(200*dpr)));
        QCOMPARE(translated[0][1].toRect(),QRect(-760,140,300,200));
        QVERIFY(image.pixelColor(qRound(60*dpr),qRound(40*dpr))!=QColor(Qt::red));
        QCOMPARE(QApplication::clipboard()->text(),QString("translation-does-not-copy"));
        QTest::keyClick(&w,Qt::Key_T,Qt::ControlModifier);QCOMPARE(translated.size(),1);
    }
    void doubleClickCompletesOnce()
    {
        SnipOverlay w(fixture());
        ready(w);
        QSignalSpy copied(&w, &SnipOverlay::copied), captured(&w, &SnipOverlay::captured);
        QTest::mouseDClick(&w, Qt::LeftButton, Qt::NoModifier, { 200, 180 });
        QCOMPARE(copied.count(), 1);
        QCOMPARE(captured.count(), 1);
    }
    void deleteUndoRestoresPixels()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 80 });
        QTest::keyClick(&w, Qt::Key_Delete);
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        const QImage image = output(w);
        QVERIFY(!image.isNull());
        QVERIFY(image.pixelColor(60, 40) != QColor(Qt::red));
    }
    void moveUndoRestoresPixels()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        drag(w, { 100, 80 }, { 120, 100 });
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        const QImage image = output(w);
        QVERIFY(image.pixelColor(60, 40) != QColor(Qt::red));
        QCOMPARE(image.pixelColor(140, 120), QColor(Qt::red));
    }
    void narrowToolbarFits()
    {
        SnipOverlay w(fixture(1, {}, { 360, 240 }));
        ready(w, { 30, 20, 200, 80 });
        QVERIFY(w.rect().contains(w.toolbarRect()));
    }
    void toolbarAbsentWhileSelecting()
    {
        SnipOverlay w(fixture());
        w.setAttribute(Qt::WA_DeleteOnClose, false);
        if (!taskPinAutomation())
            QTest::ignoreMessage(QtWarningMsg, "This plugin does not support raise()");
        w.show();
        QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, { 40, 40 });
        QMouseEvent move(QEvent::MouseMove, QPointF(200, 180), QPointF(200, 180), Qt::NoButton,
            Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&w, &move);
        w.repaint();
        QVERIFY(w.toolbarRect().isEmpty());
    }
    void outputCommitsChineseText()
    {
        SnipOverlay w(fixture());
        ready(w);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 110 });
        auto *edit = w.findChild<QLineEdit *>();
        QVERIFY(edit);
        inputChinese(edit, QStringLiteral("中文任务"));
        const QImage image = output(w);
        int glyphPixels = 0;
        for (int y = 65; y < 100; ++y)
            for (int x = 60; x < 180; ++x)
                glyphPixels += image.pixelColor(x, y) == QColor(229, 59, 59);
        QVERIFY(glyphPixels > 10);
    }
    void undoInitiallyDisabled()
    {
        SnipOverlay w(fixture());
        ready(w);
        QVERIFY(!action(w, "undo").enabled);
        QVERIFY(!action(w, "redo").enabled);
        rectangle(w);
        QVERIFY(action(w, "undo").enabled);
    }
    void recolorUndoRestoresPixels()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 80 });
        clickAction(w, "swatch:3");
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(output(w).pixelColor(60, 40), QColor(229, 59, 59));
    }
    void recolorRedoRestoresPixels()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 80 });
        clickAction(w, "swatch:3");
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(output(w).pixelColor(60, 40), QColor(59, 156, 255));
    }
    void redoInvalidatedByDelete()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        QTest::keyClick(&w, Qt::Key_N);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 220, 100 });
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&w, Qt::Key_V);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 80 });
        QTest::keyClick(&w, Qt::Key_Delete);
        QVERIFY(!action(w, "redo").enabled);
        QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
        QCOMPARE(output(w).pixelColor(60, 40), QColor(Qt::red));
    }
    void eraseGestureIsOneUndo()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        QTest::keyClick(&w, Qt::Key_R);
        drag(w, { 220, 80 }, { 280, 140 });
        QTest::keyClick(&w, Qt::Key_E);
        drag(w, { 100, 80 }, { 240, 80 });
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        const auto image = output(w);
        QCOMPARE(image.pixelColor(60, 40), QColor(229, 59, 59));
        QCOMPARE(image.pixelColor(200, 40), QColor(229, 59, 59));
    }
    void moveRedoIsOneGesture()
    {
        SnipOverlay w(fixture());
        ready(w);
        rectangle(w);
        drag(w, { 100, 80 }, { 120, 100 });
        QTest::keyClick(&w, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(&w, Qt::Key_Y, Qt::ControlModifier);
        const auto image = output(w);
        QCOMPARE(image.pixelColor(60, 40), QColor(Qt::red));
        QCOMPARE(image.pixelColor(80, 60), QColor(229, 59, 59));
    }
    void newSelectionCannotActivateTool()
    {
        SnipOverlay w(fixture());
        ready(w, {});
        QTest::keyClick(&w, Qt::Key_R);
        drag(w, { 40, 40 }, { 200, 180 });
        drag(w, { 100, 100 }, { 120, 120 });
        QCOMPARE(w.selection(), QRect(60, 60, 161, 141));
    }
    void textEscapeCancelsOnlyInput()
    {
        SnipOverlay w(fixture());
        ready(w);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 100 });
        auto *edit = w.findChild<QLineEdit *>();
        QVERIFY(edit);
        edit->setText(QStringLiteral("取消"));
        QTest::keyClick(edit, Qt::Key_Escape);
        QVERIFY(w.isVisible());
        QCOMPARE(output(w).pixelColor(65, 65), QColor(Qt::red));
    }
    void textEnterCommitsWithoutOutput()
    {
        SnipOverlay w(fixture());
        ready(w);
        QSignalSpy captured(&w, &SnipOverlay::captured);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 100 });
        auto *edit = w.findChild<QLineEdit *>();
        QVERIFY(edit);
        edit->setText(QStringLiteral("中文"));
        QTest::keyClick(edit, Qt::Key_Return);
        QCOMPARE(captured.count(), 0);
        QVERIFY(action(w, "undo").enabled);
    }
    void failedSaveKeepsEditorAndReportsReason()
    {
        SnipOverlay w(fixture());
        ready(w);
        QSignalSpy captured(&w, &SnipOverlay::captured);
        QSignalSpy saved(&w, &SnipOverlay::saved);
        QApplication::clipboard()->setText(QStringLiteral("keep clipboard on failure"));
        QVERIFY(!w.saveToPath(data.path() + "/missing/screenshot.png"));
        QVERIFY(w.isVisible());
        QVERIFY(!w.errorMessage().isEmpty());
        QCOMPARE(captured.count(), 0);
        QCOMPARE(saved.count(), 0);
        QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("keep clipboard on failure"));
        QVERIFY(w.saveToPath(data.path() + "/screenshot.png"));
        QCOMPARE(captured.count(), 1);
        QVERIFY(!w.saveToPath(data.path() + "/duplicate.png"));
        QCOMPARE(captured.count(), 1);
    }
    void successfulSaveCopiesAbsoluteNativePath()
    {
        SnipOverlay w(fixture());
        ready(w);
        const QString directory = data.path() + QStringLiteral("/截图 空格");
        QVERIFY(QDir().mkpath(directory));
        const QString path = directory + QStringLiteral("/文件.png");
        QSignalSpy saved(&w, &SnipOverlay::saved);
        QString clipboardAtSignal;
        connect(&w, &SnipOverlay::saved, &w, [&] { clipboardAtSignal = QApplication::clipboard()->text(); });
        QApplication::clipboard()->setText(QStringLiteral("old clipboard"));
        const QString relativePath = QDir::current().relativeFilePath(path);
        QVERIFY(w.saveToPath(relativePath));
        const QImage image(path);
        QCOMPARE(image.size(), QSize(300, 200));
        QCOMPARE(image.pixelColor(10, 10), QColor(Qt::red));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
        QCOMPARE(clipboardAtSignal, QDir::toNativeSeparators(path));
        QVERIFY(QFileInfo(QApplication::clipboard()->text()).isAbsolute());
        QVERIFY(!w.isVisible());
    }
    void quickSaveCopiesFinalUniquePath()
    {
        auto &settings = Settings::instance();
        const QString oldDirectory = settings.autoSaveDir, oldPattern = settings.namePattern;
        const auto restore = qScopeGuard([&] {
            settings.autoSaveDir = oldDirectory;
            settings.namePattern = oldPattern;
        });
        settings.autoSaveDir = data.path() + "/quick-save";
        settings.namePattern = QStringLiteral("capture");
        QVERIFY(QDir().mkpath(settings.autoSaveDir));
        QImage existing(2, 2, QImage::Format_RGB32);
        existing.fill(Qt::blue);
        QVERIFY(existing.save(settings.autoSaveDir + "/capture.png"));
        SnipOverlay w(fixture());
        ready(w);
        QSignalSpy saved(&w, &SnipOverlay::saved);
        QTest::keyClick(&w, Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(saved.count(), 1);
        const QString path = saved.first().first().toString();
        QVERIFY(path != settings.autoSaveDir + "/capture.png");
        QCOMPARE(QApplication::clipboard()->text(), QDir::toNativeSeparators(path));
        QCOMPARE(QImage(path).size(), QSize(300, 200));
        QCOMPARE(QImage(settings.autoSaveDir + "/capture.png").pixelColor(0, 0), QColor(Qt::blue));
    }
    void failedSaveReasonVisibleOnSmallFullScreenSelection()
    {
        SnipOverlay w(fixture(1, {}, { 360, 240 }));
        ready(w, { 0, 0, 360, 240 });
        const QImage before = w.grab().toImage();
        QVERIFY(!w.saveToPath(data.path() + "/missing/screenshot.png"));
        const QImage after = w.grab().toImage();
        const QString artifactDir = qEnvironmentVariable("TASKPIN_TEST_ARTIFACT_DIR");
        if (!artifactDir.isEmpty()) QVERIFY(after.save(artifactDir + "/save-failure-small-screen.png"));
        int visibleTextPixels = 0;
        int visibleBannerPixels = 0;
        for (int y = 0; y < 240; ++y) {
            for (int x = 0; x < 360; ++x) {
                if (w.toolbarRect().contains(x, y) || before.pixel(x,y) == after.pixel(x,y)) continue;
                const QColor pixel = after.pixelColor(x,y);
                visibleTextPixels += pixel.red() > 230 && pixel.green() > 230 && pixel.blue() > 230;
                visibleBannerPixels += pixel.red() > 100 && pixel.green() < 90 && pixel.blue() < 90;
            }
        }
        QVERIFY(visibleBannerPixels > 500);
        QVERIFY2(visibleTextPixels > 30, "Save failure needs readable glyphs clear of the toolbar");
        QVERIFY(w.isVisible());
    }
    void ocrCapturesExactlyOnce()
    {
        SnipOverlay w(fixture());
        ready(w);
        QSignalSpy output(&w, &SnipOverlay::recognizeRequested), captured(&w, &SnipOverlay::captured);
        QTest::keyClick(&w, Qt::Key_R, Qt::ControlModifier);
        QTest::keyClick(&w, Qt::Key_R, Qt::ControlModifier);
        QCOMPARE(output.count(), 1);
        QCOMPARE(captured.count(), 1);
    }
    void toolbarAbsentDuringResize()
    {
        SnipOverlay w(fixture());
        ready(w);
        QTest::mousePress(&w, Qt::LeftButton, Qt::NoModifier, { 339, 239 });
        w.repaint();
        QVERIFY(w.toolbarRect().isEmpty());
        QTest::mouseRelease(&w, Qt::LeftButton, Qt::NoModifier, { 359, 259 });
        QCOMPARE(w.selection(), QRect(40, 40, 320, 220));
    }
    void toolOutsideSelectionDoesNothing()
    {
        SnipOverlay w(fixture());
        ready(w);
        QTest::keyClick(&w, Qt::Key_R);
        drag(w, { 500, 400 }, { 600, 500 });
        QVERIFY(!action(w, "undo").enabled);
    }
    void everyNarrowActionAccessible()
    {
        SnipOverlay w(fixture(1, {}, { 360, 240 }));
        ready(w, { 30, 20, 200, 80 });
        QTest::keyClick(&w, Qt::Key_T);
        w.repaint();
        for (const auto &a : w.toolbarActions()) {
            QVERIFY2(w.rect().contains(a.rect), qPrintable(a.id));
            QVERIFY(!a.label.isEmpty());
        }
        QVERIFY(!action(w, "font+").rect.isEmpty());
        QVERIFY(!action(w, "more").rect.isEmpty());
        const QString dir = qEnvironmentVariable("TASKPIN_TEST_ARTIFACT_DIR");
        if (!dir.isEmpty()) QVERIFY(w.grab().save(dir + "/narrow-toolbar.png"));
    }
    void textBoundsFollowRenderedBaseline()
    {
        Annotation a;
        a.kind = Annotation::Text;
        a.pos = { 30, 100 };
        a.fontSize = 24;
        a.text = QStringLiteral("中文");
        QVERIFY(a.bounds().top() < 90);
        QVERIFY(a.bounds().bottom() < 115);
        QImage image(180, 140, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter p(&image);
        a.paint(p);
        p.end();
        for (int y = 0; y < 140; ++y)
            for (int x = 0; x < 180; ++x)
                if (image.pixelColor(x, y) != QColor(Qt::white)) QVERIFY(a.bounds().contains(x, y));
    }
    void focusedTextOutputShortcut()
    {
        SnipOverlay w(fixture());
        ready(w);
        QTest::keyClick(&w, Qt::Key_T);
        QTest::mouseClick(&w, Qt::LeftButton, Qt::NoModifier, { 100, 100 });
        auto *edit = w.findChild<QLineEdit *>();
        QVERIFY(edit);
        edit->setText(QStringLiteral("中文任务"));
        QSignalSpy captured(&w, &SnipOverlay::captured);
        QTest::keyClick(edit, Qt::Key_P, Qt::ControlModifier);
        QCOMPARE(captured.count(), 1);
    }
    void invalidSelectionClearsToolbar()
    {
        SnipOverlay w(fixture());
        ready(w);
        QVERIFY(!w.toolbarRect().isEmpty());
        w.setPreselect({});
        w.repaint();
        QVERIFY(w.toolbarRect().isEmpty());
        QVERIFY(w.toolbarActions().isEmpty());
    }
    void toolbarUsesFreeSideStrip()
    {
        SnipOverlay w(fixture());
        ready(w, { 40, 20, 300, 550 });
        QVERIFY(!w.toolbarRect().intersects(w.selection()));
        QVERIFY(w.rect().contains(w.toolbarRect()));
    }
    void disabledToolbarButtonDoesNotDragSelection()
    {
        SnipOverlay w(fixture(1, {}, { 360, 240 }));
        ready(w, { 30, 20, 200, 180 });
        const auto undo = action(w, "undo");
        QVERIFY(!undo.enabled);
        drag(w, undo.rect.center(), undo.rect.center() + QPoint(12, 12));
        QCOMPARE(w.selection(), QRect(30, 20, 200, 180));
    }
    void frozenDisplay_data() { dpiOutput_data(); }
    void frozenDisplay()
    {
        QFETCH(qreal, dpr);
        SnipOverlay w(fixture(dpr));
        ready(w, { 0, 0, 800, 600 });
        const auto display = w.grab().toImage();
        QCOMPARE(display.pixelColor(200, 100), QColor(Qt::red));
        QCOMPARE(display.pixelColor(450, 100), QColor(Qt::white));
    }
    void edgeSelection_data() { dpiOutput_data(); }
    void edgeSelection()
    {
        QFETCH(qreal, dpr);
        QFETCH(QSize, expectedSize);
        SnipOverlay w(fixture(dpr, { 1200, -600 }));
        ready(w, { 1800, -100, 200, 100 });
        const auto image = output(w);
        QCOMPARE(image.size(), expectedSize);
        QCOMPARE(image.pixelColor(image.width() - 1, image.height() - 1), QColor(Qt::white));
    }
    void dpiOutput_data()
    {
        QTest::addColumn<qreal>("dpr");
        QTest::addColumn<QSize>("expectedSize");
        QTest::addColumn<QPoint>("redPixel");
        QTest::newRow("100") << qreal(1) << QSize(200,100) << QPoint(120,60);
        QTest::newRow("125") << qreal(1.25) << QSize(250,125) << QPoint(150,75);
        QTest::newRow("150") << qreal(1.5) << QSize(300,150) << QPoint(180,90);
        QTest::newRow("200") << qreal(2) << QSize(400,200) << QPoint(240,120);
    }
    void dpiOutput()
    {
        QFETCH(qreal, dpr);
        QFETCH(QSize, expectedSize);
        QFETCH(QPoint, redPixel);
        SnipOverlay w(fixture(dpr, { -800, 120 }));
        ready(w, { -720, 160, 200, 100 });
        QSignalSpy captured(&w, &SnipOverlay::captured);
        const QImage image = output(w);
        QCOMPARE(image.size(), expectedSize);
        QCOMPARE(image.pixelColor(redPixel), QColor(Qt::red));
        QCOMPARE(captured.count(), 1);
        QCOMPARE(captured.first().at(1).toRect(), QRect(-720, 160, 200, 100));
    }
};
QTEST_MAIN(ScreenshotTests)
#include "tst_screenshot.moc"
