# Selected-radio recovery, R-R3-27 and R-R3-29

The [recovery design](../2026-09-22-radio-recovery-design.md) separates radio
transport loss from GUI/Core media loss and initial connection timeout. Core
can start before its selected radio appears and can retire an established P2
connection after all accepted inbound UDP stops. DaemonApp alone owns fresh
discovery and reconnect, pinned to the configured MAC or the first MAC chosen
for this daemon run. Retry delays are 1, 2, 4, 8, then at most 15 seconds;
the existing post-stop discovery quiet period still applies.

## Behavior and boundaries

P2 uses the three-second all-inbound deadline from Thetis v2.10.3.15
`ChannelMaster/network.c:655–666`. Valid status traffic alone keeps it alive;
this does not detect missing I/Q while status continues. Accepted traffic must
come from the selected address and negotiated role and pass the existing
packet-size check. Disabled wideband ADC streams do not count. IPv4-mapped
IPv6 addresses identify the same IPv4 peer; unrelated IPv6 addresses do not.

Loss stops producers, clears transmit intent, sends run=0/MOX=0, closes
ingress, then reports one NoDataTimeout and LinkLost. There is no low-level
P2 retry. Core retires DSP/audio/FFT routing and rebuilds around the same
slice objects, preserving frequency, mode, pan membership and active selection.
Queued codec and wideband callbacks cannot affect the replacement connection.

Discovery runs on a cancellable worker with fresh results. The control plane
stays available, and an already authenticated GUI receives updated radio
capabilities and MAC-scoped settings without a second handshake or replay of
slice/pan creation. Explicit disconnect and stop cancel recovery. A stop
during non-interruptible WDSP wisdom generation cancels the eventual dial and
defers destruction until setup unwinds; a second start cannot overlap it.

## Regression and review evidence

The preserving-reconnect regression first reproduced active selection being
reset from B to A. Real P1 loopback and WDSP initialization now verify object
and value preservation, including a wrong-MAC rejection. Daemon tests cover
late arrival, a wrong radio responding first, busy responses, quiet-period
deferral, worker interruption, stale completion and stop during setup.

Real P2 UDP tests cover first I/Q versus established loss, continued command
egress before loss, one unkeyed terminal stop, status-only and enabled-wideband
keepalive, malformed/wrong-source/disabled-wideband rejection, explicit stop,
endpoint replacement and reentrant error observers. A diagnostic run exposed
macOS's mapped sender address `::ffff:127.0.0.1`; the corrected acceptance path
passes the real socket test while rejecting a distinct `::1` peer.

The integrated daemon test connects through real P2 UDP, stops ingress, waits
for connection and WDSP retirement, then rediscovers the same radio and checks
both receivers. It also rejects old wideband work queued before FFT and old
publication already queued after FFT, while accepting a fresh frame. Session
tests authenticate real client/server peers and verify late capabilities,
scoped settings and replacement-session isolation. Audio/display tests verify
LinkLost retirement and fresh-generation resubscription.

One consolidated independent review covered the integrated change. Its codec
lifetime, wideband retirement and disabled-stream acceptance findings are
corrected. Final focused P2 and daemon executables passed **2/2 in 11.45 s**;
the other six affected executables also passed. Private evidence logs use
`r3-recovery-final-focused-{build,test}.log` and
`r3-recovery-corrected-test.log`.

A fresh `all_tests` and `nereusd` build followed by unfiltered ctest passed
**698/698 executables in 146.59 s**. The eleven existing inner Qt skips concern
host audio APIs or deferred harness coverage; none of the new recovery cases
was skipped. Logs: `r3-recovery-all-{build,test,load,skips}.log`. The recorded
pre-suite load averages were 15.86 / 10.16 / 7.57.

Native installation and real-radio acceptance are pending. This checkpoint
does not close the two-hour listening soak, IQ-only health policy,
Core-restart receiver persistence or the separate P2 Setup Network WDT parity
audit.
