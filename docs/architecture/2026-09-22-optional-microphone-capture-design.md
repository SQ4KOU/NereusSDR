# Optional microphone capture lifecycle

**Status:** approved by the operator on 2026-09-22. Asked in plain terms, the
operator chose "a small helper program inside NereusSDR runs the mic" over a
background thread inside the app or opening the mic only on demand, together
with the operator behavior below (receiving starts right away, a mic problem
shows in Audio settings with Retry, PC-mic transmit waits for a ready mic and a
fresh PTT press, and the selected mic is never silently replaced). The
implementation plan fixes the exact transport bounds and deadlines and runs the
native permission/latency experiment before production capture is replaced.
The source-intent prerequisite is complete at `5a1c7ef3`. **Requirements:**
R-R3-36, preserving R-R3-06/07/17 and the parent architecture's local desktop
Core/DSP and future R4 microphone transport.

Design workflow: `yonder-brainstorming`; implementation planning and execution
use `yonder-writing-plans` and `yonder-cost-aware-execution` after approval.

## Outcome and established evidence

A microphone that cannot open must not delay receiving, reconnecting,
disconnecting or exiting. The selected microphone must be the one actually
used. Local users retain microphone metering, Test Mic, VOX and ordinary PC-mic
transmission; the remote GUI later reuses the same capture owner for R4.

An installed Mac app spent 541.93 seconds at optional input opening before a
PortAudio error. Related lifecycle tests stopped at the same boundary. The
exact native call was not stack-proven, so this design isolates initialization,
device resolution, open, start and close together, rather than assuming only
`Pa_OpenStream` can stall.

Current source:

- `AudioEngine::start()` opens speakers and then calls `ensureTxInputOpen()`
  synchronously. `setTxInputConfig()` also opens synchronously after destroying
  its prior bus. The hardware-owning daemon follows the local AudioEngine path.
- `PcMicSource` only delegates to `AudioEngine::pullTxMic`; it is not a second
  capture backend or cancellable opener.
- Devices configures persisted `audio/TxInput`. The PC Mic page edits separate
  TransmitModel session properties, and Test Mic currently starts only a meter
  timer. These surfaces do not reliably configure/test the same actual input.
- `PortAudioBus` silently resolves an unavailable named device to a default.
  It already contains the required native-rate resampling; preserve that DSP.
- The source-intent fix makes unavailable PC input silence in ordinary and RADE
  paths. It does not implement readiness admission or safe bus replacement.
- `MoxController::setMox(true)` invokes RadioModel's precheck before interlocks,
  state advance, relay/MOX effects and TX. Release does not consult that check.
- The existing RadioModel teardown stops AudioEngine before the TX worker.
  Capture storage cannot be retired in that order while a worker can read it.

## Alternatives and recommendation

| Approach | Result | Remaining problem |
| --- | --- | --- |
| Open synchronously only on demand | Removes optional input from RX startup | Selecting/testing a microphone can still freeze its caller and shutdown |
| Open on an owned thread in the Core process | Keeps the caller responsive during open | Native open is not cancellable; joining can hang and detaching can race ownership/PortAudio termination |
| Isolate native capture in a child process | Keeps native capture lifetime separate and allows failed capture to be terminated | Adds process supervision, bounded PCM transfer, packaging and native permission acceptance |

**Recommendation:** a supervised capture child, with an event-driven parent
transport on an owned I/O thread. The parent I/O thread never calls native audio
APIs and never waits for a native opener. Native audio callbacks never block on
IPC. Keep the current input backend/resampling in the child rather than changing
audio behavior and lifecycle simultaneously.

PortAudio documents abort/close for an existing stream, and returns a valid
stream handle only after successful opening. Its threading guidance also
requires serialized open/close. Neither establishes safe cancellation of an
in-progress open. Sources: [PortAudio API](https://files.portaudio.com/docs/v19-doxydocs/portaudio_8h.html),
[PortAudio threading guidance](https://github.com/PortAudio/portaudio/wiki/Tips_Threading).

## Operator behavior

1. Receive startup completes without waiting for PC capture. A stored PC-source
   preference alone does not open a microphone in an idle client or daemon.
2. An active local radio session that permits TX and has PC mic selected
   automatically prepares that input after receive startup. This preserves
   ordinary ready-to-use local operation and live mic metering without an extra
   arm button. The conjunction of active session, authority and selected source
   is the capture demand; restoring a preference alone is not.
3. Test Mic obtains a separate capture demand while its page is testing, even
   before radio connection. It releases that demand on Stop Test, page closure
   or destruction. One consumer ending does not stop another active consumer.
4. PC VOX requires the same prepared input before detector operation. A muted
   mic does not release capture: mute continues to mean silence while readiness
   and metering remain observable according to existing meter behavior.
5. Show **Preparing microphone**, **Microphone ready**, or a concrete failure
   beside the microphone controls. Failed state offers **Retry microphone**.
   Do not put capture diagnostics over a receive spectrum/waterfall.
6. A PC-dependent transmit request made before Ready is refused before all RF
   effects, with **Microphone is not ready. Check Audio settings and retry.**
   Becoming Ready never replays a queued PTT/MOX request; a fresh press is needed.
7. If the active PC input is lost or reconfigured during transmission, publish
   unavailable input immediately and use the existing orderly unkey path. Hold
   zeros throughout release. Never switch implicitly to the radio microphone.
8. Failure remains visible until an explicit retry, device change or genuinely
   new eligible session. Do not retry indefinitely or keep stale speech queued.
9. Named-device failure reports that device as unavailable. An explicit Default
   selection still permits normal default-device resolution. Speaker fallback
   remains independent.
10. Remote receive capability opens no TX capture on either endpoint. Later R4
    enables capture on the client that owns its microphone; it must not turn a
    client device preference into a request to open that device on Core.

## Ownership and interfaces

Proposed source units (names are design interfaces, not existing classes):

- `src/core/audio/CaptureSupervisor.{h,cpp}` owns demand leases, configuration,
  generation, status and an owned event-driven I/O worker. It publishes
  `CaptureStatus { state, configuredDevice, actualDevice, reason, generation }`.
  States are Closed, PreparingPermission, Opening, Ready, Failed and Stopping.
  Readiness includes the first valid PCM from the current generation.
- `src/core/audio/CaptureAudioBus.{h,cpp}` exposes a stable parent-side PCM
  reader/meter to AudioEngine. Its queue exists for the worker's full lifetime;
  native streams/pointers never cross into this process. Unavailable state and
  generation changes make reads return no frames and retire buffered speech.
- `src/capture_main.cpp` runs the native input owner with PortAudioBus and its
  existing resampler. It constructs no RadioModel, DSP channels or widgets.
  Native init/open/close and input enumeration stay in the child.
- `AudioEngine` owns the supervisor and stable reader. Its start path does not
  eagerly open input. Configuration updates go through the supervisor instead
  of resetting a native bus while an audio worker may read it.
- `RadioModel` derives active-session capture demand and composes the readiness
  check into the existing MOX precheck. It derives the actual resolved source:
  active TCI, VAX, radio microphone and generated Tune/two-tone do not depend on
  PC capture. Existing band-plan, interlock and remote-capability checks remain.
- Audio Devices and PC Mic/Test Mic bind to one `AudioDeviceConfig` owner under
  `audio/TxInput`. Legacy TransmitModel properties project that config instead
  of remaining a second device selection. Preserve persisted values/defaults.

On disconnect: cancel TX intent and perform ordinary release, stop/quiesce the
TX worker, withdraw capture demand, retire samples/status, stop the child and
then release reader storage. Asynchronous completions carry generation and
session identity; a retired result cannot publish Ready into a new connection.

Reconfiguration deliberately retires the old input before preparing the new
one. Keeping old capture active while displaying a new selected device would
misrepresent the source. On failure, show Failed for the requested device.

## Process and PCM contract

Use inherited private process pipes, without a network listener. Parent control
messages configure/open/stop a generation; child status identifies protocol
version, generation, actual selected device and negotiated format. Framed PCM
records include generation, monotonically increasing frame position, frame count
and canonical 48 kHz mono float samples. Reuse existing mono extraction and
native-rate resampling behavior; do not add a new gain/downmix algorithm.

Every parser has explicit maximum record/storage bounds and rejects malformed
lengths, unsupported formats, non-finite samples and stale generations before
publication. Control/error records remain distinct from PCM and child diagnostic
logging goes to stderr. There are no unbounded queued Qt PCM signals or buffers.
The transport thread feeds the stable reader directly, independent of GUI event
processing. The native callback only writes its bounded local ring.

The implementation plan must specify and test record sizes, queue bounds,
write/backpressure handling and deadlines as one contract. Reuse existing
capture buffer settings; measure added PCM delivery delay before selecting the
transport drain interval. Do not turn existing ring capacity into a new latency
target. Overflow/discontinuity must retire stale buffered speech and expose
unavailability under the same release policy; it cannot replay a long backlog.

Permission is a distinct PreparingPermission phase. Do not count a legitimate
OS consent prompt as a hung native open. After permission resolves, supervise
open/first-PCM and stop with explicit bounded deadlines. On expiry, invalidate
the generation and terminate the child. Its control watchdog must also exit
after parent pipe closure even when the native owner is stuck, avoiding an
orphan microphone process after parent failure. Never kill an unrelated process
by name, detach a parent native-opener thread, or call Pa_Terminate across one.

## Packaging and boundaries of the guarantee

Build `nereus-audio-capture` from the same revision as its parent. On macOS,
stage it in the app's Helpers directory with correct private-library resolution;
sign nested content before the containing bundle. Package it with the desktop
on Linux/Windows. The nereusd install component carries no helper, since the
daemon never opens a microphone.

Permission identity must be observed for the signed application and its helper,
including deny/retry behavior. Apple documents the input entitlement and usage
description requirements, but that is not evidence that a new helper inherits
the existing grant. Do not reset TCC or alter system permission databases to
make a test pass. Sources: [Audio Input entitlement](https://developer.apple.com/documentation/bundleresources/entitlements/com.apple.security.device.audio-input),
[media authorization](https://developer.apple.com/documentation/bundleresources/requesting-authorization-for-media-capture-on-macos).

This boundary isolates optional capture. Parent-side output initialization,
speaker opening and VAX are separate native operations; this design does not
claim that every host-audio API is now asynchronous. If measurements show another
blocking boundary, track and fix that boundary explicitly.

## Acceptance and implementation handoff

- A fake child that never completes open cannot delay radio receive startup,
  reconnect, disconnect or exit. A stale success after cancellation is ignored.
- Child crash, malformed records, partial reads/writes, backpressure, no first
  PCM, input loss, reconfiguration and parent death have deterministic tests.
  No stale speech or Ready state crosses generation/session boundaries.
- Rejected PC-dependent keying emits no hardwareFlipped(true), antenna/MOX
  change, TX worker start or RX withdrawal. Unkey remains available in every
  failure state. TCI/VAX/radio/generated paths preserve their existing checks.
- Devices and PC Mic show the same actual configuration. Test Mic really opens
  that input, displays readiness/failure and releases only its own demand.
- Native acceptance covers packaged macOS permission and named-device identity,
  Windows/Linux child lifecycle, input continuity/added latency, and the Rock
  session reconnect. A successful fake test does not satisfy these checks.
- Safeguarded hardware TX remains a separate operator checkpoint; this work
  must not enable the unimplemented remote TX protocol ahead of R4.

This architecture and operator behavior were approved on 2026-09-22. The
implementation plan fixes exact IPC/lifecycle parameters
and file/test ownership, including a native permission/latency experiment before
replacing production capture. Unproven native behavior is an explicit dependency,
not a guessed API contract. R3 completion and the full Core/GUI goal remain open.

## Addendum 2026-10-16: one helper for the whole session (macOS)

**Status:** approved by the operator on 2026-10-16 ("yes, build the
persistent helper"); the Mic Mode step, the 1-second hold and the background
device list were approved the same day, after the first bench launch.
Requirement R-R3-36, unchanged in its protections.

### What the Mac showed

- On macOS 27, a bundled app's first Core Audio call reads the Mic Mode
  stored for its bundle id. When that mode is Voice Isolation, Core Audio
  re-applies voice isolation across the system for 40 to 100 seconds, with
  about 30 to 40 processes reacting (a "storm"). Over 18 hours of system logs,
  a stored Voice Isolation mode stormed on 6 of 6 window launches; a stored
  Standard mode stormed on 0 of 92 first calls.
- During a storm, a process that is new to Core Audio blocks in its first call
  (the HAL client check-in, waiting on the audio server) until the storm ends.
  A process that is already a Core Audio client keeps working: the window
  opened its speakers in the middle of one.
- The helper was started for each open, so every open was a new client. Its
  first call stalled between "audio engine made" and "devices listed", the
  10-second open deadline killed it, and the next one stalled the same way:
  the microphone reached Ready 45 to 110 seconds after launch.
- The helper has no bundle id. macOS logs that voice isolation is unavailable
  for a client without one, and the helper never reads the Mic Mode. That
  holds for a helper the application starts, although macOS counts the
  application as the helper's responsible app: in one day of logs, 79 helpers
  the application started each logged that voice isolation was unavailable to
  it, and no helper read a Mic Mode.
- Picking Standard for NereusSDR in Control Center did not change the stored
  mode in the operator's test, so asking users to change it is not a fix.
  Writing the stored mode ourselves (a private setter) and an opt-out made
  before the first call (whose own Mic Mode read started the storm) were both
  set aside at first; the bench launch below brought the setter back.
- A bench launch of the standing helper with Voice Isolation stored confirmed
  that the helper reads no Mic Mode. But the helper had not warmed within the
  first hold of 3 seconds, and the window's own first Core Audio call read the
  stored mode and set the storm going. That call was the device catalog's
  first listener registration, on the GUI thread; the launch's microphone
  permission query made no Core Audio call. The catalog then held the GUI
  thread up to its 3-second wait for the first device list.
- During that storm the running helper's microphone open stalled too, after
  its device list: an existing client does not get through every call, and
  device lists and input opens wait on the storm in every audio process. So
  the only fix is to keep the storm from starting: the window's stored mode
  must be Standard before the window's first Core Audio call.

### The change

- **One standing helper for the application.** On macOS, `main()` makes a
  `CaptureStandby` before any session or window starts. It owns the
  application's one `CaptureSupervisor` and holds its
  `CaptureSupervisor::Demand::Standby`, which keeps one helper running with
  no microphone open. Every window's AudioEngine uses that supervisor
  (`AudioEngine::setSharedCaptureSupervisor`) in place of its own and leaves
  it running when it goes, so the helper outlives a window: switching Cores
  retires the window and its AudioEngine, and the next window's opens run on
  the same helper. Windows, Linux, nereusd and the tests hold no standby and
  run as before.
- **The start is ordered, not raced.** The first session (and with it the
  window, its device catalog, its speakers and the launch's microphone
  permission query) starts from the event loop once the helper has answered
  Warmed, or after 1 second (`CaptureStandby::kWarmWaitMs`; 3 seconds before
  the bench launch), whichever comes first. Nothing blocks meanwhile: the GUI thread runs its event loop, and
  when no helper is installed nothing is held. The helper's first audio call
  and device list therefore come before the window's first native audio call
  unless the helper takes longer than the bound. The log records how long the
  start was held and what ended the hold.
- **Warm.** Protocol version 5 adds `Warm` (window to helper,
  `{"micModeBundle"}`) and `Warmed` (helper to window,
  `{"inputs","startNs","listedNs","micMode","micModeBefore"}`). The
  supervisor sends Warm after the helper's hello while standby is held. The
  helper first keeps the named bundle's Mic Mode at Standard (below), then
  makes its audio engine backends and lists the default engine's devices,
  opening nothing, and answers with what came of the Mic Mode, the number of
  inputs, and when the warm-up began and the listing ended on the steady clock
  both processes share. A test run lists nothing and answers at once.
- **The window's Mic Mode kept at Standard.** On the Mac the window's Warm
  names its own bundle id (`CaptureSupervisor::Options::micModeBundle`), and
  the helper, as the first step of its warm-up, reads the Mic Mode stored for
  it; when it is anything but Standard, the helper sets Standard and reads it
  back (`MicModeKeeper`). The calls are Apple's own, the ones Control Center's
  Mic Mode panel makes: private, found at run time in AVFoundation, and
  guarded so that a missing call or an exception is an answer, never a crash
  (`MacMicMode.mm`). The helper has no bundle id, so these calls are not
  keyed on it. Warmed carries the outcome (`not-asked`, `standard`,
  `made-standard`, `refused`, `unavailable`) and the mode read before, and the
  window logs one line: the old mode when it changed it, a warning when the
  system refused or could not read it, nothing when it was Standard. The
  supervisor names the bundle in every Warm until one helper has answered for
  it, so a helper killed in its warm-up leaves the step to the next. nereusd
  and the tests have no bundle id and ask nothing; a test run's helper never
  makes the system's calls (it uses stand-ins, or answers `unavailable`).
  This changes what a user sees: a user who picks Voice Isolation or Wide
  Spectrum for NereusSDR in Control Center finds it back at Standard at the
  next launch. NereusSDR wants none of the system's voice processing on its
  microphone.
- **The window's first device list in the background.** The window's device
  catalog no longer waits for this computer's first device list
  (`AudioDeviceCatalog::FirstList::InBackground`,
  `AudioEngine::setDeviceListInBackground`). This one is not Mac-only: the
  window never waits on audio, on any platform. The list arrives through the
  main thread's queue, and the Setup page's Rescan note follows it. A device
  start asked before it (a radio connecting, the Core speaker) waits for it
  instead, for what is left of the same 3 seconds counted from the catalog's
  start (`IAudioDeviceCatalog::firstListWaitLeftMs()`), so a chosen device is
  never judged against the empty list (it would read as not connected and open
  only on its retry). nereusd and the tests still wait in the catalog, as
  before, and a device start after that wait does not wait again.
- **Opens during the warm-up.** The helper runs its commands in order, so an
  open sent during the warm-up would only wait behind it. The supervisor keeps
  such a generation Opening and sends its Configure and Open once the helper
  has warmed; the 10-second open deadline starts then. The warm-up has its own
  deadline, 120 seconds (`warmTimeoutMs`), longer than any storm seen: a
  helper that has not answered Warmed by then is killed, and a generation that
  was waiting on it fails as Timeout.
- **Reuse.** Every generation after that (an open, a close, a device change, a
  retry, a remote window's uplink, a network reconnect, a Core switch) runs on
  the same helper. Closing the microphone stops its generation only, as an
  ASIO demand already did; the helper ends when the application quits.
- **Protections kept.** The window never waits on the helper: the held start
  is bounded at 1 second and runs from the event loop, and no other path
  waits for Warmed. The hello (3 s), open (10 s) and stop (2 s) deadlines are
  unchanged, and a helper that misses one is killed as before. Failed still
  persists; no generation is retried by itself. Receiving, reconnecting and
  quitting do not wait on the microphone; quitting is still bounded by the
  stop deadline.
- **Replacement.** When a helper that standby holds ends (killed at a
  deadline, crashed, or exited), one fresh helper starts at once and warms,
  with no microphone open. Another replacement is allowed only after that
  helper has answered Warmed, so a helper that cannot start does not loop,
  and no more than three times within a minute, so a helper that warms and
  then ends does not loop either; past that, no helper stands by until a
  microphone demand starts one. A fresh helper is never started for an open
  while one is running.
- **No microphone while idle.** Warm opens no input, so macOS shows no
  microphone indicator until a demand opens one, and the helper still never
  reads the Mic Mode.
- **Measurement.** Each process marks its first native audio call
  (`NativeAudioFirstCall`, fed by the traced Core Audio queries, the Core
  Audio stream opens, the VAX device watch, the real-time workgroup lookups,
  and the PortAudio start) and logs it once. The launch's microphone
  permission query is not marked: the bench launch showed it makes no Core
  Audio call, so the window's mark names the catalog's listener
  registration. On Warmed the supervisor logs how long the helper took to list its
  devices and whether that ended before or after the window's first native
  audio call. The mark is a measurement: nothing waits on it.

### Boundaries

- If the helper has not set the Mic Mode within the 1-second bound (a slow
  start, or a storm another app set off), the window starts anyway and its
  first native audio call can come first and read a stored Voice Isolation.
  The helper's warm-up then waits out the storm, up to its 120-second
  deadline, and an open waits with it, Opening. The log lines above show which
  came first. At the next launch the stored mode is Standard.
- The window's first Core Audio call (the catalog's listener registration)
  still runs on the GUI thread. If another app has set a storm going, that
  call can block the GUI thread until it ends; moving it off the GUI thread
  is a threading change outside this addendum.
- The Mic Mode calls are private and have run only in a probe, never on the
  application's own bundle id at a real launch: that the set takes, how long
  it takes, and that the window's first call then reads Standard are still to
  be measured on a bench launch, with the time the first window was held and
  the microphone's time to Ready.
