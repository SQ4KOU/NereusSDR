// no-port-check: NereusSDR-original test helper. Thetis cites in method
// comments below describe which Thetis handler each accessor models (for
// reader orientation), not ported logic — all accessor bodies are trivially
// NereusSDR-native array reads/writes with bounds checks.

// tests/TestMockRadioModel.h  (NereusSDR)
// NereusSDR-original test helper — no Thetis upstream port.
//
// Minimal in-process mock for TciProtocol tests. Inherits QObject so tests
// can pass `&mock` directly to TciProtocol (no reinterpret_cast hazard).
// Accessors marked Q_INVOKABLE so TciProtocol can call them via
// QMetaObject::invokeMethod(Qt::DirectConnection) without a layering
// violation (production code never #includes test headers). Extended per
// phase as new command families need additional accessors.
//
// Phase 1: 7 accessors (VFO hz, mode, MOX) + resetToBaseline().
// Phase 3: now inherits QObject; resolves Task 1.1 reinterpret_cast<QObject*>
//           workaround in tst_tci_matrix_runner.cpp.
// Phase 6: 11 accessors total — added setVfoLock/vfoLock/setLock/lock;
//           Q_INVOKABLE on setVfoHz/vfoHz/setVfoLock/vfoLock/setLock/lock
//           so TciProtocol dispatch uses invokeMethod (no header dependency).
// Phase 7: 14 accessors total — added setFilterBand/filterLow/filterHigh;
//           Q_INVOKABLE on setMode/mode/setFilterBand/filterLow/filterHigh
//           so TciProtocol modulation + rx_filter_band handlers dispatch correctly.
// Phase 8: 19 accessors total — added setSplit/split/setGlobalMute/globalMute/
//           setRxMute/rxMute; Q_INVOKABLE on all six so TciProtocol trx-family
//           handlers dispatch correctly. mox/setMox upgraded to Q_INVOKABLE.
// Aim: ~30 accessors total by end of Phase 14. Each commit that adds
// accessors should note the addition in the commit message.

#pragma once

#include <QObject>
#include <QString>
#include <array>

namespace NereusSDR {

class TestMockRadioModel : public QObject {
    Q_OBJECT
public:
    explicit TestMockRadioModel(QObject* parent = nullptr) : QObject(parent) {}

    // Set the VFO frequency for a given slice (0-based) and channel (0=A,1=B).
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol.
    Q_INVOKABLE void setVfoHz(int slice, int chan, qint64 hz)
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            m_vfoHz[slice][chan] = hz;
        }
    }

    // Get the VFO frequency for a given slice and channel.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol.
    Q_INVOKABLE qint64 vfoHz(int slice, int chan) const
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            return m_vfoHz[slice][chan];
        }
        return 0;
    }

    // Set the DSP mode string for a given slice (e.g. "USB", "LSB", "CW").
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol modulation handler.
    Q_INVOKABLE void setMode(int slice, const QString& mode)
    {
        if (slice >= 0 && slice < 2) {
            m_mode[slice] = mode;
        }
    }

    // Get the DSP mode string for a given slice.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol modulation handler.
    Q_INVOKABLE QString mode(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_mode[slice];
        }
        return {};
    }

    // Set the filter band (low/high Hz) for a given slice.
    // From Thetis TCIServer.cs:4393-4399 [v2.10.3.13] — handleRxFilterBand set path.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol rx_filter_band handler.
    Q_INVOKABLE void setFilterBand(int slice, int lowHz, int highHz)
    {
        if (slice >= 0 && slice < 2) {
            m_filterLow[slice]  = lowHz;
            m_filterHigh[slice] = highHz;
        }
    }

    // Get the filter low cutoff Hz for a given slice.
    // From Thetis TCIServer.cs:4380-4384 [v2.10.3.13] — handleRxFilterBand query path.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol rx_filter_band handler.
    Q_INVOKABLE int filterLow(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_filterLow[slice];
        }
        return 0;
    }

    // Get the filter high cutoff Hz for a given slice.
    // From Thetis TCIServer.cs:4380-4384 [v2.10.3.13] — handleRxFilterBand query path.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol rx_filter_band handler.
    Q_INVOKABLE int filterHigh(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_filterHigh[slice];
        }
        return 0;
    }

    // MOX (TX active) state. Q_INVOKABLE: called via QMetaObject::invokeMethod
    // from TciProtocol trx handler (Phase 8).
    // From Thetis TCIServer.cs:3468 [v2.10.3.13] — handleTrxMessage rx + mox parse.
    Q_INVOKABLE void setMox(bool on) { m_mox = on; }
    Q_INVOKABLE bool mox() const { return m_mox; }

    // Split state per slice (0-based).
    // From Thetis TCIServer.cs:3091 [v2.10.3.13] — handleSplitEnableMessage.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol split_enable handler.
    Q_INVOKABLE void setSplit(int slice, bool on)
    {
        if (slice >= 0 && slice < 2) {
            m_split[slice] = on;
        }
    }

    Q_INVOKABLE bool split(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_split[slice];
        }
        return false;
    }

    // Global mute state (both MUT and MUT2 in Thetis).
    // From Thetis TCIServer.cs:4051 [v2.10.3.13] — handleMute set/query.
    // Named globalMute to avoid collision with future per-slice mute.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol mute handler.
    Q_INVOKABLE void setGlobalMute(bool on) { m_globalMute = on; }
    Q_INVOKABLE bool globalMute() const { return m_globalMute; }

    // Per-RX mute state (0-based slice index).
    // From Thetis TCIServer.cs:4069 [v2.10.3.13] — handleMuteRX set/query.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol rx_mute handler.
    Q_INVOKABLE void setRxMute(int slice, bool on)
    {
        if (slice >= 0 && slice < 2) {
            m_rxMute[slice] = on;
        }
    }

    Q_INVOKABLE bool rxMute(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_rxMute[slice];
        }
        return false;
    }

    // VFO lock state for a given slice (0-based) and channel (0=A,1=B).
    // From Thetis TCIServer.cs:3284-3302 [v2.10.3.13] — handleVFOLock args dispatch.
    // Q_INVOKABLE: called via QMetaObject::invokeMethod from TciProtocol.
    Q_INVOKABLE void setVfoLock(int slice, int chan, bool locked)
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            m_vfoLock[slice][chan] = locked;
        }
    }

    Q_INVOKABLE bool vfoLock(int slice, int chan) const
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            return m_vfoLock[slice][chan];
        }
        return false;
    }

    // Main VFO lock (the "lock:" command's 2-arg form; drops channel arg per Thetis).
    // From Thetis TCIServer.cs:3265-3283 [v2.10.3.13] — handleLock args dispatch.
    // rx=0 → VFOALock, rx=1 → VFOBLock. Q_INVOKABLE: QMetaObject::invokeMethod.
    Q_INVOKABLE void setLock(int slice, bool locked)
    {
        if (slice >= 0 && slice < 2) {
            m_lock[slice] = locked;
        }
    }

    Q_INVOKABLE bool lock(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_lock[slice];
        }
        return false;
    }

    // Reset all state to baseline (zeros/empty). Called before each matrix row.
    void resetToBaseline()
    {
        m_vfoHz = {};
        m_mode = {};
        m_mox = false;
        m_vfoLock = {};
        m_lock = {};
        // Phase 7: common SSB filter defaults (200 Hz low, 2900 Hz high).
        m_filterLow  = { 200, 200 };
        m_filterHigh = { 2900, 2900 };
        // Phase 8: TRX family state resets.
        m_split = {};
        m_globalMute = false;
        m_rxMute = {};
    }

private:
    // 2 slices x 2 channels is sufficient for Phase 1; expand in later phases.
    std::array<std::array<qint64, 2>, 2> m_vfoHz{};
    std::array<QString, 2> m_mode{};
    bool m_mox{false};
    // Phase 6: VFO lock state (vfo_lock command) and main lock (lock command).
    std::array<std::array<bool, 2>, 2> m_vfoLock{};
    std::array<bool, 2> m_lock{};
    // Phase 7: filter band state (rx_filter_band command).
    std::array<int, 2> m_filterLow{200, 200};
    std::array<int, 2> m_filterHigh{2900, 2900};
    // Phase 8: TRX family state.
    std::array<bool, 2> m_split{};
    bool m_globalMute{false};
    std::array<bool, 2> m_rxMute{};
};

} // namespace NereusSDR
