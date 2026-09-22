#pragma once
// no-port-check: NereusSDR-original. Bounded observational Core telemetry
// collection for R-R3-32/33; no radio, media, retry or liveness policy.

#include "core/session/StationTelemetry.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/ConnectionState.h"

#include <QElapsedTimer>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <cstdint>
#include <functional>
#include <optional>

namespace NereusSDR {

class RadioConnection;
class RadioModel;
class StationServer;

/// Publishes one bounded station snapshot per second while an authenticated,
/// snapshot-complete telemetry session exists. All observations are read-only.
class DaemonTelemetryController final : public QObject {
    Q_OBJECT
public:
    using MonotonicClock = std::function<qint64()>;
    using AudioDiagnosticsProvider = std::function<DaemonAudioDiagnostics()>;

    static constexpr int kSamplePeriodMs = 1000;
    static constexpr int kObservationStalePeriods = 3;

    explicit DaemonTelemetryController(
        StationServer* server, RadioModel* radioModel,
        DaemonMediaController* mediaController, QObject* parent = nullptr,
        MonotonicClock clock = {},
        AudioDiagnosticsProvider audioDiagnosticsProvider = {});
    ~DaemonTelemetryController() override;

    bool isCollecting() const noexcept { return m_epoch != 0; }

    /// Runs the same bounded sample path as the 1 Hz timer. Public so tests
    /// and explicit host loops can sample without sleeping.
    void sampleNow();

#ifdef NEREUS_BUILD_TESTS
    void disableAutomaticSamplingForTest()
    {
        m_automaticSamplingEnabled = false;
        m_timer.stop();
    }
#endif

signals:
    void radioTelemetryRequested(quint64 requestId);

private slots:
    void onSessionStarted(quint64 epoch);
    void onSessionEnded(quint64 epoch);
    void onRadioConnectionStateChanged(ConnectionState state);
    void onRadioObservation(quint64 requestId, double rxMbps, double txMbps,
                            bool hasRtt, qint64 rttMs, qint64 rttAgeMs);

private:
    struct RadioObservation {
        double rxMbps = 0.0;
        double txMbps = 0.0;
        std::optional<qint64> rttMs;
        std::optional<qint64> rttAgeMs;
        qint64 requestedElapsedMs = 0;
    };

    struct AudioBaseline {
        quint32 contextGeneration = 0;
        qint64 sampledElapsedMs = 0;
        std::uint64_t sourceFrames = 0;
        std::uint64_t sourceDrops = 0;
        std::uint64_t encodedPackets = 0;
        std::uint64_t encodeFailures = 0;
        std::uint64_t sendAccepted = 0;
        std::uint64_t sendRejected = 0;
    };

    qint64 clockNowMs() const;
    qint64 sessionElapsedMs() const;
    void synchronizeRadioConnection();
    void retireRadioConnection();
    void requestRadioObservation();
    void applyRadioObservation(StationTelemetrySnapshot& snapshot,
                               qint64 sampledElapsedMs) const;
    void applyAudioObservation(StationTelemetrySnapshot& snapshot,
                               qint64 sampledElapsedMs);
    void stopCollecting();

    QPointer<StationServer> m_server;
    QPointer<RadioModel> m_radioModel;
    QPointer<DaemonMediaController> m_mediaController;
    QPointer<RadioConnection> m_radioConnection;
    QPointer<RadioConnection> m_requestedConnection;
    QMetaObject::Connection m_radioRequestConnection;
    QMetaObject::Connection m_radioReplyConnection;
    QTimer m_timer;
    QElapsedTimer m_processClock;
    MonotonicClock m_clock;
    AudioDiagnosticsProvider m_audioDiagnosticsProvider;
    std::optional<RadioObservation> m_radioObservation;
    std::optional<AudioBaseline> m_audioBaseline;
    quint64 m_epoch{0};
    quint64 m_nextRadioRequestId{0};
    quint64 m_outstandingRadioRequestId{0};
    qint64 m_sessionStartedMs{0};
    qint64 m_radioRequestElapsedMs{0};
    quint32 m_sequence{0};
    bool m_automaticSamplingEnabled{true};
};

} // namespace NereusSDR
