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
but nothing reads the key and `setWatchdogEnabled()` has no caller.

What Thetis does with it (v2.10.3.15; mi0bot-Thetis the same for the HL2). The checkbox
(default checked, `setup.designer.cs:8434`, applied at startup by `setup.cs:2195`) calls
`NetworkIO.SetWatchdogTimer` (`setup.cs:18024-18028`), which stores `prn->wdt` and sends
the general packet when it changes (`netInterface.c:1364-1372`).

- Protocol 2: byte 38 of the general packet carries it (`network.c:897-898`); the 500 ms
  keepalive general packet goes out only while it is on (`network.c:1436`); and an
  established stream waits three seconds for data before the radio is declared lost,
  for ever with it off (`network.c:656`).
- Protocol 1: nothing goes on the wire. The start and stop packets are `0x01` / `0x00`
  whatever the setting (`networkproto1.c:50, 85`). The setting only sets the wait for
  data: three seconds on, for ever off (`networkproto1.c:292-294`; mi0bot 297 and 443).

Operator decision (2026-09-24, from the final review's I1): NereusSDR keeps the radio's own
safety timer on. On Protocol 2, byte 38 is always 1 and the 500 ms keepalive always runs,
whatever the checkbox says, because a radio left keyed when the computer dies is a hazard.
This is a deliberate divergence from Thetis (`network.c:897-898, 1436`). The checkbox
governs only how long NereusSDR waits for data before it declares the radio lost, on both
protocols.

**Files:** `src/core/P1RadioConnection.*`, `src/core/P2RadioConnection.*`, the connect
path that applies it (`src/models/RadioModel.cpp`), the Core's apply on a window's change
(`src/core/session/StationServer.cpp`), `src/core/settings/SettingsScope.cpp` (the
setting becomes Core-owned: a radio setting, applied where the radio is), the page, tests.

**Acceptance:**
- Read first: the Thetis lines above, quoted with file:line in the report.
- Protocol 2: byte 38 is always 1 and the keepalive always runs, with the setting on or
  off (operator decision above); with it off no loss is declared when data stops.
- Protocol 1: the start packet stays as today (no watchdog bit). The wait for data before
  the radio is declared lost is 3000 ms with the setting on and has no limit with it off.
- The setting is a Station setting: applied by the window on a local radio and by the
  Core on the Core's; a remote window's change reaches the Core. An older Core refuses the
  key; the page puts the box back and says the Core needs updating, in plain words. A
  remote window may change the wait; it cannot turn off the radio's safety timer.
- The checkbox's tooltip says, in plain words, what it does: how long NereusSDR waits
  before it treats the radio as lost.
- Tests: the P2 fake shows byte 38 = 1 and the keepalive running with the setting off;
  P1 and P2 loss detection follow the wait; a remote window's change reaches the Core
  and its radio.

**Verification:** `tst_network_watchdog`, `tst_network_watchdog_setting`,
`tst_p1_watchdog_wire`, `tst_remote_gui_gating` by exact name.

**Execution note (advisory):** opus (small; source research first).

- [ ] **Step 1:** Source research (report cites); P2 wire and wait; P1 wait; scope, page
  and Core apply; tests; commit.

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

## Task 5: A window that loses its Core for good says why and what to do

**Requirements:** R-R3-21, R-R3-23 (connection feedback), R-R3-38.

Operator decision of 2026-09-24 (question C, option 1): when a remote window loses its
Core for a reason that will not fix itself, it stays where it is, says what happened in
plain words and offers the next steps as buttons; nothing retries by itself. Today such a
window sits at "Core connection failed" (`RemoteConnectionController.cpp:89`).

**Files:** `src/gui/RemoteConnectionController.{h,cpp}`, `src/gui/MainWindow.{h,cpp}`
(the message and its buttons over the window's content, keeping the layout),
`src/core/session/StationClient.{h,cpp}` (carry the Core's end reason and whether it is
retryable, and who took over when the Core says so), the tests of both.

**Acceptance:**
- Another window or device takes over the Core: this window stays, shows that the Core
  was taken over (naming the other device when the Core says which one) and offers
  **Take it back** (connects again, which takes the Core back) and **Choose another
  Core** (opens Connections); it does not retry by itself.
- The Core refuses this window for good because the app is too old or too new (link
  version): the window says which side needs updating, in the words the Core or
  `LinkVersion::refusalText` gives, and offers **Choose another Core** (and **Check for
  updates** where the app has an update check); it does not retry.
- Any other end the Core marks not retryable: the Core's plain reason and **Choose
  another Core**; no retry.
- A dropped link (retryable) behaves as today: the window retries and says so.
- Every string passes `OperatorWording::isPlain`; local windows are unchanged.
- The pairing cases (removed from the Core, the Core's identity changed) are left to the
  iPhone plan's Part C, which adds them to this same message.

**Verification:** a remote window test with a loopback Core for each case (takeover,
version refusal, other refusal, dropped link), by exact name, offscreen.

**Execution note (advisory):** opus (small).

- [ ] **Step 1:** The message, the buttons, no retry for permanent ends, tests; commit.

## Task 6: The TCI and MMIO pages in plain words

**Requirements:** R-R3-21 (operator wording), R-R3-48.

Operator decision of 2026-09-24 (question D, option 1): keep the words an operator must
match in another program exactly as that program shows them (TCI, TCP, UDP, JSON, XML,
port, IP address; MMIO kept in brackets so Thetis users can find it), and put
NereusSDR's own jargon into plain words. Runs after the accessories plan's TCI rework
lands on integration (it edits the same TCI page).

**Files:** `src/gui/setup/CatNetworkSetupPages.cpp` (TCI Server page),
`src/gui/containers/MmioEndpointsDialog.{h,cpp}`,
`src/gui/containers/MmioVariablePickerPopup.{h,cpp}`, any other surface showing the same
words, their tests, `tests/tst_operator_wording_sweep.cpp`.

**Acceptance (the approved wording):**
- "Bind interface:" and "Bind IP:" read **"Listen on:"**.
- "Endpoints", "New endpoint" and "Endpoint properties" read **"Data sources"**, **"New
  data source"** and **"Data source settings"**.
- "Transport" reads **"Connection type"** (its choices keep TCP client, TCP listener, UDP
  listener and the rest as they are).
- "Discovered variables" reads **"Values received"**; "Pick MMIO Variable" reads
  **"Choose a value"**.
- "Clear binding" reads **"Unlink from this meter"**.
- The MMIO window's title reads **"Meter Data Sources (MMIO)"**.
- The TCI enable tooltip reads **"Turn on the TCI server so programs like WSJT-X or JTDX
  can control this radio."**
- The formats JSON, XML and "RAW (key:value)" and every port and address stay as they
  are; saved settings keys do not change; local and remote windows alike.
- The wording sweep holds the new words (and fails if "endpoint", "binding", "bind
  interface" or "WebSocket" come back in text a user reads on these surfaces).

**Verification:** the pages' tests and the wording sweep, by exact name, offscreen.

**Execution note (advisory):** opus (small; strings and their tests).

- [ ] **Step 1:** The wording, the tests; commit.

## Task 7: Receiver streams use Opus at 48 kbit/s whenever they are compressed

**Requirements:** R-R3-43, R-R3-44, R-R3-23.

Operator decision of 2026-09-24: receiver streams (VAX and TCI audio to apps) that are
compressed use Opus at 48 kbit/s, not 24. The confirming FT8 run (fresh seed 20260924,
5 signals per step, 225 files) decoded untouched 177, lossless 177, opus48 175, opus24
164 (crowded band 58/58/57/49), with the first run (seed 20260923) showing the same
order. The controller's reading, told to the operator: this applies whenever those
streams are compressed (Opus chosen, or lossless fallen back to Opus); the speaker mix
keeps its own Opus rate.

**Files:** the receiver-stream profile choice (`src/core/session/media/DaemonMediaController.*`
and wherever the receiver streams pick their Opus profile; `OpusAudioCodec` already has
the 48 kbit/s fullband profile), the window's side if it assumes the stream's rate, the
receiver audio tests, `docs/architecture/2026-09-20-remote-daemon-r3-verification/digital-modes-over-opus.md`
(add the confirming run: the table above, the seed, the output directory
`/Users/j.j.boyd/.config/nereus/work/ft8-opus48-confirm-2026-09-24`, and the decision).

**Acceptance:**
- A receiver stream sent compressed uses the 48 kbit/s fullband Opus profile, whether
  the operator chose Opus or lossless fell back; the speaker mix's rate is unchanged.
- The Core's accepted settings and the window's audio status report the stream's real
  rate; older windows keep working (the Opus decoder takes either rate; check the SDP
  and the receiver's expectations).
- The VAX page's compressed-audio note stays accurate (the wording may say the cost is
  small; plain words).
- Tests: the profile chosen in both cases, the speaker mix untouched, an older window.

**Verification:** the receiver audio and session tests by exact name, offscreen.

**Execution note (advisory):** opus (small).

- [ ] **Step 1:** Profile choice, reporting, tests, measurement record; commit.

## Task 8: "Core" in every user-visible text that means the Core

**Requirements:** R-R3-21 (operator wording).

Operator decision of 2026-09-24: user-visible text calls the NereusSDR computer you
connect to "the Core", in the desktop and the iPhone app; "station" stays only in its
ham sense (your station, the station callsign, the station's network). Code
identifiers, wire names and the link document's prose do not change. Runs after lane
B's Part A reaches integration (it reworded the reasons the Core sends).

**Files:** every desktop string and every reason or notice text the Core sends that says
"station" for the Core (grep the GUI and the Core's reason writers); the desktop's
"Remote Station" Setup page name (the operator names it; the controller asks);
`tests/tst_operator_wording_sweep.cpp` and the Core's reason-wording test (a rule that
fails on "station" meaning the Core in user text, with an allow-list for the ham-sense
uses); fixtures and goldens that pin a reworded reason (conformance fixtures on
integration, if the Core's reason text changes).

**Acceptance:**
- No user-visible text says "station" for the Core; ham-sense uses remain.
- The Core's reason texts follow (wording only; codes and fields unchanged), with the
  link's conformance fixtures updated and `render-link-tables.py --check` clean.
- The tests hold the rule.

**Verification:** the wording sweeps and the conformance runners by exact name,
offscreen.

**Execution note (advisory):** opus (mechanical but wide).

- [ ] **Step 1:** Reword, tests, fixtures; commit.
