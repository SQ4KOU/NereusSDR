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
    /// retry. Two stages, each on a precise single-shot timer:
    ///
    /// Stage one, kMediaDescriptionDeadlineMs: Core's media description
    /// must arrive within one control heartbeat interval
    /// (StationClient::kDefaultHeartbeatIntervalMs, 20,000 ms). Core sends
    /// it as soon as its peer starts; nothing in the peer library runs on
    /// this side before it arrives, so a Core that never answers is found
    /// in about 20 s rather than after the whole library chain.
    ///
    /// Stage two, kMediaConnectDeadlineMs, restarted when the description
    /// is accepted: it must outlast the slowest failure the pinned peer
    /// library reports by itself, so the library's own typed reason wins
    /// whenever one comes. That worst case is its three connection stages
    /// running one after another, the first two succeeding only just short
    /// of their limits and the last failing at its own:
    ///   - ICE: 39,500 ms, the libjuice connectivity timer
    ///     (_deps/nereus_libjuice-src/src/agent.h:43 [@3c40a354]
    ///     ICE_PAC_TIMEOUT, armed at agent.c:2603 [@3c40a354], reported as
    ///     failed at agent.c:1224-1226 [@3c40a354]).
    ///   - DTLS: 31,000 ms, libdatachannel v0.24.5 with OpenSSL: a 1 s
    ///     retransmit timer doubling until the next wait would exceed 30 s,
    ///     so 1+2+4+8+16 s (src/impl/dtlstransport.cpp:1024-1031), then
    ///     "DTLS handshake failed" (dtlstransport.cpp:1007-1008).
    ///   - SCTP: 35,000 ms, usrsctp INIT with a 1 s initial RTO capped at
    ///     10 s and 5 retransmissions, so 1+2+4+8+10+10 s
    ///     (src/impl/sctptransport.cpp:127-142; this build keeps those
    ///     defaults, LibDataChannelMediaTransport sets only buffer sizes).
    /// That is 105,500 ms, which the library's report always comes before.
    /// The whole bound, kMediaEstablishmentDeadlineMs, is the two stages'
    /// sum, 125,500 ms. The .cpp checks each derivation.
    ///
    /// The deadline only ever fires on a live control link: if control
    /// dies first (the heartbeat declares Core dead after two missed
    /// pongs, 40 to 60 s), the session ends, media is stopped with it and
    /// the heartbeat's own retry runs instead.
    static constexpr int kMediaDescriptionDeadlineMs = 20'000;
    static constexpr int kMediaConnectDeadlineMs = 105'500;
    static constexpr int kMediaEstablishmentDeadlineMs =
        kMediaDescriptionDeadlineMs + kMediaConnectDeadlineMs;

    RemoteMediaController(StationClient* client, RadioModel* model,
                          PanadapterStack* stack, QObject* parent = nullptr,
                          MediaPeer::TransportFactory factory = {},
                          AllocationClock allocationClock = {},
                          int allocationAckTimeoutMs = 10'000,
                          int descriptionDeadlineMs = kMediaDescriptionDeadlineMs,
                          int connectDeadlineMs = kMediaConnectDeadlineMs);
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

    /// R-R3-23: the AppSettings key (stored on this computer, never on the
    /// Core) holding the remote audio choice, "Opus" or "Lossless".
    static constexpr const char* kAudioProfileSettingKey = "RemoteAudioProfile";
    /// The operator's remote audio choice, read from this computer's
    /// settings at construction and replayed on every connection.
    RemoteAudioProfile audioProfileChoice() const;
    /// Core and this GUI can use the choice: the minor-8 audio detail and a
    /// Core advertising audioProfileVersion 1 or later. Without it the GUI
    /// sends exactly today's media start and audio controls.
    bool audioProfileNegotiated() const;
    /// R-R3-35: this Core answers audio clock probes (audioClockVersion 1 or
    /// later). Without it no probe is sent and no delay is measured.
    bool audioClockNegotiated() const;
    /// R-R3-35: the measured audio delay now. measurable follows
    /// audioClockNegotiated() while a media session exists; estimate is
    /// present only while audio plays, echoes arrive (the newest younger
    /// than AudioClockEstimator::kEchoStaleNs) and the Core's capture
    /// belongs to the audio context being played.
    RemoteAudioDelayReport audioDelay() const;
    /// R-R3-35: how often a clock probe goes out while audio plays.
    static constexpr int kClockProbeIntervalMs = 1000;

public slots:
    /// Ask Core for audio again: a new request, enabled per mute and radio
    /// state like every request. A no-op without a media session or while
    /// muted on this computer. It never clears a playback problem by itself.
    void retryAudio();
    /// Stores the choice on this computer and, with a media session, asks
    /// Core for it at once. Choosing again also ends an earlier fallback to
    /// Opus and starts a new link trial.
    void setAudioProfileChoice(NereusSDR::RemoteAudioProfile profile);

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
    void settleWithoutRetry(quint32 expectedEpoch, const QString& reason);
    void refreshSubscriptions();
    void refreshBudgetSubscriptions();
    bool retireSubscriptions(const QList<quint32>& endpointIds);
    void receiveAllocationResult(const QJsonObject& payload);
    void setPanStatus(const QString& panId, const QString& status);
    QString statusWithGrant(const QString& panId, const QString& status) const;
    void refreshPanGrantStatus(const QString& panId);
    QString perPanRefusalStatus(const QString& panId) const;
    void refreshCtunState();
    void receiveControl(const QJsonObject& payload, quint32 epoch);
    void receiveDisplay(const QByteArray& packet);
    void reportDisplayDrops();
    void requestKeyframe(quint32 endpointId);
    void requestAudio();
    void refreshAudioStatus();
    void checkLosslessLink();
    void fallBackToOpus(const QString& cause);
    void sendClockProbe();
    void reconcileClockProbe();
    void receiveClockEcho(const QJsonObject& payload, qint64 receivedNs);
    bool send(QJsonObject payload);
};
} // namespace NereusSDR
