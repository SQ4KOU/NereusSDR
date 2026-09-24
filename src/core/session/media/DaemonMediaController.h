#pragma once
// =================================================================
// src/core/session/media/DaemonMediaController.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. Session-owned daemon display media
// coordination; it contains neither GUI nor radio control policy.
// =================================================================

#include "core/NoiseFloorEstimator.h"
#include "core/session/media/DaemonAudioSender.h"
#include "core/session/media/DaemonSpectrumSource.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/MediaPeer.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/SpectrumEndpoint.h"

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>

namespace NereusSDR {

class RadioModel;
class SliceModel;
class StationServer;
enum class ConnectionState;

/// Read-only diagnostics for the most recent active daemon audio context.
/// A successful send means the media transport accepted the RTP packet; it is
/// not evidence of network delivery. For every context,
/// attempts = accepted + rejected + inFlight + unresolvedAtRetirement.
/// Unresolved sends were interrupted by retirement and are not packet loss.
struct DaemonAudioDiagnostics {
    quint32 contextGeneration = 0;
    quint32 revision = 0;
    bool activeContext = false;
    qint64 elapsedMs = 0;
    DaemonAudioSenderTelemetry sender;
    std::uint64_t sendAttempts = 0;
    std::uint64_t sendAccepted = 0;
    std::uint64_t sendRejected = 0;
    std::uint64_t sendInFlight = 0;
    std::uint64_t sendUnresolvedAtRetirement = 0;
};

/// Real display traffic for the current media peer (R-R3-03, R-R3-05).
/// Sizes are spectrum frames the transport took, including one the library
/// queued; fragments are SCTP DATA chunks, ceil(bytes /
/// IMediaTransport::kSctpDataPayloadBytes). A refusal is a display message
/// (spectrum frame or PureSignal chunk) offered to the transport and not
/// taken: a refused spectrum frame is dropped, a PureSignal chunk refused
/// only because the channel was busy is offered again, never having been
/// sent. queuedLate counts messages the library took but held until SCTP
/// had room; each is still sent once. Transport errors are errors on the
/// display channel only. Every count starts again with each new media peer.
struct DaemonDisplayDiagnostics {
    quint32 displayMaxKeyframeBytes = 0;
    quint32 displayMaxDeltaBytes = 0;
    quint32 displayMaxFragments = 0;
    quint64 displaySendRefusals = 0;
    quint64 displayTransportErrors = 0;
    quint64 displayQueuedLate = 0;

    bool operator==(const DaemonDisplayDiagnostics&) const = default;
};

/// The periodic journal line for these diagnostics, e.g.
/// "largestKeyframe=2977 bytes/4 fragments largestDelta=... ".
QString daemonDisplayDiagnosticsLine(const DaemonDisplayDiagnostics& diagnostics);

/// Owns one authenticated daemon media session: strict control validation,
/// actual RadioModel I/Q to bounded source, per-endpoint reduction/codec and
/// one-at-a-time media sends. The caller owns StationServer and RadioModel.
class DaemonMediaController final : public QObject {
    Q_OBJECT
public:
    /// Monotonic nanoseconds, never negative. Besides display pacing it is
    /// the Core's audio clock (R-R3-35): clock-echo times and the capture
    /// times of audio blocks, which the DSP thread reads, so an injected
    /// clock must be safe to call from any thread.
    using MonotonicClock = std::function<qint64()>;
    explicit DaemonMediaController(StationServer* server, RadioModel* radioModel,
                                   QObject* parent = nullptr,
                                   MediaPeer::TransportFactory peerFactory = {},
                                   MonotonicClock monotonicClock = {});
    ~DaemonMediaController() override;

    /// Small read-only lifecycle telemetry for daemon diagnostics and core
    /// integration tests. Endpoint internals remain session-private.
    int activeEndpointCount() const;
    int activeSourceCount() const;
    DaemonAudioDiagnostics audioDiagnostics() const;
    /// The Opus target, bit/s, for audio this controller sends (R-R3-23:
    /// nereusd's audio_bitrate). Applies to the next media peer and audio
    /// sender it creates, so DaemonApp sets it before the listener opens.
    /// Default is the encoder's own default target.
    void setAudioTargetBitrate(int bitsPerSecond);
    int audioTargetBitrate() const noexcept { return m_audioTargetBitrate; }
    /// R-R3-23: whether a GUI may switch audio to the lossless profile
    /// (nereusd.conf audio_lossless; default allow). With false a request
    /// is refused as lossless-not-allowed and Opus keeps running, and no
    /// media offer carries the lossless format. Applies to the next media
    /// peer and request.
    void setAudioLosslessAllowed(bool allowed) { m_audioLosslessAllowed = allowed; }
    bool audioLosslessAllowed() const noexcept { return m_audioLosslessAllowed; }
    /// The profile the Core's audio runs for the current peer (R-R3-23).
    RemoteAudioProfile audioProfile() const noexcept { return m_audioActiveProfile; }
    /// R-R3-43: receiver streams sending now (enabled receiver contexts).
    int activeReceiverAudioStreamCount() const;
    /// R-R3-43: the profile slice `sliceId`'s receiver stream runs, or empty
    /// while that stream is not sending.
    std::optional<RemoteAudioProfile> receiverAudioProfile(int sliceId) const;
    /// R-R3-45: the headphones mix is sending now (an enabled headphones
    /// context), and the profile it runs; empty while it is not sending.
    bool headphonesMixSending() const;
    std::optional<RemoteAudioProfile> headphonesMixProfile() const;
    DaemonDisplayDiagnostics displayDiagnostics() const;
    /// What Core granted a live spectrum endpoint: FFT size and tier after
    /// the largest-size and shared-engine rules, and pixels after the source
    /// bin rule (R-R3-01, R-R3-08). Empty for an unknown endpoint.
    std::optional<SpectrumGrant> spectrumGrant(quint32 endpointId) const;
    /// The frame rate Core configured on the engine that feeds a live
    /// spectrum endpoint (R-R3-01, R-R3-08). Empty for an unknown endpoint.
    std::optional<int> spectrumSourceFps(quint32 endpointId) const;
    /// Whether that engine's transforms follow its frame rate: true only
    /// while the display budget is lowered because the Core is busy
    /// (R-R3-08, R-R3-40). Empty for an unknown endpoint.
    std::optional<bool> spectrumSourceTransformsFollowFrameRate(quint32 endpointId) const;
    /// Display traffic accepted now: every live spectrum endpoint's charge
    /// plus PureSignal's display while it is subscribed (R-R3-08, R-R3-37).
    /// What the display load governor scales when the Core is busy.
    DisplayBudgetCharge acceptedDisplayCharge() const;

private:
    struct EndpointEntry;
    struct AllocationRecord {
        QJsonObject request;
        quint32 revision{0};
        bool accepted{false};
        bool explicitlyRetired{false};
        QString reason;
    };
    struct SourceRuntime;
    /// R-R3-43: one slice's receiver audio request and, while it holds a
    /// receiver stream id, the sender capturing that slice.
    struct ReceiverAudioStream {
        quint32 revision{0};
        bool desiredEnabled{false};
        RemoteAudioProfile requestedProfile{RemoteAudioProfile::Opus};
        RemoteAudioProfile activeProfile{RemoteAudioProfile::Opus};
        std::optional<RemoteAudioProfileRefusal> profileRefusal;
        /// Index into MediaPeer::receiverAudioSsrcs(), or -1 with none.
        int streamIndex{-1};
        bool sending{false};
        std::unique_ptr<DaemonAudioSender> sender;
    };
    /// R-R3-45: the headphones mix for a GUI that declared
    /// headphonesMixVersion: its latest request and, while it runs, the
    /// sender capturing AudioEngine's headphones-mix tap. The RTP timeline
    /// of the one headphones stream id continues across contexts.
    struct HeadphonesAudioStream {
        quint32 revision{0};
        bool desiredEnabled{false};
        RemoteAudioProfile requestedProfile{RemoteAudioProfile::Opus};
        RemoteAudioProfile activeProfile{RemoteAudioProfile::Opus};
        std::optional<RemoteAudioProfileRefusal> profileRefusal;
        bool sending{false};
        quint16 nextSequence{1};
        quint32 nextTimestamp{0};
        std::unique_ptr<DaemonAudioSender> sender;
    };
    struct AdmittedAudioProfile {
        RemoteAudioProfile active{RemoteAudioProfile::Opus};
        std::optional<RemoteAudioProfileRefusal> refusal;
    };

    void onSessionStarted(quint64 epoch);
    void onSessionEnded(quint64 epoch);
    void onControl(const QJsonObject& control, quint64 epoch);
    void onSourceFrame(MediaSourceKey key);
    void onWidebandSourceChanged(int adc);
    bool reconcileWidebandDemand(EndpointEntry& entry);
    std::optional<WidebandDisplayContext> widebandContext(const EndpointEntry& entry) const;
    void onSendTick();
    void onStreamGeometryChanged(int streamIndex, double centreHz, int sampleRateHz);
    void onStreamBindingsChanged(int streamIndex, const QVector<int>& sliceIds);
    void onSliceRemoved(int sliceId);
    void onRadioConnectionStateChanged(ConnectionState state);

    bool handleStart(const QJsonObject& control);
    bool handleSubscribe(const QJsonObject& control);
    bool handleUnsubscribe(const QJsonObject& control);
    bool handleKeyframe(const QJsonObject& control);
    bool handleAudio(const QJsonObject& control);
    /// R-R3-43: {op:"receiver-audio", connectionId, sliceId, revision,
    /// enabled, profile}, only from a GUI that declared receiverAudioVersion
    /// in its start; anything else is ignored. Answered with a
    /// receiver-audio-context; never touches the main audio context.
    bool handleReceiverAudio(const QJsonObject& control);
    /// R-R3-45: {op:"headphones-audio", connectionId, revision, enabled,
    /// profile}, only from a GUI that declared headphonesMixVersion in its
    /// start; anything else is ignored. Answered with a
    /// headphones-audio-context; never touches the main audio context.
    bool handleHeadphonesAudio(const QJsonObject& control);
    /// R-R3-35: answers {op:"clock-probe", connectionId, id, t0} with
    /// {op:"clock-echo", connectionId, id, t0, t1, t2, generation,
    /// rtpTimestamp, capturedNs}. t1 is the Core clock on entry to
    /// onControl(), t2 just before the reply. generation is the running
    /// audio context and rtpTimestamp/capturedNs the newest captured block's
    /// end (DaemonAudioSenderTelemetry::captureTimestamp/captureNs); all
    /// three are 0 when no audio context is capturing.
    bool handleClockProbe(const QJsonObject& control, qint64 receivedNs);
    bool acceptPeerControl(const QJsonObject& control);

    void clearSession();
    void clearProduction();
    QList<quint32> endpointIds() const;
    void removeEndpoint(quint32 endpointId, bool retainOperation = true);
    bool reconcileSource(const MediaSourceKey& key);
    void releaseSourceIfUnused(const MediaSourceKey& key);
    void rebalanceSourceAfterDeparture(const MediaSourceKey& key);
    void configureEndpointFromFrame(EndpointEntry& endpoint,
                                    const DaemonSpectrumFrame& frame);
    void sendContext(EndpointEntry& endpoint);
    std::optional<float> fullSourceNoiseFloor(const DaemonSpectrumFrame& sourceFrame,
                                               double stationOffsetDb);
    void sendRejected(const QString& connectionId, quint32 endpointId,
                      quint32 revision, const QString& reason);
    void sendAllocationResult(const QString& connectionId, quint32 endpointId,
                              quint32 revision, bool accepted, const QString& reason);
    bool rejectAllocation(const QJsonObject& control, quint32 endpointId,
                          quint32 revision, const QString& reason,
                          bool remember = true);
    bool displayBudgetWireAvailable() const;
    bool displayPacingRequired() const;
    /// R-R3-08/40: the budget in force is lowered because the Core is busy,
    /// so every source's transforms follow its frame rate.
    bool coreBusyLimitsSources() const;
    qint64 displayNowNs() const;
    void beginDisplayBudgetIfNeeded();
    void refreshDisplayBudgetPacer();
    DisplayBudgetCharge currentSpectrumCharge() const;
    std::optional<DisplayBudgetCharge> proposedSpectrumCharge(
        quint32 endpointId, const DisplayBudgetCharge& replacement) const;
    bool spectrumAdmissionFits(quint32 endpointId,
                               const DisplayBudgetCharge& replacement) const;
    bool admitPs3Display(bool enabled, QString* refusal);
    void onRemoteAmpViewSubscriptionChanged(bool subscribed);
    void rememberNonliveOperation(quint32 endpointId, const AllocationRecord& record);
    void forgetNonliveOperation(quint32 endpointId);
    void clearAllocationIdentity();
    void promoteLatestPs3Frame();
    bool trySendPs3(MediaPeer* peer, quint64 epoch, qint64 nowNs);
    bool trySendSpectrum(MediaPeer* peer, quint64 epoch, qint64 nowNs);
    void reconcileAudio();
    void stopAudioCapture();
    /// `reason` travels only in a disabled context, and only to a peer that
    /// agreed the minor-8 detail; an enabled one carries the encoder profile.
    void sendAudioContext(bool enabled, RemoteAudioOffReason reason);
    void beginAudioDiagnostics(quint32 contextGeneration);
    void finalizeAudioDiagnostics();
    void maybeLogAudioDiagnostics(bool final);
    DaemonAudioDiagnostics snapshotAudioDiagnostics() const;
    void resetAudioSession();
    /// Admits the requested profile against the Core setting and the peer's
    /// negotiated formats, setting the active profile and any refusal.
    void admitAudioProfile();
    /// The profile rules on their own: lossless when requested, allowed by
    /// the Core and carried by this media connection (or not yet known,
    /// before the peer is ready); otherwise Opus and why.
    AdmittedAudioProfile admitProfile(RemoteAudioProfile requested) const;
    void reconcileReceiverAudio(int sliceId);
    void stopReceiverAudioCapture(ReceiverAudioStream& stream);
    void releaseReceiverStreamIndex(ReceiverAudioStream& stream);
    void retireReceiverSender(ReceiverAudioStream& stream);
    void sendReceiverAudioContext(int sliceId, const ReceiverAudioStream& stream,
                                  bool enabled, RemoteAudioOffReason reason);
    void onReceiverAudioPacket(int sliceId, DaemonAudioSender* sender, const QByteArray& packet);
    void resetReceiverAudioSession();
    quint32 nextReceiverContextGeneration();
    // R-R3-45: the headphones mix.
    void reconcileHeadphonesAudio();
    void stopHeadphonesAudioCapture();
    void sendHeadphonesAudioContext(bool enabled, RemoteAudioOffReason reason);
    void onHeadphonesAudioPacket(DaemonAudioSender* sender, const QByteArray& packet);
    void resetHeadphonesAudioSession();
    quint32 nextHeadphonesContextGeneration();
    /// Some slice on this Core plays on the headphones now.
    bool anySliceOnHeadphones() const;
    void watchSliceOutputRoute(SliceModel* slice);
    void onOutputRoutesChanged();
    void recordDisplaySent(const QByteArray& spectrumFrame, bool keyframe);
    void onMediaTransportError(const QString& message);
    void onMediaPeerError(const QString& message);
    void logDisplayDiagnostics(bool final);
    bool sendControl(const QJsonObject& payload) const;
    quint32 nextContextGeneration();

    QPointer<StationServer> m_server;
    QPointer<RadioModel> m_radioModel;
    DaemonSpectrumSource m_source;
    /// coreBusyLimitsSources() as last applied to the sources.
    bool m_transformsFollowFrameRate = false;
    NoiseFloorEstimator m_noiseFloorEstimator;
    MediaPeer::TransportFactory m_peerFactory;
    MonotonicClock m_monotonicClock;
    std::unique_ptr<MediaPeer> m_peer;
    std::unique_ptr<DaemonAudioSender> m_audioSender;
    int m_audioTargetBitrate{OpusAudioCodecConfig{}.bitrate};
    // R-R3-23 lossless audio, per media peer. The GUI declares it understands
    // the profile in its media start (the offer then carries L16) and in its
    // audio control (contexts then carry the profile shape).
    bool m_audioLosslessAllowed{true};
    bool m_audioProfileNegotiated{false};
    RemoteAudioProfile m_audioRequestedProfile{RemoteAudioProfile::Opus};
    RemoteAudioProfile m_audioActiveProfile{RemoteAudioProfile::Opus};
    std::optional<RemoteAudioProfileRefusal> m_audioProfileRefusal;
    // R-R3-43 receiver audio, per media peer. Requests are honoured only
    // when the GUI declared receiverAudioVersion at start (the offer then
    // declares the receiver stream ids). Entries outlive their streams so a
    // stale revision stays refused; only slices that existed are entered.
    bool m_receiverAudioNegotiated{false};
    std::map<int, ReceiverAudioStream> m_receiverStreams;
    /// Per receiver stream id: the slice holding it (-1 free) and the next
    /// RTP sequence and timestamp, so each id's timeline continues across
    /// contexts as the main stream's does.
    std::array<int, IMediaTransport::kMaxReceiverAudioStreams> m_receiverStreamSlice{};
    std::array<quint16, IMediaTransport::kMaxReceiverAudioStreams> m_receiverNextSequence{};
    std::array<quint32, IMediaTransport::kMaxReceiverAudioStreams> m_receiverNextTimestamp{};
    quint32 m_nextReceiverContextGeneration{0};
    // R-R3-45 headphones mix, per media peer. Requests are honoured only
    // when the GUI declared headphonesMixVersion at start (the offer then
    // declares the headphones stream id, and the main stream carries the
    // speakers' mix alone). m_headphonesRouted is anySliceOnHeadphones() as
    // last acted on.
    bool m_headphonesMixNegotiated{false};
    HeadphonesAudioStream m_headphones;
    quint32 m_nextHeadphonesContextGeneration{0};
    bool m_headphonesRouted{false};
    std::map<quint32, EndpointEntry> m_endpoints;
    QMap<MediaSourceKey, SourceRuntime> m_sources;
    QTimer m_sendTimer;
    QTimer m_audioDiagnosticsTimer;
    QElapsedTimer m_displayClock;
    DisplayBudgetPacer m_displayPacer;
    bool m_displayPacerInitialized{false};
    quint64 m_lastSessionEpoch{0};
    quint64 m_epoch{0};
    quint32 m_nextContextGeneration{0};
    quint32 m_audioRevision{0};
    quint16 m_audioNextSequence{1};
    quint32 m_audioNextTimestamp{0};
    bool m_audioDesiredEnabled{false};
    DaemonAudioDiagnostics m_audioDiagnostics;
    QElapsedTimer m_audioDiagnosticsClock;
    qint64 m_audioDiagnosticsLastLogMs{0};
    QTimer m_displayDiagnosticsTimer;
    DaemonDisplayDiagnostics m_displayDiagnostics;
    DaemonDisplayDiagnostics m_displayDiagnosticsLogged;
    QSet<QString> m_loggedTransportErrorKinds;
    int m_roundRobinCursor{0};
    QList<QByteArray> m_ps3CurrentChunks;
    QList<QByteArray> m_ps3LatestChunks;
    bool m_ps3CurrentAttempted{false};
    bool m_lastDisplayAttemptWasPs3{false};
    quint32 m_endpointHighWater{0};
    std::map<quint32, AllocationRecord> m_nonliveOperations;
    QList<quint32> m_nonliveOperationOrder;
};

} // namespace NereusSDR
