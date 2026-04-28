// =================================================================
// src/core/audio/PcMicSource.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. See PcMicSource.h for design rationale and
// license block. Plan: 3M-1b F.1.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#include "core/audio/PcMicSource.h"
#include "core/AudioEngine.h"

namespace NereusSDR {

PcMicSource::PcMicSource(AudioEngine* engine)
    : m_engine(engine)
{
}

int PcMicSource::pullSamples(float* dst, int n)
{
    if (m_engine == nullptr) {
        return 0;
    }
    return m_engine->pullTxMic(dst, n);
}

} // namespace NereusSDR
