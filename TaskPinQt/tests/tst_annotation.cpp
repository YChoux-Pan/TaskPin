#include "ui/Annotation.h"

#include <QFontDatabase>
#include <QGuiApplication>
#include <QPainter>
#include <QtTest>
#include <limits>

namespace {
QImage render(const Annotation &annotation, QColor background = Qt::transparent, int scale = 1)
{
    QImage image(400 * scale, 160 * scale, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(scale);
    image.fill(background);
    QPainter painter(&image);
    annotation.paint(painter);
    painter.end();
    return image;
}
}

class AnnotationTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        const QString fontPath = qEnvironmentVariable("TASKPIN_TEST_FONT", "C:/Windows/Fonts/msyh.ttc");
        const int id = QFontDatabase::addApplicationFont(fontPath);
        QVERIFY2(id >= 0, qPrintable(fontPath));
        QGuiApplication::setFont(QFont(QFontDatabase::applicationFontFamilies(id).first()));
    }

    void sequenceLabels()
    {
        using Format = Annotation::NumberFormat;
        struct Row { Format format; int number; QString expected; };
        const Row rows[] = {
            {Format::Decimal, 4, "4"}, {Format::Padded, 4, "04"}, {Format::Padded, 100, "100"},
            {Format::UpperAlpha, 26, "Z"}, {Format::UpperAlpha, 27, "AA"},
            {Format::LowerAlpha, 703, "aaa"}, {Format::UpperRoman, 49, "XLIX"},
            {Format::LowerRoman, 1994, "mcmxciv"}, {Format::UpperRoman, 4000, "4000"}
        };
        Annotation a;
        for (const auto &row : rows) {
            a.number = row.number;
            a.numberFormat = row.format;
            QCOMPARE(a.numberLabel(), row.expected);
        }
    }

    void cutoutPreservesUnderlyingPixels()
    {
        Annotation a;
        a.kind = Annotation::Number;
        a.number = 8;
        a.width = 10;
        a.pos = {200, 80};
        const QColor source(67, 100, 180);
        const QImage badge = render(a, source, 2);
        a.numberFill = Annotation::NumberFill::Ink;
        const QImage ink = render(a, source, 2);
        a.numberFill = Annotation::NumberFill::Cutout;
        const QImage cutout = render(a, source, 2);
        const QImage transparent = render(a, Qt::transparent, 2);
        int glyphPixels = 0;
        int surroundPixels = 0;
        // Compare interior pixels. Edge coverage may round differently when a path is inverted.
        for (int y = 1; y < badge.height() - 1; ++y) {
            for (int x = 1; x < badge.width() - 1; ++x) {
                const QColor sample = ink.pixelColor(x, y);
                if (ink.pixelColor(x - 1, y) != sample || ink.pixelColor(x + 1, y) != sample
                    || ink.pixelColor(x, y - 1) != sample || ink.pixelColor(x, y + 1) != sample)
                    continue;
                if (sample == a.color) {
                    QCOMPARE(badge.pixelColor(x, y), QColor(Qt::white));
                    QCOMPARE(cutout.pixelColor(x, y), source);
                    QCOMPARE(transparent.pixelColor(x, y).alpha(), 0);
                    ++glyphPixels;
                } else if (badge.pixelColor(x, y) == a.color && sample == source) {
                    QCOMPARE(cutout.pixelColor(x, y), a.color);
                    ++surroundPixels;
                }
            }
        }
        QVERIFY(glyphPixels > 10);
        QVERIFY(surroundPixels > 100);
    }

    void arrowSilhouettesAndSelection()
    {
        Annotation a;
        a.kind = Annotation::Arrow;
        a.width = 8;
        a.points = {{60, 80}, {240, 80}};
        const QImage filled = render(a);
        a.arrowStyle = Annotation::ArrowStyle::Open;
        const QImage open = render(a);
        QVERIFY(open != filled);
        a.arrowStyle = Annotation::ArrowStyle::Double;
        const QImage twoHeads = render(a);
        QVERIFY(twoHeads.pixelColor(86, 90).alpha() > 150);
        QVERIFY(filled.pixelColor(86, 90).alpha() == 0);
        a.arrowStyle = Annotation::ArrowStyle::Tapered;
        const QImage tapered = render(a);
        QVERIFY(tapered != filled && tapered != open && tapered != twoHeads);
        QVERIFY(!a.hitTest({110, 120}));

        for (const auto style : {Annotation::ArrowStyle::Filled, Annotation::ArrowStyle::Open,
                                 Annotation::ArrowStyle::Double, Annotation::ArrowStyle::Tapered}) {
            a.arrowStyle = style;
            a.points = {{62, 135}, {234, 36}};
            const QImage image = render(a);
            const QRect bounds = a.bounds();
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QPoint point(x, y);
                    if (image.pixelColor(point).alpha() > 150) {
                        QVERIFY(bounds.contains(point));
                        QVERIFY(a.hitTest(point));
                    }
                }
            }
        }
    }

    void longNumbersExpandSelectableBadge()
    {
        Annotation a;
        a.kind = Annotation::Number;
        a.pos = {200, 80};
        const int singleDigitWidth = a.bounds().width();
        a.number = std::numeric_limits<int>::max();
        QVERIFY(a.bounds().width() > singleDigitWidth * 2);
        const QImage image = render(a);
        bool foundOuterInk = false;
        for (int x = 0; x < a.pos.x() - singleDigitWidth / 2; ++x) {
            const QPoint point(x, a.pos.y());
            if (image.pixelColor(point).alpha() == 255) {
                QVERIFY(a.bounds().contains(point));
                QVERIFY(a.hitTest(point));
                foundOuterInk = true;
            }
        }
        QVERIFY(foundOuterInk);
    }
};

QTEST_MAIN(AnnotationTests)
#include "tst_annotation.moc"
