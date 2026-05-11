#pragma once

// NereusSDR-native stub. Full implementation lands in Phase 3R Task I2/I3.
// no-port-check: NereusSDR-native scaffolding to let RadeChannel's
// std::unique_ptr<Resampler> members resolve their destructors. I2/I3
// will replace this with a real Resampler class wrapping r8brain (or
// the freedv-gui equivalent).

namespace NereusSDR {

// Forward stub. The real Resampler will offer source/dest sample-rate
// configuration and a streaming int16/float resample API; see
// freedv-gui src/audio/SampleRateConverter or AetherSDR
// src/core/Resampler.{h,cpp} for the target surface.
class Resampler {};

}  // namespace NereusSDR
