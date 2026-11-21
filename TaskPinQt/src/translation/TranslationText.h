#pragma once
#include "translation/TranslationTypes.h"
#include <QJsonArray>

namespace Translation {
QList<Block> buildBlocks(const OcrResult &ocr, QStringList *warnings = nullptr);
Block protect(Block block, const QString &nonce);
bool restore(const Block &block, const QString &translated, QString *text);
bool validate(const QList<Block> &requested, const QJsonArray &response,
              QList<Result> *results, QStringList *invalidIds, QString *error);
QString sourceText(const QList<Block> &blocks);
QString resultText(const QList<Block> &blocks, const QList<Result> &results);
QString glossary();
}
