# Remote media control version 1

This is the R3 implementation contract for R-R3-02 through R-R3-09.
Session protocol minor 1 and capability `remoteMediaVersion=1` negotiate it.
An authenticated, snapshot-complete active control session is required in
both directions. Older clients retain the R2 control protocol.

Control messages use the existing WSS envelope:

```json
{"type":"media.control","payload":{"op":"start","connectionId":"canonical-uuid"}}
```

The complete encoded envelope is limited to 128 KiB. Each operation has an
exact set of keys. Integers are JSON numbers checked for range and integrality
before narrowing; strings and numeric values are never coerced. Display
arrays and audio packets use the separate encrypted media connection.

## Media peer

`connectionId` is a non-null lowercase UUID with hyphens and no braces.
The GUI creates it after the control snapshot and sends `start`; Core is
the offerer and the GUI the answerer. Every subsequent operation carries
the same ID. Control-session replacement retires the old peer, subscriptions,
codec histories and callbacks, including deliberate silent redials.

| Operation | Exact payload fields beyond `op` and `connectionId` |
| --- | --- |
| `start` | None; a GUI whose Core advertised `audioProfileVersion` adds `audioProfileVersion`, a whole number of at least 1 (anything else is refused and no peer starts) |
| `description` | `sdp`, `type` (`offer` or `answer`, appropriate to peer role) |
| `candidate` | `candidate`, `mid` |

SDP is limited to 64 KiB. Candidate and MID strings are limited to 4 KiB and
256 bytes respectively; NUL is forbidden. At most 64 remote candidate
controls are admitted, including candidates buffered before the description.
The R3 backend accepts host ICE candidates. SDP-embedded candidates are
rejected: this version uses bounded trickle candidates exclusively. This
provides direct LAN media,
and does not close the separate R5 TURN/TCP, TURN/TLS or NAT traversal gates.

Display is an unordered SCTP data channel with zero retransmissions, secured
by DTLS. Audio is separate RTP/SRTP, payload type 111. The session audio SSRC
is derived by SHA-256 over the ASCII prefix `NereusSDR/media-audio-ssrc/v1:`
followed by the canonical connection UUID. The first four digest bytes form
an unsigned big-endian integer; zero maps to one. Both peers use this ID,
and reject mismatching RTP. SSRC is a routing identity; authentication comes
from the pinned WSS session and its negotiated DTLS peer.

Receiver audio streams (R-R3-43) share the one audio m-line and its SRTP
context with the main stream; only their SSRCs differ. Receiver stream `n`,
for `n` from 0 to 3, has the SSRC formed the same way from the ASCII prefix
`NereusSDR/media-receiver-ssrc/v1:`, the decimal `n`, a colon, then the
canonical connection UUID. A value that is zero, equal to the main SSRC or
equal to an earlier receiver's SSRC is replaced by the next integer (modulo
2^32) until it is none of these, so the five IDs are distinct and both peers
derive the same set. The receiver streams are declared only when both peers
start the media connection with receiver audio (the GUI asked for it at
`start`; the control that asks is defined with the receiver audio operation).
Then Core's offer carries, after the main stream's
`a=ssrc:<main> cname:nereus-mixed-stereo` line, one
`a=ssrc:<receiver n> cname:nereus-receiver-<n>` line per receiver in order,
and each peer sends and accepts exactly the main SSRC and the four receiver
SSRCs. Without it the offer, the answer and every audio line are exactly as
before, and only the main SSRC is sent or accepted. RTP with any other SSRC
is refused and reported. The transport library itself does not refuse it:
libdatachannel v0.24.5 hands every packet on a connection with one media
line to that line's track whatever its SSRC (measured, not assumed), so the
refusal is the media peer's receive filter. Each peer's queue of received
RTP between drains holds 64 packets per declared stream, 256 ms of lossless
audio (250 packets/s) for every stream: 64 packets without receiver
streams, 320 with them; when full, the oldest packet is dropped.

## Display subscriptions

GUI-to-Core `subscribe` has these exact additional fields:

```text
endpointId, revision, sliceId, tier, fftSize, windowType,
centreHz, spanHz, pixels, fps, framesPerLine,
trace, waterfall, minDbm, maxDbm, wideSpanFactor
```

`endpointId` and `revision` are nonzero uint32 values. Revisions use unsigned
half-range ordering across wrap. `sliceId` must identify a live station
slice; Core resolves its stream, so the client cannot request an arbitrary
DDC. `tier` is `wide` or `fine`. Up to eight endpoints are admitted, each
requesting 1..4096 pixels and 1..60 frames/second. An FFT size is a power of
two from 1024; a size FFTEngine supports is honoured as asked, and a larger
one is granted FFTEngine's largest size. A request sizes its (stream, tier)
engine only while it is that engine's only subscriber; beside another
endpoint it is granted the engine's current size, so no pan's spectrum
changes to satisfy another's request. When the last neighbour leaves, an
endpoint held to a neighbour's size is granted its own. The engine runs at
the highest frame rate its endpoints ask for, and each endpoint keeps its own
cadence, so a rate change alone renews no endpoint. Pixels are granted as
min(requested, visible source bins, 4096). Incompatible window choices are
refused. A global
window change unsubscribes all old-window endpoints before requesting any
replacement, so shared sources can adopt the new window.

Both `trace` and `waterfall` contain exactly `detector`, `averageMode` and
`averageAlpha`. Detector codes are the existing SpectrumDetectorMode values:
0 peak, 1 Rosenfell, 2 average, 3 sample, 4 RMS. Averaging codes are the
existing SpectrumAvenger values: -1 peak hold, 0 none, 1 recursive linear,
2 time window linear, 3 recursive logarithmic. Alpha is finite in [0,1].
Each plane owns an independent reducer history.

Frequencies and spans are in Hz. Spans must be positive; source and request
edges and the requested wide product must remain finite. The quantization
window has finite `minDbm < maxDbm`. `wideSpanFactor=0` disables wide coverage;
otherwise it exceeds one and represents the GUI's maximum 3D shape. Core
clamps coverage to the actual source and the wide row to 768 samples. A
wholly nonoverlapping request is rejected. A retune that removes its coverage
also rejects and retires the endpoint; Core never acknowledges a zero-span
display context.

Core-to-GUI `context` has exactly 19 fields in total:

```text
op, connectionId, endpointId, revision, contextGeneration, sourceStream,
sourceCentreHz, sampleRateHz, centreHz, spanHz, wideCentreHz, wideSpanHz,
traceSamples, waterfallSamples, wideSamples, minDbm, maxDbm, fps, framesPerLine
```

A subscription that negotiated the extended view (minor 6) adds a 20th
field, `wideband`.

Session protocol minor 9 and capability `spectrumGrantVersion=1` add five
fields reporting what Core granted the endpoint (R-R3-01, R-R3-08), so the
context has 24 fields, or 25 with `wideband`:

```text
grantedFftSize, grantedTier, requestedPixels, grantedPixels, limit
```

`grantedFftSize` is the FFT size the endpoint's engine actually runs
(1..262144). `grantedTier` is `wide` or `fine`. `requestedPixels` and
`grantedPixels` are 1..4096 with granted not above requested. `limit` names
what reduced the grant: `none`, `largest-size` (the request was above the
largest supported FFT size), `shared` (another endpoint uses the same stream
and tier engine, so its size stands) or `source-bins` (the crop has fewer
source bins than the requested pixels). A minor 8 or older peer receives
the 19- or 20-field context unchanged, and each side accepts only the shape
it negotiated. Both sides use one codec, `RemoteSpectrumContext`. The GUI
shows a plain status line on the pan while the grant is limited.

Core configures this from the first actual frame of the current source
generation, then sends it before encoded display. Exact sample counts clamp
to available cropped bins. Center/span describe the accepted bin-aligned
coverage; they are acknowledgments, not a new GUI gesture. A retune or
reconfiguration creates a new context generation and clears input overlap,
reduction history and pending output. A frame arriving before its context
is discarded; the GUI requests a keyframe after accepting context.

| Operation | Exact additional fields |
| --- | --- |
| `unsubscribe` | `endpointId` |
| `keyframe` | `endpointId`, `contextGeneration` |
| `rejected` (Core to GUI) | `endpointId`, `revision`, `reason` |
| `noise-floor` (Core to GUI) | `endpointId`, `revision`, `contextGeneration`, `floorDbm` |

R-R3-12 adds `noise-floor` as optional display metadata; older clients ignore
the unknown operation. It has exactly six payload fields including `op` and
`connectionId`. Core sends it after the accepted context, once initially and
then at most twice per second per endpoint, using a fresh source frame.
`floorDbm` is finite and bounded to [-400,100]. It is the existing
NoiseFloorEstimator's 30th percentile of the full-source FFT dBm bins, with
station calibration applied once, before viewport crop, detector, averaging
or codec quantization. No full FFT array travels over WSS.

The GUI requires the current authenticated session, connection ID, endpoint
revision and context generation. Only the visible active pan with unchanged
subscription inputs may feed its existing Clarity controller. Local cadence,
EWMA, deadband, manual override, re-tune, TX pause and palette behavior remain
in effect. The binary display codec and R3 media version are unchanged. This
restores the existing global active-pan Clarity behavior; independent
per-pan Clarity controllers and Auto AGC-T telemetry remain separate work.

Keyframe requests are limited to five per endpoint per second and must
match its current context. A whole-peer rejection uses endpoint/revision
zero. Unsubscribe releases an unused source. Rebinding, slice removal and
disconnect invalidate the corresponding endpoint. Hidden GUI panes
unsubscribe, and a newly shown pane receives a new endpoint ID.

The [display codec specification](2026-09-20-display-codec-v1.md) defines
the binary packets, reconstruction and loss recovery. Pending source/output
slots and transport queues are bounded. A failed nonblocking send is not
retried with the same bytes: it may already have entered the library buffer;
the following attempt uses a keyframe. Dropped I/Q invalidates the FFT input
history before post-gap samples are processed. Endpoint cadence follows an
advancing schedule with bounded early-jitter tolerance; it does not restart
its entire interval after each arrival or catch up with a burst after a stall.

## Measured audio delay (clock-probe and clock-echo)

Capability `audioClockVersion=1` (R-R3-35) negotiates it; the session
protocol minor is unchanged. The Core advertises version 1 whenever media is
on. A GUI sends probes only while audio plays to a Core that advertised it,
one every 1000 ms (`RemoteMediaController::kClockProbeIntervalMs`); a Core
without it is never probed and shows no delay.

GUI-to-Core `clock-probe` has exactly these fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation; the current peer's ID |
| `id` | uint32, the probe's number |
| `t0` | Non-negative integer nanoseconds on the GUI's clock when the probe was sent |

Core-to-GUI `clock-echo` has exactly nine fields:

| Field | Meaning |
| --- | --- |
| `op`, `connectionId` | As every operation |
| `id`, `t0` | Copied from the probe |
| `t1` | Core clock (non-negative integer nanoseconds) when the probe arrived, read first |
| `t2` | Core clock when the echo left, read last |
| `generation` | The running audio context's generation, or 0 while no context is capturing |
| `rtpTimestamp` | The RTP time at the end of the newest captured block (its timestamp plus 1920), or 0 |
| `capturedNs` | The Core clock when that block's last frame reached the audio tap, or 0 |

The Core ignores a probe with any other key, a wrong or retired
`connectionId`, an `id` that is not a uint32, or a negative or non-integer
`t0`. The GUI accepts an echo only as the answer to one of its last eight
probes, matching both `id` and `t0`; `t3` is its own clock on arrival.

From the echo the GUI computes the Core-minus-GUI clock offset
`((t1 - t0) + (t2 - t3)) / 2` and the round trip `(t3 - t0) - (t2 - t1)`. Of
the probes in the last 16 s it uses the one with the lowest round trip; half
that round trip, plus a 100 ppm allowance for the two clocks' drift, bounds
the offset's error. Half the round trip is shown only as accuracy, never as
a delay. No figure is shown once the newest echo is 3 s old, while the
echo's `generation` is 0 or is not the context this computer plays, or across
a new audio context until its first echo arrives.

The delay is the time this computer plays a sample minus the time the Core
captured it (mapped through the offset). The play time counts, after the
rate matcher fill and the speaker queue, the fixed delays inside the
pipeline: the Opus codec's algorithmic delay (`OPUS_GET_LOOKAHEAD`, 312
frames at 48 kHz, for Opus only) and the rate matcher's filter delay (69
frames). The speaker queue drains a device callback at a time, so half a
callback is counted and the other half is added to the accuracy, together
with half the time the queue read took. The device's own latency is added
when the audio backend reports it; otherwise the figure says it does not
count the speaker device.

The figure is the delay of the sample heard when the queue was read, not of
the newest sample behind it. While the rate matcher corrects its fill, its
ratio (output frames made per input frame, WDSP rmatch's `var`) is not 1 and
the delay itself changes as the audio plays: the audio between the two
samples was made at that ratio, so it spans `1 / ratio` as much capture time
as play time. The newest sample's delay is reduced by that stretch, the
play time the rate matcher made (everything ahead of the newest sample but
the codec's delay) times `1 - 1 / ratio`, using the ratio at the reading.

The audio enable/context lifecycle, playback buffering and adaptive session
budget are still being implemented. Their acceptance remains open in the
[R3 plan](2026-09-20-remote-daemon-r3-plan.md); this document does not claim
live hardware or internet traversal acceptance.
