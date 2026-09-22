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

Matching native Core and GUI installation is complete at signed `dd2a9ebf`.
Hardware two-pan/RADE listening acceptance failed with stuttering on A; investigation continues below.

The first native build of `aed2278f` found a compile-only configuration defect:
the moved `attachDspWorkerForTest` definition lacked the test-build guard already
present on its declaration. The following corrective checkpoint restores that
guard without changing runtime behavior. The failed native build was never
installed; the native build and matching GUI are regenerated after correction.

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

## Matching installation, September 21, 21:11

The corrected native build (`NEREUS_BUILD_TESTS=OFF`), staged installation,
dependency checks and all 72 packaged source hashes passed. The installed
Core library SHA-256 is
`7c1b2c6356ade7ed64973df0ba5edce6ad9f42af1e3b69a8f768229f8615ee4c`.
The prior installation is retained at
`/var/lib/nereus-build/rollback-d9c7bce1-before-dd2a9ebf/`.
Core PID 10969 was active with zero service restarts after installation.

The matching GUI's executable tag, both private library UUIDs and strict/deep
bundle signature were verified before opening saved profile `radxa_5c_r3`
at `.106:50055`. GUI PID 12088 authenticated to ANAN-G2 Saturn at 21:11:49.
The initial speaker-open delay caused a packet backlog and one audio-context
recovery; encrypted display and Opus reception then resumed. The Core sample
at about 30 seconds showed approximately 48,000 source frames and 25 encoded/
accepted packets per second, no encoder/send errors and twelve source drops.
These are short observations, not a soak pass. The GUI window title confirmed
`dd2a9ebf`; a fresh screenshot was unavailable, so no new visual waterfall or
meter acceptance is inferred from that observation.

The operator has been asked to keep A in ordinary receive while selecting
RADE-U on B. The answer and real decodable-signal acceptance remain pending.

## Live cadence failure, September 21, 21:16–21:19

The operator reported that the ordinary SSB receiver stutters and cuts in/out
while B uses RADE-U. This fails the two-receiver acceptance check despite the
routing/lifecycle tests passing. With RADE active, Core's one-second summaries
showed approximately 39,000–44,000 mixed source frames and 20–23 encoded packets,
below the required 48,000 frames and 25 packets. There were no encoder failures
or transport rejections, and most intervals had no source-drop events. The GUI
repeatedly underflowed and restarted audio contexts at about one-second intervals;
its packet delivery owner-thread delay was generally 0–5 ms.

B left RADE at 21:19:06. Core immediately resumed approximately 48,000 source
frames per second and retained the same audio context for the following twelve
seconds. This correlates the deficit with the RADE path; the exact sample-loss
cause and the operator's USB comparison remain under investigation. The earlier
regression proves routing and eventual output, not sustained sample conservation.
Telemetry installation is held while this cadence defect is investigated.
