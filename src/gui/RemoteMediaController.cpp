// no-port-check: NereusSDR-original. Remote daemon R3 receive display wiring.

#include "gui/RemoteMediaController.h"
#include "core/AppSettings.h"
#include "core/FFTEngine.h"
#include "core/session/StationClient.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "gui/DssGeometry.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace NereusSDR {
Q_LOGGING_CATEGORY(lcRemoteMedia, "nereus.remote.media")
namespace {
constexpr int kMaxEndpoints = 8;

bool number(const QJsonObject& object, const char* key, double low, double high,
            double& value, bool integer = false)
{
    const QJsonValue json = object.value(QLatin1String(key));
    if (!json.isDouble()) {
        return false;
    }
    value = json.toDouble();
    return std::isfinite(value) && value >= low && value <= high
        && (!integer || std::floor(value) == value);
}

bool uint32(const QJsonObject& object, const char* key, quint32& value)
{
    double parsed = 0.0;
    if (!number(object, key, 1, std::numeric_limits<quint32>::max(), parsed, true)) {
        return false;
    }
    value = static_cast<quint32>(parsed);
    return true;
}

bool newer(quint32 next, quint32 previous)
{
    return next != previous && quint32(next - previous) < 0x80000000u;
}

int fftSizeFor(double target)
{
    int size = 1024;
    while (size < FFTEngine::maximumFftSize() && size < target) {
        size *= 2;
    }
    return size;
}

QJsonObject plane(int detector, int averageMode, double alpha)
{
    return {{QStringLiteral("detector"), detector},
            {QStringLiteral("averageMode"), averageMode},
            {QStringLiteral("averageAlpha"), alpha}};
}

QJsonObject requestFor(SpectrumWidget* widget, SliceModel* slice)
{
    auto& settings = AppSettings::instance();
    const int fps = qBound(1, settings.value(QStringLiteral("DisplaySpectrumFps"),
                                QStringLiteral("30")).toString().toInt(), 60);
    const int baseSize = fftSizeFor(settings.value(QStringLiteral("DisplayFftSize"),
                                          QStringLiteral("4096")).toString().toInt());
    const int window = qBound(0, settings.value(QStringLiteral("DisplayFftWindow"),
                                    QString::number(int(WindowFunction::BlackmanHarris4)))
                                    .toString().toInt(), int(WindowFunction::Count) - 1);
    const int pixels = qBound(1, widget->width() - widget->reservedRightEdgeWidth(),
                             SpectrumEndpoint::kMaxPixels);
    const double span = widget->bandwidth();
    if (!std::isfinite(span) || span <= 0 || !std::isfinite(widget->centerFrequency())
        || slice->sampleRateHz() <= 0) {
        return {};
    }
    // R-R3-08: a deep zoom requests its own tier; it cannot lengthen the
    // shared Wide engine. Size is capped to the actual FFT engine limit.
    double target = double(slice->sampleRateHz()) * pixels / span;
    const double hzPerBin = settings.value(QStringLiteral("DisplayHzPerBinTarget"),
                                               QStringLiteral("0")).toString().toDouble();
    if (std::isfinite(hzPerBin) && hzPerBin > 0) {
        target = std::max(target, slice->sampleRateHz() / hzPerBin);
    }
    const int size = std::max(baseSize, fftSizeFor(target));
    const int framesPerLine = qBound(1, int(std::ceil(
        double(widget->wfUpdatePeriodMs()) * fps / 1000.0)), 10000);
    const double wideFactor = widget->spectrumRenderMode() == int(SpectrumRenderMode::Mode3D)
        ? dssMaxRowSpanFactor(dssShapeForAngle(0)) : 0.0;
    return {{QStringLiteral("sliceId"), slice->sliceIndex()},
            {QStringLiteral("tier"), size > baseSize ? QStringLiteral("fine") : QStringLiteral("wide")},
            {QStringLiteral("fftSize"), size}, {QStringLiteral("windowType"), window},
            {QStringLiteral("centreHz"), widget->centerFrequency()},
            {QStringLiteral("spanHz"), span}, {QStringLiteral("pixels"), pixels},
            {QStringLiteral("fps"), fps}, {QStringLiteral("framesPerLine"), framesPerLine},
            {QStringLiteral("trace"), plane(int(widget->spectrumDetector()),
                int(widget->spectrumAveraging()), widget->spectrumAverageAlpha())},
            {QStringLiteral("waterfall"), plane(int(widget->waterfallDetector()),
                int(widget->waterfallAveraging()), widget->waterfallAverageAlpha())},
            {QStringLiteral("minDbm"), -180.0}, {QStringLiteral("maxDbm"), 0.0},
            {QStringLiteral("wideSpanFactor"), wideFactor}};
}
} // namespace

struct RemoteMediaController::Private {
    struct Binding {
        QPointer<SpectrumWidget> widget;
        QPointer<SliceModel> slice;
        QJsonObject observed;
        int observedStream = -1;
        quint32 revision = 0;
        SpectrumEndpointContext context;
        DisplayCodecDecoder decoder;
        qint64 lastKeyframeMs = -1000;
        bool accepted = false;
        bool rejected = false;
    };
    QPointer<StationClient> client;
    QPointer<RadioModel> model;
    QPointer<PanadapterStack> stack;
    QPointer<MediaPeer> peer;
    MediaPeer::TransportFactory factory;
    QTimer* timer = nullptr;
    QElapsedTimer clock;
    std::map<quint32, Binding> bindings;
    QString connectionId;
    quint32 epoch = 0;
    quint32 nextEndpoint = 1;
    quint64 frames = 0;
};

RemoteMediaController::RemoteMediaController(StationClient* client, RadioModel* model,
    PanadapterStack* stack, QObject* parent, MediaPeer::TransportFactory factory)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->client = client;
    d->model = model;
    d->stack = stack;
    d->factory = std::move(factory);
    d->clock.start();
    d->timer = new QTimer(this);
    d->timer->setInterval(100);
    connect(d->timer, &QTimer::timeout, this, &RemoteMediaController::refreshSubscriptions);
    connect(client, &StationClient::handshakeComplete, this, &RemoteMediaController::start);
    connect(client, &StationClient::mediaSessionEnded, this, [this](quint32 epoch) {
        if (epoch == d->epoch) {
            stop();
        }
    });
    connect(client, &StationClient::mediaControlReceived,
            this, &RemoteMediaController::receiveControl);
    connect(client, &QObject::destroyed, this, &RemoteMediaController::stop);
    connect(model, &RadioModel::connectionStateChanged, this, [this](ConnectionState state) {
        if (state != ConnectionState::Connected) {
            // The daemon retires FFT production when the radio disconnects,
            // even if this authenticated station session remains connected.
            // Retire our observations too so identical settings resubscribe.
            for (auto& [id, binding] : d->bindings) {
                send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                      {QStringLiteral("endpointId"), double(id)}});
                if (binding.widget) { binding.widget->clearRemoteSpectrum(); }
            }
            d->bindings.clear();
        } else {
            refreshSubscriptions();
        }
    });
    if (client && client->mediaAvailable()) {
        start();
    }
}

RemoteMediaController::~RemoteMediaController() { stop(); }
quint64 RemoteMediaController::receivedDisplayFrames() const { return d->frames; }
int RemoteMediaController::activeEndpointCount() const { return int(d->bindings.size()); }

void RemoteMediaController::stop()
{
    d->timer->stop();
    d->connectionId.clear();
    if (d->peer) {
        MediaPeer* old = d->peer;
        d->peer = nullptr;
        disconnect(old, nullptr, this, nullptr);
        old->stop();
        old->deleteLater();
    }
    for (auto& [id, binding] : d->bindings) {
        if (binding.widget) {
            binding.widget->clearRemoteSpectrum();
        }
    }
    d->bindings.clear();
}

void RemoteMediaController::start()
{
    stop();
    if (!d->client || !d->client->mediaAvailable()) {
        return;
    }
    d->epoch = d->client->sessionEpoch();
    d->connectionId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto* peer = new MediaPeer(this, d->factory);
    d->peer = peer;
    const quint32 epoch = d->epoch;
    const auto current = [this, peer, epoch] {
        return d->peer == peer && d->client && d->client->mediaAvailable()
            && d->client->sessionEpoch() == epoch;
    };
    connect(peer, &MediaPeer::controlReady, this, [this, current](const QJsonObject& payload) {
        if (current()) { send(payload); }
    });
    connect(peer, &MediaPeer::displayReceived, this, [this, current](const QByteArray& packet) {
        if (current()) { receiveDisplay(packet); }
    });
    connect(peer, &MediaPeer::ready, this, [this, current] {
        if (current()) {
            d->timer->start();
            refreshSubscriptions();
        }
    });
    connect(peer, &MediaPeer::closed, this, [this, current] {
        if (current()) {
            stop();
            emit errorOccurred(QStringLiteral("Station media connection closed"));
        }
    });
    connect(peer, &MediaPeer::errorOccurred, this, [this, current](const QString& reason) {
        if (current()) {
            stop();
            emit errorOccurred(reason);
        }
    });
    if (!peer->start(IMediaTransport::Role::Answerer, d->connectionId)) {
        stop();
        return;
    }
    send({{QStringLiteral("op"), QStringLiteral("start")}});
}

bool RemoteMediaController::send(QJsonObject payload)
{
    if (!d->client || d->connectionId.isEmpty()) { return false; }
    payload.insert(QStringLiteral("connectionId"), d->connectionId);
    return d->client->sendMediaControl(payload, d->epoch);
}

void RemoteMediaController::refreshSubscriptions()
{
    if (!d->client || !d->client->mediaAvailable() || d->epoch != d->client->sessionEpoch()
        || !d->peer || !d->peer->isReady() || !d->model || !d->stack) {
        return;
    }
    if (!d->model->isConnected()) { return; }
    struct Desired {
        SpectrumWidget* widget;
        SliceModel* slice;
        QJsonObject request;
    };
    QList<Desired> desired;
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        SpectrumWidget* widget = applet->spectrumWidget();
        SliceModel* slice = d->model->sliceById(applet->activeSliceIndex());
        if (!widget || !widget->isVisible() || !slice || slice->streamIndex() < 0) { continue; }
        QJsonObject request = requestFor(widget, slice);
        if (!request.isEmpty()) { desired.append({widget, slice, std::move(request)}); }
    }
    // Retire obsolete bindings before creating any replacement. In particular,
    // a global FFT-window change must release every old shared-source window
    // first; updating them individually would reject each against its peers.
    // The reliable control stream preserves all unsubscriptions before adds.
    for (auto it = d->bindings.begin(); it != d->bindings.end();) {
        const auto next = std::find_if(desired.cbegin(), desired.cend(),
            [&it](const Desired& item) {
                return item.widget == it->second.widget && item.slice == it->second.slice;
            });
        const bool windowChanged = next != desired.cend()
            && next->request.value(QStringLiteral("windowType"))
                != it->second.observed.value(QStringLiteral("windowType"));
        if (next != desired.cend() && !windowChanged) { ++it; continue; }
        send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
              {QStringLiteral("endpointId"), double(it->first)}});
        if (it->second.widget) { it->second.widget->clearRemoteSpectrum(); }
        it = d->bindings.erase(it);
    }
    for (const Desired& item : desired) {
        SpectrumWidget* widget = item.widget;
        SliceModel* slice = item.slice;
        auto found = std::find_if(d->bindings.begin(), d->bindings.end(),
            [widget, slice](const auto& entry) {
                return entry.second.widget == widget && entry.second.slice == slice;
            });
        if (found == d->bindings.end()) {
            if (d->bindings.size() >= kMaxEndpoints || d->nextEndpoint == 0) { continue; }
            const quint32 id = d->nextEndpoint++;
            found = d->bindings.try_emplace(id).first;
            found->second.widget = widget;
            found->second.slice = slice;
            widget->clearRemoteSpectrum();
        }
        const quint32 id = found->first;
        auto& binding = found->second;
        QJsonObject request = item.request;
        // observed is the last attempted geometry, not proof of acceptance.
        // A rejected request remains blank until its inputs change; reconnect
        // retires the binding, and source-window changes are batched above.
        if (request == binding.observed
            && binding.observedStream == slice->streamIndex()) { continue; }
        binding.observed = request;
        binding.observedStream = slice->streamIndex();
        ++binding.revision;
        if (binding.revision == 0) { ++binding.revision; }
        binding.accepted = false;
        binding.rejected = false;
        binding.decoder.reset();
        widget->clearRemoteSpectrum();
        request.insert(QStringLiteral("op"), QStringLiteral("subscribe"));
        request.insert(QStringLiteral("endpointId"), double(id));
        request.insert(QStringLiteral("revision"), double(binding.revision));
        if (!send(request)) { binding.observed = {}; }
    }
}

void RemoteMediaController::receiveControl(const QJsonObject& payload, quint32 epoch)
{
    if (!d->client || !d->client->mediaAvailable() || epoch != d->epoch
        || !d->peer || payload.value(QStringLiteral("connectionId")) != d->connectionId) { return; }
    const QString op = payload.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("description") || op == QLatin1String("candidate")) {
        d->peer->acceptControl(payload);
        return;
    }
    if (op == QLatin1String("rejected") && payload.size() == 6
        && payload.value(QStringLiteral("endpointId")).isDouble()
        && payload.value(QStringLiteral("endpointId")).toDouble() == 0
        && payload.value(QStringLiteral("revision")).isDouble()
        && payload.value(QStringLiteral("revision")).toDouble() == 0
        && payload.value(QStringLiteral("reason")).isString()) {
        const QString reason = payload.value(QStringLiteral("reason")).toString().left(512);
        stop();
        emit errorOccurred(reason);
        return;
    }
    quint32 endpointId = 0, revision = 0;
    if (!uint32(payload, "endpointId", endpointId) || !uint32(payload, "revision", revision)) { return; }
    auto it = d->bindings.find(endpointId);
    if (it == d->bindings.end() || it->second.revision != revision || !it->second.widget) { return; }
    auto& binding = it->second;
    if (op == QLatin1String("rejected")) {
        if (payload.size() == 6 && payload.value(QStringLiteral("reason")).isString()) {
            binding.accepted = false;
            binding.rejected = true;
            binding.decoder.reset();
            binding.widget->clearRemoteSpectrum();
            emit errorOccurred(payload.value(QStringLiteral("reason")).toString().left(512));
        }
        return;
    }
    if (op != QLatin1String("context") || payload.size() != 19 || binding.rejected) { return; }
    // A gesture/rebind may arrive between the outgoing request and its ACK.
    // Issue the newer request before accepting an old view over that gesture.
    if (binding.slice && (requestFor(binding.widget, binding.slice) != binding.observed
                         || binding.observedStream != binding.slice->streamIndex())) {
        refreshSubscriptions();
        return;
    }
    SpectrumEndpointContext context;
    context.codec.endpointId = endpointId;
    double stream = 0, sourceCentre = 0, rate = 0, trace = 0, waterfall = 0, wide = 0;
    double min = 0, max = 0, fps = 0, lines = 0;
    if (!uint32(payload, "contextGeneration", context.codec.contextGeneration)
        || !number(payload, "sourceStream", 0, 255, stream, true)
        || !number(payload, "sourceCentreHz", 0, 1.0e12, sourceCentre)
        || !number(payload, "sampleRateHz", 1, 1.0e8, rate)
        || !number(payload, "centreHz", 0, 1.0e12, context.exactCentreHz)
        || !number(payload, "spanHz", 0.000001, rate, context.exactSpanHz)
        || !number(payload, "wideCentreHz", 0, 1.0e12, context.wideCentreHz)
        || !number(payload, "wideSpanHz", 0, rate, context.wideSpanHz)
        || !number(payload, "traceSamples", 1, SpectrumEndpoint::kMaxPixels, trace, true)
        || !number(payload, "waterfallSamples", 1, SpectrumEndpoint::kMaxPixels, waterfall, true)
        || !number(payload, "wideSamples", 0, SpectrumEndpoint::kMaxWideSamples, wide, true)
        || !number(payload, "minDbm", -400, 100, min)
        || !number(payload, "maxDbm", -400, 100, max) || min >= max
        || !number(payload, "fps", 1, 60, fps, true)
        || !number(payload, "framesPerLine", 1, 10000, lines, true)) { return; }
    if (binding.accepted && !newer(context.codec.contextGeneration,
                                  binding.context.codec.contextGeneration)) { return; }
    if ((wide == 0) != (context.wideSpanHz == 0)) { return; }
    context.codec.traceSamples = quint16(trace);
    context.codec.waterfallSamples = quint16(waterfall);
    context.codec.wideSamples = quint16(wide);
    context.codec.minDbm = float(min);
    context.codec.maxDbm = float(max);
    context.source.streamIndex = int(stream);
    context.targetFps = int(fps);
    context.framesPerLine = int(lines);
    binding.context = context;
    binding.decoder.reset();
    binding.accepted = true;
    binding.rejected = false;
    binding.widget->setRemoteSpectrumContext(context, sourceCentre, rate);
    // The source crop may be bin-aligned. Remember the displayed accepted
    // window so the polling observer does not feed an ACK back as a new zoom.
    if (binding.slice) { binding.observed = requestFor(binding.widget, binding.slice); }
    requestKeyframe(endpointId);
}

void RemoteMediaController::requestKeyframe(quint32 endpointId)
{
    auto it = d->bindings.find(endpointId);
    if (it == d->bindings.end() || !it->second.accepted) { return; }
    auto& binding = it->second;
    const qint64 now = d->clock.elapsed();
    if (now - binding.lastKeyframeMs < 200) { return; }
    binding.lastKeyframeMs = now;
    send({{QStringLiteral("op"), QStringLiteral("keyframe")},
          {QStringLiteral("endpointId"), double(endpointId)},
          {QStringLiteral("contextGeneration"), double(binding.context.codec.contextGeneration)}});
}

void RemoteMediaController::receiveDisplay(const QByteArray& packet)
{
    // Route only the documented v1 prefix. The decoder validates the complete
    // envelope and bounds before any plane can reach the renderer.
    if (packet.size() < 42 || packet.size() > DisplayCodecEncoder::kMaxEncodedBytes
        || packet.first(4) != QByteArrayLiteral("NSDC")) { return; }
    const quint32 id = qFromBigEndian<quint32>(packet.constData() + 8);
    const quint32 generation = qFromBigEndian<quint32>(packet.constData() + 12);
    auto it = d->bindings.find(id);
    if (it == d->bindings.end() || !it->second.accepted || !it->second.widget
        || generation != it->second.context.codec.contextGeneration) { return; }
    auto& binding = it->second;
    const DisplayCodecDecodeResult decoded = binding.decoder.decode(packet);
    if (decoded.disposition == DisplayCodecDisposition::NeedKeyframe) {
        requestKeyframe(id);
    } else if (decoded.disposition == DisplayCodecDisposition::Accepted) {
        const QPointer<RemoteMediaController> self(this);
        const QString connectionId = d->connectionId;
        const bool rendered = binding.widget->updateRemoteSpectrum(decoded.frame);
        // Rendering emits application signals; a receiver may end this session.
        if (!self || d->connectionId != connectionId || !rendered) { return; }
        ++d->frames;
        if (d->frames == 1) { qCInfo(lcRemoteMedia) << "First encrypted remote spectrum frame received"; }
        emit displayFrameReceived(id);
    }
}
} // namespace NereusSDR
