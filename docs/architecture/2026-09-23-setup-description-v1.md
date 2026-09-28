# Setup description version 1

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
pages is sent as an empty string, not a page with empty controls. Task 43's
Diagnostics file is held this way until its local file and log actions have a
phone binding contract.

Control `kind` is one of `toggle`, `integer`, `decimal`, `slider`, `choice`,
`text`, `colour`, `button`, `readout` or `table`. Numeric controls carry
`min`, `max`, `step` and, where shown, `unit`. Choices have an ordered `choices`
array. A table has `rows`, `columns` and a cell kind. `tooltip` is the
desktop's exact text, including an empty string when the desktop has none.
Every control has exactly one `binding`: `setting` (an AppSettings key routed
by `classifySettingsKey`), `property` (a mirrored object and property),
`command` (a station verb), or `phone` (a key the phone keeps locally). A
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

An optional `decimals` field on a `kind:readout` control is an integer from 0
through 6. It formats a finite numeric mirrored value with that many decimal
places; the existing `unit` string follows the number (empty means no unit).
Missing, nonfinite, wrong-type, or stale-session values display unavailable,
never zero or a cached value from another session. Readouts send no edits,
including when the source property happens to be writable. A readout without
`decimals` keeps the renderer's prior behavior. The PA Values partial page
uses only five `txState` scalars with `txReadingsVersion:1`; no derived
formula, peak/min tracker, or reset action is implied by this field.
`setup.pa` is appended after `setup.revision` in the fixed mirror schema and
is empty on a board without an integrated PA or on an RX-only SKU. Its five
readouts remain visible while the radio transmits; they require neither
transmit permission nor an off-air gate. A peer without the negotiated
Setup-description feature receives no `setup` mirror object.

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
