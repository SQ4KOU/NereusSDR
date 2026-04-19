// =================================================================
// src/core/audio/MasterMixer.cpp  (NereusSDR)
// =================================================================
// See MasterMixer.h for contract. NereusSDR-original.
// =================================================================

#include "MasterMixer.h"

#include <algorithm>

namespace NereusSDR {


void MasterMixer::setSliceGain(int sliceId, float gain, float pan) {
    std::lock_guard<std::mutex> lk(m_sliceMapMutex);
    auto& st = m_slices[sliceId];
    st.gain.store(std::clamp(gain, 0.0f, 1.0f), std::memory_order_release);
    st.pan.store(std::clamp(pan, -1.0f, 1.0f),  std::memory_order_release);
}

void MasterMixer::setSliceMuted(int sliceId, bool muted) {
    std::lock_guard<std::mutex> lk(m_sliceMapMutex);
    m_slices[sliceId].muted.store(muted, std::memory_order_release);
}

void MasterMixer::removeSlice(int sliceId) {
    std::lock_guard<std::mutex> lk(m_sliceMapMutex);
    m_slices.erase(sliceId);
}

void MasterMixer::accumulate(int sliceId, const float* samples, int frames) {
    // Audio-thread hot path. No lock; rely on startup/connect-time
    // invariant that the map is stable while audio is streaming.
    auto it = m_slices.find(sliceId);
    if (it == m_slices.end()) { return; }

    const SliceState& st = it->second;
    if (st.muted.load(std::memory_order_acquire)) { return; }

    const float gain = st.gain.load(std::memory_order_acquire);
    const float pan  = st.pan.load(std::memory_order_acquire);

    // Linear pan law: pan ∈ [-1..+1].
    // At pan=0 both channels pass through at full gain (unity).
    // At pan=-1 only the left channel passes; at pan=+1 only right.
    const float lGain = gain * (pan <= 0.0f ? 1.0f : 1.0f - pan);
    const float rGain = gain * (pan >= 0.0f ? 1.0f : 1.0f + pan);

    const int neededFloats = frames * 2;
    if (static_cast<int>(m_acc.size()) < neededFloats) {
        m_acc.resize(static_cast<size_t>(neededFloats), 0.0f);
    }
    for (int i = 0; i < frames; ++i) {
        m_acc[i * 2 + 0] += samples[i * 2 + 0] * lGain;
        m_acc[i * 2 + 1] += samples[i * 2 + 1] * rGain;
    }
}

void MasterMixer::mixInto(float* out, int frames) {
    const int n = frames * 2;
    const int have = static_cast<int>(m_acc.size());
    if (have < n) {
        // Block size shrank (or no one accumulated yet) — output silence
        // AND drain whatever the accumulator does have so a stale tail
        // from a prior larger block cannot leak into a future mixInto
        // call.
        std::fill(out, out + n, 0.0f);
        if (have > 0) {
            std::fill(m_acc.begin(), m_acc.end(), 0.0f);
        }
        return;
    }
    std::copy(m_acc.begin(), m_acc.begin() + n, out);
    std::fill(m_acc.begin(), m_acc.begin() + n, 0.0f);
}

} // namespace NereusSDR
