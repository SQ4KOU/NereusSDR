// no-port-check: NereusSDR-original. Remote daemon R3 reason wording.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/OperatorReasonText.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. Display-time translation of the
// reasons the Core sends (and this computer records) into user words.
//
// The reasons themselves never change: older apps compare some of them as
// exact text, and the log keeps the raw text. Only what a user reads is
// translated, here, when it is shown (R-R3-21, R-R3-37).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23  J.J. Boyd / KG4VCF  Created. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================
#pragma once

#include <QString>
#include <QStringList>

namespace NereusSDR::OperatorReasonText {

/// A sentence in user words saying why, for a reason as the Core sent it.
/// An unknown reason gets a general sentence, never the raw text.
QString forDisplay(const QString& wireReason);

/// A short line for a narrow pan, for the same reason.
QString shortForDisplay(const QString& wireReason);

/// Every reason the table knows, for tests that check its wording.
QStringList knownReasons();

} // namespace NereusSDR::OperatorReasonText
