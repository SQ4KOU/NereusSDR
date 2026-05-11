# RADE test fixtures

## rx_test_iq.bin (synthetic)

This fixture is *not* a captured RADE transmission. It is synthetic
24 kHz interleaved float32 I/Q noise generated at test-runtime via
`std::mt19937` with a fixed seed. The exercise is to drive the RX
pipeline (24 kHz I/Q -> 8 kHz RADE_COMP -> rade_rx -> FARGAN -> 16 kHz
mono -> 24 kHz stereo) with input that the codec will *not* sync on,
verify that:

  - `syncChanged(false)` is emitted within a reasonable window
  - no `rxSpeechReady` chunks fire (because the codec never syncs,
    so it never emits decoded features)
  - the pipeline runs without crashing across multiple chunks

The fixture is generated in-test (not on disk) so the test stays
self-contained. A real RADE I/Q capture is needed to verify the
"synced + decoded speech" path; that fixture lands at bench-test
time per the Phase 3R N2 matrix because it requires the TX path
(Task I3) and a known clean-encoded utterance.

## Why synthetic?

The plan asks for `rx_test_iq.bin` containing canned RADE I/Q. A real
file would require:

  1. The Task I3 TX path to encode known speech, OR
  2. A live RADE transmission captured off-the-air.

Both are blocked at I2 time. The synthetic path exercises the
resample-then-rade_rx-then-FARGAN chain and pins the no-sync
contract; the syncing-and-decoding contract is bench-verified.

## Reproducibility

Test code:

```cpp
std::mt19937 rng(0xC0DEC0DEu);
std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
// Fill N * 2 floats with noise (I, Q, I, Q, ...) at 24 kHz.
```

Fixed seed `0xC0DEC0DE` and fixed amplitude `[-0.1, +0.1]` so the
test is deterministic across runs.
