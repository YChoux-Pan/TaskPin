#pragma once
#include <QImage>
#include <QList>
#include <QMap>
#include <QMetaType>
#include <QRectF>
#include <QStringList>
#include <optional>

namespace Translation {
struct Line {
    QString id, text;
    QRectF rect;
    std::optional<double> recognitionConfidence;
};
struct OcrResult {
    QList<Line> lines;
    QString language;
    QSize imageSize;
    QString warning;
    QString engine,modelVersion;
};
struct Block {
    QString id, kind, source, protectedText;
    QStringList lineIds;
    QRectF rect;
    QMap<QString, QString> tokens;
    bool bypass() const { return kind == "code" || kind == "formula"; }
};
struct Result {
    QString id, text, status, warning;
};
}
Q_DECLARE_METATYPE(Translation::OcrResult)
Q_DECLARE_METATYPE(QList<Translation::Result>)
