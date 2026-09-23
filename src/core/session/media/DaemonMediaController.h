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
#include <QTimer>

#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <optional>

namespace NereusSDR {

class RadioModel;
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

/// Owns one authenticated daemon media session: strict control validation,
/// actual RadioModel I/Q to bounded source, per-endpoint reduction/codec and
/// one-at-a-time media sends. The caller owns StationServer and RadioModel.
class DaemonMediaController final : public QObject {
    Q_OBJECT
public:
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
    /// What Core granted a live spectrum endpoint: FFT size and tier after
    /// the largest-size and shared-engine rules, and pixels after the source
    /// bin rule (R-R3-01, R-R3-08). Empty for an unknown endpoint.
    std::optional<SpectrumGrant> spectrumGrant(quint32 endpointId) const;

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
    bool acceptPeerControl(const QJsonObject& control);

    void clearSession();
    void clearProduction();
    QList<quint32> endpointIds() const;
    void removeEndpoint(quint32 endpointId, bool retainOperation = true);
    bool reconcileSource(const MediaSourceKey& key);
    void releaseSourceIfUnused(const MediaSourceKey& key);
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
    bool sendControl(const QJsonObject& payload) const;
    quint32 nextContextGeneration();

    QPointer<StationServer> m_server;
    QPointer<RadioModel> m_radioModel;
    DaemonSpectrumSource m_source;
    NoiseFloorEstimator m_noiseFloorEstimator;
    MediaPeer::TransportFactory m_peerFactory;
    MonotonicClock m_monotonicClock;
    std::unique_ptr<MediaPeer> m_peer;
    std::unique_ptr<DaemonAudioSender> m_audioSender;
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
