# Sample Rate Wiring — Design Spec (PR #35 / Plan C Phase 1)

**Status:** Design approved 2026-04-16
**Author:** JJ Boyd (KG4VCF), drafted with Claude Code
**Scope:** First PR under the Hardware Config refactor (Plan C). Wires the
RX1 sample rate combo and active RX count spinbox on `RadioInfoTab` into
the actual connection path. Fixes the single bug the user hit ("adjusting
sample rate has no effect") while establishing the AppSettings→RadioModel
bridge pattern that subsequent Plan C PRs will reuse for the other eight
Hardware Config tabs.

---

## 1. Context and motivation

`RadioInfoTab` exposes a sample-rate combo and an active-RX-count spinbox
under Hardware Config. As of 2026-04-16:

1. The combo emits `settingChanged("radioInfo/sampleRate", rate)` →
   `HardwarePage::onTabSettingChanged` writes it to AppSettings and stops.
   **Nothing reads it back.** The value lands in
   `hardware/<mac>/radioInfo/sampleRate` and stays there, inert.
2. The spinbox's `valueChanged` signal is **never connected**
   (`RadioInfoTab.cpp:74-97`). Restoration from AppSettings works but the
   user cannot change it.
3. `RadioModel::connectToRadio` hardcodes
   `wdspInputRate = isP1 ? 192000 : 768000` and
   `wdspInSize = isP1 ? 256 : 1024` (`RadioModel.cpp:203-204`), so the
   running rate is always the per-protocol max regardless of what the UI
   shows.
4. The `isP1 ? 768000` leg is also wrong for ANAN-G2 class radios, which
   Thetis offers 1536000 on as well (setup.cs:850).

This is the first of nine "hollow backend" tabs in Hardware Config — see
the 2026-04-16 wiring audit conversation for the full picture. Plan C
ships the sample-rate fix first, establishes the read-on-connect bridge
pattern, then decomposes the remaining work into per-tab follow-up PRs.

---

## 2. Thetis reference (source-verified)

Every decision in this spec is cited to Thetis source at
`C:/Users/boyds/Thetis/Project Files/Source/`. Agent-paraphrased claims
were verified first-hand after the initial audit.

| Fact | Source | Notes |
|---|---|---|
| P1 rate list = {48000, 96000, 192000} | setup.cs:849 | Base list for every non-REDPITAYA board |
| P1 rate list (REDPITAYA only) = {48000, 96000, 192000, 384000} | setup.cs:847-849 | `include_extra_p1_rate` flag |
| P2 rate list = {48000, 96000, 192000, 384000, 768000, 1536000} | setup.cs:850 | Every ETH board |
| Default selected rate = 192000 | setup.cs:866 | `Array.IndexOf(rates, 192000)` |
| Combo stores `int`, not string | setup.cs:856 `Items.Add(rate)` + 6990 `Int32.TryParse(Text, ...)` | |
| Buffer size formula = `64 * rate / 48000` | `ChannelMaster/cmsetup.c:106-111` `getbuffsize` | `base_size=64`, `base_rate=48000` |
| Live-apply sequence (ETH RX1) | setup.cs:6982-7075 | WDSP disable → mix remove → VAC disable → DDC disable → 20ms drain → rate set → buffer resize → DDC restart → 1ms wait → mix restore → VAC enable → WDSP enable → bin-width recompute |
| Live-apply sequence (USB RX1) | setup.cs:7076-7158 | Similar, uses `SendStopToMetis`/`SendStartToMetis`, forces RX2=RX1 |
| RX2 forced-equal on ETH for ANAN-10E/100B | setup.cs:7065-7073 | Single-ADC boards |
| RX2 forced-equal on USB (all boards) | setup.cs:7155-7156 | P1 constraint |
| `MaxRXFreq` is **not** sample-rate-related | setup.cs:14031 `udMaxFreq_ValueChanged` sets `console.MaxFreq` | VFO frequency clamp, separate feature |

CLAUDE.md currently says "hardware sample rate is radio-authoritative, do
NOT persist". Thetis persists the rate via `DB.SaveVarsDictionary("Options", ...)`
(setup.cs:1627) — not per-profile as earlier analysis claimed, but via
the global Options store. The CLAUDE.md rule does not match Thetis and
will be corrected in the same PR.

---

## 3. Goals and non-goals

### Goals

- The RX1 sample rate combo drives the actual wire rate used on the next
  radio connection.
- The active RX count spinbox drives the actual active receiver count on
  the next connection.
- Rate list shown in the combo matches Thetis: protocol-filtered, then
  intersected with `BoardCapabilities.sampleRates` for the connected
  board.
- Default rate (no persisted value) is 192000, matching Thetis.
- Values persist across app launches, scoped per-MAC so a user with
  multiple radios doesn't lose their per-radio selection.
- Users get honest feedback: changing the combo while connected shows a
  "reconnect to apply" banner; the banner clears itself when the new
  rate is actually live.
- The pattern established here — AppSettings→RadioModel bridge with
  per-MAC persistence and read-on-connect — is the template for the
  eight follow-up PRs that wire the other Hardware Config tabs.

### Non-goals (explicit out-of-scope for this PR)

- **Live-apply of rate changes.** Deferred to a follow-up PR. Current PR
  requires reconnect to apply. The live-apply primitive (`WdspEngine`
  teardown/rebuild, DDC disable/enable choreography matching Thetis
  setup.cs:6982-7075) is a cross-cutting feature used by several later
  Hardware Config PRs, so it gets its own focused PR.
- **RX2 combo becoming functional.** Phase 3F (multi-panadapter) has not
  shipped; there is no second WDSP channel to feed. RX2 combo is a
  disabled visual stub with a tooltip noting when it activates. The
  Thetis force-equal rules are captured in a comment so the 3F work
  doesn't re-invent them.
- **Dither / Random controls.** Thetis exposes these on the same sub-tab
  (setup.cs:23328-23351 `chkMercRandom` / `chkMercDither`) but they
  route through `NetworkIO.SetADCDither` / `SetADCRandom` — new
  protocol compose paths that don't exist in our connection classes.
  Belongs in its own Plan C tracked PR.
- **MaxRXFreq.** Not sample-rate-related. Belongs with VFO/XVTR work.
- **The other seven Hardware Config tabs.** Alex/OC/XVTR/PureSignal/
  Diversity/PA/HL2/Bandwidth — one tracked issue per tab under Plan C.

---

## 4. UI design (RadioInfoTab)

Replace the existing single sample-rate combo with two combos, keep the
activeRxCount spinbox, and add an inline reconnect banner.

```
┌─ Operating Params ───────────────────────────────┐
│ Sample rate RX1:  [192000 ▼]                     │
│ Sample rate RX2:  [192000 ▼]  (disabled)         │
│   tooltip: "Enabled when Phase 3F multi-         │
│            panadapter support lands."            │
│ Active RX count:  [1 ▲▼]                         │
│ ┌─ (shown only when pending) ──────────────────┐ │
│ │ ⚠ Reconnect to apply new sample rate         │ │
│ │   (pending: 384 kHz)                         │ │
│ └──────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
```

- **RX1 combo (`m_sampleRateRx1Combo`)**: populated from
  `allowedSampleRates(proto, caps)` in `populate()`. Enabled whenever a
  radio is connected. Emits `settingChanged("radioInfo/sampleRate", rate)`
  on user change.
- **RX2 combo (`m_sampleRateRx2Combo`)**: always disabled in PR #35.
  Value tracks RX1 visually. Carries a comment citing setup.cs:7065-7073
  and setup.cs:7155-7156 recording the Thetis force-equal rules (all P1
  boards force RX2=RX1 always; P2 ANAN-10E/100B force RX2=RX1; other P2
  boards allow independent). When Phase 3F enables the combo, those
  rules are already documented at the callsite.
- **Active RX count spin (`m_activeRxSpin`)**: range `1..caps.maxReceivers`.
  Fix the existing bug by wiring
  `connect(m_activeRxSpin, QSpinBox::valueChanged, this, &RadioInfoTab::onActiveRxCountChanged)`
  and emitting `settingChanged("radioInfo/activeRxCount", count)`.
- **Banner (`m_reconnectBanner`)**: hidden by default. Shows when the
  connected radio's active wire rate differs from the persisted
  `radioInfo/sampleRate` for that MAC. Updates on combo change (persisted
  value now differs from live) and on `RadioModel::wireSampleRateChanged`
  (live rate caught up to persisted — banner hides). Visual: inline
  `QLabel` styled like existing status labels in `SetupDialog`; no
  modal, no toast. Commented `// Removed when live-apply lands` so the
  follow-up PR can strip it cleanly.

---

## 5. Rate catalog (`src/core/SampleRateCatalog.h/.cpp`)

New header-only-ish module exposing:

```cpp
namespace NereusSDR {
std::vector<int> allowedSampleRates(ProtocolVersion proto,
                                     const BoardCapabilities& caps);
int defaultSampleRate(ProtocolVersion proto,
                       const BoardCapabilities& caps);
}
```

Constants (all cited to Thetis):

- `kP1RatesBase = {48000, 96000, 192000}` — setup.cs:849.
- `kP1RatesRedPitaya = {48000, 96000, 192000, 384000}` — setup.cs:847-849.
- `kP2Rates = {48000, 96000, 192000, 384000, 768000, 1536000}` — setup.cs:850.
- `kDefaultRate = 192000` — setup.cs:866.

`allowedSampleRates` implementation:

1. Pick the protocol list: `proto == Protocol1 ? (isRedPitaya(caps) ? kP1RatesRedPitaya : kP1RatesBase) : kP2Rates`.
2. Intersect with `caps.sampleRates` (skip zero-slot sentinels).
3. Return ascending.

`defaultSampleRate` returns `kDefaultRate` if present in the allowed
list for this proto/caps, otherwise the first allowed entry (paranoia
fallback; no current board fails this).

`isRedPitaya(caps)` checks `caps.boardId == HardwareModel::RedPitaya` (or
whichever BoardCapabilities field identifies it — spec assumes this
exists; implementation plan verifies).

---

## 6. Data flow

### 6.1 User changes combo (live, connected)

```
RadioInfoTab::onRx1SampleRateChanged(idx)
  → emit settingChanged("radioInfo/sampleRate", rate)
HardwarePage::onTabSettingChanged("radioInfo", "sampleRate", rate)
  → AppSettings::setHardwareValue(mac, "radioInfo/sampleRate", rate)
  → AppSettings::save()
  → AppSettings::hardwareSettingChanged(mac, key, value)   [NEW signal]
RadioInfoTab::onHardwareSettingChanged(mac, key, value)
  → if mac matches current and key is "radioInfo/sampleRate":
      updateReconnectBanner()
```

### 6.2 User clicks Connect

```
RadioModel::connectToRadio(info)
  → const QString mac = info.macAddress;
  → const auto& caps = *hardwareProfile().caps;
  → int rate = AppSettings::instance()
        .hardwareValue(mac, "radioInfo/sampleRate").toInt();
  → const auto allowed = allowedSampleRates(info.protocol, caps);
  → if (rate <= 0 || std::find(allowed.begin(), allowed.end(), rate)
                     == allowed.end()) {
        qCWarning(lcConnection) << "Persisted sample rate" << rate
                                 << "invalid for" << mac
                                 << "; falling back to default";
        rate = defaultSampleRate(info.protocol, caps);
    }
  → int persistedCount = AppSettings::instance()
        .hardwareValue(mac, "radioInfo/activeRxCount").toInt();
    int rxCount = (persistedCount < 1)
        ? 1                                            // not yet persisted → default
        : std::min(persistedCount, caps.maxReceivers); // clamp to board cap
  → const int wdspInputRate = rate;
  → const int wdspInSize = 64 * wdspInputRate / 48000;   // cmsetup.c:106
  → (rest of connection flow, using these variables; hardcodes removed)
  → emit wireSampleRateChanged(static_cast<double>(wdspInputRate));
```

### 6.3 Connection completes

```
RadioModel emits wireSampleRateChanged(rate)   [already exists]
  → RadioInfoTab::onWireSampleRateChanged(rate)
  → updateReconnectBanner()   [banner hides when persisted == live]
```

### 6.4 New `AppSettings::hardwareSettingChanged` signal

Emitted from `AppSettings::setHardwareValue` after the internal map
update. Parameters: `(QString mac, QString key, QVariant value)`. Lets
`RadioInfoTab` react to its own setting changes without routing through
`HardwarePage`, and lets future tabs subscribe to each other's changes
without introducing cross-tab coupling through `HardwarePage`.

---

## 7. Persistence

Keys in AppSettings, per-MAC (matches existing Hardware Config pattern):

| Key | Type | Range | Default |
|---|---|---|---|
| `hardware/<mac>/radioInfo/sampleRate` | int (Hz) | member of `allowedSampleRates(proto, caps)` | `defaultSampleRate(proto, caps)` = 192000 |
| `hardware/<mac>/radioInfo/activeRxCount` | int | 1..`caps.maxReceivers` | 1 |

**Why per-MAC and not global:** Thetis uses a global-per-install key
(`DB.SaveVarsDictionary("Options", ...)`, setup.cs:1627) because Thetis
has no multi-radio concept. NereusSDR already uses per-MAC hardware keys
for the other 80+ Hardware Config settings, and users with multiple
radios on the network (e.g. HL2 + ANAN-G2) benefit from per-radio rate
memory. This is a NereusSDR-appropriate refinement of Thetis's
single-radio assumption, not a deviation from Thetis behavior (Thetis
would persist per-MAC too if it supported multiple radios).

### CLAUDE.md update (same PR)

Current text (CLAUDE.md:~294):

> **Radio-authoritative (do NOT persist):** ADC attenuation, preamp,
> TX power, antenna selection, hardware sample rate.

Corrected text:

> **Radio-authoritative (do NOT persist):** ADC attenuation, preamp,
> TX power, antenna selection.
>
> **Hardware sample rate and active RX count:** persisted per-MAC in
> AppSettings, applied on next connect. Matches Thetis which persists
> rate globally via DB.SaveVarsDictionary (setup.cs:1627). Live-apply
> of rate changes to a running connection is deferred to the follow-up
> PR that adds WDSP teardown/rebuild infrastructure.

---

## 8. Error handling

- **Stale persisted rate** (e.g. user persisted 1.536M for ANAN-G2 then
  plugged in an HL2): validate against `allowedSampleRates`, fall back
  to default, log via `qCWarning(lcConnection)`. No silent acceptance.
- **Stale persisted RX count** (board swap lowers `maxReceivers`):
  `std::clamp(count, 1, caps.maxReceivers)`. Silently clamped; no
  warning needed since the user can see the spinbox max.
- **AppSettings returns no value** (first run, clean install): `toInt()`
  yields 0 → triggers fallback path → default rate, `rxCount = 1`.
- **No radio connected, user interacts with combo**: tab's
  `m_currentMac` is empty; `HardwarePage::onTabSettingChanged` early-exits
  without persisting. Consistent with existing behavior.

---

## 9. Testing

### 9.1 New: `tests/tst_sample_rate_catalog.cpp`

For every entry in the `BoardCapabilities` registry, verify:

- `allowedSampleRates(Protocol1, caps)` = expected P1 list (base or
  RedPitaya-extended) ∩ `caps.sampleRates`.
- `allowedSampleRates(Protocol2, caps)` = `kP2Rates` ∩ `caps.sampleRates`.
- `defaultSampleRate(proto, caps)` = 192000 for all standard boards.
- Explicit case: REDPITAYA gets 384000 on P1, other boards don't.
- Explicit case: empty `caps.sampleRates` → returns empty list (no
  crash).

### 9.2 Extend: `tests/tst_hardware_page_persistence.cpp`

Add round-trip cases:

- Write `radioInfo/sampleRate` via `RadioInfoTab::onRx1SampleRateChanged`
  → readback from AppSettings matches.
- Write `radioInfo/activeRxCount` via `RadioInfoTab::onActiveRxCountChanged`
  → readback matches.
- `RadioInfoTab::restoreSettings` populates combo + spinbox from stored
  AppSettings.

### 9.3 New: `tests/tst_radio_model_sample_rate.cpp`

Integration test using existing P1/P2 connection mocks.

- **P1 + persisted 96000**: connect → mock's captured sample rate is
  96000, `wdspInSize` is 128.
- **P2 + persisted 384000**: connect → captured 384000, `wdspInSize`
  512.
- **P2 + persisted 1536000** (on a cap-supporting board): connect →
  captured 1536000, `wdspInSize` 2048.
- **No persisted value**: connect → captured 192000 (default),
  `wdspInSize` 256.
- **Stale persisted 1536000 on HL2 (P2 but caps cap at 192000)**:
  connect → captured 192000 (default fallback), warning logged.
- **Persisted RX count clamped**: persist 7 for an HL2 (max 2) →
  connect → mock's captured count is 2.

### 9.4 Not tested in this PR

- Live-apply teardown/rebuild (no such code path yet — PR #36).
- Banner rendering (manual smoke on Windows per user's current
  platform).
- Phase 3F RX2-combo-becomes-active scenarios (future PR).

---

## 10. Files touched

| File | Change |
|---|---|
| `src/core/SampleRateCatalog.h` | NEW — constants, `allowedSampleRates`, `defaultSampleRate` declarations |
| `src/core/SampleRateCatalog.cpp` | NEW — implementations |
| `src/core/AppSettings.h` | Add `hardwareSettingChanged` signal |
| `src/core/AppSettings.cpp` | Emit `hardwareSettingChanged` from `setHardwareValue` |
| `src/gui/setup/hardware/RadioInfoTab.h` | Add RX2 combo member, banner member, `valueChanged` slot for active RX spin |
| `src/gui/setup/hardware/RadioInfoTab.cpp` | Build RX2 combo (disabled) + banner, wire `valueChanged` for spin, subscribe to `AppSettings::hardwareSettingChanged` and `RadioModel::wireSampleRateChanged`, populate from `allowedSampleRates` |
| `src/models/RadioModel.cpp` | Read persisted rate + RX count on connect, validate against allowed list, drop `isP1 ? 192000 : 768000` hardcode, use `64 * rate / 48000` formula |
| `CLAUDE.md` | Correct the "sample rate is radio-authoritative" rule |
| `CHANGELOG.md` | Entry under Unreleased |
| `docs/architecture/2026-04-16-sample-rate-wiring-design.md` | NEW — this spec |
| `tests/tst_sample_rate_catalog.cpp` | NEW |
| `tests/tst_hardware_page_persistence.cpp` | Extend with sample-rate / RX-count cases |
| `tests/tst_radio_model_sample_rate.cpp` | NEW |
| `tests/CMakeLists.txt` | Register the two new test targets |

---

## 11. Rollout — Plan C context

This PR is the first of a phased Hardware Config refactor ("Plan C"):

1. **PR #35 (this PR):** Sample rate + active RX count wiring,
   AppSettings→RadioModel bridge pattern.
2. **PR #36:** Live-apply primitive — `WdspEngine::rebuildRxChannel`,
   DDC disable/enable choreography matching Thetis setup.cs:6982-7158.
   Removes the reconnect banner.
3. **PR #37+:** Per-tab wiring using the bridge pattern. One tab per
   PR, sized for quick review:
   - Antenna / ALEX relay bits and per-band antenna selection
   - OC Outputs per-band masks
   - XVTR auto-select band and per-row config
   - PureSignal (gated on Phase 3I-4)
   - Diversity (gated on real HL2 calibration)
   - PA Calibration (includes per-band key format bug fix)
   - HL2 I/O Board GPIO assignment via I2C
   - Bandwidth Monitor live stats hook + throttle enforcement
4. **PR #X:** Dither / Random / MaxRXFreq — grouped with ADC options.

Tracking issues are opened for each before PR #35 merges so the
decomposition is visible.

---

## Appendix A — Thetis source quote (RX1 live-apply, ETH)

From `C:/Users/boyds/Thetis/Project Files/Source/Console/setup.cs:6982-7075`,
quoted for reference when PR #36 implements live-apply:

```csharp
private void comboAudioSampleRate1_SelectedIndexChanged(object sender, System.EventArgs e)
{
    if (initializing) return;
    if (comboAudioSampleRate1.SelectedIndex < 0) return;

    int old_rate = console.SampleRateRX1;
    bool ok = Int32.TryParse(comboAudioSampleRate1.Text, out int new_rate);
    if (!ok) { /* error MessageBox */ return; }

    bool was_enabled = console.RX1Enabled;

    if (new_rate != old_rate || initializing || m_bForceAudio)
    {
        switch (NetworkIO.CurrentRadioProtocol)
        {
            case RadioProtocol.ETH:
                // turn OFF the DSP channels so they get flushed out
                WDSP.SetChannelState(WDSP.id(0, 1), 0, 0);
                WDSP.SetChannelState(WDSP.id(0, 0), 0, 1);
                Thread.Sleep(10);

                // remove the RX1 main and sub audio streams from the mix set
                unsafe { cmaster.SetAAudioMixStates((void*)0, 0, 3, 0); }

                // disable VAC
                if (console.PowerOn && console.VACEnabled && !initializing)
                    Audio.EnableVAC1(false);

                // turn OFF the DDC(s)
                NetworkIO.EnableRx(0, 0);
                NetworkIO.EnableRx(1, 0);
                NetworkIO.EnableRx(2, 0);

                // wait for all inflight packets to arrive
                Thread.Sleep(20);

                // set the new rate
                console.SampleRateRX1 = new_rate;

                // new buffer size from cmaster.GetBuffSize (= 64 * rate / 48000)
                int new_size = cmaster.GetBuffSize(new_rate);
                console.BlockSize1 = new_size;

                // display follows wire rate, not resampled rate
                console.specRX.GetSpecRX(0).SampleRate = new_rate;
                console.specRX.GetSpecRX(0).BlockSize = new_size;

                // turn on the DDC(s)
                console.UpdateDDCs(console.RX2Enabled);
                Thread.Sleep(1);

                unsafe { cmaster.SetAAudioMixStates((void*)0, 0, 3, 3); }

                if (console.PowerOn && console.VACEnabled && !initializing)
                    Audio.EnableVAC1(true);

                int w_enable = was_enabled ? 1 : 0;
                WDSP.SetChannelState(WDSP.id(0, 0), w_enable, 0);
                if (console.radio.GetDSPRX(0, 1).Active)
                    WDSP.SetChannelState(WDSP.id(0, 1), w_enable, 0);

                double bin_width = (double)new_rate / (double)console.specRX.GetSpecRX(0).FFTSize;
                // ... label updates ...

                // RX2 combo state
                if (HardwareSpecific.Model == HPSDRModel.ANAN10E ||
                    HardwareSpecific.Model == HPSDRModel.ANAN100B)
                {
                    comboAudioSampleRateRX2.Enabled = false;
                    comboAudioSampleRateRX2.SelectedIndex = comboAudioSampleRate1.SelectedIndex;
                }
                else
                    comboAudioSampleRateRX2.Enabled = true;

                break;
            /* RadioProtocol.USB similar, forces RX2=RX1 always */
        }
    }
}
```
