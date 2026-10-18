// =================================================================
// src/core/audio/MacMicMode.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The Mac's side of MicModeKeeper:
// the system's Mic Mode calls, which only the capture helper makes, and
// the window's bundle id, which its Warm carries.  Nothing off the Mac.
//
// Modification history (NereusSDR):
//   2026-10-16: new, for the standing capture helper's warm-up. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "core/NereusCoreExport.h"
#include "core/audio/MicModeKeeper.h"

#include <QString>
#include <QtGlobal>

namespace NereusSDR {

#ifdef Q_OS_MAC
// The system's calls; empty when this macOS has none.  Called only in the
// capture helper, which has no bundle id, and never in a test run.
NEREUS_CORE_EXPORT MicModeCalls systemMicModeCalls();
// This process's bundle id; empty for a bare binary (the helper, nereusd,
// a test).
NEREUS_CORE_EXPORT QString macMainBundleId();
#else
inline MicModeCalls systemMicModeCalls() { return {}; }
inline QString macMainBundleId() { return {}; }
#endif

} // namespace NereusSDR
