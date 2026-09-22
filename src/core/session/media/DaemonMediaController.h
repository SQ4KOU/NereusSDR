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
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/MediaPeer.h"
#include "core/session/media/SpectrumEndpoint.h"

#include <QElapsedTimer>
#include <QMap>
#include <QPointer>
#include <QTimer>

#include <cstdint>
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
    explicit DaemonMediaController(StationServer* server, RadioModel* radioModel,
                                   QObject* parent = nullptr,
                                   MediaPeer::TransportFactory peerFactory = {});
    ~DaemonMediaController() override;

    /// Small read-only lifecycle telemetry for daemon diagnostics and core
    /// integration tests. Endpoint internals remain session-private.
    int activeEndpointCount() const;
    int activeSourceCount() const;
    DaemonAudioDiagnostics audioDiagnostics() const;

private:
    struct EndpointEntry;
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
    void removeEndpoint(quint32 endpointId);
    bool reconcileSource(const MediaSourceKey& key);
    void releaseSourceIfUnused(const MediaSourceKey& key);
    void configureEndpointFromFrame(EndpointEntry& endpoint,
                                    const DaemonSpectrumFrame& frame);
    void sendContext(EndpointEntry& endpoint);
    std::optional<float> fullSourceNoiseFloor(const DaemonSpectrumFrame& sourceFrame,
                                               double stationOffsetDb);
    void sendRejected(const QString& connectionId, quint32 endpointId,
                      quint32 revision, const QString& reason);
    void reconcileAudio();
    void stopAudioCapture();
    void sendAudioContext(bool enabled);
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
    std::unique_ptr<MediaPeer> m_peer;
    std::unique_ptr<DaemonAudioSender> m_audioSender;
    std::map<quint32, EndpointEntry> m_endpoints;
    QMap<MediaSourceKey, SourceRuntime> m_sources;
    QTimer m_sendTimer;
    QTimer m_audioDiagnosticsTimer;
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
    QList<QByteArray> m_ps3Chunks;
};

} // namespace NereusSDR
