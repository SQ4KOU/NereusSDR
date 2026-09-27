# Core/GUI plan addendum: every gap found, with its ruling or question

JJ's goal (2026-09-27), from `landing-for-phone.md`: "land the Core/GUI work on main as one
PR passing ci.yml (Linux, macOS, Windows) and ios.yml where it applies, after finishing the
parity plan, the Core station tasks the phone depends on, R5, and a plan addendum of every gap
found." This document is that addendum. It is kept for the whole effort, not just this one
push: see the last section for how it grows.

Each entry: what the operator sees or would see, where it was found, the evidence, the ruling
(verbatim quote and date, or an open question with options and a recommendation), its build
status, and the plan task or requirement ID it belongs to.

## Open questions (JJ has not ruled)

### G-04: ATT on TX changes take effect at the next key, not at once

Thetis applies a step-attenuator-on-TX change immediately (`console.cs:19078`). NereusSDR
applies it only the next time the operator keys.

- Found: 2026-09-25/26, parity Task 31.
- Evidence: `nereus-parity/.../progress.md:111`: "Task 31: minor (deferred): ATT on TX toggled
  while keyed takes effect at the next key; Thetis at once (console.cs:19078)."
- Ruling: OPEN (recorded as a deferred minor, no operator ruling on record).
- Status: open.
- Plan: parity Task 31 (A11, R-R3-49).

### G-05: MOX release 10 ms after the drain can cut a stall's backlog at the end of an over

If the network send ring stalls near the end of a transmission, releasing MOX 10 ms after the
drain can cut off the tail of what was queued instead of waiting for it to actually go out.

- Found: 2026-09-27, P2 TX review.
- Evidence: `nereus-lane-b/.../progress.md:426`: "P2 TX: questions for JJ: (a) hold MOX until
  the send ring drains (bounded) so a stall's backlog is not cut at the end of an over."
- Ruling: OPEN. Recommendation (from the ledger): hold MOX until the send ring drains, bounded
  (i.e. wait for the drain, but with a ceiling so a stuck link cannot hold MOX forever).
- Status: open.
- Plan: needs an ID (P2 TX send thread work; no R-IOS/R-R3 tag recorded for this item).

### G-06: On the Rock with three receivers, TX threads have no fast core while keyed

The Rock (3 receivers) does not currently give the transmit threads a fast CPU core while
keyed, unlike the receive-side thread placement work already done.

- Found: 2026-09-27, P2 TX review.
- Evidence: `nereus-lane-b/.../progress.md:426`: "(b) on the Rock with three receivers, let TX
  take a fast core while keyed."
- Ruling: OPEN.
- Status: open.
- Plan: needs an ID.

### G-07: Protocol 1 TX ring stays at 84 ms; a stall over ~64 ms can overflow it

- Found: 2026-09-27, P2 TX review (carried, not new to this pass).
- Evidence: `nereus-lane-b/.../progress.md:426`: "Carried: P1 ring still 84 ms (stall > ~64 ms
  can overflow); clock drift only over many-minute overs; the unkey line has no dedicated
  test."
- Ruling: OPEN.
- Status: open; also missing a dedicated test for the unkey line.
- Plan: needs an ID.

### G-08: The Core keeps a dead rendezvous path open up to 60 s; the service gives up in 20-40 s

When the path to the rendezvous service dies, the Core takes up to 60 seconds to notice, while
the service itself gives up in 20-40 seconds. A phone reconnecting through rv can be stuck
waiting on the slower side.

- Found: 2026-09-27, Task 28 tail.
- Evidence: `nereus-lane-b/.../progress.md:430`: "Task 28 tail: question for JJ (low): should
  the Core give up on a dead rv path ~20 s sooner (matching the service) so the phone's reach
  returns faster."
- Ruling: OPEN (marked low priority by the implementer).
- Status: open.
- Plan: R-IOS-16 (station Task 28).

### G-10: The Pi's and Rock's explicit `audio_bitrate = 24000` config lines need JJ's yes to change

JJ approved 48 kbps full-band Opus as the new default, but the Pi 4's and Rock's installed
config files each set `audio_bitrate = 24000` explicitly, which overrides the new default. An
install would need to either change that line or leave those two Cores at the old rate.

- Found: 2026-09-26, Opus 48k task dispatch.
- Evidence: `nereus-lane-b/.../progress.md:367`: "the Rock's and Pi's explicit audio_bitrate =
  24000 lines need JJ's yes to change at install."
- Ruling: OPEN (a config edit, needs JJ's yes per the standing rule on device installs).
- Status: the 48 kbps default itself is built and merged (commit `e324e2bb`); the two
  installed Cores' config files are unchanged pending JJ's answer.
- Plan: R-R3-21, R-IOS-09.

### G-11: Protocol 2 sample rates for Atlas, Hermes, HermesII and HL2 sit below Thetis's range

Thetis allows these boards 48 kHz to 1536 kHz under Protocol 2 firmware; NereusSDR's current
range is narrower for the same boards.

- Found: 2026-09-24/25, gaps review.
- Evidence: `~/.config/nereus/work/checkpoint-next-operator-list.md:184`: "JJ QUEUE: Protocol 2
  rates for Atlas, Hermes, HermesII and HL2 kept below Thetis's 48-1536 kHz (Hermes-class
  radios can run P2 firmware): follow Thetis? (from the gaps review)."
- Ruling: OPEN; no answer found in any source read for this addendum.
- Status: open.
- Plan: needs an ID (raised from the receiver-and-transmit-gaps plan review).

### G-12: TCI and MMIO Setup pages keep protocol-facing names (WebSocket, endpoint, binding)

JJ's standing rule is plain operator wording with no protocol jargon, but the TCI Server and
MMIO setup pages still use "WebSocket," "endpoint," and "binding," because those are the
features' own names.

- Found: 2026-09-23, checkpoint wording review.
- Evidence: `~/.config/nereus/work/checkpoint-next-operator-list.md:67`: "Question queued for
  JJ: the TCI and MMIO Setup pages keep their feature names (WebSocket, endpoint, binding):
  keep or reword?"
- Ruling: OPEN.
- Status: open; the same file's line 99 lists specific TCI/MMIO strings an implementer chose
  beyond this list, also awaiting a look.
- Plan: needs an ID.

### G-13: The Connections window says "Your stations" / "Stations on this network" while the app says "Core"

- Found: 2026-09-23, checkpoint wording review.
- Evidence: `~/.config/nereus/work/checkpoint-next-operator-list.md:68`: "Operator review item
  (wording): the Connections window says 'Your stations' and 'Stations on this network' while
  the rest of the app says 'Core'; keep 'station' (a Core with its radio) or change to
  'Core'?"
- Ruling: OPEN.
- Status: open.
- Plan: needs an ID.

## In progress

### G-16: Other config-file keys that may override every start (audit)

Beyond `sample_rate_hz` (G-17 below), `audio_device` and `audio_bitrate` may have the same
"overwrites the saved choice on every start" problem. The continuation audit found one
remaining settings overwrite: an explicit audio_device replaces the saved speaker choice.
Audio bitrate is a runtime encoder policy, with no separate saved choice found to overwrite.

- Found: 2026-09-27, controller's own audit alongside the sample-rate fix.
- Evidence: brief's own seed list, corroborated by the sample-rate fix's commit message
  (`043b8cfb`) treating `sample_rate_hz` as the first instance of the pattern.
- Ruling: OPEN for whether explicit audio_device should seed only an absent speaker choice,
  like sample_rate_hz. No radio config file has been edited.
- Status: source audit complete in DaemonApp::applyConfigToSettings and setupRemoteSession.
  applyConfigToSettings has only sample-rate seeding and the audio-device overwrite; bitrate
  goes directly to DaemonMediaHub::setAudioTargetBitrate. Regression/behavior change awaits
  the audio-device ruling.
- Plan: R-R3-49 (small-followups lane).

### G-17: Config-file `sample_rate_hz` overwrote the saved rate every start

Every Core start or install re-applied the config file's `sample_rate_hz` over the per-radio
saved rate, so a rate chosen from a window was thrown away at the next restart.

- Found: 2026-09-27, controller's own review.
- Ruling (JJ, 2026-09-27, "your recommendation"): `nereus-lane-b/.../progress.md:445`: "Ruling
  (JJ 2026-09-27, 'your recommendation'): the Core's config-file sample_rate_hz is a starting
  value only; a saved per-radio rate wins across restarts and installs."
- Evidence of the fix: commit `043b8cfb`, "Seed the Core's sample rate from the config file
  only when none is saved" (worktree `nereus-small`, branch `codex/conf-rate-seed`).
- Status: built (commit `043b8cfb`) but not yet merged into the trunk (`codex/checkpoint-b`,
  head `6c3f543d`); confirmed not an ancestor of the trunk head as of this writing.
- Plan: R-R3-49.

### G-18: Tests failing under load, including "the data channel did not open"

A cluster of tests fail only under shared-machine load (contention, timing-sensitive setup,
libdatachannel races), not in isolation. A dedicated lane is fixing them one at a time.

- Found: throughout the effort (recurring FINDING across lane-b and parity ledgers).
- Evidence: worktree `nereus-r2-integration`, branch `codex/flaky-tests`, 11 commits ahead of
  `codex/checkpoint-b` as of this writing (e.g. "Wait for the Core's receive lane before
  reading its DSP results in tst_remote_dsp_info," "Keep a remote description before
  libdatachannel's ICE agent takes it," "Hold the test data channel pair's offer candidates
  until the answer is kept"); trunk head's own non-realtime run
  (`nereus-lane-b/.../progress.md:444`) shows "tst_tci_remote_window lossless, three
  data-channel rows 'did not open'," passing below load 25.
- Ruling: none needed; this is test-only hardening, not a product or design decision.
- Status: in progress (`codex/flaky-tests`, not yet merged into the trunk).
- Plan: R-R3-49.

### G-19: TX monitor plays only on the transmitting device

- Found: 2026-09-26, parity lane.
- Ruling (JJ, 2026-09-26, "no" to the Core's own speakers also playing):
  `nereus-parity/.../progress.md:94`: "MON ruling (JJ 2026-09-26, 'no' to the Core's speakers
  also playing): while a remote device holds transmit its MON plays only on that device; the
  Core's local monitor stays quiet; a local window at the Core that holds transmit still hears
  it."
- Evidence of the build: `nereus-parity/.../progress.md:113`, Task 32 "complete with concerns
  (opus, medium; 7bc095a8 G; MON level after Opus decode within 0.2 dB (main) / 0.07 dB
  (headphones))."
- Status: built and integrated. Git confirms `7bc095a8` is an ancestor of current trunk;
  the earlier handoff describing it as queued is stale. Hardware acceptance remains distinct
  from the synthetic monitor-level evidence above.
- Plan: parity Task 32.

## Queued

### G-20: TGXL TRANSMITTING hold (~400 ms) so the amplifier never sweeps without carrier

When a network device's TUNE turns on while the TGXL amplifier is still switching, the display
should hold "TRANSMITTING" for up to about 400 ms so the antenna tuner never sweeps with no
carrier present.

- Found: 2026-09-2x, PGXL/TGXL capture analysis (read-only).
- Evidence: `nereus-lane-b/.../progress.md:401`: "QUEUED (low priority): hold TRANSMITTING for
  a network device's tune on while the amp is still switching, at most ~400 ms, so the TGXL
  never sweeps without carrier (SmartSdrApiListener.cpp:429)."
- Ruling: queued as low priority; no operator ruling needed beyond the queue placement itself.
- Status: queued, not started.
- Plan: needs an ID.

## Ruled and built (record with commits)

### G-21: Absent hardware hidden; controls that exist but cannot run stay disabled with a reason

- Ruling (JJ, 2026-09-26): `nereus-lane-b/.../progress.md:394`: "JJ 2026-09-26: controls for
  hardware the radio lacks are hidden; controls that exist but can't run are disabled with a
  reason."
- Built: commit `96eeecd8`, "Send the transmit ranges, the noise-reduction controls and the
  board's relays in the Core's catalogue," whose message states the catalogue gives
  `board.rx1Preamp` and `board.relays` "for an app to hide what the radio lacks, as the desktop
  does." Confirmed an ancestor of the trunk head `6c3f543d`.
- Plan: R-IOS-06, R-IOS-27.

### G-22: DFNR, MNR and BNR shown disabled with a reason, never hidden (and BNR's button removed)

- Ruling (JJ, 2026-09-25): `nereus-lane-b/.../progress.md:208`: "JJ 2026-09-25 on DFNR: 'why
  cant dfnr run? Not a fan of disappearing buttons but rather disabled. Also a big fan of the
  dfnr just working.'" And on BNR, `nereus-lane-b/.../progress.md:245`: "JJ 2026-09-25 on BNR
  (NVIDIA noise removal, in no build): option 2, take its button out ('2 for bnr we can look at
  that again later')."
- Built: commit `6812ed66`, "Show DFNR, MNR and BNR disabled with the reason, never hidden";
  commit `7e4f3480`, "Take the BNR button out, as the operator decided." Both confirmed
  ancestors of the trunk head.
- Plan: R-R3-49, Sub-epic C-1.

### G-23: DFNR loaded lazily at first selection, not at channel construction

Every channel used to build a DeepFilterNet3 instance at construction (loading an 8 MB model,
about 250 ms, five channels per connect), so any build with the DFNR model spent over a second
per connect. It now loads only at a channel's first DFNR selection.

- Found: 2026-09-25, lane D follow-up.
- Ruling: controller ruling (not a direct JJ quote, but a design decision recorded and acted
  on): "create the DFNR instance lazily at its first selection, on the receive lane... a small
  task in the tx lane right after Task 31."
- Built: `src/core/RxChannel.cpp:37-40` (comment): "2026-09-25 - R-R3-39, Sub-epic C-1: the
  DeepFilterNet3 instance is built at a channel's first DFNR selection, on the receive lane,
  not in the constructor." Present in the trunk head's checked-out tree.
- Plan: R-R3-39, Sub-epic C-1.

### G-24: Opus at 48 kbps full-band for every mode, no FEC

- Ruling (JJ, 2026-09-26): `nereus-lane-b/.../progress.md:367`: "JJ 2026-09-26: Opus at 48 kbps
  full-band for every mode ('48kbps seems thin enough for everything')." FEC was approved the
  same day, then reversed after measurement: `nereus-lane-b/.../progress.md:379`: "JJ
  2026-09-26: Opus 48 kbps full-band, NO FEC (option 3 after the measurement)," because FEC
  forces SILK/hybrid and kills audio above 8 kHz at 48k.
- Built: commit `e324e2bb`, "Merge the Core's 48 kbps full-band Opus default into the trunk."
  Confirmed an ancestor of the trunk head.
- Plan: R-R3-21, R-IOS-09.

### G-25: Display presented on the audio's playout clock, with blended gap rows

Watching the waterfall over a WAN link with real jitter stuttered independently of the audio.
JJ asked that audio and waterfall stay in sync.

- Ruling (JJ, 2026-09-26): `nereus-lane-b/.../progress.md:381`: "JJ 2026-09-26: audio and
  waterfall must stay in sync; present the display on the audio's playout clock with blended
  gap rows ('yes on your recommendation assuming you have thought about this carefully')."
- Built: commit `83ac32ca`, "Merge the remote display playing in step with its audio into the
  trunk," carrying `6662a48a` ("Present a remote window's display on its audio's clock and ride
  a stalling link") and `f59cc8ee`. Confirmed an ancestor of the trunk head.
- Plan: R-R3-21, R-R3-08.

### G-26: Display duplex (DUP) off by default, with a View menu item in both windows

- Ruling (JJ, 2026-09-26, on question Q5): `nereus-parity/.../progress.md:92`: "Q5 ruled by JJ
  (2026-09-26, 'yes for your DUP recommendation'): DUP off by default as in Thetis, with a
  'Display duplex (DUP)' View menu item in both windows; Task 31 stands as written."
- Built: commit `17cd4dc5`, "Show the receiver while transmitting with display duplex (DUP) in
  both windows." Confirmed an ancestor of the trunk head.
- Plan: parity Task 31 (A11, R-R3-49).

### G-27: Radio change goes through the Core's own restart, and windows reconnect by themselves

Rather than build a live radio-swap path, the Core restarts itself when its radio is changed;
connected windows are told why and reconnect on their own.

- Ruling (JJ, 2026-09-26, "go with your recommendation"): `nereus-parity/.../progress.md:88`:
  "JJ ruled 2026-09-26 ('go with your recommendation'): option (c) keep the run restart,
  windows reconnect by themselves with a plain radio-change reason; fix I1 (control socket) and
  I2 (answer/notices/end reason flushed before teardown)."
- Built: commit `f355f7da`, "Merge parity Tasks 19 and 21 (spots and the Core's radio from a
  window) into the trunk," carrying `9c9e4546` ("Change the Core's radio from a remote
  window"), `87af370f` ("Tell every app why the Core restarts for a radio change and keep its
  console"), `96c7786b`, `a7e04997`, and `2ad4cce3`. Confirmed an ancestor of the trunk head.
- Plan: R-IOS-18, R-R3-38, R-R3-49.

### G-28: Relay quota raised to 8 allocations per Core, 128 in total

- Ruling (JJ, 2026-09-26): `nereus-rendezvous/.../progress.md` line 55: "Relay quota raise
  approved by JJ (8 per Core, 128 total)." Superseded by a later sizing ruling the same day,
  recorded in the same ledger: "Rulings (JJ 2026-09-26): 2000 Cores; ... (user-quota 4, 64
  slots, ...)" for the dedicated server, then raised again: `nereus-lane-b/.../progress.md`
  and the rendezvous ledger both confirm "coturn user-quota 8, total 128" as the value
  deployed and carried into the trunk.
- Built: commit `bb2a3d16` ("Raise the relay allowance to 8 per Core and 128 slots in total"),
  merged into the trunk as commit `5036485a` ("Merge the rendezvous relay quota (8 per Core,
  128 total) into the trunk"). Confirmed an ancestor of the trunk head.
- Plan: R-IOS-16.

### G-29: Local and remote windows both get an Operate button for the Power Genius XL tab

Only the remote window's 4O3A PowerGenius tab had an Operate button; the local tab had none.

- Ruling (JJ, 2026-09-25): `nereus-parity/.../progress.md:45`: "Task 9: ruling amended by JJ
  2026-09-25 ('1 add it to the local tab we want parity no matter how i am connected'): the
  local window's PowerGenius XL tab (PgxlAdvancedPage) gets an Operate button too... Parity
  runs both ways: a control a remote window has, a local window gets without asking."
- Built: commit `1eb12fc4`, "Operate the Power Genius from a local window's PowerGenius XL
  tab." Confirmed an ancestor of the trunk head.
- Plan: parity Task 9.

### G-30: What switches the amplifier or tuner is blocked on the air in both windows; what only listens or saves is allowed in both

- Ruling (JJ, 2026-09-25, on group B finding M5): `nereus-parity/.../progress.md:54`: "JJ
  2026-09-25 on group B M5: '1 block them in both windows': a local window's PGXL
  Operate/Standby, RF-Kit Operate/antenna and TGXL relay moves get the remote on-air rule,
  disabled with the reason while keyed."
- Built: commit `b9d475bd`, "the on-air rule by what a control does in both windows: amp/tuner
  switches incl. RF-Kit TCI mode held; Scan LAN and address saves taken on the air; a local
  click in the unkey window shows the reason" (per `nereus-parity/.../progress.md:63`).
  Confirmed an ancestor of the trunk head.
- Plan: group B fix wave, parity plan.

### G-31: Wideband on the second ADC (ADC1) is kept, as NereusSDR's own identity choice, for the G2 and other two-ADC P2 radios

Thetis only enables ADC0 on every board; NereusSDR offers two ADCs on boards that have them.

- Ruling (JJ, 2026-09-25): `~/.config/nereus/work/checkpoint-next-operator-list.md:201`:
  "ANSWERED: wideband on the second ADC stays for the G2 and the other two-ADC Protocol 2
  radios ('1 that was a feature we worked on as part of our own identity')."
- Status: no code change required (kept as-is; the ruling is to not follow Thetis here). A
  checkpoint bench item was added: on the G2, zoom slice B's pan (the second ADC) out past its
  receiver's bandwidth and check the wide edges fill in.
- Plan: needs an ID (raised in the gaps review; not a plan task on its own).

## Approved continuation work

### G-09: Restrictive networks use JJ's approved layered connection plan

- Evidence/ruling: lane-B crew progress records JJ on 2026-09-27: "Yes build the layerd
  plan if this is the ideal way to handle naturalversal, given everything we know".
  The accepted plan uses direct wss, rendezvous ICE, the rendezvous WebSocket relay as a
  low-priority ICE candidate, direct-wss media fallback, fast failure detection and system
  proxy support. Transmit deadline behavior must be measured on TCP fallback paths.
- Status: R5 implementation and Linux/macOS traversal verification are in progress. The
  earlier measurement document's pending choice is superseded by this recorded approval.
- Plan: R-IOS-16, R-IOS-08; R5 remote access.

### G-14: Older windows finding a full Core receive a retryable refusal

- Ruling: JJ's 2026-09-24 board v50/v51 decisions are recorded in the phone crew ledger and
  phone design D66. With four devices or no free receiver, an older window is refused with
  "The Core is full. Update NereusSDR to take a device's place, or try again later."
  Nobody already connected is disturbed. The phone controller confirmed these sources;
  the Core lead read the ruling directly.
- Status: decision settled; fifth-device station implementation and verification remain.
- Plan: R-IOS-30, R-IOS-31.

### G-15: Away devices come first in the fifth-device choice

- Ruling: phone design section 5.9, kept by JJ on board v50/v51, explicitly puts an away
  device first. D55's idle-longest ordering remains for connected candidates, and D64
  protects the desktop hosting the Core.
- Status: decision settled; implementation is not implied by the approved drawings.
- Plan: R-IOS-30, R-IOS-31.


### G-01: Spectrum decimation range cited to Thetis is wider than Thetis allows

The Rendering setup page lets an operator pick a decimation step from 1 to 32. Thetis's own
control tops out at 16, so NereusSDR's range is wider than the source it was ported from.

- Found: 2026-09-27, trunk catalogue-ranges task, lane B.
- Evidence: `nereus-lane-b/.../progress.md:434`: "the decimation range 1-32 is cited to Thetis,
  which allows 1-16 (setup.designer.cs:33834 [v2.10.3.15]); cite correction sent back to the
  implementer; the range itself is a question for JJ (low)."
- Ruling: JJ approved on 2026-09-27: "Match Thetis: 1–16 (recommended)".
- Status: implementation in `codex/display-decimation-parity`; the UI, catalogue, local FFT
  engines and Core media request validation share the same bounds. Boundary regression
  first failed because 17 was accepted. Signed implementation `87f59425b` passed five focused
  tests including link conformance in 45.43 s; the integrated trunk passed the same
  five targets in 41.99 s. Generated protocol tables and diff checks pass.
- Plan: R-IOS-06, R-IOS-27 (catalogue ranges task).

### G-02: Several noise-reduction ranges and new-slice defaults differ from Thetis

The desktop's NR2, NR4 and new-slice NR4 controls use different numeric ranges and starting
values than Thetis's own dialogs. An operator moving between the two apps would see different
numbers for what should be the same control.

- Found: 2026-09-27, catalogue-ranges scout and task, lane B.
- Evidence: `nereus-lane-b/.../progress.md:440`: "desktop NR values that differ from Thetis
  v2.10.3.15 (NR2 Factor/Rate 0-30 vs 0-100; NR4 Rescale 0-20 vs 0-12; NR4 SNRthresh -30..0 vs
  -10..+10; new-slice NR4 Smoothing 65/Whitening 2/Algo 2 vs 0/0/Algo 1). Recommend correcting
  to Thetis, as NR1 was."
- Ruling: JJ approved matching Thetis on 2026-09-27 in the Core/GUI Codex continuation:
  "Match Thetis (recommended)". Match the NR2 Factor/Rate range and fractional steps,
  NR4 Rescale/SNR threshold ranges, and new-slice Smoothing/Whitening/algorithm defaults.
  Preserve saved operator choices; this is not a settings reset.
- Status: NR1 corrected and built (commit `d6d96eaa`, in trunk); NR2/NR4 implementation
  merged into trunk as signed `edfd220bb` (implementation `b9bedc3c5`). New control/default/save
  regressions, catalogue checks and both session transports pass. Existing saved choices
  remain intact; catalogue fixtures advertise the same ranges as the controls.
- Plan: R-IOS-06, R-IOS-27.

### G-03: A local window's receive waterfall never takes the display calibration

A remote window's waterfall rows arrive already calibrated from the Core. A local window's
receive waterfall does not apply the same calibration, so the two show slightly different
colours for the same signal.

- Found: 2026-09-25/26, parity Task 31.
- Evidence: `nereus-parity/.../progress.md:110`: "Task 31: question for JJ: a local window's
  receive waterfall never takes the display calibration (Thetis adds it; a remote window's
  rows arrive calibrated from the Core), so local and remote differ; fixing it moves every
  operator's receive colours."
- Ruling: JJ approved on 2026-09-27: "Apply calibration in both windows (recommended)".
- Status: signed implementation `f7af7860f` applies local receive calibration once before
  colours, threshold tracking and 3D history. Six focused tests passed in 7.83 s, including
  positive/negative offsets and remote/TX no-double-calibration checks. The integrated
  trunk passed the same six targets in 7.64 s.
  The colour shift is explicitly approved.
- Plan: parity Task 31 (A11, R-R3-49).

## Continuation findings and verification, 2026-09-27

### G-32: Slice publication and incoming DTLS records could race initialization

- Evidence: the resumed half-merge published an atomic slice audio view and applied the
  DTLS MTU before incoming records. Signed trunk merge `29ce3594f` contains both fixes.
- Ruling: JJ explicitly requested finishing this merge and its required verification.
- Status: application and all test targets built; focused checks passed. The full baseline
  completed in two recorded segments (865 entries, then the 83 interrupted/unstarted entries).
  Combined result: 944/948 passed. The four findings below remain tracked independently.
- Plan: R-R3-49; Core station prerequisites for phone media.

### G-33: Load exposes test setup races and unresolved timing failures

- Evidence: the baseline failed the control heartbeat setup, fake-radio meter startup,
  pairing helper startup and transmit event-loop gap checks. The heartbeat test enabled its
  100 ms deadline before the handshake completed. The fake radio stopped producing during
  synchronous DSP initialization. The latter now runs on its own thread; neither production
  watchdog was weakened. Pairing startup now reports phase timings without exposing wire data.
- Ruling: JJ: "A test that fails only under load is a finding: bring JJ its cause and a
  suggested fix." The earlier instruction also rejects merely extending the audio-clock limit.
- Status: integrated load-fix build passed; focused run passed 14/16 entries in 307.21 s.
  Both remote-audio entries failed (14 failing rows total) at observed load up to about 140.
  Evidence includes source/speaker timer delays and receiver worker wake gaps up to 201.5 ms.
  These are open findings, not waived tests. Separate independently paced source/device
  measurement is needed to distinguish harness starvation from receiver scheduling.
  Signed diagnostics `944311ba` are integrated as `2ebb270`: they capture source lateness,
  device/worker wake gaps and first excess concealment without a later finite-source timeout
  hiding it. The exact integrated case passed in 4.098 s; this does not close the load finding.
  Pairing's original slow phase and
  the 25.81 ms transmit timer gap (25 ms bound) remain unresolved. A standalone passing run
  does not close either finding. Suggested next investigation: phase measurements for pairing
  and a same-load unkeyed timer baseline plus key-call timing for transmit.
- Audio-clock evidence: unchanged simulated-hour coverage passed in 177.19 s in the baseline
  remainder. A separate measured run used about 105 CPU seconds; about 98 were in production
  audio push/resampling. Test tone generation used about 4 seconds. Existing limits retained.
- Plan: R-R3-49; load-failure work.

### G-34: Relay cleanup must retain the actual ICE socket and its local route

- Evidence: PeerConnection close returns before asynchronous ICE teardown; a fixed delay
  cannot prove that an old agent stopped using its local route. Shared phone patches retain
  the TURN agent through bounded release processing and any outstanding resolver. Core adds
  an optional lifetime owner, released only after actual agent destruction, then releases the
  route on its Qt thread. Overlapping media uses distinct UUID routes and a bounded count.
- Ruling: implementation under JJ's explicit authorization to finish R5 and Core phone
  prerequisites. No new operator behavior ruling is inferred. Existing identity checks and
  the transmit watchdog remain required.
- Status: signed implementation `50fcf932` built and verified in trunk: lifetime, rendezvous
  and provenance checks passed 4/4 in 67.90 s. Phone both-end interop passed all seven tests
  plus three release repeats with a staged copy of the verified helper. Signed trunk
  `c2b00a54` had a built Rock package that passed the isolated 15-second startup check
  before the later deployment.
  The subsequently verified `1f3cc251` Rock package is installed: the Core is active with
  no restarts, registered with the service, and `/etc/nereusd.conf` retains its measured
  hash (`core-gui-rock-1f3cc251-health.log`). An earlier build was found to have skipped
  vendor patches despite a successful tool exit. That evidence was withdrawn. The patch
  helper now rejects skipped application and materialized hashes are checked; that earlier
  skipped-patch build was never deployed.
- Plan: R5 remote access; phone relay cleanup and replacement.

### G-35: Pairing confirmation can be deleted before the socket drains

- Evidence: phone finding `46f01fca` identified a Core connection deleted immediately after
  sending pair.confirm. A Core regression reproduces premature deletion with queued output,
  including server destruction. The original proposed hunk also needed to register the closed
  handler before calling close, since a transport can signal closure synchronously.
- Ruling: ordinary correctness fix within JJ's authorized Core station work; no change to
  pairing trust, permissions, or timing policy is proposed.
- Status: signed implementation `ecb939955` integrated into trunk as `40244520c`. The regression first
  failed for delayed close and server destruction; all three closure cases pass after the fix.
  Session and pairing suites pass both in the lane (2/2, 26.07 s) and on the integrated
  working tree (2/2, 25.35 s). Only Core code is included; the phone's final-frame receive
  change remains phone-owned.
- Plan: R-IOS-08; pairing interoperability.

### G-36: Late packets from a retired media peer bypass duplicate filtering

- Evidence: R5 load testing reproduced a duplicate packet after replacement. The overlap
  filter can become inactive immediately at promotion while the old peer remains alive for
  two seconds. A second replacement can also overwrite the retiring peer owner.
- Ruling: correctness repair within JJ's approved R5 replacement work; no watchdog or test
  tolerance change. Preserve the current connection while retirement completes.
- Status: keep filtering until the retired peer stops, defer another replacement and retry
  the desired path afterward. Deterministic regression and loaded verification in progress.
- Plan: R5 media replacement and recovery.

### G-37: Local TCI I/Q labels every receiver as stream zero at 192 kHz

- Evidence: the raw I/Q audit found the local TCI producer used only untagged stream zero
  and a fixed 192 kHz rate despite the radio's actual binding and sample rate.
- Ruling: existing local/remote parity requirement applies in both directions. Preserve
  samples without resampling and report the actual supported receiver rate.
- Status: signed local correction `4da887a0` maps receiver to its stream and current rate,
  with focused nonzero-receiver/rate-change/refusal checks. Remote I/Q remains under
  implementation and is not advertised as available yet.
- Plan: remote-window parity TCI raw I/Q.

### G-38: Settings reset labels and remote hygiene commands disagree with behavior

- Evidence: local Reset to defaults only repairs invalid board settings; Forget removes
  per-MAC settings. Existing station.forgetRadio instead removes a saved radio entry and
  refuses the current radio, so it cannot implement remote hygiene parity.
- Ruling: OPEN for reset semantics. JJ has been asked whether Reset should reset all
  saved settings for the radio or retain repair behavior under a clearer label.
- Status: capability-gated validate/forget hygiene operations from `ce40a2c` are
  integrated, with Core-owned MAC validation and paired-device/on-air mutation gates.
  Reset remains disabled remotely pending the ruling. Integrated app/Core build and
  20 focused tests passed (79.27 s), including setup, hygiene and link conformance.
- Plan: remote-window parity Setup diagnostics and preferences.

### G-39: Restrictive-network relay stress test falsely unkeys simulated TUNE

- Evidence: Linux isolated relay test with 2% loss and 150 ms RTT tripped the transmit
  watchdog 4.882 s after simulated TUNE began, after 403 ms without a keepalive. The session
  still reported connected. The accepted-gap histogram omitted the missing interval and
  the harness incorrectly printed a passing summary. This was synthetic; no radio keyed.
- Ruling: JJ's existing requirement applies: diagnose load failures and bring the cause
  and suggested fix. The 400 ms safety deadline remains unchanged.
- Status: R5 acceptance remains open. The original tests used the control-only keepalive
  fallback; a corrected production-follower harness also reproduced 402-403 ms false unkeys
  on web relay at 2% and 3% loss and direct WSS at 2%, each with 150 ms RTT. A bounded
  in-memory monotonic trace on the 2% web case captured client keepalive sequences 66-70
  accepted locally every about 100 ms and tag-2 WebSocket enqueue/send continuing with
  sampled backlog zero, while Core channel decode last accepted seq 66 at trip minus 402 ms
  and Core relay tag-2 receive stopped at trip minus 318 ms. The missing stage is after
  client `sendBinaryMessage` and before Core `RelayLeg::onMessage`. Service forwarding
  versus socket/TCP buffering or retransmission remains unproved. Control and media share
  the same TCP floor, so sending a duplicate over control has no independent deadline
  guarantee. No watchdog adjustment or production behavior change was made. The old green
  accepted-gap summary is withdrawn; an absent interval is a failure.
- Plan: R5 restrictive-network transmit deadline.

### G-40: Code-only rendezvous reconnect stops at an already paired device

- Evidence: JJ's build 7 phone reached the Rock through pairing but received the
  existing-key refusal before SPAKE. Returning to a saved entry connected, while
  saved private addresses still delayed the intended address-free workflow.
- Ruling: JJ said the operator should enter the pairing code and nothing else,
  without managing saved IP addresses. This is part of the code-only objective.
- Status: Core correction verified by the full pairing test (35 Qt rows, 13.39 s)
  and three integrated session/readings/log regressions (43.42 s): complete code proof confirms an existing
  key without replacing its record; removal during the exchange invalidates it.
  Mailboxes remain pairing-only. The phone owns authenticated upsert and ordinary
  rendezvous reconnect without waiting for stale saved private addresses. The verified
  `1f3cc251` Core is now installed and healthy on Rock, registered to the service, with
  `/etc/nereusd.conf` unchanged by hash (`core-gui-rock-1f3cc251-health.log`). The phone's
  automatic initial-race and later path-switch work remains separately open.
- Plan: Core station pairing and R5 code-only access.

### G-41: Audio reset headphone wording conflicts with default-off behavior

- Evidence: remote parity acceptance B6.10 says reset reopens headphones, but
  AudioEngine::resetAudioSettings removes Headphones/Enabled and closes the headphone
  output to restore its default-off state.
- Ruling: OPEN on whether reset should preserve enabled headphones. Existing behavior
  is preserved while the independent immediate audio-output rebuild work proceeds.
- Status: Setup lane reports the discrepancy; no invented new headphone default.
- Plan: remote-window parity Setup diagnostics and preferences.

### G-42: Region and extended-transmit controls do not yet drive the Core TX gate

- Evidence: General Options writes Region as a string (`GeneralOptionsPage.cpp`), while
  the transmit gate reads numeric `BandPlanRegion` (`RadioModel.cpp`). The saved
  `ExtendedTxAllowed` and cross-band values are OperatorLocal in `SettingsScope`; the
  Core gate currently uses false constants instead of those values. A stale saved
  Extended=true activation and split RX/TX band policy need a source-based safety review.
  `BandPlanGuard::bandRangesFor` had TODOs for most country tables and fell back
  to US ranges; a stale comment calling the guard inert is false because MOX already
  invokes it. Thetis `Console.cs` 6780-6812 also checks non-CW transmit filter edges
  through `CheckValidTXFreq`, while Nereus's sole band-plan guard call passes carrier
  frequency only; `pushTxModeAndBandpass` uses positive audio-space filter values before
  TxChannel mode conversion. Filter-edge policy is another unresolved safety subgap.
- Ruling: existing parity work authorizes making these controls functional, but the
  precise safety policy and settings ownership migration remain OPEN. Do not import
  an operator-local value into the Core gate without that ruling.
- Status: country ranges from signed `7ffcc419` are integrated: all 24 supported
  regions and 264 HF ranges independently match Thetis. Integrated app/Core build and
  both band-plan and TX-frequency suites pass (1.11 s). Unknown enum values fail closed.
  Region activation still needs Core-owned validated settings and the filter-edge gate;
  enabling the combo alone would be unsafe. Thetis's current `Init60mChannels` has only UK, US and
  default cases, so missing extra country-channel arrays are not established; Nereus's
  explicit UK/Japan channelization is a native exception. The isolated parity lane is only
  disabling misleading interim UI and recording this gap. Ganymede and DisableHFPA remain
  the absent-producer classifications under G-21;
  the alleged missing TX-inhibit reader was a false positive, since production
  `attachRadioInput` is wired.
- Plan: remote-window parity Setup and transmit gate safety.

### G-43: Transmit frequency guard omits XIT offset

- Evidence: `RadioModel::installBandPlanMoxCheck` passes the transmit-bound
  slice's dial frequency to `BandPlanGuard::checkMoxAllowed`, while the hardware
  transmit path uses `txFrequencyForSlice(slice)` including XIT. Thetis
  `Console.cs` lines 29440-29450 applies XIT before `CheckValidTXFreq`
  at line 29486.
- Ruling: the existing source-faithful transmit guard objective authorizes this narrow
  fix, without a broader Extended/cross-band policy change.
- Status: signed fix `d83b4e88c` is integrated: the guard uses the actual TX carrier
  including enabled XIT for both frequency and band. The integrated transmit-frequency
  test passed in the 20-test setup integration run; no physical transmission was used.
- Plan: transmit guard parity and safety.

### G-44: Saved legacy control-channel result can permanently suppress recovery

- Evidence: station-link sections 6.3 and 21.1, `CoreTargetStore`, and
  `StationClient::newRacer` retain a saved `controlChannelVersion` of 0 and
  skip the service rung on every later race. An upgraded Core can therefore
  remain unreachable through rendezvous after a stale direct address fails.
- Ruling: JJ approved automatic direct/manual/rendezvous recovery. The lead's
  bounded implementation contract is a persisted, authenticated negative
  observation fresh for five minutes; older records without a timestamp,
  expired/future timestamps, and unparseable timestamps are unknown for
  discovery. A real network-generation change invalidates the observation
  once, without clearing it on every retry. A fresh authenticated 0 suppresses
  the service rung until expiry or network change; authenticated 1 clears the
  negative timestamp. Failed or unanswered probes do not renew 0. Full
  same-identity code pairing invalidates the negative observation. Existing
  identity/certificate checks, single authenticated race winner, bounded
  deadlines and retries, and `relayAllowed` remain in force.
- Status: OPEN for Core and desktop implementation and the exact shared link
  document update. The phone is implementing its corresponding cache. This
  lead contract does not assert a separate JJ ruling on the five-minute value.
- Plan: automatic direct/manual/rendezvous recovery.

## How this addendum is kept

New gaps are appended here as they are found, each with its own `G-` number (next available
number, not reused). A ruling is recorded only when a source quotes the operator's words or
otherwise plainly records his decision; short of that, the entry stays marked OPEN with the
options and a recommendation, never a guessed ruling. Status is updated in place as work lands:
"queued" becomes "in progress" when a lane is dispatched, and "in progress" becomes "ruled and
built" with the landing commit hash once it is in the trunk (`codex/checkpoint-b` or its
successor). Entries are re-sorted (open questions first, then in progress, then queued, then
ruled-and-built) whenever the addendum is revisited, so JJ always sees what still needs him at
the top.
