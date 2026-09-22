# R3 remote control acceptance matrix

Requirements: R-R3-16/17/21/22/24/25. Execution uses
`yonder-cost-aware-execution`. This matrix records the bounded connection,
capability and telemetry checkpoint; it is not a completed audit of every
Setup page or an assertion that all station accessories work.

## Changed controls and ownership

| Surface | Owner and production path | Automated evidence | Live acceptance |
| --- | --- | --- | --- |
| Radio menu Connect / Disconnect | GUI -> configured StationClient through RemoteConnectionController | Real WebSocket connect, intentional disconnect and reconnect; duplicate connect does not create another session | Installed; intentional disconnect/cancel observed; successful reconnect pending |
| Title connection segment, station block, disconnected pan | MainWindow::connectionRequestedByOperator -> same controller; automatic local panel callbacks remain inert remotely | Actual title mouse activation; controller tests. MainWindow routing inspected, full window interactions still need smoke | Title segment opens details; all three paths and reconnect still pending |
| Core connection details | Persistent endpoint, session state, radio state, failure and retry/cancel; no credentials displayed | Authenticated Core with offline radio; failed dial/cancel beyond retry deadline; credential stripping | Endpoint, separated state and stopped presentation observed; recovery pending |
| Saved layout / snapshot | Core owns slices; GUI restoration only hydrates. Explicit post-snapshot layout/add remains a command | Authenticated attach/reconnect preserves one station slice; production population helper emits no implicit add; explicit action waits for readiness | Fresh attach displayed one slice; reconnect pending |
| TX applet / PureSignal / TX filter match | Negotiated txPermitted + handshake; Core retains admission checks | Widget activation, no mutation when gating, preservation of independent disabled state; remote refusal | Tune/MOX/VOX disabled appearance observed; remaining controls pending; no RF test |
| High-resolution FIR graph | Requires absent local RxChannel; visibly unavailable remotely | Remote/local checkbox interaction tests | Pending appearance; ordinary mirrored RX DSP is not classified as broken |
| Tuner telemetry | Core TunerModel -> outbound mirror -> client-only assign/notify adapter | All 13 fields, false/zero updates and absence of accessory commands | Core connected after restart to .234:9010 and received actual tuner info; live configuration/identity admission remains pending |
| Tuner TUNE / operate / antenna / relay / recall | Receive-only remote capability blocks commands; Core policy additionally guards autotune callbacks | Authenticated band change and telemetry replay issue no tune; MOX/TUNE admission blocked before radio connection and after session teardown | No RF or tuner actuation permitted in this checkpoint |
| Tuner cached values after Core loss | Retained values explicitly marked stale; unsupported remote accessory reconnect disabled | Applet session presentation/command tests | Pending disconnect smoke |

## Current follow-ups (September 22)

The historical results below describe their named checkpoints. This table
tracks subsequent source work; installation and operator acceptance remain
separate, as recorded in the linked evidence and master plan.

| Surface | Remaining work |
| --- | --- |
| Peripheral configuration, scan and connect/disconnect | Task 4d's Core-owned configuration, identity checks and lifecycle commands are implemented. Actual TGXL identity/admission and operator acceptance remain open; see the master plan and deployment ledger. |
| 4O3A master enable / SmartSDR reporting / PGXL band tracking | Core-owned implementation is present; retain task 4d's hardware acceptance requirements. |
| Audio profile and health | Profile/telemetry/recovery implementation is present. Sustained playback remains open: the installed c28e1565 session recorded audio-context recoveries and source queue drops. No cause is inferred from those counters alone. |
| Full-band zoom | ADC-wide display and adaptive transport are implemented. Wideband/3D hardware parity and capacity acceptance remain open; see [display evidence](display-capacity.md). |
| Core/radio and local-radio selection | Task 4g implemented and installed at c28e1565, with desktop Core/DSP retained. Native interaction acceptance is pending; see [selector evidence](station-selection.md). Full pairing/administration remains R6. |

## Evidence and remaining checks

The snapshot regression first failed because automatic and pre-snapshot explicit
population each emitted an add. The accessory regressions first failed on the
missing daemon policy, band-triggered autotune and unapplied tuner fields.
These were behavior reproductions, not merely checks that slots exist.

Connection, visible TX/FIR gating, tuner and daemon suites pass after separating
the daemon test's synthetic controller notification from actual MOX admission.
The real admission request is required to be refused with RX state unchanged.

One consolidated independent review found that raw authenticated TransmitModel
writes still bypassed RadioModel's guard. A loopback regression reproduced the
station adopting the requested MOX model state. StationServer now rejects all
inbound transmit-object writes under its receive-only policy and uses the
existing correction delta to return authoritative values. This regression is
about model admission, not evidence that RF was emitted. The review's second
finding, the remote applet's local accessory reconnect action, is corrected
and covered by disabled-action/no-emission tests.

The lead also found that radio teardown cleared the local-role MOX check,
including the daemon's persistent receive-only policy. A regression using the
real non-null connection teardown path reproduced the missing refusal. Teardown
now preserves the receive-only check, while ordinary local-direct teardown
retains its existing behavior. This complements the session-disconnect test.

An initial unfiltered 682-test run exposed an unrelated popup-exposure wait in
`tst_dss_overlay_menu`. That test verifies pre-show value seeding/no signal echo;
it now performs those same assertions without requiring OS popup exposure.
The next full run passed 682/682 in 90.53 seconds before the additional radio
teardown regression. Final combined source, including the radio-teardown correction, passed a fresh
`all_tests` build and unfiltered `ctest --test-dir build-integration -j8
--no-tests=error --output-on-failure`: **682 test executables passed, zero failed, zero executables skipped**
in **93.58 seconds**. Logs are retained privately as
`r3-controls-teardown-full-{build,test}.log`. Corrected native build and
installation of signed `95b19467` passed; the matching GUI bundle also passed
code-signature verification. The [deployment ledger](README.md) records source
verification, installed hashes and the corrected packaging timestamp failure.

The receive smoke is **failed/incomplete**: authentication and waterfall frames
arrived, but audio stalled, the session timed out and steady spectrum/meter
operation was not established. The user reported slow waterfall painting that
stopped after a VFO movement. Manual Disconnect cancelled retry and remained
stopped. Network reachability to both the board and switch management endpoint
was lost while the router and Saturn remained reachable. No tuning cause or
hardware root cause is claimed; no live tuner connectivity is established.

Hardware smoke is receive-only: attach to the existing station, inspect endpoint
and radio state, disconnect and leave stopped, reconnect once, exercise the
three click surfaces and confirm slice count does not grow. Check disabled
controls and stale tuner presentation. Do not classify cached amplifier data
from the currently misconfigured TGXL endpoint as a working tuner.

The passive discovery capture was re-read after board recovery: the actual
announcement includes `TunerGenius`, version `1.2.17`, serial `241288-1` and
nickname `Tuner_Genius_XL`. An abbreviated investigation note omitted the last
two fields; there is no evidence that this live announcement lacks them.
Native captured `info` uses `serial`, while the existing model accepts only
`serial_num`; that normalization belongs with task 4d's identity validation.

## Scoped follow-up inventory

The second bounded control pass identified Phone/CW TX writers (mic/PROC/VAX/
DEXP), XIT controls, the inert remote TX-slice handoff, VFO RX-bypass-on-TX,
Tools TX Equalizer and editable TX-specific Setup pages. The September 22
follow-up below implements their consistent unavailable presentation and
interaction coverage in task 4c. This does not claim all visible controls or
their eventual remote TX implementations are complete. Keep shared
receive functions available: in particular, the antenna labelled TX participates
in TRX receive routing, so its name alone is not grounds for disabling it.
Review the actual station/receive effect before restricting Slice properties.

## Remaining named TX controls — September 22

Requirements R-R3-16/17/21/24. The existing negotiated `txPermitted` value
now reaches Phone/CW, every VFO flag, open and newly created Setup dialogs,
and Tools TX Equalizer. Remote widgets start unavailable before a handshake.
This is presentation of current capability, not a replacement for R4's TX
commands or Core's independent refusal of TX writes.

| Surface | Concrete boundary and acceptance evidence |
| --- | --- |
| Phone/CW MIC, PROC, VAX source, DEXP | Disable their writer widgets and guard direct value/context callbacks. Tests activate the widgets, observe zero model mutation or setup request, preserve model-to-widget readback, and restore local interaction without overriding mic mute. |
| XIT | Disable enable/offset/zero. A real MainWindow with an authenticated loopback Core produces no XIT property write or state change, while its RIT button produces an accepted Core change. |
| TX-slice badge and menu | Retain the indicator; suppress badge handoff and disable the actual dynamically constructed context action. Tests execute the nested menu and verify no remote handoff, with local handoff retained. |
| RX-bypass-on-TX | Disable BYPS and its writer callback. Do not restrict the TX-labelled antenna selector used for TRX receive routing. |
| TX Equalizer | Disable the actual Tools QAction and guard activation. The applet's alternate context callback is also guarded. Tests show no remote editor and successful local editor launch. |
| TX-specific Setup | Six explicit leaves: Audio TX Input/TX Profile; Transmit Power/TX Profiles/Speech Processor/DEXP/VOX. Content remains readable with a visible reason; editing and TX-EQ cross-links are disabled. Tests cover each page, local enablement, permission changes and preservation of the independent local-DSP gate. |

VAX Setup configures receive export through the audio engine. It retains its
existing resource restriction; it is not newly labelled a TX-only page.
Receive NR/ANF remains usable. No protocol schema or radio/DSP behavior changes.

Focused verification: rebuilt matching production libraries and 13 relevant
test targets, then ran all 13 successfully in 11.14 seconds with zero inner
Qt skips. The rendered remote Power page was inspected: the unavailable reason
is visible above the readable content, with no overlap. It is an offscreen
fixture, not evidence from the installed Mac.

Consolidated review found that capability updates after the initial handshake
did not refresh existing controls. An authenticated second-capability regression
first reproduced the stale XIT permission, then passed after wiring the existing
station-link notification into presentation refresh. It checks both directions
without changing the session epoch, including an already-open Setup page.

The first unfiltered run passed 739 of 740 targets but exposed a Core teardown
use-after-free in the native receive-layout test. AddressSanitizer reproduced
the VOX callback unregister accessing DEXP after destruction. The correction
unregisters the C++ wrapper while DSP objects are alive, closes the channel,
destroys DEXP and clears its dangling lookup slot before releasing its backing
buffer. The normal test passed in 38.82 s and the sanitizer test in 43.51 s.
The bounded follow-up lifetime review found no actionable issue. This is not
evidence that the earlier physical power or network failures share that cause.

The new context-menu fixture also needed to expose its parent VFO window before
opening native popups; after correction the widget test passed alone and with
the remote display controller. The matching production/all-tests rebuild and
unfiltered suite passed all 740 targets in 259.73 s. Signed checkpoint and
installed interaction verification remain pending. Native inspection of the
previously installed selector exposed a separate macOS accessibility crash;
that acceptance defect is being corrected before delivery.

The full-run load averages were 2.95 / 3.09 / 3.43 before build,
24.95 / 8.82 / 5.51 before CTest, and 7.58 / 8.14 / 6.09 afterward.
Twelve existing inner Qt cases skipped for the same device/UI, optional
capture, modal-menu, obsolete RADE TX and unopened WDSP TX harness reasons
recorded at the selector checkpoint. No executable was excluded or timed out;
those skipped TX behaviors still require R4 evidence.

## Follow-up at 8c011066

Title-segment activation opened Core details while media had failed but the
control session remained connected. Disconnect changed the panel to stopped
with Connect enabled; Connect began a fresh handshake and restored audio at
17:47:35. That verifies this manual recovery path. Automatic media-only
recovery is still missing (R-R3-28); later full control-link losses exercised
the existing retry path. The live receive status is tracked in the current
[deployment ledger](README.md), not the older 95b19467 result above.

## TGXL socket report and corrected endpoint, September 21

At 18:23 the GUI was repeatedly attempting the actual tuner IP with port 9008.
A read-only TCP connection check reached `.234:9010` in 0.01 seconds; port 9008
timed out. The user corrected the port, and the GUI log then showed recurring
status responses. This establishes local-GUI connectivity, not completion of
Core-owned accessory configuration or positive identity validation.

The retry log also exposed a separate lifecycle defect: after a connect timeout,
source binding reported a socket that was not in UnconnectedState, followed by
an invalid descriptor. That fault remains under investigation in task 4d.

The user next reported that ANT1–3 appeared but did not work. These controls
are explicitly disabled by the current receive-only remote policy; they are
not repaired by correcting the TCP port. Enabling tuner RF-path actions remains
part of the authorized remote tuner workflow, with Core ownership and capability
checks. No tuner antenna, operate or tune commands were exercised here.

After installation of 706b9a5f, Core itself auto-connected at 18:31:15 from
`.106` to `.234:9010`; `ss` confirmed the socket belonged to nereusd. It received
the captured real tuner info (`serial=241288-1`, `version=1.2.17`,
`nickname=Tuner_Genius_XL`, `3way=1`). Thus the corrected setting persisted to
Core and applied at restart. This does not establish the missing ordered live
apply/connect contract or identity rejection; no tuner RF action was sent.

A later socket check confirmed the separate ownership defect directly: GUI PID
54322 had `.30:52225 -> .234:9010` established while Core PID 3656 already owned
`.106:37183 -> .234:9010`. The GUI connection began at 18:32:22. This demonstrates
that remote Peripherals can still operate a Mac-local socket; it does not prove
that two clients caused any earlier device/network failure. Task 4d must route
remote configuration/connect/cancel to Core and keep the local socket inert.

The bounded TGXL retry/cancellation regression is tracked in
[TGXL connection lifecycle](tgxl-recovery.md).
