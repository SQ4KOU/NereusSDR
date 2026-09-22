# R-R3-32/33: Core banner and connection/audio history

## Scope and implementation

The user selected the Core banner and connection/audio graphs first. CPU and
memory history remain a follow-on. The source-first Aether port is pinned to
`0dea0dd7d73e25a40c8c01d46873af5834e23921`; see
[provenance](../../attribution/AETHER-TELEMETRY-PROVENANCE.md) and
[design](../2026-09-21-core-telemetry-design.md).

Core publishes typed, capability-negotiated `station.metrics.v1` observations
only after authentication and snapshot completion. The collector requests
radio observations on the connection's owning thread and discards late replies
from retired sessions/connections. Audio rates use actual elapsed intervals
and reset their baselines at context changes. Missing values remain unavailable.

The GUI combines those observations with actual WebSocket payload/RTT counters
and local receiver activity. The banner distinguishes radio rates, Core RTT
and playback. A remote diagnostics dialog adds bounded in-memory history,
explicit reconnect/missing-data gaps, source labels and separate units. RTT is
a labelled last-observed gauge with actual age, not an invented fresh sample.
History persists when the dialog closes. Underflow/overflow lifetime totals
preserve interruptions that occur between GUI polls across receiver restarts.

## Verification to date

Focused tests cover the typed codec, negotiated/authenticated delivery,
old-epoch suppression, actual queued cross-thread radio observations, context
resets, real packet admission and device progress, transport payload/pong
measurements, bounded weighted history, gap-preserving graph paths, the
production banner/controller and real authenticated dialog graph wiring.

One consolidated independent review found two issues: restart boundaries could
lose underflow/overflow events between one-second polls, and the dialog test
did not exercise its production graph refresh. Both are corrected. The event
regression drives a real receiver and independent device-render thread to force
an interruption, then verifies its lifetime count after restart. The dialog
test receives authenticated production samples and inspects actual graph series.
The corrected targets pass. Fixture failures and their corrections are retained
in the private build logs; no production behavior was relaxed to satisfy them.

The integrated `all_tests`/`nereusd` build passed. The unfiltered full test
run passed **692/692 executables**, with eleven pre-existing inner Qt skips
and no skips in the new telemetry tests. Native production configuration, installation, live banner/graph
readability, receive-only reconnect gaps and smooth playback acceptance remain
pending. No telemetry work in progress has been installed.

The telemetry checkpoint is ready for the native production build, but its
installation is held while the installed RADE checkpoint's live sample-cadence
failure is investigated. The passing telemetry suite does not close that separate
hardware failure; see [RADE multislice](rade-multislice.md).
