# Phase 3G — RX Epic Sub-epic D: Bandplan Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Port AetherSDR's `BandPlanManager` + `drawBandPlan()` into NereusSDR — a selectable per-region amateur bandplan overlay (mode-coloured strip + spot markers) painted at the bottom of the spectrum area.

**Architecture:** A new `BandPlanManager` model loads bundled JSON region files from `:/bandplans/*.json` at startup, owns the active region, and emits `planChanged()`. `RadioModel` owns the manager (mirroring `m_alexController` / `m_ocMatrix` pattern). `SpectrumWidget` gains a non-owning `BandPlanManager*` pointer + a `m_bandPlanFontSize` knob (0 = off, AetherSDR convention) and a ported `drawBandPlan()` that paints into both the QPainter fallback path and the GPU static-overlay texture (same dual-path pattern as the dBm scale strip). Setup → Display gains a region dropdown + font-size control.

**Tech Stack:** C++20, Qt6 (QObject, QJsonDocument, Q_PROPERTY-free signal/slot model), QRhi widget (existing GPU + QPainter dual-path), QTest for unit tests.

**Spec reference:** [phase3g-rx-experience-epic-design.md](phase3g-rx-experience-epic-design.md) § sub-epic D.

**Upstream stamp:** AetherSDR `main@0cd4559` (2026-04-21). All inline cites in ports use `// From AetherSDR <file>:<line> [@0cd4559]`.

---

## Authoring-time decisions (lock these before coding)

Three points where the design doc paraphrase and the AetherSDR upstream code disagree. **The port follows upstream**, since this is a port, not a redesign. Documented here so a reviewer doesn't read the doc, look at the code, and ask "wait, why does it look like that?".

1. **Geometry: bottom strip, not full-height alpha overlay.**
   Design §D says "Segments render as semi-transparent coloured bands (alpha ~0.25) beneath the spectrum trace". AetherSDR's actual implementation paints a **thin opaque strip** at the bottom of `specRect`, height `m_bandPlanFontSize + 4` px, with license-class brightness blending against a dark background — not a full-height alpha overlay. The strip approach avoids visually competing with the spectrum trace and keeps mode labels legible. **Port matches upstream** ([SpectrumWidget.cpp:4220-4293 @0cd4559](https://github.com/ten9876/AetherSDR/blob/0cd4559/src/gui/SpectrumWidget.cpp)).

2. **AppSettings keys: AetherSDR names, not the design doc's `BandplanRegion`.**
   Design §D suggests `BandplanRegion` (string "arrl-us"-style) + `BandplanOverlayEnabled` (bool). AetherSDR uses `BandPlanName` (display name, e.g. `"ARRL (US)"`) + `BandPlanFontSize` (int, `0` = off). Port matches AetherSDR — display-name keys round-trip cleanly with the dropdown UI, and the int-as-visibility convention lets the same setter drive both visibility and label scale.

3. **First-launch default: hardcoded "ARRL (US)", not locale-derived.**
   Design §D says "Default selected from user's locale at first run". AetherSDR hardcodes `"ARRL (US)"`. Locale-driven default is YAGNI for the first port — adds platform-specific surface area, a fallback table, and a test matrix without measurable user benefit (a Setup → Display dropdown is one click away). **Port matches upstream**; locale-default is an explicit follow-up in the verification matrix's "deferred" column.

---

## File Structure

| File | Responsibility | Change type |
|---|---|---|
| `resources/bandplans/rac-canada.json` *(new)* | RAC Canada bandplan data | Create — copy verbatim from AetherSDR |
| `resources.qrc` | Register the new JSON in `:/bandplans` prefix | Modify — add one `<file alias>` line |
| `src/models/BandPlan.h` *(new)* | `Segment` + `Spot` value types (no Qt deps beyond QString/QColor) | Create — extracted from BandPlanManager.h so other code can `#include` types without pulling QObject |
| `src/models/BandPlanManager.h` *(new)* | QObject loader: enumerates `:/bandplans/*.json`, owns active plan, emits `planChanged()` | Create — port of AetherSDR file with verbatim header + NereusSDR mod block |
| `src/models/BandPlanManager.cpp` *(new)* | Loader impl | Create — port of AetherSDR file |
| `src/models/RadioModel.h` | Owns `BandPlanManager` instance (mirrors `m_alexController`) | Modify — add member, accessor, `loadPlans()` call in `init()` |
| `src/models/RadioModel.cpp` | Wire into `init()` | Modify — call `m_bandPlanManager.loadPlans()` after AppSettings ready |
| `src/gui/SpectrumWidget.h` | Add `setBandPlanManager(BandPlanManager*)`, `setBandPlanFontSize(int)`, members | Modify — non-owning pointer + int knob |
| `src/gui/SpectrumWidget.cpp` | Port `drawBandPlan()` + integrate into both QPainter + GPU overlay-static paths + AppSettings load/save | Modify — same dual-path pattern as `drawDbmScale` |
| `src/gui/MainWindow.cpp` | Wire `RadioModel::bandPlanManager()` → every active `SpectrumWidget` | Modify — one-line setter call where panadapter wiring already lives |
| `src/gui/setup/DisplaySetupPages.cpp` | Region dropdown + font-size spinbox in Spectrum Defaults page | Modify — new group box "Bandplan Overlay" |
| `tests/tst_bandplan_manager.cpp` *(new)* | Unit tests for JSON loader + active-plan switching | Create — QtTest, no QApplication needed |
| `tests/CMakeLists.txt` | Register the new test target | Modify |
| `docs/architecture/phase3g-rx-epic-d-verification/README.md` *(new)* | Manual verification matrix | Create — following 3G-8 / sub-epic A pattern |

---

## Pre-flight inventory (read-only — no code changes)

Before starting, run these greps to refresh your mental model. Pin the actual line numbers for the cite comments you'll add later.

```bash
# Confirm 4 JSONs already shipped, 1 to add (rac-canada.json missing)
ls resources/bandplans/

# Confirm qrc has bandplan prefix already wired
grep -A4 'prefix="/bandplans"' resources.qrc

# How drawDbmScale integrates into BOTH paint paths — bandplan follows the same shape
rg -n 'drawDbmScale|m_dbmScaleVisible|effectiveStripW' src/gui/SpectrumWidget.cpp src/gui/SpectrumWidget.h

# RadioModel ownership pattern for AlexController / OcMatrix — bandplan mirrors this
rg -n 'm_alexController|m_ocMatrix' src/models/RadioModel.h src/models/RadioModel.cpp

# Where MainWindow wires RadioModel sub-models into SpectrumWidget(s)
rg -n 'spectrumWidget\(\)|setOcMatrix|alexController' src/gui/MainWindow.cpp

# Where Setup → Display Spectrum Defaults page builds its UI
rg -n 'class SpectrumDefaultsPage|buildSpectrumDefaults' src/gui/setup/DisplaySetupPages.h src/gui/setup/DisplaySetupPages.cpp
```

Expected hits at plan-write time (will drift):

- `resources/bandplans/`: arrl-us.json, iaru-region1.json, iaru-region2.json, iaru-region3.json (4 of 5)
- `resources.qrc:4-9`: `prefix="/bandplans"` block with 4 file entries
- `src/gui/SpectrumWidget.cpp:1116-1120`: QPainter path `drawDbmScale` call
- `src/gui/SpectrumWidget.cpp:2696-2700`: GPU static-overlay path `drawDbmScale` call
- `src/gui/SpectrumWidget.cpp:432, 518`: AppSettings load/save for `DisplayDbmScaleVisible`
- `src/gui/SpectrumWidget.h:477`: `void drawDbmScale(...)` decl
- `src/models/RadioModel.h:167`: `alexController()` accessor; `:400` `m_alexController` member

If your `rg` results don't match this shape, line numbers have drifted — re-read the surrounding code before patching.

---

## Task 1: Copy `rac-canada.json` and register it

The 5th bandplan JSON. Pure data file + one qrc line — zero code change.

**Files:**
- Create: `resources/bandplans/rac-canada.json`
- Modify: `resources.qrc`

- [ ] **Step 1: Copy the JSON byte-for-byte from AetherSDR**

```bash
cp /Users/j.j.boyd/AetherSDR/resources/bandplans/rac-canada.json resources/bandplans/rac-canada.json
```

(If the AetherSDR clone path differs on your machine, locate it via `find ~ -name rac-canada.json -path '*AetherSDR*' 2>/dev/null` and substitute.)

Verify byte-identical:

```bash
diff -q resources/bandplans/rac-canada.json /Users/j.j.boyd/AetherSDR/resources/bandplans/rac-canada.json
```

Expected: no output (files match).

- [ ] **Step 2: Register the file in `resources.qrc`**

Edit `resources.qrc` — add one line inside the `<qresource prefix="/bandplans">` block, between `iaru-region3.json` and the closing `</qresource>`:

```xml
        <file alias="rac-canada.json">resources/bandplans/rac-canada.json</file>
```

Final block should read:

```xml
    <qresource prefix="/bandplans">
        <file alias="arrl-us.json">resources/bandplans/arrl-us.json</file>
        <file alias="iaru-region1.json">resources/bandplans/iaru-region1.json</file>
        <file alias="iaru-region2.json">resources/bandplans/iaru-region2.json</file>
        <file alias="iaru-region3.json">resources/bandplans/iaru-region3.json</file>
        <file alias="rac-canada.json">resources/bandplans/rac-canada.json</file>
    </qresource>
```

- [ ] **Step 3: Verify the build picks up the new resource**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: clean rebuild. Qt's `rcc` regenerates the resource symbol table; no source code touches it yet.

- [ ] **Step 4: Commit**

```bash
git add resources/bandplans/rac-canada.json resources.qrc
git commit -m "feat(bandplan): add rac-canada bandplan JSON

Final of the 5 bundled regions (ARRL-US + IARU R1/R2/R3 + RAC Canada).
Verbatim copy from AetherSDR main@0cd4559. No code consumer yet —
BandPlanManager port lands in the next commits."
```

---

## Task 2: Define the `BandPlan` value types (TDD: tests first)

Pure value types so tests can be written before the loader and consumers can `#include "BandPlan.h"` without pulling QObject. Tests verify type shape; behaviour comes in Task 3.

**Files:**
- Create: `src/models/BandPlan.h`
- Create: `tests/tst_bandplan_manager.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create the value-types header**

Create `src/models/BandPlan.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR — bandplan value types
//
// Ported from AetherSDR src/models/BandPlanManager.h [@0cd4559].
// AetherSDR is © its contributors and is licensed GPL-3.0-or-later.
//
// Modification history (NereusSDR):
//   2026-04-25  J.J. Boyd <jj@skyrunner.net>  Initial port for Phase 3G RX Epic sub-epic D.
//                                              Extracted Segment/Spot value types out of
//                                              BandPlanManager so consumers can include
//                                              them without QObject. AI assistance:
//                                              Anthropic Claude (claude-opus-4-7).

#pragma once

#include <QColor>
#include <QString>

namespace NereusSDR {

struct BandSegment {
    double  lowMhz{0.0};
    double  highMhz{0.0};
    QString label;
    QString license;  // "E", "E,G", "E,G,T", "T", "" = beacon/no TX
    QColor  color;
};

struct BandSpot {
    double  freqMhz{0.0};
    QString label;
};

}  // namespace NereusSDR
```

- [ ] **Step 2: Write failing tests for the value types and loader contract**

Create `tests/tst_bandplan_manager.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QSignalSpy>

#include "models/BandPlan.h"
#include "models/BandPlanManager.h"

using namespace NereusSDR;

class TestBandPlanManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Value types
    void segment_defaultIsEmpty();
    void spot_defaultIsEmpty();

    // Loader: bundled resources
    void loadPlans_findsAllFiveRegions();
    void loadPlans_arrlUsHasSegmentsAndSpots();
    void loadPlans_iaruRegion2HasSegments();

    // Active plan
    void setActivePlan_emitsPlanChanged();
    void setActivePlan_unknownNameDoesNothing();
    void setActivePlan_segmentsReflectActivePlan();

    // Default selection
    void loadPlans_defaultsToArrlUs();
};

void TestBandPlanManager::initTestCase()
{
    // Resource init runs at static-init time; nothing to do here.
}

void TestBandPlanManager::segment_defaultIsEmpty()
{
    BandSegment s;
    QCOMPARE(s.lowMhz, 0.0);
    QCOMPARE(s.highMhz, 0.0);
    QVERIFY(s.label.isEmpty());
    QVERIFY(!s.color.isValid());
}

void TestBandPlanManager::spot_defaultIsEmpty()
{
    BandSpot s;
    QCOMPARE(s.freqMhz, 0.0);
    QVERIFY(s.label.isEmpty());
}

void TestBandPlanManager::loadPlans_findsAllFiveRegions()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    const QStringList names = mgr.availablePlans();
    QCOMPARE(names.size(), 5);
    QVERIFY(names.contains("ARRL (US)"));
    QVERIFY(names.contains("IARU Region 1"));
    QVERIFY(names.contains("IARU Region 2"));
    QVERIFY(names.contains("IARU Region 3"));
    QVERIFY(names.contains("RAC (Canada)"));
}

void TestBandPlanManager::loadPlans_arrlUsHasSegmentsAndSpots()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("ARRL (US)");
    QVERIFY(mgr.segments().size() > 50);   // arrl-us.json has ~70 segments
    QVERIFY(mgr.spots().size()    > 50);   // arrl-us.json has ~80+ spots

    // 20m CW segment must exist (sanity-check round-trip of low/high/label/color)
    bool found20mCw = false;
    for (const auto& seg : mgr.segments()) {
        if (qFuzzyCompare(seg.lowMhz, 14.025) && seg.label == "CW" && seg.color.isValid()) {
            found20mCw = true;
            break;
        }
    }
    QVERIFY(found20mCw);
}

void TestBandPlanManager::loadPlans_iaruRegion2HasSegments()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("IARU Region 2");
    QVERIFY(mgr.segments().size() > 10);
}

void TestBandPlanManager::setActivePlan_emitsPlanChanged()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    QSignalSpy spy(&mgr, &BandPlanManager::planChanged);
    mgr.setActivePlan("IARU Region 1");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mgr.activePlanName(), QString("IARU Region 1"));
}

void TestBandPlanManager::setActivePlan_unknownNameDoesNothing()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    const QString before = mgr.activePlanName();
    QSignalSpy spy(&mgr, &BandPlanManager::planChanged);
    mgr.setActivePlan("Klingon Empire Bandplan");
    QCOMPARE(spy.count(), 0);
    QCOMPARE(mgr.activePlanName(), before);
}

void TestBandPlanManager::setActivePlan_segmentsReflectActivePlan()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("ARRL (US)");
    const int arrlCount = mgr.segments().size();
    mgr.setActivePlan("IARU Region 1");
    const int r1Count = mgr.segments().size();
    QVERIFY(arrlCount != r1Count);   // different region = different segment count
}

void TestBandPlanManager::loadPlans_defaultsToArrlUs()
{
    // First-launch default (no AppSettings key yet) is "ARRL (US)".
    // We can't easily reset AppSettings in a unit test, but we can verify
    // the ctor + loadPlans() leaves a non-empty active plan and that the
    // public API returns segments without a setActivePlan() call.
    BandPlanManager mgr;
    mgr.loadPlans();
    QVERIFY(!mgr.activePlanName().isEmpty());
    QVERIFY(!mgr.segments().isEmpty());
}

QTEST_MAIN(TestBandPlanManager)
#include "tst_bandplan_manager.moc"
```

- [ ] **Step 3: Register the test in `tests/CMakeLists.txt`**

Find the existing pattern (e.g. how `tst_alex_controller` is registered) and add a parallel block. Example shape — match the surrounding style:

```cmake
add_executable(tst_bandplan_manager tst_bandplan_manager.cpp)
target_link_libraries(tst_bandplan_manager PRIVATE
    NereusSDRObjs Qt6::Test Qt6::Core Qt6::Gui)
add_test(NAME tst_bandplan_manager COMMAND tst_bandplan_manager)
```

The target needs to link against `NereusSDRObjs` (or whatever object library exposes `BandPlanManager.cpp`) and `Qt6::Test`/`Qt6::Core`/`Qt6::Gui` (Gui is needed for `QColor`).

- [ ] **Step 4: Confirm tests fail to build (BandPlanManager doesn't exist yet)**

```bash
cmake --build build --target tst_bandplan_manager -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: **build failure** — `BandPlanManager.h: No such file or directory`. That's the red bar; Task 3 makes it green.

- [ ] **Step 5: Commit (red bar — explicit)**

```bash
git add src/models/BandPlan.h tests/tst_bandplan_manager.cpp tests/CMakeLists.txt
git commit -m "test(bandplan): failing tests for BandPlanManager loader

Defines BandSegment + BandSpot value types in src/models/BandPlan.h.
tst_bandplan_manager.cpp covers the 5-region resource loader, active-
plan switching, signal emission, and the ARRL-US default. Build will
fail on this commit (BandPlanManager.h does not exist yet); Task 3
ports it and turns the bar green."
```

---

## Task 3: Port `BandPlanManager` to make tests green

Verbatim port of AetherSDR `src/models/BandPlanManager.{h,cpp}` with NereusSDR namespace + AppSettings binding.

**Files:**
- Create: `src/models/BandPlanManager.h`
- Create: `src/models/BandPlanManager.cpp`
- Modify: `CMakeLists.txt` (top-level — register the new sources in the object library)

- [ ] **Step 1: Port the header**

Create `src/models/BandPlanManager.h`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR — bandplan manager
//
// Ported from AetherSDR src/models/BandPlanManager.h [@0cd4559].
// AetherSDR is © its contributors and is licensed GPL-3.0-or-later.
//
// Modification history (NereusSDR):
//   2026-04-25  J.J. Boyd <jj@skyrunner.net>  Initial port for Phase 3G RX
//                                              Epic sub-epic D. Renamed to
//                                              NereusSDR namespace; reuses
//                                              src/models/BandPlan.h value
//                                              types instead of nested ones.
//                                              AppSettings key kept as
//                                              "BandPlanName" for upstream
//                                              parity. AI assistance:
//                                              Anthropic Claude
//                                              (claude-opus-4-7).

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "BandPlan.h"

namespace NereusSDR {

// Manages selectable band-plan overlays for the spectrum display.
// Plans are loaded from bundled Qt resource JSON files at
// :/bandplans/*.json. Active selection persists in AppSettings under
// "BandPlanName".
//
// From AetherSDR src/models/BandPlanManager.h [@0cd4559].
class BandPlanManager : public QObject {
    Q_OBJECT

public:
    explicit BandPlanManager(QObject* parent = nullptr);

    // Load all bundled plans from Qt resources. Idempotent: re-reads the
    // resource directory and re-applies the saved active plan.
    void loadPlans();

    // Active plan
    QString activePlanName() const { return m_activeName; }
    void    setActivePlan(const QString& name);
    const QVector<BandSegment>& segments() const { return m_segments; }
    const QVector<BandSpot>&    spots()    const { return m_spots; }

    // All loaded plan display names (for Setup → Display dropdown).
    QStringList availablePlans() const;

signals:
    void planChanged();

private:
    struct PlanData {
        QString             name;
        QVector<BandSegment> segments;
        QVector<BandSpot>    spots;
    };

    bool loadPlanFromJson(const QString& path, PlanData& out);

    QVector<PlanData>    m_plans;
    QString              m_activeName;
    QVector<BandSegment> m_segments;  // active plan's segments
    QVector<BandSpot>    m_spots;     // active plan's spots
};

}  // namespace NereusSDR
```

- [ ] **Step 2: Port the implementation**

Create `src/models/BandPlanManager.cpp`:

```cpp
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See BandPlanManager.h for license, attribution, and modification history.

#include "BandPlanManager.h"

#include "core/AppSettings.h"

#include <QColor>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcBandPlan, "nereussdr.bandplan")
}

BandPlanManager::BandPlanManager(QObject* parent)
    : QObject(parent)
{
}

// From AetherSDR BandPlanManager.cpp:17-40 [@0cd4559]
void BandPlanManager::loadPlans()
{
    m_plans.clear();

    QDir resDir(":/bandplans");
    const auto entries = resDir.entryList({"*.json"}, QDir::Files, QDir::Name);
    for (const auto& filename : entries) {
        PlanData plan;
        if (loadPlanFromJson(":/bandplans/" + filename, plan)) {
            m_plans.append(std::move(plan));
        }
    }

    // Activate the persisted plan, defaulting to "ARRL (US)" on first launch.
    QString saved = AppSettings::instance()
                        .value("BandPlanName", QStringLiteral("ARRL (US)"))
                        .toString();
    bool found = false;
    for (const auto& p : m_plans) {
        if (p.name == saved) { found = true; break; }
    }
    if (!found && !m_plans.isEmpty()) {
        saved = m_plans.first().name;
    }

    setActivePlan(saved);
}

// From AetherSDR BandPlanManager.cpp:42-55 [@0cd4559]
void BandPlanManager::setActivePlan(const QString& name)
{
    for (const auto& p : m_plans) {
        if (p.name == name) {
            m_activeName = name;
            m_segments   = p.segments;
            m_spots      = p.spots;
            AppSettings::instance().setValue("BandPlanName", name);
            emit planChanged();
            return;
        }
    }
    qCDebug(lcBandPlan) << "setActivePlan: unknown plan" << name;
}

QStringList BandPlanManager::availablePlans() const
{
    QStringList names;
    names.reserve(m_plans.size());
    for (const auto& p : m_plans) {
        names << p.name;
    }
    return names;
}

// From AetherSDR BandPlanManager.cpp:65-107 [@0cd4559]
bool BandPlanManager::loadPlanFromJson(const QString& path, PlanData& out)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        qCWarning(lcBandPlan) << "open failed:" << path;
        return false;
    }

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError) {
        qCWarning(lcBandPlan) << "JSON parse error in" << path << err.errorString();
        return false;
    }

    const QJsonObject root = doc.object();
    out.name = root.value("name").toString();
    if (out.name.isEmpty()) {
        qCWarning(lcBandPlan) << "missing 'name' in" << path;
        return false;
    }

    const auto segs = root.value("segments").toArray();
    for (const auto& val : segs) {
        const QJsonObject obj = val.toObject();
        BandSegment seg;
        seg.lowMhz  = obj.value("low").toDouble();
        seg.highMhz = obj.value("high").toDouble();
        seg.label   = obj.value("label").toString();
        seg.license = obj.value("license").toString();
        seg.color   = QColor(obj.value("color").toString());
        if (seg.lowMhz < seg.highMhz && seg.color.isValid()) {
            out.segments.append(seg);
        }
    }

    const auto spotArr = root.value("spots").toArray();
    for (const auto& val : spotArr) {
        const QJsonObject obj = val.toObject();
        BandSpot spot;
        spot.freqMhz = obj.value("freq").toDouble();
        spot.label   = obj.value("label").toString();
        if (spot.freqMhz > 0.0) {
            out.spots.append(spot);
        }
    }

    return true;
}

}  // namespace NereusSDR
```

Notes vs upstream:
- AetherSDR's port also calls `AppSettings::instance().save()` after `setValue` — NereusSDR's `AppSettings` writes are coalesced via the existing settings-save scheduler, so the explicit `save()` is omitted to match house style. Round-trip persistence is still tested in Task 9's verification matrix.
- `qWarning`/`qDebug` upgraded to a project-style `lcBandPlan` `QLoggingCategory` so the existing log-category checkbox surface picks it up.

- [ ] **Step 3: Register the new sources in the build**

Edit the top-level `CMakeLists.txt`. Find the existing `src/models/AlexController.cpp` entry (or whichever model neighbour exists) and add the two new files alongside, **alphabetically**:

```cmake
    src/models/BandPlan.h
    src/models/BandPlanManager.cpp
    src/models/BandPlanManager.h
```

- [ ] **Step 4: Build the test target**

```bash
cmake --build build --target tst_bandplan_manager -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: **builds clean**.

- [ ] **Step 5: Run the test**

```bash
ctest --test-dir build -R tst_bandplan_manager --output-on-failure
```

Expected: **9/9 tests PASS** (green bar).

- [ ] **Step 6: Verify byte-identical JSON values across regions (sanity)**

```bash
for f in resources/bandplans/*.json; do
    diff -q "$f" "/Users/j.j.boyd/AetherSDR/$f" || echo "  ✗ drift in $f"
done
```

Expected: no drift output. (If the AetherSDR clone path differs, substitute.)

- [ ] **Step 7: Commit**

```bash
git add src/models/BandPlanManager.h src/models/BandPlanManager.cpp CMakeLists.txt
git commit -m "feat(bandplan): port BandPlanManager from AetherSDR

Loads :/bandplans/*.json at startup, owns active region, persists
selection under AppSettings[\"BandPlanName\"]. Verbatim port of
AetherSDR src/models/BandPlanManager.{h,cpp} @0cd4559 with NereusSDR
namespace + value types from src/models/BandPlan.h.

tst_bandplan_manager: 9/9 PASS."
```

---

## Task 4: Wire `BandPlanManager` into `RadioModel`

`RadioModel` owns the manager (single instance per app, lifetime tied to RadioModel) and exposes a const accessor mirroring `alexController()`. `init()` calls `loadPlans()` once AppSettings is ready.

**Files:**
- Modify: `src/models/RadioModel.h`
- Modify: `src/models/RadioModel.cpp`

- [ ] **Step 1: Add the member + accessor in `RadioModel.h`**

Find the existing `AlexController m_alexController;` member (around line 400) and the `alexController()` accessor (around line 167). Add a parallel `BandPlanManager` member + accessor right after, keeping the same const/mutable/non-mutable shape used elsewhere.

Add the include near the top of `RadioModel.h` (alongside `AlexController.h`):

```cpp
#include "BandPlanManager.h"
```

Add the accessor in the public section, right after `alexController()`:

```cpp
    const BandPlanManager& bandPlanManager()        const { return m_bandPlanManager; }
    BandPlanManager&       bandPlanManagerMutable()       { return m_bandPlanManager; }
```

Add the member in the private section, right after `m_alexController`:

```cpp
    BandPlanManager m_bandPlanManager;
```

(`BandPlanManager` is a QObject; default-constructed with no parent here is fine — `RadioModel` owns it and the QObject parent chain runs through `RadioModel`'s deleter.)

- [ ] **Step 2: Initialize in `RadioModel::init()`**

In `RadioModel.cpp`, find the existing `m_alexController.load(...)` call (or wherever sub-models are initialized in `init()`). Add right after:

```cpp
    m_bandPlanManager.loadPlans();
```

- [ ] **Step 3: Build**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: clean build.

- [ ] **Step 4: Smoke-test that the app loads bandplans on launch**

Run the app once, then check the log:

```bash
./build/NereusSDR &
sleep 3
pkill NereusSDR
grep -i 'bandplan' ~/.config/NereusSDR/logs/*.log 2>/dev/null | head
```

Expected: no `bandplan` warnings (silent success). If there's a `JSON parse error` or `open failed`, fix the JSON or the qrc registration before continuing.

- [ ] **Step 5: Commit**

```bash
git add src/models/RadioModel.h src/models/RadioModel.cpp
git commit -m "feat(radiomodel): own BandPlanManager + load on init

Mirrors m_alexController / m_ocMatrix ownership pattern. loadPlans()
fires once during RadioModel::init() after AppSettings is ready, so
the saved BandPlanName round-trips on relaunch."
```

---

## Task 5: Add `BandPlanManager*` + visibility/font-size knobs to `SpectrumWidget`

Non-owning pointer to the model, plus a `m_bandPlanFontSize` int (matches AetherSDR convention: 0 = off, otherwise pixel-font label size). AppSettings round-trip uses the AetherSDR keys `"BandPlanFontSize"` (not the design-doc paraphrase `BandplanOverlayEnabled`).

**Files:**
- Modify: `src/gui/SpectrumWidget.h`
- Modify: `src/gui/SpectrumWidget.cpp`

- [ ] **Step 1: Add the forward declaration + members + setter API to `SpectrumWidget.h`**

Near the top of the class, alongside other forward decls:

```cpp
namespace NereusSDR { class BandPlanManager; }
```

In the public setter section (alongside `setDbmScaleVisible`), add:

```cpp
    // Bandplan overlay (Phase 3G RX Epic sub-epic D)
    void setBandPlanManager(NereusSDR::BandPlanManager* mgr);
    void setBandPlanFontSize(int pt);             // 0 = off
    int  bandPlanFontSize() const { return m_bandPlanFontSize; }
    bool bandPlanVisible() const { return m_bandPlanFontSize > 0; }
```

Add the `drawBandPlan` private decl alongside `drawDbmScale`:

```cpp
    void drawBandPlan(QPainter& p, const QRect& specRect);
```

Add the members in the private state block, alongside `m_dbmScaleVisible`:

```cpp
    NereusSDR::BandPlanManager* m_bandPlanMgr{nullptr};   // non-owning
    int                          m_bandPlanFontSize{6};   // 0 = off; AetherSDR default
```

- [ ] **Step 2: Add setter implementations in `SpectrumWidget.cpp`**

Near `setDbmScaleVisible()`:

```cpp
// From AetherSDR SpectrumWidget.cpp:364-368 [@0cd4559]
void SpectrumWidget::setBandPlanManager(NereusSDR::BandPlanManager* mgr)
{
    if (m_bandPlanMgr == mgr) { return; }
    if (m_bandPlanMgr) {
        disconnect(m_bandPlanMgr, nullptr, this, nullptr);
    }
    m_bandPlanMgr = mgr;
    if (mgr) {
        connect(mgr, &NereusSDR::BandPlanManager::planChanged,
                this, [this]() {
                    markOverlayDirty();
                    update();
                });
    }
    markOverlayDirty();
    update();
}

void SpectrumWidget::setBandPlanFontSize(int pt)
{
    pt = std::clamp(pt, 0, 16);
    if (m_bandPlanFontSize == pt) { return; }
    m_bandPlanFontSize = pt;
    markOverlayDirty();
    update();
}
```

Add the include at the top of `SpectrumWidget.cpp`:

```cpp
#include "models/BandPlanManager.h"
```

- [ ] **Step 3: Add AppSettings load/save**

Find the existing `m_dbmScaleVisible = s.value(...)` block (around line 432) and add right after, in the same load block:

```cpp
    m_bandPlanFontSize = s.value(QStringLiteral("BandPlanFontSize"),
                                 QStringLiteral("6")).toInt();
```

Find the existing `s.setValue(... DisplayDbmScaleVisible ...)` block (around line 518) and add right after, in the same save block:

```cpp
    s.setValue(QStringLiteral("BandPlanFontSize"),
               QString::number(m_bandPlanFontSize));
```

Notes:
- `BandPlanFontSize` is **global** (no `m_panIndex` suffix), matching the design's "one region app-wide" decision and AetherSDR's behaviour. The `BandPlanName` key is owned by `BandPlanManager` itself (Task 3).
- Use `QStringLiteral` for keys to match the surrounding house style.

- [ ] **Step 4: Build (no painter integration yet — `drawBandPlan` is unused)**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: clean build with one **unused-private-method warning** for `drawBandPlan`. That's fine — Task 6 wires the call site.

If the build is configured `-Werror`, the warning fails the build. In that case, either silence it inline with `[[maybe_unused]]` on the decl for this commit, or merge Tasks 5 + 6 into one commit. House-style preference: merge.

- [ ] **Step 5: Commit**

```bash
git add src/gui/SpectrumWidget.h src/gui/SpectrumWidget.cpp
git commit -m "feat(spectrum): add BandPlanManager hookup + font-size knob

setBandPlanManager() takes a non-owning pointer (RadioModel owns it);
planChanged signal triggers overlay invalidation. setBandPlanFontSize
clamps 0..16 with 0 = off, matching AetherSDR's convention. AppSettings
key BandPlanFontSize round-trips. drawBandPlan() decl + impl land in
the next commit; this one wires the plumbing only."
```

---

## Task 6: Port `drawBandPlan()` and integrate into both paint paths

The actual rendering — bottom-of-spec strip with mode-coloured rectangles + license-class brightness blend + spot-marker dots. Verbatim port of AetherSDR `SpectrumWidget.cpp:4220-4293`.

**Files:**
- Modify: `src/gui/SpectrumWidget.cpp`

- [ ] **Step 1: Port `drawBandPlan()` itself**

Add the implementation alongside `drawDbmScale()` in `SpectrumWidget.cpp` (search for the existing `void SpectrumWidget::drawDbmScale` and add `drawBandPlan` directly after it):

```cpp
// From AetherSDR SpectrumWidget.cpp:4220-4293 [@0cd4559]
void SpectrumWidget::drawBandPlan(QPainter& p, const QRect& specRect)
{
    if (!m_bandPlanMgr || m_bandPlanFontSize <= 0) {
        return;
    }

    const double startMhz = m_centerHz / 1.0e6 - (m_bandwidthHz / 2.0) / 1.0e6;
    const double endMhz   = m_centerHz / 1.0e6 + (m_bandwidthHz / 2.0) / 1.0e6;
    const int    bandH    = m_bandPlanFontSize + 4;
    const int    bandY    = specRect.bottom() - bandH + 1;

    const auto& segments = m_bandPlanMgr->segments();
    for (const auto& seg : segments) {
        if (seg.highMhz <= startMhz || seg.lowMhz >= endMhz) {
            continue;
        }

        const int x1 = freqToX(std::max(seg.lowMhz, startMhz) * 1.0e6);
        const int x2 = freqToX(std::min(seg.highMhz, endMhz)  * 1.0e6);
        if (x2 <= x1) {
            continue;
        }

        // License-class brightness blend: more-restrictive classes paint dimmer
        // so the eye can scan to "where I'm allowed to operate" at a glance.
        // From AetherSDR SpectrumWidget.cpp:4239-4244 [@0cd4559].
        const QString& lic = seg.license;
        float blend = 0.6f;
        if      (lic == "E")              { blend = 0.20f; }
        else if (lic == "E,G")            { blend = 0.40f; }
        else if (lic.contains('T'))       { blend = 0.60f; }
        else if (lic.isEmpty())           { blend = 0.50f; }

        const QColor bg(0x0a, 0x0a, 0x14);
        const QColor fill(
            static_cast<int>(seg.color.red()   * blend + bg.red()   * (1.0f - blend)),
            static_cast<int>(seg.color.green() * blend + bg.green() * (1.0f - blend)),
            static_cast<int>(seg.color.blue()  * blend + bg.blue()  * (1.0f - blend)),
            255);
        p.fillRect(x1, bandY, x2 - x1, bandH, fill);

        // Separator line at left edge of each segment.
        p.setPen(QColor(0x0f, 0x0f, 0x1a, 200));
        p.drawLine(x1, bandY, x1, bandY + bandH);

        // Label: mode + lowest license class allowed (only if there's room).
        if (x2 - x1 > 20) {
            QFont f = p.font();
            f.setPointSize(m_bandPlanFontSize);
            f.setBold(true);
            p.setFont(f);

            QString lowestClass;
            if      (lic.contains('T'))   { lowestClass = QStringLiteral("Tech"); }
            else if (lic.contains('G'))   { lowestClass = QStringLiteral("General"); }
            else if (lic == "E")          { lowestClass = QStringLiteral("Extra"); }

            QString label = seg.label;
            if (!lowestClass.isEmpty() && x2 - x1 > 60) {
                label = QStringLiteral("%1 %2").arg(seg.label, lowestClass);
            }

            p.setPen(Qt::white);
            p.drawText(QRect(x1, bandY, x2 - x1, bandH), Qt::AlignCenter, label);
        }
    }

    // Spot markers (white dots for digital calling frequencies, etc.).
    // From AetherSDR SpectrumWidget.cpp:4282-4292 [@0cd4559].
    const auto& spots = m_bandPlanMgr->spots();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);
    p.setBrush(Qt::white);
    for (const auto& spot : spots) {
        if (spot.freqMhz < startMhz || spot.freqMhz > endMhz) {
            continue;
        }
        const int sx = freqToX(spot.freqMhz * 1.0e6);
        p.drawEllipse(QPoint(sx, bandY + bandH / 2), 4, 4);
    }
    p.setRenderHint(QPainter::Antialiasing, false);
}
```

Notes vs upstream:
- AetherSDR uses `m_centerMhz` / `m_bandwidthMhz` and `mhzToX()`. NereusSDR uses Hz-native `m_centerHz` / `m_bandwidthHz` and `freqToX()`. Conversion is `* 1e6` at the boundary; arithmetic stays in MHz internally to keep the JSON values readable.
- Keep `'T'` / `'G'` as `QChar` (not the AetherSDR `"T"` / `"G"` `QString::contains`) — slightly cheaper, unambiguous since license codes are ASCII.

- [ ] **Step 2: Hook the call into the QPainter path**

Find the existing `drawDbmScale(p, QRect(0, 0, w, specH));` call in `paintEvent` (around line 1120). Add right after, **before** the `drawCursorInfo` call:

```cpp
    drawBandPlan(p, specRect);
```

(`specRect` here is the spectrum-only rect — already correctly bounded to the area above the freq scale and to the left of the dBm strip. Bandplan paints inside it at the bottom.)

- [ ] **Step 3: Hook the call into the GPU static-overlay path**

Find the existing `drawDbmScale(p, QRect(0, 0, w, specH));` call inside the `if (m_overlayStaticDirty) { ... }` block (around line 2700). Add right after, before the `p.fillRect(0, specH, w, kDividerH, ...)` divider line:

```cpp
            drawBandPlan(p, specRect);
```

- [ ] **Step 4: Ensure overlay-cache invalidation triggers on freq/bandwidth changes**

The static overlay texture caches grid + dbm-scale + freq-scale + (now) bandplan. Bandplan depends on `m_centerHz` and `m_bandwidthHz`, which already trigger `markOverlayDirty()` via the existing tuning paths. Verify by grepping:

```bash
rg -n 'markOverlayDirty\(\)' src/gui/SpectrumWidget.cpp | head -20
```

You should see calls in `setFrequency`, `setFrequencyRange`, `setCenterFrequency`, `setVfoFrequency`, etc. If `setBandwidth` (or whatever bandwidth setter is used) is missing a `markOverlayDirty()`, add it — bandplan won't redraw on zoom otherwise. (Note: bandwidth changes during a live drag may already invalidate via a different path; check before patching.)

- [ ] **Step 5: Build + smoke-test visually**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
pkill -f NereusSDR; sleep 1
./build/NereusSDR &
```

Expected: app launches; if a radio is connected (or you switch to a known band like 20m), a thin coloured strip with "CW" / "DATA" / "PHONE" labels appears at the bottom of the spectrum. If you don't see anything yet, check:
- `RadioModel::init()` actually called `m_bandPlanManager.loadPlans()` (Task 4)
- MainWindow set the manager on the SpectrumWidget — that comes in Task 7. Skip ahead to wire it temporarily for the smoke test, then come back here.

(If you're block-merging Tasks 5 + 6 + 7, do them all before this smoke test.)

- [ ] **Step 6: Commit**

```bash
git add src/gui/SpectrumWidget.cpp
git commit -m "feat(spectrum): port drawBandPlan from AetherSDR

Mode-coloured strip at the bottom of the spectrum rect with license-
class brightness blending + spot-marker dots. Verbatim port of
AetherSDR SpectrumWidget.cpp:4220-4293 @0cd4559, adapted to NereusSDR
Hz-native freqToX(). Drawn into both QPainter and GPU static-overlay
paths to match the dBm scale strip's dual-path pattern."
```

---

## Task 7: Wire `RadioModel::bandPlanManager()` → every `SpectrumWidget`

One-line setter call wherever `MainWindow` already wires panadapter-scoped models into the SpectrumWidget(s).

**Files:**
- Modify: `src/gui/MainWindow.cpp`

- [ ] **Step 1: Find the existing wiring site**

```bash
rg -n 'spectrumWidget\(\)|new SpectrumWidget|setOcMatrix|setSliceModel' src/gui/MainWindow.cpp | head -20
```

You're looking for the section that hands the SpectrumWidget its sub-model dependencies. Some are likely set in `MainWindow::wirePanadapter()` or `setRadioModel()`.

- [ ] **Step 2: Add the setter call**

In the same block where existing model wires happen, add:

```cpp
    m_spectrumWidget->setBandPlanManager(&m_radioModel->bandPlanManagerMutable());
```

(Adjust the member-pointer name to match local conventions — could be `m_radioModel`, `radioModel()`, etc. The pattern follows however `setOcMatrix` / `alexController()` already wire.)

- [ ] **Step 3: Build + smoke-test**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
pkill -f NereusSDR; sleep 1
./build/NereusSDR &
```

Expected: bandplan strip visible at the bottom of the spectrum. Tune across 7.000–7.300 MHz on 40m (or any band with ARRL-US sub-segments) — the segment labels (CW / DATA / PHONE) should change as you sweep.

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -m "feat(mainwindow): wire BandPlanManager → SpectrumWidget

Bandplan strip is now live on launch. RadioModel owns the manager;
MainWindow hands a non-owning pointer to the SpectrumWidget at the
same panadapter-wire site as the OcMatrix / SliceModel hookup."
```

---

## Task 8: Setup → Display UI — region dropdown + font-size control

A new "Bandplan Overlay" group box on the existing Setup → Display → Spectrum Defaults page. Two controls: `QComboBox` for region (`availablePlans()`-driven) and a `QSpinBox` (or labelled slider — match house style) for label size with `0 = off` semantics.

**Files:**
- Modify: `src/gui/setup/DisplaySetupPages.h`
- Modify: `src/gui/setup/DisplaySetupPages.cpp`

- [ ] **Step 1: Identify the existing page builder**

```bash
rg -n 'Spectrum Defaults|SpectrumDefaultsPage|buildSpectrum' src/gui/setup/DisplaySetupPages.h src/gui/setup/DisplaySetupPages.cpp | head -10
```

The page should already host the dBm strip toggle (`DisplayDbmScaleVisible`) — bandplan UI lands in the same page.

- [ ] **Step 2: Add the bandplan group box**

In the `SpectrumDefaultsPage` build method (after the existing dBm strip toggle), insert:

```cpp
    // Bandplan Overlay (Phase 3G RX Epic sub-epic D)
    auto* bandplanGroup = new QGroupBox(tr("Bandplan Overlay"), this);
    auto* bandplanForm  = new QFormLayout(bandplanGroup);

    auto* regionCombo = new QComboBox(bandplanGroup);
    if (m_radioModel) {
        const auto& mgr = m_radioModel->bandPlanManager();
        regionCombo->addItems(mgr.availablePlans());
        regionCombo->setCurrentText(mgr.activePlanName());
    }
    connect(regionCombo, &QComboBox::currentTextChanged, this,
            [this](const QString& name) {
                if (m_radioModel) {
                    m_radioModel->bandPlanManagerMutable().setActivePlan(name);
                }
            });
    bandplanForm->addRow(tr("Region"), regionCombo);

    auto* fontSizeSpin = new QSpinBox(bandplanGroup);
    fontSizeSpin->setRange(0, 16);
    fontSizeSpin->setSuffix(tr(" pt"));
    fontSizeSpin->setSpecialValueText(tr("Off"));
    fontSizeSpin->setToolTip(
        tr("Label font size in points. 0 hides the bandplan strip entirely."));
    if (m_spectrumWidget) {
        fontSizeSpin->setValue(m_spectrumWidget->bandPlanFontSize());
    }
    connect(fontSizeSpin, qOverload<int>(&QSpinBox::valueChanged), this,
            [this](int pt) {
                if (m_spectrumWidget) {
                    m_spectrumWidget->setBandPlanFontSize(pt);
                }
            });
    bandplanForm->addRow(tr("Label size"), fontSizeSpin);

    pageLayout->addWidget(bandplanGroup);
```

(Adjust the page-layout variable name and the model accessors to match the surrounding code. `m_radioModel` and `m_spectrumWidget` are placeholders — the real names depend on how `DisplaySetupPages` already pulls these in for the dBm strip toggle.)

- [ ] **Step 3: Add necessary includes**

At the top of `DisplaySetupPages.cpp`:

```cpp
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSpinBox>

#include "models/BandPlanManager.h"
#include "models/RadioModel.h"
#include "gui/SpectrumWidget.h"
```

(Skip any that are already included.)

- [ ] **Step 4: Build + smoke-test**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
pkill -f NereusSDR; sleep 1
./build/NereusSDR &
```

Open Setup → Display → Spectrum Defaults. The new "Bandplan Overlay" group box should appear with:
- Region dropdown listing all 5 regions, defaulting to "ARRL (US)" on first launch
- Label size spinbox showing "6 pt" (default), with `0` rendering as "Off"

Switch the region — segment labels on the spectrum should change immediately. Set label size to `0` — strip disappears. Quit + relaunch — region + label size restore.

- [ ] **Step 5: Commit**

```bash
git add src/gui/setup/DisplaySetupPages.h src/gui/setup/DisplaySetupPages.cpp
git commit -m "feat(setup-display): bandplan region + label-size controls

Adds a 'Bandplan Overlay' group box to Setup → Display → Spectrum
Defaults. Region dropdown drives BandPlanManager::setActivePlan;
label-size spinbox drives SpectrumWidget::setBandPlanFontSize with
0=Off semantics matching AetherSDR. Both round-trip through
AppSettings on quit/relaunch."
```

---

## Task 9: Manual verification matrix

Capture the user-visible behaviour as a 12-row matrix that the user (or whoever runs the build) walks through before the PR opens.

**Files:**
- Create: `docs/architecture/phase3g-rx-epic-d-verification/README.md`

- [ ] **Step 1: Write the matrix**

Create the file with content:

```markdown
# Phase 3G RX Epic Sub-epic D — Bandplan Overlay Verification

12-row manual verification matrix for the bandplan port. Walk every row
on a connected radio (or the demo I/Q feed) before opening the PR.

| # | Action | Expected | Pass? |
|---|---|---|---|
| 1 | Launch app fresh (delete `~/.config/NereusSDR/NereusSDR.settings`), connect radio | Bandplan strip visible at bottom of spectrum on first launch; default region "ARRL (US)" | |
| 2 | Tune to 7.000 MHz on 40m | "CW" segment label visible at left edge of strip | |
| 3 | Pan to 7.150 MHz | Label transitions through "DATA" → "PHONE" as the segment boundaries cross center | |
| 4 | Tune to 14.225 MHz on 20m | "PHONE" + "General" suffix visible (license-class label kicks in for segments > 60 px wide) | |
| 5 | Hover near 14.074 MHz | White spot marker dot visible on the strip at the FT8 calling frequency | |
| 6 | Setup → Display → Bandplan → Region = "IARU Region 1" | Strip relabels live; segment count + boundaries change to R1 plan | |
| 7 | Setup → Display → Bandplan → Region = "IARU Region 2" / "IARU Region 3" / "RAC (Canada)" | All four non-default regions load and render | |
| 8 | Setup → Display → Bandplan → Label size = 0 (Off) | Strip disappears entirely; spectrum extends to bottom of widget | |
| 9 | Setup → Display → Bandplan → Label size = 12 pt | Strip thickens; labels render larger | |
| 10 | Quit, relaunch | Region + label size restored; same view as before quit | |
| 11 | Resize the SpectrumWidget very narrow (~200 px wide) | Labels disappear from segments < 20 px; strip itself still paints; no crash | |
| 12 | Switch panadapter colour scheme (overlay menu) | Bandplan strip background stays at fixed dark blue (`#0a0a14`); segment colours unchanged | |

## Regressions to watch

- dBm scale strip: still on right edge, behaviour unchanged
- Filter passband shading: unchanged
- VFO marker + cursor info: unchanged
- Off-screen slice indicator: unchanged
- FPS overlay: unchanged

## Deferred (explicit non-goals for this sub-epic)

- **Locale-driven default region**: design §D mentions auto-selecting from user locale; AetherSDR hardcodes "ARRL (US)" and we matched upstream. Revisit if user demand justifies the platform-specific code.
- **User-editable bandplans**: design §D explicitly defers — bundled JSONs only.
- **Custom colour overrides**: per-mode colour customisation deferred; segments use the JSON-baked colours.

## Upstream reference

Ported from AetherSDR `main@0cd4559` (2026-04-21):
- [`src/models/BandPlanManager.{h,cpp}`](https://github.com/ten9876/AetherSDR/blob/0cd4559/src/models/BandPlanManager.cpp)
- [`src/gui/SpectrumWidget.cpp:4220-4293`](https://github.com/ten9876/AetherSDR/blob/0cd4559/src/gui/SpectrumWidget.cpp) (`drawBandPlan`)
- [`resources/bandplans/*.json`](https://github.com/ten9876/AetherSDR/tree/0cd4559/resources/bandplans) (5 region files)
```

- [ ] **Step 2: Commit**

```bash
git add docs/architecture/phase3g-rx-epic-d-verification/
git commit -m "docs(verify): 12-row manual matrix for bandplan port"
```

---

## Task 10: Final gate — full test suite + pre-commit hooks + post-port inventory

- [ ] **Step 1: Build clean**

```bash
cmake --build build -j $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

Expected: clean build, no new warnings.

- [ ] **Step 2: Run the full ctest suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: **all green**, including the new `tst_bandplan_manager` (9 cases).

- [ ] **Step 3: Verify pre-commit hooks were happy on every commit**

```bash
git log --oneline -10
```

You should see roughly 8 commits from Tasks 1–9 on top of the starting branch. Every commit should have passed the `verify-thetis-headers.py` / `verify-inline-tag-preservation.py` / `check-new-ports.py` / `verify-inline-cites.py` hook chain. (The bandplan port has no Thetis cites — this sub-epic is AetherSDR-only — but the hooks still validate the new files don't claim a Thetis origin they don't have.)

- [ ] **Step 4: Confirm provenance is registered**

The new files (`BandPlanManager.{h,cpp}`, `BandPlan.h`) are AetherSDR ports. Add them to `docs/attribution/THETIS-PROVENANCE.md` under the AetherSDR section if a separate provenance doc tracks AetherSDR ports — otherwise the file-header comments are the canonical record.

```bash
grep -l "BandPlan" docs/attribution/*.md 2>/dev/null
```

If no hits and the project does track AetherSDR provenance separately, add a row. If the project tracks provenance via per-file headers only (which is the current state for other AetherSDR ports like `SpectralNR`, `RNNoiseFilter`), no further action needed.

- [ ] **Step 5: Verify the AetherSDR JSON resources stayed byte-identical**

```bash
for f in resources/bandplans/*.json; do
    diff -q "$f" "/Users/j.j.boyd/AetherSDR/$f" || echo "  ✗ drift in $f"
done
```

Expected: no drift output.

- [ ] **Step 6: Walk the verification matrix**

Per `docs/architecture/phase3g-rx-epic-d-verification/README.md` — every row must pass before the PR opens. Hand off to the user if you can't drive a real radio yourself.

---

## Completion

When all tasks above are committed and the verification matrix is 12/12 PASS:

1. The sub-epic branch is ready for PR against `main`.
2. PR title: `feat(spectrum): port AetherSDR bandplan overlay (Phase 3G RX Epic, sub-epic D)`
3. PR body: link the verification README, paste the matrix result, attach before/after screenshots showing the strip visible on 40m and 20m.
4. **Do NOT open the PR via `gh` without explicit user approval** (per `feedback_no_public_posts_without_review` memory).

Sub-epic E (waterfall scrollback) is the next unit of work; its plan is written separately via `writing-plans` after sub-epic D merges.

---

## Self-Review (post-write)

**1. Spec coverage:**
- Design §D: BandSegment + region switcher + 5 bundled JSONs + per-region default + spectrum overlay — all covered (Tasks 1, 2, 3, 6, 8).
- Design §D AppSettings keys (`BandplanRegion`, `BandplanOverlayEnabled`) — explicitly diverged to AetherSDR keys (`BandPlanName`, `BandPlanFontSize`); divergence documented in §"Authoring-time decisions".
- Design §D "alpha 0.25 beneath trace" geometry — explicitly diverged to AetherSDR's bottom-strip; divergence documented.
- Design §D locale-driven default — explicitly deferred; documented in verification matrix's "Deferred" section.

**2. Placeholder scan:** No "TBD" / "TODO" / "Add appropriate error handling" / "Similar to Task N" markers.

**3. Type consistency:** `BandSegment` / `BandSpot` defined in Task 2, used unchanged in Tasks 3, 6. `BandPlanManager` interface in Task 3 (`loadPlans`, `setActivePlan`, `activePlanName`, `segments`, `spots`, `availablePlans`, `planChanged`) used unchanged in Tasks 4, 6, 8. `setBandPlanManager` / `setBandPlanFontSize` / `bandPlanFontSize` declared in Task 5, used unchanged in Tasks 6, 7, 8.

**4. Build sequence:** Tasks 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 → 9 → 10. Task 2 introduces a deliberate red bar (Step 4 expects build failure); Task 3 turns it green. Task 5 introduces a `[[maybe_unused]]`-style warning that Task 6 retires — flagged inline with merge-suggestion if `-Werror`.
