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
| `start` | None |
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

The audio enable/context lifecycle, playback buffering and adaptive session
budget are still being implemented. Their acceptance remains open in the
[R3 plan](2026-09-20-remote-daemon-r3-plan.md); this document does not claim
live hardware or internet traversal acceptance.
