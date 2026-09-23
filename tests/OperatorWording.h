// no-port-check: NereusSDR-original test helper.
//
// OperatorWording: the shared check that words a user reads are written for
// the user (R-R3-21, R-R3-37; operator directive of 2026-09-23). A string
// passes when it is not empty and names none of the internal terms. Log
// lines and source comments may use them; nothing a user reads may.
//
// The term list itself belongs to the product (OperatorReasonText), which
// uses it to decide whether a reason it does not know may be shown as sent.
// One list, so a test and the app can never disagree about a word.
#pragma once

#include "gui/OperatorReasonText.h"

#include <QString>
#include <QStringList>

namespace NereusSDR::OperatorWording {

/// Internal subsystem, roadmap and wire terms (the product's list).
inline const QStringList& internalTerms()
{
    return OperatorReasonText::internalTerms();
}

/// The first internal term `text` names, or an empty string.
inline QString internalTermIn(const QString& text)
{
    return OperatorReasonText::internalTermIn(text);
}

/// True when `text` is written for the user: not empty, no internal term.
inline bool isPlain(const QString& text)
{
    return !text.trimmed().isEmpty() && internalTermIn(text).isEmpty();
}

} // namespace NereusSDR::OperatorWording
