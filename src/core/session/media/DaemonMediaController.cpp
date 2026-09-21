// =================================================================
// src/core/session/media/DaemonMediaController.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DaemonMediaController.h.
// =================================================================

#include "core/session/media/DaemonMediaController.h"

#include "core/FFTEngine.h"
#include "core/session/StationServer.h"
#include "core/session/media/MediaPeer.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QJsonArray>
#include <QJsonValue>
#include <QUuid>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace NereusSDR {
namespace {

constexpr int kMaxEndpoints = 8;
constexpr int kSenderIntervalMs = 5;
constexpr int kMaxKeyframesPerSecond = 5;

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
{
    m_source.setRadioModel(radioModel);
    m_sendTimer.setInterval(kSenderIntervalMs);
    connect(&m_sendTimer, &QTimer::timeout, this, &DaemonMediaController::onSendTick);
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
        clearProduction();
    }
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

void DaemonMediaController::onSourceFrame(MediaSourceKey key)
{
    const std::optional<DaemonSpectrumFrame> frame = m_source.takeLatest(key);
    if (!frame.has_value()) {
        return;
    }
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        EndpointEntry& entry = it->second;
        if (!(entry.request.source == key)) {
            continue;
        }
        if (!entry.endpoint.configured()
            || entry.endpoint.context().sourceGeneration != frame->generation
            || entry.endpoint.context().codec.contextGeneration == 0) {
            configureEndpointFromFrame(entry, *frame);
        }
        if (entry.endpoint.configured() && entry.contextSent) {
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
    sendContext(entry);
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

void DaemonMediaController::clearSession()
{
    // Detach and move ownership before stop(): MediaPeer::stop() can emit
    // closed synchronously. Its callbacks are identity/epoch guarded, but
    // disconnecting them too prevents recursive clearSession() and deletion
    // of the peer while it is emitting one of its own signals.
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
