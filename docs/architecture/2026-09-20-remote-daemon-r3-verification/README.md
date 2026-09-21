# R3 verification ledger

This ledger separates the recovered integration baseline, component evidence
and real remote receive acceptance. A component pass does not close R3.

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
