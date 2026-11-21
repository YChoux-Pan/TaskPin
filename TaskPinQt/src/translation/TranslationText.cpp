#include "translation/TranslationText.h"
#include <QJsonObject>
#include <QFile>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace Translation {
QList<Line> readingLines(const QList<Line> &fragments)
{
    QList<Line> sorted;
    for (const auto &line : fragments) if (!line.text.trimmed().isEmpty()) sorted.append(line);
    std::stable_sort(sorted.begin(), sorted.end(), [](const Line &a, const Line &b) {
        return a.rect.top() == b.rect.top() ? a.rect.left() < b.rect.left() : a.rect.top() < b.rect.top();
    });
    // Detection may split a visual line into words with slightly different tops.
    // Cluster baselines before sorting horizontally, without bridging column gaps.
    QList<QList<Line>> rows;
    for (const auto &line : sorted) {
        int best=-1;double distance=std::numeric_limits<double>::max();
        for (int i=0;i<rows.size();++i) {
            const auto &anchor=rows[i].first().rect;
            const double delta=std::abs(line.rect.center().y()-anchor.center().y());
            if (delta<=qMin(line.rect.height(),anchor.height())*.5 && delta<distance) {best=i;distance=delta;}
        }
        if(best<0)rows.append(QList<Line>{line});else rows[best].append(line);
    }
    QList<Line> out;
    auto cjk=[](QChar c){const auto u=c.unicode();return (u>=0x2e80&&u<=0x9fff)||(u>=0xf900&&u<=0xfaff)||(u>=0xff00&&u<=0xffef);};
    for (auto &row : rows) {
        std::stable_sort(row.begin(),row.end(),[](const Line &a,const Line &b){return a.rect.left()<b.rect.left();});
        Line combined=row.first();combined.text=combined.text.trimmed();
        for(int i=1;i<row.size();++i) {
            const auto &next=row[i];const QString text=next.text.trimmed();
            const double fragmentHeight=qMax(row[i-1].rect.height(),next.rect.height());
            if(next.rect.left()-combined.rect.right()>fragmentHeight*1.5) {
                out.append(combined);combined=next;combined.text=text;continue;
            }
            const bool noSpace=cjk(combined.text.back())&&cjk(text.front());
            combined.text+=(noSpace?QString():QStringLiteral(" "))+text;
            combined.id+='+'+next.id;combined.rect=combined.rect.united(next.rect);
            // Keep an actual constituent score, never manufacture a probability.
            if(combined.recognitionConfidence&&next.recognitionConfidence)
                combined.recognitionConfidence=qMin(*combined.recognitionConfidence,*next.recognitionConfidence);
            else combined.recognitionConfidence.reset();
        }
        out.append(combined);
    }
    return out;
}
static QString classify(const QString &text)
{
    static const QRegularExpression call(R"(^\s*(?:[A-Za-z_]\w*\s*:=|[a-z]\w*_[\w]+\s*\(|(?:inner_circle|smallest_circle)\s*\())");
    static const QRegularExpression formula(R"((?:max|min|sqrt|abs)\s*\([^\n]+\)\s*[<>≤≥=]|\b\w+\s*:=)");
    static const QRegularExpression uncertainCall(R"(^\s*[A-Za-z][\w ]{0,40}\s*\([^\n]*[,()][^\n]*\)\s*$)");
    if (uncertainCall.match(text).hasMatch()) return "code";
    if (text.trimmed().startsWith('*') || text.trimmed().startsWith("//") || call.match(text).hasMatch()) return "code";
    if (formula.match(text).hasMatch() && !text.contains(QRegularExpression("[a-z]{5,}.*[a-z]{5,}"))) return "formula";
    if (text.contains(QRegularExpression(R"(^\s*(?:[-•]|\d+[.)])\s+)"))) return "list_item";
    return "paragraph";
}
QList<Block> buildBlocks(const OcrResult &ocr, QStringList *warnings)
{
    const auto lines=readingLines(ocr.lines);
    QList<double> heights;
    for (const auto &line : lines) if (line.rect.height() > 0) heights.append(line.rect.height());
    std::sort(heights.begin(), heights.end());
    const double scale = heights.isEmpty() ? 16 : heights[heights.size()/2];
    bool complex = false, unknown = false, low = false;
    QList<Block> out;
    QRectF previous;
    for (const auto &line : lines) {
        unknown |= !line.recognitionConfidence.has_value();
        low |= line.recognitionConfidence.has_value() && *line.recognitionConfidence < .8;
        const QString kind = classify(line.text);
        if (!out.isEmpty() && std::abs(line.rect.top()-previous.top()) < scale*.6 &&
            line.rect.left() > previous.right()+scale*2) complex = true;
        const bool join = !out.isEmpty() && kind == "paragraph" && out.last().kind == kind &&
            line.rect.top() >= previous.top()+scale*.5 && line.rect.top()-previous.bottom() < scale*1.1 &&
            std::abs(line.rect.left()-out.last().rect.left()) < scale*1.5 && out.last().source.size()+line.text.size() < 1800 &&
            !out.last().source.endsWith(':') && !out.last().source.endsWith(QChar(0xff1a));
        if (join) {
            out.last().source += QLatin1Char(' ') + line.text.trimmed();
            out.last().lineIds.append(line.id);
            out.last().rect = out.last().rect.united(line.rect);
        } else {
            Block block;
            block.id = QStringLiteral("block-%1").arg(out.size()+1);
            block.kind = kind; block.source = line.text.trimmed(); block.lineIds = {line.id}; block.rect = line.rect;
            out.append(block);
        }
        previous = line.rect;
    }
    if (warnings) {
        if (complex) warnings->append(QStringLiteral("疑似双栏或表格，阅读顺序可能不完整，请核对原图。"));
        if (unknown) warnings->append(QStringLiteral("部分 OCR 文字缺少置信度；代码、数字和公式请对照原图核对。"));
        if (low) warnings->append(QStringLiteral("存在低置信度文字，已保留，请核对原图。"));
    }
    return out;
}
Block protect(Block block, const QString &nonce)
{
    block.tokens.clear();
    block.protectedText = block.source;
    if (block.bypass()) return block;
    // Long expressions/paths precede individual identifiers and numbers. Match case-sensitive
    // parameter names, so ordinary prose such as "image" is not treated as parameter Image.
    static const QRegularExpression pattern(QStringLiteral(
        R"((?:https?://[^\s<>]+|[A-Za-z]:[\\/][^\s<>]+|(?:max|min|sqrt|abs)\s*\([^\n;]+?\)\s*(?:[<>≤≥]=?|==)\s*[A-Za-z0-9_.+-]+|`[^`]+`|'[A-Za-z_][A-Za-z0-9_-]*'|"[A-Za-z_][A-Za-z0-9_-]*"|\b[A-Za-z]\w*_\w+\b|\b(?:Image|Basins|Threshold|Row|Column|Radius|Region|XLD|HObject|HTuple)\b|(?<![\w.])[+-]?(?:\d+(?:\.\d+)*|\.\d+)(?:[eE][+-]?\d+)?(?:\s?(?:mm|cm|px|ms|MB|GB|%))?))"));
    QString prefix = "__KEEP_"+nonce+"_";
    while (block.source.contains(prefix)) prefix += "X";
    QString result; int position = 0;
    auto matches = pattern.globalMatch(block.source);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString key = prefix+QString::number(block.tokens.size())+"__";
        result += block.source.mid(position, match.capturedStart()-position) + key;
        block.tokens.insert(key, match.captured()); position = match.capturedEnd();
    }
    result += block.source.mid(position); block.protectedText = result;
    return block;
}
bool restore(const Block &block, const QString &translated, QString *text)
{
    QString restored = translated;
    for (auto it = block.tokens.cbegin(); it != block.tokens.cend(); ++it) {
        if (translated.count(it.key()) != 1) return false;
        restored.replace(it.key(), it.value());
    }
    // An invented/damaged placeholder is never accepted as ordinary translated content.
    static const QRegularExpression placeholder(R"(__KEEP_[A-Za-z0-9_]+__)");
    if (placeholder.match(restored).hasMatch() && !block.source.contains(placeholder.match(restored).captured())) return false;
    *text = restored; return true;
}
bool validate(const QList<Block> &requested, const QJsonArray &response, QList<Result> *results,
              QStringList *invalidIds, QString *error)
{
    QSet<QString> expected, seen;
    for (const auto &block : requested) expected.insert(block.id);
    QMap<QString,QJsonObject> byId;
    for (const auto &item : response) {
        const auto object = item.toObject(); const QString id = object.value("block_id").toString();
        if (!item.isObject() || !expected.contains(id) || seen.contains(id)) {
            *error = QStringLiteral("翻译响应包含未知或重复的文本块。"); return false;
        }
        seen.insert(id); byId.insert(id,object);
    }
    for (const auto &block : requested) {
        const auto object = byId.value(block.id); const auto value = object.value("translated_text");
        const QString status = object.value("status").toString(); QString restored;
        if (!seen.contains(block.id) || !value.isString() || value.toString().trimmed().isEmpty() ||
            value.toString().size() > 12000 || status != "ok" || !restore(block,value.toString(),&restored)) {
            invalidIds->append(block.id); continue;
        }
        results->append({block.id,restored,"ok",{}});
    }
    return true;
}
QString sourceText(const QList<Block> &blocks)
{
    QStringList out; for (const auto &block : blocks) out.append(block.source); return out.join("\n\n");
}
QString resultText(const QList<Block> &blocks, const QList<Result> &results)
{
    QMap<QString,Result> byId; for (const auto &result : results) byId.insert(result.id,result);
    QStringList text;
    for (const auto &block : blocks) {
        if (!byId.contains(block.id)) { text.append(QStringLiteral("[待翻译] ")+block.source); continue; }
        const auto result = byId.value(block.id);
        text.append((result.status == "failed" ? QStringLiteral("[未译，保留原文] ") : QString())+result.text);
    }
    return text.join("\n\n");
}
QString glossary()
{
    QFile resource(QStringLiteral(":/translation/glossary.json"));
    if (!resource.open(QIODevice::ReadOnly)) return QStringLiteral("Keep HALCON identifiers unchanged.");
    return QString::fromUtf8(resource.readAll());
}
}
