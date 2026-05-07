# Phase 3M-3a-iv - Anti-VOX Cancellation Feed: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the anti-VOX cancellation gap left in 3M-3a-iii by wiring `RxDspWorker` audio output to `TxChannel::sendAntiVoxData()`, adding the four missing WDSP wrappers (`SetAntiVOXSize`, `SetAntiVOXRate`, `SetAntiVOXDetectorTau`, `SendAntiVOXData`), fixing the 10 ms / 20 ms tau-default drift, exposing `tau` on the existing Setup page, and persisting it per-MAC.

**Architecture:** Single-RX direct pump (no aamix). `RxDspWorker` emits `antiVoxSampleReady(sliceId, interleaved, sampleCount)` per drained chunk alongside the existing `rxBlockReady`. `TxWorkerThread` receives via queued connection, gates on a local `m_antiVoxRun` atomic, calls `TxChannel::sendAntiVoxData()` which converts float to double in a member scratch buffer and calls WDSP `SendAntiVOXData`. DEXP detector dimensions follow RX-side block geometry (`SetAntiVOXSize` + `SetAntiVOXRate` driven by `RxDspWorker::bufferSizesChanged`), not TX-side mic geometry. `tau` ms control on Setup page persists via existing `TransmitModel::persistOne` per-MAC pattern.

**Tech Stack:** C++20, Qt6 (QtWidgets, QtTest), CMake/Ninja, GPG-signed commits, ctest. WDSP via TxChannel wrappers. Per-MAC AppSettings for persistence. Pre-commit hooks: `verify-thetis-headers.py`, `check-new-ports.py`, `verify-inline-tag-preservation.py`, `verify-inline-cites.py`.

**Spec:** [docs/architecture/phase3m-3a-iv-antivox-feed-design.md](../../architecture/phase3m-3a-iv-antivox-feed-design.md)

**Branch:** Currently on `claude/kind-goldwasser-2ca59b`. Rename to `feature/phase3m-3a-iv-antivox-feed` before opening the PR.

---

## File Map

| File | New / Modify | Responsibility |
|---|---|---|
| `src/core/wdsp_api.h` | Modify | 4 new WDSP entry-point declarations near existing anti-VOX block |
| `src/core/TxChannel.h` | Modify | 4 new method declarations + `m_antiVoxSize` + `m_antiVoxScratch` |
| `src/core/TxChannel.cpp` | Modify | 4 implementations + size-mismatch guard + float→double conversion |
| `src/core/WdspEngine.cpp` | Modify | Constant fix anti-VOX tau 0.01 → 0.02 (line 593) |
| `src/models/RxDspWorker.h` | Modify | 2 new signals: `antiVoxSampleReady` and `bufferSizesChanged` |
| `src/models/RxDspWorker.cpp` | Modify | Emit `antiVoxSampleReady` per chunk, emit `bufferSizesChanged` from `setBufferSizes` |
| `src/core/TxWorkerThread.h` | Modify | 3 new slots (`onAntiVoxSamplesReady`, `setAntiVoxBlockGeometry`, `setAntiVoxDetectorTau`), `m_antiVoxRun` atomic |
| `src/core/TxWorkerThread.cpp` | Modify | Slot bodies; mirror existing `setAntiVoxRun` to also flip atomic |
| `src/core/MoxController.h` | Modify | `setAntiVoxTau(int)` slot + `antiVoxDetectorTauRequested(double)` signal + state |
| `src/core/MoxController.cpp` | Modify | Implementation with NaN-sentinel idempotency, ms→s scaling |
| `src/models/TransmitModel.h` | Modify | `antiVoxTauMs` Q_PROPERTY + range constants + signal + accessor |
| `src/models/TransmitModel.cpp` | Modify | Implementation with `persistOne` auto-persist + load + clamp |
| `src/models/RadioModel.cpp` | Modify | 4 new `connect()` calls in the lambda-wrapped TX-channel-ready block |
| `src/gui/setup/DexpVoxSetupPage.h` | Modify | `m_antiVoxTauSpin` member |
| `src/gui/setup/DexpVoxSetupPage.cpp` | Modify | Build spinbox row, tooltip from Thetis verbatim, wire to TM with QSignalBlocker round-trip guard |
| `tests/tst_tx_channel_vox_anti_vox.cpp` | Modify | Extend with 4 new setter cases + `sendAntiVoxData` happy / mismatch / float→double cases |
| `tests/tst_mox_controller_anti_vox.cpp` | Modify | Extend with 6 new tau-slot cases (default, idempotent, ms→s, NaN-sentinel, boundary 1 ms, boundary 500 ms) |
| `tests/tst_rx_dsp_worker_buffer_sizing.cpp` | Modify | Extend with `bufferSizesChanged` signal-fires test |
| `tests/tst_rx_dsp_worker_thread.cpp` | Modify | Extend with `antiVoxSampleReady` payload-matches-rxBlockReady test |
| `tests/tst_transmit_model_anti_vox.cpp` | Modify | Extend with `antiVoxTauMs` default / clamp / persist round-trip cases |
| `tests/tst_dexp_vox_setup_page.cpp` | Modify | Extend with Tau spinbox round-trip + tooltip-verbatim cases |
| `tests/CMakeLists.txt` | None | All new tests are extensions to existing executables, no new lines needed |
| `docs/architecture/phase3m-3a-iv-verification/README.md` | Create | Manual bench-verification matrix (5 rows × ANAN-G2 + HL2) |
| `CHANGELOG.md` | Modify | New v0.3.3 (or next-tag) entry under "Unreleased" |
| `CLAUDE.md` | Modify | Current-phase paragraph: "3M-3a-iv anti-VOX feed wired" |

---

## Source-first dispatch protocol (read once; referenced by every task)

Every implementer agent dispatched against this plan MUST receive this preamble in their prompt (per `feedback_subagent_thetis_source_first.md`):

```
Thetis source path: /Users/j.j.boyd/Thetis/Project Files/Source/
Thetis stamp:       v2.10.3.13 (@501e3f51, 7 commits past tag, all docs-only)
WDSP source path:   /Users/j.j.boyd/Thetis/Project Files/Source/wdsp/
                    (mirror at third_party/wdsp/src/ in NereusSDR tree)
ChannelMaster path: /Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/
                    (NOT ported; reference-only for understanding aamix routing)

Protocol per CLAUDE.md: READ -> SHOW -> TRANSLATE.
For every WDSP wrapper, model property, or UI surface this task creates:
  1. READ the Thetis source line(s) cited in the task.
  2. READ the WDSP impl in wdsp/dexp.c for the function semantics.
  3. READ the Thetis call-site (setup.cs handler) for unit conversions and value-change patterns.
  4. SHOW each verbatim in the commit message body before writing C++.
  5. TRANSLATE faithfully, preserving:
     - Every inline author tag (//DH1KLM, //MW0LGE, //W2PA, etc.) within +/-5 source lines.
     - Byte-for-byte license header if creating a new .cpp/.h file (template: docs/attribution/HOW-TO-PORT.md).
     - Version stamp `[v2.10.3.13]` on every new `// From Thetis ...` cite.

STOP AND ASK if you cannot locate a Thetis source for any value, range,
default, behavior, or attribution.  Do not invent.  Do not infer.

Self-review checklist before requesting commit:
  - [ ] Thetis source quoted in commit message body for every wrapper / property.
  - [ ] Every inline author tag from upstream preserved verbatim within +/-10 port lines.
  - [ ] Version stamp `[v2.10.3.13]` on every new `// From Thetis ...` cite.
  - [ ] All constants/ranges traceable to a specific Thetis line.
  - [ ] No new file created without a verbatim upstream header.
  - [ ] AppSettings keys follow the existing `AntiVox_*` underscore-separated convention.
```

This phase creates **no new ported source files** (only existing-file edits, model additions, and tests). The header-attribution rule applies only if any task introduces a brand-new file. Watch the verification matrix doc at Task 11; that one is NereusSDR-original, no Thetis header needed.

---

## Task 1: WDSP API declarations

**Files:**
- Modify: `src/core/wdsp_api.h:1029-1038` (add 4 declarations after existing `SetAntiVOXGain` block)

**Source-first reads required before coding:**
- `third_party/wdsp/src/dexp.c:666-714` (the four entry-point implementations)
- `Project Files/Source/Console/cmaster.cs:208-222` (the C# DllImport block to confirm signature shapes)

**Self-contained: yes** (declarations only; no behavioral change).

- [ ] **Step 1: Read the WDSP source for the four entry points**

```bash
sed -n '666,675p;677,686p;697,706p;708,720p' third_party/wdsp/src/dexp.c
```

Expected: confirms signatures
- `void SetAntiVOXSize(int id, int size)`
- `void SetAntiVOXRate(int id, double rate)`
- `void SetAntiVOXDetectorTau(int id, double tau)`
- `void SendAntiVOXData(int id, int nsamples, double* data)` (note: `nsamples` is unused; buffer must be exactly `antivox_size` complex samples)

- [ ] **Step 2: Add the declarations**

Edit `src/core/wdsp_api.h` immediately after the existing `void SetAntiVOXGain(int id, double gain);` line at line 1038:

```cpp
// Anti-VOX detector dimensions, smoothing tau, and audio feed.
// From Thetis dexp.c:666 [v2.10.3.13]. antivox_size: complex-sample buffer
//   size; setting it forces realloc of antivox_data + recompute of antivox_mult.
void SetAntiVOXSize(int id, int size);

// From Thetis dexp.c:677 [v2.10.3.13]. antivox_rate: sample rate of incoming
//   anti-vox data, used to recompute antivox_mult = exp(-1/(rate*tau)).
void SetAntiVOXRate(int id, double rate);

// From Thetis dexp.c:697 [v2.10.3.13]. antivox_tau: smoothing time-constant
//   in seconds.  Setup page passes ms/1000.0 (setup.cs:18995).
void SetAntiVOXDetectorTau(int id, double tau);

// From Thetis dexp.c:708 [v2.10.3.13]. Push one block of L/R audio (as
//   complex pairs) into DEXP's anti-VOX detector.  nsamples is ignored;
//   buffer MUST be exactly antivox_size complex samples wide.
void SendAntiVOXData(int id, int nsamples, double* data);
```

- [ ] **Step 3: Update the file-level "Modification history (NereusSDR)" block**

In the header comment block of `src/core/wdsp_api.h` (around lines 25-32), add a new line under the existing 2026-04-27 / 3M-1b D.3 entry:

```
//   2026-05-07: anti-VOX detector dimension + audio-feed wrappers added by
//               J.J. Boyd (KG4VCF) during 3M-3a-iv (close cancellation
//               feed gap from 3M-3a-iii).
```

- [ ] **Step 4: Verify build still passes**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: build succeeds; no new errors. Declarations alone are harmless even if no caller exists yet.

- [ ] **Step 5: Commit**

```bash
git add src/core/wdsp_api.h
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): declare anti-VOX dimension/feed WDSP wrappers

Adds 4 declarations in wdsp_api.h matching dexp.c [v2.10.3.13] entry points:
  SetAntiVOXSize       (dexp.c:666)
  SetAntiVOXRate       (dexp.c:677)
  SetAntiVOXDetectorTau (dexp.c:697)
  SendAntiVOXData      (dexp.c:708)

Implementation (TxChannel wrappers + RxDspWorker pump) lands in
follow-up tasks.  Closes the cancellation-feed gap left by 3M-3a-iii
(D.3 wired only Run + Gain).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: TxChannel anti-VOX dimension setters (3 of 4 wrappers)

**Files:**
- Modify: `src/core/TxChannel.h` (add 3 method declarations + `m_antiVoxSize` + `m_antiVoxScratch`)
- Modify: `src/core/TxChannel.cpp` (3 implementations near existing `setAntiVoxRun` at line 1674)
- Modify: `tests/tst_tx_channel_vox_anti_vox.cpp` (extend with 3 setter cases)

**Source-first reads required before coding:**
- `third_party/wdsp/src/dexp.c:666-706` (semantics for each setter)

**Self-contained: yes** (no caller yet; setters are idempotent shell).

- [ ] **Step 1: Write failing tests for the 3 setters**

Open `tests/tst_tx_channel_vox_anti_vox.cpp` and add three new test cases inside the existing test class (find the existing `setAntiVoxRun` / `setAntiVoxGain` test block to mirror the pattern):

```cpp
void setAntiVoxSize_callsWdsp()
{
    // Verifies SetAntiVOXSize is invoked with the size value, and the
    // internal m_antiVoxSize tracks the value for the size-mismatch guard.
    // From Thetis dexp.c:666 [v2.10.3.13].
    TxChannel ch(/*channelId=*/0, /*inputBufferSize=*/1024,
                 /*inputSampleRate=*/48000.0, nullptr);
    ch.setAntiVoxSize(2048);
    QCOMPARE(ch.antiVoxSize(), 2048);  // accessor for testability
}

void setAntiVoxRate_callsWdsp()
{
    // Verifies SetAntiVOXRate is invoked with the rate value.
    // From Thetis dexp.c:677 [v2.10.3.13].
    TxChannel ch(0, 1024, 48000.0, nullptr);
    ch.setAntiVoxRate(96000.0);
    // No state to inspect on success path; just ensure no crash and the
    // accessor (if added) returns the cached value.
    QCOMPARE(ch.antiVoxRate(), 96000.0);
}

void setAntiVoxDetectorTau_callsWdsp()
{
    // Verifies SetAntiVOXDetectorTau is invoked with the tau in seconds.
    // From Thetis dexp.c:697 [v2.10.3.13].
    // Thetis converts ms->s via /1000.0 in setup.cs:18995; this wrapper
    // takes seconds directly (MoxController does the ms->s conversion).
    TxChannel ch(0, 1024, 48000.0, nullptr);
    ch.setAntiVoxDetectorTau(0.020);
    QCOMPARE(ch.antiVoxDetectorTau(), 0.020);
}
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target tst_tx_channel_vox_anti_vox -j$(nproc) 2>&1 | tail -10
```

Expected: build fails with "no member named 'setAntiVoxSize'" / "antiVoxSize" / similar.

- [ ] **Step 3: Add declarations to `TxChannel.h`**

In `src/core/TxChannel.h`, find the existing `setAntiVoxRun(bool run)` and `setAntiVoxGain(double gain)` declarations (around line 1674's analog in the header). Add the three new declarations directly after them:

```cpp
// Anti-VOX detector dimension setters.  Drive WDSP DEXP block to align
// with the post-decimation RX block geometry (out_size / out_rate).
// From Thetis cmaster.c:154-155 [v2.10.3.13]: DEXP's antivox_size /
// antivox_rate are sourced from audio_outsize / audio_outrate, NOT from
// TX in_size / in_rate.  In NereusSDR's single-RX path, audio_outsize ==
// RxDspWorker::outSize and audio_outrate == post-decimation panel rate.
void setAntiVoxSize(int size);
void setAntiVoxRate(double rate);
void setAntiVoxDetectorTau(double seconds);

// Test-side accessors for the cached sizing values.  Read-only from the
// non-DSP thread is safe; the underlying ints are written only on the
// TX worker thread which is the same thread as TxChannel's setters.
int    antiVoxSize() const noexcept       { return m_antiVoxSize; }
double antiVoxRate() const noexcept       { return m_antiVoxRate; }
double antiVoxDetectorTau() const noexcept { return m_antiVoxTauSec; }
```

In the private members block (find `m_antiVoxRun` or similar; if absent, add near the other DEXP member cache):

```cpp
// Anti-VOX detector dimension cache.  m_antiVoxSize is consulted by
// sendAntiVoxData() to reject size-mismatched buffers without invoking
// SendAntiVOXData (which would memcpy past the end of antivox_data).
// Initialized to 0 so that the first sendAntiVoxData call before
// setAntiVoxSize is rejected with a single qCWarning.
int    m_antiVoxSize{0};
double m_antiVoxRate{0.0};
double m_antiVoxTauSec{0.020};                  // matches WDSP create-time default
std::vector<double> m_antiVoxScratch;           // float->double conversion
                                                // resized on setAntiVoxSize
```

- [ ] **Step 4: Add implementations to `TxChannel.cpp`**

In `src/core/TxChannel.cpp` immediately after the existing `setAntiVoxRun` definition at line 1674, add:

```cpp
// ---------------------------------------------------------------------------
// setAntiVoxSize()
// From Thetis dexp.c:666 [v2.10.3.13]: SetAntiVOXSize reallocs antivox_data
// to size complex samples and recomputes antivox_mult via decalc/calc.
// We mirror the WDSP block's internal size in m_antiVoxSize for the
// sendAntiVoxData() runtime guard, and pre-size m_antiVoxScratch to avoid
// per-call allocation in the audio path.
// ---------------------------------------------------------------------------
void TxChannel::setAntiVoxSize(int size)
{
    if (size <= 0) {
        qCWarning(lcDsp) << "TxChannel::setAntiVoxSize: rejecting non-positive size" << size;
        return;
    }
    m_antiVoxSize = size;
    m_antiVoxScratch.resize(static_cast<std::size_t>(2 * size));  // I and Q
#ifdef HAVE_WDSP
    ::SetAntiVOXSize(m_channelId, size);
#endif
}

// ---------------------------------------------------------------------------
// setAntiVoxRate()
// From Thetis dexp.c:677 [v2.10.3.13]: SetAntiVOXRate updates antivox_rate
// and recomputes antivox_mult so the smoothing tau remains correct in
// absolute seconds.
// ---------------------------------------------------------------------------
void TxChannel::setAntiVoxRate(double rate)
{
    if (rate <= 0.0) {
        qCWarning(lcDsp) << "TxChannel::setAntiVoxRate: rejecting non-positive rate" << rate;
        return;
    }
    m_antiVoxRate = rate;
#ifdef HAVE_WDSP
    ::SetAntiVOXRate(m_channelId, rate);
#endif
}

// ---------------------------------------------------------------------------
// setAntiVoxDetectorTau()
// From Thetis dexp.c:697 [v2.10.3.13]: SetAntiVOXDetectorTau updates the
// smoothing tau and recomputes antivox_mult.
// Thetis call-site converts the spinbox ms value via /1000.0
// (setup.cs:18995).  This wrapper takes seconds directly.
// ---------------------------------------------------------------------------
void TxChannel::setAntiVoxDetectorTau(double seconds)
{
    if (seconds <= 0.0) {
        qCWarning(lcDsp) << "TxChannel::setAntiVoxDetectorTau: rejecting non-positive tau" << seconds;
        return;
    }
    m_antiVoxTauSec = seconds;
#ifdef HAVE_WDSP
    ::SetAntiVOXDetectorTau(m_channelId, seconds);
#endif
}
```

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target tst_tx_channel_vox_anti_vox -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_tx_channel_vox_anti_vox -V 2>&1 | tail -20
```

Expected: build succeeds, all anti-VOX tests in this executable PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/TxChannel.h src/core/TxChannel.cpp tests/tst_tx_channel_vox_anti_vox.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): TxChannel anti-VOX dimension setters

Adds setAntiVoxSize / setAntiVoxRate / setAntiVoxDetectorTau wrappers
mirroring Thetis dexp.c:666-706 [v2.10.3.13].  Size setter pre-sizes
m_antiVoxScratch (float->double conversion buffer used by sendAntiVoxData
in the next task) to avoid per-call allocation.  Tau setter takes
seconds directly; ms->s conversion lives at the Setup page handler per
Thetis setup.cs:18995 [v2.10.3.13].

Tests: tst_tx_channel_vox_anti_vox extended with 3 cases.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: TxChannel `sendAntiVoxData` with size-mismatch guard

**Files:**
- Modify: `src/core/TxChannel.h` (add `sendAntiVoxData` declaration)
- Modify: `src/core/TxChannel.cpp` (implementation)
- Modify: `tests/tst_tx_channel_vox_anti_vox.cpp` (extend with 3 cases)

**Source-first reads required:**
- `third_party/wdsp/src/dexp.c:708-715` (the SendAntiVOXData entry-point body)

**Self-contained: yes** (no caller yet; runtime path tested via direct call from test).

- [ ] **Step 1: Write failing tests**

Add to `tests/tst_tx_channel_vox_anti_vox.cpp`:

```cpp
void sendAntiVoxData_pumpsWdsp_whenSizeMatches()
{
    // Verifies SendAntiVOXData is invoked when nsamples == m_antiVoxSize.
    // We can't intercept the WDSP call directly, but we can verify the
    // float->double conversion path by inspecting m_antiVoxScratch via
    // an accessor.
    TxChannel ch(0, 1024, 48000.0, nullptr);
    ch.setAntiVoxSize(4);  // deliberately tiny for test brevity
    const float interleaved[8] = { 0.1f, 0.2f, 0.3f, 0.4f,
                                   0.5f, 0.6f, 0.7f, 0.8f };
    ch.sendAntiVoxData(interleaved, /*nsamples=*/4);
    // Inspect the conversion via accessor (added in this task).
    const auto& scratch = ch.antiVoxScratchForTest();
    QCOMPARE(scratch.size(), std::size_t{8});
    for (std::size_t i = 0; i < 8; ++i) {
        QCOMPARE(scratch[i], static_cast<double>(interleaved[i]));
    }
}

void sendAntiVoxData_skipsWdsp_whenSizeMismatches()
{
    // Verifies the size-mismatch guard rejects mismatched buffers and
    // does NOT touch m_antiVoxScratch (no partial conversion).
    TxChannel ch(0, 1024, 48000.0, nullptr);
    ch.setAntiVoxSize(4);
    const float interleaved[10] = {};  // 5 stereo samples instead of 4
    ch.sendAntiVoxData(interleaved, /*nsamples=*/5);
    // Scratch should be sized 8 (from setAntiVoxSize(4)) but unmodified.
    const auto& scratch = ch.antiVoxScratchForTest();
    QCOMPARE(scratch.size(), std::size_t{8});
    for (double v : scratch) {
        QCOMPARE(v, 0.0);  // initial zero-fill from std::vector<double>
    }
}

void sendAntiVoxData_skipsWdsp_whenSizeUnconfigured()
{
    // First call with m_antiVoxSize == 0 (no setAntiVoxSize) must reject
    // rather than memcpy into a zero-sized antivox_data buffer.
    TxChannel ch(0, 1024, 48000.0, nullptr);
    const float interleaved[2] = { 1.0f, 1.0f };
    ch.sendAntiVoxData(interleaved, /*nsamples=*/1);
    // No crash; scratch stays empty.
    const auto& scratch = ch.antiVoxScratchForTest();
    QCOMPARE(scratch.size(), std::size_t{0});
}
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target tst_tx_channel_vox_anti_vox -j$(nproc) 2>&1 | tail -10
```

Expected: build fails with "no member named 'sendAntiVoxData'" / "antiVoxScratchForTest".

- [ ] **Step 3: Add declaration to `TxChannel.h`**

After the three setters added in Task 2, append:

```cpp
// Push one block of interleaved L/R float audio into the WDSP DEXP
// anti-VOX detector.  Buffer size MUST match the most-recent
// setAntiVoxSize() value or the call is rejected with qCWarning(lcDsp)
// (no partial memcpy into antivox_data).
//
// Conversion: float -> double, in-order, into m_antiVoxScratch.  No
// allocation in the audio path provided setAntiVoxSize was called once.
//
// From Thetis dexp.c:708-715 [v2.10.3.13]: SendAntiVOXData ignores its
// nsamples arg and memcpys exactly antivox_size complex samples.
void sendAntiVoxData(const float* interleaved, int nsamples);

// Test accessor.  NEVER consume in production code; the m_antiVoxScratch
// lifetime is managed exclusively on the TX worker thread.
const std::vector<double>& antiVoxScratchForTest() const { return m_antiVoxScratch; }
```

- [ ] **Step 4: Add implementation to `TxChannel.cpp`**

After the three setter definitions added in Task 2:

```cpp
// ---------------------------------------------------------------------------
// sendAntiVoxData()
// From Thetis dexp.c:708-715 [v2.10.3.13]:
//
//   void SendAntiVOXData (int id, int nsamples, double* data)
//   {
//       DEXP a = txa[id].dexp.p;
//       memcpy (a->antivox_data, data, a->antivox_size * sizeof (complex));
//       a->antivox_new = 1;
//   }
//
// The nsamples argument is unused by Thetis; the memcpy length is fixed
// at antivox_size complex samples.  We enforce nsamples == m_antiVoxSize
// here so a caller mismatch logs and skips rather than corrupting memory.
//
// In NereusSDR the equivalent of Thetis ChannelMaster aamix is the direct
// RxDspWorker -> TxWorkerThread queued connection.  See
// docs/architecture/phase3m-3a-iv-antivox-feed-design.md §4.2.
// ---------------------------------------------------------------------------
void TxChannel::sendAntiVoxData(const float* interleaved, int nsamples)
{
    if (m_antiVoxSize <= 0) {
        // Pre-configuration call.  Log once-per-channel via static guard
        // would be tidier, but a per-call qCWarning at qInfo verbosity is
        // acceptable: this only fires before the first setAntiVoxSize.
        qCWarning(lcDsp) << "TxChannel::sendAntiVoxData: m_antiVoxSize unset, ignoring"
                         << "channel" << m_channelId;
        return;
    }
    if (nsamples != m_antiVoxSize) {
        qCWarning(lcDsp) << "TxChannel::sendAntiVoxData: size mismatch,"
                         << nsamples << "vs expected" << m_antiVoxSize
                         << "channel" << m_channelId;
        return;
    }
    // Float -> double conversion into the resident scratch buffer.  Size
    // 2*nsamples to cover I+Q.
    const std::size_t total = static_cast<std::size_t>(2 * nsamples);
    for (std::size_t i = 0; i < total; ++i) {
        m_antiVoxScratch[i] = static_cast<double>(interleaved[i]);
    }
#ifdef HAVE_WDSP
    ::SendAntiVOXData(m_channelId, nsamples, m_antiVoxScratch.data());
#endif
}
```

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target tst_tx_channel_vox_anti_vox -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_tx_channel_vox_anti_vox -V 2>&1 | tail -20
```

Expected: all three new cases PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/TxChannel.h src/core/TxChannel.cpp tests/tst_tx_channel_vox_anti_vox.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): TxChannel::sendAntiVoxData + size-mismatch guard

Pumps interleaved L/R float audio into WDSP DEXP anti-VOX detector via
SendAntiVOXData (dexp.c:708 [v2.10.3.13]).  Enforces nsamples ==
m_antiVoxSize at runtime; mismatched calls log qCWarning(lcDsp) and skip
rather than memcpy past antivox_data's allocation.  Pre-configuration
call (m_antiVoxSize == 0) also rejected.

Float -> double conversion lives in m_antiVoxScratch (resident, sized
on setAntiVoxSize) so no allocation occurs in the audio path.

Tests: tst_tx_channel_vox_anti_vox extended with 3 cases (happy path,
size-mismatch, unconfigured).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: WdspEngine anti-VOX tau default fix (10 ms → 20 ms)

**Files:**
- Modify: `src/core/WdspEngine.cpp:593` (single constant change)

**Source-first reads required:**
- `Project Files/Source/Console/setup.designer.cs:44682-44686` (verify default 20)

**Self-contained: yes** (one-line edit; no API surface change).

- [ ] **Step 1: Read the Thetis default**

```bash
sed -n '44680,44690p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/setup.designer.cs"
```

Expected: confirms `udAntiVoxTau.Value = new decimal(new int[] { 20, 0, 0, 0 });` → 20 ms.

- [ ] **Step 2: Edit `src/core/WdspEngine.cpp`**

Find the `create_dexp` call (currently around line 587). The arg list ends with the anti-vox params; the `tau` argument is the very last positional. Replace:

```cpp
        0,                        // anti-vox 'run' flag
        inputBufferSize,          // anti-vox data buffer size (complex samples)
        inputSampleRate,          // anti-vox data sample-rate
        0.01,                     // anti-vox gain
        0.01);                    // anti-vox smoothing time-constant
```

with:

```cpp
        0,                        // anti-vox 'run' flag
        inputBufferSize,          // anti-vox data buffer size (placeholder; overridden
                                  // by setAntiVoxSize once RxDspWorker geometry is known)
        inputSampleRate,          // anti-vox data sample-rate (placeholder; overridden
                                  // by setAntiVoxRate at the same point)
        0.01,                     // anti-vox gain (linear, -40 dB; overridden by
                                  // udAntiVoxGain handler at first paint)
        // From Thetis setup.designer.cs:44682-44686 [v2.10.3.13]:
        //   udAntiVoxTau default 20 ms (= 0.02 s).  Thetis ChannelMaster
        //   cmaster.c:157 passes 0.01 at create time but the spinbox-applied
        //   value overrides it on every Thetis startup; we initialize at the
        //   spinbox value so behavior is identical with or without an open
        //   Setup page.
        0.02);                    // anti-vox smoothing time-constant
```

- [ ] **Step 3: Verify build still passes**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/core/WdspEngine.cpp
git commit -S -m "$(cat <<'EOF'
fix(3m-3a-iv): anti-VOX tau default 10ms -> 20ms (Thetis parity)

WdspEngine.cpp passed 0.01 (10 ms) for anti-VOX smoothing tau at
create_dexp time, drift from Thetis udAntiVoxTau.Value=20 (= 0.02 s)
at setup.designer.cs:44682-44686 [v2.10.3.13].  In Thetis the C-side
default 0.01 from cmaster.c:157 is overridden on every startup by the
spinbox handler; NereusSDR doesn't have that auto-override yet, so a
fresh user with no Setup page interaction got the wrong default.

Fix: initialize at 0.02 directly so first-run behavior matches Thetis
even before the Setup page paints.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: RxDspWorker `bufferSizesChanged` and `antiVoxSampleReady` signals

**Files:**
- Modify: `src/models/RxDspWorker.h` (2 new signals)
- Modify: `src/models/RxDspWorker.cpp:154` (emit `antiVoxSampleReady`) and `setBufferSizes` (emit `bufferSizesChanged`)
- Modify: `tests/tst_rx_dsp_worker_buffer_sizing.cpp` (extend with `bufferSizesChanged` case)
- Modify: `tests/tst_rx_dsp_worker_thread.cpp` (extend with `antiVoxSampleReady` case)

**Source-first reads required:**
- `Project Files/Source/ChannelMaster/cmaster.c:159-175` (aamix-pattern reference for the cite comment)

**Self-contained: yes** (signals fire even with no consumer; harmless).

- [ ] **Step 1: Write failing test for `bufferSizesChanged`**

In `tests/tst_rx_dsp_worker_buffer_sizing.cpp`, add inside the existing test class:

```cpp
void setBufferSizes_emitsBufferSizesChanged()
{
    // Verifies that setBufferSizes(in, out) fires the bufferSizesChanged
    // signal with (out, sampleRate).  Consumed by TxWorkerThread to align
    // DEXP anti-VOX detector dimensions with RX block geometry.
    RxDspWorker worker;
    worker.setSampleRate(48000.0);  // pre-existing API
    QSignalSpy spy(&worker, &RxDspWorker::bufferSizesChanged);

    worker.setBufferSizes(2048, 1024);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1024);
    QCOMPARE(args.at(1).toDouble(), 48000.0);
}

void setBufferSizes_idempotent_noDuplicateEmit()
{
    RxDspWorker worker;
    worker.setSampleRate(48000.0);
    worker.setBufferSizes(2048, 1024);
    QSignalSpy spy(&worker, &RxDspWorker::bufferSizesChanged);

    worker.setBufferSizes(2048, 1024);  // identical

    QCOMPARE(spy.count(), 0);
}
```

If `setSampleRate` does not exist on RxDspWorker, instead snapshot the rate via the existing constructor / setEngines path; check `src/models/RxDspWorker.h` for the actual API and adapt the test accordingly.

- [ ] **Step 2: Write failing test for `antiVoxSampleReady`**

In `tests/tst_rx_dsp_worker_thread.cpp`, add inside the existing test class (this test exercises `processIqBatch` end-to-end):

```cpp
void processIqBatch_emitsAntiVoxSampleReady_perChunk()
{
    // Verifies the antiVoxSampleReady signal fires once per drained chunk
    // alongside the existing rxBlockReady call.  Payload must match the
    // same interleaved L/R buffer that flows to AudioEngine.
    RxDspWorker worker;
    worker.setBufferSizes(/*in=*/4, /*out=*/2);  // tiny for test brevity

    QSignalSpy antiVoxSpy(&worker, &RxDspWorker::antiVoxSampleReady);

    // Feed 4 stereo I/Q samples (matches inSize=4) so processIqBatch
    // drains exactly one chunk.
    QVector<float> iq{ 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f };
    worker.processIqBatch(/*receiverIndex=*/0, iq);

    QCOMPARE(antiVoxSpy.count(), 1);
    const auto args = antiVoxSpy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);                  // sliceId
    const auto interleaved = args.at(1).value<QVector<float>>();
    QCOMPARE(interleaved.size(), 4);                  // 2 stereo samples = 4 floats
    QCOMPARE(args.at(2).toInt(), 2);                  // sampleCount (out_size)
}
```

Note: this test runs without WDSP wired (no `m_wdspEngine` / `m_audioEngine`), so the existing `processIqBatch` path skips the inner block and emits only `chunkDrained` and `batchProcessed`. **The new signal must fire on the inner-block path with WDSP wired**, so this test pattern needs the existing fake-WDSP harness. If `tst_rx_dsp_worker_thread.cpp` already sets up fake engines, mirror that pattern. If not, the test belongs in `tst_rx_dsp_worker_buffer_sizing.cpp` style (signal-only, no full wiring) and the assertion changes to "fires when wired; doesn't fire when not wired."

If neither test fixture supports a wired `m_audioEngine`, defer this assertion to the integration verification matrix in Task 11 and replace with a compile-only check that the signal exists.

- [ ] **Step 3: Run tests, verify they fail**

```bash
cmake --build build --target tst_rx_dsp_worker_buffer_sizing tst_rx_dsp_worker_thread -j$(nproc) 2>&1 | tail -10
```

Expected: build fails with "no member named 'bufferSizesChanged'" / "antiVoxSampleReady".

- [ ] **Step 4: Add signals to `RxDspWorker.h`**

In the signals block of `src/models/RxDspWorker.h` (next to the existing `chunkDrained` / `batchProcessed`):

```cpp
// Emitted whenever setBufferSizes() changes the (inSize, outSize) pair.
// Consumed by TxWorkerThread::setAntiVoxBlockGeometry to align DEXP's
// anti-VOX detector dimensions with the post-decimation RX block.
//
// Payload: (outSize_complexSamples, outRate_Hz).
//
// From Thetis ChannelMaster cmaster.c:159-175 [v2.10.3.13]: aamix is
// configured with audio_outsize / audio_outrate, which in NereusSDR's
// single-RX path correspond to RxDspWorker::outSize and the post-
// decimation panel rate.
void bufferSizesChanged(int outSize, double outRate);

// Emitted per drained chunk after the existing rxBlockReady delivery.
// Consumed by TxWorkerThread::onAntiVoxSamplesReady to feed WDSP DEXP.
//
// Payload: (sliceId, interleaved L/R float buffer, sampleCount).
//
// From Thetis ChannelMaster cmaster.c:171 [v2.10.3.13]: aamix's
// SendAntiVOXData callback delivers exactly one audio_outsize block at
// audio_outrate.  Single-RX equivalent here.
void antiVoxSampleReady(int sliceId, const QVector<float>& interleaved, int sampleCount);
```

Idempotency state for `bufferSizesChanged` (avoid spamming on identical calls): add private member alongside the existing `m_inSize` / `m_outSize` atomics:

```cpp
// Tracks last-emitted (in, out) pair; bufferSizesChanged fires only on change.
int m_lastEmittedInSize{-1};
int m_lastEmittedOutSize{-1};
```

- [ ] **Step 5: Wire emission in `RxDspWorker.cpp`**

In `setBufferSizes` (currently around line 87):

```cpp
void RxDspWorker::setBufferSizes(int inSize, int outSize)
{
    m_inSize.store(inSize, std::memory_order_relaxed);
    m_outSize.store(outSize, std::memory_order_relaxed);

    // Emit only on change to avoid spamming TxWorkerThread::setAntiVoxBlockGeometry
    // with no-op SetAntiVOXSize/SetAntiVOXRate calls.
    if (inSize != m_lastEmittedInSize || outSize != m_lastEmittedOutSize) {
        m_lastEmittedInSize  = inSize;
        m_lastEmittedOutSize = outSize;
        // Emit the post-decimation RX-side geometry.  Sample rate comes
        // from m_sampleRate (set by setSampleRate) which mirrors the
        // panel-side WDSP rate.
        emit bufferSizesChanged(outSize, m_sampleRate);
    }
}
```

(If `m_sampleRate` doesn't exist as a member, source it from wherever the existing `setSampleRate` writes, or pass it as a third arg to `setBufferSizes` and update callers.)

In `processIqBatch` immediately after the existing `m_audioEngine->rxBlockReady(0, interleaved, outSize)` call at line 154:

```cpp
            m_audioEngine->rxBlockReady(0, interleaved, outSize);

            // Phase 3M-3a-iv: fork the same buffer to TxWorkerThread for
            // anti-VOX detector feed.  Slice 0 is the only consumer in
            // single-RX; multi-RX mux lands with 3F.
            //
            // From Thetis ChannelMaster cmaster.c:159-175 [v2.10.3.13]:
            // single-RX equivalent of pavoxmix -> SendAntiVOXData.
            //
            // QVector<float> deep-copies on cross-thread queued connection,
            // which keeps m_interleavedOut owned by this thread.
            QVector<float> antiVoxBuffer(interleaved, interleaved + outSize * 2);
            emit antiVoxSampleReady(0, antiVoxBuffer, outSize);
```

- [ ] **Step 6: Run tests, verify they pass**

```bash
cmake --build build --target tst_rx_dsp_worker_buffer_sizing tst_rx_dsp_worker_thread -j$(nproc) 2>&1 | tail -10
ctest --test-dir build -R "tst_rx_dsp_worker" -V 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add src/models/RxDspWorker.h src/models/RxDspWorker.cpp \
        tests/tst_rx_dsp_worker_buffer_sizing.cpp \
        tests/tst_rx_dsp_worker_thread.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): RxDspWorker bufferSizesChanged + antiVoxSampleReady signals

Adds two queued cross-thread signals consumed by TxWorkerThread to wire
WDSP DEXP anti-VOX detector to the RX audio path.

  bufferSizesChanged(outSize, outRate): fires from setBufferSizes when
  geometry changes.  Drives SetAntiVOXSize / SetAntiVOXRate on TX side.

  antiVoxSampleReady(sliceId, interleaved, sampleCount): fires per
  drained chunk in processIqBatch, alongside the existing rxBlockReady
  call.  Drives TxChannel::sendAntiVoxData on TX side.

Single-RX equivalent of Thetis ChannelMaster aamix
(cmaster.c:159-175 [v2.10.3.13]): aamix's SendAntiVOXData callback is
replaced by the queued signal -> slot wire because we have one RX, no
resample, and one source.  When 3F multi-pan ships, the connection is
replaced by an aamix port; this plumbing carries over unchanged.

Tests: tst_rx_dsp_worker_buffer_sizing + tst_rx_dsp_worker_thread
extended.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: TxWorkerThread anti-VOX slots and atomic gate

**Files:**
- Modify: `src/core/TxWorkerThread.h` (3 new slots, `m_antiVoxRun` atomic)
- Modify: `src/core/TxWorkerThread.cpp` (slot bodies, mirror existing setAntiVoxRun to flip atomic)
- Modify: `tests/tst_tx_worker_thread.cpp` (extend with 3 cases)

**Source-first reads required:**
- `Project Files/Source/wdsp/dexp.c:288-297` (gate condition: anti-vox calc only runs in DEXP_LOW state with antivox_new != 0)

**Self-contained: yes** (TxChannel methods exist from Tasks 2-3; no UI yet).

- [ ] **Step 1: Write failing tests**

Open `tests/tst_tx_worker_thread.cpp` and add three cases mirroring existing `setAntiVoxRun` test patterns:

```cpp
void onAntiVoxSamplesReady_skipsTxChannel_whenAntiVoxOff()
{
    // m_antiVoxRun starts false (default).  onAntiVoxSamplesReady is a
    // no-op until setAntiVoxRun(true) flips it.
    TxWorkerThread thread;
    auto* fakeTx = makeFakeTxChannel();   // pattern from existing tests
    thread.setTxChannel(fakeTx);

    QVector<float> samples{ 0.1f, 0.2f, 0.3f, 0.4f };
    thread.onAntiVoxSamplesReady(0, samples, 2);

    QCOMPARE(fakeTx->sendAntiVoxDataCallCount, 0);
}

void onAntiVoxSamplesReady_callsTxChannel_whenAntiVoxOn()
{
    TxWorkerThread thread;
    auto* fakeTx = makeFakeTxChannel();
    fakeTx->antiVoxSize = 2;          // pre-configured size
    thread.setTxChannel(fakeTx);
    thread.setAntiVoxRun(true);

    QVector<float> samples{ 0.1f, 0.2f, 0.3f, 0.4f };
    thread.onAntiVoxSamplesReady(0, samples, 2);

    QCOMPARE(fakeTx->sendAntiVoxDataCallCount, 1);
    QCOMPARE(fakeTx->sendAntiVoxDataLastSamples, 2);
}

void setAntiVoxBlockGeometry_callsBothSetters()
{
    // setAntiVoxBlockGeometry(outSize, outRate) is the queued slot for
    // RxDspWorker::bufferSizesChanged.  Must call BOTH setAntiVoxSize and
    // setAntiVoxRate so DEXP's antivox_size and antivox_rate stay in sync.
    TxWorkerThread thread;
    auto* fakeTx = makeFakeTxChannel();
    thread.setTxChannel(fakeTx);

    thread.setAntiVoxBlockGeometry(/*outSize=*/1024, /*outRate=*/48000.0);

    QCOMPARE(fakeTx->antiVoxSize, 1024);
    QCOMPARE(fakeTx->antiVoxRate, 48000.0);
}

void setAntiVoxDetectorTau_passesThrough()
{
    TxWorkerThread thread;
    auto* fakeTx = makeFakeTxChannel();
    thread.setTxChannel(fakeTx);

    thread.setAntiVoxDetectorTau(0.020);

    QCOMPARE(fakeTx->antiVoxTauSec, 0.020);
}
```

If the existing `tst_tx_worker_thread.cpp` does not yet have a `makeFakeTxChannel()` helper, model it on whatever fake-TX pattern is in use (see how `setVoxRun` / `setVoxHangTime` is already tested in that file, and reuse its fixture).

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target tst_tx_worker_thread -j$(nproc) 2>&1 | tail -10
```

Expected: "no member named 'onAntiVoxSamplesReady'" / "setAntiVoxBlockGeometry" / "setAntiVoxDetectorTau".

- [ ] **Step 3: Add slots and atomic to `TxWorkerThread.h`**

Inside the public slots block, after the existing `setAntiVoxRun(bool)` and `setAntiVoxGain(double)` slots:

```cpp
// onAntiVoxSamplesReady: receive RX audio fork from RxDspWorker and
// pump it into TxChannel::sendAntiVoxData when anti-VOX is enabled.
// Gates on m_antiVoxRun (acquire) so the float->double conversion in
// TxChannel is skipped when the user has anti-VOX off.
//
// From Thetis ChannelMaster cmaster.c:159-175 [v2.10.3.13]: single-RX
// equivalent of aamix's SendAntiVOXData callback.
void onAntiVoxSamplesReady(int sliceId, const QVector<float>& interleaved, int sampleCount);

// setAntiVoxBlockGeometry: queued slot for RxDspWorker::bufferSizesChanged.
// Calls both TxChannel::setAntiVoxSize and TxChannel::setAntiVoxRate so
// DEXP's antivox_size and antivox_rate stay aligned with the RX-side
// post-decimation block.
//
// From Thetis ChannelMaster cmaster.c:154-155 [v2.10.3.13].
void setAntiVoxBlockGeometry(int outSize, double outRate);

// setAntiVoxDetectorTau: queued slot for MoxController::antiVoxDetectorTauRequested.
// Pass-through to TxChannel::setAntiVoxDetectorTau (which calls WDSP
// SetAntiVOXDetectorTau).  Tau here is in seconds; ms->s conversion
// already done by MoxController.
//
// From Thetis setup.cs:18992-18996 [v2.10.3.13].
void setAntiVoxDetectorTau(double seconds);
```

In the existing `setAntiVoxRun(bool)` body (or wherever the queued setter forwards to TxChannel), add the atomic-mirror line so the gate stays in sync:

```cpp
void TxWorkerThread::setAntiVoxRun(bool run)
{
    // ... existing code that calls m_txChannel->setAntiVoxRun(run) ...
    m_antiVoxRun.store(run, std::memory_order_release);  // gate for onAntiVoxSamplesReady
}
```

In the private members block:

```cpp
// Mirrors the most-recent setAntiVoxRun(bool) call.  Read with acquire
// in onAntiVoxSamplesReady, written with release in setAntiVoxRun.
// Default false matches the existing TxChannel::m_antiVoxRun default.
std::atomic<bool> m_antiVoxRun{false};
```

- [ ] **Step 4: Implement slot bodies in `TxWorkerThread.cpp`**

After the existing `setAntiVoxGain` definition (around the line range cited in the file's top-comment block at lines 161-162):

```cpp
// ---------------------------------------------------------------------------
// onAntiVoxSamplesReady()
// Queued slot from RxDspWorker::antiVoxSampleReady.
// Single-RX equivalent of Thetis cmaster.c:171 [v2.10.3.13]: aamix's
// SendAntiVOXData callback.  Skipped when anti-VOX is off (acquire-load
// of m_antiVoxRun) to avoid the float->double conversion cost in
// TxChannel::sendAntiVoxData.
// ---------------------------------------------------------------------------
void TxWorkerThread::onAntiVoxSamplesReady(int sliceId,
                                           const QVector<float>& interleaved,
                                           int sampleCount)
{
    if (!m_antiVoxRun.load(std::memory_order_acquire)) { return; }
    if (m_txChannel == nullptr) { return; }
    if (sliceId != 0) { return; }  // single-RX gate; multi-RX mux is 3F
    m_txChannel->sendAntiVoxData(interleaved.constData(), sampleCount);
}

// ---------------------------------------------------------------------------
// setAntiVoxBlockGeometry()
// Queued slot from RxDspWorker::bufferSizesChanged.
// From Thetis cmaster.c:154-155 [v2.10.3.13]: DEXP's antivox_size /
// antivox_rate follow the audio output block geometry.
// ---------------------------------------------------------------------------
void TxWorkerThread::setAntiVoxBlockGeometry(int outSize, double outRate)
{
    if (m_txChannel == nullptr) { return; }
    m_txChannel->setAntiVoxSize(outSize);
    m_txChannel->setAntiVoxRate(outRate);
}

// ---------------------------------------------------------------------------
// setAntiVoxDetectorTau()
// Queued slot from MoxController::antiVoxDetectorTauRequested.
// From Thetis setup.cs:18992-18996 [v2.10.3.13]: ms->s conversion is
// done at the controller, this slot takes seconds directly.
// ---------------------------------------------------------------------------
void TxWorkerThread::setAntiVoxDetectorTau(double seconds)
{
    if (m_txChannel == nullptr) { return; }
    m_txChannel->setAntiVoxDetectorTau(seconds);
}
```

Update the documentation block at the top of `TxWorkerThread.cpp` (around lines 158-162) to add the new slots to the existing wire-list:

```cpp
//   - 1703-1706  antiVoxGainRequested             → setAntiVoxGain
//   - 1715-1718  antiVoxSourceWhatRequested       → setAntiVoxRun
//   - NEW (3M-3a-iv): antiVoxSampleReady          → onAntiVoxSamplesReady
//   - NEW (3M-3a-iv): bufferSizesChanged          → setAntiVoxBlockGeometry
//   - NEW (3M-3a-iv): antiVoxDetectorTauRequested → setAntiVoxDetectorTau
```

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target tst_tx_worker_thread -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_tx_worker_thread -V 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/core/TxWorkerThread.h src/core/TxWorkerThread.cpp tests/tst_tx_worker_thread.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): TxWorkerThread anti-VOX slots + atomic gate

Adds 3 queued slots:
  onAntiVoxSamplesReady     <- RxDspWorker::antiVoxSampleReady
  setAntiVoxBlockGeometry   <- RxDspWorker::bufferSizesChanged
  setAntiVoxDetectorTau     <- MoxController::antiVoxDetectorTauRequested

m_antiVoxRun atomic mirrors the existing setAntiVoxRun(bool) setter so
onAntiVoxSamplesReady can short-circuit the float->double conversion when
anti-VOX is off.  acquire/release ordering pairs with the existing slot.

Single-RX equivalent of Thetis ChannelMaster aamix output stage
(cmaster.c:159-175 [v2.10.3.13]); RadioModel wires the connection in a
follow-up task.

Tests: tst_tx_worker_thread extended with 4 cases.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: MoxController setAntiVoxTau slot

**Files:**
- Modify: `src/core/MoxController.h` (slot, signal, state, NaN sentinel)
- Modify: `src/core/MoxController.cpp` (implementation, mirroring existing setAntiVoxGain)
- Modify: `tests/tst_mox_controller_anti_vox.cpp` (extend with 6 cases)

**Source-first reads required:**
- `Project Files/Source/Console/setup.cs:18992-18996` (the udAntiVoxTau_ValueChanged handler)
- `Project Files/Source/Console/setup.designer.cs:44661-44688` (range, increment, default)

**Self-contained: yes** (slot is wired in Task 9; signal can fire to no consumer harmlessly).

- [ ] **Step 1: Read the Thetis handler**

```bash
sed -n '18990,18998p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/setup.cs"
```

Expected: `cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);`

- [ ] **Step 2: Write failing tests**

In `tests/tst_mox_controller_anti_vox.cpp`, after the existing `antiVoxGain_*` block (currently around line 138), add:

```cpp
// § D. setAntiVoxTau tests
//
// From Thetis setup.cs:18992-18996 [v2.10.3.13]:
//   private void udAntiVoxTau_ValueChanged(object sender, EventArgs e)
//   {
//       if (!initializing)
//           cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);
//   }
//
// Range from setup.designer.cs:44661-44688:
//   Minimum=1, Maximum=500, Increment=1, default Value=20

void antiVoxTau_default20ms_emits20ThousandthsSecond()
{
    MoxController ctrl;
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

    ctrl.setAntiVoxTau(20);  // ms; default; NaN sentinel forces emit

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.020);
}

void antiVoxTau_msToSeconds_scaling()
{
    MoxController ctrl;
    ctrl.setAntiVoxTau(20);  // prime NaN sentinel
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

    ctrl.setAntiVoxTau(80);  // ms

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.080);
}

void antiVoxTau_idempotent_noDoubleEmit()
{
    MoxController ctrl;
    ctrl.setAntiVoxTau(20);  // prime NaN; emits 0.020
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);

    ctrl.setAntiVoxTau(20);  // same -> no emit

    QCOMPARE(spy.count(), 0);
}

void antiVoxTau_boundary_1ms()
{
    MoxController ctrl;
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
    ctrl.setAntiVoxTau(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.001);
}

void antiVoxTau_boundary_500ms()
{
    MoxController ctrl;
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
    ctrl.setAntiVoxTau(500);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.takeFirst().at(0).toDouble(), 0.500);
}

void antiVoxTau_nanSentinel_firstCallAlwaysEmits()
{
    MoxController ctrl;
    QSignalSpy spy(&ctrl, &MoxController::antiVoxDetectorTauRequested);
    ctrl.setAntiVoxTau(20);  // default; sentinel forces emit
    QCOMPARE(spy.count(), 1);
}
```

- [ ] **Step 3: Run tests, verify they fail**

```bash
cmake --build build --target tst_mox_controller_anti_vox -j$(nproc) 2>&1 | tail -10
```

Expected: "no member named 'setAntiVoxTau'" / "antiVoxDetectorTauRequested".

- [ ] **Step 4: Add slot, signal, state to `MoxController.h`**

In the public slots block, after the existing `setAntiVoxGain(int dB)` and `setAntiVoxSourceVax(bool useVax)` declarations:

```cpp
// setAntiVoxTau: set the anti-VOX detector smoothing time-constant in ms.
//
// Mirrors udAntiVoxTau_ValueChanged from Thetis setup.cs:18992-18996
// [v2.10.3.13]:
//   if (!initializing)
//       cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);
//
// Emits antiVoxDetectorTauRequested(seconds) only when the dB-scaled value
// changes vs the most recent emission (NaN sentinel forces first-call
// emit so the WDSP DEXP block is primed).
//
// Range: clamped to [1, 500] in TransmitModel::setAntiVoxTauMs (the
// upstream caller).  This slot expects clamped input; defensive clamp
// here repeats the work but stays robust.
//
// Connection:
//   TransmitModel::antiVoxTauMsChanged → MoxController::setAntiVoxTau
void setAntiVoxTau(int ms);
```

In the signals block:

```cpp
// antiVoxDetectorTauRequested: emitted when the anti-VOX detector tau
// changes (in seconds, post ms/1000.0 scaling).  Consumed by
// TxWorkerThread::setAntiVoxDetectorTau (queued).
//
// From Thetis setup.cs:18995 [v2.10.3.13]:
//   cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);
void antiVoxDetectorTauRequested(double seconds);
```

In the private members block, near `m_antiVoxGainDb`:

```cpp
// m_antiVoxTauMs: mirrors TransmitModel::antiVoxTauMs().  Range [1,500] ms.
int      m_antiVoxTauMs{20};

// m_lastAntiVoxTauEmitted: NAN sentinel forces first-call emit.  Mirrors
// the m_lastAntiVoxGainEmitted pattern from H.3.
double   m_lastAntiVoxTauEmitted{std::numeric_limits<double>::quiet_NaN()};
```

- [ ] **Step 5: Implement in `MoxController.cpp`**

Locate the existing `setAntiVoxGain` implementation; immediately below it add:

```cpp
// ---------------------------------------------------------------------------
// setAntiVoxTau()
// Mirrors Thetis udAntiVoxTau_ValueChanged at setup.cs:18992-18996
// [v2.10.3.13].  Range [1, 500] ms enforced defensively; primary clamp
// lives at TransmitModel::setAntiVoxTauMs.
// ---------------------------------------------------------------------------
void MoxController::setAntiVoxTau(int ms)
{
    const int clamped = std::clamp(ms, 1, 500);
    const double seconds = static_cast<double>(clamped) / 1000.0;
    if (!std::isnan(m_lastAntiVoxTauEmitted) &&
        seconds == m_lastAntiVoxTauEmitted) {
        return;  // idempotent guard
    }
    m_antiVoxTauMs = clamped;
    m_lastAntiVoxTauEmitted = seconds;
    emit antiVoxDetectorTauRequested(seconds);
}
```

- [ ] **Step 6: Run tests, verify they pass**

```bash
cmake --build build --target tst_mox_controller_anti_vox -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_mox_controller_anti_vox -V 2>&1 | tail -25
```

Expected: all anti-VOX cases including the new 6 tau cases PASS.

- [ ] **Step 7: Commit**

```bash
git add src/core/MoxController.h src/core/MoxController.cpp tests/tst_mox_controller_anti_vox.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): MoxController setAntiVoxTau slot

Mirrors Thetis udAntiVoxTau_ValueChanged at setup.cs:18992-18996
[v2.10.3.13]:
  cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0);

Range [1, 500] ms from setup.designer.cs:44661-44688 [v2.10.3.13].
NaN sentinel forces first-call emit so WDSP DEXP block is primed.
ms->s conversion lives here; TxChannel takes seconds directly.

Tests: tst_mox_controller_anti_vox extended with 6 tau cases (default,
ms->s, idempotent, 1ms boundary, 500ms boundary, NaN-sentinel).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: TransmitModel `antiVoxTauMs` Q_PROPERTY + persistence

**Files:**
- Modify: `src/models/TransmitModel.h` (Q_PROPERTY + range constants + signal + accessor)
- Modify: `src/models/TransmitModel.cpp` (setter with clamp + persistOne, load case in `loadFromSettings(mac)`, save case in `persistToSettings`)
- Modify: `tests/tst_transmit_model_anti_vox.cpp` (extend with 4 cases)

**Source-first reads required:**
- `Project Files/Source/Console/setup.designer.cs:44661-44688` (range and default for the property declaration)

**Self-contained: yes** (property fires `antiVoxTauMsChanged` signal; consumer is wired in Task 9).

- [ ] **Step 1: Write failing tests**

In `tests/tst_transmit_model_anti_vox.cpp`, add:

```cpp
void antiVoxTauMs_default20()
{
    // From Thetis setup.designer.cs:44682 [v2.10.3.13]: udAntiVoxTau.Value=20.
    TransmitModel m;
    QCOMPARE(m.antiVoxTauMs(), 20);
}

void antiVoxTauMs_clampsToRange()
{
    // From Thetis setup.designer.cs:44672 / 44667: Min=1, Max=500.
    TransmitModel m;
    m.setAntiVoxTauMs(0);     QCOMPARE(m.antiVoxTauMs(), 1);
    m.setAntiVoxTauMs(-100);  QCOMPARE(m.antiVoxTauMs(), 1);
    m.setAntiVoxTauMs(501);   QCOMPARE(m.antiVoxTauMs(), 500);
    m.setAntiVoxTauMs(50000); QCOMPARE(m.antiVoxTauMs(), 500);
}

void antiVoxTauMs_changedSignal_idempotentGuard()
{
    TransmitModel m;
    QSignalSpy spy(&m, &TransmitModel::antiVoxTauMsChanged);
    m.setAntiVoxTauMs(50);
    QCOMPARE(spy.count(), 1);
    m.setAntiVoxTauMs(50);  // no change
    QCOMPARE(spy.count(), 1);
}

void antiVoxTauMs_persistsAcrossLoad()
{
    // Round-trip via loadFromSettings(mac) / persistToSettings.
    // Pattern mirrors existing AntiVox_Gain test in this file.
    TransmitModel m1;
    m1.loadFromSettings(QStringLiteral("aa:bb:cc:dd:ee:ff"));
    m1.setAntiVoxTauMs(80);  // auto-persists via persistOne

    TransmitModel m2;
    m2.loadFromSettings(QStringLiteral("aa:bb:cc:dd:ee:ff"));
    QCOMPARE(m2.antiVoxTauMs(), 80);
}
```

- [ ] **Step 2: Run tests, verify they fail**

```bash
cmake --build build --target tst_transmit_model_anti_vox -j$(nproc) 2>&1 | tail -10
```

Expected: "no member named 'antiVoxTauMs'" / "setAntiVoxTauMs" / "antiVoxTauMsChanged".

- [ ] **Step 3: Add Q_PROPERTY, constants, signal, accessor to `TransmitModel.h`**

In the public block, near the existing `antiVoxGainDb` Q_PROPERTY (around line 894-919):

```cpp
// From Thetis setup.designer.cs:44661-44688 [v2.10.3.13]:
//   udAntiVoxTau.Minimum=1, Maximum=500, Increment=1, Value=20
static constexpr int kAntiVoxTauMsMin     = 1;
static constexpr int kAntiVoxTauMsMax     = 500;
static constexpr int kAntiVoxTauMsDefault = 20;

Q_PROPERTY(int antiVoxTauMs READ antiVoxTauMs WRITE setAntiVoxTauMs
                            NOTIFY antiVoxTauMsChanged)

/// Anti-VOX detector smoothing time-constant in ms.
/// Clamped to [kAntiVoxTauMsMin, kAntiVoxTauMsMax].
/// Default kAntiVoxTauMsDefault matches Thetis udAntiVoxTau.Value.
int antiVoxTauMs() const noexcept { return m_antiVoxTauMs; }

public slots:
    void setAntiVoxTauMs(int ms);

signals:
    void antiVoxTauMsChanged(int ms);
```

Place these alongside the existing `antiVoxGainDb` Q_PROPERTY block. Match the visibility / blank-line layout exactly.

In the private members block:

```cpp
int m_antiVoxTauMs{kAntiVoxTauMsDefault};
```

- [ ] **Step 4: Implement setter in `TransmitModel.cpp`**

Find the existing `setAntiVoxGainDb` definition (around line 1770). Below it add:

```cpp
// ---------------------------------------------------------------------------
// setAntiVoxTauMs()
// From Thetis setup.designer.cs:44672 / 44667 [v2.10.3.13]: range [1, 500].
// Auto-persist via persistOne; load handled in loadFromSettings(mac).
// ---------------------------------------------------------------------------
void TransmitModel::setAntiVoxTauMs(int ms)
{
    const int clamped = std::clamp(ms, kAntiVoxTauMsMin, kAntiVoxTauMsMax);
    if (clamped == m_antiVoxTauMs) { return; }
    m_antiVoxTauMs = clamped;
    persistOne(QStringLiteral("AntiVox_Tau_Ms"), QString::number(m_antiVoxTauMs));
    emit antiVoxTauMsChanged(clamped);
}
```

In `loadFromSettings(const QString& mac)` (around line 1349), next to the existing `antiVoxGainDb` and `antiVoxSourceVax` reads:

```cpp
// antiVoxTauMs: default kAntiVoxTauMsDefault from Thetis
// setup.designer.cs:44682 [v2.10.3.13] (udAntiVoxTau.Value=20).
const int antiVoxTauMs = s.value(pfx + QLatin1String("AntiVox_Tau_Ms"),
                                 QString::number(kAntiVoxTauMsDefault))
                          .toInt();
setAntiVoxTauMs(antiVoxTauMs);
```

In `persistToSettings` (around line 1633), next to `AntiVox_Gain` / `AntiVox_Source_VAX`:

```cpp
s.setValue(pfx + QLatin1String("AntiVox_Tau_Ms"), QString::number(m_antiVoxTauMs));
```

- [ ] **Step 5: Run tests, verify they pass**

```bash
cmake --build build --target tst_transmit_model_anti_vox -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_transmit_model_anti_vox -V 2>&1 | tail -20
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add src/models/TransmitModel.h src/models/TransmitModel.cpp tests/tst_transmit_model_anti_vox.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): TransmitModel antiVoxTauMs property + persistence

Q_PROPERTY mirrors Thetis udAntiVoxTau:
  Min=1, Max=500, Default=20  (setup.designer.cs:44661-44688 [v2.10.3.13])

Auto-persist via persistOne under per-MAC key "AntiVox_Tau_Ms" in the
existing AntiVox_* family.  Load default kAntiVoxTauMsDefault when the
key is absent (fresh user) so first-paint matches Thetis behavior.

Tests: tst_transmit_model_anti_vox extended with 4 cases (default,
clamp, idempotent signal, persist round-trip).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: RadioModel signal wiring (4 new connects)

**Files:**
- Modify: `src/models/RadioModel.cpp` (add 4 `connect()` calls inside the existing TX-channel-ready lambda block at line ~2107)

**Source-first reads required:**
- (none; this is wiring, NereusSDR-original code)

**Self-contained: yes** (all endpoints exist from Tasks 5-8).

- [ ] **Step 1: Read the existing TX-channel wire block**

```bash
sed -n '2100,2135p' src/models/RadioModel.cpp
```

Expected: the lambda-wrapped block where existing `antiVoxGainRequested` and `antiVoxSourceWhatRequested` connections live.

- [ ] **Step 2: Add 4 new `connect()` calls**

Inside the same lambda where the existing `antiVoxGainRequested` connect lives (so `m_txChannel` is in scope), append (preserving the existing lambda-capture pattern):

```cpp
// 3M-3a-iv: RxDspWorker::antiVoxSampleReady -> TxWorkerThread::onAntiVoxSamplesReady
//
// Single-RX equivalent of Thetis ChannelMaster aamix output stage
// (cmaster.c:171 [v2.10.3.13]).  Queued so the DSP thread doesn't block
// on TxWorkerThread.
connect(m_dspWorker, &RxDspWorker::antiVoxSampleReady,
        m_txWorkerThread, &TxWorkerThread::onAntiVoxSamplesReady,
        Qt::QueuedConnection);

// 3M-3a-iv: RxDspWorker::bufferSizesChanged -> TxWorkerThread::setAntiVoxBlockGeometry
//
// Aligns DEXP's antivox_size / antivox_rate with the post-decimation RX
// block geometry.  From Thetis cmaster.c:154-155 [v2.10.3.13]:
// audio_outsize / audio_outrate are the canonical anti-VOX detector
// dimensions, not TX in_size / in_rate.
connect(m_dspWorker, &RxDspWorker::bufferSizesChanged,
        m_txWorkerThread, &TxWorkerThread::setAntiVoxBlockGeometry,
        Qt::QueuedConnection);

// 3M-3a-iv: TransmitModel::antiVoxTauMsChanged -> MoxController::setAntiVoxTau
//
// Both objects live on main thread; direct connection.
// Mirrors the existing antiVoxGainDbChanged -> setAntiVoxGain pattern.
connect(m_transmitModel, &TransmitModel::antiVoxTauMsChanged,
        m_moxController,  &MoxController::setAntiVoxTau);

// 3M-3a-iv: MoxController::antiVoxDetectorTauRequested -> TxWorkerThread::setAntiVoxDetectorTau
//
// MoxController emits seconds (post ms/1000.0 conversion); TxWorkerThread
// queued slot pass-through to TxChannel::setAntiVoxDetectorTau.
//
// From Thetis setup.cs:18992-18996 [v2.10.3.13].
connect(m_moxController, &MoxController::antiVoxDetectorTauRequested,
        m_txWorkerThread, &TxWorkerThread::setAntiVoxDetectorTau,
        Qt::QueuedConnection);
```

If the existing block uses a `[m_txChannel]` capture instead of `m_txWorkerThread` directly, mirror that pattern. The wiring goes to `TxWorkerThread` slots (queued) rather than `m_txChannel` directly because TxChannel methods are not slots; the worker thread is the queue boundary.

- [ ] **Step 3: Push the current TM value on connect (initial sync)**

Immediately after the four new connects, prime MoxController with the current TM value so the first WDSP setter runs at `loadFromSettings`-restored state, not at the default:

```cpp
// Initial push: feed MoxController the current TM tau so the first
// emission of antiVoxDetectorTauRequested aligns DEXP with whatever
// AppSettings restored.  The NaN sentinel inside MoxController forces
// the emit even if the value matches its default.
m_moxController->setAntiVoxTau(m_transmitModel->antiVoxTauMs());
```

- [ ] **Step 4: Verify build still passes**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 5: Run the existing RadioModel test suite to confirm no regressions**

```bash
ctest --test-dir build -R "tst_radio_model|tst_rx_dsp_worker|tst_tx_worker_thread|tst_mox_controller_anti_vox|tst_transmit_model_anti_vox" -V 2>&1 | tail -40
```

Expected: existing tests all PASS; no signal-loop or double-fire issues.

- [ ] **Step 6: Commit**

```bash
git add src/models/RadioModel.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): RadioModel wires anti-VOX feed + tau path

Adds 4 queued connections inside the existing TX-channel-ready lambda
block:

  RxDspWorker::antiVoxSampleReady     → TxWorkerThread::onAntiVoxSamplesReady
  RxDspWorker::bufferSizesChanged     → TxWorkerThread::setAntiVoxBlockGeometry
  TransmitModel::antiVoxTauMsChanged  → MoxController::setAntiVoxTau
  MoxController::antiVoxDetectorTauRequested → TxWorkerThread::setAntiVoxDetectorTau

Plus an initial push of TM antiVoxTauMs into MoxController so the first
DEXP setter runs at loadFromSettings-restored state.

Closes the cancellation-feed wire chain end to end.  Visible effect:
moving the anti-VOX gain slider with VOX engaged now audibly suppresses
RX-bleed VOX trips.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: DexpVoxSetupPage Tau (ms) spinbox

**Files:**
- Modify: `src/gui/setup/DexpVoxSetupPage.h` (add `m_antiVoxTauSpin` member)
- Modify: `src/gui/setup/DexpVoxSetupPage.cpp` (build + wire spinbox in existing `grpAntiVOX`)
- Modify: `tests/tst_dexp_vox_setup_page.cpp` (extend with 3 cases)

**Source-first reads required:**
- `Project Files/Source/Console/setup.designer.cs:44660-44698` (range, default, label, tooltip, layout)

**Self-contained: yes** (TransmitModel from Task 8 is the model behind the spinbox).

- [ ] **Step 1: Read the Thetis Designer block for the spinbox layout**

```bash
sed -n '44650,44700p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/setup.designer.cs"
```

Expected: confirms `Tau (ms)` label, tooltip text, range, default 20, increment 1.

- [ ] **Step 2: Write failing tests**

In `tests/tst_dexp_vox_setup_page.cpp`, add:

```cpp
void tauSpinbox_defaultIs20()
{
    TransmitModel tm;  // default antiVoxTauMs is 20
    DexpVoxSetupPage page(&tm);
    auto* spin = page.findChild<QSpinBox*>("antiVoxTauSpin");
    QVERIFY(spin != nullptr);
    QCOMPARE(spin->value(), 20);
}

void tauSpinbox_writesTransmitModel()
{
    TransmitModel tm;
    DexpVoxSetupPage page(&tm);
    auto* spin = page.findChild<QSpinBox*>("antiVoxTauSpin");

    spin->setValue(80);

    QCOMPARE(tm.antiVoxTauMs(), 80);
}

void tauSpinbox_readsTransmitModel()
{
    TransmitModel tm;
    DexpVoxSetupPage page(&tm);
    auto* spin = page.findChild<QSpinBox*>("antiVoxTauSpin");

    tm.setAntiVoxTauMs(120);
    // QSignalBlocker round-trip should not echo back into TM.
    QCOMPARE(spin->value(), 120);
    QCOMPARE(tm.antiVoxTauMs(), 120);
}

void tauSpinbox_tooltipMatchesThetisVerbatim()
{
    TransmitModel tm;
    DexpVoxSetupPage page(&tm);
    auto* spin = page.findChild<QSpinBox*>("antiVoxTauSpin");
    // From Thetis setup.designer.cs:44681 [v2.10.3.13].
    QCOMPARE(spin->toolTip(),
             QStringLiteral("Time-constant used in smoothing Anti-VOX data"));
}
```

- [ ] **Step 3: Run tests, verify they fail**

```bash
cmake --build build --target tst_dexp_vox_setup_page -j$(nproc) 2>&1 | tail -10
```

Expected: "antiVoxTauSpin" child not found.

- [ ] **Step 4: Add member to `DexpVoxSetupPage.h`**

```cpp
QSpinBox* m_antiVoxTauSpin{nullptr};
```

- [ ] **Step 5: Build the spinbox row in `DexpVoxSetupPage.cpp`**

Locate the existing `grpAntiVOX` group construction (where the anti-VOX gain spinbox lives). Append a new row:

```cpp
// Tau (ms) spinbox.  From Thetis grpAntiVOX layout in
// setup.designer.cs:44634-44698 [v2.10.3.13]:
//   udAntiVoxTau: Min=1, Max=500, Increment=1, Value=20
//   lblAntiVoxTau.Text = "Tau (ms)"
//   tooltip = "Time-constant used in smoothing Anti-VOX data"
{
    auto* row = new QHBoxLayout();
    auto* label = new QLabel(tr("Tau (ms)"), this);
    m_antiVoxTauSpin = new QSpinBox(this);
    m_antiVoxTauSpin->setObjectName(QStringLiteral("antiVoxTauSpin"));
    m_antiVoxTauSpin->setRange(TransmitModel::kAntiVoxTauMsMin,
                               TransmitModel::kAntiVoxTauMsMax);
    m_antiVoxTauSpin->setSingleStep(1);
    m_antiVoxTauSpin->setValue(m_tm->antiVoxTauMs());
    // Tooltip from Thetis setup.designer.cs:44681 [v2.10.3.13].
    m_antiVoxTauSpin->setToolTip(tr("Time-constant used in smoothing Anti-VOX data"));
    row->addWidget(label);
    row->addWidget(m_antiVoxTauSpin);
    row->addStretch();
    grpAntiVOXLayout->addLayout(row);

    // Spinbox -> model.
    connect(m_antiVoxTauSpin, qOverload<int>(&QSpinBox::valueChanged),
            m_tm, &TransmitModel::setAntiVoxTauMs);

    // Model -> spinbox (with round-trip guard so spinbox->TM->spinbox
    // doesn't echo).  Pattern matches the existing AntiVox gain wire.
    connect(m_tm, &TransmitModel::antiVoxTauMsChanged,
            this, [this](int ms) {
        QSignalBlocker block(m_antiVoxTauSpin);
        m_antiVoxTauSpin->setValue(ms);
    });
}
```

If `grpAntiVOXLayout` is not the actual identifier in the file, adapt to whatever the existing gain spinbox layout variable is named. Place the new row directly below the gain spinbox row to match Thetis ordering at `setup.designer.cs:44634-44635`.

- [ ] **Step 6: Run tests, verify they pass**

```bash
cmake --build build --target tst_dexp_vox_setup_page -j$(nproc) 2>&1 | tail -5
ctest --test-dir build -R tst_dexp_vox_setup_page -V 2>&1 | tail -25
```

Expected: PASS.

- [ ] **Step 7: Run the app and visually confirm**

```bash
cmake --build build -j$(nproc) && pkill NereusSDR; ./build/NereusSDR &
```

Open Setup → Transmit → DEXP/VOX. Confirm a "Tau (ms)" spinbox appears in `grpAntiVOX` below the gain spinbox, default 20, range 1-500, hover tooltip matches Thetis verbatim.

- [ ] **Step 8: Commit**

```bash
git add src/gui/setup/DexpVoxSetupPage.h src/gui/setup/DexpVoxSetupPage.cpp \
        tests/tst_dexp_vox_setup_page.cpp
git commit -S -m "$(cat <<'EOF'
feat(3m-3a-iv): Setup -> Transmit -> DEXP/VOX gains Tau (ms) control

Adds a single QSpinBox to the existing grpAntiVOX group, mirroring
Thetis udAntiVoxTau at setup.designer.cs:44634-44698 [v2.10.3.13]:
  Min=1, Max=500, Increment=1, Value=20
  Label: "Tau (ms)"
  Tooltip: "Time-constant used in smoothing Anti-VOX data" (verbatim)

Wired bidirectionally to TransmitModel::antiVoxTauMs with QSignalBlocker
round-trip guard, matching the existing anti-VOX gain spinbox pattern.

Tests: tst_dexp_vox_setup_page extended with 4 cases.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Manual bench-verification matrix doc

**Files:**
- Create: `docs/architecture/phase3m-3a-iv-verification/README.md`

**Source-first reads required:**
- (none; NereusSDR-original deliverable)

**Self-contained: yes** (documentation-only).

- [ ] **Step 1: Create the matrix doc**

```bash
mkdir -p docs/architecture/phase3m-3a-iv-verification
```

Then write `docs/architecture/phase3m-3a-iv-verification/README.md` with this content:

````markdown
# Phase 3M-3a-iv Manual Bench Verification

**Goal:** Confirm anti-VOX cancellation feed works end-to-end on real hardware.

**Setup:**
- Workstation running NereusSDR build of `feature/phase3m-3a-iv-antivox-feed`.
- ANAN-G2 (Protocol 2) and Hermes Lite 2 (Protocol 1) on the same bench.
- Quiet mic and a mic-mute switch.
- A known-strong RX signal source: tune to a CW spotting beacon or an SSB
  net at S9+5 measured on the in-applet S-meter.

**Pre-test:** Confirm the Setup page Tau (ms) spinbox shows 20 by default and
range-clamps at 1 and 500. Confirm the tooltip text matches Thetis verbatim.

## Per-radio matrix

For each radio (ANAN-G2, then HL2 mi0bot), run rows 1-5 in order. Record
PASS / FAIL with a short note for each cell.

| # | Test | Setup | Expected | ANAN-G2 | HL2 |
|---|------|-------|----------|---------|-----|
| 1 | Speaker-bleed false-trip baseline | VOX engaged with threshold above mic noise floor; mic muted; RX speaker on at S9+5 SSB voice; anti-VOX gain = 0 dB | VOX false-trips on RX speaker audio; TX-LED toggles on speech bursts | | |
| 2 | Anti-VOX -20 dB suppression | Same as row 1 but anti-VOX gain = -20 dB, tau = 20 ms | False-trips stop or substantially reduce; mic-talk still trips | | |
| 3 | Anti-VOX -40 dB suppression | Same as row 1 but anti-VOX gain = -40 dB | False-trips fully suppressed even at S9+10 RX bursts | | |
| 4 | Tau range bench | Anti-VOX gain = -20 dB; sweep tau from 1 ms to 500 ms in five values (1, 20, 80, 200, 500); strong RX signal | Larger tau audibly extends the engage / disengage time on bursty RX audio (e.g. SSB voice peaks). 1 ms responds twitchy; 500 ms feels lazy. | | |
| 5 | Restart persistence | Set anti-VOX gain = -15 dB and tau = 80 ms; close NereusSDR; reopen; reconnect to the radio | Anti-VOX gain spinbox and Tau (ms) spinbox both restored to set values. Anti-VOX behavior pre- and post-restart is identical. | | |

## Cross-cutting checks

- [ ] No `lcDsp` warnings in the log during normal RX -> TX -> RX cycles.
- [ ] No `lcDsp` warnings on radio reconnect (geometry change covered).
- [ ] CPU usage at idle RX with anti-VOX run = false matches v0.3.2 baseline
      (no perceptible regression from the per-chunk fork).
- [ ] CPU usage at idle RX with anti-VOX run = true is within +1% of run = false.

## Sign-off

Verification owner: J.J. Boyd (KG4VCF). PR is not merged until both
ANAN-G2 and HL2 columns are PASS for rows 1-5 plus all four cross-cutting
checks. If any cell is FAIL, file an issue against this branch and resolve
before tagging.
````

- [ ] **Step 2: Commit**

```bash
git add docs/architecture/phase3m-3a-iv-verification/README.md
git commit -S -m "$(cat <<'EOF'
docs(3m-3a-iv): manual bench-verification matrix

Bench matrix for ANAN-G2 + HL2 across 5 rows: false-trip baseline,
-20 dB suppression, -40 dB suppression, tau range bench, restart
persistence.  Cross-cutting CPU + log-noise checks at the end.

Sign-off rule: PR doesn't merge until both columns + cross-cutting
all PASS.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: CHANGELOG + CLAUDE.md current-phase update

**Files:**
- Modify: `CHANGELOG.md` (add v0.3.3 entry under "Unreleased")
- Modify: `CLAUDE.md` (current-phase paragraph updated)

**Source-first reads required:** none.

**Self-contained: yes** (docs-only).

- [ ] **Step 1: Add CHANGELOG entry**

Open `CHANGELOG.md` and find the most-recent "Unreleased" or topmost section. Add a new bullet (preserve the existing voice and bullet style):

```markdown
- feat(3m-3a-iv): anti-VOX cancellation feed wired end-to-end. Closes the
  gap left by 3M-3a-iii where the gain control was plumbed but the
  RX-audio feedback path into DEXP was never connected. Moving the
  anti-VOX gain slider with VOX engaged now audibly suppresses RX-bleed
  false-trips. Adds 4 WDSP wrappers (`SetAntiVOXSize`/`Rate`/
  `DetectorTau`/`SendData`), wires `RxDspWorker -> TxWorkerThread ->
  TxChannel::sendAntiVoxData` per chunk, and exposes Tau (ms) on Setup
  -> Transmit -> DEXP/VOX with persistence under per-MAC key
  `AntiVox_Tau_Ms`. Aamix port deferred to 3F multi-pan; today's
  plumbing is forward-compatible.
```

(No em-dash above; project convention bans em-dashes in drafted text. Use periods, colons, or semicolons.)

- [ ] **Step 2: Update CLAUDE.md current-phase paragraph**

Find the "Current Phase" paragraph (the long prose block that lists what shipped in v0.3.2 and what's next). Add a short clause noting that 3M-3a-iv (anti-VOX cancellation feed) shipped:

Search for the string `3M-3a-iii DEXP/VOX + AMSQ` and locate the v0.3.2 description. Add immediately after the `;` that closes that clause:

```
; **3M-3a-iv anti-VOX cancellation feed (PR #TBD)** closes the gap left by
3M-3a-iii: 4 new WDSP wrappers (Size/Rate/DetectorTau/SendData), direct
RxDspWorker -> TxWorkerThread pump (single-RX, no aamix; aamix port
deferred to 3F multi-pan), Tau (ms) spinbox on Setup -> Transmit -> DEXP/VOX,
fixed 10ms->20ms tau-default drift, per-MAC `AntiVox_Tau_Ms` persistence,
4 new test cases on each of `tst_tx_channel_vox_anti_vox`,
`tst_mox_controller_anti_vox`, `tst_transmit_model_anti_vox`, and
`tst_dexp_vox_setup_page`
```

If the file's current convention is one-bullet-per-feature rather than the long prose run-on, fall back to whichever style the existing block uses.

Update the phase table (the ASCII table further down with phases 3A through 3N and their Status column). Find the existing 3M-3a-iii row and add a new row directly after it:

```
| **3M-3a-iv: Anti-VOX Feed** | TxChannel sendAntiVoxData + 3 dimension setters; RxDspWorker antiVoxSampleReady fork; TxWorkerThread queued slot with atomic gate; MoxController setAntiVoxTau (ms->s); TransmitModel antiVoxTauMs Q_PROPERTY with persistOne; Setup -> Transmit -> DEXP/VOX Tau (ms) spinbox; fixed WdspEngine tau default 10ms->20ms; bench matrix at docs/architecture/phase3m-3a-iv-verification/README.md. PR #TBD. | **Complete (pending bench)** |
```

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md CLAUDE.md
git commit -S -m "$(cat <<'EOF'
docs(3m-3a-iv): CHANGELOG + CLAUDE.md current-phase refresh

CHANGELOG: new bullet under Unreleased for v0.3.3-rc.
CLAUDE.md: current-phase paragraph + phase-table row for 3M-3a-iv.

Pending bench verification on ANAN-G2 + HL2.  PR # to be filled in
when the PR is opened.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Final pass: full ctest sweep + build sanity

After all 12 tasks land, run the full test suite and a clean build to confirm no regressions slipped in across modules.

- [ ] **Step 1: Clean rebuild**

```bash
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: clean build, no warnings beyond pre-existing baseline.

- [ ] **Step 2: Full ctest run**

```bash
ctest --test-dir build -j$(nproc) --output-on-failure 2>&1 | tail -40
```

Expected: 100% pass rate. If any pre-existing test is flaky or fails for unrelated reasons, file an issue and exclude from the gate (do not silently disable).

- [ ] **Step 3: Smoke launch**

```bash
pkill NereusSDR; ./build/NereusSDR &
```

Open the app, confirm:
- Setup → Transmit → DEXP/VOX shows the Tau (ms) spinbox.
- VOX still engages on mic input (no regression in the existing path).
- Connect to a radio, confirm RX audio still flows to speakers and `chunkDrained` fires (no regression in the RxDspWorker fork).

- [ ] **Step 4: Open PR**

```bash
git push -u origin feature/phase3m-3a-iv-antivox-feed
gh pr create --title "feat(3m-3a-iv): anti-VOX cancellation feed (closes 3M-3a-iii gap)" --body "$(cat <<'EOF'
## Summary

- Closes the cancellation-feed gap from 3M-3a-iii: anti-VOX gain control was plumbed end-to-end but `SendAntiVOXData` was never called, leaving the slider silent. This wires `RxDspWorker -> TxWorkerThread -> TxChannel::sendAntiVoxData` per chunk.
- Adds 4 WDSP wrappers: `SetAntiVOXSize`, `SetAntiVOXRate`, `SetAntiVOXDetectorTau`, `SendAntiVOXData`.
- Adds Tau (ms) spinbox to Setup -> Transmit -> DEXP/VOX, persisted per-MAC under `AntiVox_Tau_Ms`.
- Fixes incidental constant drift: anti-VOX tau default 10 ms -> 20 ms (Thetis `udAntiVoxTau.Value=20`).
- Aamix port deferred to 3F multi-pan; today's direct pump is forward-compatible.

Spec: `docs/architecture/phase3m-3a-iv-antivox-feed-design.md`
Plan: `docs/superpowers/plans/2026-05-07-phase3m-3a-iv-antivox-feed.md`

## Test plan

- [ ] All ctest cases PASS on Linux (Arch + Ubuntu) and macOS Apple Silicon.
- [ ] Manual bench matrix at `docs/architecture/phase3m-3a-iv-verification/README.md` PASS on ANAN-G2 (5 rows + cross-cutting).
- [ ] Manual bench matrix PASS on HL2 mi0bot (5 rows + cross-cutting).
- [ ] CPU baseline check: idle RX with anti-VOX off and on within +1%.
- [ ] No `lcDsp` warnings on connect / disconnect / sample-rate change.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

Return the PR URL when done.
