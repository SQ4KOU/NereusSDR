#pragma once
// no-port-check: NereusSDR-original. Remote daemon R3 receive display wiring.

#include "core/session/media/MediaPeer.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "gui/RemoteAudioStatus.h"
#include <QObject>
#include <functional>
#include <memory>
#include <optional>

namespace NereusSDR {
class StationClient;
class RadioModel;
class PanadapterStack;

/// Owns the GUI's media session and one bounded subscription per logical pan.
/// Layout reparenting does not retire a pan; removing it from the stack does.
/// Display data never passes through the control/property mirror.
class RemoteMediaController final : public QObject {
    Q_OBJECT
public:
    using AllocationClock = std::function<qint64()>;

    RemoteMediaController(StationClient* client, RadioModel* model,
                          PanadapterStack* stack, QObject* parent = nullptr,
                          MediaPeer::TransportFactory factory = {},
                          AllocationClock allocationClock = {},
                          int allocationAckTimeoutMs = 10'000);
    ~RemoteMediaController() override;

    quint64 receivedDisplayFrames() const;
    int activeEndpointCount() const;
    std::optional<MediaPeerTelemetry> trafficTelemetry() const;
    RemoteAudioReceiverTelemetry audioTelemetry() const;
    /// The audio context most recently accepted from Core. Its encoder and
    /// off reason are present only when audioDetailNegotiated().
    std::optional<RemoteAudioContextMessage> acceptedAudioContext() const; // nullopt before the first accepted context and after stop()
    /// Core and this GUI agreed the minor-8 audio-context detail.
    bool audioDetailNegotiated() const; // d->client && d->client->remoteAudioStatusAvailable()
    /// This computer's remote audio status. It is recomputed whenever
    /// something it depends on changes; audioStatusChanged() fires only when
    /// the value does.
    RemoteAudioStatus audioStatus() const;

public slots:
    /// Ask Core for audio again: a new request, enabled per mute and radio
    /// state like every request. A no-op without a media session or while
    /// muted on this computer. It never clears a playback problem by itself.
    void retryAudio();

signals:
    void recoveryRequested(quint32 expectedEpoch, const QString& reason);
    void errorOccurred(const QString& reason);
    void displayFrameReceived(quint32 endpointId);
    /// Once per accepted audio context, after playback was started or
    /// stopped for it. A malformed or stale context emits nothing.
    void audioContextAccepted();
    void audioStatusChanged();

private:
    struct Private;
    std::unique_ptr<Private> d;
    void start();
    void stop();
    void requestRecovery(quint32 expectedEpoch, const QString& reason);
    void refreshSubscriptions();
    void refreshBudgetSubscriptions();
    bool retireSubscriptions(const QList<quint32>& endpointIds);
    void receiveAllocationResult(const QJsonObject& payload);
    void setPanStatus(const QString& panId, const QString& status);
    void refreshCtunState();
    void receiveControl(const QJsonObject& payload, quint32 epoch);
    void receiveDisplay(const QByteArray& packet);
    void requestKeyframe(quint32 endpointId);
    void requestAudio();
    void refreshAudioStatus();
    bool send(QJsonObject payload);
};
} // namespace NereusSDR
