#pragma once
// no-port-check: NereusSDR-original remote audio lifecycle and worker wiring.
#include <QObject>
#include <QByteArray>
#include <memory>

namespace NereusSDR {
class AudioEngine;

// One generation of bounded RTP receive, Opus decoding and WDSP rate matching.
// All codec/resampler work runs off the GUI and device callback threads.
class RemoteAudioReceiver final : public QObject {
    Q_OBJECT
public:
    explicit RemoteAudioReceiver(AudioEngine* engine, QObject* parent = nullptr);
    ~RemoteAudioReceiver() override;
    bool start(quint32 ssrc, quint32 firstTimestamp);
    void stop();
    void submit(const QByteArray& packet);
    bool isRunning() const;
    quint64 decodedPackets() const;
    quint64 concealedPackets() const;
    int rateMatcherUnderflows() const;
    int rateMatcherOverflows() const;
signals:
    void restartRequested(const QString& reason);
    void errorOccurred(const QString& reason);
private:
    struct Private;
    std::unique_ptr<Private> d;
};
} // namespace NereusSDR
