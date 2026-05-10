// tests/TestMockRadioModel.h  (NereusSDR)
// NereusSDR-original test helper — no Thetis upstream port.
//
// Minimal in-process mock for TciProtocol tests. Inherits QObject so tests
// can pass `&mock` directly to TciProtocol (no reinterpret_cast hazard).
// Phase 6+ will mark accessors as Q_INVOKABLE so QMetaObject::invokeMethod
// can dispatch from TciProtocol. Extended per phase as new command families
// need additional accessors.
//
// Phase 1: 7 accessors (VFO hz, mode, MOX) + resetToBaseline().
// Phase 3: now inherits QObject; resolves Task 1.1 reinterpret_cast<QObject*>
//           workaround in tst_tci_matrix_runner.cpp.
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
    void setVfoHz(int slice, int chan, qint64 hz)
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            m_vfoHz[slice][chan] = hz;
        }
    }

    // Get the VFO frequency for a given slice and channel.
    qint64 vfoHz(int slice, int chan) const
    {
        if (slice >= 0 && slice < 2 && chan >= 0 && chan < 2) {
            return m_vfoHz[slice][chan];
        }
        return 0;
    }

    // Set the DSP mode string for a given slice (e.g. "USB", "LSB", "CW").
    void setMode(int slice, const QString& mode)
    {
        if (slice >= 0 && slice < 2) {
            m_mode[slice] = mode;
        }
    }

    // Get the DSP mode string for a given slice.
    QString mode(int slice) const
    {
        if (slice >= 0 && slice < 2) {
            return m_mode[slice];
        }
        return {};
    }

    void setMox(bool on) { m_mox = on; }
    bool mox() const { return m_mox; }

    // Reset all state to baseline (zeros/empty). Called before each matrix row.
    void resetToBaseline()
    {
        m_vfoHz = {};
        m_mode = {};
        m_mox = false;
    }

private:
    // 2 slices x 2 channels is sufficient for Phase 1; expand in later phases.
    std::array<std::array<qint64, 2>, 2> m_vfoHz{};
    std::array<QString, 2> m_mode{};
    bool m_mox{false};
};

} // namespace NereusSDR
