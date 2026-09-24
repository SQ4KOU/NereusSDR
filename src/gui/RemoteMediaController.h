#pragma once
// no-port-check: NereusSDR-original. Remote daemon R3 receive display wiring.

#include "core/session/media/DisplayBudget.h"
#include "core/session/media/IReceiverPcmSink.h"
#include "core/session/media/MediaPeer.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "gui/PanStatusText.h"
#include "gui/RemoteAudioStatus.h"
#include <QHash>
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
    /// Why a pan's display is below what it asked for (R-R3-08, R-R3-37):
    /// CoreBusy when the Core lowered its display budget because its
    /// computer is busy; None for a pan at its requested quality, a pan the
    /// Core did not give a reason for, or an unknown pan. Set on every
    /// budget replan with the pan's status line.
    DisplayBudgetReason panDisplayBudgetReason(const QString& panId) const;

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
    /// R-R3-43: this Core can send a receiver's audio on its own stream:
    /// audioProfileNegotiated() and a Core advertising receiverAudioVersion
    /// 1 or later. Only then does the media start carry
    /// receiverAudioVersion and a receiver-audio request go out; otherwise
    /// the controls on the wire are exactly today's.
    bool receiverAudioNegotiated() const;
    /// R-R3-43: the stop reason a consumer gets from a Core that cannot send
    /// a receiver's audio. Plain words, shown as it is.
    static constexpr const char* kReceiverAudioUnavailableReason =
        "This Core cannot send a receiver's audio.";
    /// R-R3-43: an app on this computer (TCI, VAX) wants the Core's slice
    /// `sliceId` audio, without a speaker. Reference-counted per slice: the
    /// first sink asks the Core for the slice's stream, later sinks share
    /// it. The stream follows the one audio quality choice and runs while
    /// the speakers are muted. The sink stays registered across media
    /// reconnects until released; see IReceiverPcmSink for what it is told.
    /// Adding a sink that is already registered for the slice does nothing.
    /// GUI thread only.
    void requestReceiverAudio(int sliceId, IReceiverPcmSink* sink);
    /// R-R3-43: undoes one requestReceiverAudio(). When it returns, `sink`
    /// is not called again for this slice; the last sink's release asks the
    /// Core to stop the stream. GUI thread only.
    void releaseReceiverAudio(int sliceId, IReceiverPcmSink* sink);
    /// R-R3-43: each wanted receiver stream's measured health, by slice id.
    QHash<int, RemoteAudioReceiverTelemetry> receiverAudioTelemetry() const;
    /// R-R3-45: this Core can send the headphones mix on its own stream:
    /// audioProfileNegotiated() and a Core advertising headphonesMixVersion
    /// 1 or later. Only then does the media start carry
    /// headphonesMixVersion (the Core then sends the speakers' mix alone on
    /// the main stream) and a headphones-audio request go out; otherwise
    /// the controls on the wire are exactly today's.
    bool headphonesMixNegotiated() const;
    /// R-R3-45: why a receiver routed to the headphones is not heard, for
    /// the slice flags; empty when nothing is wrong on the Core's side or
    /// this computer's headphones device. Plain words, shown as they are.
    /// (No headphones set up on this computer is the flag's own notice.)
    QString headphonesProblem() const;
    /// R-R3-45: headphonesProblem() from a Core that cannot send the
    /// headphones mix.
    static constexpr const char* kHeadphonesMixUnavailableReason =
        "This Core cannot send audio for the headphones.";
    /// R-R3-45: headphonesProblem() when the Core could not start the mix.
    static constexpr const char* kHeadphonesCoreCouldNotStart =
        "The Core could not start the audio for the headphones.";
    /// R-R3-45: headphonesProblem() after a headphones device fault on this
    /// computer, in the operator's words (the toast says the same).
    static QString headphonesFaultText(RemoteAudioReceiver::Fault fault);
    /// R-R3-45: the headphones mix's playback health on this computer.
    RemoteAudioReceiverTelemetry headphonesTelemetry() const;
    /// R-R3-45: the headphones-audio-context most recently accepted, or
    /// empty before the first and after stop().
    std::optional<RemoteAudioContextMessage> acceptedHeadphonesContext() const;
    /// R-R3-35: the measured audio delay now. measurable follows
    /// audioClockNegotiated() while a media session exists; estimate is
    /// present only while audio plays, echoes arrive (the newest younger
    /// than AudioClockEstimator::kEchoStaleNs) and the Core's capture
    /// belongs to the audio context being played.
    RemoteAudioDelayReport audioDelay() const;
    /// R-R3-35: how often a clock probe goes out while audio plays.
    static constexpr int kClockProbeIntervalMs = 1000;
    /// R-R3-37: how long a pan in budget mode may wait for the Core's first
    /// answer before it says "Waiting for the Core". A Core that answers
    /// within this (the usual case at session start) never flashes the line.
    static constexpr int kPanWaitingGraceMs = 2000;
    /// R-R3-37: what the pan named `panId` was last told about its remote
    /// display, including its zoom-detail limit. The pan paints
    /// buildPanStatusText() of this.
    PanDisplayState panDisplayState(const QString& panId) const;

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
    /// R-R3-45: headphonesProblem() changed.
    void headphonesProblemChanged(const QString& problem);

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
    void setPanStatus(const QString& panId, const PanDisplayState& status);
    PanDisplayState statusWithGrant(const QString& panId, PanDisplayState status) const;
    void refreshPanGrantStatus(const QString& panId);
    PanDisplayState perPanRefusalStatus(const QString& panId) const;
    void refreshCtunState();
    void receiveControl(const QJsonObject& payload, quint32 epoch);
    void receiveDisplay(const QByteArray& packet);
    void reportDisplayDrops();
    void requestKeyframe(quint32 endpointId);
    void requestAudio();
    void sendReceiverAudioRequest(int sliceId, bool enabled);
    void requestWantedReceiverAudio();
    void receiveReceiverAudioContext(const QJsonObject& payload);
    void notifyReceiverStopped(int sliceId, const QString& reason);
    void onReceiverRestart(int sliceId, RemoteAudioReceiver* receiver, const QString& reason,
                           RemoteAudioReceiver::Fault fault);
    void onReceiverError(int sliceId, RemoteAudioReceiver* receiver, const QString& reason,
                         RemoteAudioReceiver::Fault fault);
    void reconcileLinkTrial();
    // R-R3-45: the headphones mix.
    bool headphonesWanted() const;
    void requestHeadphonesAudio();
    void receiveHeadphonesAudioContext(const QJsonObject& payload);
    void onHeadphonesRestart(const QString& reason, RemoteAudioReceiver::Fault fault);
    void onHeadphonesError(const QString& reason, RemoteAudioReceiver::Fault fault);
    void setHeadphonesProblem(const QString& problem);
    void refreshAudioStatus();
    void checkLosslessLink();
    void fallBackToOpus(const QString& cause);
    void sendClockProbe();
    void reconcileClockProbe();
    void receiveClockEcho(const QJsonObject& payload, qint64 receivedNs);
    bool send(QJsonObject payload);
};
} // namespace NereusSDR
