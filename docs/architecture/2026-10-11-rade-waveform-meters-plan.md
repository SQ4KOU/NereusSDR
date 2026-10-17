# Native RADE Waveform Meters Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Ship the first two approved ordinary waveform meter items with full-rate bounded data, normal Stack/Canvas composition and PR #381 face styling.

**Architecture:** Core Qt queued bounded publishers observe raw/conditioned mic and actual decoder PCM; a main-thread model shares history across views. A typed MeterPoller path updates independent WaveformMeterItem faces through existing registry, source routing, properties and layout projections. No changes to audio processing, threads or container schemas.

**Tech Stack:** C++20, Qt6/QPainter/QRhi, existing core/model/meter libraries, CMake/Ninja.

**Spec:** docs/architecture/2026-10-11-rade-waveform-meters-design.md; approved composition/refinement in 2026-10-11-rade-audio-audit.md.

## Global Constraints

- Preserve the complete uncommitted RADEV2 experiment on codex/radev2-experiment and the user's primary checkout.
- 5000 ms default; only 1000/100 ms alternatives; fixed -1..+1 amplitude/.2 divisions; full rate and no hidden averaging.
- Mic Stage default Conditioned; explicit RawMic option; actual 16k/48k/24k source labels, no silent fallback.
- Original face paint matches pinned PR381 role facts and accepted green trace; no unmerged style-framework merge/port.
- Observation-only: no radio/device connection/keying, no mic demand/permission or DSP/default/codec/routing change.
- Queued PCM boundary, at most one in-flight batch/stream, five-second pending/history caps, bounded packet leases, no producer mutex/wait/heap growth in accumulation.
- Core/models include no GUI headers; no new threads or unbounded Qt event stream; runtime PCM never persists.
- Local and remote Core desktop waveforms in this pass; older Core without negotiated capability is Unsupported. TCI remains Unavailable. Exact bounded feed contract follows the existing authenticated media transport.
- Targeted tests only per fast-test-loop; no commits/push required for this local deliverable.

## Review Focus

- A delayed block arriving after TX/RX/source replacement cannot become a new source's waveform.
- A retained immutable packet lease or stopped GUI cannot stall audio, grow memory, or authorize packet mutation.
- Raw silence differs from underflow/padding/no decoder output; a missing source never becomes a flat measured line.
- Multiple views, hidden targets and inert previews cannot consume or multiply physical observation streams.
- Impulses at column boundaries/last sample and gaps survive DPR/zoom and dynamic-layer GPU caching.

### Task 1: Bounded publisher, history and physical taps

**Create:** src/core/audio/WaveformSnapshot.h; WaveformPublisher.h/.cpp; WaveformHistory.h/.cpp; src/models/RadeWaveformModel.h/.cpp.
**Modify:** TxWorkerThread.h/.cpp, RadeChannel.h/.cpp, RadioModel.h/.cpp and only necessary existing lifecycle hooks; CMakeLists.txt, tests/CMakeLists.txt.
**Test:** tests/tst_waveform_publisher.cpp; tst_waveform_history.cpp; tst_rade_waveform_model.cpp; affected rade_channel/rade_tx_pump/rade_rx_multislice_routing tests.

**Interfaces (settle matching names once before code):**
- `enum class WaveformKind { RawMicrophone, ConditionedMicrophone, DecodedSpeech };`
- `WaveformBatch` owns bounded mono float samples plus token, sampleRate, source sequence/time segments and dropped-frame metadata; `using WaveformBatchPtr = std::shared_ptr<const WaveformBatch>` with queued metatype declaration.
- `WaveformSnapshot` owns/shares immutable full-rate samples, segments/gaps, generation/revision, sampleRate, asOfNs, availability and reason; `using WaveformSnapshotPtr = std::shared_ptr<const WaveformSnapshot>`.
- `WaveformPublisher(WaveformKind kind, int sampleRate, QObject* parent=nullptr)`; `void setBindingToken(quint64 token) noexcept`; `quint64 bindingToken() const noexcept`; producer `bool appendFloat(quint64 token, const float* pcm, int frames, int stride, qint64 atNs, bool valid) noexcept`, with the same signature using `const int16_t*` for appendInt16 and `const double*` for appendDouble; `void acknowledge(quint64 batchSerial) noexcept`; signal `batchReady(WaveformBatchPtr batch)`. Token 0 disabled; return/drop behavior tested. Fixed packet pool with lease ownership; acknowledgement does not free a retained lease.
- `WaveformHistory::append(const WaveformBatch&)`, `clear()`, `snapshot(qint64 asOfNs)` retaining only five seconds, preserving exact samples/segments.
- `RadeWaveformModel::setDemand(quintptr owner, const QList<WaveformRequest>& requests)` and `clearDemand(owner)`; `snapshot(int sliceId, WaveformKind kind, qint64 asOfNs)`; main-thread physical token/lifecycle registrations. RadioModel exposes `radeWaveformModel()`.
- Request is sliceId + kind; owner identifies a window's aggregate demand. Exact source key includes live connection/channel generation internally; no raw QObject pointers in DTOs.

- [x] Write behavior tests first: disabled capture copies nothing; pristine sample/stride/int16 normalization; one pending queued batch maximum; retained leases remain immutable; overflow is bounded and reported; stale token does not enter current history; last/first samples survive history wrap; 5s eviction and time gaps.
- [x] Build named new targets and run their tests, recording red behavior before implementation (minimal API scaffolding acceptable; compile-error-only evidence is insufficient).
- [x] Implement pooled queued publication + single model history consumer; no shared mutable vector or SPSC exception to signal-boundary rule. Use producer-owned bounded pending ring, safe overflow and exact segment/sequence records.
- [x] Wire observation taps at verified sites. Capture token at block entry and check at publication; raw WDSP display conversion to float32 is observation-only. Do not reset/change conditioning or audio gate.
- [x] Wire physical demand/retirement to TX owner changes, connection/worker/channel/route replacement and receive gating. Invalid/underflow/no output state remains explicit. Do not activate restored RADE owner from a meter query.
- [x] Extend actual codec/pump/routing behavior tests for unchanged audio with disabled/enabled/full observer, real decoder data before zero padding, raw setup vs conditioned availability, stale-source retirement and other-slice progress. Keep existing legacy skips, don't add new skips/timeouts.
- [x] Build/run named tests; record evidence and any implementation-level interface correction in work/waveform-implementation-report.md.

### Task 2: Typed ordinary meter face and refresh

**Create:** src/gui/meters/WaveformMeterItem.h/.cpp; small original WaveformMeterStyle.h if needed.
**Modify:** ContainerContentRegistry.cpp; ContainerSourceAdapter.h/.cpp; MeterPoller.h/.cpp; MainWindow.cpp; ContainerPreviewWidget.cpp; CMakeLists.txt/tests registration.
**Test:** tests/tst_waveform_meter.cpp; tst_waveform_dispatch.cpp.

**Interfaces:**
- `WaveformMeterItem` direct MeterItem subclass, enum `Source { Microphone, DecodedSpeech }`, enum `SourceStage { Conditioned, RawMic }`; `configuration()`, `applyConfiguration(QJsonObject)`, `serialize()/deserialize()`, `preferredFaceHeight()/minimumFaceSize()`, `setSnapshot(WaveformSnapshotPtr)`, `viewWindowMs()`, `waveformKind()`; no scalar reading bindings. Source follows canonical type; stage applies only mic.
- Registry types meter.radeMicWaveform / meter.radeSpeechWaveform, non-singleton, constructible with no live data; default height300/min240x180, title visible, Conditioned/5000 default.
- Poller `setWaveformSource(std::function<WaveformSnapshotPtr(const QJsonObject&, WaveformKind)>)`, demand callback with deduplicated context/kind requests, separate `waveformFrameAdvanced()` signal and33-ms timer. Model demand owner belongs to MainWindow; teardown clears it.
- SourceAdapter resolves common context/session/identity before snapshot/demand, rejects foreign/closed/remote; preview copies cached pointers and has no second physical subscription.

- [x] Add failing registry/paint/dispatch tests: actual catalog instantiation, exact config validity, fixed axes/window labels, synthetic positive/negative one-sample impulses at physical column boundaries/final sample, valid silence vs unavailable, gaps not joined, DPR1/2 and all windows.
- [x] Add fake-provider tests for deduplication, same snapshot across duplicate targets, different source kinds/slices, follow-selected identity resets, foreign/remote rejection, destruction and visible demand reference counts; previews copy cache without a new capture.
- [x] Implement original QPainter rendering with DPR bins and actual close samples. Clip display only, preserve over-range data and indicate clipping. Split static backdrop/axes and dynamic trace/status; setSnapshot invalidates only needed layers.
- [x] Register hydrate/capture JSON config without primitive legacy validator fallthrough; preserve opaque imported config/context.
- [x] Implement typed provider/33-ms refresh and inert preview copying independently of scalar DSP/meter timer preferences. Availability includes actual source/rate/stage captions; source change clears old waveform.
- [x] Build/run new tests and selected existing meter dispatcher/preview tests; save deterministic fake-data face screenshots at wide/detail/DPR1/2/unavailable for lead visual inspection.

### Task 3: Properties, Stack/Canvas composition and final verification

**Modify:** ContentPropertyEditor.cpp; ContainerContentHost.cpp; ContainerPreviewWidget.cpp sizing branches; tests/tst_waveform_containers.cpp or focused extension of existing host/FreeCanvas tests.
**Interfaces:** normal common source/visibility/geometry UI; waveform window5000/1000/100 and mic stageConditioned/RawMic; no scalar bindings/MMIO controls on this face. Existing ContainerDocument, duplication and FreeCanvas projection remain authorities.

- [x] Add failing actual document/host/property tests: mix two waveforms with ordinary meter; Stack -> FreeCanvas -> Stack; duplicate retains config/source with new ID; codec/workspace round-trip retains geometry/order/visibility/context; no-op Apply preserves opaque/sparse fields; invalid window/stage doesn't partially mutate.
- [x] Implement only typed size/property integration and ordinary hydration paths; no new host, layout mode, singleton, schema or parallel arrange UI.
- [x] Build/run focused new + affected existing container/poller/preview tests and tst_core_has_no_gui_includes. Build NereusSDR/nereusd; no whole-suite default or normal app launch/radio setup.
- [x] Inspect saved actual QPainter Stack/Canvas/detailed faces against accepted preview/PR381 role facts. Verify native QRhi dynamic texture update using existing safe NativeWindowFrames fixture if warranted by the new layered painter; no radio/device launch.
- [x] Fresh independent consolidated review checks all acceptance and concurrency/source invariants. Original worker fixes findings; rerun covering checks only. Lead checks actual diff/results, preserves local branch and records capabilities/platform/time-overhead limits.

## Execution record

JJ's design approval is authorization for these first native items. Continue the existing cost-aware workflow: one Sol6.1-high worker owns the tightly coupled end-to-end change, one fresh Sol6.1-high integrated reviewer after source settles; lead owns plan/interface choices and visual acceptance. No repeated execution-method approval. Read-only recon used Sol6.1-high workers waveform_audio_recon and waveform_meter_recon; usage/elapsed unknown. Reports referenced above. Public design and this plan add detail to the approved behavior; implementation choices/default boundaries are stated, not falsely attributed to JJ.

Plan self-review complete: each acceptance maps to Tasks1–3; queuedpublisher lease ownership, stale source tokens, visible demand and pixel-boundary tests explicitly covered. Default Conditioned preserves the approved encoder-input interpretation; RawMic is an explicit setup option, superseding the recon report's earlier tentative RawMic-default proposal. Baseline copies/hashes at work/waveform-baseline preserve the already-reviewed RADEV2 delta for a waveform-only review package. No product code was modified during planning.

Ruling during Task1 lead inspection: publication is capped at100ms latest samples, older stalled-UI backlog counted/discarded, pending block metadata bounded4096, native kind/rate/input bounds validated. The original5s bulk publication and per-sample segment reservation were memory/CPU risks. Core pooled batch uses std::vector to avoid Qt implicit-sharing detach allocations on producers; GUI snapshots may use QVector. Fixedslot count3, all retained leases remain immutable. Worker added regressions before correction.

## Remote Core scope update — JJ, 2026-10-11

JJ answered the pending scope question: “Include remote Core desktop support now.” This supersedes the earlier local-first assumption wherever it appears above. The current deliverable includes exact microphone and decoder waveform samples on a desktop connected to a capable Core. Older Cores without the negotiated observer feed must report Unsupported explicitly. The additional feed remains observational, visibility-demand driven and bounded; it must preserve full-rate samples, source generations and gaps, and cannot use Opus/program audio as a substitute. The precise transport extension will be based on the existing Core media/control code and recorded here before implementation. Local rendering and container verification continue independently.

### Task 4: Authorized remote Core desktop feed

**Scope update:** JJ explicitly selected remote Core desktop support during implementation. Local and remote work have disjoint ownership; waveform_impl retains the shared model, RadioModel/TX taps, GUI meter integration and CMake/build ownership. waveform_remote_impl owns the codec, existing media/server/client controllers, capability negotiation, RemoteMicFeed observation metadata and associated remote tests. Both use Sol 6.1 high; usage/elapsed unknown. Root settles interfaces, updates public requirements, inspects artifacts and requests one combined fresh review.

**Reference basis:** work/waveform-remote-recon.md cites the existing IQ transport, authenticated media policy, byte budget/pacer and clock-probe estimator. Direct control-session binary does not provide a universal media feed; use the established media path. New wire code is Nereus-original. Follow the optional remoteTx/IQ/display feature precedent at current minor 11 with hello waveform:1 and gated waveformVersion:1, documenting this deliberate version choice.

- [x] Add/run codec, controller, microphone validity and remote model behavior coverage. Codec/validity and later bounds/recovery/retirement fixes have actual pre-fix runtime RED; initial controller fixtures followed plumbing, an execution deviation recorded in Final acceptance below.
- [x] Carry incremental bit-exact float32 PCM on a dedicated negotiated waveform media channel across the existing direct/ICE/TURN/WebSocket-tunnel paths. Preserve native rates, contiguous segment coordinates, sequences, generations and gaps; reject malformed/nonfinite/overflowing input before allocation. Maximum1024 samples per packet; no packet joins an observation gap.
- [x] Add subscription/context control with lossless identities and revisions. Bind only known authorized slices. Decoded speech follows existing hearing permission; remote mic follows the actual authenticated writer, and station mic follows the current allowed transmitter holder. Recheck on fanout/send/revocation and source change.
- [x] Consume only the sole model's accepted batches. Bound each endpoint to one copied100ms pending batch and each connection to one assembled pending plus one library-buffered packet. Never retain producer packet leases for slow peers. Cap receive aggregate32 packets/128KiB with one drain notification; drop telemetry and sequence gaps remain explicit.
- [x] Reserve full native float bandwidth plus actual header/segment overhead in the existing display budget and application-byte pacer. Insufficient bandwidth gives an explicit refusal; observers cannot reduce audio/keying service, secretly decimate samples, or accumulate stale history.
- [x] Add optional RemoteMicFeed::Observation output without changing DSP samples, return values, counters or keys. Startup fill, inserted silence, failed reads, underflow/fades, Shed and use-change races are invalid observation spans. Preserve actual received/decoded microphone PCM, including existing codec concealment. Retire on writer or physical line/session replacement even when source category is unchanged.
- [x] Bridge visible deduplicated demand through RemoteMediaController and the shared waveform model. Track physical source and per-session delivery generations separately. Disable/reset old queued arrivals on source, session, connection, permission or media replacement. Older Core without the capability shows Unsupported.
- [x] Extend clock probes for waveform-only demand. Keep remote history in Core steady-clock coordinates; apply Core-minus-local offset consistently at snapshot, without speaker playout delay. No valid clock map gives explicit Unavailable and bounded pending data, never fabricated arrival-time sample origin.
- [x] Build/run only the new and affected remote/media/model/capability tests, serialized with the existing local build. Verify actual remote snapshots reach ordinary Stack/Canvas items unchanged. Build app/nereusd and finish combined review after both modules settle. No live radio/device/on-air/network-quality claim.

Remote wire contract: [Waveform media v1](2026-10-11-waveform-media-v1.md). This is the Nereus-original protocol extension for the authorized remote desktop scope.

Remote interface refinement: stable RadioModel getter; RadeWaveformModel::setRefreshDemand(owner,active)/clearRefreshDemand(owner) supplies refresh-only ownership, independent of capture setDemand. One33ms main-thread timer runs while capture or visible authorized refresh owners exist and requests endpoint synchronization. This avoids headless conditioned-before-first-key starvation and reentrant getter side effects. Cover timer activation/teardown, unavailable-to-available transitions, and budget refusal with publisher token0.

## Final acceptance — 2026-10-11

Tasks 1–4 are complete for the approved first native waveform increment, including JJ's additional remote Core desktop scope. The final matching build of NereusSDR, nereusd and 29 named tests exited 0 (`work/waveform-logs/final-acceptance-build.log`). The complete selected CTest run passed 29/29, zero failed, 239.03 s (`final-acceptance-green.log`). This includes full desktop/Core controllers, real encrypted media loopback, microphone validity, real V1/V2 wrapper speech/version transition, output invariants, model/history/publisher, ordinary container properties/persistence/demand/rendering, native QRhi, existing affected routing/pollers, R1 and canonical protocol/version guards. Two pre-existing legacy TX pump skips remain; no new waveform skips or timeout increases were added.

A fresh consolidated Sol 6.1 high review and narrow correction followup accepted the final frozen 63-file waveform source package with no open finding. Important decoded same-channel stream/epoch retirement and protocol inventory gaps, plus the minor hidden-only timer, were reproduced/corrected with covering checks. Source identity now includes synchronous slice index/epoch edges and receive-worker replacement; queued acceptance projects current identity before testing tokens. This is observer state only, with no receive DSP/routing policy change. The pooled batch API requires an owning strong lease while reading; no current weak consumer exists.

TDD record is deliberately precise: core/model/tap/face/dispatch/codec/validity and subsequent retirement/recovery/bounds/render fixes have actual runtime RED evidence. Some remote controller fixtures were added after initial plumbing; their passing runtime coverage does not become pre-implementation RED evidence. The original recovery failure used an invalid negative timestamp and is excluded; the corrected nonnegative fixture reproduced the intended recovery defect before its fix. These execution deviations are recorded in the workers' reports rather than hidden by the checked completion items.

Root inspected actual detailed/Stack/Canvas/unavailable fixtures, the complete final output, source hashes and faithful generated surface diff. All 34 current codec vendor hashes match their verified upstream manifest. The source package SHA256 is `53ea9ac01084f29c732f91a18c85774560264ceeac533d920da7c42f9eb9622c`; tested source hashes and review manifest agree. Verification is macOS arm64/fake/loopback/native-window scope, with no normal application launch or physical radio/keying. No Windows/Linux/live-Core/RF, audio-quality improvement or real-time CPU benchmark claim is made. The original DSP audit and V2 text/reporting limits remain. All changes are preserved uncommitted in the isolated worktree.

Implementation/review usage and elapsed dispatch time are unknown. Model choices remain the existing Sol 6.1 high local/remote implementers plus one fresh consolidated reviewer; focused corrections reused the owners/reviewer. No new user-visible task or repeated review cycle was created. [Experiment guide](2026-10-11-rade-experiment-delivery.md) gives operation and boundaries.

### Final environment correction

The final base-settings preservation check failed after the 29-pass run. Installed Qt 6.11 resolved the existing native HOME-only test exemption to the real macOS preferences directory even in test mode. The existing preview fixture has a matching clear/save/remove path. tests/CMakeLists.txt now isolates every macOS target using per-target CFFIXED_USER_HOME. Both affected native tests pass after configure/build (2/2, 13.35 s), and their actual resolved paths were checked before execution. This is an additional test-infrastructure correction, with no product/DSP change. The original 29-pass product evidence remains; the new final source fingerprint above includes the CMake fix. The real base file is absent; the running radxa_5c_r3 profile is separate and remains present. Exact-checkpoint recovery was not found, and restoring a validated October 6 base backup awaits JJ's choice. See work/waveform-settings-incident.md for source/path/backup evidence. Do not claim all real settings were preserved.

Approved recovery completed: JJ selected the validated October 6 base backup. Root restored it atomically on October 11 at 21:12:30 America/New_York, verified byte equality/source hash and the unchanged separate running profile hash. No exact-checkpoint recovery is claimed. See work/waveform-settings-restore-result.json. No remaining recovery approval/action is pending.
