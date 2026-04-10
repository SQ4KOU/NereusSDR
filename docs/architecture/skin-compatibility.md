# Legacy Skin Compatibility Layer

**Status:** Phase 3 -- Design

---

## 1. Overview

NereusSDR supports importing and rendering Thetis skin packages to provide
visual continuity for operators migrating from Thetis. The compatibility
layer parses Thetis skin ZIP archives (XML layout + PNG images), maps
control names from Thetis's WinForms namespace to NereusSDR's Qt6 widget
tree, and applies visual overrides at runtime.

Legacy skins coexist with NereusSDR's native theming system (Qt Style Sheets).
When a legacy skin is active, it overrides the default theme for controls
that the skin defines; controls not present in the skin fall through to the
native theme.

---

## 2. Skin Package Format

### 2.1 Distribution

Skins are distributed as ZIP archives, one per skin. The archive is either
downloaded from a skin server or loaded from a local directory.

### 2.2 ZIP Contents

After extraction, a skin package contains:

```
SkinName/
+-- SkinName.xml              -- Layout and property definitions
+-- Console/
|   +-- Console.png           -- Main console background image
+-- [ControlName]/            -- One directory per skinned control
    +-- up.png                -- NormalUp state
    +-- down.png              -- NormalDown state (pressed)
    +-- over.png              -- MouseOverUp state (hover)
    +-- disabled.png          -- DisabledUp state
    +-- checked.png           -- CheckedUp state
    +-- unchecked.png         -- UncheckedUp state
    +-- checked_over.png      -- CheckedUp + hover
    +-- unchecked_over.png    -- UncheckedUp + hover
```

Not all 8 states are required. Missing states fall back:

| Missing State | Fallback |
|---------------|----------|
| `over.png` | `up.png` |
| `disabled.png` | `up.png` (desaturated automatically) |
| `checked_over.png` | `checked.png` |
| `unchecked_over.png` | `unchecked.png` |
| `down.png` | `up.png` |

### 2.3 JSON Manifest

Each skin package includes or is described by a JSON manifest served by
skin repositories:

```json
{
  "SkinName": "DarkPanel",
  "SkinUrl": "https://example.com/skins/DarkPanel.zip",
  "SkinVersion": "1.2",
  "FromThetisVersion": "2.10.3",
  "ThumbnailUrl": "https://example.com/skins/DarkPanel_thumb.png"
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `SkinName` | string | Yes | Display name and directory name inside the ZIP |
| `SkinUrl` | string | Yes | Download URL for the skin ZIP archive |
| `SkinVersion` | string | Yes | Skin version (for update detection) |
| `FromThetisVersion` | string | Yes | Minimum Thetis version this skin was designed for |
| `ThumbnailUrl` | string | No | Preview image URL (256x192 recommended) |

### 2.4 XML Layout Format

The `SkinName.xml` file defines form-level and control-level visual properties.

**Form-level properties:**

```xml
<Form>
  <BackColor>30, 30, 30</BackColor>
  <Font>Microsoft Sans Serif, 8.25pt</Font>
  <Size>1312, 864</Size>
  <TransparencyKey>0, 0, 1</TransparencyKey>
</Form>
```

**Control-level properties:**

```xml
<Control Name="btnBandVHF">
  <Position X="123" Y="456" />
  <Size Width="48" Height="23" />
  <ForeColor>255, 255, 255</ForeColor>
  <BackColor>64, 64, 64</BackColor>
  <Font>Microsoft Sans Serif, 7pt, Bold</Font>
  <Text>VHF</Text>
</Control>
```

**Supported control types in XML:**

| XML Type | Thetis Type | Qt6 Equivalent |
|----------|-------------|----------------|
| `Button` | `System.Windows.Forms.Button` | `QPushButton` |
| `CheckBox` | `System.Windows.Forms.CheckBox` | `QCheckBox` / `QPushButton` (toggle) |
| `RadioButton` | `System.Windows.Forms.RadioButton` | `QRadioButton` |
| `Label` | `System.Windows.Forms.Label` | `QLabel` |
| `TextBox` | `System.Windows.Forms.TextBox` | `QLineEdit` |
| `ComboBox` | `System.Windows.Forms.ComboBox` | `QComboBox` |
| `NumericUpDown` | `System.Windows.Forms.NumericUpDown` | `QSpinBox` / `QDoubleSpinBox` |
| `PrettyTrackBar` | Custom Thetis slider | `GuardedSlider` |
| `PictureBox` | `System.Windows.Forms.PictureBox` | `QLabel` with pixmap |
| `GroupBox` | `System.Windows.Forms.GroupBox` | `QGroupBox` |
| `Panel` | `System.Windows.Forms.Panel` | `QWidget` / `QFrame` |

---

## 3. SkinParser Class

### 3.1 Responsibility

`SkinParser` extracts a skin ZIP, parses the XML layout, loads PNG images,
and produces a `SkinLayout` data structure that is independent of any GUI
framework. The parser validates control names against a known mapping table
and logs warnings for unrecognized controls.

### 3.2 Data Structures

```cpp
#pragma once

#include <QColor>
#include <QFont>
#include <QImage>
#include <QMap>
#include <QPoint>
#include <QSize>
#include <QString>
#include <QVector>

#include <optional>

namespace NereusSDR {

// Metadata from the skin's JSON manifest.
struct SkinManifest {
    QString skinName;
    QString skinUrl;
    QString skinVersion;
    QString fromThetisVersion;
    QString thumbnailUrl;
};

// Visual state images for a button-like control.
// Not all states are required; missing states are std::nullopt.
struct ButtonStateImages {
    std::optional<QImage> normalUp;
    std::optional<QImage> normalDown;
    std::optional<QImage> disabledUp;
    std::optional<QImage> disabledDown;
    std::optional<QImage> focusedUp;
    std::optional<QImage> focusedDown;
    std::optional<QImage> mouseOverUp;
    std::optional<QImage> mouseOverDown;
};

// Visual properties for a single control as parsed from the XML.
struct SkinControlDef {
    QString thetisName;           // Original Thetis control name
    QString controlType;          // "Button", "CheckBox", "Label", etc.
    QPoint position;              // (X, Y) from XML
    QSize size;                   // (Width, Height) from XML
    std::optional<QColor> foreColor;
    std::optional<QColor> backColor;
    std::optional<QFont> font;
    std::optional<QString> text;
    std::optional<ButtonStateImages> images;
};

// Form-level visual properties.
struct SkinFormDef {
    QColor backColor{30, 30, 30};
    QFont font;
    QSize formSize{1312, 864};
    QColor transparencyKey{0, 0, 1};
    std::optional<QImage> backgroundImage;  // Console.png
};

// Complete parsed skin layout -- framework-independent.
struct SkinLayout {
    SkinManifest manifest;
    SkinFormDef form;
    QMap<QString, SkinControlDef> controls;  // keyed by Thetis control name
};

} // namespace NereusSDR
```

### 3.3 SkinParser Interface

```cpp
#pragma once

#include <QObject>
#include <QString>

#include <expected>

namespace NereusSDR {

struct SkinLayout;
struct SkinManifest;

// Parses a Thetis skin package (ZIP or extracted directory) into a
// SkinLayout data structure.
//
// Thread safety: not thread-safe. Call from main thread only.
class SkinParser : public QObject {
    Q_OBJECT

public:
    explicit SkinParser(QObject* parent = nullptr);
    ~SkinParser() override;

    // Parse a skin from a ZIP archive. Extracts to a temporary directory,
    // parses XML, loads images, and returns the complete SkinLayout.
    // Returns std::unexpected with error message on failure.
    std::expected<SkinLayout, QString> parseFromZip(const QString& zipPath);

    // Parse a skin from an already-extracted directory.
    std::expected<SkinLayout, QString> parseFromDirectory(const QString& dirPath);

    // Parse only the JSON manifest (for browsing/preview without full load).
    static std::expected<SkinManifest, QString> parseManifest(
        const QByteArray& jsonData);

    // Return the list of Thetis control names that were found in the XML
    // but have no known NereusSDR mapping. Populated after a successful parse.
    QStringList unmappedControls() const { return m_unmappedControls; }

signals:
    // Emitted during parse to report progress (0.0 to 1.0).
    void parseProgress(float progress);

    // Emitted for each control name in the skin XML that does not have
    // a known NereusSDR mapping.
    void unmappedControlFound(const QString& thetisName);

private:
    // Internal helpers
    std::expected<SkinFormDef, QString> parseFormXml(const QString& xmlPath);
    std::expected<QMap<QString, SkinControlDef>, QString> parseControlsXml(
        const QString& xmlPath);
    ButtonStateImages loadButtonImages(const QString& controlDir);
    QColor parseColor(const QString& colorStr);
    QFont parseFont(const QString& fontStr);

    QStringList m_unmappedControls;
};

} // namespace NereusSDR
```

### 3.4 Parse Sequence

1. If input is a ZIP, extract to `{configDir}/skins/{SkinName}/`.
2. Locate `{SkinName}.xml` in the extracted directory.
3. Parse XML DOM: extract `<Form>` element for form-level properties.
4. Iterate `<Control>` elements: build `SkinControlDef` for each.
5. For each control, check `{SkinName}/{ControlName}/` directory for PNG images.
6. Load images into `ButtonStateImages` (lazy: only the states that exist).
7. Load `Console/Console.png` as the form background image.
8. Validate each control name against the control name mapping table (section 6).
9. Emit `unmappedControlFound` for controls not in the mapping table.
10. Return populated `SkinLayout`.

---

## 4. SkinRenderer Class

### 4.1 Responsibility

`SkinRenderer` takes a `SkinLayout` and applies it to the live NereusSDR
widget tree. It walks the mapping table, finds the corresponding Qt widget
by object name, and applies position, size, color, font, and image overrides.

### 4.2 Interface

```cpp
#pragma once

#include <QObject>
#include <QWidget>

namespace NereusSDR {

struct SkinLayout;

// Applies a parsed SkinLayout to NereusSDR's widget tree.
//
// Usage:
//   SkinRenderer renderer;
//   renderer.apply(mainWindow, skinLayout);
//
// To revert to the default theme:
//   renderer.revert(mainWindow);
//
// Thread safety: must be called from main (GUI) thread.
class SkinRenderer : public QObject {
    Q_OBJECT

public:
    explicit SkinRenderer(QObject* parent = nullptr);
    ~SkinRenderer() override;

    // Apply the skin to the given root widget and all descendants.
    // Returns the number of controls successfully skinned.
    int apply(QWidget* root, const SkinLayout& layout);

    // Revert all skin overrides, restoring the default NereusSDR theme.
    void revert(QWidget* root);

    // Query whether a legacy skin is currently active.
    bool isSkinActive() const { return m_skinActive; }

    // Return the name of the currently active skin, or empty string.
    QString activeSkinName() const;

    // Return the number of controls that were in the skin but could
    // not be mapped to a NereusSDR widget (after apply()).
    int unmappedCount() const { return m_unmappedCount; }

signals:
    // Emitted when a skin is applied or reverted.
    void skinChanged(const QString& skinName);

    // Emitted for each control in the skin that could not be found
    // in the NereusSDR widget tree.
    void controlNotFound(const QString& thetisName,
                         const QString& nereusName);

private:
    // Apply form-level properties (background, size, transparency)
    void applyFormProperties(QWidget* root, const SkinLayout& layout);

    // Apply per-control properties via the mapping table
    void applyControlProperties(QWidget* root, const SkinLayout& layout);

    // Apply button state images to a QPushButton using stylesheet
    void applyButtonImages(QWidget* button,
                           const ButtonStateImages& images);

    // Apply color/font to any widget via stylesheet
    void applyColorFont(QWidget* widget, const SkinControlDef& def);

    // Build Qt stylesheet from button state images (writes temp PNGs)
    QString buildButtonStyleSheet(const ButtonStateImages& images,
                                  const QString& controlName);

    // Save original stylesheets before overriding (for revert)
    void saveOriginalStyle(QWidget* widget);

    bool m_skinActive{false};
    QString m_activeSkinName;
    int m_unmappedCount{0};

    // Saved original stylesheets keyed by widget objectName
    QMap<QString, QString> m_savedStyles;
};

} // namespace NereusSDR
```

### 4.3 Rendering Strategy

**Background image:** The `Console.png` is rendered as the main window's
background using `QWidget::setAutoFillBackground(true)` and a palette
with the image set via `QPalette::Window`. For high-DPI displays, the
image is scaled using `Qt::SmoothTransformation`.

**Button state images:** QPushButton state images are applied via
Qt Style Sheets using the `border-image` property:

```css
QPushButton#btnBandVHF {
    border-image: url(:/skins/DarkPanel/btnBandVHF/up.png);
}
QPushButton#btnBandVHF:pressed {
    border-image: url(:/skins/DarkPanel/btnBandVHF/down.png);
}
QPushButton#btnBandVHF:hover {
    border-image: url(:/skins/DarkPanel/btnBandVHF/over.png);
}
QPushButton#btnBandVHF:disabled {
    border-image: url(:/skins/DarkPanel/btnBandVHF/disabled.png);
}
QPushButton#btnBandVHF:checked {
    border-image: url(:/skins/DarkPanel/btnBandVHF/checked.png);
}
```

Skin image files are cached on disk at `{configDir}/skins/{SkinName}/`
and referenced by absolute file path in stylesheets. No Qt resource
compilation is needed.

**Control positioning:** Thetis skins use absolute pixel positioning.
`SkinRenderer` applies `QWidget::move()` and `QWidget::resize()` for
each mapped control. When a legacy skin is active, the main window's
layout managers are temporarily disabled for the skinned region to
allow absolute positioning.

**Color and font:** Parsed `ForeColor`, `BackColor`, and `Font` properties
are applied via per-widget stylesheets:

```css
QLabel#lblVFOA {
    color: rgb(255, 255, 255);
    background-color: rgb(30, 30, 30);
    font: bold 12pt "Microsoft Sans Serif";
}
```

**Graceful fallback:** If a control named in the skin cannot be found in
the NereusSDR widget tree (after mapping), `SkinRenderer` emits
`controlNotFound` and logs a warning via `qCWarning(lcSkin)`. The
control is skipped and the default theme applies.

---

## 5. Layout Constraints

### 5.1 Panadapter Layout (UPDATED 2026-04-10)

> **Correction:** The original design constrained legacy skins to 2
> panadapters. Per the 2026-04-10 plan review, NereusSDR always allows
> up to 4 panadapters regardless of skin. Legacy Thetis skins define
> positions for 2 display panels — these are used as defaults, but the
> user can add additional panadapters at any time.

Thetis skins were designed for a fixed two-panadapter layout: RX1
(primary) and RX2 (secondary). Skin XML files define positions for
exactly two display panels.

When a legacy skin is active:

- The two original panel positions from the skin are applied as defaults.
- The user can always add up to 4 panadapters via the View menu.
- Additional panels beyond the skin's 2 use NereusSDR's native flexible
  layout engine for positioning.
- The skin's colors, fonts, and button images apply to all panels.

### 5.3 Resolution Scaling

Thetis skins target a specific form size (e.g., 1312x864). NereusSDR
scales skin coordinates proportionally when the main window size differs:

```
scaleFactor = min(actualWidth / skinWidth, actualHeight / skinHeight)
```

Control positions and sizes are multiplied by this factor. Images are
scaled using `Qt::SmoothTransformation`. Font sizes are scaled
proportionally but clamped to a minimum of 6pt for readability.

---

## 6. Control Name Mapping Table

This table maps Thetis WinForms control names to their NereusSDR
`QWidget::objectName()` equivalents. The mapping is the authoritative
reference for `SkinRenderer`.

### 6.1 VFO Controls

| Thetis Name | NereusSDR Name | Widget Type | Notes |
|-------------|----------------|-------------|-------|
| `txtVFOAFreq` | `vfoAFreqDisplay` | `QLabel` | VFO A frequency readout |
| `txtVFOBFreq` | `vfoBFreqDisplay` | `QLabel` | VFO B frequency readout |
| `txtVFOABand` | `vfoABandLabel` | `QLabel` | VFO A band label |
| `txtVFOBBand` | `vfoBBandLabel` | `QLabel` | VFO B band label |
| `btnVFOA` | `btnVfoA` | `QPushButton` | Select VFO A active |
| `btnVFOB` | `btnVfoB` | `QPushButton` | Select VFO B active |
| `btnVFOSwap` | `btnVfoSwap` | `QPushButton` | Swap VFO A/B |
| `btnVFOAtoB` | `btnVfoAToB` | `QPushButton` | Copy A to B |
| `btnVFOBtoA` | `btnVfoBToA` | `QPushButton` | Copy B to A |
| `btnSplit` | `btnSplit` | `QPushButton` | Split mode |
| `btnRIT` | `btnRit` | `QPushButton` | RIT enable |
| `btnXIT` | `btnXit` | `QPushButton` | XIT enable |
| `udRIT` | `spinRit` | `QSpinBox` | RIT offset value |

### 6.2 Mode Buttons

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `radModeLSB` | `btnModeLsb` | `QPushButton` (checkable) |
| `radModeUSB` | `btnModeUsb` | `QPushButton` (checkable) |
| `radModeDSB` | `btnModeDsb` | `QPushButton` (checkable) |
| `radModeCWL` | `btnModeCwl` | `QPushButton` (checkable) |
| `radModeCWU` | `btnModeCwu` | `QPushButton` (checkable) |
| `radModeFMN` | `btnModeFm` | `QPushButton` (checkable) |
| `radModeAM` | `btnModeAm` | `QPushButton` (checkable) |
| `radModeSAM` | `btnModeSam` | `QPushButton` (checkable) |
| `radModeDIGU` | `btnModeDigU` | `QPushButton` (checkable) |
| `radModeDIGL` | `btnModeDigL` | `QPushButton` (checkable) |
| `radModeSPEC` | `btnModeSpec` | `QPushButton` (checkable) |
| `radModeDRM` | `btnModeDrm` | `QPushButton` (checkable) |

### 6.3 Band Buttons

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `btnBand160` | `btnBand160` | `QPushButton` |
| `btnBand80` | `btnBand80` | `QPushButton` |
| `btnBand60` | `btnBand60` | `QPushButton` |
| `btnBand40` | `btnBand40` | `QPushButton` |
| `btnBand30` | `btnBand30` | `QPushButton` |
| `btnBand20` | `btnBand20` | `QPushButton` |
| `btnBand17` | `btnBand17` | `QPushButton` |
| `btnBand15` | `btnBand15` | `QPushButton` |
| `btnBand12` | `btnBand12` | `QPushButton` |
| `btnBand10` | `btnBand10` | `QPushButton` |
| `btnBand6` | `btnBand6` | `QPushButton` |
| `btnBand2` | `btnBand2` | `QPushButton` |
| `btnBandVHF` | `btnBandVhf` | `QPushButton` |
| `btnBandGEN` | `btnBandGen` | `QPushButton` |

### 6.4 DSP Controls

| Thetis Name | NereusSDR Name | Widget Type | Notes |
|-------------|----------------|-------------|-------|
| `chkNR` | `btnNr` | `QPushButton` (checkable) | NR1 toggle |
| `chkNR2` | `btnNr2` | `QPushButton` (checkable) | NR2 toggle |
| `chkNB` | `btnNb` | `QPushButton` (checkable) | NB1 toggle |
| `chkNB2` | `btnNb2` | `QPushButton` (checkable) | NB2 toggle |
| `chkANF` | `btnAnf` | `QPushButton` (checkable) | ANF toggle |
| `chkSquelch` | `btnSquelch` | `QPushButton` (checkable) | Squelch toggle |
| `btnRX2` | `btnRx2` | `QPushButton` (checkable) | RX2 enable |
| `chkBIN` | `btnBinaural` | `QPushButton` (checkable) | Binaural toggle |
| `chkMUT` | `btnMute` | `QPushButton` (checkable) | Mute toggle |
| `comboAGC` | `comboAgcMode` | `QComboBox` | AGC mode selector |
| `ptbSquelch` | `sliderSquelch` | `GuardedSlider` | Squelch level |

### 6.5 Filter Controls

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `radFilter1` | `btnFilter1` | `QPushButton` (checkable) |
| `radFilter2` | `btnFilter2` | `QPushButton` (checkable) |
| `radFilter3` | `btnFilter3` | `QPushButton` (checkable) |
| `radFilter4` | `btnFilter4` | `QPushButton` (checkable) |
| `radFilter5` | `btnFilter5` | `QPushButton` (checkable) |
| `radFilter6` | `btnFilter6` | `QPushButton` (checkable) |
| `radFilter7` | `btnFilter7` | `QPushButton` (checkable) |
| `radFilter8` | `btnFilter8` | `QPushButton` (checkable) |
| `radFilter9` | `btnFilter9` | `QPushButton` (checkable) |
| `radFilter10` | `btnFilter10` | `QPushButton` (checkable) |
| `radFilterVar1` | `btnFilterVar1` | `QPushButton` (checkable) |
| `radFilterVar2` | `btnFilterVar2` | `QPushButton` (checkable) |

### 6.6 TX Controls

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `chkMOX` | `btnMox` | `QPushButton` (checkable) |
| `chkTUN` | `btnTune` | `QPushButton` (checkable) |
| `chkVOX` | `btnVox` | `QPushButton` (checkable) |
| `chkCOMP` | `btnComp` | `QPushButton` (checkable) |
| `ptbDrive` | `sliderDrive` | `GuardedSlider` |
| `ptbMic` | `sliderMicGain` | `GuardedSlider` |
| `chkPS` | `btnPureSignal` | `QPushButton` (checkable) |

### 6.7 Audio/Volume Controls

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `ptbAF` | `sliderVolume` | `GuardedSlider` |
| `ptbRF` | `sliderRfGain` | `GuardedSlider` |
| `ptbPanMainRX` | `sliderPanRx1` | `GuardedSlider` |
| `ptbPanSubRX` | `sliderPanRx2` | `GuardedSlider` |
| `ptbRX2AF` | `sliderVolumeRx2` | `GuardedSlider` |
| `ptbRX2RF` | `sliderRfGainRx2` | `GuardedSlider` |

### 6.8 Display Panels

| Thetis Name | NereusSDR Name | Widget Type | Notes |
|-------------|----------------|-------------|-------|
| `panelDisplay` | `panadapterPanel1` | `QWidget` | RX1 panadapter container |
| `panelRX2Display` | `panadapterPanel2` | `QWidget` | RX2 panadapter container |
| `picDisplay` | `spectrumWidget1` | `SpectrumWidget` | RX1 spectrum renderer |
| `picRX2Display` | `spectrumWidget2` | `SpectrumWidget` | RX2 spectrum renderer |

### 6.9 Meter Displays

| Thetis Name | NereusSDR Name | Widget Type |
|-------------|----------------|-------------|
| `picMultiMeterDigital` | `meterDigitalDisplay` | `QLabel` |
| `picRX2Meter` | `meterRx2Display` | `QLabel` |
| `lblMultiSMeter` | `lblSmeter` | `QLabel` |

---

## 7. Skin Server Integration

### 7.1 Server Configuration

Skin server URLs are stored in `{configDir}/skin_servers.json`:

```json
{
  "servers": [
    {
      "name": "Official NereusSDR Skins",
      "url": "https://nereus-sdr.example.com/skins/manifest.json",
      "enabled": true
    },
    {
      "name": "Thetis Community Skins (ramdor)",
      "url": "https://raw.githubusercontent.com/ramdor/ThetisSkins/master/manifest.json",
      "enabled": true
    },
    {
      "name": "OE3IDE Skins",
      "url": "https://oe3ide.com/thetis/skins/manifest.json",
      "enabled": true
    }
  ]
}
```

### 7.2 SkinServer Interface

```cpp
#pragma once

#include <QObject>
#include <QUrl>

namespace NereusSDR {

struct SkinManifest;

// Fetches skin manifests and ZIP archives from remote skin servers.
// All network operations are asynchronous.
class SkinServer : public QObject {
    Q_OBJECT

public:
    explicit SkinServer(QObject* parent = nullptr);
    ~SkinServer() override;

    // Load server list from skin_servers.json.
    void loadServerList(const QString& configDir);

    // Fetch all manifests from all enabled servers.
    // Emits manifestsReady when complete.
    void fetchManifests();

    // Download a skin ZIP archive. Emits downloadComplete when done.
    void downloadSkin(const SkinManifest& manifest);

    // Download a thumbnail image. Emits thumbnailReady when done.
    void fetchThumbnail(const SkinManifest& manifest);

    // Return the local cache directory for downloaded skins.
    QString cacheDir() const;

    // Check if a skin is already cached locally.
    bool isCached(const QString& skinName) const;

signals:
    void manifestsReady(const QVector<SkinManifest>& manifests);
    void downloadProgress(const QString& skinName, float progress);
    void downloadComplete(const QString& skinName,
                          const QString& localZipPath);
    void downloadFailed(const QString& skinName, const QString& error);
    void thumbnailReady(const QString& skinName, const QImage& thumbnail);

private:
    struct ServerEntry {
        QString name;
        QUrl url;
        bool enabled{true};
    };

    QVector<ServerEntry> m_servers;
    QString m_cacheDir;
};

} // namespace NereusSDR
```

### 7.3 Download and Cache Flow

```
User opens Skin Browser
  |
  v
SkinServer::fetchManifests()
  |-- GET each server's manifest.json
  |-- Parse JSON array of SkinManifest entries
  |-- Merge results (deduplicate by SkinName)
  +-> emit manifestsReady(manifests)
        |
        v
   Skin Browser displays list with thumbnails
   (thumbnails fetched lazily via fetchThumbnail)
        |
        v
   User clicks "Install"
        |
        v
   SkinServer::downloadSkin(manifest)
     |-- GET manifest.skinUrl
     |-- Save to {configDir}/skins/cache/{SkinName}.zip
     |-- Verify ZIP integrity
     +-> emit downloadComplete(skinName, localPath)
           |
           v
      SkinParser::parseFromZip(localPath)
        |-- Extract to {configDir}/skins/{SkinName}/
        |-- Parse XML, load images
        +-> SkinLayout ready for SkinRenderer::apply()
```

### 7.4 Cache Management

- Downloaded ZIPs are stored in `{configDir}/skins/cache/`.
- Extracted skins are stored in `{configDir}/skins/{SkinName}/`.
- Skin version is checked against the manifest on each server refresh.
  If a newer version is available, the user is prompted to update.
- Cache can be cleared from the skin settings dialog.

---

## 8. SkinManager -- Orchestration

`SkinManager` ties together `SkinParser`, `SkinRenderer`, and `SkinServer`
and provides the high-level API used by the settings dialog and MainWindow.

```cpp
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace NereusSDR {

class SkinParser;
class SkinRenderer;
class SkinServer;
struct SkinLayout;

// High-level skin management: install, activate, deactivate, browse.
// Owns SkinParser, SkinRenderer, and SkinServer.
class SkinManager : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString activeSkin READ activeSkin NOTIFY activeSkinChanged)
    Q_PROPERTY(bool skinActive READ isSkinActive NOTIFY activeSkinChanged)

public:
    explicit SkinManager(QWidget* mainWindow, QObject* parent = nullptr);
    ~SkinManager() override;

    // Query installed skins (extracted to the skins directory).
    QStringList installedSkins() const;

    // Activate a previously installed skin by name.
    // Returns false if the skin cannot be loaded.
    bool activateSkin(const QString& skinName);

    // Deactivate the current skin and revert to the default theme.
    void deactivateSkin();

    // Return the name of the currently active skin, or empty string.
    QString activeSkin() const;
    bool isSkinActive() const;

    // Access sub-components.
    SkinServer* server() { return m_server; }

    // Persist active skin name to AppSettings.
    void saveState();
    void restoreState();

signals:
    void activeSkinChanged(const QString& skinName);
    void skinInstalled(const QString& skinName);
    void skinError(const QString& message);

private:
    QWidget* m_mainWindow;
    SkinParser* m_parser;
    SkinRenderer* m_renderer;
    SkinServer* m_server;
    std::unique_ptr<SkinLayout> m_activeLayout;
};

} // namespace NereusSDR
```

### 8.1 Startup Sequence

1. `SkinManager::restoreState()` reads `ActiveSkin` from `AppSettings`.
2. If a skin name is saved, call `activateSkin(skinName)`.
3. `SkinParser` loads the extracted skin directory.
4. `SkinRenderer::apply()` walks the widget tree and applies the skin.
5. If the skin fails to load (missing files, corrupt XML), log a warning
   and fall through to the default theme.

### 8.2 Settings Persistence

| Key | Type | Description |
|-----|------|-------------|
| `ActiveSkin` | string | Name of the active legacy skin (empty = none) |
| `SkinDirectory` | string | Override path for skin storage |
| `SkinServersEnabled` | string | "True" / "False" -- enable remote skin browsing |

All settings use `AppSettings`, never `QSettings`.

---

## 9. Error Handling

| Scenario | Behavior |
|----------|----------|
| ZIP extraction fails | Log `qCWarning(lcSkin)`, emit `skinError`, use default theme |
| XML parse error | Log warning with line number, skip malformed elements |
| Missing control image directory | Use default widget appearance for that control |
| Unknown control name in XML | Log `qCInfo(lcSkin)`, skip (non-fatal) |
| Skin references unavailable font | Fall back to system default font |
| Network timeout fetching manifest | Retry once, then show cached list |
| Downloaded ZIP fails integrity check | Delete cached ZIP, emit `downloadFailed` |

---

## 10. Future Considerations

- **Native NereusSDR theme format:** A new JSON-based theme format that
  takes advantage of Qt6 features (SVG icons, DPI-aware scaling, QML
  theming) without the constraints of Thetis's WinForms layout model.
- **Skin converter tool:** A utility that converts a Thetis skin ZIP
  into the native NereusSDR theme format, allowing skin authors to
  update their skins for the new layout engine.
- **Community skin repository:** A centralized NereusSDR skin repository
  with upload/review workflow, separate from the Thetis skin servers.
