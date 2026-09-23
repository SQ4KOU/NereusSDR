// no-port-check: NereusSDR-original. See header.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/PanStatusText.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See header for full
// Modification history (NereusSDR).
// =================================================================

#include "gui/PanStatusText.h"

#include "gui/OperatorReasonText.h"

#include <QStringList>

namespace NereusSDR {
namespace {

using Phase = PanDisplayState::Phase;

// Every short line must fit the status row of a 200 px pan without elision
// (tst_pan_status_overlay checks it), so they stay near twenty characters.
// Each reduction and pause today comes from the Core's display limit, never
// from the network, so each one says "Core busy".

// The pan's own display: what happened, why, and what happens next.
PanStatusText displayText(const PanDisplayState& state)
{
    switch (state.phase) {
    case Phase::None:
        return {};
    case Phase::Showing: {
        QString explanation;
        if (state.reduced()) {
            explanation = QStringLiteral(
                "The Core is sending %1 points across, %2 updates a second, instead of "
                "the %3 points and %4 updates a second this pan asked for. The Core has "
                "a limit on how much display it sends, and your open pans share it. "
                "Full quality comes back by itself when there is room.")
                .arg(state.pixels).arg(state.fps)
                .arg(state.requestedPixels).arg(state.requestedFps);
        } else {
            explanation = QStringLiteral(
                "Full quality from the Core: %1 points across, %2 updates a second.")
                .arg(state.pixels).arg(state.fps);
        }
        if (state.receivedFps >= 0.0) {
            explanation += QStringLiteral(" Arriving at about %1 updates a second.")
                .arg(state.receivedFps, 0, 'f', 1);
        }
        if (state.extendedView) {
            explanation += QStringLiteral(" Includes the extended view.");
        }
        if (!state.reduced()) {
            return {QString(), explanation};
        }
        return {state.pixels != state.requestedPixels
                    ? QStringLiteral("Less detail: Core busy")
                    : QStringLiteral("Slower: Core busy"),
                explanation};
    }
    case Phase::Waiting:
        return {QStringLiteral("Waiting for the Core"),
                QStringLiteral("This pan has asked the Core for its display. "
                               "It appears as soon as the Core answers.")};
    case Phase::ChangingWindow:
        return {QStringLiteral("Waiting for the Core"),
                QStringLiteral("This receiver's spectrum settings changed, so this pan is "
                               "asking the Core for a display that matches. The last "
                               "picture stays until the Core answers.")};
    case Phase::Stalled:
        return {QStringLiteral("Core not answering"),
                QStringLiteral("This pan asked the Core for its display and the answer "
                               "is overdue. It keeps waiting, and the display comes back "
                               "as soon as the Core answers.")};
    case Phase::Paused:
        if (state.pureSignalOverLimit) {
            return {QStringLiteral("Paused: Core busy"),
                    QStringLiteral("The PureSignal display already running uses more than "
                                   "the Core's current display limit, so this pan's display "
                                   "is paused and the last picture is held. It resumes when "
                                   "the PureSignal display closes or the limit rises.")};
        }
        return {QStringLiteral("Paused: Core busy"),
                QStringLiteral("The Core's display limit has no room for this pan right "
                               "now, so the last picture is held. It resumes by itself "
                               "when there is room, for example when another pan closes "
                               "or gets smaller.")};
    case Phase::Refused:
        return {OperatorReasonText::shortForDisplay(state.refusalReason),
                QStringLiteral("This pan's display request did not go through. %1 "
                               "It asks again when you change this pan's view.")
                    .arg(OperatorReasonText::forDisplay(state.refusalReason))};
    case Phase::TooManyPans:
        return {QStringLiteral("Too many pans"),
                QStringLiteral("This computer is already showing the Core's display on as "
                               "many pans as it can. This pan's display starts when another "
                               "pan closes.")};
    }
    return {};
}

PanStatusText pureSignalText(const PanDisplayState& state)
{
    switch (state.pureSignal) {
    case PanDisplayState::PureSignal::Fine:
        return {};
    case PanDisplayState::PureSignal::Refused:
        return {QStringLiteral("PureSignal: refused"),
                QStringLiteral("The PureSignal display did not start. %1 Your pan "
                               "displays carry on as before.")
                    .arg(OperatorReasonText::forDisplay(state.pureSignalRefusalReason))};
    case PanDisplayState::PureSignal::Stalled:
        return {QStringLiteral("PureSignal: no answer"),
                QStringLiteral("A change to the PureSignal display is waiting for the "
                               "Core, and its answer is overdue. It takes effect as soon "
                               "as the Core answers.")};
    }
    return {};
}

PanStatusText zoomText(const PanDisplayState& state)
{
    switch (state.zoomLimit) {
    case PanDisplayState::ZoomLimit::None:
        return {};
    case PanDisplayState::ZoomLimit::LargestSize:
        return {QStringLiteral("Finest detail reached"),
                QStringLiteral("You have zoomed in as far as the Core can add detail. "
                               "Zooming in further enlarges the same points.")};
    case PanDisplayState::ZoomLimit::SharedEngine:
        return {QStringLiteral("Less detail: shared"),
                QStringLiteral("This receiver's spectrum is shared with another pan, so "
                               "this pan gets the detail that pan's setting gives. It gets "
                               "its own detail when the spectrum is no longer shared.")};
    case PanDisplayState::ZoomLimit::SourceBins:
        return {QStringLiteral("Showing %1 points").arg(state.zoomPoints),
                QStringLiteral("The receiver has no finer detail at this zoom, so this pan "
                               "shows %1 points stretched to fit. Zooming out brings back "
                               "full detail.")
                    .arg(state.zoomPoints)};
    }
    return {};
}

} // namespace

PanStatusText buildPanStatusText(const PanDisplayState& state)
{
    // The pan's own display speaks first, then the PureSignal display, then
    // the zoom detail. The short line is the first that has one; the
    // explanation carries all of them.
    PanStatusText text;
    QStringList paragraphs;
    for (const PanStatusText& part :
         {displayText(state), pureSignalText(state), zoomText(state)}) {
        if (text.shortLine.isEmpty()) {
            text.shortLine = part.shortLine;
        }
        if (!part.explanation.isEmpty()) {
            paragraphs.append(part.explanation);
        }
    }
    text.explanation = paragraphs.join(QLatin1Char('\n'));
    return text;
}

} // namespace NereusSDR
