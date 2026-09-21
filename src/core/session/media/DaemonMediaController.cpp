// =================================================================
// src/core/session/media/DaemonMediaController.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DaemonMediaController.h.
// =================================================================

#include "core/session/media/DaemonMediaController.h"

#include "core/FFTEngine.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonAudioSender.h"
#include "core/session/media/MediaPeer.h"
#include "models/RadioModel.h"
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
constexpr int kSenderIntervalMs = 5;
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

} // namespace

struct DaemonMediaController::EndpointEntry {
    quint32 revision{0};
    int sliceId{-1};
    int sourceFftSize{0};
    int sourceWindowType{0};
    double sourceCentreHz{0.0};
    double sourceSampleRateHz{0.0};
    SpectrumEndpointRequest request;
    SpectrumEndpoint endpoint;
    DisplayCodecEncoder encoder;
    std::optional<DaemonSpectrumFrame> latestInput;
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
                                               MediaPeer::TransportFactory peerFactory)
    : QObject(parent)
    , m_server(server)
    , m_radioModel(radioModel)
    , m_source(this)
    , m_peerFactory(std::move(peerFactory))
    , m_sendTimer(this)
    , m_audioDiagnosticsTimer(this)
{
    m_source.setRadioModel(radioModel);
    m_sendTimer.setInterval(kSenderIntervalMs);
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
}

DaemonMediaController::~DaemonMediaController()
{
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

void DaemonMediaController::onSessionStarted(quint64 epoch)
{
    clearSession();
    m_epoch = epoch;
}

void DaemonMediaController::onSessionEnded(quint64 epoch)
{
    if (epoch == m_epoch) {
        clearSession();
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
            sendAudioContext(false);
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
    if (!exactKeys(control, {"op", "connectionId", "endpointId", "revision", "sliceId",
                             "tier", "fftSize", "windowType", "centreHz", "spanHz", "pixels",
                             "fps", "framesPerLine", "trace", "waterfall", "minDbm", "maxDbm",
                             "wideSpanFactor"})
        || !m_peer || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        || control.value(QStringLiteral("connectionId")).toString() != m_peer->connectionId()) {
        return false;
    }

    quint32 endpointId = 0;
    quint32 revision = 0;
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
    if (!exactUnsigned(control.value(QStringLiteral("endpointId")), endpointId, true)
        || !exactUnsigned(control.value(QStringLiteral("revision")), revision, true)
        || !exactInt(control.value(QStringLiteral("sliceId")), 0, std::numeric_limits<int>::max(), sliceId)
        || !parseTier(control.value(QStringLiteral("tier")), tier)
        || !exactInt(control.value(QStringLiteral("fftSize")), 1024,
                     FFTEngine::maximumFftSize(), fftSize)
        || !supportedFftSize(fftSize)
        || !exactInt(control.value(QStringLiteral("windowType")),
                     static_cast<int>(WindowFunction::Rectangular),
                     static_cast<int>(WindowFunction::Count) - 1, windowType)
        || !finiteNumber(control.value(QStringLiteral("centreHz")), request.centreHz)
        || !finiteNumber(control.value(QStringLiteral("spanHz")), request.spanHz)
        || !exactInt(control.value(QStringLiteral("pixels")), 1, SpectrumEndpoint::kMaxPixels, pixels)
        || !exactInt(control.value(QStringLiteral("fps")), 1, 60, fps)
        || !exactInt(control.value(QStringLiteral("framesPerLine")), 1, 65535, framesPerLine)
        || !parsePlane(control.value(QStringLiteral("trace")), request.trace)
        || !parsePlane(control.value(QStringLiteral("waterfall")), request.waterfall)
        || !finiteNumber(control.value(QStringLiteral("minDbm")), minDbm)
        || !finiteNumber(control.value(QStringLiteral("maxDbm")), maxDbm)
        || !finiteNumber(control.value(QStringLiteral("wideSpanFactor")), request.requestedWideSpanFactor)
        || minDbm < -std::numeric_limits<float>::max()
        || minDbm > std::numeric_limits<float>::max()
        || maxDbm < -std::numeric_limits<float>::max()
        || maxDbm > std::numeric_limits<float>::max()
        || maxDbm <= minDbm
        || !std::isfinite(maxDbm - minDbm)
        || !validRequestedFrequencyRange(request.centreHz, request.spanHz,
                                         request.requestedWideSpanFactor)) {
        sendRejected(m_peer->connectionId(), endpointId, revision, QStringLiteral("invalid subscription"));
        return false;
    }
    SliceModel* slice = m_radioModel ? m_radioModel->sliceById(sliceId) : nullptr;
    if (!slice || slice->streamIndex() < 0 || !m_radioModel->streamActive(slice->streamIndex())) {
        sendRejected(m_peer->connectionId(), endpointId, revision, QStringLiteral("slice is unavailable"));
        return false;
    }
    const MediaSourceKey source{slice->streamIndex(), tier};
    const double sourceCentreHz = m_radioModel->streamCentreHz(source.streamIndex);
    const double sourceSampleRateHz = m_radioModel->streamSampleRateHz(source.streamIndex);
    const double sourceHalfRate = sourceSampleRateHz * 0.5;
    if (!std::isfinite(sourceCentreHz) || !std::isfinite(sourceSampleRateHz)
        || sourceSampleRateHz <= 0.0 || !std::isfinite(sourceHalfRate)
        || !std::isfinite(sourceCentreHz - sourceHalfRate)
        || !std::isfinite(sourceCentreHz + sourceHalfRate)) {
        sendRejected(m_peer->connectionId(), endpointId, revision,
                     QStringLiteral("source geometry is unavailable"));
        return false;
    }
    auto existing = m_endpoints.find(endpointId);
    if (existing != m_endpoints.end()
        && staleOrEqualRevision(revision, existing->second.revision)) {
        sendRejected(m_peer->connectionId(), endpointId, revision, QStringLiteral("stale revision"));
        return false;
    }
    if (existing == m_endpoints.end() && m_endpoints.size() >= kMaxEndpoints) {
        sendRejected(m_peer->connectionId(), endpointId, revision, QStringLiteral("endpoint limit reached"));
        return false;
    }
    for (auto it = m_endpoints.cbegin(); it != m_endpoints.cend(); ++it) {
        if (it->first != endpointId && it->second.request.source == source
            && it->second.sourceWindowType != windowType) {
            sendRejected(m_peer->connectionId(), endpointId, revision,
                         QStringLiteral("incompatible source window"));
            return false;
        }
    }

    request.endpointId = endpointId;
    request.source = source;
    request.pixels = pixels;
    request.targetFps = fps;
    request.framesPerLine = framesPerLine;
    request.minDbm = static_cast<float>(minDbm);
    request.maxDbm = static_cast<float>(maxDbm);
    if (!requestOverlapsSource(request, sourceCentreHz, sourceSampleRateHz)) {
        sendRejected(m_peer->connectionId(), endpointId, revision,
                     QStringLiteral("requested crop is outside source coverage"));
        return false;
    }
    EndpointEntry entry;
    entry.revision = revision;
    entry.sliceId = sliceId;
    entry.sourceFftSize = fftSize;
    entry.sourceWindowType = windowType;
    entry.request = request;
    std::optional<EndpointEntry> replaced;
    MediaSourceKey replacedSource;
    if (existing != m_endpoints.end()) {
        replacedSource = existing->second.request.source;
        replaced.emplace(std::move(existing->second));
        m_endpoints.erase(existing);
    }
    m_endpoints.emplace(endpointId, std::move(entry));
    if (!reconcileSource(source)) {
        removeEndpoint(endpointId);
        if (replaced.has_value()) {
            m_endpoints.emplace(endpointId, std::move(*replaced));
            reconcileSource(replacedSource);
        }
        sendRejected(m_peer->connectionId(), endpointId, revision,
                     QStringLiteral("source configuration rejected"));
        return false;
    }
    if (replaced.has_value() && !(replacedSource == source)) {
        releaseSourceIfUnused(replacedSource);
    }
    return true;
}

bool DaemonMediaController::handleUnsubscribe(const QJsonObject& control)
{
    quint32 endpointId = 0;
    if (!exactKeys(control, {"op", "connectionId", "endpointId"}) || !m_peer
        || !canonicalConnectionId(control.value(QStringLiteral("connectionId")))
        || control.value(QStringLiteral("connectionId")).toString() != m_peer->connectionId()
        || !exactUnsigned(control.value(QStringLiteral("endpointId")), endpointId, true)) {
        return false;
    }
    removeEndpoint(endpointId);
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
    DaemonSpectrumFrame frame = *sourceFrame;
    frame.dbmOffset += stationOffsetDb;

    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        EndpointEntry& entry = it->second;
        if (!(entry.request.source == key)) {
            continue;
        }
        if (!entry.endpoint.configured()
            || entry.endpoint.context().sourceGeneration != frame.generation
            || entry.endpoint.context().codec.contextGeneration == 0) {
            configureEndpointFromFrame(entry, frame);
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
            for (auto& [endpointId, entry] : m_endpoints) {
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
                if (sendControl(message)) {
                    entry.lastNoiseFloorTimestampNs = frame.producedAtNs;
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
    sourceContext.contextGeneration = nextContextGeneration();
    if (!entry.endpoint.configure(entry.request, sourceContext)) {
        return;
    }
    entry.encoder.reset();
    entry.sourceCentreHz = frame.centreHz;
    entry.sourceSampleRateHz = frame.sampleRateHz;
    entry.latestInput.reset();
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
    QJsonObject message{
        {QStringLiteral("op"), QStringLiteral("context")},
        {QStringLiteral("connectionId"), m_peer->connectionId()},
        {QStringLiteral("endpointId"), static_cast<qint64>(context.codec.endpointId)},
        {QStringLiteral("revision"), static_cast<qint64>(entry.revision)},
        {QStringLiteral("contextGeneration"), static_cast<qint64>(context.codec.contextGeneration)},
        {QStringLiteral("sourceStream"), context.source.streamIndex},
        {QStringLiteral("sourceCentreHz"), entry.sourceCentreHz},
        {QStringLiteral("sampleRateHz"), entry.sourceSampleRateHz},
        {QStringLiteral("centreHz"), context.exactCentreHz},
        {QStringLiteral("spanHz"), context.exactSpanHz},
        {QStringLiteral("wideCentreHz"), context.wideCentreHz},
        {QStringLiteral("wideSpanHz"), context.wideSpanHz},
        {QStringLiteral("traceSamples"), context.codec.traceSamples},
        {QStringLiteral("waterfallSamples"), context.codec.waterfallSamples},
        {QStringLiteral("wideSamples"), context.codec.wideSamples},
        {QStringLiteral("minDbm"), context.codec.minDbm},
        {QStringLiteral("maxDbm"), context.codec.maxDbm},
        {QStringLiteral("fps"), context.targetFps},
        {QStringLiteral("framesPerLine"), context.framesPerLine},
    };
    entry.contextSent = sendControl(message);
}

void DaemonMediaController::onSendTick()
{
    if (!m_peer || !m_peer->isReady() || m_endpoints.empty()) {
        return;
    }
    QList<quint32> ids;
    ids.reserve(static_cast<qsizetype>(m_endpoints.size()));
    for (const auto& [endpointId, unused] : m_endpoints) {
        Q_UNUSED(unused);
        ids.append(endpointId);
    }
    for (int offset = 0; offset < ids.size(); ++offset) {
        const int index = (m_roundRobinCursor + offset) % ids.size();
        EndpointEntry& entry = m_endpoints.at(ids.at(index));
        if (!entry.latestInput.has_value() || !entry.contextSent) {
            continue;
        }
        const DaemonSpectrumFrame frame = std::move(*entry.latestInput);
        entry.latestInput.reset();
        const std::optional<DisplayCodecFrame> reduced = entry.endpoint.consume(frame);
        m_roundRobinCursor = (index + 1) % ids.size();
        if (!reduced.has_value()) {
            return;
        }
        const QByteArray bytes = entry.encoder.encode(*reduced, entry.forceKeyframe);
        entry.forceKeyframe = false;
        if (bytes.isEmpty() || !m_peer->sendDisplay(bytes)) {
            // A false return can mean the backend accepted/buffered bytes.
            // Do not resend this sequence; force a fresh keyframe later.
            entry.forceKeyframe = true;
        }
        return;
    }
}

void DaemonMediaController::removeEndpoint(quint32 endpointId)
{
    auto it = m_endpoints.find(endpointId);
    if (it == m_endpoints.end()) {
        return;
    }
    const MediaSourceKey key = it->second.request.source;
    m_endpoints.erase(it);
    releaseSourceIfUnused(key);
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
    const QList<MediaSourceKey> keys = m_sources.keys();
    for (const MediaSourceKey& key : keys) {
        if (key.streamIndex != streamIndex) {
            continue;
        }
        const double centreHz = m_radioModel ? m_radioModel->streamCentreHz(streamIndex) : 0.0;
        const double sampleRateHz = m_radioModel
            ? m_radioModel->streamSampleRateHz(streamIndex) : 0.0;
        QList<quint32> outsideCoverage;
        for (const auto& [endpointId, entry] : m_endpoints) {
            if (entry.request.source == key
                && !requestOverlapsSource(entry.request, centreHz, sampleRateHz)) {
                sendRejected(m_peer ? m_peer->connectionId() : QString(), endpointId,
                             entry.revision,
                             QStringLiteral("source retune no longer covers requested crop"));
                outsideCoverage.append(endpointId);
            }
        }
        for (quint32 endpointId : outsideCoverage) {
            removeEndpoint(endpointId);
        }
        if (!m_sources.contains(key)) {
            continue;
        }
        if (!reconcileSource(key)) {
            // A retuned/disconnected stream must never keep emitting frames
            // labelled with its former geometry. The GUI will resubscribe if
            // a later binding makes this slice available again.
            m_source.deactivate(key);
            m_sources.remove(key);
            for (auto& [unused, entry] : m_endpoints) {
                Q_UNUSED(unused);
                if (entry.request.source == key) {
                    entry.endpoint.reset();
                    entry.encoder.reset();
                    entry.latestInput.reset();
                    entry.contextSent = false;
                    entry.lastNoiseFloorTimestampNs = -1;
                }
            }
        }
    }
}

void DaemonMediaController::onStreamBindingsChanged(int streamIndex, const QVector<int>&)
{
    QList<quint32> removed;
    for (auto it = m_endpoints.cbegin(); it != m_endpoints.cend(); ++it) {
        const EndpointEntry& entry = it->second;
        SliceModel* slice = m_radioModel ? m_radioModel->sliceById(entry.sliceId) : nullptr;
        if (entry.request.source.streamIndex == streamIndex
            && (!slice || slice->streamIndex() != streamIndex)) {
            sendRejected(m_peer ? m_peer->connectionId() : QString(), it->first, entry.revision,
                         QStringLiteral("slice stream binding changed"));
            removed.append(it->first);
        }
    }
    for (quint32 endpointId : removed) {
        removeEndpoint(endpointId);
    }
}

void DaemonMediaController::onSliceRemoved(int sliceId)
{
    QList<quint32> removed;
    for (auto it = m_endpoints.cbegin(); it != m_endpoints.cend(); ++it) {
        const EndpointEntry& entry = it->second;
        if (entry.sliceId == sliceId) {
            sendRejected(m_peer ? m_peer->connectionId() : QString(), it->first, entry.revision,
                         QStringLiteral("slice removed"));
            removed.append(it->first);
        }
    }
    for (quint32 endpointId : removed) {
        removeEndpoint(endpointId);
    }
}

void DaemonMediaController::reconcileAudio()
{
    // Every accepted control and every readiness transition begins by
    // flushing bounded captured PCM.  The saved next values preserve RTP
    // ordering across false/true contexts for this same MediaPeer.
    stopAudioCapture();
    const bool shouldRun = m_audioDesiredEnabled && m_peer && m_peer->isReady()
        && m_radioModel && m_radioModel->isConnected();
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
                    const bool accepted = peer->sendRtp(packet);
                    // The transport can synchronously retire or replace this
                    // session. Resolve only into the context that initiated
                    // this send, never a replacement that appeared mid-call.
                    // Retirement finalizes any remaining in-flight attempt as
                    // unresolved, without treating it as a packet-loss event.
                    if (activeContext && m_audioSender.get() == sender
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
    sendAudioContext(actualEnabled);
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

void DaemonMediaController::sendAudioContext(bool enabled)
{
    if (!m_peer || m_audioRevision == 0) {
        return;
    }
    const quint32 contextGeneration = nextContextGeneration();
    if (enabled) {
        beginAudioDiagnostics(contextGeneration);
    }
    sendControl({
        {QStringLiteral("op"), QStringLiteral("audio-context")},
        {QStringLiteral("connectionId"), m_peer->connectionId()},
        {QStringLiteral("revision"), static_cast<qint64>(m_audioRevision)},
        {QStringLiteral("generation"), static_cast<qint64>(contextGeneration)},
        {QStringLiteral("enabled"), enabled},
        {QStringLiteral("ssrc"), static_cast<qint64>(m_peer->audioSsrc())},
        {QStringLiteral("firstSequence"), static_cast<qint64>(m_audioNextSequence)},
        {QStringLiteral("firstTimestamp"), static_cast<qint64>(m_audioNextTimestamp)},
    });
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
    // disconnecting them too prevents recursive clearSession() and deletion
    // of the peer while it is emitting one of its own signals.
    // Sender remains owned by this controller; stop its timer/capture while
    // the current peer is still identifiable, then retire the peer.
    resetAudioSession();
    std::unique_ptr<MediaPeer> peer = std::move(m_peer);
    if (peer) {
        peer->disconnect(this);
        peer->stop();
    }
    clearProduction();
}

void DaemonMediaController::clearProduction()
{
    m_sendTimer.stop();
    const QList<MediaSourceKey> keys = m_sources.keys();
    for (const MediaSourceKey& key : keys) {
        m_source.deactivate(key);
    }
    m_sources.clear();
    m_endpoints.clear();
    m_roundRobinCursor = 0;
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
