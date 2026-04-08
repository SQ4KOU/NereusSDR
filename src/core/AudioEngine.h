#pragma once

#include <QObject>
#include <QString>

namespace NereusSDR {

// Audio engine for NereusSDR.
// Unlike AetherSDR (which receives decoded PCM from the radio), NereusSDR
// receives raw I/Q samples and must decode via WDSP before outputting audio.
//
// Data flow: Radio → Raw I/Q → WdspEngine → Decoded audio → AudioEngine → Speakers
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Feed decoded audio from WDSP to the output device
    void feedAudio(int receiverId, const float* leftSamples, const float* rightSamples, int sampleCount);

    // Audio output device management
    void setAudioOutputDevice(const QString& deviceName);
    QString audioOutputDevice() const;

    // Volume control
    void setVolume(float volume);
    float volume() const { return m_volume; }

signals:
    void audioOutputDeviceChanged(const QString& deviceName);
    void volumeChanged(float volume);

private:
    QString m_outputDevice;
    float m_volume{1.0f};
};

} // namespace NereusSDR
