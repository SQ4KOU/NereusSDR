# R3 completion implementation plan

> **Execution:** run with `crew` under `cost-aware-execution`. Requirements and
> acceptance cases are binding; one whole-branch review at the end. Runs in the lane
> the controller names at dispatch, after the core-owned accessories plan, the
> unfinished-controls plan and the remote radio hardware plan's Task 6 have landed on
> integration.

**Goal:** close what the R3 plan promises and nothing has built yet, so R3's only open
items are the operator's device checks.

**Architecture:** four small, independent tasks found by a read-only audit of the R3
plan's 38 unchecked items on 2026-09-24 (11 done but never ticked, 2 superseded by
operator decisions, 22 waiting only on a device check, the rest below). Each closes a
visible control that does not do what it says, or a decision already made and not
built, or the plan's own record.

**Tech stack:** C++20, Qt 6 widgets and Core, Qt Test (off-screen); Python for the
install script checks.

**Spec:** the R3 plan (`2026-09-20-remote-daemon-r3-plan.md`) and its requirement map;
the operator's rules: remote parity (a control a local window offers is never
silently disabled in a remote one), until built = hide (R-R3-49), plain operator
words; the operator's decision (questions file, 2026-09-23) that the VAX page says so
if the digital-mode measurement shows Opus costs decodes; the audit's evidence
(controller's session notes, 2026-09-24).

## Global Constraints

- Work in the worktree, branch and build directory the controller names at dispatch.
- Commits: GPG-signed with hooks (`NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis`), never
  `--no-gpg-sign` or `--no-verify`, no `Co-Authored-By`, no em-dash characters. Stage
  explicit paths only. Every commit names its R-R3 IDs.
- Source first: any logic that has a Thetis equivalent is read from Thetis
  (`/Users/j.j.boyd/Thetis`, v2.10.3.15 at 3759d09) before it is written, with inline
  cites and every author tag preserved; for Hermes Lite 2 behaviour,
  `/Users/j.j.boyd/mi0bot-Thetis` is authoritative. If the source cannot be found, stop
  and report NEEDS_CONTEXT; never guess a register, bit or constant.
- Remote parity and receive-only safety: a remote window does what a local one does
  through the Core; nothing added here keys the radio or puts an amplifier in operate.
- Capability versions are per feature; `kSessionProtocolMinor` stays 11; older windows
  see exactly today's wire.
- Operator wording: plain user words; every new or changed string passes
  `OperatorWording::isPlain`; no source cites inside strings.
- Tests: prefix every ctest and test binary with `QT_QPA_PLATFORM=offscreen`; build
  exact targets (test executables are EXCLUDE_FROM_ALL); run by exact name with
  `--no-tests=error`; no unfiltered suite. Tests never open real audio devices. No
  hardware: devices stay pending for the operator's checkpoint.

## Task 1: The amplifier applets connect and disconnect from a remote window

**Requirements:** R-R3-22, R-R3-47, R-R3-21.

The Power Genius and RF-Kit applets' Disconnect and Reconnect are disabled in a remote
window (`tests/tst_remote_gui_gating.cpp` around 3106-3140 and 3408 pin it), although
the Core now owns those connections and offers the verbs (the core-owned accessories
plan, Tasks 2, 3 and 6).

**Files:** `src/gui/applets/AmpApplet.cpp`, `src/gui/applets/Rf2ksApplet.cpp`,
`src/gui/MainWindow.cpp` (remote gating), `tests/tst_remote_gui_gating.cpp`, the
applets' tests, and `docs/architecture/2026-09-20-remote-daemon-r3-verification/remote-controls.md`
(rows 123, 126 and 182).

**Acceptance:**
- In a remote window, the applets' Disconnect and Reconnect send the Core's existing
  verbs; the applet shows the Core's connection state as it changes; a refusal shows
  its plain reason; with an older Core that lacks the verbs, the buttons say why they
  are unavailable.
- Operate, standby and antenna buttons stay with remote transmit (unchanged).
- A local window is unchanged.

**Verification:** the tests above by exact name.

**Execution note (advisory):** opus (small).

- [ ] **Step 1:** Wire both applets, update the gating tests and the matrix; commit.

## Task 2: The Network Watchdog setting does what it says

**Requirements:** R-R3-49, R-R3-21, R-R3-11.

Setup > General > Options saves `NetworkWatchdogEnabled` (`GeneralOptionsPage.cpp:350-360`),
but nothing reads the key and `setWatchdogEnabled()` has no caller; P2 has no known
wire bit in NereusSDR (`P2RadioConnection.cpp:1320-1340`).

**Files:** `src/core/P1RadioConnection.*`, `src/core/P2RadioConnection.*` (only if
Thetis sends a P2 watchdog), the connect path that applies it
(`src/models/RadioModel.cpp`), `src/core/settings/SettingsScope.cpp` (the setting
becomes Core-owned: a radio setting, applied where the radio is), the page, the unbuilt
list if P2 has no watchdog, tests.

**Acceptance:**
- Read first: where Thetis sends the network watchdog for Protocol 1 and whether it
  does for Protocol 2 (`console.cs`, `setup.cs`, `NetworkIO.cs`, `networkproto1.c`,
  `network.c`; mi0bot-Thetis for the HL2). Report the file:line cites before writing
  code.
- Protocol 1: the setting is sent at connect and on change, with the value Thetis
  sends, on a local radio and on the Core; a remote window changes the Core's setting.
- Protocol 2: if Thetis sends one, the same; if it does not, the checkbox is hidden for
  P2 radios through the unbuilt list (its own entry), local and remote.
- A test on the P1 fake shows the bit on the wire for on and off; a remote-window test
  shows the change reaching the Core.

**Verification:** the tests above by exact name.

**Execution note (advisory):** opus (small; source research first).

- [ ] **Step 1:** Source research (report cites); P1 wiring and scope; P2 wiring or
  hide; tests; commit.

## Task 3: The VAX page says Opus can cost the weakest digital decodes

**Requirements:** R-R3-43, R-R3-44, R-R3-21.

The operator decided that if the digital-mode measurement showed a cost, the VAX page
says so. It did: 12 of 180 decodes were lost over 24 kbit/s Opus
(`docs/architecture/2026-09-20-remote-daemon-r3-verification/digital-modes-over-opus.md:156-165`).
Nothing is built.

**Files:** `src/gui/setup/AudioVaxPage.cpp` (a plain note shown in a remote window
when the receiver streams use Opus rather than lossless), its test.

**Acceptance:**
- In a remote window using Opus for receiver streams, the VAX page says in plain words
  that the weakest digital-mode signals may not decode over the compressed stream and
  that the lossless audio choice avoids it; the note is absent with lossless or in a
  local window. The measurement is cited in a code comment beside the string, not in
  it.

**Verification:** the page test by exact name, and `tst_operator_wording_sweep`.

**Execution note (advisory):** opus (small).

- [ ] **Step 1:** The note, its conditions and its test; commit.

## Task 4: The R3 plan says what is done

**Requirements:** R-R3-21 (the plan's record).

The R3 plan's unchecked boxes, ledger table (plan lines about 1351-1374) and the R3
verification README's "Operator acceptance still pending" section and component list
(README about 386-428) are stale.

**Files:** `docs/architecture/2026-09-20-remote-daemon-r3-plan.md`,
`docs/architecture/2026-09-20-remote-daemon-r3-verification/README.md`,
`docs/architecture/2026-09-20-remote-daemon-r3-verification/radio-recovery.md`.

**Acceptance:**
- Every box the audit found done is ticked with its evidence (commit and test); the two
  superseded boxes say which operator decision replaced them (D1: renew the view at the
  end of the gesture, no margin; the soak runs removed as gates on 2026-09-23); every
  device-pending box says exactly what the operator must observe.
- The ledger table and the README's pending list match the audit.
- `radio-recovery.md` records the R-R3-27 observations (a Core started while its radio
  was down found it later and played audio with no restart, 2026-09-23 at 08:24-08:35
  and again at 20:15), from the controller's notes.
- No em dash; the document lint passes.

**Verification:** read-through against the audit; the repository's documentation
checks.

**Execution note (advisory):** sonnet (documentation only).

- [ ] **Step 1:** Update the plan, the README and radio-recovery; commit.
