# Phase 3M-4 PureSignal Port: Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Source-first directive (binding for every dispatched subagent):** every task references specific Thetis source lines. READ the cited Thetis source FIRST. SHOW the original code in the commit message. TRANSLATE faithfully. STOP-AND-ASK if a value, range, default, or behavior cannot be located in the cited source. Do not infer. Do not fabricate. Paths: ramdor `/Users/j.j.boyd/Thetis/Project Files/Source/` (`v2.10.3.13 @ 501e3f51`), mi0bot `/Users/j.j.boyd/mi0bot-Thetis/Project Files/Source/` (`v2.10.3.13-beta2 @ c26a8a4c`). License preservation per `docs/attribution/HOW-TO-PORT.md`. Inline cite versioning per `feedback_inline_cite_versioning.md`. Author-tag preservation per `feedback_subagent_thetis_source_first.md`.

**Goal:** Port Thetis PureSignal (PA pre-distortion) to NereusSDR with full feature parity across 15 UI surfaces, in a single PR with 15 logical commits.

**Architecture:** 5 layers per [phase3m-4-puresignal-design.md §3](phase3m-4-puresignal-design.md): WDSP (verbatim port of `calcc.c` + `iqc.c`), C++ channel wrappers (TxChannel PS setters + new PsFeedbackChannel), per-board codec (PS DDC config in P1/P2 codecs), host coordinator (`PureSignal` C++ class managing cal lifecycle / MOX / auto-att), UI (PsForm + AmpView + PsaIndicatorWidget + Setup deltas + IMD spectrum overlay + applet wiring).

**Tech Stack:** C++20, Qt6 (Widgets, Svg, Charts module optional for AmpView), WDSP C99, FFTW3, QRhi (existing SpectrumWidget pipeline).

**Spec source:** [docs/architecture/phase3m-4-puresignal-design.md](phase3m-4-puresignal-design.md) (commit 40a1d25), 846 lines. This plan executes that spec.

---

## File Structure

### New files

| File | Responsibility |
| --- | --- |
| `third_party/wdsp/calcc.c` + `calcc.h` | Verbatim port of upstream WDSP calcc state machine (1,164 LOC) |
| `third_party/wdsp/iqc.c` + `iqc.h` | Verbatim port of upstream WDSP iqc real-time correction (315 LOC) |
| `src/core/PsFeedbackChannel.h` + `.cpp` | Wrapper for the WDSP PS feedback RX channel id |
| `src/core/PureSignal.h` + `.cpp` | Host-side PS coordinator: cal lifecycle, MOX integration, auto-attention, status polling, save/restore (~300 LOC) |
| `src/gui/PsForm.h` + `.cpp` | Modeless dialog port of Thetis `PSForm.cs` (1,164 LOC); single flat form + Advanced collapse mode |
| `src/gui/AmpViewWindow.h` + `.cpp` | Modeless dialog port of Thetis `AmpView.cs` (528 LOC); chart with 5 curves + 4 toggles |
| `src/gui/PsaIndicatorWidget.h` + `.cpp` | Bottom-banner FB + PS label pair port of Thetis `ucInfoBar.cs:827-1098` |
| `src/core/codec/P2CodecG2.h` + `.cpp` | New per-board codec (G2 family) if not already covered by `P2CodecOrionMkII` |
| `src/core/codec/P1CodecHermesII.h` + `.cpp` | New P1 codec (HermesII / ANAN-10E / ANAN-100B) if not already covered by `P1CodecStandard` |

### Modified files

| File | What changes |
| --- | --- |
| `src/core/BoardCapabilities.h` + `.cpp` | Add `psDefaultPeak` (float) and `psSampleRate` (int) fields; populate per-model |
| `src/core/TxChannel.h` + `.cpp` | Add 22 WDSP PS API wrappers (15 setters + 4 readers + 3 routing) |
| `src/core/codec/P2CodecOrionMkII.{h,cpp}` | Add `applyPureSignalDdcConfig` virtual method override |
| `src/core/codec/P1CodecHl2.{h,cpp}` | Add `applyPureSignalDdcConfig` (mi0bot deltas) |
| `src/core/codec/P1CodecStandard.{h,cpp}` | Add `applyPureSignalDdcConfig` (no-op stub for non-PS boards) |
| `src/core/codec/CodecContext.h` | Add `PsDdcConfig` struct + virtual method declaration |
| `src/core/ReceiverManager.h` + `.cpp` | Port `UpdateDDCs` PS branch from `console.cs:8186-8538` |
| `src/core/MicProfileManager.{h,cpp}` | Add `Pure_Signal_Enabled` key (key index TBD; see plan-write step) |
| `src/gui/MainWindow.h` + `.cpp` | Add `Tools > PureSignal...` menu, wire `PsaIndicatorWidget` into bottom-banner HBox after `m_rxDashboard` and before `m_stationBlock`, add `openPureSignalDialog()` slot |
| `src/gui/applets/PureSignalApplet.{h,cpp}` | Wire all 7 buttons + 2 gauges + 3 LEDs + 3 info labels to `PureSignal` coordinator. Per-control right-click → `MainWindow::openPureSignalDialog()` |
| `src/gui/applets/TxApplet.{h,cpp}` | Un-hide `m_psaBtn`, wire left-click to `PureSignal::setAutoCalEnabled`, right-click to `MainWindow::openPureSignalDialog()` |
| `src/gui/setup/hardware/AntennaAlexAlex1Tab.{h,cpp}` | Wire existing `m_hpfBypassOnPs` checkbox: add IMD warning dialog on un-check, wire toggle to `console.DisableHPFonPS` equivalent |
| `src/gui/setup/GeneralOptionsPage.{h,cpp}` | Add 2 new checkboxes inside the existing Options group: "Hide feedback level" + "Swap red and blue PS-A feedback colours" |
| `src/gui/SpectrumWidget.{h,cpp}` | Add IMD overlay layer (peak markers + readout box). **Coordinate with carson-branch** if peak-blob infrastructure lands first |
| `src/gui/setup/HardwarePage.{h,cpp}` | RETIRE: remove PureSignal tab from tab list + capability gating |
| `src/gui/setup/TransmitSetupPages.{h,cpp}` | RETIRE: remove `PureSignalPage` class and registration |
| `CMakeLists.txt` | Wire new third_party/wdsp/calcc.c + iqc.c into the wdsp static lib build |

### Deleted files

| File | Reason |
| --- | --- |
| `src/gui/setup/hardware/PureSignalTab.h` + `.cpp` | NereusSDR-only stub; no Thetis equivalent |

### New tests

| File | Coverage |
| --- | --- |
| `tests/tst_puresignal_caps.cpp` | `BoardCapabilities::psDefaultPeak` + `psSampleRate` per-model |
| `tests/tst_tx_channel_ps_setters.cpp` | TxChannel PS API wrappers (signature + thread-safety) |
| `tests/tst_ps_feedback_channel.cpp` | PsFeedbackChannel lifecycle |
| `tests/tst_codec_ps_ddc_config.cpp` | Per-board codec PS DDC config wire bytes (G2-class + HL2 + Standard) |
| `tests/tst_receiver_manager_ps_ddc.cpp` | UpdateDDCs PS state machine for diversity x MOX x PS combinations |
| `tests/tst_puresignal_coordinator.cpp` | PureSignal class cal lifecycle + MOX + auto-att |
| `tests/tst_psform.cpp` | PsForm dialog: control wiring, Advanced collapse, Save/Restore gating |
| `tests/tst_ampview_window.cpp` | AmpView: 5 chart series, 4 toolbar toggles, GetPSDisp wiring |
| `tests/tst_psa_indicator.cpp` | PsaIndicatorWidget 6-state machine, color encoding, click handlers |
| `tests/tst_setup_deltas.cpp` | HPF Bypass IMD warning + 2 General Options PS checkboxes |
| `tests/tst_imd_overlay.cpp` | SpectrumWidget IMD overlay: peak detection, IMD3/IMD5 sort, readout box |
| `tests/tst_applet_ps_wiring.cpp` | PureSignalApplet + TxApplet [PS-A] left/right-click behavior |
| `tests/tst_micprofile_ps_enabled.cpp` | Pure_Signal_Enabled per-profile round-trip |

### New documentation

| File | Coverage |
| --- | --- |
| `docs/architecture/phase3m-4-verification.md` | Bench acceptance matrix (15 surfaces × verification steps); HW required; bench procedures |

---

## Conventions for every task

- **Build cmd:** `cmake --build build -j$(nproc)`
- **Test cmd:** `ctest --test-dir build -V -R <test_name>`
- **Commit:** GPG-signed via `git commit` (no `--no-gpg-sign`); per `feedback_gpg_sign_commits.md`
- **Inline cite format:** `// From Thetis <file>:<line> [v2.10.3.13]` or `// From mi0bot-Thetis <file>:<line> [v2.10.3.13-beta2]`. Per `feedback_inline_cite_versioning.md`
- **Author-tag preservation:** every Thetis `//MW0LGE` / `//W2PA` / `//DH1KLM` / `//G8NJJ` / `//MI0BOT` etc. tag within ±5 lines of cited Thetis line is preserved verbatim. Per `feedback_subagent_thetis_source_first.md`
- **License headers:** byte-for-byte from upstream + NereusSDR Modification History trailing block. Per `docs/attribution/HOW-TO-PORT.md`. CI-enforced via `scripts/verify-thetis-headers.py`
- **Branch:** `claude/wizardly-poitras-dbab75` (current worktree)
- **Verifiers:** every commit triggers pre-commit hooks: `verify-thetis-headers.py`, `check-new-ports.py`, `verify-inline-cites.py`, `compliance-inventory.py`, `verify-inline-tag-preservation.py`. All must pass green
- **Skip-test guard:** if writing tests that need hardware, mark with `[hwbench]` tag; never skip via stub

---

# Phase 1: Foundation (commits 1-3)

## Task 1: BoardCapabilities (psDefaultPeak + psSampleRate)

**Files:**
- Modify: `src/core/BoardCapabilities.h`, `src/core/BoardCapabilities.cpp`
- Test: `tests/tst_puresignal_caps.cpp`

### Step 1.1: Read Thetis HardwareSpecific.PSDefaultPeak source

- [ ] Locate per-model `PSDefaultPeak` values in Thetis source

```bash
grep -nE "PSDefaultPeak\s*=" "/Users/j.j.boyd/Thetis/Project Files/Source/Console/clsHardwareSpecific.cs" | head -30
```

Expected: per-`HPSDRModel` literal float assignments (e.g. ANAN-G2: `0.2899`, etc.). Record actual values at `[v2.10.3.13]`.

If file not found or values are computed rather than literal, STOP and report. Do NOT fabricate values.

### Step 1.2: Write failing cap-fields test

- [ ] Create `tests/tst_puresignal_caps.cpp`

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later
// tests/tst_puresignal_caps.cpp (NereusSDR)
#include <QtTest>
#include "core/BoardCapabilities.h"

using namespace NereusSDR;

class TstPureSignalCaps : public QObject {
    Q_OBJECT
private slots:
    void g2HasPureSignalAndPsRate192k();
    void hl2HasPureSignalAndRx1RatePsSampleRate();
    void atlasHasNoPureSignal();
};

void TstPureSignalCaps::g2HasPureSignalAndPsRate192k() {
    auto caps = BoardCapabilities::forModel(HpsdrModel::ANAN_G2);
    QVERIFY(caps.hasPureSignal);
    QCOMPARE(caps.psSampleRate, 192000);
    // Source-cite: psDefaultPeak from clsHardwareSpecific.cs PSDefaultPeak switch (TBD-verify exact value at task time)
    QVERIFY(caps.psDefaultPeak > 0.0f);
}

void TstPureSignalCaps::hl2HasPureSignalAndRx1RatePsSampleRate() {
    auto caps = BoardCapabilities::forModel(HpsdrModel::HERMES_LITE);
    QVERIFY(caps.hasPureSignal);
    QCOMPARE(caps.psSampleRate, 0); // 0 = "use rx1_rate" sentinel; verified at task time vs mi0bot console.cs:8476-8480
}

void TstPureSignalCaps::atlasHasNoPureSignal() {
    auto caps = BoardCapabilities::forModel(HpsdrModel::ATLAS);
    QVERIFY(!caps.hasPureSignal);
}

QTEST_MAIN(TstPureSignalCaps)
#include "tst_puresignal_caps.moc"
```

- [ ] Wire test into `tests/CMakeLists.txt`

```cmake
nereus_add_test(tst_puresignal_caps tst_puresignal_caps.cpp)
```

### Step 1.3: Run test, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
ctest --test-dir build -V -R tst_puresignal_caps
```

Expected: compile error on `caps.psDefaultPeak` / `caps.psSampleRate` (fields not yet defined).

### Step 1.4: Add fields to `BoardCapabilities.h`

- [ ] Insert into the `BoardCapabilities` struct (after `hasPureSignal`):

```cpp
// PureSignal hardware-default peak. From Thetis clsHardwareSpecific.cs PSDefaultPeak switch [v2.10.3.13].
// Used by PsForm to flag drift via pbWarningSetPk (PSForm.cs:802).
float psDefaultPeak = 0.0f;

// PureSignal feedback channel sample rate during MOX.
// 192000 for G2-class boards (cmaster.cs:424 ps_rate=192000 [v2.10.3.13]).
// 0 sentinel = "use current rx1_rate" for HL2 (mi0bot console.cs:8476-8480 [v2.10.3.13-beta2]:
// "HL2 can work at a high sample rate", uses Rate[0]=rx1_rate during PS).
int psSampleRate = 192000;
```

### Step 1.5: Populate per-model in `BoardCapabilities.cpp`

- [ ] In each `forModel()` case-arm, set `psDefaultPeak` + `psSampleRate`. Use values verified in Step 1.1. Examples (verify at task time):

```cpp
// ANAN-G2 / G2-1K / 100D / 200D / 7000DLE / 8000DLE / OrionMkII / ANVELINAPRO3
caps.hasPureSignal = true;
caps.psDefaultPeak = 0.2899f;  // From clsHardwareSpecific.cs:N [v2.10.3.13] ← verify exact line
caps.psSampleRate = 192000;

// HermesLite (HL2)
caps.hasPureSignal = true;
caps.psDefaultPeak = /* TBD per mi0bot HardwareSpecific.cs */;
caps.psSampleRate = 0;  // sentinel meaning "use rx1_rate" per mi0bot console.cs:8476-8480 [v2.10.3.13-beta2]

// Atlas / classic Hermes / Angelia / classic Orion / Hermes-Lite v1
caps.hasPureSignal = false;  // (already in scaffolding)
```

### Step 1.6: Run test, verify PASS

```bash
ctest --test-dir build -V -R tst_puresignal_caps
```

Expected: 3/3 PASS.

### Step 1.7: Commit

```bash
git add src/core/BoardCapabilities.h src/core/BoardCapabilities.cpp \
        tests/tst_puresignal_caps.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(caps): add psDefaultPeak + psSampleRate to BoardCapabilities

Foundation for Phase 3M-4 PureSignal port. psDefaultPeak ports per-model
PS hw peak from Thetis clsHardwareSpecific.cs [v2.10.3.13]. psSampleRate
is 192000 for G2-class (cmaster.cs:424) or 0 sentinel for HL2 (mi0bot
console.cs:8476-8480 [v2.10.3.13-beta2], "HL2 can work at a high sample
rate"). Both consumed by per-board codec applyPureSignalDdcConfig() and
the PureSignal coordinator.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Vendor calcc.c + iqc.c verbatim from Thetis

**Files:**
- Create: `third_party/wdsp/calcc.c`, `third_party/wdsp/calcc.h`, `third_party/wdsp/iqc.c`, `third_party/wdsp/iqc.h`
- Modify: `third_party/wdsp/CMakeLists.txt` (or top-level `CMakeLists.txt` depending on existing wdsp build wiring)
- Test: `tests/tst_wdsp_ps_smoke.cpp` (smoke test that calcc/iqc can be linked + entry points called)

### Step 2.1: Copy upstream files verbatim

- [ ] Copy from ramdor Thetis (DO NOT MODIFY):

```bash
cp "/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/calcc.c" third_party/wdsp/calcc.c
cp "/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/calcc.h" third_party/wdsp/calcc.h
cp "/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/iqc.c"   third_party/wdsp/iqc.c
cp "/Users/j.j.boyd/Thetis/Project Files/Source/wdsp/iqc.h"   third_party/wdsp/iqc.h
```

The license headers are already byte-for-byte from upstream (since we just copied verbatim). DO NOT add a NereusSDR Modification History block until after verifying the file builds cleanly.

### Step 2.2: Run header verifier on the new files

```bash
python3 scripts/verify-thetis-headers.py --files third_party/wdsp/calcc.c third_party/wdsp/calcc.h third_party/wdsp/iqc.c third_party/wdsp/iqc.h
```

Expected: PASS (license headers verbatim from upstream).

### Step 2.3: Add Modification History trailing block

For each of the 4 files, append at the very end of the existing file header (after the upstream license block):

```c
// =================================================================
// Modification history (NereusSDR):
//   2026-MM-DD: Vendored verbatim from Thetis v2.10.3.13 @501e3f51
//                 by J.J. Boyd (KG4VCF), with AI-assisted source-first
//                 protocol via Anthropic Claude Code. No source-level
//                 modifications. Cross-platform compatibility provided
//                 via existing third_party/wdsp/linux_port.h shim.
// =================================================================
```

(Replace `MM-DD` with the current date.)

### Step 2.4: Wire calcc.c + iqc.c into wdsp build

- [ ] Examine current `third_party/wdsp/CMakeLists.txt`:

```bash
grep -nE "calcc|iqc|add_library|target_sources" third_party/wdsp/CMakeLists.txt
```

- [ ] If calcc/iqc not in source list, add:

```cmake
target_sources(wdsp PRIVATE
    calcc.c
    iqc.c
    # ... other existing sources ...
)
```

- [ ] Run header check script in `--all-kinds` mode (catches WDSP-side checks):

```bash
python3 scripts/verify-thetis-headers.py --all-kinds 2>&1 | tail -10
```

### Step 2.5: Write smoke test

- [ ] Create `tests/tst_wdsp_ps_smoke.cpp`:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>

extern "C" {
    void SetPSRunCal(int channel, int run);
    void SetPSMox(int channel, int mox);
    void GetPSInfo(int channel, int* info);
}

class TstWdspPsSmoke : public QObject {
    Q_OBJECT
private slots:
    void linksAndCanCallSetPSRunCal();
};

void TstWdspPsSmoke::linksAndCanCallSetPSRunCal() {
    // Must not crash; calcc internals will be uninitialized so we just verify linkage.
    // In real usage, channel must be opened via OpenChannel() first, but for a smoke
    // test we just confirm the WDSP library exposes the symbol.
    int info[16] = {};
    SetPSRunCal(99, 0);  // Bogus channel, expect graceful no-op or guard inside.
    GetPSInfo(99, info);
    QCOMPARE(info[0], 0); // No-op: counters stay zero.
}

QTEST_MAIN(TstWdspPsSmoke)
#include "tst_wdsp_ps_smoke.moc"
```

- [ ] Wire into `tests/CMakeLists.txt` with link to wdsp lib.

### Step 2.6: Run smoke test

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -V -R tst_wdsp_ps_smoke
```

Expected: PASS.

### Step 2.7: Run all verifiers

```bash
python3 scripts/verify-thetis-headers.py --all-kinds
python3 scripts/check-new-ports.py
python3 scripts/verify-inline-tag-preservation.py
```

All must pass.

### Step 2.8: Commit

```bash
git add third_party/wdsp/calcc.c third_party/wdsp/calcc.h \
        third_party/wdsp/iqc.c   third_party/wdsp/iqc.h \
        third_party/wdsp/CMakeLists.txt \
        tests/tst_wdsp_ps_smoke.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
chore(wdsp): vendor calcc.c + iqc.c verbatim from Thetis

Foundation for Phase 3M-4 PureSignal port. calcc.c (1,164 LOC, calcc
state machine) and iqc.c (315 LOC, real-time IQ correction) ported
verbatim from Thetis v2.10.3.13 @501e3f51. License headers byte-for-byte
from upstream + NereusSDR Modification History trailing block per
docs/attribution/HOW-TO-PORT.md. Cross-platform via existing linux_port.h.
Smoke-tested via tst_wdsp_ps_smoke (verifies link + symbol export).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: TxChannel PS API wrappers (22 functions)

**Files:**
- Modify: `src/core/TxChannel.h`, `src/core/TxChannel.cpp`
- Test: `tests/tst_tx_channel_ps_setters.cpp`

### Step 3.1: Verify exact WDSP signatures

- [ ] Read `third_party/wdsp/calcc.c` lines 891-1132 and confirm signatures match the table below. STOP-AND-ASK if any mismatch.

| Function | Signature | calcc.c line |
| --- | --- | --- |
| `SetPSRunCal` | `void SetPSRunCal(int channel, int run)` | 891 |
| `SetPSMox` | `void SetPSMox(int channel, int mox)` | 901 |
| `GetPSInfo` | `void GetPSInfo(int channel, int* info)` | 914 (info is int[16]) |
| `SetPSReset` | `void SetPSReset(int channel, int reset)` | 924 |
| `SetPSMancal` | `void SetPSMancal(int channel, int mancal)` | 934 |
| `SetPSAutomode` | `void SetPSAutomode(int channel, int automode)` | 942 |
| `SetPSTurnon` | `void SetPSTurnon(int channel, int turnon)` | 950 |
| `SetPSControl` | `void SetPSControl(int channel, int reset, int mancal, int automode, int turnon)` | 958 |
| `SetPSLoopDelay` | `void SetPSLoopDelay(int channel, double delay)` | 971 |
| `SetPSMoxDelay` | `void SetPSMoxDelay(int channel, double delay)` | 982 |
| `SetPSTXDelay` | `double SetPSTXDelay(int channel, double delay)` (returns double!) | 993 |
| `SetPSHWPeak` | `void SetPSHWPeak(int channel, double peak)` | 1016 |
| `GetPSHWPeak` | `void GetPSHWPeak(int channel, double* peak)` | 1026 |
| `GetPSMaxTX` | `void GetPSMaxTX(int channel, double* maxtx)` | 1034 |
| `SetPSPtol` | `void SetPSPtol(int channel, double ptol)` | 1042 |
| `GetPSDisp` | `void GetPSDisp(int channel, double* x, double* ym, double* yc, double* ys, double* cm, double* cc, double* cs)` (7 buffers!) | 1050 |
| `SetPSFeedbackRate` | `void SetPSFeedbackRate(int channel, int rate)` | 1065 |
| `SetPSPinMode` | `void SetPSPinMode(int channel, int pin)` | 1094 |
| `SetPSMapMode` | `void SetPSMapMode(int channel, int map)` | 1102 |
| `SetPSStabilize` | `void SetPSStabilize(int channel, int stbl)` | 1110 |
| `SetPSIntsAndSpi` | `void SetPSIntsAndSpi(int channel, int ints, int spi)` | 1132 |

Plus 2 routing P/Invokes from `cmaster.cs` (no calcc.c entry; routed through `ChannelMaster.dll`):

| Function | Signature | cmaster.cs line |
| --- | --- | --- |
| `SetPSRxIdx` | `void SetPSRxIdx(int id, int idx)` | 146-147 |
| `SetPSTxIdx` | `void SetPSTxIdx(int id, int idx)` | 143-144 |

### Step 3.2: Add WDSP extern declarations

- [ ] In `src/core/TxChannel.cpp` (or a shared `wdsp_extern.h` if one exists), add:

```cpp
extern "C" {
    // PureSignal calcc API. From Thetis wdsp/calcc.c:891-1132 [v2.10.3.13].
    void SetPSRunCal(int channel, int run);
    void SetPSMox(int channel, int mox);
    void GetPSInfo(int channel, int* info);
    void SetPSReset(int channel, int reset);
    void SetPSMancal(int channel, int mancal);
    void SetPSAutomode(int channel, int automode);
    void SetPSTurnon(int channel, int turnon);
    void SetPSControl(int channel, int reset, int mancal, int automode, int turnon);
    void SetPSLoopDelay(int channel, double delay);
    void SetPSMoxDelay(int channel, double delay);
    double SetPSTXDelay(int channel, double delay);
    void SetPSHWPeak(int channel, double peak);
    void GetPSHWPeak(int channel, double* peak);
    void GetPSMaxTX(int channel, double* maxtx);
    void SetPSPtol(int channel, double ptol);
    void GetPSDisp(int channel, double* x, double* ym, double* yc, double* ys,
                                  double* cm, double* cc, double* cs);
    void SetPSFeedbackRate(int channel, int rate);
    void SetPSPinMode(int channel, int pin);
    void SetPSMapMode(int channel, int map);
    void SetPSStabilize(int channel, int stbl);
    void SetPSIntsAndSpi(int channel, int ints, int spi);

    // PureSignal channel routing. From Thetis cmaster.cs:143-147 [v2.10.3.13].
    void SetPSRxIdx(int id, int idx);
    void SetPSTxIdx(int id, int idx);
}
```

### Step 3.3: Write failing wrapper test

- [ ] Create `tests/tst_tx_channel_ps_setters.cpp`:

```cpp
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QtTest>
#include "core/TxChannel.h"
#include "core/WdspEngine.h"

using namespace NereusSDR;

class TstTxChannelPsSetters : public QObject {
    Q_OBJECT
private slots:
    void initTestCase();
    void cleanupTestCase();
    void setPsControlForwardsAllArgs();
    void setPsHwPeakRoundTripsViaGet();
    void getPsInfoReturnsSixteenInts();
    void setPsTxDelayReturnsCompensatedDelay();

private:
    WdspEngine* m_engine = nullptr;
    TxChannel* m_tx = nullptr;
};

void TstTxChannelPsSetters::initTestCase() {
    m_engine = new WdspEngine(this);
    m_engine->initialize();  // Opens PS feedback channel + TX channel
    m_tx = m_engine->txChannel();
}

void TstTxChannelPsSetters::cleanupTestCase() {
    delete m_engine;
}

void TstTxChannelPsSetters::setPsControlForwardsAllArgs() {
    // SetPSControl args (reset, mancal, automode, turnon) per calcc.c:958
    m_tx->setPSControl(1, 0, 0, 0);  // reset
    int info[16] = {};
    m_tx->getPSInfo(info);
    QCOMPARE(info[0] /* state */, 0); // LRESET state per calcc.c
}

void TstTxChannelPsSetters::setPsHwPeakRoundTripsViaGet() {
    m_tx->setPSHWPeak(0.2899);
    QCOMPARE(m_tx->getPSHWPeak(), 0.2899);
}

void TstTxChannelPsSetters::getPsInfoReturnsSixteenInts() {
    int info[16] = {};
    m_tx->getPSInfo(info);
    // Confirm pointer-write happened: at least one slot should be writeable
    // (state index 0 is always written by calcc.c GetPSInfo).
    QVERIFY(info[0] >= 0);
}

void TstTxChannelPsSetters::setPsTxDelayReturnsCompensatedDelay() {
    // SetPSTXDelay returns the delay it actually applied (may snap to fractional steps)
    double applied = m_tx->setPSTXDelay(0.000020); // 20us
    QVERIFY(applied >= 0.0);
}

QTEST_MAIN(TstTxChannelPsSetters)
#include "tst_tx_channel_ps_setters.moc"
```

- [ ] Wire into `tests/CMakeLists.txt`.

### Step 3.4: Run, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: compile error on `m_tx->setPSControl` etc. (methods not yet defined).

### Step 3.5: Add TxChannel PS API to header

- [ ] In `src/core/TxChannel.h`, add to the public section (matching existing setter style):

```cpp
public:
    // PureSignal API wrappers. Each method delegates to the corresponding
    // WDSP entry point. Channel-id parameter is implicit (this->m_channelId).
    // From Thetis cmaster.cs DllImports + wdsp/calcc.c:891-1132 [v2.10.3.13].

    void setPSRunCal(int run);
    void setPSMox(bool mox);
    void getPSInfo(int* info16);  // info must be int[16]
    void setPSReset(bool reset);
    void setPSMancal(bool mancal);
    void setPSAutomode(bool automode);
    void setPSTurnon(bool turnon);
    void setPSControl(int reset, int mancal, int automode, int turnon);
    void setPSLoopDelay(double seconds);
    void setPSMoxDelay(double seconds);
    double setPSTXDelay(double seconds);  // returns delay actually applied
    void setPSHWPeak(double peak);
    double getPSHWPeak();
    double getPSMaxTX();
    void setPSPtol(double ptol);
    void getPSDisp(double* x, double* ym, double* yc, double* ys,
                   double* cm, double* cc, double* cs);
    void setPSFeedbackRate(int rate);
    void setPSPinMode(bool pin);
    void setPSMapMode(bool map);
    void setPSStabilize(bool stbl);
    void setPSIntsAndSpi(int ints, int spi);

    // Channel routing. Called once at PS init. From cmaster.cs:533-534 [v2.10.3.13].
    static void setPSRxIdx(int txid, int idx);
    static void setPSTxIdx(int txid, int idx);
```

### Step 3.6: Implement wrappers in `TxChannel.cpp`

- [ ] Each is a one-line forward to the WDSP entry point. Example:

```cpp
void TxChannel::setPSControl(int reset, int mancal, int automode, int turnon) {
    ::SetPSControl(m_channelId, reset, mancal, automode, turnon);
}

void TxChannel::setPSMox(bool mox) {
    ::SetPSMox(m_channelId, mox ? 1 : 0);
}

void TxChannel::getPSInfo(int* info16) {
    ::GetPSInfo(m_channelId, info16);
}

double TxChannel::setPSTXDelay(double seconds) {
    return ::SetPSTXDelay(m_channelId, seconds);
}

double TxChannel::getPSHWPeak() {
    double peak = 0.0;
    ::GetPSHWPeak(m_channelId, &peak);
    return peak;
}

double TxChannel::getPSMaxTX() {
    double maxtx = 0.0;
    ::GetPSMaxTX(m_channelId, &maxtx);
    return maxtx;
}

void TxChannel::getPSDisp(double* x, double* ym, double* yc, double* ys,
                          double* cm, double* cc, double* cs) {
    ::GetPSDisp(m_channelId, x, ym, yc, ys, cm, cc, cs);
}

// Static channel routing. Called once at PS init.
void TxChannel::setPSRxIdx(int txid, int idx) {
    ::SetPSRxIdx(txid, idx);
}

void TxChannel::setPSTxIdx(int txid, int idx) {
    ::SetPSTxIdx(txid, idx);
}

// (...similar one-liners for the remaining setters...)
```

### Step 3.7: Run test, verify PASS

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -V -R tst_tx_channel_ps_setters
```

Expected: 4/4 PASS.

### Step 3.8: Commit

```bash
git add src/core/TxChannel.h src/core/TxChannel.cpp \
        tests/tst_tx_channel_ps_setters.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(tx-channel): add 22 PureSignal API wrappers

Wraps WDSP calcc.c:891-1132 + cmaster.cs:143-147 PS API. 19 setters
(SetPSRunCal/Mox/Reset/Mancal/Automode/Turnon/Control/LoopDelay/
MoxDelay/TXDelay/HWPeak/Ptol/FeedbackRate/PinMode/MapMode/Stabilize/
IntsAndSpi) + 4 readers (GetPSInfo/HWPeak/MaxTX/Disp) + 2 routing
(SetPSRxIdx/TxIdx). Note SetPSTXDelay returns double (compensated
delay). Note GetPSDisp takes 7 output buffers (Ref/MagAmp/PhsAmp/
MagCorr/PhsCorr feed AmpView). Per Phase 3M-4 plan §Task 3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

# Phase 2: Wire (commits 4-7)

## Task 4: PsFeedbackChannel class

**Files:**
- Create: `src/core/PsFeedbackChannel.h`, `src/core/PsFeedbackChannel.cpp`
- Modify: `src/core/WdspEngine.{h,cpp}` (add `psFeedbackChannel()` accessor + lifecycle)
- Test: `tests/tst_ps_feedback_channel.cpp`

### Step 4.1: Read existing RxChannel for the pattern

```bash
grep -nE "class RxChannel|RxChannel::|m_channelId|setSampleRate" src/core/RxChannel.h | head -20
```

Use the same pattern: holds a channel ID, exposes `setSampleRate(int)`, `feedSamples(const float*, int)`, `channelId()`.

### Step 4.2: Write failing test

- [ ] Create `tests/tst_ps_feedback_channel.cpp`:

```cpp
#include <QtTest>
#include "core/PsFeedbackChannel.h"
#include "core/WdspEngine.h"

using namespace NereusSDR;

class TstPsFeedbackChannel : public QObject {
    Q_OBJECT
private slots:
    void hasUniqueChannelId();
    void sampleRateRoundTrips();
    void feedSamplesIncreasesActivityCounter();
};

void TstPsFeedbackChannel::hasUniqueChannelId() {
    WdspEngine engine;
    engine.initialize();
    auto* fb = engine.psFeedbackChannel();
    QVERIFY(fb != nullptr);
    QVERIFY(fb->channelId() != engine.txChannel()->channelId());
}

void TstPsFeedbackChannel::sampleRateRoundTrips() {
    WdspEngine engine;
    engine.initialize();
    engine.psFeedbackChannel()->setSampleRate(192000);
    QCOMPARE(engine.psFeedbackChannel()->sampleRate(), 192000);
}

void TstPsFeedbackChannel::feedSamplesIncreasesActivityCounter() {
    WdspEngine engine;
    engine.initialize();
    auto* fb = engine.psFeedbackChannel();
    auto before = fb->totalSamplesIn();
    float buf[1024] = {};
    fb->feedSamples(buf, 512);  // 512 complex pairs
    QVERIFY(fb->totalSamplesIn() > before);
}

QTEST_MAIN(TstPsFeedbackChannel)
#include "tst_ps_feedback_channel.moc"
```

### Step 4.3: Run, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

Expected: compile error (PsFeedbackChannel.h not found).

### Step 4.4: Implement `PsFeedbackChannel.h` + `.cpp`

```cpp
// src/core/PsFeedbackChannel.h
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QObject>
#include <atomic>

namespace NereusSDR {

class WdspEngine;

// Wraps the WDSP feedback RX channel that calcc reads autonomously.
// One per WdspEngine. Created when the engine initializes, destroyed
// at engine shutdown. The host's role: set sample rate per board and
// pump samples in via feedSamples(). DSP runs autonomously inside WDSP.
//
// Source-first context:
//   - calcc reads from this channel via pscc() (autonomous, calcc.c:617).
//   - SetPSRxIdx(0, 0) routes Stream0 here (cmaster.cs:533 [v2.10.3.13]).
class PsFeedbackChannel : public QObject {
    Q_OBJECT
public:
    explicit PsFeedbackChannel(int channelId, QObject* parent = nullptr);
    ~PsFeedbackChannel() override;

    int channelId() const { return m_channelId; }
    int sampleRate() const { return m_sampleRate.load(); }
    void setSampleRate(int rate);

    // Push interleaved I/Q samples (size = number of complex pairs).
    void feedSamples(const float* iqInterleaved, int size);

    qint64 totalSamplesIn() const { return m_totalSamples.load(); }

private:
    int m_channelId;
    std::atomic<int> m_sampleRate{192000};
    std::atomic<qint64> m_totalSamples{0};
};

} // namespace NereusSDR
```

```cpp
// src/core/PsFeedbackChannel.cpp
#include "PsFeedbackChannel.h"
extern "C" void SetInputSamplerate(int channel, int rate);  // From wdsp.c
extern "C" void fexchange0(int channel, double* in, double* out, int* error);  // RX-side feed

namespace NereusSDR {

PsFeedbackChannel::PsFeedbackChannel(int channelId, QObject* parent)
    : QObject(parent), m_channelId(channelId) {}

PsFeedbackChannel::~PsFeedbackChannel() = default;

void PsFeedbackChannel::setSampleRate(int rate) {
    m_sampleRate.store(rate);
    ::SetInputSamplerate(m_channelId, rate);
}

void PsFeedbackChannel::feedSamples(const float* iqInterleaved, int size) {
    // TBD-verify: confirm WDSP RX-channel feed API (fexchange0 or similar)
    // matches existing RxChannel pattern. See src/core/RxChannel.cpp for precedent.
    // For now, increment counter; actual fexchange wiring verified at task time.
    m_totalSamples.fetch_add(size);
    // ::fexchange0(m_channelId, /* converted I/Q */, /* output buffer (unused for PS) */, &err);
}

} // namespace NereusSDR
```

- [ ] Add accessor to `WdspEngine`: open the PS feedback channel during `initialize()`, store as member, expose via `psFeedbackChannel()` getter. Use the existing OpenChannel pattern.

### Step 4.5: Run test, verify PASS

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -V -R tst_ps_feedback_channel
```

Expected: 3/3 PASS.

### Step 4.6: Commit

```bash
git add src/core/PsFeedbackChannel.h src/core/PsFeedbackChannel.cpp \
        src/core/WdspEngine.h src/core/WdspEngine.cpp \
        src/core/CMakeLists.txt \
        tests/tst_ps_feedback_channel.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(core): add PsFeedbackChannel wrapper for WDSP PS feedback channel

Holds the WDSP channel id that calcc autonomously reads via pscc()
(calcc.c:617 [v2.10.3.13]). Sample rate is per-board (192000 for
G2-class, rx1_rate for HL2 mi0bot). Routed via SetPSRxIdx(0, 0) per
cmaster.cs:533-534. WdspEngine owns one instance, opened during
initialize().

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Per-board codec applyPureSignalDdcConfig

**Files:**
- Modify: `src/core/codec/CodecContext.h` (add `PsDdcConfig` struct + virtual method)
- Modify: `src/core/codec/P2CodecOrionMkII.{h,cpp}`, `src/core/codec/P1CodecHl2.{h,cpp}`, `src/core/codec/P1CodecStandard.{h,cpp}`
- Possibly create: `src/core/codec/P2CodecG2.{h,cpp}`, `src/core/codec/P1CodecHermesII.{h,cpp}` (verify at task whether OrionMkII / Standard already covers these board families)
- Test: `tests/tst_codec_ps_ddc_config.cpp`

### Step 5.1: Read Thetis UpdateDDCs PS branches verbatim

- [ ] Read ramdor `console.cs:8186-8538` for G2-class. Capture every per-board branch's exact wire bytes (`cntrl1`, `cntrl2`, `Rate[]`, `DDCEnable`, `SyncEnable`, `P1_DDCConfig`, `P1_rxcount`).

```bash
sed -n '8186,8290p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/console.cs"
```

- [ ] Read mi0bot `console.cs:8409-8488` for HL2.

```bash
sed -n '8409,8488p' "/Users/j.j.boyd/mi0bot-Thetis/Project Files/Source/Console/console.cs"
```

- [ ] Read ramdor `console.cs:8453+` for HermesII / ANAN-10E / ANAN-100B.

If branch-by-branch values are non-trivial to extract, transcribe a per-board table to a comment in the codec file before coding.

### Step 5.2: Define `PsDdcConfig` struct

- [ ] In `src/core/codec/CodecContext.h`, add:

```cpp
namespace NereusSDR {

// PureSignal DDC config bytes/words emitted by per-board codec.
// Consumed by ReceiverManager::updateDdcAssignment() during MOX+PS-on transitions.
// Field semantics: from Thetis console.cs:8186-8538 UpdateDDCs() [v2.10.3.13]
// (and mi0bot console.cs:8409-8488 [v2.10.3.13-beta2] for HL2 deltas).
struct PsDdcConfig {
    uint8_t  cntrl1     = 0;     // ADC control register 1
    uint8_t  cntrl2     = 0;     // ADC control register 2
    uint32_t rate[8]    = {};    // Per-DDC sample rates
    uint8_t  ddcEnable  = 0;     // Bit mask of enabled DDCs
    uint8_t  syncEnable = 0;     // Bit mask of synced DDCs
    int      p1DdcConfig = 0;    // P1-only board-config code (3 for G2-class, 6 for HL2)
    int      p1RxCount  = 0;     // P1-only RX count for state-machine
    int      nDdc       = 0;
};

} // namespace NereusSDR
```

### Step 5.3: Add virtual method to base codec

- [ ] In `src/core/codec/CodecContext.h` (or wherever the codec base class lives), add:

```cpp
virtual PsDdcConfig applyPureSignalDdcConfig(
    bool psEnabled,
    bool diversityEnabled,
    bool moxState,
    int rx1Rate,
    int rx2Rate,
    bool rx2Enabled
) const = 0;
```

- [ ] Default implementation in the base (PS not supported):

```cpp
// Default (non-PS boards): returns zeros, ddcEnable=0.
inline PsDdcConfig applyPureSignalDdcConfigDefault() {
    return PsDdcConfig{}; // all zeros
}
```

### Step 5.4: Write per-codec tests (one test per board family)

- [ ] Create `tests/tst_codec_ps_ddc_config.cpp` with one test per board family. Example for G2:

```cpp
void TstCodecPsDdcConfig::g2WithPsOnNoDiversityProducesCorrectBytes() {
    P2CodecOrionMkII codec;  // (or P2CodecG2 if separate)
    auto cfg = codec.applyPureSignalDdcConfig(
        /*psEnabled=*/true, /*diversityEnabled=*/false,
        /*moxState=*/true,  /*rx1Rate=*/48000, /*rx2Rate=*/0, /*rx2Enabled=*/false);

    // From Thetis console.cs:8264-8266 [v2.10.3.13] (G2-class, PS-on, no-diversity, MOX):
    //   DDCEnable = DDC0 + DDC2 = 1 + 4 = 5
    //   SyncEnable = DDC1 = 2
    //   Rate[0] = ps_rate = 192000
    //   Rate[1] = ps_rate = 192000
    //   Rate[2] = rx1_rate = 48000
    //   cntrl1 = (rx_adc_ctrl1 & 0xf3) | 0x08
    //   cntrl2 = rx_adc_ctrl2 & 0x3f
    //   P1_DDCConfig = 3
    QCOMPARE(cfg.ddcEnable, 0x05u);
    QCOMPARE(cfg.syncEnable, 0x02u);
    QCOMPARE(cfg.rate[0], 192000u);
    QCOMPARE(cfg.rate[1], 192000u);
    QCOMPARE(cfg.rate[2], 48000u);
    QCOMPARE(cfg.p1DdcConfig, 3);
}

void TstCodecPsDdcConfig::hl2WithPsOnProducesCorrectBytes() {
    P1CodecHl2 codec;
    auto cfg = codec.applyPureSignalDdcConfig(
        /*psEnabled=*/true, /*diversityEnabled=*/false,
        /*moxState=*/true, /*rx1Rate=*/48000, /*rx2Rate=*/0, /*rx2Enabled=*/false);

    // From mi0bot console.cs:8472-8488 [v2.10.3.13-beta2] (HL2, PS-on, MOX):
    //   DDCEnable = DDC0 = 1
    //   SyncEnable = DDC1 = 2
    //   Rate[0] = rx1_rate = 48000  (HL2 can run any rate)
    //   Rate[1] = rx1_rate = 48000
    //   cntrl1 = 4
    //   cntrl2 = 0
    //   P1_DDCConfig = 6
    QCOMPARE(cfg.ddcEnable, 0x01u);
    QCOMPARE(cfg.syncEnable, 0x02u);
    QCOMPARE(cfg.rate[0], 48000u);
    QCOMPARE(cfg.rate[1], 48000u);
    QCOMPARE(cfg.cntrl1, 4u);
    QCOMPARE(cfg.cntrl2, 0u);
    QCOMPARE(cfg.p1DdcConfig, 6);
}

void TstCodecPsDdcConfig::standardCodecReturnsAllZeros() {
    P1CodecStandard codec;
    auto cfg = codec.applyPureSignalDdcConfig(true, false, true, 48000, 0, false);
    QCOMPARE(cfg.ddcEnable, 0u);
    QCOMPARE(cfg.cntrl1, 0u);
}
```

(Add additional tests for diversity-on, MOX-off, RX2-on permutations covering each branch from `console.cs:8186-8538`.)

### Step 5.5: Run, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

Expected: compile error, virtual method not implemented in subclasses.

### Step 5.6: Implement per-codec methods

- [ ] In `P2CodecOrionMkII.cpp` (covers G2 family unless verified to need P2CodecG2 split):

```cpp
PsDdcConfig P2CodecOrionMkII::applyPureSignalDdcConfig(
    bool psEnabled, bool diversityEnabled, bool moxState,
    int rx1Rate, int rx2Rate, bool rx2Enabled) const
{
    PsDdcConfig cfg;
    cfg.p1RxCount = 5;  // From console.cs:8214 [v2.10.3.13]: P1_rxcount = 5
    cfg.nDdc = 5;

    constexpr uint8_t DDC0 = 1, DDC1 = 2, DDC2 = 4, DDC3 = 8;
    constexpr int ps_rate = 192000;  // From cmaster.cs:424 [v2.10.3.13]

    // Assume rx_adc_ctrl1 + rx_adc_ctrl2 are passed in or accessible via member.
    // For now, use the documented mask pattern; concrete value injection TBD at task.
    const uint8_t rx_adc_ctrl1 = 0;  // TBD: pass through from caller or member
    const uint8_t rx_adc_ctrl2 = 0;

    if (!moxState) {
        // RX-only branch: DDC2 = RX1, no PS feedback
        cfg.p1DdcConfig = 1;
        cfg.ddcEnable = DDC2;
        cfg.rate[0] = static_cast<uint32_t>(rx1Rate);  // p1 only, see console.cs:8243 MW0LGE comment
        cfg.rate[2] = static_cast<uint32_t>(rx1Rate);
        cfg.cntrl1 = rx_adc_ctrl1 & 0xff;
        cfg.cntrl2 = rx_adc_ctrl2 & 0x3f;
    } else if (psEnabled && !diversityEnabled) {
        // PS-on, no-diversity, MOX: DDC0+DDC2 sync DDC1, ps_rate.
        // From console.cs:8261-8268 [v2.10.3.13]
        cfg.p1DdcConfig = 3;
        cfg.ddcEnable = DDC0 | DDC2;
        cfg.syncEnable = DDC1;
        cfg.rate[0] = ps_rate;
        cfg.rate[1] = ps_rate;
        cfg.rate[2] = static_cast<uint32_t>(rx1Rate);
        cfg.cntrl1 = static_cast<uint8_t>((rx_adc_ctrl1 & 0xf3) | 0x08);
        cfg.cntrl2 = rx_adc_ctrl2 & 0x3f;
    }
    // ... (additional branches: PS-on with diversity, !PS, etc.) Each transcribed verbatim
    // from console.cs:8186-8538 [v2.10.3.13].

    if (rx2Enabled) {
        cfg.ddcEnable |= DDC3;
        cfg.rate[3] = static_cast<uint32_t>(rx2Rate);
    }
    return cfg;
}
```

- [ ] In `P1CodecHl2.cpp`:

```cpp
PsDdcConfig P1CodecHl2::applyPureSignalDdcConfig(
    bool psEnabled, bool diversityEnabled, bool moxState,
    int rx1Rate, int rx2Rate, bool rx2Enabled) const
{
    PsDdcConfig cfg;
    constexpr uint8_t DDC0 = 1, DDC1 = 2;
    cfg.p1RxCount = 2;
    cfg.nDdc = 2;

    if (moxState && psEnabled && !diversityEnabled) {
        // From mi0bot console.cs:8472-8488 [v2.10.3.13-beta2]:
        //   "transmitting and PS is ON" branch
        cfg.p1DdcConfig = 6;
        cfg.ddcEnable = DDC0;
        cfg.syncEnable = DDC1;
        // MI0BOT: HL2 can work at a high sample rate
        cfg.rate[0] = static_cast<uint32_t>(rx1Rate);
        cfg.rate[1] = static_cast<uint32_t>(rx1Rate);
        cfg.cntrl1 = 4;
        cfg.cntrl2 = 0;
    } else if (!moxState && !diversityEnabled && !psEnabled) {
        cfg.p1DdcConfig = 4;
        cfg.ddcEnable = DDC0;
        cfg.rate[0] = static_cast<uint32_t>(rx1Rate);
        if (rx2Enabled) {
            cfg.ddcEnable |= DDC1;
            cfg.rate[1] = static_cast<uint32_t>(rx2Rate);
        }
    }
    // ... additional HL2 branches transcribed verbatim from mi0bot console.cs

    return cfg;
}
```

- [ ] In `P1CodecStandard.cpp` (no-PS stub):

```cpp
PsDdcConfig P1CodecStandard::applyPureSignalDdcConfig(
    bool, bool, bool, int, int, bool) const
{
    return PsDdcConfig{};  // all zeros; PS not supported on this board
}
```

### Step 5.7: Run test, verify PASS

```bash
cmake --build build -j$(nproc)
ctest --test-dir build -V -R tst_codec_ps_ddc_config
```

Expected: all per-codec tests pass.

### Step 5.8: Commit

```bash
git add src/core/codec/*.h src/core/codec/*.cpp \
        tests/tst_codec_ps_ddc_config.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(codec): add applyPureSignalDdcConfig per-board

Per-board PS DDC config bytes ported verbatim from Thetis
console.cs:8186-8538 [v2.10.3.13] (G2-class, OrionMkII, HermesII) and
mi0bot console.cs:8409-8488 [v2.10.3.13-beta2] (HL2 single-ADC mode-
switched). G2-class uses (rx_adc_ctrl1 & 0xf3) | 0x08 + ps_rate=192000;
HL2 uses cntrl1=4 + rx1_rate. Standard codec stubs to all-zeros.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: ReceiverManager UpdateDDCs PS branch

**Files:**
- Modify: `src/core/ReceiverManager.h`, `src/core/ReceiverManager.cpp`
- Test: `tests/tst_receiver_manager_ps_ddc.cpp`

### Step 6.1: Locate existing UpdateDDCs equivalent

```bash
grep -nE "updateDdc|UpdateDDC|ddcConfig|cmdHighPriority" src/core/ReceiverManager.cpp | head
```

If a function already exists, augment it. If not, create `updateDdcAssignment()`.

### Step 6.2: Write failing integration test

- [ ] In `tests/tst_receiver_manager_ps_ddc.cpp`, set up a fake `RadioConnection` that captures the wire bytes, configure the manager for ANAN-G2 + PS-on + MOX, and assert the captured bytes match the expected `PsDdcConfig`.

```cpp
void TstReceiverManagerPsDdc::g2PsOnMoxSendsExpectedBytes() {
    auto fakeConn = std::make_shared<FakeRadioConnection>();
    auto* mgr = new ReceiverManager(fakeConn, BoardCapabilities::forModel(HpsdrModel::ANAN_G2));
    mgr->setPureSignalEnabled(true);
    mgr->setMox(true);

    auto sent = fakeConn->lastDdcConfigBytes();
    QCOMPARE(sent.cntrl1, static_cast<uint8_t>(0x08));  // (0 & 0xf3) | 0x08 = 0x08
    QCOMPARE(sent.ddcEnable, 0x05);  // DDC0 + DDC2
    QCOMPARE(sent.rate[0], 192000);
    QCOMPARE(sent.p1DdcConfig, 3);
}
```

### Step 6.3: Run, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

### Step 6.4: Implement `updateDdcAssignment`

- [ ] In `ReceiverManager.cpp`:

```cpp
// Source: ports Thetis console.cs UpdateDDCs() (lines 8186-8538) [v2.10.3.13],
// with mi0bot console.cs:8409-8488 [v2.10.3.13-beta2] HL2 deltas.
void ReceiverManager::updateDdcAssignment() {
    auto cfg = m_codec->applyPureSignalDdcConfig(
        m_psEnabled, m_diversityEnabled, m_moxState,
        m_rx1Rate, m_rx2Rate, m_rx2Enabled);
    m_connection->writeDdcConfig(cfg);
}
```

- [ ] Hook to `setPureSignalEnabled(bool)`, `setMox(bool)`, etc., to call `updateDdcAssignment()` on every relevant change.

### Step 6.5: Run, verify PASS

```bash
ctest --test-dir build -V -R tst_receiver_manager_ps_ddc
```

### Step 6.6: Commit

```bash
git add src/core/ReceiverManager.h src/core/ReceiverManager.cpp \
        tests/tst_receiver_manager_ps_ddc.cpp tests/CMakeLists.txt
git commit -m "feat(receiver-manager): port UpdateDDCs PS branch from Thetis console.cs:8186-8538

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 7: PureSignal coordinator class

**Files:**
- Create: `src/core/PureSignal.h`, `src/core/PureSignal.cpp`
- Modify: `src/models/RadioModel.{h,cpp}` to own a `PureSignal*`
- Modify: `src/models/TransmitModel.{h,cpp}` to flip `isPureSignalActiveForTest()` predicate seam to live state
- Test: `tests/tst_puresignal_coordinator.cpp`

### Step 7.1: Read Thetis PSForm.cs timer1code + ucInfoBar update

```bash
sed -n '530,650p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/PSForm.cs"
```

Capture the polling loop, FB level read, CorrectionsBeingApplied flag, CalibrationCount field.

### Step 7.2: Write failing tests for cal lifecycle

- [ ] `tests/tst_puresignal_coordinator.cpp`:

```cpp
void TstPureSignalCoordinator::singleCalibrateTriggersCalcc() {
    // Mock TxChannel that records SetPSControl calls.
    MockTxChannel tx;
    PureSignal ps(&engine, &tx, &fb, &mox);
    ps.singleCalibrate();
    QCOMPARE(tx.lastSetPSControlArgs(), std::make_tuple(1, 0, 0, 0));
    // From Thetis PSForm.cs ForcePS [v2.10.3.13]:
    //   if (!_autoON) puresignal.SetPSControl(_txachannel, 1, 0, 0, 0);  // single
    //   else          puresignal.SetPSControl(_txachannel, 0, 0, 1, 0);  // auto
}

void TstPureSignalCoordinator::setAutoCalEnabledTriggersAutoMode() {
    PureSignal ps(...);
    ps.setAutoCalEnabled(true);
    QCOMPARE(tx.lastSetPSControlArgs(), std::make_tuple(0, 0, 1, 0));
}

void TstPureSignalCoordinator::moxUpInformsCalcc() {
    PureSignal ps(...);
    ps.setEnabled(true);
    mox.setMox(true);
    QCOMPARE(tx.lastSetPSMoxArg(), true);
}

void TstPureSignalCoordinator::feedbackLevelOutOfRangeTriggersAutoAttention() {
    PureSignal ps(...);
    ps.setEnabled(true);
    ps.setAutoCalEnabled(true);
    ps.setMockFeedbackLevel(220);  // overdriven
    ps.tickAutoAttention();
    QVERIFY(stepAtt.lastIncrement() > 0);  // pulled ATT up
}
```

### Step 7.3: Run, verify FAIL

```bash
cmake --build build -j$(nproc) 2>&1 | tail -10
```

### Step 7.4: Implement `PureSignal.h` skeleton

(Use the skeleton from design doc §8.1, fleshed out with bodies that delegate to `m_tx->setPSControl()` etc.)

### Step 7.5: Run, verify PASS

```bash
ctest --test-dir build -V -R tst_puresignal_coordinator
```

### Step 7.6: Wire `TransmitModel::isPureSignalActiveForTest()` predicate seam to live state

```cpp
// In TransmitModel, replace the test-stub predicate with a getter that reads the live PureSignal state:
bool TransmitModel::isPureSignalActive() const {
    return m_radioModel->pureSignal() && m_radioModel->pureSignal()->correctionsBeingApplied();
}
```

Update all call sites (StepAttenuatorController etc.) accordingly.

### Step 7.7: Commit

```bash
git add src/core/PureSignal.h src/core/PureSignal.cpp \
        src/models/RadioModel.h src/models/RadioModel.cpp \
        src/models/TransmitModel.h src/models/TransmitModel.cpp \
        tests/tst_puresignal_coordinator.cpp tests/CMakeLists.txt
git commit -m "feat(core): add PureSignal coordinator class

Cal lifecycle, MOX integration, auto-attention sub-state machine,
status polling, save/restore, two-tone test integration. Ports the
host-side logic from Thetis PSForm.cs timer1code + ucInfoBar driver
(\\u005bv2.10.3.13\\u005d). Wires TransmitModel::isPureSignalActiveForTest seam
to live state so existing 3G-13 StepAttenuatorController integration
fires correctly.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

# Phase 3: UI (commits 8-12)

## Task 8: PsForm modeless dialog

**Files:**
- Create: `src/gui/PsForm.h`, `src/gui/PsForm.cpp`
- Modify: `src/gui/MainWindow.{h,cpp}` (add `Tools > PureSignal...` menu + `openPureSignalDialog()` slot)
- Test: `tests/tst_psform.cpp`

### Step 8.1: Read Thetis PSForm.designer.cs

```bash
sed -n '40,260p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/PSForm.designer.cs"
```

Inventory: 23 controls per design doc §4 #1. Match positions, sizes, text verbatim.

### Step 8.2: Write failing test

- [ ] `tests/tst_psform.cpp`: open the dialog, assert each control exists by objectName, verify Advanced button collapses ClientSize from 560×300 to 560×60 (per `PSForm.cs:889-905`), verify Save button gating on `correctionsBeingApplied`.

### Step 8.3: Implement `PsForm.h` + `.cpp`

Build with QGridLayout / QHBoxLayout / QVBoxLayout matching the designer positions. Bind every control to `m_pureSignal` (via `m_radioModel->pureSignal()`) using `QSignalBlocker` to prevent feedback loops on programmatic updates. Connect:
- `m_btnSingleCal` → `m_pureSignal->singleCalibrate()`
- `m_chkAutoCal` → `m_pureSignal->setAutoCalEnabled(checked)`
- `m_btnSave` → `QFileDialog::getSaveFileName` + `m_pureSignal->saveCorrections(filename)`
- `m_btnRestore` → `QFileDialog::getOpenFileName` + `m_pureSignal->restoreCorrections(filename)`
- `m_btnTwoTone` → `m_pureSignal->setTwoToneOn(checked)`
- `m_btnAmpView` → `MainWindow::openAmpViewWindow()` (or open directly via `m_ampView->show()`)
- `m_btnAdvanced` → `setAdvancedView(!_advancedON)` toggling ClientSize per Thetis behavior
- 100 ms `QTimer` polls `m_pureSignal->getInfo()` and updates `lblPSInfoFB`, `lblPSInfoCO`, `lblPSfb2`, `lblPSInfo*` labels

### Step 8.4: Wire `Tools > PureSignal...` menu in MainWindow

```cpp
// MainWindow.cpp constructor
auto* toolsMenu = menuBar()->addMenu(tr("&Tools"));
m_psFormAction = toolsMenu->addAction(tr("&PureSignal..."));
connect(m_psFormAction, &QAction::triggered,
        this, &MainWindow::openPureSignalDialog);

void MainWindow::openPureSignalDialog() {
    if (!m_psForm) m_psForm = new PsForm(m_radioModel, this);
    m_psForm->show();
    m_psForm->raise();
    m_psForm->activateWindow();
}

// Capability gating: hide menu when no PS
m_psFormAction->setVisible(currentCaps.hasPureSignal);
```

### Step 8.5: Run test, verify PASS, commit

```bash
git add src/gui/PsForm.h src/gui/PsForm.cpp \
        src/gui/MainWindow.h src/gui/MainWindow.cpp \
        tests/tst_psform.cpp tests/CMakeLists.txt
git commit -m "feat(gui): port PsForm modeless dialog (Tools > PureSignal)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 9: AmpViewWindow modeless dialog

**Files:**
- Create: `src/gui/AmpViewWindow.h`, `src/gui/AmpViewWindow.cpp`
- Modify: `src/gui/PsForm.cpp` (wire `m_btnAmpView` to open AmpView)
- Test: `tests/tst_ampview_window.cpp`

### Step 9.1: Read Thetis AmpView.designer.cs and AmpView.cs:125-200

Capture chart series setup + GetPSDisp polling + 4 toggle behaviors.

### Step 9.2: Write failing test

```cpp
void TstAmpViewWindow::hasFiveChartSeries() {
    AmpViewWindow win(model);
    QCOMPARE(win.chartSeriesCount(), 5);
    QStringList expected = {"Ref", "MagAmp", "PhsAmp", "MagCorr", "PhsCorr"};
    QCOMPARE(win.chartSeriesNames(), expected);
}

void TstAmpViewWindow::phaseZoomTogglesPhaseAxisRange() {
    AmpViewWindow win(model);
    auto rangeBefore = win.phaseAxisRange();
    win.setPhaseZoom(true);
    auto rangeAfter = win.phaseAxisRange();
    QVERIFY(rangeAfter.span() < rangeBefore.span());
}

void TstAmpViewWindow::lowResSkipsEverySecondPoint() {
    AmpViewWindow win(model);
    win.setLowRes(true);
    QCOMPARE(win.lowResStride(), 4);  // Per AmpView.cs note "low res will skip every 4"
}
```

### Step 9.3-9.5: Implement, run, commit

Use Qt6 `QChart` (from QtCharts module) or custom `QPainter` widget; choose at task time per design doc §15 TBD #14. Wire `m_btnAmpView` in PsForm to open this window with `FixAmpViewOnTop()` lifecycle.

```bash
git commit -m "feat(gui): port AmpView modeless dialog (opened from PsForm btnPSAmpView)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 10: PsaIndicatorWidget bottom-banner pair

**Files:**
- Create: `src/gui/PsaIndicatorWidget.h`, `src/gui/PsaIndicatorWidget.cpp`
- Modify: `src/gui/MainWindow.cpp` (wire into bottom-banner HBox after `m_rxDashboard`, before `m_stationBlock`)
- Test: `tests/tst_psa_indicator.cpp`

### Step 10.1: Read Thetis ucInfoBar.cs:827-1098

```bash
sed -n '820,1100p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/ucInfoBar.cs"
```

### Step 10.2: Write failing test (6-state machine)

```cpp
void TstPsaIndicator::psOffShowsDimGray() {
    PsaIndicatorWidget w;
    w.setPsEnabled(false);
    QCOMPARE(w.fbBackgroundColor(), QColor("#5a5a5a"));
    QCOMPARE(w.psBackgroundColor(), QColor("#5a5a5a"));
    QCOMPARE(w.fbText(), QString("Feedback"));
}

void TstPsaIndicator::psOnTxCorrectingShowsLimePlusNumeric() {
    PsaIndicatorWidget w;
    w.setPsEnabled(true);
    w.setMox(true);
    w.setCorrectionsBeingApplied(true);
    w.setFeedbackLevel(163);
    QCOMPARE(w.fbText(), QString("163"));
    QCOMPARE(w.psText(), QString("Correcting"));
    QCOMPARE(w.psBackgroundColor(), QColor("#5fdf5f"));  // Lime
}

void TstPsaIndicator::leftClickFbTogglesInvertRedBlue() {
    PsaIndicatorWidget w;
    QSignalSpy spy(&w, &PsaIndicatorWidget::invertRedBlueRequested);
    w.simulateLeftClickOnFb();
    QCOMPARE(spy.count(), 1);
}

void TstPsaIndicator::rightClickFbTogglesHideFeedback() {
    PsaIndicatorWidget w;
    QSignalSpy spy(&w, &PsaIndicatorWidget::hideFeedbackToggleRequested);
    w.simulateRightClickOnFb();
    QCOMPARE(spy.count(), 1);
}

// All 6 state transitions tested.
```

### Step 10.3-10.5: Implement, run, commit

Implement using QLabel pair with custom mousePressEvent for left/right click handling. Bind to `PureSignal` Q_PROPERTY signals for state updates. Tooltip updates per `setToolTips()` from `ucInfoBar.cs:1082-1098`.

Wire into MainWindow bottom-banner HBox:

```cpp
// MainWindow.cpp around line 2680, after m_rxDashboard, before m_stationBlock:
m_psaIndicator = new PsaIndicatorWidget(this);
m_psaIndicator->setVisible(currentCaps.hasPureSignal);
hbox->addWidget(m_psaIndicator);
```

```bash
git commit -m "feat(gui): PsaIndicatorWidget bottom-banner FB+PS pair

6-state machine + left/right click handlers + dynamic tooltip per
Thetis ucInfoBar.cs:827-1098 [v2.10.3.13]. Color encoding verified:
Blue 0-90 / Yellow 91-128 / Green 129-181 / Red 182+ (default un-
swapped). Wired into MainWindow bottom-banner HBox after
m_rxDashboard, before m_stationBlock.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 11: Setup deltas (HPF warning + General Options 2 PS checkboxes)

**Files:**
- Modify: `src/gui/setup/hardware/AntennaAlexAlex1Tab.{h,cpp}` (wire existing `m_hpfBypassOnPs` checkbox)
- Modify: `src/gui/setup/GeneralOptionsPage.{h,cpp}` (add 2 new checkboxes)
- Test: `tests/tst_setup_deltas.cpp`

### Step 11.1: Add IMD warning dialog wire

```cpp
// AntennaAlexAlex1Tab.cpp toggle handler:
connect(m_hpfBypassOnPs, &QCheckBox::toggled, this, [this](bool checked) {
    if (!checked) {
        // From Thetis setup.cs:29275-29295 [v2.10.3.13]: warning dialog
        QMessageBox box(QMessageBox::Warning, tr("PureSignal Issue"),
            tr("Including the BPFs during a PureSignal transmission may produce "
               "passive Inter-Modulation Distortion in the inductors of the bandpass filters.\n\n"
               "You will NOT be able to observe this degraded performance on the panadapter "
               "because PS is correcting to the distorted feedback and the panadapter is \"seeing\" "
               "that same distorted feedback. It can only be observed with an external spectrum analyzer.\n\n"
               "Please ensure you understand the implications of including the BPFs when "
               "transmitting a PureSignal based signal. It is not recommended."),
            QMessageBox::Ok | QMessageBox::Cancel, this);
        box.setDefaultButton(QMessageBox::Cancel);  // MessageBoxDefaultButton.Button2
        if (box.exec() == QMessageBox::Cancel) {
            QSignalBlocker block(m_hpfBypassOnPs);
            m_hpfBypassOnPs->setChecked(true);
            return;
        }
    }
    AppSettings::instance().setValue("hardware/" + m_macKey + "/disableHPFonPS",
                                     checked ? "True" : "False");
    // After AppSettings persists, emit a signal here (e.g. hpfBypassOnPsChanged(bool))
    // for RadioModel to apply live to the PureSignal feedback path. Wire signal at task time.
});
```

### Step 11.2: Add 2 new checkboxes to General Options group

```cpp
// GeneralOptionsPage.cpp, inside buildOptionsGroup():
m_chkHideFeedback = new QCheckBox(tr("Hide feedback level"), group);
m_chkHideFeedback->setToolTip(tr("When checked, bottom-banner FB shows \"Feedback\" "
                                  "text instead of the numeric level. Mirror of FB-label right-click."));
group->layout()->addWidget(m_chkHideFeedback);
connect(m_chkHideFeedback, &QCheckBox::toggled, this, [](bool on) {
    AppSettings::instance().setValue("ui/hideFeedbackLevel", on ? "True" : "False");
});

m_chkSwapRedBlue = new QCheckBox(tr("Swap red and blue PS-A feedback colours"), group);
m_chkSwapRedBlue->setToolTip(tr("For users with red/blue color blindness or alternate display "
                                 "preferences. Mirror of FB-label left-click."));
group->layout()->addWidget(m_chkSwapRedBlue);
connect(m_chkSwapRedBlue, &QCheckBox::toggled, this, [](bool on) {
    AppSettings::instance().setValue("ui/invertRedBluePSA", on ? "True" : "False");
});
```

### Step 11.3: Tests verify warning dialog appears + Cancel reverts + checkboxes round-trip

### Step 11.4: Commit

```bash
git commit -m "feat(setup): wire HPF Bypass on PureSignal feedback warning + General Options PS checkboxes

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 12: SpectrumWidget IMD overlay

**Files:**
- Modify: `src/gui/SpectrumWidget.h`, `src/gui/SpectrumWidget.cpp`
- Test: `tests/tst_imd_overlay.cpp`
- **Coordinate with carson-branch:** if peak-blob infrastructure has landed, build on top; else port peak-detection here and let carson-branch refactor later.

### Step 12.1: Verify carson-branch status

```bash
git log --oneline claude/wizardly-carson-a79b0e --not main 2>/dev/null | head
ls /Users/j.j.boyd/NereusSDR/.claude/worktrees/wizardly-carson-a79b0e
```

If carson-branch contains peak-detection code, plan to merge or rebase before starting. If not, proceed.

### Step 12.2: Read Thetis display.cs IMD overlay

```bash
sed -n '5200,5700p' "/Users/j.j.boyd/Thetis/Project Files/Source/Console/display.cs"
```

Capture: peak detection (`5283-5298`), peak ellipse markers (`5453-5475`), IMD3/IMD5 sort + `findImd` (`5512-5560`), readout box (`5520`), text format (`5650-5685`), EMA constants (search `_ema_`).

### Step 12.3: Write failing tests for peak detection + IMD3 sort + readout text

```cpp
void TstImdOverlay::peakDetectionFindsTwoFundamentals() {
    // Synth spectrum with two peaks at -22.5 dBm at bin 340 and 460 (per mockup data)
    std::vector<float> bins(800, -110.0f);
    bins[340] = bins[460] = -22.5f;
    bins[280] = bins[520] = -65.0f; // IMD3 sidebands
    bins[200] = bins[600] = -88.0f; // IMD5 sidebands

    auto peaks = ImdOverlay::detectPeaks(bins, /*triggerDelta=*/3.0f);
    QCOMPARE(peaks.size(), 6u);
    // Top 2 should be the fundamentals
    auto sorted = peaks;
    std::sort(sorted.begin(), sorted.end(),
              [](auto& a, auto& b) { return a.dBm > b.dBm; });
    QCOMPARE(sorted[0].x, 460);  // or 340
    QCOMPARE(sorted[1].x, 340);  // or 460
}

void TstImdOverlay::readoutTextFormatMatchesThetis() {
    auto text = ImdOverlay::formatReadout(-22.55, -22.30, -64.73, -65.10, -88.21, -89.05,
                                          -42.18, -65.66, 24.50, 12.30);
    // Per display.cs:5650-5685 [v2.10.3.13]
    QVERIFY(text.contains("    f0 L\n"));
    QVERIFY(text.contains("    f0 U\n"));
    QVERIFY(text.contains("IMD3 L\n"));
    QVERIFY(text.contains("    -42.18 dBc\n"));
    QVERIFY(text.contains("    24.50 dB"));   // OIP3
}
```

### Step 12.4-12.6: Implement, run, commit

Implement peak detection + IMD sort + readout overlay in `SpectrumWidget` overlay layer (QPainter on a child overlay widget, OR new QRhi layer if carson-branch has set up the pipeline).

```bash
git commit -m "feat(spectrum): two-tone IMD overlay (peak markers + readout box)

Ports display.cs:5008/5283-5298/5453-5475/5512-5560/5650-5685 [v2.10.3.13].
Renders only when MOX && testingIMD && showIMDMeasurements && displayDuplex.
Box 260x180 px rounded 14 px at X=50, Y=50. 3-column readout: f0 L/U, IMD3 L/U,
IMD5 L/U + worst IMD3 dBc + worst IMD5 dBc + OIP3 + OIP5. EMA-smoothed.
Coordinated with claude/wizardly-carson-a79b0e peak-blob infrastructure.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

# Phase 4: Integration & polish (commits 13-15)

## Task 13: Applet wiring (PureSignalApplet + TxApplet [PS-A])

**Files:**
- Modify: `src/gui/applets/PureSignalApplet.{h,cpp}` (wire all 7 buttons + 2 gauges + 3 LEDs + 3 info labels)
- Modify: `src/gui/applets/TxApplet.{h,cpp}` (un-hide `m_psaBtn`, wire left + right click)
- Test: `tests/tst_applet_ps_wiring.cpp`

### Step 13.1: PureSignalApplet wiring

```cpp
// PureSignalApplet.cpp constructor (replacing the NyiOverlay::markNyi calls):
auto* ps = m_radioModel->pureSignal();

connect(m_calibrateBtn, &QPushButton::clicked, ps, &PureSignal::singleCalibrate);
connect(m_autoCalBtn, &QPushButton::toggled, ps, &PureSignal::setAutoCalEnabled);
connect(m_saveBtn, &QPushButton::clicked, this, [this, ps]() {
    QString file = QFileDialog::getSaveFileName(
        this, tr("Save PureSignal corrections"),
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/PureSignal/");
    if (!file.isEmpty()) ps->saveCorrections(file);
});
connect(m_restoreBtn, &QPushButton::clicked, this, [this, ps]() {
    QString file = QFileDialog::getOpenFileName(
        this, tr("Restore PureSignal corrections"),
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/PureSignal/");
    if (!file.isEmpty()) ps->restoreCorrections(file);
});
connect(m_twoToneBtn, &QPushButton::toggled, ps, &PureSignal::setTwoToneOn);

// State bindings
connect(ps, &PureSignal::feedbackLevelChanged, m_feedbackGauge, [this](int level) {
    m_feedbackGauge->setValue(level * 100.0 / 255.0);  // 0..255 -> 0..100
});
connect(ps, &PureSignal::correctingChanged, m_saveBtn, &QPushButton::setEnabled);
connect(ps, &PureSignal::calibrationCountChanged, m_iterations, [this](int n) {
    m_iterations->setText(tr("Iterations: %1").arg(n));
});

// Right-click on every button -> opens PsForm (Thetis pattern from chkFWCATUBypass_MouseDown
// at console.cs:46149-46152 [v2.10.3.13]).
auto setupRightClick = [this](QWidget* w) {
    w->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(w, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
        emit openPureSignalDialogRequested();
    });
};
for (auto* w : {m_calibrateBtn, m_autoCalBtn, m_saveBtn, m_restoreBtn, m_twoToneBtn}) {
    setupRightClick(w);
}
setupRightClick(m_feedbackGauge);
setupRightClick(m_correctionGauge);

// Visibility gating
setVisible(currentCaps.hasPureSignal);
```

### Step 13.2: TxApplet [PS-A] un-hide + wire

```cpp
// TxApplet.cpp - change visibility from hidden to gated:
m_psaBtn->setVisible(currentCaps.hasPureSignal);  // was: m_psaBtn->setVisible(false);

// Left-click (toggle) - drives autoCal:
connect(m_psaBtn, &QPushButton::toggled, ps, &PureSignal::setAutoCalEnabled);

// Right-click -> opens PsForm. Per Thetis chkFWCATUBypass_MouseDown
// (console.cs:46149-46152 [v2.10.3.13]):
//   if (IsRightButton(e)) linearityToolStripMenuItem_Click(null, EventArgs.Empty);
m_psaBtn->setContextMenuPolicy(Qt::CustomContextMenu);
connect(m_psaBtn, &QWidget::customContextMenuRequested, this, [this](const QPoint&) {
    emit openPureSignalDialogRequested();
});

// State binding (so external state changes reflect in button)
connect(ps, &PureSignal::autoCalEnabledChanged, m_psaBtn, [this](bool on) {
    QSignalBlocker block(m_psaBtn);
    m_psaBtn->setChecked(on);
});
```

### Step 13.3: MainWindow wires the openPureSignalDialogRequested signals

```cpp
// MainWindow.cpp setupApplets()
connect(m_pureSignalApplet, &PureSignalApplet::openPureSignalDialogRequested,
        this, &MainWindow::openPureSignalDialog);
connect(m_txApplet, &TxApplet::openPureSignalDialogRequested,
        this, &MainWindow::openPureSignalDialog);
```

### Step 13.4: Test left + right click + state binding

```cpp
void TstAppletPsWiring::pureSignalAppletCalibrateButtonInvokesPS() {
    // ... assertion that PureSignal::singleCalibrate called ...
}

void TstAppletPsWiring::txAppletPsaRightClickOpensPsForm() {
    QSignalSpy spy(&txApplet, &TxApplet::openPureSignalDialogRequested);
    QTest::mouseClick(m_psaBtn, Qt::RightButton);
    QCOMPARE(spy.count(), 1);
}
```

### Step 13.5: Commit

```bash
git commit -m "feat(applet): wire PureSignalApplet + TxApplet [PS-A] to live PureSignal state

PureSignalApplet: 7 buttons, 2 gauges, 3 LEDs, 3 info labels all bound
to PureSignal coordinator. Per-control right-click opens PsForm via
MainWindow::openPureSignalDialog (NereusSDR pattern, mirrors Thetis).

TxApplet [PS-A]: un-hidden under caps.hasPureSignal gating. Left-click
drives PureSignal::setAutoCalEnabled (Thetis chkFWCATUBypass_Click,
console.cs:36762 [v2.10.3.13]). Right-click opens PsForm (Thetis
chkFWCATUBypass_MouseDown, console.cs:46149-46152 [v2.10.3.13]).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 14: Retire Setup → Hardware → PureSignal tab + Setup → Transmit → PureSignal page

**Files:**
- Modify: `src/gui/setup/HardwarePage.{h,cpp}`
- Modify: `src/gui/setup/TransmitSetupPages.{h,cpp}`
- Delete: `src/gui/setup/hardware/PureSignalTab.{h,cpp}`
- Possible: update `CMakeLists.txt` source list

### Step 14.1: Remove PureSignal tab from HardwarePage

- [ ] In `HardwarePage.h`, remove `class PureSignalTab;` forward declaration, remove `PureSignal` from `Tab` enum, remove `m_pureSignalTab` member.
- [ ] In `HardwarePage.cpp`, remove `#include "hardware/PureSignalTab.h"`, remove construction/registration, remove `setTabVisible` call, remove `nameForTab` case.

### Step 14.2: Delete `PureSignalTab.{h,cpp}`

```bash
git rm src/gui/setup/hardware/PureSignalTab.h src/gui/setup/hardware/PureSignalTab.cpp
```

### Step 14.3: Remove `PureSignalPage` from TransmitSetupPages

- [ ] Remove the class declaration + implementation around `TransmitSetupPages.cpp:1249-1366`.
- [ ] Remove its registration in the page list.

### Step 14.4: Confirm build still green

```bash
cmake --build build -j$(nproc)
```

### Step 14.5: Commit

```bash
git commit -m "chore(setup): retire NereusSDR-only PureSignal Setup pages

Setup -> Hardware -> PureSignal tab and Setup -> Transmit -> PureSignal
page have no Thetis equivalent (verified during 3M-4 brainstorm).
PsForm (Tools > PureSignal) is the entire PS control surface in Thetis;
NereusSDR matches per source-first IA. Hardware tab count 9 -> 8;
Transmit sub-page count 4 -> 3.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

## Task 15: Verification matrix + integration tests

**Files:**
- Create: `docs/architecture/phase3m-4-verification.md`
- Verify all 13 new tests pass
- Run full ctest suite

### Step 15.1: Write verification matrix

```markdown
# Phase 3M-4 PureSignal Verification Matrix

Bench-acceptance checklist for the 15 UI surfaces. Run on ANAN-G2 +
HL2 (mi0bot firmware) + dummy load + spectrum analyzer (or second SDR
RX) before merging the PR.

## Hardware required
- [ ] ANAN-G2 (or G2-1K, 7000DLE, 8000DLE) for primary G2-class verification
- [ ] Hermes Lite 2 (mi0bot firmware) for HL2-specific path
- [ ] Dummy load with thermal capacity for steady TX
- [ ] Spectrum analyzer OR second SDR receiver for IMD3/IMD5 measurement

## 15 UI surfaces

### 1. PsForm
- [ ] Opens from Tools > PureSignal menu (capability-gated)
- [ ] Title bar reads "PureSignal 2.0"
- [ ] All 23 controls render at correct positions
- [ ] Advanced toggle collapses ClientSize 560x300 -> 560x60 and back
- [ ] Single Cal button triggers calcc transition LRESET -> LWAIT -> ...
- [ ] OFF button halts cal

### 2. AmpView
- [ ] Opens from PsForm AmpView button
- [ ] Title "AmpView 1.0"
- [ ] 5 chart series render: Ref, MagAmp, PhsAmp, MagCorr, PhsCorr
- [ ] All 4 toolbar checkboxes drive their effects
- [ ] FixAmpViewOnTop lifecycle: AmpView stays on top when PsForm is on top

(... rest of 15 surfaces ...)

## DSP correctness (bench)
- [ ] Two-tone test on G2 + dummy load
- [ ] Without PS: measure IMD3 baseline (typical 25-35 dBc)
- [ ] Enable PS, run single calibration, FB level enters 129-181
- [ ] "Correcting" indicator activates (PS label Lime)
- [ ] Re-measure IMD3: improvement of 10-25 dB
- [ ] Repeat on HL2

## Auto-attention regression
- [ ] Set ATT manually too high: PS auto-att pulls down
- [ ] Set ATT manually too low: PS auto-att pushes up
- [ ] Disable AutoCal: ATT-on-TX un-stash logic runs
```

### Step 15.2: Run full ctest suite

```bash
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure 2>&1 | tail -50
```

Expected: all PS-related tests pass; no regressions in pre-existing tests.

### Step 15.3: Run all CI verifiers

```bash
python3 scripts/verify-thetis-headers.py --all-kinds
python3 scripts/check-new-ports.py
python3 scripts/verify-inline-cites.py
python3 scripts/compliance-inventory.py --fail-on-unclassified
python3 scripts/verify-inline-tag-preservation.py
```

Expected: all green.

### Step 15.4: Commit verification doc + final integration tests

```bash
git add docs/architecture/phase3m-4-verification.md \
        tests/tst_*.cpp tests/CMakeLists.txt
git commit -m "test(puresignal): verification matrix + integration suite

13 new test executables covering caps, TxChannel PS API, PsFeedback
channel, per-board codec PS DDC config, ReceiverManager UpdateDDCs,
PureSignal coordinator, PsForm, AmpViewWindow, PsaIndicatorWidget,
Setup deltas, IMD overlay, applet wiring, MicProfileManager Pure_
Signal_Enabled. docs/architecture/phase3m-4-verification.md captures
the 15-surface bench acceptance matrix.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>"
```

---

# Final pre-merge checks

After all 15 commits land:

- [ ] **Build clean** on Linux + macOS (Windows TBD per design §15)
- [ ] **All ctest pass**: `ctest --test-dir build --output-on-failure`
- [ ] **All verifiers green**: `verify-thetis-headers`, `check-new-ports`, `verify-inline-cites`, `compliance-inventory`, `verify-inline-tag-preservation`
- [ ] **Bench verification matrix** at `docs/architecture/phase3m-4-verification.md` runs green on ANAN-G2 + HL2
- [ ] **PR branch** rebased on latest main; merge conflicts (if any with carson-branch peak-blob work) resolved
- [ ] **MASTER-PLAN.md** §3M-4 row updated to Complete with PR # and version stamp
- [ ] **CHANGELOG.md** entry added under Unreleased
- [ ] **PR description** drafted using `/release` skill or manually, listing all 15 commits and the verification status

---

# Open questions / TBDs (resolve during execution)

These are inherited from design doc §15 and need plan-write resolution before each affected task:

1. **Default values** for `udPSMoxDelay` (MOX Wait), `udPSCalDelay` (CAL Wait), `udPSPhnum` (AMP Delay): verify from Thetis source at Task 8
2. **`comboPSTint` value list**: verify at Task 8
3. **`puresignal.PSSaveCorr` / `PSRestoreCorr` file format**: verify at Task 7 / Task 8 (binary blob layout)
4. **`HermesII` PS DDC config**: verify at Task 5 from `console.cs:8453+` ramdor branch
5. **Auto-attention sub-state machine** (`eAAState`): verify at Task 7
6. **Thetis menu hierarchy** for `linearityToolStripMenuItem`: not blocking; we use Tools > PureSignal per user call
7. **`_ema_*` smoothing constants**: verify at Task 12 from display.cs
8. **`findImd()` algorithm**: verify at Task 12 from display.cs:5512-5560
9. **Compact-fonts threshold**: verify at Task 10 from ucInfoBar.cs
10. **AmpView `GetPSDisp` buffer indices**: verify at Task 9 from AmpView.cs (note: GetPSDisp takes 7 buffers but design doc said 5; verify which 5 of the 7 AmpView renders)
11. **MicProfileManager key index** for `Pure_Signal_Enabled`: assign at Task 7
12. **Chart rendering choice** for AmpView: Qt6 `QChart` vs `QPainter`. Pick at Task 9 start
13. **carson-branch coordination order**: check at Task 12 start; rebase or merge accordingly
14. **rx_adc_ctrl1 + rx_adc_ctrl2 source**: passed in or read from member? Verify at Task 5 from existing P2CodecOrionMkII state

---

*Plan complete. Generated 2026-05-06 by J.J. Boyd (KG4VCF) with AI-assisted source-first protocol via Anthropic Claude Code.*
