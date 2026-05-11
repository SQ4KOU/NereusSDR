#pragma once

// NereusSDR-native stub. Full implementation lands in Phase 3R Task I4.
// no-port-check: NereusSDR-native scaffolding to let RadeChannel's
// std::unique_ptr<RadeText> member resolve its destructor. I4 will
// replace this with a Qt6 wrapper around third_party/rade/src/rade_text.c
// (the freedv-gui aux text channel that piggybacks callsign + grid
// over the RADE waveform).

namespace NereusSDR {

// Forward stub. The real RadeText class will own a rade_text_t handle,
// expose configureTx/configureRx slots, and emit a textDecoded signal
// when the underlying rade_text_get_data() yields a complete payload;
// see freedv-gui src/pipeline/rade_text.{c,h} for the target surface.
class RadeText {};

}  // namespace NereusSDR
