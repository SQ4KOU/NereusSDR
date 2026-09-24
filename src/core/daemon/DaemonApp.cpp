// =================================================================
// src/core/daemon/DaemonApp.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DaemonApp.h for the design
// rationale (R1 Task 10).
//
// Modification history (NereusSDR):
//   2026-09-20: relay RadioModel connection state without dereferencing a
//               RadioModel being destroyed, by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via OpenAI Codex.
//   2026-09-22: R-R3-36 Task 5: the daemon's RadioModel never demands PC
//               microphone capture, so nereusd never starts the capture
//               helper, by J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-23: display load governor inputs follow thread placement, a
//               step is accepted only once published, and a computed
//               ceiling reaches only apps that know the budget reason
//               (R-R3-08, R-R3-37, R-R3-40), by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-23: the Core's attenuator range is the one the local RX applet
//               uses (BoardCapsTable::stepAttMaxDb, so a board with Alex
//               reaches 61 dB), and its controller saves a change shortly
//               after it is made and sends a band's restored attenuation
//               to the radio (R-R3-46, R-R3-11), by J.J. Boyd (KG4VCF),
//               with AI-assisted implementation via Anthropic Claude Code.
//   2026-09-23: the step attenuator uses RadioModel's feed (slice A's
//               receive band, as Thetis rx1_band) instead of a copy of it
//               (R-R3-46, R-R3-11), by J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-23: R-R3-44: nereusd publishes no VAX devices on the Core
//               host (VAX belongs to the remote window's computer), by
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-24: R-R3-48: the Core runs its own station TCI server
//               (station_tci_bind), by J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "core/daemon/DaemonApp.h"
#include "core/daemon/DaemonAgcSource.h"
#include "core/daemon/DaemonTelemetryController.h"
#include "core/daemon/HostTelemetrySampler.h"
#include "models/ReceiverDspLoadSampler.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/CoreInit.h"
#include "core/FFTRouter.h"
#include "core/LogCategories.h"
#include "core/MoxController.h"
#include "core/BoardCapabilities.h"
#include "core/StepAttenuatorController.h"
#include "core/TxSliceArbiter.h"
#include "core/WdspEngine.h"
#include "core/session/StationServer.h"
#include "core/session/StationLanAnnouncer.h"
#include "core/session/media/DaemonMediaController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QEventLoop>
#include <QHostAddress>
#include <QHostInfo>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

DaemonApp::DaemonApp(QObject* parent)
    : QObject(parent)
{
    m_radioRetryTimer = new QTimer(this);
    m_radioRetryTimer->setSingleShot(true);
    connect(m_radioRetryTimer, &QTimer::timeout,
            this, &DaemonApp::attemptRadioDiscovery);
    m_stationListenRetryTimer = new QTimer(this);
    m_stationListenRetryTimer->setSingleShot(true);
    connect(m_stationListenRetryTimer, &QTimer::timeout,
            this, &DaemonApp::attemptStationServerListen);
}

DaemonApp::~DaemonApp()
{
    stop();
}

bool DaemonApp::start(const DaemonConfig& cfg)
{
    if ((cfg.displayApplicationBytesPerSecond || cfg.spectrumSampleUnitsPerSecond)
        && !cfg.displayBudgetLimits()) {
        QString configurationError;
        cfg.validate(&configurationError);
        qCWarning(lcApp) << "DaemonApp: invalid configuration:" << configurationError;
        return false;
    }
    // Defensive: no test calls start() twice without an intervening
    // stop(), but leaking the previous RadioModel (socket, WdspEngine,
    // discovery timers, ...) would be a silent resource leak the moment
    // one does. Tearing down first makes a second start() behave exactly
    // like stop() + start().
    if (m_radioModel) {
        stop();
    }

    if (m_radioConnectInProgress) {
        return false; // stop is deferred until the nested connection setup unwinds.
    }
    m_stopDeferred = false;
    m_radioConfig = cfg;
    m_selectedRadioMac = cfg.radioMac;
    m_radioAttempted = false;
    m_radioConnectedBefore = false;
    m_radioRetryNextMs = m_radioRetryInitialMs;

    // Idempotent process-wide (CoreInit.cpp's s_initialized guard), so
    // this is a genuine no-op when server_main.cpp already called it
    // before constructing this DaemonApp. Calling it here too means a
    // caller that constructs a DaemonApp directly -- every test in
    // tst_daemon_app.cpp -- does not have to replay that bootstrap step
    // itself.
    NereusSDR::CoreInit::initialize();

    m_radioModel = std::make_unique<RadioModel>();
    // R-R3-36: nereusd never opens a microphone. Set before anything below
    // can connect the model, so no local-session capture demand is ever
    // taken and the capture helper is never started.
    m_radioModel->setPcCaptureAllowed(false);
    // R-R3-44: nor does it publish VAX devices. A remote window's VAX
    // channels are on the operator's computer; outputs here would be
    // devices nothing on the Core host feeds.
    m_radioModel->audioEngine()->setVaxOutputsAllowed(false);
#ifdef NEREUS_BUILD_TESTS
    m_radioModel->wdspEngine()->setSynchronousInitForTest(m_synchronousWdspForTest);
    if (m_radioInitializerForTest) {
        m_radioInitializerForTest(m_radioModel.get());
    }
#endif
    const quint64 runGeneration = m_radioRecoveryGeneration;
    connect(m_radioModel.get(), &RadioModel::connectionStateChanged, this,
            [this, runGeneration](ConnectionState state) {
        if (runGeneration == m_radioRunGeneration) {
            onRadioStateForRecovery(state);
        }
    }, Qt::QueuedConnection);
    m_radioRunGeneration = runGeneration;
    connect(m_radioModel.get(), &RadioModel::radioDisconnectRequested, this, [this]() {
        if (!m_retiringRadio) {
            m_radioRecoveryEnabled = false;
            cancelRadioDiscovery();
        }
    });
    // R3 remote operation is receive-only. Install the station-side policy
    // before controllers, peripherals, slices, or the radio can produce a
    // callback: the daemon owns real hardware even though its model has the
    // normal local role.
    m_radioModel->setReceiveOnlyStationPolicy(true);
    m_radioModel->enableStationAccessoryIdentity();
    // R-R3-48: the Core's own TCI server on the station network, switched
    // by the app's one TCI switch (setStationTci) and kept in the Core's
    // settings. Receive-only until remote transmit (StationTciController).
    m_radioModel->enableStationTci(cfg.stationTciBind);
    m_stepAttController = std::make_unique<StepAttenuatorController>();
    // R-R3-46 / R-R3-11: a change from a remote window is on disk shortly
    // after the Core applies it, not only when the Core stops.
    m_stepAttController->setDebouncedSaveEnabled(true);
    // R-R3-46: a band change on the Core sends that band's attenuation and
    // preamp to the radio, so the radio runs what every window shows.
    m_stepAttController->setBandRestoreToRadio(true);
    m_radioModel->setStepAttController(m_stepAttController.get());
    m_stepAttController->setReceiverManager(m_radioModel->receiverManager());
    if (MoxController* const mox = m_radioModel->moxController()) {
        connect(mox, &MoxController::hardwareFlipped,
                m_stepAttController.get(),
                &StepAttenuatorController::onMoxHardwareFlipped,
                Qt::QueuedConnection);
    }

    m_agcSource = std::make_unique<DaemonAgcSource>(m_radioModel.get());

    // R1 Task 11: dedicated thread for the wideband FFT dispatch hop --
    // RadioModel currently hops that work onto ITS OWN thread to stay off
    // the P2 connection thread's hot path (wireConnectionSignals,
    // RadioModel.cpp), which is fine for the GUI (that own thread IS the
    // main thread, and getting off the network thread onto it is the
    // whole point) but nereusd has no such spare thread of its own -- its
    // RadioModel lives on the daemon's single Qt event-loop thread, which
    // will carry other daemon responsibilities from R1 Task 12 onward.
    //
    // Lazily created once and reused across a restart (see
    // widebandThread()'s doc comment in the header for why stop() joins
    // rather than destroys it): a QThread that has been quit()+wait()'d
    // can be start()ed again.
    if (!m_widebandThread) {
        m_widebandThread = std::make_unique<QThread>();
        m_widebandThread->setObjectName(QStringLiteral("WidebandFftThread"));
    }
    m_widebandThread->start();

    // Injected BEFORE either branch below that might call connectToRadio()
    // (and therefore wireConnectionSignals()), so the very first P2
    // connection's wideband-frame hop already targets the dedicated
    // thread instead of RadioModel's own.
    m_radioModel->setWidebandDispatchThread(m_widebandThread.get());

    // Relay connection-state transitions to this class's own signal.
    // Fires only on a REAL state change (RadioModel emits
    // connectionStateChanged from its connection-thread-marshalled state
    // machine), never synthetically from start() itself.
    connect(m_radioModel.get(), &RadioModel::connectionStateChanged, this,
            [this](NereusSDR::ConnectionState state) {
        if (m_stepAttControllerConfigured && m_stepAttController) {
            if (state == ConnectionState::Connected && m_radioModel
                && m_radioModel->connection()) {
                applyStepAttenuatorConnection(
                    m_radioModel->connection()->radioInfo().macAddress);
            } else {
                m_stepAttController->setRadioConnection(nullptr);
            }
        }
        emit radioConnected(state == ConnectionState::Connected);
    });

    QString radioMac;
#ifdef NEREUS_BUILD_TESTS
    if (m_testBoard.has_value()) {
        // Test-only path -- see primeBoardForTest()'s doc comment in
        // DaemonApp.h for why this exists instead of a real
        // connectToRadio() round trip. Mirrors the exact two calls
        // tst_p1_hl2_rx2_wiring.cpp already uses to prime a RadioModel
        // without a live connection: setBoardForTest() populates
        // boardCapabilities() from the real BoardCapabilities table (the
        // same table applyHpsdrModel() reads from inside connectToRadio()),
        // and configureStreamPool() sizes the stream allocator the same
        // way connectToRadio() does right before creating Slice A.
        m_radioModel->setBoardForTest(*m_testBoard);
        m_radioModel->prepareReceiveLayout(m_testRadioMac);
        const auto& primedCaps = m_radioModel->boardCapabilities();
        const int poolSlices = primedCaps.maxSlices > 0 ? primedCaps.maxSlices : 1;
        m_radioModel->configureStreamPool(primedCaps.userDdcCount, poolSlices,
                                           cfg.sampleRateHz);
        radioMac = m_testRadioMac;
    } else
#endif
    {
        // Bring up the station before asynchronous discovery. An authenticated
        // client can observe the disconnected state while a radio powers up.
        m_radioRecoveryEnabled = true;
        radioMac = cfg.radioMac;
        m_radioModel->prepareReceiveLayout(radioMac);
    }

    createConfiguredSlices(cfg.sliceCount);
#ifdef NEREUS_BUILD_TESTS
    if (m_testBoard.has_value()) {
        m_radioModel->bindUnboundSlices();
        createConfiguredSlices(cfg.sliceCount); // all-refused manifests use normal fallback count
        m_radioModel->completeReceiveLayoutStartup();
    }
#endif
    configureStepAttenuatorController(radioMac);
    mintFftEndpoints();

    // Remote Daemon R2 Task 18: the wss control plane. AFTER the slices
    // exist, so a client connecting immediately gets them in its
    // connect-time burst without waiting for a delta -- StationServer's
    // own ObjectRegistry::backfillExistingSlices() covers the case either
    // way, but there is no reason to make it the only cover.
    startStationServer(cfg);

    // Fix round 1, Finding 1: mintFftEndpoints() only fills m_topology's
    // own private bookkeeping. Without this call the subscriptions never
    // reached RadioModel's live FFTRouter (RadioModel::fftRouter()) and
    // were completely inert. See the file header above.
    publishFftTopology();
    if (m_radioRecoveryEnabled) {
        m_radioRetryTimer->start(0);
    }

    return true;
}

void DaemonApp::stop()
{
    m_radioRecoveryEnabled = false;
    cancelRadioDiscovery();
    cancelStationServerListenRetry();
    m_stationAnnouncer.reset();
    if (m_radioConnectInProgress) {
        // A cold WDSP initialization pumps a nested event loop. Never delete
        // the RadioModel from inside its still-running connect stack. Its
        // non-interruptible wisdom job may finish, but the cancelled connect
        // path cannot create a radio socket afterward.
        m_stopDeferred = true;
        if (m_stationServer) {
            m_stationServer->close();
        }
        m_radioModel->disconnectFromRadio();
        return;
    }
    m_stopDeferred = false;
    // Cancel before destroying any object the timeout callback reads. Clear
    // the latched endpoint as well so even an already-delivered/stale callback
    // cannot bind a previous run's address after teardown.
    cancelStationServerListenRetry();

    // BEFORE the m_radioModel guard below: stop the telemetry timer and
    // pending owner-thread observations, then retire media, then the server.
    // A StationServer can exist without a RadioModel only transiently; it has
    // to go before m_radioModel.reset() because its StateMirror and
    // ObjectRegistry hold QPointers into that model and every SliceModel under
    // it. The station is still alive while connected clients are detached.
    m_displayGovernorTimer.reset();
    m_displayGovernor.reset();
    m_telemetryController.reset();
    m_hostSampler.reset();
    m_mediaController.reset();
    m_stationServer.reset();
    m_agcSource.reset();

    if (!m_radioModel) {
        m_stepAttController.reset();
        m_stepAttControllerConfigured = false;
        return;
    }

    // Reverse of start(): drop this run's FFT-topology subscriptions and
    // push the removal to the router BEFORE the RadioModel that owns it
    // is destroyed, so the router explicitly loses every mapping rather
    // than the mapping only going away because the whole object does.
    // See clearFftTopology()'s own doc comment for why this is a
    // per-consumer unsubscribe() loop, not `m_topology = FftTopology{};`
    // (fix round 2, Finding 1 reopened -- the wholesale-replacement
    // version silently made the removal a no-op).
    clearFftTopology();

    // R1 Task 11: quiesce the wideband thread before destroying the
    // RadioModel below. RadioModel::setWidebandDispatchThread pointed a
    // member OF this (about to be destroyed) RadioModel --
    // m_widebandDispatchContext -- at this thread; if the thread's event
    // loop were still processing when ~RadioModel() runs, a queued
    // wideband-frame delivery could land mid-dispatch against an object
    // being destroyed concurrently on this thread. quit()+wait() blocks
    // until the thread's event loop has fully stopped, so nothing can
    // touch RadioModel from another thread by the time m_radioModel.reset()
    // runs below. Deliberately NOT reset()/destroyed here -- see
    // widebandThread()'s own doc comment in the header for why the same
    // pointer must stay valid (joined, not running) after stop() rather
    // than going null or dangling.
    if (m_widebandThread) {
        m_widebandThread->quit();
        m_widebandThread->wait();
    }

    // Stop controller callbacks from the connection before RadioModel begins
    // destroying that connection. Keep the controller itself alive through
    // RadioModel teardown: teardownConnection() saves the controller's
    // per-MAC state while the model still holds its non-owning pointer.
    if (m_stepAttController) {
        m_stepAttController->setRadioConnection(nullptr);
    }

    // ~RadioModel() calls teardownConnection() and deletes every slice
    // (RadioModel.cpp), so resetting the pointer alone satisfies
    // "stop() leaves sliceCount() == 0." Reset before emitting so any
    // radioConnected(false) listener already sees a consistent
    // (sliceCount() == 0) state if it queries back into this object.
    m_radioModel->flushPendingSettingsSave();
    m_radioModel.reset();
    // Teardown also saves controller state. Commit the final accepted Core
    // configuration while AppSettings and the daemon profile still exist.
    AppSettings::instance().save();
    m_stepAttController.reset();
    m_stepAttControllerConfigured = false;

    emit radioConnected(false);
}

void DaemonApp::configureStepAttenuatorController(const QString& mac)
{
    if (!m_radioModel || !m_stepAttController) {
        return;
    }

    // R-R3-46: the same feed a local window uses (RadioModel::
    // followReceiveSliceWithStepAttenuator): slice A's receive band for the
    // attenuator and preamp memory, the transmit slice's band and mode for
    // ATT-on-TX, on every change of either and when the binding moves.
    m_radioModel->followReceiveSliceWithStepAttenuator();

    m_stepAttControllerConfigured = true;
    applyStepAttenuatorConnection(mac);
}

void DaemonApp::applyStepAttenuatorConnection(const QString& mac)
{
    if (!m_radioModel || !m_stepAttController) {
        return;
    }

    const auto& caps = m_radioModel->boardCapabilities();
    m_stepAttController->setMinAttenuation(caps.attenuator.minDb);
    // R-R3-46: the same ceiling the local RX applet gives this board
    // (RxApplet.cpp, BoardCapsTable::stepAttMaxDb): 61 dB on Atlas, Hermes,
    // Hermes II, Angelia and Orion with Alex filters, the board row's own
    // maximum otherwise (31 dB; the Hermes Lite 2 keeps -28..31 dB).
    m_stepAttController->setMaxAttenuation(
        BoardCapsTable::stepAttMaxDb(caps.board, caps.hasAlexFilters));
    m_stepAttController->setHasStepAttenuatorCal(caps.hasStepAttenuatorCal);
    m_stepAttController->setIsHpsdrBoard(caps.board == HPSDRHW::Atlas);
    m_stepAttController->setRadioConnection(m_radioModel->connection());

    // Select the current band before loading, because loadSettings restores
    // the per-band RX attenuation and preamp slot for m_currentBand.
    m_radioModel->syncStepAttenuatorToReceiveSlice();
    if (!mac.isEmpty()) {
        m_stepAttController->loadSettings(mac);
    }
}

// Remote Daemon R2 Task 18. Opt-in: cfg.remotePort == 0 means "do not
// listen" and is the default (DaemonConfig.h explains why). A listener
// that fails to come up is logged with StationServer::lastError() and is
// retried at a bounded rate without rebuilding station state. The initial
// failure is NOT a startup failure, matching this class's existing treatment
// of a radio that cannot be found: a daemon that still demodulates locally is
// more useful than one that refuses to boot.
void DaemonApp::startStationServer(const DaemonConfig& cfg)
{
    cancelStationServerListenRetry();
    m_stationListenAttemptCount = 0;
    m_stationListenNextDelayMs = m_stationListenRetryInitialMs;
    if (cfg.remotePort == 0) {
        qCInfo(lcApp) << "DaemonApp: remote control disabled (remote_port = 0)";
        return;
    }
    if (cfg.remotePort < 0 || cfg.remotePort > 65535) {
        qCWarning(lcApp) << "DaemonApp: remote_port is out of range:"
                          << cfg.remotePort << "- remote control not started";
        return;
    }

    const QHostAddress bind(cfg.remoteBind);
    if (bind.isNull()) {
        qCWarning(lcApp) << "DaemonApp: remote_bind is not a valid address:"
                          << cfg.remoteBind << "- remote control not started";
        return;
    }

    // Constructing it is what provisions the TLS certificate and the
    // pairing token, and what prints the first-run pairing banner to stdout
    // on the run that generates them (StationServer's constructor). Secrets
    // are deliberately kept out of the normal log. AppSettings::instance()
    // is the daemon's OWN store here -- server_main.cpp resolved the profile
    // before this point.
    m_stationServer = std::make_unique<StationServer>(m_radioModel.get(),
                                                      AppSettings::instance());
    // Set before listen() so the first authenticated client sees the media
    // capability, never a control-only session that cannot be upgraded.
    m_stationServer->setMediaEnabled(true);
    // R-R3-08/37/40: with display_adaptive on, the Core always advertises a
    // display budget, so apps plan in budget mode from the start and follow
    // it down when the Core is busy: the configured pair when there is one,
    // otherwise a ceiling no real layout reaches. Off: exactly as before.
    std::optional<DisplayBudgetLimits> displayCeiling = cfg.displayBudgetLimits();
    if (cfg.displayAdaptive && !displayCeiling) {
        displayCeiling = DisplayLoadGovernor::computedCeiling();
        // An app older than the budget reason keeps legacy mode exactly.
        m_stationServer->setDisplayBudgetForReasonPeersOnly(true);
    }
    if (displayCeiling) {
        m_stationServer->setDisplayBudgetLimits(*displayCeiling);
    }
    m_mediaController = std::make_unique<DaemonMediaController>(
        m_stationServer.get(), m_radioModel.get(), this);
    // R-R3-23: before listen(), so the first peer and sender use it.
    m_mediaController->setAudioTargetBitrate(cfg.audioBitrate);
    m_mediaController->setAudioLosslessAllowed(cfg.audioLosslessAllowed);
    // Install every source before advertising the capability. A client can
    // authenticate immediately after listen(), so there must be no window in
    // which telemetry is negotiated without a collector to publish it.
    m_hostSampler = std::make_shared<SharedHostSampler>();
    m_telemetryController = std::make_unique<DaemonTelemetryController>(
        m_stationServer.get(), m_radioModel.get(), m_mediaController.get(), this,
        DaemonTelemetryController::MonotonicClock{},
        DaemonTelemetryController::AudioDiagnosticsProvider{},
        std::unique_ptr<HostTelemetrySampler>{},
        DaemonTelemetryController::ReceiverLoadProvider{}, m_hostSampler);
    m_stationServer->setTelemetryEnabled(m_telemetryController != nullptr);
    if (cfg.displayAdaptive && displayCeiling) {
        m_displayGovernor = std::make_unique<DisplayLoadGovernor>(*displayCeiling);
        m_displayGovernorClock.start();
        m_displayGovernorTimer = std::make_unique<QTimer>();
        m_displayGovernorTimer->setInterval(ReceiverDspLoadSampler::kSampleIntervalMs);
        connect(m_displayGovernorTimer.get(), &QTimer::timeout,
                this, &DaemonApp::evaluateDisplayLoad);
        // Only a media session has display to lower. When it ends, the next
        // one starts from the ceiling rather than from this one's load.
        connect(m_stationServer.get(), &StationServer::mediaSessionStarted, this, [this] {
            if (m_displayGovernorTimer) { m_displayGovernorTimer->start(); }
        });
        connect(m_stationServer.get(), &StationServer::mediaSessionEnded, this, [this] {
            if (!m_displayGovernor || !m_displayGovernorTimer) { return; }
            m_displayGovernorTimer->stop();
            publishDisplayBudget(m_displayGovernor->reset());
        });
    }
    m_stationAnnouncer = std::make_unique<StationLanAnnouncer>();
    connect(m_stationServer.get(), &StationServer::listeningChanged,
            this, &DaemonApp::updateStationAnnouncement);
    connect(m_radioModel.get(), &RadioModel::connectionStateChanged,
            this, &DaemonApp::updateStationAnnouncement);
    connect(m_radioModel.get(), &RadioModel::infoChanged,
            this, &DaemonApp::updateStationAnnouncement);
    m_stationListenBind = cfg.remoteBind;
    m_stationListenPort = static_cast<quint16>(cfg.remotePort);
    attemptStationServerListen();
}

namespace {
QString announcementName(const QString& input, const QString& fallback)
{
    QString result;
    int bytes = 0;
    for (char32_t codepoint : input.toUcs4()) {
        if (QChar::category(codepoint) == QChar::Other_Control) { continue; }
        const QString character = QString::fromUcs4(&codepoint, 1);
        const int size = character.toUtf8().size();
        if (bytes + size > kStationLanMaxCoreNameBytes) { break; }
        result += character;
        bytes += size;
    }
    return result.trimmed().isEmpty() ? fallback : result.trimmed();
}
}

static_assert(DisplayLoadGovernor::kLoadIntervalMs == ReceiverDspLoadSampler::kSampleIntervalMs,
              "the display load governor settles in load intervals");

DisplayLoadInputs DaemonApp::gatherDisplayLoadInputs()
{
    if (m_displayLoadInputsForTest) {
        return m_displayLoadInputsForTest();
    }
    DisplayLoadInputs inputs;
    // The plan changes only when a channel or signal processing thread
    // starts or stops; its mutex is taken only then.
    ThreadPlacement& placement = ThreadPlacement::instance();
    const quint64 revision = placement.planRevision();
    if (m_placementPlanRevision != revision) {
        // Where threads actually run: a refused move leaves its role
        // sharing the housekeeping cores.
        m_placementPlan = placement.appliedPlan();
        m_placementPlanRevision = revision;
    }
    inputs.placement = m_placementPlan;
    for (SliceModel* slice : m_radioModel->slices()) {
        if (slice == nullptr) {
            continue;
        }
        const int sliceId = slice->sliceIndex();
        if (const std::optional<ReceiverDspLoad> load = m_radioModel->receiverDspLoad(sliceId)) {
            inputs.receivers.append({sliceId, *load});
        }
    }
    if (m_hostSampler) {
        m_hostSampler->setGovernorCpus(inputs.placement.active ? inputs.placement.housekeeping
                                                               : QList<int>{});
        inputs.systemCpuPercent = m_hostSampler->reading().systemCpuPercent;
        inputs.housekeepingCpuPercent = m_hostSampler->governorCpuPercent();
        inputs.cpuSampleBeganMsAgo = m_hostSampler->cpuSampleBeganMsAgo();
    }
    return inputs;
}

void DaemonApp::evaluateDisplayLoad()
{
    if (!m_displayGovernor || !m_radioModel || !m_mediaController || !m_stationServer) {
        return;
    }
    // An app older than the budget reason plans without a budget (legacy
    // mode): there is nothing for it to follow.
    if (!m_stationServer->displayBudgetLimits()) {
        return;
    }
    // Cached readings only: RadioModel's 500 ms load snapshot, the shared
    // host sampler and the placement plan. None takes a DSP lock or
    // restarts another reader's interval.
    const qint64 nowMs = m_displayGovernorNowForTest ? m_displayGovernorNowForTest()
                                                     : m_displayGovernorClock.elapsed();
    const DisplayBudgetCharge accepted = m_acceptedDisplayChargeForTest
        ? m_acceptedDisplayChargeForTest() : m_mediaController->acceptedDisplayCharge();
    const DisplayLoadReading reading
        = displayLoadReadingFrom(gatherDisplayLoadInputs(), nowMs, accepted);
    const std::optional<DisplayLoadDecision> decision = m_displayGovernor->update(reading);
    if (!decision) {
        return;
    }
    if (publishDisplayBudget(decision)) {
        m_displayGovernor->accept(*decision);
    } else if (const auto published = m_stationServer->configuredDisplayBudgetLimits()) {
        // Refused: the next proposal follows what is published.
        m_displayGovernor->syncGeneration(published->generation);
    }
}

bool DaemonApp::publishDisplayBudget(const std::optional<DisplayLoadDecision>& decision)
{
    if (!decision || !m_stationServer) {
        return false;
    }
    if (!m_stationServer->setDisplayBudgetLimits(decision->limits, decision->reason)) {
        qCWarning(lcApp) << "DaemonApp: display budget generation"
                          << decision->limits.generation << "was not accepted";
        return false;
    }
    qCInfo(lcApp).nospace() << "DaemonApp: display budget "
                            << (decision->reason == DisplayBudgetReason::CoreBusy
                                    ? "lowered, Core busy" : "restored")
                            << ": " << decision->limits.applicationBytesPerSecond
                            << " bytes/s, " << decision->limits.spectrumSampleUnitsPerSecond
                            << " samples/s (generation " << decision->limits.generation << ")";
    return true;
}

void DaemonApp::updateStationAnnouncement()
{
    if (!m_stationAnnouncer) { return; }
    if (!m_stationServer || !m_stationServer->isListening() || !m_radioModel) {
        m_stationAnnouncer->stop();
        return;
    }
    StationLanAnnouncement announcement;
    announcement.controlPort = m_stationServer->serverPort();
    announcement.fingerprint = m_stationServer->certificateFingerprint();
    announcement.coreName = announcementName(m_radioConfig.coreName.isEmpty()
        ? QHostInfo::localHostName() : m_radioConfig.coreName, QStringLiteral("Nereus Core"));
    announcement.radioConnected = m_radioModel->isConnected();
    announcement.radioName = announcementName(m_radioModel->name().isEmpty()
        ? m_radioModel->model() : m_radioModel->name(),
        announcement.radioConnected ? QStringLiteral("Radio") : QString());
    announcement.radioMac = m_radioModel->currentRadioMac().toUpper();
    if (announcement.radioMac.isEmpty()) { announcement.radioMac = m_selectedRadioMac.toUpper(); }
    if (announcement.radioMac.isEmpty()) { announcement.radioMac = QStringLiteral("00:00:00:00:00:00"); }
    m_stationAnnouncer->update(m_stationServer->serverAddress(), announcement);
}

void DaemonApp::attemptStationServerListen()
{
    if (!m_stationServer || m_stationListenPort == 0 || m_stationListenBind.isEmpty()) {
        return;
    }
    if (m_stationServer->isListening()) {
        m_stationListenRetryTimer->stop();
        return;
    }

    const QHostAddress bind(m_stationListenBind);
    if (bind.isNull()) {
        // startStationServer validates before latching, so this is only a
        // defensive guard against future mutation. Invalid config never
        // becomes an indefinitely retried bind target.
        qCWarning(lcApp) << "DaemonApp: refusing listener retry for invalid bind"
                          << m_stationListenBind;
        cancelStationServerListenRetry();
        return;
    }

    ++m_stationListenAttemptCount;
    if (m_stationServer->listen(bind, m_stationListenPort)) {
        m_stationListenRetryTimer->stop();
        qCInfo(lcApp) << "DaemonApp: remote control listening on wss://"
                       << m_stationListenBind << ":" << m_stationServer->serverPort()
                       << "after" << m_stationListenAttemptCount << "attempt(s)";
        return;
    }

    qCWarning(lcApp) << "DaemonApp: remote control listener attempt"
                      << m_stationListenAttemptCount << "failed on"
                      << m_stationListenBind << m_stationListenPort << ":"
                      << m_stationServer->lastError();
    scheduleStationServerListenRetry();
}

void DaemonApp::scheduleStationServerListenRetry()
{
    if (!m_stationServer || m_stationListenPort == 0 || m_stationListenBind.isEmpty()) {
        return;
    }

    const int delayMs = m_stationListenNextDelayMs;
    qCInfo(lcApp) << "DaemonApp: retrying remote control listener in"
                   << delayMs << "ms on" << m_stationListenBind << m_stationListenPort;
    m_stationListenRetryTimer->start(delayMs);

    if (m_stationListenNextDelayMs < m_stationListenRetryMaximumMs) {
        const qint64 doubled = static_cast<qint64>(m_stationListenNextDelayMs) * 2;
        m_stationListenNextDelayMs = static_cast<int>(
            std::min<qint64>(doubled, m_stationListenRetryMaximumMs));
    }
}

void DaemonApp::cancelStationServerListenRetry()
{
    if (m_stationListenRetryTimer) {
        m_stationListenRetryTimer->stop();
    }
    m_stationListenBind.clear();
    m_stationListenPort = 0;
    m_stationListenNextDelayMs = m_stationListenRetryInitialMs;
}

int DaemonApp::sliceCount() const
{
    return m_radioModel ? m_radioModel->slices().size() : 0;
}

bool DaemonApp::stationListenerReady() const
{
    return m_stationServer && m_stationServer->isListening();
}

bool DaemonApp::stationListenerRetryPending() const
{
    return m_stationListenRetryTimer && m_stationListenRetryTimer->isActive();
}

void DaemonApp::applyConfigToSettings(const DaemonConfig& cfg,
                                      const QString& mac) const
{
    auto& settings = AppSettings::instance();

    // sampleRateExplicit, not sampleRateHz > 0: the field always holds a
    // usable rate (validate() rejects <= 0), so it cannot express "the
    // operator did not ask". Writing unconditionally would stamp the
    // struct default over a rate already persisted for this radio every
    // time nereusd ran without a config file. Same reasoning as the
    // audioDevice branch below.
    if (!mac.isEmpty() && cfg.sampleRateExplicit && cfg.sampleRateHz > 0) {
        // resolveSampleRate() (SampleRateCatalog.cpp) reads exactly this
        // key at connect time and validates it against the board's
        // allowed-rate list, falling back to the board default with a
        // warning when the value is not supported. That is the behaviour
        // nereusd.conf.sample documents for this key ("must be a rate the
        // connected board actually supports"), so the daemon gets it by
        // using the same path the GUI does rather than by re-deriving it.
        settings.setHardwareValue(mac,
                                  QStringLiteral("radioInfo/sampleRate"),
                                  cfg.sampleRateHz);
    }

    // Only written when set. An empty audio_device must not stamp an
    // empty DeviceName over a value the operator configured some other
    // way; leaving the key untouched lets
    // AudioDeviceConfig::loadFromSettings keep whatever is already there,
    // and a genuinely unset key resolves to the platform default inside
    // AudioEngine::ensureSpeakersOpen().
    if (!cfg.audioDevice.isEmpty()) {
        settings.setValue(QStringLiteral("audio/Speakers/DeviceName"),
                          cfg.audioDevice);
    }
}

void DaemonApp::cancelRadioDiscovery()
{
    ++m_radioRecoveryGeneration;
    m_radioRetryTimer->stop();
    if (m_radioDiscoveryThread) {
        m_radioDiscoveryThread->requestInterruption();
        m_radioDiscoveryThread->wait();
        m_radioDiscoveryThread.reset();
    }
}

void DaemonApp::scheduleRadioDiscovery()
{
    if (!m_radioRecoveryEnabled || !m_radioModel || m_radioDiscoveryThread
        || m_radioRetryTimer->isActive()) {
        return;
    }
    // Nereus daemon policy, not a radio-protocol timeout.
    m_radioRetryTimer->start(m_radioRetryNextMs);
    m_radioRetryNextMs = std::min(m_radioRetryMaximumMs, m_radioRetryNextMs * 2);
}

void DaemonApp::attemptRadioDiscovery()
{
    if (!m_radioRecoveryEnabled || !m_radioModel || m_radioDiscoveryThread
        || m_radioConnectInProgress || m_radioModel->isConnected()) {
        return;
    }
    // Preserve the full process-wide post-stop quiet interval, including in
    // tests which inject discoveries without sending any network probes.
    const qint64 quietMs = m_radioModel->discovery()->holdOffRemainingMs();
    if (quietMs > 0) {
        m_radioRetryTimer->start(int(quietMs));
        return;
    }
    const quint64 generation = m_radioRecoveryGeneration;
    auto result = std::make_shared<QList<RadioInfo>>();
#ifdef NEREUS_BUILD_TESTS
    const auto provider = m_discoveryProviderForTest;
    auto* worker = QThread::create([result, provider]() {
        if (provider) {
            *result = provider();
            return;
        }
#else
    auto* worker = QThread::create([result]() {
#endif
        RadioDiscovery discovery;
        QEventLoop loop;
        QTimer cancellation;
        cancellation.setInterval(25);
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&loop]() {
            if (QThread::currentThread()->isInterruptionRequested()) {
                loop.quit();
            }
        });
        QObject::connect(&discovery, &RadioDiscovery::discoveryFinished,
                         &loop, &QEventLoop::quit);
        QTimer::singleShot(0, &discovery, [&discovery]() {
            discovery.startDiscovery();
        });
        cancellation.start();
        loop.exec();
        if (!QThread::currentThread()->isInterruptionRequested()) {
            *result = discovery.discoveredRadios();
        }
    });
    worker->setObjectName(QStringLiteral("DaemonRadioDiscovery"));
    m_radioDiscoveryThread.reset(worker);
    connect(worker, &QThread::finished, this, [this, worker, generation, result]() {
        if (generation != m_radioRecoveryGeneration || !m_radioRecoveryEnabled
            || m_radioDiscoveryThread.get() != worker) {
            return;
        }
        worker->wait();
        m_radioDiscoveryThread.reset();
        finishRadioDiscovery(*result);
    });
    worker->start();
}

void DaemonApp::finishRadioDiscovery(const QList<RadioInfo>& found)
{
    if (!m_radioRecoveryEnabled || !m_radioModel) {
        return;
    }
    const auto selected = std::find_if(found.cbegin(), found.cend(), [this](const RadioInfo& info) {
        return !info.inUse && !info.macAddress.isEmpty()
            && (m_selectedRadioMac.isEmpty()
                || info.macAddress.compare(m_selectedRadioMac, Qt::CaseInsensitive) == 0);
    });
    if (selected == found.cend()) {
        scheduleRadioDiscovery();
        return;
    }
    if (m_selectedRadioMac.isEmpty()) {
        m_selectedRadioMac = selected->macAddress;
    }
    const bool preserve = m_radioAttempted;
    m_radioAttempted = true;
    if (!preserve) {
        m_radioModel->prepareReceiveLayout(m_selectedRadioMac);
        applyConfigToSettings(m_radioConfig, m_selectedRadioMac);
    }
    m_radioConnectInProgress = true;
    if (preserve) {
        m_radioModel->connectToRadioPreservingSlices(*selected);
    } else {
        m_radioModel->connectToRadio(*selected);
    }
    m_radioConnectInProgress = false;
    if (m_stopDeferred) {
        stop();
        return;
    }
    if (!m_radioRecoveryEnabled) {
        return;
    }
    if (RadioConnection* const connection = m_radioModel->connection()) {
        const quint64 generation = m_radioRecoveryGeneration;
        connect(connection, &RadioConnection::connectFailed, this,
                [this, generation](ConnectFailure, const QString&) {
            if (generation == m_radioRecoveryGeneration && m_radioRecoveryEnabled) {
                retireRadioAndRetry();
            }
        }, Qt::QueuedConnection);
    } else {
        scheduleRadioDiscovery();
    }
}

void DaemonApp::onRadioStateForRecovery(ConnectionState state)
{
    if (!m_radioRecoveryEnabled || !m_radioModel || m_retiringRadio
        || state != m_radioModel->connectionState()) {
        return;
    }
    if (state == ConnectionState::Connected) {
        m_radioModel->completeReceiveLayoutStartup();
        m_radioRetryNextMs = m_radioRetryInitialMs;
        if (!m_radioConnectedBefore) {
            createConfiguredSlices(m_radioConfig.sliceCount);
            m_radioConnectedBefore = true;
        }
        clearFftTopology();
        mintFftEndpoints();
        publishFftTopology();
    } else if ((state == ConnectionState::LinkLost
                || state == ConnectionState::Disconnected) && m_radioAttempted
               && m_radioModel->connection()) {
        retireRadioAndRetry();
    }
}

void DaemonApp::retireRadioAndRetry()
{
    if (!m_radioRecoveryEnabled || !m_radioModel || m_retiringRadio) {
        return;
    }
    // Invalidate both discovery completions and terminal reports from the
    // retired connection. RadioModel keeps the slices while retiring all DSP.
    cancelRadioDiscovery();
    clearFftTopology();
    m_retiringRadio = true;
    m_radioModel->disconnectFromRadio();
    m_retiringRadio = false;
    scheduleRadioDiscovery();
}

void DaemonApp::createConfiguredSlices(int sliceCountRequested)
{
    if (m_radioModel->receiveLayoutOverridesConfiguredCount()) {
        return;
    }
    // caps.maxSlices directly, NOT the maxSlices() accessor: that
    // accessor returns 1 until RadioModel::isConnected() is true.
    // RadioModel::connectToRadio() itself reads boardCapabilities()
    // directly for the identical reason (RadioModel.cpp, the comment
    // beside its own "poolSlices" local: "caps.maxSlices rather than the
    // maxSlices() accessor: that accessor returns 1 until isConnected()
    // is true, and m_connection is not assigned until further down this
    // function") -- isConnected() does not become true until AFTER
    // connectToRadio()'s own synchronous WDSP-wisdom wait completes and
    // m_connection is assigned, well after hardware-profile resolution
    // (and therefore this SKU's maxSlices) is already settled. Reading
    // boardCapabilities() directly means this method sees the right
    // number regardless of which path start() took (a real
    // connectToRadio(), the primeBoardForTest() seam, or neither).
    const auto& caps = m_radioModel->boardCapabilities();
    const int capMaxSlices = caps.maxSlices > 0 ? caps.maxSlices : 1;
    const int target = std::min(sliceCountRequested, capMaxSlices);

    // Tops up from however many slices already exist. connectToRadio()
    // (when a radio was found and connected above) already created
    // Slice A via its own no-argument addSlice() call before this method
    // runs, so this loop runs (target - 1) more times in that case. The
    // primeBoardForTest() seam does NOT create Slice A itself (it only
    // primes boardCapabilities() and sizes the stream pool, mirroring
    // tst_p1_hl2_rx2_wiring.cpp's own setBoardForTest() + configureStreamPool()
    // pattern, which likewise leaves the first addSlice() call to its
    // caller), so in that path -- and in the no-radio-found path -- this
    // loop starts from zero and runs the full target times.
    while (m_radioModel->slices().size() < target) {
        const int id = m_radioModel->addSlice();
        if (id < 0) {
            // The allocator refused (e.g. no stream left to share).
            // "Up to" the target, per this class's own contract: stop
            // rather than loop forever re-asking for something that
            // just failed.
            qCWarning(lcApp) << "DaemonApp: addSlice() refused at"
                              << m_radioModel->slices().size() << "of"
                              << target << "requested slices";
            break;
        }
    }
}

void DaemonApp::mintFftEndpoints()
{
    for (SliceModel* slice : m_radioModel->slices()) {
        const int stream = slice->streamIndex();
        if (stream < 0) {
            // Unbound -- e.g. the disconnected-default slice, created
            // before any stream pool exists. Nothing to subscribe to yet.
            continue;
        }
        const QString endpointId =
            QStringLiteral("daemon-ep-%1").arg(m_nextEndpointId++);
        m_topology.subscribe(endpointId, stream);
    }
}

void DaemonApp::publishFftTopology()
{
    if (!m_radioModel) {
        return;
    }
    m_topology.applyTo(*m_radioModel->fftRouter());
}

void DaemonApp::clearFftTopology()
{
    // Per-consumer unsubscribe(), NOT `m_topology = FftTopology{};`. See
    // this method's own doc comment in DaemonApp.h for the full
    // explanation: wholesale replacement also wipes FftTopology's
    // private m_lastAppliedConsumers, which applyTo()'s removal loop
    // needs in order to have anything to remove at all. subscriptions()
    // returns a fresh QList (not a live view into m_streamsByConsumer),
    // so mutating m_topology while iterating it here is safe.
    for (const SpectrumSubscription& sub : m_topology.subscriptions()) {
        m_topology.unsubscribe(sub.consumerId);
    }
    publishFftTopology();
}

#ifdef NEREUS_BUILD_TESTS
QList<int> DaemonApp::fftRouterMappingsForTest(const QString& consumerId) const
{
    if (!m_radioModel) {
        return {};
    }
    return m_radioModel->fftRouter()->receiversForPan(consumerId);
}
#endif

} // namespace NereusSDR
