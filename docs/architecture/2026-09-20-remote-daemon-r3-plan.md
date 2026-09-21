# Remote daemon R3: working remote receive

> Execution: yonder-cost-aware-execution. Requirements and acceptance criteria
> are binding; test order and review effort follow the Yonder risk-based
> policy. Reuse approved decisions and valid evidence for unchanged code.

**Goal:** Hear the Saturn through Core on the Rock 5C and render its live
panadapter, 2D waterfall and integrated 3D waterfall in the desktop GUI.

**Architecture:** Core owns the radio, WDSP, shared FFT engines, spectrum
reducers and the mixed stereo encoder. The existing authenticated WSS session
controls a direct encrypted media peer. Opus travels as RTP/SRTP; native
display frames travel on a separate unreliable channel. GUI models and
rendering remain client-side.

**Stack:** C++20, Qt6, existing WDSP/FFTW/Opus, OpenSSL, pinned libdatachannel,
Qt Test, CMake/Ninja. Variable clock correction requires the source validation
in task 1; the fixed-ratio Resampler is not its implementation.

**Specifications:** [umbrella](2026-07-28-remote-daemon-architecture-design.md),
[R2/R3 addendum](2026-08-03-remote-daemon-r2-r3-design-addendum.md),
[identity and network design](2026-08-02-remote-station-identity-and-pairing-design.md),
[current review and decisions](2026-09-20-remote-daemon-r3-review.md).
This plan supersedes procedural boilerplate in older plans, not their
substantive safety or acceptance requirements.

This is the next phase of Claude's original plan, expanded against recovered
code and current hardware. The source-document and conversation map is in
the review. Preserve the network addendum's raced connection attempts,
UDP/TLS relay paths, dual-stack identity/pairing and carrier measurement gates.
This R3 plan validates direct media; it makes no carrier-path or relay claim.

## What completion gives the operator

The R3 deliverable is a usable split receiver: the Rock 5C boots Core, owns
the Saturn connection and receive DSP, and serves the existing NereusSDR GUI
on the Mac. The GUI tunes and controls the station, shows live spectrum and
2D/3D waterfalls, and plays one mixed stereo Opus feed with the existing
slice volume, mute and left/right placement controls. It includes the
published open-PR changes integrated into this branch, including 3D waterfall.
Their presence in the build does not make remote transmit an R3 feature.

| Checkpoint | Observable result | Completion evidence |
| --- | --- | --- |
| First display | A live Saturn spectrum and advancing 2D/3D waterfall in the remote GUI | Real board-to-Mac session; tuning and zoom remain aligned |
| First sound | The same session plays mixed stereo Opus with working slice controls | Actual listening plus two-slice mix and loss/clock tests |
| R3 accepted | Multiple pans and slices remain usable through reconnect and sustained reception | Four-pan measurements, two-hour audio run, install/boot/reconnect checks |
| R4 | Remote microphone/PTT and transmit operation | TX chain and watchdog/starvation/handoff safety acceptance together |
| R5 | Remote access across difficult internet connections without a required VPN | Direct/relay racing, dual-stack and CGNAT-to-CGNAT evidence |
| R6 | Routine installation and use without development-session help | Connection UX, reachability diagnostics, packaging and documentation |

R1/R2 establish the split and control foundation. They are not a completed
remote receiver. Codec tests, dependency probes and successful builds are
supporting evidence; no R3 checkpoint is complete until its operator-visible
result is demonstrated. There is no separate executable named NereusUI yet;
this plan uses the existing NereusSDR executable in remote station mode.

## Network decisions carried forward

The [identity/network design](2026-08-02-remote-station-identity-and-pairing-design.md)
remains authoritative, particularly sections 5.4, 9.5, 9.8 and 13. R3's
direct LAN checkpoint is not a reduction of the internet vision.

| Work | When it enters implementation | Required result |
| --- | --- | --- |
| Encrypted, ICE-capable transport; RTP/display sequencing; bounded queues | R3 now | A media boundary reusable by direct and relayed paths |
| IPv4/IPv6 MTU, mapping behavior/lifetime and uplink measurements | Prepare alongside R3; run on the actual carrier/VPS bench before final R5 transport choices | Evidence for datagram limits, traversal and keepalive policy |
| STUN candidates, connectivity checks and UDP hole punching | R5 receive work | Establish a direct path where NAT/firewall behavior permits it |
| Private rendezvous, authenticated bootstrap and relay for control as well as media | R5 receive work | Neither endpoint needs an inbound public listener or a required third-party VPN |
| Race direct and both relay paths; prefer IPv6/direct and upgrade in the background | R5 receive work | First usable connection starts reception; path changes preserve audio timing |
| DTLS/UDP 443 and TLS/TCP 443 fallback, dual-stack self-hosting | R5 receive work | Proved relay routes including the two-CGNAT case; short-lived credentials and rate/staleness limits |
| Path switches while transmitting | Only with R4's complete TX safeguards | Watchdog, starvation and unkey/handoff acceptance remain mandatory |

Recommended sequencing from this review: implement receive-only R5 traversal
immediately after R3's working LAN receiver, without waiting for all remote
TX work. R4/R5 retain their original scope labels. The carrier bench is still
a prerequisite to settling its unmeasured policy; a LAN pass cannot replace it.

The pinned libdatachannel v0.24.5 `libjuice` backend explicitly rejects
TURN/TCP and TURN/TLS in `src/impl/icetransport.cpp:159-161`. Its optional
`libnice` backend has TURN/TLS support, but has not been selected, packaged or
validated here. The current backend therefore proves only a subset of the
future ladder. Before R5 implementation is considered ready, resolve and test
the missing relay transports; do not silently omit TLS/443. Distinguish
encrypted media carried through TURN/UDP from DTLS on the client-to-relay leg.
The present direct WSS control listener also does not supply a CGNAT bootstrap:
R5 must make authenticated control/signalling reachable along with media.

Existing open items remain open: carrier measurements, exact keepalive policy,
TURN versus a custom relay where needed, seamless-switch mechanics, PAKE
dependency and rendezvous operational ownership. They were not all settled
by the earlier brainstorming, and this review does not present them as such.

## Global constraints and requirement map

| ID | Required behavior |
| --- | --- |
| R-R3-01 | Core produces FFT frames headlessly from actual stream I/Q. Pans stay client-local; endpoints are explicit session-owned subscriptions. |
| R-R3-02 | Authentication, snapshot readiness and the active session generation gate media negotiation and delivery. Preemption, disconnect and reconnect discard the previous peer and all media state. |
| R-R3-03 | Retain WSS control, separate RTP/SRTP Opus audio and unreliable native display media. Actual media UDP datagrams meet the parent 1000-byte cap, including transport overhead. |
| R-R3-04 | Trace and waterfall retain independent detector/averaging settings. Optional bounded 3D coverage supports the integrated renderer without shipping full FFT arrays. |
| R-R3-05 | Codec error is bounded, packet loss cannot silently corrupt a delta chain, and all parser allocations and queues have explicit limits. |
| R-R3-06 | One 48 kHz stereo master is captured after per-slice gain/mute/pan and the mixer barrier, before local speaker gain/mute/device. No network or codec work runs on the DSP callback. |
| R-R3-07 | Audio jitter, loss and independent clocks are handled continuously. Client playback mute flushes queued audio and can suspend encoding without mutating station slice gain. |
| R-R3-08 | Two FFT tiers can coexist per stream, with explicit capacity refusal/downgrade. Four pans share a session-wide display budget; hidden endpoints are explicitly disabled. |
| R-R3-09 | Frequency/context changes, slice ID reuse, layout changes, float/dock and reconnect do not apply stale media or leak endpoints. |
| R-R3-10 | Installable GUI/Core builds preserve local direct mode, source attribution and settings ownership. Remote receive requires real hardware and long-session evidence. |
| R-R3-11 | Core restores station RF gain controls and produces antenna-calibrated display levels. The remote GUI applies no second local calibration; local direct rendering retains its existing calibration. |

The authorized radio is the ANAN-G2/Saturn, MAC `2C:CF:67:AB:FC:F4`, board
`0x0A`; the September 20 instruction supersedes the old G2E-only bench rows.
All live tests are receive-only. Remote TX stays disabled. No public post,
push or PR update is part of this implementation checkpoint.

Keep core GUI-free, use AppSettings, preserve upstream notices/comments,
sign commits, and use dedicated profiles for every test process. Never place
tokens, private keys or passwords in this plan, source, test fixtures or logs.

## Shared interfaces and sequencing

New media code lives under `src/core/session/media/`; rendering integration
lives in `src/gui/RemoteMediaController.{h,cpp}` and SpectrumWidget. Existing
session and model classes retain their thread ownership and synchronous echo
guards. Library callbacks must queue owned data onto the correct Qt owner;
they must not call RadioModel or AppSettings on a library worker thread.

- `MediaSourceKey { int streamIndex; FftTier tier; }`, where `FftTier` is
  `Wide` or `Fine`. This identifies production, not a pan or hardware DDC.
- `SpectrumSubscription` contains a session-local `quint32 endpointId`,
  source/resolution request, enabled state, crop center/span, pixels, target
  fps, frames-per-line, independent plane reduction settings, quantization
  range and optional 3D coverage request. The daemon returns an accepted
  `SpectrumContext` with actual values and a new context generation.
- `ReducedSpectrumFrame` carries endpoint/context identity, encoder sequence,
  producer/sample timing, exact frequency coverage, independent trace and
  waterfall arrays, waterfall-advance flag, and optional wide-row coverage.
  Context changes reset reduction and codec history. An accepted context is
  required before any matching media frame can render.
- `IMediaTransport` exposes start/stop, local signalling output, validated
  remote signalling input, nonblocking display send, RTP send, owned receive
  buffers, readiness and failure. It carries no model mutation logic.
- `RemoteAudioTap` writes borrowed interleaved float samples into a bounded
  SPSC ring synchronously; ownership ends when the callback returns.
  `RemoteAudioSender` drains it off the DSP thread. `RemoteAudioReceiver`
  owns packet ordering, Opus state, clock correction and the playback ring.

Task 1 establishes dependency/API contracts. Tasks 2 and 3 may then run in
parallel with separate file ownership. Task 4 integrates them. Task 5 completes
audio. Task 6 expands capacity and runs the final acceptance. Do not parallelize
CMake, session-message schemas, MainWindow edits or hardware operations.

## 1. Close dependency and clock-correction contracts

**Requirements:** R-R3-02, 03, 07, 10. **Dependencies:** existing R2.

**Deliverable/files:** pinned dependency manifest/build helper in `cmake/`,
dependency notices in the existing attribution inventory, and a compact
verification record under `docs/architecture/2026-09-20-remote-daemon-r3-verification/`.

- [x] Recover source and existing design decisions; record mixed stereo.
- [x] Verify the saved Opus mode warning against pinned source and packets.
- [x] Validate libdatachannel v0.24.5 and its exact transitive revisions,
  licenses, media-enabled build and standalone direct peer exchange.
- [ ] Prove an unreliable display message and separate RTP packet over real
  encrypted loopback peers without an external STUN service. Stop/recreate
  peers, and capture packet sizes with the configured MTU. A small application
  payload alone does not prove the UDP-size requirement.
- [ ] Validate a continuously adjustable resampling implementation from an
  existing source, preserving phase/history across ratio changes. Inspect the
  existing WDSP variable-rate path before proposing a new dependency. Record
  exact source and API; do not use periodic ring flushes as drift correction.
- [x] Add the pinned production dependency after its transport source gates;
  expose it through NereusCore without adding GUI linkage or an uncontrolled
  second Opus copy. Include daemon-component install dependencies. The
  independent resampling gate remains a prerequisite for audio playback.

**Verification:** dependency build and two-peer integration, actual packet
inspection, deterministic variable-rate sample accounting. Native ARM and
Windows packaging remain distinct checks. A failed prerequisite blocks its
dependent implementation, not independent codec work.

**Delegation:** Terra for bounded source/API investigation; Sol only if the
integration exposes a difficult platform/linking failure.

## 2. Wire daemon production and independent spectrum planes

**Requirements:** R-R3-01, 04, 08, 09. **Dependencies:** source contract above.

**Files:** `DaemonApp.{h,cpp}`, `FftEnginePool.{h,cpp}`, new
`session/media/SpectrumEndpoint.{h,cpp}` and source/tier types; tests
`tst_remote_fft_production`, `tst_spectrum_endpoint`.

**Consumes/produces:** stream I/Q and source timing into pool engines;
`fftReadyLinear` into one reducer per endpoint plane; produces a latest
`ReducedSpectrumFrame` and accepted `SpectrumContext`.

- [x] Give DaemonApp ownership of a live pool, connect the same per-stream
  I/Q producer used by local mode, and tear it down before RadioModel streams.
- [ ] Add `(stream,tier)` lifecycle without changing existing local callers'
  default Wide behavior. Derive FFT sizes from resolution targets, clamp to
  supported sizes, report unmet requests, and never resize an unrelated pan's
  wide engine to satisfy a fine request.
- [ ] Validate subscription values and ownership; clamp output pixels to the
  available source bins and report the clamp. Keep a latest frame slot, not
  an unbounded Qt frame queue.
- [x] Use separate SpectrumReducers for trace and waterfall. Apply cadence
  before transport encoding and preserve independent averaging histories.
- [x] Extract the existing 3D wide-crop/peak-preserving behavior into a
  GUI-free helper with its original attribution. Produce bounded coverage
  and at most the renderer's 768 wide samples. Do not substitute another
  detector/averaging formula for the current local wide-row behavior.

**Acceptance:** synthetic stream I/Q produces frames with no MainWindow;
two endpoints on one stream have independent reductions; deep zoom does not
lengthen a neighboring wide FFT; out-of-range crops produce no stale frame;
disable/unsubscribe and slice reuse release the right producer; optional 3D
peaks and frequency coverage match the local helper.

**Verification:** focused Qt core tests and one receive-only daemon capture on
the Rock 5C. Build targets before `ctest -R ... --no-tests=error`.

## 3. Implement the bounded display codec

**Requirements:** R-R3-03, 04, 05, 09. **Dependencies:** frame/context contract;
can proceed independently of the peer library.

**Files:** new `session/media/DisplayCodec.{h,cpp}`, frame codec types,
`tests/tst_display_codec.cpp`; explicit CMake registration.

**Interfaces:** encoder accepts a reduced frame and current context, emits
bounded native bytes; decoder returns either a complete decoded frame,
`NeedKeyframe`, or a typed rejection. Context application and reset are
explicit. Parser failure never changes the last accepted frame/history.

- [x] Define and check in version 1's exact byte layout before connecting
  sockets: network byte order, endpoint/context/sequence fields, bounded
  plane lengths, flags, and no native struct serialization.
- [x] Quantize dBm over the negotiated finite min/max window. Encode temporal
  residuals against the decoder's reconstructed previous frame, using
  adaptive block widths and an absolute/keyframe fallback when deltas expand.
- [x] Specify the accuracy/dead-zone setting and prove its error bound over
  long ramps; errors must not accumulate invisibly through the delta chain.
- [x] Keyframe on first frame/context change, periodically, and on reliable
  request. Reject deltas after a gap until a valid keyframe. Rate-limit keyframe
  requests, accept sequence wrap correctly and reject stale generations.
- [ ] Carry trace and waterfall independently, plus optional bounded 3D row.
  Measure maximum frame size and the peer library's fragmentation behavior;
  cap reassembly memory and transport backlog.

**Acceptance:** quantization error no greater than half one quantization step
for clipped in-range samples at the precise setting; explicitly documented
larger bound for each rate-control setting. Empty, nonfinite, truncated,
oversized, unknown-version and malformed-block inputs reject without state
mutation. Deterministic loss/reordering tests recover at the next complete
keyframe and never display an invalid delta result. Noise and peak fixtures
cover 4096-point full-span input, both planes and 3D overhead.

**Verification:** portable codec tests; fuzz-style bounded malformed inputs;
measured encoded bytes and errors. Synthetic data is codec evidence, not an
RF quality or network-throughput claim.

## 4. Deliver live spectrum through the authenticated session

**Requirements:** R-R3-02 through 05, 09. **Dependencies:** 1 through 3.

**Files:** media transport implementation, `StationServer/StationClient`,
`SessionMessages`, `StationCapabilities`, `RemoteMediaController`, MainWindow,
SpectrumWidget; tests `tst_remote_media_session`, `tst_remote_spectrum_render`.

- [x] Add a minor-version-negotiated media capability and bounded signalling
  messages. Authenticate first; bind peer SDP/certificate fingerprints and
  candidates to the accepted control session. Old peers retain control-only
  behavior and receive no unsupported media messages.
- [x] Establish direct DTLS/SCTP and SRTP using libdatachannel. Retain current
  WSS command/state handling. Configure display unordered with no retries;
  keep RTP separate. Reject oversized signalling before library parsing.
- [ ] Add subscribe/context/enable/unsubscribe/keyframe control messages with
  explicit endpoint ownership. Late library callbacks cannot revive an old
  session. A failed peer stops its producers and leaves control responsive.
- [x] Decode into a dedicated external reduced-frame widget entry point.
  Do not feed reduced dBm into the local linear-FFT path and reduce it again.
  Advance 2D and 3D from the decoded waterfall cadence; use explicit supplied
  wide coverage rather than a fictitious full-DDC cache.
- [ ] Preserve immediate crop/zoom within the negotiated margin and update
  endpoint coverage at the existing gesture boundary. Reconfiguration gets
  a new generation and keyframe; old-frequency frames cannot repaint it.

**Acceptance:** one live Saturn pan and both waterfall modes advance;
retune, zoom and band changes preserve frequency alignment; deliberate loss
recovers; preemption/reconnect has no stale rows, leaks or duplicate feeds.
An unauthenticated or old-session peer cannot subscribe or deliver media.

**Verification:** real loopback peer tests, fake-radio/session boundary tests,
offscreen renderer contracts plus a real GPU visual smoke. Then the Rock 5C
LAN run. This milestone is a display checkpoint, not completed R3.

## 5. Deliver mixed stereo Opus with continuous playback

**Requirements:** R-R3-02, 03, 06, 07, 09. **Dependencies:** 1 and 4's session
lifecycle; audio codec unit work can precede GUI spectrum completion.

**Files:** `AudioEngine`, new `RemoteAudioTap/Sender/Receiver`,
`OpusAudioCodec`, `AudioJitterBuffer`, variable resampling adapter and playback
adapter; tests `tst_remote_audio_mix`, `tst_opus_audio_codec`,
`tst_remote_audio_clock`, `tst_remote_audio_session`.

- [x] Tee exactly one successful master drain before speaker volume/mute.
  Preallocate bounded storage with nonblocking producer admission; use an
  explicit overflow/reset policy, counters
  and bounded latency. Do not retain a callback pointer or encode on DSP.
- [ ] Use the existing pinned Opus build, 48 kHz stereo and standard RFC 7587
  packet/timestamp semantics. Begin with the saved 24 kbit/s, constrained VBR,
  40 ms, AUDIO/MUSIC, complexity 10 profile and explicit wideband. Compare
  higher-rate stereo settings during listening; report actual channels,
  packet bandwidth, bytes and CPU. Do not reinterpret the old mono benchmark
  as a measured stereo result.
- [ ] Add bounded ordering/jitter, Opus loss concealment and continuous clock
  correction with preserved interpolation state. Start near the design's
  180 ms receive-buffer target; make timing injectable for tests.
- [ ] Drive a client playback bus independently of local RadioModel DSP.
  Local output trim/mute must work, mute flushes queued samples, and encoding
  suspension is a session operation. Resume starts a fresh audio generation.
- [ ] Sample-rate, mode, band and slice transitions preserve a 48 kHz mixed
  output contract or explicitly reset audio context when required. Close
  cancels pending work before rings, codec and device storage are destroyed.

**Acceptance:** two test slices yield one stereo block per period, retain
left/right placement and mute, and work with no daemon sound device. Offline
clock tests cover both signs of 500 ppm over a simulated hour with bounded
occupancy and no periodic drop/duplicate repair. Loss/bursts conceal and
recover; old SSRC/generation packets cannot play after reconnect. Stereo
listening on the real client is required in addition to packet receipt.

**Verification:** unit/integration tests, measured Rock 5C encoding and client
playback, then real long-session audio. Lossless/high-rate digital-mode audio
remains a tracked parent requirement; do not advertise Opus as transparent
input for weak-signal decoding or claim that path accepted without its gate.

## 6. Capacity, installation and release evidence

**Requirements:** all. **Dependencies:** integrated receive path.

**Files:** daemon/client capability policy, endpoint budget allocator, install
rules, configuration sample and the R3 verification ledger.

- [ ] Exercise one pan, four docked pans, floating pans and multiple slices
  sharing a stream. Allocate the session budget across focused/background
  endpoints; reduce display rate/pixels before impairing audio or control.
- [ ] Measure full-span/noise and deep-zoom cases with both FFT tiers, WDSP,
  stereo Opus and 3D enabled on the Rock 5C. Report actual effective limits;
  preserve the separate Pi 4 hardware-floor obligation until measured there.
- [ ] Verify the combined full desktop suite once after integration, plus
  ARM-sensitive focused tests and Linux daemon-component staged install.
  Do not hide known failures through exclusions or call a partial run green.
- [ ] Install a signed checkpoint with a recoverable previous binary/library
  set, preserve private station configuration, and verify boot, clean stop,
  client reconnect and live media. Provide a launcher using private pairing
  configuration outside the repository.
- [ ] Run at least two hours of real audio with counters for underrun,
  overflow, loss, jitter, drift ratio, CPU, memory and thermal state. Include
  an explicit client disconnect/reconnect and verify stale audio is flushed.
- [ ] Record one-pan and four-pan on-wire budgets and delivered quality.
  The parent screenshot figures are comparison targets, not acceptance data
  for this implementation. Complete its internet-capable receive evidence
  separately from LAN success; CGNAT-to-CGNAT remains the R5 acceptance case.

**Verification:** fresh software results, real transport observations and
hardware/operator observations recorded separately as passed, failed or
pending. An advancing waterfall alone does not complete the release gate.

## Review focus and current ledger

The integrated review focuses on session-generation races, malformed media
and bounded memory, audio callback ownership, independent clock convergence,
frequency/context correctness and 3D/local rendering parity. Each is covered
by an acceptance case above. Independent review is consolidated at that
boundary; routine findings get one focused correction rather than repeated
whole-plan review loops.

| Work | Owner/status | Evidence/next action |
| --- | --- | --- |
| R2 baseline | Preserved rollback checkpoint `14124e7c` | Authenticated control/tuning/meter baseline retained; installed runtime advanced to `ea55d24a` |
| Combined open-PR recovery | Complete source checkpoint `14124e7c` | GUI/Core build and unfiltered 662/662 desktop tests pass; fixture-path workaround recorded separately |
| Native combined Core | Installed at signed `ea55d24a`, with `14124e7c` rollback | ARM build/stage/install and live Saturn display pass; 298 changing frames in 20 seconds; [verification ledger](2026-09-20-remote-daemon-r3-verification/README.md) |
| Plan review and mixed-stereo choice | Lead, recorded | Current review; user confirmed mixed stereo and Opus |
| Direct media transport | Production adapter and MediaPeer integrated | Real encrypted peers, bounded callbacks, stop/restart/delete regressions pass; live display capture passes: Ethernet, 969-byte maximum IP packet; separate SRTP-size proof pending |
| Headless FFT and display codec | Implemented and component-tested | Independent Wide/Fine sources, retune input reset, independent planes, crop clamp, bounded codec and recovery tests pass; authenticated daemon-to-GUI regression and live Saturn display pass |
| Audio | Opus/RTP and mixed-audio capture tests pass | Stereo wideband 24/48 kbit/s packet checks, mixer gain/mute/pan, bounded capture timestamps and retirement pass; sender, jitter, adaptive clock correction and playback remain pending |
| GUI media wiring | Desktop builds; initial focused checks pass | Dedicated reduced-frame renderer and authenticated subscription controller tested; two-pane, shared-window and reconnect regressions pass; live GPU spectrum/2D observed; operator 3D confirmation and gesture refinement pending |
