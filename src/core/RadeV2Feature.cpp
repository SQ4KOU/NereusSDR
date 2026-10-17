// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// Modification history (NereusSDR):
//   2026-10-12 — J.J. Boyd (KG4VCF), experimental V2 opt-in gate.
//                AI-assisted implementation via OpenAI Codex.
// no-port-check: NereusSDR-original process feature switch.
#include "core/RadeV2Feature.h"

namespace NereusSDR::RadeV2Feature {
bool enabled()
{
    // Integer == 1: src/core/P1RadioConnection.cpp:3939 and
    // src/core/P2RadioConnection.cpp:4011. Once-per-process caching:
    // src/gui/styles/WorkspacePreview.cpp:984-991 (not its default-on parser).
    // One exported Core function keeps a single cache across DLL boundaries.
    static const bool kEnabled = qEnvironmentVariableIntValue("NEREUS_ENABLE_RADE_V2") == 1;
    return kEnabled;
}
} // namespace NereusSDR::RadeV2Feature
