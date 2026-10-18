# Phase 3-UI: Full UI Skeleton — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the complete NereusSDR UI surface with NYI shells, wire all Tier 1 controls, and give users visibility into the full feature set.

**Architecture:** Three-layer UI (left overlay, right container panel, setup dialog) sharing RadioModel/SliceModel as single source of truth. Hybrid container model extends existing ContainerManager to accept AppletWidget content alongside MeterWidget/MeterItems. All new widgets follow the dark theme from STYLEGUIDE.md.

**Tech Stack:** Qt6 Widgets, C++20, existing ContainerManager/MeterWidget infrastructure, RadioModel/SliceModel signals.

**Spec:** `docs/superpowers/specs/2026-04-11-reconciled-ui-implementation-design.md`

---

## File Structure

### New Files

```
src/gui/applets/
├── AppletWidget.h/.cpp              ← Base class for all applets
├── NyiOverlay.h/.cpp                ← Reusable NYI badge/overlay widget
├── RxApplet.h/.cpp                  ← Per-slice RX controls (Tier 1 wired)
├── TxApplet.h/.cpp                  ← TX power/keying (NYI shell)
├── PhoneCwApplet.h/.cpp             ← Phone/CW stacked (NYI shell)
├── EqApplet.h/.cpp                  ← 10-band EQ (NYI shell)
├── FmApplet.h/.cpp                  ← FM controls (NYI shell)
├── DigitalApplet.h/.cpp             ← VAC/DAX routing (NYI shell)
├── PureSignalApplet.h/.cpp          ← PS calibration (NYI shell)
├── DiversityApplet.h/.cpp           ← Diversity controls (NYI shell)
├── CwxApplet.h/.cpp                 ← CW macros (NYI shell)
├── DvkApplet.h/.cpp                 ← Voice keyer (NYI shell)
├── CatApplet.h/.cpp                 ← CAT/TCI status (NYI shell)
└── TunerApplet.h/.cpp               ← Aries ATU (NYI shell)

src/gui/
├── SpectrumOverlayPanel.h/.cpp      ← Left overlay button strip + flyouts
├── SetupDialog.h/.cpp               ← Settings dialog with tree nav
├── SetupPage.h/.cpp                 ← Base class for setup pages
└── setup/                           ← Setup sub-pages (all NYI shells)
    ├── GeneralPage.h/.cpp
    ├── HardwarePage.h/.cpp
    ├── AudioPage.h/.cpp
    ├── DspPage.h/.cpp
    ├── DisplayPage.h/.cpp
    ├── TransmitPage.h/.cpp
    ├── AppearancePage.h/.cpp
    ├── CatNetworkPage.h/.cpp
    ├── KeyboardPage.h/.cpp
    └── DiagnosticsPage.h/.cpp
```

### Modified Files

```
src/gui/MainWindow.h/.cpp            ← Menu bar, status bar, overlay + applet wiring
src/gui/containers/ContainerWidget.h/.cpp  ← Accept AppletWidget content
CMakeLists.txt                        ← Add all new source files
```

---

## Task 1: AppletWidget Base Class

**Files:**
- Create: `src/gui/applets/AppletWidget.h`
- Create: `src/gui/applets/AppletWidget.cpp`
- Modify: `CMakeLists.txt` (GUI_SOURCES section, ~line 190)

- [ ] **Step 1: Create AppletWidget header**

```cpp
// src/gui/applets/AppletWidget.h
#pragma once

#include <QWidget>
#include <QIcon>

namespace NereusSDR {

class RadioModel;

class AppletWidget : public QWidget {
    Q_OBJECT

public:
    explicit AppletWidget(RadioModel* model, QWidget* parent = nullptr);
    ~AppletWidget() override = default;

    virtual QString appletId() const = 0;
    virtual QString appletTitle() const = 0;
    virtual QIcon appletIcon() const;
    virtual void syncFromModel() = 0;

protected:
    RadioModel* m_model = nullptr;
    bool m_updatingFromModel = false;
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create AppletWidget implementation**

```cpp
// src/gui/applets/AppletWidget.cpp
#include "AppletWidget.h"

namespace NereusSDR {

AppletWidget::AppletWidget(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    // Dark theme base styling — applets inherit this
    setStyleSheet(QStringLiteral(
        "QLabel { color: #c8d8e8; font-size: 11px; }"
        "QComboBox {"
        "  background: #1a2a3a; color: #c8d8e8;"
        "  border: 1px solid #205070; border-radius: 3px;"
        "  padding: 2px 6px; font-size: 10px;"
        "}"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView {"
        "  background: #1a2a3a; color: #c8d8e8;"
        "  selection-background-color: #00b4d8;"
        "}"
        "QPushButton {"
        "  background: #1a2a3a; color: #c8d8e8;"
        "  border: 1px solid #205070; border-radius: 3px;"
        "  padding: 2px 8px; font-size: 10px; font-weight: bold;"
        "}"
        "QPushButton:hover { background: #203040; }"
        "QPushButton:checked { background: #006040; color: #00ff88; }"
        "QSlider::groove:horizontal { background: #203040; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal {"
        "  background: #00b4d8; width: 10px; height: 10px;"
        "  margin: -3px 0; border-radius: 5px;"
        "}"));
}

QIcon AppletWidget::appletIcon() const
{
    return QIcon();
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt**

In CMakeLists.txt, find the `set(GUI_SOURCES` block (~line 190) and add:

```cmake
    src/gui/applets/AppletWidget.cpp
```

- [ ] **Step 4: Build and verify**

Run:
```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build -j$(nproc)
```
Expected: clean compile, no warnings from new files.

- [ ] **Step 5: Commit**

```bash
git add src/gui/applets/AppletWidget.h src/gui/applets/AppletWidget.cpp CMakeLists.txt
git commit -m "Add AppletWidget base class for container applets"
```

---

## Task 2: NyiOverlay Utility Widget

Reusable widget for NYI badge overlay on greyed-out controls. Used by all NYI applet shells and setup pages.

**Files:**
- Create: `src/gui/applets/NyiOverlay.h`
- Create: `src/gui/applets/NyiOverlay.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create NyiOverlay header**

```cpp
// src/gui/applets/NyiOverlay.h
#pragma once

#include <QLabel>

namespace NereusSDR {

// Small badge widget showing "NYI" with a tooltip indicating which phase enables it.
// Usage: auto* badge = new NyiOverlay("Phase 3I-1", parentWidget);
//        badge->attachTo(someControl);  // positions top-right of control
class NyiOverlay : public QLabel {
    Q_OBJECT

public:
    explicit NyiOverlay(const QString& phaseHint, QWidget* parent = nullptr);

    // Position this badge at the top-right corner of the target widget.
    // Call after layout is set so geometry is valid.
    void attachTo(QWidget* target);

    // Convenience: disable a widget and attach an NYI badge to it.
    // Returns the badge so caller can store it if needed.
    static NyiOverlay* markNyi(QWidget* target, const QString& phaseHint);
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create NyiOverlay implementation**

```cpp
// src/gui/applets/NyiOverlay.cpp
#include "NyiOverlay.h"
#include <QWidget>

namespace NereusSDR {

NyiOverlay::NyiOverlay(const QString& phaseHint, QWidget* parent)
    : QLabel(QStringLiteral("NYI"), parent)
{
    setStyleSheet(QStringLiteral(
        "QLabel {"
        "  background: #3a2a00; color: #ffb800;"
        "  border: 1px solid #604000; border-radius: 2px;"
        "  padding: 0px 3px; font-size: 8px; font-weight: bold;"
        "}"));
    setToolTip(QStringLiteral("Not Yet Implemented — Available in %1").arg(phaseHint));
    setFixedSize(fontMetrics().horizontalAdvance(QStringLiteral("NYI")) + 8, 14);
    raise();
}

void NyiOverlay::attachTo(QWidget* target)
{
    if (!target) { return; }
    setParent(target->parentWidget());
    QPoint topRight = target->geometry().topRight();
    move(topRight.x() - width() + 2, topRight.y() - 2);
    show();
    raise();
}

NyiOverlay* NyiOverlay::markNyi(QWidget* target, const QString& phaseHint)
{
    if (!target) { return nullptr; }
    target->setEnabled(false);
    auto* badge = new NyiOverlay(phaseHint, target->parentWidget());
    badge->attachTo(target);
    return badge;
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add to `GUI_SOURCES`:
```cmake
    src/gui/applets/NyiOverlay.cpp
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build -j$(nproc)
```
Expected: clean compile.

- [ ] **Step 5: Commit**

```bash
git add src/gui/applets/NyiOverlay.h src/gui/applets/NyiOverlay.cpp CMakeLists.txt
git commit -m "Add NyiOverlay badge utility for NYI controls"
```

---

## Task 3: RxApplet with Tier 1 Controls

The first functional applet — wires to existing SliceModel/RxChannel for controls that already have backend support.

**Files:**
- Create: `src/gui/applets/RxApplet.h`
- Create: `src/gui/applets/RxApplet.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create RxApplet header**

```cpp
// src/gui/applets/RxApplet.h
#pragma once

#include "AppletWidget.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;

namespace NereusSDR {

class SliceModel;

class RxApplet : public AppletWidget {
    Q_OBJECT

public:
    explicit RxApplet(RadioModel* model, QWidget* parent = nullptr);

    QString appletId() const override { return QStringLiteral("RX"); }
    QString appletTitle() const override { return QStringLiteral("RX Controls"); }
    void syncFromModel() override;

    void setSlice(SliceModel* slice);

private:
    void buildUI();
    void wireSlice();

    SliceModel* m_slice = nullptr;

    // Tier 1 controls (wired)
    QLabel*       m_sliceBadge = nullptr;
    QPushButton*  m_lockBtn = nullptr;
    QComboBox*    m_modeCombo = nullptr;
    QComboBox*    m_agcCombo = nullptr;
    QSlider*      m_agcThreshold = nullptr;
    QSlider*      m_afGain = nullptr;
    QPushButton*  m_muteBtn = nullptr;
    QLabel*       m_filterLabel = nullptr;

    // Tier 2 controls (NYI — built but disabled)
    QSlider*      m_squelchSlider = nullptr;
    QPushButton*  m_squelchBtn = nullptr;
    QSlider*      m_panSlider = nullptr;
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create RxApplet implementation**

```cpp
// src/gui/applets/RxApplet.cpp
#include "RxApplet.h"
#include "NyiOverlay.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QComboBox>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>

namespace NereusSDR {

RxApplet::RxApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent)
{
    buildUI();
}

void RxApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(2);

    // --- Row 1: Slice badge + Lock + Filter width ---
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(4);

    m_sliceBadge = new QLabel(QStringLiteral("A"), this);
    m_sliceBadge->setFixedSize(22, 18);
    m_sliceBadge->setAlignment(Qt::AlignCenter);
    m_sliceBadge->setStyleSheet(QStringLiteral(
        "QLabel { background: #00d4ff; color: #000000;"
        " font-size: 10px; font-weight: bold; border-radius: 3px; }"));
    row1->addWidget(m_sliceBadge);

    m_lockBtn = new QPushButton(QStringLiteral("Lock"), this);
    m_lockBtn->setCheckable(true);
    m_lockBtn->setFixedHeight(18);
    row1->addWidget(m_lockBtn);

    m_filterLabel = new QLabel(QStringLiteral("2.9 kHz"), this);
    m_filterLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #8090a0; font-size: 10px; }"));
    m_filterLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row1->addWidget(m_filterLabel, 1);
    root->addLayout(row1);

    // --- Row 2: Mode combo ---
    auto* row2 = new QHBoxLayout;
    row2->setSpacing(4);
    auto* modeLabel = new QLabel(QStringLiteral("Mode"), this);
    modeLabel->setFixedWidth(36);
    modeLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    row2->addWidget(modeLabel);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->setFixedHeight(22);
    m_modeCombo->addItems({
        QStringLiteral("LSB"), QStringLiteral("USB"), QStringLiteral("DSB"),
        QStringLiteral("CWL"), QStringLiteral("CWU"),
        QStringLiteral("AM"), QStringLiteral("SAM"), QStringLiteral("FM"),
        QStringLiteral("DIGL"), QStringLiteral("DIGU"),
        QStringLiteral("DRM"), QStringLiteral("SPEC")
    });
    row2->addWidget(m_modeCombo, 1);
    root->addLayout(row2);

    // --- Row 3: AGC combo + threshold slider ---
    auto* row3 = new QHBoxLayout;
    row3->setSpacing(4);
    auto* agcLabel = new QLabel(QStringLiteral("AGC"), this);
    agcLabel->setFixedWidth(36);
    agcLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    row3->addWidget(agcLabel);

    m_agcCombo = new QComboBox(this);
    m_agcCombo->setFixedHeight(22);
    m_agcCombo->setFixedWidth(60);
    m_agcCombo->addItems({
        QStringLiteral("Off"), QStringLiteral("Long"), QStringLiteral("Slow"),
        QStringLiteral("Med"), QStringLiteral("Fast"), QStringLiteral("Custom")
    });
    row3->addWidget(m_agcCombo);

    m_agcThreshold = new QSlider(Qt::Horizontal, this);
    m_agcThreshold->setRange(-140, 0);
    m_agcThreshold->setValue(-100);
    m_agcThreshold->setFixedHeight(18);
    m_agcThreshold->setToolTip(QStringLiteral("AGC Threshold"));
    row3->addWidget(m_agcThreshold, 1);
    root->addLayout(row3);

    // --- Row 4: AF Gain slider + Mute ---
    auto* row4 = new QHBoxLayout;
    row4->setSpacing(4);
    auto* afLabel = new QLabel(QStringLiteral("AF"), this);
    afLabel->setFixedWidth(36);
    afLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    row4->addWidget(afLabel);

    m_afGain = new QSlider(Qt::Horizontal, this);
    m_afGain->setRange(0, 100);
    m_afGain->setValue(50);
    m_afGain->setFixedHeight(18);
    m_afGain->setToolTip(QStringLiteral("AF Gain"));
    row4->addWidget(m_afGain, 1);

    m_muteBtn = new QPushButton(QStringLiteral("Mute"), this);
    m_muteBtn->setCheckable(true);
    m_muteBtn->setFixedSize(40, 18);
    row4->addWidget(m_muteBtn);
    root->addLayout(row4);

    // --- Row 5: Squelch (NYI) ---
    auto* row5 = new QHBoxLayout;
    row5->setSpacing(4);
    m_squelchBtn = new QPushButton(QStringLiteral("SQL"), this);
    m_squelchBtn->setCheckable(true);
    m_squelchBtn->setFixedSize(36, 18);
    row5->addWidget(m_squelchBtn);

    m_squelchSlider = new QSlider(Qt::Horizontal, this);
    m_squelchSlider->setRange(0, 160);
    m_squelchSlider->setFixedHeight(18);
    row5->addWidget(m_squelchSlider, 1);
    root->addLayout(row5);
    NyiOverlay::markNyi(m_squelchBtn, QStringLiteral("Phase 3-UI Tier 2"));
    NyiOverlay::markNyi(m_squelchSlider, QStringLiteral("Phase 3-UI Tier 2"));

    // --- Row 6: Audio Pan (NYI) ---
    auto* row6 = new QHBoxLayout;
    row6->setSpacing(4);
    auto* panLabel = new QLabel(QStringLiteral("Pan"), this);
    panLabel->setFixedWidth(36);
    panLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    row6->addWidget(panLabel);
    m_panSlider = new QSlider(Qt::Horizontal, this);
    m_panSlider->setRange(-100, 100);
    m_panSlider->setValue(0);
    m_panSlider->setFixedHeight(18);
    row6->addWidget(m_panSlider, 1);
    root->addLayout(row6);
    NyiOverlay::markNyi(m_panSlider, QStringLiteral("Phase 3-UI Tier 2"));

    root->addStretch();
}

void RxApplet::setSlice(SliceModel* slice)
{
    if (m_slice) {
        disconnect(m_slice, nullptr, this, nullptr);
    }
    m_slice = slice;
    if (m_slice) {
        wireSlice();
        syncFromModel();
    }
}

void RxApplet::wireSlice()
{
    if (!m_slice) { return; }

    // Slice -> applet
    connect(m_slice, &SliceModel::dspModeChanged, this, [this](DSPMode mode) {
        m_updatingFromModel = true;
        m_modeCombo->setCurrentIndex(static_cast<int>(mode));
        m_updatingFromModel = false;
    });

    connect(m_slice, &SliceModel::agcModeChanged, this, [this](AGCMode mode) {
        m_updatingFromModel = true;
        m_agcCombo->setCurrentIndex(static_cast<int>(mode));
        m_updatingFromModel = false;
    });

    connect(m_slice, &SliceModel::afGainChanged, this, [this](int gain) {
        m_updatingFromModel = true;
        m_afGain->setValue(gain);
        m_updatingFromModel = false;
    });

    connect(m_slice, &SliceModel::rfGainChanged, this, [this](int gain) {
        m_updatingFromModel = true;
        m_agcThreshold->setValue(gain);
        m_updatingFromModel = false;
    });

    connect(m_slice, &SliceModel::filterChanged, this, [this](int low, int high) {
        int widthHz = high - low;
        QString label;
        if (widthHz >= 1000) {
            label = QStringLiteral("%1 kHz").arg(widthHz / 1000.0, 0, 'f', 1);
        } else {
            label = QStringLiteral("%1 Hz").arg(widthHz);
        }
        m_filterLabel->setText(label);
    });

    // Applet -> slice
    connect(m_modeCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updatingFromModel || !m_slice) { return; }
        m_slice->setDspMode(static_cast<DSPMode>(idx));
    });

    connect(m_agcCombo, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updatingFromModel || !m_slice) { return; }
        m_slice->setAgcMode(static_cast<AGCMode>(idx));
    });

    connect(m_afGain, &QSlider::valueChanged, this, [this](int val) {
        if (m_updatingFromModel || !m_slice) { return; }
        m_slice->setAfGain(val);
    });

    connect(m_agcThreshold, &QSlider::valueChanged, this, [this](int val) {
        if (m_updatingFromModel || !m_slice) { return; }
        m_slice->setRfGain(val);
    });
}

void RxApplet::syncFromModel()
{
    if (!m_slice) { return; }
    m_updatingFromModel = true;
    m_modeCombo->setCurrentIndex(static_cast<int>(m_slice->dspMode()));
    m_agcCombo->setCurrentIndex(static_cast<int>(m_slice->agcMode()));
    m_afGain->setValue(m_slice->afGain());
    m_agcThreshold->setValue(m_slice->rfGain());

    int widthHz = m_slice->filterHigh() - m_slice->filterLow();
    if (widthHz >= 1000) {
        m_filterLabel->setText(QStringLiteral("%1 kHz").arg(widthHz / 1000.0, 0, 'f', 1));
    } else {
        m_filterLabel->setText(QStringLiteral("%1 Hz").arg(widthHz));
    }
    m_updatingFromModel = false;
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add to `GUI_SOURCES`:
```cmake
    src/gui/applets/RxApplet.cpp
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build -j$(nproc)
```
Expected: clean compile. RxApplet not yet visible — wiring comes in Task 6.

- [ ] **Step 5: Commit**

```bash
git add src/gui/applets/RxApplet.h src/gui/applets/RxApplet.cpp CMakeLists.txt
git commit -m "Add RxApplet with Tier 1 controls (mode, AGC, AF gain, filter)"
```

---

## Task 4: TxApplet NYI Shell

**Files:**
- Create: `src/gui/applets/TxApplet.h`
- Create: `src/gui/applets/TxApplet.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create TxApplet header**

```cpp
// src/gui/applets/TxApplet.h
#pragma once

#include "AppletWidget.h"

class QPushButton;
class QSlider;
class QComboBox;
class QLabel;

namespace NereusSDR {

class TxApplet : public AppletWidget {
    Q_OBJECT

public:
    explicit TxApplet(RadioModel* model, QWidget* parent = nullptr);

    QString appletId() const override { return QStringLiteral("TX"); }
    QString appletTitle() const override { return QStringLiteral("TX Controls"); }
    void syncFromModel() override;

private:
    void buildUI();

    QPushButton* m_moxBtn = nullptr;
    QPushButton* m_tuneBtn = nullptr;
    QPushButton* m_atuBtn = nullptr;
    QSlider*     m_powerSlider = nullptr;
    QSlider*     m_tunePowerSlider = nullptr;
    QComboBox*   m_txProfile = nullptr;
    QLabel*      m_powerLabel = nullptr;
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create TxApplet implementation**

```cpp
// src/gui/applets/TxApplet.cpp
#include "TxApplet.h"
#include "NyiOverlay.h"

#include <QPushButton>
#include <QSlider>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace NereusSDR {

TxApplet::TxApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent)
{
    buildUI();
}

void TxApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(2);

    // Title
    auto* title = new QLabel(QStringLiteral("TX Controls"), this);
    title->setStyleSheet(QStringLiteral(
        "QLabel { color: #708090; font-size: 9px; font-weight: bold; }"));
    root->addWidget(title);

    // MOX / TUNE / ATU row
    auto* btnRow = new QHBoxLayout;
    btnRow->setSpacing(4);

    m_moxBtn = new QPushButton(QStringLiteral("MOX"), this);
    m_moxBtn->setCheckable(true);
    m_moxBtn->setFixedHeight(22);
    btnRow->addWidget(m_moxBtn);

    m_tuneBtn = new QPushButton(QStringLiteral("TUNE"), this);
    m_tuneBtn->setCheckable(true);
    m_tuneBtn->setFixedHeight(22);
    btnRow->addWidget(m_tuneBtn);

    m_atuBtn = new QPushButton(QStringLiteral("ATU"), this);
    m_atuBtn->setCheckable(true);
    m_atuBtn->setFixedHeight(22);
    btnRow->addWidget(m_atuBtn);
    root->addLayout(btnRow);

    // RF Power slider
    auto* pwrRow = new QHBoxLayout;
    pwrRow->setSpacing(4);
    auto* pwrLabel = new QLabel(QStringLiteral("PWR"), this);
    pwrLabel->setFixedWidth(36);
    pwrLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    pwrRow->addWidget(pwrLabel);

    m_powerSlider = new QSlider(Qt::Horizontal, this);
    m_powerSlider->setRange(0, 100);
    m_powerSlider->setValue(50);
    m_powerSlider->setFixedHeight(18);
    pwrRow->addWidget(m_powerSlider, 1);

    m_powerLabel = new QLabel(QStringLiteral("50W"), this);
    m_powerLabel->setFixedWidth(30);
    m_powerLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_powerLabel->setStyleSheet(QStringLiteral("QLabel { color: #8090a0; font-size: 9px; }"));
    pwrRow->addWidget(m_powerLabel);
    root->addLayout(pwrRow);

    // Tune Power slider
    auto* tuneRow = new QHBoxLayout;
    tuneRow->setSpacing(4);
    auto* tuneLabel = new QLabel(QStringLiteral("TUN"), this);
    tuneLabel->setFixedWidth(36);
    tuneLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    tuneRow->addWidget(tuneLabel);

    m_tunePowerSlider = new QSlider(Qt::Horizontal, this);
    m_tunePowerSlider->setRange(0, 100);
    m_tunePowerSlider->setValue(10);
    m_tunePowerSlider->setFixedHeight(18);
    tuneRow->addWidget(m_tunePowerSlider, 1);
    root->addLayout(tuneRow);

    // TX Profile combo
    auto* profRow = new QHBoxLayout;
    profRow->setSpacing(4);
    auto* profLabel = new QLabel(QStringLiteral("Profile"), this);
    profLabel->setFixedWidth(36);
    profLabel->setStyleSheet(QStringLiteral("QLabel { color: #708090; font-size: 9px; }"));
    profRow->addWidget(profLabel);

    m_txProfile = new QComboBox(this);
    m_txProfile->setFixedHeight(22);
    m_txProfile->addItem(QStringLiteral("Default"));
    profRow->addWidget(m_txProfile, 1);
    root->addLayout(profRow);

    root->addStretch();

    // Mark all controls NYI
    NyiOverlay::markNyi(m_moxBtn, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(m_tuneBtn, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(m_atuBtn, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(m_powerSlider, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(m_tunePowerSlider, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(m_txProfile, QStringLiteral("Phase 3I-1"));
}

void TxApplet::syncFromModel()
{
    // NYI — no backend yet
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt and build**

Add `src/gui/applets/TxApplet.cpp` to `GUI_SOURCES`.

```bash
cmake --build build -j$(nproc)
```

- [ ] **Step 4: Commit**

```bash
git add src/gui/applets/TxApplet.h src/gui/applets/TxApplet.cpp CMakeLists.txt
git commit -m "Add TxApplet NYI shell (MOX, TUNE, power, profile)"
```

---

## Task 5: Remaining Applet NYI Shells

All remaining applets follow the same pattern: inherit AppletWidget, build controls in `buildUI()`, mark all with `NyiOverlay::markNyi()`. Each applet gets its own .h/.cpp pair.

**Files to create (all in `src/gui/applets/`):**

| File | appletId | Key controls | NYI Phase |
|---|---|---|---|
| PhoneCwApplet.h/.cpp | "PHCW" | QStackedWidget(Phone/CW), mic slider, CW speed | 3I-1/3I-2 |
| EqApplet.h/.cpp | "EQ" | RX/TX toggle, 10 sliders, preset combo | 3I-3 |
| FmApplet.h/.cpp | "FM" | CTCSS combo, deviation radio, offset spinner | 3I-3 |
| DigitalApplet.h/.cpp | "DIG" | VAC enable, device combo, sample rate | 3-DAX |
| PureSignalApplet.h/.cpp | "PS" | Calibrate button, status LEDs, feedback bar | 3I-4 |
| DiversityApplet.h/.cpp | "DIV" | Enable toggle, gain/phase sliders | 3F |
| CwxApplet.h/.cpp | "CWX" | Text input, send button, 6 memory buttons | 3I-2 |
| DvkApplet.h/.cpp | "DVK" | 4 record/play slots, repeat toggle | 3I-1 |
| CatApplet.h/.cpp | "CAT" | rigctld/TCI/TCP enable toggles + LEDs | 3K |
| TunerApplet.h/.cpp | "TUN" | TUNE button, SWR bar, relay bars | Aries ATU |

- [ ] **Step 1: Create all 10 applet shells**

Each applet follows this template (showing PhoneCwApplet as example — repeat for each):

```cpp
// src/gui/applets/PhoneCwApplet.h
#pragma once
#include "AppletWidget.h"
class QStackedWidget;
class QSlider;
class QPushButton;

namespace NereusSDR {

class PhoneCwApplet : public AppletWidget {
    Q_OBJECT
public:
    explicit PhoneCwApplet(RadioModel* model, QWidget* parent = nullptr);
    QString appletId() const override { return QStringLiteral("PHCW"); }
    QString appletTitle() const override { return QStringLiteral("Phone / CW"); }
    void syncFromModel() override;
private:
    void buildUI();
    QStackedWidget* m_stack = nullptr;
};

} // namespace NereusSDR
```

```cpp
// src/gui/applets/PhoneCwApplet.cpp
#include "PhoneCwApplet.h"
#include "NyiOverlay.h"
#include <QStackedWidget>
#include <QSlider>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace NereusSDR {

PhoneCwApplet::PhoneCwApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent) { buildUI(); }

void PhoneCwApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(2);

    // Phone/CW tab buttons
    auto* tabRow = new QHBoxLayout;
    auto* phoneBtn = new QPushButton(QStringLiteral("Phone"), this);
    phoneBtn->setCheckable(true);
    phoneBtn->setChecked(true);
    phoneBtn->setFixedHeight(20);
    tabRow->addWidget(phoneBtn);
    auto* cwBtn = new QPushButton(QStringLiteral("CW"), this);
    cwBtn->setCheckable(true);
    cwBtn->setFixedHeight(20);
    tabRow->addWidget(cwBtn);
    root->addLayout(tabRow);

    m_stack = new QStackedWidget(this);

    // Phone page
    auto* phonePage = new QWidget(m_stack);
    auto* phoneLayout = new QVBoxLayout(phonePage);
    phoneLayout->setContentsMargins(0, 2, 0, 0);
    phoneLayout->setSpacing(2);
    auto* micSlider = new QSlider(Qt::Horizontal, phonePage);
    micSlider->setRange(0, 100);
    micSlider->setFixedHeight(18);
    auto* micRow = new QHBoxLayout;
    micRow->addWidget(new QLabel(QStringLiteral("MIC"), phonePage));
    micRow->addWidget(micSlider, 1);
    phoneLayout->addLayout(micRow);
    auto* procBtn = new QPushButton(QStringLiteral("PROC"), phonePage);
    procBtn->setCheckable(true);
    procBtn->setFixedHeight(20);
    auto* monBtn = new QPushButton(QStringLiteral("MON"), phonePage);
    monBtn->setCheckable(true);
    monBtn->setFixedHeight(20);
    auto* voxBtn = new QPushButton(QStringLiteral("VOX"), phonePage);
    voxBtn->setCheckable(true);
    voxBtn->setFixedHeight(20);
    auto* btnRow = new QHBoxLayout;
    btnRow->addWidget(procBtn);
    btnRow->addWidget(monBtn);
    btnRow->addWidget(voxBtn);
    phoneLayout->addLayout(btnRow);
    phoneLayout->addStretch();
    NyiOverlay::markNyi(micSlider, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(procBtn, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(monBtn, QStringLiteral("Phase 3I-1"));
    NyiOverlay::markNyi(voxBtn, QStringLiteral("Phase 3I-1"));
    m_stack->addWidget(phonePage);

    // CW page
    auto* cwPage = new QWidget(m_stack);
    auto* cwLayout = new QVBoxLayout(cwPage);
    cwLayout->setContentsMargins(0, 2, 0, 0);
    cwLayout->setSpacing(2);
    auto* speedSlider = new QSlider(Qt::Horizontal, cwPage);
    speedSlider->setRange(1, 60);
    speedSlider->setValue(20);
    speedSlider->setFixedHeight(18);
    auto* speedRow = new QHBoxLayout;
    speedRow->addWidget(new QLabel(QStringLiteral("WPM"), cwPage));
    speedRow->addWidget(speedSlider, 1);
    cwLayout->addLayout(speedRow);
    auto* qskBtn = new QPushButton(QStringLiteral("QSK"), cwPage);
    qskBtn->setCheckable(true);
    qskBtn->setFixedHeight(20);
    auto* iambicBtn = new QPushButton(QStringLiteral("Iambic"), cwPage);
    iambicBtn->setCheckable(true);
    iambicBtn->setFixedHeight(20);
    auto* cwBtnRow = new QHBoxLayout;
    cwBtnRow->addWidget(qskBtn);
    cwBtnRow->addWidget(iambicBtn);
    cwLayout->addLayout(cwBtnRow);
    cwLayout->addStretch();
    NyiOverlay::markNyi(speedSlider, QStringLiteral("Phase 3I-2"));
    NyiOverlay::markNyi(qskBtn, QStringLiteral("Phase 3I-2"));
    NyiOverlay::markNyi(iambicBtn, QStringLiteral("Phase 3I-2"));
    m_stack->addWidget(cwPage);

    root->addWidget(m_stack);

    // Wire tab buttons to stack
    connect(phoneBtn, &QPushButton::clicked, this, [this, phoneBtn, cwBtn]() {
        m_stack->setCurrentIndex(0);
        phoneBtn->setChecked(true);
        cwBtn->setChecked(false);
    });
    connect(cwBtn, &QPushButton::clicked, this, [this, phoneBtn, cwBtn]() {
        m_stack->setCurrentIndex(1);
        phoneBtn->setChecked(false);
        cwBtn->setChecked(true);
    });
}

void PhoneCwApplet::syncFromModel() { /* NYI */ }

} // namespace NereusSDR
```

For the remaining 9 applets, each follows the same pattern with their specific controls. The implementation engineer should create each with:
- The controls listed in the spec (Section 4) as disabled widgets
- `NyiOverlay::markNyi()` on each control with the correct phase
- An empty `syncFromModel()` body

Minimal shell example for simpler applets (EqApplet shown):

```cpp
// src/gui/applets/EqApplet.h
#pragma once
#include "AppletWidget.h"
namespace NereusSDR {
class EqApplet : public AppletWidget {
    Q_OBJECT
public:
    explicit EqApplet(RadioModel* model, QWidget* parent = nullptr);
    QString appletId() const override { return QStringLiteral("EQ"); }
    QString appletTitle() const override { return QStringLiteral("Equalizer"); }
    void syncFromModel() override;
private:
    void buildUI();
};
} // namespace NereusSDR
```

```cpp
// src/gui/applets/EqApplet.cpp
#include "EqApplet.h"
#include "NyiOverlay.h"
#include <QSlider>
#include <QPushButton>
#include <QComboBox>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace NereusSDR {

EqApplet::EqApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent) { buildUI(); }

void EqApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(2);

    // RX/TX toggle + Enable
    auto* topRow = new QHBoxLayout;
    auto* rxBtn = new QPushButton(QStringLiteral("RX"), this);
    rxBtn->setCheckable(true);
    rxBtn->setChecked(true);
    rxBtn->setFixedHeight(20);
    topRow->addWidget(rxBtn);
    auto* txBtn = new QPushButton(QStringLiteral("TX"), this);
    txBtn->setCheckable(true);
    txBtn->setFixedHeight(20);
    topRow->addWidget(txBtn);
    auto* onBtn = new QPushButton(QStringLiteral("ON"), this);
    onBtn->setCheckable(true);
    onBtn->setFixedHeight(20);
    topRow->addWidget(onBtn);
    root->addLayout(topRow);

    // 10-band EQ sliders
    static const char* bands[] = {
        "32", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"
    };
    auto* sliderRow = new QHBoxLayout;
    sliderRow->setSpacing(2);
    for (int i = 0; i < 10; ++i) {
        auto* col = new QVBoxLayout;
        col->setSpacing(1);
        auto* slider = new QSlider(Qt::Vertical, this);
        slider->setRange(-12, 12);
        slider->setValue(0);
        slider->setFixedWidth(18);
        slider->setMinimumHeight(60);
        col->addWidget(slider, 1, Qt::AlignHCenter);
        auto* label = new QLabel(QString::fromLatin1(bands[i]), this);
        label->setStyleSheet(QStringLiteral("QLabel { font-size: 7px; color: #708090; }"));
        label->setAlignment(Qt::AlignCenter);
        col->addWidget(label);
        sliderRow->addLayout(col);
        NyiOverlay::markNyi(slider, QStringLiteral("Phase 3I-3"));
    }
    root->addLayout(sliderRow);

    // Preset combo
    auto* presetRow = new QHBoxLayout;
    auto* presetCombo = new QComboBox(this);
    presetCombo->addItems({QStringLiteral("Flat"), QStringLiteral("Bass Boost"),
                           QStringLiteral("Treble Boost"), QStringLiteral("Custom")});
    presetCombo->setFixedHeight(22);
    presetRow->addWidget(presetCombo, 1);
    auto* resetBtn = new QPushButton(QStringLiteral("Reset"), this);
    resetBtn->setFixedHeight(22);
    presetRow->addWidget(resetBtn);
    root->addLayout(presetRow);

    NyiOverlay::markNyi(rxBtn, QStringLiteral("Phase 3I-3"));
    NyiOverlay::markNyi(txBtn, QStringLiteral("Phase 3I-3"));
    NyiOverlay::markNyi(onBtn, QStringLiteral("Phase 3I-3"));
    NyiOverlay::markNyi(presetCombo, QStringLiteral("Phase 3I-3"));
    NyiOverlay::markNyi(resetBtn, QStringLiteral("Phase 3I-3"));
}

void EqApplet::syncFromModel() { /* NYI */ }

} // namespace NereusSDR
```

Repeat for: FmApplet, DigitalApplet, PureSignalApplet, DiversityApplet, CwxApplet, DvkApplet, CatApplet, TunerApplet — each with their spec-defined controls, all marked NYI.

- [ ] **Step 2: Add all 10 files to CMakeLists.txt**

Add to `GUI_SOURCES`:
```cmake
    src/gui/applets/PhoneCwApplet.cpp
    src/gui/applets/EqApplet.cpp
    src/gui/applets/FmApplet.cpp
    src/gui/applets/DigitalApplet.cpp
    src/gui/applets/PureSignalApplet.cpp
    src/gui/applets/DiversityApplet.cpp
    src/gui/applets/CwxApplet.cpp
    src/gui/applets/DvkApplet.cpp
    src/gui/applets/CatApplet.cpp
    src/gui/applets/TunerApplet.cpp
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build -j$(nproc)
```
Expected: clean compile for all 10 new applet files.

- [ ] **Step 4: Commit**

```bash
git add src/gui/applets/*.h src/gui/applets/*.cpp CMakeLists.txt
git commit -m "Add 10 applet NYI shells (PhoneCw, EQ, FM, Digital, PS, DIV, CWX, DVK, CAT, Tuner)"
```

---

## Task 6: Extend ContainerManager for Mixed Content + Default Layout

Modify `ContainerWidget` and `MainWindow` so Container #0 holds both MeterWidget and AppletWidgets in a scrollable QVBoxLayout.

**Files:**
- Modify: `src/gui/containers/ContainerWidget.h` (~line 100, `setContent`)
- Modify: `src/gui/containers/ContainerWidget.cpp` (~line 149, setContent impl)
- Modify: `src/gui/MainWindow.h` (add RxApplet/TxApplet members)
- Modify: `src/gui/MainWindow.cpp` (createDefaultContainers, populateDefaultMeter)

- [ ] **Step 1: Add multi-content support to ContainerWidget**

In `ContainerWidget.h`, add after the existing `setContent()` declaration (~line 100):

```cpp
    // Add a widget to the container's vertical content stack.
    // Multiple widgets stack top-to-bottom in a scrollable layout.
    void addContentWidget(QWidget* widget);
    void clearContent();
```

In `ContainerWidget.cpp`, add the implementations. Find the existing `setContent()` method and add below it:

```cpp
void ContainerWidget::addContentWidget(QWidget* widget)
{
    if (!widget) { return; }
    if (!m_contentHolder) { return; }
    // m_contentHolder is a QVBoxLayout — just add the widget
    m_contentHolder->addWidget(widget);
}

void ContainerWidget::clearContent()
{
    if (!m_contentHolder) { return; }
    QLayoutItem* item;
    while ((item = m_contentHolder->takeAt(0)) != nullptr) {
        if (item->widget()) {
            item->widget()->setParent(nullptr);
        }
        delete item;
    }
}
```

- [ ] **Step 2: Update MainWindow to create mixed Container #0**

In `MainWindow.h`, add includes and members:

```cpp
// After existing forward declarations:
class RxApplet;
class TxApplet;

// In private section, after m_meterPoller:
RxApplet* m_rxApplet{nullptr};
TxApplet* m_txApplet{nullptr};
```

In `MainWindow.cpp`, add includes at top:

```cpp
#include "applets/RxApplet.h"
#include "applets/TxApplet.h"
```

Replace `populateDefaultMeter()` body (lines 266-296) with:

```cpp
void MainWindow::populateDefaultMeter()
{
    ContainerWidget* c0 = m_containerManager->panelContainer();
    if (!c0) {
        qCWarning(lcContainer) << "No panel container for default layout";
        return;
    }

    c0->clearContent();

    // 1. S-Meter (MeterWidget with NeedleItem preset)
    m_meterWidget = new MeterWidget();
    ItemGroup* smeter = ItemGroup::createSMeterPreset(
        MeterBinding::SignalAvg, QStringLiteral("S-Meter"), m_meterWidget);
    smeter->installInto(m_meterWidget, 0.0f, 0.0f, 1.0f, 1.0f);
    delete smeter;
    m_meterWidget->setFixedHeight(120);
    c0->addContentWidget(m_meterWidget);

    // 2. RxApplet (Tier 1 wired)
    m_rxApplet = new RxApplet(m_radioModel);
    c0->addContentWidget(m_rxApplet);

    // 3. Power/SWR + ALC (MeterWidget)
    auto* txMeterWidget = new MeterWidget();
    ItemGroup* pwrSwr = ItemGroup::createPowerSwrPreset(
        QStringLiteral("Power/SWR"), txMeterWidget);
    pwrSwr->installInto(txMeterWidget, 0.0f, 0.0f, 1.0f, 0.65f);
    delete pwrSwr;
    ItemGroup* alc = ItemGroup::createAlcPreset(txMeterWidget);
    alc->installInto(txMeterWidget, 0.0f, 0.65f, 1.0f, 0.35f);
    delete alc;
    txMeterWidget->setFixedHeight(80);
    c0->addContentWidget(txMeterWidget);

    // 4. TxApplet (NYI shell)
    m_txApplet = new TxApplet(m_radioModel);
    c0->addContentWidget(m_txApplet);

    qCDebug(lcMeter) << "Default Container #0: SMeter + RxApplet + Power/SWR/ALC + TxApplet";
}
```

- [ ] **Step 3: Wire RxApplet to SliceModel**

In `MainWindow::wireSliceToSpectrum()`, after the existing VFO wiring (~line 391), add:

```cpp
    // Wire RxApplet to active slice
    if (m_rxApplet) {
        m_rxApplet->setSlice(slice);
    }
```

- [ ] **Step 4: Update MeterPoller targets**

In `MainWindow::buildUI()`, after the existing `m_meterPoller->addTarget(m_meterWidget)` block (~line 231), the new `txMeterWidget` is created inside `populateDefaultMeter()`. We need to add it as a poller target. Modify `populateDefaultMeter()` to store and register the tx meter:

In `MainWindow.h`, add member:
```cpp
    MeterWidget* m_txMeterWidget{nullptr};
```

In `populateDefaultMeter()`, change `auto* txMeterWidget` to `m_txMeterWidget`:
```cpp
    m_txMeterWidget = new MeterWidget();
```

In `buildUI()`, after existing poller target addition:
```cpp
    if (m_txMeterWidget) {
        m_meterPoller->addTarget(m_txMeterWidget);
    }
```

Note: since `populateDefaultMeter()` is called before the poller target lines, `m_txMeterWidget` will be set by this point.

- [ ] **Step 5: Build and verify**

```bash
cmake --build build -j$(nproc)
```
Expected: clean compile. Run the app — Container #0 should show S-Meter at top, RxApplet controls below, Power/SWR+ALC meters, then TxApplet (greyed out with NYI badges).

- [ ] **Step 6: Commit**

```bash
git add src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp \
        src/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Wire mixed Container #0: SMeter + RxApplet + Power/SWR/ALC + TxApplet"
```

---

## Task 7: SpectrumOverlayPanel — Button Strip

**Files:**
- Create: `src/gui/SpectrumOverlayPanel.h`
- Create: `src/gui/SpectrumOverlayPanel.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create SpectrumOverlayPanel header**

```cpp
// src/gui/SpectrumOverlayPanel.h
#pragma once

#include <QWidget>

class QPushButton;
class QVBoxLayout;

namespace NereusSDR {

class RadioModel;
class SliceModel;

class SpectrumOverlayPanel : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumOverlayPanel(RadioModel* model, QWidget* parent = nullptr);

    void setSlice(SliceModel* slice);

signals:
    // Band
    void bandSelected(const QString& bandName, double freqHz, const QString& mode);

    // DSP toggles
    void nbToggled(bool enabled);
    void nrToggled(bool enabled);
    void anfToggled(bool enabled);
    void snbToggled(bool enabled);

    // Display
    void wfColorGainChanged(int gain);
    void wfBlackLevelChanged(int level);
    void colorSchemeChanged(int index);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildButtons();
    QPushButton* makeButton(const QString& text);
    void showFlyout(QWidget* flyout, QPushButton* anchor);
    void hideFlyout();

    QWidget* buildBandFlyout();
    QWidget* buildDspFlyout();
    QWidget* buildDisplayFlyout();
    QWidget* buildAntFlyout();

    RadioModel* m_model = nullptr;
    SliceModel* m_slice = nullptr;

    QVBoxLayout* m_buttonLayout = nullptr;
    QWidget* m_activeFlyout = nullptr;
    QPushButton* m_activeButton = nullptr;

    // Button strip
    QPushButton* m_collapseBtn = nullptr;
    QPushButton* m_addRxBtn = nullptr;
    QPushButton* m_addTnfBtn = nullptr;
    QPushButton* m_bandBtn = nullptr;
    QPushButton* m_antBtn = nullptr;
    QPushButton* m_dspBtn = nullptr;
    QPushButton* m_displayBtn = nullptr;
    QPushButton* m_daxBtn = nullptr;
    QPushButton* m_attBtn = nullptr;
    QPushButton* m_mnfBtn = nullptr;

    bool m_collapsed = false;
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create SpectrumOverlayPanel implementation**

```cpp
// src/gui/SpectrumOverlayPanel.cpp
#include "SpectrumOverlayPanel.h"
#include "applets/NyiOverlay.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QMouseEvent>

namespace NereusSDR {

static constexpr int kBtnW = 68;
static constexpr int kBtnH = 22;

SpectrumOverlayPanel::SpectrumOverlayPanel(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setFixedWidth(kBtnW + 8);

    m_buttonLayout = new QVBoxLayout(this);
    m_buttonLayout->setContentsMargins(4, 4, 0, 4);
    m_buttonLayout->setSpacing(2);

    buildButtons();

    // Block mouse events from falling through to spectrum
    installEventFilter(this);
}

QPushButton* SpectrumOverlayPanel::makeButton(const QString& text)
{
    auto* btn = new QPushButton(text, this);
    btn->setFixedSize(kBtnW, kBtnH);
    btn->setStyleSheet(QStringLiteral(
        "QPushButton {"
        "  background: rgba(20, 30, 45, 240);"
        "  color: #c8d8e8; border: 1px solid #304050;"
        "  border-radius: 2px; font-size: 10px; font-weight: bold;"
        "}"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); }"
        "QPushButton:checked { background: rgba(0, 112, 192, 220); }"));
    return btn;
}

void SpectrumOverlayPanel::buildButtons()
{
    m_collapseBtn = makeButton(QStringLiteral("\u25C0"));  // ◀
    m_collapseBtn->setFixedSize(kBtnW, kBtnH);
    connect(m_collapseBtn, &QPushButton::clicked, this, [this]() {
        m_collapsed = !m_collapsed;
        m_collapseBtn->setText(m_collapsed ? QStringLiteral("\u25B6") : QStringLiteral("\u25C0"));
        // Hide/show all buttons except collapse
        for (int i = 1; i < m_buttonLayout->count(); ++i) {
            QLayoutItem* item = m_buttonLayout->itemAt(i);
            if (item && item->widget()) {
                item->widget()->setVisible(!m_collapsed);
            }
        }
        hideFlyout();
    });
    m_buttonLayout->addWidget(m_collapseBtn);

    m_addRxBtn = makeButton(QStringLiteral("+RX"));
    m_addRxBtn->setEnabled(false);  // NYI until Phase 3F
    m_buttonLayout->addWidget(m_addRxBtn);

    m_addTnfBtn = makeButton(QStringLiteral("+TNF"));
    m_addTnfBtn->setEnabled(false);  // NYI
    m_buttonLayout->addWidget(m_addTnfBtn);

    m_bandBtn = makeButton(QStringLiteral("BAND"));
    m_bandBtn->setCheckable(true);
    connect(m_bandBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeButton == m_bandBtn && m_activeFlyout) {
            hideFlyout();
        } else {
            showFlyout(buildBandFlyout(), m_bandBtn);
        }
    });
    m_buttonLayout->addWidget(m_bandBtn);

    m_antBtn = makeButton(QStringLiteral("ANT"));
    m_antBtn->setCheckable(true);
    connect(m_antBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeButton == m_antBtn && m_activeFlyout) {
            hideFlyout();
        } else {
            showFlyout(buildAntFlyout(), m_antBtn);
        }
    });
    m_buttonLayout->addWidget(m_antBtn);

    m_dspBtn = makeButton(QStringLiteral("DSP"));
    m_dspBtn->setCheckable(true);
    connect(m_dspBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeButton == m_dspBtn && m_activeFlyout) {
            hideFlyout();
        } else {
            showFlyout(buildDspFlyout(), m_dspBtn);
        }
    });
    m_buttonLayout->addWidget(m_dspBtn);

    m_displayBtn = makeButton(QStringLiteral("Display"));
    m_displayBtn->setCheckable(true);
    connect(m_displayBtn, &QPushButton::clicked, this, [this]() {
        if (m_activeButton == m_displayBtn && m_activeFlyout) {
            hideFlyout();
        } else {
            showFlyout(buildDisplayFlyout(), m_displayBtn);
        }
    });
    m_buttonLayout->addWidget(m_displayBtn);

    m_daxBtn = makeButton(QStringLiteral("DAX"));
    m_daxBtn->setEnabled(false);  // NYI until Phase 3-DAX
    m_buttonLayout->addWidget(m_daxBtn);

    m_attBtn = makeButton(QStringLiteral("ATT"));
    m_attBtn->setEnabled(false);  // NYI — requires preamp/atten model
    m_buttonLayout->addWidget(m_attBtn);

    m_mnfBtn = makeButton(QStringLiteral("MNF"));
    m_mnfBtn->setEnabled(false);  // NYI
    m_buttonLayout->addWidget(m_mnfBtn);

    m_buttonLayout->addStretch();
}

void SpectrumOverlayPanel::showFlyout(QWidget* flyout, QPushButton* anchor)
{
    hideFlyout();
    m_activeFlyout = flyout;
    m_activeButton = anchor;
    anchor->setChecked(true);

    // Position flyout to the right of the button strip
    flyout->setParent(parentWidget());
    QPoint pos = mapToParent(QPoint(width(), anchor->y()));
    flyout->move(pos);
    flyout->show();
    flyout->raise();
}

void SpectrumOverlayPanel::hideFlyout()
{
    if (m_activeFlyout) {
        m_activeFlyout->deleteLater();
        m_activeFlyout = nullptr;
    }
    if (m_activeButton) {
        m_activeButton->setChecked(false);
        m_activeButton = nullptr;
    }
}

QWidget* SpectrumOverlayPanel::buildBandFlyout()
{
    auto* panel = new QWidget(nullptr);
    panel->setStyleSheet(QStringLiteral(
        "QWidget { background: rgba(15, 15, 26, 220); border: 1px solid #304050; }"
        "QPushButton {"
        "  background: rgba(20, 30, 45, 240); color: #c8d8e8;"
        "  border: 1px solid #304050; border-radius: 2px;"
        "  font-size: 10px; font-weight: bold; min-width: 40px; min-height: 22px;"
        "}"
        "QPushButton:hover { background: rgba(0, 112, 192, 180); }"));

    auto* grid = new QGridLayout(panel);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setSpacing(2);

    // HF bands: 6 columns x 3 rows
    struct BandDef { const char* name; double freqHz; };
    static const BandDef bands[] = {
        {"160", 1.8e6}, {"80", 3.5e6}, {"60", 5.3e6},
        {"40", 7.0e6}, {"30", 10.1e6}, {"20", 14.0e6},
        {"17", 18.068e6}, {"15", 21.0e6}, {"12", 24.89e6},
        {"10", 28.0e6}, {"6", 50.0e6}, {"WWV", 10.0e6},
        {"GEN", 5.0e6}, {"LF", 0.137e6}, {"MW", 0.530e6},
        {"120m", 2.3e6}, {"90m", 3.2e6}, {"60m", 4.75e6},
    };

    int col = 0, row = 0;
    for (const auto& band : bands) {
        auto* btn = new QPushButton(QString::fromLatin1(band.name), panel);
        double freq = band.freqHz;
        connect(btn, &QPushButton::clicked, this, [this, freq, &band]() {
            emit bandSelected(QString::fromLatin1(band.name), freq, QStringLiteral("USB"));
            hideFlyout();
        });
        grid->addWidget(btn, row, col);
        if (++col >= 6) { col = 0; ++row; }
    }

    panel->setFixedSize(grid->sizeHint());
    return panel;
}

QWidget* SpectrumOverlayPanel::buildDspFlyout()
{
    auto* panel = new QWidget(nullptr);
    panel->setFixedWidth(200);
    panel->setStyleSheet(QStringLiteral(
        "QWidget { background: rgba(15, 15, 26, 220); border: 1px solid #304050; }"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    auto makeToggle = [&](const QString& text) -> QPushButton* {
        auto* btn = new QPushButton(text, panel);
        btn->setCheckable(true);
        btn->setFixedHeight(22);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton {"
            "  background: rgba(20, 30, 45, 240); color: #c8d8e8;"
            "  border: 1px solid #304050; border-radius: 2px;"
            "  font-size: 10px; font-weight: bold;"
            "}"
            "QPushButton:hover { background: rgba(0, 112, 192, 180); }"
            "QPushButton:checked { background: #006040; color: #00ff88; }"));
        return btn;
    };

    auto* nrBtn = makeToggle(QStringLiteral("NR"));
    connect(nrBtn, &QPushButton::toggled, this, &SpectrumOverlayPanel::nrToggled);
    layout->addWidget(nrBtn);

    auto* nbBtn = makeToggle(QStringLiteral("NB"));
    connect(nbBtn, &QPushButton::toggled, this, &SpectrumOverlayPanel::nbToggled);
    layout->addWidget(nbBtn);

    auto* anfBtn = makeToggle(QStringLiteral("ANF"));
    connect(anfBtn, &QPushButton::toggled, this, &SpectrumOverlayPanel::anfToggled);
    layout->addWidget(anfBtn);

    auto* snbBtn = makeToggle(QStringLiteral("SNB"));
    connect(snbBtn, &QPushButton::toggled, this, &SpectrumOverlayPanel::snbToggled);
    layout->addWidget(snbBtn);

    auto* binBtn = makeToggle(QStringLiteral("BIN"));
    binBtn->setEnabled(false);  // NYI
    layout->addWidget(binBtn);

    auto* mnfBtn = makeToggle(QStringLiteral("MNF"));
    mnfBtn->setEnabled(false);  // NYI
    layout->addWidget(mnfBtn);

    panel->setFixedHeight(layout->sizeHint().height());
    return panel;
}

QWidget* SpectrumOverlayPanel::buildDisplayFlyout()
{
    auto* panel = new QWidget(nullptr);
    panel->setFixedWidth(220);
    panel->setStyleSheet(QStringLiteral(
        "QWidget { background: rgba(15, 15, 26, 220); border: 1px solid #304050; }"
        "QLabel { color: #8090a0; font-size: 9px; }"
        "QSlider::groove:horizontal { background: #203040; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal {"
        "  background: #00b4d8; width: 10px; height: 10px;"
        "  margin: -3px 0; border-radius: 5px;"
        "}"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // Color Scheme
    auto* schemeRow = new QHBoxLayout;
    schemeRow->addWidget(new QLabel(QStringLiteral("Scheme"), panel));
    auto* schemeCombo = new QComboBox(panel);
    schemeCombo->addItems({QStringLiteral("Default"), QStringLiteral("Enhanced"),
                           QStringLiteral("Spectran"), QStringLiteral("B&W")});
    schemeCombo->setFixedHeight(20);
    schemeRow->addWidget(schemeCombo, 1);
    connect(schemeCombo, &QComboBox::currentIndexChanged,
            this, &SpectrumOverlayPanel::colorSchemeChanged);
    layout->addLayout(schemeRow);

    // Color Gain
    auto* gainRow = new QHBoxLayout;
    gainRow->addWidget(new QLabel(QStringLiteral("Gain"), panel));
    auto* gainSlider = new QSlider(Qt::Horizontal, panel);
    gainSlider->setRange(0, 100);
    gainSlider->setValue(50);
    gainSlider->setFixedHeight(18);
    gainRow->addWidget(gainSlider, 1);
    connect(gainSlider, &QSlider::valueChanged,
            this, &SpectrumOverlayPanel::wfColorGainChanged);
    layout->addLayout(gainRow);

    // Black Level
    auto* blackRow = new QHBoxLayout;
    blackRow->addWidget(new QLabel(QStringLiteral("Black"), panel));
    auto* blackSlider = new QSlider(Qt::Horizontal, panel);
    blackSlider->setRange(0, 125);
    blackSlider->setValue(40);
    blackSlider->setFixedHeight(18);
    blackRow->addWidget(blackSlider, 1);
    connect(blackSlider, &QSlider::valueChanged,
            this, &SpectrumOverlayPanel::wfBlackLevelChanged);
    layout->addLayout(blackRow);

    panel->setFixedHeight(layout->sizeHint().height());
    return panel;
}

QWidget* SpectrumOverlayPanel::buildAntFlyout()
{
    auto* panel = new QWidget(nullptr);
    panel->setFixedWidth(180);
    panel->setStyleSheet(QStringLiteral(
        "QWidget { background: rgba(15, 15, 26, 220); border: 1px solid #304050; }"
        "QLabel { color: #8090a0; font-size: 9px; }"));

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(2);

    // RX Antenna
    auto* rxRow = new QHBoxLayout;
    rxRow->addWidget(new QLabel(QStringLiteral("RX Ant"), panel));
    auto* rxCombo = new QComboBox(panel);
    rxCombo->addItems({QStringLiteral("ANT1"), QStringLiteral("ANT2"), QStringLiteral("ANT3")});
    rxCombo->setFixedHeight(20);
    rxRow->addWidget(rxCombo, 1);
    layout->addLayout(rxRow);

    // TX Antenna
    auto* txRow = new QHBoxLayout;
    txRow->addWidget(new QLabel(QStringLiteral("TX Ant"), panel));
    auto* txCombo = new QComboBox(panel);
    txCombo->addItems({QStringLiteral("ANT1"), QStringLiteral("ANT2"), QStringLiteral("ANT3")});
    txCombo->setFixedHeight(20);
    txRow->addWidget(txCombo, 1);
    layout->addLayout(txRow);

    // RF Gain slider
    auto* rfRow = new QHBoxLayout;
    rfRow->addWidget(new QLabel(QStringLiteral("RF Gain"), panel));
    auto* rfSlider = new QSlider(Qt::Horizontal, panel);
    rfSlider->setRange(-8, 32);
    rfSlider->setValue(0);
    rfSlider->setFixedHeight(18);
    rfRow->addWidget(rfSlider, 1);
    layout->addLayout(rfRow);

    panel->setFixedHeight(layout->sizeHint().height());
    return panel;
}

void SpectrumOverlayPanel::setSlice(SliceModel* slice)
{
    m_slice = slice;
}

bool SpectrumOverlayPanel::eventFilter(QObject* obj, QEvent* event)
{
    // Block mouse/wheel events from reaching the spectrum underneath
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::MouseButtonRelease ||
        event->type() == QEvent::Wheel) {
        return true;
    }
    return QWidget::eventFilter(obj, event);
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add to `GUI_SOURCES`:
```cmake
    src/gui/SpectrumOverlayPanel.cpp
```

- [ ] **Step 4: Build and verify**

```bash
cmake --build build -j$(nproc)
```

- [ ] **Step 5: Commit**

```bash
git add src/gui/SpectrumOverlayPanel.h src/gui/SpectrumOverlayPanel.cpp CMakeLists.txt
git commit -m "Add SpectrumOverlayPanel with Band/DSP/Display/ANT flyouts"
```

---

## Task 8: Wire SpectrumOverlayPanel to MainWindow

**Files:**
- Modify: `src/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

- [ ] **Step 1: Add overlay to MainWindow**

In `MainWindow.h`, add forward declaration and member:
```cpp
class SpectrumOverlayPanel;
// ...
SpectrumOverlayPanel* m_overlayPanel{nullptr};
```

In `MainWindow.cpp`, add include:
```cpp
#include "SpectrumOverlayPanel.h"
```

In `MainWindow::buildUI()`, after `m_spectrumWidget` creation (~line 138), add:
```cpp
    // Left overlay button strip (AetherSDR SpectrumOverlayMenu pattern)
    m_overlayPanel = new SpectrumOverlayPanel(m_radioModel, m_spectrumWidget);
    m_overlayPanel->move(4, 4);
    m_overlayPanel->show();
    m_overlayPanel->raise();
```

- [ ] **Step 2: Wire overlay signals**

In `MainWindow::wireSliceToSpectrum()`, after the VFO wiring block, add:
```cpp
    // Wire overlay panel to slice
    if (m_overlayPanel) {
        m_overlayPanel->setSlice(slice);

        // Band selection → tune slice
        connect(m_overlayPanel, &SpectrumOverlayPanel::bandSelected,
                this, [slice](const QString& /*name*/, double freqHz, const QString& /*mode*/) {
            slice->setFrequency(freqHz);
        });

        // DSP toggles → RxChannel
        connect(m_overlayPanel, &SpectrumOverlayPanel::nrToggled,
                this, [this](bool on) {
            RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
            if (rxCh) { rxCh->setNrEnabled(on); }
        });
        connect(m_overlayPanel, &SpectrumOverlayPanel::nbToggled,
                this, [this](bool on) {
            RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
            if (rxCh) { rxCh->setNb1Enabled(on); }
        });
        connect(m_overlayPanel, &SpectrumOverlayPanel::anfToggled,
                this, [this](bool on) {
            RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
            if (rxCh) { rxCh->setAnfEnabled(on); }
        });

        // Display changes → spectrum widget
        connect(m_overlayPanel, &SpectrumOverlayPanel::wfColorGainChanged,
                m_spectrumWidget, &SpectrumWidget::setWaterfallColorGain);
        connect(m_overlayPanel, &SpectrumOverlayPanel::wfBlackLevelChanged,
                m_spectrumWidget, &SpectrumWidget::setWaterfallBlackLevel);
        connect(m_overlayPanel, &SpectrumOverlayPanel::colorSchemeChanged,
                m_spectrumWidget, &SpectrumWidget::setColorScheme);
    }
```

- [ ] **Step 3: Build, run, and verify**

```bash
cmake --build build -j$(nproc) && ./build/NereusSDR
```
Expected: Left overlay button strip visible on spectrum. BAND flyout shows grid, clicking a band tunes the VFO. DSP flyout toggles NR/NB/ANF. Display flyout adjusts waterfall.

- [ ] **Step 4: Commit**

```bash
git add src/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Wire SpectrumOverlayPanel to MainWindow (band, DSP, display)"
```

---

## Task 9: Menu Bar Expansion

Expand the existing 3-menu bar (File, Radio, Help) to the full 9-menu layout.

**Files:**
- Modify: `src/gui/MainWindow.cpp` (`buildMenuBar()` at line 298)

- [ ] **Step 1: Replace buildMenuBar() implementation**

Replace the entire `buildMenuBar()` method (lines 298-337) with:

```cpp
void MainWindow::buildMenuBar()
{
    // --- File ---
    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(QStringLiteral("&Settings..."), this, [this]() {
        // TODO: open SetupDialog (Task 11)
    });
    fileMenu->addSeparator();
    fileMenu->addAction(QStringLiteral("&Quit"), QKeySequence::Quit,
                        qApp, &QApplication::quit);

    // --- Radio ---
    auto* radioMenu = menuBar()->addMenu(QStringLiteral("&Radio"));
    radioMenu->addAction(QStringLiteral("&Connect..."), QKeySequence(Qt::CTRL | Qt::Key_K),
                         this, &MainWindow::showConnectionPanel);
    radioMenu->addAction(QStringLiteral("&Disconnect"), this, [this]() {
        m_radioModel->disconnectFromRadio();
    });
    radioMenu->addSeparator();
    radioMenu->addAction(QStringLiteral("&Protocol Info"), this, [this]() {
        if (m_radioModel->isConnected()) {
            RadioInfo info = m_radioModel->connection()->radioInfo();
            QString msg = QStringLiteral("Radio: %1\nProtocol: P%2\nFirmware: %3\nMAC: %4\nIP: %5")
                .arg(info.displayName())
                .arg(static_cast<int>(info.protocol))
                .arg(info.firmwareVersion)
                .arg(info.macAddress, info.address.toString());
            qCDebug(lcConnection) << msg;
        }
    });

    // --- View ---
    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    auto* panLayoutMenu = viewMenu->addMenu(QStringLiteral("Pan Layout"));
    panLayoutMenu->addAction(QStringLiteral("1-up"));
    panLayoutMenu->addAction(QStringLiteral("2 Vertical"));
    panLayoutMenu->addAction(QStringLiteral("2 Horizontal"));
    panLayoutMenu->addAction(QStringLiteral("2x2 Grid"));
    panLayoutMenu->addAction(QStringLiteral("1+2 Horizontal"));
    // All NYI until Phase 3F
    for (auto* action : panLayoutMenu->actions()) {
        action->setEnabled(false);
    }
    viewMenu->addSeparator();
    auto* bandPlanMenu = viewMenu->addMenu(QStringLiteral("Band Plan"));
    bandPlanMenu->addAction(QStringLiteral("Off"));
    bandPlanMenu->addAction(QStringLiteral("Small"));
    bandPlanMenu->addAction(QStringLiteral("Medium"));
    bandPlanMenu->addAction(QStringLiteral("Large"));
    for (auto* action : bandPlanMenu->actions()) {
        action->setEnabled(false);
    }
    viewMenu->addSeparator();
    auto* uiScaleMenu = viewMenu->addMenu(QStringLiteral("UI Scale"));
    for (int s : {100, 125, 150, 175, 200}) {
        uiScaleMenu->addAction(QStringLiteral("%1%").arg(s))->setEnabled(false);
    }

    // --- DSP ---
    auto* dspMenu = menuBar()->addMenu(QStringLiteral("&DSP"));
    auto* nrAction = dspMenu->addAction(QStringLiteral("NR"));
    nrAction->setCheckable(true);
    connect(nrAction, &QAction::toggled, this, [this](bool on) {
        RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
        if (rxCh) { rxCh->setNrEnabled(on); }
    });
    auto* nbAction = dspMenu->addAction(QStringLiteral("NB"));
    nbAction->setCheckable(true);
    connect(nbAction, &QAction::toggled, this, [this](bool on) {
        RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
        if (rxCh) { rxCh->setNb1Enabled(on); }
    });
    auto* anfAction = dspMenu->addAction(QStringLiteral("ANF"));
    anfAction->setCheckable(true);
    connect(anfAction, &QAction::toggled, this, [this](bool on) {
        RxChannel* rxCh = m_radioModel->wdspEngine()->rxChannel(0);
        if (rxCh) { rxCh->setAnfEnabled(on); }
    });
    dspMenu->addSeparator();
    dspMenu->addAction(QStringLiteral("Equalizer..."))->setEnabled(false);
    dspMenu->addAction(QStringLiteral("PureSignal..."))->setEnabled(false);
    dspMenu->addAction(QStringLiteral("Diversity..."))->setEnabled(false);

    // --- Band ---
    auto* bandMenu = menuBar()->addMenu(QStringLiteral("&Band"));
    struct BandEntry { const char* name; double freqHz; };
    static const BandEntry hfBands[] = {
        {"160m", 1.8e6}, {"80m", 3.5e6}, {"60m", 5.3e6}, {"40m", 7.0e6},
        {"30m", 10.1e6}, {"20m", 14.0e6}, {"17m", 18.068e6}, {"15m", 21.0e6},
        {"12m", 24.89e6}, {"10m", 28.0e6}, {"6m", 50.0e6},
    };
    auto* hfMenu = bandMenu->addMenu(QStringLiteral("HF"));
    for (const auto& band : hfBands) {
        double freq = band.freqHz;
        hfMenu->addAction(QString::fromLatin1(band.name), this, [this, freq]() {
            SliceModel* slice = m_radioModel->activeSlice();
            if (slice) { slice->setFrequency(freq); }
        });
    }
    bandMenu->addAction(QStringLiteral("WWV"), this, [this]() {
        SliceModel* slice = m_radioModel->activeSlice();
        if (slice) { slice->setFrequency(10.0e6); }
    });
    bandMenu->addSeparator();
    bandMenu->addAction(QStringLiteral("Band Stacking..."))->setEnabled(false);

    // --- Mode ---
    auto* modeMenu = menuBar()->addMenu(QStringLiteral("&Mode"));
    static const char* modes[] = {
        "LSB", "USB", "DSB", "CWL", "CWU", "AM", "SAM", "FM",
        "DIGL", "DIGU", "DRM", "SPEC"
    };
    for (int i = 0; i < 12; ++i) {
        int modeIdx = i;
        modeMenu->addAction(QString::fromLatin1(modes[i]), this, [this, modeIdx]() {
            SliceModel* slice = m_radioModel->activeSlice();
            if (slice) { slice->setDspMode(static_cast<DSPMode>(modeIdx)); }
        });
    }

    // --- Containers ---
    auto* containerMenu = menuBar()->addMenu(QStringLiteral("&Containers"));
    containerMenu->addAction(QStringLiteral("New Container..."))->setEnabled(false);
    containerMenu->addAction(QStringLiteral("Container Settings..."))->setEnabled(false);
    containerMenu->addSeparator();
    containerMenu->addAction(QStringLiteral("Reset Default Layout"))->setEnabled(false);

    // --- Tools ---
    auto* toolsMenu = menuBar()->addMenu(QStringLiteral("&Tools"));
    toolsMenu->addAction(QStringLiteral("CWX..."))->setEnabled(false);
    toolsMenu->addAction(QStringLiteral("Memory Manager..."))->setEnabled(false);
    toolsMenu->addAction(QStringLiteral("CAT Control..."))->setEnabled(false);
    toolsMenu->addAction(QStringLiteral("TCI Server..."))->setEnabled(false);
    toolsMenu->addAction(QStringLiteral("DAX Audio..."))->setEnabled(false);
    toolsMenu->addSeparator();
    toolsMenu->addAction(QStringLiteral("Network Diagnostics..."))->setEnabled(false);
    toolsMenu->addAction(QStringLiteral("&Support..."), this,
                         &MainWindow::showSupportDialog);

    // --- Help ---
    auto* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    helpMenu->addAction(QStringLiteral("Getting Started..."))->setEnabled(false);
    helpMenu->addAction(QStringLiteral("What's New..."))->setEnabled(false);
    helpMenu->addSeparator();
    helpMenu->addAction(QStringLiteral("&About NereusSDR"), this, [this]() {
        Q_UNUSED(this);
    });
}
```

- [ ] **Step 2: Build, run, and verify**

```bash
cmake --build build -j$(nproc) && ./build/NereusSDR
```
Expected: 9 menus visible in menu bar. Band → HF → clicking "20m" tunes to 14 MHz. Mode → USB switches mode. DSP → NR toggles noise reduction. Disabled items show greyed text.

- [ ] **Step 3: Commit**

```bash
git add src/gui/MainWindow.cpp
git commit -m "Expand menu bar to 9 menus (File, Radio, View, DSP, Band, Mode, Containers, Tools, Help)"
```

---

## Task 10: Status Bar Expansion

Replace the simple status bar with the AetherSDR double-height design.

**Files:**
- Modify: `src/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp` (`buildStatusBar()`)

- [ ] **Step 1: Add status bar members to MainWindow.h**

Replace existing status bar members (lines 51-52) with:

```cpp
    // Status bar widgets (double-height AetherSDR pattern)
    QLabel* m_connStatusLabel{nullptr};
    QLabel* m_radioInfoLabel{nullptr};
    QLabel* m_callsignLabel{nullptr};
    QLabel* m_utcTimeLabel{nullptr};
    QTimer* m_clockTimer{nullptr};
```

- [ ] **Step 2: Replace buildStatusBar() implementation**

```cpp
void MainWindow::buildStatusBar()
{
    statusBar()->setFixedHeight(46);
    statusBar()->setStyleSheet(QStringLiteral(
        "QStatusBar {"
        "  background: #1a2a3a; color: #8090a0;"
        "  border-top: 1px solid #203040;"
        "}"
        "QStatusBar::item { border: none; }"));

    // Left: connection status
    m_connStatusLabel = new QLabel(QStringLiteral(" Disconnected "), this);
    m_connStatusLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #8090a0; background: #0f1520;"
        "  border: 1px solid #203040; border-radius: 3px;"
        "  padding: 2px 8px; font-size: 11px;"
        "}"));
    m_connStatusLabel->setCursor(Qt::PointingHandCursor);
    m_connStatusLabel->installEventFilter(this);
    statusBar()->addWidget(m_connStatusLabel);

    // Left: radio info
    m_radioInfoLabel = new QLabel(this);
    m_radioInfoLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #607080; font-size: 11px; }"));
    statusBar()->addWidget(m_radioInfoLabel);

    // Center: station callsign
    m_callsignLabel = new QLabel(this);
    m_callsignLabel->setAlignment(Qt::AlignCenter);
    m_callsignLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #c8d8e8; font-size: 14px; font-weight: bold; }"));
    auto callsign = AppSettings::instance()
        .value(QStringLiteral("StationCallsign")).toString();
    if (!callsign.isEmpty()) {
        m_callsignLabel->setText(QStringLiteral("STATION: %1").arg(callsign));
    }
    statusBar()->addWidget(m_callsignLabel, 1);

    // Right: UTC time
    m_utcTimeLabel = new QLabel(this);
    m_utcTimeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_utcTimeLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #8090a0; font-size: 11px; padding-right: 8px; }"));
    statusBar()->addPermanentWidget(m_utcTimeLabel);

    // Clock timer — update every second
    m_clockTimer = new QTimer(this);
    connect(m_clockTimer, &QTimer::timeout, this, [this]() {
        QDateTime utc = QDateTime::currentDateTimeUtc();
        m_utcTimeLabel->setText(utc.toString(QStringLiteral("HH:mm:ss UTC  yyyy-MM-dd")));
    });
    m_clockTimer->start(1000);
    // Fire once immediately
    m_utcTimeLabel->setText(
        QDateTime::currentDateTimeUtc().toString(QStringLiteral("HH:mm:ss UTC  yyyy-MM-dd")));
}
```

- [ ] **Step 3: Add QTimer and QDateTime includes**

At top of MainWindow.cpp, ensure these are included:
```cpp
#include <QDateTime>
```
(QTimer is already included)

- [ ] **Step 4: Build, run, and verify**

```bash
cmake --build build -j$(nproc) && ./build/NereusSDR
```
Expected: Taller status bar (46px). UTC clock updating in real time on the right. Connection status on left. Callsign in center (blank if not set).

- [ ] **Step 5: Commit**

```bash
git add src/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "Expand status bar to double-height with UTC clock and callsign"
```

---

## Task 11: SetupDialog Skeleton

**Files:**
- Create: `src/gui/SetupPage.h`
- Create: `src/gui/SetupPage.cpp`
- Create: `src/gui/SetupDialog.h`
- Create: `src/gui/SetupDialog.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create SetupPage base class**

```cpp
// src/gui/SetupPage.h
#pragma once
#include <QWidget>
class QVBoxLayout;
class QLabel;

namespace NereusSDR {

class RadioModel;

class SetupPage : public QWidget {
    Q_OBJECT
public:
    explicit SetupPage(const QString& title, RadioModel* model, QWidget* parent = nullptr);
    virtual ~SetupPage() = default;

    QString pageTitle() const { return m_title; }
    virtual void syncFromModel();

protected:
    // Subclasses add controls to this layout
    QVBoxLayout* contentLayout() { return m_contentLayout; }
    RadioModel* model() { return m_model; }

    // Add a section header with "N/M wired" progress
    QLabel* addSectionHeader(const QString& name, int wired, int total);

private:
    QString m_title;
    RadioModel* m_model;
    QVBoxLayout* m_contentLayout = nullptr;
};

} // namespace NereusSDR
```

```cpp
// src/gui/SetupPage.cpp
#include "SetupPage.h"
#include <QVBoxLayout>
#include <QLabel>

namespace NereusSDR {

SetupPage::SetupPage(const QString& title, RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_title(title)
    , m_model(model)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 8, 12, 8);
    root->setSpacing(6);

    auto* titleLabel = new QLabel(title, this);
    titleLabel->setStyleSheet(QStringLiteral(
        "QLabel { color: #c8d8e8; font-size: 16px; font-weight: bold;"
        "  border-bottom: 1px solid #304050; padding-bottom: 4px; }"));
    root->addWidget(titleLabel);

    m_contentLayout = new QVBoxLayout;
    m_contentLayout->setSpacing(4);
    root->addLayout(m_contentLayout);
    root->addStretch();
}

QLabel* SetupPage::addSectionHeader(const QString& name, int wired, int total)
{
    auto* label = new QLabel(
        QStringLiteral("%1 (%2/%3 wired)").arg(name).arg(wired).arg(total), this);
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #8aa8c0; font-size: 12px; font-weight: bold;"
        "  margin-top: 8px; }"));
    m_contentLayout->addWidget(label);
    return label;
}

void SetupPage::syncFromModel() { /* Override in subclasses */ }

} // namespace NereusSDR
```

- [ ] **Step 2: Create SetupDialog**

```cpp
// src/gui/SetupDialog.h
#pragma once
#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;

namespace NereusSDR {

class RadioModel;
class SetupPage;

class SetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SetupDialog(RadioModel* model, QWidget* parent = nullptr);

private:
    void buildTree();
    void addPage(QTreeWidgetItem* parent, const QString& name, SetupPage* page);

    RadioModel* m_model;
    QTreeWidget* m_tree = nullptr;
    QStackedWidget* m_stack = nullptr;
};

} // namespace NereusSDR
```

```cpp
// src/gui/SetupDialog.cpp
#include "SetupDialog.h"
#include "SetupPage.h"
#include "models/RadioModel.h"

#include <QTreeWidget>
#include <QStackedWidget>
#include <QSplitter>
#include <QVBoxLayout>
#include <QHeaderView>
#include <QLabel>

namespace NereusSDR {

SetupDialog::SetupDialog(RadioModel* model, QWidget* parent)
    : QDialog(parent)
    , m_model(model)
{
    setWindowTitle(QStringLiteral("NereusSDR Settings"));
    setMinimumSize(800, 600);
    resize(900, 650);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #0f0f1a; }"
        "QTreeWidget {"
        "  background: #131326; color: #c8d8e8;"
        "  border: none; font-size: 12px;"
        "  selection-background-color: #00b4d8;"
        "}"
        "QTreeWidget::item { padding: 4px 8px; }"
        "QTreeWidget::item:hover { background: #1a2a3a; }"));

    auto* splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(1);

    // Left: tree navigation
    m_tree = new QTreeWidget(splitter);
    m_tree->setHeaderHidden(true);
    m_tree->setFixedWidth(200);
    m_tree->setIndentation(16);

    // Right: page stack
    m_stack = new QStackedWidget(splitter);
    m_stack->setStyleSheet(QStringLiteral(
        "QStackedWidget { background: #0f0f1a; }"));

    splitter->addWidget(m_tree);
    splitter->addWidget(m_stack);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(splitter);

    buildTree();

    connect(m_tree, &QTreeWidget::currentItemChanged,
            this, [this](QTreeWidgetItem* current, QTreeWidgetItem*) {
        if (!current) { return; }
        int idx = current->data(0, Qt::UserRole).toInt();
        if (idx >= 0 && idx < m_stack->count()) {
            m_stack->setCurrentIndex(idx);
        }
    });

    // Select first item
    if (m_tree->topLevelItemCount() > 0) {
        m_tree->setCurrentItem(m_tree->topLevelItem(0));
    }
}

void SetupDialog::buildTree()
{
    // 10 top-level categories with NYI sub-pages
    auto* general = new QTreeWidgetItem(m_tree, {QStringLiteral("General")});
    addPage(general, QStringLiteral("Startup & Preferences"),
            new SetupPage(QStringLiteral("Startup & Preferences"), m_model, m_stack));
    addPage(general, QStringLiteral("UI Scale & Theme"),
            new SetupPage(QStringLiteral("UI Scale & Theme"), m_model, m_stack));
    addPage(general, QStringLiteral("Navigation"),
            new SetupPage(QStringLiteral("Navigation"), m_model, m_stack));

    auto* hardware = new QTreeWidgetItem(m_tree, {QStringLiteral("Hardware")});
    addPage(hardware, QStringLiteral("Radio Selection"),
            new SetupPage(QStringLiteral("Radio Selection"), m_model, m_stack));
    addPage(hardware, QStringLiteral("ADC / DDC Configuration"),
            new SetupPage(QStringLiteral("ADC / DDC Configuration"), m_model, m_stack));
    addPage(hardware, QStringLiteral("Calibration"),
            new SetupPage(QStringLiteral("Calibration"), m_model, m_stack));
    addPage(hardware, QStringLiteral("Alex Filters"),
            new SetupPage(QStringLiteral("Alex Filters"), m_model, m_stack));
    addPage(hardware, QStringLiteral("Penny / Hermes OC"),
            new SetupPage(QStringLiteral("Penny / Hermes OC"), m_model, m_stack));
    addPage(hardware, QStringLiteral("Firmware"),
            new SetupPage(QStringLiteral("Firmware"), m_model, m_stack));
    addPage(hardware, QStringLiteral("Other H/W"),
            new SetupPage(QStringLiteral("Other H/W"), m_model, m_stack));

    auto* audio = new QTreeWidgetItem(m_tree, {QStringLiteral("Audio")});
    addPage(audio, QStringLiteral("Device Selection"),
            new SetupPage(QStringLiteral("Device Selection"), m_model, m_stack));
    addPage(audio, QStringLiteral("ASIO Configuration"),
            new SetupPage(QStringLiteral("ASIO Configuration"), m_model, m_stack));
    addPage(audio, QStringLiteral("VAC 1"),
            new SetupPage(QStringLiteral("VAC 1"), m_model, m_stack));
    addPage(audio, QStringLiteral("VAC 2"),
            new SetupPage(QStringLiteral("VAC 2"), m_model, m_stack));
    addPage(audio, QStringLiteral("NereusDAX"),
            new SetupPage(QStringLiteral("NereusDAX"), m_model, m_stack));
    addPage(audio, QStringLiteral("Recording"),
            new SetupPage(QStringLiteral("Recording"), m_model, m_stack));

    auto* dsp = new QTreeWidgetItem(m_tree, {QStringLiteral("DSP")});
    addPage(dsp, QStringLiteral("AGC / ALC"),
            new SetupPage(QStringLiteral("AGC / ALC"), m_model, m_stack));
    addPage(dsp, QStringLiteral("Noise Reduction (NR / ANF)"),
            new SetupPage(QStringLiteral("Noise Reduction (NR / ANF)"), m_model, m_stack));
    addPage(dsp, QStringLiteral("Noise Blanker (NB / SNB)"),
            new SetupPage(QStringLiteral("Noise Blanker (NB / SNB)"), m_model, m_stack));
    addPage(dsp, QStringLiteral("CW"),
            new SetupPage(QStringLiteral("CW"), m_model, m_stack));
    addPage(dsp, QStringLiteral("AM / SAM"),
            new SetupPage(QStringLiteral("AM / SAM"), m_model, m_stack));
    addPage(dsp, QStringLiteral("FM"),
            new SetupPage(QStringLiteral("FM"), m_model, m_stack));
    addPage(dsp, QStringLiteral("VOX / DEXP"),
            new SetupPage(QStringLiteral("VOX / DEXP"), m_model, m_stack));
    addPage(dsp, QStringLiteral("CFC"),
            new SetupPage(QStringLiteral("CFC"), m_model, m_stack));
    addPage(dsp, QStringLiteral("MNF"),
            new SetupPage(QStringLiteral("MNF"), m_model, m_stack));

    auto* display = new QTreeWidgetItem(m_tree, {QStringLiteral("Display")});
    addPage(display, QStringLiteral("Spectrum Defaults"),
            new SetupPage(QStringLiteral("Spectrum Defaults"), m_model, m_stack));
    addPage(display, QStringLiteral("Waterfall Defaults"),
            new SetupPage(QStringLiteral("Waterfall Defaults"), m_model, m_stack));
    addPage(display, QStringLiteral("Grid & Scales"),
            new SetupPage(QStringLiteral("Grid & Scales"), m_model, m_stack));
    addPage(display, QStringLiteral("RX2 Display"),
            new SetupPage(QStringLiteral("RX2 Display"), m_model, m_stack));
    addPage(display, QStringLiteral("TX Display"),
            new SetupPage(QStringLiteral("TX Display"), m_model, m_stack));

    auto* transmit = new QTreeWidgetItem(m_tree, {QStringLiteral("Transmit")});
    addPage(transmit, QStringLiteral("Power & PA"),
            new SetupPage(QStringLiteral("Power & PA"), m_model, m_stack));
    addPage(transmit, QStringLiteral("TX Profiles"),
            new SetupPage(QStringLiteral("TX Profiles"), m_model, m_stack));
    addPage(transmit, QStringLiteral("Speech Processor"),
            new SetupPage(QStringLiteral("Speech Processor"), m_model, m_stack));
    addPage(transmit, QStringLiteral("PureSignal"),
            new SetupPage(QStringLiteral("PureSignal"), m_model, m_stack));

    auto* appearance = new QTreeWidgetItem(m_tree, {QStringLiteral("Appearance")});
    addPage(appearance, QStringLiteral("Colors & Theme"),
            new SetupPage(QStringLiteral("Colors & Theme"), m_model, m_stack));
    addPage(appearance, QStringLiteral("Meter Styles"),
            new SetupPage(QStringLiteral("Meter Styles"), m_model, m_stack));
    addPage(appearance, QStringLiteral("Skins"),
            new SetupPage(QStringLiteral("Skins"), m_model, m_stack));

    auto* catNetwork = new QTreeWidgetItem(m_tree, {QStringLiteral("CAT & Network")});
    addPage(catNetwork, QStringLiteral("Serial Ports"),
            new SetupPage(QStringLiteral("Serial Ports"), m_model, m_stack));
    addPage(catNetwork, QStringLiteral("TCI Server"),
            new SetupPage(QStringLiteral("TCI Server"), m_model, m_stack));
    addPage(catNetwork, QStringLiteral("TCP/IP CAT"),
            new SetupPage(QStringLiteral("TCP/IP CAT"), m_model, m_stack));
    addPage(catNetwork, QStringLiteral("MIDI Control"),
            new SetupPage(QStringLiteral("MIDI Control"), m_model, m_stack));

    auto* keyboard = new QTreeWidgetItem(m_tree, {QStringLiteral("Keyboard")});
    addPage(keyboard, QStringLiteral("Shortcuts"),
            new SetupPage(QStringLiteral("Shortcuts"), m_model, m_stack));

    auto* diagnostics = new QTreeWidgetItem(m_tree, {QStringLiteral("Diagnostics")});
    addPage(diagnostics, QStringLiteral("Signal Generator"),
            new SetupPage(QStringLiteral("Signal Generator"), m_model, m_stack));
    addPage(diagnostics, QStringLiteral("Hardware Tests"),
            new SetupPage(QStringLiteral("Hardware Tests"), m_model, m_stack));
    addPage(diagnostics, QStringLiteral("Logging"),
            new SetupPage(QStringLiteral("Logging"), m_model, m_stack));

    m_tree->expandAll();
}

void SetupDialog::addPage(QTreeWidgetItem* parent, const QString& name, SetupPage* page)
{
    auto* item = new QTreeWidgetItem(parent, {name});
    int idx = m_stack->addWidget(page);
    item->setData(0, Qt::UserRole, idx);
}

} // namespace NereusSDR
```

- [ ] **Step 3: Add to CMakeLists.txt**

Add to `GUI_SOURCES`:
```cmake
    src/gui/SetupPage.cpp
    src/gui/SetupDialog.cpp
```

- [ ] **Step 4: Wire Settings menu action in MainWindow**

In `MainWindow.cpp`, add include:
```cpp
#include "SetupDialog.h"
```

In `buildMenuBar()`, replace the Settings action placeholder with:
```cpp
    fileMenu->addAction(QStringLiteral("&Settings..."), this, [this]() {
        auto* dialog = new SetupDialog(m_radioModel, this);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->show();
    });
```

- [ ] **Step 5: Build, run, and verify**

```bash
cmake --build build -j$(nproc) && ./build/NereusSDR
```
Expected: File → Settings opens the SetupDialog. Tree navigation with 10 categories, ~45 sub-pages. Clicking any sub-page shows the page title. All pages are NYI shells.

- [ ] **Step 6: Commit**

```bash
git add src/gui/SetupPage.h src/gui/SetupPage.cpp \
        src/gui/SetupDialog.h src/gui/SetupDialog.cpp \
        src/gui/MainWindow.cpp CMakeLists.txt
git commit -m "Add SetupDialog with tree navigation and 45 NYI setup pages"
```

---

## Task 12: Final Integration Verification

**Files:** None new — verification only.

- [ ] **Step 1: Full build**

```bash
cd /Users/j.j.boyd/NereusSDR
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```
Expected: clean compile, zero warnings from new files.

- [ ] **Step 2: Launch and verify UI elements**

```bash
./build/NereusSDR
```

Verify checklist:
- [ ] Left overlay panel visible on spectrum (10 buttons, collapsible)
- [ ] BAND flyout opens with grid of HF bands
- [ ] DSP flyout has NR/NB/ANF/SNB toggle buttons
- [ ] Display flyout has color scheme, gain, black level sliders
- [ ] ANT flyout has RX/TX antenna combos and RF gain slider
- [ ] Container #0 (right panel): SMeterWidget at top, RxApplet below, Power/SWR+ALC meters, TxApplet (greyed NYI)
- [ ] RxApplet mode combo changes mode (verify audio changes if radio connected)
- [ ] RxApplet AGC combo and AF gain slider functional
- [ ] Menu bar has 9 menus (File, Radio, View, DSP, Band, Mode, Containers, Tools, Help)
- [ ] Band → HF → 20m tunes to 14 MHz
- [ ] Mode → CWU switches to CW mode
- [ ] DSP → NR checkbox toggles noise reduction
- [ ] File → Settings opens SetupDialog with tree navigation
- [ ] Status bar is double-height (46px) with UTC clock
- [ ] Disabled menu items show greyed text
- [ ] NYI applet controls show amber "NYI" badges with phase tooltips

- [ ] **Step 3: Verify no regressions**

If radio is available:
- [ ] Radio connects and streams I/Q
- [ ] Spectrum and waterfall render normally
- [ ] VFO widget still tunes correctly
- [ ] Audio output works (SSB demodulation)
- [ ] Container docking/floating still works
- [ ] App closes cleanly

- [ ] **Step 4: Final commit (if any fixups needed)**

```bash
git add -A
git commit -m "Phase 3-UI integration fixups"
```

---

## Summary

| Task | Description | New Files | Commit |
|---|---|---|---|
| 1 | AppletWidget base class | 2 | `feat: add AppletWidget base class` |
| 2 | NyiOverlay badge utility | 2 | `feat: add NyiOverlay badge utility` |
| 3 | RxApplet with Tier 1 wiring | 2 | `feat: add RxApplet with Tier 1 controls` |
| 4 | TxApplet NYI shell | 2 | `feat: add TxApplet NYI shell` |
| 5 | 10 remaining applet shells | 20 | `feat: add 10 applet NYI shells` |
| 6 | ContainerManager mixed content + default layout | 0 (mods) | `feat: wire mixed Container #0` |
| 7 | SpectrumOverlayPanel | 2 | `feat: add SpectrumOverlayPanel` |
| 8 | Wire overlay to MainWindow | 0 (mods) | `feat: wire overlay to MainWindow` |
| 9 | Menu bar expansion (9 menus) | 0 (mods) | `feat: expand menu bar to 9 menus` |
| 10 | Status bar expansion | 0 (mods) | `feat: expand status bar` |
| 11 | SetupDialog + 45 pages | 4 | `feat: add SetupDialog with 45 pages` |
| 12 | Integration verification | 0 | fixups only |

**Total: ~34 new files, ~12 commits**

---

## Deferred to Follow-up

These spec items are intentionally deferred from this plan to keep scope manageable:

- **Waterfall zoom buttons** ([S][B][-][+]) — the existing zoom bar + Ctrl+scroll already provides this. Dedicated buttons can be added as a follow-up.
- **Toggle row** for applet visibility — requires the full applet show/hide system which depends on container settings (Phase 3G-6).
- **BandStackPanel** — requires band stacking register system, deferred to Phase 3F.
- **Per-Setup-page controls** — all Setup pages are blank NYI shells. Individual page controls (the ~255 items from the spec) get wired progressively as each backend phase completes.
