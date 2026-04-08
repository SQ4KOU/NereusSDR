#pragma once

#include <QObject>

namespace NereusSDR {

// Transmit state management.
// Includes MOX, tune, TX frequency, power, mic gain, and PureSignal state.
class TransmitModel : public QObject {
    Q_OBJECT

    Q_PROPERTY(bool   mox       READ isMox       WRITE setMox       NOTIFY moxChanged)
    Q_PROPERTY(bool   tune      READ isTune      WRITE setTune      NOTIFY tuneChanged)
    Q_PROPERTY(int    power     READ power       WRITE setPower     NOTIFY powerChanged)
    Q_PROPERTY(float  micGain   READ micGain     WRITE setMicGain   NOTIFY micGainChanged)
    Q_PROPERTY(bool   pureSig   READ pureSigEnabled WRITE setPureSigEnabled NOTIFY pureSigChanged)

public:
    explicit TransmitModel(QObject* parent = nullptr);
    ~TransmitModel() override;

    bool isMox() const { return m_mox; }
    void setMox(bool mox);

    bool isTune() const { return m_tune; }
    void setTune(bool tune);

    int power() const { return m_power; }
    void setPower(int power);

    float micGain() const { return m_micGain; }
    void setMicGain(float gain);

    bool pureSigEnabled() const { return m_pureSigEnabled; }
    void setPureSigEnabled(bool enabled);

signals:
    void moxChanged(bool mox);
    void tuneChanged(bool tune);
    void powerChanged(int power);
    void micGainChanged(float gain);
    void pureSigChanged(bool enabled);

private:
    bool m_mox{false};
    bool m_tune{false};
    int m_power{100};
    float m_micGain{0.0f};
    bool m_pureSigEnabled{false};
};

} // namespace NereusSDR
