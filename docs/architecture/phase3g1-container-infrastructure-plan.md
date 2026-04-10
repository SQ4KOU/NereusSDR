# Phase 3G-1: Container Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the dock/float/resize/persist container shell framework that all meters, buttons, and controls will live in — the NereusSDR equivalent of Thetis's ucMeter + frmMeterDisplay + MeterManager.

**Architecture:** Three classes — `ContainerWidget` (the container shell with title bar, drag, resize, axis-lock), `FloatingContainer` (a top-level QWidget wrapper for popped-out containers), and `ContainerManager` (singleton that owns all containers, handles float/dock transitions, persistence, and axis-lock repositioning on main window resize). Default layout creates a single right-side Container #0 with placeholder content. All state persists to AppSettings.

**Tech Stack:** C++20, Qt6 Widgets, AppSettings (XML persistence), QLoggingCategory

---

## Thetis Source Reference

This plan ports container infrastructure from these Thetis files:

| NereusSDR Class | Thetis Source | Key Functions Ported |
|---|---|---|
| `ContainerWidget` | `ucMeter.cs` (1238 lines) | Title bar show/hide on hover, drag (docked + floating), resize via grab handle, axis-lock cycling, pin-on-top, serialization (ToString/TryParse) |
| `FloatingContainer` | `frmMeterDisplay.cs` (208 lines) | TakeOwner reparenting, TopMost, minimize-with-console, DPI positioning, geometry save/restore |
| `ContainerManager` | `MeterManager.cs:244-6600` | AddMeterContainer, setMeterFloating, returnMeterFromFloating, SetPositionOfDockedMeters, StoreSettings2, RestoreSettings, RemoveMeterContainer, RecoverContainer |

## File Structure

### New Files

| File | Responsibility |
|---|---|
| `src/gui/containers/ContainerWidget.h` | Container shell widget — title bar, drag, resize, axis-lock, properties, serialization. Equivalent to Thetis `ucMeter` user control. |
| `src/gui/containers/ContainerWidget.cpp` | Implementation of all ContainerWidget behavior |
| `src/gui/containers/FloatingContainer.h` | Top-level window wrapper for popped-out containers. Equivalent to Thetis `frmMeterDisplay` Form. |
| `src/gui/containers/FloatingContainer.cpp` | Implementation of FloatingContainer |
| `src/gui/containers/ContainerManager.h` | Singleton lifecycle manager — create/destroy, float/dock, axis-lock reposition, persistence. Equivalent to Thetis `MeterManager` static class. |
| `src/gui/containers/ContainerManager.cpp` | Implementation of ContainerManager |

### Modified Files

| File | Changes |
|---|---|
| `CMakeLists.txt` | Add 3 new .cpp files to `GUI_SOURCES` |
| `src/core/LogCategories.h` | Add `lcContainer` logging category declaration |
| `src/core/LogCategories.cpp` | Define `lcContainer` logging category + register in LogManager |
| `src/gui/MainWindow.h` | Add `ContainerManager*` member, resize override |
| `src/gui/MainWindow.cpp` | Create ContainerManager, wire up, create default Container #0 as right panel, handle resize |

---

## Task 1: Logging Category + CMake Skeleton

**Files:**
- Modify: `src/core/LogCategories.h:18`
- Modify: `src/core/LogCategories.cpp:19`
- Create: `src/gui/containers/ContainerWidget.h`
- Create: `src/gui/containers/ContainerWidget.cpp`
- Create: `src/gui/containers/FloatingContainer.h`
- Create: `src/gui/containers/FloatingContainer.cpp`
- Create: `src/gui/containers/ContainerManager.h`
- Create: `src/gui/containers/ContainerManager.cpp`
- Modify: `CMakeLists.txt:188-195`

- [ ] **Step 1: Add lcContainer logging category**

In `src/core/LogCategories.h`, add after the `lcSpectrum` declaration (line 18):

```cpp
Q_DECLARE_LOGGING_CATEGORY(lcContainer)
```

In `src/core/LogCategories.cpp`, add after the `lcSpectrum` definition (line 19):

```cpp
Q_LOGGING_CATEGORY(lcContainer, "nereus.container")
```

In the `LogManager` constructor's `m_categories` list (around line 48), add before the closing brace:

```cpp
        { QStringLiteral("nereus.container"), QStringLiteral("Container"),
          QStringLiteral("Container dock/float/resize, lifecycle, persistence"), false },
```

- [ ] **Step 2: Create stub files for all three classes**

Create `src/gui/containers/ContainerWidget.h`:

```cpp
#pragma once

#include <QWidget>
#include <QPoint>
#include <QSize>
#include <QColor>
#include <QString>

namespace NereusSDR {

// From Thetis ucMeter.cs:49-59 — axis lock positions for docked containers.
// Determines which corner/edge the container anchors to when the main window resizes.
enum class AxisLock {
    Left = 0,
    TopLeft,
    Top,
    TopRight,
    Right,
    BottomRight,
    Bottom,
    BottomLeft
};

class FloatingContainer;

// Container shell widget — equivalent to Thetis ucMeter.
// Provides title bar, drag, resize, axis-lock, and serialization.
// Content area holds placeholder (3G-1) or meter items (3G-2+).
class ContainerWidget : public QWidget {
    Q_OBJECT

public:
    // From Thetis ucMeter.cs:62-63
    static constexpr int kMinContainerWidth = 24;
    static constexpr int kMinContainerHeight = 24;

    explicit ContainerWidget(QWidget* parent = nullptr);
    ~ContainerWidget() override;

    // --- Identity ---
    QString id() const { return m_id; }
    void setId(const QString& id);
    int rxSource() const { return m_rxSource; }
    void setRxSource(int rx);

    // --- Float/Dock ---
    bool isFloating() const { return m_floating; }
    void setFloating(bool floating);

    // --- Axis Lock (docked position anchoring) ---
    // From Thetis ucMeter.cs:800-808
    AxisLock axisLock() const { return m_axisLock; }
    void setAxisLock(AxisLock lock);
    void cycleAxisLock(bool reverse = false);

    // --- Pin on Top (floating only) ---
    // From Thetis ucMeter.cs:790-798
    bool isPinOnTop() const { return m_pinOnTop; }
    void setPinOnTop(bool pin);

    // --- Container Properties ---
    // From Thetis ucMeter.cs:809-911
    bool hasBorder() const { return m_border; }
    void setBorder(bool border);
    bool isLocked() const { return m_locked; }
    void setLocked(bool locked);
    bool isContainerEnabled() const { return m_enabled; }
    void setContainerEnabled(bool enabled);
    bool showOnRx() const { return m_showOnRx; }
    void setShowOnRx(bool show);
    bool showOnTx() const { return m_showOnTx; }
    void setShowOnTx(bool show);
    bool isHiddenByMacro() const { return m_hiddenByMacro; }
    void setHiddenByMacro(bool hidden);
    bool containerMinimises() const { return m_containerMinimises; }
    void setContainerMinimises(bool minimises);
    bool containerHidesWhenRxNotUsed() const { return m_containerHidesWhenRxNotUsed; }
    void setContainerHidesWhenRxNotUsed(bool hides);
    QString notes() const { return m_notes; }
    void setNotes(const QString& notes);
    bool noControls() const { return m_noControls; }
    void setNoControls(bool noControls);
    bool autoHeight() const { return m_autoHeight; }
    void setAutoHeight(bool autoHeight);
    int containerHeight() const { return m_containerHeight; }

    // --- Docked Position (stored separately from live position) ---
    // From Thetis ucMeter.cs:550-593
    QPoint dockedLocation() const { return m_dockedLocation; }
    void setDockedLocation(const QPoint& loc);
    QSize dockedSize() const { return m_dockedSize; }
    void setDockedSize(const QSize& size);
    void storeLocation();
    void restoreLocation();

    // --- Delta (console resize offset for axis-lock) ---
    // From Thetis ucMeter.cs:784-789
    QPoint delta() const { return m_delta; }
    void setDelta(const QPoint& delta);

    // --- Content Area ---
    QWidget* contentArea() const { return m_contentArea; }

    // --- Serialization ---
    // From Thetis ucMeter.cs:1012-1038 (ToString) and 1039-1160 (TryParse)
    // Pipe-delimited string for AppSettings persistence.
    QString serialize() const;
    bool deserialize(const QString& data);

signals:
    void floatingChanged(bool floating);
    void floatRequested();
    void dockRequested();
    void settingsRequested();
    void dockedMoved();

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void buildUI();
    void updateTitleBar();
    void updateTitle();
    void setupBorder();
    void setTopMost();

    // --- Title bar drag ---
    // From Thetis ucMeter.cs:281-375
    void beginDrag(const QPoint& pos);
    void updateDrag(const QPoint& globalPos);
    void endDrag();

    // --- Resize grab ---
    // From Thetis ucMeter.cs:400-549
    void beginResize(const QPoint& globalPos);
    void updateResize(const QPoint& globalPos);
    void endResize();
    void doResize(int w, int h);

    // Snap to 10px grid when Ctrl held
    // From Thetis ucMeter.cs:390-393
    static int roundToNearestTen(int value);

    // --- Identity ---
    QString m_id;
    int m_rxSource{1};

    // --- State ---
    bool m_floating{false};
    bool m_dragging{false};
    bool m_resizing{false};
    AxisLock m_axisLock{AxisLock::TopLeft};
    bool m_pinOnTop{false};

    // --- Properties ---
    // From Thetis ucMeter.cs:95-103, defaults match Thetis constructor
    bool m_border{true};
    bool m_noControls{false};
    bool m_locked{false};
    bool m_enabled{true};
    bool m_showOnRx{true};
    bool m_showOnTx{true};
    bool m_hiddenByMacro{false};
    bool m_containerMinimises{true};
    bool m_containerHidesWhenRxNotUsed{true};
    QString m_notes;
    int m_containerHeight{kMinContainerHeight};
    bool m_autoHeight{false};

    // --- Drag/Resize state ---
    QPoint m_dragStartPos;
    QPoint m_resizeStartGlobal;
    QSize m_resizeStartSize;

    // --- Docked geometry (persisted separately from live pos) ---
    QPoint m_dockedLocation;
    QSize m_dockedSize;
    QPoint m_delta;

    // --- UI elements ---
    QWidget* m_titleBar{nullptr};
    QWidget* m_contentArea{nullptr};
    QWidget* m_resizeGrip{nullptr};
    class QLabel* m_titleLabel{nullptr};
    class QPushButton* m_btnFloat{nullptr};
    class QPushButton* m_btnAxis{nullptr};
    class QPushButton* m_btnPin{nullptr};
    class QPushButton* m_btnSettings{nullptr};
};

} // namespace NereusSDR
```

Create `src/gui/containers/ContainerWidget.cpp`:

```cpp
#include "ContainerWidget.h"
#include "core/LogCategories.h"

#include <QUuid>

namespace NereusSDR {

ContainerWidget::ContainerWidget(QWidget* parent)
    : QWidget(parent)
{
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    buildUI();
}

ContainerWidget::~ContainerWidget() = default;

} // namespace NereusSDR
```

Create `src/gui/containers/FloatingContainer.h`:

```cpp
#pragma once

#include <QWidget>

namespace NereusSDR {

class ContainerWidget;

// Floating window wrapper — equivalent to Thetis frmMeterDisplay.
// Hosts a ContainerWidget when it is popped out of the main window.
class FloatingContainer : public QWidget {
    Q_OBJECT

public:
    explicit FloatingContainer(QWidget* parent = nullptr);
    ~FloatingContainer() override;

    QString id() const { return m_id; }
    void setId(const QString& id);

    void takeOwner(ContainerWidget* container);

    bool containerMinimises() const { return m_containerMinimises; }
    void setContainerMinimises(bool minimises);

signals:
    void aboutToClose();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    QString m_id;
    bool m_containerMinimises{true};
};

} // namespace NereusSDR
```

Create `src/gui/containers/FloatingContainer.cpp`:

```cpp
#include "FloatingContainer.h"
#include "ContainerWidget.h"
#include "core/LogCategories.h"

namespace NereusSDR {

FloatingContainer::FloatingContainer(QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
{
    setMinimumSize(ContainerWidget::kMinContainerWidth,
                   ContainerWidget::kMinContainerHeight);
}

FloatingContainer::~FloatingContainer() = default;

} // namespace NereusSDR
```

Create `src/gui/containers/ContainerManager.h`:

```cpp
#pragma once

#include <QObject>
#include <QList>
#include <QMap>
#include <QSize>

namespace NereusSDR {

class ContainerWidget;
class FloatingContainer;

// Singleton lifecycle manager — equivalent to Thetis MeterManager (container portion).
// Owns all containers, handles float/dock transitions, axis-lock repositioning,
// and persistence to AppSettings.
class ContainerManager : public QObject {
    Q_OBJECT

public:
    explicit ContainerManager(QWidget* dockParent, QObject* parent = nullptr);
    ~ContainerManager() override;

    // --- Container lifecycle ---
    ContainerWidget* createContainer(int rxSource, bool floating = false);
    void destroyContainer(const QString& id);

    // --- Float/Dock ---
    void floatContainer(const QString& id);
    void dockContainer(const QString& id);
    void recoverContainer(const QString& id);

    // --- Axis-lock repositioning on main window resize ---
    void updateDockedPositions(int hDelta, int vDelta);

    // --- Persistence ---
    void saveState();
    void restoreState();

    // --- Queries ---
    QList<ContainerWidget*> allContainers() const;
    ContainerWidget* container(const QString& id) const;
    int containerCount() const;

    // --- Visibility ---
    void setContainerVisible(const QString& id, bool visible);

signals:
    void containerAdded(const QString& id);
    void containerRemoved(const QString& id);

private:
    QWidget* m_dockParent{nullptr};
    QMap<QString, ContainerWidget*> m_containers;
    QMap<QString, FloatingContainer*> m_floatingForms;
};

} // namespace NereusSDR
```

Create `src/gui/containers/ContainerManager.cpp`:

```cpp
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/LogCategories.h"

namespace NereusSDR {

ContainerManager::ContainerManager(QWidget* dockParent, QObject* parent)
    : QObject(parent)
    , m_dockParent(dockParent)
{
}

ContainerManager::~ContainerManager() = default;

} // namespace NereusSDR
```

- [ ] **Step 3: Add new sources to CMakeLists.txt**

In `CMakeLists.txt`, add to the `GUI_SOURCES` list (after `src/gui/widgets/VfoWidget.cpp`):

```cmake
    src/gui/containers/ContainerWidget.cpp
    src/gui/containers/FloatingContainer.cpp
    src/gui/containers/ContainerManager.cpp
```

- [ ] **Step 4: Verify build compiles**

Run:
```bash
cd /c/Users/boyds/NereusSDR && cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -5
cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds with no errors. New .o files created for the 3 container sources.

- [ ] **Step 5: Commit skeleton**

```bash
git add src/gui/containers/ src/core/LogCategories.h src/core/LogCategories.cpp CMakeLists.txt
git commit -m "feat(3G-1): add container infrastructure skeleton — ContainerWidget, FloatingContainer, ContainerManager"
```

---

## Task 2: ContainerWidget — UI Layout and Title Bar

**Files:**
- Modify: `src/gui/containers/ContainerWidget.h`
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — constructor (lines 108-165), `setTopBarButtons()` (lines 605-624), `setTitle()` (lines 625-639), `setupBorder()` (lines 640-643), `pnlContainer_MouseMove()` (lines 1198-1229 — hover show/hide of title bar and resize grip).

- [ ] **Step 1: Implement buildUI()**

The container has three layers: title bar (top, hidden until hover), content area (fills remaining space), and resize grip (bottom-right corner, hidden until hover). This mirrors Thetis's pnlBar, pnlContainer, and pbGrab.

In `ContainerWidget.cpp`, replace the constructor body and add `buildUI()`:

```cpp
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/LogCategories.h"

#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QApplication>

namespace NereusSDR {

ContainerWidget::ContainerWidget(QWidget* parent)
    : QWidget(parent)
{
    m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    setMinimumSize(kMinContainerWidth, kMinContainerHeight);
    setMouseTracking(true);
    buildUI();
    updateTitleBar();
    updateTitle();
    setupBorder();
    storeLocation();

    qCDebug(lcContainer) << "Container created:" << m_id;
}

ContainerWidget::~ContainerWidget()
{
    qCDebug(lcContainer) << "Container destroyed:" << m_id;
}

void ContainerWidget::buildUI()
{
    // Main layout: title bar on top, content area fills rest
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // --- Title bar (shown on hover, hidden by default) ---
    // From Thetis ucMeter.cs:156 — pnlBar.Hide()
    m_titleBar = new QWidget(this);
    m_titleBar->setFixedHeight(22);
    m_titleBar->setVisible(false);
    m_titleBar->setStyleSheet(QStringLiteral(
        "background: #1a2a3a; border-bottom: 1px solid #203040;"));

    auto* barLayout = new QHBoxLayout(m_titleBar);
    barLayout->setContentsMargins(4, 0, 0, 0);
    barLayout->setSpacing(2);

    // Title label — shows "RX1" or "TX1" + notes
    // From Thetis ucMeter.cs:625-639
    m_titleLabel = new QLabel(QStringLiteral("RX1"), m_titleBar);
    m_titleLabel->setStyleSheet(QStringLiteral(
        "color: #c8d8e8; font-size: 11px; font-weight: bold; background: transparent;"));
    m_titleLabel->setCursor(Qt::SizeAllCursor);
    barLayout->addWidget(m_titleLabel, 1);

    // Button style shared by all title bar buttons
    const QString btnStyle = QStringLiteral(
        "QPushButton { background: transparent; border: none; color: #8090a0;"
        "  font-size: 11px; padding: 2px 4px; }"
        "QPushButton:hover { background: #2a3a4a; color: #c8d8e8; }");

    // Axis lock button (docked only) — cycles through 8 positions
    // From Thetis ucMeter.cs:912-968
    m_btnAxis = new QPushButton(QStringLiteral("\u2196"), m_titleBar);  // ↖ default TopLeft
    m_btnAxis->setFixedSize(22, 22);
    m_btnAxis->setToolTip(QStringLiteral("Axis lock (click to cycle, right-click reverse)"));
    m_btnAxis->setStyleSheet(btnStyle);
    barLayout->addWidget(m_btnAxis);

    // Pin-on-top button (floating only)
    // From Thetis ucMeter.cs:969-993
    m_btnPin = new QPushButton(QStringLiteral("\U0001F4CC"), m_titleBar);  // 📌
    m_btnPin->setFixedSize(22, 22);
    m_btnPin->setToolTip(QStringLiteral("Pin on top"));
    m_btnPin->setStyleSheet(btnStyle);
    m_btnPin->setVisible(false);
    barLayout->addWidget(m_btnPin);

    // Float/Dock toggle button
    // From Thetis ucMeter.cs:644-647
    m_btnFloat = new QPushButton(QStringLiteral("\u2197"), m_titleBar);  // ↗ float icon
    m_btnFloat->setFixedSize(22, 22);
    m_btnFloat->setToolTip(QStringLiteral("Float / Dock"));
    m_btnFloat->setStyleSheet(btnStyle);
    barLayout->addWidget(m_btnFloat);

    // Settings gear button
    // From Thetis ucMeter.cs:1176-1179
    m_btnSettings = new QPushButton(QStringLiteral("\u2699"), m_titleBar);  // ⚙
    m_btnSettings->setFixedSize(22, 22);
    m_btnSettings->setToolTip(QStringLiteral("Container settings"));
    m_btnSettings->setStyleSheet(btnStyle);
    barLayout->addWidget(m_btnSettings);

    mainLayout->addWidget(m_titleBar);

    // --- Content area ---
    m_contentArea = new QWidget(this);
    m_contentArea->setMouseTracking(true);
    m_contentArea->setStyleSheet(QStringLiteral("background: #0f0f1a;"));

    // Placeholder label for 3G-1 (replaced by MeterWidget in 3G-2)
    auto* placeholder = new QLabel(QStringLiteral("Container"), m_contentArea);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet(QStringLiteral(
        "color: #405060; font-size: 14px; background: transparent;"));
    auto* contentLayout = new QVBoxLayout(m_contentArea);
    contentLayout->addWidget(placeholder);
    mainLayout->addWidget(m_contentArea, 1);

    // --- Resize grip (bottom-right, hidden until hover) ---
    // From Thetis ucMeter.cs:157 — pbGrab.Hide()
    m_resizeGrip = new QWidget(this);
    m_resizeGrip->setFixedSize(12, 12);
    m_resizeGrip->setCursor(Qt::SizeFDiagCursor);
    m_resizeGrip->setStyleSheet(QStringLiteral(
        "background: #405060; border-radius: 2px;"));
    m_resizeGrip->setVisible(false);
    // Position in bottom-right — updated in resizeEvent would be needed,
    // but we position it absolutely in mouseMoveEvent instead.

    // --- Wire button signals ---
    connect(m_btnFloat, &QPushButton::clicked, this, [this]() {
        if (m_floating) {
            emit dockRequested();
        } else {
            emit floatRequested();
        }
    });

    connect(m_btnAxis, &QPushButton::clicked, this, [this]() {
        cycleAxisLock(QApplication::keyboardModifiers() & Qt::ShiftModifier);
    });

    connect(m_btnPin, &QPushButton::clicked, this, [this]() {
        setPinOnTop(!m_pinOnTop);
    });

    connect(m_btnSettings, &QPushButton::clicked, this, [this]() {
        emit settingsRequested();
    });
}

void ContainerWidget::updateTitleBar()
{
    // From Thetis ucMeter.cs:605-624 — setTopBarButtons()
    if (m_floating) {
        m_btnFloat->setText(QStringLiteral("\u2199"));  // ↙ dock icon
        m_btnFloat->setToolTip(QStringLiteral("Dock"));
        m_btnAxis->setVisible(false);
        m_btnPin->setVisible(true);
    } else {
        m_btnFloat->setText(QStringLiteral("\u2197"));  // ↗ float icon
        m_btnFloat->setToolTip(QStringLiteral("Float"));
        m_btnAxis->setVisible(true);
        m_btnPin->setVisible(false);
    }
}

void ContainerWidget::updateTitle()
{
    // From Thetis ucMeter.cs:625-639
    QString prefix = QStringLiteral("RX");  // TX prefix added in 3I when MOX state exists
    QString firstLine = m_notes.section(QLatin1Char('\n'), 0, 0);
    QString title = prefix + QString::number(m_rxSource);
    if (!firstLine.isEmpty()) {
        title += QStringLiteral(" ") + firstLine;
    }
    m_titleLabel->setText(title);
}

void ContainerWidget::setupBorder()
{
    // From Thetis ucMeter.cs:640-643
    if (m_border) {
        setStyleSheet(QStringLiteral("ContainerWidget { border: 1px solid #203040; }"));
    } else {
        setStyleSheet(QString());
    }
}
```

- [ ] **Step 2: Implement property setters**

Append to `ContainerWidget.cpp`:

```cpp
void ContainerWidget::setId(const QString& id)
{
    // From Thetis ucMeter.cs:258-261 — strip pipes (serialization delimiter)
    m_id = id;
    m_id.remove(QLatin1Char('|'));
}

void ContainerWidget::setRxSource(int rx)
{
    m_rxSource = rx;
    updateTitle();
}

void ContainerWidget::setFloating(bool floating)
{
    if (m_floating == floating) {
        return;
    }
    m_floating = floating;
    updateTitleBar();
    setTopMost();
    emit floatingChanged(floating);
}

void ContainerWidget::setAxisLock(AxisLock lock)
{
    m_axisLock = lock;

    // Update axis button icon
    // From Thetis ucMeter.cs:936-968 — setAxisButton()
    static const QChar arrows[] = {
        QChar(0x2190),  // ← LEFT
        QChar(0x2196),  // ↖ TOPLEFT
        QChar(0x2191),  // ↑ TOP
        QChar(0x2197),  // ↗ TOPRIGHT
        QChar(0x2192),  // → RIGHT
        QChar(0x2198),  // ↘ BOTTOMRIGHT
        QChar(0x2193),  // ↓ BOTTOM
        QChar(0x2199),  // ↙ BOTTOMLEFT
    };
    int idx = static_cast<int>(lock);
    if (idx >= 0 && idx < 8) {
        m_btnAxis->setText(QString(arrows[idx]));
    }
}

void ContainerWidget::cycleAxisLock(bool reverse)
{
    // From Thetis ucMeter.cs:912-935
    int n = static_cast<int>(m_axisLock);
    if (reverse) {
        n--;
    } else {
        n++;
    }
    if (n > static_cast<int>(AxisLock::BottomLeft)) {
        n = static_cast<int>(AxisLock::Left);
    }
    if (n < static_cast<int>(AxisLock::Left)) {
        n = static_cast<int>(AxisLock::BottomLeft);
    }
    setAxisLock(static_cast<AxisLock>(n));

    // Reset delta on axis change — from Thetis ucMeter.cs:930-934
    emit dockedMoved();
}

void ContainerWidget::setPinOnTop(bool pin)
{
    // From Thetis ucMeter.cs:974-978
    m_pinOnTop = pin;
    m_btnPin->setText(pin ? QStringLiteral("\U0001F4CD") : QStringLiteral("\U0001F4CC"));  // 📍 vs 📌
    setTopMost();
}

void ContainerWidget::setTopMost()
{
    // From Thetis ucMeter.cs:980-993
    if (m_floating && parentWidget()) {
        auto* fc = qobject_cast<FloatingContainer*>(parentWidget());
        if (fc) {
            bool wasVisible = fc->isVisible();
            if (m_pinOnTop) {
                fc->setWindowFlags(fc->windowFlags() | Qt::WindowStaysOnTopHint);
            } else {
                fc->setWindowFlags(fc->windowFlags() & ~Qt::WindowStaysOnTopHint);
            }
            // setWindowFlags hides the widget — restore visibility
            if (wasVisible) {
                fc->show();
            }
        }
    }
}

void ContainerWidget::setBorder(bool border)
{
    m_border = border;
    setupBorder();
}

void ContainerWidget::setLocked(bool locked) { m_locked = locked; }
void ContainerWidget::setContainerEnabled(bool enabled) { m_enabled = enabled; }
void ContainerWidget::setShowOnRx(bool show) { m_showOnRx = show; }
void ContainerWidget::setShowOnTx(bool show) { m_showOnTx = show; }
void ContainerWidget::setHiddenByMacro(bool hidden) { m_hiddenByMacro = hidden; }
void ContainerWidget::setContainerMinimises(bool minimises) { m_containerMinimises = minimises; }
void ContainerWidget::setContainerHidesWhenRxNotUsed(bool hides) { m_containerHidesWhenRxNotUsed = hides; }

void ContainerWidget::setNotes(const QString& notes)
{
    // From Thetis ucMeter.cs:888-892 — strip pipes
    m_notes = notes;
    m_notes.remove(QLatin1Char('|'));
    updateTitle();
}

void ContainerWidget::setNoControls(bool noControls) { m_noControls = noControls; }

void ContainerWidget::setAutoHeight(bool autoHeight)
{
    m_autoHeight = autoHeight;
}

void ContainerWidget::setDockedLocation(const QPoint& loc) { m_dockedLocation = loc; }
void ContainerWidget::setDockedSize(const QSize& size) { m_dockedSize = size; }

void ContainerWidget::storeLocation()
{
    // From Thetis ucMeter.cs:567-572
    m_dockedLocation = pos();
    m_dockedSize = size();
}

void ContainerWidget::restoreLocation()
{
    // From Thetis ucMeter.cs:574-593
    bool moved = false;
    if (m_dockedLocation != pos()) {
        move(m_dockedLocation);
        moved = true;
    }
    if (m_dockedSize != size()) {
        resize(m_dockedSize);
        moved = true;
    }
    if (moved) {
        update();
    }
}

void ContainerWidget::setDelta(const QPoint& delta) { m_delta = delta; }

int ContainerWidget::roundToNearestTen(int value)
{
    // From Thetis ucMeter.cs:390-393
    return ((value + 5) / 10) * 10;
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement ContainerWidget UI layout, title bar, and property setters"
```

---

## Task 3: ContainerWidget — Drag, Resize, and Hover

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — `pnlBar_MouseDown/Move/Up` (lines 281-375), `pbGrab_MouseDown/Move/Up` (lines 400-549), `pnlContainer_MouseMove` (lines 1198-1229), `lblRX_MouseDown/Move/Up` (lines 687-762).

- [ ] **Step 1: Implement hover show/hide for title bar and resize grip**

The Thetis pattern: title bar and resize grip are hidden. When the mouse enters their area, they appear. When the mouse leaves the entire container, they hide. This requires `mouseMoveEvent` on the container itself.

Add to `ContainerWidget.cpp`:

```cpp
void ContainerWidget::mouseMoveEvent(QMouseEvent* event)
{
    // From Thetis ucMeter.cs:1198-1229 — pnlContainer_MouseMove
    // Show/hide title bar and resize grip based on mouse position.
    bool noControls = m_noControls && !(QApplication::keyboardModifiers() & Qt::ShiftModifier);

    if (!m_dragging && !noControls) {
        // Title bar region: top 22px
        bool inTitleRegion = event->position().y() < 22;
        if (inTitleRegion && !m_titleBar->isVisible()) {
            m_titleBar->setVisible(true);
            m_titleBar->raise();
        } else if (!inTitleRegion && m_titleBar->isVisible() && !m_dragging) {
            m_titleBar->setVisible(false);
        }
    }

    if (!m_resizing && !noControls) {
        // Resize grip region: bottom-right 16x16
        bool inGripRegion = event->position().x() > (width() - 16)
                         && event->position().y() > (height() - 16);
        if (inGripRegion && !m_resizeGrip->isVisible()) {
            m_resizeGrip->move(width() - 12, height() - 12);
            m_resizeGrip->setVisible(true);
            m_resizeGrip->raise();
        } else if (!inGripRegion && m_resizeGrip->isVisible() && !m_resizing) {
            m_resizeGrip->setVisible(false);
        }
    }

    // Handle active drag
    if (m_dragging) {
        updateDrag(event->globalPosition().toPoint());
    }

    // Handle active resize
    if (m_resizing) {
        updateResize(event->globalPosition().toPoint());
    }

    QWidget::mouseMoveEvent(event);
}

void ContainerWidget::leaveEvent(QEvent* event)
{
    // From Thetis ucMeter.cs:1192-1196 — ucMeter_MouseLeave
    if (!m_dragging && !m_resizing) {
        m_titleBar->setVisible(false);
        m_resizeGrip->setVisible(false);
    }
    QWidget::leaveEvent(event);
}
```

- [ ] **Step 2: Implement drag (title bar and title label)**

Wire mouse events from the title bar. We use event filters since the title bar child widgets receive mouse events. Add to `buildUI()` at the end, before the closing brace:

```cpp
    // Install event filter for title bar drag + resize grip interaction
    m_titleBar->installEventFilter(this);
    m_titleLabel->installEventFilter(this);
    m_resizeGrip->installEventFilter(this);
```

Add the `eventFilter` declaration to `ContainerWidget.h` in the `protected:` section:

```cpp
    bool eventFilter(QObject* watched, QEvent* event) override;
```

Then implement it in `ContainerWidget.cpp`:

```cpp
bool ContainerWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Title bar drag (pnlBar or lblRX in Thetis)
    if ((watched == m_titleBar || watched == m_titleLabel) && !m_locked) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                beginDrag(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_dragging) {
                auto* me = static_cast<QMouseEvent*>(event);
                updateDrag(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (m_dragging) {
                endDrag();
                return true;
            }
        }
    }

    // Resize grip
    if (watched == m_resizeGrip && !m_locked) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                beginResize(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseMove) {
            if (m_resizing) {
                auto* me = static_cast<QMouseEvent*>(event);
                updateResize(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            if (m_resizing) {
                endResize();
                return true;
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}
```

- [ ] **Step 3: Implement drag logic**

```cpp
void ContainerWidget::beginDrag(const QPoint& globalPos)
{
    // From Thetis ucMeter.cs:281-294
    m_dragging = true;
    if (m_floating) {
        m_dragStartPos = globalPos - parentWidget()->pos();
    } else {
        raise();
        m_dragStartPos = globalPos - pos();
    }
}

void ContainerWidget::updateDrag(const QPoint& globalPos)
{
    if (!m_dragging) {
        return;
    }

    if (m_floating) {
        // From Thetis ucMeter.cs:319-345 — move parent form
        QPoint newPos = globalPos - m_dragStartPos;
        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            newPos.setX(roundToNearestTen(newPos.x()));
            newPos.setY(roundToNearestTen(newPos.y()));
        }
        if (parentWidget() && parentWidget()->pos() != newPos) {
            parentWidget()->move(newPos);
        }
    } else {
        // From Thetis ucMeter.cs:346-374 — move within parent, clamped
        QPoint newPos = globalPos - m_dragStartPos;
        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            newPos.setX(roundToNearestTen(newPos.x()));
            newPos.setY(roundToNearestTen(newPos.y()));
        }
        if (parentWidget()) {
            // Clamp to parent bounds — from Thetis ucMeter.cs:361-364
            int maxX = parentWidget()->width() - width();
            int maxY = parentWidget()->height() - height();
            newPos.setX(std::clamp(newPos.x(), 0, std::max(0, maxX)));
            newPos.setY(std::clamp(newPos.y(), 0, std::max(0, maxY)));
        }
        if (pos() != newPos) {
            move(newPos);
            update();
        }
    }
}

void ContainerWidget::endDrag()
{
    // From Thetis ucMeter.cs:309-315
    m_dragging = false;
    m_dragStartPos = QPoint();
    if (!m_floating) {
        m_dockedLocation = pos();
        emit dockedMoved();
    }
}
```

- [ ] **Step 4: Implement resize logic**

```cpp
void ContainerWidget::beginResize(const QPoint& globalPos)
{
    // From Thetis ucMeter.cs:400-407
    m_resizeStartGlobal = globalPos;
    m_resizeStartSize = m_floating && parentWidget() ? parentWidget()->size() : size();
    m_resizing = true;
    raise();
}

void ContainerWidget::updateResize(const QPoint& globalPos)
{
    if (!m_resizing) {
        return;
    }

    // From Thetis ucMeter.cs:489-518
    int dX = globalPos.x() - m_resizeStartGlobal.x();
    int dY = globalPos.y() - m_resizeStartGlobal.y();

    int newW = m_resizeStartSize.width() + dX;
    int newH = m_resizeStartSize.height() + dY;

    if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
        newW = roundToNearestTen(newW);
        newH = roundToNearestTen(newH);
    }

    doResize(newW, newH);
}

void ContainerWidget::endResize()
{
    // From Thetis ucMeter.cs:409-415
    m_resizing = false;
    m_resizeStartGlobal = QPoint();
    if (!m_floating) {
        m_dockedSize = size();
    }
}

void ContainerWidget::doResize(int w, int h)
{
    // From Thetis ucMeter.cs:520-549
    w = std::max(w, kMinContainerWidth);
    h = std::max(h, kMinContainerHeight);

    if (m_floating) {
        if (parentWidget()) {
            parentWidget()->resize(w, h);
        }
    } else {
        // Clamp to parent bounds — from Thetis ucMeter.cs:538-539
        if (parentWidget()) {
            if (x() + w > parentWidget()->width()) {
                w = parentWidget()->width() - x();
            }
            if (y() + h > parentWidget()->height()) {
                h = parentWidget()->height() - y();
            }
        }
        QSize newSize(w, h);
        if (newSize != size()) {
            resize(newSize);
            update();
        }
    }
}
```

- [ ] **Step 5: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement ContainerWidget drag, resize, and hover show/hide"
```

---

## Task 4: ContainerWidget — Serialization

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — `ToString()` (lines 1012-1038) and `TryParse()` (lines 1039-1160). Pipe-delimited format with 23 fields, backward-compatible parsing.

- [ ] **Step 1: Implement serialize()**

```cpp
QString ContainerWidget::serialize() const
{
    // From Thetis ucMeter.cs:1012-1038 — pipe-delimited, 23 fields
    QStringList parts;
    parts << m_id;                                              // 0: ID
    parts << QString::number(m_rxSource);                       // 1: RX
    parts << QString::number(m_dockedLocation.x());             // 2: DockedLocation.X
    parts << QString::number(m_dockedLocation.y());             // 3: DockedLocation.Y
    parts << QString::number(m_dockedSize.width());             // 4: DockedSize.Width
    parts << QString::number(m_dockedSize.height());            // 5: DockedSize.Height
    parts << (m_floating ? QStringLiteral("true") : QStringLiteral("false"));  // 6: Floating
    parts << QString::number(m_delta.x());                      // 7: Delta.X
    parts << QString::number(m_delta.y());                      // 8: Delta.Y
    parts << axisLockToString(m_axisLock);                      // 9: AxisLock
    parts << (m_pinOnTop ? QStringLiteral("true") : QStringLiteral("false"));  // 10: PinOnTop
    parts << (m_border ? QStringLiteral("true") : QStringLiteral("false"));    // 11: Border
    parts << m_backgroundColor.name(QColor::HexArgb);           // 12: BackColor
    parts << (m_noControls ? QStringLiteral("true") : QStringLiteral("false")); // 13: NoControls
    parts << (m_enabled ? QStringLiteral("true") : QStringLiteral("false"));   // 14: Enabled
    parts << m_notes;                                           // 15: Notes
    parts << (m_containerMinimises ? QStringLiteral("true") : QStringLiteral("false")); // 16
    parts << (m_autoHeight ? QStringLiteral("true") : QStringLiteral("false")); // 17
    parts << (m_showOnRx ? QStringLiteral("true") : QStringLiteral("false"));  // 18
    parts << (m_showOnTx ? QStringLiteral("true") : QStringLiteral("false"));  // 19
    parts << (m_locked ? QStringLiteral("true") : QStringLiteral("false"));    // 20
    parts << (m_containerHidesWhenRxNotUsed ? QStringLiteral("true") : QStringLiteral("false")); // 21
    parts << (m_hiddenByMacro ? QStringLiteral("true") : QStringLiteral("false")); // 22

    return parts.join(QLatin1Char('|'));
}
```

Add a `m_backgroundColor` member to `ContainerWidget.h` private section:

```cpp
    QColor m_backgroundColor{0x0f, 0x0f, 0x1a};  // dark theme default
```

And add helper functions. In `ContainerWidget.h`, add as private static methods:

```cpp
    static QString axisLockToString(AxisLock lock);
    static AxisLock axisLockFromString(const QString& str);
```

In `ContainerWidget.cpp`:

```cpp
QString ContainerWidget::axisLockToString(AxisLock lock)
{
    static const char* names[] = {
        "LEFT", "TOPLEFT", "TOP", "TOPRIGHT",
        "RIGHT", "BOTTOMRIGHT", "BOTTOM", "BOTTOMLEFT"
    };
    int idx = static_cast<int>(lock);
    if (idx >= 0 && idx < 8) {
        return QString::fromLatin1(names[idx]);
    }
    return QStringLiteral("TOPLEFT");
}

AxisLock ContainerWidget::axisLockFromString(const QString& str)
{
    static const QMap<QString, AxisLock> map = {
        {QStringLiteral("LEFT"), AxisLock::Left},
        {QStringLiteral("TOPLEFT"), AxisLock::TopLeft},
        {QStringLiteral("TOP"), AxisLock::Top},
        {QStringLiteral("TOPRIGHT"), AxisLock::TopRight},
        {QStringLiteral("RIGHT"), AxisLock::Right},
        {QStringLiteral("BOTTOMRIGHT"), AxisLock::BottomRight},
        {QStringLiteral("BOTTOM"), AxisLock::Bottom},
        {QStringLiteral("BOTTOMLEFT"), AxisLock::BottomLeft},
    };
    return map.value(str.toUpper(), AxisLock::TopLeft);
}
```

- [ ] **Step 2: Implement deserialize()**

```cpp
bool ContainerWidget::deserialize(const QString& data)
{
    // From Thetis ucMeter.cs:1039-1160 — backward-compatible parsing
    if (data.isEmpty()) {
        return false;
    }

    QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 13) {
        qCWarning(lcContainer) << "Container deserialize: too few fields:" << parts.size();
        return false;
    }

    // Fields 0-12 are required (original Thetis format)
    QString id = parts[0];
    if (id.isEmpty()) {
        return false;
    }
    setId(id);

    bool ok = false;
    int rx = parts[1].toInt(&ok);
    if (!ok) { return false; }
    setRxSource(rx);

    int x = parts[2].toInt(&ok); if (!ok) { return false; }
    int y = parts[3].toInt(&ok); if (!ok) { return false; }
    int w = parts[4].toInt(&ok); if (!ok) { return false; }
    int h = parts[5].toInt(&ok); if (!ok) { return false; }
    setDockedLocation(QPoint(x, y));
    setDockedSize(QSize(w, h));

    // From Thetis ucMeter.cs:1075-1076
    bool floating = (parts[6].toLower() == QStringLiteral("true"));
    setFloating(floating);

    x = parts[7].toInt(&ok); if (!ok) { return false; }
    y = parts[8].toInt(&ok); if (!ok) { return false; }
    setDelta(QPoint(x, y));

    setAxisLock(axisLockFromString(parts[9]));

    bool pinOnTop = (parts[10].toLower() == QStringLiteral("true"));
    setPinOnTop(pinOnTop);

    bool border = (parts[11].toLower() == QStringLiteral("true"));
    setBorder(border);

    // Field 12: background color
    QColor bgColor(parts[12]);
    if (bgColor.isValid()) {
        m_backgroundColor = bgColor;
        m_contentArea->setStyleSheet(
            QStringLiteral("background: %1;").arg(bgColor.name(QColor::HexArgb)));
    }

    // Optional fields (backward compatible — from Thetis ucMeter.cs:1102+)
    if (parts.size() > 13) {
        setNoControls(parts[13].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 14) {
        setContainerEnabled(parts[14].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 15) {
        setNotes(parts[15]);
    }
    if (parts.size() > 16) {
        setContainerMinimises(parts[16].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 17) {
        setAutoHeight(parts[17].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 18 && parts.size() > 19) {
        setShowOnRx(parts[18].toLower() == QStringLiteral("true"));
        setShowOnTx(parts[19].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 20) {
        setLocked(parts[20].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 21) {
        setContainerHidesWhenRxNotUsed(parts[21].toLower() == QStringLiteral("true"));
    }
    if (parts.size() > 22) {
        setHiddenByMacro(parts[22].toLower() == QStringLiteral("true"));
    }

    qCDebug(lcContainer) << "Deserialized container:" << m_id
                          << "rx:" << m_rxSource
                          << "floating:" << m_floating
                          << "pos:" << m_dockedLocation
                          << "size:" << m_dockedSize;
    return true;
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement ContainerWidget serialize/deserialize (23-field pipe format)"
```

---

## Task 5: FloatingContainer — Full Implementation

**Files:**
- Modify: `src/gui/containers/FloatingContainer.h`
- Modify: `src/gui/containers/FloatingContainer.cpp`

Porting from: Thetis `frmMeterDisplay.cs` — constructor (lines 62-78), `TakeOwner()` (lines 168-179), `frmMeterDisplay_FormClosing()` (lines 158-166), `OnWindowStateChanged()` (lines 114-139), `setTitle()` (lines 140-144), `ID` setter with geometry restore (lines 145-157).

- [ ] **Step 1: Implement FloatingContainer**

Replace `FloatingContainer.h`:

```cpp
#pragma once

#include <QWidget>
#include <QString>

namespace NereusSDR {

class ContainerWidget;

// Floating window wrapper — equivalent to Thetis frmMeterDisplay.
// Hosts a ContainerWidget when it is popped out of the main window.
class FloatingContainer : public QWidget {
    Q_OBJECT

public:
    explicit FloatingContainer(int rxSource, QWidget* parent = nullptr);
    ~FloatingContainer() override;

    QString id() const { return m_id; }
    void setId(const QString& id);

    int rxSource() const { return m_rxSource; }

    // From Thetis frmMeterDisplay.cs:168-179 — reparent ucMeter into this form
    void takeOwner(ContainerWidget* container);

    // From Thetis frmMeterDisplay.cs:104-113
    bool containerMinimises() const { return m_containerMinimises; }
    void setContainerMinimises(bool minimises);
    bool containerHidesWhenRxNotUsed() const { return m_containerHidesWhenRxNotUsed; }
    void setContainerHidesWhenRxNotUsed(bool hides);

    bool isFormEnabled() const { return m_formEnabled; }
    void setFormEnabled(bool enabled);

    bool isHiddenByMacro() const { return m_hiddenByMacro; }
    void setHiddenByMacro(bool hidden);

    bool isContainerFloating() const { return m_floating; }
    void setContainerFloating(bool floating);

    // Called when main console window state changes
    // From Thetis frmMeterDisplay.cs:114-139
    void onConsoleWindowStateChanged(Qt::WindowStates state, bool rx2Enabled);

    // Save/restore geometry to AppSettings
    void saveGeometry();
    void restoreGeometry();

signals:
    void aboutToClose();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void updateTitle();

    QString m_id;
    int m_rxSource{1};
    bool m_containerMinimises{true};
    bool m_containerHidesWhenRxNotUsed{true};
    bool m_formEnabled{true};
    bool m_floating{false};
    bool m_hiddenByMacro{false};
};

} // namespace NereusSDR
```

Replace `FloatingContainer.cpp`:

```cpp
#include "FloatingContainer.h"
#include "ContainerWidget.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QCloseEvent>
#include <QVBoxLayout>

namespace NereusSDR {

FloatingContainer::FloatingContainer(int rxSource, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , m_rxSource(rxSource)
{
    // From Thetis frmMeterDisplay.cs:65
    setMinimumSize(ContainerWidget::kMinContainerWidth,
                   ContainerWidget::kMinContainerHeight);

    // Dark theme styling
    setStyleSheet(QStringLiteral("background: #0f0f1a;"));

    updateTitle();

    qCDebug(lcContainer) << "FloatingContainer created for RX" << rxSource;
}

FloatingContainer::~FloatingContainer()
{
    qCDebug(lcContainer) << "FloatingContainer destroyed:" << m_id;
}

void FloatingContainer::setId(const QString& id)
{
    m_id = id;

    // From Thetis frmMeterDisplay.cs:150-156 — setting ID triggers geometry restore
    restoreGeometry();
    updateTitle();
}

void FloatingContainer::takeOwner(ContainerWidget* container)
{
    // From Thetis frmMeterDisplay.cs:168-179
    m_containerMinimises = container->containerMinimises();
    m_formEnabled = container->isContainerEnabled();

    container->setParent(this);

    // Fill the entire form — equivalent to Anchor All
    if (!layout()) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
    }
    // Remove any existing widgets from layout
    QLayoutItem* child = nullptr;
    while ((child = layout()->takeAt(0)) != nullptr) {
        delete child;
    }
    layout()->addWidget(container);

    container->show();
    container->raise();

    qCDebug(lcContainer) << "FloatingContainer" << m_id << "took ownership of container" << container->id();
}

void FloatingContainer::onConsoleWindowStateChanged(Qt::WindowStates state, bool rx2Enabled)
{
    // From Thetis frmMeterDisplay.cs:114-139
    if (m_formEnabled && m_floating && m_containerMinimises) {
        if (state & Qt::WindowMinimized) {
            hide();
        } else {
            bool shouldShow = false;
            if (m_rxSource == 1) {
                shouldShow = !m_hiddenByMacro;
            } else if (m_rxSource == 2) {
                if (rx2Enabled || !m_containerHidesWhenRxNotUsed) {
                    shouldShow = !m_hiddenByMacro;
                }
            }
            if (shouldShow) {
                show();
            }
        }
    }
}

void FloatingContainer::closeEvent(QCloseEvent* event)
{
    // From Thetis frmMeterDisplay.cs:158-166 — hide instead of close on user action
    if (event->spontaneous()) {
        hide();
        event->ignore();
        return;
    }
    saveGeometry();
    QWidget::closeEvent(event);
}

void FloatingContainer::saveGeometry()
{
    // From Thetis frmMeterDisplay.cs:165 — Common.SaveForm
    auto& s = AppSettings::instance();
    QString key = QStringLiteral("MeterDisplay_%1_Geometry").arg(m_id);
    QRect r = geometry();
    QString val = QStringLiteral("%1,%2,%3,%4")
        .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    s.setValue(key, val);
}

void FloatingContainer::restoreGeometry()
{
    // From Thetis frmMeterDisplay.cs:152 — Common.RestoreForm
    auto& s = AppSettings::instance();
    QString key = QStringLiteral("MeterDisplay_%1_Geometry").arg(m_id);
    QString val = s.value(key).toString();
    if (val.isEmpty()) {
        return;
    }
    QStringList parts = val.split(QLatin1Char(','));
    if (parts.size() != 4) {
        return;
    }
    bool ok1, ok2, ok3, ok4;
    int x = parts[0].toInt(&ok1);
    int y = parts[1].toInt(&ok2);
    int w = parts[2].toInt(&ok3);
    int h = parts[3].toInt(&ok4);
    if (ok1 && ok2 && ok3 && ok4) {
        setGeometry(x, y, w, h);
    }
}

void FloatingContainer::updateTitle()
{
    // From Thetis frmMeterDisplay.cs:140-144 — unique title for OBS/streaming
    uint hash = qHash(m_id) % 100000;
    setWindowTitle(QStringLiteral("NereusSDR Meter [%1]").arg(hash, 5, 10, QLatin1Char('0')));
}

void FloatingContainer::setContainerMinimises(bool minimises) { m_containerMinimises = minimises; }
void FloatingContainer::setContainerHidesWhenRxNotUsed(bool hides) { m_containerHidesWhenRxNotUsed = hides; }
void FloatingContainer::setFormEnabled(bool enabled) { m_formEnabled = enabled; }
void FloatingContainer::setHiddenByMacro(bool hidden) { m_hiddenByMacro = hidden; }
void FloatingContainer::setContainerFloating(bool floating) { m_floating = floating; }

} // namespace NereusSDR
```

- [ ] **Step 2: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/FloatingContainer.h src/gui/containers/FloatingContainer.cpp
git commit -m "feat(3G-1): implement FloatingContainer — reparent, TopMost, minimize-with-console, geometry persist"
```

---

## Task 6: ContainerManager — Lifecycle and Float/Dock

**Files:**
- Modify: `src/gui/containers/ContainerManager.h`
- Modify: `src/gui/containers/ContainerManager.cpp`

Porting from: Thetis `MeterManager.cs` — `AddMeterContainer()` (lines 5613-5673, 5763-5773), `setMeterFloating()` (lines 5894-5918), `returnMeterFromFloating()` (lines 5867-5893), `RemoveMeterContainer()` (lines 6533-6579), `RecoverContainer()` (lines 6514-6531), `SetPositionOfDockedMeters()` (lines 5812-5865).

- [ ] **Step 1: Implement create/destroy and float/dock**

Replace `ContainerManager.cpp`:

```cpp
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

namespace NereusSDR {

ContainerManager::ContainerManager(QWidget* dockParent, QObject* parent)
    : QObject(parent)
    , m_dockParent(dockParent)
{
    qCDebug(lcContainer) << "ContainerManager created";
}

ContainerManager::~ContainerManager()
{
    qCDebug(lcContainer) << "ContainerManager destroyed — cleaning up"
                          << m_containers.size() << "containers";
}

ContainerWidget* ContainerManager::createContainer(int rxSource, bool floating)
{
    // From Thetis MeterManager.cs:5613-5673 — AddMeterContainer()
    auto* container = new ContainerWidget(m_dockParent);
    container->setRxSource(rxSource);

    // Create floating form wrapper
    // From Thetis MeterManager.cs:5631-5632
    auto* floatingForm = new FloatingContainer(rxSource);
    floatingForm->setId(container->id());

    // Wire container float/dock requests
    connect(container, &ContainerWidget::floatRequested, this, [this, container]() {
        floatContainer(container->id());
    });
    connect(container, &ContainerWidget::dockRequested, this, [this, container]() {
        dockContainer(container->id());
    });

    // Wire docked move to update delta
    connect(container, &ContainerWidget::dockedMoved, this, [this, container]() {
        // Delta will be set by the caller via updateDockedPositions
    });

    // Store in maps — from Thetis MeterManager.cs:5652-5655
    m_containers.insert(container->id(), container);
    m_floatingForms.insert(container->id(), floatingForm);

    // Apply initial float/dock state
    if (floating) {
        container->setFloating(true);
        setMeterFloating(container, floatingForm);
    } else {
        container->setFloating(false);
        returnMeterFromFloating(container, floatingForm);
    }

    qCDebug(lcContainer) << "Created container:" << container->id()
                          << "rx:" << rxSource << "floating:" << floating;

    emit containerAdded(container->id());
    return container;
}

void ContainerManager::destroyContainer(const QString& id)
{
    // From Thetis MeterManager.cs:6533-6579 — RemoveMeterContainer()
    if (!m_containers.contains(id)) {
        qCWarning(lcContainer) << "destroyContainer: unknown id:" << id;
        return;
    }

    // Hide and clean up floating form
    if (m_floatingForms.contains(id)) {
        FloatingContainer* form = m_floatingForms.take(id);
        form->hide();
        form->deleteLater();
    }

    // Remove container widget
    ContainerWidget* container = m_containers.take(id);
    container->hide();
    container->setParent(nullptr);
    container->deleteLater();

    qCDebug(lcContainer) << "Destroyed container:" << id;
    emit containerRemoved(id);
}

void ContainerManager::floatContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) {
        return;
    }
    setMeterFloating(m_containers[id], m_floatingForms[id]);
}

void ContainerManager::dockContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) {
        return;
    }
    returnMeterFromFloating(m_containers[id], m_floatingForms[id]);
}

void ContainerManager::recoverContainer(const QString& id)
{
    // From Thetis MeterManager.cs:6514-6531 — off-screen rescue
    if (!m_containers.contains(id)) {
        return;
    }
    ContainerWidget* container = m_containers[id];

    // Force docked and re-enable
    if (container->isFloating()) {
        dockContainer(id);
    }
    container->setContainerEnabled(true);
    container->show();

    // Center in parent
    if (m_dockParent) {
        int cx = (m_dockParent->width() / 2) - (container->width() / 2);
        int cy = (m_dockParent->height() / 2) - (container->height() / 2);
        container->move(cx, cy);
        container->storeLocation();
    }

    qCDebug(lcContainer) << "Recovered container:" << id;
}

QList<ContainerWidget*> ContainerManager::allContainers() const
{
    return m_containers.values();
}

ContainerWidget* ContainerManager::container(const QString& id) const
{
    return m_containers.value(id, nullptr);
}

int ContainerManager::containerCount() const
{
    return m_containers.size();
}

void ContainerManager::setContainerVisible(const QString& id, bool visible)
{
    if (!m_containers.contains(id)) {
        return;
    }
    ContainerWidget* c = m_containers[id];
    if (c->isFloating() && m_floatingForms.contains(id)) {
        m_floatingForms[id]->setVisible(visible);
    } else {
        c->setVisible(visible);
    }
}
```

- [ ] **Step 2: Add private helpers for float/dock transitions**

Add to `ContainerManager.h` in the `private:` section:

```cpp
    void setMeterFloating(ContainerWidget* container, FloatingContainer* form);
    void returnMeterFromFloating(ContainerWidget* container, FloatingContainer* form);
```

Add to `ContainerManager.cpp`:

```cpp
void ContainerManager::setMeterFloating(ContainerWidget* container, FloatingContainer* form)
{
    // From Thetis MeterManager.cs:5894-5918 — setMeterFloating()
    container->hide();
    form->takeOwner(container);
    form->setContainerFloating(true);
    container->setFloating(true);
    form->show();

    qCDebug(lcContainer) << "Floated container:" << container->id();
}

void ContainerManager::returnMeterFromFloating(ContainerWidget* container, FloatingContainer* form)
{
    // From Thetis MeterManager.cs:5867-5893 — returnMeterFromFloating()
    form->setContainerFloating(false);
    form->hide();
    container->hide();
    container->setParent(m_dockParent);
    container->setFloating(false);
    container->restoreLocation();
    container->show();
    container->raise();

    qCDebug(lcContainer) << "Docked container:" << container->id();
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/containers/ContainerManager.h src/gui/containers/ContainerManager.cpp
git commit -m "feat(3G-1): implement ContainerManager — create/destroy, float/dock, recover"
```

---

## Task 7: ContainerManager — Axis-Lock Repositioning

**Files:**
- Modify: `src/gui/containers/ContainerManager.cpp`

Porting from: Thetis `MeterManager.cs` — `SetPositionOfDockedMeters()` (lines 5812-5821) and `setPoisitionOfDockedMeter()` (lines 5822-5865). This is the core axis-lock logic that keeps containers anchored to their designated corner/edge when the main window resizes.

- [ ] **Step 1: Implement updateDockedPositions()**

```cpp
void ContainerManager::updateDockedPositions(int hDelta, int vDelta)
{
    // From Thetis MeterManager.cs:5812-5865 — SetPositionOfDockedMeters + setPoisitionOfDockedMeter
    for (auto it = m_containers.constBegin(); it != m_containers.constEnd(); ++it) {
        ContainerWidget* c = it.value();
        if (c->isFloating()) {
            continue;
        }

        QPoint dockedLoc = c->dockedLocation();
        QPoint delta = c->delta();
        QPoint newLocation;

        // From Thetis MeterManager.cs:5833-5856 — axis-specific position calculation.
        // Delta tracks the console size at the time the container was last moved.
        // newLocation = dockedLocation - oldDelta + currentDelta
        switch (c->axisLock()) {
        case AxisLock::Right:
        case AxisLock::BottomRight:
            // Anchored to right/bottom — tracks both H and V changes
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta,
                                 dockedLoc.y() - delta.y() + vDelta);
            break;
        case AxisLock::BottomLeft:
        case AxisLock::Left:
            // Anchored to left — X stays fixed, Y tracks V changes
            newLocation = QPoint(dockedLoc.x(),
                                 dockedLoc.y() - delta.y() + vDelta);
            break;
        case AxisLock::TopLeft:
            // Anchored to top-left — stays put
            newLocation = QPoint(dockedLoc.x(), dockedLoc.y());
            break;
        case AxisLock::Top:
            // Anchored to top — X tracks H changes, Y stays
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta,
                                 dockedLoc.y());
            break;
        case AxisLock::Bottom:
            // Anchored to bottom — tracks both H and V
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta,
                                 dockedLoc.y() - delta.y() + vDelta);
            break;
        case AxisLock::TopRight:
            // Anchored to top-right — X tracks H, Y stays
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta,
                                 dockedLoc.y());
            break;
        }

        // Clamp to parent bounds — from Thetis MeterManager.cs:5859-5862
        if (m_dockParent) {
            int maxX = m_dockParent->width() - c->width();
            int maxY = m_dockParent->height() - c->height();
            newLocation.setX(std::clamp(newLocation.x(), 0, std::max(0, maxX)));
            newLocation.setY(std::clamp(newLocation.y(), 0, std::max(0, maxY)));
        }

        if (newLocation != c->pos()) {
            c->move(newLocation);
        }
    }
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/ContainerManager.cpp
git commit -m "feat(3G-1): implement axis-lock repositioning on main window resize"
```

---

## Task 8: ContainerManager — Persistence (Save/Restore)

**Files:**
- Modify: `src/gui/containers/ContainerManager.cpp`

Porting from: Thetis `MeterManager.cs` — `StoreSettings2()` (lines 6391-6447) and `RestoreSettings()` (lines 6012-6105). Container data is stored as pipe-delimited strings keyed by `ContainerData_{id}`.

- [ ] **Step 1: Implement saveState()**

```cpp
void ContainerManager::saveState()
{
    // From Thetis MeterManager.cs:6391-6447 — StoreSettings2()
    auto& s = AppSettings::instance();

    // First clear old container data
    // Build list of keys to remove
    QStringList keysToRemove;
    // We need to track what we've saved so we can clean up stale entries.
    // AppSettings doesn't support key enumeration, so we store a container ID list.
    QString oldIdList = s.value(QStringLiteral("ContainerIdList")).toString();
    if (!oldIdList.isEmpty()) {
        for (const QString& oldId : oldIdList.split(QLatin1Char(','))) {
            s.remove(QStringLiteral("ContainerData_%1").arg(oldId));
            s.remove(QStringLiteral("MeterDisplay_%1_Geometry").arg(oldId));
        }
    }

    // Save current containers
    QStringList idList;
    for (auto it = m_containers.constBegin(); it != m_containers.constEnd(); ++it) {
        ContainerWidget* c = it.value();

        // Store current position before serializing
        if (!c->isFloating()) {
            c->storeLocation();
        }

        // From Thetis MeterManager.cs:6404
        QString key = QStringLiteral("ContainerData_%1").arg(c->id());
        s.setValue(key, c->serialize());
        idList << c->id();

        // Save floating form geometry
        if (m_floatingForms.contains(c->id())) {
            m_floatingForms[c->id()]->saveGeometry();
        }
    }

    s.setValue(QStringLiteral("ContainerIdList"), idList.join(QLatin1Char(',')));
    s.setValue(QStringLiteral("ContainerCount"), QString::number(m_containers.size()));

    qCDebug(lcContainer) << "Saved" << m_containers.size() << "container(s) to AppSettings";
}
```

- [ ] **Step 2: Implement restoreState()**

```cpp
void ContainerManager::restoreState()
{
    // From Thetis MeterManager.cs:6012-6105 — RestoreSettings()
    auto& s = AppSettings::instance();

    QString idList = s.value(QStringLiteral("ContainerIdList")).toString();
    if (idList.isEmpty()) {
        qCDebug(lcContainer) << "No saved containers found";
        return;
    }

    QStringList ids = idList.split(QLatin1Char(','), Qt::SkipEmptyParts);
    int restored = 0;

    for (const QString& id : ids) {
        QString key = QStringLiteral("ContainerData_%1").arg(id);
        QString data = s.value(key).toString();
        if (data.isEmpty()) {
            continue;
        }

        // Create a new container and deserialize its state
        // From Thetis MeterManager.cs:6022-6029
        auto* container = new ContainerWidget(m_dockParent);
        if (!container->deserialize(data)) {
            qCWarning(lcContainer) << "Failed to deserialize container:" << id;
            delete container;
            continue;
        }

        // Create floating form
        auto* floatingForm = new FloatingContainer(container->rxSource());
        floatingForm->setId(container->id());

        // Wire float/dock requests
        connect(container, &ContainerWidget::floatRequested, this, [this, container]() {
            floatContainer(container->id());
        });
        connect(container, &ContainerWidget::dockRequested, this, [this, container]() {
            dockContainer(container->id());
        });

        // Store
        m_containers.insert(container->id(), container);
        m_floatingForms.insert(container->id(), floatingForm);

        // Apply float/dock state
        // From Thetis MeterManager.cs:5639-5640 — restore floating form size
        if (container->isFloating()) {
            container->resize(floatingForm->size());
            setMeterFloating(container, floatingForm);
        } else {
            returnMeterFromFloating(container, floatingForm);
        }

        if (container->isContainerEnabled() && !container->isHiddenByMacro()) {
            if (container->isFloating()) {
                floatingForm->show();
            } else {
                container->show();
            }
        }

        restored++;
        emit containerAdded(container->id());
    }

    qCDebug(lcContainer) << "Restored" << restored << "container(s) from AppSettings";
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 4: Commit**

```bash
git add src/gui/containers/ContainerManager.cpp
git commit -m "feat(3G-1): implement ContainerManager save/restore to AppSettings"
```

---

## Task 9: Wire into MainWindow — Container #0 Right Panel

**Files:**
- Modify: `src/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

This wires the container system into the main window and creates the default Container #0 as a right-side panel. On resize, axis-lock positions are updated.

- [ ] **Step 1: Add ContainerManager to MainWindow.h**

Add forward declaration and include:

```cpp
class ContainerManager;
```

Add members to the `private:` section:

```cpp
    // Container infrastructure (Phase 3G-1)
    ContainerManager* m_containerManager{nullptr};
    QSize m_previousSize;    // for computing resize delta
    int m_hDelta{0};         // cumulative horizontal resize delta
    int m_vDelta{0};         // cumulative vertical resize delta
```

Add to `protected:` section:

```cpp
    void resizeEvent(QResizeEvent* event) override;
```

Add a private method:

```cpp
    void createDefaultContainer();
```

- [ ] **Step 2: Update MainWindow.cpp — create ContainerManager and default layout**

Add include at top:

```cpp
#include "containers/ContainerManager.h"
#include "containers/ContainerWidget.h"
```

In `buildUI()`, after the `setCentralWidget(central)` line, add the container infrastructure:

```cpp
    // --- Container Infrastructure (Phase 3G-1) ---
    // ContainerManager owns all containers, handles float/dock/resize/persist.
    m_containerManager = new ContainerManager(central, this);

    // Restore saved containers, or create defaults on first run
    m_containerManager->restoreState();
    if (m_containerManager->containerCount() == 0) {
        createDefaultContainer();
    }

    m_previousSize = size();
```

Add `createDefaultContainer()`:

```cpp
void MainWindow::createDefaultContainer()
{
    // Create Container #0 — default right-side panel with placeholder.
    // From MASTER-PLAN.md: "single right-side container (Container #0) with placeholder content"
    ContainerWidget* c0 = m_containerManager->createContainer(1, false);
    c0->setAxisLock(AxisLock::TopRight);

    // Position at the right edge of the central widget
    QWidget* central = centralWidget();
    if (central) {
        int containerWidth = 200;
        int x = central->width() - containerWidth;
        int y = 0;
        int h = central->height();
        c0->move(x, y);
        c0->resize(containerWidth, h);
        c0->storeLocation();
        c0->setDelta(QPoint(central->width(), central->height()));
    }

    qCDebug(lcContainer) << "Created default Container #0:" << c0->id();
}
```

- [ ] **Step 3: Implement resizeEvent for axis-lock repositioning**

```cpp
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Compute cumulative delta for axis-lock positioning
    // From Thetis console.cs HDelta/VDelta — tracks total resize offset from initial size
    QWidget* central = centralWidget();
    if (central && m_containerManager) {
        m_hDelta = central->width();
        m_vDelta = central->height();
        m_containerManager->updateDockedPositions(m_hDelta, m_vDelta);
    }
}
```

- [ ] **Step 4: Save container state on close**

In `closeEvent()`, before `AppSettings::instance().save()`, add:

```cpp
    // Save container layout
    if (m_containerManager) {
        m_containerManager->saveState();
    }
```

- [ ] **Step 5: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "feat(3G-1): wire ContainerManager into MainWindow — default Container #0 right panel, axis-lock on resize, persist on close"
```

---

## Task 10: Add LogCategories include and Final Build Verification

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp` (ensure all includes are correct)
- Verify: Full clean build

- [ ] **Step 1: Ensure all includes are consistent**

Verify `ContainerWidget.cpp` has these includes at the top:

```cpp
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/LogCategories.h"

#include <QUuid>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QApplication>
```

Verify `ContainerManager.cpp` has:

```cpp
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <algorithm>
```

- [ ] **Step 2: Clean build**

```bash
cd /c/Users/boyds/NereusSDR
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -10
cmake --build build -j$(nproc) 2>&1 | tail -30
```

Expected: Clean build with no errors and no warnings related to container code.

- [ ] **Step 3: Run the application**

```bash
cd /c/Users/boyds/NereusSDR && ./build/NereusSDR
```

Expected: Application launches. Container #0 is visible on the right side of the main window with a dark background and "Container" placeholder text. Hovering near the top shows the title bar with RX1 label and float/axis/settings buttons. Hovering near the bottom-right shows the resize grip.

- [ ] **Step 4: Manual verification checklist**

Test each behavior:
1. **Hover title bar**: Move mouse to top of Container #0 — title bar appears
2. **Hover resize grip**: Move mouse to bottom-right corner — resize grip appears
3. **Drag (docked)**: Drag title bar — container moves within main window, clamped to bounds
4. **Resize**: Drag resize grip — container changes size
5. **Float**: Click ↗ button — container pops out as separate window
6. **Dock**: Click ↙ button on floating container — returns to main window
7. **Axis lock**: Click arrow button — cycles through 8 positions (↖↑↗→↘↓↙←)
8. **Pin on top**: Float a container, click pin button — window stays on top
9. **Window resize**: Resize main window — docked container repositions based on axis lock
10. **Persist**: Close and reopen app — container position/size/state restored
11. **Ctrl-snap**: Hold Ctrl while dragging — position snaps to 10px grid

- [ ] **Step 5: Final commit (if any fixes needed)**

```bash
git add -A
git commit -m "fix(3G-1): address build or runtime issues from integration testing"
```

---

---

## Addendum: AetherSDR-Style Default Panel + Thetis Container Extensibility

### Design Goal

The default right-side experience should look and feel like AetherSDR / SmartSDR:
a fixed-width panel with toggle buttons, S-meter, and a scrollable stack of
reorderable applet boxes. But the underlying container system must also support
Thetis's free-form containers (arbitrary position, any monitor, axis-lock) for
power users.

### AetherSDR AppletPanel Pattern (Reference)

AetherSDR's right panel (`AppletPanel`, 260px fixed-width):

```
┌─────────────────────────────┐
│ [RX] [TX] [PHNE] [P/CW] [EQ] │  ← Toggle button row 1
│ [TUN][AMP][DIGI][MTR][AG][LCK]│  ← Toggle button row 2
├─────────────────────────────┤
│ S-Meter  [TX▾] [RX▾]        │  ← Fixed S-meter section
│ ┌───────────────────────┐   │
│ │ ▓▓▓▓▓▓▓▓░░░░░░░░░░░ │   │     (always visible)
│ └───────────────────────┘   │
│ [Peak Hold ▾] [RST]         │
├─────────────────────────────┤
│ ⋮⋮ RX Controls              │  ← Scrollable, drag-reorderable
│ [ANT1] [ANT2] [ANT3]       │     applet stack
│ [LSB] [USB] [AM] [FM]      │
│ AF ▓▓▓▓░░  RF ▓▓▓▓░░       │
│ ⋮⋮ TX Controls              │
│ [PWR ▓▓▓░] [Tune ▓▓░]     │
│ ⋮⋮ Phone                    │
│ [VOX] [DEXP] [TX Filter]   │
│ ⋮⋮ Equalizer                │
│ [63][125][250]...[8k]       │
│ ...                         │
└─────────────────────────────┘
```

Key patterns:
- **Toggle buttons** show/hide applets (persisted to AppSettings)
- **Applets are drag-reorderable** within the stack
- **Each applet can float independently** (right-click → Pop Out)
- **S-Meter is fixed** (not reorderable, always above the stack)
- **260px fixed width**, scrollable vertically

### NereusSDR Hybrid Approach

Container #0 is a special "AppletPanel" container that uses the AetherSDR
applet-stack pattern internally. But it's still a `ContainerWidget` — it can
be floated, resized, axis-locked, or hidden like any other container.

Additionally, individual applets within Container #0 can be "promoted" to
standalone `ContainerWidget`s (creating new containers via ContainerManager).
And Thetis-style free-form containers can be created separately for power
users (custom meter layouts, multi-monitor, etc.).

```
Default (out-of-box):              Power user (customized):
┌──────────┬─────────────┐         ┌──────────────────────────┐
│ Spectrum  │ Container #0│         │ Spectrum (full width)     │
│ + WF     │ (AppletPanel)│        │ + WF + VFO overlay        │
│ + VFO    │ [RX][TX]... │         ├──────────────────────────┤
│          │ ┌S-Meter───┐│         │ Container #1 (bottom)     │
│          │ │▓▓▓▓░░░░░ ││         │ [S-Meter] [PWR] [CW Ctl] │
│          │ ├──────────┤│         └──────────────────────────┘
│          │ │RX Controls││
│          │ │TX Controls││          Floating Container #2 (mon 2):
│          │ │Equalizer  ││          ┌────────────────────┐
│          │ └──────────┘│          │ RX Controls (RX1)   │
│          │             │          │ TX Controls         │
└──────────┴─────────────┘          └────────────────────┘
```

### Phase 3G-1 Scope for This

Phase 3G-1 builds **only the container infrastructure** (Tasks 1-10 above).
The AppletPanel content (toggle buttons, S-meter, applet stack, individual
applet widgets) is deferred to later phases:

| Phase | Scope |
|---|---|
| **3G-1** (this plan) | ContainerWidget, FloatingContainer, ContainerManager, persistence, Container #0 with placeholder |
| **3G-2** | MeterWidget GPU renderer (QRhi-based item rendering inside containers) |
| **3G-3** | Core meter groups (S-Meter needle, Power/SWR, ALC) |
| **3G-5** | Interactive items (band/mode/filter buttons, VFO display) |
| **3G-AppletPanel** (new, between 3G-5 and 3G-6) | AppletPanel layout for Container #0: toggle button rows, S-meter section, scrollable applet stack, drag-reorder, applet pop-out/dock. Port AetherSDR's AppletPanel/AppletTitleBar/FloatingAppletWindow patterns into NereusSDR's container system. |
| **3G-6** | Container settings dialog (full composability UI) |

### What This Means for Task 9 (This Plan)

Task 9 creates Container #0 with a simple placeholder. In 3G-AppletPanel,
this placeholder gets replaced with the full AetherSDR-style applet stack.
The container infrastructure built in Tasks 1-8 supports this transition
cleanly because:

1. `ContainerWidget::contentArea()` returns the QWidget where content goes
2. The applet panel simply becomes the content of Container #0
3. The container's drag/resize/float/dock/persist all work regardless of content
4. Individual applets can create new ContainerWidgets via ContainerManager
   when popped out — unifying the AetherSDR pop-out and Thetis free-form models

### AetherSDR Source Files for Future Reference

These files will be ported in 3G-AppletPanel:

| AetherSDR File | NereusSDR Equivalent |
|---|---|
| `src/gui/AppletPanel.h/.cpp` | `src/gui/containers/AppletPanel.h/.cpp` (new) |
| `src/gui/FloatingAppletWindow.h/.cpp` | Reuse existing `FloatingContainer` |
| `src/gui/RxApplet.h/.cpp` | `src/gui/applets/RxApplet.h/.cpp` |
| `src/gui/TxApplet.h/.cpp` | `src/gui/applets/TxApplet.h/.cpp` |
| `src/gui/PhoneApplet.h/.cpp` | `src/gui/applets/PhoneApplet.h/.cpp` |
| `src/gui/PhoneCwApplet.h/.cpp` | `src/gui/applets/PhoneCwApplet.h/.cpp` |
| `src/gui/EqApplet.h/.cpp` | `src/gui/applets/EqApplet.h/.cpp` |
| `src/gui/SMeterWidget.h/.cpp` | `src/gui/applets/SMeterWidget.h/.cpp` |

---

## Summary

| Task | Deliverable | Thetis Source |
|---|---|---|
| 1 | Logging + CMake skeleton + stub files | — |
| 2 | ContainerWidget UI layout, title bar, properties | ucMeter.cs:108-643 |
| 3 | ContainerWidget drag, resize, hover | ucMeter.cs:281-549, 1198-1229 |
| 4 | ContainerWidget serialize/deserialize | ucMeter.cs:1012-1160 |
| 5 | FloatingContainer full implementation | frmMeterDisplay.cs (all) |
| 6 | ContainerManager create/destroy, float/dock | MeterManager.cs:5613-5918, 6514-6579 |
| 7 | ContainerManager axis-lock repositioning | MeterManager.cs:5812-5865 |
| 8 | ContainerManager save/restore persistence | MeterManager.cs:6012-6447 |
| 9 | MainWindow integration + Container #0 (placeholder) | — |
| 10 | Full build + manual verification | — |
