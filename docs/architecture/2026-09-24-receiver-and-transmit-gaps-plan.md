# Receiver and Transmit Gaps Implementation Plan

> **Execution:** run with `crew` under `cost-aware-execution`. Requirements and
> acceptance cases are binding; test order and review effort follow the risk-based
> policy. No review between tasks; one whole-branch review at the end.

**Goal:** Fix the defects in the receiver, sample-rate, PureSignal, TCI and transmit-keying
code that a read-only inventory of the multi-client groundwork found on 2026-09-24.

**Architecture:** Each task is a bounded fix inside the code that already exists. None of them
changes the session model, which the operator is redesigning for several clients at once; each
fix holds in either model.

**Tech Stack:** C++20, Qt6, WDSP, the Protocol 1 and Protocol 2 codecs, the TCI server.

**Source of the findings:** the inventory's reports in the controller's crew workspace
(`.crew/2026-09-24-receiver-and-transmit-gaps-plan/inventory-*.md`), read at integration
`93a5708e`. Line numbers below are at that commit.

## Global Constraints

- Work in the worktree, branch and build directory the controller names at dispatch.
- Commits: GPG-signed with hooks (`NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis`), never
  `--no-gpg-sign` or `--no-verify`, no `Co-Authored-By`, no em-dash characters. Stage
  explicit paths only. Every commit names the requirement or design section it implements.
- Source first. Any logic that has a Thetis equivalent is read from Thetis
  (`/Users/j.j.boyd/Thetis`, v2.10.3.15 at 3759d096) before it is written, with inline cites
  (`// From Thetis <file>:<line> [v2.10.3.15]`) and every author tag preserved. For Hermes
  Lite 2 behaviour, `/Users/j.j.boyd/mi0bot-Thetis` is authoritative. For a hardware fact
  (receiver count, rate), prefer the gateware (`/Users/j.j.boyd/n1gp-Anvelina_PROIII` at
  8e86a61) as CLAUDE.md describes. If the source cannot be found, stop and report
  NEEDS_CONTEXT; never guess a register, bit or constant.
- Nothing in a test keys a radio. A transmit change is pending on the bench until the
  operator tries it.
- Remote parity: a remote window does what a local one does, through the Core.
- Operator wording: plain user words; every new or changed string passes
  `OperatorWording::isPlain`; no source cites inside strings; "Core" for the NereusSDR
  computer.
- Tests: prefix every ctest and test binary with `QT_QPA_PLATFORM=offscreen`; build exact
  targets (test executables are EXCLUDE_FROM_ALL); run by exact name with
  `--no-tests=error`; tests labelled `realtime` run alone. Tests never open real audio
  devices. A change to wire bytes also runs `tst_p1_regression_freeze` and
  `tst_p2_regression_freeze`, and updates a baseline only in its own commit with the reason.

## What already exists

- Slices: `RadioModel::addSlice` (`src/models/RadioModel.cpp:7023`), `addSliceImpl` (:7056),
  `addSliceOnPan` (the only cap check, :7707-7720), `addSliceWithStationId` (:4478, the remote
  window's mirror of the Core's slices), `maxSlices()` (:4354). The session verb `addSlice`
  (`src/core/session/SessionCommandDispatcher.cpp:550`). `SliceStreamAllocator`
  (`src/core/SliceStreamAllocator.{h,cpp}`; `m_maxSlices` stored at :25-30, never read in
  placement at :90-170). DaemonApp's startup top-up (`src/core/daemon/DaemonApp.cpp:968-994`).
- The live sample-rate change: `RadioModel::setSampleRateLive` (:17812-18015); step 1 drains
  only channel 0 (:17866-17869), step 9 re-enables only channel 0 (:17977-17979), while the
  rate is set on every slice's channel (:17927-17931).
- PureSignal receivers: `P1CodecStandard.cpp:905-926`, `P1CodecHl2.cpp:912-932`,
  `CodecContext.h:541-586` (`PsDdcConfig`), the fixed pattern in `P2CodecHermes.cpp:248-254`.
- TCI: `TciServer.cpp:2660-2749` (the `trx` intercept and the TX-audio holder), `:157`;
  `TciProtocol.cpp:1898-2003`; the setting `TciRateLimitMsgsPerSec`
  (`src/gui/setup/CatNetworkSetupPages.cpp:285-287`), never read by `TciServer`.
- Transmit keying: `MoxController` (`src/core/MoxController.{h,cpp}`; "last setter wins" at
  h:567-570; the missing unkey subscriber at h:562-565); raw `setMox` callers
  (`TxApplet.cpp:1084-1087`, `ContainerButtonDispatcher.cpp:253`, `TwoToneController.cpp:179,
  213, 397`, the TCI shim `RadioModel.cpp:16875-16887`); `TxSliceArbiter`
  (`src/core/TxSliceArbiter.{h,cpp}`).
- Boards: `src/core/BoardCapabilities.cpp` (Angelia :472-505, Orion :534-566);
  `docs/architecture/2026-05-26-phase3f-multi-pan-multi-slice-design.md` section 2 table.
- Tests to extend: `tst_board_capabilities_phase3f`, `tst_codec_ps_ddc_config`,
  `tst_p1_codec_standard`, `tst_p1_codec_hl2`, the `tst_mox_controller_*` family, the
  `tst_tci_*` family, the slice and allocator tests, `tst_unbuilt_features`.

## Task 1: Every path that adds a slice keeps to the slice limit

**Requirements:** Phase 3F design section 3 (the slice cap and its message, line 188);
R-R3-21 (a remote window's request is held to the Core's rules).

**Files:** Modify `src/models/RadioModel.cpp` (`addSliceImpl`, `addSlice`, `addSliceOnPan`),
`src/core/SliceStreamAllocator.{h,cpp}` if its unused cap goes, and the session verb's result
path if it needs one. Test: the existing slice-cap test, or a new
`tests/tst_slice_cap_every_path.cpp`.

**Interfaces:** Produces the same `sliceAddRejected(QString)` wording as `addSliceOnPan`
("%1 supports a maximum of %2 slices") on every path that creates a slice on the radio's own
side.

**Acceptance:**
- At the cap, a local `addSlice()` returns -1, creates nothing, and emits the cap message.
- At the cap, the session verb `addSlice` is refused with the same plain reason in its command
  result, and the Core creates no slice.
- `addSliceWithStationId` (a remote window reproducing a slice the Core already made) is not
  refused by the window's own count.
- DaemonApp's startup top-up still stops at `min(requested, maxSlices)`.
- Below the cap nothing changes. The allocator keeps no cap field that nothing reads: either
  placement reads it, or it goes.

**Verification:** ordinary bug; the verb-path test first (red before the fix).

**Execution note (advisory):** opus.

- [ ] **Step 1:** Test, fix, commit.

## Task 2: A live sample-rate change stops and restarts every slice's DSP channel

**Requirements:** the live-apply rule in CLAUDE.md ("RadioModel::setSampleRateLive (12-step
sequence ported from Thetis setup.cs:7003-7159 [v2.10.3.13])"); Phase 3F section 3 (one WDSP
channel per slice).

**Source first:** Thetis `setup.cs:7003-7159` (how the sequence treats each receiver channel)
and `cmaster.c:453-507` (`SetXcmInrate`).

**Files:** Modify `src/models/RadioModel.cpp` (`setSampleRateLive`). Test: the existing live
rate test, or a new `tests/tst_sample_rate_live_all_slices.cpp`.

**Acceptance:**
- With slices on channels 0, 1 and 2 active, every one of them is inactive before any rate is
  applied and active again afterwards, in the order Thetis uses for its channels (cited).
- A channel that was inactive before stays inactive.
- No channel's input rate changes while it is active.
- One-slice behaviour is unchanged; the existing live-rate tests pass.

**Verification:** a consequential state transition; the multi-slice test first (red: channels
1 and 2 stay active during the change).

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read the Thetis sequence, test, fix, commit.

## Task 3: PureSignal feedback receivers on Protocol 1 radios

**Requirements:** Phase 3F design section 16.3.2 (the PS4 defect); the 3M-4 PureSignal work.

**Source first:** Thetis `console.cs` `UpdateDDCs` (the Protocol 1 Hermes-class branches) and
mi0bot-Thetis `console.cs` `UpdateDDCs` (the HL2).

**Files:** Modify `src/core/codec/P1CodecStandard.cpp`, `src/core/codec/P1CodecHl2.cpp`,
`src/core/codec/CodecContext.h`. Tests: `tst_p1_codec_standard`, `tst_p1_codec_hl2`,
`tst_codec_ps_ddc_config`; the P1 wire baseline if bytes change.

**Acceptance:**
- P1CodecStandard: the assignment under PureSignal with MOX matches Thetis (cited). Stream 0
  is no longer set before the PureSignal branch decides, as section 16.3.2 fixed in
  `P2CodecHermes`.
- HL2: `psFwd` and `psRev` under PureSignal with MOX match mi0bot-Thetis (cited). The code
  and the comments at `P1CodecHl2.cpp:912-932` and `CodecContext.h:558-561` agree.
- A wire byte change updates the P1 baseline in its own commit, with the reason.

**Verification:** transmit-coupled; tests first. Bench pending: PureSignal on a Hermes-class
Protocol 1 radio and on the HL2.

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read both sources, test, fix, commit.

## Task 4: TCI's rate limit, a second client's transmit request, and stale comments

**Requirements:** R-R3-49 (every control a user can see does what its label says); the TCI
design (`docs/architecture/2026-05-09-phase3j-1-tci-port-design.md`).

**Source first:** Thetis's TCI server (`TCIServer.cs` or its equivalent under
`Project Files/Source/Console/`): any incoming-message rate limit, and what it does with `trx`
from a client while another client holds transmit audio.

**Files:** Modify `src/core/TciServer.cpp`, `src/core/TciProtocol.cpp` if needed,
`src/gui/setup/CatNetworkSetupPages.cpp` or the unbuilt-features list,
`src/core/TxSliceArbiter.h`, `src/models/RadioModel.h`. Tests: the `tst_tci_*` family,
`tst_unbuilt_features`.

**Acceptance:**
- **The rate limit.** If Thetis limits incoming messages, the setting does what Thetis does
  (cited), with a test. Otherwise the control is hidden in local and remote windows through
  the unbuilt-features list, with its reason.
- **A second client's `trx`.** Today the second client is refused transmit audio but still
  keys MOX (`TciServer.cpp:2694-2705` falls through to the protocol), and any client can
  unkey. Make it do what Thetis does (cited): if Thetis keys on any client's `trx`, keep that
  and say so in the log line; if Thetis refuses, refuse without keying and answer
  `trx:N,false` to that client. Test with two clients and a fake MOX.
- **Stale comments** (no behaviour change):
  - `TciServer.cpp:157` ("the Core runs none"): the Core runs a receive-only station server.
  - `TxSliceArbiter.h:46-47` ("waits for moxChanged confirmation"): it relies on a
    synchronous `setMox`.
  - `RadioModel.h:1046` ("positional"): it matches `sliceIndex()`.

**Verification:** transmit-coupled for the `trx` item; tests first.

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read the Thetis TCI server, test, fix, commit.

## Task 5: Angelia and Orion sample rates and wideband ADCs

**Requirements:** CLAUDE.md's hardware-fact rules (gateware for receiver counts and rates);
the Phase 3F design section 2 table.

**Source first:** Thetis (the per-model sample-rate lists and wideband settings in `setup.cs`
and `console.cs`) and the gateware.

**Files:** `src/core/BoardCapabilities.cpp`, the Phase 3F design's section 2 table, any plan
that states the old value, `tests/tst_board_capabilities_phase3f.cpp`.

**Acceptance:**
- Today the code gives Angelia and Orion a 384 kHz top rate and `widebandAdcs = 0`, while the
  design table says 192 kHz and 2. Each of the four values is settled from a cited source.
- Whichever side is wrong is fixed in the same commit as the table it came from (the code row
  or the design table), with the invariant asserted by a test.
- No other board row changes.

**Verification:** a capability table; tests of the values. Hardware pending (no Angelia or
Orion on the bench).

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read the sources, fix, test, commit.

## Task 6: The Phase 3F design document says what shipped

**Requirements:** the documentation rule (a plan or design states what is true).

**Files:** `docs/architecture/2026-05-26-phase3f-multi-pan-multi-slice-design.md`.

**Acceptance:**
- The header status says sub-epics A-G shipped and H (bench) is pending.
- The pan section describes the 9 layouts of up to 5 pans that shipped, not 5 templates.
- Section 16.2.6's `FilterChainRouter` is marked not built; the chain logic lives in
  `RadioModel::chainForStream`.
- Section 16.3.2 records Task 3's outcome.

**Verification:** a read-through.

**Execution note (advisory):** sonnet (documentation only); after Task 3.

- [ ] **Step 1:** Edit, commit.

## Task 7: Transmit keying sources follow Thetis

**Requirements:** the 3M-1 transmit work (`docs/architecture/phase3m-1a-*`,
`phase3m-1b-mic-ssb-voice-plan.md`).

**Flag:** this task changes how transmit keys and unkeys. It is a candidate for an earlier
independent review; the operator decides before it runs. It runs last.

**Source first:** Thetis `console.cs`:
- `chkMOX_CheckedChanged` (the MOX button's PTT mode);
- `PollPTT` (the mic PTT keys in MIC mode, and a release unkeys only in MIC mode);
- where Thetis resets the PTT mode on unkey;
- VOX's release rule.

**Files:** `src/core/MoxController.{h,cpp}`, the raw `setMox` callers named above, and the
`tst_mox_controller_*` family.

**Acceptance:**
- Each keying source sets its PTT mode as Thetis does: the MOX button and container buttons
  (manual), two-tone, and TCI (`PttMode::Tci`, through the `RadioModel::setMox` shim).
- The mode clears on unkey as Thetis does. The missing subscriber that `MoxController.h:562-565`
  describes exists, or the header changes to what the code does.
- Releases are guarded by source as Thetis guards them: a mic release during a manual key, and
  a VOX release during a manual key, each do what Thetis does, with a test per pair.
- The existing MoxController tests pass.

**Verification:** transmit keying, high risk; tests first. Bench pending on real transmit,
with the operator's go-ahead.

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read the Thetis PTT code, test, fix, commit.

## Task 8: A stopped DSP channel is fed until its stop completes

**Requirements:** the live-apply rule in CLAUDE.md; Phase 3F section 3. Found by Task 2's review
(C1) and its fix wave.

**Source first:** WDSP `channel.c` `SetChannelState` (about 280-310) and `iobuffs.c` (about
540-565) in `third_party/wdsp/src`; Thetis's own I/Q flow while a channel stops.

**Files:** `src/core/RxChannel.{h,cpp}` (`applyActive`, `processIq`,
`deactivateWithoutDrain`), `src/models/RadioModel.cpp` (`setSampleRateLive`), tests.

**Acceptance:**
- Today `RxChannel` marks itself inactive before `SetChannelState(ch, 0, 1)`, and `processIq`
  stops feeding the channel. So the drain never completes; each stop waits out WDSP's
  timeout (about 100 ms). With five slices a live rate change blocks the GUI thread about
  half a second.
- After this task, a stopping channel keeps being fed (as Thetis's I/Q keeps flowing) until
  WDSP reports the slew complete. Then it counts as inactive.
- The no-drain stop becomes safe to use, as Thetis uses it for the other channels.
- A live rate change with five slices finishes within one DSP block per channel, not
  100 ms each. The test measures and asserts the bound.
- Task 2's audio test (a channel already at the new rate stays audible) still passes.

**Verification:** a consequential state transition; tests first. Bench pending: a live rate
change with five slices on the G2.

**Execution note (advisory):** opus.

- [ ] **Step 1:** Read the WDSP source, test, fix, commit.

## Task 9: Follow-ups from the checkpoint

**Requirements:** R-R3-21 (wording), R-R3-26 (reachable listeners), R-R3-50 (licences), the
fast-test-loop rules.

**Items:**
1. **A CI build with tests off.** Add a CI step that configures and builds the app and
   `nereusd` with `NEREUS_BUILD_TESTS=OFF` (one platform is enough; Linux, reusing the
   job's ccache). A function defined only in a test-only block then fails CI, not a release
   build. The 2026-09-24 checkpoint found two: `AudioEngine::configureSpeakersConverter` and
   `HardwarePage::showAntennaTab`.
2. **`station_bind`** (the accessory listeners) reads its address the plain way, so
   `station_bind = ::` is IPv6-only. Use the same `listenAddressFor` as the remote listener,
   with a test binding `::` and connecting over 127.0.0.1 and ::1.
3. **`tst_media_transport`** does a real encrypted loopback handshake with a fixed 10 s wait,
   which misses at load 20-30. Give it the `REALTIME` option.
4. **The parked Minors from the review of Tasks 1-2:**
   - a window with no Core says "The Core supports a maximum of 5 slices" before its pool is
     sized: choose the subject by role;
   - stale comments at `MainWindow.cpp` (about 5544-5546, "1 slices"), `RadioModel.h`
     (about 3313-3314, `maxSlices()`) and `RadioModel.cpp` (about 18062's heading);
   - `tst_status_toast_preserves_bottom_bar.cpp:122`'s "1 slices" sample text.
5. **The licence check's rule 5** compares the crate notices only with
   `third_party/deepfilter/COMMIT`. Also require the pins in `setup-deepfilter.sh`
   (`DFNR_COMMIT`) and `setup-deepfilter.ps1` to agree.
6. **crunchy 0.2.2 and realfft 3.3.0** declare MIT but ship no licence file. Fetch each one's
   upstream licence text at the matching version, byte for byte, and add it to
   `deepfilternet-crates.txt`, marked as from upstream. Downloading these crates' own texts
   falls within the operator's DeepFilterNet approval of 2026-09-24.

**Verification:** each item's own test or check; the CI step read, and run once by hand in the
Linux container if possible.

**Execution note (advisory):** opus; items can be separate commits.

- [ ] **Step 1:** Each item, test, commit.
