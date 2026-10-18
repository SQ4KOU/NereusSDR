# Phase 3M-1b — Mic + SSB Voice: Thetis Pre-Code Review

**Status:** ready for implementation planning.
**Date:** 2026-04-27.
**Branch:** `feature/phase3m-1b-mic-ssb-voice`.
**Base:** `origin/feature/phase3m-1a-tune-only-first-rf` @ `708ceb8`.
**Master design:** [`docs/architecture/phase3m-tx-epic-master-design.md`](phase3m-tx-epic-master-design.md) §5.2.
**Thetis version stamp for inline cites:** `[v2.10.3.13]` for values verified against the tagged release; `[@501e3f51]` for content past the tag (HEAD currently 7 commits past).
**Risk profile:** MEDIUM — see §15.

---

## 0. Document purpose and base decisions

### 0.1 What this document is

3M-1a established the source-first pre-code review pattern: read the upstream
Thetis sources for every behaviour we intend to port, document file:line cites
inline with what we're porting and what we're deferring, surface the
research-bounded gaps (places where the open-source code doesn't have the
behaviour at all and we can't faithfully port). The implementation plan that
follows reads from this document — every TDD task in the plan should cite back
to a section here.

### 0.2 Cite conventions

Per `CLAUDE.md` source-first protocol and JJ's `feedback_inline_cite_versioning`
memory:

- Inline cites use `[file:line] [v<version>|@<shortsha>]` format.
- Cites referencing the v2.10.3.13 tag use `[v2.10.3.13]`.
- Cites for content that only exists past the tag use `[@501e3f51]` (current Thetis
  HEAD).
- Annotations like "unmerged PR #N" go outside the brackets.
- When the same file:line appears multiple times in this doc, the version stamp
  is given at first use and abbreviated thereafter when context is unambiguous.

The pre-commit verifier `scripts/verify-inline-tag-preservation.py` requires
`NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis` set in the shell during commits. Set
this once per session at the top of every shell that will commit.

### 0.3 Brainstorm-locked decisions (already taken)

The master design §5.2 IS the brainstorm output. Locking just the deltas from
the kickoff conversation:

1. **MON pulled forward + RX-leak-during-MOX fold.** MON wiring (TXA siphon →
   speaker mixer) and the existing PR-#144 cosmetic bug ("RX still plays during
   MOX") share the same audio-mixer surface. They land as a single AudioEngine
   pass, not two. Justification: the activeSlice-during-MOX gate is the same
   surface MON's siphon-mix attaches to.
2. **HL2 mic-jack hiding gets a real `BoardCapabilities` flag.** New field
   `hasMicJack` on `BoardCapabilities`; HL2 sets `false`; the rest default
   `true`. The Setup → Audio → TX Input page checks this flag to hide the
   Radio-Mic radio button + sub-options on HL2.
3. **PcMicSource taps the existing AudioEngine `m_txInputBus`.** No duplicate
   PortAudio stack. PcMicSource is a thin shim over a new
   `AudioEngine::pullTxMic(float*, int)` accessor. The 3O VAX TX-input bus
   already opens the mic device under user control via Setup → Audio →
   Devices.

### 0.4 Out-of-scope (deferred)

See §13 for the full deferral list. Headlines:

- AM / SAM / DSB / FM / DRM TX → 3M-3b (mode-gated TXA stages).
- CW TX → 3M-2 (different code path).
- Full DEXP / EQ / Leveler / CFC / CFComp / Compressor → 3M-3a.
- 2-Tone test, profile combo → 3M-1c.
- PureSignal → 3M-4.

---

## 1. Audio class static properties (`audio.cs`)

### 1.1 The `Audio` static class as global radio-state

Thetis stores all process-global audio state on a single `Audio` static class in
`audio.cs:1-2124 [v2.10.3.13]`. Properties trip cmaster / IVAC / NetworkIO calls
in their setters; reads return cached values. NereusSDR's equivalent is split
across `RadioModel`, `TransmitModel`, `AudioEngine`. The mapping is
property-by-property; not a 1:1 file port.

### 1.2 Mic-side properties to port

Numbered by line offset in `audio.cs [v2.10.3.13]`. Each row gets a destination
on the NereusSDR side.

| Thetis property | File:line | Default | NereusSDR target |
|---|---|---|---|
| `Audio.MicPreamp` | `audio.cs:215-224` | `1.0` | `TransmitModel::micPreampLinear` (linear scalar; UI shows dB via 20·log10) |
| `Audio.WavePreamp` | `audio.cs:226-234` | `1.0` | Deferred (wave playback in 3M-3 / 3M-6 recording phase) |
| `Audio.WavePreampAdjust` | `audio.cs:236-245` | `0` (preamp delta dB) | Deferred (paired with `WavePreamp`) |
| `Audio.VACPreamp` | `audio.cs:579-589` | `1.0` | `TransmitModel::vaxPreampLinear` (NereusSDR rename: VAC→VAX) — but 3M-1b leaves storage-only; full VAX TX path lives in 3M-3 |
| `Audio.MonitorVolume` | `audio.cs:247-259` | `0.0` | `TransmitModel::monitorVolume` (0.0–1.0; UI slider in TxApplet) |
| `Audio.VOXEnabled` | `audio.cs:167-192` | `false` | `TransmitModel::voxEnabled` |
| `Audio.VOXGain` | `audio.cs:194-202` | `1.0f` | `TransmitModel::voxGainScalar` (mic-boost-aware threshold scaler) |
| `Audio.AntiVOXSourceVAC` | `audio.cs:446-455` | `false` | `TransmitModel::antiVoxSourceVax` (renamed; default `false` = use local RX, matches Thetis) |
| `Audio.MOX` | `audio.cs:349-384` | `false` | Already `MoxController::moxChanged` (3M-1a) — listener slot only |
| `Audio.MON` | `audio.cs:406-425` | `false` | `TransmitModel::monEnabled` |
| `Audio.TXDSPMode` | `audio.cs:624-634` | `LSB` | `SliceModel::mode` (already wired since 3E) — listener slot triggers `CMSetTXAVoxRun` + `CMSetTXAPanelGain1` equivalents |
| `Audio.VOXActive` | `audio.cs:14718` (defined on `Console`, not `Audio`; Thetis namespacing inconsistent) | live state | `MoxController::voxActiveChanged` (NereusSDR boundary) |

### 1.3 Property setter side effects to mirror

The setter side effects matter — many trip `cmaster.CMSetTXAPanelGain1` to
recompute mic gain. NereusSDR mirrors via signal/slot:

```text
TransmitModel::micPreampLinear setter
    → emit micPreampChanged(double)
    → TxChannel::recomputeTxAPanelGain1() slot
        (mode-gated; see §3.1)

TransmitModel::voxEnabled setter
    → emit voxEnabledChanged(bool)
    → TxChannel::setVoxRun(bool, DSPMode) slot (mode-gated; see §3.2)

SliceModel::modeChanged
    → TxChannel::setTxMode(DSPMode) slot
    → AND TxChannel::recomputeTxAPanelGain1() slot
    → AND TxChannel::setVoxRun(bool, DSPMode) slot
    (matches Thetis TXDSPMode setter at audio.cs:625-634)
```

### 1.4 What we map vs. what we defer

**Port** (3M-1b scope):

- MicPreamp / MonitorVolume / VOXEnabled / VOXGain / AntiVOXSourceVAC / MON
- Setter cascade order (TXDSPMode triggers VoxRun + PanelGain1)

**Defer to 3M-3+:**

- `Audio.HighSWRScale` (already done in 3M-0)
- `Audio.RadioVolume` (already done in 3I)
- `Audio.WavePlayback` / `WaveRecord` (3M-6 recording phase)
- IVAC mox/mon/vox state setters (NereusSDR has VAX, no IVAC; ports to VAX
  deferred to 3M-3)
- TCI mox/mon/vox state (TCI lives in 3J)

---

## 2. Console mic-side controls (`console.cs`)

### 2.1 `MicMute` — counter-intuitively named (true = mic in use)

`console.cs:28752-28765 [v2.10.3.13]`:

```csharp
public bool MicMute // NOTE: although called MicMute, true = mic in use
{
    get { return chkMicMute.Checked; }
    set { ... }
}
```

The Thetis comment is verbatim ("// NOTE: although called MicMute, true = mic
in use"). Per `feedback_thetis_attribution_rules` we preserve the inline note.

`chkMicMute_CheckedChanged` at `console.cs:28767-28772`:

```csharp
private void chkMicMute_CheckedChanged(object sender, System.EventArgs e)
{
    ptbMic_Scroll(this, EventArgs.Empty);
    SetGeneralSetting(0, OtherButtonId.MIC, chkMicMute.Checked);
}
```

It re-runs `ptbMic_Scroll` to refresh the gain calculation based on the new
"mic in use" state.

`ptbMic_Scroll` at `console.cs:28774-28804` and `setAudioMicGain` at
`console.cs:28805-28817`:

```csharp
private void setAudioMicGain(double gain_db)
{
    if (chkMicMute.Checked) // although it is called chkMicMute, checked = mic in use
    {
        Audio.MicPreamp = Math.Pow(10.0, gain_db / 20.0); // convert to scalar 
        _mic_muted = false;
    }
    else
    {
        Audio.MicPreamp = 0.0;
        _mic_muted = true;
    }
}
```

**Mute = MicPreamp = 0**, not a separate mute path. Setting MicPreamp to 0
silences the mic via `cmaster.CMSetTXAPanelGain1` (which reads MicPreamp;
see §3.1).

### 2.2 MicGain via `ptbMic` slider + `setAudioMicGain`

The slider value (`ptbMic.Value`) is in dB; `setAudioMicGain` converts to a
linear scalar via `10^(dB/20)` and stores in `Audio.MicPreamp`. Range from
`mic_gain_min` / `mic_gain_max` (per-board values; HL2 differs).

NereusSDR `TransmitModel::micGainDb` is the user-facing value (int, dB).
Setter computes the linear scalar, stores both:

```cpp
void TransmitModel::setMicGainDb(int dB) {
    if (m_micGainDb == dB) return;
    m_micGainDb = dB;
    m_micPreampLinear = std::pow(10.0, dB / 20.0);
    emit micGainChanged(dB);
    emit micPreampChanged(m_micPreampLinear);
}
```

### 2.3 `MicBoost` / `MicXlr` / `LineIn` / `LineInBoost`

`console.cs:13213-13247 [v2.10.3.13]`:

```csharp
private bool line_in = false;
public bool LineIn {
    get { return line_in; }
    set {
        line_in = value;
        ptbMic_Scroll(this, EventArgs.Empty);
        SetMicGain();
    }
}

private double line_in_boost = 0.0;
public double LineInBoost { ... ptbMic_Scroll + SetMicGain ... }

private bool mic_boost = true;  // [v2.10.3.13] DEFAULT TRUE
public bool MicBoost {
    get { return mic_boost; }
    set {
        mic_boost = value;
        ptbMic_Scroll(this, EventArgs.Empty);
        SetMicGain();
    }
}

private bool mic_xlr = true;  // [v2.10.3.13] DEFAULT TRUE
public bool MicXlr {
    get { return mic_xlr; }
    set {
        mic_xlr = value;
        ptbMic_Scroll(this, EventArgs.Empty);
        SetMicXlr();
    }
}
```

Defaults: `mic_boost = true`, `mic_xlr = true`, `line_in = false`,
`line_in_boost = 0.0`.

### 2.4 `SetMicGain()` dispatch (mic-jack wire bits)

`console.cs:40847-40860 [v2.10.3.13]`:

```csharp
public void SetMicGain()
{
    var v = mic_boost ? 1 : 0;
    NetworkIO.SetMicBoost(v);

    v = line_in ? 1 : 0;
    NetworkIO.SetLineIn(v);

    if (!lineinarrayfill) MakeLineInList();

    var lineboost = Array.IndexOf(lineinboost, line_in_boost.ToString());
    NetworkIO.SetLineBoost(lineboost);
}
```

Despite the name, `SetMicGain()` is a **wire-bit dispatcher** — it sends MicBoost,
LineIn, LineBoost over `NetworkIO`, *not* a WDSP gain setter. The actual WDSP
gain comes via `Audio.MicPreamp` → `cmaster.CMSetTXAPanelGain1`.

### 2.5 `SetMicXlr()` dispatch

`console.cs:40841-40845 [v2.10.3.13]`:

```csharp
public void SetMicXlr()
{
    var v = mic_xlr ? 1 : 0;
    NetworkIO.SetMicXlr(v);
}
```

Pushes only the `mic_xlr` bit. **Note: this bit is stored on the radio side but
is not transmitted in any open-source `networkproto1.c` wire-byte. See §6.2.**

### 2.6 `chkVOX` UI button + `_vox_enable`

`console.cs:28843-28862 [v2.10.3.13]`:

```csharp
private void chkVOX_CheckedChanged(object sender, System.EventArgs e)
{
    _vox_enable = chkVOX.Checked;
    if (!IsSetupFormNull) SetupForm.VOXEnable = _vox_enable;
    if (_vox_enable) {
        chkVOX.BackColor = button_selected_color;
    } else {
        Audio.VOXActive = false;
        chkVOX.BackColor = SystemColors.Control;
    }
    LineInBoost = line_in_boost;  // re-trigger ptbMic + SetMicGain
    SetGeneralSetting(0, OtherButtonId.VOX, chkVOX.Checked);
}
```

The console-side property at `console.cs:13123-13133`:

```csharp
public bool VOXEnable
{
    get { return _vox_enable; }
    set { if (chkVOX != null) chkVOX.Checked = value; }
}
```

Setter goes via the chkVOX UI control, which then triggers
`chkVOX_CheckedChanged` and the cascade.

`_rx_only` and `_tx_inhibit` gate `chkVOX.Enabled` at `console.cs:15297` and
`:15328` — already wired in NereusSDR via 3M-0's `BandPlanGuard` /
`TxInhibitMonitor`.

### 2.7 `AllModeMicPTT` and `MicPTTDisabled`

`console.cs:12022-12027 [v2.10.3.13]` — `AllModeMicPTT` is a Setup→General
toggle; default `false`. When false, mic-PTT only fires in voice modes
(LSB/USB/DSB/AM/SAM/FM/DIGL/DIGU). When true, mic-PTT fires regardless of mode.

`console.cs:19757-19767`:
```csharp
public bool MicPTTDisabled
{
    get { return mic_ptt_disabled; }
    set {
        mic_ptt_disabled = value;
        NetworkIO.SetMicPTT(Convert.ToInt32(value));
    }
}
```

Setter pushes `SetMicPTT` over the wire (`networkproto1.c` case 11 C1 bit 6;
see §6.1). Default `false` (mic PTT enabled).

### 2.8 What we port vs. defer

**Port:**

- `MicMute` (counter-intuitive naming preserved in inline cite comment)
- `MicGain` slider with dB→scalar conversion via `pow(10, dB/20)`
- `MicBoost` / `LineIn` / `LineInBoost` (wired to RadioConnection mic-bit
  setters per §6)
- `chkVOX` toggle → `MoxController::setVoxEnabled(bool)`
- `MicPTTDisabled` → `RadioConnection::setMicPTT(!enabled)`
- `AllModeMicPTT` toggle (Setup → General)

**Defer:**

- (none — see §6.3 for `MicXlr` resolution via deskhpsdr)

---

## 3. cmaster dispatchers (`cmaster.cs`)

### 3.1 `CMSetTXAPanelGain1` — the mic-gain dispatch

`cmaster.cs:1061-1101 [v2.10.3.13]`:

```csharp
public static void CMSetTXAPanelGain1(int channel)
{
    double gain = 1.0;
    DSPMode mode = Audio.TXDSPMode;
    if ((!Audio.VACEnabled &&
         (mode == DSPMode.LSB || mode == DSPMode.USB ||
          mode == DSPMode.DSB || mode == DSPMode.AM ||
          mode == DSPMode.SAM || mode == DSPMode.FM ||
          mode == DSPMode.DIGL || mode == DSPMode.DIGU)) ||
        (Audio.VACEnabled && Audio.VACBypass &&
         (mode == DSPMode.DIGL || mode == DSPMode.DIGU ||
          mode == DSPMode.LSB || mode == DSPMode.USB ||
          mode == DSPMode.DSB || mode == DSPMode.AM ||
          mode == DSPMode.SAM || mode == DSPMode.FM)))
    {
        if (Audio.WavePlayback) {
            // [WavePlayback gain path — 20·log10(WavePreamp) + 20·log10(WavePreampAdjust)
            //  clipped to ±70 dB then back to scalar]
        } else {
            if (!Audio.VACEnabled && (mode == DSPMode.DIGL || mode == DSPMode.DIGU))
                gain = Audio.VACPreamp;
            else
                gain = Audio.MicPreamp;
        }
    }
    Audio.console.radio.GetDSPTX(0).MicGain = gain;
}
```

Three logical paths:

1. **Mode in {LSB,USB,DSB,AM,SAM,FM,DIGL,DIGU} and !VACEnabled** → SSB-family
   mic-gain path. `gain = Audio.MicPreamp` (or `VACPreamp` for digital modes
   with VAC disabled).
2. **Mode in same set, VACEnabled, VACBypass** → bypass-VAC path; same gain
   logic (treats as if VAC were disabled).
3. **Anything else** → `gain = 1.0` (unity) — modes outside SSB family get
   default unity gain.

The output flows to `radio.GetDSPTX(0).MicGain`, which calls
`WDSP.SetTXAPanelGain1(channelId, gain)`.

Per the SSB-only mode scope of 3M-1b, NereusSDR ports the mode-gated path
(case 1) and the SSB-family list. Cases 2 and 3 are deferred to 3M-3 (VAX
integration) and 3M-3b (non-SSB modes).

### 3.2 `CMSetTXAVoxRun` — VOX mode-gate

`cmaster.cs:1039-1052 [v2.10.3.13]`:

```csharp
public static void CMSetTXAVoxRun(int id)
{
    DSPMode mode = Audio.TXDSPMode;
    bool run = Audio.VOXEnabled &&
               (mode == DSPMode.LSB || mode == DSPMode.USB ||
                mode == DSPMode.DSB || mode == DSPMode.AM ||
                mode == DSPMode.SAM || mode == DSPMode.FM ||
                mode == DSPMode.DIGL || mode == DSPMode.DIGU);
    cmaster.SetDEXPRunVox(id, run);
}
```

VOX runs iff `VOXEnabled && mode ∈ {voice family}`. The mode-gate prevents VOX
from triggering during CW or DRM. NereusSDR mirrors via
`MoxController::setVoxRun(bool enabled, DSPMode mode)`.

### 3.3 `CMSetTXAVoxThresh` — mic-boost-aware threshold

`cmaster.cs:1054-1059 [v2.10.3.13]`:

```csharp
public static void CMSetTXAVoxThresh(int id, double thresh)
{
    //double thresh = (double)Audio.VOXThreshold;
    if (Audio.console.MicBoost) thresh *= (double)Audio.VOXGain;
    cmaster.SetDEXPAttackThreshold(id, thresh);
}
```

When `MicBoost` is on, the user-set VOX threshold is **scaled by** `VOXGain`
(default 1.0). This compensates for the +20dB hardware preamp gain so a single
VOX threshold-knob position works at both boost states. NereusSDR ports this
mic-boost-aware scaling exactly.

The `VOXThreshold` reference is commented-out in Thetis; the actual threshold
value is passed in by the caller (the slider value). NereusSDR
`TransmitModel::voxThresholdDb` provides the slider value; the scaling math
runs in `MoxController::setVoxThreshold(int dB)`.

### 3.4 `CMSetAntiVoxSourceWhat` — anti-VOX source switch

`cmaster.cs:912-943 [v2.10.3.13]`:

```csharp
public static void CMSetAntiVoxSourceWhat()
{
    bool VACEn = Audio.console.VACEnabled;
    bool VAC2En = Audio.console.VAC2Enabled;
    bool useVAC = Audio.AntiVOXSourceVAC;
    int RX1  = WDSP.id(0, 0);
    int RX1S = WDSP.id(0, 1);
    int RX2  = WDSP.id(2, 0);
    if (useVAC)   // use VAC audio
    {
        if (VACEn) {
            cmaster.SetAntiVOXSourceWhat(0, RX1,  1);
            cmaster.SetAntiVOXSourceWhat(0, RX1S, 1);
        } else {
            cmaster.SetAntiVOXSourceWhat(0, RX1,  0);
            cmaster.SetAntiVOXSourceWhat(0, RX1S, 0);
        }
        if (VAC2En) cmaster.SetAntiVOXSourceWhat(0, RX2, 1);
        else        cmaster.SetAntiVOXSourceWhat(0, RX2, 0);
    }
    else  // use audio going to hardware minus MON
    {
        cmaster.SetAntiVOXSourceWhat(0, RX1,  1);
        cmaster.SetAntiVOXSourceWhat(0, RX1S, 1);
        cmaster.SetAntiVOXSourceWhat(0, RX2,  1);
    }
}
```

Two paths:

- **`useVAC=true`**: Anti-VOX listens to VAC audio. Per-RX bit flips by
  whether that RX's VAC slot is enabled.
- **`useVAC=false`** (Thetis comment: "use audio going to hardware minus MON"):
  Anti-VOX listens to all three local-RX slots unconditionally.

NereusSDR maps VAC→VAX. The "useVAC=false" path is unconditional — that's the
3M-1b path-agnostic version. Full state-machine for VAX-source anti-VOX
(`SetAntiVOXSourceStates` per `console.cs:27602-27721 [v2.10.3.13]`) deferred
to 3M-3a where VAX TX integration lands.

### 3.5 `SetDEXP*` — WDSP DEXP DLL imports

`cmaster.cs:208-221 [v2.10.3.13]`:

```csharp
[DllImport("wdsp.dll", EntryPoint = "SetAntiVOXRun", ...)]
public static extern void SetAntiVOXRun(int id, bool run);
[DllImport("wdsp.dll", EntryPoint = "SetAntiVOXGain", ...)]
public static extern void SetAntiVOXGain(int id, double gain);
[DllImport("wdsp.dll", EntryPoint = "SetAntiVOXDetectorTau", ...)]
public static extern void SetAntiVOXDetectorTau(int id, double tau);

[DllImport("ChannelMaster.dll", EntryPoint = "SetAntiVOXSourceStates", ...)]
public static extern void SetAntiVOXSourceStates(int txid, int streams, int states);
[DllImport("ChannelMaster.dll", EntryPoint = "SetAntiVOXSourceWhat", ...)]
public static extern void SetAntiVOXSourceWhat(int txid, int stream, int state);
```

WDSP calls (`SetAntiVOX*Run/Gain/DetectorTau`) live in WDSP itself —
NereusSDR's WDSP wrapper exposes these; just need new wrappers on
`TxChannel`. ChannelMaster calls (`SetAntiVOXSource*`) are NereusSDR-native
state in `MoxController` (no separate ChannelMaster.dll in NereusSDR).

### 3.6 What we port vs. defer

**Port:**

- `CMSetTXAPanelGain1` mode-gated path (SSB family + MicPreamp/VACPreamp split)
- `CMSetTXAVoxRun` mode-gate (voice-family-only)
- `CMSetTXAVoxThresh` mic-boost-aware scaling
- `CMSetAntiVoxSourceWhat` path-agnostic version (all RX slots = 1 when not
  VAX-source)

**Defer to 3M-3:**

- VAX-source anti-VOX state-machine (when MOX/RX2/VAC interactions matter)
- IVAC integration (NereusSDR has no IVAC)

---

## 4. MON path (`audio.cs` + cmaster aaudio mix)

### 4.1 The MON property setter

`audio.cs:386-425 [v2.10.3.13]`:

```csharp
private static void setupIVACforMon()
{
    if (mon) {
        ivac.SetIVACmon(0, 1);
        ivac.SetIVACmon(1, 0);
        ivac.SetIVACmonVol(0, monitor_volume);
        cmaster.SetTCIRxAudioMon(0, 1);
        cmaster.SetTCIRxAudioMon(1, 1);
        cmaster.SetTCIRxAudioMonVol(0, monitor_volume);
        cmaster.SetTCIRxAudioMonVol(1, monitor_volume);
    } else {
        ivac.SetIVACmon(0, 0);
        ivac.SetIVACmon(1, 0);
        cmaster.SetTCIRxAudioMon(0, 0);
        cmaster.SetTCIRxAudioMon(1, 0);
    }
}

private static bool mon;
public static bool MON
{
    set
    {
        mon = value;
        setupIVACforMon();
        unsafe {
            cmaster.SetAAudioMixVol((void*)0, 0, WDSP.id(1, 0), 0.5);
            cmaster.SetAAudioMixWhat((void*)0, 0, WDSP.id(1, 0), value);
        }
    }
    get { return mon; }
}
```

Three subsystems get notified when MON toggles:

1. **IVAC** (Thetis VAC) — turn IVAC mon on/off per RX (RX1 mon=1, RX2 mon=0).
2. **TCI RX-audio** — TCI clients also get a mon hint.
3. **cmaster aaudio mix** — the ACTUAL audio mixer that produces the
   user-audible MON. `SetAAudioMixVol` with **fixed coefficient 0.5** and
   `SetAAudioMixWhat` toggles the channel.

The `0.5` is a Thetis-internal mix coefficient (probably for mix headroom).
The user-controlled volume comes via `MonitorVolume` setter (next).

### 4.2 The `MonitorVolume` property

`audio.cs:247-259 [v2.10.3.13]`:

```csharp
private static double monitor_volume = 0.0;
public static double MonitorVolume
{
    get { return monitor_volume; }
    set
    {
        monitor_volume = value;
        cmaster.CMSetAudioVolume(value);
        ivac.SetIVACmonVol(0, monitor_volume);
        cmaster.SetTCIRxAudioMonVol(0, monitor_volume);
        cmaster.SetTCIRxAudioMonVol(1, monitor_volume);
    }
}
```

`cmaster.CMSetAudioVolume(value)` calls
`cmaster.SetAAudioMixVolume((void*)0, 0, value)` (cmaster.cs:945-948). This
is where the **user volume** lives; the 0.5 in §4.1 is a
*per-channel-slot* mix coefficient, the user's `MonitorVolume` is a
*master-output volume*. They multiply.

Default `monitor_volume = 0.0` (mute). When MON toggles on without volume
adjustment, the user hears nothing — they must drag the slider.

### 4.3 NereusSDR mapping (with RX-leak-during-MOX fold)

NereusSDR has no IVAC and no TCI in 3M-1b. The MON path collapses to a single
`AudioEngine` integration:

```cpp
// AudioEngine.h — new public API
void setTxMonitorEnabled(bool on);     // emits txMonitorEnabledChanged
void setTxMonitorVolume(float v);      // 0.0..1.0; emits txMonitorVolumeChanged
void txMonitorBlockReady(const float* samples, int frames);  // slot
                                       // — receives TXA siphon output

// AudioEngine.cpp — rxBlockReady gate fix (the RX-leak-during-MOX fold)
void AudioEngine::rxBlockReady(int sliceId, const float* samples, int frames) {
    SliceModel* slice = m_radio->slices().forId(sliceId);
    if (!slice) return;

    // 3M-1b fold: respect activeSlice during MOX. When MOX is true, only the
    // active TX slice's audio gets gated by MON. Non-active slices (RX2 in
    // TX-on-VFO-A, RX1 in TX-on-VFO-B) keep playing unmodified — matches
    // Thetis IVAC mox state-machine in audio.cs:349-384.
    if (m_radio->moxState() && slice->isActiveSlice()) {
        return;  // RX leak silenced
    }

    // ... existing mix into MasterMixer ...
}

void AudioEngine::txMonitorBlockReady(const float* samples, int frames) {
    if (!m_txMonitorEnabled) return;
    float vol = m_txMonitorVolume.load();
    // Mix into MasterMixer at vol scaling. MasterMixer flushes to speakers
    // bus on the same DSP-thread cadence as RX blocks.
    m_masterMix.addChannel(kTxMonitorSlotId, samples, frames, vol);
}
```

`TxChannel` exposes `Stage::Sip1` siphon as a Qt signal:

```cpp
// TxChannel.h
signals:
    void sip1OutputReady(const float* samples, int frames);
    // Emitted on the audio thread after fexchange2 completes. Carries
    // the post-processing TX audio (post-EQ, post-Leveler, pre-RsmpOut).
    // For 3M-1b: this is the SSB-modulated audio at TXA dsp-rate (96 kHz);
    // AudioEngine resamples to speakers rate before mixing.
```

The connection wires `TxChannel::sip1OutputReady` →
`AudioEngine::txMonitorBlockReady` via `Qt::DirectConnection` (audio thread,
no thread hop).

### 4.4 What we port vs. defer

**Port:**

- MON enable/disable toggle.
- Monitor volume slider (0.0..1.0; default 0.0).
- TXA Sip1 siphon → AudioEngine MasterMixer mix-in.
- The Thetis fixed `0.5` mix coefficient is replaced with the user's
  `monitorVolume` directly (single user-facing volume; no per-slot
  intermediate coefficient).

**Defer:**

- IVAC mon (NereusSDR has VAX; VAX mon support is 3M-3+).
- TCI mon hooks (3J).
- Headphone-only MON (separate speakers/headphones bus split required).

---

## 5. Setup → Audio → Primary mic page (`setup.cs` + `setup.designer.cs`)

### 5.1 Per-board UI panels

Thetis Setup → Audio → Primary uses per-board GroupBoxes that show/hide based
on the active radio model. From `setup.designer.cs [v2.10.3.13]`:

| Panel | Family | Controls |
|---|---|---|
| `panelSaturnMicInput` | Saturn G2 family | `radSaturn3p5mm` (3.5mm vs XLR — line 8632-8644) |
| `panelOrionMic` | Orion-MkII / ANAN-7000DLE family | `radOrionMicTip` (Tip-is-mic vs Ring-is-mic — line 8680-8692) |
| `panelOrionPTT` | Orion-MkII | `radOrionPTTOff` (Disable mic-PTT — line 8729-8739) |
| `panelOrionBias` | Orion-MkII | `radOrionBiasOn` (Bias on/off — line 8763-8767) |
| `grpBoxMic` | Hermes / general HPSDR | `radMicIn`, `radLineIn`, `chk20dbMicBoost` (lines 2854-2858, 46751-46933) |

HL2 and Atlas show *none* of these panels — they have no radio-side mic-jack
hardware. The visibility is driven by `RadioModelChanged()` at
`setup.cs:19834-20310 [@501e3f5]` (per-model if-ladder).

### 5.2 Handler call sites

| Handler | File:line | What it sets |
|---|---|---|
| `radSaturn3p5mm_CheckedChanged` | `setup.cs:16450-16455` | `NetworkIO.SetMicXlr(0)` if 3.5mm checked, else `1`. **Note: counter-intuitive — checking the 3.5mm radio button sets MicXlr=0 (i.e., disables XLR routing).** |
| `radOrionPTTOff_CheckedChanged` | `setup.cs:16457-16461` | `console.MicPTTDisabled = radOrionPTTOff.Checked` |
| `radOrionMicTip_CheckedChanged` | `setup.cs:16463-16469` | `NetworkIO.SetMicTipRing(0)` if Tip checked, else `1` |
| `radOrionBiasOn_CheckedChanged` | `setup.cs:16471-16477` | `NetworkIO.SetMicBias(1)` if Bias checked, else `0` |
| `radMicIn_CheckedChanged` | `setup.cs:14257-14267` | `console.LineIn = false`, makes `chk20dbMicBoost` visible |
| `radLineIn_CheckedChanged` | `setup.cs:14269-14279` | `console.LineIn = true`, hides `chk20dbMicBoost` |
| `chk20dbMicBoost_CheckedChanged` | `setup.cs:7684-7687` | `console.MicBoost = chk20dbMicBoost.Checked`; re-runs `udVOXGain_ValueChanged` for mic-boost-aware threshold |

### 5.3 `chk20dbMicBoost` default + visibility

`setup.designer.cs:46924-46925 [v2.10.3.13]`:

```csharp
this.chk20dbMicBoost.Checked = true;
this.chk20dbMicBoost.CheckState = System.Windows.Forms.CheckState.Checked;
```

Default checked (enabled). Visible only when `radMicIn` is selected, hidden
when `radLineIn` is selected (`setup.cs:14263-14264 vs 14275-14276`).

### 5.4 NereusSDR mapping

New page `Setup → Audio → TX Input` at
`src/gui/setup/AudioTxInputPage.{h,cpp}` (matches existing flat audio-page
naming — `AudioVaxPage`, `AudioDevicesPage`, `AudioTciPage`).

UI surface (capability-gated):

```text
PC Mic [○]  Radio Mic [○]   ← top-level radio-button source selector
                               (Radio Mic disabled with tooltip on HL2)

[PC Mic settings group, visible iff PC Mic selected]
  Backend: [PortAudio ▾]  Device: [Built-in Microphone ▾]
  Buffer size: [256 samples]  Latency: 5.3 ms
  [Test Mic] (live VU bar)
  Mic Gain: [────●────] 0 dB

[Radio Mic settings group, visible iff Radio Mic selected AND caps.hasMicJack]

  ───── Hermes/Atlas family (caps.hasMicJack && family == HPSDR_HERMES) ─────
  Source:  [○] Mic In   [○] Line In
  [chk20dbMicBoost: ☑ 20 dB Mic Boost]  ← visible iff Mic In selected
  Line Boost: [────●────] 0 dB           ← visible iff Line In selected

  ───── Orion-MkII family (caps.hasMicJack && family == HPSDR_ORION) ─────
  Mic Tip-Ring:  [○] Tip  [○] Ring
  Mic PTT:       [☐] Disable mic PTT
  Mic Bias:      [☐] Bias on
  20 dB Boost:   [☑]

  ───── Saturn G2 family (caps.hasMicJack && family == HPSDR_SATURN) ─────
  Mic Input:     [○] 3.5mm  [○] XLR     ← both wired via P2 byte 50 bit 5
  Mic PTT / Bias / Boost: same as Orion-MkII
```

### 5.5 What we port vs. defer

**Port:**

- Setup → Audio → TX Input page with capability-gated layout.
- All six Hermes/Orion/Saturn mic-jack handlers (MicTipRing, MicBias,
  MicPTT, MicBoost, LineIn, MicXlr).
- Per-family panel logic via `BoardCapabilities`.

**Defer:**

- (none — all P1 + P2 wire emission ports cleanly per §6.3)

---

## 6. NetworkIO mic-jack wire-bit setters (P1 + P2)

### 6.1 P1 wire-byte map (from `networkproto1.c`)

Thetis `Project Files/Source/ChannelMaster/networkproto1.c [v2.10.3.13]` —
the open-source C side of the wire-byte composer. Setters in
`netInterface.c:535-805` write into `prn->mic` then trigger `CmdTx()` to
retransmit.

P1 wire-byte placement (Hermes + Atlas + Orion + ANAN family):

| Field | C0 case | Byte | Bit | Cite |
|---|---|---|---|---|
| `mic_boost` | `0x12` (case 10) | C2 | 0 | `networkproto1.c:581 [v2.10.3.13]` |
| `line_in` | `0x12` (case 10) | C2 | 1 | `networkproto1.c:581 [v2.10.3.13]` |
| `mic_trs` (= MicTipRing) | `0x14` (case 11) | C1 | 4 | `networkproto1.c:597 [v2.10.3.13]` |
| `mic_bias` | `0x14` (case 11) | C1 | 5 | `networkproto1.c:597 [v2.10.3.13]` |
| `mic_ptt` | `0x14` (case 11) | C1 | 6 | `networkproto1.c:598 [v2.10.3.13]` |

Bit layout reference (from `network.h:170-189 [v2.10.3.13]`):

```c
struct {
    unsigned char line_in   : 1, // bit 00
                  mic_boost : 1, // bit 01
                  mic_ptt   : 1, // bit 02
                  mic_trs   : 1, // bit 03
                  mic_bias  : 1, // bit 04
                  mic_xlr   : 1, // bit 05
                            : 1, // bit 06
                            : 1; // bit 07
} mic;
```

This is the **internal storage layout** in `prn->mic.mic_control`, not the
wire-byte position. The wire emission scrambles bits across two C0 cases per
the table above.

### 6.2 P1 polarity notes (cross-validated against deskhpsdr)

Two semantic notes about the P1 wire bits in §6.1, confirmed by reading
deskhpsdr's clean-room P1 emitter at
`deskhpsdr/src/old_protocol.c:2895-3010 [@120188f]`:

- **`mic_ptt` polarity is INVERTED.** The wire bit means "mic PTT *disabled*",
  not "mic PTT enabled". Thetis stores it as `prn->mic.mic_ptt` (misleading
  name) but the value sent is the disable flag. deskhpsdr names it
  `mic_ptt_enabled` and emits `if (mic_ptt_enabled == 0) { output_buffer[C1]
  |= 0x40; }` (`old_protocol.c:3000-3002 [@120188f]`). Console.cs:19764
  confirms (`MicPTTDisabled` setter calls `SetMicPTT(value)` directly — the
  field name on both ends is "disabled").
- **`mic_trs` polarity:** deskhpsdr names this `mic_ptt_tip_bias_ring` —
  `1` = "Tip is BIAS/PTT, Ring is Mic"; `0` = "Tip is Mic, Ring is BIAS/PTT"
  (`old_protocol.c:3008-3010 [@120188f]`).

NereusSDR `RadioConnection::setMicPTT(bool enabled)` takes the **enabled**
semantic (intuitive UI level) and inverts internally before wire emission.
`RadioConnection::setMicTipRing(bool tipIsHot)` takes the **Tip-is-mic**
semantic (matches the radOrionMicTip Thetis radio-button label) and inverts
internally.

### 6.3 P2 wire-byte map (sourced from deskhpsdr)

The P2 mic-jack wire emission is not in the open-source ChannelMaster tree —
no `networkproto2.c` exists in `Thetis/Project Files/Source/ChannelMaster/`.
The P2 path is in closed-source ChannelMaster.dll for Thetis.

deskhpsdr (`/Users/j.j.boyd/deskhpsdr`, current HEAD `@120188f`) is a clean-room
GPLv3 fork of piHPSDR by DL1YCF, originally G0ORX/N6LYT (2015), now maintained
by Heiko Amft DL1BZ. Its `src/new_protocol.c` has the full P2 mic-jack
emission. License header at `new_protocol.c:1-20 [@120188f]` — GPLv3+, fully
compatible with NereusSDR's GPLv3.

P2 mic-jack wire layout — single byte `transmit_specific_buffer[50]` in the
P2 TX-specific command packet (`new_protocol.c:1480-1502 [@120188f]`):

| Bit | Mask | Field | Polarity |
|---|---|---|---|
| 0 | `0x01` | `mic_linein` (= `line_in`) | `1 = line in` |
| 1 | `0x02` | `mic_boost` | `1 = boost on` |
| 2 | `0x04` | `mic_ptt_disabled` | `1 = PTT disabled` (inverted; matches P1) |
| 3 | `0x08` | `mic_ptt_tip_bias_ring` (= MicTipRing) | `1 = Tip is BIAS/PTT` |
| 4 | `0x10` | `mic_bias_enabled` | `1 = bias on` |
| 5 | `0x20` | `mic_input_xlr` (Saturn-only — XLR vs 3.5mm jack) | `1 = XLR jack` |
| 6 | (unused) |  |  |
| 7 | (unused) |  |  |

deskhpsdr P2 emission (`new_protocol.c:1480-1502 [@120188f]`):

```c
transmit_specific_buffer[50] = 0;
if (mic_linein)              { transmit_specific_buffer[50] |= 0x01; }
if (mic_boost)               { transmit_specific_buffer[50] |= 0x02; }
if (mic_ptt_enabled == 0)    { transmit_specific_buffer[50] |= 0x04; }  // inverted
if (mic_ptt_tip_bias_ring)   { transmit_specific_buffer[50] |= 0x08; }
if (mic_bias_enabled)        { transmit_specific_buffer[50] |= 0x10; }
if (mic_input_xlr)           { transmit_specific_buffer[50] |= 0x20; }

// Following byte: line-in gain encoded as -34..+12 dB → 0..31 in 1.5 dB steps
transmit_specific_buffer[51] = (int)((linein_gain + 34.0) * 0.6739 + 0.5);
```

The decoder side at `deskhpsdr/src/newhpsdrsim.c:707-712 [@120188f]` confirms
bit 5: `// Bit 5: Saturn Mic: 0=Mic 1=XLR`.

This **fully closes** the two research-bounded gaps surfaced in the original
draft of this section. Both `mic_input_xlr` (P2-only — Saturn G2 is the only
board with XLR hardware, and Saturn is P2-native) and the full P2 mic-jack
bit set can be ported byte-exact from deskhpsdr.

### 6.4 NereusSDR `RadioConnection` interface additions

New virtuals on `RadioConnection`:

```cpp
// src/core/RadioConnection.h additions (3M-1b)

// Hardware mic-jack bit setters. Implementations write into the
// appropriate wire frame (P1 case-10 / case-11; P2 transmit-specific
// byte 50) and retransmit via the existing CmdTx / CmdHighPriority cadence.
//
// All six wire bits are present on Hermes/Atlas/Orion/ANAN family. HL2
// implementations are no-ops (no jack hardware).
//
// P1 source: Thetis ChannelMaster/networkproto1.c [v2.10.3.13]
//   - mic_boost  → case 10 (C0=0x12) C2 bit 0
//   - line_in    → case 10 (C0=0x12) C2 bit 1
//   - mic_trs    → case 11 (C0=0x14) C1 bit 4
//   - mic_bias   → case 11 (C0=0x14) C1 bit 5
//   - mic_ptt    → case 11 (C0=0x14) C1 bit 6 (polarity: 1=disabled)
//   - mic_xlr    → not transmitted on P1 (Saturn is P2-only)
//
// P2 source: deskhpsdr src/new_protocol.c:1480-1502 [@120188f]
//   transmit_specific_buffer[50]:
//   - bit 0 (0x01) line_in
//   - bit 1 (0x02) mic_boost
//   - bit 2 (0x04) mic_ptt_disabled (polarity: 1=disabled — same as P1)
//   - bit 3 (0x08) mic_tip_ring (1=Tip is BIAS/PTT)
//   - bit 4 (0x10) mic_bias
//   - bit 5 (0x20) mic_xlr (Saturn G2 only — 1=XLR jack)
//
// All setter parameters are in the "intuitive" semantic
// (e.g. setMicPTT(true) means enable, not disable). Implementations
// invert internally before wire emission to match Thetis/deskhpsdr.
virtual void setMicBoost(bool on)        = 0;
virtual void setLineIn(bool on)          = 0;
virtual void setMicTipRing(bool tipHot)  = 0;  // true = Tip is mic
virtual void setMicBias(bool on)         = 0;
virtual void setMicPTT(bool enabled)     = 0;  // true = PTT enabled
virtual void setMicXlr(bool xlrJack)     = 0;  // true = XLR jack (P2-only; P1 stores only)
```

**P1 implementation** (`P1RadioConnection.cpp`): writes into the buffered C0
cases (`0x12` and `0x14`) in the existing transmit path, polarity-inverted as
documented.

**P2 implementation** (`P2RadioConnection.cpp`): writes byte 50 of the
transmit-specific command packet (the same packet identified in 3M-1a — port
1029 outbound, drive_level + ATT bytes already wired). `setMicXlr` writes
P2 byte 50 bit 5; on P1 it's storage-only with a brief comment ("Saturn G2
P2-only feature; P1 hardware has no XLR jack").

deskhpsdr is GPLv3-compatible. The license-preservation rule applies: the
ported file's NereusSDR header must include deskhpsdr's GPL header verbatim
plus the original piHPSDR + DL1BZ + G0ORX attribution. See
`docs/attribution/HOW-TO-PORT.md` for the multi-upstream pattern.

---

## 7. Per-mode TXA configuration (`radio.cs`)

### 7.1 `SetTXAMode` — DSPMode setter cascade

`radio.cs:2670-2696 [v2.10.3.13]`:

```csharp
if (current_dsp_mode == DSPMode.AM || current_dsp_mode == DSPMode.SAM)
{
    switch (sub_am_mode)
    {
        case 0: // double-sided AM
            WDSP.SetTXAMode(WDSP.id(thread, 0), DSPMode.AM);
            break;
        case 1:
            WDSP.SetTXAMode(WDSP.id(thread, 0), DSPMode.AM_LSB);
            break;
        case 2:
            WDSP.SetTXAMode(WDSP.id(thread, 0), DSPMode.AM_USB);
            break;
    }
}
else
    WDSP.SetTXAMode(WDSP.id(thread, 0), value);
```

For AM/SAM, the `sub_am_mode` (0=DSB, 1=LSB-only, 2=USB-only) dispatches.
For all other modes, `SetTXAMode(value)` directly.

`SubAMMode` setter at `radio.cs:2699-2728 [v2.10.3.13]` runs the same dispatch
when the user changes which sideband to transmit.

### 7.2 `SetTXABandpassFreqs`

`radio.cs:2730-2780 [v2.10.3.13]`:

```csharp
public void SetTXFilter(int low, int high)
{
    tx_filter_low = low;
    tx_filter_high = high;
    if (update) {
        if (low != tx_filter_low_dsp || high != tx_filter_high_dsp || force) {
            WDSP.SetTXABandpassFreqs(WDSP.id(thread, 0), low, high);
            tx_filter_low_dsp = low;
            tx_filter_high_dsp = high;
        }
    }
}

public int TXFilterLow { ... WDSP.SetTXABandpassFreqs(..., value, tx_filter_high) ... }
public int TXFilterHigh { ... WDSP.SetTXABandpassFreqs(..., tx_filter_low, value) ... }
```

`SetTXFilter` is the bulk setter; `TXFilterLow`/`TXFilterHigh` are individual
edge setters that preserve the other edge.

### 7.3 NereusSDR mapping

```cpp
// TxChannel additions (3M-1b)
void setTxMode(DSPMode mode);                          // calls WDSP.SetTXAMode
void setTxBandpass(int lowHz, int highHz);             // calls WDSP.SetTXABandpassFreqs
void setSubAmMode(int sub);                             // 0=DSB, 1=LSB, 2=USB (deferred to 3M-3b)
```

For 3M-1b SSB scope, `setTxMode` is called for LSB / USB / DIGL / DIGU only.
AM / SAM / FM / DSB / DRM / CW are rejected by `BandPlanGuard` with tooltip
("AM/FM TX coming in Phase 3M-3 (audio modes)" — see §12.11).

### 7.4 What we port vs. defer

**Port:**

- `setTxMode` for LSB/USB/DIGL/DIGU.
- `setTxBandpass` for SSB filter widths (per existing slice `RXFilter*` /
  `TXFilter*` mapping in 3E).

**Defer:**

- `setSubAmMode` (AM/SAM sideband selection — 3M-3b).
- AM_LSB / AM_USB / DSB modes (3M-3b).

---

## 8. VOX state machine

### 8.1 `chkVOX_CheckedChanged` flow (already covered in §2.6)

UI toggle → `_vox_enable` flag → `Audio.VOXActive` clears on disable →
SetGeneralSetting persists → `LineInBoost = line_in_boost` re-triggers the
mic-gain cascade.

### 8.2 `vox_ptt` integration into PTT-source dispatch

`console.cs:25429 [v2.10.3.13]`:

```csharp
bool vox_ptt = vox_ok && Audio.VOXActive;
```

`vox_ok` is a precondition (typically `_vox_enable && !_tx_inhibit`).
`Audio.VOXActive` is set by the WDSP DEXP detector when input level exceeds
the configured threshold. The combined flag flows into the MOX state
machine alongside `mic_ptt`, `cw_ptt`, `cat_ptt`, `_tci_ptt`.

### 8.3 VOX threshold (mic-boost-aware) — covered in §3.3

`CMSetTXAVoxThresh` scales the user's threshold by `Audio.VOXGain` when
`MicBoost` is on.

### 8.4 VOX hang time / attack / release / hysteresis

`console.cs:14707-14716 [v2.10.3.13]`:

```csharp
public double VOXHangTime
{
    get { return vox_hang_time; }
    set {
        vox_hang_time = value;
        if (!IsSetupFormNull) SetupForm.VOXHangTime = (int)value;
    }
}
```

The Setup form holds the master values; Console mirrors. The full DEXP knob
set (attack, release, hysteresis, expansion ratio, etc.) lives in the Setup
DEXP page and wires to `WDSP.SetDEXP*`. For 3M-1b basic VOX, NereusSDR ports
**threshold + gain + hang-time only**. Full DEXP deferred to 3M-3a-iii.

### 8.5 NereusSDR mapping

```cpp
// TransmitModel additions (3M-1b)
bool   voxEnabled        {false};   // matches Audio.VOXEnabled default
int    voxThresholdDb    {-40};     // ptbVOX slider value (Thetis range from
                                    // ptbVOX.Minimum / ptbVOX.Maximum)
float  voxGainScalar     {1.0f};    // matches Audio.VOXGain default
int    voxHangTimeMs     {500};     // sane SSB default
int    antiVoxGainDb     {0};       // anti-VOX user-controlled gain
bool   antiVoxSourceVax  {false};   // matches Audio.AntiVOXSourceVAC default
                                    // (renamed VAC→VAX; false = local-RX source)

// MoxController additions
void setVoxEnabled(bool on);            // wires CMSetTXAVoxRun mode-gate logic
void setVoxThreshold(int dB);           // applies mic-boost-aware scaling
void setVoxHangTime(int ms);
void setAntiVoxGain(int dB);
void setAntiVoxSourceVax(bool useVax);  // wires CMSetAntiVoxSourceWhat
```

WDSP wiring routes through `TxChannel`:

```cpp
// TxChannel additions (3M-1b)
void setVoxRun(bool run);                       // → WDSP.SetDEXPRunVox
void setVoxAttackThreshold(double scaled);      // → WDSP.SetDEXPAttackThreshold
void setVoxHangTime(double seconds);            // → WDSP.SetDEXPHangTime (deferred for full ctl)
void setAntiVoxGain(double gain);               // → WDSP.SetAntiVOXGain
void setAntiVoxRun(bool run);                   // → WDSP.SetAntiVOXRun
```

---

## 9. Anti-VOX state machine

### 9.1 `CMSetAntiVoxSourceWhat` path-toggle (covered in §3.4)

Two paths: VAX-source (per-RX VAC enable flags) vs. local-RX-source
(unconditional all-RX). 3M-1b ports the local-RX path unconditionally; VAX-source
path deferred.

### 9.2 `SetAntiVOXSourceStates` state-machine (DEFERRED — too rich for 3M-1b)

`console.cs:27602-27721 [v2.10.3.13]` has roughly 15 separate state-states
calls inside `chkRX2_CheckedChanged` and similar transitions. The states form
a 3-bit bitmap (RX1=1, RX1S=2, RX2=4, RX2EN=4 with VAC bypass logic).

For 3M-1b, NereusSDR uses the path-agnostic version: when anti-VOX is
enabled and source is local-RX, all three slots fire `1`. When MOX engages,
the underlying `SetAntiVOXRun(0, false)` covers the silencing — no need for
per-slot state composition. Full state-machine deferred to 3M-3a (where
VAX TX integration brings back the per-slot logic).

### 9.3 NereusSDR anti-VOX wiring

```cpp
// MoxController slot
void onTxDspModeChanged(DSPMode mode);
    // → if (voxEnabled && mode in voiceFamily) emit setVoxRun(true);
    // → call setAntiVoxSourceWhat(antiVoxSourceVax) — port of CMSetAntiVoxSourceWhat
```

---

## 10. MOX-vs-RX path: "RX still plays during MOX" interaction

### 10.1 Thetis behaviour (covered in §1.2 row `Audio.MOX`)

`audio.cs:349-384 [v2.10.3.13]` — MOX setter flips per-RX IVAC mox states
based on `rx2_enabled` and `vfob_tx`. The active TX RX gets `IVACmox=1`
(silenced); the other RX gets `IVACmox=0` (keeps playing). This is intentional
full-duplex on the non-TX RX.

### 10.2 NereusSDR's "RX still plays during MOX" cosmetic bug

Per PR #144 known-deferred-items: "Audio still flows from the speakers during
a TUN press despite `setActiveSlice(false)` being called."

Suspected root cause (validated via investigation in 3M-1b implementation):

1. `MoxController::hardwareFlipped(true)` fires.
2. Connected slot calls `SliceModel::setActiveSlice(false)` on the active TX
   slice.
3. **But `AudioEngine::rxBlockReady` doesn't check `slice.activeSlice` flag**
   before pushing the per-slice block into the master mixer. Or: the flag
   isn't being set. Or: the Mox state isn't readable from inside
   rxBlockReady.

Whichever of those it turns out to be, the fix lives in `AudioEngine` (gate
the per-slice push) AND in the Mox→Slice signal wiring. The fix is part of
the MON wiring task per JJ's lock; not a separate task.

### 10.3 NereusSDR fold-in implementation (per JJ's lock)

Single-pass design (see also §4.3):

```cpp
// AudioEngine::rxBlockReady — RX-leak gate
void AudioEngine::rxBlockReady(int sliceId, const float* samples, int frames)
{
    SliceModel* slice = m_radio ? m_radio->slices().forId(sliceId) : nullptr;
    if (!slice) return;

    // 3M-1b fold: silence active TX slice's RX audio during MOX. Non-active
    // slices (RX2 in TX-on-VFO-A, RX1 in TX-on-VFO-B) keep playing. Matches
    // Thetis IVAC mox state-machine in audio.cs:349-384 [v2.10.3.13].
    const bool moxActive = m_radio->moxState();
    if (moxActive && slice->isActiveSlice()) {
        return;
    }

    // ... existing mix into MasterMixer ...
}

// AudioEngine::txMonitorBlockReady — TXA siphon mix-in
void AudioEngine::txMonitorBlockReady(const float* samples, int frames)
{
    if (!m_txMonitorEnabled.load(std::memory_order_acquire)) return;
    const float vol = m_txMonitorVolume.load(std::memory_order_acquire);
    m_masterMix.addChannel(kTxMonitorSlotId, samples, frames, vol);
    // MasterMixer flushes to speakers on the same DSP-thread cadence.
}
```

Three-way audio state matrix (after fold):

| MOX | MON | Active slice plays? | Non-active slice plays? | TXA siphon plays? |
|---|---|---|---|---|
| OFF | * | Yes | Yes | No |
| ON  | OFF | **No** (gated) | Yes | No |
| ON  | ON  | **No** (gated) | Yes | **Yes** |

Matches Thetis behaviour where MON is the "hear myself" feature and
non-active RX always plays.

### 10.4 What we port vs. defer

**Port:**

- `AudioEngine::rxBlockReady` activeSlice + MOX gate (the RX-leak fix).
- `AudioEngine::txMonitorBlockReady` slot + atomic enable + volume.
- `TxChannel::sip1OutputReady` signal emission from `Stage::Sip1` siphon.
- `MoxController::moxState()` read access from `AudioEngine` (cross-thread
  via std::atomic mirror).

**Defer:**

- VAX TX-on-VFO-B mode (rx2_enabled + vfob_tx logic from Thetis). NereusSDR
  3M-1b is single-RX TX only.
- IVAC mon (NereusSDR has VAX; 3M-3+).

---

## 11. HL2 mic-jack capability flag (already locked)

### 11.1 Why this matters

Per master design §5.2.2 + §5.2.3: HL2 has no radio-side mic jack hardware.
UI must hide the Radio-Mic radio button + all sub-options. PC mic is the only
TX path on HL2.

### 11.2 `BoardCapabilities::hasMicJack` flag

Add to `src/core/BoardCapabilities.h`:

```cpp
// Radio-side microphone input present.
//
// True for Hermes / Atlas / Orion / ANAN family + Saturn G2.
// False for HermesLite 2 (no radio-side mic input).
//
// Source: NereusSDR-original; derived from Thetis Setup→Audio→Primary
// per-board panel visibility:
//   panelSaturnMicInput  (setup.designer.cs:8613 [v2.10.3.13])
//   panelOrionMic        (setup.designer.cs:8661 [v2.10.3.13])
//   panelOrionPTT        (setup.designer.cs:8709 [v2.10.3.13])
//   panelOrionBias       (setup.designer.cs:8755 [v2.10.3.13])
//   grpBoxMic            (setup.designer.cs:5154 [v2.10.3.13])
// All hidden when CurrentModel == HermesLite2 per setup.cs:19834-20310
// RadioModelChanged() per-model if-ladder [@501e3f5].
//
// Drives Setup → Audio → TX Input page Radio-Mic radio button visibility.
// 3M-1b.
bool hasMicJack {true};
```

Defaults `true` (matches all but HL2). HL2 entry in `BoardCapsTable` sets
`false`.

### 11.3 Per-board defaults

| Board | hasMicJack |
|---|---|
| Atlas | `true` |
| Hermes / Hermes-II | `true` |
| Angelia | `true` |
| Orion / Orion-MkII | `true` |
| ANAN-7000 / 8000 / Anvelina | `true` |
| Saturn G2 / G2-1K | `true` |
| **HermesLite 2** | **`false`** |

### 11.4 Test approach

Unit test `tst_setup_tx_input_page.cpp` parametrized over `caps.hasMicJack`:

- `caps.hasMicJack == true` → Radio-Mic radio button enabled, sub-options
  visible per family.
- `caps.hasMicJack == false` → Radio-Mic radio button hidden + disabled with
  tooltip "Radio mic jack not present on Hermes Lite 2"; PC Mic is the only
  selectable source.

Bench verification: launch HL2 → Setup → Audio → TX Input → confirm Radio
Mic hidden. Launch G2 → confirm full radio-mic sub-options.

---

## 12. Open decisions to lock in implementation plan

The implementation plan picks defaults per-decision with one-line rationale.
Recommendations here; JJ overrides on plan review if any disagree.

### 12.1 Mic-source UX (radio button vs dropdown vs auto-detect)

**Recommend:** Radio buttons on Setup → Audio → TX Input page (PC Mic / Radio
Mic). TxApplet shows a small read-only label/badge with the active selection.

**Why:** Matches Thetis's `radMicIn`/`radLineIn`/`radSaturn3p5mm` radio-button
pattern. Set-and-forget config; not session-frequent. Caveat: if users
frequently swap (contesting PC mic, ragchew radio mic), they'd want it on
TxApplet — recommend Setup-only and add to TxApplet later if reported.

### 12.2 PortAudio backend default per OS

**Recommend:** macOS = CoreAudio, Linux = PipeWire (when
`NEREUS_HAVE_PIPEWIRE`) with PulseAudio fallback, Windows = WASAPI shared.

**Why:** Matches AudioEngine 3O VAX behaviour and modern OS conventions.

### 12.3 Anti-VOX source default

**Recommend:** Local-RX (`antiVoxSourceVax = false`). Matches Thetis
`Audio.AntiVOXSourceVAC = false` default.

**Why:** New users without VAX configured get sane behaviour. VAX users opt
in.

### 12.4 VOX defaults

**Recommend:** Off at startup (always). Persistence-loaded threshold / gain /
hang-time on next start, but enable flag itself doesn't persist (always
loads as off).

**Why:** VOX is a "user must enable" feature — safety-first. Avoids surprise
TX-on-startup if VOX state somehow gets stuck enabled.

### 12.5 MON defaults

**Recommend:** OFF at startup. Volume default 0.5 (50%; matches Thetis's
fixed-coefficient `0.5` default in `cmaster.SetAAudioMixVol` at
`audio.cs:417 [v2.10.3.13]`). Headphone-only toggle deferred to 3M-3c.

**Why:** OFF keeps users from unexpected headphone audio. 0.5 matches
Thetis's literal 0.5 default. Headphone-only fold-in adds scope; defer.

### 12.6 HL2 mic-jack flag (LOCKED — see §11)

`BoardCapabilities::hasMicJack {true};` field; HL2 sets `false`.

### 12.7 PcMicSource architecture (LOCKED — see §0.3)

PcMicSource taps `AudioEngine::pullTxMic(float*, int)` (new public method
that drains `m_txInputBus`). No duplicate audio stack.

### 12.8 Setup page location

**Recommend:** `src/gui/setup/AudioTxInputPage.{h,cpp}` (matches existing flat
`AudioVaxPage.h` / `AudioDevicesPage.h` / `AudioTciPage.h` convention). NOT
`src/gui/setup/audio/TxInputPage.{h,cpp}` as the master design suggested.

**Why:** Existing audio pages live flat; no `audio/` subdir for setup pages.
The `audio/` subdir DOES exist for audio core (`PortAudioBus`, `PipeWireBus`,
`MasterMixer`) but not for setup UI.

### 12.9 Mic-jack P2 wire-bit emission (gap closed — see §6.3)

**Decision:** Port full P2 byte-50 mic-jack bit map from deskhpsdr's
`new_protocol.c:1480-1502 [@120188f]`. All six bits emit on the wire.
Polarity inversions for `mic_ptt_disabled` (bit 2) match Thetis P1 (which
also inverts). The file porting deskhpsdr logic carries the deskhpsdr +
piHPSDR + G0ORX attribution headers verbatim per `HOW-TO-PORT.md`.

### 12.10 MicXlr wire-bit emission (gap closed — see §6.3)

**Decision:** Port `mic_input_xlr` as P2 byte 50 bit 5 (`0x20`) per
deskhpsdr `new_protocol.c:1500-1502 [@120188f]`. P1 stores only with a
comment noting Saturn G2 is P2-native and no P1 hardware has XLR jacks.
Saturn G2 XLR users get full XLR routing on day one of 3M-1b.

### 12.11 SSB mode-gating tooltip text

**Recommend:** "AM/FM TX coming in Phase 3M-3 (audio modes)" — friendly,
version-anchored.

**Why:** Users who try to MOX in AM/FM see a clear reason. Tooltip shows on
the rejected control (BandPlanGuard surface). Status-bar transient toast
"Mode TX not yet supported" provides a parallel signal.

### 12.12 Mic-gain default (safety)

**Recommend:** `TransmitModel::micGainDb = -6` (conservative −6 dB default)
on first run. Persists per-MAC.

**Why:** Default unity-gain mic + 100W PA can produce audible distortion at
default knob positions; SWR foldback won't catch all overdrive. −6 dB on
first run gives users headroom to walk the gain up while monitoring ALC.

---

## 13. Deferred to later phases

| Item | Phase | Reason |
|---|---|---|
| AM / SAM / DSB / FM / DRM TX | 3M-3b | Mode-gated TXA stages (`ammod`, `fmmod`) inactive in 3M-1b |
| CW TX (sidetone, firmware keyer, QSK) | 3M-2 | Different code path entirely |
| TX EQ / Leveler / CFC / CFComp / Compressor / Phase rotator | 3M-3a | Speech-processing chain |
| Full DEXP parameter set (attack / release / hysteresis envelopes) | 3M-3a-iii | Today's basic VOX is threshold + gain + hang-time only |
| 2-Tone test | 3M-1c | Polish-phase |
| TX EQ profile schema / Profile combo | 3M-1c | Polish-phase |
| TCI PTT path (TCI-bus PTT) | 3J | TCI server lives in 3J |
| VAX mic-source path for digital modes | 3M-3 | NereusSDR VAX integration with TX |
| PureSignal feedback loop (`calcc`, `IQC`) | 3M-4 | Calibration channel + adaptive predistortion |
| Anti-VOX full state-machine (RX2 / VAC source-state composition) | 3M-3a | Path-agnostic version sufficient for 3M-1b |
| MON headphone-only toggle | 3M-3c | Speakers/headphones bus separation required |
| TX-on-VFO-B (`rx2_enabled && vfob_tx` per Thetis MOX setter) | 3M-3 / 3F | NereusSDR 3M-1b is single-RX TX |
| Wave playback gain path in `CMSetTXAPanelGain1` | 3M-6 | Recording phase |

---

## 14. Risk surface highlights

### 14.1 PortAudio buffer tuning

**Risk:** Mic latency too high → PTT-press to first-audible-audio > 50 ms.
Or buffer underruns → audible glitches.

**Mitigation:** Default 256-sample (~5.3 ms at 48 kHz) on macOS + Linux;
1024-sample on Windows (WASAPI shared). Setup → Audio → TX Input page exposes
buffer-size slider + ms-latency readout.

### 14.2 Mic gain calibration vs. ALC interaction

**Risk:** Default mic-gain unity but PA at 100 W → audible distortion at
default volumes can damage front-end without tripping SWR foldback.

**Mitigation:** Default `MicPreamp` = −6 dB (per §12.12). ALC threshold
conservative. Bench testing must verify unity-gain TX output doesn't
overdrive ALC at default `MicPreamp`.

### 14.3 VOX false-trigger from RX audio (without anti-VOX)

**Risk:** Users without anti-VOX configured + PC speaker on get RX audio
bleeding into mic → false VOX trigger → unexpected TX.

**Mitigation:** Anti-VOX defaults to local-RX (per §12.3). Anti-VOX
source-states match Thetis path-agnostic version. Document in tooltips.

### 14.4 P2 byte-50 polarity bench-verify

**Risk:** P2 byte-50 mic-jack bits ported from deskhpsdr instead of Thetis;
polarity or bit assignment could differ subtly between the two clean-room
emitters.

**Mitigation:** Bench wireshark on G2 — capture Thetis with mic-jack toggles
across each bit and diff against NereusSDR's emission. Six bits × 2 states
= 12 wire snapshots needed for the full verification. Same pcap-byte-diff
approach used in 3M-1a to find the port-1029 bug.

### 14.5 RX-leak-during-MOX cosmetic bug (folded into MON)

**Risk:** Existing bug from 3M-1a. Fold-in to MON wiring task; risk that the
`activeSlice` gate fix is more invasive than expected (cross-thread signal
timing, lock-free `rxBlockReady` path).

**Mitigation:** If the gate fix turns out to require deeper rework, spin out
as a separate task within 3M-1b. Don't let MON wiring block on it; MON
itself can ship with the leak still cosmetic.

### 14.6 Mic-boost-aware VOX threshold scaling

**Risk:** Threshold scaling with `MicBoost` not applied identically to
Thetis → VOX feels different at +20dB boost vs. without.

**Mitigation:** Port `cmaster.cs:1054-1059 [v2.10.3.13]` math byte-exact.
Unit test `tst_vox_threshold_mic_boost.cpp` parametrized over `MicBoost ∈
{true, false}` and threshold values; cross-check against Thetis values from
the source.

---

## 15. Risk profile summary

**MEDIUM** — same as master design §5.2.5.

Higher-risk items than 3M-1a (which was "first MOX byte"):

- Real mic capture infrastructure (PortAudio + PipeWire integration, but
  3O-tested for VAX RX).
- VOX live-state machine with mic-boost-aware threshold scaling.
- Anti-VOX source-state composition (path-agnostic version is straightforward
  but needs bench verification).
- MON path with RX-leak-during-MOX fold (potential surprise from underlying
  bug).
- Mic-jack hardware-bit wire emission across both P1 + P2 (P2 byte 50
  ported from deskhpsdr; bench-verify polarity per §14.4).

Lower-risk items than 3M-1a:

- No new MOX wire bytes (already wired in 3M-1a).
- TX I/Q frame format already proven.
- WDSP TXA pipeline already constructed (just stage activation changes).

---

## 16. Workflow shape (mirrors 3M-1a)

Single branch, single PR, one commit per task. This pre-code review is
commit 1; the implementation plan (next) is commit 2; impl commits 3..N;
verification + post-code review at the tail.

Per-task discipline (rigid TDD):

1. State the pre-code review section being ported.
2. Write the failing test first (or for source-first ports: state cite, write
   the failing assertion that captures the cited behaviour).
3. Implement to green.
4. Run the relevant `ctest` subset locally before commit.
5. GPG-sign the commit (no `--no-gpg-sign`; per `feedback_gpg_sign_commits`).
6. After each commit, run a two-stage subagent review (spec compliance + code
   quality). Fresh subagent per task — no shared state.
7. Resolve review feedback (if any) via fixup commit on the same task, then
   advance.

**Inline cite stamp:** `[v2.10.3.13]` (Thetis tag) or `[@<shortsha>]` when no
tagged release applies. The verifier script
(`scripts/verify-inline-tag-preservation.py`) runs in the pre-commit hook
chain — `NEREUS_THETIS_DIR=/Users/j.j.boyd/Thetis` must be set in the shell
before each commit.

**Bench-test gate:** HL2 first (lower stakes), then G2, BEFORE PR opens. PR
review happens on already-validated code.

---

End of pre-code review.
