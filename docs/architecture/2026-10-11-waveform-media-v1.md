# Exact waveform observation media v1

This optional desktop observer extension uses session protocol 1.11, following
remote TX, raw I/Q and display-clock optional-channel negotiation. The desktop
hello declares `features.waveform=1`; a Core with local waveform observers and
media enabled offers `waveformVersion=1` only to that peer. The media `start`
declares `waveformVersion=1` before either side opens the dedicated `waveform`
data channel. It is unordered with zero retransmissions. Older peers open no
channel and the desktop model reports Unsupported. Direct ICE, TURN and the
existing WebSocket media tunnel use the same channel. No phone UI is added.

Control requests have exactly `op`, `connectionId`, `sliceId`, `kind`,
`revision`, `enabled`. `op` is `waveform-stream`; revision is a nonzero,
wrap-aware increasing uint32 for the endpoint on this connection. The explicit
kind strings are `rawMic`, `micConditioned`, `decodedSpeech`, corresponding to
48000, 16000 and 24000 Hz. A context response has exactly these six fields,
plus `availability`, `reason`, `sourceGeneration`, `deliveryGeneration`,
`sampleRateHz`; its op is `waveform-stream-context`. Enabled contexts are
Available, have nonzero source and delivery generations, and the native rate.
Disabled contexts are Unavailable with rate 0. uint64 generations are canonical
ASCII decimal JSON strings, including `0` for absent identities. No JSON double
carries a uint64 identity. Source generation identifies the physical PCM source;
delivery generation retires an endpoint binding independently of request
revision, publisher token, packet serial and physical source identity.

Binary integers are big endian. The fixed header is 72 bytes:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 4 | ASCII `NWF1` |
| 4 | 1 | Schema version 1 |
| 5 | 1 | Kind: 1 raw microphone, 2 conditioned microphone, 3 decoded speech |
| 6 | 2 | Reserved zero |
| 8 | 4 | Nonnegative signed-int-range slice ID |
| 12 | 4 | Native sample rate Hz |
| 16 | 8 | Nonzero delivery generation |
| 24 | 8 | Nonzero physical source generation |
| 32 | 8 | Nonzero uint64 packet serial, increasing within the delivery |
| 40 | 8 | First sample sequence, uint64 |
| 48 | 8 | Segment start in Core monotonic nanoseconds, nonnegative int64 range |
| 56 | 4 | Sample count, 1 through 1024 |
| 60 | 4 | Reserved zero |
| 64 | 8 | Cumulative dropped observer frames within this delivery |
| 72 | 4 × count | IEEE-754 float32 bits, big endian |

A packet contains one contiguous source segment. Splitting advances sequence by
the segment-relative sample offset, and time by floor(offset × 10^9 / rate).
Packets never join across a source gap. Float values, including signed zero and
over-range finite values, round-trip exactly. Codec validation rejects unknown
schema/kind/flags, rates inconsistent with kind, oversized/zero counts, wrong
length, zero identities, nonfinite floats and overflowing coordinates before
allocating decoded PCM. The desktop rejects retired context/session/source or
delivery identities, duplicate/old packet serials and overlapping sequences.
Sequence holes remain holes. Cumulative drops are converted to history deltas;
local receive losses are also visible through sequence holes and transport
packet-drop accounting.

Only visible, deduplicated ordinary meter demand enables observation. The Core
requires a real known slice and existing listen permission for decoded speech.
Microphone observation requires the allowed TX-bound slice, plus either the
actual authenticated remote microphone writer, or the current TX holder for the
configured station source. An empty vanished remote writer while its line remains
selected grants no station microphone fallback. Authorization is checked at
subscription, accepted-batch fanout and send, and rechecked on ownership,
listening, writer, holder, TX binding and source changes. This observer never
starts capture, grants permission, keys or changes processing.

The single model publisher consumer acknowledges the producer once. The remote
sender copies only an accepted batch, at most 100 ms, on the main thread; it
retains no producer pool lease. There is one copied pending batch per endpoint,
one assembled packet per connection and one library-buffered packet. Superseded
pending observations increment dropped-frame accounting. Receive callbacks hold
at most 32 packets and 128 KiB per connection, dropping oldest; the existing
single owner-thread 2 ms drain handles the aggregate without a per-packet queued
event. Histories retain the existing five-second native-rate/time bound. A
250 ms send stall retires delivery and clears capture demand with an explicit
media-stall or exhausted-share pause reason. A writable notification retries
still-visible, authorized demand; pacing-only stalls also retry after a bounded
250 ms deadline on the existing sender tick. No new retry thread/timer exists.
Hiding the last endpoint or losing the session cancels the deadline. A terminal
waveform channel error stays unavailable until a new media connection.

Authorized visible intent keeps a refresh-only watch in the shared model while
the physical source is temporarily unavailable or full-rate admission refused.
The model's single 33 ms Core timer refreshes source state without setting a
publisher token or starting capture. Thus a headless conditioned source can
become available after its existing TX path latches, without requiring a local
window. Capture demand remains limited to admitted sending endpoints. Last hide,
revocation and media/session cleanup remove the watch. A negotiated channel that
has not opened yet is Busy; an unnegotiated channel is Refused.

The display application-byte budget reserves float payload plus native-segment
and 1024-sample fragmentation header overhead, without lowering sample rate.
Raw and conditioned TX observation occurs at most once per 64-frame/48-kHz
worker dispatch (`TxWorkerThread.cpp`, conditioned/raw taps): at most 750 native
segments per second. Decoded synthesis consumes 160-frame/16-kHz neural feature
frames and emits at most once per nonempty synthesis call (`RadeChannel.cpp`,
feature synthesis and upsample sections): at most 100 segments per second in
steady operation; catch-up features share one segment. A conservative extra 30
headers/s accommodates bounded publication boundaries. The v1 charges are
251544 B/s raw, 121312 B/s conditioned and 107088 B/s decoded. The shared byte
pacer handles burst recovery and charges attempted packet sends once. A link
that cannot carry full-rate PCM explicitly refuses the endpoint. Audio and
keying keep their existing independent service paths.

Waveform timestamp origin is the Core's steady monotonic clock. Visible
waveforms alone keep the existing clock probes running, even when remote audio
and displays are off. The existing estimator uses the actual `RemoteAudioReceiver` local clock, whose
default origin is a process-local steady-clock base; waveform snapshots use the
absolute desktop steady-clock epoch. The GUI bridge converts that origin:
Core-minus-waveform offset = estimator offset + audio local now - waveform local
now, with checked signed arithmetic. The model retains history in Core time and
translates all segment timestamps by subtracting the converted offset at
snapshot. It adds no speaker playout delay. No usable
map gives explicit unavailable state and bounded pending observations. Source,
delivery, media or session retirement clears stale history; estimator corrections
translate history consistently and cannot invent a source gap.

Remote microphone observation metadata distinguishes actual complete post-rate-
match PCM from initial fill, inserted synthetic blocks, underflow fade, failed
matcher take, shed blocks and use-change races. Genuine numerical silence is
valid. Existing Opus concealment is actual TX pipeline PCM and is preserved.
Metadata changes no DSP output, return values, counters, capture demand or keys.
