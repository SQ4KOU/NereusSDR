// no-port-check: NereusSDR-original. Shared approved power scale formatting.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#pragma once
#include "gui/instruments/ReadingSource.h"
namespace NereusSDR {
ReadingDescriptor powerReadingDescriptor(double limitWatts, double topWatts);
}
