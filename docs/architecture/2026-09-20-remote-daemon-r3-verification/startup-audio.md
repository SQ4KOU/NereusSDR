# Startup receive cadence and audio diagnostics

Requirements: R-R3-02/07/10/23. This checkpoint addresses station startup
ordering and observability. It does not establish that the September 21
network outage or live waterfall freeze has been resolved.

## Reproduced startup defect

The P2 connection initially has disabled DDCs at 48 kHz. The model queues its
sample-rate setter before connecting, but that setter changes only enabled
DDCs. The codec subsequently computes the full assignment from the stream
pool, including Saturn's primary DDC2 at 192 kHz. The model previously refused
to send that assignment while Connecting and did not request it again on the
Connected transition. A later slice operation could mask the missing update.

A regression uses the real Saturn codec and connection with exactly one slice,
192 kHz stream geometry and the connection worker initially stopped. After
that worker drains, the composed CmdRx enable mask remained zero instead of
DDC2's `0x04`; the constructor rate was 48 kHz. The established-connection
marshalling case and disconnected negative case passed. This demonstrates the
missing assignment without a radio, retune or extra slice.

The correction admits the codec assignment during Connecting, retains
connection-thread marshalling and re-requests it on both Connecting and
Connected. The first retry matters because the codec notification can precede
the model's Connecting notification. Delivery re-checks the actual connection
state and rejects a connection that stopped since the update was queued.
P1 receives no new lifecycle assignment requests. Codec ownership is checked
inside the connection-thread delivery, alongside its current lifecycle state.

## Diagnostic coverage

Core now exposes source frame and source drop-event counters, consumed blocks,
encoded packets/failures, last emitted sequence/timestamp, and transport send
acceptance/refusal. Source drop events mix rejected ingress callbacks and
full-ring blocks; they are not a packet-loss percentage. Send acceptance means
the transport accepted a packet, not that the client received it.

Diagnostics reset for each active audio context, retain a final snapshot after
source quiescence, and log periodically even if no audio arrives. Accounting
records each attempt before entering the transport and checks context/peer
identity on return. A synchronous retirement retains an unresolved-at-retirement
attempt, rather than losing the attempt or crediting a replacement context.
The invariant is attempts = accepted + refused + in-flight + unresolved; an
unresolved outcome is not a claim of packet loss.
No logging or new blocking work was added to the DSP callback. The receiver's
500 ms watchdog now reports no admitted/playable packets: late packets can
still be arriving when that watchdog fires.

The four affected audio test executables passed in the first combined focused
run; only the intentionally failing startup assignment test failed. Tests use
real mixer ingress, source overflow/stop/restart and controlled transport
acceptance/refusal. Logs are retained privately as
`r3-startup-audio-red-{build,test}.log`.

## Combined verification

One consolidated review identified a connection-thread codec-ownership read and
synchronous transport retirement accounting. Both were corrected; the latter
was first reproduced with a transport that retires its session from sendRtp.
The regression failed with zero recorded attempts before the correction, and
now retains one unresolved-at-retirement attempt.

A fresh build of `all_tests` and `nereusd`, then unfiltered
`ctest --test-dir build-integration -j8 --no-tests=error --output-on-failure`,
passed **682/682 test executables in 153.12 seconds**. Eleven pre-existing Qt
cases skipped for host API or deferred harness/UI reasons; all new cases ran.
Private logs: `r3-startup-audio-reviewed-{build,test,cases}.log`.

The first full run hit an unrelated native window-exposure wait in the DSS
span-tooltip test. Tooltip updates are synchronous, so that test now verifies
the actual wording without requiring a popup to become exposed. The separate
shown-widget wiring case remains intact.

## Required live check

Install a signed checkpoint with rollback and
attach one GUI slice at a fixed receive frequency. Verify the configured host
rate, actual DDC2 CmdRx rate and source-frame/Opus cadence together for at least
ten seconds. A 192 kHz DDC must match the host's 192 kHz input geometry; the
mixed bus remains 48 kHz with one 1,920-frame Opus packet per 40 ms. Then check
VFO movement, spectrum, waterfall, meter, audio and disconnect/reconnect.

The latest GUI log's 159–200 ms RTP arrival gaps and prompt Qt drain are
consistent with a fourfold source slowdown, but do not prove the live wire
rate. The board is reachable again at its Wi-Fi address `.105`; its wired port has
carrier but lost its IPv4 lease. The existing installed checkpoint was
temporarily rebound to `.105` with unchanged pairing and certificate. No live
startup-rate correction is claimed before deploying and checking this code.

## Pre-install hardware evidence, September 21

A passive capture spanning a restart of installed `95b19467` recorded the real
CmdRx sent to Saturn at 17:31:32: DDC enable mask `0x04`, DDC2 rate **48 kHz**.
The same startup log declares `sampleRate=192000`, one active receiver, and a
computed codec assignment of 192 kHz. This establishes the live wire/host
mismatch before the correction. Private capture: `r3-startup-rate-before.pcap`.
The radio was reached through `wlan0`; Core listened on `.105`. Post-install
wire convergence and continuous media remain separate acceptance checks.
