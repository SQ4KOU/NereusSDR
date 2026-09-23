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

    /// R-R3-28. How long a started media session may take to become ready
    /// before it counts as a media failure and enters the authenticated
    /// retry. It must outlast the slowest failure the pinned peer library
    /// reports by itself, so the library's own typed reason wins whenever
    /// one comes. That worst case is its three connection stages failing
    /// one after another, each only just short of its own limit:
    ///   - ICE: 39,500 ms, the libjuice connectivity timer
    ///     (_deps/nereus_libjuice-src/src/agent.h:43 ICE_PAC_TIMEOUT,
    ///     armed at agent.c:2603, reported as failed at agent.c:1224-1226).
    ///   - DTLS: 31,000 ms, libdatachannel v0.24.5 with OpenSSL: a 1 s
    ///     retransmit timer doubling until the next wait would exceed 30 s,
    ///     so 1+2+4+8+16 s (src/impl/dtlstransport.cpp:1024-1031), then
    ///     "DTLS handshake failed" (dtlstransport.cpp:1007-1008).
    ///   - SCTP: 35,000 ms, usrsctp INIT with a 1 s initial RTO capped at
    ///     10 s and 5 retransmissions, so 1+2+4+8+10+10 s
    ///     (src/impl/sctptransport.cpp:127-142; this build keeps those
    ///     defaults, LibDataChannelMediaTransport sets only buffer sizes).
    /// That is 105,500 ms. One control heartbeat interval
    /// (StationClient::kDefaultHeartbeatIntervalMs, 20,000 ms) is added as
    /// the margin, giving 125,500 ms. The .cpp checks the sum.
    ///
    /// The deadline only ever fires on a live control link: if control
    /// dies first (the heartbeat declares Core dead after two missed
    /// pongs, 40 to 60 s), the session ends, media is stopped with it and
    /// the heartbeat's own retry runs instead.
    static constexpr int kMediaEstablishmentDeadlineMs = 125'500;

    RemoteMediaController(StationClient* client, RadioModel* model,
                          PanadapterStack* stack, QObject* parent = nullptr,
                          MediaPeer::TransportFactory factory = {},
                          AllocationClock allocationClock = {},
                          int allocationAckTimeoutMs = 10'000,
                          int establishmentDeadlineMs = kMediaEstablishmentDeadlineMs);
    ~RemoteMediaController() override;

    quint64 receivedDisplayFrames() const;
    int activeEndpointCount() const;
    std::optional<MediaPeerTelemetry> trafficTelemetry() const;
    /// Display updates this computer received but discarded, oldest first,
    /// because newer ones arrived before it could show them. Zero without a
    /// media session; each session starts from zero.
    quint64 displayMessagesDropped() const;
    RemoteAudioReceiverTelemetry audioTelemetry() const;
    /// The audio context most recently accepted from Core. Its encoder and
    /// off reason are present only when audioDetailNegotiated().
    std::optional<RemoteAudioContextMessage> acceptedAudioContext() const; // nullopt before the first accepted context and after stop()
    /// Core and this GUI agreed the minor-8 audio-context detail.
    bool audioDetailNegotiated() const; // d->client && d->client->remoteAudioStatusAvailable()
    /// Core and this GUI agreed the minor-9 spectrum grant report.
    bool spectrumGrantNegotiated() const; // d->client && d->client->spectrumGrantAvailable()
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
    QString statusWithGrant(const QString& panId, const QString& status) const;
    void refreshPanGrantStatus(const QString& panId);
    void refreshCtunState();
    void receiveControl(const QJsonObject& payload, quint32 epoch);
    void receiveDisplay(const QByteArray& packet);
    void reportDisplayDrops();
    void requestKeyframe(quint32 endpointId);
    void requestAudio();
    void refreshAudioStatus();
    bool send(QJsonObject payload);
};
} // namespace NereusSDR
