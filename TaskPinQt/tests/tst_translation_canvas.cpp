#include "ui/TranslationCanvas.h"
#include <QPainter>
#include <QtTest>
#include <limits>

using namespace Translation;

namespace {
Block paragraph(const QString &id, const QRectF &rect, const QString &kind = "paragraph")
{
    Block block;
    block.id = id;
    block.kind = kind;
    block.source = "Original text";
    block.rect = rect;
    return block;
}

QImage fixture(const QColor &background = Qt::white)
{
    QImage image(320, 160, QImage::Format_ARGB32_Premultiplied);
    image.fill(background);
    QPainter painter(&image);
    painter.fillRect(QRect(175, 59, 12, 7), Qt::red);
    painter.fillRect(QRect(230, 100, 50, 20), QColor("#28a745"));
    return image;
}

bool preservesOutside(const QImage &before, const QImage &after, const QRect &allowed)
{
    for (int y = 0; y < before.height(); ++y)
        for (int x = 0; x < before.width(); ++x)
            if (!allowed.contains(x, y) && before.pixelColor(x, y) != after.pixelColor(x, y))
                return false;
    return true;
}

int inkBandsBelow(const QImage &image, int top)
{
    int bands = 0;
    bool previousInk = false;
    for (int y = top; y < image.height(); ++y) {
        bool ink = false;
        for (int x = 10; x < image.width() - 10 && !ink; ++x)
            ink = qGray(image.pixel(x, y)) < 160;
        if (ink && !previousInk)
            ++bands;
        previousInk = ink;
    }
    return bands;
}

int rightmostInk(const QImage &image, const QRect &rect)
{
    int right = -1;
    for (int y = rect.top(); y <= rect.bottom(); ++y)
        for (int x = rect.left(); x <= rect.right(); ++x)
            if (qGray(image.pixel(x, y)) < 160)
                right = qMax(right, x);
    return right;
}
}

class TranslationCanvasTests : public QObject {
    Q_OBJECT
private slots:
    void emptyContentIsSafe()
    {
        TranslationCanvas canvas;
        canvas.setContent({}, {}, {});
        QVERIFY(canvas.renderedImage().isNull());
        QCOMPARE(canvas.size(), QSize(0, 0));
    }

    void originalIsAnIndependentPhysicalPixelImage()
    {
        auto input = fixture();
        input.setDevicePixelRatio(2);
        const auto before = input.copy();
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {30, 30, 170, 44})}, {{"a", "OK", "ok", {}}});
        QCOMPARE(input, before);
        QCOMPARE(input.devicePixelRatio(), qreal(2));
        canvas.setOriginal(true);
        auto expected = before.copy();
        expected.setDevicePixelRatio(1);
        QCOMPARE(canvas.renderedImage(), expected);
        QCOMPARE(canvas.size(), QSize(320, 160));
        input.fill(Qt::cyan);
        auto exported = canvas.renderedImage();
        exported.fill(Qt::yellow);
        QCOMPARE(canvas.renderedImage(), expected);
        canvas.setOriginal(false);
        QVERIFY(canvas.renderedImage() != expected);
        QCOMPARE(canvas.renderedImage().devicePixelRatio(), qreal(1));
    }

    void successfulBlockReplacesTextInsideItsBounds_data()
    {
        QTest::addColumn<QColor>("background");
        QTest::newRow("white") << QColor(Qt::white);
        QTest::newRow("dark") << QColor("#203040");
    }

    void successfulBlockReplacesTextInsideItsBounds()
    {
        QFETCH(QColor, background);
        const auto input = fixture(background);
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {30, 30, 170, 44})}, {{"a", "OK", "ok", {}}});
        const auto output = canvas.renderedImage();
        QCOMPARE(output.size(), input.size());
        QCOMPARE(output.pixelColor(180, 62), background);
        QVERIFY(output.copy(QRect(30, 30, 170, 44)) != input.copy(QRect(30, 30, 170, 44)));
        QVERIFY(preservesOutside(input, output, {30, 30, 170, 44}));
        int contrastingPixels = 0;
        for (int y = 32; y < 57; ++y)
            for (int x = 32; x < 100; ++x)
                if (qAbs(qGray(output.pixel(x, y)) - qGray(background.rgb())) > 100)
                    ++contrastingPixels;
        QVERIFY(contrastingPixels > 10);
    }

    void borderMedianIgnoresSparseForeground()
    {
        const QColor background("#203040");
        auto input = fixture(background);
        QPainter painter(&input);
        painter.fillRect(QRect(30, 30, 20, 1), Qt::red);
        painter.end();
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {30, 30, 170, 44})}, {{"a", "OK", "ok", {}}});
        QCOMPARE(canvas.renderedImage().pixelColor(180, 62), background);
        QCOMPARE(canvas.renderedImage().pixelColor(31, 30), background);
    }

    void failedPendingCodeAndFormulaRemainOriginal()
    {
        const auto input = fixture();
        const QList<Block> blocks = {
            paragraph("failed", {0, 0, 100, 30}),
            paragraph("pending", {0, 30, 100, 30}),
            paragraph("code", {160, 40, 100, 40}, "code"),
            paragraph("formula", {160, 90, 140, 40}, "formula"),
            paragraph("empty", {0, 90, 100, 40})
        };
        TranslationCanvas canvas;
        canvas.setContent(input, blocks, {{"failed", "wrong", "failed", {}},
            {"code", "wrong", "ok", {}}, {"formula", "wrong", "ok", {}}, {"empty", {}, "ok", {}}});
        QCOMPARE(canvas.renderedImage(), input);
    }

    void resultsUseIdsAndClampPhysicalCoordinates()
    {
        auto input = fixture();
        QPainter painter(&input);
        painter.fillRect(QRect(50, 30, 10, 6), Qt::red);
        painter.end();
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {-10, 10, 80, 40}),
            paragraph("b", {100, 10, 100, 60}),
            paragraph("invalid", {std::numeric_limits<qreal>::quiet_NaN(), 0, 20, 20})},
            {{"b", "failed text", "failed", {}}, {"a", "OK", "ok", {}}, {"invalid", "bad", "ok", {}}});
        const auto output = canvas.renderedImage();
        QCOMPARE(output.pixelColor(55, 33), QColor(Qt::white));
        QCOMPARE(output.copy(QRect(100, 10, 100, 60)), input.copy(QRect(100, 10, 100, 60)));
        QVERIFY(preservesOutside(input, output, {0, 10, 70, 40}));
    }

    void longTranslationCompletesBelowImageAndPreservesNeighbor()
    {
        const auto input = fixture();
        QStringList lines;
        for (int i = 0; i < 40; ++i)
            lines.append(QString("Complete translated line %1").arg(i, 2, 10, QChar('0')));
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {20, 15, 200, 18}),
            paragraph("code", {20, 40, 280, 100}, "code")}, {{"a", lines.join('\n'), "ok", {}}});
        const auto output = canvas.renderedImage();
        QCOMPARE(output.width(), 320);
        QVERIFY(output.height() >= 160 + 40 * 11);
        QCOMPARE(output.copy(QRect(0, 40, 320, 120)), input.copy(QRect(0, 40, 320, 120)));
        QVERIFY(inkBandsBelow(output, 170) >= 40);
        QVERIFY(output.copy(QRect(20, 15, 200, 18)) != input.copy(QRect(20, 15, 200, 18)));
    }

    void overlappingCodeIsProtected()
    {
        const auto input = fixture();
        const QRect codeRect(140, 50, 100, 20);
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {30, 30, 170, 44}), paragraph("code", codeRect, "code")},
            {{"a", "Translation", "ok", {}}, {"code", "must not draw", "ok", {}}});
        const auto output = canvas.renderedImage();
        QCOMPARE(output.copy(codeRect), input.copy(codeRect));
        QVERIFY(output.height() > input.height());
    }

    void markupIsRenderedAsLiteralText()
    {
        QImage input(320, 80, QImage::Format_ARGB32_Premultiplied);
        input.fill(Qt::white);
        TranslationCanvas plain, markup;
        const auto blocks = QList<Block>{paragraph("a", {10, 10, 300, 60})};
        plain.setContent(input, blocks, {{"a", "X", "ok", {}}});
        markup.setContent(input, blocks, {{"a", "<b>X</b>", "ok", {}}});
        QCOMPARE(markup.renderedImage().size(), input.size());
        QVERIFY(rightmostInk(markup.renderedImage(), {10, 10, 300, 60}) >
                rightmostInk(plain.renderedImage(), {10, 10, 300, 60}) + 30);
    }

    void mockIsVisiblyMarked()
    {
        const auto input = fixture();
        TranslationCanvas real, mock;
        const auto blocks = QList<Block>{paragraph("a", {30, 30, 170, 44})};
        real.setContent(input, blocks, {{"a", "OK", "ok", {}}});
        mock.setContent(input, blocks, {{"a", "OK", "mock", {}}});
        QVERIFY(mock.renderedImage() != real.renderedImage());
    }

    void zoomAndOriginalModeUseCurrentCachedImageSize()
    {
        const auto input = fixture();
        TranslationCanvas canvas;
        canvas.setContent(input, {paragraph("a", {10, 10, 60, 12})},
            {{"a", QString("A long translated sentence. ").repeated(30), "ok", {}}});
        const auto translated = canvas.renderedImage();
        QVERIFY(translated.height() > input.height());
        const auto cacheKey = translated.cacheKey();
        canvas.setZoom(1.5);
        QCOMPARE(canvas.zoom(), 1.5);
        QCOMPARE(canvas.size(), QSize(480, qRound(translated.height() * 1.5)));
        QCOMPARE(canvas.renderedImage().cacheKey(), cacheKey);
        canvas.setZoom(0);
        canvas.setZoom(std::numeric_limits<double>::infinity());
        QCOMPARE(canvas.zoom(), 1.5);
        canvas.setOriginal(true);
        QVERIFY(canvas.original());
        QCOMPARE(canvas.size(), QSize(480, 240));
        canvas.setZoom(0.5);
        QCOMPARE(canvas.size(), QSize(160, 80));
        canvas.setOriginal(false);
        QCOMPARE(canvas.renderedImage().cacheKey(), cacheKey);
        QCOMPARE(canvas.size(), QSize(160, qRound(translated.height() * 0.5)));
        canvas.setZoom(1000000);
        QCOMPARE(canvas.zoom(), 4.0);
        QCOMPARE(canvas.size(), QSize(1280, translated.height() * 4));
    }

    void excessiveCalloutsUseVisibleNoticeWithinPixelBudget_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("32-long-blocks") << 32;
        QTest::newRow("128-long-blocks") << 128;
    }

    void excessiveCalloutsUseVisibleNoticeWithinPixelBudget()
    {
        QFETCH(int, count);
        QImage input(4096, 160, QImage::Format_ARGB32_Premultiplied);
        input.fill(Qt::white);
        QList<Block> blocks;
        QList<Result> results;
        const auto text = QString("Full translated text remains available in the comparison view. ").repeated(195);
        for (int i = 0; i < count; ++i) {
            const auto id = QString::number(i);
            blocks.append(paragraph(id, {qreal(4 + i % 32 * 120), qreal(4 + i / 32 * 30), 110, 20}));
            results.append({id, text, "ok", {}});
        }
        TranslationCanvas canvas;
        canvas.setContent(input, blocks, results);
        const auto output = canvas.renderedImage();
        QVERIFY(!output.isNull());
        QVERIFY(qint64(output.width()) * output.height() <= 32000000);
        QVERIFY(output.height() > input.height());
        QVERIFY(output.height() < input.height() + 500);
        QVERIFY(inkBandsBelow(output, input.height() + 10) > 0);
        canvas.setOriginal(true);
        QCOMPARE(canvas.renderedImage(), input);
    }

    void displayedPixelsMatchCachedExport()
    {
        TranslationCanvas canvas;
        canvas.setContent(fixture(), {paragraph("a", {30, 30, 170, 44})}, {{"a", "OK", "ok", {}}});
        const auto expected = canvas.renderedImage();
        const auto cacheKey = expected.cacheKey();
        QImage displayed(canvas.size(), QImage::Format_ARGB32_Premultiplied);
        displayed.fill(Qt::transparent);
        canvas.render(&displayed);
        QCOMPARE(displayed, expected);
        QCOMPARE(canvas.renderedImage().cacheKey(), cacheKey);
    }
};

QTEST_MAIN(TranslationCanvasTests)
#include "tst_translation_canvas.moc"
