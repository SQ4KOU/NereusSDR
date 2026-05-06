// =================================================================
// src/core/audio/VaxTxMicSource.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. See VaxTxMicSource.h for design rationale
// and license block.
// =================================================================

// no-port-check: NereusSDR-original file; no Thetis logic ported here.

#include "core/audio/VaxTxMicSource.h"
#include "core/AudioEngine.h"

namespace NereusSDR {

VaxTxMicSource::VaxTxMicSource(AudioEngine* engine)
    : m_engine(engine)
{
}

int VaxTxMicSource::pullSamples(float* dst, int n)
{
    if (m_engine == nullptr) {
        return 0;
    }
    return m_engine->pullVaxTxMic(dst, n);
}

} // namespace NereusSDR
