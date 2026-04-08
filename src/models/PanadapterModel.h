#pragma once

#include <QObject>

namespace NereusSDR {

// Represents a single panadapter display.
// In NereusSDR, panadapters are entirely client-side — the radio sends
// raw I/Q, and the client computes FFT data for display. This model
// holds display state (center frequency, bandwidth, dBm range).
class PanadapterModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(double centerFrequency READ centerFrequency WRITE setCenterFrequency NOTIFY centerFrequencyChanged)
    Q_PROPERTY(double bandwidth       READ bandwidth       WRITE setBandwidth       NOTIFY bandwidthChanged)
    Q_PROPERTY(int    dBmFloor        READ dBmFloor        WRITE setdBmFloor        NOTIFY levelChanged)
    Q_PROPERTY(int    dBmCeiling      READ dBmCeiling      WRITE setdBmCeiling      NOTIFY levelChanged)

public:
    explicit PanadapterModel(QObject* parent = nullptr);
    ~PanadapterModel() override;

    double centerFrequency() const { return m_centerFrequency; }
    void setCenterFrequency(double freq);

    double bandwidth() const { return m_bandwidth; }
    void setBandwidth(double bw);

    int dBmFloor() const { return m_dBmFloor; }
    void setdBmFloor(int floor);

    int dBmCeiling() const { return m_dBmCeiling; }
    void setdBmCeiling(int ceiling);

    int fftSize() const { return m_fftSize; }
    void setFftSize(int size);

    int averageCount() const { return m_averageCount; }
    void setAverageCount(int count);

signals:
    void centerFrequencyChanged(double freq);
    void bandwidthChanged(double bw);
    void levelChanged();
    void fftSizeChanged(int size);

private:
    double m_centerFrequency{14225000.0};
    double m_bandwidth{200000.0};  // 200 kHz default
    int m_dBmFloor{-130};
    int m_dBmCeiling{-40};
    int m_fftSize{4096};
    int m_averageCount{3};
};

} // namespace NereusSDR
