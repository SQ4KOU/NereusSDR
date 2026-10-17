#pragma once
// =================================================================
// src/core/settings/PanDisplayKeys.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Per-pan display settings are a
// NereusSDR construct (any number of panadapters, each able to differ
// from the Setup default); Thetis has a fixed RX1/RX2 pair with
// hand-written RX2 duplicates of some settings, and AetherSDR's
// "base for pan 0, base_N for pan N" naming carries no default layer.
//
// Where a display setting that a panadapter can override is stored.
//
//   base      the Setup default. Every pan shows it until that pan is
//             given a value of its own.
//   base_N    pan N's own override, for every pan, pan 0 included
//             (base_0).
//
// The bare key is the Setup default on purpose: before this split pan 0
// stored its values under the bare key and every other pan fell back to
// them, so an existing install's pan 0 values become the Setup defaults
// with nothing copied or renamed, and a pan that had its own "_N" keys
// keeps reading them.
//
// Overrides are sparse. writeSparse() creates a pan's key only when the
// value differs from what the pan would inherit, so a pan that was never
// changed keeps following the Setup default instead of freezing a copy of
// it. Setting a pan back to the inherited value removes that override.
// An explicit master write removes every pan's override of that setting,
// so every pan follows the new master value, including pans not open.
//
// Presence. A pan key counts when it exists, whatever it holds: an empty
// value there is a value (the transmit waterfall's custom gradient is
// legitimately empty). The default key counts when it exists and is not
// empty, because an empty bare key is how the rest of the code spells
// "never set".
//
// Values are stored as the strings the callers format (AppSettings stores
// strings; booleans are "True" / "False"). Main thread only, like every
// other AppSettings access.
//
// Modification history (NereusSDR)
//   2026-10-12 - Reuse sparse-value equality for selected Setup controls.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-12 - Document the approved master and equality rules.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-10: Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//     Claude Code.
// =================================================================

#include "core/NereusCoreExport.h"

#include <QString>
#include <QStringList>
#include <QStringView>
#include <QVariant>

namespace NereusSDR::PanDisplayKeys {

/// Whether two stored values agree under the same numeric/color rules as sparse writes.
NEREUS_CORE_EXPORT bool valuesEqual(const QVariant& a, const QVariant& b);

/// The Setup default key for `base`: `base` itself.
NEREUS_CORE_EXPORT QString defaultKey(QStringView base);

/// Pan `panIndex`'s own override key: `base_N`, pan 0 included.
NEREUS_CORE_EXPORT QString panKey(QStringView base, int panIndex);

/// Whether pan `panIndex` has a value of its own for `base`.
NEREUS_CORE_EXPORT bool hasOverride(QStringView base, int panIndex);

/// The pan key when present, else the default key, else `fallback`.
NEREUS_CORE_EXPORT QVariant read(QStringView base, int panIndex,
                                 const QVariant& fallback);

/// Writes the pan key when `value` differs from what the pan would inherit
/// (the default key, else `fallback`). Equality removes an existing override.
/// `fallback` must be the same default the
/// matching read uses, formatted the same way as `value`.
///
/// "Differs" is by meaning, not spelling: two numbers that agree to six
/// significant figures are the same value ("0.7" against
/// "0.699999988079071", which is what a float 0.7 is stored as), and
/// colour strings compare without regard to letter case.
NEREUS_CORE_EXPORT void writeSparse(QStringView base, int panIndex,
                                    const QVariant& value,
                                    const QVariant& fallback);

/// Writes the Setup default and clears every pan override of this base.
/// The only writer of the bare key. Explicit resets also act at the same value.
NEREUS_CORE_EXPORT void writeDefault(QStringView base, const QVariant& value);

/// Removes pan `panIndex`'s override for every base in `bases`. The
/// default keys are not touched.
NEREUS_CORE_EXPORT void clearPan(int panIndex, const QStringList& bases);

} // namespace NereusSDR::PanDisplayKeys
