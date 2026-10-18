# Display sending on a thread of its own: plan (2026-10-15)

JJ approved 2026-10-15 ("yes go ahead on its own branch"). Lands after the 2026.10.1 release cut.

## Why

When the desktop app hosts the Core, the Core's display sender is a 5 ms QTimer on the GUI main
thread (`DaemonMediaController::onSendTimerTurn`, `DaemonMediaController.cpp:680-681`,
`DisplayBudget.h:35`), and so is the take-in of every finished FFT frame
(`DaemonSharedSpectrum::onFrameAvailable`, `:607-620`, and `onSourceFrame`, `:4764-4906`). The same
thread draws the spectrum and meters through Qt's Direct3D 11 backend, which can wait up to a
second per frame on the swap chain (`qrhid3d11.cpp:1468-1471`). On the STRIX with the session
locked, the window's sender turned 2 to 4 times a second (gaps 294 to 484 ms, 55 frames a second
replaced unsent), while nereusd on the same locked desktop, with no drawing on its main thread,
kept 6.7 to 9.8 ms with none replaced. Receive audio, which already has its own send thread
(R-AUD-SEND, `DaemonAudioSendThread`), stayed at 40 ms.

## How the references keep network sending off the drawing thread

- **Thetis** draws on its own thread, not the UI thread: `draw_display_thread`, "Run Display
  Thread" (`console.cs:1071-1078` [v2.10.3.15]). Radio network I/O has threads of its own too:
  `MetisReadThreadMain` and `sendProtocol1Samples` for Protocol 1, `ReadThreadMain` and
  `KeepAliveMain` for Protocol 2 (`ChannelMaster/netInterface.c:61-83` [v2.10.3.15]). A slow paint
  never holds a send.
- **AetherSDR** runs each wire object on a QThread of its own, the VITA-49 stream on
  "PanadapterStream" and the command connection on "RadioConnection", and draws on the GUI thread
  (`src/core/backends/flex/FlexBackend.cpp:44-56` [@1e0718ad]). Its threads receive, as a client
  does, but the rule is the same: the network never waits on the GUI.
- **piHPSDR** keeps its radio protocol I/O on threads of its own (`new_protocol.c:480-485`
  [@4aa95c5]). Its server's spectrum send to a remote client is the exception: `send_rxspectrum`
  (`server_thread.c:183`) is called from `rx_update_display` (`receiver.c:575`), a GTK timeout on the
  main loop (`receiver.c:596`), right beside its own panadapter drawing (`receiver.c:578`). So
  piHPSDR shares the arrangement this plan removes; its software drawing (cairo) does not wait on a
  swap chain the way Direct3D does.
- None of the three pauses drawing when the session is locked.

NereusSDR takes Thetis's shape for the sending side: the Core's display sending gets a thread of
its own, the way its receive audio already has one.

## Design

**One display thread per hub.** `DaemonSharedSpectrum` (one per `DaemonMediaHub`, and one inside a
stand-alone controller) owns a new `DaemonDisplaySendThread`, a `std::thread` named
"NereusDisplayTx" built like `DaemonAudioSendThread`: it starts when the first controller needs
it, wakes on a condition variable, and is joined before its owner goes. It does two things:

1. **Take-in.** The FFT engine thread that publishes a frame into a source's one-frame slot
   (`DaemonSpectrumSource::publishFrame`, slot under its mutex at `DaemonSpectrumSource.cpp:673-718`)
   wakes the display thread instead of queueing `frameAvailable` to the main thread. The display
   thread takes the frame (`takeLatest`) and hands it to every controller, as
   `DaemonSharedSpectrum::onFrameAvailable` does now.
2. **Turns.** Every `kDisplaySenderIntervalMs` (5 ms) it runs one sender turn per controller that
   sends from it: today's `onSendTick` unchanged in what it sends and in what order (PureSignal
   and spectrum alternating, display extras right after their frame, the transmit display, raw
   I/Q), at most one display message a turn, paced by the same `DisplayBudgetPacer`. The
   sender-gap diagnostic (e3e59675f3, from pull request 455) is taken at each turn, by whichever
   thread runs it.

**One lock per hub.** All of the controllers' state stays where it is. A recursive mutex owned by
the hub's `DaemonSharedSpectrum` guards it. The display thread holds it for each take-in and each
turn. The main thread holds it whenever it runs controller code: every signal handler, timer and
public method of `DaemonMediaController` and `DaemonSharedSpectrum` takes it on entry. No audio
callback, DSP thread, FFT engine thread or transport library thread ever takes it, so nothing there
can wait on it. The display thread waits only while the main thread is inside controller code,
which never draws and never runs an event loop; it never waits for a paint.

**Model values as a snapshot.** The turn and the take-in read the radio's state now: the station
calibration of a stream (`rxMeterOffsetDbForStream`, and the keyed one for a duplex pan), each
endpoint's wideband context and newest wideband frame, the slice values the display extras use,
whether the PureSignal display goes to this device, whether display pacing applies, and whether
raw I/Q fits the share. RadioModel and StationServer are main-thread objects, so main copies these
onto each endpoint and controller under the lock: on every 5 ms turn of the controller's own
timer, and each time main leaves controller code. The display thread reads only the copies. A
change made by the GUI reaches the frames within 5 ms; a change made by controller code reaches
them before the display thread runs again.

**Main-only work goes back as queued calls.** Sending a spectrum context or a noise-floor message
on the control link, configuring an endpoint for a new source generation or a changed wideband
context, and ending a raw I/Q stream (its context, the budget split) all need main-thread objects.
From the display thread they are queued to the controller's thread; on the main thread they run at
once, as now. An endpoint waiting for a configure takes one queued call, not one per frame.

**A display send lane, modeled on the audio one.** `IMediaTransport` gains `displaySink()`, a
`MediaDisplaySink` whose `submitDisplay`, `displayBusy`, `submitIq` and `iqBusy` may be called from
any thread, as `audioRtpSink()` does for audio (`IMediaTransport.h`). `LibDataChannelMediaTransport`
answers with a `DisplaySendLane` modeled on `AudioSendLane`
(`LibDataChannelMediaTransport.cpp:691-745`): the owner publishes ready, the display and I/Q
channels and the callback bridge under the lane's mutex whenever they change; a send copies them
out and calls the library. libdatachannel v0.24.5 allows that off the owner's thread:
`Channel::bufferedAmount()` reads an atomic (`src/channel.cpp:22`, `src/impl/channel.hpp:43`),
`DataChannel::send` takes the channel's mutex shared and then the SCTP transport's send mutex
(`src/datachannel.cpp:53-55`, `src/impl/datachannel.cpp:181-200`, `src/impl/sctptransport.cpp:374-375`).
A send error is queued as the display or I/Q error event it is today and reported on the owner's
thread. `MediaPeer` hands it on with its own checks, as it does the audio sink.

**Which controllers send from the thread.** A controller sends from the display thread while its
current media peer is ready and that peer's transport gives a display sink. Every Core transport
(libdatachannel) does. A transport without one, as the test transports are, keeps sending from the
controller's own thread through `MediaPeer`, exactly as now, so the existing display tests run the
same code they run today. A controller moves on and off the thread as its peer becomes ready, is
replaced, is retired or the session ends (where `syncAudioRoutes` runs now).

**Lock order.** Hub lock, then the display lane's mutex, then the library's own locks; then a
source's frame mutex. Main never takes the hub lock while holding any of those. The display
thread's wake mutex is never held while taking the hub lock. Stopping the thread never holds the
hub lock.

## Not in this branch

Changing Qt's frame latency, moving drawing off the main thread, pausing drawing while the session
is locked, the transmit watchdog, and any visual or DSP change. Nothing here changes what is sent,
in what order or at what rate.

## Test

- New `tst_daemon_display_send_thread`: a controller whose transport gives a recording display
  sink. A feeder thread produces frames as the FFT engine thread does, while the test holds the main
  thread for 500 ms. Pass: the longest gap between two sends during the hold is under 50 ms, and no
  send ran on the main thread. Written first; it fails on main (no sends during the hold, every
  send on main), then passes.
- `DisplaySendLane`: sends from a second thread while the owner publishes and stops.
- The existing display tests unchanged: `tst_daemon_media_controller`, `tst_remote_fft_production`,
  `tst_daemon_display_keep`, `tst_daemon_display_retune`, `tst_remote_display_hold`,
  `tst_display_duplex`, `tst_remote_tx_display`, `tst_ps3_session`, `tst_media_replace`,
  `tst_station_multi_session`, `tst_media_peer`, `tst_media_transport`.
- ThreadSanitizer: the new tests and the display neighbors in a separate `-fsanitize=thread` build,
  removed afterwards.
- STRIX, scratch build in my own folder, JJ's installs untouched: the hosted window with Windows
  locked, sender gaps and frames replaced from the display log line before and after. nereusd must
  stay at 6.7 to 9.8 ms. `QT_D3D_MAX_FRAME_LATENCY=0` only as a diagnostic.

Estimate: about 1,000 to 1,400 lines with tests.
