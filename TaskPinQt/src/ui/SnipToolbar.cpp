#include "ui/SnipToolbar.h"
#include "core/Settings.h"

namespace
{
struct Definition {
    const char *id;
    const char *label;
    const char *shortcut;
};
const Definition definitions[] = {
    { "tool:select", "选择", "V" },
    { "tool:rect", "矩形", "R" },
    { "tool:ellipse", "椭圆", "O" },
    { "tool:arrow", "箭头", "A" },
    { "tool:line", "直线", "L" },
    { "tool:pen", "画笔", "P" },
    { "tool:marker", "荧光笔", "H" },
    { "tool:text", "文字", "T" },
    { "tool:number", "序号", "N" },
    { "tool:mosaic", "马赛克", "M" },
    { "tool:blur", "模糊", "B" },
    { "tool:eraser", "橡皮", "E" },
    { "undo", "撤销", "Ctrl+Z" },
    { "redo", "重做", "Ctrl+Y" },
    { "pin", "贴到桌面", "" },
    { "ocr", "识别为任务", "Ctrl+R" },
    { "translate", "翻译", "" },
    { "save", "保存", "Ctrl+S" },
    { "copy", "复制", "Enter" },
    { "close", "取消截图", "Esc" },
    { "more", "更多", "" },
    { "custom", "自选颜色", "" },
    { "width-", "减小线宽", "滚轮 ↓" },
    { "width+", "增大线宽", "滚轮 ↑" },
    { "font-", "减小字号", "滚轮 ↓" },
    { "font+", "增大字号", "滚轮 ↑" },
    { "font-value", "当前字号", "" },
    { "text-background", "切换文字背景", "" },
    { "block-", "减小强度", "滚轮 ↓" },
    { "block+", "增大强度", "滚轮 ↑" },
    { "quick", "快速保存", "Ctrl+Shift+S" },
    { "print", "打印", "" },
};
}

QString SnipToolbar::label(const QString &id)
{
    for (const auto &definition : definitions)
        if (id == QLatin1String(definition.id)) return QString::fromUtf8(definition.label);
    return id.startsWith("swatch:") ? QStringLiteral("颜色") : QString();
}

QString SnipToolbar::shortcut(const QString &id)
{
    if (id == "pin") return Settings::instance().chordFor("pin");
    if (id == "translate") return Settings::instance().chordFor("editorTranslate");
    for (const auto &definition : definitions)
        if (id == QLatin1String(definition.id)) return QString::fromUtf8(definition.shortcut);
    return {};
}

QRect SnipToolbar::layout(QVector<SnipAction> &actions, const QRect &viewport, const QRect &selection)
{
    if (actions.isEmpty() || viewport.width() < 40 || viewport.height() < 40) return {};
    constexpr int pad = 6, gap = 3, button = 30, margin = 4;
    const QRect available = viewport.adjusted(margin, margin, -margin, -margin);
    QVector<int> cells;
    auto sizeFor = [&](const QRect &space, int &columns) {
        columns = qMin(20, (space.width() - 2 * pad + gap) / (button + gap));
        if (columns < 1) return QSize();
        cells.clear();
        int index = 0;
        for (const auto &action : actions) {
            if (action.id == "font-" && columns >= 3 && columns - index % columns < 3)
                index += columns - index % columns;
            cells.append(index++);
        }
        const int rows = (index + columns - 1) / columns;
        return QSize(columns * (button + gap) - gap + 2 * pad, rows * (button + gap) - gap + 2 * pad);
    };
    const QRect spaces[] = {
        QRect(QPoint(available.left(), selection.bottom() + 10), available.bottomRight()),
        QRect(available.topLeft(), QPoint(available.right(), selection.top() - 10)),
        QRect(QPoint(selection.right() + 10, available.top()), available.bottomRight()),
        QRect(available.topLeft(), QPoint(selection.left() - 10, available.bottom())),
        available,
    };
    int columns = 1;
    QRect bar;
    for (int i = 0; i < 5; ++i) {
        const QRect space = spaces[i].intersected(available);
        const QSize size = sizeFor(space, columns);
        if (size.isEmpty() || (size.height() > space.height() && i < 4)) continue;
        const int x = qBound(space.left(), selection.right() - size.width() + 1,
            qMax(space.left(), space.right() - size.width() + 1));
        int y = i == 0 ? space.top() : i == 1 ? space.bottom() - size.height() + 1 : selection.top();
        y = qBound(space.top(), y, qMax(space.top(), space.bottom() - size.height() + 1));
        bar = QRect(QPoint(x, y), size);
        break;
    }
    if (bar.isEmpty()) return {};
    for (int i = 0; i < actions.size(); ++i)
        actions[i].rect = QRect(bar.left() + pad + (cells[i] % columns) * (button + gap),
            bar.top() + pad + (cells[i] / columns) * (button + gap), button, button);
    return bar;
}
