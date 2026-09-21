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
