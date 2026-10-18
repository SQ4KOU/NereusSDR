# Approved display rebuild: source map at the recovered baseline

Read from `c3996f7f6` on October 12, 2026. This is an implementation map of the
accepted live page, not a new design. Production source is unchanged during
this mapping pass; the prepared five-tab regression is waiting for its build slot.

## Page regrouping

| Accepted destination | Existing controls / APIs | Existing source and required relocation |
| --- | --- | --- |
| Spectrum, Trace | `DisplaySpectrumDetector`, `DisplaySpectrumAveraging`, `DisplaySpectrumAverageTimeMs`, `DisplayLineWidth`, `DisplayTraceColor`, `dispNormalize` / `setDispNormalize` | `SpectrumOverlayPanel::buildPillSpectrumPage`; normalization currently only in `SpectrumDefaultsPage`. Move the latter's existing behavior into the pill too. |
| Spectrum, Fill | `DisplayPanFill`, `DisplayFftFillAlpha`, `DisplayFillColor`, `DisplayGradientEnabled` | Existing pill rows. In Setup, Trace/Fill colors currently live in `AppearanceSetupPages.cpp:77` onward. Move those bindings, retaining `applyDisplayDefault` behavior. Gradient is below Fill Color in the accepted Setup page; the compact pill has its separate shared toggle line. |
| Spectrum, Peak hold | `DisplayPeakHoldEnabled`, `DisplayPeakHoldResetMs` | Existing pill rows; compose one compact switch/delay row. Keep highest-seen Peak Hold distinct from the decaying peak trace on Peaks & Markers. |
| Spectrum, Grid and readouts | `DisplayGridEnabled`, `DisplayFreqGridSpacing`, `DisplayFreqScaleFontPx`, `DisplayShowCursorFreq`, `DisplayShowNoiseFloor`, `DisplayShowBinWidth`, `DisplayShowPeakValueOverlay` | Existing pill rows. Noise Floor, Bin Width and Peak Value retain their Setup detail rows on Peaks & Markers, with the live page's explicit cross-link. |
| Levels, spectrum scale | `DisplayGridMax`, `DisplayGridMin`, `setBandGrid`, `perBandDbMaxRequested`, `perBandDbMinRequested` | Move existing rows from Spectrum. Retain band-store requests through `DisplayPillWiring.cpp:124` onward and `MainWindow::wirePanDisplayFlyout`. The new scale chooser/offset/keep-range rows consume the existing grid-follow methods. |
| Levels, waterfall range | `ClarityEnabled`, `ClarityRetune`, `DisplayWfBlackLevel`, `DisplayWfColorGain`, `WaterfallAGCOffsetDb`, `DisplayWfLowLevel`, `DisplayWfHighLevel` | Move from Waterfall. Replace the old Clarity/AGC/NF-AGC/min-max switches with the accepted single mode chooser in the later functional step; moving the rows alone must not change their binding. |
| Waterfall, Drawing and Smoothing | `DisplayWfColorScheme`, `DisplayWfUpdatePeriodMs`, `DisplayWfOpacity`, `WaterfallBlankingEnabled`, `WaterfallBlankingThresholdDb`, `DisplayWaterfallAveraging`, `DisplayWaterfallAverageTimeMs` | Retain existing Waterfall controls; compose NB Blank/threshold together. WF Detector remains Setup-only. Add the receive Custom editor using the existing gradient widget. |
| Waterfall, Overlays and Time | stop-on-TX, RX/TX Filter, RX/TX Zero, Timestamp, Rewind | Retain existing pill setters. Time Zone remains Setup-only. Preserve the live page's rate-dependent rewind explanation. |
| View, 2D/3D | render mode, `Display3DFloor`, `Display3DGain`, `Display3DSpan`, `Display3DAngle`, `Display3DSpeed`, `Display3DSliceShadow` | Existing `buildPillView3DPage`. Visible name becomes View. Its saved token `View3D` and object names can remain stable independently of the visible label. |
| View, Background | `DisplayBackgroundImage`, Clear, `DisplayBackgroundEnabled`, `DisplayBackgroundOpacity`, `DisplayBackgroundColor` | Move the existing block from Spectrum. Its file/color chooser seams and immediate setters remain usable; preserve their existing comments and behavior. |
| FFT | `DisplayFftSizingMode`, `DisplayFftSize`, `DisplayHzPerBinTarget`, FPS, Window, Decimation | Existing `buildPillFftPage` and `DisplayPillFftState`; requests already route through `DisplayPillWiring`. Visible header scope changes from ALL PANS to STATION. Do not create per-pan FFT settings. |

The enum, tab-button vector and page vector must retain the same order, since
`showPillTab()` uses the enum's integer as the button/page index. The current
page array has four entries (`SpectrumOverlayPanel.cpp:1130`); `pillTabName`
and `pillTabFromName` encode strings, so changing integer order need not break
existing remembered `Fft` or `View3D` tokens. Add a Levels token. Unknown saved
values continue to fall back to Spectrum.

The existing row registry is sufficient for moving rows: `registerPillRow`,
`seedPillRow`, `m_pillRowOrder`. `bindDisplayPill` at line 1698 seeds the target,
follows its `displaySettingChanged`, its specific 3D/model signals, key state,
Clarity state and reset. The move must preserve those connections and seed
without writing. The accepted new dependency gates will also require enable
refresh when their parents change; today's general handler refreshes for only
Peak Hold, AGC and spectrum-min/max (lines 1707-1715).

## Scope changes that cannot be achieved by moving widgets

| Accepted behavior | Baseline evidence | Implementation boundary |
| --- | --- | --- |
| Noise Floor and Bin Width can be each pan's own | `SpectrumWidget.cpp:3805-3855`: setters explicitly call `shareWithOtherPans` | Change persistence and propagation together; a moved row alone still changes every pan. |
| Track floor, Floor Offset, Keep range can be each pan's own | `SpectrumWidget.cpp:4126-4153`: all three setters explicitly share with other pans | Preserve controller timing; remove unconditional sharing only as part of tested per-pan/master semantics. |
| Peak Value can be each pan's own | `SpectrumWidget.cpp:4298-4352`: setter shares with other pans | Include the detail controls and reset/inheritance behavior, not only the top toggle. |
| Clarity belongs to the pan's chosen levels mode | `MainWindow.cpp:6977-6978` routes the existing switch to `RadioModel::setClarityEnabled` | The five-tab regrouping can keep the old hook initially. The mode step must update registry ownership and the applet/phone contract together. |
| Master changes reach every pan; equality clears own values | `PanDisplayKeys.h:23-30` and `writeSparse` contract: an existing override survives equality | The accepted assumption differs from current behavior. Test equality, no-op choice, dot clearing, and master application before changing it. |
| dB Step is a per-pan picture setting | `GridScalesPage`, `DisplaySetupPages.cpp:2817-2831`, writes `PanadapterModel::setGridStep`; `PanadapterModel.cpp:394-402` shares it across models | Preserve the band pair's separate scope. Keep the step and actual grid rendering synchronized. |
| One saved TX scale on this computer | `SpectrumWidget.cpp:1393-1395` loads the pair and `1855-1859` saves it through the per-pan settings layer | Requires migration/synchronization. This baseline already saves TX scale; do not treat it as an absent save feature. |

The band's receive dB Max/Min still go through `PanadapterModel` and the station
store. Their station-authoritative scope is distinct from the per-pan picture
controls. 3D Floor keeps the accepted per-band behavior already present.

## Existing work to reuse rather than redo

- **Normalization rendering exists.** `normalizeActive` and `normalizeShiftDb`
  are implemented at `SpectrumWidget.cpp:7532-7540`; the shift is applied to both
  pixel conversion paths at 7629 and 7644. The mockup's "moves the trace" item
  does not establish a missing implementation at this recovered head.
- **Stored grid-step rendering exists.** `setDbGridStep` at 5381 sets the widget's
  step, and `forEachDbGridLine` in `SpectrumWidget.h:3010-3016` uses it to draw
  the horizontal lines. The older study's no-effect finding predates this code.
- **Receive Custom is still a fallback in the palette table.**
  `WaterfallPalettes.cpp:143-147` records the fallback stops; the selected scheme
  uses them at 196-198. Trace the actual row-render path before adding the
  accepted shared receive Custom editor. The existing TX editor is in
  `DisplaySetupPages.cpp:3581-3695` and currently hides until Custom is chosen;
  the accepted page instead keeps the editor button visible and gray with its
  reason outside Custom.
- **Setup links use page names.** `DisplayPillWiring.cpp:113-116` forwards the
  pill's page string; `SetupDialog.cpp:1869-1909` registers the old names.
  Rename/register destinations and update cross-links together. A five-tab pill
  must not point to a new Setup name before that destination exists.

## Setup description and phone carriers

Version **28 already exists at this baseline**, unlike the October 11 scout
inventory. Current evidence:

- `resources/setup/display.json:2`: version 28.
- `StationServer.cpp:1470`: description cap 28.
- `SetupDescriptionService.cpp:155-174`: V28 and pre-V28 row tables;
  `fitCategoryForVersion` at 3230-3232 substitutes older rows.
- `ios/NereusKit/Sources/NereusMirror/SetupDescription.swift:260`: highestVersion 28.
- `SetupDescription+V28.swift`: exact closed-row checks for sizing and averaging.
- `LinkFeatures.swift:209`: requests setupDescription 28.
- `SetupDescriptionFeed.swift:134`: Display version ceilings including 28.

The rebuilt page order, moved controls, labels and mode selectors need the next
available revision, with V28 and earlier payloads preserved. **29 is the next
integer at this head**, subject to rechecking the integration base before the
versioned change. This is a compatibility implementation fact, not permission
to replace the V28 contract in place.

Surfaces requiring their own changes and verification:

| Surface | Source |
| --- | --- |
| Desktop pill | `src/gui/SpectrumOverlayPanel.*`, `DisplayPillWiring.*`, window hooks |
| Desktop Setup and navigation | `src/gui/setup/DisplaySetupPages.*`, `SpectrumPeaksPage.*`, `MultimeterPage.*`, `AppearanceSetupPages.*`, `src/gui/SetupDialog.cpp`, `SetupPage.*` |
| Display applet | `src/gui/applets/DisplayApplet.*` |
| Described Setup | `resources/setup/display.json`, `appearance.json`, `src/core/setup/SetupDescriptionService.cpp`, version fitting and parity tests |
| Station catalog | `src/core/session/StationCatalog.cpp`, `ios/NereusKit/Sources/NereusModels/StationCatalog+Display.swift` |
| Phone described rows | `ios/NereusKit/Sources/NereusMirror/SetupDescription*`, request version and decoding tests |
| Phone hand-built display | `ios/NereusApp/Setup/DisplayOnThisPhonePage*.swift`, `Main/DisplaySheet*.swift`, `ios/NereusKit/Sources/NereusBand/BandDisplaySettings*` |

Do not replace the phone's separate decoder/version contract with an assumption
that changing desktop controls updates it automatically. No source edits,
builds, tests, phone simulator batches or hardware runs occurred in this pass.
