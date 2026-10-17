#pragma once
// no-port-check: NereusSDR-original helper for Setup > Hardware Config.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/setup/hardware/IndicatorColumn.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. A check box or radio button with no text, placed in
// a grid column under a centred header, draws its indicator at the left of
// its own width, and that width also holds the label spacing and margins
// the style adds. Centring the button therefore left the indicator a few
// pixels left of the header. fitToIndicator() keeps the button exactly as
// wide as its indicator plus equal margins, refitting whenever the style
// or sheet changes, so a centred button centres its indicator.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-09  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <algorithm>

#include <QAbstractButton>
#include <QEvent>
#include <QObject>
#include <QRadioButton>
#include <QStyle>
#include <QStyleOptionButton>

namespace NereusSDR::IndicatorColumn {

/// Refits its button on every polish, style, sheet or font change.
class IndicatorFit : public QObject
{
public:
    explicit IndicatorFit(QAbstractButton* button)
        : QObject(button)
    {
        button->installEventFilter(this);
        fit(button);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::Polish:
        case QEvent::StyleChange:
        case QEvent::FontChange:
        case QEvent::Show:
            fit(static_cast<QAbstractButton*>(watched));
            break;
        default:
            break;
        }
        return false;
    }

    static void fit(QAbstractButton* button)
    {
        QStyleOptionButton option;
        option.initFrom(button);
        const bool radio = qobject_cast<QRadioButton*>(button) != nullptr;
        const QRect indicator = button->style()->subElementRect(
            radio ? QStyle::SE_RadioButtonIndicator : QStyle::SE_CheckBoxIndicator,
            &option, button);
        if (indicator.width() <= 0) {
            return;
        }
        const int width = indicator.width() + 2 * std::max(0, indicator.x());
        if (button->minimumWidth() != width || button->maximumWidth() != width) {
            button->setFixedWidth(width);
        }
    }
};

/// Keeps a text-less button as wide as its indicator (plus equal margins),
/// so centring the button in a grid cell centres the indicator under the
/// column header. The fitter is the button's child.
inline void fitToIndicator(QAbstractButton* button)
{
    new IndicatorFit(button); // owned by button (Qt parent)
}

} // namespace NereusSDR::IndicatorColumn
