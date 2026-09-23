// no-port-check: NereusSDR-original. See DaemonTelemetryController.h.

#include "core/daemon/DaemonTelemetryController.h"

#include "core/RadioConnection.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

#include <algorithm>
#include <utility>

namespace NereusSDR {
namespace {

std::optional<double> rate(std::uint64_t current, std::uint64_t previous,
                           qint64 elapsedMs)
{
    if (elapsedMs <= 0 || current < previous) {
        return std::nullopt;
    }
    return static_cast<double>(current - previous) * 1000.0
        / static_cast<double>(elapsedMs);
}

} // namespace

DaemonTelemetryController::DaemonTelemetryController(
    StationServer* server, RadioModel* radioModel,
    DaemonMediaController* mediaController, QObject* parent,
    MonotonicClock clock, AudioDiagnosticsProvider audioDiagnosticsProvider,
    std::unique_ptr<HostTelemetrySampler> hostSampler)
    : QObject(parent)
    , m_server(server)
    , m_radioModel(radioModel)
    , m_mediaController(mediaController)
    , m_clock(std::move(clock))
    , m_audioDiagnosticsProvider(std::move(audioDiagnosticsProvider))
    , m_hostSampler(std::move(hostSampler))
{
    if (!m_hostSampler) {
        m_hostSampler = std::make_unique<HostTelemetrySampler>();
    }
    m_processClock.start();
    if (!m_clock) {
        m_clock = [this] { return m_processClock.elapsed(); };
    }
    if (!m_audioDiagnosticsProvider) {
        m_audioDiagnosticsProvider = [this] {
            return m_mediaController ? m_mediaController->audioDiagnostics()
                                     : DaemonAudioDiagnostics{};
        };
    }

    m_timer.setInterval(kSamplePeriodMs);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this,
            &DaemonTelemetryController::sampleNow);
    if (m_server) {
        connect(m_server, &StationServer::telemetrySessionStarted, this,
                &DaemonTelemetryController::onSessionStarted);
        connect(m_server, &StationServer::telemetrySessionEnded, this,
                &DaemonTelemetryController::onSessionEnded);
    }
    if (m_radioModel) {
        connect(m_radioModel, &RadioModel::connectionStateChanged, this,
                &DaemonTelemetryController::onRadioConnectionStateChanged);
    }
}

DaemonTelemetryController::~DaemonTelemetryController()
{
    stopCollecting();
    retireRadioConnection();
}

qint64 DaemonTelemetryController::clockNowMs() const
{
    return std::max<qint64>(0, m_clock ? m_clock() : 0);
}

qint64 DaemonTelemetryController::sessionElapsedMs() const
{
    return std::max<qint64>(0, clockNowMs() - m_sessionStartedMs);
}

void DaemonTelemetryController::onSessionStarted(quint64 epoch)
{
    stopCollecting();
    if (epoch == 0 || !m_server || !m_server->telemetryAvailable()) {
        return;
    }

    m_epoch = epoch;
    m_sessionStartedMs = clockNowMs();
    m_sequence = 0;
    m_audioBaseline.reset();
    m_radioObservation.reset();
    m_hostSampler->reset();
    synchronizeRadioConnection();
    requestRadioObservation();
    if (m_automaticSamplingEnabled) {
        m_timer.start();
    }
}

void DaemonTelemetryController::onSessionEnded(quint64 epoch)
{
    if (epoch == m_epoch) {
        stopCollecting();
    }
}

void DaemonTelemetryController::stopCollecting()
{
    m_timer.stop();
    m_epoch = 0;
    m_sequence = 0;
    m_audioBaseline.reset();
    m_radioObservation.reset();
    m_outstandingRadioRequestId = 0;
    m_requestedConnection.clear();
}

void DaemonTelemetryController::onRadioConnectionStateChanged(ConnectionState)
{
    synchronizeRadioConnection();
    if (m_epoch != 0) {
        requestRadioObservation();
    }
}

void DaemonTelemetryController::synchronizeRadioConnection()
{
    RadioConnection* next = nullptr;
    if (m_radioModel && m_radioModel->isConnected()) {
        next = m_radioModel->connection();
    }
    if (next == m_radioConnection) {
        return;
    }

    retireRadioConnection();
    m_radioConnection = next;
    if (!m_radioConnection) {
        return;
    }

    m_radioRequestConnection = connect(
        this, &DaemonTelemetryController::radioTelemetryRequested,
        m_radioConnection, &RadioConnection::collectTelemetryObservation,
        Qt::QueuedConnection);
    m_radioReplyConnection = connect(
        m_radioConnection, &RadioConnection::telemetryObservationReady,
        this, &DaemonTelemetryController::onRadioObservation,
        Qt::QueuedConnection);
}

void DaemonTelemetryController::retireRadioConnection()
{
    QObject::disconnect(m_radioRequestConnection);
    QObject::disconnect(m_radioReplyConnection);
    m_radioRequestConnection = {};
    m_radioReplyConnection = {};
    m_radioConnection.clear();
    m_requestedConnection.clear();
    m_outstandingRadioRequestId = 0;
    m_radioObservation.reset();
}

void DaemonTelemetryController::requestRadioObservation()
{
    synchronizeRadioConnection();
    if (m_epoch == 0 || !m_radioConnection
        || m_outstandingRadioRequestId != 0) {
        return;
    }

    ++m_nextRadioRequestId;
    if (m_nextRadioRequestId == 0) {
        ++m_nextRadioRequestId;
    }
    m_outstandingRadioRequestId = m_nextRadioRequestId;
    m_requestedConnection = m_radioConnection;
    m_radioRequestElapsedMs = sessionElapsedMs();
    emit radioTelemetryRequested(m_outstandingRadioRequestId);
}

void DaemonTelemetryController::onRadioObservation(
    quint64 requestId, double rxMbps, double txMbps, bool hasRtt,
    qint64 rttMs, qint64 rttAgeMs)
{
    if (m_epoch == 0 || requestId == 0
        || requestId != m_outstandingRadioRequestId
        || !m_requestedConnection
        || m_requestedConnection != m_radioConnection
        || sender() != m_requestedConnection.data()) {
        return;
    }

    RadioObservation observation;
    observation.rxMbps = rxMbps;
    observation.txMbps = txMbps;
    observation.requestedElapsedMs = m_radioRequestElapsedMs;
    if (hasRtt && rttMs >= 0 && rttAgeMs >= 0) {
        observation.rttMs = rttMs;
        observation.rttAgeMs = rttAgeMs;
    }
    m_radioObservation = observation;
    m_outstandingRadioRequestId = 0;
    m_requestedConnection.clear();
}

void DaemonTelemetryController::applyRadioObservation(
    StationTelemetrySnapshot& snapshot, qint64 sampledElapsedMs) const
{
    snapshot.radio.connected = m_radioModel && m_radioModel->isConnected()
        && m_radioConnection;
    if (!snapshot.radio.connected || !m_radioObservation) {
        return;
    }

    const qint64 observationAgeMs = std::max<qint64>(
        0, sampledElapsedMs - m_radioObservation->requestedElapsedMs);
    if (observationAgeMs > kSamplePeriodMs * kObservationStalePeriods) {
        return;
    }

    snapshot.radio.rxMbps = m_radioObservation->rxMbps;
    snapshot.radio.txMbps = m_radioObservation->txMbps;
    if (m_radioObservation->rttMs && m_radioObservation->rttAgeMs) {
        snapshot.radio.rttMs = m_radioObservation->rttMs;
        snapshot.radio.rttAgeMs = *m_radioObservation->rttAgeMs
            + observationAgeMs;
    }
}

void DaemonTelemetryController::applyAudioObservation(
    StationTelemetrySnapshot& snapshot, qint64 sampledElapsedMs)
{
    const DaemonAudioDiagnostics diagnostics = m_audioDiagnosticsProvider
        ? m_audioDiagnosticsProvider() : DaemonAudioDiagnostics{};
    snapshot.audio.active = diagnostics.activeContext;
    snapshot.audio.contextGeneration = diagnostics.contextGeneration;
    if (!diagnostics.activeContext) {
        m_audioBaseline.reset();
        return;
    }

    const AudioBaseline current{
        diagnostics.contextGeneration,
        sampledElapsedMs,
        diagnostics.sender.source.capturedValidRateFrames,
        diagnostics.sender.source.sourceDropEvents,
        diagnostics.sender.encodedPackets,
        diagnostics.sender.encodeFailures,
        diagnostics.sendAccepted,
        diagnostics.sendRejected,
    };

    if (m_audioBaseline
        && m_audioBaseline->contextGeneration == current.contextGeneration) {
        const qint64 elapsedMs = current.sampledElapsedMs
            - m_audioBaseline->sampledElapsedMs;
        const std::optional<double> sourceFrames = rate(
            current.sourceFrames, m_audioBaseline->sourceFrames, elapsedMs);
        const std::optional<double> sourceDrops = rate(
            current.sourceDrops, m_audioBaseline->sourceDrops, elapsedMs);
        const std::optional<double> encodedPackets = rate(
            current.encodedPackets, m_audioBaseline->encodedPackets, elapsedMs);
        const std::optional<double> encodeFailures = rate(
            current.encodeFailures, m_audioBaseline->encodeFailures, elapsedMs);
        const std::optional<double> sendAccepted = rate(
            current.sendAccepted, m_audioBaseline->sendAccepted, elapsedMs);
        const std::optional<double> sendRejected = rate(
            current.sendRejected, m_audioBaseline->sendRejected, elapsedMs);

        // A reset in any counter retires the whole baseline. Mixing rates
        // from different effective lifetimes would make one snapshot
        // internally inconsistent.
        if (sourceFrames && sourceDrops && encodedPackets && encodeFailures
            && sendAccepted && sendRejected) {
            snapshot.audio.sourceFramesPerSecond = sourceFrames;
            snapshot.audio.sourceDropsPerSecond = sourceDrops;
            snapshot.audio.encodedPacketsPerSecond = encodedPackets;
            snapshot.audio.encodeFailuresPerSecond = encodeFailures;
            snapshot.audio.sendAcceptedPerSecond = sendAccepted;
            snapshot.audio.sendRejectedPerSecond = sendRejected;
        }
    }
    m_audioBaseline = current;
}

void DaemonTelemetryController::sampleNow()
{
    if (m_epoch == 0 || !m_server) {
        return;
    }

    synchronizeRadioConnection();
    const qint64 sampledElapsedMs = sessionElapsedMs();
    StationTelemetrySnapshot snapshot;
    ++m_sequence;
    if (m_sequence == 0) {
        ++m_sequence;
    }
    snapshot.sequence = m_sequence;
    snapshot.sampledElapsedMs = sampledElapsedMs;
    applyRadioObservation(snapshot, sampledElapsedMs);
    applyAudioObservation(snapshot, sampledElapsedMs);
    snapshot.host = m_hostSampler->sample();
    m_server->sendTelemetry(snapshot, m_epoch);
    requestRadioObservation();
}

} // namespace NereusSDR
