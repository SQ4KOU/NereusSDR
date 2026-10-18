# Phase 3G-1: Container Infrastructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a hybrid container system that delivers an AetherSDR/SmartSDR-clean right-side panel experience out of the box, while supporting Thetis's full free-form container capabilities (arbitrary positioning, multi-monitor, axis-lock) for power users. This is the shell infrastructure — no rendering or applet content yet.

**Architecture:** Four classes across two docking models:

- `ContainerWidget` — the container shell (title bar, drag, resize, axis-lock, content slot, serialization). Equivalent to Thetis `ucMeter`.
- `FloatingContainer` — top-level QWidget wrapper for popped-out containers. Equivalent to Thetis `frmMeterDisplay`.
- `ContainerManager` — singleton lifecycle manager (create/destroy, three dock modes, axis-lock reposition, persistence). Combines Thetis `MeterManager` container lifecycle with AetherSDR `AppletPanel` layout awareness.
- MainWindow uses a **QSplitter** to give Container #0 a proper layout slot as the right panel (AetherSDR pattern), while additional containers can overlay-dock or float freely (Thetis pattern).

**Tech Stack:** C++20, Qt6 Widgets, QSplitter, AppSettings (XML persistence), QLoggingCategory

---

## Design Rationale: The Hybrid Model

### The Two-Source Rule Applied to Containers

| Question | Source | What it gives us |
|---|---|---|
| **What does the default UX look like?** | AetherSDR `AppletPanel` | Clean 260px right panel, toggle buttons, scrollable applet stack, drag-reorder, pop-out/dock |
| **What power-user capabilities exist?** | Thetis `ucMeter` + `MeterManager` | Free-form containers anywhere, 8-position axis-lock, multi-monitor float, pin-on-top, per-container RX source, macro visibility |

### Three Docking Modes

```
┌─ MainWindow ──────────────────────────────────────────────┐
│ MenuBar                                                    │
├───────────────────────────────────┬────────────────────────┤
│                                   │                        │
│  QSplitter left:                  │ QSplitter right:       │
│  SpectrumWidget + ZoomBar         │ Container #0           │
│  (+ overlay-docked containers)    │ (PANEL-DOCKED)         │
│                                   │ ┌──────────────────┐  │
│  ┌──────────────────────────┐     │ │ [Placeholder for │  │
│  │ Container #N (OVERLAY)   │     │ │  AppletPanel in  │  │
│  │ (absolutely positioned,  │     │ │  later phases]   │  │
│  │  axis-locked, Thetis     │     │ │                  │  │
│  │  style)                  │     │ └──────────────────┘  │
│  └──────────────────────────┘     │                        │
│                                   │                        │
├───────────────────────────────────┴────────────────────────┤
│ StatusBar                                                  │
└────────────────────────────────────────────────────────────┘

  + Container #M (FLOATING) — separate window on any monitor
    ┌────────────────────┐
    │ [title bar]        │
    │ [content]          │
    └────────────────────┘
```

1. **Panel-docked** — Container #0 lives in MainWindow's QSplitter as the right panel. Proper layout management, resizable splitter handle, no overlapping with spectrum. Width persisted. This is how AetherSDR's AppletPanel works.

2. **Overlay-docked** — Additional containers positioned absolutely over the central widget area. Axis-lock repositioning on resize. This is how Thetis containers work (free-form over the console form).

3. **Floating** — Container in its own `FloatingContainer` window (`Qt::Window | Qt::Tool`). Pin-on-top, per-monitor DPI, geometry persist. Both AetherSDR and Thetis support this.

### Why QSplitter for Container #0

Without QSplitter, Container #0 would overlay the spectrum (absolute positioning). This means:
- GPU wastes cycles rendering spectrum under the panel
- No clean resize handle between spectrum and panel
- Spectrum mouse events don't know about the panel boundary
- It doesn't feel like a proper panel — it feels like a stacked overlay

AetherSDR solves this with a QSplitter. We do the same.

### Content Slot Design

`ContainerWidget` exposes a clean content replacement API:

```cpp
QWidget* contentArea() const;      // The current content widget
void setContent(QWidget* widget);  // Replace content (takes ownership)
```

In 3G-1, `setContent()` receives a placeholder widget. In 3G-AppletPanel (future), it receives a full `AppletPanel` with toggle buttons, S-meter, and scrollable applet stack. The container shell (title bar, drag, resize, axis-lock, persist) works identically regardless of content.

### Phase Roadmap (Container Subsystem)

| Phase | Scope | Source |
|---|---|---|
| **3G-1** (this plan) | Container shell + 3 dock modes + QSplitter + persistence | Thetis ucMeter/frmMeterDisplay/MeterManager + AetherSDR layout |
| **3G-2** | MeterWidget GPU renderer (QRhi item rendering inside containers) | Thetis MeterManager rendering |
| **3G-3** | Core meter groups (S-Meter, Power/SWR, ALC) | Thetis MeterManager meter types |
| **3G-5** | Interactive items (band/mode/filter buttons, VFO display) | Thetis MeterManager interactive items |
| **3G-AP** | AppletPanel for Container #0: toggle buttons, S-meter section, scrollable applet stack, drag-reorder, applet pop-out/dock | AetherSDR AppletPanel/AppletTitleBar/FloatingAppletWindow |
| **3G-6** | Container settings dialog (full composability UI) | Thetis Setup meter tab |

### AetherSDR Source Files (Reference for Future Phases)

| AetherSDR File | NereusSDR Equivalent | Phase |
|---|---|---|
| `src/gui/AppletPanel.h/.cpp` | `src/gui/containers/AppletPanel.h/.cpp` | 3G-AP |
| `src/gui/FloatingAppletWindow.h/.cpp` | Reuse `FloatingContainer` | 3G-1 (this plan) |
| `src/gui/RxApplet.h/.cpp` | `src/gui/applets/RxApplet.h/.cpp` | 3G-AP |
| `src/gui/TxApplet.h/.cpp` | `src/gui/applets/TxApplet.h/.cpp` | 3G-AP |
| `src/gui/PhoneApplet.h/.cpp` | `src/gui/applets/PhoneApplet.h/.cpp` | 3G-AP |
| `src/gui/PhoneCwApplet.h/.cpp` | `src/gui/applets/PhoneCwApplet.h/.cpp` | 3G-AP |
| `src/gui/EqApplet.h/.cpp` | `src/gui/applets/EqApplet.h/.cpp` | 3G-AP |
| `src/gui/SMeterWidget.h/.cpp` | `src/gui/applets/SMeterWidget.h/.cpp` | 3G-AP |

---

## Thetis Source Reference

| NereusSDR Class | Thetis Source | Key Functions Ported |
|---|---|---|
| `ContainerWidget` | `ucMeter.cs` (1238 lines) | Title bar show/hide on hover, drag (docked + floating), resize via grab handle, axis-lock cycling, pin-on-top, serialization (ToString/TryParse) |
| `FloatingContainer` | `frmMeterDisplay.cs` (208 lines) | TakeOwner reparenting, TopMost, minimize-with-console, DPI positioning, geometry save/restore |
| `ContainerManager` | `MeterManager.cs:244-6600` | AddMeterContainer, setMeterFloating, returnMeterFromFloating, SetPositionOfDockedMeters, StoreSettings2, RestoreSettings, RemoveMeterContainer, RecoverContainer |

---

## File Structure

### New Files

| File | Responsibility |
|---|---|
| `src/gui/containers/ContainerWidget.h` | Container shell — title bar, drag, resize, axis-lock, content slot, properties, serialization. Supports all 3 dock modes. |
| `src/gui/containers/ContainerWidget.cpp` | Implementation of all ContainerWidget behavior |
| `src/gui/containers/FloatingContainer.h` | Top-level window wrapper for floating containers (also reused for applet pop-out in 3G-AP). |
| `src/gui/containers/FloatingContainer.cpp` | Implementation of FloatingContainer |
| `src/gui/containers/ContainerManager.h` | Singleton lifecycle manager — create/destroy, 3 dock modes, axis-lock reposition, QSplitter awareness, persistence. |
| `src/gui/containers/ContainerManager.cpp` | Implementation of ContainerManager |

### Modified Files

| File | Changes |
|---|---|
| `CMakeLists.txt` | Add 3 new .cpp files to `GUI_SOURCES` |
| `src/core/LogCategories.h` | Add `lcContainer` logging category declaration |
| `src/core/LogCategories.cpp` | Define `lcContainer` logging category + register in LogManager |
| `src/gui/MainWindow.h` | Add `ContainerManager*`, `QSplitter*`, `QWidget* m_overlayArea`, resize override |
| `src/gui/MainWindow.cpp` | Replace QVBoxLayout with QSplitter, create ContainerManager, wire up, create default Container #0 |

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

// Docking mode for a container within the application layout.
// Panel-docked: lives in MainWindow's QSplitter (AetherSDR right-panel pattern).
// Overlay-docked: absolutely positioned over central widget (Thetis free-form pattern).
// Floating: separate top-level window on any monitor.
enum class DockMode {
    PanelDocked,    // In QSplitter (Container #0 default)
    OverlayDocked,  // Absolute position over central widget (Thetis style)
    Floating        // Separate window
};

class FloatingContainer;

// Container shell widget — equivalent to Thetis ucMeter.
// Provides title bar, drag, resize, axis-lock, content slot, and serialization.
// The content slot holds a placeholder in 3G-1, an AppletPanel in 3G-AP,
// or MeterWidget items in 3G-2+. The shell works identically regardless.
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

    // --- Dock Mode ---
    DockMode dockMode() const { return m_dockMode; }
    void setDockMode(DockMode mode);
    bool isFloating() const { return m_dockMode == DockMode::Floating; }
    bool isPanelDocked() const { return m_dockMode == DockMode::PanelDocked; }
    bool isOverlayDocked() const { return m_dockMode == DockMode::OverlayDocked; }

    // --- Axis Lock (overlay-docked position anchoring) ---
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
    // Only meaningful for OverlayDocked mode.
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

    // --- Content Slot ---
    // Returns the current content widget. In 3G-1 this is a placeholder.
    // In 3G-AP it will be an AppletPanel. In 3G-2+ it may hold MeterWidgets.
    QWidget* content() const { return m_content; }
    // Replace the content widget. Takes ownership via Qt parent-child.
    // The old content is deleted. Pass nullptr to clear.
    void setContent(QWidget* widget);

    // --- Serialization ---
    // From Thetis ucMeter.cs:1012-1038 (ToString) and 1039-1160 (TryParse)
    // Pipe-delimited string for AppSettings persistence.
    QString serialize() const;
    bool deserialize(const QString& data);

signals:
    void floatRequested();
    void dockRequested();
    void settingsRequested();
    void dockedMoved();
    void dockModeChanged(DockMode mode);

protected:
    void mouseMoveEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

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

    // Serialization helpers
    static QString axisLockToString(AxisLock lock);
    static AxisLock axisLockFromString(const QString& str);
    static QString dockModeToString(DockMode mode);
    static DockMode dockModeFromString(const QString& str);

    // --- Identity ---
    QString m_id;
    int m_rxSource{1};

    // --- State ---
    DockMode m_dockMode{DockMode::OverlayDocked};
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
    QColor m_backgroundColor{0x0f, 0x0f, 0x1a};

    // --- Drag/Resize state ---
    QPoint m_dragStartPos;
    QPoint m_resizeStartGlobal;
    QSize m_resizeStartSize;

    // --- Docked geometry (persisted separately from live pos, overlay mode only) ---
    QPoint m_dockedLocation;
    QSize m_dockedSize;
    QPoint m_delta;

    // --- UI elements ---
    QWidget* m_titleBar{nullptr};
    QWidget* m_contentHolder{nullptr};  // Layout wrapper for content slot
    QWidget* m_content{nullptr};        // The actual content widget
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
#include <QString>

namespace NereusSDR {

class ContainerWidget;

// Floating window wrapper — equivalent to Thetis frmMeterDisplay.
// Hosts a ContainerWidget when it is popped out of the main window.
// Also reused by AetherSDR-style applet pop-out in Phase 3G-AP.
class FloatingContainer : public QWidget {
    Q_OBJECT

public:
    explicit FloatingContainer(int rxSource, QWidget* parent = nullptr);
    ~FloatingContainer() override;

    QString id() const { return m_id; }
    void setId(const QString& id);

    int rxSource() const { return m_rxSource; }

    // From Thetis frmMeterDisplay.cs:168-179 — reparent container into this form
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

Create `src/gui/containers/FloatingContainer.cpp`:

```cpp
#include "FloatingContainer.h"
#include "ContainerWidget.h"
#include "core/LogCategories.h"

namespace NereusSDR {

FloatingContainer::FloatingContainer(int rxSource, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , m_rxSource(rxSource)
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

class QSplitter;

namespace NereusSDR {

class ContainerWidget;
class FloatingContainer;
enum class DockMode;

// Singleton lifecycle manager — combines Thetis MeterManager container lifecycle
// with AetherSDR AppletPanel layout awareness.
//
// Manages three docking modes:
// - PanelDocked: Container #0 in MainWindow's QSplitter (AetherSDR pattern)
// - OverlayDocked: Thetis-style absolute positioning over central widget
// - Floating: Separate window on any monitor
class ContainerManager : public QObject {
    Q_OBJECT

public:
    // dockParent: the widget that overlay-docked containers are parented to
    // splitter: the main QSplitter that panel-docked containers live in
    explicit ContainerManager(QWidget* dockParent, QSplitter* splitter,
                              QObject* parent = nullptr);
    ~ContainerManager() override;

    // --- Container lifecycle ---
    ContainerWidget* createContainer(int rxSource, DockMode mode);
    void destroyContainer(const QString& id);

    // --- Dock mode transitions ---
    void floatContainer(const QString& id);
    void dockContainer(const QString& id);           // returns to previous dock mode
    void panelDockContainer(const QString& id);      // move into splitter
    void overlayDockContainer(const QString& id);    // move to absolute position
    void recoverContainer(const QString& id);        // off-screen rescue

    // --- Axis-lock repositioning on main window resize (overlay-docked only) ---
    void updateDockedPositions(int hDelta, int vDelta);

    // --- Panel width (QSplitter state for Container #0) ---
    void saveSplitterState();
    void restoreSplitterState();

    // --- Persistence ---
    void saveState();
    void restoreState();

    // --- Queries ---
    QList<ContainerWidget*> allContainers() const;
    ContainerWidget* container(const QString& id) const;
    ContainerWidget* panelContainer() const;  // Container #0 (panel-docked), or nullptr
    int containerCount() const;

    // --- Visibility ---
    void setContainerVisible(const QString& id, bool visible);

signals:
    void containerAdded(const QString& id);
    void containerRemoved(const QString& id);

private:
    void setMeterFloating(ContainerWidget* container, FloatingContainer* form);
    void returnMeterFromFloating(ContainerWidget* container, FloatingContainer* form);
    void wireContainer(ContainerWidget* container);

    QWidget* m_dockParent{nullptr};    // Parent for overlay-docked containers
    QSplitter* m_splitter{nullptr};    // MainWindow splitter for panel-docked containers
    QMap<QString, ContainerWidget*> m_containers;
    QMap<QString, FloatingContainer*> m_floatingForms;
    QString m_panelContainerId;        // ID of the panel-docked container (Container #0)
};

} // namespace NereusSDR
```

Create `src/gui/containers/ContainerManager.cpp`:

```cpp
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QSplitter>

namespace NereusSDR {

ContainerManager::ContainerManager(QWidget* dockParent, QSplitter* splitter,
                                   QObject* parent)
    : QObject(parent)
    , m_dockParent(dockParent)
    , m_splitter(splitter)
{
    qCDebug(lcContainer) << "ContainerManager created";
}

ContainerManager::~ContainerManager()
{
    qCDebug(lcContainer) << "ContainerManager destroyed —"
                          << m_containers.size() << "containers";
}

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

Expected: Build succeeds with no errors.

- [ ] **Step 5: Commit skeleton**

```bash
git add src/gui/containers/ src/core/LogCategories.h src/core/LogCategories.cpp CMakeLists.txt
git commit -m "feat(3G-1): add container infrastructure skeleton — ContainerWidget, FloatingContainer, ContainerManager with 3 dock modes"
```

---

## Task 2: ContainerWidget — UI Layout, Title Bar, and Content Slot

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — constructor (lines 108-165), `setTopBarButtons()` (lines 605-624), `setTitle()` (lines 625-639), `setupBorder()` (lines 640-643). The content slot design follows AetherSDR's pattern where AppletPanel is a replaceable child widget.

- [ ] **Step 1: Implement buildUI() and setContent()**

Replace `ContainerWidget.cpp`:

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

    // Default placeholder content — replaced by setContent() in later phases
    auto* placeholder = new QLabel(QStringLiteral("Container"), this);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setStyleSheet(QStringLiteral(
        "color: #405060; font-size: 14px; background: transparent;"));
    setContent(placeholder);

    qCDebug(lcContainer) << "Container created:" << m_id;
}

ContainerWidget::~ContainerWidget()
{
    qCDebug(lcContainer) << "Container destroyed:" << m_id;
}

void ContainerWidget::buildUI()
{
    // Main layout: title bar on top, content holder fills rest
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

    // Axis lock button (overlay-docked only) — cycles through 8 positions
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

    // --- Content holder (layout slot for setContent) ---
    m_contentHolder = new QWidget(this);
    m_contentHolder->setMouseTracking(true);
    m_contentHolder->setStyleSheet(QStringLiteral("background: #0f0f1a;"));
    new QVBoxLayout(m_contentHolder);  // layout created, content added via setContent()
    m_contentHolder->layout()->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(m_contentHolder, 1);

    // --- Resize grip (bottom-right, hidden until hover) ---
    // From Thetis ucMeter.cs:157 — pbGrab.Hide()
    // Not shown for panel-docked mode (QSplitter handle serves this role).
    m_resizeGrip = new QWidget(this);
    m_resizeGrip->setFixedSize(12, 12);
    m_resizeGrip->setCursor(Qt::SizeFDiagCursor);
    m_resizeGrip->setStyleSheet(QStringLiteral(
        "background: #405060; border-radius: 2px;"));
    m_resizeGrip->setVisible(false);

    // --- Wire button signals ---
    connect(m_btnFloat, &QPushButton::clicked, this, [this]() {
        if (isFloating()) {
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

    // Install event filter for title bar drag + resize grip interaction
    m_titleBar->installEventFilter(this);
    m_titleLabel->installEventFilter(this);
    m_resizeGrip->installEventFilter(this);
}

void ContainerWidget::setContent(QWidget* widget)
{
    // Replace the content in the content holder.
    // AetherSDR pattern: AppletPanel is just a QWidget set as content.
    // The container shell (title bar, drag, resize, persist) works identically.
    QLayout* layout = m_contentHolder->layout();

    // Remove old content
    if (m_content) {
        layout->removeWidget(m_content);
        m_content->deleteLater();
    }

    m_content = widget;
    if (m_content) {
        m_content->setParent(m_contentHolder);
        layout->addWidget(m_content);
    }
}

void ContainerWidget::updateTitleBar()
{
    // From Thetis ucMeter.cs:605-624 — setTopBarButtons()
    // Adapts button visibility based on dock mode.
    if (isFloating()) {
        m_btnFloat->setText(QStringLiteral("\u2199"));  // ↙ dock icon
        m_btnFloat->setToolTip(QStringLiteral("Dock"));
        m_btnAxis->setVisible(false);
        m_btnPin->setVisible(true);
    } else if (isPanelDocked()) {
        // Panel-docked: float button available, axis lock not meaningful
        m_btnFloat->setText(QStringLiteral("\u2197"));  // ↗ float icon
        m_btnFloat->setToolTip(QStringLiteral("Float"));
        m_btnAxis->setVisible(false);  // QSplitter manages position
        m_btnPin->setVisible(false);
    } else {
        // Overlay-docked: full Thetis controls
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

- [ ] **Step 2: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

Expected: Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/ContainerWidget.h src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement ContainerWidget UI layout, title bar, content slot, dock-mode-aware buttons"
```

---

## Task 3: ContainerWidget — Properties, Setters, and Drag/Resize

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — property setters (lines 257-911), drag via pnlBar/lblRX (lines 281-762), resize via pbGrab (lines 400-549), hover show/hide (lines 1198-1229).

- [ ] **Step 1: Implement all property setters**

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

void ContainerWidget::setDockMode(DockMode mode)
{
    if (m_dockMode == mode) {
        return;
    }
    m_dockMode = mode;
    updateTitleBar();
    // Resize grip: only show for overlay-docked and floating, not panel-docked
    // (QSplitter handle serves the resize role for panel-docked)
    emit dockModeChanged(mode);
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
    emit dockedMoved();
}

void ContainerWidget::setPinOnTop(bool pin)
{
    // From Thetis ucMeter.cs:974-978
    m_pinOnTop = pin;
    m_btnPin->setText(pin ? QStringLiteral("\U0001F4CD") : QStringLiteral("\U0001F4CC"));
    setTopMost();
}

void ContainerWidget::setTopMost()
{
    // From Thetis ucMeter.cs:980-993
    if (isFloating() && parentWidget()) {
        auto* fc = qobject_cast<FloatingContainer*>(parentWidget());
        if (fc) {
            bool wasVisible = fc->isVisible();
            if (m_pinOnTop) {
                fc->setWindowFlags(fc->windowFlags() | Qt::WindowStaysOnTopHint);
            } else {
                fc->setWindowFlags(fc->windowFlags() & ~Qt::WindowStaysOnTopHint);
            }
            if (wasVisible) {
                fc->show();
            }
        }
    }
}

void ContainerWidget::setBorder(bool border) { m_border = border; setupBorder(); }
void ContainerWidget::setLocked(bool locked) { m_locked = locked; }
void ContainerWidget::setContainerEnabled(bool enabled) { m_enabled = enabled; }
void ContainerWidget::setShowOnRx(bool show) { m_showOnRx = show; }
void ContainerWidget::setShowOnTx(bool show) { m_showOnTx = show; }
void ContainerWidget::setHiddenByMacro(bool hidden) { m_hiddenByMacro = hidden; }
void ContainerWidget::setContainerMinimises(bool minimises) { m_containerMinimises = minimises; }
void ContainerWidget::setContainerHidesWhenRxNotUsed(bool hides) { m_containerHidesWhenRxNotUsed = hides; }
void ContainerWidget::setNotes(const QString& notes) { m_notes = notes; m_notes.remove(QLatin1Char('|')); updateTitle(); }
void ContainerWidget::setNoControls(bool noControls) { m_noControls = noControls; }
void ContainerWidget::setAutoHeight(bool autoHeight) { m_autoHeight = autoHeight; }
void ContainerWidget::setDockedLocation(const QPoint& loc) { m_dockedLocation = loc; }
void ContainerWidget::setDockedSize(const QSize& size) { m_dockedSize = size; }
void ContainerWidget::setDelta(const QPoint& delta) { m_delta = delta; }

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
    if (m_dockedLocation != pos()) { move(m_dockedLocation); moved = true; }
    if (m_dockedSize != size()) { resize(m_dockedSize); moved = true; }
    if (moved) { update(); }
}

int ContainerWidget::roundToNearestTen(int value)
{
    return ((value + 5) / 10) * 10;
}
```

- [ ] **Step 2: Implement hover, drag, resize, and event filter**

Append to `ContainerWidget.cpp`:

```cpp
void ContainerWidget::mouseMoveEvent(QMouseEvent* event)
{
    // From Thetis ucMeter.cs:1198-1229 — pnlContainer_MouseMove
    // Panel-docked containers suppress the resize grip (QSplitter handles resize).
    bool noControls = m_noControls && !(QApplication::keyboardModifiers() & Qt::ShiftModifier);

    if (!m_dragging && !noControls) {
        bool inTitleRegion = event->position().y() < 22;
        if (inTitleRegion && !m_titleBar->isVisible()) {
            m_titleBar->setVisible(true);
            m_titleBar->raise();
        } else if (!inTitleRegion && m_titleBar->isVisible() && !m_dragging) {
            m_titleBar->setVisible(false);
        }
    }

    // Resize grip: only for overlay-docked and floating, not panel-docked
    if (!m_resizing && !noControls && !isPanelDocked()) {
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

    if (m_dragging) { updateDrag(event->globalPosition().toPoint()); }
    if (m_resizing) { updateResize(event->globalPosition().toPoint()); }

    QWidget::mouseMoveEvent(event);
}

void ContainerWidget::leaveEvent(QEvent* event)
{
    if (!m_dragging && !m_resizing) {
        m_titleBar->setVisible(false);
        m_resizeGrip->setVisible(false);
    }
    QWidget::leaveEvent(event);
}

bool ContainerWidget::eventFilter(QObject* watched, QEvent* event)
{
    // Title bar drag
    if ((watched == m_titleBar || watched == m_titleLabel) && !m_locked) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && !isPanelDocked()) {
                // Panel-docked containers can't be dragged (QSplitter manages position)
                beginDrag(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_dragging) {
            auto* me = static_cast<QMouseEvent*>(event);
            updateDrag(me->globalPosition().toPoint());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_dragging) {
            endDrag();
            return true;
        }
    }

    // Resize grip (not available for panel-docked)
    if (watched == m_resizeGrip && !m_locked && !isPanelDocked()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                beginResize(me->globalPosition().toPoint());
                return true;
            }
        } else if (event->type() == QEvent::MouseMove && m_resizing) {
            auto* me = static_cast<QMouseEvent*>(event);
            updateResize(me->globalPosition().toPoint());
            return true;
        } else if (event->type() == QEvent::MouseButtonRelease && m_resizing) {
            endResize();
            return true;
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ContainerWidget::beginDrag(const QPoint& globalPos)
{
    // From Thetis ucMeter.cs:281-294
    m_dragging = true;
    if (isFloating()) {
        m_dragStartPos = globalPos - parentWidget()->pos();
    } else {
        raise();
        m_dragStartPos = globalPos - pos();
    }
}

void ContainerWidget::updateDrag(const QPoint& globalPos)
{
    if (!m_dragging) { return; }

    if (isFloating()) {
        // From Thetis ucMeter.cs:319-345
        QPoint newPos = globalPos - m_dragStartPos;
        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            newPos.setX(roundToNearestTen(newPos.x()));
            newPos.setY(roundToNearestTen(newPos.y()));
        }
        if (parentWidget() && parentWidget()->pos() != newPos) {
            parentWidget()->move(newPos);
        }
    } else {
        // From Thetis ucMeter.cs:346-374 — overlay-docked, clamped
        QPoint newPos = globalPos - m_dragStartPos;
        if (QApplication::keyboardModifiers() & Qt::ControlModifier) {
            newPos.setX(roundToNearestTen(newPos.x()));
            newPos.setY(roundToNearestTen(newPos.y()));
        }
        if (parentWidget()) {
            int maxX = parentWidget()->width() - width();
            int maxY = parentWidget()->height() - height();
            newPos.setX(std::clamp(newPos.x(), 0, std::max(0, maxX)));
            newPos.setY(std::clamp(newPos.y(), 0, std::max(0, maxY)));
        }
        if (pos() != newPos) { move(newPos); update(); }
    }
}

void ContainerWidget::endDrag()
{
    m_dragging = false;
    m_dragStartPos = QPoint();
    if (isOverlayDocked()) {
        m_dockedLocation = pos();
        emit dockedMoved();
    }
}

void ContainerWidget::beginResize(const QPoint& globalPos)
{
    m_resizeStartGlobal = globalPos;
    m_resizeStartSize = isFloating() && parentWidget() ? parentWidget()->size() : size();
    m_resizing = true;
    raise();
}

void ContainerWidget::updateResize(const QPoint& globalPos)
{
    if (!m_resizing) { return; }
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
    m_resizing = false;
    m_resizeStartGlobal = QPoint();
    if (isOverlayDocked()) { m_dockedSize = size(); }
}

void ContainerWidget::doResize(int w, int h)
{
    // From Thetis ucMeter.cs:520-549
    w = std::max(w, kMinContainerWidth);
    h = std::max(h, kMinContainerHeight);

    if (isFloating()) {
        if (parentWidget()) { parentWidget()->resize(w, h); }
    } else {
        if (parentWidget()) {
            if (x() + w > parentWidget()->width()) { w = parentWidget()->width() - x(); }
            if (y() + h > parentWidget()->height()) { h = parentWidget()->height() - y(); }
        }
        QSize newSize(w, h);
        if (newSize != size()) { resize(newSize); update(); }
    }
}
```

- [ ] **Step 3: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

- [ ] **Step 4: Commit**

```bash
git add src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement ContainerWidget properties, drag, resize, hover — dock-mode-aware"
```

---

## Task 4: ContainerWidget — Serialization

**Files:**
- Modify: `src/gui/containers/ContainerWidget.cpp`

Porting from: Thetis `ucMeter.cs` — `ToString()` (lines 1012-1038) and `TryParse()` (lines 1039-1160). Extended with DockMode field (field 23) for the hybrid model.

- [ ] **Step 1: Implement serialization helpers and serialize()**

Append to `ContainerWidget.cpp`:

```cpp
QString ContainerWidget::axisLockToString(AxisLock lock)
{
    static const char* names[] = {
        "LEFT", "TOPLEFT", "TOP", "TOPRIGHT",
        "RIGHT", "BOTTOMRIGHT", "BOTTOM", "BOTTOMLEFT"
    };
    int idx = static_cast<int>(lock);
    return (idx >= 0 && idx < 8) ? QString::fromLatin1(names[idx]) : QStringLiteral("TOPLEFT");
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

QString ContainerWidget::dockModeToString(DockMode mode)
{
    switch (mode) {
    case DockMode::PanelDocked:   return QStringLiteral("PANEL");
    case DockMode::OverlayDocked: return QStringLiteral("OVERLAY");
    case DockMode::Floating:      return QStringLiteral("FLOATING");
    }
    return QStringLiteral("OVERLAY");
}

DockMode ContainerWidget::dockModeFromString(const QString& str)
{
    if (str == QStringLiteral("PANEL"))    return DockMode::PanelDocked;
    if (str == QStringLiteral("FLOATING")) return DockMode::Floating;
    // "OVERLAY" or legacy "true"/"false" from Thetis format
    return DockMode::OverlayDocked;
}

QString ContainerWidget::serialize() const
{
    // Based on Thetis ucMeter.cs:1012-1038, extended with DockMode (field 23)
    QStringList p;
    p << m_id;                                                              // 0
    p << QString::number(m_rxSource);                                       // 1
    p << QString::number(m_dockedLocation.x());                             // 2
    p << QString::number(m_dockedLocation.y());                             // 3
    p << QString::number(m_dockedSize.width());                             // 4
    p << QString::number(m_dockedSize.height());                            // 5
    p << (isFloating() ? QStringLiteral("true") : QStringLiteral("false")); // 6 (Thetis compat)
    p << QString::number(m_delta.x());                                      // 7
    p << QString::number(m_delta.y());                                      // 8
    p << axisLockToString(m_axisLock);                                      // 9
    p << (m_pinOnTop ? QStringLiteral("true") : QStringLiteral("false"));   // 10
    p << (m_border ? QStringLiteral("true") : QStringLiteral("false"));     // 11
    p << m_backgroundColor.name(QColor::HexArgb);                           // 12
    p << (m_noControls ? QStringLiteral("true") : QStringLiteral("false")); // 13
    p << (m_enabled ? QStringLiteral("true") : QStringLiteral("false"));    // 14
    p << m_notes;                                                           // 15
    p << (m_containerMinimises ? QStringLiteral("true") : QStringLiteral("false")); // 16
    p << (m_autoHeight ? QStringLiteral("true") : QStringLiteral("false")); // 17
    p << (m_showOnRx ? QStringLiteral("true") : QStringLiteral("false"));   // 18
    p << (m_showOnTx ? QStringLiteral("true") : QStringLiteral("false"));   // 19
    p << (m_locked ? QStringLiteral("true") : QStringLiteral("false"));     // 20
    p << (m_containerHidesWhenRxNotUsed ? QStringLiteral("true") : QStringLiteral("false")); // 21
    p << (m_hiddenByMacro ? QStringLiteral("true") : QStringLiteral("false")); // 22
    p << dockModeToString(m_dockMode);                                      // 23 (NereusSDR extension)
    return p.join(QLatin1Char('|'));
}

bool ContainerWidget::deserialize(const QString& data)
{
    // Based on Thetis ucMeter.cs:1039-1160, backward-compatible
    if (data.isEmpty()) { return false; }

    QStringList p = data.split(QLatin1Char('|'));
    if (p.size() < 13) {
        qCWarning(lcContainer) << "deserialize: too few fields:" << p.size();
        return false;
    }

    if (p[0].isEmpty()) { return false; }
    setId(p[0]);

    bool ok = false;
    int rx = p[1].toInt(&ok); if (!ok) { return false; }
    setRxSource(rx);

    int x = p[2].toInt(&ok); if (!ok) { return false; }
    int y = p[3].toInt(&ok); if (!ok) { return false; }
    int w = p[4].toInt(&ok); if (!ok) { return false; }
    int h = p[5].toInt(&ok); if (!ok) { return false; }
    setDockedLocation(QPoint(x, y));
    setDockedSize(QSize(w, h));

    // Field 6: Thetis floating bool — used as fallback if field 23 absent
    bool thetisFloating = (p[6].toLower() == QStringLiteral("true"));

    x = p[7].toInt(&ok); if (!ok) { return false; }
    y = p[8].toInt(&ok); if (!ok) { return false; }
    setDelta(QPoint(x, y));

    setAxisLock(axisLockFromString(p[9]));
    setPinOnTop(p[10].toLower() == QStringLiteral("true"));
    setBorder(p[11].toLower() == QStringLiteral("true"));

    QColor bgColor(p[12]);
    if (bgColor.isValid()) {
        m_backgroundColor = bgColor;
        m_contentHolder->setStyleSheet(
            QStringLiteral("background: %1;").arg(bgColor.name(QColor::HexArgb)));
    }

    if (p.size() > 13) { setNoControls(p[13].toLower() == QStringLiteral("true")); }
    if (p.size() > 14) { setContainerEnabled(p[14].toLower() == QStringLiteral("true")); }
    if (p.size() > 15) { setNotes(p[15]); }
    if (p.size() > 16) { setContainerMinimises(p[16].toLower() == QStringLiteral("true")); }
    if (p.size() > 17) { setAutoHeight(p[17].toLower() == QStringLiteral("true")); }
    if (p.size() > 19) { setShowOnRx(p[18].toLower() == QStringLiteral("true")); setShowOnTx(p[19].toLower() == QStringLiteral("true")); }
    if (p.size() > 20) { setLocked(p[20].toLower() == QStringLiteral("true")); }
    if (p.size() > 21) { setContainerHidesWhenRxNotUsed(p[21].toLower() == QStringLiteral("true")); }
    if (p.size() > 22) { setHiddenByMacro(p[22].toLower() == QStringLiteral("true")); }

    // Field 23: NereusSDR DockMode extension
    if (p.size() > 23) {
        setDockMode(dockModeFromString(p[23]));
    } else {
        // Thetis compat: field 6 floating bool → DockMode
        setDockMode(thetisFloating ? DockMode::Floating : DockMode::OverlayDocked);
    }

    qCDebug(lcContainer) << "Deserialized:" << m_id << "rx:" << m_rxSource
                          << "mode:" << dockModeToString(m_dockMode)
                          << "pos:" << m_dockedLocation << "size:" << m_dockedSize;
    return true;
}
```

- [ ] **Step 2: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/ContainerWidget.cpp
git commit -m "feat(3G-1): implement serialize/deserialize with DockMode extension (24 fields)"
```

---

## Task 5: FloatingContainer — Full Implementation

**Files:**
- Modify: `src/gui/containers/FloatingContainer.h` (already final from Task 1)
- Modify: `src/gui/containers/FloatingContainer.cpp`

Porting from: Thetis `frmMeterDisplay.cs` — constructor (lines 62-78), `TakeOwner()` (lines 168-179), `frmMeterDisplay_FormClosing()` (lines 158-166), `OnWindowStateChanged()` (lines 114-139), `setTitle()` (lines 140-144), `ID` setter with geometry restore (lines 145-157).

- [ ] **Step 1: Implement FloatingContainer**

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
    setMinimumSize(ContainerWidget::kMinContainerWidth,
                   ContainerWidget::kMinContainerHeight);
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
    restoreGeometry();
    updateTitle();
}

void FloatingContainer::takeOwner(ContainerWidget* container)
{
    // From Thetis frmMeterDisplay.cs:168-179
    m_containerMinimises = container->containerMinimises();
    m_formEnabled = container->isContainerEnabled();

    container->setParent(this);
    if (!layout()) {
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(0, 0, 0, 0);
    }
    QLayoutItem* child = nullptr;
    while ((child = layout()->takeAt(0)) != nullptr) { delete child; }
    layout()->addWidget(container);
    container->show();
    container->raise();

    qCDebug(lcContainer) << "FloatingContainer" << m_id << "took ownership of" << container->id();
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
            if (shouldShow) { show(); }
        }
    }
}

void FloatingContainer::closeEvent(QCloseEvent* event)
{
    // From Thetis frmMeterDisplay.cs:158-166
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
    auto& s = AppSettings::instance();
    QRect r = geometry();
    s.setValue(QStringLiteral("MeterDisplay_%1_Geometry").arg(m_id),
              QStringLiteral("%1,%2,%3,%4").arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height()));
}

void FloatingContainer::restoreGeometry()
{
    auto& s = AppSettings::instance();
    QString val = s.value(QStringLiteral("MeterDisplay_%1_Geometry").arg(m_id)).toString();
    if (val.isEmpty()) { return; }
    QStringList parts = val.split(QLatin1Char(','));
    if (parts.size() != 4) { return; }
    bool ok1, ok2, ok3, ok4;
    int x = parts[0].toInt(&ok1);
    int y = parts[1].toInt(&ok2);
    int w = parts[2].toInt(&ok3);
    int h = parts[3].toInt(&ok4);
    if (ok1 && ok2 && ok3 && ok4) { setGeometry(x, y, w, h); }
}

void FloatingContainer::updateTitle()
{
    // From Thetis frmMeterDisplay.cs:140-144
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

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/FloatingContainer.h src/gui/containers/FloatingContainer.cpp
git commit -m "feat(3G-1): implement FloatingContainer — reparent, TopMost, minimize-with-console, geometry persist"
```

---

## Task 6: ContainerManager — Lifecycle and Three Dock Modes

**Files:**
- Modify: `src/gui/containers/ContainerManager.cpp`

Porting from: Thetis `MeterManager.cs` — `AddMeterContainer()` (5613-5673), `setMeterFloating()` (5894-5918), `returnMeterFromFloating()` (5867-5893), `RemoveMeterContainer()` (6533-6579), `RecoverContainer()` (6514-6531). Extended with QSplitter awareness for panel-docked mode.

- [ ] **Step 1: Implement full ContainerManager**

Replace `ContainerManager.cpp`:

```cpp
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QSplitter>
#include <algorithm>

namespace NereusSDR {

ContainerManager::ContainerManager(QWidget* dockParent, QSplitter* splitter,
                                   QObject* parent)
    : QObject(parent)
    , m_dockParent(dockParent)
    , m_splitter(splitter)
{
    qCDebug(lcContainer) << "ContainerManager created";
}

ContainerManager::~ContainerManager()
{
    qCDebug(lcContainer) << "ContainerManager destroyed —" << m_containers.size() << "containers";
}

void ContainerManager::wireContainer(ContainerWidget* container)
{
    connect(container, &ContainerWidget::floatRequested, this, [this, container]() {
        floatContainer(container->id());
    });
    connect(container, &ContainerWidget::dockRequested, this, [this, container]() {
        dockContainer(container->id());
    });
}

ContainerWidget* ContainerManager::createContainer(int rxSource, DockMode mode)
{
    // From Thetis MeterManager.cs:5613-5673
    auto* container = new ContainerWidget(nullptr);  // parent set below by mode
    container->setRxSource(rxSource);
    container->setDockMode(mode);

    auto* floatingForm = new FloatingContainer(rxSource);
    floatingForm->setId(container->id());

    wireContainer(container);

    m_containers.insert(container->id(), container);
    m_floatingForms.insert(container->id(), floatingForm);

    // Place container according to dock mode
    switch (mode) {
    case DockMode::PanelDocked:
        container->setParent(m_splitter);
        m_splitter->addWidget(container);
        m_panelContainerId = container->id();
        container->show();
        break;
    case DockMode::OverlayDocked:
        container->setParent(m_dockParent);
        container->show();
        container->raise();
        break;
    case DockMode::Floating:
        setMeterFloating(container, floatingForm);
        break;
    }

    qCDebug(lcContainer) << "Created container:" << container->id()
                          << "rx:" << rxSource << "mode:" << static_cast<int>(mode);
    emit containerAdded(container->id());
    return container;
}

void ContainerManager::destroyContainer(const QString& id)
{
    // From Thetis MeterManager.cs:6533-6579
    if (!m_containers.contains(id)) {
        qCWarning(lcContainer) << "destroyContainer: unknown id:" << id;
        return;
    }

    if (m_floatingForms.contains(id)) {
        FloatingContainer* form = m_floatingForms.take(id);
        form->hide();
        form->deleteLater();
    }

    ContainerWidget* container = m_containers.take(id);
    container->hide();
    container->setParent(nullptr);
    container->deleteLater();

    if (m_panelContainerId == id) { m_panelContainerId.clear(); }

    qCDebug(lcContainer) << "Destroyed container:" << id;
    emit containerRemoved(id);
}

void ContainerManager::floatContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) { return; }
    setMeterFloating(m_containers[id], m_floatingForms[id]);
}

void ContainerManager::dockContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) { return; }

    // Return to previous dock mode: panel if it was the panel container, overlay otherwise
    if (id == m_panelContainerId) {
        panelDockContainer(id);
    } else {
        overlayDockContainer(id);
    }
}

void ContainerManager::panelDockContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) { return; }
    ContainerWidget* container = m_containers[id];
    FloatingContainer* form = m_floatingForms[id];

    form->setContainerFloating(false);
    form->hide();
    container->hide();
    container->setParent(m_splitter);
    m_splitter->addWidget(container);
    container->setDockMode(DockMode::PanelDocked);
    container->show();
    m_panelContainerId = id;

    qCDebug(lcContainer) << "Panel-docked container:" << id;
}

void ContainerManager::overlayDockContainer(const QString& id)
{
    if (!m_containers.contains(id) || !m_floatingForms.contains(id)) { return; }
    ContainerWidget* container = m_containers[id];
    FloatingContainer* form = m_floatingForms[id];

    // From Thetis MeterManager.cs:5867-5893
    form->setContainerFloating(false);
    form->hide();
    container->hide();
    container->setParent(m_dockParent);
    container->setDockMode(DockMode::OverlayDocked);
    container->restoreLocation();
    container->show();
    container->raise();

    qCDebug(lcContainer) << "Overlay-docked container:" << id;
}

void ContainerManager::setMeterFloating(ContainerWidget* container, FloatingContainer* form)
{
    // From Thetis MeterManager.cs:5894-5918
    container->hide();
    form->takeOwner(container);
    form->setContainerFloating(true);
    container->setDockMode(DockMode::Floating);
    form->show();
    qCDebug(lcContainer) << "Floated container:" << container->id();
}

void ContainerManager::returnMeterFromFloating(ContainerWidget* container, FloatingContainer* form)
{
    // From Thetis MeterManager.cs:5867-5893
    form->setContainerFloating(false);
    form->hide();
    container->hide();
    container->setParent(m_dockParent);
    container->setDockMode(DockMode::OverlayDocked);
    container->restoreLocation();
    container->show();
    container->raise();
    qCDebug(lcContainer) << "Docked container:" << container->id();
}

void ContainerManager::recoverContainer(const QString& id)
{
    // From Thetis MeterManager.cs:6514-6531
    if (!m_containers.contains(id)) { return; }
    ContainerWidget* container = m_containers[id];

    if (container->isFloating()) { overlayDockContainer(id); }
    container->setContainerEnabled(true);
    container->show();

    if (m_dockParent) {
        int cx = (m_dockParent->width() / 2) - (container->width() / 2);
        int cy = (m_dockParent->height() / 2) - (container->height() / 2);
        container->move(cx, cy);
        container->storeLocation();
    }
    qCDebug(lcContainer) << "Recovered container:" << id;
}

void ContainerManager::updateDockedPositions(int hDelta, int vDelta)
{
    // From Thetis MeterManager.cs:5812-5865 — only affects overlay-docked containers
    for (auto it = m_containers.constBegin(); it != m_containers.constEnd(); ++it) {
        ContainerWidget* c = it.value();
        if (!c->isOverlayDocked()) { continue; }

        QPoint dockedLoc = c->dockedLocation();
        QPoint delta = c->delta();
        QPoint newLocation;

        switch (c->axisLock()) {
        case AxisLock::Right:
        case AxisLock::BottomRight:
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta, dockedLoc.y() - delta.y() + vDelta);
            break;
        case AxisLock::BottomLeft:
        case AxisLock::Left:
            newLocation = QPoint(dockedLoc.x(), dockedLoc.y() - delta.y() + vDelta);
            break;
        case AxisLock::TopLeft:
            newLocation = QPoint(dockedLoc.x(), dockedLoc.y());
            break;
        case AxisLock::Top:
        case AxisLock::TopRight:
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta, dockedLoc.y());
            break;
        case AxisLock::Bottom:
            newLocation = QPoint(dockedLoc.x() - delta.x() + hDelta, dockedLoc.y() - delta.y() + vDelta);
            break;
        }

        if (m_dockParent) {
            int maxX = m_dockParent->width() - c->width();
            int maxY = m_dockParent->height() - c->height();
            newLocation.setX(std::clamp(newLocation.x(), 0, std::max(0, maxX)));
            newLocation.setY(std::clamp(newLocation.y(), 0, std::max(0, maxY)));
        }

        if (newLocation != c->pos()) { c->move(newLocation); }
    }
}

void ContainerManager::saveSplitterState()
{
    if (!m_splitter) { return; }
    QList<int> sizes = m_splitter->sizes();
    QStringList parts;
    for (int s : sizes) { parts << QString::number(s); }
    AppSettings::instance().setValue(QStringLiteral("MainSplitterSizes"), parts.join(QLatin1Char(',')));
}

void ContainerManager::restoreSplitterState()
{
    if (!m_splitter) { return; }
    QString val = AppSettings::instance().value(QStringLiteral("MainSplitterSizes")).toString();
    if (val.isEmpty()) { return; }
    QStringList parts = val.split(QLatin1Char(','));
    QList<int> sizes;
    for (const QString& p : parts) {
        bool ok;
        int s = p.toInt(&ok);
        if (ok) { sizes << s; }
    }
    if (sizes.size() == m_splitter->count()) { m_splitter->setSizes(sizes); }
}

QList<ContainerWidget*> ContainerManager::allContainers() const { return m_containers.values(); }
ContainerWidget* ContainerManager::container(const QString& id) const { return m_containers.value(id, nullptr); }
ContainerWidget* ContainerManager::panelContainer() const { return m_containers.value(m_panelContainerId, nullptr); }
int ContainerManager::containerCount() const { return m_containers.size(); }

void ContainerManager::setContainerVisible(const QString& id, bool visible)
{
    if (!m_containers.contains(id)) { return; }
    ContainerWidget* c = m_containers[id];
    if (c->isFloating() && m_floatingForms.contains(id)) {
        m_floatingForms[id]->setVisible(visible);
    } else {
        c->setVisible(visible);
    }
}

void ContainerManager::saveState()
{
    // From Thetis MeterManager.cs:6391-6447
    auto& s = AppSettings::instance();

    QString oldIdList = s.value(QStringLiteral("ContainerIdList")).toString();
    if (!oldIdList.isEmpty()) {
        for (const QString& oldId : oldIdList.split(QLatin1Char(','))) {
            s.remove(QStringLiteral("ContainerData_%1").arg(oldId));
            s.remove(QStringLiteral("MeterDisplay_%1_Geometry").arg(oldId));
        }
    }

    QStringList idList;
    for (auto it = m_containers.constBegin(); it != m_containers.constEnd(); ++it) {
        ContainerWidget* c = it.value();
        if (c->isOverlayDocked()) { c->storeLocation(); }
        s.setValue(QStringLiteral("ContainerData_%1").arg(c->id()), c->serialize());
        idList << c->id();
        if (m_floatingForms.contains(c->id())) { m_floatingForms[c->id()]->saveGeometry(); }
    }

    s.setValue(QStringLiteral("ContainerIdList"), idList.join(QLatin1Char(',')));
    s.setValue(QStringLiteral("ContainerCount"), QString::number(m_containers.size()));
    saveSplitterState();

    qCDebug(lcContainer) << "Saved" << m_containers.size() << "container(s)";
}

void ContainerManager::restoreState()
{
    // From Thetis MeterManager.cs:6012-6105
    auto& s = AppSettings::instance();

    QString idList = s.value(QStringLiteral("ContainerIdList")).toString();
    if (idList.isEmpty()) {
        qCDebug(lcContainer) << "No saved containers found";
        return;
    }

    QStringList ids = idList.split(QLatin1Char(','), Qt::SkipEmptyParts);
    int restored = 0;

    for (const QString& id : ids) {
        QString data = s.value(QStringLiteral("ContainerData_%1").arg(id)).toString();
        if (data.isEmpty()) { continue; }

        auto* container = new ContainerWidget(nullptr);
        if (!container->deserialize(data)) {
            qCWarning(lcContainer) << "Failed to deserialize:" << id;
            delete container;
            continue;
        }

        auto* floatingForm = new FloatingContainer(container->rxSource());
        floatingForm->setId(container->id());

        wireContainer(container);
        m_containers.insert(container->id(), container);
        m_floatingForms.insert(container->id(), floatingForm);

        // Apply dock mode
        switch (container->dockMode()) {
        case DockMode::PanelDocked:
            container->setParent(m_splitter);
            m_splitter->addWidget(container);
            m_panelContainerId = container->id();
            container->show();
            break;
        case DockMode::OverlayDocked:
            container->setParent(m_dockParent);
            container->restoreLocation();
            if (container->isContainerEnabled() && !container->isHiddenByMacro()) {
                container->show();
                container->raise();
            }
            break;
        case DockMode::Floating:
            container->resize(floatingForm->size());
            setMeterFloating(container, floatingForm);
            if (container->isContainerEnabled() && !container->isHiddenByMacro()) {
                floatingForm->show();
            }
            break;
        }

        restored++;
        emit containerAdded(container->id());
    }

    restoreSplitterState();
    qCDebug(lcContainer) << "Restored" << restored << "container(s)";
}

} // namespace NereusSDR
```

- [ ] **Step 2: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

- [ ] **Step 3: Commit**

```bash
git add src/gui/containers/ContainerManager.h src/gui/containers/ContainerManager.cpp
git commit -m "feat(3G-1): implement ContainerManager — 3 dock modes, axis-lock, QSplitter, persistence"
```

---

## Task 7: Wire into MainWindow — QSplitter + Container #0

**Files:**
- Modify: `src/gui/MainWindow.h`
- Modify: `src/gui/MainWindow.cpp`

This replaces the current QVBoxLayout central widget with a QSplitter that divides the window into spectrum (left) and Container #0 panel (right). The QSplitter gives proper layout management, a draggable divider, and prevents the spectrum from rendering under the panel.

- [ ] **Step 1: Add members to MainWindow.h**

Add includes/forward declarations:

```cpp
class QSplitter;
```

```cpp
class ContainerManager;
```

Add to `private:` section:

```cpp
    // Container infrastructure (Phase 3G-1)
    ContainerManager* m_containerManager{nullptr};
    QSplitter* m_mainSplitter{nullptr};
    int m_hDelta{0};
    int m_vDelta{0};
```

Add to `protected:`:

```cpp
    void resizeEvent(QResizeEvent* event) override;
```

Add to `private:`:

```cpp
    void createDefaultContainers();
```

- [ ] **Step 2: Rewrite buildUI() to use QSplitter**

Add includes at top of MainWindow.cpp:

```cpp
#include "containers/ContainerManager.h"
#include "containers/ContainerWidget.h"
#include <QSplitter>
```

Replace `buildUI()` with:

```cpp
void MainWindow::buildUI()
{
    setWindowTitle(QStringLiteral("NereusSDR %1").arg(NEREUSSDR_VERSION));
    setMinimumSize(800, 600);
    resize(1280, 800);

    // --- Main QSplitter: spectrum (left) + container panel (right) ---
    // AetherSDR pattern: right panel is a proper layout element, not an overlay.
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setHandleWidth(3);
    m_mainSplitter->setStyleSheet(QStringLiteral(
        "QSplitter::handle { background: #203040; }"));

    // Left side: spectrum + zoom bar
    auto* spectrumPane = new QWidget(m_mainSplitter);
    auto* spectrumLayout = new QVBoxLayout(spectrumPane);
    spectrumLayout->setContentsMargins(0, 0, 0, 0);

    m_spectrumWidget = new SpectrumWidget(spectrumPane);
    m_spectrumWidget->loadSettings();
    spectrumLayout->addWidget(m_spectrumWidget, 1);

    // Zoom slider bar
    auto* zoomBar = new QSlider(Qt::Horizontal, spectrumPane);
    zoomBar->setRange(1, 768);
    zoomBar->setValue(768);
    zoomBar->setFixedHeight(20);
    zoomBar->setToolTip(QStringLiteral("Zoom: drag to adjust spectrum bandwidth"));
    zoomBar->setStyleSheet(QStringLiteral(
        "QSlider { background: #0a0a14; }"
        "QSlider::groove:horizontal { background: #1a2a3a; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: #00b4d8; width: 14px; margin: -4px 0; border-radius: 7px; }"));
    spectrumLayout->addWidget(zoomBar);
    connect(zoomBar, &QSlider::valueChanged, this, [this](int val) {
        double bwHz = val * 1000.0;
        m_spectrumWidget->setFrequencyRange(m_spectrumWidget->centerFrequency(), bwHz);
        emit m_spectrumWidget->bandwidthChangeRequested(bwHz);
    });

    m_mainSplitter->addWidget(spectrumPane);

    // Right side: Container #0 (added by ContainerManager)
    // ContainerManager adds Container #0 to the splitter in createDefaultContainers()

    setCentralWidget(m_mainSplitter);

    // --- Container Infrastructure ---
    m_containerManager = new ContainerManager(spectrumPane, m_mainSplitter, this);
    m_containerManager->restoreState();
    if (m_containerManager->containerCount() == 0) {
        createDefaultContainers();
    }
    m_containerManager->restoreSplitterState();

    // If no saved splitter sizes, set default: ~80% spectrum, ~20% panel
    if (!AppSettings::instance().contains(QStringLiteral("MainSplitterSizes"))) {
        m_mainSplitter->setSizes({1024, 256});
    }

    // Wire connection state changes to status bar
    connect(m_radioModel, &RadioModel::connectionStateChanged,
            this, &MainWindow::onConnectionStateChanged);

    // ... (rest of existing buildUI content follows — slice wiring, FFT engine, etc.)
```

Note: The existing slice wiring code (`connect(m_radioModel, &RadioModel::sliceAdded, ...)`) and FFT engine creation remain unchanged — just move them after the splitter setup.

- [ ] **Step 3: Add createDefaultContainers()**

```cpp
void MainWindow::createDefaultContainers()
{
    // Container #0: panel-docked right side (AetherSDR pattern).
    // Placeholder content in 3G-1, replaced by AppletPanel in 3G-AP.
    ContainerWidget* c0 = m_containerManager->createContainer(1, DockMode::PanelDocked);
    c0->setNotes(QStringLiteral("Main Panel"));
    c0->setNoControls(false);

    qCDebug(lcContainer) << "Created default Container #0 (panel-docked):" << c0->id();
}
```

- [ ] **Step 4: Add resizeEvent**

```cpp
void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);

    // Update axis-lock positions for overlay-docked containers
    QWidget* central = centralWidget();
    if (central && m_containerManager) {
        m_hDelta = central->width();
        m_vDelta = central->height();
        m_containerManager->updateDockedPositions(m_hDelta, m_vDelta);
    }
}
```

- [ ] **Step 5: Save state on close**

In `closeEvent()`, before `AppSettings::instance().save()`, add:

```cpp
    if (m_containerManager) {
        m_containerManager->saveState();
    }
```

- [ ] **Step 6: Build and verify**

```bash
cd /c/Users/boyds/NereusSDR && cmake --build build -j$(nproc) 2>&1 | tail -20
```

- [ ] **Step 7: Commit**

```bash
git add src/gui/MainWindow.h src/gui/MainWindow.cpp
git commit -m "feat(3G-1): wire QSplitter + ContainerManager into MainWindow — Container #0 as right panel"
```

---

## Task 8: Final Build Verification and Manual Testing

**Files:**
- All container files (verify includes)
- Full clean build + runtime test

- [ ] **Step 1: Clean build**

```bash
cd /c/Users/boyds/NereusSDR
rm -rf build
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo 2>&1 | tail -10
cmake --build build -j$(nproc) 2>&1 | tail -30
```

Expected: Clean build, no errors, no warnings from container code.

- [ ] **Step 2: Run the application**

```bash
cd /c/Users/boyds/NereusSDR && ./build/NereusSDR
```

Expected: Application launches. QSplitter divides the window — spectrum on left, Container #0 on right (~256px). Container shows "Container" placeholder text. Splitter handle is draggable to resize.

- [ ] **Step 3: Manual verification checklist**

1. **QSplitter resize**: Drag the splitter handle — spectrum and panel resize proportionally
2. **Hover title bar**: Move mouse to top of Container #0 — title bar appears with "RX1 Main Panel"
3. **Float**: Click ↗ button — Container #0 pops out, spectrum fills the window
4. **Dock**: Click ↙ on floating container — returns to QSplitter right side
5. **Pin on top**: Float container, click pin — stays on top of other windows
6. **Persist splitter**: Resize splitter, close app, reopen — splitter width restored
7. **Persist container**: Close and reopen — Container #0 restored as panel-docked
8. **Float + persist**: Float Container #0, close app, reopen — container restored as floating at saved position

- [ ] **Step 4: Commit any fixes**

```bash
git add -A
git commit -m "fix(3G-1): address build or runtime issues from integration testing"
```

---

## Summary

| Task | Deliverable | Source |
|---|---|---|
| 1 | Logging + CMake skeleton + stubs with DockMode enum | — |
| 2 | ContainerWidget UI layout, title bar, content slot (`setContent()`) | ucMeter.cs:108-643 + AetherSDR content pattern |
| 3 | ContainerWidget properties, drag, resize, hover — dock-mode-aware | ucMeter.cs:257-1229 |
| 4 | ContainerWidget serialize/deserialize with DockMode field | ucMeter.cs:1012-1160 |
| 5 | FloatingContainer full implementation | frmMeterDisplay.cs (all) |
| 6 | ContainerManager — 3 dock modes, axis-lock, QSplitter, persistence | MeterManager.cs:5613-6447 + AetherSDR QSplitter |
| 7 | MainWindow QSplitter integration + Container #0 panel-docked | AetherSDR MainWindow pattern |
| 8 | Full build + manual verification | — |
