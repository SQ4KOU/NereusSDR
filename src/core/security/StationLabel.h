#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/security/StationLabel.h  (NereusSDR)
// =================================================================
//
// The Core's label, `<callsign>/<suffix>` (iPhone app plan Task 12,
// R-IOS-08; the pairing design, docs/architecture/2026-08-02-remote-
// station-identity-and-pairing-design.md section 3.3):
//
//   - the callsign defaults to the StationCallsign setting, so nothing is
//     asked of the operator;
//   - the suffix is at most 32 characters of [A-Za-z0-9_-];
//   - comparison is case-insensitive; display keeps what was typed;
//   - an empty suffix is legal and displays as the bare callsign.
//
// The label is not an identifier: nothing matches on it but a person
// reading it. A callsign may hold '/' itself (a portable call, KG4VCF/P),
// so the suffix is what follows the LAST '/'.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QString>

#include <optional>

namespace NereusSDR {

class AppSettings;

struct StationLabel {
    static constexpr int kMaxSuffixLength = 32;
    static constexpr int kMaxCallsignLength = 32;

    /// As typed (trimmed).
    QString callsign;
    QString suffix;

    /// `callsign/suffix`, or the bare callsign when the suffix is empty.
    QString display() const;

    /// nullopt when `text` is not a label: an empty callsign, a callsign of
    /// other than letters, digits and '/', either part too long, or a
    /// suffix with a character outside [A-Za-z0-9_-]. Text with no '/' is a
    /// bare callsign.
    static std::optional<StationLabel> parse(const QString& text);

    /// Case-insensitive equality of two labels as displayed. False when
    /// either is not a label.
    static bool sameLabel(const QString& a, const QString& b);

    /// The label a Core starts with: its StationCallsign setting, with no
    /// suffix. nullopt when the setting is empty or is not a callsign.
    static std::optional<StationLabel> defaultLabel(const AppSettings& settings);
};

} // namespace NereusSDR
