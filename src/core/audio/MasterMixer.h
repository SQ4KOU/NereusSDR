// =================================================================
// src/core/audio/MasterMixer.h  (NereusSDR)
// =================================================================
//
// Phase 3O per-slice mute / volume / pan mixer. NereusSDR-original.
//
// Design spec §3.4: producers call accumulate(sliceId, samples, frames)
// once per block, consumer calls mixInto(out, frames) once to flush.
// =================================================================

#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace NereusSDR {

// Audio-thread-safe per-slice mixing. UI thread mutates per-slice params;
// audio thread reads via atomics. Map structural changes are guarded by a
// mutex (held only on UI thread).
class MasterMixer {
public:
    // UI thread: per-slice gain [0..1] + pan [-1..+1]. Safe to call anytime.
    void setSliceGain(int sliceId, float gain, float pan);

    // UI thread: mute / unmute a slice.
    void setSliceMuted(int sliceId, bool muted);

    // UI thread: remove a slice (e.g. slice destroyed).
    void removeSlice(int sliceId);

    // Audio thread: accumulate a slice's stereo block. samples is
    // interleaved L/R float32. frames = sample pairs.
    void accumulate(int sliceId, const float* samples, int frames);

    // Audio thread: flush the accumulated mix into out and reset. Always
    // drains the accumulator — if the block size shrank below the
    // accumulator's current size, out is zeroed and the accumulator is
    // fully cleared so no stale tail can leak into a future call.
    // out is interleaved L/R float32, length = frames * 2.
    void mixInto(float* out, int frames);

private:
    struct SliceState {
        std::atomic<float> gain{1.0f};
        std::atomic<float> pan{0.0f};
        std::atomic<bool>  muted{false};
    };

    // Map guarded by m_sliceMapMutex on structural changes. Audio-thread
    // find() is a lock-free const lookup; this is safe under the invariant
    // that slice creation/removal happens only at startup/connect, not
    // mid-stream. Parameter mutation is lock-free via atomics.
    std::unordered_map<int, SliceState> m_slices;
    std::mutex m_sliceMapMutex;

    std::vector<float> m_acc;  // audio-thread only; resized on demand
};

} // namespace NereusSDR
