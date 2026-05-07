// no-port-check: NereusSDR-original driver class.  Thetis pumps pscc()
// from inside ChannelMaster.dll's xrouter → InboundBlock(id=1) chain
// (Project Files/Source/ChannelMaster/router.c:71-108 +
// sync.c:44-67 [v2.10.3.13]).  ChannelMaster receives multi-stream
// packets and demultiplexes into per-stream double-pointer arrays
// before calling pscc.  NereusSDR's network layer uses the OpenHPSDR
// P2 protocol-native one-stream-per-UDP-port architecture, so the
// pairing has to happen here in the host: this driver class buffers
// per-DDC float-interleaved I/Q from RadioConnection::iqDataReceived,
// pairs them block-by-block, and calls pscc() with the matched
// double buffers.
//
// =================================================================
// src/core/PsccPump.h  (NereusSDR)
// =================================================================
//
// PsccPump — the missing pscc() driver for PureSignal.
//
// Background
// ----------
// The WDSP calcc engine (third_party/wdsp/src/calcc.c [v2.10.3.13])
// runs its corrections-being-applied / cal-attempts state machine
// inside `pscc(channel, size, tx, rx)` (calcc.c:617).  pscc reads
// paired TX-monitor + PS-feedback I/Q blocks, evaluates the calcc
// state, and populates info[16] (which downstream code consumes
// via GetPSInfo).
//
// In Thetis, ChannelMaster.dll's xrouter dispatches multi-stream
// packets and demultiplexes into per-stream pointers, then sync.c's
// InboundBlock(id=1) calls pscc with `data[ps_tx_idx]` and
// `data[ps_rx_idx]` selected from the array.  cmaster.cs:533-534
// configures `ps_rx_idx=0, ps_tx_idx=1` for "all current models".
//
// In NereusSDR, the OpenHPSDR P2 network layer delivers each DDC's
// I/Q as a separate stream (RadioConnection::iqDataReceived(ddcIndex,
// samples)) — one UDP port per DDC.  PsccPump subscribes to that
// stream, buffers DDC0 (PS-feedback per cmaster.cs convention) and
// DDC1 (TX-monitor) independently, and calls pscc when both rings
// have a paired block ready.
//
// Without this driver, the WDSP calcc engine never receives any
// data and `GetPSInfo` returns all zeros — every PsForm Calibration
// Information field, the bottom-banner FB number, the IMD overlay
// peak detection, and the GetPk readout are blocked on info[]
// becoming non-zero.  See docs/architecture/phase3m-4-handoff-bench-debug.md
// "Round 2 status".
//
// Source-first cite map
// ---------------------
//   /Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/sync.c:44-67
//     [v2.10.3.13] — InboundBlock(id=1) puresignal case
//   /Users/j.j.boyd/Thetis/Project Files/Source/ChannelMaster/router.c:71-108
//     [v2.10.3.13] — xrouter de-interleave + InboundBlock dispatch
//   /Users/j.j.boyd/Thetis/Project Files/Source/Console/cmaster.cs:533-534
//     [v2.10.3.13] — SetPSRxIdx(0,0) + SetPSTxIdx(0,1) for all current models
//   /Users/j.j.boyd/Thetis/Project Files/Source/wdsp/calcc.c:617-837
//     [v2.10.3.13] — pscc() public entry + state machine
//
// Thread placement
// ----------------
// PsccPump runs on the main thread (constructed by RadioModel,
// receives auto-queued iqDataReceived signals from the connection
// thread).  pscc internally takes a critical section
// (txa[channel].calcc.cs_update at calcc.c:621), so thread safety
// is enforced by WDSP.  Per-block work is sample copy + state
// machine — trivially cheap on main thread.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Created by J.J. Boyd (KG4VCF) for Phase 3M-4
//                 Task 17 chunk C — closed the calcc-feed gap that
//                 prevented PureSignal from working end-to-end.  AI-
//                 assisted source-first protocol via Anthropic
//                 Claude Code.
// =================================================================

#pragma once

#include <QObject>
#include <QVector>

#include "codec/CodecContext.h"   // PsDdcConfig

namespace NereusSDR {

class MoxController;

class PsccPump : public QObject {
    Q_OBJECT

public:
    explicit PsccPump(QObject* parent = nullptr);
    ~PsccPump() override;

    // WDSP TX channel id for psccF.  Mirrors Thetis sync.c:54-55
    // [v2.10.3.13]: `chid(inid(1, 0), 0)` — typically 1 for the
    // primary TX channel.  Defaults to 1; setter exists for tests
    // and future multi-TX support.
    void setTxChannelId(int channelId);

    // Bind a MoxController so onIqData can derive the mox + solidmox
    // flags for psccF.  Optional — without it the pump assumes
    // mox=true while active (calcc clamps internally via SetPSMox).
    void setMoxController(MoxController* mox);

    // Block size for psccF calls.  P2 packets carry spp=238 samples;
    // the pump accumulates to kBlockSize and drains in one call.
    // Default 256 — the WDSP calcc temptx/temprx scratch buffers
    // hold 2048 complex samples per calcc.c:185 [v2.10.3.13], so any
    // size <= 2048 is safe.
    void setBlockSize(int n);

    // ── Test seam ────────────────────────────────────────────────────
    int  txChannelId()        const { return m_txChannelId; }
    int  blockSize()          const { return m_blockSize; }
    bool isActive()           const { return m_active; }
    int  txMonDdc()           const { return m_txMonDdc; }
    int  psFbDdc()            const { return m_psFbDdc; }
    qint64 totalBlocksPumped() const { return m_totalBlocksPumped; }

public slots:
    // Connect P2RadioConnection::iqDataReceived(ddcIndex, samples) here.
    // Routes the block into the appropriate ring based on ddcIndex
    // matching m_txMonDdc (TX-monitor) or m_psFbDdc (PS-feedback).
    // Other DDC indices are ignored (RX1's audio path stays untouched).
    void onIqData(int ddcIndex, const QVector<float>& samples);

    // Connect ReceiverManager::ddcConfigChanged here so the pump
    // tracks which DDC is which.  When the codec returns a config
    // with both PS streams enabled (ddcEnable+syncEnable bits both
    // set + ps rate on those DDCs), activate the pump and identify
    // the indices.  When PS gates close, deactivate.
    void onDdcConfigChanged(const NereusSDR::PsDdcConfig& cfg);

    // Manual activation hook for tests + the off-band production
    // case where ReceiverManager isn't available (unit tests).
    // Caller specifies which DDC index carries TX-monitor and
    // which carries PS-feedback (per Thetis cmaster.cs:533-534
    // [v2.10.3.13]: psFbDdc=0, txMonDdc=1 for all current models).
    void setActive(bool active, int txMonDdc, int psFbDdc);

private:
    // Drains kBlockSize samples from each ring (when both have ≥
    // kBlockSize available), de-interleaves into double tx/rx
    // buffers, and calls pscc().  Called after every onIqData ring
    // append.  Mirrors the InboundBlock(id=1) call pattern at
    // sync.c:53-58 [v2.10.3.13].
    void tryPump();

    // From Thetis calcc.c:902-911 [v2.10.3.13] SetPSMox + the solidmox
    // book-keeping: solidmox flips true ~10 packet cycles after MOX
    // becomes true (not used by pscc directly — calcc internally
    // tracks solidmox via the LMOXDELAY → LSETUP transition based on
    // moxsamps count — but the psccF wrapper takes the flag for the
    // disabled-since-2.9 codepath at calcc.c:846-847).
    int m_txChannelId{1};
    bool m_active{false};
    int m_txMonDdc{1};   // Thetis cmaster.cs:534 [v2.10.3.13]: Stream1 = TX
    int m_psFbDdc{0};    // Thetis cmaster.cs:533 [v2.10.3.13]: Stream0 = RX
    int m_blockSize{256};

    QVector<float> m_txMonRing;  // interleaved I/Q (size = 2 * samples)
    QVector<float> m_psFbRing;   // interleaved I/Q

    MoxController* m_mox{nullptr};

    qint64 m_totalBlocksPumped{0};
};

} // namespace NereusSDR
