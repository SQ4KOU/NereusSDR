# Phase 3I-RP: Radio Model Selector + P1 C&C Completion — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix RedPitaya (and all non-standard Hermes devices) by adding a per-MAC radio model selector and completing the P1 C&C 17-bank round-robin.

**Architecture:** Two-layer approach. Layer 1: HardwareProfile struct computed from user-selected HPSDRModel, ported from Thetis clsHardwareSpecific.cs. Layer 2: P1 C&C full 17-bank round-robin ported from Thetis networkproto1.c WriteMainLoop. ConnectionPanel gets a detail panel with model dropdown, persisted per-MAC in AppSettings.

**Tech Stack:** C++20, Qt6, AppSettings XML persistence

**Design Spec:** `docs/superpowers/specs/2026-04-16-radio-model-selector-p1-cc-design.md`

**Thetis Source (READ before implementing):**
- `../Thetis/Project Files/Source/Console/clsHardwareSpecific.cs` (model overrides)
- `../Thetis/Project Files/Source/ChannelMaster/networkproto1.c` (WriteMainLoop cases 0-17)
- `../Thetis/Project Files/Source/Console/HPSDR/NetworkIO.cs:160-176` (board validation)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/core/HardwareProfile.h` | Create | HardwareProfile struct, `profileForModel()` factory, `compatibleModels()` |
| `src/core/HardwareProfile.cpp` | Create | Implementation of factory + compatibility list |
| `src/core/P1RadioConnection.h` | Modify | New state members, composeCcBank signatures |
| `src/core/P1RadioConnection.cpp` | Modify | Full 17-bank round-robin, reconnect log fix |
| `src/core/P2RadioConnection.cpp` | Modify | Use HardwareProfile for caps lookup |
| `src/core/RadioDiscovery.h` | Modify | Add `modelOverride` field to RadioInfo |
| `src/core/AppSettings.h` | Modify | `modelOverride` read/write helpers |
| `src/core/AppSettings.cpp` | Modify | Persist/restore modelOverride per-MAC |
| `src/gui/ConnectionPanel.h` | Modify | Detail panel widgets, model dropdown |
| `src/gui/ConnectionPanel.cpp` | Modify | Detail panel build + wiring |
| `src/gui/MainWindow.cpp` | Modify | Wire "Radio Setup..." menu item |
| `src/models/RadioModel.h` | Modify | Store HardwareProfile |
| `src/models/RadioModel.cpp` | Modify | Compute profile at connect, pass to connection |
| `src/gui/setup/HardwarePage.cpp` | Modify | Use profile.effectiveBoard for tab visibility |
| `CMakeLists.txt` | Modify | Add HardwareProfile.cpp to CORE_SOURCES |

---

### Task 1: HardwareProfile struct and factory

**Files:**
- Create: `src/core/HardwareProfile.h`
- Create: `src/core/HardwareProfile.cpp`
- Modify: `CMakeLists.txt:244-258`

This is the foundation. Port of Thetis `clsHardwareSpecific.cs:85-184`.

- [ ] **Step 1: Read Thetis source**

Read `../Thetis/Project Files/Source/Console/clsHardwareSpecific.cs` lines 85-184 and `NetworkIO.cs` lines 160-176 to confirm the override values for each model.

- [ ] **Step 2: Create HardwareProfile.h**

```cpp
// src/core/HardwareProfile.h
//
// Model-driven hardware configuration overrides.
// Port of Thetis clsHardwareSpecific.cs:85-184.
//
// The discovery board byte identifies the chip family (6 values), but 16+
// distinct radio models share those bytes. This struct resolves the ambiguity
// by letting the user select their actual model, then computing the correct
// hardware parameters.

#pragma once

#include "HpsdrModel.h"
#include "BoardCapabilities.h"

#include <QList>

namespace NereusSDR {

struct HardwareProfile {
    HPSDRModel               model{HPSDRModel::HERMES};
    HPSDRHW                  effectiveBoard{HPSDRHW::Hermes};
    const BoardCapabilities* caps{nullptr};
    int                      adcCount{1};         // 1 or 2
    bool                     mkiiBpf{false};       // MKII bandpass filter enable
    int                      adcSupplyVoltage{33}; // 33 or 50
    bool                     lrAudioSwap{true};    // L/R audio channel swap
};

// Compute a HardwareProfile for the given model.
// Source: Thetis clsHardwareSpecific.cs:85-184
HardwareProfile profileForModel(HPSDRModel model);

// Return the default (auto-guessed) HPSDRModel for a discovered board byte.
// Picks the first model whose boardForModel() matches the board type.
HPSDRModel defaultModelForBoard(HPSDRHW board);

// Return the list of HPSDRModel values compatible with a discovered board byte.
// Used to populate the model dropdown in ConnectionPanel.
// Source: Thetis NetworkIO.cs:160-176 board_is_expected_for_model validation.
QList<HPSDRModel> compatibleModels(HPSDRHW board);

} // namespace NereusSDR
```

- [ ] **Step 3: Create HardwareProfile.cpp**

```cpp
// src/core/HardwareProfile.cpp
//
// Porting from Thetis clsHardwareSpecific.cs:85-184 — Model setter switch.
// Each case maps an HPSDRModel to its hardware overrides.

#include "HardwareProfile.h"

namespace NereusSDR {

HardwareProfile profileForModel(HPSDRModel model)
{
    HardwareProfile p;
    p.model = model;

    // Source: clsHardwareSpecific.cs:85-184
    switch (model) {
    case HPSDRModel::HERMES:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::Hermes;
        break;
    case HPSDRModel::ANAN10:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::Hermes;
        break;
    case HPSDRModel::ANAN10E:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::HermesII;
        break;
    case HPSDRModel::ANAN100:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::Hermes;
        break;
    case HPSDRModel::ANAN100B:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::HermesII;
        break;
    case HPSDRModel::ANAN100D:
        p.adcCount = 2; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::Angelia;
        break;
    case HPSDRModel::ANAN200D:
        p.adcCount = 2; p.mkiiBpf = false; p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::Orion;
        break;
    case HPSDRModel::ORIONMKII:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::OrionMKII;
        break;
    case HPSDRModel::ANAN7000D:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::OrionMKII;
        break;
    case HPSDRModel::ANAN8000D:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::OrionMKII;
        break;
    case HPSDRModel::ANAN_G2:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::Saturn;
        break;
    case HPSDRModel::ANAN_G2_1K:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::Saturn;
        break;
    case HPSDRModel::ANVELINAPRO3:
        p.adcCount = 2; p.mkiiBpf = true;  p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::OrionMKII;
        break;
    case HPSDRModel::HERMESLITE:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::HermesLite;
        break;
    // From clsHardwareSpecific.cs:178-183 — DH1KLM RedPitaya contribution
    case HPSDRModel::REDPITAYA:
        p.adcCount = 2; p.mkiiBpf = false; p.adcSupplyVoltage = 50;
        p.lrAudioSwap = false; p.effectiveBoard = HPSDRHW::OrionMKII;
        break;
    default:
        p.adcCount = 1; p.mkiiBpf = false; p.adcSupplyVoltage = 33;
        p.lrAudioSwap = true;  p.effectiveBoard = HPSDRHW::Hermes;
        break;
    }

    p.caps = &BoardCapsTable::forBoard(p.effectiveBoard);
    return p;
}

HPSDRModel defaultModelForBoard(HPSDRHW board)
{
    // Walk the enum and return the first model whose boardForModel() matches.
    for (int i = static_cast<int>(HPSDRModel::FIRST) + 1;
         i < static_cast<int>(HPSDRModel::LAST); ++i) {
        auto m = static_cast<HPSDRModel>(i);
        if (boardForModel(m) == board) {
            return m;
        }
    }
    return HPSDRModel::HERMES;
}

QList<HPSDRModel> compatibleModels(HPSDRHW board)
{
    QList<HPSDRModel> result;

    // Source: Thetis NetworkIO.cs:160-176 board_is_expected_for_model
    // RedPitaya can report as Hermes OR OrionMKII.
    // ANAN-10/10E/100/100B can report as Hermes OR HermesII.
    for (int i = static_cast<int>(HPSDRModel::FIRST) + 1;
         i < static_cast<int>(HPSDRModel::LAST); ++i) {
        auto m = static_cast<HPSDRModel>(i);
        bool compatible = false;

        // Primary match: boardForModel maps to this board byte
        if (boardForModel(m) == board) {
            compatible = true;
        }

        // Special cases from NetworkIO.cs:164-171
        if (m == HPSDRModel::REDPITAYA) {
            // RedPitaya can report as Hermes or OrionMKII
            compatible = (board == HPSDRHW::Hermes || board == HPSDRHW::OrionMKII);
        }
        if (m == HPSDRModel::ANAN10 || m == HPSDRModel::ANAN100) {
            // These Hermes-based radios can also appear as HermesII
            if (board == HPSDRHW::Hermes || board == HPSDRHW::HermesII) {
                compatible = true;
            }
        }
        if (m == HPSDRModel::ANAN10E || m == HPSDRModel::ANAN100B) {
            if (board == HPSDRHW::Hermes || board == HPSDRHW::HermesII) {
                compatible = true;
            }
        }

        if (compatible) {
            result.append(m);
        }
    }
    return result;
}

} // namespace NereusSDR
```

- [ ] **Step 4: Add to CMakeLists.txt**

Add `src/core/HardwareProfile.cpp` to the `CORE_SOURCES` list after `BoardCapabilities.cpp`:

```
    src/core/BoardCapabilities.cpp
    src/core/HardwareProfile.cpp
```

- [ ] **Step 5: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS, no errors.

- [ ] **Step 6: Commit**

```bash
git add src/core/HardwareProfile.h src/core/HardwareProfile.cpp CMakeLists.txt
git commit -m "feat(core): add HardwareProfile model-driven configuration

Port of Thetis clsHardwareSpecific.cs:85-184. Maps HPSDRModel enum
to hardware overrides (ADC count, BPF, supply voltage, audio swap,
effective board identity). Includes compatibleModels() for populating
the ConnectionPanel model dropdown.

Part of Phase 3I-RP (Issue #38).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 2: AppSettings modelOverride persistence + RadioInfo field

**Files:**
- Modify: `src/core/RadioDiscovery.h:21-52`
- Modify: `src/core/AppSettings.h:100-124`
- Modify: `src/core/AppSettings.cpp:238-260`

- [ ] **Step 1: Add modelOverride to RadioInfo**

In `src/core/RadioDiscovery.h`, add a field to the RadioInfo struct after the `maxSampleRate` field (line 41):

```cpp
    // Model override — set by user in ConnectionPanel, persisted per-MAC.
    // When set to a value other than FIRST, overrides the auto-detected model.
    HPSDRModel modelOverride{HPSDRModel::FIRST}; // FIRST = no override (auto-detect)
```

- [ ] **Step 2: Add modelOverride to AppSettings saveRadio/savedRadio**

In `src/core/AppSettings.cpp`, inside `saveRadio()` (after the `lastSeen` insert at line 259), add:

```cpp
    // Model override (Phase 3I-RP). FIRST = no override.
    if (info.modelOverride != HPSDRModel::FIRST) {
        m_settings.insert(prefix + QStringLiteral("modelOverride"),
                          QString::number(static_cast<int>(info.modelOverride)));
    }
```

In `savedRadio()` (after the `lastSeen` parse at line 343), add:

```cpp
    // Model override (Phase 3I-RP)
    const QString moStr = m_settings.value(prefix + QStringLiteral("modelOverride"),
                                            QStringLiteral("-1"));
    int moInt = moStr.toInt();
    if (moInt > static_cast<int>(HPSDRModel::FIRST) &&
        moInt < static_cast<int>(HPSDRModel::LAST)) {
        sr.info.modelOverride = static_cast<HPSDRModel>(moInt);
    }
```

- [ ] **Step 3: Add convenience helpers to AppSettings.h**

In `src/core/AppSettings.h`, after `setDiscoveryProfile()` (line 123), add:

```cpp
    // Model override for a specific radio MAC (Phase 3I-RP).
    HPSDRModel modelOverride(const QString& macKey) const;
    void setModelOverride(const QString& macKey, HPSDRModel model);
```

In `src/core/AppSettings.cpp`, add the implementations:

```cpp
HPSDRModel AppSettings::modelOverride(const QString& macKey) const
{
    const QString prefix = radioKeyPrefix(macKey);
    const QString val = m_settings.value(prefix + QStringLiteral("modelOverride"),
                                          QStringLiteral("-1"));
    int v = val.toInt();
    if (v > static_cast<int>(HPSDRModel::FIRST) &&
        v < static_cast<int>(HPSDRModel::LAST)) {
        return static_cast<HPSDRModel>(v);
    }
    return HPSDRModel::FIRST; // no override
}

void AppSettings::setModelOverride(const QString& macKey, HPSDRModel model)
{
    const QString prefix = radioKeyPrefix(macKey);
    m_settings.insert(prefix + QStringLiteral("modelOverride"),
                      QString::number(static_cast<int>(model)));
}
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/core/RadioDiscovery.h src/core/AppSettings.h src/core/AppSettings.cpp
git commit -m "feat(core): add modelOverride to RadioInfo and AppSettings

Per-MAC model override persistence for Phase 3I-RP. RadioInfo gets a
modelOverride field (FIRST = auto-detect). AppSettings saves/restores
it alongside existing per-radio fields.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 3: RadioModel — compute and pass HardwareProfile

**Files:**
- Modify: `src/models/RadioModel.h`
- Modify: `src/models/RadioModel.cpp:117-240`

- [ ] **Step 1: Add profile storage to RadioModel.h**

Add include and member. After the existing `#include` block, add:

```cpp
#include "core/HardwareProfile.h"
```

Add a member variable in the private section:

```cpp
    HardwareProfile m_hardwareProfile;
```

Add a public accessor:

```cpp
    const HardwareProfile& hardwareProfile() const { return m_hardwareProfile; }
```

- [ ] **Step 2: Compute profile in connectToRadio()**

In `src/models/RadioModel.cpp`, at the top of `connectToRadio()` after `m_lastRadioInfo = info;` (line 124), add:

```cpp
    // Compute HardwareProfile from model override (Phase 3I-RP).
    // If the user set a model override via ConnectionPanel, use it.
    // Otherwise auto-detect from the discovered board byte.
    HPSDRModel selectedModel = info.modelOverride;
    if (selectedModel == HPSDRModel::FIRST) {
        selectedModel = defaultModelForBoard(info.boardType);
    }
    m_hardwareProfile = profileForModel(selectedModel);

    qCDebug(lcConnection) << "HardwareProfile: model=" << displayName(m_hardwareProfile.model)
                          << "effectiveBoard=" << static_cast<int>(m_hardwareProfile.effectiveBoard)
                          << "adcCount=" << m_hardwareProfile.adcCount;
```

Change line 128 from:

```cpp
    m_model = QString::fromLatin1(BoardCapsTable::forBoard(info.boardType).displayName);
```

to:

```cpp
    m_model = QString::fromLatin1(m_hardwareProfile.caps->displayName);
```

- [ ] **Step 3: Pass profile to connection via RadioConnection interface**

In `src/core/RadioConnection.h`, add a virtual setter (or store on RadioInfo). The simplest approach: since RadioInfo already carries modelOverride, the connection can compute the profile itself. But to keep the profile computation centralized, add a setter to RadioConnection:

In `RadioConnection.h`, add to the public section:

```cpp
    void setHardwareProfile(const HardwareProfile& profile) { m_hardwareProfile = profile; }
    const HardwareProfile& hardwareProfile() const { return m_hardwareProfile; }
protected:
    HardwareProfile m_hardwareProfile;
```

In `RadioModel::connectToRadio()`, after creating the connection (after line 208), add:

```cpp
    m_connection->setHardwareProfile(m_hardwareProfile);
```

- [ ] **Step 4: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 5: Commit**

```bash
git add src/models/RadioModel.h src/models/RadioModel.cpp src/core/RadioConnection.h
git commit -m "feat(model): compute HardwareProfile at connect time

RadioModel computes the profile from the user's model override (or
auto-detects from board byte) and passes it to RadioConnection. The
profile drives capability lookups, display name, and will drive the
P1 C&C round-robin values.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 4: P1RadioConnection — use HardwareProfile + fix bank 0

**Files:**
- Modify: `src/core/P1RadioConnection.h:127-181`
- Modify: `src/core/P1RadioConnection.cpp:104-130, 605-810`

This task fixes the immediate bank 0 issue (dither/random/preamp all zeros) and adds the new state members. The full 17-bank round-robin comes in Task 5.

- [ ] **Step 1: Add state members to P1RadioConnection.h**

Replace the existing state block (lines 158-166) with the expanded set:

```cpp
    int     m_sampleRate{48000};
    int     m_activeRxCount{1};
    quint64 m_rxFreqHz[7]{};
    quint64 m_txFreqHz{0};
    bool    m_mox{false};
    int     m_antennaIdx{0};

    // Per-ADC state — initialized from HardwareProfile at connect time
    bool    m_dither[3]{true, true, true};
    bool    m_random[3]{true, true, true};
    bool    m_rxPreamp[3]{};
    int     m_stepAttn[3]{};      // per-ADC step attenuator (0-31)
    int     m_txStepAttn{0};

    // Alex filter state — computed from frequency
    quint8  m_alexHpfBits{0};     // Bank 10 C3: HPF select bits
    quint8  m_alexLpfBits{0};     // Bank 10 C4: LPF select bits

    // Hardware config
    int     m_txDrive{0};
    bool    m_paEnabled{false};
    bool    m_duplex{true};        // Always on for NereusSDR
    bool    m_diversity{false};
    quint8  m_ocOutput{0};         // OC output mask
    quint16 m_adcCtrl{0};          // ADC-to-DDC assignment bits (P1_adc_cntrl)

    // Reconnect log guard
    bool    m_reconnectedLogged{false};
```

- [ ] **Step 2: Initialize state from HardwareProfile in connectToRadio()**

In `P1RadioConnection::connectToRadio()`, after `m_caps = &BoardCapsTable::forBoard(info.boardType);` (line 359), replace with:

```cpp
    // Use HardwareProfile for caps (Phase 3I-RP)
    m_caps = m_hardwareProfile.caps;

    // Initialize per-ADC state from profile
    for (int i = 0; i < 3; ++i) {
        m_dither[i] = true;   // From Thetis pcap: dither enabled on all ADCs
        m_random[i] = true;   // From Thetis pcap: random enabled on all ADCs
        m_rxPreamp[i] = false;
        m_stepAttn[i] = 0;
    }
    m_paEnabled = m_hardwareProfile.caps->hasPaProfile;
    m_duplex = true;
    m_reconnectedLogged = false;
```

- [ ] **Step 3: Fix composeCcBank0 to populate C3 and C4**

Replace the current `composeCcBank0` (lines 104-130) with:

```cpp
// Source: networkproto1.c:450-471 — WriteMainLoop case 0 (general settings)
void P1RadioConnection::composeCcBank0(quint8 out[5], int sampleRate, bool mox,
                                        int activeRxCount) noexcept
{
    // C0 = XmitBit (networkproto1.c:446)
    out[0] = mox ? 0x01 : 0x00;

    // C1 = SampleRateIn2Bits (networkproto1.c:451)
    quint8 srBits = 0;
    if      (sampleRate >= 384000) { srBits = 3; }
    else if (sampleRate >= 192000) { srBits = 2; }
    else if (sampleRate >= 96000)  { srBits = 1; }
    else                            { srBits = 0; }
    out[1] = srBits & 0x03;

    // C2 = OC outputs (networkproto1.c:452) — zero for now
    out[2] = 0;

    // C3 = preamp/dither/random/input (networkproto1.c:453-461)
    // Populated from instance state in the non-static path.
    // Static version keeps zeros for backward compat with existing tests.
    out[3] = 0;

    // C4 = antenna, duplex, NDDCs, diversity (networkproto1.c:463-471)
    int nddc = (activeRxCount < 1) ? 1 : (activeRxCount > 7 ? 7 : activeRxCount);
    out[4] = static_cast<quint8>((nddc - 1) << 3);
    out[4] |= 0x04; // duplex bit (networkproto1.c:469)
}
```

- [ ] **Step 4: Add instance-level composeCcBank0Full()**

Add a new non-static method that uses instance state for C2/C3/C4:

```cpp
// Instance-level bank 0 compose — uses HardwareProfile + runtime state.
// Source: networkproto1.c:450-471
void P1RadioConnection::composeCcBank0Full(quint8 out[5]) const
{
    out[0] = m_mox ? 0x01 : 0x00;

    quint8 srBits = 0;
    if      (m_sampleRate >= 384000) { srBits = 3; }
    else if (m_sampleRate >= 192000) { srBits = 2; }
    else if (m_sampleRate >= 96000)  { srBits = 1; }
    out[1] = srBits & 0x03;

    // C2: OC outputs (networkproto1.c:452)
    out[2] = (m_ocOutput << 1) & 0xFE;

    // C3: dither, random, preamp, attenuator, RX input (networkproto1.c:453-461)
    out[3] = (m_rxPreamp[0] ? 0x04 : 0)
           | (m_dither[0]   ? 0x08 : 0)
           | (m_random[0]   ? 0x10 : 0);
    // RX input select bits [6:5] — default to Rx_1_In (0b00100000)
    out[3] |= 0x20;

    // C4: antenna, duplex, NDDCs, diversity (networkproto1.c:463-471)
    out[4] = static_cast<quint8>(m_antennaIdx & 0x03);
    out[4] |= 0x04; // duplex (networkproto1.c:469)
    int nddc = (m_activeRxCount < 1) ? 1 : (m_activeRxCount > 7 ? 7 : m_activeRxCount);
    out[4] |= static_cast<quint8>((nddc - 1) << 3);
    out[4] |= (m_diversity ? 0x80 : 0);
}
```

Declare in the header:

```cpp
    void composeCcBank0Full(quint8 out[5]) const;
```

- [ ] **Step 5: Update sendCommandFrame to use composeCcBank0Full**

In `sendCommandFrame()` (line 781), replace the subframe 0 composition:

```cpp
    // Subframe 0: bank 0 (general settings) — use instance state
    frame[8]  = 0x7F;
    frame[9]  = 0x7F;
    frame[10] = 0x7F;
    quint8 cc0[5] = {};
    composeCcBank0Full(cc0);
    frame[11] = cc0[0];
    frame[12] = cc0[1];
    frame[13] = cc0[2];
    frame[14] = cc0[3];
    frame[15] = cc0[4];
```

- [ ] **Step 6: Fix reconnect log spam**

In the ep6 receive handler (find the "Reconnected" log line), add the guard:

```cpp
    if (!m_reconnectedLogged) {
        qCDebug(lcConnection) << "P1: Reconnected — ep6 stream restored";
        m_reconnectedLogged = true;
    }
```

Clear the flag in the error/disconnect transitions:

In `onWatchdogTick()` where it sets `ConnectionState::Error`, add:
```cpp
    m_reconnectedLogged = false;
```

In `disconnect()`, add:
```cpp
    m_reconnectedLogged = false;
```

- [ ] **Step 7: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp
git commit -m "feat(p1): use HardwareProfile in P1, fix bank 0 C3/C4

Bank 0 now sends dither, random, preamp, duplex, and antenna bits
instead of zeros. New composeCcBank0Full() reads from instance state
initialized from HardwareProfile. Fixes reconnect log spam (30-80
duplicate lines per cycle → 1 line).

Part of Phase 3I-RP (Issue #38).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 5: P1 C&C full 17-bank round-robin

**Files:**
- Modify: `src/core/P1RadioConnection.h`
- Modify: `src/core/P1RadioConnection.cpp`

This is the big port. Each bank is a case in the round-robin, ported from Thetis `networkproto1.c` WriteMainLoop cases 0-17.

- [ ] **Step 1: Read Thetis source**

Read `../Thetis/Project Files/Source/ChannelMaster/networkproto1.c` lines 419-697 (WriteMainLoop). Confirm all 17 bank layouts byte-for-byte.

- [ ] **Step 2: Add composeCcBankN dispatcher**

Add a new method to P1RadioConnection:

```cpp
// Compose C&C bytes for bank `bankIdx` (0-17).
// Dispatcher for the full round-robin, ported from Thetis
// networkproto1.c WriteMainLoop cases 0-17.
void P1RadioConnection::composeCcForBank(int bankIdx, quint8 out[5]) const
{
    memset(out, 0, 5);
    quint8 C0 = m_mox ? 0x01 : 0x00;

    switch (bankIdx) {
    case 0: // General settings (networkproto1.c:450-471)
        composeCcBank0Full(out);
        return;

    case 1: // TX VFO (networkproto1.c:476-482)
        composeCcBankTxFreq(out, m_txFreqHz);
        return;

    case 2: // RX1 VFO DDC0 (networkproto1.c:484-494)
        composeCcBankRxFreq(out, 0, m_rxFreqHz[0]);
        return;

    case 3: { // RX2 VFO DDC1 (networkproto1.c:497-511)
        out[0] = C0 | 0x06;
        quint64 freq = m_rxFreqHz[1]; // RX2 freq
        out[1] = (freq >> 24) & 0xFF;
        out[2] = (freq >> 16) & 0xFF;
        out[3] = (freq >>  8) & 0xFF;
        out[4] =  freq        & 0xFF;
        return;
    }

    case 4: // ADC assignments + TX ATT (networkproto1.c:517-523)
        out[0] = C0 | 0x1C;
        out[1] = m_adcCtrl & 0xFF;
        out[2] = (m_adcCtrl >> 8) & 0x3F;
        out[3] = m_txStepAttn & 0x1F;
        out[4] = 0;
        return;

    case 5: case 6: case 7: case 8: case 9: {
        // RX3-RX7 VFO (networkproto1.c:525-575)
        // C0 address: case5=0x08, 6=0x0A, 7=0x0C, 8=0x0E, 9=0x10
        static constexpr quint8 kAddr[] = {0x08, 0x0A, 0x0C, 0x0E, 0x10};
        out[0] = C0 | kAddr[bankIdx - 5];
        // Most unused DDCs get TX freq (networkproto1.c:541,551)
        quint64 freq = m_txFreqHz;
        out[1] = (freq >> 24) & 0xFF;
        out[2] = (freq >> 16) & 0xFF;
        out[3] = (freq >>  8) & 0xFF;
        out[4] =  freq        & 0xFF;
        return;
    }

    case 10: // TX drive, mic, Alex HPF/LPF, PA (networkproto1.c:578-591)
        out[0] = C0 | 0x12;
        out[1] = m_txDrive & 0xFF;
        out[2] = 0x40; // line_in=0, mic_boost=0, apollo defaults (networkproto1.c:582)
        out[3] = m_alexHpfBits | (m_paEnabled ? 0x80 : 0);
        out[4] = m_alexLpfBits;
        return;

    case 11: // Preamp control (networkproto1.c:593-601)
        out[0] = C0 | 0x14;
        out[1] = (m_rxPreamp[0] ? 0x01 : 0)
               | (m_rxPreamp[1] ? 0x02 : 0)
               | (m_rxPreamp[2] ? 0x04 : 0)
               | (m_rxPreamp[0] ? 0x08 : 0); // bit 3 = rx0 preamp again (Thetis quirk)
        out[2] = 0; // line_in_gain + puresignal
        out[3] = 0; // user digital outputs
        out[4] = (m_stepAttn[0] & 0x1F) | 0x20; // ADC0 step ATT + enable bit
        return;

    case 12: { // Step ATT ADC1/2, CW keyer (networkproto1.c:604-628)
        out[0] = C0 | 0x16;
        // RedPitaya-specific: don't force 31dB on ADC1 during TX
        // Source: networkproto1.c:606-616 (DH1KLM fix)
        if (m_mox && m_hardwareProfile.model != HPSDRModel::REDPITAYA) {
            out[1] = 0x1F; // force 31dB during TX (Thetis default)
        } else if (m_hardwareProfile.model == HPSDRModel::REDPITAYA) {
            out[1] = m_stepAttn[1] & 0x1F;
        } else {
            out[1] = m_stepAttn[1] & 0xFF;
        }
        out[1] |= 0x20; // enable bit
        out[2] = (m_stepAttn[2] & 0x1F) | 0x20; // ADC2 step ATT + enable
        out[3] = 0; // CW keyer defaults (speed + mode)
        out[4] = 0; // CW weight + strict spacing
        return;
    }

    case 13: // CW enable (networkproto1.c:633-639)
        out[0] = C0 | 0x1E;
        out[1] = 0; // cw_enable
        out[2] = 0; // sidetone_level
        out[3] = 0; // rf_delay
        out[4] = 0;
        return;

    case 14: // CW hang/sidetone (networkproto1.c:641-646)
        out[0] = C0 | 0x20;
        out[1] = 0; out[2] = 0; out[3] = 0; out[4] = 0;
        return;

    case 15: // EER PWM (networkproto1.c:649-654)
        out[0] = C0 | 0x22;
        out[1] = 0; out[2] = 0; out[3] = 0; out[4] = 0;
        return;

    case 16: // BPF2 (networkproto1.c:657-665)
        out[0] = C0 | 0x24;
        out[1] = 0; out[2] = 0; out[3] = 0; out[4] = 0;
        return;

    case 17: // AnvelinaPro3 extra OC (networkproto1.c:668-673)
        out[0] = C0 | 0x26;
        out[1] = 0; out[2] = 0; out[3] = 0; out[4] = 0;
        return;

    default:
        return;
    }
}
```

Declare in header:

```cpp
    void composeCcForBank(int bankIdx, quint8 out[5]) const;
```

- [ ] **Step 3: Refactor sendCommandFrame to use round-robin**

Replace `sendCommandFrame(bool sub1TxBank)` with a new version that cycles through all banks:

```cpp
void P1RadioConnection::sendCommandFrame()
{
    if (!m_socket) { return; }

    // Determine how many banks this model cycles through
    // AnvelinaPro3 gets bank 17; all others stop at 16.
    const int maxBank = (m_hardwareProfile.model == HPSDRModel::ANVELINAPRO3) ? 17 : 16;

    quint8 frame[1032];
    memset(frame, 0, sizeof(frame));

    // EP2 header
    frame[0] = 0xEF; frame[1] = 0xFE; frame[2] = 0x01; frame[3] = 0x02;
    const quint32 seq = m_epSendSeq++;
    frame[4] = (seq >> 24) & 0xFF;
    frame[5] = (seq >> 16) & 0xFF;
    frame[6] = (seq >>  8) & 0xFF;
    frame[7] =  seq        & 0xFF;

    // Subframe 0: current bank
    frame[8] = 0x7F; frame[9] = 0x7F; frame[10] = 0x7F;
    quint8 cc0[5] = {};
    composeCcForBank(m_ccRoundRobinIdx, cc0);
    frame[11] = cc0[0]; frame[12] = cc0[1]; frame[13] = cc0[2];
    frame[14] = cc0[3]; frame[15] = cc0[4];

    // Advance to next bank
    m_ccRoundRobinIdx++;
    if (m_ccRoundRobinIdx > maxBank) { m_ccRoundRobinIdx = 0; }

    // Subframe 1: next bank
    frame[520] = 0x7F; frame[521] = 0x7F; frame[522] = 0x7F;
    quint8 cc1[5] = {};
    composeCcForBank(m_ccRoundRobinIdx, cc1);
    frame[523] = cc1[0]; frame[524] = cc1[1]; frame[525] = cc1[2];
    frame[526] = cc1[3]; frame[527] = cc1[4];

    // Advance again
    m_ccRoundRobinIdx++;
    if (m_ccRoundRobinIdx > maxBank) { m_ccRoundRobinIdx = 0; }

    QByteArray pkt(reinterpret_cast<const char*>(frame), 1032);
    m_socket->writeDatagram(pkt, m_radioInfo.address, m_radioInfo.port);
}
```

- [ ] **Step 4: Update onWatchdogTick to call parameterless sendCommandFrame**

In `onWatchdogTick()`, replace:

```cpp
        if (!m_hl2Throttled) {
            const bool txBank = (m_ccRoundRobinIdx++ & 1) != 0;
            sendCommandFrame(txBank);
        }
```

with:

```cpp
        if (!m_hl2Throttled) {
            sendCommandFrame();
        }
```

- [ ] **Step 5: Update sendPrimingBurst**

Replace calls to `sendCommandFrame(true/false)` in `sendPrimingBurst` with the parameterless version. The round-robin naturally covers all banks, and the priming burst sends multiple packets which cycle through the first few banks:

```cpp
void P1RadioConnection::sendPrimingBurst(int countPerBank)
{
    // Send enough packets to cover at least the general settings + frequency banks.
    // Each call advances the round-robin by 2 banks.
    for (int i = 0; i < countPerBank; ++i) {
        sendCommandFrame();
    }
    QThread::msleep(10);
    for (int i = 0; i < countPerBank; ++i) {
        sendCommandFrame();
    }
    QThread::msleep(10);
}
```

- [ ] **Step 6: Update header — remove old sendCommandFrame(bool) signature**

Change the declaration from:

```cpp
    void sendCommandFrame(bool sub1TxBank);
```

to:

```cpp
    void sendCommandFrame();
```

Remove the old `composeEp2Frame` static method declaration if no longer used by tests (check first — if tests use it, keep it).

- [ ] **Step 7: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS. Fix any compile errors from signature changes.

- [ ] **Step 8: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp
git commit -m "feat(p1): complete 17-bank C&C round-robin

Port of Thetis networkproto1.c WriteMainLoop cases 0-17. Each 25ms
tick now sends 2 consecutive banks from the full 17-bank cycle
(~225ms per full rotation). Includes bank 4 (ADC assignments),
bank 10 (Alex filters, PA enable), bank 11 (preamp, step ATT),
bank 12 (RedPitaya-specific ATT handling), banks 13-16 (CW, EER,
BPF2 defaults).

Fixes Issue #38: RedPitaya stream drops every ~9 seconds due to
incomplete C&C causing firmware watchdog timeout.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 6: P2RadioConnection — use HardwareProfile

**Files:**
- Modify: `src/core/P2RadioConnection.cpp:128, 141`

- [ ] **Step 1: Replace caps lookup**

In `P2RadioConnection::connectToRadio()`, replace line 128:

```cpp
    m_caps = &BoardCapsTable::forBoard(info.boardType);
```

with:

```cpp
    // Use HardwareProfile for capability lookup (Phase 3I-RP).
    // This ensures a RedPitaya (board byte Hermes) connecting via P2
    // gets OrionMKII capabilities when the user sets the model override.
    m_caps = m_hardwareProfile.caps;
```

Also replace line 141 (`m_numAdc = m_caps->adcCount;`) with:

```cpp
    m_numAdc = m_hardwareProfile.adcCount;
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/core/P2RadioConnection.cpp
git commit -m "feat(p2): use HardwareProfile for caps and ADC count

P2RadioConnection now reads capabilities and ADC count from the
HardwareProfile instead of raw board byte lookup. Fixes RedPitaya
P2 firmware getting Hermes (1-ADC) capabilities instead of
OrionMKII (2-ADC).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 7: ConnectionPanel detail panel with model dropdown

**Files:**
- Modify: `src/gui/ConnectionPanel.h`
- Modify: `src/gui/ConnectionPanel.cpp`

- [ ] **Step 1: Add detail panel widgets to header**

In `ConnectionPanel.h`, add includes:

```cpp
#include "core/HardwareProfile.h"
#include <QComboBox>
#include <QGroupBox>
```

Add new widget members after the existing ones:

```cpp
    // Detail panel (Phase 3I-RP)
    QGroupBox*   m_detailGroup{nullptr};
    QLabel*      m_detailBoardLabel{nullptr};
    QLabel*      m_detailProtoLabel{nullptr};
    QLabel*      m_detailFwLabel{nullptr};
    QLabel*      m_detailIpLabel{nullptr};
    QLabel*      m_detailMacLabel{nullptr};
    QComboBox*   m_modelCombo{nullptr};
    QLabel*      m_modelHintLabel{nullptr};
```

Add new slots:

```cpp
    void onModelComboChanged(int index);
    void updateDetailPanel();
```

- [ ] **Step 2: Build the detail panel in buildUI()**

In `ConnectionPanel::buildUI()`, after the radio table group box is added to `mainLayout` (after line 196), add the detail panel:

```cpp
    // --- Selected Radio detail panel (Phase 3I-RP) ---
    m_detailGroup = new QGroupBox(QStringLiteral("Selected Radio"), this);
    m_detailGroup->setVisible(false); // hidden until a row is selected
    auto* detailLayout = new QVBoxLayout(m_detailGroup);
    detailLayout->setSpacing(4);

    // Info row 1: Board + Protocol + Firmware
    auto* infoRow1 = new QHBoxLayout();
    m_detailBoardLabel = new QLabel(m_detailGroup);
    m_detailProtoLabel = new QLabel(m_detailGroup);
    m_detailFwLabel    = new QLabel(m_detailGroup);
    for (auto* lbl : {m_detailBoardLabel, m_detailProtoLabel, m_detailFwLabel}) {
        lbl->setStyleSheet(QStringLiteral("QLabel { color: #8090a0; font-size: 12px; }"));
    }
    infoRow1->addWidget(m_detailBoardLabel);
    infoRow1->addWidget(m_detailProtoLabel);
    infoRow1->addWidget(m_detailFwLabel);
    infoRow1->addStretch();
    detailLayout->addLayout(infoRow1);

    // Info row 2: IP + MAC
    auto* infoRow2 = new QHBoxLayout();
    m_detailIpLabel  = new QLabel(m_detailGroup);
    m_detailMacLabel = new QLabel(m_detailGroup);
    for (auto* lbl : {m_detailIpLabel, m_detailMacLabel}) {
        lbl->setStyleSheet(QStringLiteral("QLabel { color: #8090a0; font-size: 12px; }"));
    }
    infoRow2->addWidget(m_detailIpLabel);
    infoRow2->addWidget(m_detailMacLabel);
    infoRow2->addStretch();
    detailLayout->addLayout(infoRow2);

    // Model selector row
    auto* modelRow = new QHBoxLayout();
    auto* modelLabel = new QLabel(QStringLiteral("Radio Model:"), m_detailGroup);
    modelLabel->setStyleSheet(QStringLiteral("QLabel { color: #c8d8e8; font-weight: bold; }"));
    m_modelCombo = new QComboBox(m_detailGroup);
    m_modelCombo->setMinimumWidth(200);
    m_modelCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background: #1a2a3a; color: #c8d8e8; border: 1px solid #304050;"
        "  border-radius: 3px; padding: 4px 8px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #12202e; color: #c8d8e8;"
        "  selection-background-color: #005578; }"));
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ConnectionPanel::onModelComboChanged);
    modelRow->addWidget(modelLabel);
    modelRow->addWidget(m_modelCombo);
    modelRow->addStretch();
    detailLayout->addLayout(modelRow);

    // Hint label
    m_modelHintLabel = new QLabel(m_detailGroup);
    m_modelHintLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #b08020; font-size: 11px; font-style: italic; }"));
    m_modelHintLabel->setVisible(false);
    detailLayout->addWidget(m_modelHintLabel);

    mainLayout->addWidget(m_detailGroup);
```

- [ ] **Step 3: Implement updateDetailPanel()**

```cpp
void ConnectionPanel::updateDetailPanel()
{
    RadioInfo info = selectedRadio();
    if (info.macAddress.isEmpty()) {
        m_detailGroup->setVisible(false);
        return;
    }

    m_detailGroup->setVisible(true);

    // Board name from raw discovery byte
    QString boardName = QString::fromLatin1(
        BoardCapsTable::forBoard(info.boardType).displayName);
    m_detailBoardLabel->setText(QStringLiteral("Board: %1 (0x%2)")
        .arg(boardName)
        .arg(static_cast<int>(info.boardType), 2, 16, QLatin1Char('0')));
    m_detailProtoLabel->setText(QStringLiteral("Protocol: P%1")
        .arg(info.protocol == ProtocolVersion::Protocol2 ? 2 : 1));
    m_detailFwLabel->setText(QStringLiteral("Firmware: %1")
        .arg(info.firmwareVersion));
    m_detailIpLabel->setText(QStringLiteral("IP: %1").arg(info.address.toString()));
    m_detailMacLabel->setText(QStringLiteral("MAC: %1").arg(info.macAddress));

    // Populate model combo with compatible models
    m_modelCombo->blockSignals(true);
    m_modelCombo->clear();
    QList<HPSDRModel> models = compatibleModels(info.boardType);
    HPSDRModel defaultModel = defaultModelForBoard(info.boardType);

    // Check for persisted override
    HPSDRModel persisted = AppSettings::instance().modelOverride(info.macAddress);
    HPSDRModel selected = (persisted != HPSDRModel::FIRST) ? persisted : defaultModel;

    int selectIdx = 0;
    for (int i = 0; i < models.size(); ++i) {
        m_modelCombo->addItem(
            QString::fromLatin1(displayName(models[i])),
            static_cast<int>(models[i]));
        if (models[i] == selected) {
            selectIdx = i;
        }
    }
    m_modelCombo->setCurrentIndex(selectIdx);
    m_modelCombo->blockSignals(false);

    // Show hint if override differs from auto-detect
    if (selected != defaultModel) {
        m_modelHintLabel->setText(QStringLiteral("Board reports \"%1\" -- model override applied")
            .arg(boardName));
        m_modelHintLabel->setVisible(true);
    } else {
        m_modelHintLabel->setVisible(false);
    }
}
```

- [ ] **Step 4: Implement onModelComboChanged()**

```cpp
void ConnectionPanel::onModelComboChanged(int index)
{
    if (index < 0) { return; }
    RadioInfo info = selectedRadio();
    if (info.macAddress.isEmpty()) { return; }

    int modelInt = m_modelCombo->itemData(index).toInt();
    auto model = static_cast<HPSDRModel>(modelInt);

    AppSettings::instance().setModelOverride(info.macAddress, model);
    AppSettings::instance().save();

    // Update hint
    HPSDRModel defaultModel = defaultModelForBoard(info.boardType);
    QString boardName = QString::fromLatin1(
        BoardCapsTable::forBoard(info.boardType).displayName);
    if (model != defaultModel) {
        m_modelHintLabel->setText(QStringLiteral("Board reports \"%1\" -- model override applied")
            .arg(boardName));
        m_modelHintLabel->setVisible(true);
    } else {
        m_modelHintLabel->setVisible(false);
    }

    setStatusText(QStringLiteral("Model set to %1 for %2")
        .arg(QString::fromLatin1(displayName(model)), info.macAddress));
}
```

- [ ] **Step 5: Wire updateDetailPanel to selection changes**

In `onRadioSelectionChanged()` (line 676), add:

```cpp
    updateDetailPanel();
```

- [ ] **Step 6: Pass modelOverride in onConnectClicked()**

In `onConnectClicked()`, after `RadioInfo info = selectedRadio();` (line 585), add:

```cpp
    // Apply model override from dropdown (Phase 3I-RP)
    if (m_modelCombo && m_modelCombo->currentIndex() >= 0) {
        int modelInt = m_modelCombo->itemData(m_modelCombo->currentIndex()).toInt();
        info.modelOverride = static_cast<HPSDRModel>(modelInt);
    }
```

- [ ] **Step 7: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 8: Commit**

```bash
git add src/gui/ConnectionPanel.h src/gui/ConnectionPanel.cpp
git commit -m "feat(gui): add radio model selector to ConnectionPanel

Detail panel appears below the discovered radios table when a row is
selected. Shows board info, protocol, firmware, IP, MAC, and a Radio
Model dropdown populated with compatible models for the discovered
board byte. Selection persisted per-MAC in AppSettings. Includes
hint label when override differs from auto-detection.

Part of Phase 3I-RP (Issue #38).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 8: Wire "Radio Setup..." menu item + HardwarePage integration

**Files:**
- Modify: `src/gui/MainWindow.cpp:631-635`
- Modify: `src/gui/setup/HardwarePage.cpp:141-156`

- [ ] **Step 1: Wire Radio Setup menu item**

In `MainWindow.cpp`, replace the disabled "Radio Setup..." action (lines 631-635):

```cpp
    {
        QAction* radioSetupAction = radioMenu->addAction(QStringLiteral("&Radio Setup..."));
        radioSetupAction->setEnabled(false);
        radioSetupAction->setToolTip(QStringLiteral("NYI — Phase X"));
    }
```

with:

```cpp
    radioMenu->addAction(QStringLiteral("&Radio Setup..."), this, [this]() {
        if (!m_radioModel->isConnected()) {
            showConnectionPanel();
            return;
        }
        // Open ConnectionPanel focused on current radio
        showConnectionPanel();
    });
```

- [ ] **Step 2: Update HardwarePage to use profile for tab visibility**

In `HardwarePage::onCurrentRadioChanged()` (line 141), replace:

```cpp
    const BoardCapabilities& caps = BoardCapsTable::forBoard(info.boardType);
```

with:

```cpp
    // Use HardwareProfile for capability lookup (Phase 3I-RP).
    // This ensures model overrides (e.g. RedPitaya → OrionMKII) affect
    // which tabs are visible.
    const BoardCapabilities& caps = *m_model->hardwareProfile().caps;
```

- [ ] **Step 3: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp src/gui/setup/HardwarePage.cpp
git commit -m "feat(gui): wire Radio Setup menu + profile-driven HardwarePage

Radio Setup menu item now opens ConnectionPanel (was disabled NYI).
HardwarePage uses RadioModel::hardwareProfile() for tab visibility
instead of raw board byte lookup, so model overrides correctly gate
which hardware tabs are shown.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 9: Auto-reconnect with model override

**Files:**
- Modify: `src/models/RadioModel.cpp` (tryAutoReconnect path)

- [ ] **Step 1: Load modelOverride during auto-reconnect**

Find the auto-reconnect path in RadioModel (where it reads from `AppSettings::lastConnected()` and calls `connectToRadio`). Before the `connectToRadio()` call, load the model override:

```cpp
    // Load persisted model override for auto-reconnect (Phase 3I-RP)
    HPSDRModel mo = AppSettings::instance().modelOverride(info.macAddress);
    if (mo != HPSDRModel::FIRST) {
        info.modelOverride = mo;
    }
```

- [ ] **Step 2: Build and verify**

Run: `cmake --build build -j$(nproc) 2>&1 | tail -5`
Expected: BUILD SUCCESS.

- [ ] **Step 3: Commit**

```bash
git add src/models/RadioModel.cpp
git commit -m "feat(model): load model override on auto-reconnect

When auto-reconnecting to a saved radio at startup, the persisted
model override is loaded from AppSettings and applied to RadioInfo
before connecting. Ensures RedPitaya users don't have to re-select
their model every launch.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>"
```

---

### Task 10: Build, smoke test, and final commit

**Files:** None (verification only)

- [ ] **Step 1: Full clean build**

```bash
cd /c/Users/boyds/NereusSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

Expected: BUILD SUCCESS with no warnings related to new code.

- [ ] **Step 2: Run the application**

```bash
./build/NereusSDR
```

Verify:
1. App launches without crash
2. Open Radio > Discover (Ctrl+K)
3. If a radio is discovered, click it — detail panel appears with model dropdown
4. Model dropdown shows compatible models for the discovered board byte
5. Close and reopen — model persists

- [ ] **Step 3: Verify no regressions with existing radio**

If an ANAN-G2 or HL2 is available:
1. Connect — confirm normal I/Q reception
2. Check Setup > Hardware Config — tabs should match the radio's capabilities
3. Waterfall should display normally, no stream drops

- [ ] **Step 4: Final commit if any fixups needed**

Only if small fixes were needed during smoke testing.
