#pragma once
#include "core/Settings.h"
#include <QMetaType>

// A draft contains appearance only; dragging, hotkeys and newly created tasks stay independent.
struct CardAppearance {
    QColor background, foreground;
    double opacity;
    int width, rows;
    bool carryMark;
    static CardAppearance current()
    {
        const auto &s = Settings::instance();
        return {s.cardBg, s.cardFg, s.opacity, s.cardWidth, s.visibleRows, s.showCarryMark};
    }
};
Q_DECLARE_METATYPE(CardAppearance)
