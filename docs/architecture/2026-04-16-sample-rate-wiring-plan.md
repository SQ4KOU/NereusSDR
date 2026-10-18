# Sample Rate Wiring — Implementation Plan (PR #35 / Plan C Phase 1)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire `RadioInfoTab`'s RX1 sample-rate combo and active-RX-count spinbox into the actual connection path, so user selections on `Hardware Config → Radio Info` take effect on the next connect. Fix the `activeRxCount` emit bug. Remove the `isP1 ? 192000 : 768000` hardcode from `RadioModel`.

**Architecture:** New `SampleRateCatalog` module provides the Thetis-parity rate list and default. Two pure resolver functions (`resolveSampleRate`, `resolveActiveRxCount`) pull persisted values from AppSettings, validate against board capabilities, and fall back to defaults — they are unit-testable without spinning up the full radio stack. `RadioModel::connectToRadio` calls these resolvers in place of the hardcoded `isP1 ? … : …` branches. `RadioInfoTab` gains a disabled RX2 combo stub (Phase 3F placeholder), fixes the `activeRxCount` emit bug, and shows an inline "reconnect to apply" banner when the persisted value differs from the live wire rate.

**Tech Stack:** C++20, Qt6 (Widgets, Test), CMake, Ninja, existing AppSettings XML store.

**Spec:** `docs/architecture/2026-04-16-sample-rate-wiring-design.md`

---

## File structure

### Created

- `src/core/SampleRateCatalog.h` — rate-list and resolver declarations
- `src/core/SampleRateCatalog.cpp` — implementations
- `tests/tst_sample_rate_catalog.cpp` — unit tests for catalog + resolvers

### Modified

- `src/CMakeLists.txt` — register `SampleRateCatalog.{h,cpp}` as app sources
- `src/gui/setup/hardware/RadioInfoTab.h` — add RX2 combo member, banner member, `activeRxCount` slot declaration, `onWireSampleRateChanged` slot
- `src/gui/setup/hardware/RadioInfoTab.cpp` — build RX2 combo (disabled stub), wire `activeRxCount` `valueChanged` signal, add inline reconnect banner, subscribe to `RadioModel::wireSampleRateChanged`, populate RX1 combo from `allowedSampleRates`
- `src/models/RadioModel.cpp` — call resolvers in `connectToRadio`, drop protocol hardcodes, compute `wdspInSize` via `64 * rate / 48000`
- `tests/tst_hardware_page_persistence.cpp` — add `activeRxCount` round-trip case
- `tests/CMakeLists.txt` — register `tst_sample_rate_catalog`
- `CLAUDE.md` — correct the "sample rate is radio-authoritative" rule
- `CHANGELOG.md` — Unreleased entry

### Not touched

- `src/core/AppSettings.{h,cpp}` — no changes; existing per-MAC API is sufficient. (Spec earlier proposed a `hardwareSettingChanged` signal; dropped during implementation-planning because the banner can self-update locally + via `RadioModel::wireSampleRateChanged`.)

---

## Prereqs / environment

Build directory lives at `build/`. Standard build:

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -DNEREUS_BUILD_TESTS=ON
cmake --build build -j
```

Run a single test (Windows bash, forward slashes):

```bash
./build/tests/tst_sample_rate_catalog.exe
```

Or the full suite via CTest:

```bash
ctest --test-dir build --output-on-failure
```

All commits must be GPG-signed (user memory: `feedback_gpg_sign_commits.md`).
Signature footer on every commit is `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` (per user memory `feedback_github_signature.md`).

Assume the feature branch is already created. Name: `feature/sample-rate-wiring`.

---

## Task 1: SampleRateCatalog — header + constants

**Files:**
- Create: `src/core/SampleRateCatalog.h`

- [ ] **Step 1: Write the header**

```cpp
// src/core/SampleRateCatalog.h
//
// Sample-rate catalog and resolvers for Hardware Config.
//
// Source: Thetis Project Files/Source/Console/setup.cs:847-852 (rate lists),
//         setup.cs:866 (default selection), ChannelMaster/cmsetup.c:104-111
//         (buffer-size formula — getbuffsize).

#pragma once

#include "BoardCapabilities.h"
#include "HpsdrModel.h"
#include "RadioDiscovery.h" // ProtocolVersion

#include <vector>

class QVariant;
class QString;

namespace NereusSDR {

class AppSettings;

// Thetis-source constants.
//
// From setup.cs:849 — P1 base list (every non-RedPitaya board).
inline constexpr int kP1RatesBase[] = {48000, 96000, 192000};

// From setup.cs:847-849 — P1 list when include_extra_p1_rate is true
// (only HPSDRModel::REDPITAYA).
inline constexpr int kP1RatesRedPitaya[] = {48000, 96000, 192000, 384000};

// From setup.cs:850 — P2 list, every ETH board.
inline constexpr int kP2Rates[] = {48000, 96000, 192000, 384000, 768000, 1536000};

// From setup.cs:866 — default selected rate when nothing is persisted.
inline constexpr int kDefaultSampleRate = 192000;

// From ChannelMaster/cmsetup.c:108-110 — base constants for getbuffsize.
// buffer_size = kBufferBaseSize * rate / kBufferBaseRate.
inline constexpr int kBufferBaseRate = 48000;
inline constexpr int kBufferBaseSize = 64;

// Return the allowed sample-rate list for a given protocol + board + model.
// Intersects the protocol-appropriate master list with caps.sampleRates
// (skipping zero sentinels). HPSDRModel distinguishes RedPitaya (which
// shares HPSDRHW::OrionMKII with real OrionMKII but gets the extra 384k
// on P1, per setup.cs:847).
std::vector<int> allowedSampleRates(ProtocolVersion proto,
                                     const BoardCapabilities& caps,
                                     HPSDRModel model);

// Return the default rate: kDefaultSampleRate if present in allowed,
// otherwise the first allowed entry (paranoia fallback).
int defaultSampleRate(ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model);

// Compute WDSP in_size for a given rate. From cmsetup.c:104-111.
// Precondition: rate > 0 (callers must validate).
constexpr int bufferSizeForRate(int rate) noexcept
{
    return kBufferBaseSize * rate / kBufferBaseRate;
}

// Read the persisted sample rate for the given MAC, validate it against
// allowedSampleRates, and return a valid rate. If the persisted value is
// missing, zero, or not in the allowed list, returns defaultSampleRate
// and logs a warning via qCWarning(lcConnection) for the not-in-list case.
int resolveSampleRate(const AppSettings& settings,
                      const QString& mac,
                      ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model);

// Read the persisted active-RX count for the given MAC, clamp to
// [1, caps.maxReceivers]. If the persisted value is missing or < 1,
// returns 1.
int resolveActiveRxCount(const AppSettings& settings,
                         const QString& mac,
                         const BoardCapabilities& caps);

} // namespace NereusSDR
```

- [ ] **Step 2: Commit**

```bash
git add src/core/SampleRateCatalog.h
git commit -S -m "$(cat <<'EOF'
feat(sample-rate): add SampleRateCatalog header with Thetis-cited constants

Declares kP1RatesBase/kP1RatesRedPitaya/kP2Rates/kDefaultSampleRate
sourced from setup.cs:847-866, plus bufferSizeForRate() from
cmsetup.c:104-111. Also declares allowedSampleRates / defaultSampleRate
/ resolveSampleRate / resolveActiveRxCount. Implementation in
follow-up commit.

Part of PR #35 / Plan C Phase 1 sample-rate refactor.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: SampleRateCatalog — tests first (TDD)

**Files:**
- Create: `tests/tst_sample_rate_catalog.cpp`

- [ ] **Step 1: Write the failing test**

```cpp
// tests/tst_sample_rate_catalog.cpp
//
// Unit tests for SampleRateCatalog rate lists and resolvers.

#include <QtTest/QtTest>
#include <QTemporaryDir>

#include "core/SampleRateCatalog.h"
#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"
#include "core/RadioDiscovery.h" // ProtocolVersion

using namespace NereusSDR;

class TestSampleRateCatalog : public QObject {
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
    }

    // ── allowedSampleRates ────────────────────────────────────────────────────

    void p1_hermes_allows_48_96_192()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMES);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000}));
    }

    void p2_anan_g2_allows_all_six_p2_rates()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000, 384000, 768000, 1536000}));
    }

    void p1_redpitaya_gets_extra_384()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::REDPITAYA);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::REDPITAYA);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000, 384000}));
    }

    void p1_orionmkii_does_not_get_extra_384()
    {
        // OrionMKII and RedPitaya share HPSDRHW::OrionMKII but only
        // REDPITAYA gets the extra 384k on P1 (setup.cs:847 flag).
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ORIONMKII);
        const auto got   = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::ORIONMKII);
        QCOMPARE(got, std::vector<int>({48000, 96000, 192000}));
    }

    void caps_sample_rates_intersect_with_master_list()
    {
        // Hand-built caps with only 48k and 192k populated; should drop
        // 96k even though the master P1 list has it.
        BoardCapabilities caps{};
        caps.sampleRates  = {48000, 192000, 0, 0};
        caps.maxReceivers = 1;
        caps.maxSampleRate = 192000;
        const auto got = allowedSampleRates(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, std::vector<int>({48000, 192000}));
    }

    void empty_caps_sample_rates_returns_empty()
    {
        BoardCapabilities caps{};
        caps.sampleRates = {0, 0, 0, 0};
        const auto got = allowedSampleRates(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2);
        QVERIFY(got.empty());
    }

    // ── defaultSampleRate ─────────────────────────────────────────────────────

    void default_is_192k_when_present()
    {
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(defaultSampleRate(ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2), 192000);
    }

    void default_falls_back_to_first_when_192k_missing()
    {
        BoardCapabilities caps{};
        caps.sampleRates = {48000, 96000, 0, 0};
        const auto got = defaultSampleRate(ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES);
        QCOMPARE(got, 48000);
    }

    // ── bufferSizeForRate ─────────────────────────────────────────────────────

    void buffer_size_matches_thetis_formula()
    {
        // cmsetup.c:104-111 — base_size * rate / base_rate, base_size=64, base_rate=48000.
        QCOMPARE(bufferSizeForRate(48000),   64);
        QCOMPARE(bufferSizeForRate(96000),   128);
        QCOMPARE(bufferSizeForRate(192000),  256);
        QCOMPARE(bufferSizeForRate(384000),  512);
        QCOMPARE(bufferSizeForRate(768000),  1024);
        QCOMPARE(bufferSizeForRate(1536000), 2048);
    }

    // ── resolveSampleRate ─────────────────────────────────────────────────────

    void resolve_returns_persisted_when_valid()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r1.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"), 384000);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveSampleRate(s, mac, ProtocolVersion::Protocol2, caps, HPSDRModel::ANAN_G2), 384000);
    }

    void resolve_falls_back_to_default_when_unset()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r2.xml")));
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMES);
        QCOMPARE(resolveSampleRate(s, QStringLiteral("aa:bb:cc:11:22:33"),
                                    ProtocolVersion::Protocol1, caps, HPSDRModel::HERMES),
                 192000);
    }

    void resolve_falls_back_when_persisted_not_in_allowed()
    {
        // User persisted 1.536M for ANAN-G2, then plugs in an HL2 (caps max 192k).
        AppSettings s(m_dir.filePath(QStringLiteral("r3.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/sampleRate"), 1536000);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMESLITE);
        QCOMPARE(resolveSampleRate(s, mac, ProtocolVersion::Protocol1, caps, HPSDRModel::HERMESLITE),
                 192000);
    }

    // ── resolveActiveRxCount ──────────────────────────────────────────────────

    void resolve_rx_count_returns_persisted_when_in_range()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r4.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 3);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveActiveRxCount(s, mac, caps), 3);
    }

    void resolve_rx_count_clamps_to_max_receivers()
    {
        // User persisted 7 while on G2, then swapped to HL2 (max 2).
        AppSettings s(m_dir.filePath(QStringLiteral("r5.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 7);
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::HERMESLITE);
        QCOMPARE(resolveActiveRxCount(s, mac, caps), caps.maxReceivers);
    }

    void resolve_rx_count_defaults_to_1_when_unset()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("r6.xml")));
        const auto& caps = BoardCapsTable::forModel(HPSDRModel::ANAN_G2);
        QCOMPARE(resolveActiveRxCount(s, QStringLiteral("aa:bb:cc:11:22:33"), caps), 1);
    }
};

QTEST_MAIN(TestSampleRateCatalog)
#include "tst_sample_rate_catalog.moc"
```

- [ ] **Step 2: Register the test**

Edit `tests/CMakeLists.txt`. Find the block around line 70 where `tst_hardware_page_persistence` is registered and add immediately after:

```cmake
# SampleRateCatalog — PR #35
nereus_add_test(tst_sample_rate_catalog)
```

- [ ] **Step 3: Verify the test fails to link (SampleRateCatalog.cpp doesn't exist yet)**

```bash
cmake --build build --target tst_sample_rate_catalog 2>&1 | tail -20
```

Expected: undefined reference to `NereusSDR::allowedSampleRates(...)`, `NereusSDR::defaultSampleRate(...)`, `NereusSDR::resolveSampleRate(...)`, `NereusSDR::resolveActiveRxCount(...)`.

(If instead the tests build but ALL pass, something is wrong — stop and investigate.)

- [ ] **Step 4: Commit the failing test**

```bash
git add tests/tst_sample_rate_catalog.cpp tests/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
test(sample-rate): failing tests for SampleRateCatalog resolvers

Covers allowedSampleRates (P1/P2, RedPitaya extra, caps intersection,
empty caps), defaultSampleRate fallback chain, bufferSizeForRate
verified against cmsetup.c:104-111, resolveSampleRate (valid/unset/
stale), resolveActiveRxCount (in-range/clamp/default).

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: SampleRateCatalog — implementation

**Files:**
- Create: `src/core/SampleRateCatalog.cpp`
- Modify: `src/CMakeLists.txt` — add the new source

- [ ] **Step 1: Write the implementation**

```cpp
// src/core/SampleRateCatalog.cpp
//
// See header for Thetis source citations.

#include "SampleRateCatalog.h"

#include "AppSettings.h"

#include <QLoggingCategory>
#include <QString>
#include <QVariant>

#include <algorithm>
#include <array>
#include <span>

Q_DECLARE_LOGGING_CATEGORY(lcConnection)

namespace NereusSDR {

namespace {

std::span<const int> masterListFor(ProtocolVersion proto, HPSDRModel model) noexcept
{
    if (proto == ProtocolVersion::Protocol1) {
        if (model == HPSDRModel::REDPITAYA) {
            return {kP1RatesRedPitaya, std::size(kP1RatesRedPitaya)};
        }
        return {kP1RatesBase, std::size(kP1RatesBase)};
    }
    // Protocol 2 — every ETH board gets the full list.
    return {kP2Rates, std::size(kP2Rates)};
}

} // namespace

std::vector<int> allowedSampleRates(ProtocolVersion proto,
                                     const BoardCapabilities& caps,
                                     HPSDRModel model)
{
    const auto master = masterListFor(proto, model);
    std::vector<int> out;
    out.reserve(master.size());
    for (int rate : master) {
        // Skip zero-sentinel slots in caps.sampleRates, include only rates
        // the board actually supports.
        const bool supported = std::any_of(caps.sampleRates.begin(),
                                            caps.sampleRates.end(),
                                            [rate](int r) { return r == rate; });
        if (supported) {
            out.push_back(rate);
        }
    }
    return out;
}

int defaultSampleRate(ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model)
{
    const auto allowed = allowedSampleRates(proto, caps, model);
    if (allowed.empty()) {
        // Board registry is broken — caller should never hit this, but
        // returning 0 is safer than reading past end. Log and let the
        // caller decide.
        qCWarning(lcConnection) << "defaultSampleRate: no allowed rates for"
                                 << static_cast<int>(proto) << static_cast<int>(model);
        return 0;
    }
    if (std::find(allowed.begin(), allowed.end(), kDefaultSampleRate) != allowed.end()) {
        return kDefaultSampleRate;
    }
    return allowed.front();
}

int resolveSampleRate(const AppSettings& settings,
                      const QString& mac,
                      ProtocolVersion proto,
                      const BoardCapabilities& caps,
                      HPSDRModel model)
{
    const int persisted = settings.hardwareValue(
        mac, QStringLiteral("radioInfo/sampleRate")).toInt();
    if (persisted <= 0) {
        return defaultSampleRate(proto, caps, model);
    }
    const auto allowed = allowedSampleRates(proto, caps, model);
    if (std::find(allowed.begin(), allowed.end(), persisted) == allowed.end()) {
        qCWarning(lcConnection) << "Persisted sample rate" << persisted
                                 << "not valid for" << mac
                                 << "— falling back to default";
        return defaultSampleRate(proto, caps, model);
    }
    return persisted;
}

int resolveActiveRxCount(const AppSettings& settings,
                         const QString& mac,
                         const BoardCapabilities& caps)
{
    const int persisted = settings.hardwareValue(
        mac, QStringLiteral("radioInfo/activeRxCount")).toInt();
    if (persisted < 1) {
        return 1;
    }
    return std::min(persisted, caps.maxReceivers);
}

} // namespace NereusSDR
```

- [ ] **Step 2: Register the source in CMake**

Find the source list in `src/CMakeLists.txt` (grep for `BoardCapabilities.cpp` to locate the relevant list) and add alongside the other `src/core/` entries:

```cmake
    core/SampleRateCatalog.cpp
    core/SampleRateCatalog.h
```

- [ ] **Step 3: Build and run the tests**

```bash
cmake --build build -j && ./build/tests/tst_sample_rate_catalog.exe
```

Expected: all 14 test cases pass.

- [ ] **Step 4: Commit**

```bash
git add src/core/SampleRateCatalog.cpp src/CMakeLists.txt
git commit -S -m "$(cat <<'EOF'
feat(sample-rate): implement SampleRateCatalog resolvers

allowedSampleRates intersects the Thetis protocol-appropriate master
list (setup.cs:847-852) with caps.sampleRates. defaultSampleRate
returns 192000 (setup.cs:866) if present, else first allowed.
resolveSampleRate validates against allowed and falls back with a
qCWarning. resolveActiveRxCount clamps to caps.maxReceivers.

All 14 unit tests in tst_sample_rate_catalog pass.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: RadioModel — read resolvers on connect

**Files:**
- Modify: `src/models/RadioModel.cpp` — replace the hardcoded `wdspInputRate`/`wdspInSize` lines around lines 202-204

- [ ] **Step 1: Inspect the current connect path**

Read `src/models/RadioModel.cpp:170-340` to locate the code block:

```cpp
    const bool isP1 = (info.protocol == ProtocolVersion::Protocol1);
    const int wdspInputRate = isP1 ? 192000 : 768000;
    const int wdspInSize    = isP1 ? 256    : 1024;
```

Also note that `m_hardwareProfile` is already set (used in other branches of this function) and has both `caps` and `model` fields. Verify the exact field names by reading `src/core/HardwareProfile.h`.

- [ ] **Step 2: Add the include**

At the top of `src/models/RadioModel.cpp`, near the existing `#include "core/..."` cluster, add:

```cpp
#include "core/SampleRateCatalog.h"
```

- [ ] **Step 3: Replace the hardcoded rate block**

Locate the three-line block from Step 1 and replace with:

```cpp
    // Sample rate + active RX count come from Hardware Config (per-MAC).
    // Falls back to Thetis default (192000) when nothing is persisted, and
    // to the board-cap first-entry if 192000 isn't in the allowed list.
    // From setup.cs:847-852 (rate list) + setup.cs:866 (default) +
    // ChannelMaster/cmsetup.c:104-111 (buffer-size formula).
    const auto& caps = *m_hardwareProfile.caps;
    const auto model = m_hardwareProfile.model;
    const int wdspInputRate = resolveSampleRate(
        AppSettings::instance(), info.macAddress, info.protocol, caps, model);
    const int wdspInSize = bufferSizeForRate(wdspInputRate);
    const int activeRxCount = resolveActiveRxCount(
        AppSettings::instance(), info.macAddress, caps);
    qCInfo(lcConnection) << "Connecting with sampleRate=" << wdspInputRate
                          << "inSize=" << wdspInSize
                          << "activeRxCount=" << activeRxCount;
```

(If `#include "core/AppSettings.h"` is not already in the file, add it near the other core includes.)

- [ ] **Step 4: Use activeRxCount when activating the receiver**

Find the block around line 193:

```cpp
    m_receiverManager->activateReceiver(rxIdx);
```

This activates one receiver. For `activeRxCount > 1`, Phase 3F will iterate and activate additional receivers. For PR #35, we only have one WDSP channel so we don't loop. But we MUST pass `activeRxCount` to the connection so protocol 1 sends the correct nrx bits. Locate the existing `setSampleRate` invocation (line 316-319):

```cpp
    QMetaObject::invokeMethod(m_connection, [conn = m_connection, wireSampleRate]() {
        conn->setSampleRate(wireSampleRate);
    });
```

Immediately after this block add:

```cpp
    QMetaObject::invokeMethod(m_connection, [conn = m_connection, activeRxCount]() {
        conn->setActiveReceiverCount(activeRxCount);
    });
```

(Verify `setActiveReceiverCount` exists on `RadioConnection` or its derived classes by grepping `src/core/`. If only P1 has it, guard with the P1 check that already exists elsewhere. A quick grep: `grep -n "setActiveReceiverCount" src/core/*.h`. If it's on the base class, unconditionally call it.)

- [ ] **Step 5: Build**

```bash
cmake --build build -j 2>&1 | tail -30
```

Expected: clean build.

- [ ] **Step 6: Verify existing tests still pass**

```bash
ctest --test-dir build --output-on-failure 2>&1 | tail -30
```

Expected: all existing tests still pass. This is the regression-safety gate — the wdspInputRate value for a default unset AppSettings should still be 192000 for P1 (identical to the prior hardcode) and 768000 for P2 was wrong but now becomes 192000 (intentional behavior change; may break any test that asserted 768000 for P2 — if so, update the test to match the new spec default. Do NOT change the default; the spec says 192000).

- [ ] **Step 7: Commit**

```bash
git add src/models/RadioModel.cpp
git commit -S -m "$(cat <<'EOF'
feat(sample-rate): RadioModel reads persisted rate + RX count on connect

Replaces the isP1 ? 192000 : 768000 hardcode with resolveSampleRate()
/ resolveActiveRxCount() from SampleRateCatalog. wdspInSize now follows
the Thetis formula (64 * rate / 48000, cmsetup.c:104-111) instead of
a protocol-gated constant.

Pushes activeRxCount to RadioConnection::setActiveReceiverCount so
protocol 1 encodes the correct nrx bits.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: RadioInfoTab — populate from allowedSampleRates + RX2 stub combo

**Files:**
- Modify: `src/gui/setup/hardware/RadioInfoTab.h` — add `m_sampleRateRx2Combo` member
- Modify: `src/gui/setup/hardware/RadioInfoTab.cpp` — build stub combo, use `allowedSampleRates`

- [ ] **Step 1: Add the RX2 combo member to the header**

In `src/gui/setup/hardware/RadioInfoTab.h`, inside the `private:` block, add after the existing `m_sampleRateCombo` member:

```cpp
    QComboBox*   m_sampleRateRx2Combo{nullptr};   // disabled in PR #35; activates with Phase 3F multi-panadapter.
```

Also rename the existing `m_sampleRateCombo` to `m_sampleRateRx1Combo` throughout `.h` and `.cpp` for clarity (two combos now present). Use `git grep -l m_sampleRateCombo src/gui/setup/hardware/RadioInfoTab` to find the four call sites.

- [ ] **Step 2: Update RadioInfoTab.cpp ctor**

In the ctor, find the block starting `// Sample rate combo — entries populated in populate()` (around line 66) and replace with:

```cpp
    // RX1 sample rate combo — entries populated in populate() from
    // allowedSampleRates(proto, caps, model). Matches Thetis setup.cs:847-852.
    m_sampleRateRx1Combo = new QComboBox(paramGroup);
    m_sampleRateRx1Combo->setMinimumWidth(120);

    // RX2 sample rate combo — disabled stub in PR #35. Thetis exposes an
    // independent RX2 rate (setup.cs comboAudioSampleRateRX2). When Phase 3F
    // multi-panadapter lands, this combo becomes live with these gating rules
    // (from setup.cs:7065-7073 and 7155-7156):
    //   • P1 (all boards): RX2 forced equal to RX1, combo disabled.
    //   • P2 ANAN-10E / ANAN-100B: RX2 forced equal to RX1 (single-ADC).
    //   • P2 other boards: RX2 independent.
    m_sampleRateRx2Combo = new QComboBox(paramGroup);
    m_sampleRateRx2Combo->setMinimumWidth(120);
    m_sampleRateRx2Combo->setEnabled(false);
    m_sampleRateRx2Combo->setToolTip(
        tr("Enabled when Phase 3F multi-panadapter support lands."));
```

Replace the line:

```cpp
    paramForm->addRow(tr("Sample rate (Hz):"), m_sampleRateCombo);
```

with:

```cpp
    paramForm->addRow(tr("RX1 sample rate (Hz):"), m_sampleRateRx1Combo);
    paramForm->addRow(tr("RX2 sample rate (Hz):"), m_sampleRateRx2Combo);
```

Also update the existing `connect(...)` for the sample-rate combo (around line 92) to reference the renamed member.

- [ ] **Step 3: Switch populate() to use allowedSampleRates**

Find the block in `populate()`:

```cpp
    // Rebuild sample-rate combo from caps.sampleRates (zero = unused slot)
    // Source: Thetis Setup.cs:847-850 — rate list filtered per model
    {
        QSignalBlocker blocker(m_sampleRateCombo);
        m_sampleRateCombo->clear();
        for (int rate : caps.sampleRates) {
            if (rate > 0) {
                m_sampleRateCombo->addItem(
                    QStringLiteral("%1").arg(rate), rate);
            }
        }
        // Select the max-rate entry by default (last non-zero slot)
        if (m_sampleRateCombo->count() > 0) {
            m_sampleRateCombo->setCurrentIndex(m_sampleRateCombo->count() - 1);
        }
    }
```

Replace with:

```cpp
    // Rebuild RX1 combo from allowedSampleRates(proto, caps, model) — matches
    // Thetis setup.cs:847-852 filtering (per-protocol list ∩ caps.sampleRates,
    // with the RedPitaya extra-384k exception). Default selection is 192000
    // per setup.cs:866; if absent, first allowed entry.
    //
    // model comes from HardwareProfile; RadioModel sets it via
    // m_hardwareProfile.model. RadioInfoTab gets access via m_model.
    HPSDRModel model = HPSDRModel::HERMES;
    if (m_model) {
        model = m_model->hardwareProfile().model;
    }
    const auto allowed = allowedSampleRates(info.protocol, caps, model);
    const int fallbackRate = defaultSampleRate(info.protocol, caps, model);
    {
        QSignalBlocker blocker(m_sampleRateRx1Combo);
        m_sampleRateRx1Combo->clear();
        for (int rate : allowed) {
            m_sampleRateRx1Combo->addItem(QStringLiteral("%1").arg(rate), rate);
        }
        // Default selection — 192000 (setup.cs:866).
        int idx = -1;
        for (int i = 0; i < m_sampleRateRx1Combo->count(); ++i) {
            if (m_sampleRateRx1Combo->itemData(i).toInt() == fallbackRate) {
                idx = i;
                break;
            }
        }
        if (idx < 0 && m_sampleRateRx1Combo->count() > 0) {
            idx = 0;
        }
        if (idx >= 0) {
            m_sampleRateRx1Combo->setCurrentIndex(idx);
        }
    }

    // RX2 combo mirrors RX1 items and selection (disabled stub).
    {
        QSignalBlocker blocker(m_sampleRateRx2Combo);
        m_sampleRateRx2Combo->clear();
        for (int rate : allowed) {
            m_sampleRateRx2Combo->addItem(QStringLiteral("%1").arg(rate), rate);
        }
        m_sampleRateRx2Combo->setCurrentIndex(m_sampleRateRx1Combo->currentIndex());
    }
```

- [ ] **Step 4: Add the include**

At the top of `src/gui/setup/hardware/RadioInfoTab.cpp`, add:

```cpp
#include "core/SampleRateCatalog.h"
#include "core/HpsdrModel.h"
#include "core/HardwareProfile.h"
```

- [ ] **Step 5: Update onSampleRateChanged to use the renamed member**

```cpp
void RadioInfoTab::onSampleRateChanged(int index)
{
    if (index < 0) { return; }
    int rate = m_sampleRateRx1Combo->itemData(index).toInt();
    if (rate > 0) {
        emit settingChanged(QStringLiteral("radioInfo/sampleRate"), rate);
    }
}
```

- [ ] **Step 6: Update restoreSettings to use the renamed member**

Update the `sampleRate` read block to use `m_sampleRateRx1Combo`, and also mirror the selection into the RX2 combo:

```cpp
    // sampleRate — match combo item data
    auto srIt = settings.constFind(QStringLiteral("sampleRate"));
    if (srIt != settings.constEnd()) {
        const int rate = srIt.value().toInt();
        QSignalBlocker blocker(m_sampleRateRx1Combo);
        for (int i = 0; i < m_sampleRateRx1Combo->count(); ++i) {
            if (m_sampleRateRx1Combo->itemData(i).toInt() == rate) {
                m_sampleRateRx1Combo->setCurrentIndex(i);
                // Mirror into RX2 stub so it stays visually aligned.
                QSignalBlocker b2(m_sampleRateRx2Combo);
                m_sampleRateRx2Combo->setCurrentIndex(i);
                break;
            }
        }
    }
```

- [ ] **Step 7: Build and run the existing RadioInfoTab-related tests**

```bash
cmake --build build -j && \
  ./build/tests/tst_hardware_page_persistence.exe && \
  ./build/tests/tst_hardware_page_capability_gating.exe
```

Expected: both pass.

- [ ] **Step 8: Commit**

```bash
git add src/gui/setup/hardware/RadioInfoTab.h src/gui/setup/hardware/RadioInfoTab.cpp
git commit -S -m "$(cat <<'EOF'
feat(sample-rate): RadioInfoTab populates from allowedSampleRates + RX2 stub

RX1 combo populated from SampleRateCatalog::allowedSampleRates (Thetis
setup.cs:847-852 per-protocol + RedPitaya 384k filter ∩ caps.sampleRates).
Default selection is 192000 (setup.cs:866) when present, first-allowed
otherwise — replaces the prior "max rate always selected" UX.

RX2 combo added as a disabled stub; mirrors RX1 items and selection.
Activates with Phase 3F. Force-equal rules from setup.cs:7065-7073 and
7155-7156 documented in a comment block at the combo creation site.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: RadioInfoTab — fix activeRxCount emit bug

**Files:**
- Modify: `src/gui/setup/hardware/RadioInfoTab.h` — add slot declaration
- Modify: `src/gui/setup/hardware/RadioInfoTab.cpp` — connect valueChanged signal

- [ ] **Step 1: Extend the test to cover activeRxCount round-trip**

Edit `tests/tst_hardware_page_persistence.cpp` and add this test inside the class, after `hardwareValuesReturnsNamespacedMap`:

```cpp
    void activeRxCountRoundTrip()
    {
        AppSettings s(m_dir.filePath(QStringLiteral("hw4.xml")));
        const QString mac = QStringLiteral("aa:bb:cc:11:22:33");
        s.setHardwareValue(mac, QStringLiteral("radioInfo/activeRxCount"), 3);
        s.save();

        AppSettings s2(m_dir.filePath(QStringLiteral("hw4.xml")));
        s2.load();
        QCOMPARE(s2.hardwareValue(mac, QStringLiteral("radioInfo/activeRxCount")).toInt(), 3);
    }
```

Run: `./build/tests/tst_hardware_page_persistence.exe`. Expected: PASS (this one's pure AppSettings — the bug is in RadioInfoTab emit, not storage).

- [ ] **Step 2: Add the slot declaration**

In `src/gui/setup/hardware/RadioInfoTab.h`, inside the `private slots:` block, add:

```cpp
    void onActiveRxCountChanged(int count);
```

- [ ] **Step 3: Implement the slot**

In `src/gui/setup/hardware/RadioInfoTab.cpp`, add the slot definition after `onSampleRateChanged`:

```cpp
void RadioInfoTab::onActiveRxCountChanged(int count)
{
    if (count < 1) { return; }
    emit settingChanged(QStringLiteral("radioInfo/activeRxCount"), count);
}
```

- [ ] **Step 4: Wire the signal**

In the ctor's `// ── Connections ──` block, after the existing `m_sampleRateRx1Combo` connect, add:

```cpp
    connect(m_activeRxSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &RadioInfoTab::onActiveRxCountChanged);
```

- [ ] **Step 5: Build and manually verify**

```bash
cmake --build build -j
```

(No new automated test needed — the round-trip is covered by Task 2's resolver tests + Step 1's storage round-trip. The emit behavior itself is a one-line Qt connect; automated testing requires dispatching the Qt event loop, which is covered by the end-to-end spec test run at the end.)

- [ ] **Step 6: Commit**

```bash
git add tests/tst_hardware_page_persistence.cpp \
        src/gui/setup/hardware/RadioInfoTab.h \
        src/gui/setup/hardware/RadioInfoTab.cpp
git commit -S -m "$(cat <<'EOF'
fix(sample-rate): wire activeRxCount valueChanged so it emits settingChanged

The spinbox's valueChanged signal was never connected in the ctor.
Restoration from AppSettings worked but the user could not change it.
Adds onActiveRxCountChanged slot and connects it, and adds a round-trip
storage test covering the new AppSettings key.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: RadioInfoTab — inline reconnect banner

**Files:**
- Modify: `src/gui/setup/hardware/RadioInfoTab.h` — add banner label + onWireSampleRateChanged slot + m_activeWireRate member
- Modify: `src/gui/setup/hardware/RadioInfoTab.cpp` — build banner, update logic

- [ ] **Step 1: Extend the header**

In `src/gui/setup/hardware/RadioInfoTab.h`:

Add to the forward declarations near the top:

```cpp
class QFrame;
```

Add to the `private slots:` block:

```cpp
    void onWireSampleRateChanged(double hz);
```

Add a private helper declaration:

```cpp
    void updateReconnectBanner();
```

Add to the `private:` block, after the RX2 combo:

```cpp
    QFrame*      m_reconnectBanner{nullptr};
    QLabel*      m_reconnectBannerLabel{nullptr};
    int          m_activeWireRate{0}; // last rate reported via wireSampleRateChanged
```

- [ ] **Step 2: Extend the ctor to build the banner**

In `src/gui/setup/hardware/RadioInfoTab.cpp` at the top, add:

```cpp
#include <QFrame>
```

At the end of the `Operating parameters group` block (after `outerLayout->addWidget(paramGroup);` around current line 82), insert:

```cpp
    // Pending-reconnect banner — hidden by default. Shown when the combo
    // value differs from the active wire rate AND a radio is connected.
    // Removed when Phase C live-apply lands (PR #36); until then, users
    // must reconnect for rate changes to take effect.
    m_reconnectBanner = new QFrame(this);
    m_reconnectBanner->setFrameShape(QFrame::StyledPanel);
    m_reconnectBanner->setStyleSheet(QStringLiteral(
        "QFrame { background-color: #3a2a10; border: 1px solid #a07020; "
        "border-radius: 3px; padding: 4px; }"));
    auto* bannerLayout = new QHBoxLayout(m_reconnectBanner);
    bannerLayout->setContentsMargins(6, 4, 6, 4);
    m_reconnectBannerLabel = new QLabel(m_reconnectBanner);
    m_reconnectBannerLabel->setStyleSheet(QStringLiteral("color: #ffcc66;"));
    m_reconnectBannerLabel->setWordWrap(true);
    bannerLayout->addWidget(m_reconnectBannerLabel);
    m_reconnectBanner->setVisible(false);
    outerLayout->addWidget(m_reconnectBanner);
```

Also add `#include <QHBoxLayout>` to the includes near the top if missing.

- [ ] **Step 3: Subscribe to wireSampleRateChanged**

In the `// ── Connections ──` block, add after the activeRxSpin connect:

```cpp
    if (m_model) {
        connect(m_model, &RadioModel::wireSampleRateChanged,
                this, &RadioInfoTab::onWireSampleRateChanged);
    }
```

- [ ] **Step 4: Implement onWireSampleRateChanged and updateReconnectBanner**

Add to the `// ── private slots ──` section:

```cpp
void RadioInfoTab::onWireSampleRateChanged(double hz)
{
    m_activeWireRate = static_cast<int>(hz);
    updateReconnectBanner();
}

void RadioInfoTab::updateReconnectBanner()
{
    // Banner shows only when a radio is connected AND the combo's selected
    // rate differs from the active wire rate. m_activeWireRate is 0 when
    // no radio has ever connected this session — hide the banner then.
    if (!m_model || !m_model->isConnected() || m_activeWireRate <= 0) {
        m_reconnectBanner->setVisible(false);
        return;
    }
    const int selected = m_sampleRateRx1Combo->currentData().toInt();
    if (selected <= 0 || selected == m_activeWireRate) {
        m_reconnectBanner->setVisible(false);
        return;
    }
    m_reconnectBannerLabel->setText(
        tr("⚠ Reconnect to apply new sample rate (pending: %1 kHz, active: %2 kHz)")
            .arg(selected / 1000)
            .arg(m_activeWireRate / 1000));
    m_reconnectBanner->setVisible(true);
}
```

- [ ] **Step 5: Call updateReconnectBanner after the user changes the combo**

Update `onSampleRateChanged`:

```cpp
void RadioInfoTab::onSampleRateChanged(int index)
{
    if (index < 0) { return; }
    int rate = m_sampleRateRx1Combo->itemData(index).toInt();
    if (rate > 0) {
        emit settingChanged(QStringLiteral("radioInfo/sampleRate"), rate);
        // Mirror into RX2 stub visually.
        QSignalBlocker blocker(m_sampleRateRx2Combo);
        m_sampleRateRx2Combo->setCurrentIndex(index);
        updateReconnectBanner();
    }
}
```

- [ ] **Step 6: Build and manual-smoke on Windows**

```bash
cmake --build build -j && \
  taskkill //F //IM NereusSDR.exe 2>/dev/null; \
  ./build/NereusSDR.exe &
```

(Per user memory `feedback_auto_launch_after_build.md` — kill + relaunch app after every successful build.)

Manual verification checklist (document in PR description):

1. With no radio connected, open Setup → Hardware Config → Radio Info. Expect both combos present, RX2 disabled with tooltip, spinbox range `1..1`, no banner.
2. Connect to a test radio (HL2 or ANAN-G2). Expect combos populate from allowed list, spinbox range `1..caps.maxReceivers`, no banner (combo matches wire rate).
3. Change RX1 combo to a non-default rate. Expect banner appears with "pending: N kHz, active: M kHz".
4. Reconnect. Expect banner disappears; wire rate matches persisted value.
5. Close and relaunch app, reconnect same radio. Expect combo shows the previously-selected rate, and the session runs at that rate.

- [ ] **Step 7: Commit**

```bash
git add src/gui/setup/hardware/RadioInfoTab.h src/gui/setup/hardware/RadioInfoTab.cpp
git commit -S -m "$(cat <<'EOF'
feat(sample-rate): inline reconnect banner on RadioInfoTab

Banner shows "⚠ Reconnect to apply new sample rate (pending: X kHz,
active: Y kHz)" whenever the combo's selected value differs from the
active wire rate reported via RadioModel::wireSampleRateChanged. Hidden
when disconnected, when no active rate is yet known, or when selection
matches active.

The banner is transitional — it disappears when the Phase C live-apply
PR (#36) lands and rate changes take effect without reconnect. Comment
block in the ctor flags it for removal.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: CLAUDE.md rule correction

**Files:**
- Modify: `CLAUDE.md` — update the "radio-authoritative" rule

- [ ] **Step 1: Locate the current rule**

Grep to confirm the current location:

```bash
grep -n "Radio-authoritative" CLAUDE.md
```

Expected: one line near 293-294.

- [ ] **Step 2: Replace the block**

Current text (verify before editing — lines may shift):

```
**Radio-authoritative (do NOT persist):** ADC attenuation, preamp, TX power,
antenna selection, hardware sample rate.
```

Replace with:

```
**Radio-authoritative (do NOT persist):** ADC attenuation, preamp, TX power,
antenna selection.

**Hardware sample rate and active RX count:** persisted per-MAC in AppSettings
under `hardware/<mac>/radioInfo/sampleRate` and `.../activeRxCount`. Applied
on next connect. This matches Thetis, which persists rate globally via
`DB.SaveVarsDictionary("Options", ...)` (setup.cs:1627). NereusSDR scopes
per-MAC so users with multiple radios retain per-radio selections. Live-apply
of rate changes to a running connection is deferred to the follow-up PR that
adds WDSP channel teardown/rebuild infrastructure.
```

- [ ] **Step 3: Commit**

```bash
git add CLAUDE.md
git commit -S -m "$(cat <<'EOF'
docs: correct CLAUDE.md — sample rate IS persisted, matching Thetis

Previous rule said "hardware sample rate is radio-authoritative, do
NOT persist". That doesn't match Thetis, which persists RX1/RX2 rate
via DB.SaveVarsDictionary("Options", ...) at setup.cs:1627. Corrected
text scopes the rule: ADC attenuation/preamp/TX power/antenna
selection remain radio-authoritative; rate + active RX count persist
per-MAC in AppSettings and apply on next connect.

Companion to PR #35 Hardware Config sample-rate wiring.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: CHANGELOG entry

**Files:**
- Modify: `CHANGELOG.md` — add Unreleased entry

- [ ] **Step 1: Check current Unreleased structure**

```bash
head -30 CHANGELOG.md
```

- [ ] **Step 2: Add the entry**

Under the `## Unreleased` heading (or create one if missing, directly under the main title), add:

```markdown
### Fixed
- Hardware Config → Radio Info: sample-rate combo now actually drives the
  wire rate used on the next connection (previously emitted to AppSettings
  but was never read back; `RadioModel::connectToRadio` hardcoded
  `isP1 ? 192000 : 768000`). Active-RX count spinbox now emits on change
  (its `valueChanged` signal was not connected).

### Changed
- Sample-rate combo now offers the full Thetis-parity rate list per protocol:
  P1 = 48/96/192 kHz (plus 384 on RedPitaya), P2 = 48/96/192/384/768/1536 kHz.
  Default selection is 192 kHz (matches Thetis setup.cs:866) instead of the
  previous "always max rate" UX.
- Hardware Config gains an RX2 sample-rate combo as a disabled stub for Phase
  3F multi-panadapter. The Thetis force-equal rules (setup.cs:7065-7073,
  7155-7156) are documented at the combo creation site.
- `RadioInfoTab` shows a "⚠ Reconnect to apply new sample rate" banner when
  the combo differs from the active wire rate. The banner disappears when
  live-apply lands in the follow-up PR.
```

- [ ] **Step 3: Commit**

```bash
git add CHANGELOG.md
git commit -S -m "$(cat <<'EOF'
docs: CHANGELOG entry for sample-rate wiring (PR #35)

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Full-suite regression check

**Files:** (no changes — verification only)

- [ ] **Step 1: Clean build**

```bash
cmake --build build -j 2>&1 | tail -30
```

Expected: clean build, no warnings about the touched files.

- [ ] **Step 2: Run the entire test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Pay particular attention to:
- `tst_sample_rate_catalog` — 14 cases, all pass.
- `tst_hardware_page_persistence` — 4 cases including new `activeRxCountRoundTrip`.
- `tst_hardware_page_capability_gating` — not affected, should still pass.
- `tst_board_capabilities` — not affected.
- Anything under `tst_rxchannel_*` / `tst_p1_*` — not affected; flag if any break.

If a pre-existing test fails that references the prior hardcoded 768000 for P2, update the expected value to 192000 and add a brief note in the test: `// Default rate changed from 768000 (hardcode) to 192000 (Thetis setup.cs:866) in PR #35.`

- [ ] **Step 3: Launch the app and run through the manual smoke checklist**

(Same as Task 7 Step 6 — relisting for completeness before PR open.)

1. No radio → both combos present, RX2 disabled, spinbox read-only 1..1, no banner.
2. Connect HL2 → RX1 combo shows {48000, 96000, 192000}, default 192000 selected.
3. Connect ANAN-G2 → RX1 combo shows all six P2 rates, default 192000.
4. Change ANAN-G2 to 384000 → banner appears.
5. Disconnect + reconnect → banner gone, wire rate 384000 per logs (`qCInfo(lcConnection) << "Connecting with sampleRate=" << ...`).
6. Change spinbox to 2 (on G2). No banner (rate unchanged), value persisted.
7. Relaunch app, reconnect → spinbox shows 2, combo shows 384000.
8. Persist 1536000 on G2, then swap cable to HL2 (or in-app "Forget radio" and connect HL2). HL2 shows 192000 (fallback) and log has `qCWarning: Persisted sample rate 1536000 not valid for <mac> — falling back to default`.

- [ ] **Step 4: If any smoke step fails**

Stop, do NOT open a PR. Diagnose via systematic-debugging skill, fix at root cause, re-run the entire smoke checklist from step 1.

- [ ] **Step 5: Push the branch and open PR #35**

```bash
git push -u origin feature/sample-rate-wiring
gh pr create --title "Hardware Config: wire sample rate + active RX count (Plan C Phase 1)" --body "$(cat <<'EOF'
## Summary
- Wires Hardware Config → Radio Info sample-rate combo and active-RX-count spinbox into `RadioModel::connectToRadio` via new `SampleRateCatalog` resolvers.
- Replaces the `isP1 ? 192000 : 768000` / `isP1 ? 256 : 1024` hardcodes with Thetis-cited catalog lookups (setup.cs:847-852 rate lists, setup.cs:866 default, cmsetup.c:104-111 buffer formula).
- Fixes the `activeRxCount` spinbox emit bug (`valueChanged` signal was never connected).
- Adds disabled RX2 combo stub ready for Phase 3F multi-panadapter.
- Inline reconnect banner surfaces pending rate changes until Phase C live-apply (follow-up PR) lands.
- Updates CLAUDE.md to match Thetis: sample rate IS persisted (per-MAC).

First PR under the Plan C Hardware Config refactor. Establishes the AppSettings→RadioModel read-on-connect pattern that subsequent PRs apply to the other eight Hardware Config tabs.

## Test plan
- [x] `tst_sample_rate_catalog` — 14 unit cases (rate lists, RedPitaya exception, caps intersection, defaults, resolvers)
- [x] `tst_hardware_page_persistence` — extended with `activeRxCountRoundTrip`
- [x] Full CTest suite passes
- [x] Manual smoke on Windows with HL2 and ANAN-G2 (checklist in `docs/architecture/2026-04-16-sample-rate-wiring-plan.md`)

## References
- Design spec: [`docs/architecture/2026-04-16-sample-rate-wiring-design.md`](docs/architecture/2026-04-16-sample-rate-wiring-design.md)
- Implementation plan: [`docs/architecture/2026-04-16-sample-rate-wiring-plan.md`](docs/architecture/2026-04-16-sample-rate-wiring-plan.md)

JJ Boyd ~KG4VCF
Co-Authored with Claude Code
EOF
)"
```

- [ ] **Step 6: Open the PR URL in the browser**

(Per user memory `feedback_open_links_after_post.md` — always open GitHub URLs in browser after posting.)

```bash
start "" "$(gh pr view --json url --jq .url)"
```

---

## Self-review

Running the self-review checklist before offering execution modes.

**Spec coverage:**

| Spec section | Task(s) covering it |
|---|---|
| UI: RX1 combo + RX2 stub + activeRxCount spin + banner (§4) | Task 5 (RX2 stub + populate), Task 6 (activeRxCount), Task 7 (banner) |
| SampleRateCatalog module (§5) | Task 1 (header), Task 2 (tests), Task 3 (impl) |
| Data flow read-on-connect (§6) | Task 4 (RadioModel integration) |
| Persistence keys per-MAC (§7) | Tasks 5, 6 (tab emits), Task 4 (consume), AppSettings unchanged |
| CLAUDE.md correction (§7) | Task 8 |
| Error handling — stale rate + clamp (§8) | Task 2 (tests), Task 3 (impl) |
| Testing — catalog unit tests (§9.1) | Task 2 |
| Testing — persistence extension (§9.2) | Task 6 Step 1 |
| Testing — integration (§9.3) | Task 2 covers it (resolvers are pure-function, tested without RadioModel boot). Simpler than spec's "integration test against a mock RadioModel" — justified because the pure-function split puts all the interesting logic under direct test. |
| File touch list (§10) | Task 1-10 collectively |
| CHANGELOG (§10) | Task 9 |

All sections covered.

**Placeholder scan:** No "TBD", "TODO" (comments about Phase 3F stub are real design notes, not placeholders), "implement later", or vague "add appropriate error handling". Every code block shows real code.

**Type consistency:**
- `allowedSampleRates(ProtocolVersion, const BoardCapabilities&, HPSDRModel)` — signature consistent across header (Task 1), impl (Task 3), tests (Task 2), and call site (Tasks 4, 5).
- `resolveSampleRate(const AppSettings&, const QString&, ProtocolVersion, const BoardCapabilities&, HPSDRModel)` — same.
- `resolveActiveRxCount(const AppSettings&, const QString&, const BoardCapabilities&)` — same.
- `bufferSizeForRate(int)` — used in Task 4 call site, matches Task 1 declaration.
- Member rename `m_sampleRateCombo` → `m_sampleRateRx1Combo` is consistent across Tasks 5-7.

**One dependency to verify during execution:** Task 4 assumes `RadioConnection::setActiveReceiverCount` exists (grep verifies in Task 4 Step 4). If it doesn't exist on the base class but only on `P1RadioConnection`, the guard logic branches. The plan flags this explicitly so an executor can choose the right path.

Plan review complete. No blocking issues.

---
