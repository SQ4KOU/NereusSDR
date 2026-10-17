// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
//   2026-10-12 — J.J. Boyd (KG4VCF), experimental V2 opt-in gate.
//                AI-assisted implementation via OpenAI Codex.
// no-port-check: NereusSDR-original.
// Experimental codec opt-in belongs to the process that owns DSP.
#pragma once
#include "core/NereusCoreExport.h"

namespace NereusSDR::RadeV2Feature {
NEREUS_CORE_EXPORT bool enabled();
} // namespace NereusSDR::RadeV2Feature
