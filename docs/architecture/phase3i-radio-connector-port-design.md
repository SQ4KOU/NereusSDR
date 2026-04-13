# Phase 3I — Radio Connector & Radio-Model Port

**Status:** Design — awaiting approval
**Date:** 2026-04-12
**Author:** JJ Boyd ~KG4VCF, Co-Authored with Claude Code
**Branch:** `feature/phase3i-radio-connector-port`

---

## 0. TL;DR

Fill in Protocol 1 support across every ANAN/Hermes-family board and reach
feature-wiring parity with Thetis for all radios. When this phase ships, any
supported P1 radio (HL2, ANAN-10/100/10E/100B/100D/200D, Metis) behaves
exactly like a Saturn on P2 does today: discover → connect → tune → hear
demodulated SSB → see spectrum/waterfall/meters → persist settings. P2 is
already working; this phase audits P2 against the new capability registry
and fills in `SaturnMKII`, `ANAN-G2-1K`, and `AnvelinaPro3` dispatch.

**Out of scope by design:** TX IQ generation (plumbed cold — commands go on
the wire, IQ source stays silent until the TX phase), PureSignal feedback
DSP (toggle persists, tab visible, feedback loop deferred), TCI protocol,
RedPitaya board, sidetone generator, firmware flashing, multi-radio
simultaneous connection.

**Source-first protocol applies throughout.** Every function body cites its
Thetis origin `file:line`. The P2 port (`P2RadioConnection.cpp`, 890 LOC) is
the proven template.

---

## 1. Architecture Overview

### 1.1 Success criterion

When Phase 3I ships, a user with any supported P1 radio can:

1. Launch NereusSDR, open ConnectionPanel, see the radio in the discovery list.
2. Click Connect → radio connects, status flips to `Connected`.
3. Tune RX1 to a live SSB signal (e.g., 14.200 MHz LSB).
4. **Hear demodulated SSB audio** through `WdspEngine` → `AudioEngine`,
   identical to what a Saturn does today.
5. See spectrum + waterfall updating in real time.
6. See S-meter, noise floor, signal meters updating.
7. Adjust preamp / attenuator / antenna / sample-rate from the Hardware page
   and hear the change.
8. Disconnect cleanly, reconnect, settings persisted.

The implicit contract: `P1RadioConnection::iqDataReceived` must emit samples
in the same format `ReceiverManager` already consumes from
`P2RadioConnection` — interleaved `float` I/Q pairs normalised to `[-1.0,
1.0]`, per-hardware-receiver index. That's the base-class contract in
`src/core/RadioConnection.h:82`; P1 just has to honor it.

### 1.2 System diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                      Main thread                                 │
│  ┌──────────────────┐          ┌────────────────────────┐       │
│  │ HardwarePage     │◄─────────│ RadioModel (existing)   │       │
│  │ + nested tabs    │          │ capability-gated props  │       │
│  └──────────────────┘          └──────────┬─────────────┘       │
│         ▲                                  │                    │
│         │ reads                            │ signals            │
│  ┌──────┴────────────┐                     │                    │
│  │ BoardCapabilities │◄────────────────────┘                    │
│  │ (constexpr table) │                                           │
│  └───────────────────┘                                           │
└──────────────────────────────────────────┬──────────────────────┘
                                            │ auto-queued signals
┌───────────────────────────────────────────▼──────────────────────┐
│                    Connection worker thread                      │
│  ┌───────────────────┐        ┌──────────────────────┐           │
│  │ RadioConnection   │◄───────│ RadioDiscovery       │           │
│  │ (abstract base)   │        │ (ports mi0bot        │           │
│  └──────┬────────────┘        │  clsRadioDiscovery)  │           │
│         │ factory                └──────────────────┘           │
│    ┌────┴─────────┐                                              │
│    ▼              ▼                                              │
│ P1RadioConnection  P2RadioConnection (existing, 890 LOC)         │
│ (NEW)              audited + SaturnMKII/G2-1K/AnvelinaPro3       │
└──────────────────────────────────────────────────────────────────┘
```

### 1.3 Files touched

**New:**
- `src/core/HpsdrModel.h` — `HPSDRModel` and `HPSDRHW` enums ported 1:1 from
  `mi0bot/Thetis@Hermes-Lite Project Files/Source/Console/enums.cs:109` and
  `:388`. Integer values preserved exactly including the `7..9` reserved gap.
- `src/core/BoardCapabilities.{h,cpp}` — pure `constexpr` capability table
  with one entry per `HPSDRHW` value. Lookup by board or by model.
- `src/core/P1RadioConnection.{h,cpp}` — the P1 wire-format connection class;
  source-first port of Thetis `networkproto1.c` + `NetworkIO.cs` + per-board
  quirks. Plugs into the existing `RadioConnection::create` factory.
- `src/gui/setup/HardwarePage.{h,cpp}` — container page with nested
  `QTabWidget` mirroring Thetis setup.cs "Hardware Config" sub-tabs.
- `src/gui/setup/hardware/*.{h,cpp}` — one widget per sub-tab (Radio Info,
  Antenna/ALEX, OC Outputs, XVTR, PureSignal, Diversity, PA Calibration, HL2
  I/O Board, Bandwidth Monitor).
- `tests/tst_hpsdr_enums.cpp`, `tst_board_capabilities.cpp`,
  `tst_radio_discovery_parse.cpp`, `tst_p1_wire_format.cpp`,
  `tst_p1_board_quirks.cpp`, `tst_p1_loopback_connection.cpp`,
  `tst_hardware_page_capability_gating.cpp`,
  `tst_hardware_page_persistence_roundtrip.cpp`,
  `tst_connection_panel_saved_radios.cpp`, `tst_reconnect_on_silence.cpp`.
- `tests/fixtures/discovery/*.hex` — captured P1 + P2 discovery replies.

**Modified:**
- `src/core/RadioConnection.cpp` — factory P1 branch constructs
  `P1RadioConnection` instead of logging "not yet implemented".
- `src/core/RadioDiscovery.{h,cpp}` — port mi0bot `clsRadioDiscovery`:
  tunable timing profiles, NIC walker, dual P1+P2 probe, MAC de-dup.
- `src/core/P2RadioConnection.{h,cpp}` — audit only. Read from
  `BoardCapabilities` instead of hard-coded Saturn assumptions; add
  `SaturnMKII`, `ANAN_G2_1K`, `AnvelinaPro3` dispatch. No wire-format rewrite.
- `src/models/RadioModel.{h,cpp}` — expose `currentBoardCapabilities()` for
  UI + WdspEngine gating.
- `src/gui/ConnectionPanel.{h,cpp}` — expand from 315 LOC skeleton to full
  Thetis-equivalent UI: radio list (ported from `ucRadioList.cs`), manual-add
  dialog (ported from `frmAddCustomRadio.cs`), saved-radio persistence,
  rescan, forget, connect/disconnect. Stays a modal spawned from
  `MainWindow::showConnectionPanel`.
- `docs/architecture/radio-abstraction.md` — corrections (§8).

**Deleted:** none.

### 1.4 Threading model

Unchanged from the existing P2 path. Each `RadioConnection` instance lives on
its own worker thread (`QThread`). All socket I/O, timers, and command
composition happen on the worker. Public slots are auto-queued from the main
thread. Signals back to main thread are auto-queued. No shared mutexes; all
cross-thread data flow uses `Q_DECLARE_METATYPE` + `Qt::QueuedConnection`.

---

## 2. BoardCapabilities + HPSDR Enums

### 2.1 Enum port

Ported 1:1 from `mi0bot/Thetis@Hermes-Lite` branch. Integer values preserved
exactly — including the `7..9` reserved gap in `HPSDRHW` — per the
source-first "preserve constants exactly" rule.

```cpp
// src/core/HpsdrModel.h
namespace NereusSDR {

// Logical radio model — what the user says they have.
// Source: enums.cs:109 (mi0bot/Thetis Hermes-Lite branch)
enum class HPSDRModel : int {
    FIRST        = -1,
    HPSDR        = 0,   // Atlas/Metis kit
    HERMES       = 1,
    ANAN10       = 2,
    ANAN10E      = 3,
    ANAN100      = 4,
    ANAN100B     = 5,
    ANAN100D     = 6,
    ANAN200D     = 7,
    ORIONMKII    = 8,
    ANAN7000D    = 9,
    ANAN8000D    = 10,
    ANAN_G2      = 11,  // G8NJJ
    ANAN_G2_1K   = 12,  // G8NJJ
    ANVELINAPRO3 = 13,
    HERMESLITE   = 14,  // MI0BOT
    REDPITAYA    = 15,  // DH1KLM — enum slot preserved, implementation deferred (§9)
    LAST         = 16
};

// Physical board — what's actually on the wire.
// Source: enums.cs:388 + ChannelMaster/network.h:446
enum class HPSDRHW : int {
    Atlas       = 0,   // HPSDR kit (aka Metis in PowerSDR)
    Hermes      = 1,   // ANAN-10 / ANAN-100
    HermesII    = 2,   // ANAN-10E / ANAN-100B
    Angelia     = 3,   // ANAN-100D
    Orion       = 4,   // ANAN-200D
    OrionMKII   = 5,   // ANAN-7000DLE / ANAN-8000DLE / AnvelinaPro3
    HermesLite  = 6,   // Hermes Lite 2
    // 7..9 reserved — DO NOT REUSE (Thetis wire format compares these ints)
    Saturn      = 10,  // ANAN-G2
    SaturnMKII  = 11,  // ANAN-G2 MkII board revision
    Unknown     = 999
};

constexpr HPSDRHW boardForModel(HPSDRModel m) noexcept;
constexpr const char* displayName(HPSDRModel m) noexcept;

} // namespace NereusSDR
```

`BoardType` in `src/core/RadioDiscovery.h:16` is deleted. Every caller
migrates to `HPSDRHW`. The previous `Griffin=2` slot was a documentation
mistake — mi0bot clarifies it as `HermesII` (ANAN-10E / ANAN-100B). This is
the only backwards-incompat break in Phase 3I and it is correcting a bug.

### 2.2 BoardCapabilities struct

Pure data. No virtuals. One `constexpr` entry per `HPSDRHW`.

```cpp
struct BoardCapabilities {
    HPSDRHW         board;
    ProtocolVersion protocol;

    // Identity & sizing
    int  adcCount;            // 1 or 2
    int  maxReceivers;        // wire-format max
    std::array<int, 4> sampleRates;   // e.g. {48000,96000,192000,384000}
    int  maxSampleRate;

    // RF front-end
    struct {
        int minDb;
        int maxDb;
        int stepDb;
        bool present;
    } attenuator;
    struct {
        bool present;
        bool hasBypassAndPreamp;   // OrionMkII+
    } preamp;

    // I/O hardware
    int  ocOutputCount;       // 7 for most; 0 for HL2
    bool hasAlexFilters;      // false for HL2/Atlas
    bool hasAlexTxRouting;    // implies hasAlexFilters
    int  xvtrJackCount;
    int  antennaInputCount;

    // Feature flags
    bool hasPureSignal;
    bool hasDiversityReceiver;     // requires adcCount == 2
    bool hasStepAttenuatorCal;
    bool hasPaProfile;

    // HL2-specific extras
    bool hasBandwidthMonitor;      // mi0bot ChannelMaster/bandwidth_monitor
    bool hasIoBoardHl2;            // mi0bot HPSDR/IoBoardHl2.cs
    bool hasSidetoneGenerator;     // mi0bot ChannelMaster/sidetone

    // Firmware gating
    int  minFirmwareVersion;       // refuse below
    int  knownGoodFirmware;        // warn below

    // Display
    const char* displayName;       // "ANAN-100D (Angelia)"
    const char* sourceCitation;    // e.g. "Thetis specHPSDR.cs:312-389"
};

namespace BoardCapsTable {
    constexpr const BoardCapabilities& forBoard(HPSDRHW) noexcept;
    constexpr const BoardCapabilities& forModel(HPSDRModel) noexcept;
    constexpr std::span<const BoardCapabilities> all() noexcept;
}
```

### 2.3 Sample entry — Hermes Lite 2

```cpp
// src/core/BoardCapabilities.cpp
// ─── HermesLite (HL2) ─────────────────────────────────────────────────
// Sources:
//   enums.cs:396                       board enum slot
//   specHPSDR.cs:1180-1240             HL2 branch in setup
//   HPSDR/IoBoardHl2.cs:1-80           HL2 I/O board TLV
//   ChannelMaster/bandwidth_monitor.h  HL2 LAN throttle detector
//   HL2 firmware v72 release notes     min-fw rationale
constexpr BoardCapabilities kHermesLite = {
    .board               = HPSDRHW::HermesLite,
    .protocol            = ProtocolVersion::Protocol1,
    .adcCount            = 1,
    .maxReceivers        = 4,
    .sampleRates         = {48000, 96000, 192000, 384000},
    .maxSampleRate       = 384000,
    .attenuator          = { .minDb = 0, .maxDb = 60, .stepDb = 1, .present = true },
    .preamp              = { .present = false, .hasBypassAndPreamp = false },
    .ocOutputCount       = 0,
    .hasAlexFilters      = false,
    .hasAlexTxRouting    = false,
    .xvtrJackCount       = 0,
    .antennaInputCount   = 1,
    .hasPureSignal       = false,
    .hasDiversityReceiver= false,
    .hasStepAttenuatorCal= false,
    .hasPaProfile        = false,
    .hasBandwidthMonitor = true,
    .hasIoBoardHl2       = true,
    .hasSidetoneGenerator= true,
    .minFirmwareVersion  = 70,
    .knownGoodFirmware   = 72,
    .displayName         = "Hermes Lite 2",
    .sourceCitation      = "mi0bot/Thetis@Hermes-Lite enums.cs:396, specHPSDR.cs:1180+",
};
```

One entry per `HPSDRHW` value in the same shape. Each cited individually.

### 2.4 Unit test invariants

`tst_board_capabilities.cpp` asserts:

- For each `HPSDRHW`, `forBoard(hw).board == hw`.
- For each `HPSDRModel`, `forModel(m).board == boardForModel(m)`.
- `sampleRates` are monotonically increasing, all ≤ `maxSampleRate`,
  `48000` is always present.
- `attenuator.present == false` ⇒ `minDb == maxDb == 0`.
- `hasDiversityReceiver == true` ⇒ `adcCount == 2`.
- `hasAlexTxRouting == true` ⇒ `hasAlexFilters == true`.
- Every `displayName` and `sourceCitation` is non-null.
- `minFirmwareVersion ≤ knownGoodFirmware`.

`static_assert` anywhere `constexpr` allows; runtime `QVERIFY` otherwise.

---

## 3. P1RadioConnection

### 3.1 Source basis

Every method body cites Thetis `file:line`. Primary sources:

- `Project Files/Source/ChannelMaster/networkproto1.c` — P1 wire format
  (metis discovery / start / stop, ep2 command frame, ep6 I/Q frame parse).
- `Project Files/Source/Console/HPSDR/NetworkIO.cs` — C# command composition
  + C↔managed marshalling.
- `Project Files/Source/Console/HPSDR/IoBoardHl2.cs` — HL2 TLV command
  stream (mi0bot Hermes-Lite branch).
- `Project Files/Source/ChannelMaster/bandwidth_monitor.{c,h}` — HL2 LAN
  throttle detector (mi0bot Hermes-Lite branch).

### 3.2 Class shape

```cpp
class P1RadioConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit P1RadioConnection(QObject* parent = nullptr);
    ~P1RadioConnection() override;

public slots:
    void init() override;
    void connectToRadio(const RadioInfo& info) override;
    void disconnect() override;
    void setReceiverFrequency(int rxIndex, quint64 hz) override;
    void setTxFrequency(quint64 hz) override;
    void setActiveReceiverCount(int count) override;
    void setSampleRate(int sampleRate) override;
    void setAttenuator(int dB) override;
    void setPreamp(bool enabled) override;
    void setTxDrive(int level) override;
    void setMox(bool enabled) override;
    void setAntenna(int antennaIndex) override;

private slots:
    void onReadyRead();
    void onWatchdogTick();
    void onReconnectTimeout();

private:
    // Wire format (networkproto1.c)
    void sendMetisStart(bool iqAndMic);
    void sendMetisStop();
    void sendCommandFrame();
    void parseEp6Frame(const QByteArray& pkt);
    void parseDiscoveryResponse(const QByteArray& pkt);

    // Command & Control banks (NetworkIO.cs SetC0..SetC4)
    void composeCcBank0(quint8* out);
    void composeCcBank1(quint8* out);
    void composeCcBank2(quint8* out);
    void composeCcBank3(quint8* out);
    // ... through bank 14
    void composeCcTxFreq(quint8* out);
    void composeCcAlexRx(quint8* out);
    void composeCcAlexTx(quint8* out);
    void composeCcOcOutputs(quint8* out);

    // Per-board quirks
    void applyBoardQuirks(HPSDRHW board);
    void hl2SendIoBoardTlv(const QByteArray& tlv);
    void hl2CheckBandwidthMonitor();
    void checkFirmwareMinimum(int fw);

    static float scaleSample24(const quint8* be24);

    QUdpSocket* m_socket{nullptr};
    QTimer* m_watchdogTimer{nullptr};
    QTimer* m_reconnectTimer{nullptr};

    quint32 m_epSendSeq{0};
    quint32 m_epRecvSeqExpected{0};
    int     m_ccRoundRobinIdx{0};

    int     m_sampleRate{48000};
    int     m_activeRxCount{1};
    quint64 m_rxFreqHz[7]{};
    quint64 m_txFreqHz{0};
    int     m_atten{0};
    bool    m_preamp{false};
    bool    m_mox{false};
    int     m_antennaIdx{0};

    const BoardCapabilities* m_caps{nullptr};
};
```

### 3.3 RX data path

```
metis ep6 UDP datagram (1032 bytes)
    │
    ▼
parseEp6Frame()
    │   strips 8-byte header, walks 2 × 512-byte USB frames
    │   per frame: sync+C&C bank (8 bytes) + N×8 I/Q/mic samples
    ▼
scaleSample24() per sample   // 24-bit BE signed → float / 8388608.0
    │
    ▼
QVector<float> (interleaved I,Q pairs)
    │
    ▼
emit iqDataReceived(rxIndex, samples)   // RadioConnection.h:82 contract
    │
    ▼
ReceiverManager → WdspEngine → AudioEngine
```

Convergence point: `emit iqDataReceived`. Everything downstream is already
working for Saturn and is untouched by this phase.

### 3.4 TX command path (plumbed cold)

Control bytes for MOX, TX freq, TX drive, ALEX TX, and OC outputs get
composed and sent in the next ep2 frame exactly as Thetis does.
**No TX IQ producer is wired** — ep2 frames carry zeros in the TX sample
slots. The TX phase later drops in a real producer that fills those slots;
nothing in `P1RadioConnection` changes then. This mirrors the current P2
state: `P2RadioConnection::m_txIqTimer` already feeds silence until a real
source is wired.

### 3.5 Per-board quirks table

| Quirk | Boards | Handling |
|---|---|---|
| HL2 IoBoardHl2 TLV init | HermesLite | On connect, send TLV stream from `IoBoardHl2.cs` before first metis-start |
| HL2 bandwidth monitor | HermesLite | Subscribe to LAN throttle events; pause/resume ep2 sends when triggered |
| HL2 step-atten range 0..60 dB | HermesLite | `setAttenuator` clamps to 0..60 |
| No OC outputs | HermesLite, Atlas | `composeCcOcOutputs` writes zeros |
| No ALEX filters | HermesLite, Atlas | `composeCcAlexRx/Tx` writes bypass bits |
| Firmware refuse | all | On discovery, reject connect if `fw < caps.minFirmwareVersion` |
| Firmware stale warn | all | Non-fatal `errorOccurred` warn if `fw < caps.knownGoodFirmware` |
| 2-ADC random/dither per-ADC | Angelia, Orion | Bank 0 writes per-ADC bits |

All branches are `if (m_caps->hasXxx)` or `switch(m_caps->board)`. No
polymorphism.

### 3.6 Reconnection state machine

```
Disconnected ──connect()──▶ Connecting
    ▲                          │ success
    │                          ▼
    │                     Connected ──watchdog-ok──▶ Connected
    │                          │
    │                     no-data 2s
    │                          ▼
    └──disconnect()──────  Error ──reconnect-timer-5s──▶ rediscovery
                              │
                         unrecoverable (3 fails)
                              │
                              ▼
                         Disconnected
```

Bounded at 3 attempts, 5 seconds apart. After the 3rd failure, stay in
`Error` until the user explicitly reconnects.

---

## 4. HardwarePage + Nested Tabs

### 4.1 Container structure

```
SetupDialog (existing)
└── Hardware (new top-level entry)
    └── QTabWidget (nested, mirrors Thetis setup.cs Hardware Config)
        ├── Radio Info           always shown
        ├── Antenna / ALEX       if caps.hasAlexFilters
        ├── OC Outputs           if caps.ocOutputCount > 0
        ├── XVTR                 if caps.xvtrJackCount > 0
        ├── PureSignal           if caps.hasPureSignal
        ├── Diversity            if caps.hasDiversityReceiver
        ├── PA Calibration       if caps.hasPaProfile
        ├── HL2 I/O Board        if caps.hasIoBoardHl2
        └── Bandwidth Monitor    if caps.hasBandwidthMonitor
```

Tabs use `setTabVisible(idx, flag)` rather than add/remove, so per-tab state
survives radio-swap without a rebuild.

### 4.2 File layout

```
src/gui/setup/
├── HardwarePage.{h,cpp}          container + capability reconciliation
└── hardware/
    ├── RadioInfoTab.{h,cpp}
    ├── AntennaAlexTab.{h,cpp}
    ├── OcOutputsTab.{h,cpp}
    ├── XvtrTab.{h,cpp}
    ├── PureSignalTab.{h,cpp}
    ├── DiversityTab.{h,cpp}
    ├── PaCalibrationTab.{h,cpp}
    ├── Hl2IoBoardTab.{h,cpp}
    └── BandwidthMonitorTab.{h,cpp}
```

Absorbs the current 195-LOC `HardwareSetupPages.{h,cpp}` shell; that file is
renamed to `HardwarePage.{h,cpp}` and expanded.

### 4.3 Per-tab control lists

Control citations against Thetis `setup.cs` go in the `.cpp` files
inline — this section lists what each tab exposes at a summary level.

**Radio Info (read-mostly):** board type · protocol · ADC count · max RX ·
firmware version · MAC address · IP · sample-rate combo (filtered by
`caps.sampleRates`) · active receiver count · "Copy support info" button.

**Antenna / ALEX:** RX antenna select per receiver (1..`caps.antennaInputCount`
enabled) · TX antenna select · ALEX RX filter manual override per band
(14-band grid) · ALEX TX filter manual override per band · Alex Bypass on
RX/TX · disable ALEX TX relays during RX.

**OC Outputs:** 7 × per-band output mask (14 bands × 7 outputs grid of
checkboxes) · separate RX and TX masks · relay settle delay slider (ms).

**XVTR:** up to 5 transverter entries — editable table with band name, RF
start, RF end, LO frequency, offset sign, RX-only flag, power override dB,
active-band auto-select toggle.

**PureSignal:** PS enable checkbox (cold this phase) · feedback source select
(Internal / External loopback) · auto-calibrate on band change · preserve
calibration across restarts · RX feedback atten slider.

**Diversity:** enable toggle · phase (°) slider · gain (dB) slider · reference
ADC select · "Null signal" preset button.

**PA Calibration:** per-band target power table (14 rows) · per-band gain
correction table · step-attenuator calibration (if present) · PA profile
dropdown · "Auto-calibrate" button (cold this phase).

**HL2 I/O Board:** board present toggle (auto-detected, override available) ·
GPIO bank enable · external PTT / CW key input mapping · aux output
assignments.

**Bandwidth Monitor:** current LAN PHY rate (read-only live) · throttle-
triggered pause count (read-only) · throttle threshold (Mbps) · auto-pause
ep2 on throttle toggle.

### 4.4 Capability reconciliation

```cpp
void HardwarePage::onCurrentRadioChanged(const RadioInfo& info) {
    const auto& caps = BoardCapsTable::forBoard(info.boardType);
    m_tabs->setTabVisible(m_antennaAlexIdx,     caps.hasAlexFilters);
    m_tabs->setTabVisible(m_ocOutputsIdx,       caps.ocOutputCount > 0);
    m_tabs->setTabVisible(m_xvtrIdx,            caps.xvtrJackCount > 0);
    m_tabs->setTabVisible(m_pureSignalIdx,      caps.hasPureSignal);
    m_tabs->setTabVisible(m_diversityIdx,       caps.hasDiversityReceiver);
    m_tabs->setTabVisible(m_paCalIdx,           caps.hasPaProfile);
    m_tabs->setTabVisible(m_hl2IoIdx,           caps.hasIoBoardHl2);
    m_tabs->setTabVisible(m_bwMonitorIdx,       caps.hasBandwidthMonitor);
    m_radioInfoTab->populate(info, caps);
    m_settings->loadHardwareSettings(info.macAddress);
    for (auto* tab : m_allTabs) tab->onSettingsLoaded();
}
```

### 4.5 Persistence schema

One namespace per MAC under `AppSettings`:

```
hardware/<MAC>/
    radioInfo/sampleRate = 192000
    radioInfo/activeRxCount = 2
    antennaAlex/rxAnt[0] = "ANT1"
    antennaAlex/bypassRx = false
    ocOutputs/txMask[band20m] = 0b0010000
    xvtr/enabled[0] = true
    xvtr/rfStart[0] = 144000000
    pureSignal/enabled = false
    pureSignal/autoCalOnBandChange = true
    diversity/phase = 127
    paCalibration/profile = "ANAN-100D-default"
    paCalibration/targetPower[band40m] = 100
    hl2IoBoard/pttInputPin = 14
    bandwidthMonitor/autoPauseEnabled = true
    bandwidthMonitor/thresholdMbps = 80
```

Tabs emit `valueChanged` on edit; `HardwarePage` writes through immediately.
No explicit Save button — matches NereusSDR's existing SetupDialog behavior
from Phase 3G-8. Board-default values live next to each `BoardCapabilities`
entry so a brand-new MAC gets sensible defaults without copying Thetis.ini.

---

## 5. Discovery + ConnectionPanel

### 5.1 RadioDiscovery — port mi0bot `clsRadioDiscovery.cs`

Replaces the current 306-LOC implementation with a tunable, NIC-aware
scanner. Timing profiles ported exactly from the table at
`clsRadioDiscovery.cs:56-74`:

```cpp
enum class DiscoveryProfile {
    UltraFast,    // 1 × 2 × 40ms   ≈  80ms/NIC
    VeryFast,     // 1 × 3 × 60ms   ≈ 180ms/NIC
    Fast,         // 2 × 3 × 80ms   ≈ 480ms/NIC
    Balanced,     // 2 × 4 × 100ms  ≈ 800ms/NIC
    SafeDefault,  // 3 × 5 × 150ms  ≈ 1800ms/NIC   (default)
    VeryTolerant  // 3 × 6 × 300ms  ≈ 5400ms/NIC
};
```

**NIC walk:**
1. `QNetworkInterface::allInterfaces()` → filter up + IPv4 + non-loopback.
2. Bind a temporary UDP socket per NIC.
3. Send 63-byte P1 discovery frame (`0xEFFE 0x02`) + 60-byte P2 discovery
   frame (per `network.c:listen_*`).
4. Poll per profile; collect replies from both protocols.
5. De-duplicate by MAC.

**Discovery reply parsing:**
- `HPSDRHW` at byte 10 of the P1 reply.
- Firmware version at byte 9.
- `inUse` flag at byte 2 (`0x02` free, `0x03` in use).
- MAC at bytes 3..8.
- Display name from `BoardCapsTable::forBoard(hw).displayName`.

P2 discovery parsing stays where it is in `P2RadioConnection::parseDiscoveryReply`.

### 5.2 ConnectionPanel expansion

Current 315-LOC shell grows to full Thetis-equivalent UI. Stays modal.

**Radio list widget** (ported from `ucRadioList.cs`):
- Columns: `●` status dot · Name · Board · Protocol · IP · MAC · Firmware · In-Use
- Sortable by any column.
- Double-click a row → connect.
- Right-click menu: Connect / Disconnect / Forget / Edit IP / Copy MAC.
- Colour-coding: green = online + free, amber = online + in-use, grey =
  offline, red = error.

**"Pick a radio" flow** (ported from `clsDiscoveredRadioPicker.cs`):
- List merges saved + newly-discovered rows.
- Newly-discovered rows show a `[+ save]` inline action.
- Discovery runs automatically on dialog open.
- "Rescan" button re-runs it.
- Scan spinner + count: "Scanning 3 NICs… found 2 radios".

**Manual-add dialog** (ported from `frmAddCustomRadio.cs`):
- Fields: Name · IP address · Port (default 1024) · MAC (optional) · Board
  type (combo from `HPSDRHW`) · Protocol (P1 / P2).
- "Test" button sends unicast discovery to the entered IP and echoes board
  + firmware if it replies.
- Save button writes to AppSettings.

**Bottom-strip buttons** (matches Thetis):
- Start Discovery / Stop Discovery (toggle)
- Connect / Disconnect
- Add Manually…
- Forget
- Close

### 5.3 Saved-radio persistence schema

```
radios/
  <MAC>/                            # or "manual-192.168.1.42" for MAC-less manual adds
    name            = "Bench HL2"
    ipAddress       = "192.168.1.42"
    port            = 1024
    macAddress      = "aa:bb:cc:11:22:33"
    boardType       = HermesLite
    protocol        = Protocol1
    firmwareVersion = 72            # updated on every successful connect
    pinToMac        = true
    autoConnect     = false
    lastSeen        = 2026-04-12T15:23:00Z

radios/lastConnected   = "aa:bb:cc:11:22:33"
radios/discoveryProfile = SafeDefault
```

`pinToMac=true` means "trust the MAC, ignore IP drift." `pinToMac=false`
means "connect to the saved IP directly without discovery" — needed for
remote-over-VPN setups where broadcast discovery doesn't work.

### 5.4 Auto-reconnect on launch

On app start, if `radios/lastConnected` is set and `autoConnect=true` for
that entry:

1. Run `DiscoveryProfile::Fast` (shorter than UI default) against saved IP/MAC.
2. If the radio replies within the profile timeout, connect silently.
3. If not, do nothing — no error popup, ConnectionPanel works normally.

No auto-reconnect if the user closed the app with the radio disconnected.

---

## 6. Error Handling + Reconnection

### 6.1 Error taxonomy

| Class | Trigger | User-visible | Action |
|---|---|---|---|
| `DiscoveryNicFailure` | Socket bind fail on one NIC | "Cannot scan interface <name>" (warn) | Continue on other NICs |
| `DiscoveryAllFailed` | No NIC scannable | "No usable network interfaces found" | Offer manual-add |
| `RadioInUse` | Discovery reply `inUse=0x03` | "Radio is in use by another client at <ip>" | Row amber; Connect disabled |
| `FirmwareTooOld` | fw < `minFirmwareVersion` | "Firmware v<N> is too old. Minimum supported is v<M>." | Refuse connect |
| `FirmwareStale` | fw < `knownGoodFirmware` | "Firmware v<N> is older than recommended v<M>." | Warn, proceed |
| `SocketBindFailure` | Cannot bind ep6 listener | "Port <P> in use. Another SDR client may be running." | Fail, no retry |
| `NoDataTimeout` | ep6 silent > 2s while running | "Radio stopped responding" | Enter Error, start 5s reconnect timer |
| `UnknownBoardType` | Discovery reports unknown `HPSDRHW` | "Unrecognized board type <N>" | Row shows "Unknown"; Connect disabled |
| `ProtocolMismatch` | User forces P2 on P1-only board | "This radio is P1; cannot connect with P2" | Reject in factory |

All errors flow through `RadioConnection::errorOccurred(QString)`. A new
`errorCode` enum is added to the signal for programmatic handling.

### 6.2 Reconnect state machine

See §3.6. Bounded: 3 attempts, 5s apart, then stay in `Error` until the
user explicitly reconnects.

---

## 7. Testing

### 7.1 Unit tests (no hardware, no network)

- `tst_hpsdr_enums` — model→board mapping complete; integer values preserved
  from Thetis; display names non-null.
- `tst_board_capabilities` — §2.4 invariants, ~120 assertions.
- `tst_radio_discovery_parse` — feed hex-encoded known-good P1 + P2 discovery
  replies, assert correct `RadioInfo`. Fixtures under
  `tests/fixtures/discovery/*.hex`, extracted from the P1 pcap reference doc
  and from live Saturn captures.
- `tst_p1_wire_format` — `composeCcBank0..14`, `scaleSample24`, `parseEp6Frame`
  against captured hex fixtures. No socket, no thread.
- `tst_p1_board_quirks` — per quirky board, mock `BoardCapabilities`,
  `applyBoardQuirks`, assert state bits.
- `tst_hardware_page_capability_gating` — §4.4 for every `HPSDRHW`.
- `tst_hardware_page_persistence_roundtrip` — write/read every tab's state.
- `tst_hardware_page_radio_swap` — connect A, set values, swap to B, swap
  back, assert A's values survived.
- `tst_connection_panel_saved_radios` — add/remove/edit through public API,
  assert AppSettings round-trip.

### 7.2 Integration tests (loopback UDP)

- `tst_p1_loopback_connection` — `P1FakeRadio` on a loopback socket emits
  canned discovery replies + ep6 frames. End-to-end: discovery → connect →
  N ep6 frames → samples land in `iqDataReceived` with correct scale.
- `tst_p2_loopback_regression` — same for P2; guards against P1 work
  regressing Saturn. Reuses fake-radio fixtures.
- `tst_reconnect_on_silence` — fake radio stops ep6; assert
  `Connected → Error → Connecting → Connected` within the reconnect window.

### 7.3 Hardware smoke checklist (manual)

Run against each physical radio available before declaring phase done.
Items for unavailable hardware logged as "unverified in 3I" in the handoff
doc rather than blocking merge.

| Board | Check | Expected |
|---|---|---|
| HL2 | Discover on Wi-Fi NIC | Appears with `fw ≥ 70` |
| HL2 | Connect + tune 14.200 LSB | Hear audio, spectrum live |
| HL2 | Set atten 30 dB | Noise drops ~30 dB |
| HL2 | Bandwidth monitor throttle | Pause triggers, resumes when clear |
| ANAN-100D | Connect + dual RX | Both waterfalls live |
| ANAN-100D | Diversity tab visible | Phase/gain sliders update |
| ANAN-100D | ALEX RX filter override | Band filter switches audibly |
| ANAN-200D | PureSignal tab visible | PS enable toggle persists |
| Saturn | Regression vs main branch | Everything identical |

---

## 8. Migration + Backwards-Incompat

**Break 1: `BoardType` → `HPSDRHW`.**
- `src/core/RadioDiscovery.h:16` `BoardType` deleted.
- Callers migrated: `RadioDiscovery.cpp`, `ConnectionPanel.cpp`,
  `RadioModel.cpp`, `P2RadioConnection.cpp`, `applets/DiversityApplet.cpp`.
- The `Griffin=2 → HermesII=2` slot is corrected; `Griffin` was never
  actually used in practice, so no AppSettings migration is required.

**Break 2: `radio-abstraction.md` corrections.** Docs-only. Delete the
`Griffin` row, add `HermesII`, `SaturnMKII`, `ANAN_G2_1K`, `AnvelinaPro3`.
Update ADC counts + max RX per `BoardCapabilities`.

**Rename: `HardwareSetupPages` → `HardwarePage`.** Old name was
plural-but-single-class; new container uses a `QTabWidget`. File rename +
include fixes only.

No `AppSettings` migration code needed. New keys under `radios/*` and
`hardware/<MAC>/*` are purely additive. Existing Saturn users' settings
unchanged.

---

## 9. Out of Scope (Explicit)

- **TCI protocol** (mi0bot `tci.{c,h}`) — separate phase.
- **RedPitaya board** (`REDPITAYA` in mi0bot enum) — revisit after HL2
  family lands.
- **TX IQ producer** — plumbed cold here; wired in dedicated TX phase.
- **PureSignal feedback DSP** — toggle persists, tab visible, feedback loop
  deferred to TX phase.
- **WDSP retuning** — this phase calls existing `WdspEngine` APIs only.
- **Multi-radio simultaneous connection** — one at a time.
- **CAT server** — separate phase.
- **Sidetone generator** (mi0bot `sidetone.{c,h}`) — defer until CW TX lands.
- **Firmware updater** — Thetis has in-app HL2 flashing; out of scope.
- **Connection chip / ⌘K switcher / workspace tabs** — explored during
  brainstorming, dropped in favour of keeping ConnectionPanel modal.

---

## 10. Risks + Mitigations

| Risk | L | I | Mitigation |
|---|---|---|---|
| P1 ep6 parse bugs (scaling, byte order) | M | H | Hex fixtures from P1 pcap doc; unit-test every scale boundary; line-by-line Thetis diff in PR review |
| Board quirks we haven't found yet | M | M | Manual smoke checklist (§7.3); deferred quirks logged as follow-ups |
| `BoardCapabilities` misses a Thetis field | L-M | M | Field-by-field walk of `specHPSDR.cs` + `setup.cs`; registry is data-only so appends are cheap |
| Radio enum integer drift breaks persistence | L | H | §8 preserves integer values; `tst_hpsdr_enums` asserts at compile time |
| Thetis source ambiguity (build-flag conditionals) | M | L | Resolve in PR review; prefer HL2-branch interpretation when genuinely ambiguous |
| 2.3 MB `console.cs` hides board logic | M | M | Grep `console.cs` for every `HPSDRModel.` / `HPSDRHW.` reference before finalising the registry; log "fields touched per board" in design appendix |
| macOS multi-NIC discovery flaky | L | M | Reuse the P2 NIC walker that already works |
| Phase exceeds one-shot | H | M | **Acknowledged.** Strict §9 exclusion; no scope creep; defer anything that doesn't fit |

---

## 11. Open Questions

None at design time. All decisions reached during brainstorming are recorded
in §§1–10. If a question surfaces during implementation, log it here and
decide by source-first default (what does Thetis do?).

---

*Co-Authored-By: Claude Opus 4.6 (1M context) <noreply@anthropic.com>*
