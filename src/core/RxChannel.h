#pragma once

#include "WdspTypes.h"

#include <QObject>

#include <atomic>
#include <cstring>

namespace NereusSDR {

// Per-receiver WDSP channel wrapper.
//
// Owns one WDSP RX channel and provides Qt property access to DSP
// parameters. Each setter immediately calls the corresponding WDSP
// API function and emits a change signal.
//
// Thread safety:
//   - Main thread: create/destroy, all property setters
//   - Audio thread (future): processIq() calls fexchange2
//   - Meter timer: getMeter() — WDSP meter reads are lock-free
//
// Ported from Thetis cmaster.c create_rcvr / wdsp-integration.md section 4.
class RxChannel : public QObject {
    Q_OBJECT

    Q_PROPERTY(NereusSDR::DSPMode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(NereusSDR::AGCMode agcMode READ agcMode WRITE setAgcMode NOTIFY agcModeChanged)
    Q_PROPERTY(bool active READ isActive WRITE setActive NOTIFY activeChanged)

public:
    explicit RxChannel(int channelId, int bufferSize, int sampleRate,
                       QObject* parent = nullptr);
    ~RxChannel() override;

    int channelId() const { return m_channelId; }
    int bufferSize() const { return m_bufferSize; }
    int sampleRate() const { return m_sampleRate; }

    // --- Demodulation ---

    DSPMode mode() const { return static_cast<DSPMode>(m_mode.load()); }
    void setMode(DSPMode mode);

    // --- Bandpass filter ---

    void setFilterFreqs(double lowHz, double highHz);
    double filterLow() const { return m_filterLow; }
    double filterHigh() const { return m_filterHigh; }

    // --- AGC ---

    AGCMode agcMode() const { return static_cast<AGCMode>(m_agcMode.load()); }
    void setAgcMode(AGCMode mode);
    void setAgcTop(double topdB);

    // AGC advanced parameters
    // From Thetis Project Files/Source/Console/radio.cs:1037-1124
    // From Thetis Project Files/Source/Console/console.cs:45977
    int agcThreshold() const { return m_agcThreshold.load(); }
    void setAgcThreshold(int dBu);

    int agcHang() const { return m_agcHang.load(); }
    void setAgcHang(int ms);

    int agcSlope() const { return m_agcSlope.load(); }
    void setAgcSlope(int slope);

    int agcAttack() const { return m_agcAttack.load(); }
    void setAgcAttack(int ms);

    int agcDecay() const { return m_agcDecay.load(); }
    void setAgcDecay(int ms);

    // --- Noise blanker ---

    bool nb1Enabled() const { return m_nb1Enabled.load(); }
    void setNb1Enabled(bool enabled);

    bool nb2Enabled() const { return m_nb2Enabled.load(); }
    void setNb2Enabled(bool enabled);

    // --- Noise reduction ---

    void setNrEnabled(bool enabled);
    void setAnfEnabled(bool enabled);

    // EMNR (NR2) — Enhanced Multiband Noise Reduction
    // From Thetis Project Files/Source/Console/radio.cs:2216-2251
    bool emnrEnabled() const { return m_emnrEnabled.load(); }
    void setEmnrEnabled(bool enabled);
    void setEmnrGainMethod(int method);
    void setEmnrNpeMethod(int method);
    void setEmnrAeRun(bool run);
    void setEmnrPosition(int position);

    // SNB — Spectral Noise Blanker
    // From Thetis Project Files/Source/Console/radio.cs (SetRXASNBARun call site)
    bool snbEnabled() const { return m_snbEnabled.load(); }
    void setSnbEnabled(bool enabled);

    // --- Frequency shift (for pan offset from VFO) ---

    void setShiftFrequency(double offsetHz);

    // --- Channel state ---

    bool isActive() const { return m_active.load(); }
    void setActive(bool active);

    // --- Audio processing (called from audio thread) ---

    // Process I/Q samples through the WDSP RX chain.
    // Input:  inI/inQ arrays of sampleCount floats (raw I/Q from radio)
    // Output: outI/outQ arrays of sampleCount floats (decoded audio L/R)
    //
    // NB1/NB2 are processed before fexchange2:
    //   xanbEXTF(id, I, Q)  -- if NB1 enabled (in-place)
    //   xnobEXTF(id, I, Q)  -- if NB2 enabled (in-place)
    //   fexchange2(channel, Iin, Qin, Iout, Qout, &error)
    //
    // From Thetis wdsp-integration.md section 4.3
    void processIq(float* inI, float* inQ,
                   float* outI, float* outQ,
                   int sampleCount);

    // --- Metering ---

    double getMeter(RxMeterType type) const;

signals:
    void modeChanged(NereusSDR::DSPMode mode);
    void agcModeChanged(NereusSDR::AGCMode mode);
    void activeChanged(bool active);
    void filterChanged(double low, double high);

private:
    const int m_channelId;
    const int m_bufferSize;
    const int m_sampleRate;

    // Atomic flags for lock-free audio thread reads
    std::atomic<int> m_mode{static_cast<int>(DSPMode::LSB)};  // Must match WdspEngine::createRxChannel init
    std::atomic<int> m_agcMode{static_cast<int>(AGCMode::Med)};
    std::atomic<bool> m_nb1Enabled{false};
    std::atomic<bool> m_nb2Enabled{false};
    std::atomic<bool> m_nrEnabled{false};
    std::atomic<bool> m_anfEnabled{false};
    // emnr: From Thetis radio.cs:2216 — rx_nr2_run default = 0
    std::atomic<bool> m_emnrEnabled{false};
    // snb: Spectral Noise Blanker — off by default
    std::atomic<bool> m_snbEnabled{false};
    std::atomic<bool> m_active{false};

    // AGC advanced parameters — atomic for thread-safe reads from audio thread
    // Defaults from Thetis Project Files/Source/Console/radio.cs:1037-1124
    // threshold default: From Thetis console.cs:45977 — agc_thresh_point = -20
    std::atomic<int> m_agcThreshold{-20};
    // hang: From Thetis radio.cs:1056-1057 — rx_agc_hang = 250 ms
    std::atomic<int> m_agcHang{250};
    // slope: From Thetis radio.cs:1107-1108 — rx_agc_slope = 0
    std::atomic<int> m_agcSlope{0};
    // attack: From WDSP wcpAGC.c create_wcpagc — tau_attack default 2 ms
    std::atomic<int> m_agcAttack{2};
    // decay: From Thetis radio.cs:1037-1038 — rx_agc_decay = 250 ms
    std::atomic<int> m_agcDecay{250};

    // Cached filter state
    double m_filterLow{150.0};
    double m_filterHigh{2850.0};
};

} // namespace NereusSDR
