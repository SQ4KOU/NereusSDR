# Display codec v1

`DisplayCodec` carries reduced dBm display rows on the R3 unreliable media
channel. It has no socket, GUI, radio, or Qt Multimedia dependency. The codec
is NereusSDR-original; its input is the output of the independent trace and
waterfall reducers.

## Bounds and context

One frame has an endpoint ID, a context generation, a 32-bit encoder sequence,
a sender-monotonic nanosecond timestamp, a finite `minDbm..maxDbm` quantisation interval,
and two required rows (trace and waterfall) plus one optional DSS-wide row.
Every row has 1 through 4096 samples. The interval and all row lengths are
part of the wire context. A context change requires a new generation and a
keyframe.

The timestamp is only ordered against frames from the same sender; it is not
comparable to the receiver's local clock. Source sample position and source
timing are accepted-context/endpoint-layer fields, not v1 codec fields.

One encoder instance belongs to one endpoint. The caller advances its context
generation for every range or row-shape change, and advances the sequence
strictly within that generation. A newer generation may restart the sequence.
The encoder rejects a same-generation context-shape change and stale/equal
sequence rather than emitting a frame the decoder must reject.

Inputs outside the interval are clipped. A finite input maps to the nearest
integer in `[0,255]`, using `round((dbm - minDbm) * 255 / (maxDbm - minDbm))`.
The normal reconstructed error for in-range inputs is at most half a quantum,
where `quantum = (maxDbm - minDbm) / 255`.

The encoder can use a non-negative integer dead zone in quantised units. When
the newly quantised value differs from the previous reconstructed value by at
most that dead zone, it retains the reconstructed value. Its error bound is
then `(deadZone + 0.5) * quantum`; each comparison is against the original
newly quantised input and the decoder reconstruction, so error does not
accumulate along a delta chain. Version 1 uses dead zone zero by default.

Frames are bounded to 16 KiB by the parser and the encoder. This is a codec
allocation limit, not the media transport datagram limit: transport owns
fragmentation and its 1000-byte total-packet cap.

## Byte layout

All multibyte integers are unsigned network byte order. Floating-point values
are IEEE-754 binary32 bit patterns written as unsigned 32-bit network-order
integers. No native C++ object is serialized.

The fixed header is exactly 42 bytes:

| Offset | Size | Field |
| --- | ---: | --- |
| 0 | 4 | ASCII magic `NSDC` |
| 4 | 1 | version, `1` |
| 5 | 1 | flags: `0x01` keyframe, `0x02` waterfall advance, `0x04` wide row; all other bits reject |
| 6 | 2 | header size, `42` |
| 8 | 4 | endpoint ID |
| 12 | 4 | context generation |
| 16 | 4 | encoder sequence |
| 20 | 8 | sender-monotonic timestamp in nanoseconds |
| 28 | 4 | minimum dBm |
| 32 | 4 | maximum dBm |
| 36 | 2 | trace length |
| 38 | 2 | waterfall length |
| 40 | 2 | wide-row length, zero when absent |

The header is followed by one encoded plane in trace, waterfall, then wide
order. Each plane begins with `blockSizeCode:u8` (`0=16`, `1=32`, `2=64`,
`3=128`) and `blockCount:u16`. The chosen size applies to the entire plane.
The block count must exactly cover the declared plane length, with all nonfinal
blocks at the chosen size.

Each block has `mode:u8`, `bitWidth:u8`, `sampleCount:u8`, `payloadBytes:u16`,
then its payload. `mode=0` is a signed temporal residual, and `mode=1` is an
absolute quantised value. Absolute blocks always use bit width 8 and exactly
one byte per sample. Residuals have widths 1 through 8; values are packed MSB
first, each signed residual biased by `1 << (bitWidth - 1)`. A residual block
whose values do not fit that representation falls back to absolute mode.
Keyframe blocks must all be absolute. Delta residuals are relative to the
decoder's previous reconstructed plane, never to a sender-only float input.

The encoder evaluates the four block sizes and deterministically selects the
one with the smallest block payload plus headers; equal costs select the
smaller size. This adapts stable rows and noisy/peaked rows without changing
the decoder contract.

## State and recovery

The first frame, every context change, every 120th frame, and an explicit
keyframe request are keyframes. The decoder accepts a delta only when the
endpoint/context match its accepted history and the sequence is exactly the
next unsigned 32-bit sequence. A loss or forward gap returns `NeedKeyframe`
and leaves history untouched until a complete keyframe arrives. Older/equal
sequences use the unsigned half-range rule and reject as stale. An older
context generation rejects; a newer generation needs a keyframe.

Any malformed packet (including unknown flags, invalid floats/context, bad
plane count/length, block overrun, invalid packing, truncation, or packet over
the codec bound) returns `Rejected` without changing the prior accepted frame
or reconstruction history.
