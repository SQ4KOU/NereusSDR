# Setup description versions 1–5

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
`command` (a station verb), or `phone` (a key the phone keeps locally). The
version-3 Settings Validation panel instead has its one closed
`settingsHygiene` binding; it does not extend those generic binding rules. A
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
colours. The hidden Waterfall Low Color row, Reset Colors action, and Meter
Styles controls remain undescribed. Appearance's source category is V4 so its
RGBA defaults are sent only to V4+ peers; older projections retain all ten
colour controls without `default`. No station settings permission is needed.

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
These fields imply no derived formula, peak/min tracker, or reset action.
`setup.pa` is appended after `setup.revision` in the fixed mirror schema and
is empty on a board without an integrated PA or on an RX-only SKU. Its six
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
future declaration at version 5. Display and Appearance retain version-4
ceilings; PA has a version-5 ceiling, and other categories retain their
version-3 ceiling. No mirror field or ordinal changes.

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

V4 adds `default` metadata to these exact Display and Appearance controls.
Display toggles use JSON booleans; its numeric controls use JSON numbers,
with choice defaults as integer ordinals and FFT option defaults as their
actual integer values. Appearance colour defaults are eight-digit
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
local colours, palettes, per-band tables, derived readouts, and actions are not
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
