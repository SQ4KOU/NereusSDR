# Setup description versions 1–8

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
colors. The hidden Waterfall Low Color row and Reset Colors action remain
undescribed. Appearance's source category is V7; its RGBA defaults are sent
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
The existing whole-Setup on-air and session freshness lock still applies;
the description adds no permission or capability gate. VFO Small Filter,
Skins, and unused controls remain omitted. Phone rendering is owned by the
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
future declaration at version 8. Hardware has a version-6 ceiling, PA a
version-5 ceiling, Display a version-8 ceiling, and Appearance a version-7
ceiling with its prior version-4 projection for V4–V6; the other
categories on this source retain version 3.
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

Each table has the 14 `Band` rows from 160m through XVTR, in enum order.
The TX columns are Ant 1/2/3 (`field: "tx"`). RX has three RX1 columns
(`field: "rx"`, labels 1/2/3) and three RX-only columns
(`field: "rxOnly"`, labels from the current Core SKU). The RX table's
required `columnGroups` are exactly RX1 over rx1/rx2/rx3 and RX-only over
rxOnly1/rxOnly2/rxOnly3; TX has no column groups. Each row's `cells` carry
the native button tooltips in column order. These are fixed display and
source facts, not a command-template language. The source is the existing
three 14-integer CSV mirrors `txAntennas`, `rxAntennas`, and
`rxOnlyAntennas`; RX-only value 0 means no button selected. The blocked
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
or ordinal string choices. The category and all three pages remain partial;
local colors, palettes, per-band tables, derived readouts, and actions are not
described by this slice.

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
