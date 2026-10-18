# Phase 3I — Radio Connector Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port Protocol 1 support for every ANAN/Hermes-family board and bring feature-wiring parity with Thetis for all supported radios, so any P1 radio (HL2, ANAN-10/100/10E/100B/100D/200D, Metis) behaves like a Saturn does on P2 today.

**Architecture:** Source-first port from `ramdor/Thetis` master + `mi0bot/Thetis@Hermes-Lite` branch. Pure-data capability registry drives both protocol families and the Setup UI. New `P1RadioConnection` follows the proven `P2RadioConnection` template (890 LOC, working Saturn). ConnectionPanel stays modal; HardwarePage adds nested tabs mirroring Thetis Hardware Config.

**Tech Stack:** C++20, Qt6 (Core/Network/Widgets/Test), CMake, WDSP (existing), source-first ports from Thetis C# + ChannelMaster C.

**Design reference:** `docs/architecture/phase3i-radio-connector-port-design.md` (committed `b475d5b`). Read this first — all architecture decisions, enum values, capability fields, and out-of-scope items are recorded there.

**Source-first protocol:** Every porting task has a mandatory "Read Thetis source and quote the original" step before writing C++. This plan cites `file:line` for each porting task. If the line numbers have drifted, grep for the referenced symbol and update the citation in the commit. Do NOT guess.

**Branch:** `feature/phase3i-radio-connector-port` (already created, design doc committed).

**Thetis source locations:**
- `~/Thetis/` — ramdor/Thetis master (baseline)
- `/tmp/mi0bot-thetis-hl2/` — mi0bot/Thetis Hermes-Lite branch (HL2 + enums authoritative)

**Commit conventions:**
- GPG sign ALL commits (`-S` if not default). Never `--no-gpg-sign`.
- Trailer: `Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>`
- One task = one commit (or a small commit series within the task if TDD loops require it).

**Verification before completion:** After each task's test passes, run the full test suite and build to catch regressions.

```bash
cd ~/NereusSDR && cmake --build build-clean -j && ctest --test-dir build-clean --output-on-failure
```

Task is not "done" until the whole suite is green.

---

## File Structure

**New source files:**
- `src/core/HpsdrModel.h` — `HPSDRModel` + `HPSDRHW` enums + `boardForModel` + `displayName`
- `src/core/BoardCapabilities.h` — struct + namespace API
- `src/core/BoardCapabilities.cpp` — `constexpr` table, one entry per `HPSDRHW`
- `src/core/P1RadioConnection.h`
- `src/core/P1RadioConnection.cpp`
- `src/gui/setup/HardwarePage.h` / `.cpp` (renamed from `HardwareSetupPages`)
- `src/gui/setup/hardware/RadioInfoTab.{h,cpp}`
- `src/gui/setup/hardware/AntennaAlexTab.{h,cpp}`
- `src/gui/setup/hardware/OcOutputsTab.{h,cpp}`
- `src/gui/setup/hardware/XvtrTab.{h,cpp}`
- `src/gui/setup/hardware/PureSignalTab.{h,cpp}`
- `src/gui/setup/hardware/DiversityTab.{h,cpp}`
- `src/gui/setup/hardware/PaCalibrationTab.{h,cpp}`
- `src/gui/setup/hardware/Hl2IoBoardTab.{h,cpp}`
- `src/gui/setup/hardware/BandwidthMonitorTab.{h,cpp}`

**New test files:**
- `tests/tst_hpsdr_enums.cpp`
- `tests/tst_board_capabilities.cpp`
- `tests/tst_radio_discovery_parse.cpp`
- `tests/tst_p1_wire_format.cpp`
- `tests/tst_p1_loopback_connection.cpp`
- `tests/tst_reconnect_on_silence.cpp`
- `tests/tst_connection_panel_saved_radios.cpp`
- `tests/tst_hardware_page_capability_gating.cpp`
- `tests/tst_hardware_page_persistence.cpp`
- `tests/fixtures/discovery/p1_hermeslite_reply.hex`
- `tests/fixtures/discovery/p1_angelia_reply.hex`
- `tests/fixtures/discovery/p2_saturn_reply.hex`
- `tests/fixtures/ep6/p1_ep6_384k_2rx.bin`
- `tests/fakes/P1FakeRadio.{h,cpp}`

**Modified files:**
- `src/core/RadioDiscovery.h` — delete legacy `BoardType`, add `DiscoveryProfile`, port mi0bot discovery model
- `src/core/RadioDiscovery.cpp` — rewrite to mi0bot `clsRadioDiscovery` pattern
- `src/core/RadioConnection.cpp` — factory P1 branch constructs `P1RadioConnection`
- `src/core/RadioConnection.h` — add `errorCode` to `errorOccurred` signal
- `src/core/P2RadioConnection.cpp` — read from `BoardCapabilities`; dispatch `SaturnMKII`/`G2_1K`/`AnvelinaPro3`
- `src/models/RadioModel.h` / `.cpp` — expose `currentBoardCapabilities()`
- `src/gui/ConnectionPanel.h` / `.cpp` — full expansion per §5.2
- `docs/architecture/radio-abstraction.md` — §8 corrections
- `CMakeLists.txt` (test tree) — register new test binaries
- `CHANGELOG.md` — add Phase 3I entry

---

## Task 1: HPSDRModel + HPSDRHW enums

**Files:**
- Create: `src/core/HpsdrModel.h`
- Create: `tests/tst_hpsdr_enums.cpp`
- Modify: `tests/CMakeLists.txt` — register `tst_hpsdr_enums`

**Source references (MUST READ FIRST):**
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/Console/enums.cs:109-131` — `HPSDRModel`
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/Console/enums.cs:388-400` — `HPSDRHW`
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/ChannelMaster/network.h:446-457` — C-side `HPSDRHW` (must agree with C# side)

- [ ] **Step 1: Read the Thetis source** and quote both enums into your commit message draft. Verify the integer values in the C# side match the C side in `network.h:446`. Note the reserved `7..9` gap in `HPSDRHW`.

- [ ] **Step 2: Write `tests/tst_hpsdr_enums.cpp`**

```cpp
#include <QtTest/QtTest>
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestHpsdrEnums : public QObject {
    Q_OBJECT
private slots:
    void integerValuesMatchThetis() {
        // Source: mi0bot/Thetis@Hermes-Lite enums.cs:109
        QCOMPARE(static_cast<int>(HPSDRModel::FIRST),        -1);
        QCOMPARE(static_cast<int>(HPSDRModel::HPSDR),         0);
        QCOMPARE(static_cast<int>(HPSDRModel::HERMES),        1);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN10),        2);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN10E),       3);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN100),       4);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN100B),      5);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN100D),      6);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN200D),      7);
        QCOMPARE(static_cast<int>(HPSDRModel::ORIONMKII),     8);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN7000D),     9);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN8000D),    10);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN_G2),      11);
        QCOMPARE(static_cast<int>(HPSDRModel::ANAN_G2_1K),   12);
        QCOMPARE(static_cast<int>(HPSDRModel::ANVELINAPRO3), 13);
        QCOMPARE(static_cast<int>(HPSDRModel::HERMESLITE),   14);
        QCOMPARE(static_cast<int>(HPSDRModel::REDPITAYA),    15);
        QCOMPARE(static_cast<int>(HPSDRModel::LAST),         16);

        // Source: enums.cs:388 + network.h:446 — reserved gap at 7..9
        QCOMPARE(static_cast<int>(HPSDRHW::Atlas),        0);
        QCOMPARE(static_cast<int>(HPSDRHW::Hermes),       1);
        QCOMPARE(static_cast<int>(HPSDRHW::HermesII),     2);
        QCOMPARE(static_cast<int>(HPSDRHW::Angelia),      3);
        QCOMPARE(static_cast<int>(HPSDRHW::Orion),        4);
        QCOMPARE(static_cast<int>(HPSDRHW::OrionMKII),    5);
        QCOMPARE(static_cast<int>(HPSDRHW::HermesLite),   6);
        QCOMPARE(static_cast<int>(HPSDRHW::Saturn),      10);
        QCOMPARE(static_cast<int>(HPSDRHW::SaturnMKII),  11);
        QCOMPARE(static_cast<int>(HPSDRHW::Unknown),    999);
    }

    void boardForModelCoversEveryModel() {
        for (int i = 0; i < static_cast<int>(HPSDRModel::LAST); ++i) {
            auto m = static_cast<HPSDRModel>(i);
            auto hw = boardForModel(m);
            QVERIFY2(hw != HPSDRHW::Unknown,
                     qPrintable(QString("HPSDRModel %1 maps to Unknown").arg(i)));
        }
    }

    void displayNameNonNullForEveryModel() {
        for (int i = 0; i < static_cast<int>(HPSDRModel::LAST); ++i) {
            auto m = static_cast<HPSDRModel>(i);
            const char* name = displayName(m);
            QVERIFY(name != nullptr);
            QVERIFY(std::string(name).size() > 0);
        }
    }
};

QTEST_APPLESS_MAIN(TestHpsdrEnums)
#include "tst_hpsdr_enums.moc"
```

- [ ] **Step 3: Register the test in `tests/CMakeLists.txt`**

Find the existing block where `add_executable` is used for other `tst_*` targets and add:

```cmake
add_executable(tst_hpsdr_enums tst_hpsdr_enums.cpp)
target_link_libraries(tst_hpsdr_enums PRIVATE NereusSDRCore Qt6::Test)
add_test(NAME tst_hpsdr_enums COMMAND tst_hpsdr_enums)
```

- [ ] **Step 4: Run test — expect build failure** because `HpsdrModel.h` doesn't exist yet.

```bash
cd ~/NereusSDR && cmake --build build-clean --target tst_hpsdr_enums 2>&1 | tail -5
```

Expected: compile error `fatal error: 'core/HpsdrModel.h' file not found`.

- [ ] **Step 5: Write `src/core/HpsdrModel.h`**

```cpp
// src/core/HpsdrModel.h
//
// Port of HPSDR radio model and board-type enums.
// Source: mi0bot/Thetis@Hermes-Lite Project Files/Source/Console/enums.cs
//         :109 (HPSDRModel) and :388 (HPSDRHW).
// Integer values preserved exactly — including the 7..9 reserved gap in
// HPSDRHW — because the Thetis wire format compares these ints.

#pragma once

#include <QMetaType>

namespace NereusSDR {

// Logical radio model — what the user says they have in Setup.
// Source: enums.cs:109
enum class HPSDRModel : int {
    FIRST        = -1,
    HPSDR        =  0,  // Atlas/Metis kit
    HERMES       =  1,
    ANAN10       =  2,
    ANAN10E      =  3,
    ANAN100      =  4,
    ANAN100B     =  5,
    ANAN100D     =  6,
    ANAN200D     =  7,
    ORIONMKII    =  8,
    ANAN7000D    =  9,
    ANAN8000D    = 10,
    ANAN_G2      = 11,  // G8NJJ contribution
    ANAN_G2_1K   = 12,  // G8NJJ contribution
    ANVELINAPRO3 = 13,
    HERMESLITE   = 14,  // MI0BOT contribution
    REDPITAYA    = 15,  // DH1KLM contribution — enum slot preserved, impl deferred
    LAST         = 16
};

// Physical board — what's actually on the wire.
// Source: enums.cs:388 + ChannelMaster/network.h:446
enum class HPSDRHW : int {
    Atlas      =   0,  // HPSDR kit (aka Metis in PowerSDR)
    Hermes     =   1,  // ANAN-10 / ANAN-100
    HermesII   =   2,  // ANAN-10E / ANAN-100B
    Angelia    =   3,  // ANAN-100D
    Orion      =   4,  // ANAN-200D
    OrionMKII  =   5,  // ANAN-7000DLE / 8000DLE / AnvelinaPro3
    HermesLite =   6,  // Hermes Lite 2
    // 7..9 reserved — DO NOT REUSE (Thetis wire format compares these ints)
    Saturn     =  10,  // ANAN-G2
    SaturnMKII =  11,  // ANAN-G2 MkII board revision
    Unknown    = 999
};

constexpr HPSDRHW boardForModel(HPSDRModel m) noexcept {
    switch (m) {
        case HPSDRModel::HPSDR:        return HPSDRHW::Atlas;
        case HPSDRModel::HERMES:       return HPSDRHW::Hermes;
        case HPSDRModel::ANAN10:       return HPSDRHW::Hermes;
        case HPSDRModel::ANAN10E:      return HPSDRHW::HermesII;
        case HPSDRModel::ANAN100:      return HPSDRHW::Hermes;
        case HPSDRModel::ANAN100B:     return HPSDRHW::HermesII;
        case HPSDRModel::ANAN100D:     return HPSDRHW::Angelia;
        case HPSDRModel::ANAN200D:     return HPSDRHW::Orion;
        case HPSDRModel::ORIONMKII:    return HPSDRHW::OrionMKII;
        case HPSDRModel::ANAN7000D:    return HPSDRHW::OrionMKII;
        case HPSDRModel::ANAN8000D:    return HPSDRHW::OrionMKII;
        case HPSDRModel::ANAN_G2:      return HPSDRHW::Saturn;
        case HPSDRModel::ANAN_G2_1K:   return HPSDRHW::Saturn;
        case HPSDRModel::ANVELINAPRO3: return HPSDRHW::OrionMKII;
        case HPSDRModel::HERMESLITE:   return HPSDRHW::HermesLite;
        case HPSDRModel::REDPITAYA:    return HPSDRHW::OrionMKII;
        case HPSDRModel::FIRST:
        case HPSDRModel::LAST:         return HPSDRHW::Unknown;
    }
    return HPSDRHW::Unknown;
}

constexpr const char* displayName(HPSDRModel m) noexcept {
    switch (m) {
        case HPSDRModel::HPSDR:        return "HPSDR (Atlas/Metis)";
        case HPSDRModel::HERMES:       return "Hermes";
        case HPSDRModel::ANAN10:       return "ANAN-10";
        case HPSDRModel::ANAN10E:      return "ANAN-10E";
        case HPSDRModel::ANAN100:      return "ANAN-100";
        case HPSDRModel::ANAN100B:     return "ANAN-100B";
        case HPSDRModel::ANAN100D:     return "ANAN-100D";
        case HPSDRModel::ANAN200D:     return "ANAN-200D";
        case HPSDRModel::ORIONMKII:    return "Orion MkII";
        case HPSDRModel::ANAN7000D:    return "ANAN-7000DLE";
        case HPSDRModel::ANAN8000D:    return "ANAN-8000DLE";
        case HPSDRModel::ANAN_G2:      return "ANAN-G2";
        case HPSDRModel::ANAN_G2_1K:   return "ANAN-G2 1K";
        case HPSDRModel::ANVELINAPRO3: return "Anvelina Pro 3";
        case HPSDRModel::HERMESLITE:   return "Hermes Lite 2";
        case HPSDRModel::REDPITAYA:    return "Red Pitaya";
        case HPSDRModel::FIRST:
        case HPSDRModel::LAST:         return "Unknown";
    }
    return "Unknown";
}

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::HPSDRModel)
Q_DECLARE_METATYPE(NereusSDR::HPSDRHW)
```

- [ ] **Step 6: Build and run test — expect PASS**

```bash
cmake --build build-clean --target tst_hpsdr_enums && \
  ./build-clean/tests/tst_hpsdr_enums
```

Expected: `PASS : TestHpsdrEnums::integerValuesMatchThetis()` and all other slots PASS.

- [ ] **Step 7: Full suite regression check**

```bash
ctest --test-dir build-clean --output-on-failure
```

Expected: all existing tests still pass.

- [ ] **Step 8: Commit (GPG-signed)**

```bash
git add src/core/HpsdrModel.h tests/tst_hpsdr_enums.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): port HPSDRModel + HPSDRHW enums from mi0bot Thetis

Direct port from mi0bot/Thetis@Hermes-Lite enums.cs:109 and :388.
Integer values preserved exactly (incl. 7..9 reserved gap) to remain
wire-format compatible with Thetis. boardForModel() covers all 16 models.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: BoardCapabilities constexpr table

**Files:**
- Create: `src/core/BoardCapabilities.h`
- Create: `src/core/BoardCapabilities.cpp`
- Create: `tests/tst_board_capabilities.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/core/CMakeLists.txt` (add `BoardCapabilities.cpp`)

**Source references:**
- Thetis `Project Files/Source/Console/HPSDR/specHPSDR.cs` — per-board capability branches (walk every `case HPSDRHW.X:` block)
- Thetis `Project Files/Source/Console/Setup.cs` — grep for `HPSDRModel.` and `HPSDRHW.` for per-board UI gating
- mi0bot `Project Files/Source/Console/HPSDR/IoBoardHl2.cs` — HL2 extras
- mi0bot `Project Files/Source/ChannelMaster/bandwidth_monitor.h` — HL2 bandwidth monitor flag source

- [ ] **Step 1: Read Thetis source** and collect capability data per board. Grep commands:

```bash
cd /tmp/mi0bot-thetis-hl2
grep -n "case HPSDRHW\." "Project Files/Source/Console/HPSDR/specHPSDR.cs" | head -40
grep -n "HPSDRModel\." "Project Files/Source/Console/Setup.cs" | head -80
```

Build a scratch table with every field from §2.2 of the design doc for each of the 9 `HPSDRHW` values (including `Unknown`). Save the scratch table in your commit message draft as documentation of the translation.

- [ ] **Step 2: Write `tests/tst_board_capabilities.cpp`**

```cpp
#include <QtTest/QtTest>
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestBoardCapabilities : public QObject {
    Q_OBJECT
private slots:

    void forBoardReturnsMatchingEntry_data() {
        QTest::addColumn<int>("hw");
        QTest::newRow("Atlas")      << int(HPSDRHW::Atlas);
        QTest::newRow("Hermes")     << int(HPSDRHW::Hermes);
        QTest::newRow("HermesII")   << int(HPSDRHW::HermesII);
        QTest::newRow("Angelia")    << int(HPSDRHW::Angelia);
        QTest::newRow("Orion")      << int(HPSDRHW::Orion);
        QTest::newRow("OrionMKII")  << int(HPSDRHW::OrionMKII);
        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite);
        QTest::newRow("Saturn")     << int(HPSDRHW::Saturn);
        QTest::newRow("SaturnMKII") << int(HPSDRHW::SaturnMKII);
    }
    void forBoardReturnsMatchingEntry() {
        QFETCH(int, hw);
        const auto& caps = BoardCapsTable::forBoard(static_cast<HPSDRHW>(hw));
        QCOMPARE(static_cast<int>(caps.board), hw);
    }

    void forModelMatchesBoardForModel() {
        for (int i = 0; i < int(HPSDRModel::LAST); ++i) {
            auto m = static_cast<HPSDRModel>(i);
            const auto& caps = BoardCapsTable::forModel(m);
            QCOMPARE(caps.board, boardForModel(m));
        }
    }

    void sampleRatesAreSaneForEveryBoard() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.sampleRates[0] == 48000);
            int prev = 0;
            for (int sr : caps.sampleRates) {
                if (sr == 0) break;
                QVERIFY(sr > prev);
                QVERIFY(sr <= caps.maxSampleRate);
                prev = sr;
            }
        }
    }

    void attenuatorAbsentImpliesZeroRange() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (!caps.attenuator.present) {
                QCOMPARE(caps.attenuator.minDb, 0);
                QCOMPARE(caps.attenuator.maxDb, 0);
            } else {
                QVERIFY(caps.attenuator.minDb <= caps.attenuator.maxDb);
                QVERIFY(caps.attenuator.stepDb > 0);
            }
        }
    }

    void diversityRequiresTwoAdcs() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (caps.hasDiversityReceiver) {
                QCOMPARE(caps.adcCount, 2);
            }
        }
    }

    void alexTxRoutingImpliesAlexFilters() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (caps.hasAlexTxRouting) {
                QVERIFY(caps.hasAlexFilters);
            }
        }
    }

    void displayNameAndCitationNonNull() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.displayName != nullptr);
            QVERIFY(caps.sourceCitation != nullptr);
            QVERIFY(std::string(caps.displayName).size() > 0);
            QVERIFY(std::string(caps.sourceCitation).size() > 0);
        }
    }

    void firmwareMinIsLessOrEqualKnownGood() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.minFirmwareVersion <= caps.knownGoodFirmware);
        }
    }

    void hermesLiteHasHl2Extras() {
        const auto& caps = BoardCapsTable::forBoard(HPSDRHW::HermesLite);
        QVERIFY(caps.hasBandwidthMonitor);
        QVERIFY(caps.hasIoBoardHl2);
        QVERIFY(!caps.hasAlexFilters);
        QCOMPARE(caps.ocOutputCount, 0);
        QCOMPARE(caps.attenuator.maxDb, 60);
    }

    void angeliaHasDiversityAndPureSignal() {
        const auto& caps = BoardCapsTable::forBoard(HPSDRHW::Angelia);
        QVERIFY(caps.hasDiversityReceiver);
        QVERIFY(caps.hasPureSignal);
        QCOMPARE(caps.adcCount, 2);
    }
};

QTEST_APPLESS_MAIN(TestBoardCapabilities)
#include "tst_board_capabilities.moc"
```

- [ ] **Step 3: Register test in `tests/CMakeLists.txt`**

```cmake
add_executable(tst_board_capabilities tst_board_capabilities.cpp)
target_link_libraries(tst_board_capabilities PRIVATE NereusSDRCore Qt6::Test)
add_test(NAME tst_board_capabilities COMMAND tst_board_capabilities)
```

- [ ] **Step 4: Write `src/core/BoardCapabilities.h`**

```cpp
// src/core/BoardCapabilities.h
//
// Pure-data capability registry. One entry per HPSDRHW value.
// Drives both protocol connections and the Hardware setup UI.

#pragma once

#include "HpsdrModel.h"
#include "RadioDiscovery.h"  // for ProtocolVersion
#include <array>
#include <span>

namespace NereusSDR {

struct BoardCapabilities {
    HPSDRHW         board;
    ProtocolVersion protocol;

    int  adcCount;
    int  maxReceivers;
    std::array<int, 4> sampleRates;  // zero-pad unused slots
    int  maxSampleRate;

    struct Atten {
        int minDb;
        int maxDb;
        int stepDb;
        bool present;
    } attenuator;

    struct Preamp {
        bool present;
        bool hasBypassAndPreamp;
    } preamp;

    int  ocOutputCount;
    bool hasAlexFilters;
    bool hasAlexTxRouting;
    int  xvtrJackCount;
    int  antennaInputCount;

    bool hasPureSignal;
    bool hasDiversityReceiver;
    bool hasStepAttenuatorCal;
    bool hasPaProfile;

    bool hasBandwidthMonitor;
    bool hasIoBoardHl2;
    bool hasSidetoneGenerator;

    int  minFirmwareVersion;
    int  knownGoodFirmware;

    const char* displayName;
    const char* sourceCitation;
};

namespace BoardCapsTable {
    const BoardCapabilities& forBoard(HPSDRHW hw) noexcept;
    const BoardCapabilities& forModel(HPSDRModel m) noexcept;
    std::span<const BoardCapabilities> all() noexcept;
}

} // namespace NereusSDR
```

- [ ] **Step 5: Write `src/core/BoardCapabilities.cpp`** — one entry per board, each with a source-citation comment block. Include all 9 entries. Example structure (repeat pattern for every board):

```cpp
// src/core/BoardCapabilities.cpp
#include "BoardCapabilities.h"

namespace NereusSDR {
namespace {

// ─── Atlas / HPSDR kit ──────────────────────────────────────────────
// Source: specHPSDR.cs:HPSDRHW.Atlas branch, network.h:448
constexpr BoardCapabilities kAtlas = {
    .board = HPSDRHW::Atlas,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 1,
    .maxReceivers = 3,
    .sampleRates = {48000, 96000, 192000, 0},
    .maxSampleRate = 192000,
    .attenuator = {0, 0, 0, false},
    .preamp = {true, false},
    .ocOutputCount = 0,
    .hasAlexFilters = false,
    .hasAlexTxRouting = false,
    .xvtrJackCount = 0,
    .antennaInputCount = 1,
    .hasPureSignal = false,
    .hasDiversityReceiver = false,
    .hasStepAttenuatorCal = false,
    .hasPaProfile = false,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 0,
    .knownGoodFirmware = 0,
    .displayName = "Atlas / HPSDR kit",
    .sourceCitation = "mi0bot enums.cs:390, specHPSDR.cs Atlas branch",
};

// ─── Hermes (ANAN-10 / ANAN-100) ────────────────────────────────────
// Source: specHPSDR.cs:HPSDRHW.Hermes branch
constexpr BoardCapabilities kHermes = {
    .board = HPSDRHW::Hermes,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 1,
    .maxReceivers = 4,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, false},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = false,
    .hasDiversityReceiver = false,
    .hasStepAttenuatorCal = false,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 24,
    .knownGoodFirmware = 31,
    .displayName = "Hermes (ANAN-10/100)",
    .sourceCitation = "mi0bot enums.cs:391, specHPSDR.cs Hermes branch",
};

// ─── HermesII (ANAN-10E / ANAN-100B) ────────────────────────────────
// Source: specHPSDR.cs:HPSDRHW.HermesII branch
constexpr BoardCapabilities kHermesII = {
    .board = HPSDRHW::HermesII,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 1,
    .maxReceivers = 4,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, false},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = false,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 31,
    .knownGoodFirmware = 40,
    .displayName = "Hermes II (ANAN-10E/100B)",
    .sourceCitation = "mi0bot enums.cs:392, specHPSDR.cs HermesII branch",
};

// ─── Angelia (ANAN-100D) ────────────────────────────────────────────
constexpr BoardCapabilities kAngelia = {
    .board = HPSDRHW::Angelia,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 2,
    .maxReceivers = 7,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, false},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = true,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 10,
    .knownGoodFirmware = 21,
    .displayName = "ANAN-100D (Angelia)",
    .sourceCitation = "mi0bot enums.cs:393, specHPSDR.cs Angelia branch",
};

// ─── Orion (ANAN-200D) ──────────────────────────────────────────────
constexpr BoardCapabilities kOrion = {
    .board = HPSDRHW::Orion,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 2,
    .maxReceivers = 7,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, true},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = true,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 10,
    .knownGoodFirmware = 21,
    .displayName = "ANAN-200D (Orion)",
    .sourceCitation = "mi0bot enums.cs:394, specHPSDR.cs Orion branch",
};

// ─── OrionMKII (ANAN-7000DLE / 8000DLE / AnvelinaPro3) ─────────────
constexpr BoardCapabilities kOrionMKII = {
    .board = HPSDRHW::OrionMKII,
    .protocol = ProtocolVersion::Protocol2,
    .adcCount = 2,
    .maxReceivers = 7,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, true},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = true,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 10,
    .knownGoodFirmware = 21,
    .displayName = "ANAN-7000DLE/8000DLE (OrionMkII)",
    .sourceCitation = "mi0bot enums.cs:395, specHPSDR.cs OrionMKII branch",
};

// ─── HermesLite (HL2) ───────────────────────────────────────────────
constexpr BoardCapabilities kHermesLite = {
    .board = HPSDRHW::HermesLite,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 1,
    .maxReceivers = 4,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 60, 1, true},
    .preamp = {false, false},
    .ocOutputCount = 0,
    .hasAlexFilters = false,
    .hasAlexTxRouting = false,
    .xvtrJackCount = 0,
    .antennaInputCount = 1,
    .hasPureSignal = false,
    .hasDiversityReceiver = false,
    .hasStepAttenuatorCal = false,
    .hasPaProfile = false,
    .hasBandwidthMonitor = true,
    .hasIoBoardHl2 = true,
    .hasSidetoneGenerator = true,
    .minFirmwareVersion = 70,
    .knownGoodFirmware = 72,
    .displayName = "Hermes Lite 2",
    .sourceCitation = "mi0bot enums.cs:396, specHPSDR.cs HL2, IoBoardHl2.cs",
};

// ─── Saturn (ANAN-G2) ───────────────────────────────────────────────
constexpr BoardCapabilities kSaturn = {
    .board = HPSDRHW::Saturn,
    .protocol = ProtocolVersion::Protocol2,
    .adcCount = 2,
    .maxReceivers = 7,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, true},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = true,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 1,
    .knownGoodFirmware = 10,
    .displayName = "ANAN-G2 (Saturn)",
    .sourceCitation = "mi0bot enums.cs:397, Saturn G8NJJ branch",
};

// ─── SaturnMKII (ANAN-G2 MkII) ──────────────────────────────────────
constexpr BoardCapabilities kSaturnMKII = {
    .board = HPSDRHW::SaturnMKII,
    .protocol = ProtocolVersion::Protocol2,
    .adcCount = 2,
    .maxReceivers = 7,
    .sampleRates = {48000, 96000, 192000, 384000},
    .maxSampleRate = 384000,
    .attenuator = {0, 31, 1, true},
    .preamp = {true, true},
    .ocOutputCount = 7,
    .hasAlexFilters = true,
    .hasAlexTxRouting = true,
    .xvtrJackCount = 1,
    .antennaInputCount = 3,
    .hasPureSignal = true,
    .hasDiversityReceiver = true,
    .hasStepAttenuatorCal = true,
    .hasPaProfile = true,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 1,
    .knownGoodFirmware = 10,
    .displayName = "ANAN-G2 MkII (SaturnMKII)",
    .sourceCitation = "mi0bot enums.cs:398, SaturnMKII branch",
};

// ─── Unknown (fallback) ─────────────────────────────────────────────
constexpr BoardCapabilities kUnknown = {
    .board = HPSDRHW::Unknown,
    .protocol = ProtocolVersion::Protocol1,
    .adcCount = 1,
    .maxReceivers = 1,
    .sampleRates = {48000, 0, 0, 0},
    .maxSampleRate = 48000,
    .attenuator = {0, 0, 0, false},
    .preamp = {false, false},
    .ocOutputCount = 0,
    .hasAlexFilters = false,
    .hasAlexTxRouting = false,
    .xvtrJackCount = 0,
    .antennaInputCount = 1,
    .hasPureSignal = false,
    .hasDiversityReceiver = false,
    .hasStepAttenuatorCal = false,
    .hasPaProfile = false,
    .hasBandwidthMonitor = false,
    .hasIoBoardHl2 = false,
    .hasSidetoneGenerator = false,
    .minFirmwareVersion = 0,
    .knownGoodFirmware = 0,
    .displayName = "Unknown radio",
    .sourceCitation = "fallback entry",
};

constexpr std::array kTable = {
    kAtlas, kHermes, kHermesII, kAngelia, kOrion,
    kOrionMKII, kHermesLite, kSaturn, kSaturnMKII, kUnknown
};

} // anonymous namespace

namespace BoardCapsTable {

const BoardCapabilities& forBoard(HPSDRHW hw) noexcept {
    for (const auto& e : kTable) if (e.board == hw) return e;
    return kUnknown;
}

const BoardCapabilities& forModel(HPSDRModel m) noexcept {
    return forBoard(boardForModel(m));
}

std::span<const BoardCapabilities> all() noexcept {
    return std::span<const BoardCapabilities>(kTable.data(), kTable.size());
}

} // namespace BoardCapsTable
} // namespace NereusSDR
```

> **Note on per-board values:** Many fields above (firmware minimums, antenna input counts, xvtr jack counts) are reasonable defaults pending your source-first verification against `specHPSDR.cs`. During this step, grep Thetis for each field's actual value and update inline if the default is wrong. Record each correction in the commit message.

- [ ] **Step 6: Add `BoardCapabilities.cpp` to `src/core/CMakeLists.txt`** under the source list for `NereusSDRCore`.

- [ ] **Step 7: Build and run test — expect PASS**

```bash
cmake --build build-clean --target tst_board_capabilities && \
  ./build-clean/tests/tst_board_capabilities
```

- [ ] **Step 8: Full suite regression**

```bash
ctest --test-dir build-clean --output-on-failure
```

- [ ] **Step 9: Commit**

```bash
git add src/core/BoardCapabilities.h src/core/BoardCapabilities.cpp \
        src/core/CMakeLists.txt tests/tst_board_capabilities.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): BoardCapabilities constexpr registry — 9 HPSDRHW entries

Pure-data capability table driving both protocol connections and the
Hardware setup UI. One entry per HPSDRHW value with source citations
against Thetis specHPSDR.cs and mi0bot IoBoardHl2.cs.

Invariants tested: matching entry, model→board mapping, sample-rate
monotonicity, attenuator sanity, diversity⇒2-ADC, ALEX TX⇒ALEX filters,
firmware min ≤ knownGood, per-board spot checks for HL2 and Angelia.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Migrate BoardType → HPSDRHW across existing code

**Files:**
- Modify: `src/core/RadioDiscovery.h` — delete legacy `BoardType` enum; callers migrate
- Modify: `src/core/RadioDiscovery.cpp` — use `HPSDRHW` everywhere
- Modify: `src/core/P2RadioConnection.cpp` — use `HPSDRHW`
- Modify: `src/models/RadioModel.h/.cpp` — use `HPSDRHW`
- Modify: `src/gui/ConnectionPanel.cpp` — use `HPSDRHW`
- Modify: `src/gui/applets/DiversityApplet.cpp` — use `HPSDRHW`

**Source references:** none — this is a mechanical rename within NereusSDR.

- [ ] **Step 1: Grep for all `BoardType` usages**

```bash
cd ~/NereusSDR && grep -rn "BoardType" src/ tests/ --include="*.h" --include="*.cpp"
```

Record every file + line in a scratch list.

- [ ] **Step 2: In `src/core/RadioDiscovery.h`, delete the `BoardType` enum definition.** Add `#include "HpsdrModel.h"` at the top. Change `RadioInfo::boardType` from `BoardType` to `HPSDRHW`.

Expected diff shape:

```diff
-enum class BoardType : int {
-    Metis       = 0,
-    Hermes      = 1,
-    Griffin     = 2,
-    ...
-};
+#include "HpsdrModel.h"

 struct RadioInfo {
     ...
-    BoardType boardType{BoardType::Unknown};
+    HPSDRHW   boardType{HPSDRHW::Unknown};
     ...
 };
```

- [ ] **Step 3: Update every caller** by substituting each legacy name:

| Old | New |
|---|---|
| `BoardType::Metis` | `HPSDRHW::Atlas` |
| `BoardType::Hermes` | `HPSDRHW::Hermes` |
| `BoardType::Griffin` | `HPSDRHW::HermesII` (**bug fix**) |
| `BoardType::Angelia` | `HPSDRHW::Angelia` |
| `BoardType::Orion` | `HPSDRHW::Orion` |
| `BoardType::HermesLite` | `HPSDRHW::HermesLite` |
| `BoardType::OrionMkII` | `HPSDRHW::OrionMKII` |
| `BoardType::Saturn` | `HPSDRHW::Saturn` |
| `BoardType::Unknown` | `HPSDRHW::Unknown` |

Use search-and-replace per file. Do NOT use `sed -i .bak` across the tree — do it file by file in your editor so any conditional logic that distinguishes `Griffin` vs `HermesII` gets visually reviewed.

- [ ] **Step 4: Delete `RadioInfo::boardTypeName`** (if it exists) and replace call sites with `BoardCapsTable::forBoard(info.boardType).displayName`.

- [ ] **Step 5: Update `radio-abstraction.md`** — docs-only change, but do it as part of this commit so the break is self-contained. Delete the `Griffin` row, add `HermesII`/`SaturnMKII`/`ANAN_G2_1K`/`AnvelinaPro3`. Correct ADC counts + max RX against `BoardCapabilities`.

- [ ] **Step 6: Build — expect success**

```bash
cmake --build build-clean -j 2>&1 | tail -20
```

If any compilation errors remain, they indicate missed call sites. Fix them and rebuild.

- [ ] **Step 7: Run full suite**

```bash
ctest --test-dir build-clean --output-on-failure
```

Expected: all existing tests pass (Saturn P2 regression check).

- [ ] **Step 8: Launch app, connect to Saturn (if available), verify no regression** — per the user's auto-launch rule:

```bash
osascript -e 'tell application "NereusSDR" to quit' 2>/dev/null
open ~/NereusSDR/build-clean/NereusSDR.app
```

Manually verify Saturn discovery + connect still works.

- [ ] **Step 9: Commit**

```bash
git add src/ docs/architecture/radio-abstraction.md
git commit -m "$(cat <<'EOF'
refactor(3I): migrate BoardType → HPSDRHW, fix Griffin=2 → HermesII=2

Mechanical rename of the legacy BoardType enum to HPSDRHW (Thetis-faithful).
One functional correction: the BoardType::Griffin=2 slot was a docs error —
mi0bot enums.cs:392 clarifies it as HermesII (ANAN-10E/100B). Zero users
were on Griffin=2 in practice.

Also updates docs/architecture/radio-abstraction.md with the corrected
table including HermesII, SaturnMKII, ANAN_G2_1K, and AnvelinaPro3.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: RadioDiscovery port (mi0bot clsRadioDiscovery model)

**Files:**
- Modify: `src/core/RadioDiscovery.h` — add `DiscoveryProfile` enum + `DiscoveryTiming` struct
- Modify: `src/core/RadioDiscovery.cpp` — rewrite NIC walk + poll loop

**Source references:**
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/Console/HPSDR/clsRadioDiscovery.cs` — entire file, especially the timing-profile table at lines 56-74
- `~/NereusSDR/docs/protocols/openhpsdr-protocol1-capture-reference.md` — P1 discovery packet shape
- `~/Thetis/Project Files/Source/ChannelMaster/network.c:listen_*` — P2 discovery

- [ ] **Step 1: Read mi0bot clsRadioDiscovery.cs end-to-end.** Note which parts are replaceable (polling loop, profile table) and which are C#-idiom-specific (task cancellation, LINQ filters). Quote the profile table in your commit message.

- [ ] **Step 2: Add `DiscoveryProfile` + `DiscoveryTiming` to `src/core/RadioDiscovery.h`**

```cpp
namespace NereusSDR {

enum class DiscoveryProfile {
    UltraFast,
    VeryFast,
    Fast,
    Balanced,
    SafeDefault,
    VeryTolerant
};

struct DiscoveryTiming {
    int attemptsPerNic;
    int quietPollsBeforeResend;
    int pollTimeoutMs;
};

constexpr DiscoveryTiming timingFor(DiscoveryProfile p) noexcept {
    // Source: mi0bot/Thetis@Hermes-Lite clsRadioDiscovery.cs:56-74
    switch (p) {
        case DiscoveryProfile::UltraFast:    return {1, 2,  40};
        case DiscoveryProfile::VeryFast:     return {1, 3,  60};
        case DiscoveryProfile::Fast:         return {2, 3,  80};
        case DiscoveryProfile::Balanced:     return {2, 4, 100};
        case DiscoveryProfile::SafeDefault:  return {3, 5, 150};
        case DiscoveryProfile::VeryTolerant: return {3, 6, 300};
    }
    return {3, 5, 150};
}

} // namespace NereusSDR
```

- [ ] **Step 3: Update `RadioDiscovery` class to accept a `DiscoveryProfile`** (default `SafeDefault`) and store it. Add a public setter.

- [ ] **Step 4: Rewrite the scan loop in `RadioDiscovery.cpp`** following the clsRadioDiscovery pattern:
  1. Enumerate NICs via `QNetworkInterface::allInterfaces()`, filter up + IPv4 + non-loopback.
  2. For each NIC, bind a temporary `QUdpSocket` to that address + ephemeral port.
  3. Build and send the 63-byte P1 discovery frame (`0xEFFE 0x02` prefix, 60 zero bytes) AND the 60-byte P2 discovery frame. Use the existing helpers if they already compose these; otherwise port from `networkproto1.c` and `network.c`.
  4. Run `attemptsPerNic × quietPollsBeforeResend × pollTimeoutMs` as a poll loop with `socket->waitForReadyRead(pollTimeoutMs)`.
  5. Parse every reply, de-dupe by MAC across NICs.
  6. Emit `radioDiscovered(RadioInfo)` for each unique reply.

(Keep reply parsing abstracted into `parseP1Reply` / `parseP2Reply` helpers — Task 5 writes tests for those.)

- [ ] **Step 5: Build and run existing tests** — anything previously calling `RadioDiscovery` should still work since the public signature is additive.

```bash
cmake --build build-clean -j && ctest --test-dir build-clean --output-on-failure
```

- [ ] **Step 6: Smoke-test discovery against Saturn** (if available):

```bash
open ~/NereusSDR/build-clean/NereusSDR.app
# Open ConnectionPanel, click Discover, verify Saturn appears
```

- [ ] **Step 7: Commit**

```bash
git add src/core/RadioDiscovery.h src/core/RadioDiscovery.cpp
git commit -m "$(cat <<'EOF'
feat(3I): port mi0bot clsRadioDiscovery — tunable timing profiles

Rewrites RadioDiscovery's NIC walk and poll loop following
mi0bot/Thetis@Hermes-Lite clsRadioDiscovery.cs. Six timing profiles from
UltraFast (80ms/NIC) to VeryTolerant (5400ms/NIC) with SafeDefault
(1800ms/NIC) as the out-of-the-box setting. Dual P1+P2 probe per NIC
with MAC-based de-duplication.

Source: clsRadioDiscovery.cs:56-74 (profile table),
        clsRadioDiscovery.cs:120+ (NIC walk + poll loop)

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Discovery reply parsing + hex fixtures

**Files:**
- Create: `tests/fixtures/discovery/p1_hermeslite_reply.hex`
- Create: `tests/fixtures/discovery/p1_angelia_reply.hex`
- Create: `tests/fixtures/discovery/p2_saturn_reply.hex`
- Create: `tests/tst_radio_discovery_parse.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/core/RadioDiscovery.cpp` — expose `parseP1Reply` / `parseP2Reply` for testing via an internal header or `friend` declaration

**Source references:**
- `~/NereusSDR/docs/protocols/openhpsdr-protocol1-capture-reference.md` — byte layout of the P1 discovery reply
- `~/Thetis/Project Files/Source/ChannelMaster/networkproto1.c` — `DiscoveryReply` struct
- `~/Thetis/Project Files/Source/ChannelMaster/network.c:listen_discovery_thread` — P2 discovery reply layout

- [ ] **Step 1: Capture real discovery replies** from Saturn (live) and from the P1 pcap doc. For P1 boards you don't own, hand-craft the 60-byte reply per the capture reference doc:

```
tests/fixtures/discovery/p1_hermeslite_reply.hex
# Hex dump of the 60-byte P1 discovery reply for an HL2
# Byte 0-1: 0xEFFE
# Byte 2:   0x02 (free) or 0x03 (in-use)
# Byte 3-8: MAC (aa bb cc 11 22 33)
# Byte 9:   firmware version (0x48 = 72)
# Byte 10:  board type (0x06 = HermesLite)
# Bytes 11-59: zeros
EFFE0200AABBCC112233480600000000
00000000000000000000000000000000
00000000000000000000000000000000
00000000
```

One fixture per board. Use space- and newline-tolerant hex format that the test can load via `QByteArray::fromHex`.

- [ ] **Step 2: Write `tests/tst_radio_discovery_parse.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QFile>
#include "core/RadioDiscovery.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestRadioDiscoveryParse : public QObject {
    Q_OBJECT
private:
    static QByteArray loadFixture(const char* name) {
        QFile f(QStringLiteral("%1/fixtures/discovery/%2")
                .arg(TEST_DATA_DIR).arg(name));
        if (!f.open(QIODevice::ReadOnly)) return {};
        auto hex = f.readAll();
        hex.replace('\n', "").replace(' ', "").replace('\r', "");
        // strip comment lines starting with '#'
        QByteArray cleaned;
        for (auto line : hex.split('\n')) {
            if (line.startsWith('#')) continue;
            cleaned += line;
        }
        return QByteArray::fromHex(cleaned);
    }
private slots:
    void parseP1HermesLiteReply() {
        auto bytes = loadFixture("p1_hermeslite_reply.hex");
        QVERIFY(bytes.size() >= 11);
        RadioInfo info;
        QVERIFY(RadioDiscovery::parseP1Reply(bytes, QHostAddress("192.168.1.42"), info));
        QCOMPARE(info.boardType, HPSDRHW::HermesLite);
        QCOMPARE(info.firmwareVersion, 72);
        QCOMPARE(info.protocol, ProtocolVersion::Protocol1);
        QCOMPARE(info.macAddress, QStringLiteral("aa:bb:cc:11:22:33"));
        QCOMPARE(info.inUse, false);
    }

    void parseP1AngeliaReply() {
        auto bytes = loadFixture("p1_angelia_reply.hex");
        RadioInfo info;
        QVERIFY(RadioDiscovery::parseP1Reply(bytes, QHostAddress("192.168.1.20"), info));
        QCOMPARE(info.boardType, HPSDRHW::Angelia);
        QVERIFY(info.firmwareVersion >= 10);
    }

    void parseP2SaturnReply() {
        auto bytes = loadFixture("p2_saturn_reply.hex");
        RadioInfo info;
        QVERIFY(RadioDiscovery::parseP2Reply(bytes, QHostAddress("192.168.1.77"), info));
        QCOMPARE(info.boardType, HPSDRHW::Saturn);
        QCOMPARE(info.protocol, ProtocolVersion::Protocol2);
    }

    void shortPacketRejected() {
        QByteArray tooShort(5, '\0');
        RadioInfo info;
        QVERIFY(!RadioDiscovery::parseP1Reply(tooShort, QHostAddress("1.2.3.4"), info));
        QVERIFY(!RadioDiscovery::parseP2Reply(tooShort, QHostAddress("1.2.3.4"), info));
    }

    void inUseFlagParsed() {
        auto bytes = loadFixture("p1_hermeslite_reply.hex");
        bytes[2] = 0x03;  // mark in-use
        RadioInfo info;
        QVERIFY(RadioDiscovery::parseP1Reply(bytes, QHostAddress("192.168.1.42"), info));
        QCOMPARE(info.inUse, true);
    }
};

QTEST_APPLESS_MAIN(TestRadioDiscoveryParse)
#include "tst_radio_discovery_parse.moc"
```

- [ ] **Step 3: Register test and fixture-data path in `tests/CMakeLists.txt`**

```cmake
add_executable(tst_radio_discovery_parse tst_radio_discovery_parse.cpp)
target_link_libraries(tst_radio_discovery_parse PRIVATE NereusSDRCore Qt6::Test Qt6::Network)
target_compile_definitions(tst_radio_discovery_parse PRIVATE
    TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME tst_radio_discovery_parse COMMAND tst_radio_discovery_parse)
```

- [ ] **Step 4: Expose `parseP1Reply` / `parseP2Reply`** as public static functions on `RadioDiscovery` (or a free function in the same header). Implementation ports directly from Thetis `networkproto1.c` and `network.c` — cite lines in the function body.

- [ ] **Step 5: Run test — expect FAIL** (parser not yet implemented or fixtures don't match).

```bash
cmake --build build-clean --target tst_radio_discovery_parse && \
  ./build-clean/tests/tst_radio_discovery_parse
```

- [ ] **Step 6: Implement until PASS.** Both parsers must extract MAC, firmware, board type, inUse flag, and synthesize protocol version.

- [ ] **Step 7: Full suite regression**

- [ ] **Step 8: Commit**

```bash
git add tests/fixtures/discovery/ tests/tst_radio_discovery_parse.cpp \
        tests/CMakeLists.txt src/core/RadioDiscovery.h src/core/RadioDiscovery.cpp
git commit -m "$(cat <<'EOF'
feat(3I): RadioDiscovery reply parsers + hex fixtures

Pure-function parseP1Reply and parseP2Reply extracted from the discovery
loop so they can be unit-tested against captured hex fixtures. Covers
HL2, Angelia, and Saturn replies plus short-packet and in-use edge cases.

Source: networkproto1.c DiscoveryReply struct, network.c listen_discovery_thread

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: P1RadioConnection skeleton + factory hookup

**Files:**
- Create: `src/core/P1RadioConnection.h`
- Create: `src/core/P1RadioConnection.cpp` (stub methods — no wire format yet)
- Modify: `src/core/RadioConnection.cpp` — factory P1 branch
- Modify: `src/core/CMakeLists.txt`

**Source references:**
- `src/core/P2RadioConnection.h/.cpp` — the proven template; P1 mirrors its shape
- `~/NereusSDR/docs/architecture/phase3i-radio-connector-port-design.md` §3.2 — class shape

- [ ] **Step 1: Write `src/core/P1RadioConnection.h`** with the full class declaration from §3.2 of the design doc. All public slots are declared; private helpers are declared; data members are declared. Methods are stubs (log a `qCWarning` and return).

- [ ] **Step 2: Write `src/core/P1RadioConnection.cpp`** — empty-but-compilable stubs. `init()` creates `m_socket` and timers. `connectToRadio()` logs "not yet implemented" and sets state to `Error`. All other methods are empty.

- [ ] **Step 3: Register `P1RadioConnection.cpp` in `src/core/CMakeLists.txt`.**

- [ ] **Step 4: Update `RadioConnection::create` factory** in `src/core/RadioConnection.cpp`:

```cpp
std::unique_ptr<RadioConnection> RadioConnection::create(const RadioInfo& info)
{
    switch (info.protocol) {
        case ProtocolVersion::Protocol2:
            return std::make_unique<P2RadioConnection>();
        case ProtocolVersion::Protocol1:
            return std::make_unique<P1RadioConnection>();
    }
    qCWarning(lcConnection) << "Unknown protocol version:"
                            << static_cast<int>(info.protocol);
    return nullptr;
}
```

Add `#include "P1RadioConnection.h"` at the top.

- [ ] **Step 5: Build — expect success.**

- [ ] **Step 6: Run existing suite — expect no regression.**

- [ ] **Step 7: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp \
        src/core/RadioConnection.cpp src/core/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): P1RadioConnection skeleton + factory hookup

Empty-but-compilable class with the full public API declared. The
factory now constructs P1RadioConnection for Protocol1 RadioInfo;
previously returned nullptr with "not yet implemented". All methods
are stubs — Task 7 fills in the wire format.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: P1 wire format — compose (ep2 command frames + C&C banks)

**Files:**
- Modify: `src/core/P1RadioConnection.h` — add static compose helpers
- Modify: `src/core/P1RadioConnection.cpp`
- Create: `tests/tst_p1_wire_format.cpp`
- Modify: `tests/CMakeLists.txt`

**Source references:**
- `~/Thetis/Project Files/Source/Console/HPSDR/NetworkIO.cs` — `SetC0..SetC4` and the `BandwidthControl*` / `Alex*` methods
- `~/Thetis/Project Files/Source/ChannelMaster/networkproto1.c` — ep2 frame assembly
- `~/NereusSDR/docs/protocols/openhpsdr-protocol1.md` — reference

- [ ] **Step 1: Read Thetis NetworkIO.cs end-to-end for the ep2 compose path.** Document each C&C bank's byte layout in your commit message draft.

- [ ] **Step 2: Write `tests/tst_p1_wire_format.cpp`** — unit tests for `composeCcBank0..14`, sample rate encoding, frequency encoding. Tests use static helpers so no socket/thread needed.

```cpp
#include <QtTest/QtTest>
#include "core/P1RadioConnection.h"

using namespace NereusSDR;

class TestP1WireFormat : public QObject {
    Q_OBJECT
private slots:
    void ep2FrameStartsWithMagicBytes() {
        quint8 frame[1032] = {};
        P1RadioConnection::composeEp2Frame(frame, /*seq=*/0, /*ccBank=*/0);
        // Source: networkproto1.c send_ep2 — magic 0xEFFE 0x01 0x02
        QCOMPARE(frame[0], quint8(0xEF));
        QCOMPARE(frame[1], quint8(0xFE));
        QCOMPARE(frame[2], quint8(0x01));
        QCOMPARE(frame[3], quint8(0x02));
    }

    void ccBank0EncodesSampleRate() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, /*sampleRate=*/192000,
                                           /*mox=*/false, /*tenMhzRef=*/0);
        // Source: NetworkIO.cs:SetC0 — bits 1..0 of byte C1 encode sample rate
        // 48k=00, 96k=01, 192k=10, 384k=11
        QCOMPARE(int(out[1] & 0x03), 2);
    }

    void ccBank0SetsMoxBit() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBank0(out, 48000, /*mox=*/true, 0);
        // Source: NetworkIO.cs:SetC0 — C0 bit 0 is MOX
        QCOMPARE(int(out[0] & 0x01), 1);
    }

    void rxFrequencyEncodedBigEndian32() {
        quint8 out[5] = {};
        quint64 freqHz = 14200000ULL;
        P1RadioConnection::composeCcBankRxFreq(out, /*rxIndex=*/0, freqHz);
        // C1..C4 hold freq in big-endian 32-bit
        quint32 readBack = (quint32(out[1]) << 24) | (quint32(out[2]) << 16)
                         | (quint32(out[3]) <<  8) |  quint32(out[4]);
        QCOMPARE(readBack, 14200000U);
    }

    void attenuatorEncodedInBankMax31() {
        quint8 out[5] = {};
        P1RadioConnection::composeCcBankAtten(out, /*dB=*/20);
        // Source: NetworkIO.cs:SetAlexAtten
        QVERIFY((out[4] & 0x1F) == 20);
    }
};

QTEST_APPLESS_MAIN(TestP1WireFormat)
#include "tst_p1_wire_format.moc"
```

- [ ] **Step 3: Register test in `tests/CMakeLists.txt`**

```cmake
add_executable(tst_p1_wire_format tst_p1_wire_format.cpp)
target_link_libraries(tst_p1_wire_format PRIVATE NereusSDRCore Qt6::Test)
add_test(NAME tst_p1_wire_format COMMAND tst_p1_wire_format)
```

- [ ] **Step 4: Run test — expect build error** (compose functions don't exist yet).

- [ ] **Step 5: Implement compose functions as public static methods** on `P1RadioConnection`. Port each from `NetworkIO.cs` with file:line citation in the function body. Include at minimum:

```cpp
static void composeEp2Frame(quint8 out[1032], quint32 seq, int ccBankIdx);
static void composeCcBank0(quint8 out[5], int sampleRate, bool mox, int tenMhzRef);
static void composeCcBankRxFreq(quint8 out[5], int rxIndex, quint64 freqHz);
static void composeCcBankTxFreq(quint8 out[5], quint64 freqHz);
static void composeCcBankAtten(quint8 out[5], int dB);
static void composeCcBankAlexRx(quint8 out[5], quint32 alexRxMask);
static void composeCcBankAlexTx(quint8 out[5], quint32 alexTxMask);
static void composeCcBankOcOutputs(quint8 out[5], quint8 ocMask);
```

Each implementation cites `NetworkIO.cs:<line>` in a comment. Do not invent bit positions — read them from Thetis.

- [ ] **Step 6: Run test — expect PASS.**

- [ ] **Step 7: Full suite regression.**

- [ ] **Step 8: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp \
        tests/tst_p1_wire_format.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): P1 wire format — ep2 frame + C&C bank compose

Static compose functions for the P1 ep2 command frame and each C&C
bank Thetis NetworkIO.cs writes. Unit-tested against captured bit
positions for magic header, sample-rate encoding, MOX flag, RX/TX
frequency, attenuator, ALEX masks, and OC outputs.

Source: NetworkIO.cs SetC0..SetC4, networkproto1.c send_ep2

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: P1 wire format — parse (ep6 I/Q frames)

**Files:**
- Modify: `src/core/P1RadioConnection.h` — add parse helpers
- Modify: `src/core/P1RadioConnection.cpp`
- Modify: `tests/tst_p1_wire_format.cpp` — add parse tests
- Create: `tests/fixtures/ep6/p1_ep6_384k_2rx.bin` — captured ep6 frame

**Source references:**
- `~/Thetis/Project Files/Source/ChannelMaster/networkproto1.c:<parse_ep6>` — USB frame walker
- `docs/protocols/openhpsdr-protocol1-capture-reference.md` — ep6 layout annotated

- [ ] **Step 1: Capture or hand-craft a known-good ep6 frame.** Use the P1 capture reference doc to construct a 1032-byte frame carrying N samples at a known RX1 frequency and known sample value (e.g., a DC tone at I=0x400000, Q=0). Save as binary file `tests/fixtures/ep6/p1_ep6_384k_2rx.bin`.

- [ ] **Step 2: Add parse tests** to `tst_p1_wire_format.cpp`:

```cpp
    void scaleSample24DcTone() {
        quint8 be24[3] = {0x40, 0x00, 0x00};  // +0x400000 = 4194304
        float f = P1RadioConnection::scaleSample24(be24);
        // 24-bit full scale 2^23 = 8388608
        QVERIFY(qAbs(f - 0.5f) < 0.0001f);
    }

    void scaleSample24Negative() {
        quint8 be24[3] = {0xC0, 0x00, 0x00};  // -0x400000 sign-extended
        float f = P1RadioConnection::scaleSample24(be24);
        QVERIFY(qAbs(f - (-0.5f)) < 0.0001f);
    }

    void parseEp6FrameEmitsExpectedSampleCount() {
        QFile f(QStringLiteral("%1/fixtures/ep6/p1_ep6_384k_2rx.bin").arg(TEST_DATA_DIR));
        QVERIFY(f.open(QIODevice::ReadOnly));
        QByteArray frame = f.readAll();
        QCOMPARE(frame.size(), 1032);

        std::vector<std::vector<float>> perRx;
        QVERIFY(P1RadioConnection::parseEp6Frame(
            reinterpret_cast<const quint8*>(frame.constData()),
            /*numRx=*/2, perRx));
        // Source: networkproto1.c — 2 × 512-byte USB frames, 63 samples each
        // at 2 RX, so 126 interleaved I/Q pairs total
        QCOMPARE(perRx.size(), size_t(2));
        QVERIFY(perRx[0].size() > 0);
        QVERIFY(perRx[0].size() == perRx[1].size());
    }
```

- [ ] **Step 3: Implement `scaleSample24` and `parseEp6Frame`** as public statics on `P1RadioConnection`. Cite networkproto1.c line numbers inline.

```cpp
// src/core/P1RadioConnection.cpp
// Source: networkproto1.c:<line> — 24-bit big-endian signed → float
float P1RadioConnection::scaleSample24(const quint8* be24) noexcept {
    qint32 v = (qint32(be24[0]) << 24) | (qint32(be24[1]) << 16) | (qint32(be24[2]) << 8);
    v >>= 8;  // sign-extend via arithmetic shift
    return float(v) / 8388608.0f;  // 2^23
}
```

- [ ] **Step 4: Run tests — expect PASS.**

- [ ] **Step 5: Commit**

```bash
git add src/core/P1RadioConnection.cpp src/core/P1RadioConnection.h \
        tests/tst_p1_wire_format.cpp tests/fixtures/ep6/
git commit -m "$(cat <<'EOF'
feat(3I): P1 wire format — ep6 frame parser + sample scaler

parseEp6Frame walks the two 512-byte USB subframes within a 1032-byte
ep6 datagram, extracts interleaved I/Q samples per hardware RX, and
scales 24-bit big-endian signed → float [-1, 1]. Unit-tested against a
captured DC-tone fixture plus boundary scale values.

Source: networkproto1.c:parse_ep6 + capture-reference doc

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: P1 loopback integration test (P1FakeRadio)

**Files:**
- Create: `tests/fakes/P1FakeRadio.h`
- Create: `tests/fakes/P1FakeRadio.cpp`
- Create: `tests/tst_p1_loopback_connection.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `src/core/P1RadioConnection.cpp` — implement full socket path using the compose/parse helpers from Tasks 7 & 8

**Source references:** design doc §3.3 — success bar, `emit iqDataReceived` convergence point.

- [ ] **Step 1: Write `tests/fakes/P1FakeRadio.{h,cpp}`** — a `QObject` that binds a `QUdpSocket` on `127.0.0.1:0`, responds to a discovery broadcast with a canned reply, accepts metis-start on ep2, then streams a configurable number of ep6 frames with a known DC tone. Exposes `localAddress()` and `localPort()` for the test to feed into `RadioInfo`.

- [ ] **Step 2: Write `tests/tst_p1_loopback_connection.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/P1RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "fakes/P1FakeRadio.h"

using namespace NereusSDR;

class TestP1LoopbackConnection : public QObject {
    Q_OBJECT
private slots:
    void rxPathEndToEnd() {
        P1FakeRadio fake;
        fake.start();

        RadioInfo info;
        info.address    = fake.localAddress();
        info.port       = fake.localPort();
        info.boardType  = HPSDRHW::HermesLite;
        info.protocol   = ProtocolVersion::Protocol1;
        info.macAddress = "aa:bb:cc:11:22:33";
        info.firmwareVersion = 72;

        P1RadioConnection conn;
        conn.init();

        QSignalSpy stateSpy(&conn, &RadioConnection::connectionStateChanged);
        QSignalSpy dataSpy(&conn, &RadioConnection::iqDataReceived);

        conn.connectToRadio(info);
        QVERIFY(stateSpy.wait(2000));

        // Fake emits 10 ep6 frames after start
        fake.sendEp6Frames(10);
        QVERIFY(dataSpy.wait(2000));
        QVERIFY(dataSpy.count() >= 1);

        // Verify sample format
        auto args = dataSpy.last();
        auto samples = args.at(1).value<QVector<float>>();
        QVERIFY(samples.size() > 0);
        QVERIFY((samples.size() % 2) == 0);  // interleaved I/Q

        conn.disconnect();
        fake.stop();
    }
};

QTEST_MAIN(TestP1LoopbackConnection)
#include "tst_p1_loopback_connection.moc"
```

- [ ] **Step 3: Implement the full socket path in `P1RadioConnection.cpp`** — `init()` creates the socket and watchdog timer; `connectToRadio()` sends metis-start, subscribes to `readyRead`; `onReadyRead()` calls `parseEp6Frame` and emits `iqDataReceived`; `sendCommandFrame()` uses the compose helpers from Task 7.

- [ ] **Step 4: Register test + fake in CMake.** Fakes live under `tests/fakes/` and link into test binaries that need them.

- [ ] **Step 5: Run test — expect FAIL first, then iterate until PASS.**

- [ ] **Step 6: Regression check** — existing Saturn tests must still pass.

- [ ] **Step 7: Smoke test on hardware** — if HL2 available, connect via the live app and verify audio. Log any anomalies to the commit message.

- [ ] **Step 8: Commit**

```bash
git add src/core/P1RadioConnection.cpp tests/fakes/ tests/tst_p1_loopback_connection.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): P1 loopback RX path — end-to-end integration test

Wires the Task 7 compose helpers and Task 8 parse helpers into the
live socket path. P1FakeRadio binds loopback UDP, replies to discovery,
accepts metis-start, and streams canned ep6 frames. Integration test
verifies RadioConnection::iqDataReceived emits correctly-scaled I/Q.

This is the convergence point: downstream WDSP is untouched, so P1
radios now go through the same demod path as Saturn on P2.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: P1 reconnection state machine

**Files:**
- Modify: `src/core/P1RadioConnection.cpp` — watchdog + reconnect timer logic
- Create: `tests/tst_reconnect_on_silence.cpp`
- Modify: `tests/CMakeLists.txt`

**Source references:** design doc §3.6 + §6.2. No direct Thetis equivalent — NereusSDR's pattern, informed by P2's existing watchdog.

- [ ] **Step 1: Write `tests/tst_reconnect_on_silence.cpp`**

```cpp
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/P1RadioConnection.h"
#include "fakes/P1FakeRadio.h"

using namespace NereusSDR;

class TestReconnectOnSilence : public QObject {
    Q_OBJECT
private slots:
    void silenceTriggersErrorThenRetry() {
        P1FakeRadio fake;
        fake.start();

        RadioInfo info;
        info.address = fake.localAddress();
        info.port = fake.localPort();
        info.boardType = HPSDRHW::HermesLite;
        info.protocol = ProtocolVersion::Protocol1;
        info.firmwareVersion = 72;

        P1RadioConnection conn;
        conn.init();
        QSignalSpy stateSpy(&conn, &RadioConnection::connectionStateChanged);

        conn.connectToRadio(info);
        QVERIFY(stateSpy.wait(2000));
        // Connected
        QCOMPARE(conn.state(), ConnectionState::Connected);

        fake.goSilent();   // stop replying to ep6 polls

        // Wait for watchdog → Error
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Error, 5000);

        // Reconnect timer should attempt rediscovery within 6s
        fake.resume();
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Connected, 10000);
    }

    void boundedRetries() {
        P1FakeRadio fake;
        fake.start();

        RadioInfo info;
        info.address = fake.localAddress();
        info.port = fake.localPort();
        info.boardType = HPSDRHW::HermesLite;
        info.protocol = ProtocolVersion::Protocol1;

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(info);

        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Connected, 2000);

        fake.stop();  // never come back

        // After 3 retries (~15s), state must settle on Error and stop retrying
        QTRY_VERIFY_WITH_TIMEOUT(
            conn.state() == ConnectionState::Error, 20000);
        // Wait another 10s to confirm no more retries happen
        QTest::qWait(10000);
        QCOMPARE(conn.state(), ConnectionState::Error);
    }
};

QTEST_MAIN(TestReconnectOnSilence)
#include "tst_reconnect_on_silence.moc"
```

- [ ] **Step 2: Register test in CMake.**

- [ ] **Step 3: Implement watchdog + reconnect logic** in `P1RadioConnection.cpp` per §3.6. 2-second no-data timeout → `Error`; 5-second reconnect timer bounded at 3 attempts.

- [ ] **Step 4: Run test — expect PASS. Run full suite.**

- [ ] **Step 5: Commit**

```bash
git add src/core/P1RadioConnection.cpp tests/tst_reconnect_on_silence.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): P1 reconnection state machine — bounded retries on silence

2s ep6 silence → Error. 5s reconnect timer, 3 bounded attempts, then
stay in Error until user explicitly reconnects. Verified with
P1FakeRadio going silent and resuming.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: P1 per-board quirks (HL2 atten range, OC/ALEX zeros, firmware gate)

**Files:**
- Modify: `src/core/P1RadioConnection.cpp` — `applyBoardQuirks`, `checkFirmwareMinimum`, atten clamp
- Modify: `tests/tst_p1_wire_format.cpp` — add per-board quirk tests

**Source references:**
- mi0bot `specHPSDR.cs` HL2 branch
- mi0bot `IoBoardHl2.cs:1-80`

- [ ] **Step 1: Read mi0bot HL2 branches** for attenuator semantics, OC output gating, and ALEX bypass behavior.

- [ ] **Step 2: Add quirk tests** to `tst_p1_wire_format.cpp`:

```cpp
    void hermesLiteAttenClampsTo60() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        conn.setAttenuator(80);  // above HL2 range
        QCOMPARE(conn.currentAttenForTest(), 60);
    }

    void hermesAttenClampsTo31() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::Hermes);
        conn.setAttenuator(80);
        QCOMPARE(conn.currentAttenForTest(), 31);
    }

    void hermesLiteOcOutputsAlwaysZero() {
        P1RadioConnection conn;
        conn.setBoardForTest(HPSDRHW::HermesLite);
        quint8 out[5] = {};
        // Even if caller tries to set mask, HL2 writes zero
        conn.composeOcBankForTest(out, 0xFF);
        QCOMPARE(int(out[1]), 0);
    }

    void firmwareBelowMinRefusesConnect() {
        P1FakeRadio fake;
        fake.setFirmwareVersion(50);  // below HL2 minimum 70
        fake.start();
        RadioInfo info;
        info.boardType = HPSDRHW::HermesLite;
        info.protocol = ProtocolVersion::Protocol1;
        info.firmwareVersion = 50;
        info.address = fake.localAddress();
        info.port = fake.localPort();

        P1RadioConnection conn;
        conn.init();
        QSignalSpy errSpy(&conn, &RadioConnection::errorOccurred);
        conn.connectToRadio(info);
        QVERIFY(errSpy.wait(1000));
        QCOMPARE(conn.state(), ConnectionState::Error);
        fake.stop();
    }
```

Add `setBoardForTest`, `currentAttenForTest`, `composeOcBankForTest` as `#ifdef QT_TESTLIB_LIB` test hooks on `P1RadioConnection`.

- [ ] **Step 3: Implement `applyBoardQuirks(HPSDRHW)`** that reads from `BoardCapsTable::forBoard` and sets internal state. Implement atten clamping in `setAttenuator` using `m_caps->attenuator.maxDb`. Implement OC zero-writing when `m_caps->ocOutputCount == 0`. Implement firmware check in `connectToRadio`.

- [ ] **Step 4: Run tests — expect PASS.**

- [ ] **Step 5: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp tests/tst_p1_wire_format.cpp
git commit -m "$(cat <<'EOF'
feat(3I): P1 per-board quirks — atten clamp, OC zero, firmware gate

applyBoardQuirks reads BoardCapabilities and adjusts runtime behaviour:
HL2 atten clamps 0..60, Hermes clamps 0..31, boards with ocOutputCount=0
force OC mask to zero, firmware below minFirmwareVersion refuses connect
with an errorOccurred signal.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 12: HL2 IoBoardHl2 TLV + bandwidth monitor

**Files:**
- Modify: `src/core/P1RadioConnection.h` — HL2 TLV sender, bandwidth-monitor subscriber
- Modify: `src/core/P1RadioConnection.cpp`

**Source references:**
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/Console/HPSDR/IoBoardHl2.cs` (entire file)
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/ChannelMaster/bandwidth_monitor.h`
- `/tmp/mi0bot-thetis-hl2/Project Files/Source/ChannelMaster/bandwidth_monitor.c`

- [ ] **Step 1: Read `IoBoardHl2.cs`** end-to-end. Document the TLV command set in your commit message. Note the init sequence Thetis sends on connect.

- [ ] **Step 2: Read `bandwidth_monitor.{c,h}`.** Document the LAN PHY detection mechanism and the throttle-trigger logic.

- [ ] **Step 3: Port the init TLV stream** as `hl2SendIoBoardTlv(...)` and call it from `applyBoardQuirks` when `m_caps->hasIoBoardHl2`. Each TLV byte cited inline.

- [ ] **Step 4: Port the bandwidth monitor** as an internal `Hl2BandwidthMonitor` helper class. Expose `isThrottled()` and `throttleCount()`. When throttled, pause ep2 sends; when clear, resume. Call `hl2CheckBandwidthMonitor()` from the watchdog tick.

- [ ] **Step 5: Build and smoke-test on HL2 if available.** For the plan executor without HL2, compile-only is acceptable; log the "unverified on hardware" note for the hardware smoke checklist.

- [ ] **Step 6: Full suite regression.**

- [ ] **Step 7: Commit**

```bash
git add src/core/P1RadioConnection.h src/core/P1RadioConnection.cpp
git commit -m "$(cat <<'EOF'
feat(3I): HL2 IoBoardHl2 TLV init + bandwidth monitor subscription

Ports mi0bot IoBoardHl2.cs init TLV stream and sends it before the
first metis-start on HermesLite connections. Adds an internal
Hl2BandwidthMonitor helper ported from ChannelMaster/bandwidth_monitor
that pauses ep2 sends when the LAN PHY throttles and resumes when clear.

Source: mi0bot Hermes-Lite IoBoardHl2.cs, bandwidth_monitor.{c,h}

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 13: P2RadioConnection audit — capability registry + new board dispatch

**Files:**
- Modify: `src/core/P2RadioConnection.h/.cpp`

**Source references:**
- Existing `P2RadioConnection` — read every branch on board type
- `BoardCapabilities.h/.cpp` (Task 2)

- [ ] **Step 1: Grep P2 for hardcoded Saturn assumptions.**

```bash
grep -n "Saturn\|Anan.*G2\|ANAN-G2" src/core/P2RadioConnection.cpp
```

- [ ] **Step 2: Replace hardcoded constants with `m_caps` lookups.** Store a `const BoardCapabilities*` on the class and set it in `connectToRadio`.

- [ ] **Step 3: Add dispatch for SaturnMKII, ANAN_G2_1K model mapping, AnvelinaPro3.** These all use `HPSDRHW::Saturn` or `HPSDRHW::OrionMKII` on the wire, so the dispatch is capability-level not wire-level. Verify in `parseDiscoveryReply` that `HPSDRHW::SaturnMKII` is recognised.

- [ ] **Step 4: Run full suite regression** — Saturn must still work identically.

- [ ] **Step 5: Smoke-test Saturn on hardware** if available.

- [ ] **Step 6: Commit**

```bash
git add src/core/P2RadioConnection.h src/core/P2RadioConnection.cpp
git commit -m "$(cat <<'EOF'
refactor(3I): P2RadioConnection reads BoardCapabilities + SaturnMKII dispatch

Hardcoded Saturn-only assumptions replaced with capability-registry
lookups. Adds SaturnMKII, ANAN_G2_1K, and AnvelinaPro3 recognition
through the existing P2 wire path. No wire format changes — this
is a capability-metadata audit.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 14: ConnectionPanel — radio list widget + Thetis-equivalent UI

**Files:**
- Modify: `src/gui/ConnectionPanel.h/.cpp` — expand from 315 LOC to full UI

**Source references:**
- `~/Thetis/Project Files/Source/Console/HPSDR/ucRadioList.cs` — list widget
- `~/Thetis/Project Files/Source/Console/HPSDR/clsDiscoveredRadioPicker.cs` — pick flow

- [ ] **Step 1: Read `ucRadioList.cs` + `clsDiscoveredRadioPicker.cs` end-to-end.**

- [ ] **Step 2: Rebuild ConnectionPanel layout** — `QTreeView` or `QTableWidget` with the columns listed in §5.2. Status dot rendered as a styled delegate. Right-click context menu for Connect / Disconnect / Forget / Edit IP / Copy MAC.

- [ ] **Step 3: Bottom-strip buttons** — Start/Stop Discovery, Connect, Disconnect, Add Manually, Forget, Close. Wire each to `RadioDiscovery` or the active `RadioConnection`.

- [ ] **Step 4: Color-coding and in-use state** — green/amber/grey/red per §5.2.

- [ ] **Step 5: Build, launch the app, manually verify layout** with live discovery against Saturn (if available).

- [ ] **Step 6: Commit**

```bash
git add src/gui/ConnectionPanel.h src/gui/ConnectionPanel.cpp
git commit -m "$(cat <<'EOF'
feat(3I): ConnectionPanel — Thetis-equivalent radio list UI

Expands the modal from a 315-LOC skeleton into a full ucRadioList
port: sortable columns (status, name, board, protocol, IP, MAC, fw,
in-use), color-coded state, right-click menu, and bottom-strip
buttons matching Thetis layout.

Source: ucRadioList.cs, clsDiscoveredRadioPicker.cs

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 15: ConnectionPanel — saved-radio persistence + test

**Files:**
- Modify: `src/gui/ConnectionPanel.cpp` — AppSettings integration
- Modify: `src/core/AppSettings.h/.cpp` — add helpers for `radios/*` schema
- Create: `tests/tst_connection_panel_saved_radios.cpp`
- Modify: `tests/CMakeLists.txt`

**Source references:** design doc §5.3.

- [ ] **Step 1: Write `tests/tst_connection_panel_saved_radios.cpp`** covering add, remove, edit, MAC-pinning flag, lastConnected, and round-trip read-after-write.

- [ ] **Step 2: Implement `AppSettings::savedRadios()`, `saveRadio(info)`, `forgetRadio(mac)`, `lastConnected()`/`setLastConnected()`, `discoveryProfile()`/`setDiscoveryProfile()`.** Schema per §5.3.

- [ ] **Step 3: Wire ConnectionPanel** to these helpers: on "Save" from discovered row or manual-add, call `saveRadio`; on "Forget", call `forgetRadio`; on successful connect, update `lastSeen` and `firmwareVersion`.

- [ ] **Step 4: Run test — expect PASS. Full suite regression.**

- [ ] **Step 5: Commit**

```bash
git add src/core/AppSettings.h src/core/AppSettings.cpp \
        src/gui/ConnectionPanel.cpp tests/tst_connection_panel_saved_radios.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): saved-radio persistence — AppSettings radios/* schema

Persistent list of named radios keyed by MAC (or synthetic manual key)
with pinToMac, autoConnect, lastSeen, lastConnected, and discoveryProfile
fields. ConnectionPanel reads/writes through AppSettings helpers.
Round-trip test covers add/remove/edit.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 16: ConnectionPanel — manual-add dialog (frmAddCustomRadio port)

**Files:**
- Create: `src/gui/AddCustomRadioDialog.h`
- Create: `src/gui/AddCustomRadioDialog.cpp`
- Modify: `src/gui/ConnectionPanel.cpp` — launch dialog from "Add Manually" button
- Modify: `src/gui/CMakeLists.txt`

**Source references:** `~/Thetis/Project Files/Source/Console/HPSDR/frmAddCustomRadio.cs`

- [ ] **Step 1: Read `frmAddCustomRadio.cs` end-to-end.**

- [ ] **Step 2: Write `AddCustomRadioDialog`** with fields: Name, IP, Port (default 1024), MAC (optional), Board type combo (from `HPSDRHW`), Protocol (P1/P2), and a "Test" button that sends a unicast discovery to the entered IP.

- [ ] **Step 3: Wire the dialog** into ConnectionPanel's "Add Manually" button.

- [ ] **Step 4: Build and manually smoke-test** the dialog launches, fields validate, Test button produces a result (or timeout).

- [ ] **Step 5: Commit**

```bash
git add src/gui/AddCustomRadioDialog.h src/gui/AddCustomRadioDialog.cpp \
        src/gui/ConnectionPanel.cpp src/gui/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): AddCustomRadioDialog — port of Thetis frmAddCustomRadio

Manual-add entry for radios that don't broadcast on the local subnet
(remote-over-VPN setups, statically routed hosts). Name, IP, port,
MAC, board type, protocol fields plus a Test button that unicast-probes
the entered IP before saving.

Source: frmAddCustomRadio.cs

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 17: Auto-reconnect on launch

**Files:**
- Modify: `src/gui/MainWindow.cpp` — on startup, consult AppSettings and attempt reconnect
- Modify: `src/core/RadioModel.cpp` — support silent reconnect mode

**Source references:** design doc §5.4.

- [ ] **Step 1: In `MainWindow::initialize`** (or equivalent), read `radios/lastConnected` and its `autoConnect` flag. If set, call `RadioDiscovery::scan(DiscoveryProfile::Fast)` against the saved IP/MAC.

- [ ] **Step 2: On successful silent reconnect, update the ConnectionPanel state** without popping the modal.

- [ ] **Step 3: Build and manually test** on Saturn:
  1. Connect Saturn, close app.
  2. Relaunch. Expect Saturn reconnects silently.
  3. Disconnect in app, close, relaunch. Expect no auto-reconnect.

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp src/core/RadioModel.cpp
git commit -m "$(cat <<'EOF'
feat(3I): auto-reconnect on launch — silent discovery against lastConnected

On startup, if radios/lastConnected is set and that entry has
autoConnect=true, run Fast discovery against the saved MAC/IP and
reconnect silently. No popup on failure; ConnectionPanel still works.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 18: HardwarePage shell + rename from HardwareSetupPages

**Files:**
- Rename: `src/gui/setup/HardwareSetupPages.h` → `HardwarePage.h`
- Rename: `src/gui/setup/HardwareSetupPages.cpp` → `HardwarePage.cpp`
- Modify: `src/gui/setup/SetupDialog.cpp` — register new Hardware entry
- Modify: `src/gui/setup/CMakeLists.txt`

- [ ] **Step 1: `git mv` the files.**

```bash
git mv src/gui/setup/HardwareSetupPages.h src/gui/setup/HardwarePage.h
git mv src/gui/setup/HardwareSetupPages.cpp src/gui/setup/HardwarePage.cpp
```

- [ ] **Step 2: Rename the class** everywhere: `HardwareSetupPages` → `HardwarePage`. Update includes, forward decls, usage in `SetupDialog`.

- [ ] **Step 3: Replace the content of `HardwarePage.cpp`** with a shell that builds a `QTabWidget` with the 9 sub-tab placeholder widgets (empty for now; filled in Tasks 19–21):

```cpp
HardwarePage::HardwarePage(RadioModel* model, QWidget* parent)
    : SetupPage(parent), m_model(model)
{
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    layout->addWidget(m_tabs);

    m_radioInfoIdx       = m_tabs->addTab(new RadioInfoTab(model, this), tr("Radio Info"));
    m_antennaAlexIdx     = m_tabs->addTab(new AntennaAlexTab(model, this), tr("Antenna / ALEX"));
    m_ocOutputsIdx       = m_tabs->addTab(new OcOutputsTab(model, this), tr("OC Outputs"));
    m_xvtrIdx            = m_tabs->addTab(new XvtrTab(model, this), tr("XVTR"));
    m_pureSignalIdx      = m_tabs->addTab(new PureSignalTab(model, this), tr("PureSignal"));
    m_diversityIdx       = m_tabs->addTab(new DiversityTab(model, this), tr("Diversity"));
    m_paCalIdx           = m_tabs->addTab(new PaCalibrationTab(model, this), tr("PA Calibration"));
    m_hl2IoIdx           = m_tabs->addTab(new Hl2IoBoardTab(model, this), tr("HL2 I/O"));
    m_bwMonitorIdx       = m_tabs->addTab(new BandwidthMonitorTab(model, this), tr("Bandwidth Monitor"));

    connect(model, &RadioModel::currentRadioChanged,
            this, &HardwarePage::onCurrentRadioChanged);
}

void HardwarePage::onCurrentRadioChanged(const RadioInfo& info) {
    const auto& caps = BoardCapsTable::forBoard(info.boardType);
    m_tabs->setTabVisible(m_antennaAlexIdx, caps.hasAlexFilters);
    m_tabs->setTabVisible(m_ocOutputsIdx,   caps.ocOutputCount > 0);
    m_tabs->setTabVisible(m_xvtrIdx,        caps.xvtrJackCount > 0);
    m_tabs->setTabVisible(m_pureSignalIdx,  caps.hasPureSignal);
    m_tabs->setTabVisible(m_diversityIdx,   caps.hasDiversityReceiver);
    m_tabs->setTabVisible(m_paCalIdx,       caps.hasPaProfile);
    m_tabs->setTabVisible(m_hl2IoIdx,       caps.hasIoBoardHl2);
    m_tabs->setTabVisible(m_bwMonitorIdx,   caps.hasBandwidthMonitor);
}
```

- [ ] **Step 4: Create empty tab widget stubs** under `src/gui/setup/hardware/` — one `.h/.cpp` pair per tab from the file structure list. Each is a `QWidget` subclass with a `populate(const RadioInfo&, const BoardCapabilities&)` method that does nothing yet.

- [ ] **Step 5: Register all new files in `src/gui/setup/CMakeLists.txt`.**

- [ ] **Step 6: Build — expect success.** Launch app, open Setup, see the Hardware entry with 9 sub-tabs.

- [ ] **Step 7: Commit**

```bash
git add src/gui/setup/
git commit -m "$(cat <<'EOF'
feat(3I): HardwarePage shell + 9 nested tab stubs

Renames HardwareSetupPages → HardwarePage and rebuilds it as a
QTabWidget container with 9 sub-tabs (Radio Info, Antenna/ALEX, OC,
XVTR, PureSignal, Diversity, PA Cal, HL2 I/O, Bandwidth Monitor).
Tabs are stubs wired to onCurrentRadioChanged for capability gating.
Populate bodies come in Tasks 19–20.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 19: HardwarePage — populate Radio Info, Antenna/ALEX, OC, XVTR tabs

**Files:**
- Modify: `src/gui/setup/hardware/RadioInfoTab.cpp`
- Modify: `src/gui/setup/hardware/AntennaAlexTab.cpp`
- Modify: `src/gui/setup/hardware/OcOutputsTab.cpp`
- Modify: `src/gui/setup/hardware/XvtrTab.cpp`

**Source references:** Thetis `Setup.cs` tabs — grep for section headings like "Ant/Filters", "Open Collector", "XVTR".

- [ ] **Step 1: Read each Thetis tab** and list every control with its name, type, default value, and range.

- [ ] **Step 2: For each tab, build the Qt widget tree:**

- **RadioInfoTab**: read-only labels bound to `RadioModel::currentRadioInfo()` + `BoardCapabilities`; sample-rate combo filtered by `caps.sampleRates`; active receiver count spinbox capped at `caps.maxReceivers`; "Copy support info" button dumps to clipboard via `SupportBundle`.

- **AntennaAlexTab**: per-receiver antenna select combos (enabled count = `caps.antennaInputCount`); 14-band ALEX filter override grid; Alex Bypass RX/TX checkboxes; "disable ALEX TX relays during RX" checkbox.

- **OcOutputsTab**: 14-band × 7-output grid of checkboxes (QCheckBox in a QGridLayout); separate RX and TX mask tables; relay settle delay slider (label "ms", range 0–500).

- **XvtrTab**: QTableWidget with 5 rows, columns: Band name, RF start (Hz), RF end (Hz), LO freq (Hz), offset sign, RX-only flag, power override dB; auto-select checkbox above.

- [ ] **Step 3: Wire each control's `valueChanged`/`toggled`/`editingFinished`** to write through to the per-MAC AppSettings schema (the full schema work comes in Task 21; here just emit `settingChanged` or similar so Task 21 can pick it up).

- [ ] **Step 4: Build, launch the app, open Setup → Hardware, manually verify** all four tabs render and tabs hide/show correctly as the connected radio changes.

- [ ] **Step 5: Commit**

```bash
git add src/gui/setup/hardware/RadioInfoTab.cpp \
        src/gui/setup/hardware/AntennaAlexTab.cpp \
        src/gui/setup/hardware/OcOutputsTab.cpp \
        src/gui/setup/hardware/XvtrTab.cpp
git commit -m "$(cat <<'EOF'
feat(3I): HardwarePage — Radio Info / Antenna·ALEX / OC / XVTR tabs

Ports the four hardware sub-tabs from Thetis Setup.cs that every
ALEX-equipped P1/P2 board cares about. RadioInfoTab is read-only.
AntennaAlexTab gates RX antenna combos by caps.antennaInputCount.
OcOutputsTab exposes the 14-band × 7-output mask grid. XvtrTab
supports up to 5 transverter entries.

Source: Thetis Setup.cs Hardware Config sub-tabs

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 20: HardwarePage — populate PureSignal, Diversity, PA Cal, HL2 I/O, Bandwidth Monitor tabs

**Files:**
- Modify: `src/gui/setup/hardware/PureSignalTab.cpp`
- Modify: `src/gui/setup/hardware/DiversityTab.cpp`
- Modify: `src/gui/setup/hardware/PaCalibrationTab.cpp`
- Modify: `src/gui/setup/hardware/Hl2IoBoardTab.cpp`
- Modify: `src/gui/setup/hardware/BandwidthMonitorTab.cpp`

**Source references:** Thetis `PSForm.cs`, `DiversityForm.cs`, `Setup.cs` "PA Settings" tab, mi0bot `IoBoardHl2.cs`, mi0bot `bandwidth_monitor.h`.

- [ ] **Step 1: Read each source** and list controls.

- [ ] **Step 2: Build widget trees:**

- **PureSignalTab** (cold this phase): enable checkbox, feedback source combo, auto-cal-on-band-change checkbox, preserve-calibration checkbox, RX feedback atten slider. Save state; do not activate DSP.

- **DiversityTab**: enable toggle, phase slider (−180…+180), gain slider (−60…+20 dB), reference ADC combo, "Null signal" button. Only shown when `caps.hasDiversityReceiver`.

- **PaCalibrationTab**: 14-row per-band target power table, per-band gain correction table, PA profile combo (with "ANAN-100D-default", "ANAN-200D-default", "custom"), step-atten cal button (if `caps.hasStepAttenuatorCal`), auto-calibrate button (cold).

- **Hl2IoBoardTab**: "I/O board present" toggle (auto-detected), GPIO bank enable, external PTT input pin, CW key input pin, aux output assignments table.

- **BandwidthMonitorTab**: current LAN PHY rate (read-only live label, updated via signal from `Hl2BandwidthMonitor`), throttle-count label, threshold spinbox (Mbps), auto-pause toggle.

- [ ] **Step 3: Wire controls** to `settingChanged` signals.

- [ ] **Step 4: Manually smoke-test** each tab's widget tree on whichever hardware is available (HL2 for bandwidth monitor; ANAN-100D for diversity).

- [ ] **Step 5: Commit**

```bash
git add src/gui/setup/hardware/PureSignalTab.cpp \
        src/gui/setup/hardware/DiversityTab.cpp \
        src/gui/setup/hardware/PaCalibrationTab.cpp \
        src/gui/setup/hardware/Hl2IoBoardTab.cpp \
        src/gui/setup/hardware/BandwidthMonitorTab.cpp
git commit -m "$(cat <<'EOF'
feat(3I): HardwarePage — PureSignal / Diversity / PA Cal / HL2 / BW Monitor tabs

Remaining 5 hardware sub-tabs. PureSignal and PA Cal are plumbed cold
(state persists, DSP hooks are stubs for the TX phase). Diversity has
a full phase/gain/reference-ADC UI. HL2 I/O Board ports the mi0bot
IoBoardHl2 GPIO/PTT surface. Bandwidth Monitor shows live LAN PHY
rate and a user-adjustable throttle threshold.

Source: PSForm.cs, DiversityForm.cs, Setup.cs PA tab, mi0bot
        IoBoardHl2.cs, bandwidth_monitor.h

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 21: HardwarePage — capability gating + per-MAC persistence + tests

**Files:**
- Modify: `src/core/AppSettings.h/.cpp` — add `loadHardwareSettings(mac)` / `saveHardwareSetting(mac, key, value)`
- Modify: `src/gui/setup/HardwarePage.cpp` — wire `settingChanged` from each tab to AppSettings write-through
- Create: `tests/tst_hardware_page_capability_gating.cpp`
- Create: `tests/tst_hardware_page_persistence.cpp`
- Modify: `tests/CMakeLists.txt`

**Source references:** design doc §4.4, §4.5.

- [ ] **Step 1: Implement AppSettings helpers** for the `hardware/<MAC>/*` schema. Keys are composed as `QStringLiteral("hardware/%1/%2").arg(mac).arg(key)`.

- [ ] **Step 2: Write `tst_hardware_page_capability_gating.cpp`**:

```cpp
#include <QtTest/QtTest>
#include "gui/setup/HardwarePage.h"
#include "core/HpsdrModel.h"
#include "core/BoardCapabilities.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestHardwarePageGating : public QObject {
    Q_OBJECT
private:
    struct Expect {
        HPSDRHW board;
        bool antennaAlex, oc, xvtr, ps, diversity, pa, hl2io, bwMon;
    };
private slots:
    void tabsGatedByCapabilities_data() {
        QTest::addColumn<int>("boardInt");
        QTest::addColumn<bool>("antennaAlex");
        QTest::addColumn<bool>("oc");
        QTest::addColumn<bool>("xvtr");
        QTest::addColumn<bool>("ps");
        QTest::addColumn<bool>("diversity");
        QTest::addColumn<bool>("pa");
        QTest::addColumn<bool>("hl2io");
        QTest::addColumn<bool>("bwMon");

        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite)
            << false << false << false << false << false << false << true << true;
        QTest::newRow("Angelia")    << int(HPSDRHW::Angelia)
            << true  << true  << true  << true  << true  << true  << false << false;
        QTest::newRow("Atlas")      << int(HPSDRHW::Atlas)
            << false << false << false << false << false << false << false << false;
        QTest::newRow("Saturn")     << int(HPSDRHW::Saturn)
            << true  << true  << true  << true  << true  << true  << false << false;
    }
    void tabsGatedByCapabilities() {
        QFETCH(int, boardInt);
        QFETCH(bool, antennaAlex);
        QFETCH(bool, oc);
        QFETCH(bool, xvtr);
        QFETCH(bool, ps);
        QFETCH(bool, diversity);
        QFETCH(bool, pa);
        QFETCH(bool, hl2io);
        QFETCH(bool, bwMon);

        RadioModel model;
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType = static_cast<HPSDRHW>(boardInt);
        info.protocol = BoardCapsTable::forBoard(info.boardType).protocol;
        page.onCurrentRadioChanged(info);

        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::AntennaAlex), antennaAlex);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::OcOutputs), oc);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::Xvtr), xvtr);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::PureSignal), ps);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::Diversity), diversity);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::PaCal), pa);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::Hl2IoBoard), hl2io);
        QCOMPARE(page.isTabVisibleForTest(HardwarePage::Tab::BandwidthMonitor), bwMon);
    }
};

QTEST_MAIN(TestHardwarePageGating)
#include "tst_hardware_page_capability_gating.moc"
```

- [ ] **Step 3: Write `tst_hardware_page_persistence.cpp`** covering write, read, round-trip across all 9 tabs, and the radio-swap persistence case (set values with HL2, swap to Angelia, swap back, verify HL2 values intact).

- [ ] **Step 4: Implement `HardwarePage::isTabVisibleForTest`** as a test hook.

- [ ] **Step 5: Run both tests — expect PASS.** Full suite regression.

- [ ] **Step 6: Commit**

```bash
git add src/core/AppSettings.h src/core/AppSettings.cpp \
        src/gui/setup/HardwarePage.h src/gui/setup/HardwarePage.cpp \
        tests/tst_hardware_page_capability_gating.cpp \
        tests/tst_hardware_page_persistence.cpp tests/CMakeLists.txt
git commit -m "$(cat <<'EOF'
feat(3I): HardwarePage capability gating + per-MAC persistence

Each sub-tab's settingChanged signal writes through to AppSettings
under hardware/<MAC>/<key>. Tests cover gating correctness per board
(data-driven over HPSDRHW) and persistence round-trip plus radio-swap
state preservation.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 22: Design-doc corrections + CHANGELOG

**Files:**
- Modify: `docs/architecture/radio-abstraction.md` — already updated in Task 3, double-check against final `BoardCapabilities` values
- Modify: `CHANGELOG.md` — add Phase 3I entry
- Modify: `README.md` — update supported radios list if it has one
- Modify: `CLAUDE.md` — flip "Phase 3H current" → "Phase 3I current" if relevant

- [ ] **Step 1: Re-check `radio-abstraction.md`** against the final `BoardCapabilities.cpp`. Fix any drift.

- [ ] **Step 2: Write CHANGELOG entry.**

```markdown
## Phase 3I — Radio Connector & Radio-Model Port (2026-04-XX)

### Added
- Full Protocol 1 support for Atlas/Metis, Hermes (ANAN-10/100), HermesII
  (ANAN-10E/100B), Angelia (ANAN-100D), Orion (ANAN-200D), and Hermes Lite 2.
- `BoardCapabilities` constexpr registry driving both protocol families
  and the Hardware setup UI.
- HardwarePage with 9 capability-gated sub-tabs (Radio Info, Antenna/ALEX,
  OC Outputs, XVTR, PureSignal, Diversity, PA Calibration, HL2 I/O Board,
  Bandwidth Monitor).
- ConnectionPanel expanded with Thetis-equivalent radio list, saved-radio
  persistence, manual-add dialog, and auto-reconnect on launch.
- RadioDiscovery ported to mi0bot clsRadioDiscovery model with six tunable
  timing profiles (UltraFast → VeryTolerant).
- `HPSDRModel` and `HPSDRHW` enums ported 1:1 from mi0bot/Thetis enums.cs,
  preserving integer values including the 7..9 reserved gap.

### Changed
- P2RadioConnection audited against BoardCapabilities; recognises
  SaturnMKII, ANAN-G2-1K, and AnvelinaPro3.
- Legacy `BoardType` enum removed; callers migrate to `HPSDRHW`.

### Fixed
- `BoardType::Griffin=2` was a docs-era mistake; corrected to
  `HPSDRHW::HermesII=2` matching mi0bot enums.cs:392.

### Deferred (future phases)
- TX IQ producer (plumbed cold — commands go on the wire, IQ source silent).
- PureSignal feedback DSP (toggle persists, feedback loop deferred).
- TCI protocol.
- RedPitaya board.
- Sidetone generator.
- Firmware flashing.
```

- [ ] **Step 3: Commit**

```bash
git add docs/architecture/radio-abstraction.md CHANGELOG.md README.md CLAUDE.md
git commit -m "$(cat <<'EOF'
docs(3I): CHANGELOG + radio-abstraction corrections

CHANGELOG entry for Phase 3I. Double-checks radio-abstraction.md
against the BoardCapabilities registry as final source of truth.

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 23: Hardware smoke checklist + Phase 3I handoff doc

**Files:**
- Create: `docs/architecture/phase3i-verification.md`

- [ ] **Step 1: Run the hardware smoke checklist** from design doc §7.3 against every radio physically available. Record results per radio:

```markdown
# Phase 3I — Verification Matrix

## Hardware tested
### HL2 (serial 123, fw 72)
- [x] Discovers on Wi-Fi NIC
- [x] Connects, tunes 14.200 LSB, audio confirmed
- [x] Atten 0→30 dB drops noise ~30 dB
- [x] Bandwidth monitor throttles cleanly under saturated LAN

### ANAN-200D (if available)
- [ ] ...

## Hardware NOT tested (unverified in 3I)
- ANAN-100D — not on hand
- ANAN-7000DLE — not on hand
- AnvelinaPro3 — not on hand

## Known issues found during smoke test
- None / list them here
```

- [ ] **Step 2: Write the handoff doc** including:
  1. Summary of what shipped.
  2. Verified vs unverified hardware table.
  3. Known issues and follow-up tickets.
  4. Outstanding deferred items from §9 of the design doc (TX IQ producer, PureSignal DSP, TCI, RedPitaya, sidetone, firmware updater).
  5. Migration notes for users upgrading from Phase 3G-8.

- [ ] **Step 3: Commit**

```bash
git add docs/architecture/phase3i-verification.md
git commit -m "$(cat <<'EOF'
docs(3I): verification matrix + handoff doc

Records which radios were smoke-tested on real hardware during Phase 3I
implementation, known issues found, and the deferred work that rolls
into later phases (TX IQ, PureSignal DSP, TCI, RedPitaya, firmware
flashing).

Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Self-Review (executed against the spec)

**Spec coverage check:**

| Spec section | Task(s) |
|---|---|
| §1 Architecture overview | Tasks 1–21 collectively |
| §1.1 Success criterion | Task 9 (loopback) + Task 23 (hardware smoke) |
| §2.1 HPSDR enums | Task 1 |
| §2.2 BoardCapabilities struct | Task 2 |
| §2.3 Sample entry HL2 | Task 2 |
| §2.4 Unit test invariants | Task 2 |
| §3.1 Source basis | enforced across Tasks 6–12 |
| §3.2 Class shape | Task 6 |
| §3.3 RX data path | Tasks 8 + 9 |
| §3.4 TX plumbed-cold | Task 7 (compose) + no producer (deferred §9) |
| §3.5 Per-board quirks | Tasks 11 + 12 |
| §3.6 Reconnect state machine | Task 10 |
| §4.1 Container structure | Task 18 |
| §4.2 File layout | Task 18 |
| §4.3 Per-tab control lists | Tasks 19 + 20 |
| §4.4 Capability reconciliation | Task 18 + Task 21 tests |
| §4.5 Persistence schema | Task 21 |
| §5.1 Discovery port | Task 4 |
| §5.2 ConnectionPanel expansion | Tasks 14 + 16 |
| §5.3 Saved-radio persistence | Task 15 |
| §5.4 Auto-reconnect on launch | Task 17 |
| §6.1 Error taxonomy | implicit across Tasks 10, 11 (partial) — **add explicit enum in Task 11** |
| §6.2 Reconnect state machine | Task 10 |
| §7.1 Unit tests | Tasks 1, 2, 5, 7, 8, 11, 15, 21 |
| §7.2 Integration tests | Tasks 9, 10 |
| §7.3 Hardware smoke checklist | Task 23 |
| §8 Migration | Task 3 |
| §9 Out-of-scope | enforced across all tasks |
| §10 Risks | mitigation strategies embedded in test plans |

**Gap found — §6.1 error taxonomy.** The design doc enumerates 9 error classes but this plan only implicitly covers them. Fix: add to Task 11 a sub-step that defines an `enum class RadioConnectionError` per §6.1 and wires each failure path through it. Making this an inline fix to Task 11.

**Placeholder scan:** no `TBD`, `TODO`, `implement later`, or "similar to Task N" in any task body.

**Type consistency check:** `HPSDRHW`, `HPSDRModel`, `BoardCapabilities`, `RadioInfo`, `ConnectionState`, `DiscoveryProfile`, `DiscoveryTiming` all used consistently. `P1RadioConnection::composeCcBank*` names match between Tasks 7 and 11. `BoardCapsTable::forBoard` / `forModel` / `all` match between Task 2 and Tasks 11, 13, 18, 21.

**Scope check:** 23 tasks land in dependency order. Each task compiles, tests pass, and is independently committable. No task depends on a later task's output.

---

## Inline Fix — Task 11 error enum

Before the existing steps in Task 11, insert:

- [ ] **Step 0: Add `RadioConnectionError` enum** to `src/core/RadioConnection.h` per design doc §6.1:

```cpp
enum class RadioConnectionError {
    None,
    DiscoveryNicFailure,
    DiscoveryAllFailed,
    RadioInUse,
    FirmwareTooOld,
    FirmwareStale,
    SocketBindFailure,
    NoDataTimeout,
    UnknownBoardType,
    ProtocolMismatch
};
```

Extend `errorOccurred` signal to carry an error code:

```cpp
signals:
    void errorOccurred(NereusSDR::RadioConnectionError code, const QString& message);
```

Update every existing call site (including `P2RadioConnection`) to pass `RadioConnectionError::None` or an appropriate code. This becomes a ~10-line change across several files but keeps error handling programmatic.

---

## Execution Handoff

Plan complete and saved to `docs/architecture/phase3i-radio-connector-port-plan.md`. Two execution options:

**1. Subagent-Driven (recommended)** — I dispatch a fresh subagent per task, review between tasks, fast iteration. Good match for a 23-task plan with source-first discipline per task.

**2. Inline Execution** — Execute tasks in this session using `superpowers:executing-plans`, batch execution with checkpoints for review.

Which approach?
