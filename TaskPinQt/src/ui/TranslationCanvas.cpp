#include "ui/TranslationCanvas.h"
#include <QAbstractTextDocumentLayout>
#include <QFontDatabase>
#include <QHash>
#include <QPainter>
#include <QRegion>
#include <QTextDocument>
#include <QTextOption>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
constexpr int minimumFontPixels = 11;
constexpr int textPadding = 2;
constexpr int calloutPadding = 12;
constexpr qint64 maximumImagePixels = 32000000;

QRect pixelRect(const QRectF &rect, const QSize &size)
{
    if (!std::isfinite(rect.x()) || !std::isfinite(rect.y()) ||
        !std::isfinite(rect.width()) || !std::isfinite(rect.height()) || rect.isEmpty())
        return {};
    // Intersect before converting to integers, so malformed coordinates cannot overflow.
    return rect.intersected(QRectF(QPointF(), size)).toAlignedRect()
        .intersected(QRect(QPoint(), size));
}

QColor backgroundColor(const QImage &image, const QRect &rect, const QRegion &safeRegion)
{
    QList<int> red, green, blue, alpha;
    const int step = qMax(1, (rect.width() + rect.height()) / 256);
    const auto sample = [&](int x, int y) {
        if (!safeRegion.contains(QPoint(x, y)))
            return;
        const auto color = image.pixelColor(x, y);
        red.append(color.red());
        green.append(color.green());
        blue.append(color.blue());
        alpha.append(color.alpha());
    };
    for (int x = rect.left(); x <= rect.right(); x += step) {
        sample(x, rect.top());
        sample(x, rect.bottom());
    }
    for (int y = rect.top(); y <= rect.bottom(); y += step) {
        sample(rect.left(), y);
        sample(rect.right(), y);
    }
    if (red.isEmpty())
        return Qt::white;
    const auto median = [](QList<int> &values) {
        const auto middle = values.begin() + values.size() / 2;
        std::nth_element(values.begin(), middle, values.end());
        return *middle;
    };
    return QColor(median(red), median(green), median(blue), median(alpha));
}

std::unique_ptr<QTextDocument> textDocument(const QString &text, const QFont &font, int width)
{
    auto document = std::make_unique<QTextDocument>();
    document->setUndoRedoEnabled(false);
    document->setDocumentMargin(0);
    document->setDefaultFont(font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    option.setUseDesignMetrics(true);
    document->setDefaultTextOption(option);
    // Never interpret translated content as HTML or load embedded resources.
    document->setPlainText(text);
    document->setTextWidth(qMax(1, width));
    return document;
}

QFont pixelFont(const QFont &base, int pixels)
{
    QFont font(base);
    font.setPixelSize(pixels);
    return font;
}

bool fits(const QString &text, const QFont &font, const QRect &rect)
{
    if (rect.isEmpty())
        return false;
    const auto document = textDocument(text, font, rect.width());
    return document->size().height() <= rect.height() && document->idealWidth() <= rect.width();
}

void drawText(QPainter &painter, const QString &text, const QFont &font,
              const QRect &rect, const QColor &color)
{
    auto document = textDocument(text, font, rect.width());
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, color);
    painter.save();
    painter.translate(rect.topLeft());
    painter.setClipRect(QRect(QPoint(), rect.size()), Qt::IntersectClip);
    document->documentLayout()->draw(&painter, context);
    painter.restore();
}

QImage renderingNotice(const QFont &baseFont, const QString &text)
{
    const auto font = pixelFont(baseFont, 13);
    constexpr int width = 512;
    const int height = qCeil(textDocument(text, font, width - 2 * calloutPadding)->size().height());
    QImage image(width, height + 2 * calloutPadding, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    drawText(painter, text, font, {calloutPadding, calloutPadding, width - 2 * calloutPadding, height}, Qt::black);
    return image;
}

struct Overlay {
    QRect rect;
    QRegion safeRegion;
    QColor background;
    QString text;
    QFont font;
    int calloutNumber = 0;
};

struct Callout {
    QString text;
    int top = 0;
    int height = 0;
};

QImage translatedImage(const QImage &original, const QList<Translation::Block> &blocks,
                       const QList<Translation::Result> &results, const QFont &baseFont)
{
    if (original.isNull())
        return {};
    QHash<QString, Translation::Result> byId;
    for (const auto &result : results)
        byId.insert(result.id, result);
    QList<QRect> rectangles;
    for (const auto &block : blocks)
        rectangles.append(pixelRect(block.rect, original.size()));

    QList<Overlay> overlays;
    QList<Callout> callouts;
    const auto annotationFont = pixelFont(baseFont, 13);
    for (qsizetype i = 0; i < blocks.size(); ++i) {
        const auto &block = blocks[i];
        const auto found = byId.constFind(block.id);
        if (block.bypass() || rectangles[i].isEmpty() || found == byId.cend() ||
            (found->status != "ok" && found->status != "mock") || found->text.trimmed().isEmpty())
            continue;
        Overlay overlay;
        overlay.rect = rectangles[i];
        overlay.safeRegion = QRegion(overlay.rect);
        // An uncertain OCR overlap must never erase a neighboring block, including code.
        for (qsizetype j = 0; j < rectangles.size(); ++j)
            if (j != i && overlay.rect.intersects(rectangles[j]))
                overlay.safeRegion -= rectangles[j];
        overlay.background = backgroundColor(original, overlay.rect, overlay.safeRegion);
        overlay.text = found->status == "mock"
            ? QStringLiteral("[Mock / 非真实译文] ") + found->text : found->text;
        const auto textRect = overlay.rect.adjusted(textPadding, textPadding, -textPadding, -textPadding);
        bool fitted = false;
        if (overlay.safeRegion == QRegion(overlay.rect)) {
            overlay.font = pixelFont(baseFont, minimumFontPixels);
            const int maximum = qMin(24, qMax(minimumFontPixels, textRect.height()));
            if (fits(overlay.text, overlay.font, textRect)) {
                fitted = true;
                for (int pixels = maximum; pixels > minimumFontPixels; --pixels) {
                    const auto candidate = pixelFont(baseFont, pixels);
                    if (fits(overlay.text, candidate, textRect)) {
                        overlay.font = candidate;
                        break;
                    }
                }
            }
        }
        if (!fitted) {
            overlay.calloutNumber = int(callouts.size()) + 1;
            callouts.append({QString("[%1] ").arg(overlay.calloutNumber) + overlay.text, 0, 0});
        }
        overlays.append(overlay);
    }
    if (overlays.isEmpty())
        return original;

    // Very narrow captures need a wider annotation area to keep every glyph readable.
    const int canvasWidth = callouts.isEmpty() ? original.width() : qMax(256, original.width());
    const qint64 maximumHeight = maximumImagePixels / canvasWidth;
    int canvasHeight = original.height();
    if (!callouts.isEmpty()) {
        if (qint64(canvasHeight) + calloutPadding + 1 > maximumHeight)
            return renderingNotice(baseFont, QStringLiteral("截图过大，无法生成译图。请切换文字对照查看全文。"));
        canvasHeight += calloutPadding + 1;
        bool exceededBudget = false;
        for (auto &callout : callouts) {
            const qreal height = textDocument(callout.text, annotationFont,
                canvasWidth - 2 * calloutPadding)->size().height();
            // Check in floating point before narrowing or allocating the output image.
            if (!std::isfinite(height) || height > maximumHeight - canvasHeight - calloutPadding) {
                exceededBudget = true;
                break;
            }
            callout.top = canvasHeight;
            callout.height = qCeil(height);
            canvasHeight += callout.height + calloutPadding;
        }
        if (exceededBudget) {
            // The window's plain text comparison keeps the complete result. Never
            // imply that an unrendered translation is present in the image.
            const QString notice = QStringLiteral("译文过长，请切换文字对照查看全文。");
            const int noticeHeight = qCeil(textDocument(notice, annotationFont,
                canvasWidth - 2 * calloutPadding)->size().height());
            canvasHeight = original.height() + calloutPadding + 1;
            if (qint64(canvasHeight) + noticeHeight + calloutPadding > maximumHeight)
                return renderingNotice(baseFont, notice);
            callouts = {{notice, canvasHeight, noticeHeight}};
            canvasHeight += noticeHeight + calloutPadding;
        }
    }
    if (qint64(canvasWidth) * canvasHeight > maximumImagePixels)
        return renderingNotice(baseFont, QStringLiteral("截图过大，无法生成译图。请切换文字对照查看全文。"));
    QImage image(canvasWidth, canvasHeight, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return renderingNotice(baseFont, QStringLiteral("译图生成失败，请切换文字对照查看全文。"));
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint(), original);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (const auto &overlay : overlays) {
        QRect textRect = overlay.rect.adjusted(textPadding, textPadding, -textPadding, -textPadding);
        QString text = overlay.text;
        QFont font = overlay.font;
        if (overlay.calloutNumber) {
            text = QString("[%1]").arg(overlay.calloutNumber);
            font = pixelFont(baseFont, minimumFontPixels);
            textRect = {};
            // Place the marker only where it fits completely inside the safe pixels.
            for (const auto &part : overlay.safeRegion) {
                const auto candidate = part.adjusted(textPadding, textPadding, -textPadding, -textPadding);
                if (fits(text, font, candidate)) {
                    textRect = candidate;
                    break;
                }
            }
            if (textRect.isEmpty())
                continue;
        }
        painter.save();
        painter.setClipRegion(overlay.safeRegion);
        // This is a local background estimate, not an inpainting operation.
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect(overlay.rect, overlay.background);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        const auto foreground = qGray(overlay.background.rgb()) < 140 ? QColor(Qt::white) : QColor(Qt::black);
        drawText(painter, text, font, textRect, foreground);
        painter.restore();
    }
    if (!callouts.isEmpty()) {
        painter.fillRect(QRect(0, original.height(), canvasWidth, 1), QColor("#cbd5e1"));
        for (const auto &callout : callouts)
            drawText(painter, callout.text, annotationFont,
                {calloutPadding, callout.top, canvasWidth - 2 * calloutPadding, callout.height}, Qt::black);
    }
    painter.end();
    image.setDevicePixelRatio(1);
    return image;
}
}

TranslationCanvas::TranslationCanvas(QWidget *parent) : QWidget(parent)
{
    setObjectName("translationCanvas");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    updateCanvasSize();
}

void TranslationCanvas::setContent(const QImage &image, const QList<Translation::Block> &blocks,
                                   const QList<Translation::Result> &results)
{
    m_originalImage = image.copy();
    m_originalImage.setDevicePixelRatio(1);
    m_translatedImage = translatedImage(m_originalImage, blocks, results,
        QFontDatabase::systemFont(QFontDatabase::GeneralFont));
    updateCanvasSize();
    update();
}

QImage TranslationCanvas::renderedImage() const
{
    return m_original ? m_originalImage : m_translatedImage;
}

void TranslationCanvas::setOriginal(bool original)
{
    if (m_original == original)
        return;
    m_original = original;
    updateCanvasSize();
    update();
}

bool TranslationCanvas::original() const { return m_original; }

void TranslationCanvas::setZoom(double zoom)
{
    if (!std::isfinite(zoom) || zoom <= 0)
        return;
    zoom = qMin(4.0, zoom);
    if (m_zoom == zoom)
        return;
    m_zoom = zoom;
    updateCanvasSize();
    update();
}

double TranslationCanvas::zoom() const { return m_zoom; }

void TranslationCanvas::updateCanvasSize()
{
    const auto size = renderedImage().size();
    setFixedSize(qRound(qMin(double(QWIDGETSIZE_MAX), size.width() * m_zoom)),
                 qRound(qMin(double(QWIDGETSIZE_MAX), size.height() * m_zoom)));
}

void TranslationCanvas::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.drawImage(rect(), renderedImage());
}
