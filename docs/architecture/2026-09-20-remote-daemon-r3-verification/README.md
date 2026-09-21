# R3 verification ledger

This ledger separates the recovered integration baseline, component evidence
and real remote receive acceptance. A component pass does not close R3.

## Current receive status, September 21

Core is installed at signed receive checkpoint `04da2aab`; the running GUI
remains `73fcccfe` while the combined audio candidate is verified. Core now
publishes BPF/WIDE state and independent Auto AGC-T measurements. Their GUI
bindings pass source/session checks; live visual acceptance awaits the new
GUI. Existing live spectrum, waterfall, Clarity inputs, applet/slice meters
and menu-driven Core reconnect have been demonstrated at earlier checkpoints.

The mixed stereo Opus sender, encrypted session control, bounded jitter,
continuous WDSP rate matching and client playback are implemented. Focused
checks pass, including a simulated hour in each direction at 500 ppm, large
speaker callbacks, packet bursts, mute/resume and encrypted reconnect. The full
681-test suite passes at signed audio checkpoint `0123d8e1`. Native audio
installation and actual listening are the next gates.
The Mac is now remote/on VPN; that existing connection is not R5 traversal.
Capacity, two-hour hardware audio soak, Clarity's subjective comparison and
R5 internet traversal remain open. Earlier sections retain checkpoint-specific
evidence and should not be read as the current installation state.

## Combined baseline, 14124e7c

The signed integration checkpoint includes the published open-PR work selected
on September 20, including the 3D waterfall. The macOS RelWithDebInfo build
has GPU spectrum enabled and DFNR disabled.

`cmake --build build-integration --target all_tests -j10` succeeded, followed
by unfiltered `ctest --test-dir build-integration -j10 --no-tests=error
--output-on-failure`: **662 passed, zero failed, zero skipped**, 41.71 seconds.

The first full run had five failures. Two source corrections are in the signed
checkpoint: nine TX-analyzer settings now have Station scope, and a programmatic
slider-signal test no longer waits unnecessarily for window exposure. Three
fixture consumers needed two local build-only symlinks (`cty.dat` and
`tests/fixtures/adif/sample.adi`). Those links are an environment workaround,
not a repository fix. Detailed transcripts remain in the maintainer's private
work directory, `~/.config/nereus/work/combined-tests-ed246176/`.

## Rock 5C installation, 14124e7c

Native aarch64 Release build and staged daemon-component installation passed
on the 2 GB Rock 5C, Armbian Trixie, Qt 6.8.2 and GCC 14. GPU rendering, tests
and DFNR were disabled for the headless build. The staged binary resolves
NereusCore and RADE beside its installation; no missing dependency was reported.
The binary RUNPATH is `$ORIGIN/../lib`, and the Core library uses `$ORIGIN`.

The checkpoint is now installed. The previous `daf05135` binary, RADE library,
systemd unit and configuration were preserved under the root-only directory
`/var/lib/nereus-build/rollback-daf05135-before-14124e7c/`. Station identity,
private pairing data and the daemon profile were preserved. The existing
service stopped cleanly, and the installed service is enabled and active with
`Result=success`, `ExecMainStatus=0`, and `NRestarts=0`.

A fresh board-local authenticated TLS probe confirmed:

- Pinned Saturn identity: MAC `2C:CF:67:AB:FC:F4`, board 10, firmware 27.
- Connected radio, one slice, 2,502 settings, `txPermitted=false`.
- Receive tune from 14.225 to 14.226 MHz, then verified restoration.
- 161 meter samples, 141 distinct values, over roughly 15 seconds.

Installed SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `/usr/local/bin/nereusd` | `705f011ebfd50ec46c6c69f690c7625ce91b33484e60e17198a8de399671c36d` |
| `/usr/local/lib/libNereusCore.so` | `22a9e65e93bf46db186c4fbc39bbf83d00741d0a704d684ce23754bf6467129c` |
| `/usr/local/lib/librade.so.0.1` | `18e56fe8ee4b8a8450cc786cbcfed9bab147ec11df64e33486ce50e0ff91c32f` |

This installation still has R2 control behavior. It does not carry R3 audio
or spectrum. Reboot/reconnect passed earlier at `daf05135`; a new reboot at
`14124e7c` has not been performed. All live tests were receive-only.

## R3 component evidence

- [Pinned transport build and two-peer probe](transport-probe.md): encrypted
  display and RTP exchange and stop/recreate passed in the standalone harness.
  Production Qt transport and MediaPeer tests now pass, including callback
  retirement during stop/restart/deletion. Real on-wire MTU verification remains
  pending. The selected libjuice backend lacks TURN/TCP and TURN/TLS; see the
  [plan's network continuity section](../2026-09-20-remote-daemon-r3-plan.md#network-decisions-carried-forward).
- [Opus source/packet probe](opus-profile-probe.txt) and
  [fixture source](opus-profile-probe.c): actual wideband, stereo channel count
  and payload rate checked. This is not listening or Rock 5C CPU evidence.
- Display codec: integrated target passes, including finite-extreme arithmetic,
  reconstructed-history error bounds, lost-delta recovery, malformed input and
  stale/wrapped sequence regressions.
- Headless FFT source and pool: focused integrated tests pass. Retune discards
  pending input and overlap, and the daemon uses bounded ingress/latest output.
- Session media gates: `tst_station_session` and `tst_session_link_loss` pass
  after fixing replacement-session retirement while preserving silent redial.
- Desktop and daemon display integration: production targets build, and the
  focused transport, peer, source, endpoint, session, local/remote rendering
  and GUI-controller checks pass. The two-pane regression uses actual
  authenticated control, the daemon controller and tagged I/Q production
  with a deterministic test carrier. Both panes receive decoded frames after
  a shared window change and a radio disconnect/reconnect. The daemon tests
  also cover subscription revision, epoch, no-overlap and slice retirement.
- Real Saturn media, GPU display and on-wire packet-size evidence remain
  pending installation of this checkpoint.
- Mixed-audio tap: `tst_daemon_audio_source` passes 8 cases using the real
  master-mixer path, independent left/right values, per-slice mute, local
  master-mute isolation, bounded overflow with honest sample positions,
  restart and synchronous callback retirement. Network sender, jitter/playback
  and adaptive clock correction remain pending.
  The existing WDSP RMATCH probe preserved stereo and bounded occupancy for
  120 simulated seconds at fixed compensation ratios for +/-500 ppm; this is
  not an adaptive-controller or long-session acceptance result.

## Operator acceptance still pending

Live remote spectrum and 2D/3D waterfall, real stereo listening, four-pan and
two-tier capacity, independent-clock stability, measured total bandwidth,
two-hour hardware audio soak, and fresh installation/reconnect with media.
Internet carrier and CGNAT-to-CGNAT evidence remain separate network gates.

## Consolidated display-checkpoint review

One independent review identified five corrections: shared FFT-window changes
needed batch retirement; endpoint pacing needed an advancing schedule;
no-overlap subscriptions needed explicit rejection; dropped I/Q needed input
history reset; and SDP-embedded candidates bypassed the host-only admission
path. These are corrected in the current source and covered by focused
regressions. Integrated and hardware results will be recorded against the
signed checkpoint after the combined checks finish.

The two-pane integration also exposed an R2 state-application gap: the
read-only mirrored `RadioModel.connected` delta had no client writer. The
client now applies it through the existing remote-only connection-state
setter, allowing media subscriptions to retire and resume when the radio
changes state while the station control connection stays up.

## Combined R3 display-checkpoint software gate

The final combined macOS build succeeded, followed by unfiltered
`ctest --test-dir build-integration -j10 --no-tests=error --output-on-failure`:
**672 passed, zero failed, zero skipped**, 58.96 seconds. The focused source
and daemon-controller gate passed before the full run. Logs are retained in
`~/.config/nereus/work/r3-transport-integration/final-suite-302df0e7/`.

Native aarch64 Core builds with the same reviewed source also passed on the
Rock 5C. The maintainer verified SHA-256 agreement for all 79 files in the
source overlay. Signed installation and live media evidence follow separately;
this software gate does not establish remote audio playback or R5 traversal.

## Installed display checkpoint, ea55d24a

Signed commit `ea55d24a088b6b611e1acc5d4cbc5341cdd0f459` is installed on the
Rock 5C. Native Release build, daemon/license staged installation, clean stop
and fresh authenticated receive-control verification passed. The service is
active with zero restarts. Previous `14124e7c` binaries, libraries, unit and
private configuration are recoverable from
`/var/lib/nereus-build/rollback-14124e7c-before-ea55d24a/`.

A real 20-second Saturn media run received **298 distinct decoded frames**,
298 waterfall advances and 298 wide rows, with zero decoder rejections or
keyframe-gap requests. The requested 15-fps view had a 48,046.875-Hz accepted
span, 1,024 trace samples, 1,024 waterfall samples and 768 wide samples.
[Machine-readable results](live-display-ea55d24a.json) contain no pairing data.

The packet capture shows the media using `end1` (Ethernet), with maximum
IPv4 packet size **969 bytes**. The measured DTLS flow was approximately
324.86 kbit/s Core to client and 23.31 kbit/s in return, including handshake
and SCTP acknowledgments. This is one 15-fps display profile, excluding WSS
control, radio I/Q and audio; it does not establish the total session budget
or SRTP packet-size acceptance. During the probe the service used about
102 MiB and the CPU sensor read 48.1 C.

The signed desktop GUI is running with private profile `radxa_5c_r3`.
Changing spectrum and 2D waterfall signal history were observed, including
operator band changes from 14.225 MHz USB to 3.650 and 3.830 MHz LSB. The
3D payload is arriving, and renderer tests pass; operator confirmation of
the live 3D view is pending. No RF transmission was requested by this work.

Installed SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `nereusd` | `c9f6fea40007f2e7c4538a8eaa00903224c04330697e77220bdbb2b0b52ff312` |
| `libNereusCore.so` | `fc03f826e82e102f9224d7a2b053e36e4d111bc37da9fcac4e46689660836aa7` |
| `librade.so.0.1` | `18e56fe8ee4b8a8450cc786cbcfed9bab147ec11df64e33486ce50e0ff91c32f` |

Remaining R3 work includes the remote Opus sender/playback and clock-control
path, gesture-margin refinement, session budgeting, multi-pan capacity and
long hardware audio verification. R5 traversal requirements remain intact.

## Receive-path investigation, September 21

At installed checkpoint `ea55d24a`, the operator reported little/no signal.
A passive capture during operator-driven 80m/20m band changes confirmed the
Saturn filter commands follow the band. The active DDC3 frequency followed
the VFO, both receive-preselector words selected the expected band, both
step-attenuator bytes were zero, and every captured command had PTT off.
Stable ANT1 selections were Alex0 `0x01400020` on 80m and `0x01100002` on
20m. The last captured antenna change selected EXT2 (`0x01100c02` at 20m).
The physical antenna/socket path remains unconfirmed; a correct command
does not prove the physical relay or antenna feed.

A separate three-second active-DDC capture contained 2,324 packets and
553,112 complex samples, with no sequence gaps or all-zero stream. The
captured signal was predominantly noise: complex RMS -88.11 dBFS; a
4096-point Hann analysis gave a median -122.06 dBFS/bin and peak -120.17
dBFS/bin. These are raw diagnostic units, not calibrated antenna dBm.

Source tracing identified two Core/UI omissions: DaemonApp did not create
and restore the RF-gain controller, and the remote display boundary did not
apply the station's `rxMeterOffsetDb()` calibration. Corrections and their
verification are tracked under R-R3-11. This finding does not establish
that the external RF path is working, nor does it close remote audio.

The operator then reported a power issue and rebooted the Rock. Core started
automatically and reconnected to the Saturn with no service restart failures.
A subsequent raw-DDC2 capture contained 498 packets with no sequence gaps;
complex RMS was -48.77 dBFS, median -84.64 dBFS/bin and strongest peak -66.73
dBFS/bin. Clear RF peaks and populated waterfall history were then observed
in the remote GUI around 3.869 MHz, still running `ea55d24a`, before either
software correction was installed. The captures differ in operating state;
this establishes recovery, not an isolated causal test of the power supply.

The operator relaunched the default local profile, which was disconnected.
At their request, the development GUI was reopened with `radxa_5c_r3`; its
profile title, established TCP connection to Core's port 50055 and fresh
authenticated station handshake were verified. The large analog meter still
showed -127 dBm while the mirrored slice flag showed about -65 dBm. That is
a separate remote meter-binding gap; it is not evidence of missing RF.

Calibration qualification: the daemon's absent-key defaults enable step ATT
at 0 dB, giving Saturn's existing factory offset of -4.476 dB. The +15.524 dB
offset applies only with step ATT disabled and preamp mode Off. Neither
number should be presented as the proven cause of the original flat RF.

R-R3-11 software gate: the daemon now owns/restores its RF-gain controller,
tracks stable slice identities and bound band/mode, reconnects its hardware
binding, and preserves controller lifetime through teardown. Core calibrates
trace/waterfall/wide rows before reduction and quantization; remote rendering
does not add the client's local calibration. Local direct rendering is
unchanged. Eight focused test executables passed (4.50 seconds), followed
by all 672 registered tests passing with zero failures/skips (65.52 seconds).
The final renderer-fixture edit was confirmed present in that build. Logs
are in `~/.config/nereus/work/r3-rx-calibration/`. Native installation and
live corrected-checkpoint evidence follow separately.

## Installed RF-gain and calibration checkpoint, f8531cd9

Signed commit `f8531cd92676209f0acaf96b475e0d6c7a51e1b3` is installed on
the Rock 5C. The ten-file source overlay was hash-verified, and the native
aarch64 Release build passed. The first installation attempt detected a
missing staged license component and rolled back successfully. After staging
both the daemon and licenses and adding the corresponding preflight check,
the second attempt succeeded. Prior `ea55d24a` binaries, libraries, service
unit and private configuration are retained in
`/var/lib/nereus-build/rollback-ea55d24a-before-f8531cd9-attempt2/`.
The service is active with `Result=success`, `ExecMainStatus=0` and zero
restarts. This verifies a service restart; the operator's earlier board
power cycle ran the previous checkpoint.

A fresh authenticated 20-second media probe received **230 distinct decoded
frames**, 230 waterfall advances and 230 wide rows, with zero decoder
rejections or keyframe-gap requests. Trace and waterfall each contained
1,024 samples; wide rows contained 768 samples. The accepted span was
48,046.875 Hz. This bounded run includes startup and is not sustained
frame-rate or long-duration acceptance.
[Machine-readable results](live-display-f8531cd9.json) contain no pairing data.

The rebuilt desktop bundle passed strict code-signature verification. It
was reopened with profile `radxa_5c_r3`; the title showed `f8531cd9`, a fresh
authenticated handshake completed, and its established TCP connection was
verified to the Rock's wired address `192.168.109.106:50055`. Live spectrum
and populated, advancing 2D waterfall were observed at the operator's
3.650-MHz LSB selection. The large analog meter and connection/status
indicators still need remote bindings; the mirrored slice meter is the
currently useful RF indication. No RF transmission was performed.

Installed SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `nereusd` | `8e0b6483c27ac79c757b41e02bdcf78eee31e8b16a07f01f066c2fc6c0faa599` |
| `libNereusCore.so` | `c0ca4f294174afb83efa14af11fe1501e9e5dd67719382c8fbf3d7a39f592e4b` |
| `librade.so.0.1` | `18e56fe8ee4b8a8450cc786cbcfed9bab147ec11df64e33486ce50e0ff91c32f` |

Remote receive audio remains unfinished: capture and Opus codec components
exist, but network audio sending, jitter/clock control and GUI playback are
not connected. Silence in this remote profile is therefore expected at this
checkpoint. The agreed next receive milestone is one mixed stereo Opus feed
preserving slice volume, mute and stereo placement. R5 traversal decisions
remain intact; neither this installation nor its display probe completes R3.

## Clarity parity investigation, September 21

The operator reported a grainier waterfall when using both automatic Clarity
and the Clarity Blue palette. Tracing confirmed that remote subscriptions
already preserve the local per-plane detector, averaging mode and alpha.
The missing boundary was Clarity's input: MainWindow connected it only to
the GUI's local `FFTEngine::fftReady`, which does not produce radio frames
in the remote role. Clarity could therefore remain enabled without updating
its thresholds, while its active flag suppressed the legacy waterfall AGC.

R-R3-12 restores the full-source percentile as bounded `noise-floor` control
metadata from Core. It is measured before display crop/reduction/quantization
and receives the station calibration once. The client routes only current,
accepted, visible active-pan measurements to the same Clarity cadence, EWMA,
deadband and operator gates used locally. The palette, detector settings and
binary display codec are unchanged. The codec's existing 180/255 dB quantum
is approximately 0.706 dB; this investigation does not attribute the reported
texture to that quantization.

The native aarch64 candidate build passed, with all four changed Core source
files verified against the immutable overlay manifest. The five relevant
test executables pass. The nonzero source regression compares Core with an
independent local FFTEngine and NoiseFloorEstimator using broadband input
plus an out-of-crop carrier, at 1,024/2,048 FFT sizes and two calibration
settings; the floor agrees within 0.01 dB. Other checks cover the zero-power
floor, context ordering, cadence, retirement, client rejection of stale or
malformed metadata, local/remote Clarity smoothing and operator gates, and
preservation of saved thresholds.

The rebuilt unfiltered desktop suite passed **672/672**, zero failed/skipped,
in 63.07 seconds. Logs are retained in
`~/.config/nereus/work/r3-clarity/`. Signed installation and live visual
evidence follow separately; this software gate does not establish remote
audio playback or exact subjective waterfall parity.

## Installed Clarity checkpoint, 3c4b15e6

Signed commit `3c4b15e67cbef1b1991bcf3d36a2b273d818272c` is installed on
the Rock 5C and in the reopened desktop bundle. The source overlay was
verified against signed Git blobs and SHA-256 hashes before the native
aarch64 Release build. Both daemon and license components were staged.
The service is active with `Result=success`, `ExecMainStatus=0` and zero
restarts. The prior installation is retained in
`/var/lib/nereus-build/rollback-f8531cd9-before-3c4b15e6/`. This verifies
a service restart, not a board reboot with this checkpoint.

The Mac bundle passed strict code-signature verification. Its title shows
`3c4b15e6` with profile `radxa_5c_r3`, and its actual TCP connection goes to
the Rock's wired address `192.168.109.106:50055`. The fresh session logged
an authenticated handshake, a Core noise-floor measurement of
**-88.9303 dBm**, and its first encrypted remote spectrum frame. Live RF
traces and slice meters were observed at the operator's 3.897600-MHz LSB
selection. No RF transmission was performed.

Installed SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `nereusd` | `8e0b6483c27ac79c757b41e02bdcf78eee31e8b16a07f01f066c2fc6c0faa599` |
| `libNereusCore.so` | `8ab49b1a0786423f8471bf2aea27e15e6771133ab9eb4cb5939fcd3b7deddd0c` |
| `librade.so.0.1` | `18e56fe8ee4b8a8450cc786cbcfed9bab147ec11df64e33486ce50e0ff91c32f` |

The first live waterfall was almost black, so visual acceptance remains
open. The saved profile has Clarity enabled, palette `Default` (index 0),
black level 32 and color gain 16. With the existing renderer formula,
black level 32 adds `(125 - 32) * 0.4 = 37.2 dB` to Clarity's low threshold.
For the first observed floor, this places the effective black cutoff at
about -56.7 dBm and hides weaker signals. The next check is to remove that
profile offset and select the operator's requested Clarity Blue palette,
then compare the live result. No profile setting has been changed during
this check: the Mac locked before the controls could be inspected. This
is not evidence that subjective grain or local/remote visual parity has
been resolved. The remote audio and R5 traversal milestones above remain
unfinished.

## Applet S-meter correction, September 21

The operator confirmed that the large applet meter remained unresponsive.
Its `MeterPoller` still waited for and polled the GUI's local RxChannel,
which is inactive in the remote role. The existing mirrored per-slice value
was insufficient for all applet modes: the headless pump defaults to signal
average, while S-Meter and S-Meter Peak select signal peak.

R-R3-13 adds independent, read-only `signalPeakDbm` and `signalAverageDbm`
properties to SliceModel. Core's existing meter pump produces both with the
station calibration once; the legacy selected reading is preserved. The
remote poller uses those source values for the applet, custom signal meter
items and each slice flag. Max Bin scans the decoded, calibrated display
within each slice's passband, preserving the existing measurement-pixel
path before visual notch rendering. The remote timer starts independently
of local WDSP. It clears the visible RX reading on disconnect and waits for
the current session's completed snapshot before showing retained models.
Local direct polling and widget ballistics are unchanged.

Six focused executables passed in 8.99 seconds. They cover actual Core meter
reads, source selection independent of the legacy selector, calibrated
read-only mirror round trips without outbound telemetry writes, active
slice IDs, applet/flag agreement, disconnect/TX/model destruction, and
decoded-passband Max Bin with context invalidation. Consolidated review
identified the pre-snapshot reconnect gap; a readiness callback and a
regression covering that interval were added before the final full suite.
The rebuilt unfiltered full suite passed **673/673**, zero failed/skipped,
in 53.34 seconds, including the readiness regression. The native aarch64
candidate built successfully after its six changed Core/model source files
were hash-verified. Logs are retained in
`~/.config/nereus/work/r3-applet-meter/`. Signed installation and live applet
observations follow separately; the locked Mac currently blocks the latter.

The adjacent binding audit identified two separate open requirements now in
the plan: R-R3-14 for Core's effective CH/BPF/WIDE state, and R-R3-15 for
station Auto AGC-T measurements. These still read local controller/tracker
state in the remote GUI. The existing RadioModel connection-state mirror
already has a client apply path; no blanket claim that all connection status
is broken is made here. The meter correction does not close these other
requirements or the mixed-stereo Opus milestone.

## Installed meter checkpoint, 74145a4b

Signed commit `74145a4bcfc2504547e563d230aa54b5ad7f452e` is installed on
the Rock 5C. All 18 files exported from the prior deployed checkpoint were
verified against signed Git blobs and SHA-256 hashes, then verified again
on the board. The native Release build passed, daemon and license components
were staged, and installation succeeded with the prior configuration and
binaries retained in
`/var/lib/nereus-build/rollback-3c4b15e6-before-74145a4b/`.
The service is active, with `Result=success`, `ExecMainStatus=0` and zero
restarts. Only a service restart was tested, not a board reboot.

The Mac GUI build identifies itself as `codex/integrate-r2-main@74145a4b`
and passes strict code-signature verification. It has **not** been reopened:
the Mac remained locked at the final UI check. The running old GUI is not
evidence of the new meter behavior. Visible applet/flag movement and mode
switching on the real Saturn feed remain pending, along with the earlier
Clarity profile adjustment. No RF transmission was performed.

Installed SHA-256 values:

| File | SHA-256 |
| --- | --- |
| `nereusd` | `8e0b6483c27ac79c757b41e02bdcf78eee31e8b16a07f01f066c2fc6c0faa599` |
| `libNereusCore.so` | `f78ae36d2ae7cc3ce7cf19c6f179ee326bae814186d24bd23dedf172a4b3e8c8` |
| `librade.so.0.1` | `18e56fe8ee4b8a8450cc786cbcfed9bab147ec11df64e33486ce50e0ff91c32f` |

## Live meter observation and overnight reconnect

After the Mac was unlocked, the old `3c4b15e6` GUI was still running but
disconnected. Its log showed an overnight closed connection and six failed
redial attempts; this does not establish the underlying network cause.
Core remained active with zero service restarts. Closing the GUI and
relaunching profile `radxa_5c_r3` opened the matching `74145a4b` build and
completed a fresh authenticated handshake. The actual TCP connection was
verified to `192.168.109.106:50055`, and the client logged fresh Core
noise-floor and encrypted spectrum input.

The applet initially showed about -67 dBm with a responsive needle. A later
observation, after the operator selected the stronger signal at 3.915100 MHz,
showed -39 dBm on both the applet and active slice flag, with advancing
spectrum and waterfall. The agent did not retune or transmit. This closes
the initial real-signal needle check; all-mode and longer reconnect testing
remain distinct acceptance work.

The current manual recovery is to quit and relaunch the saved Core profile.
The normal Radio/Connect action still targets the local radio-discovery
path, so it is not yet a remote-station reconnect control. A private desktop
launcher named `Nereus Core - Rock 5C.command` now launches the correct
build/profile and rejects a duplicate instance. R-R3-16 records the missing
direct reconnect control rather than presenting relaunch as the finished UX.


## Manual Core session controls, September 21

R-R3-16 now routes Radio/Connect and Radio/Disconnect, their existing
keyboard shortcuts, and the connection context menus to the configured
Core session. The window creates one StationClient and media controller
and reuses them across manual connections. Enablement follows session
activity, including an incomplete handshake or pending retry, independently
of whether Core's radio is connected. Disconnect cancels pending retries;
Connect starts a fresh sequence without local radio discovery.

The retry schedule saturates at 60 seconds and continues retrying. The six
failed redials observed above were a log excerpt, not a six-attempt limit.
No retry-policy change is included here.

Focused session/link-loss, remote GUI gating and media-controller tests
passed (3/3, 13.46 seconds). New session regressions cover incomplete
handshake activity, authenticated Core with an offline radio, repeated
manual sessions on the same client, cancellation during backoff and a fresh
retry sequence afterward. The matching `all_tests` and GUI targets were
built, followed by an unfiltered **673/673 pass, zero failed or skipped,
60.94 seconds**. Strict app code-signature verification passed.

The first full run and isolated retry hit the same native popup-exposure
wait in the existing 3D span-control test, before its state assertions ran.
That state-only test no longer opens a native popup; all enablement assertions
remain, and the separate real SpectrumWidget interaction test remains intact.
The final full run above includes this test correction.

Live GUI acceptance is pending at this source checkpoint. Core remains at
`74145a4b`; this client session-control change requires no wire-schema or
server behavior change. Remote audio is still incomplete. The source audit
identified WDSP rmatch/varsamp as a continuous-rate candidate, but the bus
currently lacks public device-consumption/queue-depth feedback. Add that
feedback before validating the clock loop; a timer alone cannot measure the
Mac sound device's clock. Task 5 records the sender, receiver/playback
and output-control implementation sequence. The existing order, remaining
task 4a receive bindings followed by task 5 audio, is retained.


## Live manual reconnect, 73fcccfe

The signed GUI was built and strictly code-signature-verified, then opened
with the existing `radxa_5c_r3` profile (PID 19939). Core remained running at
`74145a4b`, active with `ExecMainStatus=0` and `NRestarts=0`.

In the actual Radio menu, Connect was disabled and Disconnect enabled while
the session was active. Selecting Disconnect removed the TCP connection
without closing the GUI, enabled Connect, and disabled Disconnect. Selecting
Connect on the same process authenticated again at 09:10:47 local time and
received fresh Core noise-floor data. The established TCP endpoint was
`192.168.109.104:62313 -> 192.168.109.106:50055`.

The visible GUI then showed live spectrum and populated waterfall, with the
applet and both slice flags at -57 dBm on the operator's 3.952200 MHz tuning.
No frequency, gain, filter, mute or transmit control was changed for this
check. This closes the initial live R-R3-16 menu round trip; multi-hour link
recovery and audio reconnect acceptance remain separate work. The GUI is
left running and connected. Audio playback remains task 5, following the
remaining task 4a receive bindings.


## Core filter and Auto AGC-T bindings, September 21

R-R3-14 publishes each Core filter chain's mode, effective state, band and
reason as outbound-only RadioModel telemetry. Remote CH and WIDE indicators
use those values and the already-mirrored slice chain assignment. Presentation
waits for the complete station snapshot, including slice reconciliation, and
is unavailable after session retirement. The Filter Policy dialog shows the
reported Core state read-only; remote policy editing remains unavailable.
An offline/awaiting dialog explicitly withholds the retained prior reading.

R-R3-15 now has a daemon-lifetime, bounded FFT source and the existing
NoiseFloorTracker for each active stream. Previously, only MainWindow created
these trackers and Core's existing Auto AGC-T timer skipped all enabled slices.
Core now feeds the same FFT-to-tracker path used locally, independently of
client display subscriptions. It does not substitute the Clarity percentile
floor or introduce new AGC threshold math. Retune, mode, routing, attenuation,
MOX, stream suspension and disconnection invalidate the measurement until
fresh tracker convergence. Unbound slices become invalid immediately, and
source teardown unregisters every borrowed tracker before destroying it.

Three read-only per-slice properties carry the floor, validity and generation.
The GUI flags remain per-slice; the RX applet follows the active slice and
shows an awaiting state when no valid station reading exists. Its AUTO toggle
resolves the active slice when clicked. Local direct mode keeps its own
per-stream source.

Focused tests passed 7/7 (22.14 seconds), followed by a rebuilt full suite
675/675 (63.45 seconds). Consolidated review found the unavailable-dialog case
and two coverage gaps. The correction adds snapshot-completion gating, an
unavailable dialog state, and a test of the production two-slice GUI binding
with active switching, telemetry deltas, local-floor contamination and
connection loss. All six affected tests then passed (21.83 seconds).
The post-review full run exposed a pre-existing test timing race: polling for
exactly one scheduled retry could miss the compressed 50 ms retry under load.
The test now starts the listener synchronously on the first scheduling signal
and still verifies automatic, unaided reconnect. After rebuilding that test,
the complete suite passed 675/675 with no skips in 61.27 seconds.

No new binary is installed at this source checkpoint. Core remains
74145a4b and the GUI remains 73fcccfe. Live filter/Auto AGC-T observation on
the Saturn/Rock/Mac path remains pending. These changes do not close R3's
audio, capacity or long-run acceptance gates. Task 5 audio integration is
continuing after the receive-binding implementation.


## Receive deployment and audio integration progress, September 21

Signed receive checkpoint `04da2aab` was built natively and installed on
ROCK 5C at 10:20 EDT. Both `nereusd` and NereusCore were staged with licences.
The rollback copy is `rollback-74145a4b-before-04da2aab`. Service checks
reported active/running, zero restarts and exit status zero; the existing
GUI re-established its session automatically. The previous GUI binary does
not yet display the new filter and Auto AGC properties, so visual acceptance
awaits the combined GUI update.

The first combined audio build completed. Six of eight focused tests passed:
Opus codec, actual mixer-to-sender, audio session controls, jitter ordering,
PortAudio device pacing/flush, and existing GUI display lifecycle. Two failures
were investigated before deployment:

- The 1-hour drift gate found 20,202 underflows for the first tested clock
  direction. WDSP's feedback windows count calls; direct 1,920-frame input and
  480-frame output exposed only 125 calls/second. A separate probe using the
  source's native 64-frame blocks and unchanged WDSP defaults had zero
  under/overflows in both drift directions for 300 simulated seconds. The
  bounded 64-frame adapter subsequently passed the full-hour gate in both
  directions with zero rate-matcher underflows or overflows (104.83 seconds
  wall time for both simulations).
- A less-than-0.1 channel-correlation assertion was incompatible with the
  approved lossy 24 kbit/s Opus profile. A direct pinned-codec probe measured
  correlation 0.1042 and 23.5/27.5 dB tone isolation for 997/1703 Hz L/R inputs,
  reproducing the playback result. The receiver test now requires at least
  18 dB intended-channel isolation and nonzero channel energy. That check
  passed. The same direct probe at 48 kbit/s measured about 64-71 dB isolation;
  actual listening comparison is still pending, and the default is unchanged.

Consolidated audio review identified two integration defects: a fixed 960-frame
speaker target could not cover supported 1,024/2,048-frame callbacks, and a
three-packet arrival burst could overflow the rate matcher. Corrections now
report the actual/configured callback quantum and cover it when replenishing;
jitter release waits for room in the rate matcher. Explicit diagnostics reject
silent under/overflow repair. Large-callback and 3/8-packet burst tests pass (5.31 seconds). Larger
speaker queues retain the same initial rate-matcher reserve; the 480, 1,024
and 2,048-frame callback cases pass without rate-matcher under/overflow.
The real DTLS/SRTP two-controller test also passes (2.48 seconds), including
stereo tones from the actual master mixer, local mute without changing slice
mix settings, fresh resume context and session disconnect/reconnect. Its
isolation checks compare the same tone across the two channels, preserving
the distinct per-slice gains. These are automated fixtures, not physical
speaker listening. Audio has not yet been installed or accepted on hardware.


The first rebuilt 681-test full run found three failures. The clock simulation
exceeded the general 120-second timeout under parallel load; its dedicated
limit is now 180 seconds without reducing either hour of simulated samples.
Two existing real-channel tests crashed after `WDSP:CreateSemaphore: File
exists`. Both crash reports traced to the macOS compatibility layer's
process-local semaphore names colliding between concurrently running test
processes. The narrow correction uses process-unique names and immediately
unlinks successful named semaphores; it changes no DSP calculation. Focused
concurrent regression and the final complete rerun are recorded below when
finished. No audio installation is accepted on the basis of this failed run.


## Combined audio software gate, September 21

The semaphore collision reproduced before the correction; the same concurrent
pair passed afterward. A rebuilt unfiltered full suite then passed **681/681,
zero failed and zero skipped**, in **136.57 seconds**. The continuous clock
test completed in 136.55 seconds under the parallel workload, retaining both
simulated hours and zero-underflow/overflow assertions. The GUI target built
successfully. Detailed build/test logs are in the maintainer's private work
directory as `r3-audio-final-{build,test}.log`.

The earlier repeated real-device stress run passed 18 test invocations before
a later PortAudio reopen hung; the exact concurrent regression and the final
whole suite passed. This does not establish unlimited physical-device
reopen/close stress acceptance. Hardware audio listening, native CPU/bandwidth
measurement and the two-hour live soak remain pending at this source gate.


## Audio checkpoint awaiting installation

Signed audio checkpoint `0123d8e1` passed the repository attribution hooks.
The immutable source overlay and SHA-256 manifest were prepared from Git.
The first transfer ended with a closed connection before the native build
started; subsequent SSH attempts to both board addresses and a ping to the
home router timed out. The existing GUI also lost its control/media session.
The operator had confirmed that the Mac was now remote/on VPN. No network
settings were changed, and no audio binary was installed: Core remains
`04da2aab`, with the old `73fcccfe` GUI still running. Installation resumes
when the LAN route is available, followed by decoded live audio diagnostics
and physical speaker listening. This interruption is not R5 traversal work.

The route subsequently recovered, and the verified `0123d8e1` overlay reached
the board. Native compilation is underway. The optional `nereus-media-probe --audio` diagnostic now builds and advertises its flag. It validates the current
audio context before decoding and reports finite PCM energy, actual stereo
bandwidth/channels, byte counts and discontinuities. A successful diagnostic
is transport/decoder evidence and does not replace physical listening.


## Installed first-audio checkpoint, 0123d8e1

Native aarch64 Release build and daemon/licence staging passed. The 36-file
overlay matched the signed Git SHA-256 manifest. Core `0123d8e1` was installed
at 11:34 EDT with rollback to `04da2aab`; service checks reported active,
`Result=success`, `ExecMainStatus=0`, and `NRestarts=0`. The Mac GUI was rebuilt
at diagnostics checkpoint `8a23f8f7`, passed strict code-signature verification,
and opened with the existing private `radxa_5c_r3` profile. Its production audio
code matches Core `0123d8e1`.

The bounded live diagnostic received 218 distinct display frames and 117
stereo wideband Opus packets (224,640 decoded PCM frames), with finite channel
RMS approximately 0.09138 and no audio sequence/timestamp gaps or rejections.
The first diagnostic attempt ended early on a temporary disabled context;
the probe was corrected to wait for Core's own media-ready transition. The
production GUI already handles this transition. The capture is evidence of
nonzero live decoding, not sustained delivery at the nominal packet rate.

The operator confirmed first sound: audio works for a while, then becomes
bursty/choppy. This does **not** pass continuous playback acceptance. A separate
25-second Core-side capture establishes normal source cadence: 602 SRTP audio
packets over 24.249 seconds, median 39.986 ms between packets and maximum
89.980 ms. Ethernet carries both media paths. Display traffic measured about
460.56 kbit/s in this selected GUI view (maximum IP packet 969 bytes); audio
plus its small UDP control overhead measured about 35.1 kbit/s (maximum IP
packet 175 bytes). This excludes WSS control and radio I/Q and is not the
complete session-budget gate.

The capture exposed the chop mechanism: three RTP timestamp steps of 1,984
frames and one of 2,944 frames, instead of the normal 1,920. A short capture
lock miss abandoned a partial packet and resumed off the negotiated packet
grid. The jitter buffer then rejected following packets until the 500 ms
watchdog reset the context. Three captured timestamp shifts correlate with
GUI restarts approximately 500 ms later. The correction keeps the honest
source clock but resumes packet assembly at the next whole packet boundary,
so the existing loss-concealment path can recover. Verification follows at
the correction's checkpoint; the first-audio build remains installed for now.

Initial live receive binding observations in the new GUI show the active
slice and large applet agreeing at -52 dBm, the Core-provided Auto AGC floor
around -105 dB with AUTO active, and CH 0 selecting 20m. Spectrum and the
Clarity Blue 2D waterfall advance. These observations do not close all band,
meter-mode, reconnect or subjective Clarity acceptance cases.


## Partial-ingress loss correction

The deterministic regression reserves and rejects a 64-frame ingress callback
inside a partial packet, then verifies that the recovered blocks begin at
source frames 1,920 and 3,840 with unchanged stereo samples. Consumer allocation
now occurs outside the capture mutex, reducing the opportunity for ingress
lock misses. The source position still includes dropped frames; the correction
does not conceal loss by renumbering time.

The rebuilt source, sender, daemon session, receiver and real encrypted session
tests all passed (5/5, 4.86 seconds). Full-suite and native installation results
will be recorded after the signed correction is built. The live diagnostic's
temporary-disabled-context handling is included in this correction.


## Installed packet-grid correction, 5c24eda0

The rebuilt unfiltered suite passed **681/681, zero failed/skipped**, in
101.49 seconds; both simulated one-hour drift cases remain intact. Native
ARM build and staged daemon/licence installation passed. Core `5c24eda0`
replaced `0123d8e1` at approximately 12:01:50 EDT, preserving a rollback at
`/var/lib/nereus-build/rollback-0123d8e1-before-5c24eda0`. Service result and
exit status are successful with zero restarts. The existing `8a23f8f7` GUI
automatically reconnected.

A three-minute wired capture contains 4,242 SRTP packets over 179.213 seconds:
4,220 timestamp steps of 1,920 frames and 21 of 3,840, with **no off-grid
steps**, continuous sequence numbers and zero kernel capture drops. Median
spacing is 40.076 ms; the maximum 215.140 ms includes the reconnect phase.
The capture confirms the source correction. Playback is still **not accepted**:
initial watchdog resets continued until about 12:02:15, followed by a
62-second context before a rate-matcher underflow and later arrival-queue
bursts. Client restart diagnostics are being added to distinguish input
arrival stalls from consumer scheduling and clock behavior.

A 10-second one-pan live sample measured Core at 61.9% of one CPU core,
138,916 KiB resident memory and 47.153 degrees C. This is an observation
under the current receiver configuration, not four-pan capacity evidence.

The operator reported smoother audio after this correction, with occasional
stutters accompanied by a waterfall slowdown. Receiver restart diagnostics
were built into signed GUI `a7a6da39`; receiver and encrypted-session tests
passed 2/2. The first cold-start observation shows a three-second speaker
open followed by an arrival backlog, so startup backlog remains an explicit
follow-up rather than evidence of radio silence.

At approximately 12:07:57 the media connection failed, followed by a WSS
timeout. Both Rock addresses became unreachable while the router, Saturn
and MikroTik still answered pings. A read-only MikroTik query at 12:11
reported **no physical link on ether2**, the port identified by the operator.
No switch or VPN settings were changed. Further live acceptance waits for
board/link recovery; this outage must not be counted as an audio pass or
attributed to the receiver without additional evidence.

Transport diagnostics now record library RTP callback spacing and queue age
at actual owner-thread delivery, after display processing. Warnings above
80 ms are bounded to one per second and contain timing/counts only. The
existing 64-packet queue, retirement checks and transport API are unchanged.
The rebuilt transport, media peer, receiver and encrypted audio session tests
passed 4/4 in 5.04 seconds. Full-suite verification follows this source gate.


## Diagnostic GUI checkpoint, 4d923aeb

The complete desktop/test build and strict application code-signature check
passed. An unfiltered 681-test run passed **674 and timed out seven** in
160.95 seconds. The failures are `tst_port_audio_bus`,
`tst_audio_engine_speakers_live_reconfig`, `tst_connectable_radio_model`,
`tst_connected_state_equivalence`, `tst_remote_role_inert`,
`tst_session_verbs`, and `tst_slice_meter_pump`. A live sample of the last
test shows `Pa_OpenStream` blocked in the macOS Core Audio hardware-property
RPC while opening the microphone through `ensureTxInputOpen`. A subsequent
serial failed-test retry remained in the first PortAudio test for 45 seconds
and was stopped as a bounded diagnostic. No tests were excluded, no device
settings or audio services were changed, and this run is **not** reported as
a full pass. The production source correction's earlier 681/681 pass and
the diagnostic changes' focused 4/4 pass remain separate evidence.

Logs are retained privately as `r3-diagnostics-final-{build,test}.log`,
`r3-diagnostics-serial-audio-test.log` and `r3-diag-test-hang-sample.txt`.
The `4d923aeb` GUI was reopened with the existing station profile. It waits
for the Rock's link to return. A second read-only switch check still showed
ether2 not running, with switch-recorded last link-down `2026-09-21 12:06:40`
(the switch clock was not compared against the Mac). Remaining playback,
long-soak and capacity gates stay open.

## Tuning waterfall history regression

The operator reported that both ordinary tuning and C-Tune wiped the
waterfall. The remote client cleared all 2D/3D history when issuing a changed
subscription and again when accepting Core's context. Local tuning already
has its own history policy; renewing a remote codec must not add a full wipe.

The correction separates invalidation of live/pending planes from full
binding retirement. Subscription/context renewal preserves painted history,
while disconnect, replacement and rejection retain their full-clear paths.
Core's bin-aligned accepted geometry uses the existing local waterfall
reprojection; 3D rows retain their recorded RF centre/span. Codec, revision,
generation and view guards still reject obsolete incoming media. No Core
source invalidation, DDC policy or DSP formula is changed.

Before the correction, regressions failed for a fixed-view source retune,
a small tuning move, the request/ACK interval and RF-aligned history. After
the correction, the rebuilt renderer, authenticated media controller, 3D
row tee and 3D ring tests passed **4/4 in 5.24 seconds**. The renderer checks
actual 2D images/timestamps, 3D row RF identity, continued painting and old
generation rejection. Full-suite and installed-GUI evidence follow below.
Live tuning verification remains pending: both known Rock addresses still
failed SSH reachability at this checkpoint.

The rebuilt unfiltered suite passed **674/681**, with the same seven
audio-device tests listed in the diagnostic checkpoint timing out, in
157.89 seconds. This is not a full pass. After tightening the controller
fixture to retain its post-tune Clarity gesture guard, that rebuilt test
also passed. The production code was unchanged by that test-only follow-up.
Private logs: `r3-tune-{red,green,full}-{build,test}.log` and
`r3-tune-final-focused-{build,test}.log`.

The source audit also identified a separate remote C-Tune control gap.
MainWindow's C-Tune pan drag currently calls the client-local
ReceiverManager; a Remote-role client has no wired radio connection, and
its resulting `shiftOffsetHz` change is daemon-authoritative and is not
forwarded. C-Tune pinning is also GUI-local. Ordinary `frequency` writes
do reach Core. This is not evidence of a mislabelled Core FFT source, and
the visual correction does not close C-Tune hardware-control parity.
Follow-up needs an authenticated Core-owned pin/centre operation with
shared-stream, bounds, reconnect and remote-inertness coverage, reusing
the existing local allocator/receiver behavior.
