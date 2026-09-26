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
#include "core/session/media/RemoteSpectrumContext.h"
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
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace NereusSDR {
Q_LOGGING_CATEGORY(lcRemoteMedia, "nereus.remote.media")
namespace {
constexpr int kMaxEndpoints = 8;
constexpr int kDefaultAllocationAckTimeoutMs = kDisplayAllocationAckTimeoutMs;
// The derivation in RemoteMediaController.h, checked: one control heartbeat
// interval for Core's description, then the pinned library's slowest serial
// failure (ICE 39.5 s, DTLS 31 s, SCTP 35 s) for the connection.
constexpr int kLibraryIcePacTimeoutMs = 39'500;   // libjuice agent.h:43 [@3c40a354]
constexpr int kLibraryDtlsHandshakeFailMs = 31'000; // dtlstransport.cpp:1024-1031
constexpr int kLibrarySctpInitFailMs = 35'000;    // sctptransport.cpp:127-142
static_assert(RemoteMediaController::kMediaDescriptionDeadlineMs
                  == StationClient::kDefaultHeartbeatIntervalMs,
              "the description stage must be one control heartbeat interval");
static_assert(RemoteMediaController::kMediaConnectDeadlineMs
                  == kLibraryIcePacTimeoutMs + kLibraryDtlsHandshakeFailMs
                         + kLibrarySctpInitFailMs,
              "the connection stage must be the library's slowest serial failure");
// The audio status refresh, which runs only while a receiver runs or a
// playback problem awaits recovery.
constexpr int kAudioStatusRefreshMs = 250;
// R-R3-23: how often the lossless link trial samples playback. Its windows
// are RemoteAudioLinkTrial::kWindowMs long; a 1 s sample closes each within
// a second of its end.
constexpr int kLinkTrialSampleMs = 1000;
// R-R3-35: probes kept awaiting their echo; an older one is forgotten.
constexpr std::size_t kMaxPendingClockProbes = 8;
// The receiver restarts that say audio arrived badly, which the link trial
// counts. Speaker, decoder and clock faults are this computer's own.
bool linkInterruption(RemoteAudioReceiver::Fault fault)
{
    return fault == RemoteAudioReceiver::Fault::ArrivalBurst
        || fault == RemoteAudioReceiver::Fault::StreamGap
        || fault == RemoteAudioReceiver::Fault::NoPackets;
}

RemoteAudioProfile storedAudioProfileChoice()
{
    return AppSettings::instance()
                   .value(QLatin1String(RemoteMediaController::kAudioProfileSettingKey),
                          QStringLiteral("Opus"))
                   .toString()
               == QLatin1String("Lossless")
        ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
}
// R-R3-45: the link trial's stream number for the headphones mix (the
// speakers' mix is -1, a receiver stream its slice id).
constexpr int kHeadphonesTrialStream = -2;

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

// A clock reading in whole nanoseconds, never negative (R-R3-35).
bool nanoseconds(const QJsonObject& object, const char* key, qint64& value)
{
    const QJsonValue json = object.value(QLatin1String(key));
    value = json.isDouble() ? json.toInteger(-1) : -1;
    return value >= 0;
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
QString reportedAudioProfile(const RemoteAudioContextMessage& context)
{
    if (const std::optional<PcmEncoderProfile>& lossless = context.losslessEncoder) {
        return QStringLiteral("lossless L16 %1 Hz, %2 channels, %3-sample packets, "
                              "%4-bit, payload type %5")
            .arg(lossless->sampleRate)
            .arg(lossless->channels)
            .arg(lossless->frameSamples)
            .arg(lossless->bitsPerSample)
            .arg(lossless->payloadType);
    }
    const std::optional<OpusEncoderProfile>& encoder = context.encoder;
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

// The zoom-detail limit a grant reports, for the pan's status line
// (R-R3-37); false when nothing limited it.
bool applyGrantLimit(PanDisplayState& state, const std::optional<SpectrumContextGrant>& grant)
{
    if (!grant) {
        return false;
    }
    switch (grant->limit) {
    case SpectrumLimitReason::None:
        return false;
    case SpectrumLimitReason::LargestSize:
        state.zoomLimit = PanDisplayState::ZoomLimit::LargestSize;
        return true;
    case SpectrumLimitReason::SharedEngine:
        state.zoomLimit = PanDisplayState::ZoomLimit::SharedEngine;
        return true;
    case SpectrumLimitReason::SourceBins:
        state.zoomLimit = PanDisplayState::ZoomLimit::SourceBins;
        state.zoomPoints = grant->grantedPixels;
        return true;
    }
    return false;
}

PanDisplayState refusedState(const QString& reason)
{
    PanDisplayState state;
    state.phase = PanDisplayState::Phase::Refused;
    state.refusalReason = reason;
    return state;
}

PanDisplayState phaseState(PanDisplayState::Phase phase)
{
    PanDisplayState state;
    state.phase = phase;
    return state;
}

// Log wording only.
QString grantLogLine(const SpectrumContextGrant& grant)
{
    return QStringLiteral("FFT %1 (%2 tier), %3 of %4 points, limit %5")
        .arg(grant.grantedFftSize)
        .arg(grant.grantedTier == FftTier::Fine ? QStringLiteral("fine") : QStringLiteral("wide"))
        .arg(grant.grantedPixels)
        .arg(grant.requestedPixels)
        .arg(spectrumLimitReasonToWire(grant.limit));
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
        double(widget->wfUpdatePeriodMs()) * fps / 1000.0)), kMaxFramesPerLine);
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
        /// What Core granted, as the accepted minor-9 context reported it.
        std::optional<SpectrumContextGrant> grant;
        /// R-R3-01/R-R3-08/R-R3-37: Core granted fewer pixels than asked
        /// with no limit named (a lone pan kept at the charge it was first
        /// admitted at). The same request goes out once more as an
        /// increase; askedAgain is that request, so a second short answer
        /// to it is not asked again.
        bool askAgain = false;
        QJsonObject askedAgain;
        double sourceCentreHz = 0;
        DisplayCodecDecoder decoder;
        qint64 lastKeyframeMs = -1000;
        bool accepted = false;
        bool rejected = false;
        bool retiring = false;
        bool suspending = false;
        bool receivedNoiseFloor = false;
        /// R-R3-37: when this pan began waiting for an accepted display
        /// (allocationClock), or -1 while it has one. See kPanWaitingGraceMs.
        qint64 waitingSinceMs = -1;
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
    // The status each pan was last given, before its grant limit is added.
    QHash<QString, PanDisplayState> panBaseStatus;
    // R-R3-08/37: why each pan's display is below what it asked for, when
    // the Core said (CoreBusy); None for a pan at its requested quality.
    // The pan status builder maps it to words.
    QHash<QString, DisplayBudgetReason> panBudgetReason;
    struct CtunState {
        quint64 epoch = 0;
        int requestSliceId = -1;
        bool requestedPin = false;
        bool pending = false;
        bool initialized = false;
        quint32 rejectedContext = 0;
    };
    QHash<int, CtunState> ctunStreams;
    // R-R3-18/21: one C-Tune centre request in flight per stream. A gesture
    // runs at mouse or wheel rate; the Core answers each request, and while
    // one is out the newest wanted centre waits here. A refusal drops it, so
    // a refused gesture cannot keep asking.
    struct CentreRequest {
        quint64 epoch = 0;
        bool inFlight = false;
        bool hasQueued = false;
        int queuedSliceId = -1;
        double queuedHz = 0.0;
    };
    QHash<int, CentreRequest> centreRequests;
    QString connectionId;
    quint32 epoch = 0;
    quint32 nextEndpoint = 1;
    quint64 frames = 0;
    // Display drops already written to the log, and when (allocationClock).
    quint64 displayDropsReported = 0;
    qint64 displayDropsReportedAtMs = 0;
    Ps3DisplayAssembler ps3Assembler;
    quint64 ps3Generation = 0;
    std::unique_ptr<RemoteAudioReceiver> audio;
    // R-R3-43: the sinks one receiver stream hands audio to. Its worker
    // delivers under the mutex; release removes a sink under it, so a sink
    // is never called after releaseReceiverAudio() returns.
    struct ReceiverFanout {
        int sliceId = -1;
        std::mutex mutex;
        QList<IReceiverPcmSink*> sinks;
    };
    // R-R3-43: one wanted receiver stream, kept while any sink wants it
    // (across media connections). Everything but the sinks and the receiver
    // belongs to the current media connection and is reset with it.
    struct ReceiverStream {
        QList<IReceiverPcmSink*> sinks;
        std::shared_ptr<ReceiverFanout> fanout;
        std::unique_ptr<RemoteAudioReceiver> receiver;
        quint32 generation = 0;                        // newest accepted context
        std::optional<RemoteAudioContextMessage> context;
        quint32 ssrc = 0;                              // while the receiver runs
        std::optional<RemoteAudioProfile> runningProfile;
        bool stopped = false;                          // the sinks were told stopReason
        QString stopReason;
        // The Core forgot this request (slice-removed or receiver-limit):
        // asked again only when that can change, never in a loop.
        std::optional<RemoteAudioOffReason> heldBy;
        bool faulted = false; // this computer stopped it; asked again only by a new choice
        bool retryPending = false;
        qint64 lastRequestMs = -1000;
    };
    std::map<int, ReceiverStream> receiverStreams;
    // R-R3-43: the last receiver-audio revision sent for each slice id on
    // this media connection. The Core remembers them for the whole
    // connection, so a slice id's revision only ever grows, even when a new
    // slice reuses the id; a new connection starts afresh.
    QHash<int, quint32> receiverRevisions;
    // R-R3-43: the receiver stream ids this media connection declared.
    QList<quint32> receiverSsrcs;
    // R-R3-45: the headphones mix, played on this computer's headphones
    // with its own rate matching. Everything but the receiver belongs to
    // the current media connection and is reset with it.
    std::unique_ptr<RemoteAudioReceiver> headphones;
    quint32 headphonesSsrc = 0;               // declared by this connection
    std::optional<RemoteAudioContextMessage> headphonesContext;
    quint32 headphonesRevision = 0;           // last request sent
    quint32 headphonesGeneration = 0;         // newest accepted context
    bool headphonesRequested = false;         // last request asked for it
    // This computer's headphones failed: not asked for again until the
    // device changes or the operator chooses the audio quality again.
    bool headphonesFaulted = false;
    bool headphonesRetryPending = false;
    qint64 headphonesLastRequestMs = -1000;
    QString headphonesProblem;
    bool destroying = false;
    std::optional<RemoteAudioContextMessage> acceptedAudioContext;
    quint32 audioRevision = 0;
    quint32 audioGeneration = 0;
    bool preparingAudio = false;
    bool audioEnabled = false;
    bool audioRetryPending = false;
    // R-R3-23. The operator's choice, stored on this computer. Whether this
    // media session has sent Core a `profile` (its contexts then carry the
    // profile shape). Whether this media session's link trial failed, so
    // Opus is asked for until the session ends or the operator chooses
    // again. The trial itself and its 1 s sampling timer.
    RemoteAudioProfile audioProfileChoice = RemoteAudioProfile::Opus;
    bool audioProfileRequested = false;
    bool losslessFallback = false;
    RemoteAudioLinkTrial linkTrial;
    QTimer* linkTrialTimer = nullptr;
    // R-R3-35 measured delay, per media session: the clock offset from
    // probe echoes, the Core's newest capture from the latest echo, the
    // probes awaiting an echo (id, t0) and the 1 s probe timer.
    AudioClockEstimator clockEstimator;
    std::optional<AudioCaptureAnchor> captureAnchor;
    std::deque<std::pair<quint32, qint64>> pendingClockProbes;
    quint32 nextClockProbeId = 0;
    QTimer* clockProbeTimer = nullptr;
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
    // R-R3-28: bounds a started media session that never becomes ready, in
    // two stages: Core's description, then the connection.
    QTimer* establishTimer = nullptr;
    int descriptionDeadlineMs = RemoteMediaController::kMediaDescriptionDeadlineMs;
    int connectDeadlineMs = RemoteMediaController::kMediaConnectDeadlineMs;
    bool awaitingDescription = false;
    // While MediaPeer::start runs, a backend error is its refusal reason.
    bool startingPeer = false;
    QString startRefusal;
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
    /// Fix wave 2 (Critical 1, the several-devices design, ruling 9.3):
    /// the Core splits the display budget by what each device asks for, so
    /// the planner asks for the displays the operator wants, not only what
    /// the share allows. While askingWanted, every pan is subscribed at its
    /// wanted quality; a refusal for the budget (answered by the smaller
    /// share the Core publishes with it) ends the ask, and the planner plans
    /// inside the share again. It asks again whenever what the operator
    /// wants grows (a new pan, a wider or faster one; a resize once it
    /// settles, fix wave 3) and when the transmit holder changes
    /// (setTransmitHolder): wantedCharge is the charge of the displays
    /// wanted at the last plan.
    bool askingWanted = false;
    DisplayBudgetCharge wantedCharge;
    /// Fix wave 3 (Minor 3): the displays wanted at the last plan, and a
    /// resize's growth waiting to settle before it asks: the wanted charge
    /// before the resize began, and when a width last moved.
    QList<RemoteDisplayIntent> wantedIntents;
    struct ResizeAsk {
        DisplayBudgetCharge base;
        qint64 movedAtMs = 0;
    };
    std::optional<ResizeAsk> resizeAsk;
    /// Fix wave 3: the transmit holder last notified (setTransmitHolder);
    /// a change asks again. Kept across media sessions: it is the station's.
    quint64 holderEpoch = 0;
    bool holderAway = false;
};

RemoteMediaController::RemoteMediaController(StationClient* client, RadioModel* model,
    PanadapterStack* stack, QObject* parent, MediaPeer::TransportFactory factory,
    AllocationClock allocationClock, int allocationAckTimeoutMs, int descriptionDeadlineMs,
    int connectDeadlineMs)
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
    d->descriptionDeadlineMs = descriptionDeadlineMs > 0
        ? descriptionDeadlineMs : kMediaDescriptionDeadlineMs;
    d->connectDeadlineMs = connectDeadlineMs > 0
        ? connectDeadlineMs : kMediaConnectDeadlineMs;
    d->establishTimer = new QTimer(this);
    d->establishTimer->setObjectName(QStringLiteral("remoteMediaEstablishTimer"));
    d->establishTimer->setSingleShot(true);
    // Precise: a coarse timer may fire up to 5% early, which at stage two
    // would come before the library's own slowest report.
    d->establishTimer->setTimerType(Qt::PreciseTimer);
    connect(d->establishTimer, &QTimer::timeout, this, [this] {
        // R-R3-28: negotiation that never reaches a terminal state. Only
        // the session the timer was armed for, and only while not ready;
        // stop() disarms it for every other outcome.
        if (!d->peer || d->peer->isReady() || !d->client || !d->client->mediaAvailable()
            || d->client->sessionEpoch() != d->epoch) {
            return;
        }
        requestRecovery(d->epoch, d->awaitingDescription
            ? QStringLiteral("Core sent no station media description within %1 seconds")
                  .arg(QString::number(d->descriptionDeadlineMs / 1000.0))
            : QStringLiteral("Station media did not connect within %1 seconds")
                  .arg(QString::number(d->connectDeadlineMs / 1000.0)));
    });
    d->audio = std::make_unique<RemoteAudioReceiver>(model->audioEngine());
    d->selectedOutput = selectedSpeakerOutput();
    d->audioProfileChoice = storedAudioProfileChoice();
    d->linkTrialTimer = new QTimer(this);
    d->linkTrialTimer->setObjectName(QStringLiteral("remoteAudioLinkTrialTimer"));
    d->linkTrialTimer->setInterval(kLinkTrialSampleMs);
    connect(d->linkTrialTimer, &QTimer::timeout,
            this, &RemoteMediaController::checkLosslessLink);
    d->clockProbeTimer = new QTimer(this);
    d->clockProbeTimer->setObjectName(QStringLiteral("remoteAudioClockProbeTimer"));
    d->clockProbeTimer->setInterval(kClockProbeIntervalMs);
    connect(d->clockProbeTimer, &QTimer::timeout, this, &RemoteMediaController::sendClockProbe);
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
            QJsonObject disable{{QStringLiteral("op"), QStringLiteral("audio")},
                                {QStringLiteral("revision"), double(d->audioRevision)},
                                {QStringLiteral("enabled"), false}};
            if (audioProfileNegotiated()) {
                disable.insert(QStringLiteral("profile"), remoteAudioProfileToWire(
                    d->audioProfileChoice == RemoteAudioProfile::Lossless && !d->losslessFallback
                        ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus));
                d->audioProfileRequested = true;
            }
            send(disable);
            if (!self) { return; }
        }
        refreshAudioStatus();
        if (!self) { return; }
        emit errorOccurred(remoteAudioProblemText(fault));
    });
    connect(d->audio.get(), &RemoteAudioReceiver::restartRequested, this,
            [this](const QString& reason, RemoteAudioReceiver::Fault fault) {
        qCWarning(lcRemoteMedia) << reason;
        d->audio->stop();
        // R-R3-23: a lossless stream that arrives badly enough to restart
        // counts against the link trial; failing it asks Core for Opus now.
        if (d->linkTrial.active() && linkInterruption(fault)
            && d->linkTrial.noteInterruption(d->clock.elapsed())
                == RemoteAudioLinkTrial::Verdict::Failed) {
            fallBackToOpus(QStringLiteral("receiver restarts while playing lossless audio"));
            return;
        }
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
    // R-R3-45: the headphones mix plays on this computer's headphones with
    // its own receiver and rate matcher. A failure there stops only it.
    d->headphones = std::make_unique<RemoteAudioReceiver>(model->audioEngine(),
                                                          RemotePlaybackOutput::Headphones);
    connect(d->headphones.get(), &RemoteAudioReceiver::restartRequested,
            this, &RemoteMediaController::onHeadphonesRestart);
    connect(d->headphones.get(), &RemoteAudioReceiver::errorOccurred,
            this, &RemoteMediaController::onHeadphonesError);
    // Headphones opened, closed or moved to another device: a device that
    // failed gets a fresh chance, and the Core is asked accordingly.
    const auto headphonesDeviceChanged = [this] {
        // Opened, closed or moved: whatever played stops now (so a device
        // closed on purpose is not reported as a failure), a device that
        // failed gets a fresh chance, and the Core is asked again so the
        // mix starts on the device as it is now.
        d->headphones->stop();
        d->headphonesFaulted = false;
        if (d->headphonesProblem != QLatin1String(kHeadphonesMixUnavailableReason)
            && d->headphonesProblem != QLatin1String(kHeadphonesCoreCouldNotStart)) {
            setHeadphonesProblem(QString());
        }
        requestHeadphonesAudio();
    };
    connect(model->audioEngine(), &AudioEngine::headphonesAvailableChanged,
            this, headphonesDeviceChanged);
    connect(model->audioEngine(), &AudioEngine::headphonesConfigChanged,
            this, headphonesDeviceChanged);
    // R-R3-43: a slice id the Core had removed may come back (the Core
    // reuses ids); a consumer still waiting on it is asked for once more.
    connect(model, &RadioModel::sliceAdded, this, [this] {
        if (!d->model || !d->peer || !d->peer->isReady() || !receiverAudioNegotiated()) {
            return;
        }
        QList<int> back;
        for (const auto& [sliceId, stream] : d->receiverStreams) {
            if (stream.heldBy == RemoteAudioOffReason::SliceRemoved
                && d->model->sliceById(sliceId)) {
                back.append(sliceId);
            }
        }
        const QPointer<RemoteMediaController> self(this);
        for (int sliceId : back) {
            sendReceiverAudioRequest(sliceId, true);
            if (!self) { return; }
        }
        if (!back.isEmpty()) { refreshAudioStatus(); }
    });
    connect(model->audioEngine(), &AudioEngine::speakersConfigChanged, this, [this] {
        d->selectedOutput = selectedSpeakerOutput();
        // Opening the speaker for playback can report its configuration from
        // inside the receiver's start(); the accepted context that started it
        // refreshes the status once start() returns.
        if (!d->preparingAudio) { requestAudio(); }
    });
    d->timer = new QTimer(this);
    d->timer->setInterval(kPlannerIntervalMs);
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
            qCInfo(lcRemoteMedia).noquote() << "Remote PureSignal display refused:"
                                            << d->ps3RefusalReason;
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
            d->centreRequests.clear();
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
    connect(client, &StationClient::streamCentreFinished,
            this, &RemoteMediaController::finishCentreRequest);
    connect(client, &QObject::destroyed, this, &RemoteMediaController::stop);
    connect(model, &RadioModel::connectionStateChanged, this, [this](ConnectionState state) {
        if (state != ConnectionState::Connected) {
            // The daemon retires FFT production when the radio disconnects,
            // even if this authenticated station session remains connected.
            // Retire our observations too so identical settings resubscribe.
            QList<quint32> endpoints;
            for (const auto& [id, binding] : d->bindings) { endpoints.append(id); }
            d->ctunStreams.clear();
            d->centreRequests.clear();
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
    // Nothing observes a status change while this controller is destroyed,
    // and no consumer is told anything: they may already be gone.
    const QSignalBlocker blocker(this);
    d->destroying = true;
    stop();
    for (auto& [sliceId, stream] : d->receiverStreams) {
        {
            std::lock_guard<std::mutex> lock(stream.fanout->mutex);
            stream.fanout->sinks.clear();
        }
        stream.receiver->stop();
    }
    d->receiverStreams.clear();
}
quint64 RemoteMediaController::receivedDisplayFrames() const { return d->frames; }
DisplayBudgetReason RemoteMediaController::panDisplayBudgetReason(const QString& panId) const
{
    return d->panBudgetReason.value(panId, DisplayBudgetReason::None);
}
int RemoteMediaController::activeEndpointCount() const { return int(d->bindings.size()); }
std::optional<MediaPeerTelemetry> RemoteMediaController::trafficTelemetry() const
{
    return d->peer ? d->peer->telemetry() : std::nullopt;
}
quint64 RemoteMediaController::displayMessagesDropped() const
{
    const std::optional<MediaPeerTelemetry> traffic = trafficTelemetry();
    return traffic ? traffic->traffic.displayMessagesDropped : 0;
}

void RemoteMediaController::reportDisplayDrops()
{
    // The drop rule itself lives in the transport (8 messages or 256 KiB,
    // oldest first); this only reports it, at most every 10 seconds.
    constexpr qint64 kDisplayDropReportIntervalMs = 10'000;
    const quint64 dropped = displayMessagesDropped();
    if (dropped <= d->displayDropsReported) { return; }
    const qint64 now = d->allocationClock();
    if (d->displayDropsReported != 0
        && now - d->displayDropsReportedAtMs < kDisplayDropReportIntervalMs) { return; }
    qCInfo(lcRemoteMedia).noquote()
        << QStringLiteral("Remote display: skipped %1 late updates on this computer "
                          "to keep the picture current (%2 this session)")
               .arg(dropped - d->displayDropsReported)
               .arg(dropped);
    d->displayDropsReported = dropped;
    d->displayDropsReportedAtMs = now;
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
bool RemoteMediaController::spectrumGrantNegotiated() const
{
    return d->client && d->client->spectrumGrantAvailable();
}
RemoteAudioStatus RemoteMediaController::audioStatus() const
{
    return d->audioStatus;
}
RemoteAudioProfile RemoteMediaController::audioProfileChoice() const
{
    return d->audioProfileChoice;
}
bool RemoteMediaController::audioProfileNegotiated() const
{
    return audioDetailNegotiated() && d->client->capabilities().audioProfileVersion >= 1;
}

bool RemoteMediaController::audioClockNegotiated() const
{
    return d->client && d->client->mediaAvailable()
        && d->client->capabilities().audioClockVersion >= 1;
}

bool RemoteMediaController::receiverAudioNegotiated() const
{
    return audioProfileNegotiated() && d->client->capabilities().receiverAudioVersion >= 1;
}

QHash<int, RemoteAudioReceiverTelemetry> RemoteMediaController::receiverAudioTelemetry() const
{
    QHash<int, RemoteAudioReceiverTelemetry> telemetry;
    for (const auto& [sliceId, stream] : d->receiverStreams) {
        telemetry.insert(sliceId, stream.receiver->telemetry());
    }
    return telemetry;
}

RemoteAudioDelayReport RemoteMediaController::audioDelay() const
{
    RemoteAudioDelayReport report;
    report.measurable = !d->peer.isNull() && audioClockNegotiated();
    if (!report.measurable || !d->audioEnabled) {
        return report;
    }
    const RemoteAudioReceiverTelemetry playback = d->audio->telemetry();
    if (!playback.running) {
        return report;
    }
    AudioDelayInputs inputs;
    inputs.offset = d->clockEstimator.offset(d->audio->nowNs());
    inputs.capture = d->captureAnchor;
    inputs.playingGeneration = d->audioGeneration;
    inputs.playout = playback.playout;
    inputs.release = playback.release;
    report.estimate = measureAudioDelay(inputs);
    return report;
}

void RemoteMediaController::reconcileClockProbe()
{
    // R-R3-35: probe once a second while this computer plays the Core's
    // audio, and only to a Core that answers.
    const bool probe = d->peer && audioClockNegotiated() && d->audioEnabled
        && d->audio->isRunning();
    if (probe && !d->clockProbeTimer->isActive()) {
        d->clockProbeTimer->start();
    } else if (!probe && d->clockProbeTimer->isActive()) {
        d->clockProbeTimer->stop();
    }
}

void RemoteMediaController::sendClockProbe()
{
    if (!d->peer || !audioClockNegotiated()) {
        return;
    }
    const quint32 id = ++d->nextClockProbeId;
    const qint64 sentNs = d->audio->nowNs();
    d->pendingClockProbes.emplace_back(id, sentNs);
    while (d->pendingClockProbes.size() > kMaxPendingClockProbes) {
        d->pendingClockProbes.pop_front();
    }
    send({{QStringLiteral("op"), QStringLiteral("clock-probe")},
          {QStringLiteral("id"), qint64(id)},
          {QStringLiteral("t0"), sentNs}});
}

void RemoteMediaController::receiveClockEcho(const QJsonObject& payload, qint64 receivedNs)
{
    double idValue = 0;
    double generation = 0;
    double rtpTimestamp = 0;
    qint64 t0 = 0, t1 = 0, t2 = 0, capturedNs = 0;
    if (payload.size() != 9
        || !number(payload, "id", 0, std::numeric_limits<quint32>::max(), idValue, true)
        || !nanoseconds(payload, "t0", t0) || !nanoseconds(payload, "t1", t1)
        || !nanoseconds(payload, "t2", t2) || !nanoseconds(payload, "capturedNs", capturedNs)
        || !number(payload, "generation", 0, std::numeric_limits<quint32>::max(), generation, true)
        || !number(payload, "rtpTimestamp", 0, std::numeric_limits<quint32>::max(),
                   rtpTimestamp, true)) {
        return;
    }
    const quint32 id = quint32(idValue);
    // Only an answer to a probe this session sent, with the time it sent.
    const auto pending = std::find_if(d->pendingClockProbes.begin(), d->pendingClockProbes.end(),
        [id](const std::pair<quint32, qint64>& probe) { return probe.first == id; });
    if (pending == d->pendingClockProbes.end() || pending->second != t0) {
        return;
    }
    d->pendingClockProbes.erase(pending);
    if (!d->clockEstimator.addSample({t0, t1, t2, receivedNs})) {
        return;
    }
    if (generation != 0) {
        d->captureAnchor = AudioCaptureAnchor{quint32(generation), quint32(rtpTimestamp), capturedNs};
    } else {
        d->captureAnchor.reset();
    }
}

void RemoteMediaController::setTransmitHolder(quint64 holderEpoch, bool holderAway)
{
    if (d->holderEpoch == holderEpoch && d->holderAway == holderAway) {
        return;
    }
    d->holderEpoch = holderEpoch;
    d->holderAway = holderAway;
    // Fix wave 3 (ruling 9.3): the split changed its rule for this device
    // or the others, and the demand the Core holds is the plan made inside
    // the old share. Asking for what the operator wants again is what lets
    // a new present holder keep its whole request, and the others their
    // equal shares once a holder lets go.
    d->askingWanted = true;
    QTimer::singleShot(0, this, &RemoteMediaController::refreshSubscriptions);
}

void RemoteMediaController::setAudioProfileChoice(RemoteAudioProfile profile)
{
    const bool changed = profile != d->audioProfileChoice;
    d->audioProfileChoice = profile;
    AppSettings::instance().setValue(QLatin1String(kAudioProfileSettingKey),
                                     remoteAudioProfileName(profile));
    // Choosing again gives lossless a fresh chance on this link.
    const bool wasFallback = std::exchange(d->losslessFallback, false);
    d->linkTrial.end();
    d->linkTrialTimer->stop();
    const bool askAgain = (changed || wasFallback) && audioProfileNegotiated() && d->peer
        && d->peer->isReady();
    if (askAgain) {
        // R-R3-43: every receiver stream follows the one choice, muted
        // speakers or not; a stream this computer stopped gets its chance.
        for (auto& [sliceId, stream] : d->receiverStreams) { stream.faulted = false; }
        const QPointer<RemoteMediaController> self(this);
        requestWantedReceiverAudio();
        if (!self) { return; }
        // R-R3-45: the headphones mix too, muted speakers or not. A choice
        // of quality retries failed headphones (a decoder that could not
        // start depends on it); a media reconnect does not.
        d->headphonesFaulted = false;
        requestHeadphonesAudio();
        if (!self) { return; }
    }
    if (askAgain && d->model && !d->model->audioEngine()->masterMuted()) {
        requestAudio();
        return;
    }
    refreshAudioStatus();
}

void RemoteMediaController::checkLosslessLink()
{
    if (!d->linkTrial.active()) {
        d->linkTrialTimer->stop();
        return;
    }
    // R-R3-43: every lossless stream the link carries, the speakers' mix
    // and each receiver stream, judged together.
    std::vector<RemoteAudioLinkTrial::StreamSample> samples;
    if (d->acceptedAudioContext && d->acceptedAudioContext->losslessEncoder) {
        samples.push_back({-1, d->audio->telemetry()});
    }
    for (const auto& [sliceId, stream] : d->receiverStreams) {
        if (stream.context && stream.context->losslessEncoder) {
            samples.push_back({sliceId, stream.receiver->telemetry()});
        }
    }
    // R-R3-45: and the headphones mix.
    if (d->headphonesContext && d->headphonesContext->enabled
        && d->headphonesContext->losslessEncoder) {
        samples.push_back({kHeadphonesTrialStream, d->headphones->telemetry()});
    }
    if (d->linkTrial.observe(d->clock.elapsed(), samples)
        == RemoteAudioLinkTrial::Verdict::Failed) {
        fallBackToOpus(QStringLiteral("%1% of lossless packets lost or filled in over %2 s")
            .arg(100.0 * d->linkTrial.lastWindowLoss().value_or(0.0), 0, 'f', 1)
            .arg(RemoteAudioLinkTrial::kWindowMs / 1000));
    }
}

void RemoteMediaController::fallBackToOpus(const QString& cause)
{
    d->linkTrial.end();
    d->linkTrialTimer->stop();
    d->losslessFallback = true;
    const QString text = remoteAudioQualityReasonText(RemoteAudioQualityReason::NetworkTooSlow);
    qCInfo(lcRemoteMedia).noquote()
        << QStringLiteral("Remote audio: lossless link trial failed (%1); asking Core for Opus")
               .arg(cause);
    const QPointer<RemoteMediaController> self(this);
    requestAudio();
    if (!self) { return; }
    // R-R3-43: one fallback moves every receiver stream to Opus too, with
    // this one notice.
    requestWantedReceiverAudio();
    if (!self) { return; }
    // R-R3-45: and the headphones mix, with the same one notice.
    requestHeadphonesAudio();
    if (!self) { return; }
    emit errorOccurred(text);
}

// ---- R-R3-45: the headphones mix ----

QString RemoteMediaController::headphonesFaultText(RemoteAudioReceiver::Fault fault)
{
    // The receiver names its faults after the speaker; for the headphones
    // receiver they mean the headphones device. A fault stops the
    // headphones until they are turned off and on again (or another device
    // or audio quality is chosen), so each says how to try again.
    using Fault = RemoteAudioReceiver::Fault;
    const QString again =
        QStringLiteral(" Turn the headphones off and on in Setup, Audio, Devices to try again.");
    switch (fault) {
    case Fault::SpeakerOpenFailed:
        return QStringLiteral("The headphones could not be opened.") + again;
    case Fault::SpeakerTimingUnavailable:
        return QStringLiteral("The headphones stopped reporting their timing.") + again;
    case Fault::SpeakerCallbackTooLarge:
        return QStringLiteral("The headphones buffer is larger than remote playback "
                              "supports. Choose a smaller buffer or other headphones.");
    case Fault::SpeakerStalled:
        return QStringLiteral("The headphones stopped playing audio.") + again;
    case Fault::SpeakerWriteFailed:
        return QStringLiteral("Audio could not be sent to the headphones.") + again;
    case Fault::DecoderUnavailable:
        return QStringLiteral("The audio decoder for the headphones could not start on "
                              "this computer. Choosing the audio quality again tries once more.");
    case Fault::ArrivalBurst:
    case Fault::StreamGap:
    case Fault::NoPackets:
    case Fault::DecodeFailed:
    case Fault::ClockBuffer:
        // The receiver restarts itself after these; none of them persists.
        return QStringLiteral("Audio on the headphones was interrupted.");
    }
    return {};
}

bool RemoteMediaController::headphonesMixNegotiated() const
{
    return audioProfileNegotiated() && d->client->capabilities().headphonesMixVersion >= 1;
}

QString RemoteMediaController::headphonesProblem() const
{
    return d->headphonesProblem;
}

RemoteAudioReceiverTelemetry RemoteMediaController::headphonesTelemetry() const
{
    return d->headphones->telemetry();
}

std::optional<RemoteAudioContextMessage> RemoteMediaController::acceptedHeadphonesContext() const
{
    return d->headphonesContext;
}

void RemoteMediaController::setHeadphonesProblem(const QString& problem)
{
    if (problem == d->headphonesProblem) { return; }
    d->headphonesProblem = problem;
    emit headphonesProblemChanged(problem);
}

bool RemoteMediaController::headphonesWanted() const
{
    // This computer can play a headphones mix: headphones open here and
    // none of them failed. The Core sends it only while some receiver is
    // routed to the headphones.
    return d->model && d->model->audioEngine()->headphonesAvailable()
        && !d->headphonesFaulted;
}

void RemoteMediaController::requestHeadphonesAudio()
{
    if (!d->peer || !d->peer->isReady() || !headphonesMixNegotiated()) { return; }
    const bool wanted = headphonesWanted();
    // Nothing asked for yet on this connection: nothing to stop.
    if (!wanted && d->headphonesRevision == 0) { return; }
    ++d->headphonesRevision;
    if (!d->headphonesRevision) { ++d->headphonesRevision; }
    d->headphonesRequested = wanted;
    d->headphonesRetryPending = false;
    d->headphonesLastRequestMs = d->clock.elapsed();
    // The one quality choice, as the speakers' stream asks for it.
    const RemoteAudioProfile profile =
        d->audioProfileChoice == RemoteAudioProfile::Lossless && !d->losslessFallback
        ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
    send(QJsonObject{{QStringLiteral("op"), QStringLiteral("headphones-audio")},
                     {QStringLiteral("revision"), double(d->headphonesRevision)},
                     {QStringLiteral("enabled"), wanted},
                     {QStringLiteral("profile"), remoteAudioProfileToWire(profile)}});
}

void RemoteMediaController::receiveHeadphonesAudioContext(const QJsonObject& payload)
{
    const std::optional<RemoteAudioContextMessage> context =
        decodeHeadphonesAudioContext(payload);
    if (!context || context->revision != d->headphonesRevision
        || !isNewerGeneration(context->generation, d->headphonesGeneration)
        || d->headphonesSsrc == 0 || context->ssrc != d->headphonesSsrc) { return; }
    d->headphonesGeneration = context->generation;
    d->headphonesContext = context;
    d->headphonesRetryPending = false;
    d->headphones->stop();
    // start() can report a headphones failure synchronously, and a listener
    // to that report may retire this controller.
    const QPointer<RemoteMediaController> self(this);
    if (context->enabled && headphonesWanted()) {
        const RemoteAudioProfile profile = context->losslessEncoder
            ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
        if (d->headphones->start(context->ssrc, context->firstTimestamp, profile)) {
            setHeadphonesProblem(QString());
            qCInfo(lcRemoteMedia).noquote()
                << QStringLiteral("Remote headphones audio receiving: %1, context %2")
                       .arg(reportedAudioProfile(*context)).arg(context->generation);
        }
        if (!self) { return; }
    } else if (!context->enabled) {
        const RemoteAudioOffReason reason =
            context->offReason.value_or(RemoteAudioOffReason::EncoderUnavailable);
        if (reason == RemoteAudioOffReason::EncoderUnavailable) {
            setHeadphonesProblem(QString::fromLatin1(kHeadphonesCoreCouldNotStart));
        } else if (!d->headphonesFaulted) {
            // No receiver on the headphones, this computer asked it off, or
            // the radio or media is not ready: the speakers' status says
            // the last two, and the first two leave nothing to explain.
            setHeadphonesProblem(QString());
        }
        if (!self) { return; }
    }
    reconcileLinkTrial();
    refreshAudioStatus();
}

void RemoteMediaController::onHeadphonesRestart(const QString& reason,
                                                RemoteAudioReceiver::Fault fault)
{
    qCWarning(lcRemoteMedia).noquote()
        << QStringLiteral("Remote headphones audio: %1").arg(reason);
    d->headphones->stop();
    // R-R3-23: a lossless headphones mix's restart counts against the one
    // link trial, as the speakers' does.
    if (d->headphonesContext && d->headphonesContext->losslessEncoder
        && d->linkTrial.active() && linkInterruption(fault)
        && d->linkTrial.noteInterruption(d->clock.elapsed())
            == RemoteAudioLinkTrial::Verdict::Failed) {
        fallBackToOpus(QStringLiteral("headphones stream restarts while lossless audio plays"));
        return;
    }
    if (!d->headphonesRetryPending) {
        d->headphonesRetryPending = true;
        const QString connection = d->connectionId;
        const quint32 revision = d->headphonesRevision;
        const int delay = int(std::max<qint64>(
            0, 1000 - (d->clock.elapsed() - d->headphonesLastRequestMs)));
        QTimer::singleShot(delay, this, [this, connection, revision] {
            if (!d->headphonesRetryPending || connection != d->connectionId
                || revision != d->headphonesRevision) { return; }
            d->headphonesRetryPending = false;
            requestHeadphonesAudio();
        });
    }
    reconcileLinkTrial();
    refreshAudioStatus();
}

void RemoteMediaController::onHeadphonesError(const QString& reason,
                                              RemoteAudioReceiver::Fault fault)
{
    // The headphones device failed on this computer. Only the headphones
    // stop: the speakers' receiver and stream are untouched.
    qCWarning(lcRemoteMedia).noquote()
        << QStringLiteral("Remote headphones playback failed: %1").arg(reason);
    d->headphones->stop();
    d->headphonesFaulted = true;
    d->headphonesRetryPending = false;
    const QPointer<RemoteMediaController> self(this);
    // headphonesWanted() is false now: the Core is asked to stop the mix.
    requestHeadphonesAudio();
    if (!self) { return; }
    const QString text = headphonesFaultText(fault);
    setHeadphonesProblem(text);
    if (!self) { return; }
    reconcileLinkTrial();
    refreshAudioStatus();
    if (!self) { return; }
    emit errorOccurred(text);
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
    // R-R3-23: the choice, what Core runs, and why Lossless is not running.
    status.chosenProfile = d->audioProfileChoice;
    status.profileChoiceAvailable = inputs.mediaSession && audioProfileNegotiated();
    if (const auto& context = d->acceptedAudioContext) {
        if (context->profile) {
            status.runningProfile = context->profile;
        } else if (context->enabled) {
            status.runningProfile = RemoteAudioProfile::Opus; // a Core without the choice
        }
        status.losslessEncoder = context->losslessEncoder;
    }
    if (d->audioProfileChoice == RemoteAudioProfile::Lossless && inputs.mediaSession) {
        if (d->losslessFallback) {
            status.qualityReason = RemoteAudioQualityReason::NetworkTooSlow;
        } else if (!audioProfileNegotiated()) {
            status.qualityReason = RemoteAudioQualityReason::CoreCannotSend;
        } else if (d->acceptedAudioContext && d->acceptedAudioContext->profileRefusal) {
            status.qualityReason =
                *d->acceptedAudioContext->profileRefusal == RemoteAudioProfileRefusal::NotAllowed
                ? RemoteAudioQualityReason::CoreNotAllowed
                : RemoteAudioQualityReason::ConnectionUnavailable;
        }
    }

    // R-R3-43: each wanted receiver stream, by slice id.
    for (const auto& [sliceId, stream] : d->receiverStreams) {
        RemoteReceiverAudioStatus receiver;
        receiver.sliceId = sliceId;
        if (stream.stopped) {
            receiver.state = RemoteReceiverAudioStatus::State::Stopped;
            receiver.stopReason = stream.stopReason;
        } else if (stream.receiver->isRunning()) {
            receiver.state = RemoteReceiverAudioStatus::State::Receiving;
            receiver.runningProfile = stream.runningProfile;
            if (stream.runningProfile == RemoteAudioProfile::Opus && stream.context) {
                receiver.encoder = stream.context->encoder;
            }
        }
        status.receivers.append(receiver);
    }

    reconcileClockProbe();
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
    d->establishTimer->stop();
    d->awaitingDescription = false;
    d->ps3Generation = 0;
    d->ps3Assembler.reset(0);
    d->timer->stop();
    d->audio->stop();
    d->audioEnabled = false;
    d->audioRetryPending = false;
    d->audioRevision = 0;
    d->audioGeneration = 0;
    d->acceptedAudioContext.reset();
    // The choice outlives the session and is replayed on the next one; a
    // fallback and its trial belong to this one.
    d->audioProfileRequested = false;
    d->losslessFallback = false;
    d->linkTrial.end();
    d->linkTrialTimer->stop();
    // R-R3-35: the clock offset and the Core's capture belong to this
    // session; a reconnect measures afresh.
    d->clockProbeTimer->stop();
    d->clockEstimator.reset();
    d->captureAnchor.reset();
    d->pendingClockProbes.clear();
    // A playback problem belongs to its session and ends with it.
    d->audioFailure.reset();
    d->audioRestarting = false;
    // R-R3-43: receiver streams stop with the media connection. Their
    // sinks stay registered and are asked for again on the next one.
    d->receiverRevisions.clear();
    d->receiverSsrcs.clear();
    // R-R3-45: the headphones mix stops with the media connection and is
    // asked for again on the next one. A headphones device that failed
    // stays failed (fix wave): it is asked for again only when the device
    // or its configuration changes, not on every reconnect.
    d->headphones->stop();
    d->headphonesSsrc = 0;
    d->headphonesContext.reset();
    d->headphonesRevision = 0;
    d->headphonesGeneration = 0;
    d->headphonesRequested = false;
    d->headphonesRetryPending = false;
    QList<int> interrupted;
    for (auto& [sliceId, stream] : d->receiverStreams) {
        stream.receiver->stop();
        stream.ssrc = 0;
        stream.generation = 0;
        stream.context.reset();
        stream.runningProfile.reset();
        stream.heldBy.reset();
        stream.faulted = false;
        stream.retryPending = false;
        interrupted.append(sliceId);
    }
    d->connectionId.clear();
    d->pendingPs3.reset();
    d->ps3Refused = false;
    d->ps3RefusalReason.clear();
    d->ps3RefusedGeneration = 0;
    d->accountedPs3 = false;
    d->allocationCacheIdentity.clear();
    d->cachedAllocation.reset();
    d->cachedAllocationError.clear();
    d->askingWanted = false;
    d->wantedCharge = {};
    d->wantedIntents.clear();
    d->resizeAsk.reset();
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
    d->centreRequests.clear();
    const QPointer<RemoteMediaController> self(this);
    if (!d->destroying) {
        // The Core's reasons end with the session; a failed device's stays.
        if (!d->headphonesFaulted) {
            setHeadphonesProblem(QString());
            if (!self) { return; }
        }
        for (int sliceId : interrupted) {
            notifyReceiverStopped(sliceId, remoteAudioOffReasonToWire(
                RemoteAudioOffReason::MediaNotReady));
            if (!self) { return; }
        }
    }
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
            setPanStatus(panId, PanDisplayState{});
            if (!self) { return; }
        }
    }
    // Endpoints retired before this point left their pans; drop any grant
    // line they still show, keeping the rest of each pan's status.
    for (const QString& panId : d->panBaseStatus.keys()) {
        refreshPanGrantStatus(panId);
        if (!self) { return; }
    }
    d->panBaseStatus.clear();
    d->panBudgetReason.clear();
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

void RemoteMediaController::settleWithoutRetry(quint32 expectedEpoch, const QString& reason)
{
    // R-R3-28, review I1. Media ends here for good, with no retry, and the
    // session carries on as control only, which its handshake has already
    // proven: the reconnect backoff starts over, so a later, unrelated drop
    // retries at the first step. Epoch-scoped in StationClient, so a
    // retired session's error cannot reset a newer session's schedule.
    if (d->client) { d->client->noteMediaEstablished(expectedEpoch); }
    const QPointer<RemoteMediaController> self(this);
    stop();
    if (!self) { return; }
    emit errorOccurred(reason);
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
    d->displayDropsReported = 0;
    d->displayDropsReportedAtMs = 0;
    connect(peer, &MediaPeer::displayReceived, this, [this, current](const QByteArray& packet) {
        if (!current()) { return; }
        // Rendering can end this session; report drops for it first.
        reportDisplayDrops();
        receiveDisplay(packet);
    });
    connect(peer, &MediaPeer::rtpReceived, this, [this, current](const QByteArray& packet) {
        if (!current()) { return; }
        // R-R3-43: split by stream id before the speakers' gate, so a
        // receiver stream reaches its consumers while the speakers are
        // muted. A receiver stream nobody plays now (stopping, restarting)
        // is dropped; everything else goes to the speakers' receiver as
        // before.
        if ((!d->receiverSsrcs.isEmpty() || d->headphonesSsrc != 0) && packet.size() >= 12) {
            const quint32 ssrc = qFromBigEndian<quint32>(packet.constData() + 8);
            // R-R3-45: the headphones mix goes to its own receiver, whatever
            // the speakers do; while that receiver is stopped it is dropped.
            if (d->headphonesSsrc != 0 && ssrc == d->headphonesSsrc) {
                if (d->headphones->isRunning()) { d->headphones->submit(packet); }
                return;
            }
            if (d->receiverSsrcs.contains(ssrc)) {
                for (auto& [sliceId, stream] : d->receiverStreams) {
                    if (stream.ssrc == ssrc) {
                        stream.receiver->submit(packet);
                        break;
                    }
                }
                return;
            }
        }
        if (d->audioEnabled) { d->audio->submit(packet); }
    });
    connect(peer, &MediaPeer::ready, this, [this, current, epoch] {
        if (current()) {
            // Established: the deadline stands down, and only now does the
            // session count as working for the reconnect backoff (R-R3-28).
            d->establishTimer->stop();
            d->client->noteMediaEstablished(epoch);
            if (!d->client->remoteDisplayBudgetLimits()) {
                qCDebug(lcRemoteMedia)
                    << "Core supplied no aggregate display limits; using per-display subscriptions";
            }
            d->timer->start();
            refreshSubscriptions();
            const QPointer<RemoteMediaController> self(this);
            requestAudio();
            if (!self || !current()) { return; }
            // R-R3-43: each receiver stream an app wants, after the mix.
            requestWantedReceiverAudio();
            if (!self || !current()) { return; }
            // R-R3-45: and the headphones mix, when headphones are here.
            requestHeadphonesAudio();
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
    const auto onPeerError = [this, current, epoch](const QString& reason) {
        if (current()) {
            if (d->startingPeer) {
                // Decided below, once start() says whether it refused.
                if (d->startRefusal.isEmpty()) { d->startRefusal = reason; }
                return;
            }
            settleWithoutRetry(epoch, reason);
        }
    };
    connect(peer, &MediaPeer::errorOccurred, this, onPeerError);
    // The transport reports a display-channel error only as a display
    // error; this computer handles it as it handles any media error.
    connect(peer, &MediaPeer::displayErrorOccurred, this, onPeerError);
    d->startingPeer = true;
    d->startRefusal.clear();
    const QPointer<MediaPeer> startedPeer(peer);
    // R-R3-43: the receiver stream ids are declared only when this GUI will
    // say so in its start below.
    // R-R3-45: likewise the headphones mix's stream id.
    const bool started = peer->start(IMediaTransport::Role::Answerer, d->connectionId,
                                     IMediaTransport::kDefaultAudioTargetBitrate,
                                     /*offerLosslessAudio=*/false, receiverAudioNegotiated(),
                                     headphonesMixNegotiated());
    if (!self) { return; }
    d->startingPeer = false;
    d->receiverSsrcs = started && startedPeer ? startedPeer->receiverAudioSsrcs() : QList<quint32>{};
    d->headphonesSsrc = started && startedPeer ? startedPeer->headphonesAudioSsrc() : 0;
    const QString startError = std::exchange(d->startRefusal, QString());
    if (!started) {
        // R-R3-28, amended 2026-09-23: a refusal is never a silent stop, and
        // only a transport that could not be built is retried, through the
        // same path as any media failure, until retries reach the backoff
        // ceiling. Every other refusal is permanent, as is a transient one
        // past the ceiling: stop, show the reason, keep control.
        const QString base = QStringLiteral("Station media could not start on this computer");
        const QString reason = startError.isEmpty()
            ? base : QStringLiteral("%1: %2").arg(base, startError);
        const bool transient = startedPeer
            && startedPeer->lastStartRefusal()
                == MediaPeer::StartRefusal::TransportConstructionFailed;
        if (transient && !d->client->reconnectBackoffExhausted()) {
            requestRecovery(epoch, reason);
            return;
        }
        if (transient) {
            qCWarning(lcRemoteMedia).noquote()
                << QStringLiteral("%1; automatic retries stopped at the longest wait,"
                                  " the connection stays up without media").arg(reason);
        }
        settleWithoutRetry(epoch, reason);
        return;
    }
    if (!startError.isEmpty()) {
        // Started, but reported an error on the way: handled exactly as an
        // error after start always has been.
        settleWithoutRetry(epoch, startError);
        return;
    }
    // Stage one: Core's description (receiveControl() starts stage two).
    d->awaitingDescription = true;
    d->establishTimer->start(d->descriptionDeadlineMs);
    // R-R3-23: a Core that can send lossless audio offers it only to a GUI
    // that says it understands it; every other Core sees today's start.
    QJsonObject startControl{{QStringLiteral("op"), QStringLiteral("start")}};
    if (audioProfileNegotiated()) {
        startControl.insert(QStringLiteral("audioProfileVersion"), 1);
    }
    // R-R3-43: likewise receiver audio, only to a Core that offers it.
    if (receiverAudioNegotiated()) {
        startControl.insert(QStringLiteral("receiverAudioVersion"), 1);
    }
    // R-R3-45: likewise the headphones mix, only to a Core that offers it.
    if (headphonesMixNegotiated()) {
        startControl.insert(QStringLiteral("headphonesMixVersion"), 1);
    }
    send(startControl);
    if (!self) { return; }
    // R-R3-45: a Core that cannot send the headphones mix says so on every
    // flag routed to the headphones.
    if (!headphonesMixNegotiated()) {
        setHeadphonesProblem(QString::fromLatin1(kHeadphonesMixUnavailableReason));
    } else if (!d->headphonesFaulted) {
        setHeadphonesProblem(QString());
    }
    if (!self) { return; }
    if (!receiverAudioNegotiated()) {
        // An older Core: no request goes out, and each consumer is told why.
        QList<int> wanted;
        for (const auto& [sliceId, stream] : d->receiverStreams) { wanted.append(sliceId); }
        for (int sliceId : wanted) {
            notifyReceiverStopped(sliceId, QString::fromLatin1(kReceiverAudioUnavailableReason));
            if (!self) { return; }
        }
    }
    // A media session now exists: audio is awaited from Core.
    refreshAudioStatus();
}

bool RemoteMediaController::send(QJsonObject payload)
{
    if (!d->client || d->connectionId.isEmpty()) { return false; }
    payload.insert(QStringLiteral("connectionId"), d->connectionId);
    return d->client->sendMediaControl(payload, d->epoch);
}

bool RemoteMediaController::sendRefusedRelease(quint32 endpointId, quint32 lastRevision)
{
    // The binding is already gone here: its answer finds no binding and is
    // ignored. False when the session ended while sending.
    const QPointer<RemoteMediaController> self(this);
    const QPointer<MediaPeer> peer = d->peer;
    const quint32 epoch = d->epoch;
    const QString connectionId = d->connectionId;
    quint32 revision = lastRevision + 1;
    if (revision == 0) { ++revision; }
    send({{QStringLiteral("op"), QStringLiteral("unsubscribe")},
          {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
          {QStringLiteral("revision"), static_cast<qint64>(revision)}});
    return self && d->peer == peer && d->epoch == epoch && d->connectionId == connectionId;
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
            const QString panId = found->second.panId;
            if (widget) {
                widget->clearRemoteSpectrum();
                if (!self || !widget) { return false; }
                widget->applyRemoteCtunState(false, false);
                if (!self) { return false; }
            }
            refreshPanGrantStatus(panId);
            if (!self) { return false; }
            found = d->bindings.find(id);
            if (found == d->bindings.end()) { continue; }
            if (found->second.pending) {
                continue;
            }
            if (found->second.acceptedRevision == 0) {
                // Fix wave 2 (Important 2): a display the Core refused
                // still counts in this device's request there until it is
                // asked for again or closed, so close it.
                const quint32 sent = found->second.revision;
                d->bindings.erase(found);
                if (sent != 0 && !sendRefusedRelease(id, sent)) { return false; }
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
        const QString panId = found->second.panId;
        // Retire local ownership before sending: a synchronous transport
        // failure can end the session and clear every binding inside send().
        d->bindings.erase(found);
        if (widget) {
            widget->clearRemoteSpectrum();
            if (!self || !widget) { return false; }
            widget->applyRemoteCtunState(false, false);
            if (!self) { return false; }
        }
        refreshPanGrantStatus(panId);
        if (!self) { return false; }
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
            // Only a limited grant or a refusal is shown.
            if (applet->panId().isEmpty()) {
                applet->setRemoteDisplayStatus(PanStatusText{});
            } else {
                setPanStatus(applet->panId(), perPanRefusalStatus(applet->panId()));
            }
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
                    requestCentreFromGesture(id, centreHz);
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
        binding.refusalReason.clear();
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

void RemoteMediaController::setPanStatus(const QString& panId, const PanDisplayState& status)
{
    if (!d->stack || panId.isEmpty()) { return; }
    d->panBaseStatus.insert(panId, status);
    for (PanadapterApplet* applet : d->stack->allApplets()) {
        if (applet && applet->panId() == panId) {
            applet->setRemoteDisplayStatus(buildPanStatusText(statusWithGrant(panId, status)));
            return;
        }
    }
}

PanDisplayState RemoteMediaController::panDisplayState(const QString& panId) const
{
    return statusWithGrant(panId, d->panBaseStatus.value(panId));
}

void RemoteMediaController::refreshPanGrantStatus(const QString& panId)
{
    setPanStatus(panId, d->panBaseStatus.value(panId));
}

PanDisplayState RemoteMediaController::perPanRefusalStatus(const QString& panId) const
{
    // R-R3-01/08/37: outside budget mode a refused pan keeps the budget-mode
    // line while the refused request is still the one it would send. A new
    // request clears the reason when it goes out.
    if (!d->model || !d->stack || !d->client) { return {}; }
    for (const auto& [id, binding] : d->bindings) {
        Q_UNUSED(id);
        if (binding.panId != panId || !binding.rejected || binding.refusalReason.isEmpty()
            || !binding.widget || !binding.slice
            || currentSliceForPan(d->model, d->stack, binding.widget) != binding.slice
            || binding.observedStream != binding.slice->streamIndex()
            || binding.observedStreamEpoch != binding.slice->streamEpoch()
            || requestFor(binding.widget, binding.slice,
                          d->client->remoteWidebandAvailable()) != binding.observed) {
            continue;
        }
        return refusedState(binding.refusalReason.left(384));
    }
    return {};
}

PanDisplayState RemoteMediaController::statusWithGrant(const QString& panId,
                                                      PanDisplayState status) const
{
    // Only a live, accepted endpoint's grant is shown: the limit goes when
    // the grant is no longer limited or the endpoint does.
    status.zoomLimit = PanDisplayState::ZoomLimit::None;
    status.zoomPoints = 0;
    for (const auto& [id, binding] : d->bindings) {
        if (binding.panId == panId && binding.accepted && !binding.retiring
            && !binding.suspending && applyGrantLimit(status, binding.grant)) {
            break;
        }
    }
    return status;
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
            qCInfo(lcRemoteMedia).noquote() << "Remote PureSignal display does not fit:"
                                            << reason;
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
            setPanStatus(binding.panId, phaseState(PanDisplayState::Phase::ChangingWindow));
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
    // Fix wave 2 (Critical 1, ruling 9.3): ask for what the operator wants
    // whenever that grows. The Core counts what a device asks for when it
    // splits the budget, so a planner that only ever asked for what its
    // share allows would never show its demand, and a device that joined
    // second would stay at the share it was first given.
    {
        QList<DisplayBudgetCharge> wantedCharges;
        for (const RemoteDisplayIntent& intent : intents) {
            if (const auto cost = spectrumDisplayCost(intent.pixels, intent.fps,
                                                      intent.includeWidePlane)) {
                wantedCharges.append(cost->charge);
            }
        }
        const DisplayBudgetCharge wanted =
            sumDisplayCharges(wantedCharges).value_or(DisplayBudgetCharge{});
        // Fix wave 3 (Minor 3): a resize (the same pans at the same frame
        // rates, only their widths changed) asks once its widths have
        // settled for kResizeSettleMs, not at every step of a drag; any
        // other growth asks at once.
        bool sameDisplays = !d->wantedIntents.isEmpty()
            && d->wantedIntents.size() == intents.size();
        bool widthsMoved = false;
        for (qsizetype i = 0; sameDisplays && i < intents.size(); ++i) {
            const RemoteDisplayIntent& was = d->wantedIntents.at(i);
            const RemoteDisplayIntent& is = intents.at(i);
            sameDisplays = was.panId == is.panId && was.fps == is.fps
                && was.includeWidePlane == is.includeWidePlane;
            widthsMoved = widthsMoved || was.pixels != is.pixels;
        }
        if (d->resizeAsk && !sameDisplays) {
            // The displays changed shape mid-resize: the resize's growth is
            // asked for now, with whatever else changed.
            if (!nonIncreasing(wanted, d->resizeAsk->base)) {
                d->askingWanted = true;
            }
            d->resizeAsk.reset();
        } else if (d->resizeAsk && widthsMoved) {
            d->resizeAsk->movedAtMs = now;
        }
        if (!nonIncreasing(wanted, d->wantedCharge)) {
            if (sameDisplays) {
                if (!d->resizeAsk) {
                    d->resizeAsk = Private::ResizeAsk{d->wantedCharge, now};
                }
            } else {
                d->askingWanted = true;
            }
        }
        if (d->resizeAsk && now - d->resizeAsk->movedAtMs >= kResizeSettleMs) {
            if (!nonIncreasing(wanted, d->resizeAsk->base)) {
                d->askingWanted = true;
            }
            d->resizeAsk.reset();
        }
        d->wantedCharge = wanted;
        d->wantedIntents = intents;
    }
    const bool askWanted = d->askingWanted && !retainedPs3ExceedsCap;
    // While asking, plan without the share: every pan at its wanted quality.
    // The Core admits or refuses each against the share it gives this
    // device with the new request.
    const DisplayBudgetLimits planLimits = askWanted
        ? DisplayBudgetLimits{kDisplayBudgetJsonSafePositiveLimit,
                              kDisplayBudgetJsonSafePositiveLimit, limits->generation}
        : *limits;
    const QString cacheIdentity = allocationIdentity(
        planLimits, intents, targetPs3, retainedPs3ExceedsCap);
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
                planLimits, intents, targetPs3, &d->cachedAllocationError);
            if (!d->cachedAllocation) {
                qCInfo(lcRemoteMedia).noquote() << "Remote display allocation failed:"
                                                << d->cachedAllocationError;
            }
        }
    }
    const std::optional<RemoteDisplayAllocation>& allocation = d->cachedAllocation;
    if (!allocation) {
        const PanDisplayState status = refusedState(d->cachedAllocationError.left(384));
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

    // R-R3-08/37: a reduction the Core made because it is busy says so;
    // any other reduction keeps the general capacity wording. The pan status
    // builder words it from PanDisplayState::budgetReason.
    const DisplayBudgetReason budgetReason = d->client->remoteDisplayBudgetReason();
    const auto statusFor = [now](const Private::Binding& binding, const Desired& item) {
        if (binding.pending && binding.pending->timedOut) {
            return phaseState(PanDisplayState::Phase::Stalled);
        }
        if (!binding.refusalReason.isEmpty()) {
            return refusedState(binding.refusalReason.left(384));
        }
        if (binding.acceptedRevision == 0 || binding.acceptedRequest.isEmpty()) {
            // R-R3-37: no "Waiting for the Core" flash while the Core
            // answers within the grace, as it does at session start.
            if (binding.waitingSinceMs >= 0
                && now - binding.waitingSinceMs < kPanWaitingGraceMs) {
                return phaseState(PanDisplayState::Phase::None);
            }
            return phaseState(PanDisplayState::Phase::Waiting);
        }
        PanDisplayState status = phaseState(PanDisplayState::Phase::Showing);
        status.pixels = binding.acceptedRequest.value(QStringLiteral("pixels")).toInt();
        status.fps = binding.acceptedRequest.value(QStringLiteral("fps")).toInt();
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
            status.receivedFps = received;
        }
        status.requestedPixels = item.original.value(QStringLiteral("pixels")).toInt();
        status.requestedFps = item.original.value(QStringLiteral("fps")).toInt();
        status.extendedView =
            binding.acceptedRequest.value(QStringLiteral("wideSpanFactor")).toDouble() > 1.0;
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
                             refusedState(found->second.refusalReason.left(384)));
                continue;
            }
            d->panBudgetReason.insert(item.panId, budgetReason);
            PanDisplayState paused = phaseState(PanDisplayState::Phase::Paused);
            paused.pureSignalOverLimit = retainedPs3ExceedsCap;
            paused.budgetReason = budgetReason;
            setPanStatus(item.panId, paused);
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
                setPanStatus(item.panId, phaseState(PanDisplayState::Phase::TooManyPans));
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
                    requestCentreFromGesture(endpointId, centreHz);
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
        if (binding.acceptedRevision == 0 || binding.acceptedRequest.isEmpty()) {
            if (binding.waitingSinceMs < 0) { binding.waitingSinceMs = now; }
        } else {
            binding.waitingSinceMs = -1;
        }
        PanDisplayState status = statusFor(binding, item);
        status.budgetReason = status.reduced() ? budgetReason : DisplayBudgetReason::None;
        d->panBudgetReason.insert(item.panId, status.budgetReason);
        if (d->ps3Refused && !d->ps3RefusalReason.isEmpty()) {
            status.pureSignal = PanDisplayState::PureSignal::Refused;
            status.pureSignalRefusalReason = d->ps3RefusalReason.left(256);
        } else if (d->pendingPs3 && d->pendingPs3->timedOut) {
            status.pureSignal = PanDisplayState::PureSignal::Stalled;
        }
        setPanStatus(item.panId, status);
        if (!self) { return; }
        if (binding.pending || binding.refusedIdentity == identity) { continue; }
        if (binding.askAgain && binding.acceptedRequest != target) {
            binding.askAgain = false; // A new request supersedes it.
        }
        if (binding.acceptedRevision != 0 && binding.acceptedRequest == target
            && !binding.askAgain
            && binding.observedStream == item.slice->streamIndex()
            && binding.observedStreamEpoch == item.slice->streamEpoch()) {
            continue;
        }
        Candidate candidate{found->first, target, quality.charge, identity, false,
                            binding.acceptedRevision != 0
                                && nonIncreasing(quality.charge, binding.acceptedCharge)};
        (candidate.reduction ? reductions : increases).append(std::move(candidate));
    }

    for (quint32 endpointId : eraseUnaccepted) {
        // Fix wave 2 (Important 2): a pan paused before the Core accepted
        // it; a display the Core refused is closed there too, so its
        // request stops counting against the other devices.
        const auto found = d->bindings.find(endpointId);
        const quint32 sent = found == d->bindings.end() ? 0 : found->second.revision;
        d->bindings.erase(endpointId);
        if (sent != 0 && !sendRefusedRelease(endpointId, sent)) { return; }
    }

    if (askWanted && reductions.isEmpty() && increases.isEmpty()
        && std::none_of(d->bindings.cbegin(), d->bindings.cend(),
                        [](const auto& entry) { return entry.second.pending.has_value(); })) {
        // Every pan was answered at what the operator wants (or refused for
        // a reason other than the budget, which asking again does not
        // change): the ask is over, and the planner plans inside the share.
        d->askingWanted = false;
        d->budgetReplanRequested = true;
    }

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
        if (binding.askAgain) {
            binding.askAgain = false;
            binding.askedAgain = candidate.request;
        }
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
        if (askWanted) {
            // Asking for what the operator wants: the Core decides.
            sendCandidate(candidate);
            if (!current()) { return; }
            continue;
        }
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

void RemoteMediaController::requestCentreFromGesture(quint32 endpointId, double centreHz)
{
    auto current = d->bindings.find(endpointId);
    if (current == d->bindings.end() || !current->second.widget || !d->model) { return; }
    SpectrumWidget* const widget = current->second.widget;
    SliceModel* slice = currentSliceForPan(d->model, d->stack, widget);
    if (!slice || slice->streamIndex() < 0) { return; }
    const int stream = slice->streamIndex();
    // R-R3-18/19: a zoom re-centres the view on the VFO. With other
    // receivers on this stream the Core keeps its window where it is (it
    // refuses a centre that would drop one of them), so the zoom stays a
    // view change. On a stream of its own the VFO is always a valid centre,
    // and the Core follows the zoom as before.
    if (widget->isZoomRecentring() && d->model->slicesOnStream(stream).size() > 1) {
        return;
    }
    auto& request = d->centreRequests[stream];
    if (request.epoch != slice->streamEpoch()) {
        request = {};
        request.epoch = slice->streamEpoch();
    }
    const double wantedHz = std::round(centreHz);
    if (request.inFlight) {
        request.hasQueued = true;
        request.queuedSliceId = slice->sliceIndex();
        request.queuedHz = wantedHz;
        return;
    }
    request.inFlight = d->model->requestStreamCentre(slice->sliceIndex(), wantedHz);
}

void RemoteMediaController::finishCentreRequest(int sliceId, quint64 streamEpoch, bool accepted)
{
    if (!d->model) { return; }
    SliceModel* slice = d->model->sliceById(sliceId);
    if (!slice || slice->streamIndex() < 0) { return; }
    const int stream = slice->streamIndex();
    auto request = d->centreRequests.find(stream);
    if (request != d->centreRequests.end() && request->epoch == streamEpoch) {
        request->inFlight = false;
        if (accepted && request->hasQueued && slice->streamEpoch() == streamEpoch) {
            // The gesture moved on while the Core answered: ask for where
            // it is now, once.
            const int queuedSliceId = request->queuedSliceId;
            const double queuedHz = request->queuedHz;
            request->hasQueued = false;
            request->inFlight = d->model->requestStreamCentre(queuedSliceId, queuedHz);
            return;
        }
        request->hasQueued = false;
    }
    if (accepted || slice->streamEpoch() != streamEpoch) { return; }
    const QPointer<RemoteMediaController> self(this);
    QList<quint32> endpointIds;
    for (const auto& [id, binding] : d->bindings) { endpointIds.append(id); }
    for (quint32 id : endpointIds) {
        auto found = d->bindings.find(id);
        if (found == d->bindings.end()) { continue; }
        Private::Binding& binding = found->second;
        if (!binding.widget || !binding.slice
            || binding.slice->streamIndex() != stream
            || binding.slice->streamEpoch() != streamEpoch
            || binding.sourceCentreHz <= 0) { continue; }
        // A drag is optimistic view movement. A refused hardware move
        // returns the view to the last accepted Core source and ends that
        // drag, so the rest of the gesture neither asks again nor moves the
        // view away from what the Core is sending. The current plane is
        // kept: the view's own move retires it when the geometry changes,
        // and the subscription observer follows the restored window.
        QPointer<SpectrumWidget> widget = binding.widget;
        const double sourceCentreHz = binding.sourceCentreHz;
        widget->endPanDrag();
        if (!self || !widget) { return; }
        widget->setDisplayWindowPreservingHistory(sourceCentreHz, widget->bandwidth());
        if (!self || !widget) { return; }
        widget->setDdcCenterFrequency(sourceCentreHz);
        if (!self) { return; }
    }
    refreshSubscriptions();
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
    for (auto it = d->centreRequests.begin(); it != d->centreRequests.end();) {
        if (!occupied.contains(it.key())) { it = d->centreRequests.erase(it); }
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
    QJsonObject control{{QStringLiteral("op"), QStringLiteral("audio")},
                        {QStringLiteral("revision"), double(d->audioRevision)},
                        {QStringLiteral("enabled"), enabled}};
    // R-R3-23: the choice, replayed with every request, only to a Core that
    // offers it; Opus after this session's link trial failed.
    if (audioProfileNegotiated()) {
        control.insert(QStringLiteral("profile"), remoteAudioProfileToWire(
            d->audioProfileChoice == RemoteAudioProfile::Lossless && !d->losslessFallback
                ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus));
        d->audioProfileRequested = true;
    }
    send(control);
    if (!self) { return; }
    refreshAudioStatus();
}

// ---- R-R3-43: receiver audio streams for apps on this computer ----

void RemoteMediaController::requestReceiverAudio(int sliceId, IReceiverPcmSink* sink)
{
    if (!sink || sliceId < 0) { return; }
    auto found = d->receiverStreams.find(sliceId);
    const bool first = found == d->receiverStreams.end();
    if (first) {
        Private::ReceiverStream stream;
        stream.fanout = std::make_shared<Private::ReceiverFanout>();
        stream.fanout->sliceId = sliceId;
        const std::shared_ptr<Private::ReceiverFanout> fanout = stream.fanout;
        // Runs on the receiver's worker thread; see IReceiverPcmSink.
        stream.receiver = std::make_unique<RemoteAudioReceiver>(
            RemoteAudioReceiver::PcmSinkMode{[fanout](const float* pcm, int frames) {
                std::lock_guard<std::mutex> lock(fanout->mutex);
                for (IReceiverPcmSink* consumer : std::as_const(fanout->sinks)) {
                    consumer->receiverAudioBlock(fanout->sliceId, pcm, frames);
                }
            }});
        RemoteAudioReceiver* const receiver = stream.receiver.get();
        connect(receiver, &RemoteAudioReceiver::restartRequested, this,
                [this, sliceId, receiver](const QString& reason, RemoteAudioReceiver::Fault fault) {
            onReceiverRestart(sliceId, receiver, reason, fault);
        });
        connect(receiver, &RemoteAudioReceiver::errorOccurred, this,
                [this, sliceId, receiver](const QString& reason, RemoteAudioReceiver::Fault fault) {
            onReceiverError(sliceId, receiver, reason, fault);
        });
        found = d->receiverStreams.emplace(sliceId, std::move(stream)).first;
    } else if (found->second.sinks.contains(sink)) {
        return;
    }
    Private::ReceiverStream& stream = found->second;
    stream.sinks.append(sink);
    {
        std::lock_guard<std::mutex> lock(stream.fanout->mutex);
        stream.fanout->sinks.append(sink);
    }
    const QPointer<RemoteMediaController> self(this);
    if (first || stream.heldBy) {
        // A new consumer is a new demand: a stream the Core forgot (its
        // slice was gone, or it had no room) is asked for again, once.
        if (!d->peer) {
            notifyReceiverStopped(sliceId, remoteAudioOffReasonToWire(
                RemoteAudioOffReason::MediaNotReady));
        } else if (!receiverAudioNegotiated()) {
            notifyReceiverStopped(sliceId, QString::fromLatin1(kReceiverAudioUnavailableReason));
        } else if (d->peer->isReady()) {
            sendReceiverAudioRequest(sliceId, true);
        }
    } else if (stream.stopped) {
        // A later consumer hears why the shared stream is stopped.
        const QString reason = stream.stopReason;
        sink->receiverAudioStopped(sliceId, reason);
    }
    if (!self) { return; }
    refreshAudioStatus();
}

void RemoteMediaController::releaseReceiverAudio(int sliceId, IReceiverPcmSink* sink)
{
    const auto found = d->receiverStreams.find(sliceId);
    if (found == d->receiverStreams.end() || !found->second.sinks.contains(sink)) { return; }
    Private::ReceiverStream& stream = found->second;
    stream.sinks.removeAll(sink);
    {
        // After this, the worker cannot reach the sink.
        std::lock_guard<std::mutex> lock(stream.fanout->mutex);
        stream.fanout->sinks.removeAll(sink);
    }
    if (!stream.sinks.isEmpty()) { return; }
    // The last consumer went: stop the stream here and at the Core.
    std::unique_ptr<RemoteAudioReceiver> receiver = std::move(stream.receiver);
    receiver->stop();
    disconnect(receiver.get(), nullptr, this, nullptr);
    // This may run inside the receiver's own signal; it goes when that is over.
    receiver.release()->deleteLater();
    const bool askedThisConnection = d->receiverRevisions.contains(sliceId);
    const bool forgotten = stream.heldBy.has_value();
    d->receiverStreams.erase(found);
    const QPointer<RemoteMediaController> self(this);
    if (askedThisConnection && !forgotten && d->peer && d->peer->isReady()
        && receiverAudioNegotiated()) {
        sendReceiverAudioRequest(sliceId, false);
        if (!self) { return; }
    }
    // A stream the Core had no room for may fit now.
    if (d->peer && d->peer->isReady() && receiverAudioNegotiated()) {
        QList<int> waiting;
        for (const auto& [id, other] : d->receiverStreams) {
            if (other.heldBy == RemoteAudioOffReason::ReceiverLimit) { waiting.append(id); }
        }
        for (int id : waiting) {
            if (d->receiverStreams.count(id) == 0) { continue; }
            sendReceiverAudioRequest(id, true);
            if (!self) { return; }
        }
    }
    reconcileLinkTrial();
    refreshAudioStatus();
}

void RemoteMediaController::sendReceiverAudioRequest(int sliceId, bool enabled)
{
    if (!d->peer || !d->peer->isReady() || !receiverAudioNegotiated()) { return; }
    // Never lower than any revision this connection sent for the slice id.
    quint32& revision = d->receiverRevisions[sliceId];
    ++revision;
    if (!revision) { ++revision; }
    const auto found = d->receiverStreams.find(sliceId);
    if (found != d->receiverStreams.end()) {
        found->second.heldBy.reset();
        found->second.retryPending = false;
        found->second.lastRequestMs = d->clock.elapsed();
    }
    // The one quality choice, as the speakers' stream asks for it.
    const RemoteAudioProfile profile =
        d->audioProfileChoice == RemoteAudioProfile::Lossless && !d->losslessFallback
        ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
    send(QJsonObject{{QStringLiteral("op"), QStringLiteral("receiver-audio")},
                     {QStringLiteral("sliceId"), sliceId},
                     {QStringLiteral("revision"), double(revision)},
                     {QStringLiteral("enabled"), enabled},
                     {QStringLiteral("profile"), remoteAudioProfileToWire(profile)}});
}

void RemoteMediaController::requestWantedReceiverAudio()
{
    if (!d->peer || !d->peer->isReady() || !receiverAudioNegotiated()) { return; }
    QList<int> wanted;
    for (const auto& [sliceId, stream] : d->receiverStreams) {
        if (!stream.faulted) { wanted.append(sliceId); }
    }
    const QPointer<RemoteMediaController> self(this);
    for (int sliceId : wanted) {
        if (d->receiverStreams.count(sliceId) == 0) { continue; }
        sendReceiverAudioRequest(sliceId, true);
        if (!self) { return; }
    }
    refreshAudioStatus();
}

void RemoteMediaController::receiveReceiverAudioContext(const QJsonObject& payload)
{
    const std::optional<RemoteReceiverAudioContextMessage> message =
        decodeReceiverAudioContext(payload);
    if (!message) { return; }
    const int sliceId = message->sliceId;
    const auto found = d->receiverStreams.find(sliceId);
    // A released stream's late answer changes nothing.
    if (found == d->receiverStreams.end()) { return; }
    Private::ReceiverStream& stream = found->second;
    const RemoteAudioContextMessage& context = message->context;
    if (context.revision != d->receiverRevisions.value(sliceId)
        || !isNewerGeneration(context.generation, stream.generation)) { return; }
    if (context.enabled && !d->receiverSsrcs.contains(context.ssrc)) { return; }
    stream.generation = context.generation;
    stream.context = context;
    stream.retryPending = false;
    stream.receiver->stop();
    stream.ssrc = 0;
    stream.runningProfile.reset();
    const QPointer<RemoteMediaController> self(this);
    if (context.enabled) {
        const RemoteAudioProfile profile = context.losslessEncoder
            ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
        if (stream.receiver->start(context.ssrc, context.firstTimestamp, profile)) {
            stream.ssrc = context.ssrc;
            stream.runningProfile = profile;
            stream.stopped = false;
            stream.stopReason.clear();
            stream.faulted = false;
            qCInfo(lcRemoteMedia).noquote()
                << QStringLiteral("Remote receiver audio for slice %1: %2, context %3")
                       .arg(sliceId).arg(reportedAudioProfile(context)).arg(context.generation);
        }
    } else {
        const RemoteAudioOffReason reason =
            context.offReason.value_or(RemoteAudioOffReason::EncoderUnavailable);
        if (reason == RemoteAudioOffReason::SliceRemoved
            || reason == RemoteAudioOffReason::ReceiverLimit) {
            // The Core dropped the request; see sliceAdded and release.
            stream.heldBy = reason;
        }
        // The Core confirming a stop this computer made keeps this
        // computer's own reason.
        if (!(reason == RemoteAudioOffReason::ClientDisabled && stream.faulted)) {
            notifyReceiverStopped(sliceId, remoteAudioOffReasonToWire(reason));
            if (!self) { return; }
        }
    }
    reconcileLinkTrial();
    refreshAudioStatus();
}

void RemoteMediaController::notifyReceiverStopped(int sliceId, const QString& reason)
{
    const auto found = d->receiverStreams.find(sliceId);
    if (found == d->receiverStreams.end()) { return; }
    Private::ReceiverStream& stream = found->second;
    if (stream.stopped && stream.stopReason == reason) { return; }
    stream.stopped = true;
    stream.stopReason = reason;
    qCInfo(lcRemoteMedia).noquote()
        << QStringLiteral("Remote receiver audio for slice %1 stopped: %2").arg(sliceId).arg(reason);
    // A consumer may release itself, or another, from inside the notice.
    const QList<IReceiverPcmSink*> sinks = stream.sinks;
    const QPointer<RemoteMediaController> self(this);
    for (IReceiverPcmSink* sink : sinks) {
        const auto still = d->receiverStreams.find(sliceId);
        if (still == d->receiverStreams.end()) { return; }
        if (!still->second.sinks.contains(sink)) { continue; }
        sink->receiverAudioStopped(sliceId, reason);
        if (!self) { return; }
    }
}

void RemoteMediaController::onReceiverRestart(int sliceId, RemoteAudioReceiver* receiver,
                                              const QString& reason,
                                              RemoteAudioReceiver::Fault fault)
{
    const auto found = d->receiverStreams.find(sliceId);
    if (found == d->receiverStreams.end() || found->second.receiver.get() != receiver) { return; }
    Private::ReceiverStream& stream = found->second;
    qCWarning(lcRemoteMedia).noquote()
        << QStringLiteral("Remote receiver audio for slice %1: %2").arg(sliceId).arg(reason);
    stream.receiver->stop();
    stream.ssrc = 0;
    // R-R3-23: a lossless receiver stream's restart counts against the one
    // link trial, as the speakers' does.
    if (stream.context && stream.context->losslessEncoder && d->linkTrial.active()
        && linkInterruption(fault)
        && d->linkTrial.noteInterruption(d->clock.elapsed())
            == RemoteAudioLinkTrial::Verdict::Failed) {
        fallBackToOpus(QStringLiteral("receiver stream restarts while lossless audio plays"));
        return;
    }
    if (!stream.retryPending) {
        stream.retryPending = true;
        const QString connection = d->connectionId;
        const quint32 revision = d->receiverRevisions.value(sliceId);
        const int delay = int(std::max<qint64>(
            0, 1000 - (d->clock.elapsed() - stream.lastRequestMs)));
        QTimer::singleShot(delay, this, [this, sliceId, connection, revision] {
            const auto again = d->receiverStreams.find(sliceId);
            if (again == d->receiverStreams.end() || !again->second.retryPending
                || connection != d->connectionId
                || revision != d->receiverRevisions.value(sliceId)) { return; }
            again->second.retryPending = false;
            sendReceiverAudioRequest(sliceId, true);
        });
    }
    refreshAudioStatus();
}

void RemoteMediaController::onReceiverError(int sliceId, RemoteAudioReceiver* receiver,
                                            const QString& reason,
                                            RemoteAudioReceiver::Fault fault)
{
    const auto found = d->receiverStreams.find(sliceId);
    if (found == d->receiverStreams.end() || found->second.receiver.get() != receiver) { return; }
    Private::ReceiverStream& stream = found->second;
    qCWarning(lcRemoteMedia).noquote()
        << QStringLiteral("Remote receiver audio for slice %1 failed: %2").arg(sliceId).arg(reason);
    stream.receiver->stop();
    stream.ssrc = 0;
    stream.runningProfile.reset();
    stream.faulted = true;
    stream.retryPending = false;
    const QPointer<RemoteMediaController> self(this);
    sendReceiverAudioRequest(sliceId, false);
    if (!self) { return; }
    notifyReceiverStopped(sliceId, remoteAudioProblemText(fault));
    if (!self) { return; }
    reconcileLinkTrial();
    refreshAudioStatus();
}

void RemoteMediaController::reconcileLinkTrial()
{
    // R-R3-23, R-R3-43: one trial while any lossless stream plays.
    bool lossless = d->audioEnabled && d->acceptedAudioContext
        && d->acceptedAudioContext->losslessEncoder;
    for (const auto& [sliceId, stream] : d->receiverStreams) {
        if (stream.receiver->isRunning() && stream.runningProfile == RemoteAudioProfile::Lossless) {
            lossless = true;
        }
    }
    // R-R3-45: the headphones mix counts too.
    if (d->headphones->isRunning() && d->headphonesContext
        && d->headphonesContext->losslessEncoder) {
        lossless = true;
    }
    if (lossless && !d->linkTrial.active()) {
        d->linkTrial.begin(d->clock.elapsed());
        d->linkTrialTimer->start();
    } else if (!lossless && d->linkTrial.active()) {
        d->linkTrial.end();
        d->linkTrialTimer->stop();
    }
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
            // Under the grant report Core charges the pixels it granted,
            // which may be fewer than requested; it never charges more.
            const bool chargeAccepted = spectrumGrantNegotiated()
                ? nonIncreasing(retained, pending.charge) : retained == pending.charge;
            if (acceptedRevision != revision || !chargeAccepted) { return; }
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
            if (reason == QLatin1String(kDisplayBudgetRefusalReason)) {
                // Fix wave 2 (Critical 1): the ask for what the operator
                // wants is answered. The share the Core published with it
                // is what the planner now plans inside.
                d->askingWanted = false;
            }
            // The raw reason is kept for the log; the pan shows it translated.
            qCInfo(lcRemoteMedia).noquote() << "Remote display allocation refused:"
                                            << endpointId << binding.refusalReason;
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
                const DisplayBudgetReason reason = d->client
                    ? d->client->remoteDisplayBudgetReason() : DisplayBudgetReason::None;
                d->panBudgetReason.insert(panId, reason);
                PanDisplayState paused = phaseState(PanDisplayState::Phase::Paused);
                paused.budgetReason = reason;
                setPanStatus(panId, paused);
            }
        } else {
            if (acceptedRevision != binding.acceptedRevision
                || retained != binding.acceptedCharge) { return; }
            binding.pending.reset();
            binding.refusedIdentity = pending.identity;
            binding.refusalReason = reason.isEmpty()
                ? QStringLiteral("Core refused the display release.") : reason;
            qCInfo(lcRemoteMedia).noquote() << "Remote display release refused:"
                                            << endpointId << binding.refusalReason;
        }
    }
    refreshSubscriptions();
}

void RemoteMediaController::receiveControl(const QJsonObject& payload, quint32 epoch)
{
    // R-R3-35: a clock echo's arrival time (t3), read before anything else.
    const qint64 receivedNs = d->audio->nowNs();
    if (!d->client || !d->client->mediaAvailable() || epoch != d->epoch
        || !d->peer || payload.value(QStringLiteral("connectionId")) != d->connectionId) { return; }
    const QString op = payload.value(QStringLiteral("op")).toString();
    if (op == QLatin1String("allocation-result")) {
        receiveAllocationResult(payload);
        return;
    }
    if (op == QLatin1String("clock-echo")) {
        if (audioClockNegotiated()) { receiveClockEcho(payload, receivedNs); }
        return;
    }
    if (op == QLatin1String("audio-context")) {
        // The shape the agreed minor selects, then this session's identity.
        // Anything refused leaves generation, playback and signals untouched.
        const std::optional<RemoteAudioContextMessage> context =
            decodeRemoteAudioContext(payload, audioDetailNegotiated(),
                                     d->audioProfileRequested);
        if (!context || context->revision != d->audioRevision
            || !isNewerGeneration(context->generation, d->audioGeneration)
            || context->ssrc != d->peer->audioSsrc()) { return; }
        d->audioGeneration = context->generation;
        d->acceptedAudioContext = context;
        // R-R3-35: the Core's capture of the previous context no longer
        // describes what plays; the next echo brings this one's.
        d->captureAnchor.reset();
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
            const RemoteAudioProfile profile = context->losslessEncoder
                ? RemoteAudioProfile::Lossless : RemoteAudioProfile::Opus;
            const bool started = d->audio->start(context->ssrc, context->firstTimestamp, profile);
            if (!self) { return; }
            d->audioEnabled = started;
            d->preparingAudio = false;
            if (d->audioEnabled) {
                qCInfo(lcRemoteMedia).noquote()
                    << QStringLiteral("Remote audio receiving: %1, context %2")
                           .arg(reportedAudioProfile(*context))
                           .arg(context->generation);
            }
        }
        // R-R3-23: the link trial runs while lossless audio plays. A
        // restart's new lossless context continues it; anything else ends
        // it (mute, a radio drop, Opus), and lossless later begins anew.
        // R-R3-43: unless a receiver stream still plays lossless.
        reconcileLinkTrial();
        refreshAudioStatus();
        if (!self) { return; }
        emit audioContextAccepted();
        return;
    }
    if (op == QLatin1String("headphones-audio-context")) {
        // R-R3-45: only from a Core this connection declared it to.
        if (headphonesMixNegotiated()) { receiveHeadphonesAudioContext(payload); }
        return;
    }
    if (op == QLatin1String("receiver-audio-context")) {
        if (receiverAudioNegotiated()) { receiveReceiverAudioContext(payload); }
        return;
    }
    if (op == QLatin1String("description") || op == QLatin1String("candidate")) {
        const QPointer<MediaPeer> peer(d->peer);
        const bool accepted = peer->acceptControl(payload);
        if (accepted && op == QLatin1String("description") && d->peer == peer
            && d->awaitingDescription && !peer->isReady()) {
            // Stage two: Core's description is here; the connection now
            // has the pinned library's own slowest failure report.
            d->awaitingDescription = false;
            d->establishTimer->start(d->connectDeadlineMs);
        }
        return;
    }
    // Core refused this connection's media (its peer could not start), or
    // dropped it: op, connectionId, endpointId 0, revision 0 and reason, as
    // DaemonMediaController::sendRejected() sends them.
    if (op == QLatin1String("rejected") && payload.size() == 5
        && payload.value(QStringLiteral("endpointId")).isDouble()
        && payload.value(QStringLiteral("endpointId")).toDouble() == 0
        && payload.value(QStringLiteral("revision")).isDouble()
        && payload.value(QStringLiteral("revision")).toDouble() == 0
        && payload.value(QStringLiteral("reason")).isString()) {
        const QString reason = payload.value(QStringLiteral("reason")).toString().left(512);
        // The Core dropped its peer on its own (the connection failed or
        // closed on its side): start media over now, as when this
        // computer's own peer fails, instead of waiting for it to time out.
        if (reason == QLatin1String(kMediaPeerLostReason)
            || reason == QLatin1String(kMediaPeerClosedReason)) {
            requestRecovery(epoch, reason);
            return;
        }
        settleWithoutRetry(epoch, reason);
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
        // Core's per-endpoint refusal or retirement: op, connectionId,
        // endpointId, revision and reason, as DaemonMediaController::
        // sendRejected() sends them. R-R3-01/08/37: the pan says why, as a
        // status line only (no toast), until its request changes.
        if (!budgetMode && revision == binding.revision && payload.size() == 5
            && payload.value(QStringLiteral("reason")).isString()) {
            const QString reason = payload.value(QStringLiteral("reason")).toString();
            // Core retires an endpoint whose slice it removed or rebound
            // (DaemonMediaController::onSliceRemoved / onStreamBindingsChanged).
            // That state reaches this window over the control session, which
            // is not ordered with the media channel, so the refusal can land
            // while the slice still looks unchanged here. Those are the
            // operator's own changes, not refusals: the pan goes blank and
            // the mirrored change then retires or renews the binding. The
            // wording comes from SpectrumEndpoint.h, the same constants the
            // Core sends. A source retune (kRetireReasonSourceRetune) keeps
            // its status line: a fixed-view pan may not re-request when the
            // mirrored retune lands, and a blank pan needs its reason.
            const bool operatorChange = reason == QLatin1String(kRetireReasonSliceRemoved)
                || reason == QLatin1String(kRetireReasonStreamBindingChanged);
            binding.accepted = false;
            binding.rejected = true;
            binding.refusalReason = operatorChange ? QString()
                : (reason.isEmpty() ? QStringLiteral("Core refused the display allocation.")
                                    : reason.left(512));
            qCInfo(lcRemoteMedia).noquote() << "Remote display endpoint rejected:"
                                            << endpointId << reason.left(512);
            binding.decoder.reset();
            const QPointer<RemoteMediaController> self(this);
            const QString panId = binding.panId;
            binding.widget->clearRemoteSpectrum();
            if (!self) { return; }
            setPanStatus(panId, perPanRefusalStatus(panId));
        }
        return;
    }
    const bool widebandRequested = d->client->remoteWidebandAvailable()
        && (budgetMode ? binding.acceptedRequest : binding.observed)
               .value(QStringLiteral("extendedView")).isBool();
    if (op != QLatin1String("context") || binding.rejected
        || revision != (budgetMode ? binding.acceptedRevision : binding.revision)) { return; }
    // The shape the agreed minor selects, with wideband exactly when this
    // subscription negotiated it.
    const std::optional<SpectrumContextMessage> decoded =
        decodeRemoteSpectrumContext(payload, spectrumGrantNegotiated());
    if (!decoded || decoded->wideband.has_value() != widebandRequested) { return; }
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
    context.codec.contextGeneration = decoded->contextGeneration;
    if (decoded->wideband) { context.wideband = *decoded->wideband; }
    context.exactCentreHz = decoded->centreHz;
    context.exactSpanHz = decoded->spanHz;
    context.wideCentreHz = decoded->wideCentreHz;
    context.wideSpanHz = decoded->wideSpanHz;
    const double sourceCentre = decoded->sourceCentreHz;
    const double rate = decoded->sampleRateHz;
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
    context.codec.traceSamples = quint16(decoded->traceSamples);
    context.codec.waterfallSamples = quint16(decoded->waterfallSamples);
    context.codec.wideSamples = quint16(decoded->wideSamples);
    context.codec.minDbm = float(decoded->minDbm);
    context.codec.maxDbm = float(decoded->maxDbm);
    context.source.streamIndex = decoded->sourceStream;
    context.targetFps = decoded->fps;
    context.framesPerLine = decoded->framesPerLine;
    if (decoded->grant && decoded->grant != binding.grant) {
        qCInfo(lcRemoteMedia).noquote()
            << QStringLiteral("Remote spectrum grant for %1: %2")
                   .arg(binding.panId, grantLogLine(*decoded->grant));
    }
    binding.grant = decoded->grant;
    // A grant short of the request with no limit named is not final: ask
    // for the same request once more. Core then grants it, names a limit,
    // or refuses it (refusedIdentity stops a repeat).
    const bool shortWithoutReason = budgetMode && decoded->grant
        && decoded->grant->limit == SpectrumLimitReason::None
        && decoded->grant->grantedPixels < decoded->grant->requestedPixels;
    binding.askAgain = shortWithoutReason && binding.askedAgain != binding.acceptedRequest;
    const bool askAgain = binding.askAgain;
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
    const QString panId = binding.panId;
    binding.widget->setRemoteSpectrumContext(context, sourceCentre, rate);
    if (!self || d->connectionId != connectionId) { return; }
    // Show or clear this pan's grant line now rather than on the next refresh.
    refreshPanGrantStatus(panId);
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
    if (askAgain) {
        if (!self || d->connectionId != connectionId) { return; }
        refreshSubscriptions();
    }
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
