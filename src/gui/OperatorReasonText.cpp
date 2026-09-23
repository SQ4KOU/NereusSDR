// no-port-check: NereusSDR-original. See header.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/OperatorReasonText.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See header for full
// Modification history (NereusSDR).
// =================================================================

#include "gui/OperatorReasonText.h"

#include "core/session/media/SpectrumEndpoint.h"

#include <QLatin1String>

namespace NereusSDR::OperatorReasonText {
namespace {

struct Entry {
    const char* wire;       // Byte-for-byte as sent or recorded; never reworded.
    const char* shortLine;  // Fits a 200 px pan.
    const char* sentence;   // Why, in user words.
};

// The left column is copied from where each reason is written, so a match
// is exact. Keep it in step with those sites; the wording on the right is
// what the user reads.
constexpr Entry kEntries[] = {
    // The Core's display refusals, DaemonMediaController.cpp.
    {"session display budget exceeded", "Refused: Core busy",
     "The Core's display limit has no room left for this pan."},
    {"endpoint limit reached", "Refused: pan limit",
     "The Core is already sending as many pan displays as it can."},
    {"endpoint identifier retired", "Refused: out of date",
     "The Core had already closed the display this request was for."},
    {"stale revision", "Refused: out of date",
     "A newer request for this pan had already reached the Core."},
    {"incompatible source window", "Refused: out of date",
     "The receiver's spectrum settings changed before the Core could answer."},
    {"invalid subscription", "Refused: bad request",
     "The Core could not read this pan's display request."},
    {"invalid unsubscription", "Refused: bad request",
     "The Core could not read the request to stop this pan's display."},
    {"requested crop is outside source coverage", "Refused: out of range",
     "This view reaches past the frequencies the receiver covers."},
    {kRetireReasonSourceRetune, "Refused: out of range",
     "The receiver was retuned and no longer covers this view."},
    {"slice is unavailable", "Refused: no slice",
     "This pan's slice is not available on the Core."},
    {kRetireReasonSliceRemoved, "Refused: no slice",
     "This pan's slice was removed."},
    {kRetireReasonStreamBindingChanged, "Refused: out of date",
     "This pan's slice moved to a different receiver."},
    {"source configuration rejected", "Refused: setup failed",
     "The Core could not set up the spectrum for this receiver."},
    {"source configuration became unavailable", "Refused: not ready",
     "The spectrum for this receiver stopped on the Core."},
    {"source geometry is unavailable", "Refused: not ready",
     "The Core does not have this receiver's spectrum ready yet."},
    {"wideband source is unavailable", "Refused: no wide view",
     "The Core cannot provide the extended view right now."},
    {"display budget authority retired", "Refused: Core stopping",
     "The Core was stopping its display service."},
    {"PureSignal display does not fit the session display budget", "Refused: Core busy",
     "The Core's display limit has no room for the PureSignal display."},

    // Recorded by this computer, RemoteMediaController.cpp.
    {"Core refused the display allocation.", "Refused by the Core",
     "The Core did not accept this pan's display request."},
    {"Core refused the display release.", "Refused by the Core",
     "The Core did not accept the request to stop this pan's display."},
    {"Unable to send the allocation request.", "Request not sent",
     "This computer could not send the request to the Core."},
    {"Unable to request PureSignal display release.", "Request not sent",
     "This computer could not send the PureSignal display request to the Core."},
    {"Unable to request PureSignal display admission.", "Request not sent",
     "This computer could not send the PureSignal display request to the Core."},

    // Recorded by this computer, RemoteDisplayAllocator.cpp.
    {"Display budget limits are invalid.", "Core limits unusable",
     "The Core reported display limits this app cannot use."},
    {"At most eight remote display pans are supported.", "Too many pans",
     "The Core's display can be shown on up to eight pans at once."},
    {"Remote display pan IDs must be unique.", "Can't show this pan",
     "Two pans could not be told apart."},
    {"At most one remote display pan may be active.", "Can't show this pan",
     "More than one pan was marked as the active pan."},
    {"PureSignal display reservation does not fit the display budget.", "Refused: Core busy",
     "The Core's display limit has no room for the PureSignal display."},
    {"Display budget cannot reserve requested display demand.", "Refused: Core busy",
     "The Core's display limit has no room for the pan displays you have open."},
};

// RemoteDisplayAllocator.cpp words this one around the pan's name.
constexpr char kInvalidRangePrefix[] = "Remote display intent '";
constexpr char kInvalidRangeSuffix[] = "' has an invalid range.";

const Entry* find(const QString& wireReason)
{
    for (const Entry& entry : kEntries) {
        if (wireReason == QLatin1String(entry.wire)) {
            return &entry;
        }
    }
    return nullptr;
}

bool invalidRange(const QString& wireReason)
{
    return wireReason.startsWith(QLatin1String(kInvalidRangePrefix))
        && wireReason.endsWith(QLatin1String(kInvalidRangeSuffix));
}

} // namespace

QString forDisplay(const QString& wireReason)
{
    if (const Entry* entry = find(wireReason)) {
        return QString::fromLatin1(entry->sentence);
    }
    if (invalidRange(wireReason)) {
        return QStringLiteral("This pan's size or update rate is out of range.");
    }
    return QStringLiteral("The Core turned this request down.");
}

QString shortForDisplay(const QString& wireReason)
{
    if (const Entry* entry = find(wireReason)) {
        return QString::fromLatin1(entry->shortLine);
    }
    if (invalidRange(wireReason)) {
        return QStringLiteral("Can't show this pan");
    }
    return QStringLiteral("Refused by the Core");
}

QStringList knownReasons()
{
    QStringList reasons;
    for (const Entry& entry : kEntries) {
        reasons.append(QString::fromLatin1(entry.wire));
    }
    reasons.append(QString::fromLatin1(kInvalidRangePrefix) + QStringLiteral("pan")
                   + QString::fromLatin1(kInvalidRangeSuffix));
    return reasons;
}

} // namespace NereusSDR::OperatorReasonText
