#include "AudioEngine.h"

#include <QDebug>

namespace NereusSDR {

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine() = default;

void AudioEngine::feedAudio(int receiverId, const float* leftSamples, const float* rightSamples, int sampleCount)
{
    Q_UNUSED(receiverId);
    Q_UNUSED(leftSamples);
    Q_UNUSED(rightSamples);
    Q_UNUSED(sampleCount);
    // TODO: Route decoded audio to the output device
}

void AudioEngine::setAudioOutputDevice(const QString& deviceName)
{
    if (m_outputDevice != deviceName) {
        m_outputDevice = deviceName;
        emit audioOutputDeviceChanged(deviceName);
        qDebug() << "Audio output device set to" << deviceName;
    }
}

QString AudioEngine::audioOutputDevice() const
{
    return m_outputDevice;
}

void AudioEngine::setVolume(float volume)
{
    if (!qFuzzyCompare(m_volume, volume)) {
        m_volume = volume;
        emit volumeChanged(volume);
    }
}

} // namespace NereusSDR
