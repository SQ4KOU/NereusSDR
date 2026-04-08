#pragma once

#include <QObject>
#include <QVector>

namespace NereusSDR {

// C++20 wrapper around the WDSP DSP library.
// Each receiver gets one WDSP "channel" with independent DSP state.
//
// WDSP handles: demodulation, AGC, noise blanker (NB/NB2), noise reduction
// (NR/NR2), auto-notch filter (ANF), bandpass filtering, equalization,
// TX compression, CESSB, VOX/DEXP, PureSignal PA linearization.
//
// When WDSP is not available (HAVE_WDSP not defined), all methods are no-ops.
class WdspEngine : public QObject {
    Q_OBJECT

public:
    explicit WdspEngine(QObject* parent = nullptr);
    ~WdspEngine() override;

    // Channel lifecycle
    int createChannel(int sampleRate, int fftSize);
    void destroyChannel(int channelId);

    // Process I/Q samples through WDSP for a given channel.
    // Outputs demodulated audio (left/right).
    void processIq(int channelId, const float* iData, const float* qData,
                   float* outLeft, float* outRight, int sampleCount);

    // DSP parameter setters (per-channel)
    void setMode(int channelId, int mode);
    void setFilter(int channelId, int lowCut, int highCut);
    void setAgcMode(int channelId, int mode);
    void setAgcThreshold(int channelId, int threshold);
    void setNrEnabled(int channelId, bool enabled);
    void setNr2Enabled(int channelId, bool enabled);
    void setNbEnabled(int channelId, bool enabled);
    void setNb2Enabled(int channelId, bool enabled);
    void setAnfEnabled(int channelId, bool enabled);
    void setSquelchEnabled(int channelId, bool enabled);
    void setSquelchLevel(int channelId, int level);

    // TX DSP
    void setTxCompression(int channelId, bool enabled, float level);
    void setTxEqEnabled(int channelId, bool enabled);

    // Spectrum/FFT data extraction
    void getSpectrum(int channelId, float* buffer, int size);

signals:
    void spectrumReady(int channelId, const QVector<float>& data);

private:
    struct ChannelState {
        int sampleRate{48000};
        int fftSize{4096};
        int mode{0};
        int filterLow{-2850};
        int filterHigh{-150};
        int agcMode{3};  // Medium
        bool nrEnabled{false};
        bool nbEnabled{false};
        bool anfEnabled{false};
    };

    QMap<int, ChannelState> m_channels;
    int m_nextChannelId{0};
};

} // namespace NereusSDR
