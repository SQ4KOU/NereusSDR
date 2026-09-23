// no-port-check: NereusSDR-original. Remote daemon R3 receive display wiring.

#include "gui/RemoteMediaController.h"
#include "core/AppSettings.h"
#include "core/AudioDeviceConfig.h"
#include "core/AudioEngine.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "core/ClarityController.h"
#include "core/FFTEngine.h"
#include "core/session/StationClient.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "core/session/media/WidebandDisplayContext.h"
#include "gui/DssGeometry.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/RemoteDisplayAllocator.h"
#include "gui/RemoteGeneration.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/Ps3DisplayCodec.h"
#include "models/SliceModel.h"

#include <QElapsedTimer>
#include <QHash>
#include <QJsonDocument>
#include <QLoggingCategory>
#include <QPointer>
#include <QScopeGuard>
#include <QSet>
#include <QSignalBlocker>
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
constexpr int kDefaultAllocationAckTimeoutMs = 10'000;
// The audio status refresh, which runs only while a receiver runs or a
// playback problem awaits recovery.
constexpr int kAudioStatusRefreshMs = 250;
// Speaker progress this recent means audio is playing now: the window the
// title bar has always used.
constexpr qint64 kPlaybackProgressWindowMs = 500;

// The speaker device the operator selected. It names the selection, which is
// not proof that the device is the one in use.
QString selectedSpeakerOutput()
{
    const AudioDeviceConfig speakers =
        AudioDeviceConfig::loadFromSettings(QStringLiteral("audio/Speakers"));
    return speakers.deviceName.isEmpty() ? QStringLiteral("System default")
                                         : speakers.deviceName;
}

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

bool uint64(const QJsonObject& object, const char* key, quint64 maximum,
            quint64& value, bool allowZero = true)
{
    double parsed = 0.0;
    if (!number(object, key, allowZero ? 0 : 1, static_cast<double>(maximum),
                parsed, true)) {
        return false;
    }
    value = static_cast<quint64>(parsed);
    return true;
}

// Log wording only: the profile Core reported for this context, never a
// profile this GUI assumes.
QString reportedAudioProfile(const std::optional<OpusEncoderProfile>& encoder)
{
    if (!encoder) {
        return QStringLiteral("codec profile not reported by Core");
    }
    return QStringLiteral("Opus %1 Hz, %2 channels, %3-sample frames, "
                          "target %4 bit/s, audio bandwidth %5 Hz")
        .arg(encoder->sampleRate)
        .arg(encoder->channels)
        .arg(encoder->frameSamples)
        .arg(encoder->targetBitrate)
        .arg(encoder->audioBandwidthHz);
}

bool geometryNeedsWideband(double centreHz, double spanHz,
                           double sourceCentreHz, double sourceRateHz)
{
    const double sourceLow = sourceCentreHz - sourceRateHz * 0.5;
    const double sourceHigh = sourceCentreHz + sourceRateHz * 0.5;
    const double viewLow = centreHz - spanHz * 0.5;
    const double viewHigh = centreHz + spanHz * 0.5;
    // Inactive endpoint geometry is derived from clamped FFT bin edges and
    // can legitimately land exactly on either DDC edge. Expand by one ULP
    // solely for arithmetic roundoff; this is not a bin-width allowance.
    const double lowLimit = std::nextafter(sourceLow,
                                           -std::numeric_limits<double>::infinity());
    const double highLimit = std::nextafter(sourceHigh,
                                            std::numeric_limits<double>::infinity());
    return viewLow < lowLimit || viewHigh > highLimit;
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

SliceModel* currentSliceForPan(RadioModel* model, PanadapterStack* stack,
                              SpectrumWidget* widget)
{
    if (!model || !stack || !widget) { return nullptr; }
    for (PanadapterApplet* applet : stack->allApplets()) {
        if (applet->spectrumWidget() == widget) {
            return model->sliceById(applet->activeSliceIndex());
        }
    }
    return nullptr;
}

QJsonObject requestFor(SpectrumWidget* widget, SliceModel* slice,
                       bool remoteWidebandAvailable)
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
    QJsonObject request{{QStringLiteral("sliceId"), slice->sliceIndex()},
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
    if (remoteWidebandAvailable) {
        // This is permission, not current demand. Core derives demand from
        // the accepted span and reports the resulting active state.
        request.insert(QStringLiteral("extendedView"), widget->extendedViewAllowed());
    }
    return request;
}

QJsonObject allocatedRequest(QJsonObject request, const RemoteDisplayQuality& quality)
{
    request.insert(QStringLiteral("pixels"), quality.pixels);
    request.insert(QStringLiteral("fps"), quality.fps);
    request.insert(QStringLiteral("framesPerLine"), quality.framesPerLine);
    return request;
}

bool sameOriginalIntent(QJsonObject left, QJsonObject right)
{
    for (const QString& key : {QStringLiteral("pixels"), QStringLiteral("fps"),
                               QStringLiteral("framesPerLine")}) {
        left.remove(key);
        right.remove(key);
    }
    return left == right;
}

bool zeroCharge(const DisplayBudgetCharge& charge)
{
    return charge.applicationBytesPerSecond == 0
        && charge.spectrumSampleUnitsPerSecond == 0
        && charge.messagesPerSecond == 0;
}

bool nonIncreasing(const DisplayBudgetCharge& next, const DisplayBudgetCharge& previous)
{
    return next.applicationBytesPerSecond <= previous.applicationBytesPerSecond
        && next.spectrumSampleUnitsPerSecond <= previous.spectrumSampleUnitsPerSecond
        && next.messagesPerSecond <= previous.messagesPerSecond;
}

DisplayBudgetCharge maximumCharge(const DisplayBudgetCharge& left,
                                  const DisplayBudgetCharge& right)
{
    return {std::max(left.applicationBytesPerSecond, right.applicationBytesPerSecond),
            std::max(left.spectrumSampleUnitsPerSecond,
                     right.spectrumSampleUnitsPerSecond),
            std::max(left.messagesPerSecond, right.messagesPerSecond)};
}

QString requestIdentity(const QJsonObject& request, quint32 generation, bool ps3Desired)
{
    QJsonObject identified = request;
    identified.insert(QStringLiteral("budgetGeneration"), static_cast<qint64>(generation));
    identified.insert(QStringLiteral("ps3Desired"), ps3Desired);
    return QString::fromUtf8(QJsonDocument(identified).toJson(QJsonDocument::Compact));
}

QString allocationIdentity(const DisplayBudgetLimits& limits,
                           const QList<RemoteDisplayIntent>& intents,
                           bool ps3Enabled, bool retainedPs3ExceedsCap)
{
    QString key = QStringLiteral("%1:%2:%3:%4:%5")
        .arg(limits.applicationBytesPerSecond)
        .arg(limits.spectrumSampleUnitsPerSecond)
        .arg(limits.generation)
        .arg(ps3Enabled)
        .arg(retainedPs3ExceedsCap);
    for (const RemoteDisplayIntent& intent : intents) {
        key += QStringLiteral("|%1:%2:%3:%4:%5:%6")
            .arg(intent.panId).arg(intent.pixels).arg(intent.fps)
            .arg(intent.includeWidePlane).arg(intent.waterfallPeriodMs)
            .arg(intent.active);
    }
    return key;
}
} // namespace

struct RemoteMediaController::Private {
    struct Binding {
        enum class PendingKind { Subscribe, Unsubscribe };
        struct Pending {
            PendingKind kind = PendingKind::Subscribe;
            quint32 revision = 0;
            QJsonObject request;
            DisplayBudgetCharge charge;
            QString identity;
            qint64 sentAtMs = 0;
            bool timedOut = false;
        };
        QString panId;
        QPointer<SpectrumWidget> widget;
        QPointer<SliceModel> slice;
        QJsonObject observed;
        QJsonObject desiredOriginal;
        QJsonObject acceptedRequest;
        int observedStream = -1;
        quint64 observedStreamEpoch = 0;
        quint32 revision = 0;
        quint32 acceptedRevision = 0;
        quint32 contextRevision = 0;
        DisplayBudgetCharge acceptedCharge;
        std::optional<Pending> pending;
        SpectrumEndpointContext context;
        double sourceCentreHz = 0;
        DisplayCodecDecoder decoder;
        qint64 lastKeyframeMs = -1000;
        bool accepted = false;
        bool rejected = false;
        bool retiring = false;
        bool suspending = false;
        bool receivedNoiseFloor = false;
        QString refusedIdentity;
        QString refusalReason;
        QList<qint64> receivedFrameTimesMs;
        QMetaObject::Connection ctunGesture;
        QMetaObject::Connection centreGesture;
        ~Binding() {
            QObject::disconnect(ctunGesture);
            QObject::disconnect(centreGesture);
        }
    };
    QPointer<StationClient> client;
    QPointer<RadioModel> model;
    QPointer<PanadapterStack> stack;
    QPointer<MediaPeer> peer;
    MediaPeer::TransportFactory factory;
    QTimer* timer = nullptr;
    QElapsedTimer clock;
    RemoteMediaController::AllocationClock allocationClock;
    int allocationAckTimeoutMs = kDefaultAllocationAckTimeoutMs;
    std::map<quint32, Binding> bindings;
    struct CtunState {
        quint64 epoch = 0;
        int requestSliceId = -1;
        bool requestedPin = false;
        bool pending = false;
        bool initialized = false;
        quint32 rejectedContext = 0;
    };
    QHash<int, CtunState> ctunStreams;
    QString connectionId;
    quint32 epoch = 0;
    quint32 nextEndpoint = 1;
    quint64 frames = 0;
    Ps3DisplayAssembler ps3Assembler;
    quint64 ps3Generation = 0;
    std::unique_ptr<RemoteAudioReceiver> audio;
    std::optional<RemoteAudioContextMessage> acceptedAudioContext;
    quint32 audioRevision = 0;
    quint32 audioGeneration = 0;
    bool preparingAudio = false;
    bool audioEnabled = false;
    bool audioRetryPending = false;
    // A persistent local playback failure and the identity it was recorded
    // against; only matching recovery with real speaker progress clears it.
    std::optional<RemoteAudioFailure> audioFailure;
    // An interruption whose automatic retry is scheduled, until the next
    // accepted context or stop().
    bool audioRestarting = false;
    QString selectedOutput;
    RemoteAudioStatus audioStatus;
    QTimer* audioStatusTimer = nullptr;
    bool recoveryRequested = false;
    qint64 lastAudioRequestMs = -1000;
    bool desiredPs3 = false;
    bool accountedPs3 = false;
    bool ps3Refused = false;
    quint32 ps3RefusedGeneration = 0;
    QString ps3RefusalReason;
    struct PendingPs3 {
        quint32 commandId = 0;
        bool enabled = false;
        qint64 sentAtMs = 0;
        bool timedOut = false;
        bool commandAccepted = false;
    };
    std::optional<PendingPs3> pendingPs3;
    bool refreshingBudget = false;
    bool budgetReplanRequested = false;
    QString allocationCacheIdentity;
    std::optional<RemoteDisplayAllocation> cachedAllocation;
    QString cachedAllocationError;
};

RemoteMediaController::RemoteMediaController(StationClient* client, RadioModel* model,
    PanadapterStack* stack, QObject* parent, MediaPeer::TransportFactory factory,
    AllocationClock allocationClock, int allocationAckTimeoutMs)
    : QObject(parent), d(std::make_unique<Private>())
{
    d->client = client;
    d->model = model;
    d->stack = stack;
    d->factory = std::move(factory);
    d->clock.start();
    d->allocationClock = std::move(allocationClock);
    if (!d->allocationClock) {
        d->allocationClock = [this] { return d->clock.elapsed(); };
    }
    d->allocationAckTimeoutMs = allocationAckTimeoutMs > 0
        ? allocationAckTimeoutMs : kDefaultAllocationAckTimeoutMs;
    d->audio = std::make_unique<RemoteAudioReceiver>(model->audioEngine());
    d->selectedOutput = selectedSpeakerOutput();
    d->audioStatusTimer = new QTimer(this);
    d->audioStatusTimer->setObjectName(QStringLiteral("remoteAudioStatusTimer"));
    d->audioStatusTimer->setInterval(kAudioStatusRefreshMs);
    connect(d->audioStatusTimer, &QTimer::timeout,
            this, &RemoteMediaController::refreshAudioStatus);
    connect(d->audio.get(), &RemoteAudioReceiver::errorOccurred, this,
            [this](const QString& reason, RemoteAudioReceiver::Fault fault) {
        // Record the failure against this session and context as they stand
        // now, before the disable below advances the audio revision.
        d->audioFailure = RemoteAudioFailure{fault, d->epoch, d->connectionId,
                                             d->audioGeneration,
                                             d->audio->telemetry().generation};
        // The receiver's detail is for diagnosis; the operator sees the
        // plain-English problem below.
        qCWarning(lcRemoteMedia).noquote()
            << QStringLiteral("Remote audio playback failed: %1").arg(reason);
        d->audioEnabled = false;
        d->audio->stop();
        const QPointer<RemoteMediaController> self(this);
        if (d->peer && d->peer->isReady()) {
            ++d->audioRevision;
            if (!d->audioRevision) { ++d->audioRevision; }
            send({{QStringLiteral("op"), QStringLiteral("audio")},
                  {QStringLiteral("revision"), double(d->audioRevision)},
                  {QStringLiteral("enabled"), false}});
            if (!self) { return; }
        }
        refreshAudioStatus();
        if (!self) { return; }
        emit errorOccurred(remoteAudioProblemText(fault));
    });
    connect(d->audio.get(), &RemoteAudioReceiver::restartRequested, this,
            [this](const QString& reason, RemoteAudioReceiver::Fault) {
        qCWarning(lcRemoteMedia) << reason;
        d->audio->stop();
        d->audioRestarting = true;
        if (!d->audioRetryPending) {
            d->audioRetryPending = true;
            const QString connection = d->connectionId;
            const quint32 revision = d->audioRevision;
            const int delay = int(std::max<qint64>(0, 1000 - (d->clock.elapsed() - d->lastAudioRequestMs)));
            QTimer::singleShot(delay, this, [this, connection, revision] {
                if (connection != d->connectionId || revision != d->audioRevision) { return; }
                d->audioRetryPending = false;
                requestAudio();
            });
        }
        refreshAudioStatus();
    });
    connect(model->audioEngine(), &AudioEngine::masterMutedChanged,
            this, &RemoteMediaController::requestAudio);
    connect(model->audioEngine(), &AudioEngine::speakersConfigChanged, this, [this] {
        d->selectedOutput = selectedSpeakerOutput();
        // Opening the speaker for playback can report its configuration from
        // inside the receiver's start(); the accepted context that started it
        // refreshes the status once start() returns.
        if (!d->preparingAudio) { requestAudio(); }
    });
    d->timer = new QTimer(this);
    d->timer->setInterval(100);
    connect(d->timer, &QTimer::timeout, this, &RemoteMediaController::refreshSubscriptions);
    connect(client, &StationClient::displayBudgetChanged, this, [this] {
        if (!d->client) { return; }
        if (const auto limits = d->client->remoteDisplayBudgetLimits();
            limits && d->ps3RefusedGeneration != 0
            && limits->generation != d->ps3RefusedGeneration) {
            d->ps3Refused = false;
            d->ps3RefusalReason.clear();
            d->ps3RefusedGeneration = 0;
        }
        const bool advertised = d->client->remotePs3DisplaySubscribed();
        if (advertised) {
            d->accountedPs3 = true;
        } else if (!d->pendingPs3
                   || (d->pendingPs3->commandAccepted
                       && !d->pendingPs3->enabled)) {
            d->accountedPs3 = false;
        }
        if (d->pendingPs3 && d->pendingPs3->commandAccepted
            && advertised == d->pendingPs3->enabled) {
            d->accountedPs3 = advertised;
            d->pendingPs3.reset();
        }
        refreshSubscriptions();
    });
    connect(client, &StationClient::ps3DisplaySubscriptionRequested,
            this, [this](bool enabled) {
        if (!d->client || !d->client->remoteDisplayBudgetLimits()) { return; }
        d->desiredPs3 = enabled;
        d->ps3Refused = false;
        d->ps3RefusalReason.clear();
        d->ps3RefusedGeneration = 0;
        refreshSubscriptions();
    });
    connect(client, &StationClient::ps3DisplaySubscriptionStarted,
            this, [this](quint32 commandId, bool enabled) {
        if (!d->client || !d->client->remoteDisplayBudgetLimits()) { return; }
        d->pendingPs3 = Private::PendingPs3{
            commandId, enabled, d->allocationClock(), false, false};
        if (enabled) {
            // The enable is now in flight. Account its maximum reservation
            // until the command or capabilities prove it was refused.
            d->accountedPs3 = true;
        }
    });
    connect(client, &StationClient::ps3DisplaySubscriptionFinished,
            this, [this](quint32 commandId, bool enabled,
                         bool accepted, const QString& reason) {
        if (!d->pendingPs3 || d->pendingPs3->commandId != commandId
            || d->pendingPs3->enabled != enabled || !d->client) { return; }
        if (!accepted) {
            d->accountedPs3 = d->client->remotePs3DisplaySubscribed();
            d->ps3Refused = enabled;
            d->ps3RefusalReason = reason.left(512);
            d->ps3RefusedGeneration = d->client->remoteDisplayBudgetLimits()
                ? d->client->remoteDisplayBudgetLimits()->generation : 0;
            d->pendingPs3.reset();
        } else {
            d->pendingPs3->commandAccepted = true;
            const bool advertised = d->client->remotePs3DisplaySubscribed();
            if (advertised == enabled) {
                d->accountedPs3 = advertised;
                d->pendingPs3.reset();
            }
        }
        refreshSubscriptions();
    });
    if (stack) {
        connect(stack, &PanadapterStack::panRetired,
                this, &RemoteMediaController::refreshSubscriptions);
        connect(stack, &PanadapterStack::activePanChanged,
                this, &RemoteMediaController::refreshSubscriptions);
        connect(stack, &QObject::destroyed, this, [this] {
            d->stack = nullptr;
            QList<quint32> endpoints;
            for (const auto& [id, binding] : d->bindings) { endpoints.append(id); }
            d->ctunStreams.clear();
            retireSubscriptions(endpoints);
        });
    }
    connect(client, &StationClient::handshakeComplete, this, &RemoteMediaController::start);
    connect(client, &StationClient::mediaSessionEnded, this, [this](quint32 epoch) {
        if (epoch == d->epoch) {
            stop();
        }
    });
    connect(client, &StationClient::mediaControlReceived,
            this, &RemoteMediaController::receiveControl);
    connect(client, &StationClient::streamCtunPinFinished, this,
        [this](int sliceId, quint64 epoch, bool pinned, bool accepted) {
            if (!d->model) { return; }
            SliceModel* slice = d->model->sliceById(sliceId);
            if (!slice || slice->streamEpoch() != epoch) { return; }
            auto state = d->ctunStreams.find(slice->streamIndex());
            if (state == d->ctunStreams.end() || state->epoch != epoch
                || state->requestSliceId != sliceId || state->requestedPin != pinned) { return; }
            state->pending = false;
            state->initialized = accepted;
            if (!accepted) {
                // A migration can refuse a request after it leaves the GUI.
                // Wait for fresh source context before retrying this lifetime.
                for (const auto& [id, binding] : d->bindings) {
                    if (binding.slice && binding.slice->streamIndex() == slice->streamIndex()
                        && isNewerGeneration(binding.context.codec.contextGeneration, state->rejectedContext)) {
                        state->rejectedContext = binding.context.codec.contextGeneration;
                    }
                }
            }
            refreshCtunState();
        });
    connect(client, &StationClient::streamCentreFinished, this,
        [this](int sliceId, quint64 epoch, bool accepted) {
            if (accepted || !d->model) { return; }
            SliceModel* slice = d->model->sliceById(sliceId);
            if (!slice || slice->streamEpoch() != epoch) { return; }
            const QPointer<RemoteMediaController> self(this);
            QList<quint32> endpointIds;
            for (const auto& [id, binding] : d->bindings) { endpointIds.append(id); }
            for (quint32 id : endpointIds) {
                auto found = d->bindings.find(id);
                if (found == d->bindings.end()) { continue; }
                Private::Binding& binding = found->second;
                if (!binding.widget || !binding.slice
                    || binding.slice->streamIndex() != slice->streamIndex()
                    || binding.slice->streamEpoch() != epoch
                    || binding.sourceCentreHz <= 0) { continue; }
                // A drag is optimistic view movement. A refused hardware move
                // must return to the last accepted Core source, without
                // emitting another gesture or retaining its in-flight crop.
                QPointer<SpectrumWidget> widget = binding.widget;
                const double sourceCentreHz = binding.sourceCentreHz;
                const double bandwidth = widget->bandwidth();
                widget->setDisplayWindowPreservingHistory(sourceCentreHz, bandwidth);
                if (!self || !widget) { return; }
                widget->setDdcCenterFrequency(sourceCentreHz);
                if (!self || !widget) { return; }
                widget->invalidateRemoteSpectrumFrame();
                if (!self) { return; }
                found = d->bindings.find(id);
                if (found != d->bindings.end()) { found->second.observed = {}; }
            }
            refreshSubscriptions();
        });
    connect(client, &QObject::destroyed, this, &RemoteMediaController::stop);
    connect(model, &RadioModel::connectionStateChanged, this, [this](ConnectionState state) {
        if (state != ConnectionState::Connected) {
            // The daemon retires FFT production when the radio disconnects,
            // even if this authenticated station session remains connected.
            // Retire our observations too so identical settings resubscribe.
            QList<quint32> endpoints;
            for (const auto& [id, binding] : d->bindings) { endpoints.append(id); }
            d->ctunStreams.clear();
            if (!retireSubscriptions(endpoints)) { return; }
            requestAudio();
        } else {
            refreshSubscriptions();
            requestAudio();
        }
    });
    if (client && client->mediaAvailable()) {
        start();
    }
    refreshAudioStatus();
}

RemoteMediaController::~RemoteMediaController()
{
    // Nothing observes a status change while this controller is destroyed.
    const QSignalBlocker blocker(this);
    stop();
}
quint64 RemoteMediaController::receivedDisplayFrames() const { return d->frames; }
int RemoteMediaController::activeEndpointCount() const { return int(d->bindings.size()); }
std::optional<MediaPeerTelemetry> RemoteMediaController::trafficTelemetry() const
{
    return d->peer ? d->peer->telemetry() : std::nullopt;
}
RemoteAudioReceiverTelemetry RemoteMediaController::audioTelemetry() const
{
    return d->audio->telemetry();
}
std::optional<RemoteAudioContextMessage> RemoteMediaController::acceptedAudioContext() const
{
    return d->acceptedAudioContext;
}
bool RemoteMediaController::audioDetailNegotiated() const
{
    return d->client && d->client->remoteAudioStatusAvailable();
}
RemoteAudioStatus RemoteMediaController::audioStatus() const
{
    return d->audioStatus;
}

void RemoteMediaController::retryAudio()
{
    if (!d->peer || !d->model || d->model->audioEngine()->masterMuted()) { return; }
    requestAudio();
}

void RemoteMediaController::refreshAudioStatus()
{
    const RemoteAudioReceiverTelemetry playback = d->audio->telemetry();
    if (d->audioFailure
        && remoteAudioFailureRecovered(*d->audioFailure, d->epoch, d->connectionId,
                                       d->acceptedAudioContext, playback)) {
        d->audioFailure.reset();
    }
    RemoteAudioStatusInputs inputs;
    inputs.mediaSession = !d->peer.isNull();
    inputs.muted = d->model && d->model->audioEngine()->masterMuted();
    inputs.radioConnected = d->model && d->model->isConnected();
    inputs.context = d->acceptedAudioContext;
    inputs.receiverRunning = playback.running;
    inputs.playing = playback.running && playback.decodedPackets > 0
        && playback.lastDeviceProgressAgeMs
        && *playback.lastDeviceProgressAgeMs < kPlaybackProgressWindowMs;
    inputs.restarting = d->audioRestarting;
    if (d->audioFailure) { inputs.problem = d->audioFailure->fault; }

    RemoteAudioStatus status;
    status.state = deriveRemoteAudioState(inputs);
    status.detailNegotiated = audioDetailNegotiated();
    if (d->acceptedAudioContext) { status.encoder = d->acceptedAudioContext->encoder; }
    status.selectedOutput = d->selectedOutput;
    status.problem = inputs.problem;
    status.retryAvailable = inputs.mediaSession && !inputs.muted
        && (status.state == RemoteAudioStatus::State::PlaybackProblem
            || status.state == RemoteAudioStatus::State::CoreCouldNotStart);

    // Speaker progress and recovery are observed, not signalled, so poll
    // them while there is something to watch, and only then.
    const bool watch = playback.running || d->audioFailure.has_value();
    if (watch && !d->audioStatusTimer->isActive()) {
        d->audioStatusTimer->start();
    } else if (!watch && d->audioStatusTimer->isActive()) {
        d->audioStatusTimer->stop();
    }
    if (status == d->audioStatus) { return; }
    d->audioStatus = status;
    emit audioStatusChanged();
}

void RemoteMediaController::stop()
{
    d->ps3Generation = 0;
    d->ps3Assembler.reset(0);
    d->timer->stop();
    d->audio->stop();
    d->audioEnabled = false;
    d->audioRetryPending = false;
    d->audioRevision = 0;
    d->audioGeneration = 0;
    d->acceptedAudioContext.reset();
    // A playback problem belongs to its session and ends with it.
    d->audioFailure.reset();
    d->audioRestarting = false;
    d->connectionId.clear();
    d->pendingPs3.reset();
    d->ps3Refused = false;
    d->ps3RefusalReason.clear();
    d->ps3RefusedGeneration = 0;
    d->accountedPs3 = false;
    d->allocationCacheIdentity.clear();
    d->cachedAllocation.reset();
    d->cachedAllocationError.clear();
    if (d->peer) {
        MediaPeer* old = d->peer;
        d->peer = nullptr;
        disconnect(old, nullptr, this, nullptr);
        old->stop();
        old->deleteLater();
    }
    QList<QPair<QPointer<SpectrumWidget>, QString>> retiredWidgets;
    retiredWidgets.reserve(static_cast<qsizetype>(d->bindings.size()));
    for (const auto& [id, binding] : d->bindings) {
        retiredWidgets.append({binding.widget, binding.panId});
    }
    d->bindings.clear();
    d->ctunStreams.clear();
    const QPointer<RemoteMediaController> self(this);
    refreshAudioStatus();
    if (!self) { return; }
    for (const auto& [widget, panId] : retiredWidgets) {
        if (widget) {
            widget->clearRemoteSpectrum();
            if (!self || !widget) { return; }
            widget->applyRemoteCtunState(false, false);
            if (!self) { return; }
        }
        if (!panId.isEmpty()) {
            setPanStatus(panId, QString());
            if (!self) { return; }
        }
    }
}

void RemoteMediaController::requestRecovery(quint32 expectedEpoch, const QString& reason)
{
    if (d->recoveryRequested) { return; }
    d->recoveryRequested = true;
    QPointer<RemoteMediaController> self(this);
    stop();
    if (!self) { return; }
    emit self->errorOccurred(reason);
    // A diagnostic consumer may synchronously destroy this controller (or
    // its StationClient parent). Never continue through a deleted sender.
    if (!self) { return; }
    emit self->recoveryRequested(expectedEpoch, reason);
}

void RemoteMediaController::start()
{
    // stop() reports the retired audio status, and a listener may retire
    // this controller in turn.
    const QPointer<RemoteMediaController> self(this);
    stop();
    if (!self) { return; }
    if (!d->client || !d->client->mediaAvailable()) {
        return;
    }
    d->recoveryRequested = false;
    d->epoch = d->client->sessionEpoch();
    d->desiredPs3 = d->model && d->model->pureSignalFacade()
        && d->model->pureSignalFacade()->ampViewSubscribed();
    d->accountedPs3 = d->client->remotePs3DisplaySubscribed();
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
    connect(peer, &MediaPeer::rtpReceived, this, [this, current](const QByteArray& packet) {
        if (current() && d->audioEnabled) { d->audio->submit(packet); }
    });
    connect(peer, &MediaPeer::ready, this, [this, current] {
        if (current()) {
            if (!d->client->remoteDisplayBudgetLimits()) {
                qCDebug(lcRemoteMedia)
                    << "Core supplied no aggregate display limits; using per-display subscriptions";
            }
            d->timer->start();
            refreshSubscriptions();
            requestAudio();
        }
    });
    connect(peer, &MediaPeer::connectionFailed, this,
            [this, current, epoch](const QString& reason) {
        if (current()) {
            requestRecovery(epoch, reason);
        }
    });
    connect(peer, &MediaPeer::closed, this, [this, current, epoch] {
        if (current()) {
            requestRecovery(epoch, QStringLiteral("Station media connection closed"));
        }
    });
    connect(peer, &MediaPeer::errorOccurred, this, [this, current](const QString& reason) {
        if (current()) {
            const QPointer<RemoteMediaController> self(this);
            stop();
            if (!self) { return; }
            emit errorOccurred(reason);
        }
    });
    if (!peer->start(IMediaTransport::Role::Answerer, d->connectionId)) {
        stop();
        return;
    }
    send({{QStringLiteral("op"), QStringLiteral("start")}});
    if (!self) { return; }
    // A media session now exists: audio is awaited from Core.
    refreshAudioStatus();
}

bool RemoteMediaController::send(QJsonObject payload)
{
    if (!d->client || d->connectionId.isEmpty()) { return false; }
    payload.insert(QStringLiteral("connectionId"), d->connectionId);
    return d->client->sendMediaControl(payload, d->epoch);
}

bool RemoteMediaController::retireSubscriptions(const QList<quint32>& endpointIds)
{
    const bool budgetMode = d->client && d->client->remoteDisplayBudgetLimits().has_value();
    const QPointer<RemoteMediaController> self(this);
    const QPointer<MediaPeer> peer = d->peer;
    const quint32 epoch = d->epoch;
    const QString connectionId = d->connectionId;
    for (quint32 id : endpointIds) {
        auto found = d->bindings.find(id);
        if (found == d->bindings.end()) { continue; }
        if (budgetMode) {
            QPointer<SpectrumWidget> widget = found->second.widget;
            QObject::disconnect(found->second.ctunGesture);
            QObject::disconnect(found->second.centreGesture);
            found->second.ctunGesture = {};
            found->second.centreGesture = {};
            found->second.widget = nullptr;
            found->second.slice = nullptr;
            found->second.retiring = true;
            found->second.suspending = false;
            if (widget) {
                widget->clearRemoteSpectrum();
                if (!self || !widget) { return false; }
                widget->applyRemoteCtunState(false, false);
                if (!self) { return false; }
            }
            found = d->bindings.find(id);
            if (found == d->bindings.end()) { continue; }
            if (found->second.pending) {
                continue;
            }
            if (found->second.acceptedRevision == 0) {
                d->bindings.erase(found);
                continue;
            }
            ++found->second.revision;
            if (found->second.revision == 0) { ++found->second.revision; }
            const quint32 revision = found->second.revision;
            found->second.pending = Private::Binding::Pending{
                Private::Binding::PendingKind::Unsubscribe, revision, {}, {},
                QStringLiteral("retire"), d->allocationClock(), false};
            send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                  {QStringLiteral("endpointId"), static_cast<qint64>(id)},
                  {QStringLiteral("revision"), static_cast<qint64>(revision)}});
            if (!self || d->peer != peer || d->epoch != epoch
                || d->connectionId != connectionId) { return false; }
            continue;
        }
        QPointer<SpectrumWidget> widget = found->second.widget;
        // Retire local ownership before sending: a synchronous transport
        // failure can end the session and clear every binding inside send().
        d->bindings.erase(found);
        if (widget) {
            widget->clearRemoteSpectrum();
            if (!self || !widget) { return false; }
            widget->applyRemoteCtunState(false, false);
            if (!self) { return false; }
        }
        send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
              {QStringLiteral("endpointId"), double(id)}});
        if (!self || d->peer != peer || d->epoch != epoch
            || d->connectionId != connectionId) { return false; }
    }
    return true;
}

void RemoteMediaController::refreshSubscriptions()
{
    if (!d->client || !d->client->mediaAvailable() || d->epoch != d->client->sessionEpoch()
        || !d->peer || !d->peer->isReady() || !d->model || !d->stack) {
        return;
    }
    if (!d->model->isConnected()) { return; }
    if (d->client->remoteDisplayBudgetLimits()) {
        refreshBudgetSubscriptions();
        return;
    }
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        if (applet) {
            // This supported subscription mode does not indicate a display
            // failure or require operator action. Keep capability details in
            // diagnostics, and clear any status from a previous allocation.
            applet->setRemoteDisplayStatus(QString());
        }
    }
    const QPointer<RemoteMediaController> self(this);
    const QPointer<MediaPeer> peer = d->peer;
    const quint32 epoch = d->epoch;
    const QString connectionId = d->connectionId;
    const auto current = [this, self, peer, epoch, connectionId] {
        return self && d->peer == peer && d->epoch == epoch
            && d->connectionId == connectionId && d->stack && d->model;
    };
    struct Desired {
        QString panId;
        QPointer<SpectrumWidget> widget;
        QPointer<SliceModel> slice;
        QJsonObject request;
    };
    QList<Desired> desired;
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        SpectrumWidget* widget = applet->spectrumWidget();
        SliceModel* slice = d->model->sliceById(applet->activeSliceIndex());
        // The stack's membership is pane intent. Float/dock and layout
        // rebuilding temporarily hide the same renderer without disabling it.
        if (!widget || !slice || slice->streamIndex() < 0) { continue; }
        QJsonObject request = requestFor(widget, slice,
                                         d->client->remoteWidebandAvailable());
        if (!request.isEmpty()) {
            desired.append({applet->panId(), widget, slice, std::move(request)});
        }
    }
    // Retire obsolete bindings before creating any replacement. In particular,
    // a global FFT-window change must release every old shared-source window
    // first; updating them individually would reject each against its peers.
    // The reliable control stream preserves all unsubscriptions before adds.
    QHash<SpectrumWidget*, double> retainedSourceCentres;
    QList<quint32> retiredEndpoints;
    for (auto it = d->bindings.begin(); it != d->bindings.end(); ++it) {
        const auto next = std::find_if(desired.cbegin(), desired.cend(),
            [&it](const Desired& item) {
                return item.widget == it->second.widget && item.slice == it->second.slice;
            });
        const bool windowChanged = next != desired.cend()
            && next->request.value(QStringLiteral("windowType"))
                != it->second.observed.value(QStringLiteral("windowType"));
        if (next != desired.cend() && !windowChanged) { continue; }
        // A cohost selection changes the endpoint's slice identity, not its
        // physical receive window. Retain that accepted geometry for an
        // immediate rejected gesture while the replacement awaits its FFT.
        for (const Desired& item : desired) {
            if (item.widget == it->second.widget && it->second.sourceCentreHz > 0
                && item.slice->streamIndex() == it->second.observedStream
                && item.slice->streamEpoch() == it->second.observedStreamEpoch) {
                retainedSourceCentres.insert(item.widget, it->second.sourceCentreHz);
            }
        }
        retiredEndpoints.append(it->first);
    }
    if (!retireSubscriptions(retiredEndpoints) || !current()) { return; }
    for (const Desired& item : desired) {
        SpectrumWidget* widget = item.widget;
        SliceModel* slice = item.slice;
        if (!widget || !slice || currentSliceForPan(d->model, d->stack, widget) != slice) {
            continue;
        }
        auto found = std::find_if(d->bindings.begin(), d->bindings.end(),
            [widget, slice](const auto& entry) {
                return entry.second.widget == widget && entry.second.slice == slice;
            });
        if (found == d->bindings.end()) {
            if (d->bindings.size() >= kMaxEndpoints || d->nextEndpoint == 0) { continue; }
            const quint32 id = d->nextEndpoint++;
            found = d->bindings.try_emplace(id).first;
            found->second.panId = item.panId;
            found->second.widget = widget;
            found->second.slice = slice;
            found->second.sourceCentreHz = retainedSourceCentres.value(widget, 0);
            auto& binding = found->second;
            // Per-pan sender and gesture-time slice lookup: selection may
            // have changed since the last subscription poll. Never capture
            // its former slice as the command target.
            SpectrumWidget* const sw = item.widget;
            binding.ctunGesture = connect(sw, &SpectrumWidget::ctunEnabledChanged,
                this, [this, id](bool pinned) {
                    auto current = d->bindings.find(id);
                    if (current == d->bindings.end() || !current->second.slice
                        || !d->model
                        || !d->client || !d->client->remoteCtunAvailable()) { return; }
                    SliceModel* slice = currentSliceForPan(
                        d->model, d->stack, current->second.widget);
                    if (!slice || slice->streamIndex() < 0) { return; }
                    auto& state = d->ctunStreams[slice->streamIndex()];
                    state.epoch = slice->streamEpoch();
                    state.requestSliceId = slice->sliceIndex();
                    state.requestedPin = pinned;
                    state.pending = true;
                    state.initialized = false;
                    state.rejectedContext = 0;
                    if (!d->model->requestStreamCtunPinned(slice->sliceIndex(), pinned)) {
                        state.pending = false;
                        state.rejectedContext = current->second.context.codec.contextGeneration;
                    }
                });
            binding.centreGesture = connect(sw, &SpectrumWidget::centerChanged,
                this, [this, id](double centreHz) {
                    auto current = d->bindings.find(id);
                    if (current == d->bindings.end() || !current->second.slice
                        || !current->second.widget
                        || !current->second.widget->ctunEnabled() || !d->model
                        || !d->client || !d->client->remoteCtunAvailable()) { return; }
                    SliceModel* slice = currentSliceForPan(
                        d->model, d->stack, current->second.widget);
                    if (!slice || slice->streamIndex() < 0) { return; }
                    d->model->requestStreamCentre(slice->sliceIndex(), std::round(centreHz));
            });
            widget->clearRemoteSpectrum();
            if (!current() || !widget) { return; }
            widget->applyRemoteCtunState(false, false);
            if (!current()) { return; }
        }
        const quint32 id = found->first;
        auto& binding = found->second;
        QJsonObject request = item.request;
        // observed is the last attempted geometry, not proof of acceptance.
        // A rejected request remains blank until its inputs change; reconnect
        // retires the binding, and source-window changes are batched above.
        if (request == binding.observed
            && binding.observedStream == slice->streamIndex()
            && binding.observedStreamEpoch == slice->streamEpoch()) { continue; }
        binding.observed = request;
        binding.observedStream = slice->streamIndex();
        binding.observedStreamEpoch = slice->streamEpoch();
        ++binding.revision;
        if (binding.revision == 0) { ++binding.revision; }
        binding.accepted = false;
        binding.rejected = false;
        binding.decoder.reset();
        // A tune/zoom renews this binding's codec, not its painted history.
        // New/replaced bindings were fully cleared above; rejection and
        // session retirement still clear them through their lifecycle paths.
        widget->invalidateRemoteSpectrumFrame();
        if (!current()) { return; }
        request.insert(QStringLiteral("op"), QStringLiteral("subscribe"));
        request.insert(QStringLiteral("endpointId"), double(id));
        const quint32 revision = binding.revision;
        request.insert(QStringLiteral("revision"), double(revision));
        const bool sent = send(request);
        if (!current()) { return; }
        // An observer can retire or renew this endpoint during the send.
        // Never retain a Binding reference across that callback boundary.
        found = d->bindings.find(id);
        if (!sent && found != d->bindings.end() && found->second.revision == revision) {
            found->second.observed = {};
        }
    }
    refreshCtunState();
}

void RemoteMediaController::setPanStatus(const QString& panId, const QString& status)
{
    if (!d->stack || panId.isEmpty()) { return; }
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        if (applet && applet->panId() == panId) {
            applet->setRemoteDisplayStatus(status);
            return;
        }
    }
}

void RemoteMediaController::refreshBudgetSubscriptions()
{
    if (d->refreshingBudget) {
        d->budgetReplanRequested = true;
        return;
    }
    const QPointer<RemoteMediaController> self(this);
    d->refreshingBudget = true;
    d->budgetReplanRequested = false;
    const auto refreshGuard = qScopeGuard([this, self] {
        if (!self) { return; }
        d->refreshingBudget = false;
        if (d->budgetReplanRequested) {
            QTimer::singleShot(0, self, &RemoteMediaController::refreshSubscriptions);
        }
    });

    if (!d->client || !d->model || !d->stack || !d->peer || !d->peer->isReady()) {
        return;
    }
    const auto limits = d->client->remoteDisplayBudgetLimits();
    if (!limits) { return; }
    const qint64 now = d->allocationClock();

    for (auto& [id, binding] : d->bindings) {
        if (binding.pending && !binding.pending->timedOut
            && now - binding.pending->sentAtMs >= d->allocationAckTimeoutMs) {
            binding.pending->timedOut = true;
            qCWarning(lcRemoteMedia) << "Remote display allocation acknowledgement stalled"
                                    << id << binding.pending->revision;
        }
    }
    if (d->pendingPs3 && !d->pendingPs3->timedOut
        && now - d->pendingPs3->sentAtMs >= d->allocationAckTimeoutMs) {
        d->pendingPs3->timedOut = true;
        qCWarning(lcRemoteMedia) << "Remote PureSignal display acknowledgement stalled"
                                << d->pendingPs3->commandId;
    }
    if (!d->desiredPs3 && d->accountedPs3 && !d->pendingPs3) {
        const QPointer<StationClient> requestClient = d->client;
        const quint32 requestEpoch = d->epoch;
        const quint32 commandId = requestClient->requestPs3DisplaySubscription(false);
        if (!self || d->client != requestClient || d->epoch != requestEpoch) { return; }
        if (commandId == 0 && !d->pendingPs3) {
            d->ps3RefusalReason = QStringLiteral("Unable to request PureSignal display release.");
        }
        return;
    }

    struct Desired {
        QString panId;
        QPointer<SpectrumWidget> widget;
        QPointer<SliceModel> slice;
        QJsonObject original;
        RemoteDisplayIntent intent;
    };
    QList<Desired> desired;
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        if (!applet) { continue; }
        SpectrumWidget* widget = applet->spectrumWidget();
        SliceModel* slice = d->model->sliceById(applet->activeSliceIndex());
        if (!widget || !slice || slice->streamIndex() < 0) { continue; }
        QJsonObject original = requestFor(widget, slice,
                                          d->client->remoteWidebandAvailable());
        if (original.isEmpty()) { continue; }
        RemoteDisplayIntent intent;
        intent.panId = applet->panId();
        intent.pixels = original.value(QStringLiteral("pixels")).toInt();
        intent.fps = original.value(QStringLiteral("fps")).toInt();
        intent.includeWidePlane = original.value(QStringLiteral("wideSpanFactor")).toDouble() > 1.0;
        intent.waterfallPeriodMs = qBound(1, widget->wfUpdatePeriodMs(), 65'535);
        intent.active = intent.panId == d->stack->activePanId();
        desired.append({intent.panId, widget, slice, std::move(original), intent});
    }

    // Refuse an impossible PS3 enable before any pan operation can be
    // attributed to that request. Existing pan allocations remain intact.
    if (d->desiredPs3 && !d->accountedPs3 && !d->pendingPs3 && !d->ps3Refused) {
        QString reason;
        if (!allocateRemoteDisplay(*limits, {}, true, &reason)) {
            d->ps3Refused = true;
            d->ps3RefusalReason = reason;
            d->ps3RefusedGeneration = limits->generation;
        }
    }

    QList<quint32> obsolete;
    for (const auto& [id, binding] : d->bindings) {
        if (binding.retiring || binding.suspending) { continue; }
        const auto item = std::find_if(desired.cbegin(), desired.cend(),
            [&binding](const Desired& candidate) {
                return candidate.panId == binding.panId
                    && candidate.widget == binding.widget
                    && candidate.slice == binding.slice;
            });
        if (item == desired.cend()) { obsolete.append(id); }
    }
    if (!obsolete.isEmpty()) {
        retireSubscriptions(obsolete);
        return;
    }

    QList<quint32> incompatibleWindows;
    for (const auto& [id, binding] : d->bindings) {
        if (binding.retiring || binding.suspending || binding.acceptedRevision == 0
            || binding.acceptedRequest.isEmpty()) { continue; }
        const auto item = std::find_if(desired.cbegin(), desired.cend(),
            [&binding](const Desired& candidate) {
                return candidate.panId == binding.panId
                    && candidate.widget == binding.widget
                    && candidate.slice == binding.slice;
            });
        if (item != desired.cend()
            && item->original.value(QStringLiteral("windowType"))
                != binding.acceptedRequest.value(QStringLiteral("windowType"))) {
            incompatibleWindows.append(id);
        }
    }
    if (!incompatibleWindows.isEmpty()) {
        const QPointer<MediaPeer> peer = d->peer;
        const quint32 epoch = d->epoch;
        const QString connectionId = d->connectionId;
        for (quint32 endpointId : incompatibleWindows) {
            auto found = d->bindings.find(endpointId);
            if (found == d->bindings.end()) { continue; }
            Private::Binding& binding = found->second;
            binding.suspending = true; // Preserve the widget/history across the release.
            setPanStatus(binding.panId,
                         QStringLiteral("Display allocation pending: changing source window"));
            if (!self) { return; }
            if (binding.pending) { continue; }
            ++binding.revision;
            if (binding.revision == 0) { ++binding.revision; }
            const quint32 revision = binding.revision;
            binding.pending = Private::Binding::Pending{
                Private::Binding::PendingKind::Unsubscribe, revision, {}, {},
                QStringLiteral("source-window:%1").arg(limits->generation),
                now, false};
            send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                  {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
                  {QStringLiteral("revision"), static_cast<qint64>(revision)}});
            if (!self || d->peer != peer || d->epoch != epoch
                || d->connectionId != connectionId) { return; }
        }
        return;
    }

    QList<RemoteDisplayIntent> intents;
    intents.reserve(desired.size());
    for (const Desired& item : desired) { intents.append(item.intent); }

    const bool targetPs3 = d->accountedPs3
        || (d->desiredPs3 && !d->ps3Refused);
    const bool retainedPs3ExceedsCap = d->accountedPs3
        && !displayChargeFits(*limits, ps3DisplayCharge());
    const QString cacheIdentity = allocationIdentity(
        *limits, intents, targetPs3, retainedPs3ExceedsCap);
    if (d->allocationCacheIdentity != cacheIdentity) {
        d->allocationCacheIdentity = cacheIdentity;
        d->cachedAllocation.reset();
        d->cachedAllocationError.clear();
        if (retainedPs3ExceedsCap) {
            RemoteDisplayAllocation paused;
            paused.total = ps3DisplayCharge();
            for (const RemoteDisplayIntent& intent : intents) {
                RemoteDisplayQuality quality;
                quality.panId = intent.panId;
                quality.suspended = true;
                paused.pans.append(quality);
            }
            d->cachedAllocation = paused;
        } else {
            d->cachedAllocation = allocateRemoteDisplay(
                *limits, intents, targetPs3, &d->cachedAllocationError);
        }
    }
    const std::optional<RemoteDisplayAllocation>& allocation = d->cachedAllocation;
    if (!allocation) {
        const QString status = QStringLiteral("Display allocation refused: %1")
            .arg(d->cachedAllocationError.left(384));
        for (const Desired& item : desired) { setPanStatus(item.panId, status); }
        return;
    }

    QHash<QString, RemoteDisplayQuality> qualities;
    for (const RemoteDisplayQuality& quality : allocation->pans) {
        qualities.insert(quality.panId, quality);
    }

    struct Candidate {
        quint32 endpointId = 0;
        QJsonObject request;
        DisplayBudgetCharge charge;
        QString identity;
        bool unsubscribe = false;
        bool reduction = false;
    };
    QList<Candidate> reductions;
    QList<Candidate> increases;
    QList<quint32> eraseUnaccepted;

    const auto statusFor = [now](const Private::Binding& binding,
                                  const Desired& item) {
        if (binding.pending && binding.pending->timedOut) {
            return QStringLiteral("Display allocation stalled: no Core acknowledgement");
        }
        if (!binding.refusalReason.isEmpty()) {
            return QStringLiteral("Display allocation refused: %1")
                .arg(binding.refusalReason.left(384));
        }
        if (binding.acceptedRevision == 0 || binding.acceptedRequest.isEmpty()) {
            return QStringLiteral("Display allocation pending");
        }
        const int pixels = binding.acceptedRequest.value(QStringLiteral("pixels")).toInt();
        const int fps = binding.acceptedRequest.value(QStringLiteral("fps")).toInt();
        QString status = QStringLiteral("Display target %1 px @ %2 fps").arg(pixels).arg(fps);
        int firstRecent = 0;
        while (firstRecent < binding.receivedFrameTimesMs.size()
               && binding.receivedFrameTimesMs.at(firstRecent) < now - 2'000) {
            ++firstRecent;
        }
        const int recentCount = binding.receivedFrameTimesMs.size() - firstRecent;
        if (recentCount >= 2 && now - binding.receivedFrameTimesMs.constLast() <= 1'000
            && binding.receivedFrameTimesMs.constLast()
                > binding.receivedFrameTimesMs.at(firstRecent)) {
            const double received = double(recentCount - 1) * 1000.0
                / double(binding.receivedFrameTimesMs.constLast()
                         - binding.receivedFrameTimesMs.at(firstRecent));
            status += QStringLiteral("; received %1 fps").arg(received, 0, 'f', 1);
        }
        const int requestedPixels = item.original.value(QStringLiteral("pixels")).toInt();
        const int requestedFps = item.original.value(QStringLiteral("fps")).toInt();
        if (pixels != requestedPixels || fps != requestedFps) {
            status += QStringLiteral(" (requested %1 px @ %2 fps; Core capacity)")
                .arg(requestedPixels).arg(requestedFps);
        }
        if (binding.acceptedRequest.value(QStringLiteral("wideSpanFactor")).toDouble() > 1.0) {
            status += QStringLiteral("; WIDE plane reserved");
        }
        return status;
    };

    for (const Desired& item : desired) {
        const RemoteDisplayQuality quality = qualities.value(item.panId);
        auto found = std::find_if(d->bindings.begin(), d->bindings.end(),
            [&item](const auto& entry) {
                return !entry.second.retiring && entry.second.panId == item.panId
                    && entry.second.widget == item.widget && entry.second.slice == item.slice;
            });
        if (quality.suspended) {
            const QString suspendIdentity = QStringLiteral("suspend:%1").arg(limits->generation);
            if (found != d->bindings.end()
                && found->second.refusedIdentity == suspendIdentity) {
                setPanStatus(item.panId,
                    QStringLiteral("Display allocation refused: %1")
                        .arg(found->second.refusalReason.left(384)));
                continue;
            }
            setPanStatus(item.panId, retainedPs3ExceedsCap
                ? QStringLiteral("Display paused: Core capacity; accepted PureSignal reservation exceeds current cap")
                : QStringLiteral("Display paused: Core capacity"));
            if (!self) { return; }
            if (found == d->bindings.end()) { continue; }
            found->second.suspending = true;
            found->second.refusalReason.clear();
            if (found->second.pending) { continue; }
            if (found->second.acceptedRevision == 0) {
                eraseUnaccepted.append(found->first);
                continue;
            }
            reductions.append({found->first, {}, {},
                               suspendIdentity,
                               true, true});
            continue;
        }

        if (found == d->bindings.end()) {
            if (d->bindings.size() >= kMaxEndpoints || d->nextEndpoint == 0) {
                setPanStatus(item.panId,
                    QStringLiteral("Display allocation stalled: endpoint ledger full"));
                continue;
            }
            const quint32 endpointId = d->nextEndpoint++;
            found = d->bindings.try_emplace(endpointId).first;
            Private::Binding& binding = found->second;
            binding.panId = item.panId;
            binding.widget = item.widget;
            binding.slice = item.slice;
            SpectrumWidget* const sw = item.widget;
            binding.ctunGesture = connect(sw, &SpectrumWidget::ctunEnabledChanged,
                this, [this, endpointId](bool pinned) {
                    auto current = d->bindings.find(endpointId);
                    if (current == d->bindings.end() || !current->second.slice
                        || !d->model || !d->client || !d->client->remoteCtunAvailable()) { return; }
                    SliceModel* slice = currentSliceForPan(
                        d->model, d->stack, current->second.widget);
                    if (!slice || slice->streamIndex() < 0) { return; }
                    auto& state = d->ctunStreams[slice->streamIndex()];
                    state.epoch = slice->streamEpoch();
                    state.requestSliceId = slice->sliceIndex();
                    state.requestedPin = pinned;
                    state.pending = true;
                    state.initialized = false;
                    state.rejectedContext = 0;
                    if (!d->model->requestStreamCtunPinned(slice->sliceIndex(), pinned)) {
                        state.pending = false;
                        state.rejectedContext = current->second.context.codec.contextGeneration;
                    }
                });
            binding.centreGesture = connect(sw, &SpectrumWidget::centerChanged,
                this, [this, endpointId](double centreHz) {
                    auto current = d->bindings.find(endpointId);
                    if (current == d->bindings.end() || !current->second.slice
                        || !current->second.widget || !current->second.widget->ctunEnabled()
                        || !d->model || !d->client
                        || !d->client->remoteCtunAvailable()) { return; }
                    SliceModel* slice = currentSliceForPan(
                        d->model, d->stack, current->second.widget);
                    if (slice && slice->streamIndex() >= 0) {
                        d->model->requestStreamCentre(slice->sliceIndex(), std::round(centreHz));
                    }
                });
            sw->invalidateRemoteSpectrumFrame();
            if (!self || !sw) { return; }
            sw->applyRemoteCtunState(false, false);
            if (!self) { return; }
            found = d->bindings.find(endpointId);
            if (found == d->bindings.end()) { return; }
        }

        Private::Binding& binding = found->second;
        binding.suspending = false;
        binding.desiredOriginal = item.original;
        const QJsonObject target = allocatedRequest(item.original, quality);
        const QString identity = requestIdentity(target, limits->generation, targetPs3);
        if (!binding.refusedIdentity.isEmpty() && binding.refusedIdentity != identity) {
            binding.refusedIdentity.clear();
            binding.refusalReason.clear();
        }
        QString status = statusFor(binding, item);
        if (d->ps3Refused && !d->ps3RefusalReason.isEmpty()) {
            status += QStringLiteral("; PureSignal display refused: %1")
                .arg(d->ps3RefusalReason.left(256));
        } else if (d->pendingPs3 && d->pendingPs3->timedOut) {
            status += QStringLiteral("; PureSignal display allocation stalled");
        }
        setPanStatus(item.panId, status);
        if (!self) { return; }
        if (binding.pending || binding.refusedIdentity == identity) { continue; }
        if (binding.acceptedRevision != 0 && binding.acceptedRequest == target
            && binding.observedStream == item.slice->streamIndex()
            && binding.observedStreamEpoch == item.slice->streamEpoch()) {
            continue;
        }
        Candidate candidate{found->first, target, quality.charge, identity, false,
                            binding.acceptedRevision != 0
                                && nonIncreasing(quality.charge, binding.acceptedCharge)};
        (candidate.reduction ? reductions : increases).append(std::move(candidate));
    }

    for (quint32 endpointId : eraseUnaccepted) { d->bindings.erase(endpointId); }

    const QPointer<MediaPeer> peer = d->peer;
    const quint32 epoch = d->epoch;
    const QString connectionId = d->connectionId;
    const auto current = [this, self, peer, epoch, connectionId] {
        return self && d->peer == peer && d->epoch == epoch
            && d->connectionId == connectionId;
    };
    const auto sendCandidate = [this, &current](const Candidate& candidate) {
        auto found = d->bindings.find(candidate.endpointId);
        if (found == d->bindings.end() || found->second.pending) { return; }
        Private::Binding& binding = found->second;
        ++binding.revision;
        if (binding.revision == 0) { ++binding.revision; }
        const quint32 revision = binding.revision;
        binding.pending = Private::Binding::Pending{
            candidate.unsubscribe ? Private::Binding::PendingKind::Unsubscribe
                                  : Private::Binding::PendingKind::Subscribe,
            revision, candidate.request, candidate.charge,
            candidate.identity, d->allocationClock(), false};
        QJsonObject wire = candidate.request;
        wire.insert(QStringLiteral("op"), candidate.unsubscribe
            ? QStringLiteral("unsubscribe") : QStringLiteral("subscribe"));
        wire.insert(QStringLiteral("endpointId"), static_cast<qint64>(candidate.endpointId));
        wire.insert(QStringLiteral("revision"), static_cast<qint64>(revision));
        const bool sent = send(wire);
        if (!current()) { return; }
        found = d->bindings.find(candidate.endpointId);
        if (!sent && found != d->bindings.end() && found->second.pending
            && found->second.pending->revision == revision) {
            found->second.pending.reset();
            found->second.refusedIdentity = candidate.identity;
            found->second.refusalReason = QStringLiteral("Unable to send the allocation request.");
        }
    };

    if (!reductions.isEmpty()) {
        for (const Candidate& candidate : reductions) {
            sendCandidate(candidate);
            if (!current()) { return; }
        }
        return;
    }

    bool endpointPending = false;
    for (const auto& [id, binding] : d->bindings) {
        if (binding.pending) { endpointPending = true; break; }
    }
    if (endpointPending) { return; }

    if (targetPs3 && !d->accountedPs3 && !d->pendingPs3) {
        QList<DisplayBudgetCharge> confirmed;
        confirmed.append(ps3DisplayCharge());
        for (const auto& [id, binding] : d->bindings) {
            if (binding.acceptedRevision != 0) { confirmed.append(binding.acceptedCharge); }
        }
        const auto total = sumDisplayCharges(confirmed);
        if (total && displayChargeFits(*limits, *total)) {
            const QPointer<RemoteMediaController> requestSelf(this);
            const QPointer<StationClient> requestClient = d->client;
            const quint32 requestEpoch = d->epoch;
            const quint32 commandId = requestClient->requestPs3DisplaySubscription(true);
            if (!requestSelf || d->client != requestClient || d->epoch != requestEpoch) { return; }
            if (commandId == 0 && !d->pendingPs3) {
                d->ps3Refused = true;
                d->ps3RefusalReason = QStringLiteral("Unable to request PureSignal display admission.");
                d->ps3RefusedGeneration = limits->generation;
            }
        }
        return;
    }
    if (d->pendingPs3) { return; }

    for (const Candidate& candidate : increases) {
        QList<DisplayBudgetCharge> potential;
        if (d->accountedPs3) { potential.append(ps3DisplayCharge()); }
        for (const auto& [id, binding] : d->bindings) {
            DisplayBudgetCharge charge = binding.acceptedCharge;
            if (id == candidate.endpointId) {
                charge = maximumCharge(charge, candidate.charge);
            } else if (binding.pending) {
                charge = maximumCharge(charge, binding.pending->charge);
            }
            if (!zeroCharge(charge)) { potential.append(charge); }
        }
        const auto total = sumDisplayCharges(potential);
        if (!total || !displayChargeFits(*limits, *total)) { continue; }
        sendCandidate(candidate);
        if (!current()) { return; }
    }
}

void RemoteMediaController::refreshCtunState()
{
    if (!d->model || !d->client || !d->stack) { return; }
    const QPointer<RemoteMediaController> self(this);
    QSet<int> occupied;
    for (SliceModel* slice : d->model->slices()) {
        if (slice->streamIndex() >= 0) { occupied.insert(slice->streamIndex()); }
    }
    for (auto it = d->ctunStreams.begin(); it != d->ctunStreams.end();) {
        if (!occupied.contains(it.key())) { it = d->ctunStreams.erase(it); }
        else { ++it; }
    }
    QList<quint32> endpointIds;
    for (const auto& [id, binding] : d->bindings) { endpointIds.append(id); }
    for (quint32 id : endpointIds) {
        auto found = d->bindings.find(id);
        if (found == d->bindings.end()) { continue; }
        Private::Binding& binding = found->second;
        if (!binding.widget || !binding.slice) { continue; }
        const int stream = binding.slice->streamIndex();
        const quint64 epoch = binding.slice->streamEpoch();
        auto& state = d->ctunStreams[stream];
        if (state.epoch != epoch) { state = {}; state.epoch = epoch; }
        const bool currentContext = binding.accepted
            && binding.context.source.streamIndex == stream
            && binding.observedStreamEpoch == epoch;
        const bool available = d->client->remoteCtunAvailable()
            && d->model->isConnected() && stream >= 0 && epoch != 0
            && (state.initialized || state.pending || currentContext);
        if (available && currentContext && !state.initialized && !state.pending
            && (state.rejectedContext == 0
                || isNewerGeneration(binding.context.codec.contextGeneration, state.rejectedContext))) {
            // One hardware stream has one effective pin, even when several
            // pans show it. Restore once; mirrored truth then updates cohosts.
            // Never reassert competing saved preferences on every frame/ACK.
            bool preference = binding.widget->ctunPreference();
            // The active pan owns the initial preference when several pans
            // share a stream; ACK arrival order must not choose the winner.
            for (PanadapterApplet* applet : d->stack->allApplets()) {
                SliceModel* member = d->model->sliceById(applet->activeSliceIndex());
                if (member && member->streamIndex() == stream
                    && applet->panId() == d->stack->activePanId()) {
                    preference = applet->spectrumWidget()->ctunPreference();
                    break;
                }
            }
            state.requestSliceId = binding.slice->sliceIndex();
            state.requestedPin = preference;
            state.pending = true;
            const int requestSliceId = state.requestSliceId;
            const quint32 contextGeneration = binding.context.codec.contextGeneration;
            const bool requested = d->model->requestStreamCtunPinned(
                requestSliceId, preference);
            if (!self) { return; }
            auto currentState = d->ctunStreams.find(stream);
            if (!requested && currentState != d->ctunStreams.end()
                && currentState->epoch == epoch
                && currentState->requestSliceId == requestSliceId) {
                currentState->pending = false;
                currentState->rejectedContext = contextGeneration;
            }
        }
        found = d->bindings.find(id);
        if (found == d->bindings.end() || !found->second.widget
            || !found->second.slice) { continue; }
        QPointer<SpectrumWidget> widget = found->second.widget;
        const bool pinned = found->second.slice->streamCtunPinned();
        widget->applyRemoteCtunState(available, pinned);
        if (!self) { return; }
    }
}

void RemoteMediaController::requestAudio()
{
    d->audio->stop();
    d->audioEnabled = false;
    d->audioRetryPending = false;
    // Every request follows a mute, speaker, radio or retry change (or the
    // media link becoming ready), each of which the status reflects.
    if (!d->peer || !d->peer->isReady() || !d->model || !d->client
        || !d->client->mediaAvailable()) {
        refreshAudioStatus();
        return;
    }
    ++d->audioRevision;
    if (!d->audioRevision) { ++d->audioRevision; }
    d->lastAudioRequestMs = d->clock.elapsed();
    const bool enabled = d->model->isConnected() && !d->model->audioEngine()->masterMuted();
    const QPointer<RemoteMediaController> self(this);
    send({{QStringLiteral("op"), QStringLiteral("audio")},
          {QStringLiteral("revision"), double(d->audioRevision)},
          {QStringLiteral("enabled"), enabled}});
    if (!self) { return; }
    refreshAudioStatus();
}

void RemoteMediaController::receiveAllocationResult(const QJsonObject& payload)
{
    if (payload.size() != 11 || !payload.value(QStringLiteral("accepted")).isBool()
        || !payload.value(QStringLiteral("reason")).isString()) {
        return;
    }
    quint32 endpointId = 0;
    quint32 revision = 0;
    quint32 budgetGeneration = 0;
    quint64 acceptedRevisionValue = 0;
    DisplayBudgetCharge retained;
    quint64 messages = 0;
    if (!uint32(payload, "endpointId", endpointId)
        || !uint32(payload, "revision", revision)
        || !uint32(payload, "budgetGeneration", budgetGeneration)
        || !uint64(payload, "acceptedRevision", std::numeric_limits<quint32>::max(),
                   acceptedRevisionValue)
        || !uint64(payload, "applicationBytesPerSecond",
                   kDisplayBudgetJsonSafePositiveLimit,
                   retained.applicationBytesPerSecond)
        || !uint64(payload, "spectrumSampleUnitsPerSecond",
                   kDisplayBudgetJsonSafePositiveLimit,
                   retained.spectrumSampleUnitsPerSecond)
        || !uint64(payload, "messagesPerSecond", kDisplaySenderMessagesPerSecond,
                   messages)) {
        return;
    }
    retained.messagesPerSecond = static_cast<quint32>(messages);
    (void)budgetGeneration; // Parsed for exact shape; outcomes reconcile by endpoint/revision.
    const quint32 acceptedRevision = static_cast<quint32>(acceptedRevisionValue);
    if ((acceptedRevision == 0) != zeroCharge(retained)
        || (acceptedRevision != 0
            && (retained.applicationBytesPerSecond == 0
                || retained.spectrumSampleUnitsPerSecond == 0
                || retained.messagesPerSecond == 0))) {
        return;
    }
    auto found = d->bindings.find(endpointId);
    if (found == d->bindings.end()) { return; }
    Private::Binding& binding = found->second;
    const bool outcomeAccepted = payload.value(QStringLiteral("accepted")).toBool();
    const QString reason = payload.value(QStringLiteral("reason")).toString().left(512);

    if (!binding.pending || binding.pending->revision != revision) {
        // Core may reconcile a resource to zero after source/radio retirement.
        // A rejection that merely restates the retained current reservation is
        // not the outcome of a newer GUI operation and cannot erase it.
        if (revision == binding.revision
            && acceptedRevision == 0 && zeroCharge(retained)
            && binding.acceptedRevision != 0) {
            const QPointer<RemoteMediaController> self(this);
            QPointer<SpectrumWidget> widget = binding.widget;
            const bool erase = binding.retiring || binding.suspending || !binding.widget;
            binding.acceptedRevision = 0;
            binding.acceptedCharge = {};
            binding.acceptedRequest = {};
            binding.accepted = false;
            binding.contextRevision = 0;
            binding.decoder.reset();
            if (erase) { d->bindings.erase(found); }
            if (widget) { widget->invalidateRemoteSpectrumFrame(); }
            if (!self) { return; }
            refreshSubscriptions();
        }
        return;
    }

    const Private::Binding::Pending pending = *binding.pending;
    if (pending.kind == Private::Binding::PendingKind::Subscribe) {
        if (outcomeAccepted) {
            if (acceptedRevision != revision || retained != pending.charge) { return; }
            binding.acceptedRevision = acceptedRevision;
            binding.acceptedCharge = retained;
            binding.acceptedRequest = pending.request;
            binding.refusedIdentity.clear();
            binding.refusalReason.clear();
            if (binding.slice) {
                binding.observedStream = binding.slice->streamIndex();
                binding.observedStreamEpoch = binding.slice->streamEpoch();
            }
        } else {
            const bool sourceRetired = acceptedRevision == 0 && zeroCharge(retained)
                && binding.acceptedRevision != 0;
            if (!sourceRetired
                && (acceptedRevision != binding.acceptedRevision
                    || retained != binding.acceptedCharge)) { return; }
            binding.refusedIdentity = pending.identity;
            binding.refusalReason = reason.isEmpty()
                ? QStringLiteral("Core refused the display allocation.") : reason;
            if (sourceRetired) {
                const QPointer<RemoteMediaController> self(this);
                QPointer<SpectrumWidget> widget = binding.widget;
                const bool erase = binding.retiring || binding.suspending || !binding.widget;
                binding.acceptedRevision = 0;
                binding.acceptedCharge = {};
                binding.acceptedRequest = {};
                binding.accepted = false;
                binding.contextRevision = 0;
                binding.decoder.reset();
                binding.pending.reset();
                if (erase) { d->bindings.erase(found); }
                if (widget) {
                    widget->invalidateRemoteSpectrumFrame();
                    if (!self || !widget) { return; }
                    widget->applyRemoteCtunState(false, false);
                    if (!self) { return; }
                }
                refreshSubscriptions();
                return;
            }
        }
        binding.pending.reset();
        if (binding.retiring) {
            retireSubscriptions({endpointId});
            return;
        }
    } else {
        const bool sourceRetired = !outcomeAccepted
            && acceptedRevision == 0 && zeroCharge(retained)
            && binding.acceptedRevision != 0;
        if (outcomeAccepted || sourceRetired) {
            if (!sourceRetired && (acceptedRevision != 0 || !zeroCharge(retained))) { return; }
            const QString panId = binding.panId;
            QPointer<SpectrumWidget> widget = binding.widget;
            const bool suspended = binding.suspending;
            const QPointer<RemoteMediaController> self(this);
            binding.pending.reset();
            binding.acceptedRevision = 0;
            binding.acceptedCharge = {};
            binding.acceptedRequest = {};
            binding.accepted = false;
            binding.contextRevision = 0;
            binding.decoder.reset();
            d->bindings.erase(found);
            if (widget) {
                // Capacity suspension freezes painted history. Only reject
                // frames for the released endpoint; do not clear its rows.
                widget->invalidateRemoteSpectrumFrame();
                if (!self || !widget) { return; }
                widget->applyRemoteCtunState(false, false);
                if (!self) { return; }
            }
            if (suspended) {
                setPanStatus(panId, QStringLiteral("Display paused: Core capacity"));
            }
        } else {
            if (acceptedRevision != binding.acceptedRevision
                || retained != binding.acceptedCharge) { return; }
            binding.pending.reset();
            binding.refusedIdentity = pending.identity;
            binding.refusalReason = reason.isEmpty()
                ? QStringLiteral("Core refused the display release.") : reason;
        }
    }
    refreshSubscriptions();
}

void RemoteMediaController::receiveControl(const QJsonObject& payload, quint32 epoch)
{
    if (!d->client || !d->client->mediaAvailable() || epoch != d->epoch
        || !d->peer || payload.value(QStringLiteral("connectionId")) != d->connectionId) { return; }
    const QString op = payload.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("allocation-result")) {
        receiveAllocationResult(payload);
        return;
    }
    if (op == QLatin1String("audio-context")) {
        // The shape the agreed minor selects, then this session's identity.
        // Anything refused leaves generation, playback and signals untouched.
        const std::optional<RemoteAudioContextMessage> context =
            decodeRemoteAudioContext(payload, audioDetailNegotiated());
        if (!context || context->revision != d->audioRevision
            || !isNewerGeneration(context->generation, d->audioGeneration)
            || context->ssrc != d->peer->audioSsrc()) { return; }
        d->audioGeneration = context->generation;
        d->acceptedAudioContext = context;
        // Core has answered: any automatic retry in flight is over.
        d->audioRestarting = false;
        d->audio->stop();
        d->audioEnabled = false;
        // start() can report a speaker failure synchronously, and a listener
        // to that report, or to the status change, may retire this controller.
        const QPointer<RemoteMediaController> self(this);
        if (context->enabled && d->model
            && d->model->isConnected() && !d->model->audioEngine()->masterMuted()) {
            d->preparingAudio = true;
            const bool started = d->audio->start(context->ssrc, context->firstTimestamp);
            if (!self) { return; }
            d->audioEnabled = started;
            d->preparingAudio = false;
            if (d->audioEnabled) {
                qCInfo(lcRemoteMedia).noquote()
                    << QStringLiteral("Remote audio receiving: %1, context %2")
                           .arg(reportedAudioProfile(context->encoder))
                           .arg(context->generation);
            }
        }
        refreshAudioStatus();
        if (!self) { return; }
        emit audioContextAccepted();
        return;
    }
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
        const QPointer<RemoteMediaController> self(this);
        stop();
        if (!self) { return; }
        emit errorOccurred(reason);
        return;
    }
    quint32 endpointId = 0, revision = 0;
    if (!uint32(payload, "endpointId", endpointId) || !uint32(payload, "revision", revision)) { return; }
    auto it = d->bindings.find(endpointId);
    if (it == d->bindings.end() || !it->second.widget) { return; }
    auto& binding = it->second;
    const bool budgetMode = d->client->remoteDisplayBudgetLimits().has_value();
    if (op == QLatin1String("noise-floor")) {
        quint32 generation = 0;
        double floor = 0;
        if (payload.size() != 6 || !binding.accepted || binding.rejected
            || revision != (budgetMode ? binding.contextRevision : binding.revision)
            || !uint32(payload, "contextGeneration", generation)
            || generation != binding.context.codec.contextGeneration
            || !number(payload, "floorDbm", -400, 100, floor)
            || !d->model || !d->model->isConnected() || !d->stack
            || !binding.slice
            || d->stack->spectrum(d->stack->activePanId()) != binding.widget
            || binding.observedStream != binding.slice->streamIndex()
            || binding.observedStreamEpoch != binding.slice->streamEpoch()
            || (budgetMode
                ? !sameOriginalIntent(
                    requestFor(binding.widget, binding.slice,
                               d->client->remoteWidebandAvailable()),
                    binding.acceptedRequest)
                : requestFor(binding.widget, binding.slice,
                             d->client->remoteWidebandAvailable()) != binding.observed)) {
            return;
        }
        // Clarity remains the GUI's existing active-pan controller. Core
        // supplies the full-source percentile, before any display detector
        // or codec can bias it; palette and operator overrides stay local.
        if (ClarityController* clarity = d->model->clarityController()) {
            if (!binding.receivedNoiseFloor) {
                binding.receivedNoiseFloor = true;
                qCInfo(lcRemoteMedia) << "Core noise floor received for Clarity:" << floor << "dBm";
            }
            clarity->feedNoiseFloor(static_cast<float>(floor));
        }
        return;
    }
    if (op == QLatin1String("rejected")) {
        if (!budgetMode && revision == binding.revision && payload.size() == 6
            && payload.value(QStringLiteral("reason")).isString()) {
            binding.accepted = false;
            binding.rejected = true;
            binding.decoder.reset();
            const QPointer<RemoteMediaController> self(this);
            binding.widget->clearRemoteSpectrum();
            if (self) {
                emit errorOccurred(payload.value(QStringLiteral("reason")).toString().left(512));
            }
        }
        return;
    }
    const bool widebandRequested = d->client->remoteWidebandAvailable()
        && (budgetMode ? binding.acceptedRequest : binding.observed)
               .value(QStringLiteral("extendedView")).isBool();
    if (op != QLatin1String("context")
        || payload.size() != (widebandRequested ? 20 : 19)
        || binding.rejected
        || revision != (budgetMode ? binding.acceptedRevision : binding.revision)) { return; }
    // A gesture/rebind may arrive between the outgoing request and its ACK.
    // Issue the newer request before accepting an old view over that gesture.
    if (binding.slice) {
        const QJsonObject currentRequest = requestFor(
            binding.widget, binding.slice, d->client->remoteWidebandAvailable());
        const bool requestChanged = budgetMode
            ? !sameOriginalIntent(currentRequest, binding.acceptedRequest)
            : currentRequest != binding.observed;
        if (requestChanged || binding.observedStream != binding.slice->streamIndex()
            || binding.observedStreamEpoch != binding.slice->streamEpoch()) {
            refreshSubscriptions();
            return;
        }
    }
    SpectrumEndpointContext context;
    context.codec.endpointId = endpointId;
    double stream = 0, sourceCentre = 0, rate = 0, trace = 0, waterfall = 0, wide = 0;
    double min = 0, max = 0, fps = 0, lines = 0;
    if (!uint32(payload, "contextGeneration", context.codec.contextGeneration)
        || !number(payload, "sourceStream", 0, 255, stream, true)
        || !number(payload, "sourceCentreHz", 0, 1.0e12, sourceCentre)
        || !number(payload, "sampleRateHz", 1, 1.0e8, rate)) { return; }
    if (widebandRequested) {
        const QJsonValue widebandJson = payload.value(QStringLiteral("wideband"));
        if (!widebandJson.isObject()) { return; }
        const auto wideband = WidebandDisplayContext::fromJson(widebandJson.toObject());
        if (!wideband) { return; }
        context.wideband = *wideband;
    }
    const double maxSpan = context.wideband.available
        ? std::max(rate, context.wideband.adcRateHz / 2.0) : rate;
    if (!number(payload, "centreHz", 0, 1.0e12, context.exactCentreHz)
        || !number(payload, "spanHz", 0.000001, maxSpan, context.exactSpanHz)
        || !number(payload, "wideCentreHz", 0, 1.0e12, context.wideCentreHz)
        || !number(payload, "wideSpanHz", 0, rate, context.wideSpanHz)
        || !number(payload, "traceSamples", 1, SpectrumEndpoint::kMaxPixels, trace, true)
        || !number(payload, "waterfallSamples", 1, SpectrumEndpoint::kMaxPixels, waterfall, true)
        || !number(payload, "wideSamples", 0, SpectrumEndpoint::kMaxWideSamples, wide, true)
        || !number(payload, "minDbm", -400, 100, min)
        || !number(payload, "maxDbm", -400, 100, max) || min >= max
        || !number(payload, "fps", 1, 60, fps, true)
        || !number(payload, "framesPerLine", 1, 10000, lines, true)) { return; }
    if (widebandRequested) {
        const bool permission = (budgetMode ? binding.acceptedRequest : binding.observed).value(
            QStringLiteral("extendedView")).toBool();
        const bool needsWideband = geometryNeedsWideband(
            context.exactCentreHz, context.exactSpanHz, sourceCentre, rate);
        if (context.wideband.active != needsWideband
            || (context.wideband.active && !permission)) { return; }
    }
    if (binding.accepted && !isNewerGeneration(context.codec.contextGeneration,
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
    binding.contextRevision = revision;
    binding.sourceCentreHz = sourceCentre;
    binding.decoder.reset();
    binding.accepted = true;
    binding.rejected = false;
    binding.receivedNoiseFloor = false;
    binding.receivedFrameTimesMs.clear();
    const QPointer<RemoteMediaController> self(this);
    const QString connectionId = d->connectionId;
    const quint32 acceptedContextRevision = revision;
    binding.widget->setRemoteSpectrumContext(context, sourceCentre, rate);
    if (!self || d->connectionId != connectionId) { return; }
    it = d->bindings.find(endpointId);
    if (it == d->bindings.end() || it->second.contextRevision != acceptedContextRevision
        || it->second.context.codec.contextGeneration
            != context.codec.contextGeneration) { return; }
    auto& installed = it->second;
    // The source crop may be bin-aligned. Remember the displayed accepted
    // window so the polling observer does not feed an ACK back as a new zoom.
    if (installed.slice) {
        const QJsonObject normalized = requestFor(installed.widget, installed.slice,
                                                  d->client->remoteWidebandAvailable());
        if (budgetMode) {
            installed.desiredOriginal = normalized;
            installed.acceptedRequest.insert(QStringLiteral("centreHz"),
                                           normalized.value(QStringLiteral("centreHz")));
            installed.acceptedRequest.insert(QStringLiteral("spanHz"),
                                           normalized.value(QStringLiteral("spanHz")));
        } else {
            installed.observed = normalized;
        }
    }
    refreshCtunState();
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
    if (packet.startsWith("PS3D")) {
        if (!d->client || !d->model || d->client->capabilities().psDisplayVersion != 1
            || !d->model->pureSignalFacade()->ampViewSubscribed()) {
            return;
        }
        PureSignalSessionFacade* facade = d->model->pureSignalFacade();
        if (d->ps3Generation != facade->displayGeneration()) {
            d->ps3Generation = facade->displayGeneration();
            d->ps3Assembler.reset(d->ps3Generation);
        }
        if (const auto snapshot = d->ps3Assembler.accept(packet)) {
            facade->receiveDisplaySnapshot(*snapshot);
        }
        return;
    }
    // Route only the documented v1 prefix. The decoder validates the complete
    // envelope and bounds before any plane can reach the renderer.
    if (packet.size() < 42 || packet.size() > DisplayCodecEncoder::kMaxEncodedBytes
        || packet.first(4) != QByteArrayLiteral("NSDC")) { return; }
    const quint32 id = qFromBigEndian<quint32>(packet.constData() + 8);
    const quint32 generation = qFromBigEndian<quint32>(packet.constData() + 12);
    auto it = d->bindings.find(id);
    if (it == d->bindings.end() || !it->second.accepted || !it->second.widget
        || !it->second.slice
        || it->second.observedStream != it->second.slice->streamIndex()
        || it->second.observedStreamEpoch != it->second.slice->streamEpoch()
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
        it = d->bindings.find(id);
        if (it == d->bindings.end() || !it->second.accepted
            || it->second.context.codec.contextGeneration != generation) { return; }
        const qint64 receivedAt = d->allocationClock();
        it->second.receivedFrameTimesMs.append(receivedAt);
        while (!it->second.receivedFrameTimesMs.isEmpty()
               && it->second.receivedFrameTimesMs.constFirst() < receivedAt - 2'000) {
            it->second.receivedFrameTimesMs.removeFirst();
        }
        ++d->frames;
        if (d->frames == 1) { qCInfo(lcRemoteMedia) << "First encrypted remote spectrum frame received"; }
        emit displayFrameReceived(id);
    }
}
} // namespace NereusSDR
