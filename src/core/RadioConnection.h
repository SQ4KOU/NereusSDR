#pragma once

// no-port-check: NereusSDR-original abstract base class. Inline doc comments
// reference Thetis source filenames (console.cs / networkproto1.c / network.c)
// only as pointers to where ported logic lives in the concrete subclasses
// (P1RadioConnection.cpp, P2RadioConnection.cpp); no upstream code is
// reproduced in this header.

#include "ConnectionState.h"
#include "RadioDiscovery.h"
#include "HardwareProfile.h"
#include "RadioLinkStats.h"
#include "codec/AlexFilterMap.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QVector>

#include <atomic>
#include <array>
#include <memory>
#include <optional>

namespace NereusSDR {

// Structured failure taxonomy for the initial connect attempt — design §4.1.
// Emitted by connectFailed() when connectToRadio() cannot reach Connected state.
// The UI (TitleBar, ConnectionPanel) maps these to human-readable messages.
enum class ConnectFailure : int {
    Unreachable     = 0,  // OS reports "destination unreachable" (ICMP unreach)
    Timeout         = 1,  // No reply / no first frame within the connect budget
    MalformedReply  = 2,  // Response received but the frame parser rejected it
    ProtocolMismatch = 3, // Reply valid but P1/P2 version mismatch
    IncompatibleBoard = 4 // Reply valid but boardType is not supported
};

// Structured error taxonomy — design doc §6.1.
enum class RadioConnectionError {
    None,
    DiscoveryNicFailure,
    DiscoveryAllFailed,
    RadioInUse,
    FirmwareTooOld,
    FirmwareStale,           // non-fatal warning
    SocketBindFailure,
    NoDataTimeout,
    UnknownBoardType,
    ProtocolMismatch
};

// Antenna routing parameters — Phase 3P-I-a.
// Ports Thetis Alex.cs:310-413 UpdateAlexAntSelection output
// (HPSDR/Alex.cs [v2.10.3.13 @501e3f5]). Composed by
// RadioModel::applyAlexAntennaForBand and pushed to RadioConnection.
//
// 3P-I-a scope: trxAnt + txAnt are independent ANT1..ANT3 ports.
//     rxOnlyAnt, rxOut, tx remain zero until 3P-I-b/3M-1.
struct AntennaRouting {
    int  rxOnlyAnt {0};    // 0=none, 1=RX1, 2=RX2, 3=XVTR  (3P-I-b)
    int  trxAnt    {1};    // 1..3 — shared RX/TX port on Alex
    int  txAnt     {1};    // 1..3 — independent TX port (P2 Alex1)
    bool rxOut     {false}; // RX bypass relay active        (3P-I-b)
    bool tx        {false}; // current MOX state             (3M-1)
};

// Per-ADC RX band-pass decision — Phase 3F.
//
// Composed by AlexController::recomputeBpf over the set of slice bands on
// each ADC, published by RadioModel::republishAlexAdcSlices, and consumed by
// the connection when it composes the Alex HPF bits.
//
// A 2-ADC radio has two independent filter chains and Thetis writes them as
// two independent wire words: `prbpfilter` (Alex0, ADC0) fed from
// setAlex1HPF(_rx1_dds_freq), and `prbpfilter2` (Alex1, ADC1) fed from
// setAlex2HPF(rx2_dds_freq_mhz).
//   From Thetis console.cs:15401 + 15435-15443 [v2.10.3.15]
//[2.10.3.13]MW0LGE
//   From Thetis ChannelMaster/network.c:1040-1050 [v2.10.3.15]
//   Upstream inline attribution preserved verbatim (console.cs:15441):
//     HardwareSpecific.Model == HPSDRModel.REDPITAYA) //DH1KLM
// Before this struct existed NereusSDR derived one HPF value from whichever
// receiver was retuned last and put it in both words, so a second slice on a
// different band made the first one deaf (reported by CT1IQI on PR #293).
//
// -1 means "no decision for this ADC": no slice sits on it, so the connection
// keeps whatever HPF bits it already had. That mirrors Thetis, which only
// calls setAlex2HPF when RX2 actually exists (console.cs:15435-15442) and
// otherwise leaves prbpfilter2's HPF nibble untouched.
//
// Values are the Thetis HPF bit encoding (AlexFilterMap::computeHpf):
// 0x10/0x08/0x04/0x01/0x02 band filters, 0x40 6 m preamp, 0x20 bypass.
struct AlexRxBpf {
    int hpfBitsAdc0 {-1};
    int hpfBitsAdc1 {-1};
};

// A bounded copy made on the radio connection's parser thread for the
// existing queued telemetry reply. Ages refer to that connection's current
// Connected epoch. `known=false` means no valid status for this ADC.
struct RadioAdcOverloadObservation {
    bool known{false};
    bool active{false};
    qint64 eventsSinceConnection{0};
    qint64 statusAgeMs{0};
    std::optional<qint64> lastOverloadAgeMs;
};

struct RadioDiagnosticsObservation {
    std::optional<quint16> radioUdpBasePort;
    std::array<RadioAdcOverloadObservation, 3> adcOverloads{};
};

// Abstract base class for radio connections.
// Subclasses implement protocol-specific behavior (P1 or P2).
// Instances live on the Connection worker thread.
// Call init() after moveToThread() to create sockets/timers on the worker thread.
class RadioConnection : public QObject {
    Q_OBJECT

public:
    explicit RadioConnection(QObject* parent = nullptr);
    ~RadioConnection() override;

    // Factory — creates the appropriate subclass based on RadioInfo::protocol.
    static std::unique_ptr<RadioConnection> create(const RadioInfo& info);

    // State (atomic for cross-thread reads from main thread)
    ConnectionState state() const { return m_state.load(); }
    bool isConnected() const { return m_state.load() == ConnectionState::Connected; }
    const RadioInfo& radioInfo() const { return m_radioInfo; }

    void setHardwareProfile(const HardwareProfile& profile) { m_hardwareProfile = profile; }
    const HardwareProfile& hardwareProfile() const { return m_hardwareProfile; }

    // Wire-protocol identifier — 1 = OpenHPSDR Protocol 1 (Metis-framed UDP
    // on port 1024), 2 = OpenHPSDR Protocol 2 (multi-port UDP, higher rates).
    //
    // Mirrors Thetis NetworkIO.CurrentRadioProtocol and exists primarily so
    // protocol-conditional WDSP TXA stages (e.g. CFIR per
    // ChannelMaster/cmaster.cs:525-533 [v2.10.3.14]) can be gated without
    // dragging the concrete subclass header into the call site.
    //
    // Non-pure with a P1 default so test mocks compile unchanged; P2 must
    // override.  Override in P1RadioConnection (returns 1) is provided for
    // explicit symmetry with P2RadioConnection (returns 2).
    virtual int protocolVersion() const { return 1; }

    // Rolling-window throughput accessors. **Returns Mbps** (not bytes/sec
    // — the "ByteRate" name predates the implementation, kept for ABI
    // stability). Used by ConnectionSegment ▲▼ readouts and the network
    // diagnostics dialog. Bugs land easy here: 2026-04-30 the dialog
    // applied a second `* 8 / 1e6` conversion and TX/RX always read 0.
    // If you're computing throughput, use these values directly as Mbps.
    double txByteRate(int windowMs) const;
    double rxByteRate(int windowMs) const;

    // Hooks the protocol implementations call on each successful packet.
    // Public so subclasses (P1/P2) and tests can drive the counter.
    void recordBytesSent(qint64 n);
    void recordBytesReceived(qint64 n);

    // Ping RTT measurement via existing C&C round-trip.
    // Call notePingSent() just before emitting an outbound command and
    // notePingReceived() when the corresponding inbound status arrives.
    // A single outstanding exchange is tracked; the first valid pair
    // within 5 s emits pingRttMeasured(int rttMs). Duplicate receives
    // and receives-without-sends are silently ignored.
    // Drives the ConnectionSegment "X ms" latency readout (sub-PR-2).
    void notePingSent();
    void notePingReceived();

    // R-R3-32 / R-R3-49 (parity Task 6): the link's datagram counters (UDP
    // packets seen, packet loss, jitter, packet gap), one source for Network
    // Diagnostics in a local window and the Core's station telemetry. Safe
    // from any thread (RadioLinkStats: atomics written only by this
    // connection's receive path).
    RadioLinkStats::Snapshot linkStats() const
    { return m_linkStats.snapshot(RadioLinkStats::nowUs()); }
    const RadioLinkStats& linkStatsCounters() const { return m_linkStats; }

public slots:
    // Owner-thread observation used by the daemon telemetry collector. The
    // rolling-rate lists above are connection-thread state and must never be
    // traversed directly from the daemon control thread. This request adds no
    // radio packet and does not alter ping matching or connection liveness.
    void collectTelemetryObservation(quint64 requestId);

public:
    // Voltage signal handlers — called by P1/P2 after extracting raw ADC counts.
    // Apply per-board scaling and emit supplyVoltsChanged / userAdc0Changed
    // when the value changes by more than 50 mV (identical-raw suppression).
    //
    // handleSupplyRaw: Hermes DC-volts formula from Thetis
    //   console.cs computeHermesDCVoltage() [v2.10.3.13].
    //   Applies to all radios (supply_volts / AIN6 / P1 case 0x18 / P2 bytes 45-46).
    //
    // handleUserAdc0Raw: MKII PA-volts formula from Thetis
    //   console.cs computeMKIIPaVoltage() [v2.10.3.13].
    //   Applies to ORIONMKII / ANAN8000D / ANAN7000D / ANAN-G2 family only
    //   (user_adc0 / AIN3 / P1 case 0x10 / P2 bytes 53-54). Callers
    //   are responsible for gating by board type before calling.
    void handleSupplyRaw(quint16 raw);
    void handleUserAdc0Raw(quint16 raw);

    // Last value handleSupplyRaw / handleUserAdc0Raw computed, or the
    // -1.0f sentinel if no High-Priority status frame has been parsed yet.
    //
    // Exists to close a startup race: status-frame parsing begins as soon
    // as the socket is live, which can run before RadioModel reaches
    // Connected and before a UI listener binds supplyVoltsChanged /
    // userAdc0Changed. handleSupplyRaw/handleUserAdc0Raw suppress
    // re-emitting an unchanged value (identical-raw suppression above), so
    // a listener that missed the first sample would otherwise wait forever
    // for a steady reading that never changes again. A fresh listener
    // should read these once right after connecting, then rely on the
    // signal for subsequent changes.
    /// Last cached supply / PA-drain reading, or -1 if none has arrived.
    ///
    /// Read from the GUI thread while the connection worker thread writes
    /// them as status frames arrive, so both are atomic. Plain floats here
    /// were a data race, and therefore undefined behaviour, not merely a
    /// torn read: the Connected-state priming read can land mid-update.
    /// Relaxed ordering is sufficient; these are independent scalars that
    /// guard no other state. Found by Codex on PR #316. CLAUDE.md's
    /// cross-thread rule ("main thread writes via std::atomic") applies
    /// here in the reading direction.
    float lastSupplyVolts() const
    { return m_lastSupplyVolts.load(std::memory_order_relaxed); }
    float lastUserAdc0Volts() const
    { return m_lastUserAdc0Volts.load(std::memory_order_relaxed); }

public slots:
    // Must be called on the worker thread after moveToThread().
    // Creates sockets, timers, and other thread-local resources.
    virtual void init() = 0;

    // Connect to the specified radio. Auto-queued from main thread.
    virtual void connectToRadio(const NereusSDR::RadioInfo& info) = 0;

    // Graceful disconnect.
    virtual void disconnect() = 0;

    // --- Frequency Control ---
    virtual void setReceiverFrequency(int receiverIndex, quint64 frequencyHz) = 0;
    virtual void setTxFrequency(quint64 frequencyHz) = 0;

    // --- Receiver Configuration ---
    virtual void setActiveReceiverCount(int count) = 0;
    virtual void setSampleRate(int sampleRate) = 0;

    // Which hardware receivers are live: bit n set means a receiver is
    // routed to hardware index n (ReceiverManager::hardwareSlotsChanged).
    // Protocol 1 uses it to pick the receiver that stands in for Thetis's
    // RX1 when slice A is closed, and to announce every slot in use.
    // Non-pure so existing test mocks compile unchanged; P1 overrides.
    virtual void setLiveReceiverSlots(quint32 /*slotMask*/) {}

    // The VFO frequency of the slice each hardware receiver slot serves,
    // indexed by slot (0 = no slice, or not told). The OC outputs take their
    // band from a VFO, not from a DDC centre, which differs under CTUN
    // (Thetis: BandByFreq(VFOAFreq), plan Task 14). Non-pure so existing
    // test mocks compile unchanged; P1 and P2 override.
    virtual void setReceiverVfoFrequencies(const QVector<quint64>& /*vfoHzBySlot*/) {}

    // --- Hardware Control ---
    virtual void setAttenuator(int dB) = 0;
    virtual void setPreamp(bool enabled) = 0;
    virtual void setTxDrive(int level) = 0;
    virtual void setMox(bool enabled) = 0;
    virtual void setAntennaRouting(AntennaRouting routing) = 0;

    // Apply the per-ADC RX band-pass decision to the Alex HPF wire bits.
    //
    // Deliberately a separate pump from setAntennaRouting rather than an
    // extra field on AntennaRouting: that struct is composed by
    // RadioModel::applyAlexAntennaForBand from a single (band, isTx) pair,
    // and with N slices spread over two chains there is no single band to
    // compose it from. The filter decision also fires on a different trigger
    // set (slice bind / retune / removal) than antenna routing does
    // (antennaChanged / bandChanged / Connected).
    //
    // Non-pure so existing test mocks compile unchanged; P1 and P2 override.
    virtual void setAlexRxBpf(AlexRxBpf /*bpf*/) {}

    // Push TX-side step attenuator value to hardware.
    //
    // From Thetis ChannelMaster/netInterface.c:1006 [v2.10.3.13]
    // SetTxAttenData(int bits): broadcasts bits to all ADC tx_step_attn
    // fields and triggers CmdTx() to emit the updated frame.
    // Called by StepAttenuatorController::onMoxHardwareFlipped (F.2).
    //
    // Default no-op: subclasses that don't yet implement TX ATT (e.g. a
    // stub test connection) skip silently. P1/P2 override this method.
    virtual void setTxStepAttenuation(int /*dB*/) {}

    // ── TX path (3M-1a) ────────────────────────────────────────────────

    /// Push TX I/Q samples to the radio.
    ///
    /// `iq` points to interleaved I/Q float32 samples; `n` is the
    /// number of complex samples (so the buffer has 2*n floats).
    /// Implementations:
    ///   - P1: write to EP2 zones in the 1032-byte Metis frame
    ///     (interleaved with status bytes per the Metis spec).
    ///   - P2: write to UDP port 1029 with the per-frame layout
    ///     specified by OpenHPSDR Protocol 2.
    ///
    /// Audio-thread context: callers run this in the WDSP audio thread.
    /// The implementation must not block; it queues to the connection
    /// thread for actual UDP send.
    ///
    /// Cite: pre-code review §7.1 (P1 EP2 TX I/Q layout),
    ///        §7.6 (P2 port 1029 TX I/Q layout).
    virtual void sendTxIq(const float* iq, int n) = 0;

    /// Set the Alex T/R relay wire bit.
    ///
    /// Distinct from `setMox(bool)`:
    ///   - `setMox(true)` asserts the MOX bit on the next outbound
    ///     frame (P1 byte 3 bit 0 / P2 high-pri byte 4 bit 1).
    ///   - `setTrxRelay(true)` engages the Alex T/R relay path so
    ///     the PA can drive the antenna. P1 wire bit is C3 byte 6
    ///     bit 7 with INVERTED semantic — `1` = disabled (bypass),
    ///     `0` = enabled (normal TX). When `enabled` is true the
    ///     implementation writes `0` to bit 7 (engaged); when false
    ///     (PA disabled), writes `1` (bypass).
    ///   - P2: routed via Saturn register C0=0x24 indirect writes;
    ///     stub for 3M-1a (deferred to 3M-3).
    ///
    /// `enabled` follows caller-friendly convention (true = TX path
    /// engaged); the implementation handles the bit inversion on the
    /// wire. State is stored in base-class `m_trxRelay`.
    ///
    /// 3M-1a wires this from `MoxController::hardwareFlipped` via
    /// `RadioModel::onMoxHardwareFlipped` (Task F.1).
    ///
    /// Cite: pre-code review §7.2 + deskhpsdr/src/old_protocol.c:2909-2910
    ///       (T/R relay bit C3[6] bit 7, inverted sense).
    virtual void setTrxRelay(bool enabled) = 0;

    bool isTrxRelayEngaged() const noexcept { return m_trxRelay; }

    // ── Mic-jack hardware bits (3M-1b Phase G) ────────────────────────────

    /// Hardware mic-jack 20 dB boost preamp.
    ///
    /// P1 source: Thetis ChannelMaster/networkproto1.c:581 [v2.10.3.13]
    ///   case 10 (C0=0x12) C2 byte: (prn->mic.mic_boost & 1) → bit 0 (0x01)
    ///
    /// P2 source: deskhpsdr src/new_protocol.c:1484-1486 [@120188f]
    ///   if (mic_boost) { transmit_specific_buffer[50] |= 0x02; }
    ///   (bit 1, mask 0x02 — different bit from P1)
    ///
    /// Polarity: 1 = boost on (no inversion).
    ///
    /// HL2 has no mic jack; the P1 implementation still writes the bit
    /// (firmware ignores it).
    virtual void setMicBoost(bool on) = 0;

    /// Hardware mic-jack line-in path (replaces front-panel mic with line input).
    ///
    /// P1 source: Thetis ChannelMaster/networkproto1.c:581 [v2.10.3.13]
    ///   case 10 (C0=0x12) C2 byte: ((prn->mic.line_in & 1) << 1) → bit 1 (0x02)
    ///
    /// P2 source: deskhpsdr src/new_protocol.c:1480-1482 [@120188f]
    ///   if (mic_linein) { transmit_specific_buffer[50] |= 0x01; }
    ///   (bit 0, mask 0x01 — different bit position from P1)
    ///
    /// Polarity: 1 = line in active (no inversion).
    ///
    /// HL2 has no mic jack; the P1 implementation still writes the bit
    /// (firmware ignores it).
    virtual void setLineIn(bool on) = 0;

    /// Hardware mic-jack Tip/Ring polarity selection.
    ///
    /// NereusSDR parameter convention: `tipHot = true` means Tip carries the
    /// mic signal (the intuitive "tip is mic" meaning).
    ///
    /// POLARITY INVERSION AT THE WIRE LAYER — both upstream sources define
    /// the field as "1 = Tip is BIAS/PTT" (i.e. Tip is NOT the mic):
    ///   Thetis field name: mic_trs  ("TRS" = Tip-Ring-Sleeve; 1 = tip is ring)
    ///   deskhpsdr field: mic_ptt_tip_bias_ring (1 = tip carries BIAS/PTT, not mic)
    /// Therefore both P1 and P2 write `!tipHot` to the wire bit.
    ///
    /// P1 source: Thetis ChannelMaster/networkproto1.c:597 [v2.10.3.13]
    ///   case 11 (C0=0x14) C1 byte: ((prn->mic.mic_trs & 1) << 4) → bit 4 (0x10)
    ///   (first touch of case 11 / bank 11)
    ///
    /// P2 source: deskhpsdr src/new_protocol.c:1492-1494 [@120188f]
    ///   if (mic_ptt_tip_bias_ring) { transmit_specific_buffer[50] |= 0x08; }
    ///   (bit 3, mask 0x08 — different bit position from P1)
    ///
    /// HL2 has no mic jack; the P1 implementation still writes the bit
    /// (firmware ignores it).
    virtual void setMicTipRing(bool tipHot) = 0;

    /// Hardware mic-jack phantom power (bias) enable.
    ///
    /// Polarity: 1 = bias on (no inversion — parameter maps directly to wire bit).
    ///
    /// P1 source: Thetis ChannelMaster/networkproto1.c:597 [v2.10.3.13]
    ///   case 11 (C0=0x14) C1 byte: ((prn->mic.mic_bias & 1) << 5) → bit 5 (0x20)
    ///   (same bank 11 / case 11 as G.3 mic_trs — both OR into C1)
    ///
    /// P2 source: deskhpsdr src/new_protocol.c:1496-1498 [@120188f]
    ///   if (mic_bias_enabled) { transmit_specific_buffer[50] |= 0x10; }
    ///   (bit 4, mask 0x10 in byte 50 — different bit position from P1)
    ///
    /// HL2 has no mic jack; the P1 implementation still writes the bit
    /// (firmware ignores it).
    virtual void setMicBias(bool on) = 0;

    /// Set mic line-in gain (0-31, 5 bits).  Maps to Thetis prn->mic.line_in_gain.
    /// Source: Thetis ChannelMaster/networkproto1.c:600 [v2.10.3.13]
    ///   C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
    /// P1: bank 11 C2 low 5 bits.
    /// P2: byte 51 of CmdHighPriority (already wired pre-this-virtual via the
    ///     P2 mic struct; the P2 override bridges this virtual to that path).
    /// Default 0 = no line-in attenuation.
    virtual void setLineInGain(int gain) = 0;

    /// Set user digital outputs (0-15, low 4 bits).  Maps to Thetis prn->user_dig_out.
    /// Source: Thetis ChannelMaster/networkproto1.c:601 [v2.10.3.13]
    ///   C3 = prn->user_dig_out & 0b00001111;
    /// P1: bank 11 C3 low 4 bits.  Drives the 4 user-controllable digital pins
    ///     on the Penny/Hermes Ctrl accessory header.
    /// P2: NO EQUIVALENT — user_dig_out is a P1/Penny-only feature with no
    ///     corresponding byte in CmdHighPriority.  The P2 override stores into
    ///     base m_userDigOut for symmetric API only; it does NOT emit anything
    ///     on the wire.
    /// Default 0 = all 4 user-dig-out pins low.
    virtual void setUserDigOut(quint8 dig) = 0;

    /// Set PureSignal feedback DDC routing-active flag.  Maps to Thetis prn->puresignal_run.
    /// Source: Thetis ChannelMaster/networkproto1.c:600 [v2.10.3.13]
    ///   C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
    /// P1: bank 11 C2 bit 6 (mask 0x40).
    /// P2: NO DIRECT WIRE-BIT — PureSignal feedback DDC routing on P2 is
    ///     handled by the feedback DDC plumbing planned for 3M-4.  The P2
    ///     override stores into base m_puresignalRun for symmetric API only;
    ///     it does NOT emit anything on the wire until 3M-4.
    ///
    /// Semantic: tracks whether PureSignal feedback DDC routing is currently
    /// *active*.  Distinct from BoardCapabilities.hasPureSignal (capability)
    /// and from TransmitModel::puresignalEnabled (user toggle).  Until 3M-4
    /// lights up the actual feedback DDC routing, the PureSignalApplet
    /// "Enable" toggle is the proxy that drives this — wiring done in
    /// Task 2.5 of the P1 full-parity epic, not here.
    /// Default false = PureSignal feedback DDC NOT routing.
    virtual void setPuresignalRun(bool run) = 0;

    /// HPF Bypass on PureSignal feedback flag (G2E / OrionMKII / Saturn).
    /// When set + MOX active + PureSignal active, the host sends the Alex0
    /// high-pass word as 0x20 (bit 12, _Bypass) so the radio bypasses the
    /// HPF chain and feeds the post-PA coupler tap directly to the ADC.
    /// Default true, as Thetis chkDisableHPFonPSb.Checked=true
    /// [v2.10.3.13]. Applied on the band-pass boards only, on either
    /// protocol (codec::alex::applyAlex1HpfSwitches); RadioModel hands it
    /// the Alex tab's saved value (plan Task 14 fix wave).
    /// ANAN-G2E bench-fix 2026-05-23 (JJ Boyd).
    virtual void setHpfBypassOnPs(bool on) {
        m_hpfBypassOnPs = on;
    }
    bool hpfBypassOnPs() const noexcept { return m_hpfBypassOnPs; }

    /// "HPF Bypass on TX" (Setup > Hardware > Alex, plan Task 14). While
    /// keyed, an Alex board's high-pass word is 0x20, the bypass:
    ///   From Thetis console.cs:6843-6848 [v2.10.3.15] (setAlexHPF)
    ///     if (_mox && disable_hpf_on_tx)
    ///     { NetworkIO.SetAlexHPFBits(0x20); ... return; }
    /// Default false, as Thetis (console.cs:18753 disable_hpf_on_tx = false).
    /// P1 and P2 read it when they compose the high-pass word.
    virtual void setHpfBypassOnTx(bool on) { m_hpfBypassOnTx = on; }
    bool hpfBypassOnTx() const noexcept { return m_hpfBypassOnTx; }

    /// The Alex tab's high-pass switches as the high-pass word applies them
    /// (codec::alex::applyAlex1HpfSwitches). Plan Task 14 fix wave.
    codec::alex::Alex1HpfSwitches alexHpfSwitches() const noexcept {
        codec::alex::Alex1HpfSwitches sw;
        sw.hpfBypassOnTx = m_hpfBypassOnTx;
        sw.hpfBypassOnPs = m_hpfBypassOnPs;
        sw.hpfBypass     = m_alexHpfBypass;
        sw.disable6mLnaOnRx = m_disable6mLnaOnRx;
        sw.disable6mLnaOnTx = m_disable6mLnaOnTx;
        return sw;
    }

    /// "Disable 6m LNA on RX" / "on TX" (the Alex tab). On 6 m the high-pass
    /// word's 6 m BPF/LNA selection (0x40) becomes the bypass (0x20) while
    /// receiving (RX switch) or keyed (TX switch):
    ///   From Thetis console.cs:6931-6936 [v2.10.3.15] (setAlexHPF)
    ///     if (alex6bphpf_bypass || disable_6m_lna_on_rx || (_mox && disable_6m_lna_on_tx))
    ///     { NetworkIO.SetAlexHPFBits(0x20); // Bypass HPF
    /// Defaults as Thetis: RX false (console.cs:18719), TX true (18741).
    virtual void setDisable6mLna(bool onRx, bool onTx) {
        m_disable6mLnaOnRx = onRx;
        m_disable6mLnaOnTx = onTx;
    }

    /// A band-output (OC) pin was edited in the matrix this connection
    /// composes from (plan Task 14 fix wave, M2). Protocol 2 sends a
    /// high-priority packet when the byte changes, as Thetis pushes a pin
    /// edit at once; Protocol 1 carries bank 0 in its frame rotation, so the
    /// base does nothing.
    virtual void onBandOutputPinsChanged() {}

    /// "HPF Bypass" (the Alex tab's master switch, Thetis chkAlexHPFBypass
    /// "ByPass/55 MHz HPF"). An Alex board's high-pass word is 0x20, keyed
    /// or not:
    ///   From Thetis console.cs:6850-6855 [v2.10.3.15] (setAlexHPF)
    ///     if (alex_hpf_bypass)
    ///     { NetworkIO.SetAlexHPFBits(0x20); // Bypass HPF ... return; }
    /// Default false, as Thetis (console.cs:18793 alex_hpf_bypass = false).
    virtual void setAlexHpfBypass(bool on) { m_alexHpfBypass = on; }
    bool alexHpfBypass() const noexcept { return m_alexHpfBypass; }

    /// "Disable HF PA" (Setup > Transmit > Power, Thetis chkHFTRRelay):
    /// Thetis's NetworkIO.DisablePA, the radio's PA switched off.
    ///   From Thetis ChannelMaster/netInterface.c:623-631 [v2.10.3.15]
    ///     void DisablePA(int bit)
    ///     { if (prn->tx[0].pa != bit) { prn->tx[0].pa = bit; ... CmdGeneral(); } }
    /// P1: bank 10 C3 bit 7 (networkproto1.c:586), and on the HL2 bank 10
    /// C2 bit 3 cleared (mi0bot netInterface.c:628-629). P2: CmdGeneral byte
    /// 58 clear (network.c:904), and the Alex T/R relay left open while
    /// keyed (netInterface.c:378 SetTRXrelay). RadioModel hands it the
    /// saved setting (applyDisableHfPaSetting). Default false, as Thetis
    /// (netInterface.c:1522 prn->tx[i].pa = 0).
    virtual void setPaDisabled(bool disabled) { m_paDisabled = disabled; }
    bool paDisabled() const noexcept { return m_paDisabled; }

    /// The Alex tab's receive filter rows (Setup > Hardware > Alex-1 and
    /// Alex-2 Filters): each row's edges and per-row bypass for the
    /// high-pass ladder, the band-pass bank and the Alex-2 bank, and the
    /// Alex-2 master bypass (codec::alex::AlexHpfEdges). Thetis selects the
    /// receive high-pass from these (console.cs:6839-7175 [v2.10.3.15]
    /// setAlexHPF / setBPF1ForOrionIISaturn / setAlex2HPF). RadioModel
    /// hands it the saved rows; each protocol re-selects at once, as
    /// Thetis's per-row bypass setters do (console.cs:18823-18833
    /// Alex1_5BPHPFBypass { ... setAlex1HPF(freq); }).
    virtual void setAlexHpfEdges(const codec::alex::AlexHpfEdges& edges) { m_alexHpfEdges = edges; }
    const codec::alex::AlexHpfEdges& alexHpfEdges() const noexcept { return m_alexHpfEdges; }

    /// Hardware mic-jack PTT disable flag (Orion/ANAN front-panel PTT).
    ///
    /// Parameter and wire convention match Thetis NetworkIO.SetMicPTT exactly:
    ///   disabled = true  → PTT disabled at firmware → wire bit = 1
    ///   disabled = false → PTT enabled  at firmware → wire bit = 0  (default)
    ///
    /// Source: Thetis console.cs:19757-19766 [v2.10.3.13+501e3f51]
    ///   private bool mic_ptt_disabled = false;        // default PTT enabled
    ///   public bool MicPTTDisabled {
    ///       set {
    ///           mic_ptt_disabled = value;
    ///           NetworkIO.SetMicPTT(Convert.ToInt32(value));
    ///       }
    ///   }
    ///
    /// P1 wire field: Thetis ChannelMaster/networkproto1.c:597-598 [v2.10.3.13+501e3f51]
    ///   case 11 (C0=0x14) C1 byte: ((prn->mic.mic_ptt & 1) << 6) → bit 6 (0x40), DIRECT
    /// P2 wire field: deskhpsdr src/new_protocol.c:1488-1490 [@120188f]
    ///   if (mic_ptt_enabled == 0) { transmit_specific_buffer[50] |= 0x04; }  // byte 50 bit 2
    /// Both upstreams use the same direct-to-disabled wire convention.
    ///
    /// HL2 has no front-panel PTT jack; the P1 implementation still writes the
    /// bit (firmware ignores it).
    virtual void setMicPTTDisabled(bool disabled) = 0;

    /// Hardware mic-jack XLR input select (Saturn G2 / ANAN-G2 only).
    ///
    /// P2-ONLY FEATURE. Saturn G2 hardware ships with an XLR balanced mic
    /// input; this bit selects between XLR (balanced) and TRS (unbalanced).
    ///
    /// P2 source: deskhpsdr src/new_protocol.c:1500-1502 [@120188f]
    ///   if (mic_input_xlr) { transmit_specific_buffer[50] |= 0x20; }
    ///   Bit 5 (mask 0x20) of byte 50. Polarity: 1 = XLR jack selected.
    ///   No inversion — parameter maps directly to wire bit.
    ///
    /// P1 implementation is STORAGE-ONLY.
    ///   P1 hardware has no XLR jack. The setter stores m_micXlr for
    ///   cross-board API consistency but does NOT emit any wire bytes.
    ///   P1 case-10 and case-11 C&C bytes are unchanged regardless of value.
    ///   Comment: "Saturn G2 P2-only feature; P1 hardware has no XLR jack."
    ///
    /// Default true — Saturn G2 ships with XLR-enabled configuration.
    virtual void setMicXlr(bool xlrJack) = 0;

    // DEPRECATED — call setAntennaRouting directly. Kept for one release
    // cycle as a rollback hatch per docs/architecture/antenna-routing-design.md §7.7.
    // Removed in the release following 3P-I-b.
    Q_DECL_DEPRECATED_X("Use setAntennaRouting")
    void setAntenna(int antennaIndex) {
        const int ant = (antennaIndex >= 0 && antennaIndex <= 2) ? antennaIndex + 1 : 1;
        setAntennaRouting({0, ant, ant, false, false});
    }

    // --- ADC Mapping ---
    virtual int getAdcForDdc(int /*ddc*/) const { return 0; }

    // --- TX output sample rate (bench fix round 3 — Issue B) ---
    //
    // Returns the radio's negotiated TX I/Q output sample rate in Hz.
    // This is the rate passed to OpenChannel() as outputSampleRate, and
    // therefore the rate at which WDSP's TXA rsmpout resampler delivers
    // samples to fexchange2's Iout/Qout buffers.
    //
    // P1 (HL2/Atlas/Hermes/Angelia/Orion): TX I/Q flows into EP2 zones at
    //   the radio's sample rate.  For single-RX operation this is always
    //   48000 Hz (single-rate mode).  P1RadioConnection returns 48000.
    //
    // P2 (Saturn/ANAN-G2): TX I/Q flows to UDP port 1029 at 192000 Hz.
    //   P2RadioConnection returns 192000 — derived from m_tx[0].samplingRate
    //   (always 192 kHz per Thetis netInterface.c:1513 [v2.10.3.13]).
    //
    // Default implementation returns 48000 (correct for P1 and stubs).
    // P2RadioConnection overrides to return 192000.
    //
    // From Thetis wdsp/cmaster.c:183 [v2.10.3.13] — ch_outrate parameter.
    // From Thetis netInterface.c:1513 [v2.10.3.13] — P2 tx always 192 kHz.
    virtual int txSampleRate() const { return 48000; }

public:
    // R-IOS-13, R-R3-42: the transmit I/Q send path's counters since the
    // last key. Any thread may read them (each is one atomic load); a
    // protocol that does not keep them reports valid=false.
    struct TxSendStats {
        bool valid{false};
        quint64 framesSent{0};          ///< TX I/Q frames on the wire
        quint64 zeroPaddedSamples{0};   ///< silence sent while keyed, ring empty
        quint64 lateWakes{0};           ///< the sender woke later than 5 ms
        quint64 catchUpBursts{0};       ///< refills of more than 5 ms at once
        quint64 radioRanDry{0};         ///< times the radio's buffer ran out (estimated)
        quint64 overflowSamples{0};     ///< samples the full ring refused (lost)
        quint64 sendErrors{0};          ///< sends the socket refused (retried)
        int maxRingMs{0};               ///< deepest the ring got, in ms
        /// G-07: only overflowSamples is kept (Protocol 1); the other
        /// counters are not measured and read zero.
        bool overflowOnly{false};
    };
    virtual TxSendStats txSendStats() const { return {}; }
    /// R-IOS-13 (2026-09-27): what the transmit I/Q send ring holds now,
    /// in ms of the radio's time; negative when this connection does not
    /// know. Any thread; lock-free.
    virtual double txIqQueuedMs() const { return -1.0; }
    /// G-05 (2026-09-29): true when the transmit I/Q send ring holds nothing
    /// more the sender will put on the wire, so an unkey can release the
    /// hardware without cutting off queued audio. A connection without a
    /// send ring has nothing queued. Any thread; lock-free.
    virtual bool txIqRingDrained() const { return true; }
    /// G-05: the send ring's own length, in ms of the radio's time: the most
    /// audio it can hold, and so the longest an unkey waits for it to
    /// drain. Zero or negative when this connection has no send ring.
    virtual double txIqRingLengthMs() const { return -1.0; }

public slots:

    // --- Watchdog ---
    // R-R3-49: the Network Watchdog setting (Setup > General > Options),
    // applied where the radio is. On both protocols it sets only how long
    // an established link waits for data before the radio is declared lost:
    // three seconds on, no limit off. Nothing on the wire follows it: the
    // Protocol 2 radio's own safety timer (general packet byte 38) stays on
    // and its 500 ms keepalive always runs (operator decision 2026-09-24; a
    // deliberate divergence from Thetis, which lets both follow it). See
    // P2RadioConnection::setWatchdogEnabled and
    // P1RadioConnection::setWatchdogEnabled for the Thetis lines.
    virtual void setWatchdogEnabled(bool enabled) = 0;

    bool isWatchdogEnabled() const noexcept { return m_watchdogEnabled; }

signals:
    // --- Telemetry ---
    // Emitted when a C&C round-trip completes. rttMs is the elapsed time
    // in milliseconds between notePingSent() and notePingReceived().
    // Drives the ConnectionSegment "X ms" latency readout (sub-PR-2).
    void pingRttMeasured(int rttMs);

    // Reply to collectTelemetryObservation(), emitted on this object's owning
    // connection thread. hasRtt=false means no valid C&C RTT has completed on
    // this connection. rttAgeMs is the age of the actual measurement and is
    // never renewed merely because another observation was requested.
    void telemetryObservationReady(quint64 requestId, double rxMbps,
                                   double txMbps, bool hasRtt,
                                   qint64 rttMs, qint64 rttAgeMs,
                                   NereusSDR::RadioDiagnosticsObservation diagnostics);

    // PSU supply voltage (V) from supply_volts (P1 AIN6 / P2 bytes 45-46).
    // Converted via Hermes DC-volts formula (console.cs computeHermesDCVoltage()
    // [v2.10.3.13]). Emitted at most once per 50 mV change.
    // Drives the redesigned right-side strip "PSU X.XV" metric (sub-PR-2).
    void supplyVoltsChanged(float volts);

    // PA drain voltage (V) from user_adc0 (P1 AIN3 / P2 bytes 53-54).
    // ORIONMKII / ANAN-8000D / ANAN-7000DLE / ANAN-G2 family only.
    // Converted via MKII PA-volts formula (console.cs computeMKIIPaVoltage()
    // [v2.10.3.13]). Emitted at most once per 50 mV change.
    void userAdc0Changed(float volts);

    // --- State ---
    void connectionStateChanged(NereusSDR::ConnectionState state);
    void errorOccurred(NereusSDR::RadioConnectionError code, const QString& message);

    // Emitted once per valid ep6 (P1) or DDC I/Q (P2) frame arrival.
    // The TitleBar activity LED throttles this to ≤10 Hz visible refresh —
    // the emitter fires at the full frame rate; throttling is the receiver's job.
    // Design doc §4.1.
    void frameReceived();

    // Emitted when connectToRadio() fails to reach the Connected state.
    // reason carries a typed failure code; detail is a plain-English
    // explanation suitable for display in the ConnectionPanel / TitleBar.
    // Fully-qualified type names required for Qt MOC queued-connection support.
    // Design doc §4.1.
    void connectFailed(NereusSDR::ConnectFailure reason, QString detail);

    // --- Data ---
    // Emitted for each receiver's I/Q block.
    // hwReceiverIndex: 0-based hardware receiver number.
    // samples: interleaved float I/Q pairs, normalized to [-1.0, 1.0].
    void iqDataReceived(int hwReceiverIndex, const QVector<float>& samples);

    // Phase 3M-4 bench-fix 2026-05-23 (J.J. Boyd KG4VCF): per-packet paired
    // PureSignal I/Q streams.
    //
    // Emitted ONCE per inbound packet (P2 multi-stream UDP packet or P1
    // EP6 frame) that carries BOTH the PS-feedback and TX-monitor DDCs.
    // Both vectors are deinterleaved from the SAME packet, so cross-stream
    // sample alignment is preserved by construction — no host-side ring
    // buffering required.
    //
    // Mirrors Thetis ChannelMaster sync.c:53-58 [v2.10.3.15] InboundBlock
    // (id=1), which calls pscc() with two pointers (`data[ps_tx_idx]` and
    // `data[ps_rx_idx]`) that both reference per-stream buffers populated
    // by the same xrouter() call (router.c:91-102 case 2 [v2.10.3.15]).
    //
    // PsccPump connects to this signal in place of iqDataReceived() so
    // the calcc engine sees the streams already paired, eliminating the
    // 189-sample / 985 us cross-stream drift that the prior
    // independent-rings architecture allowed under Qt queued-connection
    // scheduling (bench-measured on ANAN-G2E + HermesC10 effective
    // board, 2026-05-23).
    //
    // psFbDdc / txMonDdc identify which DDC indices the buffers came
    // from (per cmaster.cs:533-534 [v2.10.3.13] convention: ps_rx_idx=0,
    // ps_tx_idx=1 on all current models — though per-board codecs can
    // override via PsDdcConfig::psFbDdc / .txMonDdc).
    void psPairedIqDataReceived(int psFbDdc, const QVector<float>& psFbSamples,
                                int txMonDdc, const QVector<float>& txMonSamples);

    // Emitted when mic samples are available.
    void micDataReceived(const QVector<float>& samples);

    /// Decoded mic-frame audio from the radio's mic-jack input.
    ///
    /// Emitted by P1RadioConnection on EP2 mic-byte zone arrival (Phase G);
    /// emitted by P2RadioConnection on port-1026 packet arrival (Phase G).
    /// Carries float-converted mic samples + frame count.
    ///
    /// **DirectConnection ONLY.** The pointer is only valid during the
    /// synchronous slot dispatch — Qt signals cannot safely queue raw
    /// pointers across threads. Subscribers (RadioMicSource in F.2) MUST
    /// connect with Qt::DirectConnection and immediately copy the samples
    /// into their own lock-free SPSC ring before returning.
    ///
    /// This matches the D.5 sip1OutputReady contract in TxChannel: the same
    /// raw-pointer + DirectConnection pattern is used wherever the audio
    /// thread passes data across a signal boundary.
    ///
    /// Sample format: float32 mono at the radio's mic sample rate
    /// (typically 48 kHz on HPSDR family).
    ///
    /// Plan: 3M-1b F.4. Pre-code review §6.4.
    void micFrameDecoded(const float* samples, int frames);

    // --- Meters ---
    void meterDataReceived(float forwardPower, float reversePower,
                           float supplyVoltage, float paCurrent);

    // --- PA telemetry (Phase 3P-H Task 4) ---
    // Raw 16-bit ADC counts read from C&C status bytes (P1) or the
    // High-Priority status packet (P2). Per-board scaling to watts /
    // volts / amps lives in RadioModel (console.cs computeAlexFwdPower /
    // computeRefPower / convertToVolts / convertToAmps), because the
    // bridge constants vary per HPSDRModel.
    //
    // Sources:
    //   P1: networkproto1.c:332-356 [@501e3f5] — C0 cases 0x08/0x10/0x18
    //   P2: network.c:711-748        [@501e3f5] — High-Priority byte offsets 2-3, 10-11, 18-19, 45-46, 51-52, 53-54
    //
    // Fields (all uint16, raw ADC counts):
    //   fwdRaw      — fwd_power      (P1 AIN1, P2 bytes 10-11)
    //   revRaw      — rev_power      (P1 AIN2, P2 bytes 18-19)
    //   exciterRaw  — exciter_power  (P1 AIN5, P2 bytes 2-3)
    //   userAdc0Raw — user_adc0      (P1 AIN3 MKII PA volts, P2 bytes 53-54)
    //   userAdc1Raw — user_adc1      (P1 AIN4 MKII PA amps,  P2 bytes 51-52)
    //   supplyRaw   — supply_volts   (P1 AIN6 Hermes volts,  P2 bytes 45-46)
    void paTelemetryUpdated(quint16 fwdRaw, quint16 revRaw, quint16 exciterRaw,
                            quint16 userAdc0Raw, quint16 userAdc1Raw,
                            quint16 supplyRaw);

    // ADC overflow detected.
    void adcOverflow(int adc);

    // Mic-jack PTT input from the radio hardware, decoded from the status
    // frame on every frame receipt.  Emitted unconditionally each frame;
    // MoxController::onMicPttFromRadio() is idempotent on repeated same-state
    // calls (setPttMode(Mic) is a no-op when mode unchanged; setMox(x) only
    // advances the state machine when x differs from m_mox).
    //
    // Subscriber: RadioModel wires this to MoxController::onMicPttFromRadio
    // in setupMoxController() (H.5).  The MoxController drives MOX state when
    // mic-jack PTT changes state.
    //
    // P1 source: C0 byte (ControlBytesIn[0]) bit 0 of each EP6 sub-frame.
    //   Cite: Thetis networkproto1.c:329 [v2.10.3.13]:
    //     prn->ptt_in = ControlBytesIn[0] & 0x1;
    //   + console.cs:25426 [v2.10.3.13]:
    //     bool mic_ptt = (dotdashptt & 0x01) != 0; // PTT from radio
    //
    // P2 source: High-Priority status packet byte 4 (ReadBufp[0]) bit 0.
    //   Cite: Thetis network.c:686-689 [v2.10.3.13]:
    //     //Byte 0 - Bit [0] - PTT  1 = active, 0 = inactive
    //     prn->ptt_in = prn->ReadBufp[0] & 0x1;
    //   (ReadBufp points to raw[4] in NereusSDR — after 4-byte seq prefix.)
    void micPttFromRadio(bool pressed);

    // The radio's user digital inputs (Thetis prn->user_dig_in), emitted
    // when the value changes and on the first status that carries it.
    // Task 13: TxInhibitMonitor reads the TX inhibit input from these bits
    // the way Thetis PollTXInhibit does (console.cs:25849-25887
    // [v2.10.3.15]); the per-model bit choice lives there, not here.
    //
    // P1 source: C1 bits 1..4 of a case-0x00 status subframe.
    //   From Thetis networkproto1.c:332-336 [v2.10.3.15]:
    //     switch (ControlBytesIn[0] & 0xf8)
    //     case 0x00: // C0 0000 0000
    //       prn->user_dig_in = ((ControlBytesIn[1] >> 1) & 0xf);
    //   (networkproto1.c:335, the ADC overload line in the same case, carries
    //   //[2.10.3.13]MW0LGE)
    //
    // P2 source: High-Priority status ReadBufp[55], which is datagram byte
    //   59 after the 4-byte sequence number (network.c:531 copies readbuf+4).
    //   From Thetis network.c:750-756 [v2.10.3.15]:
    //     //Byte 55 - Bit [0] - User I/O (IO4) 1 = active, 0 = inactive
    //     //          Bit [1] - User I/O (IO5) 1 = active, 0 = inactive
    //     prn->user_dig_in = prn->ReadBufp[55];
    void userDigitalInputsChanged(quint8 userDigIn);

    // Radio firmware info received during handshake.
    void firmwareInfoReceived(int version, const QString& details);

    // Plan Task 14 fix wave (R-R3-49): the band-output (OC) byte this
    // connection composed into the packet that carries it, with the band it
    // was chosen for and whether the transmitter was keyed. Emitted when any
    // of the three changes, on the connection thread. Thetis shows exactly
    // these bits (UpdateOCLedStrip(_mox, bits), console.cs:29106-29107
    // [v2.10.3.15]); RadioModel publishes them to every window.
    void bandOutputsComposed(quint8 ocByte, int band, bool keyed);

private:
    struct ByteSample { qint64 ms; qint64 bytes; };
    mutable QList<ByteSample> m_txSamples;
    mutable QList<ByteSample> m_rxSamples;

    static double rateFromSamples(const QList<ByteSample>& samples, int windowMs);
    static void   pruneSamples(QList<ByteSample>& samples, qint64 nowMs, int windowMs);

    // publishBandOutputs: what was last reported. -1 = nothing yet.
    mutable int m_publishedOcByte{-1};
    mutable int m_publishedOcBand{-1};
    mutable int m_publishedOcKeyed{-1};

    // Ping RTT state. Zero means no outstanding ping.
    qint64 m_pingSentMs{0};
    int m_lastPingRttMs{-1};
    QElapsedTimer m_lastPingRttAge;

    // Voltage conversion helpers.
    // convertSupplyVolts: 3.3V ADC ref + (4.7+0.82)/0.82 divider — Thetis-faithful
    //                     port of computeHermesDCVoltage (which is itself dead
    //                     code in Thetis — see source-first audit notes near the
    //                     PA volt label declaration in MainWindow.h).
    // convertMkiiPaVolts: 5.0V ADC ref + (22+1)/1.1 divider — Thetis-faithful
    //                     port of convertToVolts (the formula Thetis actually
    //                     uses to display voltage on MkII-class boards).
    static float convertSupplyVolts(quint16 raw);
    static float convertMkiiPaVolts(quint16 raw);

    // Last-emitted voltage values for identical-raw suppression (50 mV epsilon).
    // Initialised to -1 so the first call always emits.
    std::atomic<float> m_lastSupplyVolts{-1.0f};
    std::atomic<float> m_lastUserAdc0Volts{-1.0f};

protected:
    // The band-output byte last reported by publishBandOutputs, or -1.
    int publishedOcByte() const noexcept { return m_publishedOcByte; }

    // Reports the band-output byte composed into the packet that carries it
    // (bandOutputsComposed), once per change. Called from the compose path,
    // which is const; the emit does not change the connection's state.
    void publishBandOutputs(quint8 ocByte, int band, bool keyed) const
    {
        const int keyedInt = keyed ? 1 : 0;
        if (m_publishedOcByte == int(ocByte) && m_publishedOcBand == band
            && m_publishedOcKeyed == keyedInt) {
            return;
        }
        m_publishedOcByte  = int(ocByte);
        m_publishedOcBand  = band;
        m_publishedOcKeyed = keyedInt;
        emit const_cast<RadioConnection*>(this)->bandOutputsComposed(ocByte, band, keyed);
    }

    void setState(ConnectionState newState);

    // Call only on this connection's parser thread after a status frame has
    // validated. mask says which ADC bits this particular status contains;
    // a zero bit in mask is no observation and must never clear old state.
    void observeAdcOverloads(quint8 mask, quint8 bits);

    // The live outbound UDP base/control destination, read only in the
    // owning-thread collectTelemetryObservation slot. P2 overrides it.
    virtual int telemetryUdpBasePort() const { return m_radioInfo.port; }

    // R-R3-32 (parity Task 6): written only from the receive path on this
    // connection's thread; see linkStats().
    RadioLinkStats m_linkStats;

    // Task 13: called by the P1/P2 status parsers with the user digital
    // input bits; emits userDigitalInputsChanged on a change. Connection
    // thread only.
    void reportUserDigitalInputs(quint8 userDigIn)
    {
        if (m_lastUserDigIn == static_cast<int>(userDigIn)) {
            return;
        }
        m_lastUserDigIn = static_cast<int>(userDigIn);
        emit userDigitalInputsChanged(userDigIn);
    }
    // -1 until the first status that carries the inputs.
    int m_lastUserDigIn{-1};

    std::atomic<ConnectionState> m_state{ConnectionState::Disconnected};
    RadioInfo m_radioInfo;
    HardwareProfile m_hardwareProfile;

    struct AccumulatedAdcStatus {
        bool known{false};
        bool active{false};
        qint64 eventsSinceConnection{0};
        qint64 lastStatusAtMs{0};
        qint64 lastPositiveAtMs{-1};
    };
    QElapsedTimer m_diagnosticsEpoch;
    std::array<AccumulatedAdcStatus, 3> m_adcOverloads{};

    // Shared boolean state for setWatchdogEnabled / isWatchdogEnabled.
    // Both P1 and P2 overrides read/write this field. Default true, as
    // Thetis's checkbox is checked by default and applied at startup.
    bool m_watchdogEnabled{true};

    // Shared state for setTrxRelay / isTrxRelayEngaged (3M-1a Task E.1).
    // true = TX path engaged (bit 7 of P1 C3 bank 6 written as 0 — inverted
    // sense). P2 stub; wire emit deferred to 3M-3.
    bool m_trxRelay{false};

    // Shared state for setMicBoost (3M-1b G.1).
    // P1: emitted to case 10 (C0=0x12) C2 bit 0 (0x01).
    // P2: emitted to transmit_specific_buffer[50] bit 1 (0x02).
    // From Thetis networkproto1.c:581 [v2.10.3.13]; deskhpsdr new_protocol.c:1484-1486 [@120188f].
    bool m_micBoost{false};

    // Shared state for setLineIn (3M-1b G.2).
    // P1: emitted to case 10 (C0=0x12) C2 bit 1 (0x02).
    // P2: emitted to transmit_specific_buffer[50] bit 0 (0x01).
    // From Thetis networkproto1.c:581 [v2.10.3.13]; deskhpsdr new_protocol.c:1480-1482 [@120188f].
    bool m_lineIn{false};

    // Shared state for setMicTipRing (3M-1b G.3).
    // Parameter convention: true = Tip is mic (intuitive).
    // POLARITY INVERSION: both P1 and P2 write !m_micTipRing to the wire bit.
    // P1: emitted to case 11 (C0=0x14) C1 bit 4 (0x10) inverted.
    // P2: emitted to transmit_specific_buffer[50] bit 3 (0x08) inverted.
    // From Thetis networkproto1.c:597 [v2.10.3.13]; deskhpsdr new_protocol.c:1492-1494 [@120188f].
    bool m_micTipRing{true};

    // Shared state for setMicBias (3M-1b G.4).
    // Polarity: 1 = bias on (no inversion — parameter maps directly to wire bit).
    // Default false — bias off (per pre-code review §2.3 / §2.7 and
    //   TransmitModel::micBias default in C.2).
    // P1: emitted to case 11 (C0=0x14) C1 bit 5 (0x20).
    // P2: emitted to transmit_specific_buffer[50] bit 4 (0x10).
    // From Thetis networkproto1.c:597 [v2.10.3.13]; deskhpsdr new_protocol.c:1496-1498 [@120188f].
    bool m_micBias{false};

    // Shared state for setLineInGain (Task 2.1 of P1 full-parity epic).
    // 5-bit field (0-31).  Default 0 = no line-in attenuation.
    // P1: emitted to case 11 (C0=0x14) C2 low 5 bits via ctx.p1LineInGain.
    // P2: bridged into m_mic.lineInGain → byte 51 of CmdHighPriority.
    // From Thetis ChannelMaster/networkproto1.c:600 [v2.10.3.13]:
    //   C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
    int m_lineInGain{0};

    // Shared state for setUserDigOut (Task 2.2 of P1 full-parity epic).
    // 4-bit field (0-15).  Default 0 = all 4 user-dig-out pins low.
    // P1: emitted to case 11 (C0=0x14) C3 low 4 bits via ctx.p1UserDigOut.
    // P2: STORAGE-ONLY (no wire emission — user_dig_out is P1/Penny-only).
    // From Thetis ChannelMaster/networkproto1.c:601 [v2.10.3.13]:
    //   C3 = prn->user_dig_out & 0b00001111;
    quint8 m_userDigOut{0};

    // Shared state for setPuresignalRun (Task 2.3 of P1 full-parity epic).
    // Bool flag tracking whether PureSignal feedback DDC routing is active.
    // Default false = NOT routing.
    // P1: emitted to case 11 (C0=0x14) C2 bit 6 (mask 0x40) via ctx.p1PuresignalRun.
    // P2: STORAGE-ONLY (no wire emission — PS feedback DDC routing is 3M-4 deferred).
    // From Thetis ChannelMaster/networkproto1.c:600 [v2.10.3.13]:
    //   C2 = (prn->mic.line_in_gain & 0b00011111) | ((prn->puresignal_run & 1) << 6);
    bool m_puresignalRun{false};

    // ANAN-G2E bench-fix 2026-05-23 (JJ Boyd): HPF Bypass during MOX+PS.
    // From Thetis console.cs:6957 setBPF1ForOrionIISaturn [v2.10.3.13]:
    //   if (_mox && (disable_hpf_on_tx || (disable_hpf_on_ps && PureSignalEnabled)))
    //       NetworkIO.SetAlexHPFBits(0x20);  // Bypass — bit 12 of Alex0 (_Bypass)
    // The G2E (HermesC10) uses this branch (setAlex1HPF dispatch at
    // console.cs:6830 N1GP G2E added).  When MOX+PS is active and this flag
    // is set, the host OR's 0x20 into alexHpfBits so the codec emits bit 12
    // in the Alex0 word, telling the radio to bypass the HPF chain and feed
    // the post-PA coupler tap straight to the ADC.  Without this on a
    // single-ADC G2E the FB DDC sees HPF-attenuated signal which calcc
    // can't fit, leaving PureSignal oscillating instead of locking.
    // Bench-confirmed against Thetis-locked pcap 2026-05-22 (87 MB on .247):
    // Alex0 0x09441C00 during MOX+PS has bit 12 set; without the flag we
    // emit 0x09240C20 (bits 10+11 yes, bit 12 no).
    // Default true — matches Thetis chkDisableHPFonPSb.Checked=true at
    // setup.designer.cs:23676 [v2.10.3.13].
    bool m_hpfBypassOnPs{true};

    // "HPF Bypass on TX" (setHpfBypassOnTx). Written and read on the
    // connection thread.
    bool m_hpfBypassOnTx{false};

    // "HPF Bypass" (setAlexHpfBypass). Written and read on the connection
    // thread.
    bool m_alexHpfBypass{false};

    // "Disable HF PA" (setPaDisabled), Thetis prn->tx[0].pa. Written and read
    // on the connection thread.
    bool m_paDisabled{false};

    // The Alex tab's receive filter rows (setAlexHpfEdges), Thetis's
    // shipped values until RadioModel hands the saved ones. Written and read
    // on the connection thread.
    codec::alex::AlexHpfEdges m_alexHpfEdges{codec::alex::AlexHpfEdges::thetisDefaults()};

    // "Disable 6m LNA on RX / TX" (setDisable6mLna). Written and read on the
    // connection thread.
    bool m_disable6mLnaOnRx{false};
    bool m_disable6mLnaOnTx{true};

    // Shared state for setMicPTTDisabled (3M-1b G.5; renamed for issue #182
    // to match Thetis MicPTTDisabled / mic_ptt_disabled storage name exactly).
    // Direct polarity: m_micPTTDisabled=true means PTT is disabled at the
    // firmware (wire bit = 1). Default false — PTT enabled by default,
    // matching Thetis console.cs:19757 [v2.10.3.13+501e3f51]:
    //   private bool mic_ptt_disabled = false;
    // P1 wire: case 11 (C0=0x14) C1 bit 6 (0x40), direct.
    // P2 wire: transmit_specific_buffer[50] bit 2 (0x04), direct.
    bool m_micPTTDisabled{false};

    // Shared state for setMicXlr (3M-1b G.6).
    // P2-only wire emission. P1 stores the flag for cross-board API consistency
    //   but does NOT emit any wire bytes. "Saturn G2 P2-only feature; P1 hardware
    //   has no XLR jack." P1 case-10 and case-11 C&C bytes are UNCHANGED
    //   regardless of m_micXlr value.
    // Polarity: 1 = XLR selected (no inversion — parameter maps directly to wire bit).
    // Default true — Saturn G2 ships with XLR-enabled configuration.
    //   This matches pre-code review §2.7 and TransmitModel::micXlr default in C.2.
    // P1: STORAGE-ONLY (no wire emission).
    // P2: emitted to transmit_specific_buffer[50] bit 5 (0x20).
    // From deskhpsdr new_protocol.c:1500-1502 [@120188f]:
    //   if (mic_input_xlr) { transmit_specific_buffer[50] |= 0x20; }
    bool m_micXlr{true};
};

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::RadioConnectionError)
Q_DECLARE_METATYPE(NereusSDR::ConnectFailure)
Q_DECLARE_METATYPE(NereusSDR::RadioDiagnosticsObservation)
