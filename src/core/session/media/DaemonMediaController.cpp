// =================================================================
// src/core/session/media/DaemonMediaController.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DaemonMediaController.h.
// =================================================================

#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/RemoteSpectrumContext.h"

#include "core/FFTEngine.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonAudioSender.h"
#include "core/session/media/MediaPeer.h"
#include "models/RadioModel.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/Ps3DisplayCodec.h"
#include "models/SliceModel.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QLoggingCategory>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NereusSDR {
Q_LOGGING_CATEGORY(lcDaemonMedia, "nereus.daemon.media")
namespace {

constexpr int kMaxEndpoints = 8;
constexpr int kRecentAllocationRecords = 64;
constexpr int kMaxKeyframesPerSecond = 5;
constexpr qint64 kNoiseFloorMinimumIntervalNs = 500'000'000;
constexpr float kNoiseFloorMinimumDbm = -400.0f;
constexpr float kNoiseFloorMaximumDbm = 100.0f;
constexpr float kFftDbmFloor = -200.0f;
constexpr float kFftPowerFloor = 1.0e-20f;
constexpr qint64 kAudioDiagnosticsLogIntervalMs = 2'000;

bool exactKeys(const QJsonObject& object, std::initializer_list<const char*> keys)
{
    if (object.size() != static_cast<qsizetype>(keys.size())) {
        return false;
    }
    for (const char* key : keys) {
        if (!object.contains(QLatin1String(key))) {
            return false;
        }
    }
    return true;
}

bool canonicalConnectionId(const QJsonValue& value)
{
    if (!value.isString()) {
        return false;
    }
    const QString id = value.toString();
    const QUuid uuid = QUuid::fromString(id);
    return !uuid.isNull() && uuid.toString(QUuid::WithoutBraces) == id;
}

bool finiteNumber(const QJsonValue& value, double& result)
{
    if (!value.isDouble()) {
        return false;
    }
    result = value.toDouble();
    return std::isfinite(result);
}

bool exactUnsigned(const QJsonValue& value, quint32& result, bool nonzero = false)
{
    double number = 0.0;
    if (!finiteNumber(value, number) || number < 0.0
        || number > static_cast<double>(std::numeric_limits<quint32>::max())
        || std::floor(number) != number) {
        return false;
    }
    result = static_cast<quint32>(number);
    return !nonzero || result != 0;
}

bool exactInt(const QJsonValue& value, int minimum, int maximum, int& result)
{
    double number = 0.0;
    if (!finiteNumber(value, number) || number < minimum || number > maximum
        || std::floor(number) != number) {
        return false;
    }
    result = static_cast<int>(number);
    return true;
}

bool parseTier(const QJsonValue& value, FftTier& tier)
{
    if (!value.isString()) {
        return false;
    }
    if (value.toString() == QLatin1String("wide")) {
        tier = FftTier::Wide;
        return true;
    }
    if (value.toString() == QLatin1String("fine")) {
        tier = FftTier::Fine;
        return true;
    }
    return false;
}

bool parseDetector(const QJsonValue& value, SpectrumDetectorMode& detector)
{
    int integer = 0;
    if (!exactInt(value, static_cast<int>(SpectrumDetectorMode::Peak),
                  static_cast<int>(SpectrumDetectorMode::RMS), integer)) {
        return false;
    }
    detector = static_cast<SpectrumDetectorMode>(integer);
    return true;
}

bool parsePlane(const QJsonValue& value, SpectrumPlaneRequest& plane)
{
    if (!value.isObject()) {
        return false;
    }
    const QJsonObject object = value.toObject();
    if (!exactKeys(object, {"detector", "averageMode", "averageAlpha"})
        || !parseDetector(object.value(QStringLiteral("detector")), plane.detector)
        || !exactInt(object.value(QStringLiteral("averageMode")), -1, 3, plane.averageMode)
        || !finiteNumber(object.value(QStringLiteral("averageAlpha")), plane.averageAlpha)
        || plane.averageAlpha < 0.0 || plane.averageAlpha > 1.0) {
        return false;
    }
    return true;
}

bool sameSourceConfig(const DaemonSpectrumSourceConfig& left,
                      const DaemonSpectrumSourceConfig& right)
{
    return left.fft.fps == right.fft.fps
        && left.fft.fftSize == right.fft.fftSize
        && left.fft.windowType == right.fft.windowType
        && left.fft.hzPerBinTarget == right.fft.hzPerBinTarget
        && left.centreHz == right.centreHz
        && left.sampleRateHz == right.sampleRateHz
        && left.maxPendingIqFloats == right.maxPendingIqFloats;
}

bool supportedFftSize(int size)
{
    FFTEngine validator(-1);
    validator.setFftSize(size);
    return validator.fftSize() == size;
}

bool powerOfTwoFftSize(int size)
{
    return size >= 1024 && (size & (size - 1)) == 0;
}

// Names the root cause of a reduced grant. An FFT limit outranks the pixel
// rule because a smaller engine is what leaves fewer visible bins.
SpectrumLimitReason grantReason(const SpectrumGrant& grant)
{
    const int largest = FFTEngine::maximumFftSize();
    const int clamped = std::min(grant.requestedFftSize, largest);
    if (grant.grantedFftSize < clamped) {
        return SpectrumLimitReason::SharedEngine;
    }
    if (grant.requestedFftSize > largest && grant.grantedFftSize < grant.requestedFftSize) {
        return SpectrumLimitReason::LargestSize;
    }
    if (grant.grantedPixels < grant.requestedPixels) {
        return SpectrumLimitReason::SourceBins;
    }
    return SpectrumLimitReason::None;
}

bool staleOrEqualRevision(quint32 candidate, quint32 accepted)
{
    const quint32 difference = candidate - accepted;
    return difference == 0 || difference >= 0x80000000U;
}

bool validRequestedFrequencyRange(double centreHz, double spanHz, double wideSpanFactor)
{
    if (!std::isfinite(centreHz) || !std::isfinite(spanHz) || spanHz <= 0.0
        || !std::isfinite(wideSpanFactor)
        || (wideSpanFactor != 0.0 && wideSpanFactor <= 1.0)) {
        return false;
    }
    const double halfSpan = spanHz * 0.5;
    return std::isfinite(halfSpan) && std::isfinite(centreHz - halfSpan)
        && std::isfinite(centreHz + halfSpan)
        && (wideSpanFactor == 0.0 || std::isfinite(wideSpanFactor * spanHz));
}

bool requestOverlapsSource(const SpectrumEndpointRequest& request,
                           double sourceCentreHz, double sourceSampleRateHz)
{
    if (!std::isfinite(sourceCentreHz) || !std::isfinite(sourceSampleRateHz)
        || sourceSampleRateHz <= 0.0) {
        return false;
    }
    const double sourceHalfRate = sourceSampleRateHz * 0.5;
    const double requestHalfSpan = request.spanHz * 0.5;
    if (!std::isfinite(sourceHalfRate) || !std::isfinite(requestHalfSpan)) {
        return false;
    }
    const double sourceLow = sourceCentreHz - sourceHalfRate;
    const double sourceHigh = sourceCentreHz + sourceHalfRate;
    const double requestLow = request.centreHz - requestHalfSpan;
    const double requestHigh = request.centreHz + requestHalfSpan;
    return std::isfinite(sourceLow) && std::isfinite(sourceHigh)
        && std::isfinite(requestLow) && std::isfinite(requestHigh)
        && requestHigh > sourceLow && requestLow < sourceHigh;
}

bool needsWideband(const SpectrumEndpointRequest& request, double centreHz, double rateHz)
{
    return request.spanHz > rateHz
        || request.centreHz - request.spanHz * 0.5 < centreHz - rateHz * 0.5
        || request.centreHz + request.spanHz * 0.5 > centreHz + rateHz * 0.5;
}

// A replacement endpoint can temporarily coexist with the old one for
// rollback. Unique ownership releases exactly once when either entry retires.
struct WidebandDemandLease {
    QPointer<RadioModel> model;
    RadioModel::WidebandDemandToken token{0};
    ~WidebandDemandLease() { if (model) { model->releaseWidebandDemand(token); } }
};
} // namespace

struct DaemonMediaController::EndpointEntry {
    quint32 revision{0};
    int sliceId{-1};
    int sourceFftSize{0};
    int sourceWindowType{0};
    double sourceCentreHz{0.0};
    double sourceSampleRateHz{0.0};
    SpectrumEndpointRequest request;
    SpectrumDisplayCost displayCost;
    SpectrumGrant grant;
    AllocationRecord allocation;
    bool widebandNegotiated{false};
    bool widebandWanted{false};
    std::unique_ptr<WidebandDemandLease> widebandDemand;
    SpectrumEndpoint endpoint;
    DisplayCodecEncoder encoder;
    std::optional<DaemonSpectrumFrame> latestInput;
    double stationOffsetDb{0.0};
    bool contextSent{false};
    bool forceKeyframe{true};
    int keyframesInWindow{0};
    QElapsedTimer keyframeWindow;
    qint64 lastNoiseFloorTimestampNs{-1};
};

struct DaemonMediaController::SourceRuntime {
    DaemonSpectrumSourceConfig config;
    bool configured{false};
};

DaemonMediaController::DaemonMediaController(StationServer* server,
                                               RadioModel* radioModel,
                                               QObject* parent,
                                               MediaPeer::TransportFactory peerFactory,
                                               MonotonicClock monotonicClock)
    : QObject(parent)
    , m_server(server)
    , m_radioModel(radioModel)
    , m_source(this)
    , m_peerFactory(std::move(peerFactory))
    , m_monotonicClock(std::move(monotonicClock))
    , m_sendTimer(this)
    , m_audioDiagnosticsTimer(this)
{
    m_displayClock.start();
    m_source.setRadioModel(radioModel);
    m_sendTimer.setInterval(kDisplaySenderIntervalMs);
    connect(&m_sendTimer, &QTimer::timeout, this, &DaemonMediaController::onSendTick);
    m_audioDiagnosticsTimer.setInterval(kAudioDiagnosticsLogIntervalMs);
    connect(&m_audioDiagnosticsTimer, &QTimer::timeout, this, [this] {
        maybeLogAudioDiagnostics(false);
    });
    if (!m_server || !m_radioModel) {
        return;
    }
    connect(m_server, &StationServer::mediaSessionStarted,
            this, &DaemonMediaController::onSessionStarted);
    connect(m_server, &StationServer::mediaSessionEnded,
            this, &DaemonMediaController::onSessionEnded);
    connect(m_server, &StationServer::mediaControlReceived,
            this, &DaemonMediaController::onControl);
    connect(m_server, &StationServer::displayBudgetChanged,
            this, [this] {
        beginDisplayBudgetIfNeeded();
        refreshDisplayBudgetPacer();
    });
    connect(&m_source, &DaemonSpectrumSource::frameAvailable,
            this, &DaemonMediaController::onSourceFrame);
    connect(m_radioModel, &RadioModel::streamCentreChanged,
            this, &DaemonMediaController::onStreamGeometryChanged);
    connect(m_radioModel, &RadioModel::streamBindingsChanged,
            this, &DaemonMediaController::onStreamBindingsChanged);
    connect(m_radioModel, &RadioModel::sliceRemoved,
            this, &DaemonMediaController::onSliceRemoved);
    connect(m_radioModel, &RadioModel::connectionStateChanged,
            this, &DaemonMediaController::onRadioConnectionStateChanged);
    // Capture changes can be emitted inside endpoint lease release. Process
    // them after map replacement/removal completes, then revalidate again at
    // send time so no old source row slips through the queued notification.
    connect(m_radioModel, &RadioModel::widebandSourceChanged, this,
            &DaemonMediaController::onWidebandSourceChanged, Qt::QueuedConnection);
    connect(m_radioModel, &RadioModel::streamAdcRoutingChanged, this,
            [this]() { onWidebandSourceChanged(-1); }, Qt::QueuedConnection);
    connect(m_radioModel->pureSignalFacade(), &PureSignalSessionFacade::displayInvalidated,
            this, [this]() {
        m_ps3CurrentChunks.clear();
        m_ps3LatestChunks.clear();
        m_ps3CurrentAttempted = false;
    });
    connect(m_radioModel->pureSignalFacade(),
            &PureSignalSessionFacade::remoteAmpViewSubscriptionChanged,
            this, &DaemonMediaController::onRemoteAmpViewSubscriptionChanged);
    connect(m_radioModel->pureSignalFacade(), &PureSignalSessionFacade::displaySnapshotReady,
            this, [this](const Ps3Snapshot& snapshot) {
        if (m_epoch == 0 || !m_peer || !m_peer->isReady()
            || !m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed()) {
            return;
        }
        // One latest snapshot, including headers, remains bounded to 160 KiB.
        // The facade samples at <=10 Hz; ordinary MediaPeer sends preserve
        // the 64 KiB message limit and account every chunk in R35 telemetry.
        QList<QByteArray> encoded = Ps3DisplayCodec::encode(snapshot);
        if (encoded.isEmpty()) {
            return;
        }
        if (m_ps3CurrentChunks.isEmpty() || !m_ps3CurrentAttempted) {
            m_ps3CurrentChunks = std::move(encoded);
            m_ps3CurrentAttempted = false;
        } else {
            m_ps3LatestChunks = std::move(encoded);
        }
        if (!m_sendTimer.isActive()) {
            m_sendTimer.start();
        }
    });
    const QPointer<DaemonMediaController> self(this);
    m_server->setPs3DisplayAdmissionHandler([self](bool enabled, QString* refusal) {
        if (!self) {
            if (refusal) { *refusal = QStringLiteral("display budget authority retired"); }
            return false;
        }
        return self->admitPs3Display(enabled, refusal);
    });
    // The command gate and accepted-state notification are both installed
    // before capability publication can advertise budget enforcement.
    m_server->setDisplayBudgetEnforcementEnabled(true);
}

DaemonMediaController::~DaemonMediaController()
{
    if (m_radioModel) {
        m_radioModel->pureSignalFacade()->disconnect(this);
    }
    if (m_server) {
        m_server->disconnect(this);
        m_server->setPs3DisplayAdmissionHandler({});
        m_server->setDisplayBudgetEnforcementEnabled(false);
    }
    m_displayPacer.endSession();
    clearSession();
}

int DaemonMediaController::activeEndpointCount() const
{
    return static_cast<int>(m_endpoints.size());
}

int DaemonMediaController::activeSourceCount() const
{
    int active = 0;
    const QList<MediaSourceKey> keys = m_source.activeSources();
    for (const MediaSourceKey& key : keys) {
        if (m_source.isActive(key)) {
            ++active;
        }
    }
    return active;
}

DaemonAudioDiagnostics DaemonMediaController::audioDiagnostics() const
{
    return snapshotAudioDiagnostics();
}

std::optional<SpectrumGrant> DaemonMediaController::spectrumGrant(quint32 endpointId) const
{
    const auto it = m_endpoints.find(endpointId);
    if (it == m_endpoints.end()) {
        return std::nullopt;
    }
    return it->second.grant;
}

qint64 DaemonMediaController::displayNowNs() const
{
    return m_monotonicClock ? m_monotonicClock() : m_displayClock.nsecsElapsed();
}

bool DaemonMediaController::displayBudgetWireAvailable() const
{
    return m_server && m_server->displayBudgetAvailable();
}

bool DaemonMediaController::displayPacingRequired() const
{
    return m_server && m_server->displayBudgetLimits().has_value();
}

DisplayBudgetCharge DaemonMediaController::currentSpectrumCharge() const
{
    QList<DisplayBudgetCharge> charges;
    charges.reserve(static_cast<qsizetype>(m_endpoints.size()));
    for (const auto& [unused, entry] : m_endpoints) {
        Q_UNUSED(unused);
        charges.append(entry.displayCost.charge);
    }
    return sumDisplayCharges(charges).value_or(DisplayBudgetCharge{});
}

std::optional<DisplayBudgetCharge> DaemonMediaController::proposedSpectrumCharge(
    quint32 endpointId, const DisplayBudgetCharge& replacement) const
{
    QList<DisplayBudgetCharge> charges;
    charges.reserve(static_cast<qsizetype>(m_endpoints.size() + 1));
    bool replaced = false;
    for (const auto& [currentId, entry] : m_endpoints) {
        if (currentId == endpointId) {
            charges.append(replacement);
            replaced = true;
        } else {
            charges.append(entry.displayCost.charge);
        }
    }
    if (!replaced) {
        charges.append(replacement);
    }
    return sumDisplayCharges(charges);
}

bool DaemonMediaController::spectrumAdmissionFits(
    quint32 endpointId, const DisplayBudgetCharge& replacement) const
{
    const auto limits = m_server ? m_server->displayBudgetLimits() : std::nullopt;
    if (!limits) {
        return true;
    }
    const auto existing = m_endpoints.find(endpointId);
    if (existing != m_endpoints.end()) {
        const DisplayBudgetCharge old = existing->second.displayCost.charge;
        if (replacement.applicationBytesPerSecond <= old.applicationBytesPerSecond
            && replacement.spectrumSampleUnitsPerSecond
                <= old.spectrumSampleUnitsPerSecond
            && replacement.messagesPerSecond <= old.messagesPerSecond) {
            return true;
        }
    }
    const auto spectrum = proposedSpectrumCharge(endpointId, replacement);
    if (!spectrum) {
        return false;
    }
    const bool ps3Enabled = m_radioModel
        && m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed();
    const auto combined = sumDisplayCharges(
        {*spectrum, ps3Enabled ? ps3DisplayCharge() : DisplayBudgetCharge{}});
    return combined && displayChargeFits(*limits, *combined);
}

void DaemonMediaController::beginDisplayBudgetIfNeeded()
{
    if (m_displayPacerInitialized || m_epoch == 0 || !m_server) {
        return;
    }
    const auto limits = m_server->displayBudgetLimits();
    if (!limits) {
        return;
    }
    m_displayPacerInitialized = m_displayPacer.beginSession(
        m_epoch, *limits, displayNowNs());
}

void DaemonMediaController::refreshDisplayBudgetPacer()
{
    beginDisplayBudgetIfNeeded();
    if (!m_displayPacerInitialized || !m_server) {
        return;
    }
    const auto limits = m_server->displayBudgetLimits();
    if (!limits) {
        return;
    }
    const bool ps3Enabled = m_radioModel
        && m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed();
    if (!m_displayPacer.update(*limits, currentSpectrumCharge(), ps3Enabled,
                               displayNowNs())) {
        qCWarning(lcDaemonMedia) << "refused invalid display pacer state update";
    }
}

bool DaemonMediaController::admitPs3Display(bool enabled, QString* refusal)
{
    const bool current = m_radioModel
        && m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed();
    if (enabled == current || !enabled) {
        return true;
    }
    const auto limits = m_server ? m_server->displayBudgetLimits() : std::nullopt;
    if (!limits) {
        return true;
    }
    const auto combined = sumDisplayCharges({currentSpectrumCharge(), ps3DisplayCharge()});
    if (combined && displayChargeFits(*limits, *combined)) {
        return true;
    }
    if (refusal) {
        *refusal = QStringLiteral("PureSignal display does not fit the session display budget");
    }
    return false;
}

void DaemonMediaController::onRemoteAmpViewSubscriptionChanged(bool subscribed)
{
    if (!subscribed) {
        m_ps3CurrentChunks.clear();
        m_ps3LatestChunks.clear();
        m_ps3CurrentAttempted = false;
    }
    refreshDisplayBudgetPacer();
    if (m_server) {
        m_server->publishDisplayBudgetCapabilities();
    }
}

void DaemonMediaController::rememberNonliveOperation(
    quint32 endpointId, const AllocationRecord& record)
{
    forgetNonliveOperation(endpointId);
    m_nonliveOperations.emplace(endpointId, record);
    m_nonliveOperationOrder.append(endpointId);
    while (m_nonliveOperationOrder.size() > kRecentAllocationRecords) {
        const quint32 oldest = m_nonliveOperationOrder.takeFirst();
        m_nonliveOperations.erase(oldest);
    }
}

void DaemonMediaController::forgetNonliveOperation(quint32 endpointId)
{
    m_nonliveOperations.erase(endpointId);
    m_nonliveOperationOrder.removeAll(endpointId);
}

void DaemonMediaController::clearAllocationIdentity()
{
    m_endpointHighWater = 0;
    m_nonliveOperations.clear();
    m_nonliveOperationOrder.clear();
}

void DaemonMediaController::sendAllocationResult(
    const QString& connectionId, quint32 endpointId, quint32 revision,
    bool accepted, const QString& reason)
{
    if (connectionId.isEmpty() || !m_server) {
        return;
    }
    const auto limits = m_server->displayBudgetLimits();
    if (!limits) {
        return;
    }
    quint32 acceptedRevision = 0;
    DisplayBudgetCharge retained;
    const auto current = m_endpoints.find(endpointId);
    if (current != m_endpoints.end()) {
        acceptedRevision = current->second.revision;
        retained = current->second.displayCost.charge;
    }
    sendControl({
        {QStringLiteral("op"), QStringLiteral("allocation-result")},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
        {QStringLiteral("revision"), static_cast<qint64>(revision)},
        {QStringLiteral("accepted"), accepted},
        {QStringLiteral("reason"), reason},
        {QStringLiteral("budgetGeneration"), static_cast<qint64>(limits->generation)},
        {QStringLiteral("acceptedRevision"), static_cast<qint64>(acceptedRevision)},
        {QStringLiteral("applicationBytesPerSecond"),
         static_cast<qint64>(retained.applicationBytesPerSecond)},
        {QStringLiteral("spectrumSampleUnitsPerSecond"),
         static_cast<qint64>(retained.spectrumSampleUnitsPerSecond)},
        {QStringLiteral("messagesPerSecond"), static_cast<qint64>(retained.messagesPerSecond)},
    });
}

bool DaemonMediaController::rejectAllocation(
    const QJsonObject& control, quint32 endpointId, quint32 revision,
    const QString& reason, bool remember)
{
    const QString connectionId = control.value(QStringLiteral("connectionId")).toString();
    if (displayBudgetWireAvailable()) {
        if (remember) {
            AllocationRecord record{control, revision, false, false, reason};
            auto active = m_endpoints.find(endpointId);
            if (active != m_endpoints.end()) {
                if (!staleOrEqualRevision(revision, active->second.allocation.revision)) {
                    active->second.allocation = std::move(record);
                }
            } else {
                const auto recent = m_nonliveOperations.find(endpointId);
                if (recent == m_nonliveOperations.end()
                    || (!recent->second.explicitlyRetired
                        && !staleOrEqualRevision(revision, recent->second.revision))) {
                    rememberNonliveOperation(endpointId, record);
                }
            }
        }
        sendAllocationResult(connectionId, endpointId, revision, false, reason);
    } else {
        sendRejected(connectionId, endpointId, revision, reason);
    }
    return false;
}

void DaemonMediaController::onSessionStarted(quint64 epoch)
{
    if (epoch == 0 || epoch <= m_lastSessionEpoch) {
        return;
    }
    if (m_epoch != 0) {
        m_displayPacer.endSession();
        m_displayPacerInitialized = false;
    }
    clearSession();
    m_lastSessionEpoch = epoch;
    m_epoch = epoch;
    beginDisplayBudgetIfNeeded();
    refreshDisplayBudgetPacer();
}

void DaemonMediaController::onSessionEnded(quint64 epoch)
{
    if (epoch == m_epoch) {
        clearSession();
        m_displayPacer.endSession();
        m_displayPacerInitialized = false;
        m_epoch = 0;
    }
}

void DaemonMediaController::onRadioConnectionStateChanged(ConnectionState state)
{
    if (state != ConnectionState::Connected) {
        // Keep the accepted intent, but retire the capture bridge before any
        // display or peer lifecycle can tear down the station's audio graph.
        stopAudioCapture();
        if (m_audioRevision != 0) {
            // The client's own choice outranks the radio, as in reconcileAudio().
            sendAudioContext(false, m_audioDesiredEnabled
                                        ? RemoteAudioOffReason::RadioOffline
                                        : RemoteAudioOffReason::ClientDisabled);
        }
        clearProduction();
        return;
    }
    reconcileAudio();
}

void DaemonMediaController::onControl(const QJsonObject& control, quint64 epoch)
{
    if (!m_server || !m_server->mediaAvailable() || epoch == 0 || epoch != m_epoch
        || !control.value(QStringLiteral("op")).isString()) {
        return;
    }
    const QString op = control.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("start")) { handleStart(control); return; }
    if (op == QLatin1String("subscribe")) { handleSubscribe(control); return; }
    if (op == QLatin1String("unsubscribe")) { handleUnsubscribe(control); return; }
    if (op == QLatin1String("keyframe")) { handleKeyframe(control); return; }
    if (op == QLatin1String("audio")) { handleAudio(control); return; }
    acceptPeerControl(control);
}

bool DaemonMediaController::handleStart(const QJsonObject& control)
{
    if (!exactKeys(control, {"op", "connectionId"})
        || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))) {
        return false;
    }
    const QString connectionId = control.value(QStringLiteral("connectionId")).toString();
    if (m_peer) {
        if (m_peer->connectionId() != connectionId) {
            sendRejected(connectionId, 0, 0, QStringLiteral("media peer already active"));
        }
        return m_peer->connectionId() == connectionId;
    }
    m_peer = std::make_unique<MediaPeer>(this, m_peerFactory);
    MediaPeer* const peer = m_peer.get();
    const quint64 peerEpoch = m_epoch;
    connect(peer, &MediaPeer::controlReady, this,
            [this, peer, peerEpoch](const QJsonObject& outbound) {
        if (m_peer.get() == peer && m_epoch == peerEpoch) {
            sendControl(outbound);
        }
    });
    connect(peer, &MediaPeer::closed, this, [this, peer, peerEpoch]() {
        if (m_peer.get() == peer && m_epoch == peerEpoch) {
            clearSession();
        }
    });
    connect(peer, &MediaPeer::ready, this, [this, peer, peerEpoch]() {
        if (m_peer.get() == peer && m_epoch == peerEpoch) {
            reconcileAudio();
        }
    });
    if (!peer->start(IMediaTransport::Role::Offerer, connectionId)) {
        m_peer.reset();
        sendRejected(connectionId, 0, 0, QStringLiteral("media peer start failed"));
        return false;
    }
    return true;
}

bool DaemonMediaController::acceptPeerControl(const QJsonObject& control)
{
    if (!m_peer || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        || control.value(QStringLiteral("connectionId")).toString() != m_peer->connectionId()
        || !m_peer->acceptControl(control)) {
        return false;
    }
    return true;
}

bool DaemonMediaController::handleSubscribe(const QJsonObject& control)
{
    const bool revisioned = displayBudgetWireAvailable();
    quint32 endpointId = 0;
    quint32 revision = 0;
    const bool validIdentity = m_peer
        && canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        && control.value(QStringLiteral("connectionId")).toString() == m_peer->connectionId()
        && exactUnsigned(control.value(QStringLiteral("endpointId")), endpointId, true)
        && exactUnsigned(control.value(QStringLiteral("revision")), revision, true);
    const bool widebandNegotiated = control.contains(QStringLiteral("extendedView"));
    QJsonObject legacyShape = control;
    if (widebandNegotiated) { legacyShape.remove(QStringLiteral("extendedView")); }
    if ((widebandNegotiated && (!m_server || !m_server->remoteWidebandAvailable()
                               || !control.value(QStringLiteral("extendedView")).isBool()))
        || !exactKeys(legacyShape, {"op", "connectionId", "endpointId", "revision", "sliceId",
                             "tier", "fftSize", "windowType", "centreHz", "spanHz", "pixels",
                             "fps", "framesPerLine", "trace", "waterfall", "minDbm", "maxDbm",
                             "wideSpanFactor"})
        || !validIdentity) {
        if (revisioned && validIdentity) {
            const bool remember = m_endpoints.contains(endpointId)
                || m_nonliveOperations.contains(endpointId)
                || endpointId > m_endpointHighWater;
            m_endpointHighWater = std::max(m_endpointHighWater, endpointId);
            return rejectAllocation(control, endpointId, revision,
                                    QStringLiteral("invalid subscription"), remember);
        }
        return false;
    }

    if (revisioned) {
        auto active = m_endpoints.find(endpointId);
        if (active != m_endpoints.end()) {
            const AllocationRecord& prior = active->second.allocation;
            if (revision == prior.revision && control == prior.request) {
                const bool accepted = prior.accepted;
                sendAllocationResult(m_peer->connectionId(), endpointId, revision,
                                     accepted, prior.reason);
                return accepted;
            }
            if (staleOrEqualRevision(revision, prior.revision)) {
                return rejectAllocation(control, endpointId, revision,
                                        QStringLiteral("stale revision"), false);
            }
        } else {
            auto recent = m_nonliveOperations.find(endpointId);
            if (recent != m_nonliveOperations.end()) {
                const AllocationRecord prior = recent->second;
                if (revision == prior.revision && control == prior.request) {
                    sendAllocationResult(m_peer->connectionId(), endpointId, revision,
                                         prior.accepted, prior.reason);
                    return prior.accepted;
                }
                if (prior.explicitlyRetired
                    || staleOrEqualRevision(revision, prior.revision)) {
                    return rejectAllocation(control, endpointId, revision,
                                            prior.explicitlyRetired
                                                ? QStringLiteral("endpoint identifier retired")
                                                : QStringLiteral("stale revision"),
                                            false);
                }
            } else if (endpointId <= m_endpointHighWater) {
                return rejectAllocation(control, endpointId, revision,
                                        QStringLiteral("endpoint identifier retired"), false);
            }
        }
        m_endpointHighWater = std::max(m_endpointHighWater, endpointId);
    }

    int sliceId = -1;
    int fftSize = 0;
    int windowType = 0;
    int pixels = 0;
    int fps = 0;
    int framesPerLine = 0;
    double minDbm = 0.0;
    double maxDbm = 0.0;
    FftTier tier = FftTier::Wide;
    SpectrumEndpointRequest request;
    if (!exactInt(control.value(QStringLiteral("sliceId")), 0, std::numeric_limits<int>::max(), sliceId)
        || !parseTier(control.value(QStringLiteral("tier")), tier)
        || !exactInt(control.value(QStringLiteral("fftSize")), 1024,
                     std::numeric_limits<int>::max(), fftSize)
        || !powerOfTwoFftSize(fftSize)
        || (fftSize <= FFTEngine::maximumFftSize() && !supportedFftSize(fftSize))
        || !exactInt(control.value(QStringLiteral("windowType")),
                     static_cast<int>(WindowFunction::Rectangular),
                     static_cast<int>(WindowFunction::Count) - 1, windowType)
        || !finiteNumber(control.value(QStringLiteral("centreHz")), request.centreHz)
        || !finiteNumber(control.value(QStringLiteral("spanHz")), request.spanHz)
        || !exactInt(control.value(QStringLiteral("pixels")), 1, SpectrumEndpoint::kMaxPixels, pixels)
        || !exactInt(control.value(QStringLiteral("fps")), 1, 60, fps)
        || !exactInt(control.value(QStringLiteral("framesPerLine")), 1, kMaxFramesPerLine,
                     framesPerLine)
        || !parsePlane(control.value(QStringLiteral("trace")), request.trace)
        || !parsePlane(control.value(QStringLiteral("waterfall")), request.waterfall)
        || !finiteNumber(control.value(QStringLiteral("minDbm")), minDbm)
        || !finiteNumber(control.value(QStringLiteral("maxDbm")), maxDbm)
        || !finiteNumber(control.value(QStringLiteral("wideSpanFactor")), request.requestedWideSpanFactor)
        || minDbm < kMinDbmLimit || minDbm > kMaxDbmLimit
        || maxDbm < kMinDbmLimit || maxDbm > kMaxDbmLimit
        || maxDbm <= minDbm
        || !validRequestedFrequencyRange(request.centreHz, request.spanHz,
                                         request.requestedWideSpanFactor)) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("invalid subscription"));
    }
    SliceModel* slice = m_radioModel ? m_radioModel->sliceById(sliceId) : nullptr;
    if (!slice || slice->streamIndex() < 0 || !m_radioModel->streamActive(slice->streamIndex())) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("slice is unavailable"));
    }
    const MediaSourceKey source{slice->streamIndex(), tier};
    const double sourceCentreHz = m_radioModel->streamCentreHz(source.streamIndex);
    const double sourceSampleRateHz = m_radioModel->streamSampleRateHz(source.streamIndex);
    const double sourceHalfRate = sourceSampleRateHz * 0.5;
    if (!std::isfinite(sourceCentreHz) || !std::isfinite(sourceSampleRateHz)
        || sourceSampleRateHz <= 0.0 || !std::isfinite(sourceHalfRate)
        || !std::isfinite(sourceCentreHz - sourceHalfRate)
        || !std::isfinite(sourceCentreHz + sourceHalfRate)) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("source geometry is unavailable"));
    }
    auto existing = m_endpoints.find(endpointId);
    if (existing != m_endpoints.end()
        && staleOrEqualRevision(revision, existing->second.revision)) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("stale revision"), false);
    }
    if (existing == m_endpoints.end() && m_endpoints.size() >= kMaxEndpoints) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("endpoint limit reached"));
    }
    for (auto it = m_endpoints.cbegin(); it != m_endpoints.cend(); ++it) {
        if (it->first != endpointId && it->second.request.source == source
            && it->second.sourceWindowType != windowType) {
            return rejectAllocation(control, endpointId, revision,
                                    QStringLiteral("incompatible source window"));
        }
    }

    request.extendedView = widebandNegotiated
        && control.value(QStringLiteral("extendedView")).toBool();
    request.endpointId = endpointId;
    request.source = source;
    request.pixels = pixels;
    request.targetFps = fps;
    request.framesPerLine = framesPerLine;
    request.minDbm = static_cast<float>(minDbm);
    request.maxDbm = static_cast<float>(maxDbm);
    const auto adcRate = m_radioModel->widebandAdcRateHz(m_radioModel->sliceAdcIndex(sliceId));
    const bool extendedAllowed = request.extendedView && adcRate.has_value();
    if ((!requestOverlapsSource(request, sourceCentreHz, sourceSampleRateHz) && !extendedAllowed)
        || (extendedAllowed && request.spanHz > std::max(sourceSampleRateHz, *adcRate / 2.0))) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("requested crop is outside source coverage"));
    }

    // R-R3-01/R-R3-08: a request may size its (stream, tier) engine only
    // while it is that engine's only subscriber. Otherwise it is granted
    // the engine's current size, so no pan's spectrum changes to satisfy
    // another pan's request. Sizes above the engine limit get the largest.
    SpectrumGrant grant;
    grant.requestedFftSize = fftSize;
    grant.grantedTier = tier;
    grant.requestedPixels = pixels;
    int sharedFftSize = 0;
    for (const auto& [otherId, other] : m_endpoints) {
        if (otherId != endpointId && other.request.source == source) {
            sharedFftSize = std::max(sharedFftSize, other.sourceFftSize);
        }
    }
    grant.grantedFftSize = sharedFftSize > 0
        ? sharedFftSize : std::min(fftSize, FFTEngine::maximumFftSize());
    // The pixel grant is fixed here, where the source geometry and granted
    // FFT size are known, and the display budget charges what is granted.
    const bool extendedActive = widebandNegotiated && request.extendedView
        && extendedAllowed && needsWideband(request, sourceCentreHz, sourceSampleRateHz);
    grant.grantedPixels = SpectrumEndpoint::grantedPixels(
        request, grant.grantedFftSize, sourceCentreHz, sourceSampleRateHz, extendedActive);
    if (grant.grantedPixels <= 0) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("requested crop is outside source coverage"));
    }
    grant.reason = grantReason(grant);
    const auto displayCost = spectrumDisplayCost(
        grant.grantedPixels, fps, request.requestedWideSpanFactor > 1.0);
    if (!displayCost || !spectrumAdmissionFits(endpointId, displayCost->charge)) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("session display budget exceeded"));
    }
    // The endpoint never emits more samples than its admitted charge.
    request.pixels = grant.grantedPixels;

    EndpointEntry entry;
    entry.widebandNegotiated = widebandNegotiated;
    entry.revision = revision;
    entry.sliceId = sliceId;
    entry.sourceFftSize = grant.grantedFftSize;
    entry.sourceWindowType = windowType;
    entry.request = request;
    entry.displayCost = *displayCost;
    entry.grant = grant;
    entry.allocation = {control, revision, true, false, {}};
    if (!reconcileWidebandDemand(entry)) {
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("wideband source is unavailable"));
    }
    std::optional<EndpointEntry> replaced;
    MediaSourceKey replacedSource;
    if (existing != m_endpoints.end()) {
        replacedSource = existing->second.request.source;
        replaced.emplace(std::move(existing->second));
        m_endpoints.erase(existing);
    }
    m_endpoints.emplace(endpointId, std::move(entry));
    if (!reconcileSource(source)) {
        m_endpoints.erase(endpointId);
        releaseSourceIfUnused(source);
        if (replaced.has_value()) {
            m_endpoints.emplace(endpointId, std::move(*replaced));
            reconcileSource(replacedSource);
        }
        return rejectAllocation(control, endpointId, revision,
                                QStringLiteral("source configuration rejected"));
    }
    if (replaced.has_value() && !(replacedSource == source)) {
        releaseSourceIfUnused(replacedSource);
    }
    forgetNonliveOperation(endpointId);
    refreshDisplayBudgetPacer();
    if (revisioned) {
        sendAllocationResult(m_peer->connectionId(), endpointId, revision, true, {});
    }
    return true;
}

bool DaemonMediaController::handleUnsubscribe(const QJsonObject& control)
{
    const bool revisioned = displayBudgetWireAvailable();
    quint32 endpointId = 0;
    quint32 revision = 0;
    const bool shapeValid = revisioned
        ? exactKeys(control, {"op", "connectionId", "endpointId", "revision"})
        : exactKeys(control, {"op", "connectionId", "endpointId"});
    const bool validIdentity = m_peer
        && canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        && control.value(QStringLiteral("connectionId")).toString() == m_peer->connectionId()
        && exactUnsigned(control.value(QStringLiteral("endpointId")), endpointId, true)
        && (!revisioned
            || exactUnsigned(control.value(QStringLiteral("revision")), revision, true));
    if (!shapeValid || !validIdentity) {
        if (revisioned && validIdentity) {
            const bool remember = m_endpoints.contains(endpointId)
                || m_nonliveOperations.contains(endpointId)
                || endpointId > m_endpointHighWater;
            m_endpointHighWater = std::max(m_endpointHighWater, endpointId);
            return rejectAllocation(control, endpointId, revision,
                                    QStringLiteral("invalid unsubscription"), remember);
        }
        return false;
    }
    if (!revisioned) {
        removeEndpoint(endpointId);
        return true;
    }

    auto active = m_endpoints.find(endpointId);
    if (active != m_endpoints.end()) {
        const AllocationRecord& prior = active->second.allocation;
        if (revision == prior.revision && control == prior.request) {
            const bool accepted = prior.accepted;
            sendAllocationResult(m_peer->connectionId(), endpointId, revision,
                                 accepted, prior.reason);
            return accepted;
        }
        if (staleOrEqualRevision(revision, prior.revision)) {
            return rejectAllocation(control, endpointId, revision,
                                    QStringLiteral("stale revision"), false);
        }
    } else {
        auto recent = m_nonliveOperations.find(endpointId);
        if (recent != m_nonliveOperations.end()) {
            const AllocationRecord prior = recent->second;
            if (revision == prior.revision && control == prior.request) {
                sendAllocationResult(m_peer->connectionId(), endpointId, revision,
                                     prior.accepted, prior.reason);
                return prior.accepted;
            }
            if (prior.explicitlyRetired || staleOrEqualRevision(revision, prior.revision)) {
                return rejectAllocation(control, endpointId, revision,
                                        prior.explicitlyRetired
                                            ? QStringLiteral("endpoint identifier retired")
                                            : QStringLiteral("stale revision"),
                                        false);
            }
        } else if (endpointId <= m_endpointHighWater) {
            return rejectAllocation(control, endpointId, revision,
                                    QStringLiteral("endpoint identifier retired"), false);
        }
    }
    m_endpointHighWater = std::max(m_endpointHighWater, endpointId);
    removeEndpoint(endpointId, false);
    rememberNonliveOperation(endpointId,
        AllocationRecord{control, revision, true, true, {}});
    sendAllocationResult(m_peer->connectionId(), endpointId, revision, true, {});
    return true;
}

bool DaemonMediaController::handleKeyframe(const QJsonObject& control)
{
    quint32 endpointId = 0;
    quint32 contextGeneration = 0;
    if (!exactKeys(control, {"op", "connectionId", "endpointId", "contextGeneration"}) || !m_peer
        || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        || control.value(QStringLiteral("connectionId")).toString() != m_peer->connectionId()
        || !exactUnsigned(control.value(QStringLiteral("endpointId")), endpointId, true)
        || !exactUnsigned(control.value(QStringLiteral("contextGeneration")), contextGeneration, true)) {
        return false;
    }
    auto it = m_endpoints.find(endpointId);
    if (it == m_endpoints.end() || !it->second.endpoint.configured()
        || it->second.endpoint.context().codec.contextGeneration != contextGeneration) {
        return false;
    }
    if (!it->second.keyframeWindow.isValid() || it->second.keyframeWindow.elapsed() >= 1000) {
        it->second.keyframeWindow.start();
        it->second.keyframesInWindow = 0;
    }
    if (it->second.keyframesInWindow >= kMaxKeyframesPerSecond) {
        return false;
    }
    ++it->second.keyframesInWindow;
    it->second.forceKeyframe = true;
    return true;
}

bool DaemonMediaController::handleAudio(const QJsonObject& control)
{
    quint32 revision = 0;
    if (!exactKeys(control, {"op", "connectionId", "revision", "enabled"})
        || !m_peer
        || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        || control.value(QStringLiteral("connectionId")).toString() != m_peer->connectionId()
        || !exactUnsigned(control.value(QStringLiteral("revision")), revision, true)
        || !control.value(QStringLiteral("enabled")).isBool()
        || (m_audioRevision != 0 && staleOrEqualRevision(revision, m_audioRevision))) {
        return false;
    }

    m_audioRevision = revision;
    m_audioDesiredEnabled = control.value(QStringLiteral("enabled")).toBool();
    // An accepted control is a fresh audio context even if it leaves actual
    // capture unavailable pending peer readiness or station reconnect.
    reconcileAudio();
    return true;
}

void DaemonMediaController::onSourceFrame(MediaSourceKey key)
{
    const std::optional<DaemonSpectrumFrame> sourceFrame = m_source.takeLatest(key);
    if (!sourceFrame.has_value() || !m_radioModel) {
        return;
    }

    // The source-worker frame is in raw FFT dBFS plus window compensation.
    // This controller and RadioModel share the station thread, so sample the
    // authoritative meter calibration here before either reducer quantizes it.
    // Reading it per frame keeps remote planes current across station preamp
    // and step-attenuator changes without reading RadioModel from the worker.
    const double stationOffsetDb = m_radioModel->rxMeterOffsetDb();
    if (!std::isfinite(stationOffsetDb)
        || !std::isfinite(sourceFrame->dbmOffset + stationOffsetDb)) {
        return;
    }
    const DaemonSpectrumFrame& frame = *sourceFrame;
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    const QList<quint32> ids = endpointIds();

    for (quint32 endpointId : ids) {
        auto it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end()) { continue; }
        EndpointEntry& entry = it->second;
        if (!(entry.request.source == key)) {
            continue;
        }
        entry.stationOffsetDb = stationOffsetDb;
        entry.latestInput = frame;
        const auto wideband = widebandContext(entry);
        if (!wideband) {
            entry.contextSent = false;
            continue; // Await capture enable acknowledgement, never ADC data.
        }
        if (!entry.endpoint.configured()
            || entry.endpoint.context().sourceGeneration != frame.generation
            || entry.endpoint.context().codec.contextGeneration == 0
            || entry.endpoint.context().wideband != *wideband) {
            const QPointer<DaemonMediaController> self(this);
            configureEndpointFromFrame(entry, frame);
            if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
        }
    }

    bool needsNoiseFloor = false;
    for (const auto& [unused, entry] : m_endpoints) {
        Q_UNUSED(unused);
        if (entry.request.source == key && entry.contextSent && entry.endpoint.configured()
            && entry.endpoint.context().sourceGeneration == frame.generation
            && (entry.lastNoiseFloorTimestampNs < 0
                || frame.producedAtNs - entry.lastNoiseFloorTimestampNs
                    >= kNoiseFloorMinimumIntervalNs)) {
            needsNoiseFloor = true;
            break;
        }
    }
    if (needsNoiseFloor) {
        const std::optional<float> floorDbm = fullSourceNoiseFloor(*sourceFrame, stationOffsetDb);
        if (floorDbm.has_value() && m_peer) {
            for (quint32 endpointId : ids) {
                auto it = m_endpoints.find(endpointId);
                if (it == m_endpoints.end()) { continue; }
                EndpointEntry& entry = it->second;
                if (entry.request.source != key || !entry.contextSent || !entry.endpoint.configured()
                    || entry.endpoint.context().sourceGeneration != frame.generation
                    || (entry.lastNoiseFloorTimestampNs >= 0
                        && frame.producedAtNs - entry.lastNoiseFloorTimestampNs
                            < kNoiseFloorMinimumIntervalNs)) {
                    continue;
                }
                const QJsonObject message{
                    {QStringLiteral("op"), QStringLiteral("noise-floor")},
                    {QStringLiteral("connectionId"), m_peer->connectionId()},
                    {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
                    {QStringLiteral("revision"), static_cast<qint64>(entry.revision)},
                    {QStringLiteral("contextGeneration"), static_cast<qint64>(
                        entry.endpoint.context().codec.contextGeneration)},
                    {QStringLiteral("floorDbm"), *floorDbm},
                };
                const quint32 revision = entry.revision;
                const quint32 generation = entry.endpoint.context().codec.contextGeneration;
                const QPointer<DaemonMediaController> self(this);
                const bool sent = sendControl(message);
                if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
                it = m_endpoints.find(endpointId);
                if (sent && it != m_endpoints.end() && it->second.revision == revision
                    && it->second.endpoint.configured()
                    && it->second.endpoint.context().codec.contextGeneration == generation) {
                    it->second.lastNoiseFloorTimestampNs = frame.producedAtNs;
                }
            }
        }
    }

    for (auto& [unused, entry] : m_endpoints) {
        Q_UNUSED(unused);
        if (entry.request.source == key && entry.endpoint.configured() && entry.contextSent
            && entry.endpoint.context().sourceGeneration == frame.generation) {
            entry.latestInput = frame;
        }
    }
}

void DaemonMediaController::configureEndpointFromFrame(EndpointEntry& entry,
                                                        const DaemonSpectrumFrame& frame)
{
    SpectrumEndpointSourceContext sourceContext;
    sourceContext.source = frame.source;
    sourceContext.sourceGeneration = frame.generation;
    sourceContext.fftBins = frame.binsLinear.size();
    sourceContext.centreHz = frame.centreHz;
    sourceContext.sampleRateHz = frame.sampleRateHz;
    const auto wideband = widebandContext(entry);
    if (!wideband) { entry.contextSent = false; return; }
    sourceContext.wideband = *wideband;
    sourceContext.contextGeneration = nextContextGeneration();
    if (!entry.endpoint.configure(entry.request, sourceContext)) {
        return;
    }
    // The frame carries the actual engine size and geometry; record what
    // this context really delivers.
    entry.grant.grantedFftSize = sourceContext.fftBins;
    entry.grant.grantedPixels = entry.endpoint.context().codec.traceSamples;
    entry.grant.reason = grantReason(entry.grant);
    entry.encoder.reset();
    entry.sourceCentreHz = frame.centreHz;
    entry.sourceSampleRateHz = frame.sampleRateHz;
    entry.latestInput = frame;
    entry.forceKeyframe = true;
    entry.contextSent = false;
    entry.lastNoiseFloorTimestampNs = -1;
    sendContext(entry);
}

std::optional<float> DaemonMediaController::fullSourceNoiseFloor(
    const DaemonSpectrumFrame& sourceFrame, double stationOffsetDb)
{
    if (!std::isfinite(sourceFrame.dbmOffset) || !std::isfinite(stationOffsetDb)
        || sourceFrame.binsLinear.isEmpty()) {
        return std::nullopt;
    }

    // Match FFTEngine::fftReady exactly: it floors tiny raw FFT powers to
    // -200 dBFS before station calibration, rather than adding the window
    // coherent-gain offset to an underflow bin. Clarity estimates the complete
    // source row before any endpoint crop, detector, averaging or quantization.
    QVector<float> binsDbm;
    binsDbm.reserve(sourceFrame.binsLinear.size());
    for (float power : sourceFrame.binsLinear) {
        if (!std::isfinite(power) || power < 0.0f) {
            return std::nullopt;
        }
        const double rawDbm = power < kFftPowerFloor
            ? static_cast<double>(kFftDbmFloor)
            : 10.0 * std::log10(static_cast<double>(power)) + sourceFrame.dbmOffset;
        const double calibratedDbm = rawDbm + stationOffsetDb;
        if (!std::isfinite(calibratedDbm)) {
            return std::nullopt;
        }
        binsDbm.append(static_cast<float>(calibratedDbm));
    }
    const float floorDbm = m_noiseFloorEstimator.estimate(binsDbm);
    if (!std::isfinite(floorDbm) || floorDbm < kNoiseFloorMinimumDbm
        || floorDbm > kNoiseFloorMaximumDbm) {
        return std::nullopt;
    }
    return floorDbm;
}

void DaemonMediaController::sendContext(EndpointEntry& entry)
{
    if (!m_peer || !entry.endpoint.configured()) {
        return;
    }
    const SpectrumEndpointContext& context = entry.endpoint.context();
    SpectrumContextMessage contextMessage;
    contextMessage.connectionId = m_peer->connectionId();
    contextMessage.endpointId = context.codec.endpointId;
    contextMessage.revision = entry.revision;
    contextMessage.contextGeneration = context.codec.contextGeneration;
    contextMessage.sourceStream = context.source.streamIndex;
    contextMessage.sourceCentreHz = entry.sourceCentreHz;
    contextMessage.sampleRateHz = entry.sourceSampleRateHz;
    contextMessage.centreHz = context.exactCentreHz;
    contextMessage.spanHz = context.exactSpanHz;
    contextMessage.wideCentreHz = context.wideCentreHz;
    contextMessage.wideSpanHz = context.wideSpanHz;
    contextMessage.traceSamples = context.codec.traceSamples;
    contextMessage.waterfallSamples = context.codec.waterfallSamples;
    contextMessage.wideSamples = context.codec.wideSamples;
    contextMessage.minDbm = context.codec.minDbm;
    contextMessage.maxDbm = context.codec.maxDbm;
    contextMessage.fps = context.targetFps;
    contextMessage.framesPerLine = context.framesPerLine;
    if (entry.widebandNegotiated) {
        contextMessage.wideband = context.wideband;
    }
    contextMessage.grant = spectrumContextGrant(entry.grant);
    // A minor-8 peer receives exactly the context it already parses.
    const QJsonObject message = encodeRemoteSpectrumContext(
        contextMessage, m_server && m_server->spectrumGrantAvailable());
    // A failed send can synchronously close the session. Copy identity before
    // crossing the transport and never retain an endpoint reference across it.
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    const quint32 endpointId = context.codec.endpointId;
    const quint32 revision = entry.revision;
    const quint32 generation = context.codec.contextGeneration;
    const QPointer<DaemonMediaController> self(this);
    const bool sent = sendControl(message);
    if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
    auto it = m_endpoints.find(endpointId);
    if (it != m_endpoints.end() && it->second.revision == revision
        && it->second.endpoint.configured()
        && it->second.endpoint.context().codec.contextGeneration == generation) {
        it->second.contextSent = sent;
    }
}

void DaemonMediaController::promoteLatestPs3Frame()
{
    m_ps3CurrentChunks = std::move(m_ps3LatestChunks);
    m_ps3LatestChunks.clear();
    m_ps3CurrentAttempted = false;
}

bool DaemonMediaController::trySendPs3(MediaPeer* peer, quint64 epoch, qint64 nowNs)
{
    if (m_ps3CurrentChunks.isEmpty()) {
        promoteLatestPs3Frame();
    }
    if (m_ps3CurrentChunks.isEmpty()) {
        return false;
    }
    const QByteArray chunk = m_ps3CurrentChunks.constFirst();
    if (chunk.isEmpty() || chunk.size() > static_cast<qsizetype>(kMaximumPs3DisplayChunkBytes)) {
        m_ps3CurrentChunks.clear();
        promoteLatestPs3Frame();
        return false;
    }
    if (displayPacingRequired()
        && (!m_displayPacerInitialized
            || !m_displayPacer.canSpendPs3(static_cast<quint64>(chunk.size()), nowNs))) {
        return false;
    }
    if (displayPacingRequired()
        && !m_displayPacer.spendPs3(static_cast<quint64>(chunk.size()), nowNs)) {
        return false;
    }
    m_ps3CurrentAttempted = true;
    m_ps3CurrentChunks.removeFirst();
    m_lastDisplayAttemptWasPs3 = true;
    const QPointer<DaemonMediaController> self(this);
    const bool sent = peer->sendDisplay(chunk);
    if (!self || m_peer.get() != peer || m_epoch != epoch) {
        return true;
    }
    if (!sent) {
        // A rejected send may already have buffered bytes. Never retry the
        // failed sequence or any of its remaining chunks.
        m_ps3CurrentChunks.clear();
    }
    if (m_ps3CurrentChunks.isEmpty()) {
        promoteLatestPs3Frame();
    }
    return true;
}

bool DaemonMediaController::trySendSpectrum(MediaPeer* peer, quint64 epoch, qint64 nowNs)
{
    if (m_endpoints.empty()) {
        return false;
    }
    const QList<quint32> ids = endpointIds();
    for (int offset = 0; offset < ids.size(); ++offset) {
        const int index = (m_roundRobinCursor + offset) % ids.size();
        const quint32 endpointId = ids.at(index);
        auto it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end()) { continue; }
        EndpointEntry& entry = it->second;
        if (!entry.latestInput.has_value() || !entry.contextSent) {
            continue;
        }
        if (displayPacingRequired()
            && (!m_displayPacerInitialized
                || !m_displayPacer.canSpendSpectrum(entry.displayCost.maximumFrameBytes,
                                                    entry.displayCost.maximumFrameSampleUnits,
                                                    nowNs))) {
            continue;
        }
        const DaemonSpectrumFrame frame = *entry.latestInput;
        const quint32 revision = entry.revision;
        const auto currentWideband = widebandContext(entry);
        if (!currentWideband) { entry.contextSent = false; continue; }
        if (*currentWideband != entry.endpoint.context().wideband) {
            const QPointer<DaemonMediaController> self(this);
            configureEndpointFromFrame(entry, frame);
            if (!self || m_peer.get() != peer || m_epoch != epoch) { return true; }
        }
        // The reliable context send may also retire/rebind this endpoint.
        it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end() || it->second.revision != revision
            || !it->second.contextSent) { continue; }
        EndpointEntry& current = it->second;
        if (displayPacingRequired()
            && (!m_displayPacerInitialized
                || !m_displayPacer.canSpendSpectrum(current.displayCost.maximumFrameBytes,
                                                    current.displayCost.maximumFrameSampleUnits,
                                                    nowNs))) {
            continue;
        }
        current.latestInput.reset();
        const auto adcFrame = currentWideband->active && m_radioModel
            ? m_radioModel->latestWidebandSpectrum(currentWideband->physicalAdcIndex)
            : std::nullopt;
        const std::optional<DisplayCodecFrame> reduced =
            current.endpoint.consume(frame, current.stationOffsetDb, adcFrame);
        m_roundRobinCursor = (index + 1) % ids.size();
        if (!reduced.has_value()) {
            continue;
        }
        const QByteArray bytes = current.encoder.encode(*reduced, current.forceKeyframe);
        const quint32 generation = current.endpoint.context().codec.contextGeneration;
        const quint64 samples = static_cast<quint64>(reduced->traceDbm.size())
            + static_cast<quint64>(reduced->waterfallDbm.size())
            + static_cast<quint64>(reduced->wideDbm.size());
        if (bytes.isEmpty()) {
            continue;
        }
        if (bytes.size() > static_cast<qsizetype>(current.displayCost.maximumFrameBytes)
            || samples > current.displayCost.maximumFrameSampleUnits
            || (displayPacingRequired()
                && !m_displayPacer.spendSpectrum(static_cast<quint64>(bytes.size()),
                                                 samples, nowNs))) {
            qCWarning(lcDaemonMedia) << "spectrum codec exceeded admitted display cost";
            current.forceKeyframe = true;
            continue;
        }
        current.forceKeyframe = false;
        m_lastDisplayAttemptWasPs3 = false;
        const QPointer<DaemonMediaController> self(this);
        const bool sent = peer->sendDisplay(bytes);
        if (!self || m_peer.get() != peer || m_epoch != epoch) { return true; }
        it = m_endpoints.find(endpointId);
        if (!sent && it != m_endpoints.end() && it->second.revision == revision
            && it->second.endpoint.configured()
            && it->second.endpoint.context().codec.contextGeneration == generation) {
            // A false return can mean the backend accepted/buffered bytes.
            // Do not resend this sequence; force a fresh keyframe later.
            it->second.forceKeyframe = true;
        }
        return true;
    }
    return false;
}

void DaemonMediaController::onSendTick()
{
    if (!m_peer || !m_peer->isReady()) {
        return;
    }
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    if (!m_radioModel || !m_radioModel->pureSignalFacade()->remoteAmpViewSubscribed()) {
        m_ps3CurrentChunks.clear();
        m_ps3LatestChunks.clear();
        m_ps3CurrentAttempted = false;
    }
    const qint64 nowNs = displayNowNs();
    if (m_lastDisplayAttemptWasPs3) {
        if (trySendSpectrum(peer, epoch, nowNs)) { return; }
        trySendPs3(peer, epoch, nowNs);
    } else {
        if (trySendPs3(peer, epoch, nowNs)) { return; }
        trySendSpectrum(peer, epoch, nowNs);
    }
}

bool DaemonMediaController::reconcileWidebandDemand(EndpointEntry& entry)
{
    if (!m_radioModel) { return false; }
    const int adc = m_radioModel->sliceAdcIndex(entry.sliceId);
    const auto rate = m_radioModel->widebandAdcRateHz(adc);
    const double centre = m_radioModel->streamCentreHz(entry.request.source.streamIndex);
    const double ddcRate = m_radioModel->streamSampleRateHz(entry.request.source.streamIndex);
    entry.widebandWanted = entry.widebandNegotiated && entry.request.extendedView
        && rate.has_value() && needsWideband(entry.request, centre, ddcRate);
    if (!entry.widebandWanted) {
        entry.widebandDemand.reset();
        return true;
    }
    if (!entry.widebandDemand) {
        const auto token = m_radioModel->acquireWidebandDemand(entry.sliceId);
        if (!token) { return false; }
        entry.widebandDemand = std::make_unique<WidebandDemandLease>();
        entry.widebandDemand->model = m_radioModel;
        entry.widebandDemand->token = token;
    }
    return m_radioModel->setWidebandDemandActive(entry.widebandDemand->token, true);
}

std::optional<WidebandDisplayContext>
DaemonMediaController::widebandContext(const EndpointEntry& entry) const
{
    WidebandDisplayContext context;
    if (!entry.widebandNegotiated || !m_radioModel) { return context; }
    const int adc = m_radioModel->sliceAdcIndex(entry.sliceId);
    const int chain = m_radioModel->sliceChainIndex(entry.sliceId);
    const auto rate = m_radioModel->widebandAdcRateHz(adc);
    if (!rate || chain < 0 || chain >= m_radioModel->boardCapabilities().rxFilterChainCount) {
        return context;
    }
    context.available = true;
    context.physicalAdcIndex = adc;
    context.filterChainIndex = chain;
    context.adcRateHz = *rate;
    if (entry.widebandWanted) {
        const auto source = m_radioModel->widebandSourceDescriptor(adc);
        if (!source) { return std::nullopt; }
        context.active = true;
        context.sourceGeneration = source->sourceGeneration;
    }
    return context.valid() ? std::optional(context) : std::nullopt;
}

void DaemonMediaController::onWidebandSourceChanged(int)
{
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    for (quint32 endpointId : endpointIds()) {
        auto it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end()) { continue; }
        EndpointEntry& entry = it->second;
        if (!entry.widebandNegotiated) { continue; }
        if (!reconcileWidebandDemand(entry)) {
            entry.contextSent = false;
            entry.latestInput.reset();
            continue;
        }
        const auto context = widebandContext(entry);
        if (!context || (entry.endpoint.configured()
                         && entry.endpoint.context().wideband != *context)) {
            entry.contextSent = false;
        }
        if (context && entry.latestInput && !entry.contextSent) {
            const auto frame = *entry.latestInput;
            const QPointer<DaemonMediaController> self(this);
            configureEndpointFromFrame(entry, frame);
            if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
        }
    }
}

void DaemonMediaController::removeEndpoint(quint32 endpointId, bool retainOperation)
{
    auto it = m_endpoints.find(endpointId);
    if (it == m_endpoints.end()) {
        return;
    }
    const MediaSourceKey key = it->second.request.source;
    if (retainOperation && displayBudgetWireAvailable()) {
        AllocationRecord retired = it->second.allocation;
        retired.accepted = false;
        retired.reason = QStringLiteral("display source retired");
        rememberNonliveOperation(endpointId, retired);
    }
    m_endpoints.erase(it);
    releaseSourceIfUnused(key);
    refreshDisplayBudgetPacer();
}

void DaemonMediaController::releaseSourceIfUnused(const MediaSourceKey& key)
{
    for (const auto& [unused, entry] : m_endpoints) {
        Q_UNUSED(unused);
        if (entry.request.source == key) {
            return;
        }
    }
    m_source.deactivate(key);
    m_sources.remove(key);
}

bool DaemonMediaController::reconcileSource(const MediaSourceKey& key)
{
    int maximumFft = 0;
    int maximumFps = 0;
    int windowType = -1;
    for (const auto& [unused, entry] : m_endpoints) {
        Q_UNUSED(unused);
        if (!(entry.request.source == key)) {
            continue;
        }
        maximumFft = std::max(maximumFft, entry.sourceFftSize);
        maximumFps = std::max(maximumFps, entry.request.targetFps);
        if (windowType == -1) {
            windowType = entry.sourceWindowType;
        }
    }
    // The wire request's FFT/window values are represented in the source
    // runtime by the controller after strict validation. They are filled by
    // subscribe before this method is called.
    SourceRuntime& runtime = m_sources[key];
    if (maximumFft == 0 || !m_radioModel || !m_radioModel->streamActive(key.streamIndex)) {
        return false;
    }
    DaemonSpectrumSourceConfig config = runtime.config;
    config.centreHz = m_radioModel->streamCentreHz(key.streamIndex);
    config.sampleRateHz = m_radioModel->streamSampleRateHz(key.streamIndex);
    config.fft.fftSize = maximumFft;
    config.fft.fps = maximumFps;
    config.fft.windowType = windowType;
    config.maxPendingIqFloats = maximumFft * 4;
    if (!std::isfinite(config.centreHz) || config.sampleRateHz <= 0.0) {
        return false;
    }
    const bool changed = !runtime.configured || !sameSourceConfig(runtime.config, config);
    const bool accepted = runtime.configured
        ? (!sameSourceConfig(runtime.config, config) ? m_source.update(key, config) : true)
        : m_source.activate(key, config);
    if (!accepted) {
        return false;
    }
    runtime.config = config;
    runtime.configured = true;
    if (changed) {
        for (auto& [unused, entry] : m_endpoints) {
            Q_UNUSED(unused);
            if (entry.request.source == key) {
                entry.endpoint.reset();
                entry.encoder.reset();
                entry.latestInput.reset();
                entry.contextSent = false;
                entry.forceKeyframe = true;
                entry.lastNoiseFloorTimestampNs = -1;
            }
        }
    }
    if (!m_sendTimer.isActive()) {
        m_sendTimer.start();
    }
    return true;
}

void DaemonMediaController::onStreamGeometryChanged(int streamIndex, double, int)
{
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    const QList<MediaSourceKey> keys = m_sources.keys();
    for (const MediaSourceKey& key : keys) {
        if (key.streamIndex != streamIndex) {
            continue;
        }
        const double centreHz = m_radioModel ? m_radioModel->streamCentreHz(streamIndex) : 0.0;
        const double sampleRateHz = m_radioModel
            ? m_radioModel->streamSampleRateHz(streamIndex) : 0.0;
        for (quint32 endpointId : endpointIds()) {
            auto it = m_endpoints.find(endpointId);
            if (it == m_endpoints.end()) { continue; }
            const EndpointEntry& entry = it->second;
            const auto adcRate = m_radioModel
                ? m_radioModel->widebandAdcRateHz(m_radioModel->sliceAdcIndex(entry.sliceId))
                : std::nullopt;
            if (entry.request.source == key
                && !requestOverlapsSource(entry.request, centreHz, sampleRateHz)
                && !(entry.request.extendedView && adcRate)) {
                const quint32 revision = displayBudgetWireAvailable()
                    ? entry.allocation.revision : entry.revision;
                removeEndpoint(endpointId);
                const QPointer<DaemonMediaController> self(this);
                sendRejected(m_peer ? m_peer->connectionId() : QString(), endpointId, revision,
                             QStringLiteral("source retune no longer covers requested crop"));
                if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
            }
        }
        if (!m_sources.contains(key)) {
            continue;
        }
        for (auto& [unused, entry] : m_endpoints) {
            Q_UNUSED(unused);
            if (entry.request.source == key && !reconcileWidebandDemand(entry)) {
                entry.contextSent = false;
                entry.latestInput.reset();
            }
        }
        if (!reconcileSource(key)) {
            // The old geometry is no longer usable. Retire its reservations
            // explicitly so a client can retry instead of believing a source
            // that no longer exists still owns a live display allocation.
            m_source.deactivate(key);
            m_sources.remove(key);
            for (quint32 endpointId : endpointIds()) {
                const auto it = m_endpoints.find(endpointId);
                if (it == m_endpoints.end() || !(it->second.request.source == key)) { continue; }
                const quint32 revision = displayBudgetWireAvailable()
                    ? it->second.allocation.revision : it->second.revision;
                removeEndpoint(endpointId);
                const QPointer<DaemonMediaController> self(this);
                sendRejected(m_peer ? m_peer->connectionId() : QString(), endpointId, revision,
                             QStringLiteral("source configuration became unavailable"));
                if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
            }
        }
    }
}

void DaemonMediaController::onStreamBindingsChanged(int streamIndex, const QVector<int>&)
{
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    for (quint32 endpointId : endpointIds()) {
        auto it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end()) { continue; }
        const EndpointEntry& entry = it->second;
        SliceModel* slice = m_radioModel ? m_radioModel->sliceById(entry.sliceId) : nullptr;
        if (entry.request.source.streamIndex == streamIndex
            && (!slice || slice->streamIndex() != streamIndex)) {
            const quint32 revision = displayBudgetWireAvailable()
                ? entry.allocation.revision : entry.revision;
            removeEndpoint(endpointId);
            const QPointer<DaemonMediaController> self(this);
            sendRejected(m_peer ? m_peer->connectionId() : QString(), endpointId, revision,
                         QStringLiteral("slice stream binding changed"));
            if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
        }
    }
}

void DaemonMediaController::onSliceRemoved(int sliceId)
{
    MediaPeer* const peer = m_peer.get();
    const quint64 epoch = m_epoch;
    for (quint32 endpointId : endpointIds()) {
        auto it = m_endpoints.find(endpointId);
        if (it == m_endpoints.end()) { continue; }
        const EndpointEntry& entry = it->second;
        if (entry.sliceId == sliceId) {
            const quint32 revision = displayBudgetWireAvailable()
                ? entry.allocation.revision : entry.revision;
            removeEndpoint(endpointId);
            const QPointer<DaemonMediaController> self(this);
            sendRejected(m_peer ? m_peer->connectionId() : QString(), endpointId, revision,
                         QStringLiteral("slice removed"));
            if (!self || m_peer.get() != peer || m_epoch != epoch) { return; }
        }
    }
}

void DaemonMediaController::reconcileAudio()
{
    // Every accepted control and every readiness transition begins by
    // flushing bounded captured PCM.  The saved next values preserve RTP
    // ordering across false/true contexts for this same MediaPeer.
    stopAudioCapture();
    // Why audio is off, first cause wins: the client's own choice, then the
    // station radio, then media readiness.
    std::optional<RemoteAudioOffReason> blockedBy;
    if (!m_audioDesiredEnabled) {
        blockedBy = RemoteAudioOffReason::ClientDisabled;
    } else if (!m_radioModel || !m_radioModel->isConnected()) {
        blockedBy = RemoteAudioOffReason::RadioOffline;
    } else if (!m_peer || !m_peer->isReady()) {
        blockedBy = RemoteAudioOffReason::MediaNotReady;
    }
    const bool shouldRun = !blockedBy.has_value();
    bool actualEnabled = false;
    if (shouldRun) {
        if (!m_audioSender) {
            m_audioSender = std::make_unique<DaemonAudioSender>(m_radioModel->audioEngine());
            DaemonAudioSender* const sender = m_audioSender.get();
            connect(sender, &DaemonAudioSender::packetReady, this,
                    [this, sender](const QByteArray& packet) {
                // packetReady is emitted by the sender's owner thread.  The
                // identity checks make a stopped/retired session unable to
                // forward a late signal to a replacement peer.
                if (m_audioSender.get() == sender && sender->isRunning()
                    && m_epoch != 0 && m_peer && m_peer->isReady()) {
                    MediaPeer* const peer = m_peer.get();
                    const quint64 epoch = m_epoch;
                    const quint32 contextGeneration = m_audioDiagnostics.contextGeneration;
                    const bool activeContext = m_audioDiagnostics.activeContext;
                    if (activeContext) {
                        ++m_audioDiagnostics.sendAttempts;
                        ++m_audioDiagnostics.sendInFlight;
                    }
                    const QPointer<DaemonMediaController> self(this);
                    const bool accepted = peer->sendRtp(packet);
                    // The transport can synchronously retire or replace this
                    // session. Resolve only into the context that initiated
                    // this send, never a replacement that appeared mid-call.
                    // Retirement finalizes any remaining in-flight attempt as
                    // unresolved, without treating it as a packet-loss event.
                    if (self && activeContext && m_audioSender.get() == sender
                        && m_epoch == epoch && m_peer.get() == peer
                        && m_audioDiagnostics.activeContext
                        && m_audioDiagnostics.contextGeneration == contextGeneration) {
                        --m_audioDiagnostics.sendInFlight;
                        if (accepted) {
                            ++m_audioDiagnostics.sendAccepted;
                        } else {
                            ++m_audioDiagnostics.sendRejected;
                        }
                        maybeLogAudioDiagnostics(false);
                    }
                }
            });
        }
        actualEnabled = m_audioSender->start(m_peer->audioSsrc(), m_audioNextSequence,
                                              m_audioNextTimestamp);
        if (actualEnabled) {
            m_audioNextSequence = m_audioSender->nextSequence();
            m_audioNextTimestamp = m_audioSender->nextTimestamp();
        }
    }
    // Nothing blocked audio, so a context that is still off means the sender
    // could not start.
    sendAudioContext(actualEnabled,
                     blockedBy.value_or(RemoteAudioOffReason::EncoderUnavailable));
}

void DaemonMediaController::stopAudioCapture()
{
    if (!m_audioSender) {
        return;
    }
    // Sender advances timestamp over every consumed block, including an
    // encode failure; retaining both fields before stop preserves the next
    // audio-context boundary across a pause or reconnect. Stop quiesces the
    // source bridge before the final diagnostics sample, so ingress/drop
    // counters include any admitted DSP callback.
    m_audioNextSequence = m_audioSender->nextSequence();
    m_audioNextTimestamp = m_audioSender->nextTimestamp();
    m_audioSender->stop();
    finalizeAudioDiagnostics();
}

void DaemonMediaController::sendAudioContext(bool enabled, RemoteAudioOffReason reason)
{
    if (!m_peer || m_audioRevision == 0) {
        return;
    }
    const bool detailNegotiated = m_server && m_server->remoteAudioStatusAvailable();
    if (enabled && detailNegotiated
        && !(m_audioSender && m_audioSender->encoderProfile())) {
        // A minor-8 GUI refuses an enabled context without its encoder
        // profile. With no profile to report, stop the sender so no RTP
        // flows under the context and say plainly why audio is off.
        stopAudioCapture();
        enabled = false;
        reason = RemoteAudioOffReason::EncoderUnavailable;
    }
    const quint32 contextGeneration = nextContextGeneration();
    if (enabled) {
        beginAudioDiagnostics(contextGeneration);
    }
    RemoteAudioContextMessage message;
    message.connectionId = m_peer->connectionId();
    message.revision = m_audioRevision;
    message.generation = contextGeneration;
    message.enabled = enabled;
    message.ssrc = m_peer->audioSsrc();
    message.firstSequence = m_audioNextSequence;
    message.firstTimestamp = m_audioNextTimestamp;
    if (enabled) {
        // A started sender always has a ready encoder, so this is the
        // profile the context's packets are coded with.
        message.encoder = m_audioSender ? m_audioSender->encoderProfile() : std::nullopt;
    } else {
        message.offReason = reason;
    }
    // A minor-7 peer gets exactly the eight keys it has always parsed.
    sendControl(encodeRemoteAudioContext(message, detailNegotiated));
}

DaemonAudioDiagnostics DaemonMediaController::snapshotAudioDiagnostics() const
{
    DaemonAudioDiagnostics snapshot = m_audioDiagnostics;
    if (snapshot.activeContext) {
        if (m_audioDiagnosticsClock.isValid()) {
            snapshot.elapsedMs = m_audioDiagnosticsClock.elapsed();
        }
        if (m_audioSender) {
            snapshot.sender = m_audioSender->telemetry();
        }
    }
    return snapshot;
}

void DaemonMediaController::beginAudioDiagnostics(quint32 contextGeneration)
{
    m_audioDiagnostics = {};
    m_audioDiagnostics.contextGeneration = contextGeneration;
    m_audioDiagnostics.revision = m_audioRevision;
    m_audioDiagnostics.activeContext = true;
    m_audioDiagnosticsClock.start();
    m_audioDiagnosticsLastLogMs = 0;
    m_audioDiagnosticsTimer.start();
}

void DaemonMediaController::finalizeAudioDiagnostics()
{
    if (!m_audioDiagnostics.activeContext) {
        return;
    }
    m_audioDiagnostics = snapshotAudioDiagnostics();
    m_audioDiagnostics.sendUnresolvedAtRetirement += m_audioDiagnostics.sendInFlight;
    m_audioDiagnostics.sendInFlight = 0;
    maybeLogAudioDiagnostics(true);
    m_audioDiagnostics.activeContext = false;
    m_audioDiagnosticsTimer.stop();
}

void DaemonMediaController::maybeLogAudioDiagnostics(bool final)
{
    if (!m_audioDiagnostics.activeContext || !m_audioDiagnosticsClock.isValid()) {
        return;
    }
    const qint64 elapsedMs = m_audioDiagnosticsClock.elapsed();
    if (!final && elapsedMs - m_audioDiagnosticsLastLogMs < kAudioDiagnosticsLogIntervalMs) {
        return;
    }
    const DaemonAudioDiagnostics snapshot = snapshotAudioDiagnostics();
    qCInfo(lcDaemonMedia).nospace()
        << "daemon audio diagnostics " << (final ? "final" : "periodic")
        << " context=" << snapshot.contextGeneration
        << " revision=" << snapshot.revision
        << " elapsedMs=" << snapshot.elapsedMs
        << " sourceFrames=" << snapshot.sender.source.capturedValidRateFrames
        << " sourceDropEvents=" << snapshot.sender.source.sourceDropEvents
        << " sourceContentionRetries=" << snapshot.sender.source.contentionRetries
        << " sourceContentionLosses=" << snapshot.sender.source.contentionLosses
        << " sourceRingFullDrops=" << snapshot.sender.source.ringFullDrops
        << " sourceInvalidIngressDrops=" << snapshot.sender.source.invalidIngressDrops
        << " consumed=" << snapshot.sender.consumedBlocks
        << " encoded=" << snapshot.sender.encodedPackets
        << " encodeFailures=" << snapshot.sender.encodeFailures
        << " sendAttempts=" << snapshot.sendAttempts
        << " sendAccepted=" << snapshot.sendAccepted
        << " sendRejected=" << snapshot.sendRejected
        << " sendInFlight=" << snapshot.sendInFlight
        << " sendUnresolvedAtRetirement=" << snapshot.sendUnresolvedAtRetirement
        << " hasLastPacket=" << snapshot.sender.hasLastEmittedPacket
        << " lastSequence=" << snapshot.sender.lastEmittedSequence
        << " lastTimestamp=" << snapshot.sender.lastEmittedTimestamp;
    m_audioDiagnosticsLastLogMs = elapsedMs;
}

void DaemonMediaController::resetAudioSession()
{
    stopAudioCapture();
    m_audioDesiredEnabled = false;
    m_audioRevision = 0;
    // A different MediaPeer has a different SSRC identity, so it may start
    // a new RTP timeline. Existing peers always retain the saved values.
    m_audioNextSequence = 1;
    m_audioNextTimestamp = 0;
}

void DaemonMediaController::clearSession()
{
    // Detach and move ownership before stop(): MediaPeer::stop() can emit
    // closed synchronously. Its callbacks are identity/epoch guarded, but
    // disconnecting them too prevents recursive clearSession(). Defer deletion
    // until its send/signal stack has unwound. QObject parent ownership still
    // reclaims a retired peer if the controller dies before deferred deletion.
    // Sender remains owned by this controller; stop its timer/capture while
    // the current peer is still identifiable, then retire the peer.
    resetAudioSession();
    MediaPeer* const peer = m_peer.release();
    if (peer) {
        peer->disconnect(this);
        peer->stop();
        peer->deleteLater();
    }
    clearProduction();
    clearAllocationIdentity();
}

QList<quint32> DaemonMediaController::endpointIds() const
{
    QList<quint32> ids;
    ids.reserve(static_cast<qsizetype>(m_endpoints.size()));
    for (const auto& [endpointId, unused] : m_endpoints) {
        Q_UNUSED(unused);
        ids.append(endpointId);
    }
    return ids;
}

void DaemonMediaController::clearProduction()
{
    m_sendTimer.stop();
    m_ps3CurrentChunks.clear();
    m_ps3LatestChunks.clear();
    m_ps3CurrentAttempted = false;
    const QList<MediaSourceKey> keys = m_sources.keys();
    for (const MediaSourceKey& key : keys) {
        m_source.deactivate(key);
    }
    m_sources.clear();
    if (displayBudgetWireAvailable()) {
        for (const auto& [endpointId, entry] : m_endpoints) {
            AllocationRecord retired = entry.allocation;
            retired.accepted = false;
            retired.reason = QStringLiteral("display production retired");
            rememberNonliveOperation(endpointId, retired);
        }
    }
    m_endpoints.clear();
    m_roundRobinCursor = 0;
    refreshDisplayBudgetPacer();
}

bool DaemonMediaController::sendControl(const QJsonObject& payload) const
{
    return m_server && m_epoch != 0 && m_server->sendMediaControl(payload, m_epoch);
}

void DaemonMediaController::sendRejected(const QString& connectionId, quint32 endpointId,
                                         quint32 revision, const QString& reason)
{
    if (connectionId.isEmpty()) {
        return;
    }
    if (endpointId != 0 && revision != 0 && displayBudgetWireAvailable()) {
        // Involuntary source retirement uses the same authoritative resource
        // result as a requested allocation. Budget-aware clients ignore the
        // legacy endpoint rejection shape and must learn the released charge.
        sendAllocationResult(connectionId, endpointId, revision, false, reason);
        return;
    }
    sendControl({
        {QStringLiteral("op"), QStringLiteral("rejected")},
        {QStringLiteral("connectionId"), connectionId},
        {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
        {QStringLiteral("revision"), static_cast<qint64>(revision)},
        {QStringLiteral("reason"), reason},
    });
}

quint32 DaemonMediaController::nextContextGeneration()
{
    ++m_nextContextGeneration;
    if (m_nextContextGeneration == 0) {
        ++m_nextContextGeneration;
    }
    return m_nextContextGeneration;
}

} // namespace NereusSDR
