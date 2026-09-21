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
[current review and decisions](2026-09-20-remote-daemon-r3-review.md),
[September 21 controls/accessory audit](2026-09-21-remote-gui-control-gap-audit.md).
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
| R3 accepted | Multiple pans and slices remain usable through reconnect and sustained reception; connection, audio and receive-safe accessory controls show accepted station state | Four-pan measurements, two-hour audio run, install/boot/reconnect checks, visible-control and accessory acceptance |
| R4 | Remote microphone/PTT and transmit operation | TX chain and watchdog/starvation/handoff safety acceptance together |
| R5 | Remote access across difficult internet connections without a required VPN | Direct/relay racing, dual-stack and CGNAT-to-CGNAT evidence |
| R6 | Routine installation and use without development-session help | Full station selection/pairing UX, reachability diagnostics, packaging and documentation; basic connection controls and visible state are R3 requirements |

R1/R2 establish the split and control foundation. They are not a completed
remote receiver. Codec tests, dependency probes and successful builds are
supporting evidence; no R3 checkpoint is complete until its operator-visible
result is demonstrated. There is no separate executable named NereusUI yet;
this plan uses the existing NereusSDR executable in remote station mode.

**Current remaining order, September 21:** finish basic connection controls,
snapshot hydration and capability gating (4a/4c); block receive-only accessory
tuning side effects, then repair station accessory connection/status (4d);
finish wideband parity and sustained audio with operator feedback (4b/5/5a);
run capacity, boot/reconnect and two-hour acceptance (6). Receive-only R5
traversal follows, then safeguarded R4 TX and R6 selection/pairing/packaging.
The audit is complete; these newly identified implementation tasks are open.

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
| R-R3-12 | Remote Clarity receives Core's unsmoothed full-source noise-floor estimate before display reduction/quantization, with station calibration applied once. The GUI retains its existing smoothing, deadband, TX/manual-override gates and palette; stale or inactive endpoint measurements cannot drive the active pan. |
| R-R3-13 | Remote applet and slice S-meters select Core's independent calibrated peak/average readings, or the decoded display's passband Max Bin. They follow active slice identity and never poll local DSP or add client calibration. Disconnect clears the live reading, and reconnect waits for the current snapshot; RX telemetry remains read-only. |
| R-R3-14 | Remote CH/BPF/WIDE indicators show Core's effective per-chain filter state and reason, including initial snapshot and reconnect. Client-local Alex bookkeeping cannot stand in for station state. |
| R-R3-15 | Remote Auto AGC-T visuals reflect the station's per-slice AGC noise-floor state. GUI Clarity smoothing is not an AGC measurement source. |
| R-R3-16 | Remote GUI Connect/Disconnect actions operate the configured Core station session. Disconnect cancels pending retries; explicit Connect starts a fresh session without relaunching or invoking direct-radio discovery. |
| R-R3-17 | The GUI persistently identifies its configured Core and shows disconnected, connecting, retrying, or connected state independently of Core's radio state. Failed attempts leave a useful reason and available recovery/cancel action. A Core session with an offline radio must not appear to be a disconnected GUI session. |
| R-R3-18 | Remote C-Tune preserves the Core receive-window centre during in-window VFO tuning. Explicit pan motion moves Core's window and shifts every cohost without moving their VFOs. Pin state belongs to the stream, is negotiated, clears with the station session, and is restored from GUI preference after reconnect; direct local behavior remains unchanged. |
| R-R3-19 | Remote frequency-scale and wheel zoom stop at the currently supplied DDC bandwidth during the gesture. An unsupported ADC-wide view must not be offered then collapsed by Core's crop ACK. Preserve the local extended-view preference; wider remote coverage requires a separately advertised wideband source. |
| R-R3-20 | Restore the existing extended-pan behavior across Core/UI: supported wideband ADC data fills the view outside the listenable DDC island, with the existing zoom gestures, RF alignment, wing tuning and filter-state feedback. Core owns capture, calibration and shared ADC/BPF demand. The R-R3-19 DDC-only limit is an interim compatibility fallback, not completion of receive parity. |
| R-R3-21 | Visible remote receive controls act on their declared local or station owner and show accepted state. Controls unavailable in the negotiated role/capabilities are visibly disabled with a reason. TX affordances respect `txPermitted=false` in R3; Core refusal remains mandatory. A visible-control inventory and interaction tests distinguish implemented behavior from unavailable features. |
| R-R3-22 | Core owns station accessory sockets, discovery, live configuration application and basic connect/disconnect/cancel. It validates device identity before declaring a TGXL connected, cancels obsolete retries, and publishes current status. Tuner telemetry applies without commands; headless frequency/mode reporting follows the stable station TX-bound slice. Direct local behavior is preserved. |
| R-R3-23 | Remote audio exposes persistent playback/media/output status, the actual accepted codec profile and useful measured health. Local device/trim/mute stay separate from station slice mix controls. Any quality selector requires measured profiles and acknowledged Core configuration; unimplemented codec settings are not presented as functioning controls. |
| R-R3-24 | Attaching/reconnecting and restoring saved GUI layout hydrates the authoritative snapshot without outbound slice creation. One saved pan attaching to one existing station slice retains that slice. Explicit post-hydration operator add/layout actions still work within station capacity. |
| R-R3-25 | Receive-only remote frequency changes, snapshot replay and inbound accessory telemetry cannot initiate tune-carrier orchestration or TX-coupled accessory commands. Apply the guard before enabling repaired TGXL connection/telemetry. Local-direct operation retains its established behavior; authorized remote TX/tuner workflows remain R4. |
| R-R3-26 | A configured Core listener recovers when its bind address becomes available after startup. A running radio service must not silently remain without remote control after a transient bind failure. Retry is bounded/backed off, preserves station identity, and is cancelled by stop/reconfiguration; readiness and errors are observable. |
| R-R3-27 | A daemon that starts before its configured radio is discoverable can later recover that same radio without a daemon restart. Discovery retries must preserve the pinned radio choice, station identity and slice ownership, remain cancellable, and keep the Core control plane responsive. Listener recovery alone does not establish radio recovery. |

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
The original numbering is retained; the current remaining order above takes
precedence. Tasks 4c and 4d consume the same session/model boundary and must
agree their capabilities and reply semantics before editing shared files.

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
- [x] Validate a continuously adjustable resampling implementation from an
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

## 4a. Complete receive telemetry bindings

**Requirements:** R-R3-13 through R-R3-17. **Dependencies:** authenticated
state mirror and decoded remote display.

- [x] Publish separate read-only `SliceModel.signalPeakDbm` and
  `signalAverageDbm` from the existing Core meter pump. Preserve the legacy
  selected `signalStrengthDbm` property and local direct behavior.
- [x] Feed the remote applet and slice meters from these properties,
  selecting the existing peak/average modes locally. Obtain Max Bin from
  each slice's decoded passband. Start the GUI meter timer without waiting
  for a local WDSP channel, and clear readings on disconnect.
- [x] Mirror effective per-chain BPF/WIDE state and reasons; adapt the
  existing CH indicator bindings and replay state after reconnect.
- [x] Mirror station Auto AGC-T measurements and bind the applet/flag
  visuals without feeding back client-derived noise estimates. Core now owns
  a bounded per-stream FFT/tracker independently of display subscriptions;
  both the existing threshold tick and mirrored visuals use that tracker.
  Source and session regressions pass; live deployment acceptance is recorded
  separately and remains pending at this source checkpoint.
- [x] Route remote-role menu/shortcut Connect/Disconnect actions to the saved Core
  session, including cancellation during retry backoff and a fresh explicit
  connection on the same client. Retain direct-radio behavior for local mode.
  Live GUI acceptance is recorded separately in the verification ledger.
- [ ] Complete the disconnected title/status/pan click-to-connect paths;
  each currently reaches the suppressed local ConnectionPanel. Keep explicit
  operator actions separate from automatic panel-open callbacks so a manual
  Disconnect cannot cause an immediate redial during session teardown.
- [ ] Add persistent configured-Core identity and connection/retry/error
  feedback to the existing GUI chrome. Show Core connectivity and radio
  connectivity separately. Offer a clear retry action and cancellation;
  expiring toasts and log-only socket errors are insufficient.
- [ ] Verify all visible entry points during first connection, unreachable
  Core, retry backoff, manual cancellation, recovered Core, and connected
  Core with its radio offline. Repeat a manual disconnect after a successful
  session to catch retained-name auto-open callbacks. Treat this as a basic
  R3 usability gate before the two-hour receive acceptance run.

**Verification:** real Core meter reads, read-only mirror round trips,
source selection, stable slice IDs, disconnect/TX/lifetime tests, unchanged
local tests, then visible needle and numeric movement on the Saturn feed.
BPF and Auto AGC-T require their own state/snapshot/reconnect regressions
and live observations; the meter fix does not establish those gates.

The full connection-screen discussion remains a separate follow-up. Preserve
the identity design's three groups: radios on this network, stations on
this network, and paired stations. R5 supplies the remote reachability paths;
R6 completes the selection/pairing experience and packaging. The September 21
operator report brings basic feedback and consistent actions forward into
R3; the earlier menu-only round trip did not prove the whole interface.

## 4b. Restore wideband extended-pan parity

**Requirements:** R-R3-04/08/09/10/11/14/18/19/20. **Dependencies:** accepted
remote spectrum contexts and authenticated receive controls. This explicit
follow-through was added after the September 21 operator report; the older
R3 `Wide` FFT tier and optional 3D wide rows are DDC-bounded and do not supply
ADC-wide wings.

**Existing specification:** [Wideband Extended Pan plan](2026-05-26-phase3f-sub-epic-f-wideband-plan.md)
and the parent multi-pan design, section 7. Reuse the implemented behavior in
`WidebandFrameAccumulator`, `WidebandFftEngine`, RadioModel's per-ADC production
and demand reconciliation, and SpectrumWidget's local extended rendering.

**Owned files:** `src/core/session/{StationCapabilities,SessionMessages}.*`,
`src/core/session/media/` display contracts and endpoint production,
`src/models/{RadioModel,SliceModel}.*`,
`src/gui/{RemoteMediaController,SpectrumWidget,MainWindow}.*`, and their
remote-spectrum, extended-wing and filter-state regression tests.

**Interfaces:** consume `RadioModel::widebandSpectrumReady(adcIndex, bins)`
and Core's effective ADC/filter identity. Produce a negotiated, bounded
wideband display source alongside the DDC source, with explicit RF geometry,
calibration and current-session/context identity. Exact wire representation
is a discovery deliverable below; do not silently reinterpret `FftTier::Wide`
or transmit unrestricted full FFT arrays.

- [ ] Trace the current local extended-pan contract end to end, including
  wing click/drag behavior, physical ADC versus filter-chain mapping, BPF
  bypass ownership and restoration, and 2D/3D history. Record the proposed
  capability, source identity and reduced-frame schema before implementation.
- [ ] Carry Core-produced, calibrated wideband display data through the
  authenticated session under the existing datagram and session budgets.
  Aggregate demand across pans; hiding/removing one pan must not disable
  another pan's capture or leave bypass enabled after the last consumer.
- [ ] Composite the DDC island and wideband wings using their actual RF
  coverage. Preserve the existing local zoom range on capable hardware;
  keep the DDC fallback only when the remote source is unavailable. Wing
  tuning must use Core commands and preserve shared-slice constraints.
- [ ] Verify zoom across the DDC edge, release without snap-back, inward
  zoom, ADC changes, shared pans, rejected moves and reconnect. Reject stale
  wideband frames and apply calibration once. Verify real Saturn wideband
  capture and BPF transitions without transmitting.

**Verification:** meaningful integration tests for source identity, bounds,
RF alignment, multi-pan demand and filter restoration; existing local-wing
tests remain green. Compare the same gestures and frequency coverage in local
and remote receive modes. Record live 2D/3D behavior, network cost and Rock 5C
load separately; R3 parity remains open until those observations pass.

## 4c. Complete visible controls and snapshot-safe startup

**Requirements:** R-R3-02/09/10/16/17/21/24.
**Dependencies:** existing StationClient lifecycle and task 4a connection
entry points. This closes the interaction gaps identified by audit C01-C04
and C10; it does not replace the R6 station-selection design.

**Owned files:** `src/gui/{MainWindow,SetupDialog}.*`, affected setup/applets,
`src/core/session/StationClient.*`, `src/models/RadioModel.*`,
`tests/tst_remote_gui_gating.cpp`, session/snapshot tests, and a compact
control acceptance matrix in the existing R3 verification directory.

**Interfaces:** consume station lifecycle, snapshot readiness, generation,
negotiated capabilities and Core radio state. Produce consistent actions and
presentation from those values. Session connection is not snapshot readiness;
restoring presentation must not imply a station create command. Reuse the
existing capability schema; negotiate any genuinely missing capability before
advertising it. Preserve explicit operator add/layout operations after hydrate.

- [ ] Inventory currently visible menus, applets and setup controls by owner:
  GUI-local, station-backed, or unavailable in remote receive. Record the
  concrete handler/property and acceptance case. Trace generic mirrored DSP
  setters before labelling them broken; the observed FIR-graph gap is a local
  visualization dependency, not evidence that all DSP settings fail.
- [ ] Complete task 4a's title/status/pan connection actions and persistent
  status. Keep automatic panel callbacks separate from explicit connect.
  Disable unavailable local-resource controls with a reason, and gate all
  TX entry points using the negotiated permission while retaining Core guards.
- [ ] Reproduce the extra-slice startup through the actual connection,
  snapshot and `populateEmptyPans` path. Separate hydration/layout restoration
  from explicit operator creation; do not delete an existing station slice
  to conceal an unintended create. Test reconnect and delayed snapshots.
- [ ] Add a narrow injected session/presentation harness that exercises the
  real action routing. Slot-existence checks alone do not close this task.

**Acceptance:** each connect entry point starts one configured-Core attempt;
cancel during backoff leaves it stopped through delayed callbacks; Core-up /
radio-down presents distinct state. With one saved pan and one Core slice,
connect/reconnect emits zero add-slice commands and preserves station identity.
An explicit add after hydrate emits one valid request and handles refusal.
TX and FIR graph controls show unavailable state when their capabilities or
resources are absent. Local direct connection and layout behavior remain valid.

**Verification:** session/hydration and cancellation are consequential state
transitions: establish reproducing integration cases before changing them.
Build `tst_remote_gui_gating`, `tst_session_verbs` and `tst_remote_role_inert`
before running the corresponding `ctest --test-dir build-integration -R
'^tst_(remote_gui_gating|session_verbs|remote_role_inert)$' --no-tests=error`.
Register/build any additional harness target explicitly. Then perform the
listed connect/cancel/reconnect cases in the actual GUI with receive only;
record both automated and visible results in the matrix.

**Execution:** bounded Terra implementation is suitable after the lifecycle
contract is settled; the lead owns shared MainWindow/session integration.
Do not dispatch an overlapping MainWindow editor for task 4d.

Current bounded implementation evidence is recorded in the
[control acceptance matrix](2026-09-20-remote-daemon-r3-verification/remote-controls.md).
Connection presentation, snapshot guards, selected TX/FIR controls and tuner
telemetry now have interaction/session regressions. Full visible-control
inventory and actual GUI acceptance are still required; the checkboxes above
remain open until their complete acceptance is demonstrated.

## 4d. Make basic station accessory use work remotely

**Requirements:** R-R3-02/10/21/22/25. **Dependencies:** source audit C06-C09,
C11-C12, authenticated station commands and state/settings ownership.
Receive-only guards precede repaired live connection and telemetry acceptance.

**Owned files:** `src/core/{TgxlConnection,PgxlConnection,SmartSdrApiListener}.*`,
`src/models/{RadioModel,TunerModel}.*`, session capabilities/messages/client/server,
`src/gui/setup/{CatNetworkSetupPages,FourO3APage,TgxlAdvancedPage}.*`,
`src/gui/{MainWindow,applets/TunerApplet}.*`, accessory/session tests and the
control acceptance matrix. Core remains GUI-free.

**Interfaces:** station-scoped accessory settings already persist through
SettingsProxy. Define the missing live-application and basic connect/disconnect
contract with explicit accepted/refused replies and current-session ownership.
Choose atomic configure/connect or acknowledged settings followed by an ordered
command; do not let a click race persistence and use an old endpoint. Inbound
TunerModel state uses a client-only assign/notify adapter, never its hardware
command hook. Discovery is station-side, or its remote button is explicitly
unavailable until implemented. Full administration/pairing remains R6.

- [ ] Establish regressions for remote band-change auto-recall and applet
  reactions to `isTuning`. Guard RX-only operation and telemetry replay from
  autotune/carrier/operate/bypass/relay/antenna commands; preserve the existing
  local-direct workflow. This guard is part of the telemetry change, not a
  later R4 cleanup. Do not enable remote TX as a connection repair.
- [ ] Validate TGXL identity before declaring connected/present or enabling
  commands. Derive supported model aliases and serial correlation from actual
  discovery/info evidence; a V banner alone is insufficient. Reject a PGXL
  response and show expected versus observed device. Bound handshake timeout.
- [ ] Implement unconditional cancellation across connecting, handshaking,
  connected and backoff states for disable, disconnect, radio teardown and
  endpoint replacement. A cancellable timer or generation check must prevent
  an old captured endpoint from redialling after replacement.
- [ ] Route basic remote configuration/apply/connect/disconnect to Core;
  show selected endpoint, identity, pending state and error. Apply all 13 tuner
  snapshot/update fields safely, including false/zero values and disconnect.
  Gate applet orchestration while retaining read-only visual updates.
- [ ] Move existing SmartSDR frequency/mode seeding and PGXL band propagation
  from MainWindow to the authoritative Core slice wiring. Follow the stable
  TX-bound slice, including binding change/removal, and remove duplicate GUI
  sends. Retain source attribution and current wire behavior.
- [ ] Resolve the separate real-device control-port issue with read-only
  evidence. The current `.235:9008` setting addresses the amplifier; `.234`
  advertises the tuner but TCP 9010 times out from both hosts. Another-client
  occupancy is a question, not a proven cause. After the guards pass, correct
  the endpoint and verify basic connection/status without tuning or RF.

**Acceptance:** a PGXL banner/status never yields a connected tuner; supported
tuner identity does, and unknown/timeout responses leave an actionable error.
Disable/disconnect in every lifecycle state causes no delayed redial, including
after endpoint changes. A remote configuration action applies on Core without
restarting it and opens no GUI-side accessory socket. All 13 fields hydrate,
update and recover without any hardware command, including an `isTuning=true`
snapshot. With auto-recall enabled and a stored memory fixture, remote RX band
changes send no autotune. Headless initial, within-band, mode and rebind updates
report the correct station slice; unrelated slices cannot drive the accessory.

**Verification:** establish meaningful authorization/lifecycle regressions
before changes. Reuse observed wire fixtures, never invented TGXL identity
strings. Build/run affected targets including `tst_tgxl_connection_parse`,
`tst_tgxl_connection_ping`, `tst_pgxl_connection_reconnect`,
`tst_tuner_model_apply_status`, `tst_tuner_applet_context_menu`,
`tst_settings_proxy`, `tst_session_verbs` and `tst_remote_role_inert`; register
new lifecycle/telemetry/headless-boundary tests where coverage is absent.
Use the corresponding exact-name `ctest -R` selection with `--no-tests=error`.
Then inspect station-reported frequency/mode and tuner status on the real
bench without RF. Hardware acceptance stays pending while TCP 9010 is
unavailable. Actual tuner operation and pairing/interlock changes remain R4.

**Execution:** Sol is appropriate for this ownership/lifecycle boundary after
the lead settles command and capability contracts. Keep hardware and shared
session/MainWindow edits serial; use one integrated risk-focused review.

## 5. Deliver mixed stereo Opus with continuous playback

First sound is implemented and heard on the bench. Remaining playback
interruptions, the profile comparison and final two-hour stability run are
separate open gates. Follow the current remaining order above; R5 traversal
is not a prerequisite for LAN listening.

**Requirements:** R-R3-02, 03, 06, 07, 09. **Dependencies:** 1 and 4's session
lifecycle; audio codec unit work can precede GUI spectrum completion.

**Files:** `AudioEngine`, new `RemoteAudioTap/Sender/Receiver`,
`OpusAudioCodec`, `AudioJitterBuffer`, variable resampling adapter and playback
adapter; tests `tst_remote_audio_mix`, `tst_opus_audio_codec`,
`tst_remote_audio_clock`, `tst_remote_audio_session`.

The September 21 source audit narrows the remaining integration:

1. Own an audio sender from `DaemonMediaController`: consume the existing
   `DaemonAudioSource` off the DSP callback, encode with `OpusAudioEncoder`,
   and send via the current session's `MediaPeer::sendRtp`. Source sample
   positions drive timestamps; peer/session retirement stops capture and
   retires the encoder and SSRC.
2. Connect `RemoteMediaController` to `MediaPeer::rtpReceived`, with bounded
   reordering, jitter, loss concealment, continuous rate matching and a
   playback bus independent of local radio DSP. Validate clocks against
   actual device consumption, not the worker timer's cadence.
3. Bind client output device, trim and mute; flush queued audio and retire
   generations on mute/disconnect. Verify actual stereo listening, per-slice
   gain/mute/pan and reconnect on the Saturn/Rock/Mac path.

The vendored WDSP `rmatch`/`varsamp` implementation is a source candidate
for continuous rate matching; its exported `create_rmatchV`, `xrmatchIN`
and `xrmatchOUT` API preserves variable-resampler state. It uses critical
sections, so it must not run in a lock-free device callback. Source review
alone does not close task 1's runtime gate: the wrapper, attribution,
consumer-clock feedback and deterministic drift tests are now implemented.
The bounded adapter feeds the source's 64-frame blocks and preserves its
continuous interpolation. Both signs of 500 ppm pass a simulated hour with
zero underflows/overflows. This closes the offline runtime gate; hardware
listening and long-session acceptance remain separate.

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
- [x] Add bounded ordering/jitter, Opus loss concealment and continuous clock
  correction with preserved interpolation state. Start near the design's
  180 ms receive-buffer target; make timing injectable for tests.
- [x] Drive a client playback bus independently of local RadioModel DSP.
  Local output trim/mute must work, mute flushes queued samples, and encoding
  suspension is a session operation. Resume starts a fresh audio generation.
- [ ] Sample-rate, mode, band and slice transitions preserve a 48 kHz mixed
  output contract or explicitly reset audio context when required. Include
  cold startup with exactly one slice: the P2 codec assignment must reach the
  radio while connecting and converge on Connected, without depending on a
  later GUI slice creation or retune. Verify wire DDC rate and host DSP geometry
  agree. Close cancels pending work before rings, codec and device storage
  are destroyed.

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

## 5a. Expose useful remote audio controls and health

**Requirements:** R-R3-06/07/17/21/23. **Dependencies:** current playback path,
task 4c status ownership, and task 5's measured profile comparison.

**Owned files:** `src/gui/{RemoteMediaController,MainWindow}.*`, existing audio
setup surface, `src/core/session/media/{DaemonMediaController,OpusAudioCodec}.*`,
negotiated media contracts if needed, and remote audio/GUI tests.

**Interfaces:** consume actual accepted encoder settings, local output state,
receiver counters and session generation. Produce persistent status and a
small operator-facing profile choice only if the measurements justify it.
The currently fixed 24 kbit/s profile is not a negotiated quality selector.

- [ ] Show current codec/rate, local output/mute and persistent output/media
  error with a recovery action. Distinguish no signal from no media, local mute
  and output-device failure; show measured loss/jitter/buffer health with clear
  meanings. Arrival spacing alone must not be labelled packet loss.
- [ ] Complete the 24/48 kbit/s mixed-stereo listening/CPU/wire-rate comparison
  in task 5. Decide the offered quality profiles from those results. Preserve
  the selected mixed stereo ownership; do not silently substitute mono.
- [ ] If profiles are offered, add acknowledged Core configuration with bounded
  reconfiguration and reconnect replay. Display accepted settings on refusal;
  never present raw frame-size/FEC/complexity controls without a working wire
  contract. The addendum's three-tier example is not a mandatory profile list.

**Acceptance:** mute and device failures remain understandable after a toast
expires; client trim/mute does not change Core slice gain/pan. Displayed codec
matches actual packets. A refused or stale profile reply cannot claim a new
profile or revive an old session. Reconfiguration flushes incompatible queued
audio and recovers without unbounded latency. Listening comparison and device
failure recovery must be recorded separately from codec unit tests.

**Verification:** build `tst_opus_audio_codec`, `tst_remote_audio_receiver`,
`tst_remote_audio_session` and affected GUI targets before the matching exact
`ctest -R` selection with `--no-tests=error`; add meaningful profile/lifecycle
regressions if negotiation is introduced. Lead/operator compare the same
stereo reception at both rates and record preference plus measured costs.
No selector is required merely to make already-working Opus decoding operate.

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
- [ ] R-R3-26: reproduce configured-listener bind failure followed by address/
  port availability; recover without restarting the radio service or changing
  the configured bind/security scope. Stop/reconfiguration cancels stale retry
  callbacks. The September 21 boot left nereusd active but no control listener
  because Ethernet's address arrived late. Add lifecycle regression coverage
  and test late-network boot on the board; distinguish link flaps from Core
  readiness. Do not claim service-active alone proves remote availability.
  Software recovery/cancellation and capped-backoff regressions now pass,
  including the full 682-test suite; installation and late-network board
  acceptance remain pending (see the verification ledger).
- [ ] R-R3-27: cover startup with the configured radio absent, then present.
  `DaemonApp::resolveRadioInfo()` currently performs one startup discovery;
  the no-radio branch explicitly leaves later recovery to a service restart.
  Add cancellable discovery recovery for the selected identity without
  blocking Core control or duplicating the preserved station slices. Retain
  separate checks for established-radio link loss and initial discovery.
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
| Native combined Core | Installed signed `95b19467`, rollback `501b2701` | Corrected native rebuild/stage/install and matching GUI signature passed. Latest receive smoke failed with audio stalls and eventual connection loss; see [verification ledger](2026-09-20-remote-daemon-r3-verification/README.md). |
| Plan review and mixed-stereo choice | Lead, recorded | Current review; user confirmed mixed stereo and Opus |
| Direct media transport | Production adapter and MediaPeer integrated | Real encrypted peers, bounded callbacks, stop/restart/delete regressions pass; live display capture passes: Ethernet, 969-byte maximum IP packet; separate SRTP-size proof pending |
| Headless FFT and display codec | Implemented and component-tested | Independent Wide/Fine sources, retune input reset, independent planes, crop clamp, bounded codec and recovery tests pass; authenticated daemon-to-GUI regression and live Saturn display pass |
| Audio | First sound previously confirmed; latest smoke failed | Offline drift and encrypted session regressions pass. At `95b19467`, audio contexts repeatedly received one playable packet and late arrivals before timeout. Startup wire/host mismatch now reproduced on hardware (48/192 kHz); startup correction and source/encoding/transport-admission diagnostics passed 682 test executables, pending install. No network or VFO cause is established. |
| GUI media wiring | Desktop builds; initial focused checks pass | Dedicated reduced-frame renderer and authenticated subscription controller tested; two-pane, shared-window and reconnect regressions pass; live GPU spectrum/2D observed; operator 3D confirmation and gesture refinement pending |
| Tuning waterfall continuity | Visual regression corrected; focused tests pass | R-R3-04/09/10: subscription/context renewal preserves painted 2D/3D history while rejecting stale incoming planes; accepted RF geometry reprojects existing rows. Full-suite and live tuning gates remain recorded in the verification ledger. |
| Remote C-Tune control parity | R-R3-18 installed at `501b2701`; reported gestures accepted | Authenticated stream pin/centre commands, stream-lifetime guards, gesture-time pan selection, refusal rollback and reducer initialization pass the 681/681 combined suite. Matching Core/GUI are running; operator confirmed both wheel tuning and in-band scale zoom behave smoothly. Broader multi-slice hardware checks remain in task 6. |
| Wideband zoom parity | R-R3-20 open; task 4b added explicitly | Existing local ADC capture/FFT and extended wings are preserved. Remote display currently carries DDC data only. R-R3-19 prevents snap-back at that interim limit; it does not complete wideband transport or restore the full zoom range. |
| Applet S-meter | Implemented with earlier live movement; latest health unresolved | Earlier Saturn needle/flag agreement observed. The `95b19467` smoke did not establish healthy meter or spectrum output; diagnose alongside source cadence and connection loss. |
| BPF and Auto AGC-T indicators | Source implementation and focused tests pass | R-R3-14/15: station filter snapshot/reconnect, headless per-stream AGC source, active-applet/flag bindings implemented; live 20m filter and AGC floor observed, complete band/reconnect acceptance pending |
| Manual Core reconnect | Configured-endpoint controls installed at `95b19467`; full live acceptance open | Persistent controller/panel and title/station/pan routing have focused tests. Title opens details and manual Disconnect cancelled retry in the latest smoke; successful recovery and all entry points remain pending. |
| Visible controls and snapshot hydration | Bounded task 4c implementation installed at `95b19467` | Startup implicit slice creation is fixed and covered; fresh live attach displayed one slice. TX applet/PureSignal/filter-match, FIR and tuner gates are implemented. Phone/CW, XIT, TX settings and other remaining surfaces are listed in the control matrix. |
| Station accessories / TGXL | Receive-only guards and 13-field telemetry adapter installed; task 4d remains open | Snapshot replay is non-actuating; raw transmit writes and automatic tuner carrier requests are refused. Positive device identity, retry cancellation, Core-owned configuration/actions and headless frequency/mode propagation remain. Actual tuner TCP connectivity is unverified. |
| Audio controls and diagnostics | R-R3-23 open; task 5a | Fixed 24 kbit/s stereo is active; persistent profile/health/output feedback and measured 24/48 comparison remain. A selectable quality profile needs an acknowledged Core contract; no adaptive-rate claim. |
| Boot recovery | R-R3-26 software passed at `e518b63d`; hardware pending | Listener retries reuse station identity and cancel on stop; 682 test executables pass. Separate R-R3-27 covers one-shot initial radio discovery. |
