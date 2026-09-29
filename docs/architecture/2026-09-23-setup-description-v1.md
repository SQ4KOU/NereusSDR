# Setup description versions 1–14

The Core sends the desktop's built Setup pages as JSON strings on the read-only
`setup` mirror object (`SetupDescription`). It has one string property per
category and a `revision` that advances when any published description
changes. A category string is empty until that category is published. The Core
sends its schema, object and later deltas only to a session whose client hello
declares `setupDescription: 1`; that session receives
`setupDescriptionVersion: 1` in capabilities. Older clients see neither.

Each category file is embedded in NereusCore at `:/setup/<id>.json`:

```json
{
  "version": 1,
  "category": {"id": "test", "title": "Test", "where": "station"},
  "pages": [{
    "id": "test.twoToneImd", "title": "Two-Tone IMD", "where": "station",
    "sections": [{"title": "Mode", "controls": [{
      "id": "test.twoToneImd.invert", "label": "Invert for LS Modes",
      "tooltip": "Swap F1 and F2 for lower side band modes",
      "kind": "toggle",
      "binding": {"property": {"object": "transmit", "name": "twoToneInvert"}},
      "applies": "live", "gate": {"transmit": true}
    }]}]
  }]
}
```

The `pages`, `sections` and `controls` arrays preserve the desktop order.
Category and page `where` values are `station`, `phone` or `mixed`; the owner
of a control is determined by its binding. A page or control the desktop has
not built is absent. The desktop-only exceptions are Remote Access, Skins and
Collapsible Display. A desktop control has the dynamic QObject property
`nereusSetupId` equal to the description's control ID; no ID is reused.
An optional `coverage` field on a category or page is `partial` or a more
specific pending scope. The renderer may show described controls but must not
infer that omitted desktop controls are present. A category with no ready
pages is sent as an empty string, not a page with empty controls. Diagnostics
publishes only its Settings Validation panel to version-3 peers; versions 1
and 2 still receive an empty Diagnostics string. Its local file and log
actions remain undescribed.

Control `kind` is one of `toggle`, `integer`, `decimal`, `slider`, `choice`,
`text`, `colour`, `button`, `readout` or `table`; version 3 also has the one
closed `settingsHygiene` panel described below. Numeric controls carry
`min`, `max`, `step` and, where shown, `unit`. Choices have an ordered `choices`
array. A table has `rows`, `columns` and a cell kind. `tooltip` is the
desktop's exact text, including an empty string when the desktop has none.

Every control has exactly one `binding`: `setting` (an AppSettings key routed
by `classifySettingsKey`), `property` (a mirrored object and property),
`command` (a station verb), or `phone` (a closed phone-owned dispatch identity). The
version-3 Settings Validation panel and version-6 antenna tables have their
own closed `settingsHygiene` and `antennaRows` bindings; neither extends
those generic binding rules. A
station binding has `applies: "live"`; the only other value,
`subscription`, is for display keys a client puts into its endpoint
subscription. A `toggle` backed by a station `setting` requires exactly
`"valueEncoding":{"true":"True","false":"False"}`. The five
setting-backed toggles in `resources/setup/general.json` are the canonical
fixture. The renderer may read a native boolean or a case-insensitive exact
match of either mapped string from its current live settings value. A
missing, stale or malformed value disables the control with a plain reason.
An edit sends the exact mapped string to the settings proxy, never a JSON
boolean or a value from another epoch. Property-backed toggles carry no
`valueEncoding` and retain their mirrored boolean wire kind. Optional gates
are capability name/minimum version, transmit
permission, and a BoardCapabilities flag. A false board flag removes its
control; a permission gate disables its control with the Core's reason.
`availability: {"enabled": false, "reason": "…"}` keeps a built but currently
unavailable desktop control visible with the same plain reason. General's
Extended control uses this pending its migration policy; no edit is sent while
unavailable. Region requires transmitSettingsVersion 9 and the offAir gate.

Appearance > Colors & Theme currently publishes a partial phone-owned page of
ten built spectrum swatches. Each is `kind:"colour"`, `applies:"live"`, and has
a literal PascalCase `binding.phone`; the phone owns per-pan persistence and
never sends these values to the Core settings proxy. Each `default` and each
edited value at this boundary is an eight-digit `#RRGGBBAA` string, including
the final alpha byte. This is ColorSwatchButton's phone-facing format; the
desktop's own AppSettings uses Qt `HexArgb` (`#AARRGGBB`) and is not copied to
the phone. Core accepts only the ten named IDs/phone keys and exact default
colors. The hidden Waterfall Low Color row remains undescribed (it is an
unbuilt feature on the desktop); version 12 describes the Reset Colors action
(below). Appearance's source category is V7; its RGBA defaults are sent
only to V4+ peers, while older projections retain all ten
color controls without `default`. No station settings permission is needed.

Version 7 also publishes the built Appearance > Meter Styles > S-Meter page,
with exactly three phone-owned live controls. `appearance.meterStyles.face`
is a `choice` bound to `SMeter_FaceStyle` with integer options 0–6 in native
order: Aged Cream, VU Amber, Collins White, Blackface, Carbon, Ice, and
Classic (flat); default 0. The phone maps them to its existing typed face
cases `agedCream`, `vuAmber`, `collinsWhite`, `blackface`, `carbon`, `ice`,
and `classic`. `appearance.meterStyles.peakHold` is a `toggle` bound to
`PeakHoldEnabled`, default `true`. `appearance.meterStyles.peakDecay` is a
`choice` bound to `PeakDecayRate`, with integer options 0 Fast (20 dB/s),
1 Medium (10 dB/s), and 2 Slow (5 dB/s); default 1. The phone maps these to
its existing `fast`, `medium`, and `slow` cases. These binding strings are
dispatch identities for existing phone model actions, not independent
phone storage keys or Core settings writes. Each control has
`requiresDescriptionVersion:7`; options use only the closed numeric
`[{"value":<integer>,"label":<native text>}]` shape. The Core accepts only
these three IDs, bindings, kinds, exact options/defaults, labels and tooltips.
V1–V3 receive the old ten swatches without defaults, V4–V6 receive the old
version-4 Appearance shape with ten defaults, and V7+ receive the new page.
These controls carry no off-air gate: only Setup controls carrying the
offAir gate lock while the Core is keyed (G-61), and receive and display
controls such as these stay live on air. The existing session freshness
rule still applies; the description adds no permission or capability gate.
VFO Small Filter, Skins, and unused controls remain omitted. Phone rendering is owned by the
phone implementation and is not established by this Core publication.

An optional `decimals` field on a `kind:readout` control is an integer from 0
through 6. It formats a finite numeric mirrored value with that many decimal
places; the existing `unit` string follows the number (empty means no unit).
Missing, nonfinite, wrong-type, or stale-session values display unavailable,
never zero or a cached value from another session. Readouts send no edits,
including when the source property happens to be writable. A readout without
`decimals` keeps the renderer's prior behavior. The PA Values partial page
uses five `txState` scalars with `txReadingsVersion:1` and the selected Drive
setpoint from `transmit.power` (Int64) with `transmitSettingsVersion:1`. Drive
is a readout even though its mirrored source is writable; it sends no write.
Three additional readouts use the Core's existing PA scaling of its current
board's raw samples: `forwardRawPowerWatts` (W), `forwardAdcVolts` (V), and
`reflectedAdcVolts` (V), all Float64 from `txState` with
`txReadingsVersion:2`. They have two decimals and no transmit or off-air
gate. Their IDs are `pa.values.forwardRawPower`, `pa.values.forwardVoltage`,
and `pa.values.reflectedVoltage`. All Setup versions can carry these standard
property readouts; the independent capability gate makes them unavailable
with an older Core. There is no client formula, peak/min tracker, or reset
action in these descriptions.
`setup.pa` is appended after `setup.revision` in the fixed mirror schema and
is empty on a board without an integrated PA or on an RX-only SKU. Its nine
readouts remain visible while the radio transmits; they require neither
transmit permission nor an off-air gate. A peer without the negotiated
Setup-description feature receives no `setup` mirror object.

Version 3 adds one closed `kind:settingsHygiene` panel in Diagnostics >
Settings Validation, with ID `diagnostics.settingsValidation.health`. Its
`binding` is exactly `{"settingsHygiene":{"version":1}}` and its gate is
`settingsHygieneVersion:1`. It describes the existing validation issue list
and three desktop actions in order: Re-validate, Reset to Defaults (always
disabled with the Core's reason), and Forget This Radio. It is not a generic
command or result binding. The actual peer must separately declare
`settingsHygiene:1` and receive that capability; a descriptor alone grants
nothing. A V3 peer still receives the older controls with their existing
semantics. The Core filters every control above the peer's negotiated
description version, drops empty sections and pages, and caps an unknown
future declaration at version 14. PA has a version-14 ceiling (version 13
for V13, version 5 for V5–V12) and Hardware a version-13 ceiling (version 6
for V6–V12); Transmit a version-13 ceiling (version 3 for V3–V12, with
the Power page's earlier coverage text); Display a version-12 ceiling, and
Appearance a version-12 ceiling with its prior version-4 projection for
V4–V6 and version-7 projection for V7–V11; the other categories on this
source retain version 3.
No mirror field or ordinal changes.

Version 6 adds exactly two closed `kind: "table"` controls to the partial
Hardware Config > Antenna / ALEX page: `hardware.antenna.txRows` and
`hardware.antenna.rxRows`. Each has `binding.antennaRows` with
`object: "alexAntennas"` and mode `tx` or `rx`, and requires
`radioAntennaRowsVersion: 1`. The TX table also requires a fresh off-air
`txState`; the RX table has no added transmit-permission gate. The Core
omits both tables for a peer without the row feature and omits the whole
partial Hardware page on a board without Alex filters. V1–V5 peers retain
the scalar controls only. A description never grants an edit by itself.
For a peer that declared the row feature, the supported board's static table
shape remains described while its radio is disconnected. The live row
capability is then absent; current-session capability, radio identity, and
row-command checks must all allow an edit before a cell can be changed.

Each table has one row per antenna-list entry, in the lists' order: the
14 `Band` rows from 160m through XVTR, in enum order, then 2 m (band 27,
R-IOS-26) for a peer that declared `band2m` 1. Each row carries its band
number, so a row's list entry is its position, not its band number. A peer
without `band2m` is sent the 14 rows (station link section 6.1).
The TX columns are Ant 1/2/3 (`field: "tx"`). RX has three RX1 columns
(`field: "rx"`, labels 1/2/3) and three RX-only columns
(`field: "rxOnly"`, labels from the current Core SKU). The RX table's
required `columnGroups` are exactly RX1 over rx1/rx2/rx3 and RX-only over
rxOnly1/rxOnly2/rxOnly3; TX has no column groups. Each row's `cells` carry
the native button tooltips in column order. These are fixed display and
source facts, not a command-template language. The source is the existing
three 15-integer CSV mirrors `txAntennas`, `rxAntennas`, and
`rxOnlyAntennas` (14 for a peer without `band2m`, 2 m last); RX-only value 0 means no button selected. The blocked
TX-port mirrors disable columns 2 and 3. A phone checks all values,
capability, canonical connected MAC and session freshness together, then
uses only the existing `setAlexTxAntennaForRadio` or
`setAlexRxAntennaForRadio` typed verb for a clicked band/port. The Core
rechecks identity and authority and sends the accepted mirror or refusal.
The Setup description revision is not a per-row state revision.

Version 5 adds only two optional PA Values telemetry readouts. The closed
bindings are `{"telemetry":{"object":"radio","name":"paCurrentAmps"}}`
for `PA Current:` (two decimals, A) and
`{"telemetry":{"object":"radio","name":"supplyVolts"}}` for
`DC Voltage:` (one decimal, V). No other telemetry object or field is a
Setup binding.
Both require `stationTelemetryVersion:4`, send no writes, and have no
transmit-permission or off-air gate. The Core projects each row away when its
board lacks the corresponding amps or volts telemetry; the whole PA category
still requires an integrated PA and a non-RX-only SKU. Missing, malformed,
nonfinite, disconnected, or stale-session values are unavailable, while a
present zero is rendered as zero. The phone renderer must apply that freshness
rule to the existing optional station telemetry wire. V1–V4 projections omit
both rows; PA alone has a version-5 ceiling. This adds no peak/min/reset,
temperature conversion, derived formula, or Core command.

Version 4 publishes the partial Display category. Its station-scoped controls
cover Spectrum Defaults FFT/window/Hz-per-bin/FPS, Multimeter polling delay,
and TX Display FFT/window/panadapter/waterfall analyzer settings. The RX quartet
uses `applies: "subscription"`: it writes through the current session's settings
proxy and the existing endpoint subscription path picks up the changed values.
The meter and TX analyzer controls use `applies: "live"`; Core reloads their
running objects, and these keys are not RX subscription fields. All TX Display
controls require `txDisplayVersion: 2`. They have no transmit-permission or
off-air gate because the desktop changes display processing while on air.
Absent, malformed, stale, or unavailable settings disable their controls.

Version 8 appends seven phone-owned RX subscription controls to Display: four
in Spectrum Defaults > Rendering (Spectrum Detector, Spectrum Averaging,
Spectrum Avg Time, Decimation) and three in the new partial Waterfall Defaults
> Display page (WF Detector, WF Averaging, WF Avg Time). The new controls use
only closed `binding.phone` dispatch identities for the phone's existing
per-pan typed settings. Six identities match the desktop's existing display
names; Decimation uses the literal `decimation` field, which has no desktop
settings key. These descriptors do not create Core or phone settings keys or
grant permission to write Core settings. Detector options are numbered 0–4
for spectrum (Peak, Rosenfell, Average, Sample, RMS) and 0–3 for waterfall
(no RMS), both default 0. Averaging options are numbered 0–3 (None,
Recursive, Time Window, Log Recursive), default 3 for spectrum and 0 for
waterfall. Both averaging times span 10–9999 ms in steps of 10, default 30
and 120 ms. Decimation spans 1–16 in steps of 1, default 1. Choices use
exact integer `{value,label}` options. Each control has
`requiresDescriptionVersion:8` and `applies:"subscription"`; detectors gate
on `remoteMediaVersion:1`, averaging and times on `displayExtrasVersion:1`,
and Decimation on `spectrumGrantVersion:2`. These capability gates are
necessary but do not replace the phone's current-session media availability
checks. Versions 1–7 retain their original 11/14-control Display shape and
version numbers (1–3, then 4), and omit the new Waterfall page. Native rendering
behavior is unchanged; phone
parsing and rendering require their own implementation.

Version 9 appends eight phone-owned rendering controls to the existing partial
Display pages, in native order after the V8 RX rows: Spectrum Defaults >
Rendering has Fill under trace, Fill Alpha, Trace gradient, Peak hold, and Peak
Delay; Waterfall Defaults > Display has Update Period, Stop on TX, and Opacity.
Their closed `binding.phone` identities are the desktop's existing local keys:
`DisplayPanFill`, `DisplayFftFillAlpha`, `DisplayGradientEnabled`,
`DisplayPeakHoldEnabled`, `DisplayPeakHoldResetMs`, `DisplayWfUpdatePeriodMs`,
`WaterfallStopOnTx`, and `DisplayWfOpacity`. These are phone-owned per-pan
values, not Core settings writes or new persistence keys. The four toggles
have native defaults true, false, false, false in that order. Fill Alpha is
an integer percent 0–100, step 1, default 70; the phone's existing typed
Double stores that percent divided by 100. Peak Delay is 100–10000 ms, step
100, default 2000. Update Period is 10–500 ms, step 1, default 30; this is
the one row with `applies:"subscription"` because the phone's existing
subscriber derives frames per line from it. Opacity is integer percent
0–100, step 1, default 100. The other seven have `applies:"live"` and
affect phone rendering only. No new media field, setting, authority, or TX
verb is defined. All eight require description version 9. V1–V8 projection
retains its prior control counts and version, and V9 has 29 Display controls
on the same four partial pages. The desktop renderer and native UI behavior
remain unchanged. Phone generic V9 parsing and dispatch require separate
phone work; its existing typed fields do not by themselves consume this
description.

Version 10 appends four phone-owned Waterfall Defaults > Overlays toggles in
native order: Show RX filter on waterfall, Show TX filter on RX waterfall,
Show RX zero line on waterfall, and Show TX zero line on waterfall. Their
`binding.phone` identities are `DisplayShowRxFilterOnWaterfall`,
`DisplayShowTxFilterOnRxWaterfall`, `DisplayShowRxZeroLine`, and
`DisplayShowTxZeroLine`. All four use `applies:"live"` and require description
version 10. Their defaults are false, true, false, false; the TX filter's
true default comes from the desktop renderer's persisted-load path, not its
pre-load C++ member initializer. They remain per-pan operator-local renderer
preferences, with no Core setting write, RX subscription field or new wire
verb. V1–V9 projections retain their previous version numbers and 11/14/21/29
control counts; V10 has 33 controls on the same four partial pages. The
phone's typed settings and render paths already exist, but V10 descriptor
parsing/dispatch and its TX-filter default correction are separately owned.

Version 11 adds the Spectrum Peaks page (`display.spectrumPeaks`,
`where:"phone"`, `coverage:"partial"`) between Spectrum Defaults and
Waterfall Defaults, with all fifteen of its rows in native order. Active
Peak Hold has Enable per-bin peak trace with decay (`activePeakHold`,
`DisplayActivePeakHoldEnabled`, false), Hold duration (`activePeakHoldTime`,
`DisplayActivePeakHoldDurationMs`, integer 100–60000 ms, step 100, default
2000), Drop rate (`activePeakHoldDropRate`,
`DisplayActivePeakHoldDropDbPerSec`, integer 1–60 dB/s, step 1, default 6),
Fill area between peak trace and current trace (`activePeakHoldFill`,
`DisplayActivePeakHoldFill`, false), Update during TX (`activePeakHoldOnTx`,
`DisplayActivePeakHoldOnTx`, false) and Trace color (`activePeakHoldColor`,
`DisplayActivePeakHoldColor`, `#FFD700FF`). Peak Blobs has Show top-N peak
markers (`peakBlobs`, `DisplayPeakBlobsEnabled`, false), Number of peaks
(`peakBlobCount`, `DisplayPeakBlobsCount`, integer 1–20, step 1, default 3),
Only show peaks inside the RX filter passband (`peakBlobInsideFilter`,
`DisplayPeakBlobsInsideFilterOnly`, false), Hold peaks before decay
(`peakBlobHold`, `DisplayPeakBlobsHoldEnabled`, false), Hold duration
(`peakBlobHoldTime`, `DisplayPeakBlobsHoldMs`, integer 100–60000 ms, step
100, default 500), Decay after hold (`peakBlobHoldDrop`,
`DisplayPeakBlobsHoldDrop`, false), Fall rate (`peakBlobFallRate`,
`DisplayPeakBlobsFallDbPerSec`, integer 1–60 dB/s, step 1, default 6), Blob
color (`peakBlobColor`, `DisplayPeakBlobColor`, `#FF4500FF`) and Text color
(`peakBlobTextColor`, `DisplayPeakBlobTextColor`, `#7FFF00FF`). Each id is
prefixed `display.spectrumPeaks.`; each `binding.phone` is the desktop's own
key. The desktop stores these keys once for every pan, and a Setup change
reaches every pan at once. The desktop draws the peak hold trace and the
blobs from the frames it already has; the phone gets the same computation
from the Core's display extras `activePeakHold` and `peakBlobs`
subscription fields. The eleven rows that feed those fields use
`applies:"subscription"`; the fill and the three colors only change the
phone's drawing and use `applies:"live"`. Every row requires
`displayExtrasVersion:1` except Hold duration and Update during TX, which
require 3: the Core that holds each peak for `holdMs` and honors
`activePeakHold.onTx`. The blob hold, drop and fall rows reach the Core as
the existing `holdMs` (0 when the hold is off) and `fallDbPerSec` (0 when
decay after hold is off) numbers, which the Core turns back into the
desktop's switches. Colors are eight-digit `#RRGGBBAA` strings as in
Appearance. No new Core setting or wire verb is defined. V1–V10
projections retain their prior version numbers, page lists and
11/14/21/29/33 control counts; V11 has 48 Display controls on five partial
pages. Phone parsing and dispatch of V11 are separately owned.

Version 12 describes the rest of Setup > Display after Spectrum Peaks, and
Appearance's Reset all colors. Every new row has
`requiresDescriptionVersion:12` and a `binding.phone` that is the desktop's
own key or, for a button, an action identity; the Core accepts each row only
as the exact closed object it publishes. V1–V11 projections keep their
versions, page lists and control counts (Display V11: 48 controls on five
pages; Appearance V7: 13 controls); V12 has 100 Display controls on seven
pages and 14 Appearance controls.

Display pages, in the desktop's order: Spectrum Defaults gains a Profile
section (Reset to Smooth Defaults, Enable Clarity) before Fast Fourier
Transform and a Spectrum Overlays section after Rendering (Show cursor
frequency, Show bin width, Show noise floor, NF shift, NF line width, NF line,
text and fast-attack colours, Normalize trace, Show peak value overlay, its position and
refresh, Get Monitor Hz). Waterfall Defaults gains Levels (High and Low
Threshold, AGC, Use spectrum min/max, Copy spectrum min/max) and Waterfall
NF-AGC (Enable, NF offset) before Display, Color Scheme at the end of
Display, and Rewind history (Depth) and Time (Timestamp Position and Mode)
after Overlays. The new Grid & Scales page (`display.gridScales`) has Grid
(Show grid, Show dBm scale strip, dB Max and dB Min per band, dB Step),
Labels (Freq Label Align, Show zero line, Show FPS overlay), Noise-Floor
Tracking (Adjust grid min, NF offset, Maintain grid range) and Copy (Copy
waterfall thresholds). Multimeter gains Show decimal point, Signal Units
(Display units) and Signal History (History duration). TX Display gains
Waterfall Amplitude Scale (Low and High Level, Palette, Low Color). The new
3D View page (`display.threeD`) has one 3D VIEW section: Reset 3D, Spectrum
(2D or 3D), 3D Floor per band, 3D Gain, Span, Angle and Slice Shadow. The two
new pages describe every control they have and carry no `coverage`. The
desktop's defaults, ranges and labels are the published ones; the ids, keys,
defaults and ranges are listed in `resources/setup/display.json`.

Rows that feed the display extras subscription use `applies:"subscription"`
and gate on `displayExtrasVersion:1`: Enable Clarity, the waterfall
thresholds, AGC, Use spectrum min/max, NF-AGC and its offset (together the
`waterfallLevels` field: `clarity` when Clarity is on, else `noiseFloorAgc`
when NF-AGC is on, else `agc` when AGC is on, else `manual`; low and high are
the thresholds, or the pan's spectrum bottom and top with Use spectrum
min/max; offset is the NF offset), Show noise floor and NF shift (the
`noiseFloor` field) and Normalize trace (`normalize`, sent true only while
the spectrum detector is Average, Sample or RMS; the Core applies it only
then too). The NF line width and line and text colours draw the extras'
noise floor and gate on the same capability; the fast-attack colour needs
the noise floor state (`noiseFloor.fastAttack`) and gates on
`displayExtrasVersion:4`. The 3D Spectrum choice sets the subscription's
`wideSpanFactor` and gates on `remoteMediaVersion:1`. The Grid & Scales
noise-floor tracking rows follow the pan's display noise floor as Thetis
does (every 500 ms, not while transmitting, the extras' noise floor while
its state is not fast attack), or Clarity's estimate from the Core's
`noise-floor` operation while Clarity is on; they gate on
`displayExtrasVersion:4`. The TX
Display waterfall rows colour the transmit display and gate on
`txDisplayVersion:1`. Every other row changes only the phone's drawing,
`applies:"live"`, with no gate.

Three closed fields are new in version 12. `enabledWhen:{"phone":<key>,
"oneOf":[<values>]}` enables a row only while the named row of this
description holds one of the values (Normalize: spectrum detector 2, 3 or 4;
the waterfall thresholds, AGC and NF-AGC rows: Use spectrum min/max false;
the grid NF offset and Maintain grid range: Adjust grid min true); the
desktop disables the same rows. `perBand:{"label":<template>}` marks a row
the desktop keeps once per band: the value is stored under
`<binding.phone>_<band>` for the pan's current band (160m, 80m, 60m, 40m,
30m, 20m, 17m, 15m, 12m, 10m, 6m, GEN, WWV or XVTR; the band of the pan's
centre frequency as `Band::bandFromFrequency` finds it), shared by every pan,
and the label shows the band where the template has `%1`. `confirm` is the
desktop's exact question before a destructive action: the renderer asks it
with Yes and No, No the default, and acts only on Yes.

A `kind:"button"` row with a `binding.phone` is a phone action. It has no
value or default and acts on the pan the page is editing, exactly as the
desktop's button acts on its active pan, in a local or a remote window:
`smoothDefaults` sets Color Scheme to Clarity Blue (7), Spectrum Averaging to
Log Recursive (3), Trace & Fill Color to `#FFFFFFE6`, Fill under trace off,
waterfall AGC on and Update Period 30 ms; `getMonitorHz` sets FPS to the
screen's refresh rate rounded and held to 10–60, as an edit of the FPS row
(the same availability); `copySpectrumMinMax` sets High Threshold to the
pan's spectrum top and Low Threshold to its bottom; `copyWaterfallThresholds`
sets the current band's dB Max and dB Min to the waterfall High and Low
Threshold, rounded; `reset3d` sets the six 3D rows to their defaults (3D
Floor for the current band); `resetColors` sets the ten Colors & Theme
swatches to their defaults. No action writes a Core setting except
`getMonitorHz` through the FPS row.

Not described, with the reason: Line Width (the phone has its own line
width, D75), Cal Offset and Display Thread Priority (the Core calibrates and
schedules the display; a remote window disables both), the noise floor text
position (disabled on the desktop, no effect), the TX Custom Gradient (no
gradient editor kind), the Waterfall Low Level Color (unbuilt) and the
Multimeter peak hold, text hold, averaging window, digital delay and history
enable (unbuilt). Derived readouts (bin width, delay, effective rewind) and
the cross-links are not controls.

Version 13 describes the rest of Setup > PA's Watt Meter and PA Values
pages, three built Hardware Config tabs, the Alex-1 and Alex-2 Filters tabs'
receive rows, Radio Info's sample rate and Transmit > Power's Disable HF PA
(R-R3-46, R-R3-49, R-IOS-18). Every new row
has `requiresDescriptionVersion:13` and the Core accepts it only as the exact
closed object its resource carries; V1–V12 projections keep their versions,
page lists and controls (PA V5–V12: version 5, two pages on the ANAN-G2E, one
elsewhere; Hardware V6–V12: version 6, Antenna / ALEX only, and no Hardware
category at all on a board without ALEX filters).

PA pages, in the desktop's order: PA Gain (unchanged), Watt Meter
(`pa.wattMeter`, new, `where:"mixed"`, every control described), PA Values.
The Watt Meter's PA Forward Power Calibration section has the board class's
ten points (`pa.wattMeter.calPoint1` to `calPoint10`), then a PA Values
section with Show PA Values page and Reset PA Values. PA Values gains PA
Temperature after PA Current, ADC Overload after REV Voltage, and a Reset
section with Reset Peak/Min.

A calibration point is `kind:"decimal"` with the closed binding
`{"radioSetting":"paCalibration/calPoint<N>"}`. The Core projects each for
its radio's model: the label is the point's factory value (`"10 W"`), which is
also its `default`; `min` 0, the point's own `max` (Thetis's box for that
point: ANAN-10 class and the HL2 10 W except 11 and 12 W for points 9 and 10,
ANAN-100 class 100 W except 110 and 120 W, ANAN-8000 class 100, 100, 100,
120, 140, 200, 200, 200, 220 and 240 W), `step` 0.1, `decimals` 1, `unit`
`W`, and `boardClass` (1 ANAN-10, 2 ANAN-100, 3 ANAN-8000). A model without a
class removes the ten points. The gate is `transmitSettingsVersion:6` plus
`offAir:true`, the desktop's own gate for this table.

`binding.radioSetting` is new in version 13: a key under the connected
radio. The renderer reads and writes `hardware/<MAC>/<radioSetting>` through
the current session's settings proxy, where `<MAC>` is the canonical MAC of
the Core's connected radio in the current session (the MAC antenna rows and
Settings Validation use). A missing, stale or other-radio MAC disables the
control; the Core refuses another radio's key. Values are the stored strings
(decimals as numbers written with `.`, toggles with their `valueEncoding`).
A point reads as its stored `calPoint<N>` while
`hardware/<MAC>/paCalibration/boardClass` reads as the row's `boardClass`,
and as its `default` while that key is absent or `0`; another class disables
the point with a plain reason (the table was saved for another model). An
edit first writes `boardClass` when it is absent or `0`, then the point. The
Core applies the table at once, and while the radio transmits it refuses the
write and hands back its value (the settings proxy's off-air rule for these
keys, as for the desktop's). A value outside the row's range, a
non-number, or a `boardClass` other than the Core's radio's is refused whole
with the range in plain words (for example "Choose a calibration point from
0 to 10 W."), and the Core hands back its value; nothing is clamped.

`pa.values.paTemperature` is a readout of the optional station telemetry
`{"telemetry":{"object":"radio","name":"paTemperatureCelsius"}}`, one decimal,
`unit` `°C`, gate `stationTelemetryVersion:4`, with the closed field
`"temperatureUnit":"PaTempUnit"`: the renderer shows it in the viewer's own
PA temperature unit (the desktop's C/F toggle, `PaTempUnit` `C` or `F`,
default `C`), converting °F = °C × 9 / 5 + 32. `pa.values.adcOverload` has the
closed binding `{"adcOverload":{"object":"stepAtt"}}` and gate
`radioHardwareVersion:1`: it reads the `stepAtt` mirror's `overloadAdc0` and
`overloadAdc1` (0 none, 1 or 2 overloaded) and shows `Yes (ADC 0)` while ADC 0
is overloaded, else `Yes (ADC 1)` while ADC 1 is, else `No`, as a remote
desktop window does; a missing or stale object is unavailable. It has no
`decimals`.

Reset Peak/Min (`pa.values.resetPeakMin`) and Reset PA Values
(`pa.wattMeter.resetPaValues`) are the one phone action `resetPaValues`.
While a version 13 renderer shows PA Values it tracks, from the first change
after the page opens, the running peak and minimum of Forward (calibrated),
Reflected, SWR, PA Current, PA Temperature and DC Voltage, and shows a row as
the desktop does: the value, then `  (P <peak> / M <minimum>)` at the row's
decimals once the two differ (temperature in the viewer's unit). The action
restarts each tracker at its current value. It is the viewer's own and
reaches no other device. Show PA Values page (`pa.wattMeter.showPaValues`)
is a phone toggle, `binding.phone` `display/showPaValuesPage`, default true:
off hides the PA Values page from the viewer's own Setup, as the desktop
hides its PA Values page.

Hardware Config pages, in the desktop's order: Radio Info
(`hardware.radioInfo`, new, `where:"mixed"`, partial), Antenna / ALEX
(unchanged, only with ALEX filters), Calibration (`hardware.calibration`,
new, partial) and HL2 I/O (`hardware.hl2Io`, new, partial, only on the HL2's
I/O board). Radio Info's Board Identity section has seven readouts with the
closed binding `{"radioInfo":<field>}` (`board`, `protocol`, `adcCount`,
`maxRx`, `firmware`, `mac`, `ip`) and a `value` the Core fills with the
desktop tab's exact text for its current radio (a dash, U+2014, for a value
the radio has not reported); the description changes, and its revision
advances, when the Core's radio does. Its Support section has Copy Support
Info to Clipboard, phone action `copySupportInfo`, whose `copyText` the Core
fills with the tab's exact clipboard text; the action copies it on the
viewer's own device. Calibration's TX Display Cal section has Offset
(`hardware.calibration.txDisplayOffset`, `radioSetting` `cal/txDisplayOffset`,
-100 to 100 dB, step 0.1, one decimal, default 0), gate
`transmitSettingsVersion:8` and no off-air gate: the Core takes it on the air
too, as Thetis changes it while transmitting. HL2 I/O's Configuration section
has Enable N2ADR Filter board (`hardware.hl2Io.n2adrFilter`, `radioSetting`
`hl2IoBoard/n2adrFilter`, `True`/`False`, default true), gate
`transmitSettingsVersion:8`; the Core applies its whole preset once the
radio is back on receive.

Radio Info gains an Operating Parameters section between Board Identity and
Support with Sample rate (`hardware.radioInfo.sampleRate`, `kind:"choice"`).
Its binding is the command `setRadioSampleRate {rateHz: $controlValue}`
with `valueProperty` `slice:active`'s `sampleRateHz`, gate
`radioHardwareVersion:9` plus `offAir:true`: the whole radio's rate, every
receiver at once, as the desktop's box changes it. The Core fills `options`
with the rates the desktop's box lists for its radio (`value` the rate in
hertz, `label` its digits); with none it adds `availability` disabled with
"The radio is not connected, so its sample rate cannot change." The Core
refuses the verb from a device signed in with the pairing token, while the
radio is on the air, and for a rate outside the list.

Two pages follow Antenna / ALEX, both `where:"station"` and partial:
Alex-1 Filters (`hardware.alex1Filters`, on every board with ALEX filters)
and Alex-2 Filters (`hardware.alex2Filters`, only where the board has
Alex-2). Alex-1 Filters has Alex HPF Bands and, on Saturn, Saturn MkII and
the ANAN-G2E only (the desktop's own gate), Saturn BPF1 Bands; Alex-2
Filters has Alex-2 HPF Bands, whose first row is ByPass / 55 MHz BPF
(master) (`hardware.alex2Filters.bypass55MhzBpf`, `radioSetting`
`alex2/master/bypass55MhzBpf`). Each bank has six rows in the desktop's
order, each row three controls with ids `<page>.<bank>.<slug>.bypass`,
`.start` and `.end` (bank `hpf` or `bpf1`; slugs `1_5MHz`, `6_5MHz`,
`9_5MHz`, `13MHz`, `20MHz`, `6mBP`), labelled with the desktop's row label
and Bypass, Start or End, and bound to `radioSetting`
`<prefix>/<slug>/enabled|start|end` (prefix `alex/hpf`, `alex/bpf1` or
`alex2/hpf`). Bypass boxes are `True`/`False` toggles, default false; the
edges are decimals from 0 to 200 MHz, step 0.001, six decimals, unit `MHz`,
defaulting to Thetis's spinner values. Every row has the gate
`radioHardwareVersion:8` and no off-air rule: the Core applies a change to
its radio at once, on or off the air, as Thetis's setters do.

Alex HPF Bands opens with the tab's five switches above its rows, in the
desktop's order: HPF Bypass (master), HPF Bypass on TX, HPF Bypass on
PureSignal feedback, Disable 6m LNA on TX and Disable 6m LNA on RX (ids
`hardware.alex1Filters.hpfBypass`, `.hpfBypassOnTx`, `.hpfBypassOnPs`,
`.disable6mLnaOnTx`, `.disable6mLnaOnRx`; `radioSetting`
`alex/master/<the same name>`). Each is a `True`/`False` toggle with an
empty tooltip, defaulting as the desktop does (on PureSignal feedback and
6 m LNA on TX checked, the other three clear), gate
`radioHardwareVersion:8` and no off-air rule. The three the Core counts as
transmit hardware (on TX, on PureSignal feedback, 6 m LNA on TX) add
`transmit:true`: with remote transmit allowed, the Core takes a write of them
only from a session permitted to transmit. The desktop asks before clearing
HPF Bypass on PureSignal feedback (the IMD warning); the description carries
no such confirmation, so a phone clears it at once.

Transmit > Power gains a PA Control section last with Disable HF PA
(`transmit.power.DisableHfPa`, `setting` `DisableHfPa`, `True`/`False`,
default false, gate `transmitSettingsVersion:11` plus `transmit:true` and
no off-air rule: the Core applies it on and off the air, as Thetis does).
On the Hermes and the Atlas/Metis kit, which have no such switch, the row
carries `availability` disabled with "This radio cannot switch off its HF
PA from here."

Not described, with the reason: PA Gain's profile choice, New, Copy, Delete,
Reset Defaults, per-band gain, drive-step adjusts and max power (the profile
bank is serialized per profile and no closed profile state or command exists
on the wire yet); New Cal (hidden on the desktop, as in Thetis); the
auto-calibration sweep (it keys the radio from the desktop's own window);
the ANAN-8000DLE title bar volts/amps box (the desktop's title bar); both
Alex Filters tabs' LPF edges (hidden on the desktop too: the Core does not
apply them yet, as nothing selects the transmit low-pass from them); OC Outputs; the rest of Calibration (frequency and level calibration,
6 m LNA offsets, the correction factors, Volts/Amps calibration and its log);
HL2 Options; and the rest of HL2 I/O (register, state machine, I2C and
bandwidth monitor views, probe and reset). No new wire field, verb or
capability value is defined.

Version 14 describes PA Gain's profiles (R-R3-49, R-IOS-18). The page
`pa.gain` now carries, on every radio with a PA, a Profile section (the
profile choice, New, Copy, Delete, Reset Defaults) and a PA Gain by Band
(dB) section with one table; the ANAN-G2E's bypass box stays last and is
still the G2E's only. Every new row has `requiresDescriptionVersion:14`,
the gate `{"capability":"paProfileVersion","min":1,"offAir":true}`, and is
accepted only as the exact closed object in `resources/setup/pa.json`.
V13 and older keep their projections (PA Gain appears there only on the
G2E, with the bypass box alone).

The data is the Core's read-only `paProfiles` object (link document,
paProfileVersion 1): `json` holds `names` (the profiles the desktop's combo
lists: this radio's factory profile and every user profile), `active`,
`factory` and `bands`, the active profile's 14 rows in Band order, each
`{band, gain, adjust[9], maxPower, useMax}` at one decimal place. The
closed bindings are:

- `{"paProfile":"active"}` on the `choice`: its options are `names`, its
  value `active`; a pick sends `paProfile.select {name}`.
- `{"paProfile":"new"}`, `"copy"`, `"delete"`, `"reset"` on the buttons.
  New and Copy carry `prompt` `{title, label, default}` (Copy's default is
  `"%1 (copy)"`, `%1` the active profile's name); the renderer asks for a
  name and sends `paProfile.new {name}` or `paProfile.copy {name}`. Delete
  and Reset Defaults carry the desktop's `confirm` (`%1` the active name)
  and send `paProfile.delete {name: active}` or `paProfile.reset {}` on
  Yes.
- `{"paProfileGrid":{"object":"paProfiles"}}` on the `table`: `rows` are
  the 14 bands (`band` 0..13 and label), `columns` the Gain (dB) column
  (38.8..100 dB), the nine drive-step adjusts (`driveStep` 0..8, labels
  10%..90%, -10..10 dB), Max W (0..1500 W), each a decimal of step 0.1 and
  one decimal place, and Use Max (a toggle). A column's `tooltip` has `%1`
  for the row's band. A cell edit sends `paProfile.setGain {band, value}`,
  `paProfile.setAdjust {band, step, value}`, `paProfile.setMaxPower {band,
  value}` or `paProfile.setUseMax {band, on}`.

The Core does what the desktop's page does: select (only a listed
profile), New (a profile seeded from this radio's factory row; names must
not start with "Default" or repeat one there), Copy (the active profile's
values under a new name), Delete (never the last one; the radio's factory
profile is then selected, as Thetis does), Reset Defaults (the active
profile's factory values for its model), and the cell edits, each rounded
to one decimal place and refused whole outside its column's range. Every
verb is refused while the radio is on the air, and meets the gates the
Core gives the desktop's own PA profile writes: a receive-only Core takes
it from a peer offered transmit settings, and with remote transmit
allowed only from a device that may transmit. A peer must declare
`paProfiles:1` to get the object, the capability and the verbs.

V4 adds `default` metadata to these exact Display and Appearance controls.
Display toggles use JSON booleans; its numeric controls use JSON numbers,
with choice defaults as integer ordinals and FFT option defaults as their
actual integer values. Appearance color defaults are eight-digit
`#RRGGBBAA` strings. Display's Hz/bin `kind:"decimal"` has `decimals:2` in
V4. For V1–V3 peers, the Core strips `default` from all Display and Appearance
controls and strips `decimals` from `kind:"decimal"`; it retains the controls,
their older bindings and gates, and all existing `kind:"readout"` decimals in
other categories. This is a closed extension of those two published
categories, not permission to add arbitrary default or precision fields.

The two FFT-size controls are the only V4 `kind: "slider"` controls with an
`options` array. Each option is exactly `{ "value": 4096 * 2^index,
"label": "<that decimal value>" }` for indices 0 through 6, in ascending
order. Their `default` is the stored FFT size (RX 4096; TX 32768), not a
slider index. They have no `min`, `max`, or `step`. A view selects an option by
index and writes its decimal `value` through the current settings proxy.
Malformed, reordered, duplicate, unexpected, or empty options are rejected by
Core validation. V1–V3 projections omit both FFT-size controls.

TX Panadapter Normalize is also V4-only. Its one closed dependency is
`"enabledWhen":{"setting":"DisplayTxPanDetector","oneOf":["2","3","4"]}`.
The renderer reads that detector value from the same current-session settings
proxy; missing, malformed, stale, or other values disable Normalize. A change
to Average, Sample, or RMS enables it, and a change back to Peak or Rosenfell
disables it. Core rejects any altered or extended dependency envelope.
Normalize is stored with the existing exact `True`/`False` setting encoding.
V1–V3 omit it. The other Display controls retain their existing numeric ranges
or ordinal string choices. The category and its pages remain partial;
local colors, palettes, per-band tables, derived readouts, and actions are not
described by the V4 slice. V9 adds the eight renderer rows described above.

The phone uses a current-session settings snapshot and the current canonical
radio MAC. Re-validate sends only that MAC to `station.validateSettings`;
Forget sends only that MAC to `station.forgetSettings`. Replies must match
the request ID, session and MAC. `SettingsHygieneWire` accepts exactly two
typed values (`mac`, `issuesJson`), up to 32 issues and 64 KiB of compact
JSON with bounded fields. Render severity, summary and detail as plain text;
missing, malformed, stale or refused replies are unavailable, never an empty
healthy list. Revalidation is coalesced or serialized. Forget requires a
paired-device key, is disabled while on air or busy, and starts with a
default-cancel confirmation. After confirmation the client rechecks its
session, MAC, pairing and on-air state; the Core checks authority again on
dispatch. `fixActionId` is diagnostic text and never auto-executed. Reset
remains disabled because its operator-facing semantics are unsettled. The
panel does not describe Export / Import or Logs.

On the ANAN-G2E only, the partial `PA Gain` page also describes the existing
`transmit.paSettingsBypass` Boolean toggle. The Core projects that page away
unless its board capabilities include both an integrated PA and
`showsBypassPaSettingsUi`, and the SKU is not RX-only. Its gate is
`transmitSettingsVersion:6` plus `offAir:true`, with no `transmit:true` gate:
the Core already permits negotiated transmit *settings* on a receive-only
station while it is off the air. The Core still applies its own property-write
authority and on-air checks. This one toggle does not describe the PA profile
grid, calibration, or auto-calibration sweep.

`gate.offAir: true` requires a live txState with keyed, tuning, twoTone and
txEnding all false. Missing or stale state disables the control; a renderer
that cannot evaluate a gate disables that control rather than ignoring it.
This is independent of holding transmit: another idle holder can still trigger
the Core's existing shared confirmation. The Core rechecks the actual on-air
state when applying an edit and when proceeding with a confirmation.

For a command-backed control, `binding.command` contains `verb`, `arguments`
and optionally `valueProperty`. `valueProperty` names a mirrored property
that supplies the current control value; it is required when the command
does not have a writable property of its own. Every argument is either a
primitive literal or one declarative source object:

```json
{
  "binding": {"command": {
    "verb": "setStationTciOptions",
    "valueProperty": {"object": "stationTci", "name": "emulateExpertSdr3"},
    "arguments": {
      "emulateExpertSdr3": {"$controlValue": true},
      "emulateSunSdr2Pro": {"$property": {"object": "stationTci", "name": "emulateSunSdr2Pro"}},
      "cwluBecomesCw": {"$property": {"object": "stationTci", "name": "cwluBecomesCw"}},
      "sendInitialState": {"$property": {"object": "stationTci", "name": "sendInitialState"}}
    }
  }}
}
```

`{"$controlValue":true}` supplies the edited value with the control's
declared type. `{"$property":{"object":"…","name":"…"}}` supplies
the named mirrored property's current value. A source object has exactly
one marker; it cannot contain an expression or a fallback. The renderer
resolves every property from the same live session and epoch immediately
before dispatch. It checks the named capability and argument types. If a
reference is missing, stale, or has the wrong type, it disables the control
or refuses the edit with a plain reason; it never reuses an old value. The
station's existing command remains atomic. The Core validates the static
description shape, while each client resolves the runtime references.

For a per-receiver property, `object: "slice:active"` is the plan's dynamic
alias. The client resolves it to its currently selected, owned slice in the
current session and epoch at the start of an interaction, then writes that
concrete `slice:<id>` mirror object. If selection changes during a gesture,
the client cancels the pending edit instead of retargeting it. A missing,
retired, or no-longer-owned selected slice disables the control and refuses
the edit; there is no slice-zero or station-global active-slice fallback. The
Core continues to enforce ownership on every inbound write. The same alias
may be used in a command argument's `$property` reference, with the same
session and selection checks.

The Thetis-derived text and ranges in JSON are GPL material. Their upstream
header is preserved in `resources/setup/HEADERS.md`, with each file listed in
`docs/attribution/THETIS-PROVENANCE.md`.
