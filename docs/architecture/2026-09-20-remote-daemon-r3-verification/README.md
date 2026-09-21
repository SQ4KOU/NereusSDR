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
  Production Qt/session integration and real on-wire MTU verification remain
  pending. The selected libjuice backend lacks TURN/TCP and TURN/TLS; see the
  [plan's network continuity section](../2026-09-20-remote-daemon-r3-plan.md#network-decisions-carried-forward).
- [Opus source/packet probe](opus-profile-probe.txt) and
  [fixture source](opus-profile-probe.c): actual wideband, stereo channel count
  and payload rate checked. This is not listening or Rock 5C CPU evidence.
- Display codec: initial standalone harness passed; lead review requested
  finite-extreme arithmetic and lost-delta recovery regressions. Still under
  correction and not yet integrated into the production build.
- Headless FFT source and production transport adapter: in progress.

## Operator acceptance still pending

Live remote spectrum and 2D/3D waterfall, real stereo listening, four-pan and
two-tier capacity, independent-clock stability, measured total bandwidth,
two-hour hardware audio soak, and fresh installation/reconnect with media.
Internet carrier and CGNAT-to-CGNAT evidence remain separate network gates.
