# Phase 3G-4: Advanced Meter Items Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add 12 new passive MeterItem types achieving 1:1 parity with Thetis MeterManager display items, plus ANANMM/CrossNeedle composite presets and all remaining bar meter presets.

**Architecture:** Each new item type gets its own .h/.cpp file pair under `src/gui/meters/`, following the established MeterItem pattern (normalized 0-1 positioning, `paint()` via QPainter, `serialize()`/`deserialize()` with pipe-delimited format, `renderLayer()` declaration). Items are composed into presets via `ItemGroup` factory methods. The `ItemGroup::deserialize()` registry is extended to recognize all new type tags. New `MeterBinding` constants added for TX and hardware readings.

**Tech Stack:** C++20, Qt6 (QPainter for all rendering), existing MeterWidget 3-pipeline GPU renderer (Background texture, Geometry vertices, Overlay QPainter).

**Source Authority:**
- Item behavior, properties, calibration: Thetis `MeterManager.cs` (line references in design spec)
- Qt6 structure: existing MeterItem/MeterWidget/ItemGroup patterns in `src/gui/meters/`
- Colors: STYLEGUIDE.md and design spec `docs/architecture/phase3g4-g6-advanced-meters-design.md`

**Design Spec:** `docs/architecture/phase3g4-g6-advanced-meters-design.md` (Phase 3G-4 section)

---

## File Map

| File | Action | Responsibility |
|------|--------|----------------|
| `src/gui/meters/SpacerItem.h/.cpp` | Create | Trivial spacer rectangle |
| `src/gui/meters/FadeCoverItem.h/.cpp` | Create | Semi-transparent RX/TX fade overlay |
| `src/gui/meters/LEDItem.h/.cpp` | Create | LED indicator (3 shapes, 2 styles, blink/pulse) |
| `src/gui/meters/HistoryGraphItem.h/.cpp` | Create | Scrolling time-series with ring buffer |
| `src/gui/meters/MagicEyeItem.h/.cpp` | Create | Vacuum tube magic eye meter |
| `src/gui/meters/NeedleScalePwrItem.h/.cpp` | Create | Power scale labels for needle arcs |
| `src/gui/meters/SignalTextItem.h/.cpp` | Create | S-units/dBm/uV text display |
| `src/gui/meters/DialItem.h/.cpp` | Create | Circular dial with quadrant buttons |
| `src/gui/meters/TextOverlayItem.h/.cpp` | Create | Variable substitution text display |
| `src/gui/meters/WebImageItem.h/.cpp` | Create | Async web-fetched image |
| `src/gui/meters/FilterDisplayItem.h/.cpp` | Create | Mini passband spectrum/waterfall |
| `src/gui/meters/RotatorItem.h/.cpp` | Create | Antenna rotator compass dial |
| `src/gui/meters/MeterPoller.h` | Modify | Add new MeterBinding IDs (7-8, 106-112, 200-202, 300-301) |
| `src/gui/meters/ItemGroup.h/.cpp` | Modify | Add all new preset factories + deserialize registry |
| `CMakeLists.txt` | Modify | Add 12 new .cpp source files |

---

### Task 1: MeterBinding ID Extensions + CMakeLists Prep

**Files:**
- Modify: `src/gui/meters/MeterPoller.h:15-33`
- Modify: `CMakeLists.txt:198-201`

- [ ] **Step 1: Add new MeterBinding constants**

In `src/gui/meters/MeterPoller.h`, add the new binding IDs after the existing TX bindings (line 33):

```cpp
namespace MeterBinding {
    // RX meters (0-49) — existing
    constexpr int SignalPeak   = 0;
    constexpr int SignalAvg    = 1;
    constexpr int AdcPeak      = 2;
    constexpr int AdcAvg       = 3;
    constexpr int AgcGain      = 4;
    constexpr int AgcPeak      = 5;
    constexpr int AgcAvg       = 6;

    // RX meters — new
    constexpr int SignalMaxBin = 7;    // Spectral peak bin
    constexpr int PbSnr        = 8;    // Peak-to-baseline SNR

    // TX meters (100+) — existing
    constexpr int TxPower        = 100;
    constexpr int TxReversePower = 101;
    constexpr int TxSwr          = 102;
    constexpr int TxMic          = 103;
    constexpr int TxComp         = 104;
    constexpr int TxAlc          = 105;

    // TX meters — new
    constexpr int TxEq           = 106;  // From Thetis MeterManager.cs EQ reading
    constexpr int TxLeveler      = 107;  // TXA_LEVELER_AV
    constexpr int TxLevelerGain  = 108;  // TXA_LEVELER_GAIN
    constexpr int TxAlcGain      = 109;  // TXA_ALC_GAIN
    constexpr int TxAlcGroup     = 110;  // TXA_ALC_GROUP
    constexpr int TxCfc          = 111;  // TXA_CFC_AV
    constexpr int TxCfcGain      = 112;  // TXA_CFC_GAIN

    // Hardware readings (200+)
    constexpr int HwVolts        = 200;  // PA supply voltage
    constexpr int HwAmps         = 201;  // PA supply current
    constexpr int HwTemperature  = 202;  // PA temperature

    // Rotator readings (300+)
    constexpr int RotatorAz      = 300;  // Azimuth (0-360)
    constexpr int RotatorEle     = 301;  // Elevation (0-90)
}
```

- [ ] **Step 2: Add placeholder entries in CMakeLists.txt**

Add all 12 new source files after the existing meters block (line 201):

```cmake
    src/gui/meters/MeterPoller.cpp
    src/gui/meters/SpacerItem.cpp
    src/gui/meters/FadeCoverItem.cpp
    src/gui/meters/LEDItem.cpp
    src/gui/meters/HistoryGraphItem.cpp
    src/gui/meters/MagicEyeItem.cpp
    src/gui/meters/NeedleScalePwrItem.cpp
    src/gui/meters/SignalTextItem.cpp
    src/gui/meters/DialItem.cpp
    src/gui/meters/TextOverlayItem.cpp
    src/gui/meters/WebImageItem.cpp
    src/gui/meters/FilterDisplayItem.cpp
    src/gui/meters/RotatorItem.cpp
```

- [ ] **Step 3: Commit**

```bash
git add src/gui/meters/MeterPoller.h CMakeLists.txt
git commit -m "Add MeterBinding IDs for TX/HW/rotator meters and CMake entries for Phase 3G-4"
```

---

### Task 2: SpacerItem — Trivial Layout Spacer

**Files:**
- Create: `src/gui/meters/SpacerItem.h`
- Create: `src/gui/meters/SpacerItem.cpp`

Simplest possible item — establishes the pattern for all subsequent items.

- [ ] **Step 1: Create SpacerItem.h**

```cpp
#pragma once

#include "MeterItem.h"
#include <QColor>

namespace NereusSDR {

// From Thetis clsSpacerItem (MeterManager.cs:16116+)
// Fixed vertical spacing element with optional gradient fill.
class SpacerItem : public MeterItem {
    Q_OBJECT

public:
    explicit SpacerItem(QObject* parent = nullptr) : MeterItem(parent) {}

    void setPadding(float p) { m_padding = p; }
    float padding() const { return m_padding; }

    void setColour1(const QColor& c) { m_colour1 = c; }
    QColor colour1() const { return m_colour1; }

    void setColour2(const QColor& c) { m_colour2 = c; }
    QColor colour2() const { return m_colour2; }

    Layer renderLayer() const override { return Layer::Background; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    float  m_padding{0.1f};
    QColor m_colour1{0x20, 0x20, 0x20};
    QColor m_colour2{0x20, 0x20, 0x20};
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create SpacerItem.cpp**

```cpp
#include "SpacerItem.h"

#include <QPainter>
#include <QLinearGradient>
#include <QStringList>

namespace NereusSDR {

void SpacerItem::paint(QPainter& p, int widgetW, int widgetH)
{
    const QRect rect = pixelRect(widgetW, widgetH);
    if (m_colour1 == m_colour2) {
        p.fillRect(rect, m_colour1);
    } else {
        QLinearGradient grad(rect.topLeft(), rect.bottomLeft());
        grad.setColorAt(0.0, m_colour1);
        grad.setColorAt(1.0, m_colour2);
        p.fillRect(rect, grad);
    }
}

QString SpacerItem::serialize() const
{
    // Format: SPACER|x|y|w|h|bindingId|zOrder|padding|colour1|colour2
    return QStringLiteral("SPACER|%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(static_cast<double>(m_x))
        .arg(static_cast<double>(m_y))
        .arg(static_cast<double>(m_w))
        .arg(static_cast<double>(m_h))
        .arg(m_bindingId)
        .arg(m_zOrder)
        .arg(static_cast<double>(m_padding))
        .arg(m_colour1.name(QColor::HexArgb))
        .arg(m_colour2.name(QColor::HexArgb));
}

bool SpacerItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 9 || parts[0] != QLatin1String("SPACER")) {
        return false;
    }

    bool ok = true;
    m_x = parts[1].toFloat(&ok); if (!ok) { return false; }
    m_y = parts[2].toFloat(&ok); if (!ok) { return false; }
    m_w = parts[3].toFloat(&ok); if (!ok) { return false; }
    m_h = parts[4].toFloat(&ok); if (!ok) { return false; }
    m_bindingId = parts[5].toInt(&ok); if (!ok) { return false; }
    m_zOrder = parts[6].toInt(&ok); if (!ok) { return false; }
    m_padding = parts[7].toFloat(&ok); if (!ok) { return false; }
    m_colour1 = QColor(parts[8]); if (!m_colour1.isValid()) { return false; }
    m_colour2 = QColor(parts[9]); if (!m_colour2.isValid()) { return false; }

    return true;
}

} // namespace NereusSDR
```

- [ ] **Step 3: Build and verify**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

Expected: Clean compile. SpacerItem won't be visible yet (no preset using it).

- [ ] **Step 4: Commit**

```bash
git add src/gui/meters/SpacerItem.h src/gui/meters/SpacerItem.cpp
git commit -m "Add SpacerItem: trivial layout spacer (from Thetis clsSpacerItem)"
```

---

### Task 3: FadeCoverItem — RX/TX Transition Overlay

**Files:**
- Create: `src/gui/meters/FadeCoverItem.h`
- Create: `src/gui/meters/FadeCoverItem.cpp`

- [ ] **Step 1: Create FadeCoverItem.h**

```cpp
#pragma once

#include "MeterItem.h"
#include <QColor>

namespace NereusSDR {

// From Thetis clsFadeCover (MeterManager.cs:7665+)
// Semi-transparent overlay that fades items during RX/TX transitions.
class FadeCoverItem : public MeterItem {
    Q_OBJECT

public:
    explicit FadeCoverItem(QObject* parent = nullptr) : MeterItem(parent) {}

    void setColour1(const QColor& c) { m_colour1 = c; }
    QColor colour1() const { return m_colour1; }

    void setColour2(const QColor& c) { m_colour2 = c; }
    QColor colour2() const { return m_colour2; }

    void setAlpha(float a) { m_alpha = a; }
    float alpha() const { return m_alpha; }

    void setFadeOnRx(bool f) { m_fadeOnRx = f; }
    bool fadeOnRx() const { return m_fadeOnRx; }

    void setFadeOnTx(bool f) { m_fadeOnTx = f; }
    bool fadeOnTx() const { return m_fadeOnTx; }

    // Called by container when TX/RX state changes
    void setTxState(bool isTx);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    QColor m_colour1{0x0f, 0x0f, 0x1a, 0xc0}; // RX fade color
    QColor m_colour2{0x0f, 0x0f, 0x1a, 0xc0}; // TX fade color
    float  m_alpha{0.75f};
    bool   m_fadeOnRx{false};
    bool   m_fadeOnTx{true};
    bool   m_isTx{false};
    bool   m_active{false};
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create FadeCoverItem.cpp**

```cpp
#include "FadeCoverItem.h"

#include <QPainter>
#include <QStringList>

namespace NereusSDR {

void FadeCoverItem::setTxState(bool isTx)
{
    m_isTx = isTx;
    m_active = (isTx && m_fadeOnTx) || (!isTx && m_fadeOnRx);
}

void FadeCoverItem::paint(QPainter& p, int widgetW, int widgetH)
{
    if (!m_active) {
        return;
    }

    const QRect rect = pixelRect(widgetW, widgetH);
    QColor fillColor = m_isTx ? m_colour2 : m_colour1;
    fillColor.setAlphaF(m_alpha);
    p.fillRect(rect, fillColor);
}

QString FadeCoverItem::serialize() const
{
    return QStringLiteral("FADECOVER|%1|%2|%3|%4|%5|%6|%7|%8|%9|%10")
        .arg(static_cast<double>(m_x))
        .arg(static_cast<double>(m_y))
        .arg(static_cast<double>(m_w))
        .arg(static_cast<double>(m_h))
        .arg(m_bindingId)
        .arg(m_zOrder)
        .arg(m_colour1.name(QColor::HexArgb))
        .arg(m_colour2.name(QColor::HexArgb))
        .arg(static_cast<double>(m_alpha))
        .arg(static_cast<int>(m_fadeOnRx) * 2 + static_cast<int>(m_fadeOnTx));
}

bool FadeCoverItem::deserialize(const QString& data)
{
    const QStringList parts = data.split(QLatin1Char('|'));
    if (parts.size() < 10 || parts[0] != QLatin1String("FADECOVER")) {
        return false;
    }

    bool ok = true;
    m_x = parts[1].toFloat(&ok); if (!ok) { return false; }
    m_y = parts[2].toFloat(&ok); if (!ok) { return false; }
    m_w = parts[3].toFloat(&ok); if (!ok) { return false; }
    m_h = parts[4].toFloat(&ok); if (!ok) { return false; }
    m_bindingId = parts[5].toInt(&ok); if (!ok) { return false; }
    m_zOrder = parts[6].toInt(&ok); if (!ok) { return false; }
    m_colour1 = QColor(parts[7]); if (!m_colour1.isValid()) { return false; }
    m_colour2 = QColor(parts[8]); if (!m_colour2.isValid()) { return false; }
    m_alpha = parts[9].toFloat(&ok); if (!ok) { return false; }
    const int flags = parts[10].toInt(&ok); if (!ok) { return false; }
    m_fadeOnRx = (flags & 2) != 0;
    m_fadeOnTx = (flags & 1) != 0;

    return true;
}

} // namespace NereusSDR
```

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/FadeCoverItem.h src/gui/meters/FadeCoverItem.cpp
git commit -m "Add FadeCoverItem: RX/TX transition overlay (from Thetis clsFadeCover)"
```

---

### Task 4: LEDItem — Colored LED Indicator

**Files:**
- Create: `src/gui/meters/LEDItem.h`
- Create: `src/gui/meters/LEDItem.cpp`

- [ ] **Step 1: Create LEDItem.h**

```cpp
#pragma once

#include "MeterItem.h"
#include <QColor>
#include <QTimer>

namespace NereusSDR {

// From Thetis clsLed (MeterManager.cs:19448+)
// LED indicator with 3 shapes, 2 styles, blink/pulsate animations.
class LEDItem : public MeterItem {
    Q_OBJECT

public:
    enum class LedShape { Square, Round, Triangle };
    enum class LedStyle { Flat, ThreeD };

    explicit LEDItem(QObject* parent = nullptr);

    // Shape and style
    void setLedShape(LedShape s) { m_shape = s; }
    LedShape ledShape() const { return m_shape; }

    void setLedStyle(LedStyle s) { m_style = s; }
    LedStyle ledStyle() const { return m_style; }

    // Colors
    void setTrueColour(const QColor& c) { m_trueColour = c; }
    QColor trueColour() const { return m_trueColour; }

    void setFalseColour(const QColor& c) { m_falseColour = c; }
    QColor falseColour() const { return m_falseColour; }

    void setPanelBackColour1(const QColor& c) { m_panelBack1 = c; }
    QColor panelBackColour1() const { return m_panelBack1; }

    void setPanelBackColour2(const QColor& c) { m_panelBack2 = c; }
    QColor panelBackColour2() const { return m_panelBack2; }

    // State
    void setShowBackPanel(bool show) { m_showBackPanel = show; }
    bool showBackPanel() const { return m_showBackPanel; }

    void setBlink(bool b) { m_blink = b; }
    bool blink() const { return m_blink; }

    void setPulsate(bool p) { m_pulsate = p; }
    bool pulsate() const { return m_pulsate; }

    void setPadding(float p) { m_padding = p; }
    float padding() const { return m_padding; }

    // Threshold-based mode (alternative to boolean binding)
    void setGreenThreshold(double t) { m_greenThreshold = t; }
    void setAmberThreshold(double t) { m_amberThreshold = t; }
    void setRedThreshold(double t) { m_redThreshold = t; }

    void setValue(double v) override;

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    QColor currentColor() const;

    LedShape m_shape{LedShape::Round};
    LedStyle m_style{LedStyle::Flat};
    QColor   m_trueColour{0x00, 0xff, 0x88};
    QColor   m_falseColour{0x40, 0x40, 0x40};
    QColor   m_panelBack1{0x20, 0x20, 0x20};
    QColor   m_panelBack2{0x20, 0x20, 0x20};
    bool     m_showBackPanel{false};
    bool     m_blink{false};
    bool     m_pulsate{false};
    float    m_padding{2.0f};
    bool     m_ledOn{false};
    int      m_blinkCounter{0};

    // Threshold-based state (from Thetis LED conditional evaluation)
    double m_greenThreshold{0.0};
    double m_amberThreshold{1000.0}; // disabled by default
    double m_redThreshold{1000.0};   // disabled by default
    QColor m_amberColour{0xff, 0xb8, 0x00};
    QColor m_redColour{0xff, 0x44, 0x44};
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create LEDItem.cpp**

Implement `setValue()` with threshold logic, `paint()` with all 3 shapes and 2 styles,
`serialize()`/`deserialize()`. Key rendering:

- **Round/Flat:** `p.drawEllipse()` filled with current color + 1px darker border
- **Round/ThreeD:** Radial gradient from lighter center to edge color
- **Square:** `p.fillRect()` with border
- **Triangle:** `QPainterPath` with 3 points
- **Blink:** Toggle `m_ledOn` every 5 paint calls (500ms at 100ms update)
- **Pulsate:** Cycle alpha from 0.3→1.0→0.3 over 20 frames

Read the Thetis `clsLed` (MeterManager.cs:19448-19650) for exact blink/pulsate timing and color fade logic. Port faithfully.

Serialization format: `LED|x|y|w|h|bindingId|zOrder|shape|style|trueColour|falseColour|panelBack1|panelBack2|showBackPanel|blink|pulsate|padding|greenThresh|amberThresh|redThresh`

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/LEDItem.h src/gui/meters/LEDItem.cpp
git commit -m "Add LEDItem: LED indicator with shapes, styles, blink/pulse (from Thetis clsLed)"
```

---

### Task 5: HistoryGraphItem — Scrolling Time-Series Graph

**Files:**
- Create: `src/gui/meters/HistoryGraphItem.h`
- Create: `src/gui/meters/HistoryGraphItem.cpp`

- [ ] **Step 1: Create HistoryGraphItem.h**

Key design:
- Fixed-size ring buffer using `std::vector<float>` + write index + count
- Dual-axis support: two independent ring buffers with separate colors/scaling
- Multi-layer: `OverlayStatic` for grid+labels, `OverlayDynamic` for line graph
- Auto-scaling Y-axis from running min/max per axis

```cpp
#pragma once

#include "MeterItem.h"
#include <QColor>
#include <vector>

namespace NereusSDR {

// From Thetis clsHistoryItem (MeterManager.cs:16149+)
// Scrolling time-series line graph with dual-axis ring buffer.
class HistoryGraphItem : public MeterItem {
    Q_OBJECT

public:
    explicit HistoryGraphItem(QObject* parent = nullptr);

    void setCapacity(int cap);
    int capacity() const { return m_capacity; }

    void setLineColor0(const QColor& c) { m_lineColor0 = c; }
    QColor lineColor0() const { return m_lineColor0; }

    void setLineColor1(const QColor& c) { m_lineColor1 = c; }
    QColor lineColor1() const { return m_lineColor1; }

    void setShowGrid(bool show) { m_showGrid = show; }
    bool showGrid() const { return m_showGrid; }

    void setAutoScale0(bool a) { m_autoScale0 = a; }
    void setAutoScale1(bool a) { m_autoScale1 = a; }
    void setShowScale0(bool s) { m_showScale0 = s; }
    void setShowScale1(bool s) { m_showScale1 = s; }

    // Secondary axis binding (axis 0 uses base bindingId)
    void setBindingId1(int id) { m_bindingId1 = id; }
    int bindingId1() const { return m_bindingId1; }
    void setValue1(double v);

    void setValue(double v) override;

    // Multi-layer
    bool participatesIn(Layer layer) const override;
    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paintForLayer(QPainter& p, int widgetW, int widgetH, Layer layer) override;
    void paint(QPainter& p, int widgetW, int widgetH) override;

    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    struct RingBuffer {
        std::vector<float> data;
        int writeIdx{0};
        int count{0};
        float runMin{1e30f};
        float runMax{-1e30f};

        void resize(int cap);
        void push(float val);
        float at(int i) const; // oldest = 0, newest = count-1
    };

    void paintGrid(QPainter& p, const QRect& rect);
    void paintLine(QPainter& p, const QRect& rect, const RingBuffer& buf,
                   const QColor& color, float yMin, float yMax);

    int    m_capacity{300};
    QColor m_lineColor0{0x00, 0xb4, 0xd8};
    QColor m_lineColor1{0xff, 0xb8, 0x00};
    bool   m_showGrid{true};
    bool   m_autoScale0{true};
    bool   m_autoScale1{true};
    bool   m_showScale0{true};
    bool   m_showScale1{false};
    int    m_bindingId1{-1};

    RingBuffer m_buf0;
    RingBuffer m_buf1;
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create HistoryGraphItem.cpp**

Implement ring buffer (O(1) push, index-based read), grid painting (horizontal lines at auto-scaled dB intervals + vertical time lines at 5s boundaries), and line graph rendering (QPainter `drawLine` connecting consecutive ring buffer points).

Key implementation details:
- `RingBuffer::push()`: `data[writeIdx] = val; writeIdx = (writeIdx + 1) % data.size(); count = min(count+1, size);`
- `RingBuffer::at(i)`: `data[(writeIdx - count + i + size) % size]`
- `paintLine()`: iterate `buf.count` points, map X to `rect.left() + i * rect.width() / (count-1)`, map Y to `rect.bottom() - (val - yMin) / (yMax - yMin) * rect.height()`
- Auto-scale: use `runMin`/`runMax` with 5% padding
- Grid: 5 horizontal lines, labeled with dB values, color `#203040`

Read Thetis `clsHistoryItem` (MeterManager.cs:34811-35008) for the exact rendering approach.

Serialization format: `HISTORY|x|y|w|h|bindingId|zOrder|capacity|lineColor0|lineColor1|showGrid|autoScale0|autoScale1|showScale0|showScale1|bindingId1`

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/HistoryGraphItem.h src/gui/meters/HistoryGraphItem.cpp
git commit -m "Add HistoryGraphItem: scrolling time-series graph (from Thetis clsHistoryItem)"
```

---

### Task 6: MagicEyeItem — Vacuum Tube Magic Eye

**Files:**
- Create: `src/gui/meters/MagicEyeItem.h`
- Create: `src/gui/meters/MagicEyeItem.cpp`

- [ ] **Step 1: Create MagicEyeItem.h**

Key design:
- `OverlayDynamic` layer — repaints every update
- Green phosphor arc using radial gradient + QPainter arc clipping
- Shadow wedge angle maps from signal: S0 (full open, ~120deg) to S9+60 (closed, ~5deg)
- Reuse NeedleItem's `kS0Dbm`/`kS9Dbm`/`kMaxDbm`/`kSmoothAlpha` constants
- Optional bezel image overlay (file-based, user-replaceable)

```cpp
#pragma once

#include "MeterItem.h"
#include <QColor>
#include <QImage>

namespace NereusSDR {

// From Thetis clsMagicEyeItem (MeterManager.cs:15855+)
// Vacuum tube magic eye meter — green phosphor arc opens/closes with signal.
class MagicEyeItem : public MeterItem {
    Q_OBJECT

public:
    explicit MagicEyeItem(QObject* parent = nullptr) : MeterItem(parent) {}

    // From NeedleItem S-meter constants
    static constexpr float kS0Dbm  = -127.0f;
    static constexpr float kMaxDbm = -13.0f;
    static constexpr float kSmoothAlpha = 0.3f;

    // Shadow wedge range (degrees) — full open at S0, nearly closed at S9+60
    static constexpr float kMaxShadowDeg = 120.0f; // no signal
    static constexpr float kMinShadowDeg = 5.0f;   // full signal

    void setGlowColor(const QColor& c) { m_glowColor = c; }
    QColor glowColor() const { return m_glowColor; }

    void setBezelImagePath(const QString& path);
    QString bezelImagePath() const { return m_bezelPath; }

    void setValue(double v) override;

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    void paint(QPainter& p, int widgetW, int widgetH) override;
    QString serialize() const override;
    bool deserialize(const QString& data) override;

private:
    float dbmToFraction(float dbm) const;

    QColor  m_glowColor{0x00, 0xff, 0x88};
    QString m_bezelPath;
    QImage  m_bezelImage;
    float   m_smoothedDbm{kS0Dbm};
};

} // namespace NereusSDR
```

- [ ] **Step 2: Create MagicEyeItem.cpp**

Rendering approach:
1. Fill circular area with dark background (`#0a0a0a`)
2. Draw green phosphor using radial gradient (`glowColor` center → dark edge)
3. Draw shadow wedge as a dark pie slice from center — angle based on `dbmToFraction()`
4. `dbmToFraction()`: same as NeedleItem — `clamp((dbm - kS0Dbm) / (kMaxDbm - kS0Dbm), 0, 1)`
5. Shadow angle: `kMaxShadowDeg - fraction * (kMaxShadowDeg - kMinShadowDeg)`
6. If bezel image loaded, draw it on top
7. Smoothing: `m_smoothedDbm += kSmoothAlpha * (dbm - m_smoothedDbm)`

Read Thetis `clsMagicEyeItem` (MeterManager.cs:15855+) for the exact rendering. The shadow is drawn as two symmetric wedges from the top center, creating the classic "eye closing" effect.

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/MagicEyeItem.h src/gui/meters/MagicEyeItem.cpp
git commit -m "Add MagicEyeItem: vacuum tube magic eye meter (from Thetis clsMagicEyeItem)"
```

---

### Task 7: NeedleScalePwrItem — Power Scale Labels

**Files:**
- Create: `src/gui/meters/NeedleScalePwrItem.h`
- Create: `src/gui/meters/NeedleScalePwrItem.cpp`

- [ ] **Step 1: Create header and implementation**

This item renders text labels at scale calibration points around needle arcs. It's a companion to NeedleItem used by ANANMM and CrossNeedle presets.

Key design:
- `OverlayStatic` layer (labels don't change per-frame)
- `scaleCalibration` map: `QMap<float, QPointF>` mapping value → normalized (x,y)
- Selects subset of calibration points to label based on `marks` count
- Font scales with widget size: `fontSizeEm = (fontSize / 16.0f) * (rect.width() / 52.0f)`
- Labels colored from `lowColour` to `highColour` based on value position
- Unit auto-selection: mW when `maxPower <= 1.0`, else W
- `tidyPower()` formatting: decimal for <8W, integer for >=8W

Read Thetis `clsNeedleScalePwrItem` (MeterManager.cs:14888-15040) and `renderNeedleScale()` (MeterManager.cs:31645-31850) for the exact positioning, font scaling, and label selection algorithm.

Serialization format: `NEEDLESCALEPWR|x|y|w|h|bindingId|zOrder|lowColour|highColour|fontFamily|fontSize|marks|maxPower|darkMode|calibrationCount|v1:x1:y1|v2:x2:y2|...`

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/NeedleScalePwrItem.h src/gui/meters/NeedleScalePwrItem.cpp
git commit -m "Add NeedleScalePwrItem: power scale labels for needle arcs (from Thetis clsNeedleScalePwrItem)"
```

---

### Task 8: SignalTextItem — S-Units/dBm/uV Text Display

**Files:**
- Create: `src/gui/meters/SignalTextItem.h`
- Create: `src/gui/meters/SignalTextItem.cpp`

- [ ] **Step 1: Create header and implementation**

Key design:
- Three display units: DBM, S_UNITS, UV (enum)
- `OverlayDynamic` layer — large text, updates every frame
- Format switching: `formatDbm()`, `formatSUnits()`, `formatUv()` methods
- S-unit formatting: S0-S9 for -127 to -73 dBm (6dB/S-unit), S9+10 to S9+60 above
- uV conversion: `10^((dBm + 107) / 20)` (from Thetis Common.UVfromDBM)
- Peak hold: separate smoothed peak value with decay
- Attack/decay smoothing (0.8/0.2 from Thetis)
- Bar style rendering beneath text (None/Line/SolidFilled/GradientFilled/Segments)

Read Thetis `clsSignalText` (MeterManager.cs:20286-20540) for exact formatting and bar styles.

Serialization format: `SIGNALTEXT|x|y|w|h|bindingId|zOrder|units|showValue|showPeakValue|showType|peakHold|colour|peakValueColour|fontFamily|fontSize|barStyle|historyDuration`

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/SignalTextItem.h src/gui/meters/SignalTextItem.cpp
git commit -m "Add SignalTextItem: S-units/dBm/uV text display (from Thetis clsSignalText)"
```

---

### Task 9: DialItem — Circular Dial with Quadrant Buttons

**Files:**
- Create: `src/gui/meters/DialItem.h`
- Create: `src/gui/meters/DialItem.cpp`

- [ ] **Step 1: Create header and implementation**

Key design from Thetis `clsDialDisplay` (MeterManager.cs:15399+):
- Main circle: `QPainter::drawEllipse()` filled with `circleColour`
- Ring: dashed stroke around circle with `ringColour`
- Four quadrant zones: VFOA (top-left), VFOB (top-right), ACCEL (bottom-left), LOCK (bottom-right)
- Quadrant detection via angle from center
- Speed indicator: Slow/Hold/Fast colors for tuning acceleration state
- Multi-layer: `OverlayStatic` for circle/ring, `OverlayDynamic` for quadrant highlights
- Mouse interaction via quadrant hit testing (deferred to Phase 3G-5 mouse forwarding)

Read Thetis rendering at `renderDialDisplay()` (MeterManager.cs:33750-33899) for exact geometry.

Serialization format: `DIAL|x|y|w|h|bindingId|zOrder|textColour|circleColour|padColour|ringColour|btnOnColour|btnOffColour|btnHighlightColour|slowColour|holdColour|fastColour`

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/DialItem.h src/gui/meters/DialItem.cpp
git commit -m "Add DialItem: circular dial meter (from Thetis clsDialDisplay)"
```

---

### Task 10: TextOverlayItem — Variable Substitution Text

**Files:**
- Create: `src/gui/meters/TextOverlayItem.h`
- Create: `src/gui/meters/TextOverlayItem.cpp`

- [ ] **Step 1: Create header and implementation**

Key design from Thetis `clsTextOverlay` (MeterManager.cs:18746+):
- Two independent text lines with separate styling
- `%VARIABLE_NAME%` placeholder parsing at construction time → stored as token lists
- On each update, `parseText()` replaces tokens with current values
- Variable types: Reading enum values, `%PRECIS=n%`, `%NL%`, custom strings
- Scrolling text: `scrollX` offset incremented per frame, wraps at text width
- Per-line font, color, background color, offset positioning
- Panel background with `panelBackColour1`/`panelBackColour2`

The variable parser splits template text on `%` delimiters, checks each token against:
1. Reading enum names (SIGNAL_STRENGTH, AVG_SIGNAL_STRENGTH, PWR, SWR, etc.)
2. `PRECIS=n` precision control
3. `NL` newline
4. Literal text (passed through)

Resolved text is rendered with QPainter in `OverlayDynamic` layer.

Read Thetis `parseText()` (MeterManager.cs:19267-19395) for the exact parser logic.

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/TextOverlayItem.h src/gui/meters/TextOverlayItem.cpp
git commit -m "Add TextOverlayItem: variable substitution text display (from Thetis clsTextOverlay)"
```

---

### Task 11: WebImageItem — Async Web-Fetched Image

**Files:**
- Create: `src/gui/meters/WebImageItem.h`
- Create: `src/gui/meters/WebImageItem.cpp`

- [ ] **Step 1: Create header and implementation**

Key design from Thetis `clsWebImage` (MeterManager.cs:14165+):
- Uses `QNetworkAccessManager` for async HTTP GET
- `QTimer` for periodic refresh (default 300s)
- Downloaded image cached as `QImage`, rendered in `Background` layer
- Fallback solid color on fetch failure
- URL validation before fetch

```cpp
// WebImageItem.h key members:
QNetworkAccessManager* m_nam{nullptr};
QTimer* m_refreshTimer{nullptr};
QImage m_image;
QString m_url;
int m_refreshInterval{300};
QColor m_fallbackColor{0x20, 0x20, 0x20};
```

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/WebImageItem.h src/gui/meters/WebImageItem.cpp
git commit -m "Add WebImageItem: async web-fetched image display (from Thetis clsWebImage)"
```

---

### Task 12: RotatorItem — Antenna Rotator Compass Dial

**Files:**
- Create: `src/gui/meters/RotatorItem.h`
- Create: `src/gui/meters/RotatorItem.cpp`

- [ ] **Step 1: Create header and implementation**

Key design from Thetis `clsRotatorItem` (MeterManager.cs:15042+):
- Three modes: AZ (0-360 compass), ELE (0-90 arc), BOTH (dual)
- Multi-layer: `OverlayStatic` for compass face (ticks, cardinals, degree scale),
  `OverlayDynamic` for heading dot + arrow + beam width overlay
- Compass face: circle with tick marks every 10deg (major) and 2deg (minor)
- Cardinal labels: N/S/E/W at 0/180/90/270 degrees
- Heading indicator: colored dot at current azimuth + directional arrow
- Beam width: semi-transparent pie slice arc from heading
- Background image support (file-based, user-replaceable)
- Value smoothing: `value += sign(diff) * 0.2 * |diff|` (from Thetis line 15290)
- Wraps at 360 degrees, handles negative angles

Read Thetis rendering (MeterManager.cs:15187-15214 for bg images, 15290-15312 for smoothing).

Serialization: all properties from the design spec, plus `backgroundImagePath`.

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/RotatorItem.h src/gui/meters/RotatorItem.cpp
git commit -m "Add RotatorItem: antenna rotator compass dial (from Thetis clsRotatorItem)"
```

---

### Task 13: FilterDisplayItem — Mini Passband/Spectrum Display

**Files:**
- Create: `src/gui/meters/FilterDisplayItem.h`
- Create: `src/gui/meters/FilterDisplayItem.cpp`

This is the most complex item — a mini SpectrumWidget with its own passband/waterfall rendering.

- [ ] **Step 1: Create header and implementation**

Key design from Thetis `clsFilterItem` (MeterManager.cs:16852+):
- Four display modes: Panadapter, Waterfall, Panafall (stacked), None
- Spectrum data: receives 512 float samples via dedicated `setSpectrumData()` method
- Spectrum rendering: QPainter line connecting all 512 points, optional fill
- Waterfall: rolling `QImage` buffer, shift down 1 row per frame, colorize top row
- Filter edge markers: vertical lines at RX low/high (yellow) and TX low/high (red)
- Notch overlays: orange vertical lines at manual notch frequencies
- Waterfall palette: enum with 7 options (Enhanced, Spectran, BlackWhite, etc.)
- This is `OverlayDynamic` layer only — repaints fully every frame

The FilterDisplayItem needs its own spectrum data feed. In the signal chain:
`FFTEngine → SpectrumWidget` (main display) and also `FFTEngine → FilterDisplayItem` (mini display).
For now, the item receives data via `setSpectrumData(const float* bins, int count)` called by
MeterPoller or a dedicated feed. The data routing is wired in Phase 3G-5 or later.

Read Thetis rendering logic in `clsFilterItem` for the exact spectrum/waterfall rendering,
color palette implementation, and edge/notch overlay positioning.

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/FilterDisplayItem.h src/gui/meters/FilterDisplayItem.cpp
git commit -m "Add FilterDisplayItem: mini passband spectrum/waterfall (from Thetis clsFilterItem)"
```

---

### Task 14: Deserialization Registry Update

**Files:**
- Modify: `src/gui/meters/ItemGroup.cpp:90-170` (deserialize method)
- Modify: `src/gui/meters/ItemGroup.h` (add new includes)

- [ ] **Step 1: Add includes for all new item types**

At the top of `ItemGroup.cpp`, after the existing includes:

```cpp
#include "SpacerItem.h"
#include "FadeCoverItem.h"
#include "LEDItem.h"
#include "HistoryGraphItem.h"
#include "MagicEyeItem.h"
#include "NeedleScalePwrItem.h"
#include "SignalTextItem.h"
#include "DialItem.h"
#include "TextOverlayItem.h"
#include "WebImageItem.h"
#include "FilterDisplayItem.h"
#include "RotatorItem.h"
```

- [ ] **Step 2: Extend the deserialize type tag switch**

In `ItemGroup::deserialize()`, after the existing `NEEDLE` case (around line 161), add cases for all new types. Each follows the same pattern:

```cpp
        } else if (typeTag == QLatin1String("SPACER")) {
            SpacerItem* spacer = new SpacerItem();
            if (spacer->deserialize(itemData)) {
                item = spacer;
            } else {
                delete spacer;
            }
        } else if (typeTag == QLatin1String("FADECOVER")) {
            FadeCoverItem* fade = new FadeCoverItem();
            if (fade->deserialize(itemData)) {
                item = fade;
            } else {
                delete fade;
            }
        } else if (typeTag == QLatin1String("LED")) {
            LEDItem* led = new LEDItem();
            if (led->deserialize(itemData)) {
                item = led;
            } else {
                delete led;
            }
        } else if (typeTag == QLatin1String("HISTORY")) {
            HistoryGraphItem* hist = new HistoryGraphItem();
            if (hist->deserialize(itemData)) {
                item = hist;
            } else {
                delete hist;
            }
        } else if (typeTag == QLatin1String("MAGICEYE")) {
            MagicEyeItem* eye = new MagicEyeItem();
            if (eye->deserialize(itemData)) {
                item = eye;
            } else {
                delete eye;
            }
        } else if (typeTag == QLatin1String("NEEDLESCALEPWR")) {
            NeedleScalePwrItem* nsp = new NeedleScalePwrItem();
            if (nsp->deserialize(itemData)) {
                item = nsp;
            } else {
                delete nsp;
            }
        } else if (typeTag == QLatin1String("SIGNALTEXT")) {
            SignalTextItem* st = new SignalTextItem();
            if (st->deserialize(itemData)) {
                item = st;
            } else {
                delete st;
            }
        } else if (typeTag == QLatin1String("DIAL")) {
            DialItem* dial = new DialItem();
            if (dial->deserialize(itemData)) {
                item = dial;
            } else {
                delete dial;
            }
        } else if (typeTag == QLatin1String("TEXTOVERLAY")) {
            TextOverlayItem* to = new TextOverlayItem();
            if (to->deserialize(itemData)) {
                item = to;
            } else {
                delete to;
            }
        } else if (typeTag == QLatin1String("WEBIMAGE")) {
            WebImageItem* wi = new WebImageItem();
            if (wi->deserialize(itemData)) {
                item = wi;
            } else {
                delete wi;
            }
        } else if (typeTag == QLatin1String("FILTERDISPLAY")) {
            FilterDisplayItem* fd = new FilterDisplayItem();
            if (fd->deserialize(itemData)) {
                item = fd;
            } else {
                delete fd;
            }
        } else if (typeTag == QLatin1String("ROTATOR")) {
            RotatorItem* rot = new RotatorItem();
            if (rot->deserialize(itemData)) {
                item = rot;
            } else {
                delete rot;
            }
        }
```

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/ItemGroup.cpp
git commit -m "Update ItemGroup deserialize registry with all Phase 3G-4 item types"
```

---

### Task 15: Additional Bar Meter Presets

**Files:**
- Modify: `src/gui/meters/ItemGroup.h` — add factory method declarations
- Modify: `src/gui/meters/ItemGroup.cpp` — add factory implementations

- [ ] **Step 1: Add bar preset factory declarations to ItemGroup.h**

After existing `createCompPreset()` declaration:

```cpp
    // Additional bar presets (from Thetis MeterManager.cs AddMeter factory)
    static ItemGroup* createSignalBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAvgSignalBarPreset(QObject* parent = nullptr);
    static ItemGroup* createMaxBinBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAdcBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAdcMaxMagPreset(QObject* parent = nullptr);
    static ItemGroup* createAgcBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAgcGainBarPreset(QObject* parent = nullptr);
    static ItemGroup* createPbsnrBarPreset(QObject* parent = nullptr);
    static ItemGroup* createEqBarPreset(QObject* parent = nullptr);
    static ItemGroup* createLevelerBarPreset(QObject* parent = nullptr);
    static ItemGroup* createLevelerGainBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAlcGainBarPreset(QObject* parent = nullptr);
    static ItemGroup* createAlcGroupBarPreset(QObject* parent = nullptr);
    static ItemGroup* createCfcBarPreset(QObject* parent = nullptr);
    static ItemGroup* createCfcGainBarPreset(QObject* parent = nullptr);
    static ItemGroup* createCustomBarPreset(int bindingId, double minVal, double maxVal,
                                             const QString& name, QObject* parent = nullptr);

    // Composite presets
    static ItemGroup* createMagicEyePreset(int bindingId, QObject* parent = nullptr);
    static ItemGroup* createSignalTextPreset(int bindingId, QObject* parent = nullptr);
    static ItemGroup* createHistoryPreset(int bindingId, QObject* parent = nullptr);
    static ItemGroup* createSpacerPreset(QObject* parent = nullptr);
```

- [ ] **Step 2: Implement bar presets**

All bar presets follow the same `createHBarPreset()` pattern with different binding IDs and ranges. Read Thetis `addSMeterBar()` (MeterManager.cs:21523-21616) for calibration.

```cpp
// Example: Signal bar uses instantaneous signal, S-meter range
ItemGroup* ItemGroup::createSignalBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::SignalPeak, -140.0, 0.0,
                            QStringLiteral("Signal"), parent);
}

ItemGroup* ItemGroup::createAvgSignalBarPreset(QObject* parent)
{
    return createHBarPreset(MeterBinding::SignalAvg, -140.0, 0.0,
                            QStringLiteral("Avg Signal"), parent);
}

// ... same pattern for all bar presets with correct bindingId and range
```

Ranges from Thetis MeterManager.cs:
- Signal/AvgSignal/MaxBin: -140 to 0 dBm
- ADC/AdcMaxMag: 0 to 32768
- AGC/AgcGain: -20 to 120 dB
- PBSNR: 0 to 60 dB
- EQ: -30 to 0 dB
- Leveler/LevelerGain: -30 to 0 dB / 0 to 30 dB
- AlcGain/AlcGroup: 0 to 30 dB / -30 to 25 dB
- CFC/CfcGain: -30 to 0 dB / 0 to 30 dB

Implement composite presets (MagicEye, SignalText, History, Spacer) using the new item types.

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/ItemGroup.h src/gui/meters/ItemGroup.cpp
git commit -m "Add all Phase 3G-4 bar meter presets and composite presets"
```

---

### Task 16: ANANMM Composite Preset

**Files:**
- Modify: `src/gui/meters/ItemGroup.h` — add `createAnanMMPreset()` declaration
- Modify: `src/gui/meters/ItemGroup.cpp` — add implementation

- [ ] **Step 1: Add ANANMM factory declaration**

```cpp
    // ANAN Multi-Meter: 7-needle composite with background images
    // From Thetis AddAnanMM() (MeterManager.cs:22461-22815)
    static ItemGroup* createAnanMMPreset(QObject* parent = nullptr);
```

- [ ] **Step 2: Implement createAnanMMPreset()**

This is the most complex preset — constructs 10+ items:

1. **Background image** (ImageItem, z=1): meter face loaded from `resources/meters/ananMM.png`
2. **RX background** (ImageItem, z=5): `ananMM-bg.png`, shown only on RX
3. **TX background** (ImageItem, z=5): `ananMM-bg-tx.png`, shown only on TX
4. **Signal needle** (NeedleItem): AVG_SIGNAL_STRENGTH, red (233,51,50), RX only
   - Scale calibration 16 points from Thetis lines 22491-22506 — copy all (x,y) pairs exactly
5. **Volts needle** (NeedleItem): HwVolts, black, all modes
   - 3-point calibration: 10V→(0.559,0.756), 12.5V→(0.605,0.772), 15V→(0.665,0.784)
6. **Amps needle** (NeedleItem): HwAmps, black, TX only, DisplayGroup 4
   - 11-point calibration: 0-20A from Thetis lines 22541-22575
7. **Power needle** (NeedleItem): TxPower, red, TX only, DisplayGroup 1
   - 10-point calibration: 0/5/10/25/30/40/50/60/100/150W
8. **Power scale labels** (NeedleScalePwrItem): 7 marks, Trebuchet MS Bold 22pt
9. **SWR needle** (NeedleItem): TxSwr, black, TX only, DisplayGroup 1
   - 6-point calibration: 1/1.5/2/2.5/3/10
10. **Compression needle** (NeedleItem): TxAlcGain, black, TX only, DisplayGroup 2
    - 7-point calibration: 0-30dB
11. **ALC needle** (NeedleItem): TxAlcGroup, black, TX only, DisplayGroup 3
    - 3-point calibration: -30/0/25dB

All needle offset (0.004, 0.736), RadiusRatio (1.0, 0.58) — exact from Thetis.
All attack/decay ratios and LengthFactors exact from Thetis.

**Note:** The existing NeedleItem needs extensions for this:
- `scaleCalibration` map (QMap<float, QPointF>) — currently it uses fixed S-meter dbmToFraction()
- `needleOffset`, `radiusRatio`, `lengthFactor` — new positioning properties
- `onlyWhenRx`, `onlyWhenTx` — visibility filtering
- `displayGroup` — group filtering for TX mode subsets
- `historyEnabled`, `historyDuration`, `historyColor` — history trail

Add these properties to NeedleItem.h as part of this task. The existing S-meter behavior
continues to work (default scaleCalibration is empty → uses dbmToFraction() fallback).

- [ ] **Step 3: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/MeterItem.h src/gui/meters/MeterItem.cpp \
        src/gui/meters/ItemGroup.h src/gui/meters/ItemGroup.cpp
git commit -m "Add ANANMM 7-needle composite preset (from Thetis AddAnanMM)"
```

---

### Task 17: CrossNeedle Composite Preset

**Files:**
- Modify: `src/gui/meters/ItemGroup.h` — add `createCrossNeedlePreset()` declaration
- Modify: `src/gui/meters/ItemGroup.cpp` — add implementation

- [ ] **Step 1: Implement createCrossNeedlePreset()**

Dual crossing needles — forward and reflected power. From Thetis `AddCrossNeedle()` (MeterManager.cs:22817-23002).

1. **Background image** (ImageItem, z=1): `cross-needle.png`
2. **Bottom band** (ImageItem, z=5): `cross-needle-bg.png`
3. **Forward power needle** (NeedleItem): TxPower, black, red history
   - **Clockwise** direction, offset (0.322, 0.611), LengthFactor 1.62
   - NormaliseTo100W: true, StrokeWidth 2.5
   - 15-point calibration: 0/5/10/15/20/25/30/35/40/50/60/70/80/90/100W
4. **Forward power scale** (NeedleScalePwrItem): 8 marks, Trebuchet MS Bold 16pt
5. **Reverse power needle** (NeedleItem): TxReversePower, black, cornflower blue history
   - **CounterClockwise** direction, offset **(-0.322**, 0.611) — negative X for mirror
   - NormaliseTo100W: true, StrokeWidth 2.5
   - 19-point fine calibration: 0/0.25/0.5/0.75/1/2/3/4/5/6/7/8/9/10/12/14/16/18/20W
6. **Reverse power scale** (NeedleScalePwrItem): 8 marks, CounterClockwise

All calibration (x,y) pairs from Thetis lines 22823-22971 — copy exactly.

NeedleItem needs a `direction` property (Clockwise/CounterClockwise) for this preset.
Add it alongside the extensions from Task 16.

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/ItemGroup.h src/gui/meters/ItemGroup.cpp
git commit -m "Add CrossNeedle dual-power preset (from Thetis AddCrossNeedle)"
```

---

### Task 18: Edge Meter Display Mode

**Files:**
- Modify: `src/gui/meters/MeterItem.h` — add `BarStyle` enum to BarItem
- Modify: `src/gui/meters/MeterItem.cpp` — add Edge rendering path

- [ ] **Step 1: Add BarStyle enum and Edge rendering**

In `BarItem` class in `MeterItem.h`, add:

```cpp
    enum class BarStyle { Filled, Edge };

    void setBarStyle(BarStyle s) { m_barStyle = s; }
    BarStyle barStyle() const { return m_barStyle; }

    // Edge mode colors (from Thetis console.cs:12612-12678)
    void setEdgeBackgroundColor(const QColor& c) { m_edgeBgColor = c; }
    void setEdgeLowColor(const QColor& c) { m_edgeLowColor = c; }
    void setEdgeHighColor(const QColor& c) { m_edgeHighColor = c; }
    void setEdgeAvgColor(const QColor& c) { m_edgeAvgColor = c; }
```

In `BarItem::paint()`, add Edge rendering path:
- When `m_barStyle == BarStyle::Edge`:
  - Draw background rectangle outline with `m_edgeBgColor`
  - Calculate pixel_x from fraction * width
  - Draw 3 vertical lines: shadow-center-shadow
  - Shadow color = midpoint blend of `m_edgeAvgColor` and `m_edgeBgColor`
  - Center line color = `m_edgeAvgColor`

From Thetis `picMultiMeterDigital_Paint()` Edge mode (console.cs:23587-23663).

Update BarItem serialization to include the new fields.

- [ ] **Step 2: Build and commit**

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
git add src/gui/meters/MeterItem.h src/gui/meters/MeterItem.cpp
git commit -m "Add Edge meter display mode to BarItem (from Thetis Edge meter style)"
```

---

### Task 19: Build, Visual Verification, and Final Commit

**Files:**
- Modify: `src/gui/MainWindow.cpp` — add test presets to verify new items

- [ ] **Step 1: Full clean build**

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

Expected: Clean compile with 0 errors.

- [ ] **Step 2: Add temporary test container**

In MainWindow, temporarily add a second container with a MagicEyeItem or HistoryGraphItem
preset to verify rendering works. Connect it to the MeterPoller.

- [ ] **Step 3: Launch and visually verify**

```bash
./build/NereusSDR
```

Verify:
- Existing S-Meter, Power/SWR, ALC presets still render correctly (no regressions)
- New item types render when added to a container
- Serialization round-trips (restart app, verify containers restore)

- [ ] **Step 4: Remove test container, final commit**

```bash
git add -A
git commit -m "Phase 3G-4 complete: 12 advanced meter item types + ANANMM/CrossNeedle presets"
```

- [ ] **Step 5: Update CLAUDE.md and CHANGELOG.md**

Update the phase status in CLAUDE.md from "Planned" to "Complete" for Phase 3G-4.
Add changelog entry documenting all new item types.

```bash
git add CLAUDE.md CHANGELOG.md
git commit -m "Update docs: Phase 3G-4 complete"
```

---

## NeedleItem Extensions Summary (Tasks 16-17)

The ANANMM and CrossNeedle presets require these additions to the existing `NeedleItem`:

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `scaleCalibration` | `QMap<float, QPointF>` | empty | Custom value → position mapping |
| `needleOffset` | `QPointF` | (0,0) | Center offset for needle pivot |
| `radiusRatio` | `QPointF` | (1,1) | Elliptical scaling |
| `lengthFactor` | `float` | 1.0 | Needle length multiplier |
| `direction` | `NeedleDirection` enum | Clockwise | CW or CCW sweep |
| `strokeWidth` | `float` | 1.5 | Needle line thickness |
| `onlyWhenRx` | `bool` | false | Hide during TX |
| `onlyWhenTx` | `bool` | false | Hide during RX |
| `displayGroup` | `int` | -1 | Group filter (-1 = always visible) |
| `historyEnabled` | `bool` | false | Show history trail |
| `historyDuration` | `int` | 4000 | Trail duration ms |
| `historyColor` | `QColor` | transparent | Trail line color |
| `normaliseTo100W` | `bool` | false | Power normalisation |

When `scaleCalibration` is non-empty, `paint()` uses it instead of `dbmToFraction()`.
The existing S-meter rendering is unchanged (empty calibration map → original behavior).
