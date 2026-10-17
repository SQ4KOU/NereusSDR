// =================================================================
// src/core/audio/AudioDelayParts.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  Plain value types for the delay
// readout and the clock matcher's counters (R-AUD-15, D34); no upstream
// logic.
//
// AudioDelayParts splits a local output's expected delay into the clock
// matcher's fill, the resampler's filter delay and the device's own buffer
// and latency, each in milliseconds at the device rate.
//
// Modification history (NereusSDR):
//   2026-10-08: native audio plan Task 2 (R-AUD-15). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-10: DeviceRateMatcherStats carries the frames queued now and a
//               packet writer's size and high-water mark, for remote
//               playback's room check (R-AUD-15). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
//   2026-10-12: MatcherPacketTiming, what a packet writer tells the clock
//               matcher of each packet: its wait for room, how long ago it
//               arrived and its place in the source's stream (R-AUD-15).
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <cstdint>

namespace NereusSDR {

struct AudioDelayParts {
    double matcherFillMs = -1.0;   // -1: no matcher on this stream
    double resamplerMs = 0.0;
    double deviceBufferMs = 0.0;
    double deviceLatencyMs = 0.0;

    // The sum of the four, or -1 when matcherFillMs < 0.
    double totalMs() const
    {
        if (matcherFillMs < 0.0) {
            return -1.0;
        }
        return matcherFillMs + resamplerMs + deviceBufferMs + deviceLatencyMs;
    }
};

struct DeviceRateMatcherStats {
    std::uint64_t dryRuns = 0;
    std::uint64_t overruns = 0;
    double ratio = 1.0;
    double fillFrames = 0.0;
    int rsizeFrames = 0;
    int capacityFrames = 0;
    int delayStepMs = 0;
    bool controlActive = false;
    // The frames queued at this moment (fillFrames is the control's
    // average once the control runs).
    int queuedFrames = 0;
    // A packet writer (DeviceRateMatcher::setWritePacketFrames): its packet
    // in input frames (0: none, or a new size not yet applied), the most
    // device frames one packet makes, and the fill a packet's write may
    // reach.  A packet has room when queuedFrames + packetOutFrames is at
    // or below packetHighWaterFrames.
    int packetFrames = 0;
    int packetOutFrames = 0;
    int packetHighWaterFrames = 0;
};

// What a writer of whole packets (remote playback) knows of the packet it
// is about to write (DeviceRateMatcher::setWritePacketFrames).
struct MatcherPacketTiming {
    // How long it was kept back for room in the matcher, itself or behind
    // one that was; 0: it was not, it is written in its own time.
    std::int64_t waitedNs = 0;
    // How long ago it arrived from its source, whatever kept it since (a
    // jitter queue's hold, the wait for room); negative: nothing arrived
    // (a concealed interval), or the writer cannot say.
    std::int64_t arrivedAgoNs = -1;
    // Its first frame's place in the source's stream, in input frames from
    // any fixed origin (an RTP timestamp, unwrapped).  Read only with an
    // arrival.
    std::int64_t sourceFrame = 0;
};

} // namespace NereusSDR
