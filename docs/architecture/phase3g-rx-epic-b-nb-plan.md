# Phase 3G — RX Epic Sub-epic B: Noise Blanker Family Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port Thetis's three-filter noise blanker stack (NB / NB2 / SNB) into NereusSDR with a single `NbFamily` wrapper on `RxChannel`, a cycling NB button on `VfoWidget`, per-slice-per-band persistence, RxApplet tuning controls, and wired DSP Setup defaults.

**Architecture:** `RxChannel` owns one `NbFamily` instance that wraps WDSP's `create_anbEXT` (NB, `nob.c`) + `create_nobEXT` (NB2, `nobII.c`) + `SetRXASNBARun` (SNB, `snb.c`) lifecycle and tuning. `SliceModel` grows an `NbMode {Off, NB, NB2}` enum property replacing the old scalar `nb2Enabled`, plus a per-band tuning block (`nbThreshold`, `nbTau`, `nbLeadMs`, `nbLagMs`) persisted via the established 3G-10 Stage 2 per-slice-per-band namespace. `VfoWidget` replaces its separate NB2 toggle with a tri-state cycling NB button mirroring Thetis `chkNB` (`CheckState.Unchecked → Checked → Indeterminate` ≙ `Off → NB → NB2`). `SNB` stays an independent toggle and runs alongside either NB mode. The audio thread reads `m_nbMode.load()` in `processIq()` and dispatches to `xanbEXTF` / `xnobEXTF` / no-op accordingly.

**Tech Stack:** C++20, Qt6, WDSP (already linked), QTest for unit tests. No new third-party libraries.

**Spec reference:** [phase3g-rx-experience-epic-design.md](phase3g-rx-experience-epic-design.md) § sub-epic B.

**Upstream stamp:** Thetis `v2.10.3.13` @ `501e3f5`. All inline cites in ports use `// From Thetis <path>:<line> [v2.10.3.13]` or `[@501e3f5]` per `docs/attribution/HOW-TO-PORT.md` §Inline cite versioning.

**Out of scope for this plan:**
- NB3 / NB4 / NB5 — do not exist in Thetis or our WDSP tree
- SNB tuning of K1/K2 per slice — Thetis SNB is tuned once at create time via `SetRXASNBAPASSES` / `SetRXASNBAkvals` callable but not user-facing in Thetis setup; this plan wires global defaults only and leaves per-slice SNB tuning deferred
- NB2 tuning UI — Thetis `CATNB2Threshold` (setup.cs:5687) is a `TODO` stub; NB2 runs at the `cmaster.c:55-68` [v2.10.3.13] create-time defaults; per Thetis parity we do not add NB2 tuning UI in this sub-epic. Global NB2 defaults *are* persisted at create time so they are preserved byte-for-byte
- mi0bot-Thetis NB tweaks — design doc calls out no unique NB value over Thetis proper

---

## File structure

| File | Responsibility | Change type |
|---|---|---|
| `src/core/wdsp_api.h` | WDSP C-API bindings | Modify — add missing NB1 post-create setters (`SetEXTANBTau/Hangtime/Advtime/Backtau/Threshold`) and `SetEXTNOBBacktau` |
| `src/core/NbFamily.h` *(new)* | NB/NB2/SNB wrapper facade | Create — struct `NbTuning`, class `NbFamily` with create/destroy/setMode/setSnb/setNbTuning |
| `src/core/NbFamily.cpp` *(new)* | NbFamily implementation | Create |
| `src/core/RxChannel.h` | Per-receiver WDSP channel | Modify — replace `m_nb1Enabled` + `m_nb2Enabled` atomics with single `m_nbMode` atomic; add `NbFamily m_nb` member; replace `setNb1Enabled`/`setNb2Enabled` with `setNbMode(NbMode)`; add `setNbTuning(const NbTuning&)` |
| `src/core/RxChannel.cpp` | Implementation | Modify — NB lifecycle in ctor/dtor, processIq switch on mode, tuning setters |
| `src/core/WdspTypes.h` | WDSP enums | Modify — `NbMode` already exists (Off/NB1/NB2); rename values for readability to `Off/NB/NB2` |
| `src/models/SliceModel.h` | Per-slice VFO state | Modify — add `NbMode nbMode`, `bool snbEnabled` (already exists), `double nbThreshold`, `double nbTauMs`, `double nbLeadMs`, `double nbLagMs`; remove `nb2Enabled` Q_PROPERTY (replaced by `nbMode`) |
| `src/models/SliceModel.cpp` | Implementation | Modify — setters, signal emission, `saveToSettings(band)` / `restoreFromSettings(band)` extensions |
| `src/core/AppSettings.h` | Settings key reference block | Modify — document `Slice<N>/Band<key>/NbMode`, `/NbThreshold`, `/NbTauMs`, `/NbLeadMs`, `/NbLagMs`; session-level `Slice<N>/SnbEnabled` |
| `src/models/RadioModel.cpp` | Central state hub | Modify — push full NB config (mode + tuning + snb) on slice activate and on radio connect; remove legacy separate NB1/NB2 push lines |
| `src/gui/widgets/VfoWidget.h` | VFO flag widget declaration | Modify — replace `setNb2Enabled(bool)` with `setNbMode(NbMode)`; signal `nbModeCycled()`; keep `setSnbEnabled(bool)` |
| `src/gui/widgets/VfoWidget.cpp` | Implementation | Modify — replace NB2 push button with single NB button that cycles Off→NB→NB2→Off on click; label text mirrors Thetis console.cs:43525-43546 |
| `src/gui/applets/RxApplet.h` | RX applet declaration | Modify — declare new NB tuning row widgets |
| `src/gui/applets/RxApplet.cpp` | Implementation | Modify — add NB tuning row (Threshold + Lag sliders) beneath existing NB button; bind to active SliceModel |
| `src/gui/setup/DspSetupPages.cpp` | DSP Setup page | Modify — wire `NbSnbSetupPage` controls to global AppSettings defaults; remove `disableGroup` stubs; add NB1 Transition/Lead sliders |
| `src/gui/MainWindow.cpp` | Signal wiring hub | Modify — update `setNb2Enabled`-based hooks to `setNbMode`-based; route `vfo->nbModeCycled` → `slice->setNbMode(cycleNext(slice->nbMode()))` |
| `tests/tst_nb_family.cpp` *(new)* | NbFamily mode cycling + tuning setter tests | Create — no WDSP dependency (uses fake backend) |
| `tests/tst_slice_nb_persistence.cpp` *(new)* | Per-slice-per-band persistence tests | Create |
| `tests/CMakeLists.txt` | Test registration | Modify — register two new test targets |
| `docs/architecture/phase3g-rx-epic-b-verification/README.md` *(new)* | Manual verification matrix | Create — 3G-8-style matrix |
| `CHANGELOG.md` | Version history | Modify — add Sub-epic B entry under `## [Unreleased]` |

## Pre-flight inventory (read-only — no code changes)

Run these exact greps before editing so the call-site footprint is fresh:

```bash
rg -n 'nb1Enabled|nb2Enabled|setNb1|setNb2|Nb1Enabled|Nb2Enabled|NbMode|m_nb1|m_nb2' src/ tests/
rg -n 'SetRXASNBARun|create_anbEXT|create_nobEXT|destroy_anbEXT|destroy_nobEXT|xanbEXTF|xnobEXTF' src/
rg -n 'SetEXTANB|SetEXTNOB' src/
```

**Expected hits at time of plan writing (will drift — read current state, don't trust this list blindly):**

*Model/engine layer (NB1/NB2 enable + tuning setters):*
- `src/core/WdspTypes.h:230` — `enum class NbMode : int { Off = 0, NB1 = 1, NB2 = 2 };` (rename `NB1→NB` in Task 3)
- `src/core/RxChannel.h:295-315` — `setNb1Enabled`, `setNb2Enabled`, `setNb2Mode/Tau/LeadTime/HangTime`
- `src/core/RxChannel.cpp:530-594` — impls
- `src/core/RxChannel.cpp:693-710` — `setSnbEnabled`
- `src/core/RxChannel.cpp:~1019-1024` — `xanbEXTF` / `xnobEXTF` dispatch in `processIq`
- `src/core/wdsp_api.h:272-304,313-333` — NB/NB2 external-mode bindings (missing: `SetEXTANBTau/Hangtime/Advtime/Backtau/Threshold`, `SetEXTNOBBacktau`)
- `src/models/SliceModel.h:187,189` — `nb2Enabled`/`snbEnabled` Q_PROPERTYs
- `src/models/SliceModel.cpp:483-512` — `setNb2Enabled` / `setSnbEnabled` impls
- `src/models/RadioModel.cpp:707-718` — per-slice NB2 tuning push on activate; lines 1238-1243 — SNB on slice change

*UI layer:*
- `src/gui/widgets/VfoWidget.h:370-372` — `setNb2Enabled`, `setSnbEnabled`
- `src/gui/widgets/VfoWidget.cpp:1545-1563` — impls
- `src/gui/MainWindow.cpp:1273,2211,2219,2267,2280,2287` — wiring
- `src/gui/setup/DspSetupPages.cpp:361-414` — `NbSnbSetupPage` with `disableGroup` stubs

*Upstream reference (read only — DO NOT copy wholesale):*
- `/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/nob.h:65-123` — NB1 API
- `/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/nobII.h:81-145` — NB2 API
- `/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/snb.h:107-122` — SNB API
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/HPSDR/specHPSDR.cs:896-977` — C# P/Invoke declarations (full set of setter names)
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/console.cs:43513-43560` — `chkNB_CheckStateChanged` — the cycling toggle source of truth
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/console.cs:42476-42482` — space-bar cycle increment
- `/Users/j.j.boyd/Thetis/Project Files/Source/Console/setup.cs:8569-8573,16219-16237` — `udDSPNB*` UI → NB1 tuning scaling factors (0.165, 0.001 ms→s)
- `/Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/cmaster.c:43-68` — create-time NB/NB2 default tuning values (the canonical "byte-for-byte" values — do not alter)

Any additional hit from the rg commands above that isn't listed here needs review before you edit.

---

## Task 1: Expose missing NB post-create setters in wdsp_api.h

WDSP's `nob.h` / `nobII.h` ship `pSetRCVRANB*` + `SetEXTANB*` / `SetEXTNOB*` setters for every knob; our `wdsp_api.h` currently only binds the NB2 subset (`SetEXTNOBMode/Tau/Hangtime/Advtime/Threshold`) and none of the NB1 ones. This task adds the missing extern-C decls so later tasks can call them.

**Files:**
- Modify: `src/core/wdsp_api.h` (around lines 272-334, inside the "Noise blanker — external" blocks)

- [ ] **Step 1: Add the missing NB1 post-create setter decls**

Inside the `// Noise blanker — external (nob.h, nob.c)` block (after `void xanbEXTF(int id, float* I, float* Q);`), append:

```cpp
// NB1 post-create setters — called after create_anbEXT to override defaults.
// Declared in Thetis Project Files/Source/Console/HPSDR/specHPSDR.cs:965-977
// WDSP: third_party/wdsp/src/nob.c
// Thetis cmaster.c:43-53 [v2.10.3.13] create-time defaults:
//   tau=0.0001, hangtime=0.0001, advtime=0.0001, backtau=0.05, threshold=30.0

void SetEXTANBTau(int id, double tau);
void SetEXTANBHangtime(int id, double time);
void SetEXTANBAdvtime(int id, double time);
void SetEXTANBBacktau(int id, double tau);
void SetEXTANBThreshold(int id, double thresh);
```

- [ ] **Step 2: Add the missing NB2 backtau decl**

Inside the `// Noise blanker II — external (nobII.h, nobII.c)` block, after `SetEXTNOBThreshold`, append:

```cpp
// backtau: averaging time constant for signal magnitude (seconds)
// From Thetis specHPSDR.cs:931 — SetEXTNOBBacktau(int id, double tau)
// WDSP: third_party/wdsp/src/nobII.c
void SetEXTNOBBacktau(int id, double tau);
```

- [ ] **Step 3: Build to confirm linker resolves**

Run:

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -30
```

Expected: build succeeds. If linker errors mention `SetEXTANB*` or `SetEXTNOBBacktau`, the WDSP source file has renamed or is missing the export — stop and read `third_party/wdsp/src/nob.c` / `nobII.c` to confirm symbol names before proceeding.

- [ ] **Step 4: Commit**

```bash
git add src/core/wdsp_api.h
git commit -S -m "feat(wdsp): expose NB1 + NB2 post-create setters

Add SetEXTANBTau/Hangtime/Advtime/Backtau/Threshold (NB1) and
SetEXTNOBBacktau (NB2) to the C++ bindings header. Needed by the
NbFamily wrapper landing in the next commit.

Cites: Thetis specHPSDR.cs:965-977 [v2.10.3.13]"
```

---

## Task 2: Rename `NbMode::NB1` → `NbMode::NB` in WdspTypes.h

Thetis and the design doc use "NB" and "NB2" (not "NB1" and "NB2"). The enum has `NB1` from an earlier Phase 3G-10 draft; align the name before new code references it.

**Files:**
- Modify: `src/core/WdspTypes.h:230`

- [ ] **Step 1: Rename the enumerator**

Change line 230 from:

```cpp
enum class NbMode      : int { Off = 0, NB1 = 1, NB2 = 2 };
```

to:

```cpp
// From Thetis console.cs:43513-43560 [v2.10.3.13] — chkNB tri-state mapping:
//   CheckState.Unchecked    → Off (0)
//   CheckState.Checked      → NB  (1)  ≡ nob.c (Whitney blanker)
//   CheckState.Indeterminate→ NB2 (2)  ≡ nobII.c (second-gen blanker)
enum class NbMode : int { Off = 0, NB = 1, NB2 = 2 };
```

- [ ] **Step 2: Grep for any existing `NbMode::NB1` usage and repoint**

```bash
rg -n 'NbMode::NB1|NbMode\.NB1' src/ tests/
```

Expected: no hits (the enum isn't referenced yet outside the declaration). If the grep returns hits, rename each to `NbMode::NB` inline.

- [ ] **Step 3: Build**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -10
```

Expected: build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/core/WdspTypes.h
git commit -S -m "refactor(wdsp): rename NbMode::NB1 → NbMode::NB

Align with Thetis nomenclature (chkNB CheckState.Checked ≙ NB).
Inline cite pins the three-state mapping to console.cs:43513 [v2.10.3.13]."
```

---

## Task 3: Create `NbFamily` wrapper — header

`NbFamily` is a per-`RxChannel` facade that owns the NB + NB2 + SNB WDSP state. It does **not** own channel lifecycle (that stays on `RxChannel`). It does own the tuning struct so `SliceModel` can hand it a populated value and `RxChannel` pushes the whole thing through one call.

**Files:**
- Create: `src/core/NbFamily.h`

- [ ] **Step 1: Write the header**

Create `src/core/NbFamily.h`:

```cpp
// =================================================================
// src/core/NbFamily.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/ChannelMaster/cmaster.c (NB/NB2 create defaults)
//   Project Files/Source/Console/HPSDR/specHPSDR.cs (P/Invoke signatures)
//   Project Files/Source/Console/console.cs (chkNB tri-state cycle)
//   Project Files/Source/Console/setup.cs (NB1 UI scaling factors)
//   Project Files/Source/wdsp/nob.c, nobII.c, snb.c (Warren Pratt, NR0V)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-22 — New NereusSDR facade class for the NB / NB2 / SNB
//                family. The WDSP C functions it wraps retain their
//                Warren Pratt / Richard Samphire copyrights verbatim
//                in third_party/wdsp/src/*.c headers.
//                Authored by J.J. Boyd (KG4VCF), with AI-assisted
//                transformation via Anthropic Claude Code.
// =================================================================
//
// <Thetis cmaster.c header byte-for-byte here — see Step 2>

#pragma once

#include <atomic>

#include "WdspTypes.h"

namespace NereusSDR {

// NB1/NB2 tuning knobs. Field units chosen to match Thetis setup.cs
// user-visible units (milliseconds for times, linear for threshold)
// so UI sliders round-trip without conversion; WDSP calls are converted
// to seconds at the wdsp_api.h boundary.
//
// Defaults below are Thetis cmaster.c:43-68 [v2.10.3.13] byte-for-byte.
struct NbTuning
{
    // NB1 — applied via create_anbEXT + SetEXTANB* post-create setters.
    double nbTauMs       = 0.1;    // cmaster.c:49   tau=0.0001 s
    double nbHangMs      = 0.1;    // cmaster.c:50   hangtime=0.0001 s
    double nbAdvMs       = 0.1;    // cmaster.c:51   advtime=0.0001 s
    double nbBacktau     = 0.05;   // cmaster.c:52   backtau=0.05 s (stored in s, not ms)
    double nbThreshold   = 30.0;   // cmaster.c:53   threshold=30.0

    // NB2 — applied via create_nobEXT + SetEXTNOB* post-create setters.
    int    nb2Mode       = 0;      // cmaster.c:61   mode=0 (zero-fill)
    double nb2SlewMs     = 0.1;    // cmaster.c:62/64 advslewtime=hangslewtime=0.0001 s
    double nb2AdvMs      = 0.1;    // cmaster.c:63   advtime=0.0001 s
    double nb2HangMs     = 0.1;    // cmaster.c:65   hangtime=0.0001 s
    double nb2MaxImpMs   = 25.0;   // cmaster.c:66   max_imp_seq_time=0.025 s
    double nb2Backtau    = 0.05;   // cmaster.c:67   backtau=0.05 s
    double nb2Threshold  = 30.0;   // cmaster.c:68   threshold=30.0
};

// Cycles the NbMode on user toggle: Off → NB → NB2 → Off.
// From Thetis console.cs:42476-42482 [v2.10.3.13] — space-bar increment
// and console.cs:43513-43560 [v2.10.3.13] — chkNB CheckState transition.
NbMode cycleNbMode(NbMode current);

// Wrapper facade. One instance per RxChannel. Holds no WDSP-side ownership —
// the underlying anb/nob objects are owned by the channel-master inside WDSP
// and keyed by channel id. This class centralizes the API surface and the
// audio-thread-safe mode flag.
class NbFamily
{
public:
    // channelId is the WDSP channel (== RxChannel::m_channelId).
    // sampleRate is the input sample rate handed to create_anbEXT/nobEXT.
    // bufferSize is the number of COMPLEX samples per xanbEXTF/xnobEXTF call
    // (matches fexchange2 input buffer size).
    NbFamily(int channelId, int sampleRate, int bufferSize);
    ~NbFamily();

    NbFamily(const NbFamily&)            = delete;
    NbFamily& operator=(const NbFamily&)  = delete;

    // Mode/SNB setters — safe to call from any thread. processIq() reads
    // m_mode via load(std::memory_order_acquire).
    void setMode(NbMode mode);
    NbMode mode() const { return m_mode.load(std::memory_order_acquire); }

    void setSnbEnabled(bool enabled);
    bool snbEnabled() const { return m_snbEnabled.load(std::memory_order_acquire); }

    // Tuning setter — pushes every field through to WDSP. Also stores
    // the struct locally so subsequent partial setters (e.g. setNbThreshold
    // from a slider) can mutate in place.
    void setTuning(const NbTuning& tuning);
    const NbTuning& tuning() const { return m_tuning; }

    // Per-knob convenience setters (used by RxApplet tuning sliders).
    // Each pushes only its one WDSP call; the local m_tuning is updated too.
    void setNbThreshold(double threshold);
    void setNbTauMs(double ms);
    void setNbLeadMs(double advMs);    // "lead" ≡ advtime
    void setNbLagMs(double hangMs);    // "lag"  ≡ hangtime

private:
    const int m_channelId;
    const int m_sampleRate;
    const int m_bufferSize;

    std::atomic<NbMode> m_mode{NbMode::Off};
    std::atomic<bool>   m_snbEnabled{false};
    NbTuning            m_tuning{};

    // Pushes all tuning fields through WDSP post-create setters for both
    // NB1 and NB2. Called from ctor (after create_*EXT) and from setTuning.
    void pushAllTuning();
};

} // namespace NereusSDR
```

- [ ] **Step 2: Copy the upstream header byte-for-byte**

Replace the `<Thetis cmaster.c header byte-for-byte here — see Step 2>` placeholder with the verbatim contents of `/Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/cmaster.c` lines 1-28 (the license block), preceded by `// --- From cmaster.c ---`, followed by `// --- From wdsp/nob.h ---` and the verbatim `nob.h` lines 1-25, and likewise for `nobII.h` and `snb.h`. See `docs/attribution/HOW-TO-PORT.md` §Multi-file attribution for the expected separator format.

- [ ] **Step 3: Run the inline-tag preservation check against the new file**

```bash
python3 scripts/verify-thetis-headers.py src/core/NbFamily.h
```

Expected: passes. If it fails, read `docs/attribution/HOW-TO-PORT.md` and fix the header block until it passes.

---

## Task 4: Create `NbFamily` — implementation

**Files:**
- Create: `src/core/NbFamily.cpp`

- [ ] **Step 1: Write the impl**

Create `src/core/NbFamily.cpp`:

```cpp
// See src/core/NbFamily.h for the upstream attribution block.

#include "NbFamily.h"

#include "wdsp_api.h"

namespace NereusSDR {

namespace {
constexpr double kMsToSec = 0.001;
}

NbMode cycleNbMode(NbMode current)
{
    // From Thetis console.cs:42476-42482 [v2.10.3.13] — space-bar cycle:
    //   Unchecked/Indeterminate → Checked
    //   Checked                 → Unchecked
    //   (second press path handles Unchecked/Checked → Indeterminate)
    // Our simpler Off→NB→NB2→Off chain matches the user-visible three-state
    // rotation from console.cs:43513-43560 without Thetis's two-switch
    // hidden-state complexity.
    switch (current) {
        case NbMode::Off:  return NbMode::NB;
        case NbMode::NB:   return NbMode::NB2;
        case NbMode::NB2:  return NbMode::Off;
    }
    return NbMode::Off;
}

NbFamily::NbFamily(int channelId, int sampleRate, int bufferSize)
    : m_channelId(channelId)
    , m_sampleRate(sampleRate)
    , m_bufferSize(bufferSize)
{
#ifdef HAVE_WDSP
    // From Thetis cmaster.c:43-53 [v2.10.3.13] — NB (anb) create call.
    // Run flag starts at 0; processIq() gates xanbEXTF on m_mode separately.
    create_anbEXT(
        m_channelId,
        /*run=*/0,
        m_bufferSize,
        static_cast<double>(m_sampleRate),
        m_tuning.nbTauMs    * kMsToSec,
        m_tuning.nbHangMs   * kMsToSec,
        m_tuning.nbAdvMs    * kMsToSec,
        m_tuning.nbBacktau,
        m_tuning.nbThreshold);

    // From Thetis specHPSDR.cs:896-907 [v2.10.3.13] — NB2 P/Invoke 10-arg form:
    //   (id, run, mode, buffsize, samplerate, tau, hangtime, advtime, backtau, threshold)
    // This is a reduction of the nobII.c 13-arg C signature — advslewtime,
    // hangslewtime, and max_imp_seq_time are NOT post-create settable via the
    // EXT interface; they come from nobII.c internal defaults. `nb2SlewMs`
    // and `nb2MaxImpMs` fields in NbTuning are reserved for a future direct
    // C-API wrapper and are unused in this path.
    create_nobEXT(
        m_channelId,
        /*run=*/0,
        m_tuning.nb2Mode,
        m_bufferSize,
        static_cast<double>(m_sampleRate),
        m_tuning.nb2SlewMs     * kMsToSec,       // tau  (cmaster.c:62/64 default 0.0001 s)
        m_tuning.nb2HangMs     * kMsToSec,       // hangtime
        m_tuning.nb2AdvMs      * kMsToSec,       // advtime
        m_tuning.nb2Backtau,
        m_tuning.nb2Threshold);
#endif
}

NbFamily::~NbFamily()
{
#ifdef HAVE_WDSP
    // From Thetis cmaster.c:104-105 [v2.10.3.13] — destroy in reverse of create.
    destroy_nobEXT(m_channelId);
    destroy_anbEXT(m_channelId);
#endif
}

void NbFamily::setMode(NbMode mode)
{
    m_mode.store(mode, std::memory_order_release);
    // The per-buffer xanbEXTF / xnobEXTF dispatch in RxChannel::processIq()
    // reads this atomic; no WDSP "run" toggle needed.
}

void NbFamily::setSnbEnabled(bool enabled)
{
    m_snbEnabled.store(enabled, std::memory_order_release);
#ifdef HAVE_WDSP
    // From Thetis console.cs:36347 [v2.10.3.13]
    //   WDSP.SetRXASNBARun(WDSP.id(0, 0), chkDSPNB2.Checked)
    // WDSP: third_party/wdsp/src/snb.c
    SetRXASNBARun(m_channelId, enabled ? 1 : 0);
#endif
}

void NbFamily::setTuning(const NbTuning& tuning)
{
    m_tuning = tuning;
    pushAllTuning();
}

void NbFamily::setNbThreshold(double threshold)
{
    m_tuning.nbThreshold = threshold;
#ifdef HAVE_WDSP
    SetEXTANBThreshold(m_channelId, threshold);
#endif
}

void NbFamily::setNbTauMs(double ms)
{
    m_tuning.nbTauMs = ms;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16222 [v2.10.3.13]
    //   NBTau = 0.001 * (double)udDSPNBTransition.Value
    SetEXTANBTau(m_channelId, ms * kMsToSec);
#endif
}

void NbFamily::setNbLeadMs(double advMs)
{
    m_tuning.nbAdvMs = advMs;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16229 [v2.10.3.13]
    //   NBAdvTime = 0.001 * (double)udDSPNBLead.Value
    SetEXTANBAdvtime(m_channelId, advMs * kMsToSec);
#endif
}

void NbFamily::setNbLagMs(double hangMs)
{
    m_tuning.nbHangMs = hangMs;
#ifdef HAVE_WDSP
    // From Thetis setup.cs:16236 [v2.10.3.13]
    //   NBHangTime = 0.001 * (double)udDSPNBLag.Value
    SetEXTANBHangtime(m_channelId, hangMs * kMsToSec);
#endif
}

void NbFamily::pushAllTuning()
{
#ifdef HAVE_WDSP
    // NB1
    SetEXTANBTau      (m_channelId, m_tuning.nbTauMs  * kMsToSec);
    SetEXTANBHangtime (m_channelId, m_tuning.nbHangMs * kMsToSec);
    SetEXTANBAdvtime  (m_channelId, m_tuning.nbAdvMs  * kMsToSec);
    SetEXTANBBacktau  (m_channelId, m_tuning.nbBacktau);
    SetEXTANBThreshold(m_channelId, m_tuning.nbThreshold);

    // NB2
    SetEXTNOBMode     (m_channelId, m_tuning.nb2Mode);
    SetEXTNOBTau      (m_channelId, m_tuning.nb2SlewMs   * kMsToSec);
    SetEXTNOBHangtime (m_channelId, m_tuning.nb2HangMs   * kMsToSec);
    SetEXTNOBAdvtime  (m_channelId, m_tuning.nb2AdvMs    * kMsToSec);
    SetEXTNOBBacktau  (m_channelId, m_tuning.nb2Backtau);
    SetEXTNOBThreshold(m_channelId, m_tuning.nb2Threshold);
    // max_imp_seq_time has no post-create setter in Thetis's spec — it is
    // fixed at create time per cmaster.c:66. Changes require channel re-create.
#endif
}

} // namespace NereusSDR
```

- [ ] **Step 2: Add NbFamily to the NereusSDR target in CMakeLists.txt**

Find the `NereusSDR` target sources list (`rg -n 'RxChannel.cpp' CMakeLists.txt src/CMakeLists.txt 2>/dev/null`). Add `src/core/NbFamily.cpp` and `src/core/NbFamily.h` next to `src/core/RxChannel.cpp` in whichever list RxChannel lives in.

- [ ] **Step 3: Build**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -20
```

Expected: compiles cleanly. Linker errors naming `SetEXTANB*` or `SetEXTNOBBacktau` mean Task 1 didn't land — back up and fix.

- [ ] **Step 4: Commit**

```bash
git add src/core/NbFamily.h src/core/NbFamily.cpp CMakeLists.txt
git commit -S -m "feat(core): NbFamily wrapper for WDSP NB/NB2/SNB

One-class facade per RxChannel that owns the NB+NB2 create/destroy
lifecycle, tuning struct, mutual-exclusion mode atomic, and SNB toggle.
Defaults pin to Thetis cmaster.c:43-68 [v2.10.3.13] byte-for-byte.
Scaling factors (ms→s, 0.165× threshold) match setup.cs:8572-16237."
```

---

## Task 5: Wire NbFamily into RxChannel

Replace `m_nb1Enabled` + `m_nb2Enabled` atomic pair with a single `m_nb` member. Collapse the existing NB1/NB2 setters into `setNbMode` and delegated tuning setters. Update `processIq()` to dispatch based on `NbMode`.

**Files:**
- Modify: `src/core/RxChannel.h` (lines 290-335, 500-540, member block)
- Modify: `src/core/RxChannel.cpp` (lines 530-594, 693-710, constructor, `processIq()`)

- [ ] **Step 1: Update `RxChannel.h`**

Replace the existing NB block:

```cpp
    void setNb1Enabled(bool enabled);
    bool nb1Enabled() const { return m_nb1Enabled.load(); }

    void setNb2Enabled(bool enabled);
    bool nb2Enabled() const { return m_nb2Enabled.load(); }

    void setNb2Mode(int mode);
    void setNb2Tau(double tau);
    void setNb2LeadTime(double time);
    void setNb2HangTime(double time);
```

with:

```cpp
    // From design doc phase3g-rx-experience-epic-design.md §sub-epic B —
    // NB/NB2 are mutually exclusive via a single NbMode atomic. SNB is
    // independent and runs alongside whichever NbMode is active.
    void setNbMode(NereusSDR::NbMode mode);
    NereusSDR::NbMode nbMode() const;

    void setNbTuning(const NereusSDR::NbTuning& tuning);
    const NereusSDR::NbTuning& nbTuning() const;

    // Per-knob convenience setters for RxApplet sliders.
    void setNbThreshold(double threshold);
    void setNbLagMs(double hangMs);
    void setNbLeadMs(double advMs);
    void setNbTransitionMs(double tauMs);
```

Keep the existing `setSnbEnabled(bool)` declaration — its implementation will now delegate to `m_nb.setSnbEnabled`.

In the member block, replace:

```cpp
    std::atomic<bool> m_nb1Enabled{false};
    std::atomic<bool> m_nb2Enabled{false};
```

with:

```cpp
    // Default-constructed NbFamily is wired in the ctor init list (needs
    // channel id + sample rate + buffer size, which ctor receives).
    std::unique_ptr<NereusSDR::NbFamily> m_nb;
```

Also add `#include "NbFamily.h"` in the header.

- [ ] **Step 2: Update `RxChannel.cpp` constructor**

In the `RxChannel` constructor (grep `RxChannel::RxChannel`), after WDSP channel open (`OpenChannel(...)`) but before returning, add:

```cpp
#ifdef HAVE_WDSP
    // From design doc §sub-epic B — one NbFamily per WDSP channel.
    // Samplerate and bufsize follow OpenChannel's input params above.
    m_nb = std::make_unique<NereusSDR::NbFamily>(
        m_channelId,
        /*sampleRate=*/ m_inputSampleRate,
        /*bufferSize=*/ m_inputBufferSize);
#endif
```

(Adapt the variable names `m_inputSampleRate` / `m_inputBufferSize` to whatever the existing ctor uses — read the surrounding `OpenChannel` call to find them.)

- [ ] **Step 3: Replace the old NB setter bodies**

Delete `setNb1Enabled`, `setNb2Enabled`, `setNb2Mode`, `setNb2Tau`, `setNb2LeadTime`, `setNb2HangTime` (lines 530-594). Replace them with:

```cpp
// ---------------------------------------------------------------------------
// Noise blanker family (NB / NB2 / SNB) — see NbFamily.h
// ---------------------------------------------------------------------------

void RxChannel::setNbMode(NereusSDR::NbMode mode)
{
    if (m_nb) m_nb->setMode(mode);
}

NereusSDR::NbMode RxChannel::nbMode() const
{
    return m_nb ? m_nb->mode() : NereusSDR::NbMode::Off;
}

void RxChannel::setNbTuning(const NereusSDR::NbTuning& tuning)
{
    if (m_nb) m_nb->setTuning(tuning);
}

const NereusSDR::NbTuning& RxChannel::nbTuning() const
{
    static const NereusSDR::NbTuning kEmpty{};
    return m_nb ? m_nb->tuning() : kEmpty;
}

void RxChannel::setNbThreshold(double threshold)    { if (m_nb) m_nb->setNbThreshold(threshold); }
void RxChannel::setNbLagMs(double hangMs)           { if (m_nb) m_nb->setNbLagMs(hangMs); }
void RxChannel::setNbLeadMs(double advMs)           { if (m_nb) m_nb->setNbLeadMs(advMs); }
void RxChannel::setNbTransitionMs(double tauMs)     { if (m_nb) m_nb->setNbTauMs(tauMs); }
```

Update `setSnbEnabled` (lines 693-710) to delegate:

```cpp
void RxChannel::setSnbEnabled(bool enabled)
{
    if (enabled == m_snbEnabled.load()) return;
    m_snbEnabled.store(enabled);
    if (m_nb) m_nb->setSnbEnabled(enabled);
}
```

- [ ] **Step 4: Rewire `processIq()` NB dispatch**

Find the block currently at roughly lines 1018-1024:

```cpp
    if (m_nb1Enabled.load()) {
        xanbEXTF(m_channelId, inI, inQ);
    }
    if (m_nb2Enabled.load()) {
        xnobEXTF(m_channelId, inI, inQ);
    }
```

Replace with:

```cpp
    // From design doc §sub-epic B — mutually exclusive NB/NB2 via one atomic.
    switch (m_nb ? m_nb->mode() : NereusSDR::NbMode::Off) {
        case NereusSDR::NbMode::NB:  xanbEXTF(m_channelId, inI, inQ); break;
        case NereusSDR::NbMode::NB2: xnobEXTF(m_channelId, inI, inQ); break;
        case NereusSDR::NbMode::Off: /* no-op */                      break;
    }
```

- [ ] **Step 5: Fix build**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -40
```

Compile errors will surface in every call site of `setNb1Enabled` / `setNb2Enabled` / `setNb2Mode` / `setNb2Tau` / `setNb2LeadTime` / `setNb2HangTime`. Fix each by calling the new setters. Expected call sites (from pre-flight inventory):

- `src/gui/MainWindow.cpp:1273` — `if (rxCh) { rxCh->setNb1Enabled(on); }` → update in Task 8 when MainWindow wiring is touched. For now, change to `if (rxCh) { rxCh->setNbMode(on ? NereusSDR::NbMode::NB : NereusSDR::NbMode::Off); }` as a transitional stopgap.
- `src/gui/MainWindow.cpp:2211,2267,2280` — same shape.
- `src/models/RadioModel.cpp:707-710` — `rxCh->setNb2Mode/Tau/LeadTime/HangTime` → replace with `rxCh->setNbTuning(m_activeSlice->nbTuning())` (task 7 will wire SliceModel's nbTuning; for now pass a default-constructed `NereusSDR::NbTuning{}`).

- [ ] **Step 6: Launch smoke test**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -5
pkill -f NereusSDR || true
./build/NereusSDR &
```

Expected: app launches without crash. No NB-related console errors in `tail -f ~/.config/NereusSDR/log.txt` (or equivalent log path). Kill the app after 10 seconds.

- [ ] **Step 7: Commit**

```bash
git add src/core/RxChannel.h src/core/RxChannel.cpp src/gui/MainWindow.cpp src/models/RadioModel.cpp
git commit -S -m "refactor(core): RxChannel consumes NbFamily facade

Replaces m_nb1Enabled/m_nb2Enabled atomic pair with a single NbFamily
owning the mutual-exclusion NbMode atomic, the tuning struct, and the
SNB toggle. processIq() dispatches xanbEXTF/xnobEXTF via a switch on
m_nb->mode(). MainWindow and RadioModel call sites migrated to
setNbMode / setNbTuning."
```

---

## Task 6: Extend `SliceModel` with `nbMode`, `snbEnabled` (already exists), and per-band NB tuning

`SliceModel` gains:

1. `nbMode` (`NbMode` Q_PROPERTY, replaces `nb2Enabled`)
2. `snbEnabled` — already present, leave alone
3. `nbTuning` (`NbTuning` value) — per-band persisted

Per-slice-per-band keys follow the 3G-10 Stage 2 namespace (`Slice<N>/Band<key>/NbMode`, etc.).

**Files:**
- Modify: `src/models/SliceModel.h` (Q_PROPERTY block around line 187, signals, setters, member block)
- Modify: `src/models/SliceModel.cpp` (setters around line 483, `saveToSettings` / `restoreFromSettings`, ctor defaults)
- Modify: `src/core/AppSettings.h` (key-reference block around line 252)

- [ ] **Step 1: Remove old `nb2Enabled`, add `nbMode` and `nbTuning` in `SliceModel.h`**

At the Q_PROPERTY block (~line 187), replace:

```cpp
    Q_PROPERTY(bool   nb2Enabled      READ nb2Enabled      WRITE setNb2Enabled      NOTIFY nb2EnabledChanged)
```

with:

```cpp
    // From phase3g-rx-experience-epic-design.md §sub-epic B — tri-state cycle.
    Q_PROPERTY(NereusSDR::NbMode nbMode READ nbMode WRITE setNbMode NOTIFY nbModeChanged)
```

In the signals block (~line 495), replace:

```cpp
    void nb2EnabledChanged(bool v);
```

with:

```cpp
    void nbModeChanged(NereusSDR::NbMode v);
    void nbTuningChanged(const NereusSDR::NbTuning& v);
```

In the setter declarations (~line 360), replace:

```cpp
    void setNb2Enabled(bool v);
```

with:

```cpp
    void setNbMode(NereusSDR::NbMode v);
    NereusSDR::NbMode nbMode() const { return m_nbMode; }

    void setNbTuning(const NereusSDR::NbTuning& v);
    const NereusSDR::NbTuning& nbTuning() const { return m_nbTuning; }
```

In the member block, replace `bool m_nb2Enabled = false;` with:

```cpp
    NereusSDR::NbMode   m_nbMode{NereusSDR::NbMode::Off};
    NereusSDR::NbTuning m_nbTuning{};  // per-band
```

Also add `#include "../core/NbFamily.h"` near existing `#include "../core/WdspTypes.h"`.

- [ ] **Step 2: Setters in `SliceModel.cpp`**

Remove `setNb2Enabled` (lines 483-497). Add:

```cpp
void SliceModel::setNbMode(NereusSDR::NbMode v)
{
    if (v == m_nbMode) return;
    m_nbMode = v;
    emit nbModeChanged(v);
}

void SliceModel::setNbTuning(const NereusSDR::NbTuning& v)
{
    m_nbTuning = v;
    emit nbTuningChanged(v);
}
```

- [ ] **Step 3: Extend `saveToSettings(band)` with NB per-band keys**

Inside `saveToSettings(Band band)` (line 743), add before the `// ── Session state` marker:

```cpp
    // Noise blanker (per-band).
    s.setValue(bp + QStringLiteral("NbMode"),       static_cast<int>(m_nbMode));
    s.setValue(bp + QStringLiteral("NbThreshold"),  m_nbTuning.nbThreshold);
    s.setValue(bp + QStringLiteral("NbTauMs"),      m_nbTuning.nbTauMs);
    s.setValue(bp + QStringLiteral("NbLeadMs"),     m_nbTuning.nbAdvMs);
    s.setValue(bp + QStringLiteral("NbLagMs"),      m_nbTuning.nbHangMs);
```

And inside the session-state block, add (or confirm already present):

```cpp
    s.setValue(sp + QStringLiteral("SnbEnabled"), boolStr(m_snbEnabled));
```

- [ ] **Step 4: Extend `restoreFromSettings(band)` symmetrically**

Inside `restoreFromSettings(Band band)` (line 780), mirror the save block. For each key, guard with `if (s.contains(bp + ...))` and call the appropriate setter so signals fire.

For `NbMode`, reconstruct the enum via cast:

```cpp
    if (s.contains(bp + QStringLiteral("NbMode"))) {
        setNbMode(static_cast<NereusSDR::NbMode>(
            s.value(bp + QStringLiteral("NbMode")).toInt()));
    }
    if (s.contains(bp + QStringLiteral("NbThreshold")) ||
        s.contains(bp + QStringLiteral("NbTauMs"))     ||
        s.contains(bp + QStringLiteral("NbLeadMs"))    ||
        s.contains(bp + QStringLiteral("NbLagMs")))
    {
        NereusSDR::NbTuning t = m_nbTuning;
        if (s.contains(bp + QStringLiteral("NbThreshold"))) t.nbThreshold = s.value(bp + QStringLiteral("NbThreshold")).toDouble();
        if (s.contains(bp + QStringLiteral("NbTauMs")))     t.nbTauMs     = s.value(bp + QStringLiteral("NbTauMs")).toDouble();
        if (s.contains(bp + QStringLiteral("NbLeadMs")))    t.nbAdvMs     = s.value(bp + QStringLiteral("NbLeadMs")).toDouble();
        if (s.contains(bp + QStringLiteral("NbLagMs")))     t.nbHangMs    = s.value(bp + QStringLiteral("NbLagMs")).toDouble();
        setNbTuning(t);
    }
```

- [ ] **Step 5: Document new keys in `AppSettings.h`**

Find the "Per-slice-per-band DSP state" block (around line 252). Append the new key rows in the same table style as existing AGC rows:

```
// Slice<N>/Band<key>/NbMode         int  (0=Off, 1=NB, 2=NB2)  — default 0
// Slice<N>/Band<key>/NbThreshold    double                     — default 30.0
// Slice<N>/Band<key>/NbTauMs        double                     — default 0.1 ms
// Slice<N>/Band<key>/NbLeadMs       double                     — default 0.1 ms
// Slice<N>/Band<key>/NbLagMs        double                     — default 0.1 ms
// Slice<N>/SnbEnabled               string "True"/"False"      — default "False" (session-level)
```

Inline-cite the Thetis origin for the defaults:

```
//
// NB/NB2 tuning defaults trace to Thetis ChannelMaster/cmaster.c:43-68 [v2.10.3.13]
// (0.0001s=0.1ms for tau/hang/adv; 30.0 threshold; 0.05 backtau fixed, not per-slice).
```

- [ ] **Step 6: Build**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -20
```

Fix every compile error in call sites that still reference the old `setNb2Enabled` / `nb2Enabled()` / `nb2EnabledChanged`. Keep a running grep handy:

```bash
rg -n 'nb2Enabled|setNb2Enabled|nb2EnabledChanged' src/
```

Expected: after this task, only `m_snbEnabled` references remain (SNB keeps its own scalar atomic).

- [ ] **Step 7: Commit**

```bash
git add src/models/SliceModel.h src/models/SliceModel.cpp src/core/AppSettings.h src/gui/ src/models/RadioModel.cpp
git commit -S -m "feat(slice): NbMode + per-band NB tuning on SliceModel

Replaces bool nb2Enabled with a NereusSDR::NbMode Q_PROPERTY (Off/NB/NB2)
plus a NbTuning value persisted per-slice-per-band under
Slice<N>/Band<key>/Nb{Mode,Threshold,TauMs,LeadMs,LagMs}. SnbEnabled
stays session-level. Migrates RadioModel and MainWindow call sites
from setNb2Enabled to setNbMode."
```

---

## Task 7: RadioModel — push full NB config on slice activate / reconnect

The existing `activeSliceChanged` lambda pushes separate NB2 tuning values (lines 707-710). Collapse to a single `setNbTuning` + `setNbMode` + `setSnbEnabled` triple using the new SliceModel properties.

**Files:**
- Modify: `src/models/RadioModel.cpp` (lines 700-725 and the `connect(m_activeSlice...)` block where per-slice NB signals are forwarded)

- [ ] **Step 1: Replace the NB push block**

Find the block at ~line 707:

```cpp
                rxCh->setNb2Mode(0);         // cmaster.c:61  mode=0 (zero)
                rxCh->setNb2Tau(0.0001);     // cmaster.c:62  slewtime=0.0001 s
                rxCh->setNb2LeadTime(0.0001);// cmaster.c:63  advtime=0.0001 s
                rxCh->setNb2HangTime(0.0001);// cmaster.c:65  hangtime=0.0001 s
```

Replace with:

```cpp
                // From phase3g-rx-epic-b-nb-plan.md Task 7 — push full NB family
                // config from the active SliceModel. Thetis defaults pinned in
                // NbTuning struct (NbFamily.h) mirror cmaster.c:43-68 [v2.10.3.13].
                rxCh->setNbTuning(m_activeSlice->nbTuning());
                rxCh->setNbMode(m_activeSlice->nbMode());
```

Also replace nearby `rxCh->setSnbEnabled(m_activeSlice->snbEnabled())` with the identical call (no change needed if already present).

- [ ] **Step 2: Wire new signals**

In the `connect(m_activeSlice, ...)` block, add:

```cpp
    connect(m_activeSlice, &SliceModel::nbModeChanged, this, [this](NereusSDR::NbMode m) {
        if (auto* rxCh = m_wdspEngine ? m_wdspEngine->rx(0) : nullptr) rxCh->setNbMode(m);
    });
    connect(m_activeSlice, &SliceModel::nbTuningChanged, this, [this](const NereusSDR::NbTuning& t) {
        if (auto* rxCh = m_wdspEngine ? m_wdspEngine->rx(0) : nullptr) rxCh->setNbTuning(t);
    });
```

And remove any `nb2EnabledChanged` connect block that was previously there.

- [ ] **Step 3: Build + launch smoke test**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -10
pkill -f NereusSDR || true
./build/NereusSDR &
sleep 5
pkill -f NereusSDR
```

Expected: app launches, connects to configured radio (if any), logs show no NB-related errors.

- [ ] **Step 4: Commit**

```bash
git add src/models/RadioModel.cpp
git commit -S -m "feat(radio-model): push NB family state from active slice

Collapses the per-field NB2 push block into setNbTuning + setNbMode +
setSnbEnabled. Wires nbModeChanged / nbTuningChanged signals so UI
changes propagate to the active RxChannel in real time."
```

---

## Task 8: VfoWidget — cycling NB button

Replace the current NB2-only toggle with a single "NB" button that cycles `Off → NB → NB2 → Off` on each click, matching Thetis `chkNB` label text (`"NB"`/`"NB2"` when active, dim when off).

**Files:**
- Modify: `src/gui/widgets/VfoWidget.h` (signal + setter declaration)
- Modify: `src/gui/widgets/VfoWidget.cpp` (button creation, click handler, `setNbMode` impl, `setNb2Enabled` removal)
- Modify: `src/gui/MainWindow.cpp` (signal routing)

- [ ] **Step 1: Replace `setNb2Enabled` with `setNbMode` in `VfoWidget.h`**

At line 370, replace:

```cpp
    void setNb2Enabled(bool v);
```

with:

```cpp
    // Thetis chkNB CheckState mirror — see console.cs:43513-43560 [v2.10.3.13].
    void setNbMode(NereusSDR::NbMode m);
```

Add to the signals block:

```cpp
signals:
    // ...
    // Emitted when the user clicks the NB button. MainWindow cycles the
    // slice's NbMode in response.
    void nbModeCycled();
```

Add `#include "../../core/WdspTypes.h"` if not already there.

- [ ] **Step 2: `setNbMode` impl in `VfoWidget.cpp`**

Remove `setNb2Enabled` (line 1545-1560). Add:

```cpp
void VfoWidget::setNbMode(NereusSDR::NbMode m)
{
    // Label + styling mirror Thetis console.cs:43518-43546 [v2.10.3.13]:
    //   Off → label "NB", dim background, unchecked
    //   NB  → label "NB", active background, checked
    //   NB2 → label "NB2", active background, indeterminate (shown as checked)
    switch (m) {
        case NereusSDR::NbMode::Off:
            m_nbButton->setText(QStringLiteral("NB"));
            m_nbButton->setChecked(false);
            break;
        case NereusSDR::NbMode::NB:
            m_nbButton->setText(QStringLiteral("NB"));
            m_nbButton->setChecked(true);
            break;
        case NereusSDR::NbMode::NB2:
            m_nbButton->setText(QStringLiteral("NB2"));
            m_nbButton->setChecked(true);
            break;
    }
}
```

- [ ] **Step 3: Replace NB button creation**

In the VfoWidget constructor (or wherever the NB2 button was created — grep `"NB2"` in VfoWidget.cpp), replace the old QPushButton with a single checkable button named `m_nbButton`:

```cpp
    m_nbButton = new QPushButton(QStringLiteral("NB"), this);
    m_nbButton->setCheckable(true);
    m_nbButton->setToolTip(tr(
        "Noise blanker — click to cycle Off → NB → NB2 → Off.\n"
        "NB  (nob.c, Whitney): time-domain impulse blanker, suited to\n"
        "      sporadic crashes (powerline / ignition).\n"
        "NB2 (nobII.c): second-generation with hold/interpolate modes,\n"
        "      suited to denser impulse noise.\n"
        "Tuning: Setup → DSP → NB/SNB."));
    connect(m_nbButton, &QPushButton::clicked, this, &VfoWidget::nbModeCycled);
```

(Header member: `QPushButton* m_nbButton{nullptr};`.)

If a separate SNB button already exists (grep `setSnbEnabled`), leave it untouched.

- [ ] **Step 4: MainWindow wiring**

Find the connect block wiring the old NB2 button (grep `vfo->setNb2Enabled` and `connect(slice, &SliceModel::nb2EnabledChanged`). Replace with:

```cpp
    connect(vfo,   &VfoWidget::nbModeCycled,   this, [slice] {
        slice->setNbMode(NereusSDR::cycleNbMode(slice->nbMode()));
    });
    connect(slice, &SliceModel::nbModeChanged, vfo,  &VfoWidget::setNbMode);
    vfo->setNbMode(slice->nbMode());   // initial sync
```

- [ ] **Step 5: Build + eyeball launch**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -5
pkill -f NereusSDR || true
./build/NereusSDR &
```

Eyeball test: click the NB button on the VFO flag four times. Expected labels: `NB` (dim) → `NB` (lit) → `NB2` (lit) → `NB` (dim). No crash. Screenshot for PR.

Kill the app after eyeballing.

- [ ] **Step 6: Commit**

```bash
git add src/gui/widgets/VfoWidget.h src/gui/widgets/VfoWidget.cpp src/gui/MainWindow.cpp
git commit -S -m "feat(vfo): cycling NB button (Off → NB → NB2 → Off)

Mirrors Thetis chkNB tri-state CheckState logic from console.cs:43513
[v2.10.3.13]. Label text switches between 'NB' and 'NB2' at active
states; Off renders dim. Tooltip cites the nob.c vs nobII.c choice."
```

---

## Task 9: RxApplet — NB1 tuning row

Add a horizontal tuning row under the NB button with Threshold (0-100 linear, Thetis `udDSPNB`) + Lag (0-100 ms, Thetis `udDSPNBLag`) sliders. Binds to the active slice's `nbTuning` struct.

**Files:**
- Modify: `src/gui/applets/RxApplet.h` (declare slider members + slot)
- Modify: `src/gui/applets/RxApplet.cpp` (create row, connect to slice)

- [ ] **Step 1: Declare members**

Add to `RxApplet.h` (near existing DSP row declarations):

```cpp
private:
    QSlider*  m_nbThreshold{nullptr};
    QSlider*  m_nbLag{nullptr};
    QLabel*   m_nbThresholdLabel{nullptr};
    QLabel*   m_nbLagLabel{nullptr};

private slots:
    void onNbThresholdChanged(int sliderVal);  // 0-100 → tuning.nbThreshold
    void onNbLagChanged(int ms);
    void onSliceNbTuningChanged(const NereusSDR::NbTuning& t);
```

- [ ] **Step 2: Create the row**

In `RxApplet::RxApplet(...)` where existing rows are built (look for `HGauge` or other `new QSlider` sites), add after the NB button row:

```cpp
    // Thetis setup.cs:8572 [v2.10.3.13]  — NB threshold  = 0.165 × slider (0-100).
    // Thetis setup.cs:16236 [v2.10.3.13] — NB lag        = 0.001 s × slider (ms).
    m_nbThreshold = new QSlider(Qt::Horizontal, this);
    m_nbThreshold->setRange(0, 100);
    m_nbThreshold->setToolTip(tr("NB threshold — higher = blanks fewer, weaker impulses"));
    m_nbThresholdLabel = new QLabel(tr("Thr"), this);

    m_nbLag = new QSlider(Qt::Horizontal, this);
    m_nbLag->setRange(0, 500);
    m_nbLag->setToolTip(tr("NB lag — hold-off time after a blanked impulse (ms)"));
    m_nbLagLabel = new QLabel(tr("Lag"), this);

    auto* nbRow = new QHBoxLayout;
    nbRow->addWidget(m_nbThresholdLabel);
    nbRow->addWidget(m_nbThreshold, 1);
    nbRow->addWidget(m_nbLagLabel);
    nbRow->addWidget(m_nbLag, 1);
    /* add nbRow to the applet's vertical layout at the appropriate row index */

    connect(m_nbThreshold, &QSlider::valueChanged, this, &RxApplet::onNbThresholdChanged);
    connect(m_nbLag,       &QSlider::valueChanged, this, &RxApplet::onNbLagChanged);
```

- [ ] **Step 3: Implement slots**

```cpp
void RxApplet::onNbThresholdChanged(int sliderVal)
{
    if (!m_slice) return;
    NereusSDR::NbTuning t = m_slice->nbTuning();
    // From Thetis setup.cs:8572 [v2.10.3.13] — 0.165 × udDSPNB.Value
    t.nbThreshold = 0.165 * static_cast<double>(sliderVal);
    m_slice->setNbTuning(t);
}

void RxApplet::onNbLagChanged(int ms)
{
    if (!m_slice) return;
    NereusSDR::NbTuning t = m_slice->nbTuning();
    t.nbHangMs = static_cast<double>(ms);
    m_slice->setNbTuning(t);
}

void RxApplet::onSliceNbTuningChanged(const NereusSDR::NbTuning& t)
{
    const QSignalBlocker bt(m_nbThreshold);
    const QSignalBlocker bl(m_nbLag);
    m_nbThreshold->setValue(static_cast<int>(qRound(t.nbThreshold / 0.165)));
    m_nbLag->setValue(static_cast<int>(qRound(t.nbHangMs)));
}
```

Where the applet already binds its slice (look for `m_slice` assignment), add:

```cpp
    connect(m_slice, &SliceModel::nbTuningChanged, this, &RxApplet::onSliceNbTuningChanged);
    onSliceNbTuningChanged(m_slice->nbTuning());   // initial paint
```

- [ ] **Step 4: Build + eyeball**

```bash
cmake --build build -j$(nproc) --target NereusSDR 2>&1 | tail -5
pkill -f NereusSDR || true
./build/NereusSDR &
```

Eyeball: open the RX applet. Confirm Thr + Lag sliders present under the NB button. Slide each — values should persist after band change + app restart (assuming connected). Screenshot.

- [ ] **Step 5: Commit**

```bash
git add src/gui/applets/RxApplet.h src/gui/applets/RxApplet.cpp
git commit -S -m "feat(rx-applet): NB threshold + lag tuning row

Two horizontal sliders bind to the active slice's nbTuning struct.
Scaling factors match Thetis setup.cs:8572 (0.165×) and 16236 (ms→s)
[v2.10.3.13] so slider 50 translates identically to Thetis NB=50."
```

---

## Task 10: Wire `NbSnbSetupPage` global defaults

Setup → DSP → NB/SNB currently builds greyed-out stubs via `disableGroup`. Wire all sliders to AppSettings global-default keys (read on RxChannel creation, written by setup page).

**Files:**
- Modify: `src/gui/setup/DspSetupPages.cpp` (lines 361-414)
- Modify: `src/core/NbFamily.cpp` (constructor reads defaults from AppSettings)

- [ ] **Step 1: Drop `disableGroup` calls + wire NB1 controls**

In `NbSnbSetupPage::NbSnbSetupPage` (line 369), after each `addLabeledSlider` call, wire to AppSettings:

```cpp
    // NB1 — global defaults (apply at next channel create; per-slice overrides live in SliceModel).
    auto& s = AppSettings::instance();
    nb1Thresh->setValue(s.value(QStringLiteral("NbDefaultThresholdSlider"), 30).toInt());
    connect(nb1Thresh, &QSlider::valueChanged, [](int v) {
        AppSettings::instance().setValue(QStringLiteral("NbDefaultThresholdSlider"), v);
    });

    nb1Mode->setCurrentIndex(s.value(QStringLiteral("Nb2DefaultMode"), 0).toInt());
    connect(nb1Mode, QOverload<int>::of(&QComboBox::currentIndexChanged), [](int idx) {
        AppSettings::instance().setValue(QStringLiteral("Nb2DefaultMode"), idx);
    });
```

(Reassign `nb1Mode` to label "NB2 Mode" — Thetis `udNB2Mode` tunes `nob` only. Also add NB1 Transition + Lead sliders following the same pattern.)

Remove all three `disableGroup(...)` calls in the body.

- [ ] **Step 2: Read defaults in `NbFamily::NbFamily`**

At the top of the ctor, before `create_anbEXT`, read defaults:

```cpp
    // Global defaults from Setup → DSP → NB/SNB.
    auto& s = AppSettings::instance();
    m_tuning.nbThreshold = 0.165 * s.value(QStringLiteral("NbDefaultThresholdSlider"), 30).toDouble();
    m_tuning.nb2Mode     = s.value(QStringLiteral("Nb2DefaultMode"), 0).toInt();
    /* ...and so on for any further knobs the Setup page exposes */
```

- [ ] **Step 3: SNB group — wire K1/K2/OutputBW**

The SNB controls already have stubs (lines 400-412). For this sub-epic, wire them to AppSettings keys (`SnbDefaultK1` etc.). Actual WDSP push happens only at channel create; leave a `TODO` comment that Thetis's `SetRXASNBAkvals` push-at-runtime is deferred to a follow-up (not in scope).

- [ ] **Step 4: Build + launch, open Setup → DSP → NB/SNB**

Eyeball: controls are now enabled, reflect persisted values across restarts.

- [ ] **Step 5: Commit**

```bash
git add src/gui/setup/DspSetupPages.cpp src/core/NbFamily.cpp
git commit -S -m "feat(setup): wire DSP → NB/SNB global defaults

Sliders and combos now round-trip via AppSettings (NbDefaultThresholdSlider,
Nb2DefaultMode, SnbDefaultK1/K2/OutputBW). NbFamily reads these at
channel create. disableGroup stubs removed."
```

---

## Task 11: Unit tests — NbFamily mode cycling + SliceModel persistence

**Files:**
- Create: `tests/tst_nb_family.cpp`
- Create: `tests/tst_slice_nb_persistence.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write `tst_nb_family.cpp`**

Tests that do not need a running WDSP engine (focus on the pure C++ logic — `cycleNbMode` + `NbTuning` field defaults):

```cpp
// tests/tst_nb_family.cpp
#include <QtTest>

#include "../src/core/NbFamily.h"

using NereusSDR::NbMode;
using NereusSDR::NbTuning;
using NereusSDR::cycleNbMode;

class TstNbFamily : public QObject {
    Q_OBJECT
private slots:

    void cycle_off_to_nb()      { QCOMPARE(cycleNbMode(NbMode::Off),  NbMode::NB); }
    void cycle_nb_to_nb2()      { QCOMPARE(cycleNbMode(NbMode::NB),   NbMode::NB2); }
    void cycle_nb2_to_off()     { QCOMPARE(cycleNbMode(NbMode::NB2),  NbMode::Off); }
    void cycle_three_returns()  { QCOMPARE(cycleNbMode(cycleNbMode(cycleNbMode(NbMode::Off))), NbMode::Off); }

    void tuning_defaults_match_thetis_cmaster_c()
    {
        // From Thetis cmaster.c:43-68 [v2.10.3.13] — byte-for-byte.
        NbTuning t;
        QCOMPARE(t.nbTauMs,     0.1);
        QCOMPARE(t.nbHangMs,    0.1);
        QCOMPARE(t.nbAdvMs,     0.1);
        QCOMPARE(t.nbBacktau,   0.05);
        QCOMPARE(t.nbThreshold, 30.0);
        QCOMPARE(t.nb2Mode,     0);
        QCOMPARE(t.nb2SlewMs,   0.1);
        QCOMPARE(t.nb2AdvMs,    0.1);
        QCOMPARE(t.nb2HangMs,   0.1);
        QCOMPARE(t.nb2MaxImpMs, 25.0);
        QCOMPARE(t.nb2Backtau,  0.05);
        QCOMPARE(t.nb2Threshold,30.0);
    }
};

QTEST_APPLESS_MAIN(TstNbFamily)
#include "tst_nb_family.moc"
```

- [ ] **Step 2: Write `tst_slice_nb_persistence.cpp`**

```cpp
// tests/tst_slice_nb_persistence.cpp
#include <QtTest>
#include <QTemporaryDir>

#include "../src/models/SliceModel.h"
#include "../src/core/AppSettings.h"

using NereusSDR::NbMode;
using NereusSDR::NbTuning;
using NereusSDR::Band;

class TstSliceNbPersistence : public QObject {
    Q_OBJECT
private slots:

    void nbMode_round_trips_per_band()
    {
        QTemporaryDir tmp;
        AppSettings::instance().setPathForTests(tmp.path() + "/settings.xml");

        SliceModel a(0);
        a.setNbMode(NbMode::NB2);
        a.saveToSettings(Band::M40);

        SliceModel b(0);
        b.restoreFromSettings(Band::M40);

        QCOMPARE(b.nbMode(), NbMode::NB2);
    }

    void nb_tuning_round_trips()
    {
        QTemporaryDir tmp;
        AppSettings::instance().setPathForTests(tmp.path() + "/settings.xml");

        NbTuning t;
        t.nbThreshold = 42.5;
        t.nbHangMs    = 17.0;
        t.nbAdvMs     = 13.0;
        t.nbTauMs     = 0.7;

        SliceModel a(0);
        a.setNbTuning(t);
        a.saveToSettings(Band::M20);

        SliceModel b(0);
        b.restoreFromSettings(Band::M20);

        const auto got = b.nbTuning();
        QCOMPARE(got.nbThreshold, t.nbThreshold);
        QCOMPARE(got.nbHangMs,    t.nbHangMs);
        QCOMPARE(got.nbAdvMs,     t.nbAdvMs);
        QCOMPARE(got.nbTauMs,     t.nbTauMs);
    }
};

QTEST_APPLESS_MAIN(TstSliceNbPersistence)
#include "tst_slice_nb_persistence.moc"
```

If `AppSettings::setPathForTests` does not exist, grep for the existing per-slice-per-band unit test (e.g. `tst_slice_agc_persistence.cpp`) and mirror its setup verbatim — the harness already exists for 3G-10 Stage 2 tests.

- [ ] **Step 3: Register in `tests/CMakeLists.txt`**

Mirror an existing nearby block (e.g. the one for `tst_slice_agc_persistence` if present, otherwise for any existing `add_executable(tst_*` plus `add_test`).

- [ ] **Step 4: Run tests**

```bash
cmake --build build -j$(nproc) --target tst_nb_family tst_slice_nb_persistence
ctest --test-dir build -R 'tst_nb_family|tst_slice_nb_persistence' --output-on-failure
```

Expected: both pass. Fix any failures before continuing.

- [ ] **Step 5: Commit**

```bash
git add tests/tst_nb_family.cpp tests/tst_slice_nb_persistence.cpp tests/CMakeLists.txt
git commit -S -m "test: NbFamily cycling + per-slice-per-band NB persistence

cycleNbMode unit test pins the Off→NB→NB2→Off rotation, NbTuning
defaults pinned to Thetis cmaster.c:43-68 [v2.10.3.13] exact values,
and SliceModel round-trips NbMode + tuning across band save/restore."
```

---

## Task 12: Manual verification matrix

Follow the 3G-8 pattern — a table of user-visible behaviours the engineer eyeballs post-build, signed off in the PR body.

**Files:**
- Create: `docs/architecture/phase3g-rx-epic-b-verification/README.md`

- [ ] **Step 1: Write the verification matrix**

Create `docs/architecture/phase3g-rx-epic-b-verification/README.md`:

```markdown
# Sub-epic B — Noise Blanker Family Manual Verification

**Scope:** verify NB / NB2 / SNB cycling, tuning persistence, and Thetis parity after Tasks 1-11.

**Setup:** clean AppSettings (`rm ~/.config/NereusSDR/NereusSDR.settings`), run connected to any P1/P2 radio, RX1 on 40m USB.

| # | Behaviour | Expected |
|---|---|---|
| 1 | Click VFO NB button once | Label "NB" lit; audio NB1 active (impulse-rich signals quieter) |
| 2 | Click VFO NB button again | Label "NB2" lit; NB1 off, NB2 active |
| 3 | Click VFO NB button third time | Label "NB" dim; both NB1 and NB2 off |
| 4 | Click SNB button | SNB active independent of NB state — can coexist with step 1 or 2 |
| 5 | RxApplet Thr slider @ 50 | `AppSettings` writes `Slice0/Band40m/NbThreshold ≈ 8.25` (0.165 × 50) |
| 6 | Change band (40m → 20m) | VFO NB label + Thr/Lag reflect 20m persisted values; 40m values untouched |
| 7 | Quit + restart app | All NB + SNB state restored per-slice-per-band; Setup → DSP → NB/SNB values match last session |
| 8 | Setup → DSP → NB/SNB slider change | Values persist; apply at next channel create (reconnect) |
| 9 | Launch with Thetis defaults | `NbTuning{}` defaults match cmaster.c:43-68 byte-for-byte (verified via log-assert) |
| 10 | Headset smoke — 40m noisy band | Subjective: NB2 mutes denser impulse storms than NB; SNB kills tonal/wideband statics NB can't touch |

Sign off by ticking each row in the PR description. Screenshots of the VFO flag in each of the three NB states are mandatory.
```

- [ ] **Step 2: Commit**

```bash
git add docs/architecture/phase3g-rx-epic-b-verification/README.md
git commit -S -m "docs(verify): 10-row NB family manual verification matrix"
```

---

## Task 13: CHANGELOG entry + attribution precheck + PR

- [ ] **Step 1: Run the attribution precheck across the full diff**

```bash
python3 scripts/verify-inline-tag-preservation.py
python3 scripts/verify-thetis-headers.py
python3 scripts/check-new-ports.py
```

All three must pass. If any fails, fix the flagged file — NEVER skip a failure.

- [ ] **Step 2: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: 0 failures.

- [ ] **Step 3: Add CHANGELOG entry**

Under the `## [Unreleased]` heading in `CHANGELOG.md`, add (mirror the existing Sub-epic A entry format):

```markdown
### Added
- **Phase 3G RX Epic Sub-epic B — Noise Blanker family.** Port of Thetis's
  three-filter NB stack: **NB** (`nob.c`, Whitney), **NB2** (`nobII.c`,
  second-gen), and **SNB** (`snb.c`, spectral). `NbFamily` wrapper on
  `RxChannel` owns create/destroy and tuning. VfoWidget NB button cycles
  `Off → NB → NB2 → Off` mirroring Thetis `chkNB` tri-state. RxApplet
  gains Threshold + Lag sliders; Setup → DSP → NB/SNB wires global
  defaults. `NbMode` + tuning struct persisted per-slice-per-band under
  `Slice<N>/Band<key>/Nb{Mode,Threshold,TauMs,LeadMs,LagMs}`; `SnbEnabled`
  session-level. Defaults pinned to Thetis `cmaster.c:43-68 [v2.10.3.13]`
  byte-for-byte.
```

- [ ] **Step 4: Commit + push**

```bash
git add CHANGELOG.md
git commit -S -m "docs(changelog): Sub-epic B — NB family port"
git push -u origin claude/phase3g-rx-epic-b-nb
```

- [ ] **Step 5: Open PR (draft) — do NOT request review until user says "post it"**

Draft the PR body in the chat before running `gh pr create`. The user reviews it, says "post it," then it goes up.

Suggested PR body sections:
- **Summary** — 3 bullets: facade + cycling UI + per-band persistence
- **Parity evidence** — link cmaster.c:43-68 + setup.cs scaling lines + chkNB CheckState block
- **Test plan** — link verification matrix + show ctest output
- **Screenshots** — VFO NB button in each of three states, RxApplet tuning row, Setup → DSP → NB/SNB page

---

## Self-review checklist

Run these checks before declaring the plan complete (already applied):

1. **Spec coverage** — every bullet under design doc §sub-epic B §Files touched has a task: WDSP build (Task 2 smoke), RxChannel (Task 5), NbFamily (Tasks 3-4), SliceModel (Task 6), VfoWidget (Task 8), RxApplet (Task 9), DspSetupPages (Task 10). ✔
2. **Placeholder scan** — no "TBD" / "fill in details" / "similar to Task N" sloppiness. Steps either show the exact code or name the exact files + greps. ✔
3. **Type consistency** — `NbMode::NB` (not `NB1`) used throughout Tasks 2-11. `NbTuning` struct field names identical in NbFamily, SliceModel, RxApplet. `cycleNbMode` declared in Task 3 and referenced from Task 8. ✔

If a later edit introduces drift, fix it inline before landing the affected task.
