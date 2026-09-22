# R-R3-31: RADE on the second receiver

## Report and reproducing evidence

The operator reported that RADE-U on the second pan did not work and appeared
to mute the first pan. At signed baseline `d9c7bce1`, RadioModel published one
RadeChannel pointer without its owning slice ID, and RxDspWorker used that
pointer only for slice 0. Selecting RADE on B therefore redirected A into B's
decoder and suppressed A's ordinary audio.

The reproducing test creates two real WDSP RX channels, assigns separate
stream routes and audio buses, and lets both ordinary slices join the actual
master mixer before selecting B. On the faulty route it reported:

```text
Aordinary=0 Bordinary=96 radeAfterA=44 radeAfterB=44
masterBefore=1 masterAfter=1
Totals: 2 passed, 1 failed, 0 skipped
```

This was a routing/mix assertion failure, not a build failure. An earlier
fixture attempt omitted WDSP initialization and was corrected before accepting
the RED evidence. The test uses the repository's synchronous real WDSP init
seam, which skips wisdom generation rather than substituting DSP behavior.

## Repair contract

Publish the owning slice and a generation to the worker. QObject lifetime and
codec ownership remain on the control thread; decoded speech returns to the
DSP worker before it enters AudioEngine. Old I/Q, old decoded output and old
destroy callbacks cannot target a replacement slice/worker/decoder.

Nereus's master mixer waits for all enrolled producers and has no timeout.
The previous RADE wrapper suppressed output while the decoder was unsynced,
based on an obsolete timer-driven-audio assumption. Restore Aether's existing
same-sized quiet output when the decoder accumulator is short:
`src/core/RADEEngine.cpp:496–504` at
`0dea0dd7d73e25a40c8c01d46873af5834e23921`. A failed or warming decoder remains
withdrawn until current output exists. No DSP/codec parameters, mix timeout,
Opus profile or transmit behavior are changed by this repair.

## Verification status

The meaningful regression above is captured and the implementation is complete.
All six affected test executables pass after correcting two independent review
findings: an unkey edge could re-enrol a warming RADE target, and synthetic late
speech made the replacement-worker acceptance assertion vacuous. The regression
now exercises the actual software MOX edge without transmitting, proves ordinary
A continues after unkey, and requires new codec speech, B audio and master-mix
progress after replacement. Fresh resamplers get a bounded 256-input-block
warm-up window; missing output remains a failure with per-stage counts.

The full build (`all_tests` and `nereusd`) passed. The final unfiltered suite
passed **685/685 executables in 148.40 seconds**, with eleven pre-existing inner
Qt skips and none in the new regression. An earlier run passed 684/685 but the
two-hour simulated audio-clock test reached its 180-second timeout during a
concurrent separate build. The final run had no competing build and passed the
unchanged clock test in 148.38 seconds. No tests were excluded or weakened.

Matching native Core and GUI installation follows this signed source checkpoint.
Hardware acceptance remains pending.

Focused build/run targets are `tst_rade_rx_multislice_routing`,
`tst_rade_channel`, `tst_audio_engine_rade`, `tst_rade_channel_model_wiring` and
`tst_stream_pool_binding`, plus affected mixer/lifecycle coverage. Final checks
build `all_tests` and `nereusd` before running the full test suite.

Receive-only hardware acceptance: keep A in ordinary receive while B enters
RADE-U, including without synchronization; confirm A continues, then restore B
to ordinary receive. Exercise slice/pan replacement and reconnect without
reviving old audio. A valid on-air RADE decode requires a real decodable signal
and is separate from the no-sync continuity check. No RF or tuner actuation is
part of this checkpoint.
