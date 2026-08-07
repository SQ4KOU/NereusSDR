# Remote-daemon R2 verification

Design: `docs/architecture/2026-07-28-remote-daemon-architecture-design.md`
Addendum: `docs/architecture/2026-08-03-remote-daemon-r2-r3-design-addendum.md`
Plan: `docs/architecture/2026-08-03-remote-daemon-r2-plan.md`

**Status: matrix populated by Task 16 (2026-08-06).** The gate's automated
half (Step 1 and Step 2, below) ran and is **GREEN**: 623/623 tests passed
with zero failures. The live bench row (Step 3, on the ANAN-G2E) is written
out as a numbered, runnable procedure with its status left **OPEN**; the
maintainer is running it directly and it is not part of what Task 16 itself
executed. See "Task 16: local direct mode regression gate" below for the
full record. The paragraph immediately following this one is Task 5's own
seeded note and is unchanged.

---

## Task 5: the spot-collector gate is a safety measure, not an ownership decision

Task 5 gates `RadioModel::restoreSpotClientAutoStartState()`
(`src/models/RadioModel.cpp`, declared `RadioModel.h:1118`) on
`Role::Local`. Today `nereusd` never calls this method at all (see
`docs/architecture/2026-08-03-remote-daemon-r2-open-issues.md` issue draft
1), so the gate has no observable effect yet. It exists now, ahead of that
defect being fixed, to stop duplicate cluster logins and duplicate PSK
Reporter uploads under one callsign once a daemon-side caller exists:
**which side owns each collector is an open question R2 does not settle.**
Read as an ownership decision, gating all seven sources to the client puts
WSJT-X on the wrong machine.

The measured starting position, derived 2026-08-03:

- `WsjtxClient` binds and **listens** on UDP 2237
  (`src/core/WsjtxClient.h:103` the socket member, `:107` the default port
  field `m_port{2237}`), so its decoder belongs wherever the operator is,
  which is the **client**.
- `PskReporterClient`, `FreeDVReporterClient` and
  `FreeDVRadeReporterBridge` **upload under the operator's callsign** a
  claim about one physical receiver on a frequency only the station knows,
  so they belong to the **station**.
- `DxClusterClient` (also the RBN instance), `PotaClient` and
  `SpotCollectorClient` are read-only downloads whose only consumer is a
  GUI panel and whose login, filters and callsign are per-operator, so they
  are **arguably client**.

R2 turns all seven off on the client (via this gate) because it ships no
spot delivery over the link, so nothing is lost by doing so. **Do not
extend this gate into a delivery path** -- that is a design decision for
whichever task eventually resolves the ownership question above, informed
by a real daemon-side caller existing (open issue 1) and by an operator
actually running both a client and a station and finding out which
placement is wrong in practice.

Cross-reference: this paragraph is the client-side half of design addendum
risk 9 (section 11, item 9); the daemon-side half is open issue draft 1 in
`docs/architecture/2026-08-03-remote-daemon-r2-open-issues.md`.

### Automated coverage

`tests/tst_remote_role_inert.cpp::remoteRestoreSpotClientAutoStartStateStaysSilent`
seeds `DxClusterAutoConnect` / `RbnAutoConnect` / `WsjtxAutoStart` True
against loopback hosts / a local UDP port (never a real remote host --
POTA's real-network `startPolling()` call is deliberately not exercised;
see that test's header comment) and asserts the Local arm still attempts
each start while the Remote arm produces silence on all three. This is
regression coverage for the gate, not a substitute for a live two-process
bench run; Task 16 owns writing and running that row.

---

## Task 16: local direct mode regression gate

This is the gate. Fifteen tasks landed before this one; three of them
(Tasks 3, 12, 13) changed local direct mode's observable behaviour on
purpose. This section's job is proving nothing else did. A red result here
stops the phase before the OpenSSL dependency lands in Task 17.

Verified at HEAD `e40a8545` (branch `claude/remote-daemon-r2`), 2026-08-06.
The gate-regex component table below was originally measured by the
controller at Task 13's own commit, `10d1d35c`; HEAD is nine commits past
that point, including Tasks 14 and 15, which is why `settings_scope` and
`settings_proxy` now match where they legitimately matched nothing before
those two tasks landed.

| Step | What | Result |
| --- | --- | --- |
| 1 | Targeted regression set (`-L models` + 11-component regex) | GREEN: 259/259, then 16/16 |
| 2 | Full suite | GREEN: 623/623 |
| 3 | Bench, local direct mode, ANAN-G2E | OPEN (not run by this task, see below) |
| 4 | Record + commit | this section |

### Why a naive run would not be trustworthy

1. **A dead regex component is invisible.** `--no-tests=error` only fires
   when the *whole* selector matches nothing, not when one component inside
   an otherwise-working selector matches nothing. The gate regex originally
   named in the plan included `radio_store`, which matches zero tests in
   this tree, so the saved-radio coverage the gate exists to provide would
   have been silently absent while the run still reported green. Fixed in
   commit `c8147477`. The corrected regex is used below, and its `ctest -N`
   selection output is reproduced verbatim so the gate's non-vacuousness is
   an artifact, not a claim.
2. **Stale binaries.** `ctest -R`/`-L` executes whatever binary already sits
   on disk; it does not rebuild anything. A prior task in this branch found
   15 of 56 relevant binaries had not been relinked since their source last
   changed, so a sweep against them would report a pass for code that was
   no longer current. Both sweeps below ran only after
   `cmake --build build --target all_tests` completed and a second,
   identical build invocation reported `ninja: no work to do` -- proof the
   whole tree, not just one named target, was fresh before any test ran.

### The three tasks that changed local direct mode on purpose

If a bench or test observation matches one of these three, it is the
intended effect of a landed task, not a regression.

**Task 3** -- commit `8900b2aa` ("feat(r2): make RadioModel::isConnected()
storage-backed"), single commit, no fix round. `isConnected()` changed from
deriving off the connection pointer (`m_connection && m_connection->
isConnected()`) to reading stored state (`m_connectionState ==
ConnectionState::Connected`), because a `Role::Remote` model that
deliberately owns no `RadioConnection` used to report disconnected forever.
That derivation change makes ordering matter inside `teardownConnection()`:
`setConnectionState(Disconnected)` is now hoisted ahead of
`teardownWorkerThreadedConnection()` (maintainer-approved local-direct-mode
ordering change, design addendum section 5), closing a window where
`m_connection` used to go null 24 lines before the model's own state caught
up. **How to recognise it, not mistake it for a regression:** the disconnect
sequence still ends in the same place and should look the same from the UI;
only the internal ordering of when `Disconnected` becomes visible relative
to teardown moved earlier. Pinned by `tst_connected_state_equivalence` and
the `tst_p2_ddc_*` pair (both gate the P2 DDC wire push on `isConnected()`).

**Task 12** -- commit `cae70eb6` ("feat(remote-daemon): R2 Task 12 -
per-slice S-meter gets a model home"), fix round `66066111`. Moved
per-slice S-meter polling out of the GUI-only `MeterPoller` into a new
core-side `SliceMeterPump`, so a headless `nereusd` has a producer for the
value at all. While doing that, it deleted a real duplicate write: before
this task, the active slice's VFO flag was updated by two independent
connects every tick (a generic one and a Slice-A-specific one), with the
Slice-A-specific one always winning by call order, and every other slice
was hardcoded to the "signal average" source regardless of what the analog
S-Meter was displaying. **How to recognise it:** the VFO flag's mini-bar now
visibly follows whichever source (Peak / Average / Max Bin) the operator
has the analog S-Meter's right-click menu set to, for every slice, not just
Slice A. Seeing the flag change which source it tracks when the analog
meter's mode changes is the intended effect (task 12 step 7), not a
divergence. The full deleted-lines inventory lives in
`.superpowers/sdd/2026-08-03-remote-daemon-r2-plan/task-12-report.md` ("GUI
lines/blocks deleted" section); this file does not repeat it. Pinned by
`tst_slice_meter_pump` and `tst_meter_poller_tx_bindings`.

**Task 13** -- commit `10d1d35c` ("feat(remote-daemon): R2 Task 13 -
AppSettings mutator funnel"), fix round `c57d8a64`. Routed every
`AppSettings` mutator and reader through canonical accessors. Zero
caller-visible behaviour change was intended anywhere except the
`radios/*` saved-radio helpers, which were rewritten as part of the funnel.
**How to recognise it:** there should be nothing to see; saved radios
round-trip across a relaunch exactly as before. If they do not, that is a
real regression, not this task's intended effect. Pinned by
`tst_connection_panel_saved_radios` and the `tst_app_settings_*` family.

### Step 1: targeted regression set

**Build**, literal target name from the brief, run after `all_tests` (see
Step 2) had already built everything once:

```
$ cmake --build build --target tests_models
[0/2] Re-checking globbed directories...
ninja: no work to do.
```

`ninja: no work to do` is the expected, correct result here: `all_tests`
had already built this subset moments earlier, so re-running the build for
the `tests_models` target by name found nothing left to do. This is the
idempotency confirmation the brief asks for, applied to this target
specifically; Step 2 below shows the same confirmation for `all_tests`
itself.

**`ctest -L models --no-tests=error`: 259/259 passed, 0 failed.**

```
100% tests passed, 0 tests failed out of 259

Label Time Summary:
core      = 267.01 sec*proc (209 tests)
gui       = 107.11 sec*proc (88 tests)
models    = 308.85 sec*proc (259 tests)

Total Test time (real) = 308.98 sec
```

**Targeted regex.** Dry-run selection first, exactly as instructed, because
this output is the artifact proving the gate is not vacuous:

```
$ ctest --test-dir build -N -R 'settings_mutator_funnel|app_settings|connection_panel_saved_radios|settings_hygiene|spot_settings|connected_state_equivalence|p2_ddc|slice_meter_pump|meter_poller|settings_scope|settings_proxy'

Test project /Users/j.j.boyd/NereusSDR/.worktrees/remote-daemon-r2/build
  Test  #66: tst_connection_panel_saved_radios
  Test #138: tst_app_settings_vax_migration
  Test #139: tst_app_settings_profile
  Test #215: tst_settings_hygiene
  Test #268: tst_meter_poller_tx_bindings
  Test #305: tst_app_settings_migration
  Test #306: tst_app_settings_arbitrary_key_persistence
  Test #309: tst_app_settings_corruption
  Test #519: tst_spot_settings_round_trip
  Test #568: tst_p2_ddc_mask_ownership
  Test #569: tst_p2_ddc_assignment_marshalling
  Test #607: tst_connected_state_equivalence
  Test #616: tst_slice_meter_pump
  Test #617: tst_settings_mutator_funnel
  Test #618: tst_settings_scope
  Test #619: tst_settings_proxy

Total Tests: 16
```

Every one of the 11 regex components contributed at least one test:

| Component | Tests matched |
| --- | --- |
| `settings_mutator_funnel` | 1 |
| `app_settings` | 5 |
| `connection_panel_saved_radios` | 1 |
| `settings_hygiene` | 1 |
| `spot_settings` | 1 |
| `connected_state_equivalence` | 1 |
| `p2_ddc` | 2 |
| `slice_meter_pump` | 1 |
| `meter_poller` | 1 |
| `settings_scope` | 1 |
| `settings_proxy` | 1 |

16 total, matching the controller's pre-dispatch count exactly, now with
`settings_scope` and `settings_proxy` both present (Tasks 14 and 15 had not
landed when that count was first taken at `10d1d35c`).

Execution: **16/16 passed, 0 failed.**

```
100% tests passed, 0 tests failed out of 16
Total Test time (real) =  19.13 sec
```

**Step 1 verdict: GREEN.** 259/259 and 16/16, 0 failures in either sweep
(the two sweeps overlap on several tests, so this is not a claim of 275
distinct tests; every test that ran in Step 1, across both sweeps, passed).

### Step 2: full suite

**Build**, the target that actually covers this whole gate in one pass:

```
$ time cmake --build build --target all_tests
[... 1551 build steps ...]
cmake --build build --target all_tests  1265.52s user 526.45s system 1069% cpu 2:47.53 total
```

Exit code 0. Grepped the full build log for "error"/"FAILED": 2 matches,
both test binary names containing the word ("`tst_tci_silent_error_
invariant`"), not real errors.

Idempotency confirmation, the artifact proving the whole tree (not just one
target) was fresh before any sweep ran:

```
$ cmake --build build --target all_tests
[0/2] Re-checking globbed directories...
ninja: no work to do.
```

A note on the 2:47 build time, per this project's own timing-measurement
caution: this is far below fast-test-loop.md's "~32 min cold" estimate
because the tree was not cold. 623 test binaries already existed from
earlier tasks' work and ccache (33 GB local cache) had a substantial hit
rate banked already. Load average went from 4.88/4.23/3.98 (before) to
58.69/30.70/15.06 (immediately after) on an 18-core host, i.e. very high
contention from parallel linking; this number should not be quoted as a
general "how long does all_tests take" figure without that context.

**`ctest --no-tests=error --output-on-failure`: 623/623 passed, 0 failed.**

```
100% tests passed, 0 tests failed out of 623

Label Time Summary:
core            = 412.63 sec*proc (514 tests)
gui             = 126.27 sec*proc (175 tests)
models          = 159.78 sec*proc (259 tests)
unclassified    =   3.57 sec*proc (5 tests)

Total Test time (real) = 470.42 sec
```

Wall clock per `time`: 7:50.40 total (124.63s user + 25.16s system, 31%
CPU -- consistent with mostly-serial test execution, not the parallel
build). Load average settled back to 4.61/6.08/8.34 by the end.

**Known-acceptable ccache trio, checked explicitly: not hit.**
`tst_cty_dat_parser` (#500), `tst_adif_parser` (#501) and
`tst_dxcc_color_provider` (#503) all **passed** in this run (confirmed
individually before the full sweep finished, and again in the final
summary). `task_68cac297` describes a real but conditional failure mode
(`ccache base_dir` rewriting a `__FILE__`-derived path); this run's ccache
configuration did not trigger it, so there is nothing to explain away here.
The gate is clean, not "clean because we forgave the usual three."

**Step 2 verdict: GREEN.** 623/623, 0 failures, nothing to attribute to the
known ccache issue because it did not occur.

### Step 3: bench, local direct mode on the ANAN-G2E (STATUS: OPEN)

**Not run by Task 16.** The maintainer authorised bench access to the
ANAN-G2E (HermesC10) only, and explicitly excluded the ANAN-G2, a different
radio on the same LAN that an earlier smoke run in this branch reached by
accident (commit `d8f0ad15`, "docs(architecture): bench on the G2E only,
never the G2"). Task 16 did not run discovery, did not connect to anything,
and did not run the real `nereusd` or `NereusSDR` binaries. The maintainer
is running this row directly with the operator.

Numbered procedure, to be executed against the live ANAN-G2E:

1. Launch `NereusSDR` in local direct mode (no daemon involved).
2. Open the connect dialog and run discovery. List every responder together
   with its MAC address and board-type identification byte. Do not connect
   yet.
3. Identify the ANAN-G2E by its board-type byte: discovery byte `0x14`
   decodes to `HPSDRHW::HermesC10`. If a different radio (including an
   ANAN-G2) responds on the same LAN, do not select it.
4. Pin the candidate MAC address and confirm it with the maintainer before
   connecting. If the only responder present does not identify as the G2E,
   stop -- do not fall back to a different radio.
5. Connect to the pinned G2E MAC. Record the pinned MAC address here once
   run.
6. Per-slice S-meter check (Task 12 regression surface):
   a. Confirm each active slice's VFO flag S-meter bar moves with signal.
   b. Confirm the active slice's flag is written once per tick, not twice
      (no double-update or flicker between two competing writers).
   c. Right-click the analog S-Meter, set it to "S-Meter Peak", and confirm
      the VFO flag's mini-bar agrees with the analog needle. Repeat with
      "Max Bin". (Task 12 step 7's flag-vs-needle agreement check; see
      `task-12-report.md`, "Bench-checkable, per the three named local-mode
      risks".)
7. Open Setup -> Multimeter and move the Delay slider. Confirm both the
   composite meter widget's cadence and the VFO flags' cadence visibly
   change together.
8. Disconnect, then reconnect. Confirm the sequence completes cleanly with
   no visible glitch, hang, or stuck "Connected" indicator. This exercises
   Task 3's `setConnectionState`/teardown reordering; the reordering is
   expected to be invisible from the UI.
9. Quit and relaunch NereusSDR. Reopen the connect dialog and confirm the
   G2E's saved-radio entry is still present (Task 13 regression surface).
10. Record PASS/FAIL for steps 6 through 9 here, with the pinned MAC
    address and the date the bench was run.

**Result: OPEN. Not yet run.** A red result in Step 1 or Step 2 above
stops the phase regardless of this row's eventual outcome; those steps
already ran and are GREEN.

### Task 16 overall verdict

**GREEN** for the automated half (Steps 1 and 2: 259/259, 16/16, and
623/623, zero failures across all three sweeps). Step 3 (bench) remains
OPEN, owned by the maintainer, and is recorded above as a runnable
procedure rather than executed by this task.
