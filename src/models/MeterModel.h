#pragma once

#include <QObject>

namespace NereusSDR {

// Meter data from the radio (forward power, SWR, ALC, etc.).
// In Protocol 1, meter data comes from the C&C feedback in EP6 frames.
// In Protocol 2, meter data comes from the high-priority data stream.
class MeterModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(float forwardPower READ forwardPower NOTIFY metersChanged)
    Q_PROPERTY(float swr          READ swr          NOTIFY metersChanged)
    Q_PROPERTY(float alc          READ alc          NOTIFY metersChanged)
    Q_PROPERTY(float sMeter       READ sMeter       NOTIFY metersChanged)

public:
    explicit MeterModel(QObject* parent = nullptr);
    ~MeterModel() override;

    float forwardPower() const { return m_forwardPower; }
    float swr() const { return m_swr; }
    float alc() const { return m_alc; }
    float sMeter() const { return m_sMeter; }

    void setForwardPower(float value);
    void setSwr(float value);
    void setAlc(float value);
    void setSMeter(float value);

signals:
    void metersChanged();

private:
    float m_forwardPower{0.0f};
    float m_swr{1.0f};
    float m_alc{0.0f};
    float m_sMeter{0.0f};
};

} // namespace NereusSDR
