// =================================================================
// tests/tst_station_session.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 18: the wss session.
//
// ── HOW THIS FILE IS SPLIT, AND WHY ──────────────────────────────────────
//
// The protocol assertions (the section 7.0 connect sequence and its exact
// message ORDER, the version policy in both directions, token rejection
// and rate limiting, preemption, schema skew, the heartbeat's detection of
// a silently dead peer) all run over an IN-PROCESS, NON-TLS link
// (fakes/LoopbackTransport.h) and therefore run UNCONDITIONALLY, on every
// build. Only the genuinely TLS-specific slots -- that a real wss listener
// comes up, that a real client completes the handshake through it, and
// that a mismatched certificate fingerprint is refused -- QSKIP when
// QSslSocket::supportsSsl() is false, naming the Qt TLS backend in the
// skip message.
//
// That split is deliberate and is the difference between a suite that
// proves something and one that reports green because it skipped. It is
// possible only because StationServer and StationClient hold a
// SessionTransport rather than a QWebSocket; there is no test-only branch
// inside either class.
//
// ── THE ONE THAT MATTERS MOST ────────────────────────────────────────────
//
// heartbeatDetectsAPeerThatWentSilentWithoutClosing. The maintainer pulled
// the control-channel heartbeat forward from R4 for exactly one reason: a
// TCP connection that dies silently (laptop lid, cell handoff, NAT
// timeout) NEVER produces a close, so a daemon relying on a close sits
// believing a dead client is alive. The in-tree precedent this task was
// pointed at (TciServer.cpp's 20 s ping timer) explicitly does not track
// pongs and relies on a write eventually erroring, which does not detect
// that case. This slot is what proves this implementation does: the fake
// transport keeps the link nominally OPEN and simply stops answering
// pings, and the station is required to notice anyway. A test that killed
// the peer instead would prove nothing, because the close would do the
// work.
//
// =================================================================

#include <QtTest/QtTest>

#include <QByteArray>
#include <QCryptographicHash>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>
#include <QWebSocket>
#include <QWebSocketProtocol>

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/CoreInit.h"
#include "core/MoxController.h"
#include "core/P1RadioConnection.h"
#include "core/security/CertificateStore.h"
#include "core/security/TokenStore.h"
#include "core/dsp/DspAssetService.h"
#include "core/session/SessionMessages.h"
#include "core/session/SessionTransport.h"
#include "core/session/StateMirror.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/meters/SliceMeterPump.h"
#include "models/NotchModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"
#include "core/TgxlConnection.h"
#include "core/SmartSdrApiListener.h"

#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

constexpr const char* kTlsSkipPrefix =
    "Qt reports no working TLS backend, so a wss listener cannot be created. "
    "Active Qt TLS backend: ";

QString tlsSkipMessage()
{
    QString backend = QSslSocket::activeBackend();
    if (backend.isEmpty()) {
        backend = QStringLiteral("<none>");
    }
    QString message = QString::fromLatin1(kTlsSkipPrefix) + backend;
    const QString diagnostic = CertificateStore::tlsBackendDiagnostic();
    if (!diagnostic.isEmpty()) {
        message += QStringLiteral(". ") + diagnostic;
    }
    return message;
}

/// A daemon-side RadioModel that reports Connected against a real board,
/// without a socket. Same seams every other RadioModel-level test in this
/// suite uses; connectToRadio() is unusable from a test (it blocks on cold
/// FFTW wisdom generation for minutes -- see DaemonApp.h's own note).
std::unique_ptr<RadioModel> makeStationRadioModel(int extraSlices)
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    RadioInfo info;
    info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:01");
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    for (int i = 0; i < extraSlices; ++i) {
        model->addSlice(QStringLiteral("pan-0"));
    }
    return model;
}

/// makeStationRadioModel() reports Connected but builds no WDSP channels, so
/// the model's own SliceMeterPump writes the no-reading value to every slice
/// on each poll (R-R3-13, SliceMeterPump::poll's no-channel branch). A test
/// that feeds slice readings by hand stops it first so the value it sets is
/// the value the mirror carries.
void stopSliceMeterPump(RadioModel* model)
{
    SliceMeterPump* pump = model->sliceMeterPump();
    QVERIFY(pump != nullptr);
    pump->stop();
}

SessionMessage decodeOrFail(const QByteArray& wire)
{
    SessionMessage message;
    if (!SessionMessages::decode(wire, &message)) {
        return SessionMessage{};
    }
    return message;
}

/// Index of the first message of `kind` in a recorded stream, or -1.
int indexOfKind(const QList<QByteArray>& kinds, const char* name)
{
    return static_cast<int>(kinds.indexOf(QByteArray(name)));
}

/// Delivers synchronously only where this regression needs to preempt a
/// session before StationServer's already-queued radio callback runs. The
/// ordinary handshake tests keep using LoopbackTransport's realistic queued
/// delivery; this narrow fixture exercises the server's reentrancy guard.
class ImmediateTransport final : public SessionTransport {
public:
    explicit ImmediateTransport(const QString& description, QObject* parent = nullptr)
        : SessionTransport(parent)
        , m_description(description)
    {
    }

    void linkTo(ImmediateTransport* peer)
    {
        m_peer = peer;
        if (peer != nullptr) {
            peer->m_peer = this;
        }
    }

    void sendText(const QByteArray& wire) override
    {
        if (!m_open || m_peer == nullptr || !m_peer->m_open) {
            return;
        }
        m_peer->m_received.append(wire);
        emit m_peer->textReceived(wire);
    }

    void ping() override
    {
        if (m_open && m_peer != nullptr && m_peer->m_open) {
            emit pongReceived();
        }
    }

    void closeLink(const QString& reason) override
    {
        if (!m_open) {
            return;
        }
        m_open = false;
        emit closed();
        if (m_peer != nullptr) {
            m_peer->closeLink(reason);
        }
    }

    bool isOpen() const override { return m_open; }
    QString peerDescription() const override { return m_description; }

    QList<QByteArray> receivedKinds() const
    {
        QList<QByteArray> kinds;
        kinds.reserve(m_received.size());
        for (const QByteArray& wire : m_received) {
            const QJsonDocument document = QJsonDocument::fromJson(wire);
            kinds.append(document.object().value(QStringLiteral("type")).toString().toUtf8());
        }
        return kinds;
    }

private:
    QString m_description;
    ImmediateTransport* m_peer = nullptr;
    bool m_open = true;
    QList<QByteArray> m_received;
};

// ── Capturing whatever reaches the Qt logging handler ────────────────────
//
// Production installs CoreInit's handler, which redacts and then writes to
// BOTH stderr and a log file kept indefinitely. A test cannot assert on
// that file without running CoreInit::initialize(), so it asserts one step
// earlier instead, on what is handed to the handler at all: anything that
// arrives here in production reaches the log file.

QStringList* g_capturedLines = nullptr;

void capturingMessageHandler(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    if (g_capturedLines != nullptr) {
        g_capturedLines->append(msg);
    }
}

/// RAII, because every assertion in a QtTest slot is a bare `return`: a
/// hand-rolled install/restore pair would leave this handler installed for
/// the rest of the binary on the first failure.
class LogCapture {
public:
    explicit LogCapture(QStringList* sink)
    {
        g_capturedLines = sink;
        m_previous = qInstallMessageHandler(&capturingMessageHandler);
    }
    ~LogCapture()
    {
        qInstallMessageHandler(m_previous);
        g_capturedLines = nullptr;
    }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

private:
    QtMessageHandler m_previous = nullptr;
};

} // namespace

class TstStationSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // ---- TokenStore (task 18 step 3) ----
    void tokenIsGeneratedNotChosenAndPersists();
    void tokenVerifyIsRateLimitedAfterRepeatedFailures();

    // ---- The connect sequence, over a non-TLS in-process link ----
    void handshakeCompletesInSectionSevenZeroOrder();
    void capabilitiesAdvertiseEffectiveNotBoardLimits();
    void clientAppliesCapabilitiesAndDrivesConnected();
    void lateRadioRefreshUpdatesAuthenticatedClientWithoutReplayingMirror();
    void queuedLateRadioRefreshDoesNotReachReplacementSession();
    void mediaEnvelopeIsBoundedAndTyped();
    void mediaRejectsPreAuthenticationAndOldProtocol();
    void mediaRequiresReadySessionAndRejectsPriorEpoch();
    void telemetryRequiresReadySessionAndRejectsPriorEpoch();
    void telemetryDoesNotRequireMediaAndRejectsOldProtocol();
    void telemetryClientWaitsForCapabilityAndSnapshot();
    void hostTelemetryReachesVersionTwoPeer();
    void hostTelemetryIsOmittedForMinorNinePeer();
    void clientKeepsHostTelemetryOnlyWhenNegotiated_data();
    void clientKeepsHostTelemetryOnlyWhenNegotiated();
    void remoteTgxlClientRequiresHandshakeMinorAndCapability();
    void remoteFourO3AClientRequiresHandshakeMinorAndCapability();
    void remoteFourO3AServerRejectsPreAuthAndOldMinor();
    void remoteFourO3AAuthenticatedRoundTripMirrorsActualListenerState();
    void remoteFourO3AUnansweredCommandDoesNotSurviveSession_data();
    void remoteFourO3AUnansweredCommandDoesNotSurviveSession();
    void remoteTgxlCommandIsGatedAtAuthenticatedServerBoundary();
    void remoteTgxlConfigureAcceptanceStartsIdentityOnly();

    // ---- Version policy (task 18 step 1) ----
    void majorVersionMismatchRefusesNamingBothVersions();
    void minorVersionMismatchNegotiatesDown();

    // ---- Authentication (task 18 steps 1 and 3) ----
    void badTokenIsRefusedAndThenRateLimited();

    // ---- Session model (task 18 step 1) ----
    void secondAuthenticatedConnectionPreemptsAndSaysWhy();

    // ---- Heartbeat (task 18 step 2a) ----
    void heartbeatDetectsAPeerThatWentSilentWithoutClosing();
    void heartbeatLeavesAnAnsweringPeerAlone();

    // ---- Mirror and settings wiring (task 18 steps 7 and 8) ----
    void mirrorRoundTripsSliceStateAndDoesNotEcho();
    void filterTelemetryFollowsCoreAcrossReconnect();
    void autoAgcTelemetryFollowsCoreAcrossReconnect();
    void settingsProxyIsNotReadyBeforeTheSnapshot();
    void aRemovedStationSettingReachesTheClientAsAbsenceNotAnEmptyString();
    void schemaSkewIsCaughtByNameComparison();
    void receiveOnlyStationBlocksRemoteBandRecall();
    void receiveOnlyStationRefusesTransmitPropertyWrites();
    void receiveOnlyStationRefusesTransmitDspOptionsSettingsWrites();
    void receiveOnlyStationRefusesTransmitDspOptionsSettingsRemoves();
    void acceptedReceiveDspOptionsWriteAppliesToMatchingSlices();
    void receiveOnlyPolicySurvivesRadioTeardown();
    void nr3ModelChoiceLoadsOnceOnTheCoreAndMirrors();
    void nr3CannotRunIsRefusedOnTheCoreAndInTheWindow();
    void olderCoreLeavesTheNr3ModelUnchangeable();
    void olderAppNr3ModelPathWriteIsRefused();
    void remoteNotchEditKeepsTheCoresWholeList();
    void remoteNotchMoveToggleAndDeleteReachTheCore();
    void remoteNotchRefusalsAreInPlainWords();
    void coreNotchChangesReachTheWindow();
    void appNotchSettingsWritesAreRefused();
    void olderCoreKeepsTodaysNotchBehaviour();
    void olderAppIgnoresTheNotchesObjectGolden();

    // ---- Fix round 1 ----
    void reconnectSurvivesTheOldTransportClosing();
    void heartbeatTimeoutReportsTheSessionAsEnded();
    void tunerPropertiesHydrateWithoutClientCommands();
    void remoteTgxlStateClearsOnSessionLossRetainingConfiguredEndpoint();
    void handshakeDeadlineDropsASilentPeer();
    void peerLimitRefusesFurtherConnections();
    void listenIsIdempotent();

    // ---- Security fix round ----
    void firstRunPairingBannerNeverReachesTheLoggingHandler();
    void oversizedMessageIsRefusedBeforeAnyAuthentication();
    void clientCapsWhatAStationCanMakeItAllocate();
    void wsSchemeIsRefusedWhenAFingerprintIsPinned();
    void tokenIsNeverSentOnALinkWhosePinWasNeverChecked();
    void transientRefusalsStayRetryableAndABadTokenDoesNot();
    void lockedOutOperatorRetriesButABadTokenDoesNot();

    // ---- TLS-specific (QSKIP when the backend is unusable) ----
    void wssListenerComesUpAndCompletesAHandshake();
    void wssRefusesAMismatchedCertificateFingerprint();
    void failedInitialConnectReportsPromptly();

private:
    /// One temp dir for the whole class so the RSA-3072 key pair is
    /// generated once and every later StationServer loads it back, rather
    /// than paying key generation per slot.
    QTemporaryDir m_securityDir;
};

void TstStationSession::initTestCase()
{
    QVERIFY(m_securityDir.isValid());
    // TGXL admission persists through the process settings singleton. The
    // generic Qt test sandbox is shared by parallel test executables, so a
    // different GUI test can replace that file between our save and reload.
    // Follow the receive-layout session fixture's process-specific profile.
    const QString profile = QStringLiteral("station-session-%1")
                                .arg(QCoreApplication::applicationPid());
    AppSettings::setProfileOverride(profile);
    QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
    AppSettings::instance().clear();
}

void TstStationSession::cleanupTestCase()
{
    const QString path = AppSettings::instance().filePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

void TstStationSession::mediaEnvelopeIsBoundedAndTyped()
{
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = {{QStringLiteral("op"), QStringLiteral("start")}};
    SessionMessage decoded;
    QVERIFY(SessionMessages::decode(SessionMessages::encode(message), &decoded));
    QCOMPARE(decoded.kind, SessionMessageKind::MediaControl);
    QCOMPARE(decoded.mediaPayload, message.mediaPayload);
    QVERIFY(!SessionMessages::decode(
        QByteArrayLiteral("{\"type\":\"media.control\",\"payload\":[]}"), &decoded));
    QCOMPARE(decoded.mediaPayload, message.mediaPayload);
    message.mediaPayload.insert(QStringLiteral("sdp"),
                                QString(kMaxMediaControlBytes, QLatin1Char('x')));
    QVERIFY(SessionMessages::encode(message).isEmpty());
    const QByteArray oversized = QJsonDocument(QJsonObject{
        {QStringLiteral("type"), QStringLiteral("media.control")},
        {QStringLiteral("payload"), message.mediaPayload}}).toJson(QJsonDocument::Compact);
    QVERIFY(!SessionMessages::decode(oversized, &decoded));
}

void TstStationSession::mediaRejectsPreAuthenticationAndOldProtocol()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("media.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setMediaEnabled(true);
    QSignalSpy inbound(&server, &StationServer::mediaControlReceived);
    SessionMessage media;
    media.kind = SessionMessageKind::MediaControl;
    media.mediaPayload = {{QStringLiteral("op"), QStringLiteral("start")}};

    auto* unauthStation = new LoopbackTransport(QStringLiteral("unauth-station"), this);
    auto* unauthPeer = new LoopbackTransport(QStringLiteral("unauth-peer"), this);
    unauthStation->linkTo(unauthPeer);
    server.acceptTransport(unauthStation);
    unauthPeer->sendText(SessionMessages::encode(media));
    QTRY_VERIFY(!unauthPeer->isOpen());
    QCOMPARE(inbound.count(), 0);

    auto* oldStation = new LoopbackTransport(QStringLiteral("old-station"), this);
    auto* oldPeer = new LoopbackTransport(QStringLiteral("old-peer"), this);
    oldStation->linkTo(oldPeer);
    server.acceptTransport(oldStation);
    oldPeer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, 0, 6, QStringLiteral("old-client"))));
    oldPeer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    QVERIFY(!server.mediaAvailable());
    QVERIFY(!server.sendMediaControl(media.mediaPayload, server.mediaSessionEpoch()));
    oldPeer->sendText(SessionMessages::encode(media));
    QCoreApplication::processEvents();
    QCOMPARE(inbound.count(), 0);
    QVERIFY(oldPeer->isOpen());
}

void TstStationSession::mediaRequiresReadySessionAndRejectsPriorEpoch()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("media.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setMediaEnabled(true);
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    const QJsonObject payload{{QStringLiteral("op"), QStringLiteral("start")}};
    QSignalSpy serverInbound(&server, &StationServer::mediaControlReceived);
    QSignalSpy clientInbound(&client, &StationClient::mediaControlReceived);
    QSignalSpy ended(&server, &StationServer::mediaSessionEnded);
    QVERIFY(!client.sendMediaControl(payload, client.sessionEpoch()));
    QVERIFY(!server.sendMediaControl(payload, server.mediaSessionEpoch()));

    auto connectPair = [&] {
        auto* station = new LoopbackTransport(QStringLiteral("media-station"), this);
        auto* peer = new LoopbackTransport(QStringLiteral("media-client"), this);
        station->linkTo(peer);
        client.startSession(peer, server.token());
        server.acceptTransport(station);
    };
    connectPair();
    QTRY_VERIFY(client.mediaAvailable());
    QVERIFY(server.mediaAvailable());
    const quint32 firstClientEpoch = client.sessionEpoch();
    const quint64 firstServerEpoch = server.mediaSessionEpoch();
    QVERIFY(client.sendMediaControl(payload, firstClientEpoch));
    QVERIFY(server.sendMediaControl(payload, firstServerEpoch));
    QTRY_COMPARE(serverInbound.count(), 1);
    QTRY_COMPARE(clientInbound.count(), 1);
    QCOMPARE(serverInbound.first().at(1).toULongLong(), firstServerEpoch);
    QCOMPARE(clientInbound.first().at(1).toUInt(), firstClientEpoch);

    connectPair();
    QTRY_VERIFY(client.mediaAvailable());
    QVERIFY(client.sessionEpoch() != firstClientEpoch);
    QVERIFY(server.mediaSessionEpoch() != firstServerEpoch);
    QVERIFY(!ended.isEmpty());
    QVERIFY(!client.sendMediaControl(payload, firstClientEpoch));
    QVERIFY(!server.sendMediaControl(payload, firstServerEpoch));
    QVERIFY(client.sendMediaControl(payload, client.sessionEpoch()));
    QTRY_COMPARE(serverInbound.count(), 2);
    QCOMPARE(clientInbound.count(), 1);
    client.disconnectFromStation(QStringLiteral("media test complete"));
    QTRY_VERIFY(!server.mediaAvailable());
    QVERIFY(!client.sendMediaControl(payload, client.sessionEpoch()));
}

void TstStationSession::telemetryRequiresReadySessionAndRejectsPriorEpoch()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("telemetry.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setTelemetryEnabled(true);
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy samples(&client, &StationClient::telemetryReceived);
    QSignalSpy ended(&server, &StationServer::telemetrySessionEnded);
    StationTelemetrySnapshot snapshot;
    snapshot.sequence = 1;
    QVERIFY(!server.sendTelemetry(snapshot, server.sessionEpoch()));
    QVERIFY(!client.telemetryAvailable());
    auto connectPair = [&] {
        auto* station = new LoopbackTransport(QStringLiteral("metrics-station"), this);
        auto* peer = new LoopbackTransport(QStringLiteral("metrics-client"), this);
        station->linkTo(peer);
        client.startSession(peer, server.token());
        server.acceptTransport(station);
    };
    connectPair();
    QTRY_VERIFY(client.telemetryAvailable());
    const quint64 oldServerEpoch = server.sessionEpoch();
    const quint32 oldClientEpoch = client.sessionEpoch();
    QVERIFY(server.sendTelemetry(snapshot, oldServerEpoch));
    QTRY_COMPARE(samples.count(), 1);
    QCOMPARE(samples.first().at(1).toUInt(), oldClientEpoch);
    QVERIFY(server.sendTelemetry(snapshot, oldServerEpoch)); // duplicate ignored
    snapshot.sequence = 2;
    snapshot.sampledElapsedMs = 1000;
    QVERIFY(server.sendTelemetry(snapshot, oldServerEpoch));
    QTRY_COMPARE(samples.count(), 2);
    QCOMPARE(qvariant_cast<StationTelemetrySnapshot>(samples.last().at(0)).sequence, 2u);
    snapshot.sequence = 3;
    snapshot.sampledElapsedMs = 500; // a regressing producer sample is ignored
    QVERIFY(server.sendTelemetry(snapshot, oldServerEpoch));
    connectPair();
    QTRY_VERIFY(client.telemetryAvailable());
    QVERIFY(server.sessionEpoch() != oldServerEpoch);
    QVERIFY(client.sessionEpoch() != oldClientEpoch);
    QVERIFY(!ended.isEmpty());
    QVERIFY(!server.sendTelemetry(snapshot, oldServerEpoch));
    snapshot.sequence = 1; // new epoch establishes a new sequence baseline
    snapshot.sampledElapsedMs = 0;
    QVERIFY(server.sendTelemetry(snapshot, server.sessionEpoch()));
    QTRY_COMPARE(samples.count(), 3);
    QCOMPARE(samples.last().at(1).toUInt(), client.sessionEpoch());
    client.disconnectFromStation(QStringLiteral("telemetry complete"));
    QTRY_VERIFY(!server.telemetryAvailable());
    QVERIFY(!client.telemetryAvailable());
    QVERIFY(!server.sendTelemetry(snapshot, server.sessionEpoch()));
}

void TstStationSession::telemetryDoesNotRequireMediaAndRejectsOldProtocol()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("telemetry-old.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setTelemetryEnabled(true);
    QVERIFY(!server.mediaAvailable());
    auto* station = new LoopbackTransport(QStringLiteral("old-metrics-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("old-metrics-client"), this);
    station->linkTo(peer);
    server.acceptTransport(station);
    StationTelemetrySnapshot snapshot;
    snapshot.sequence = 1;
    QVERIFY(!server.sendTelemetry(snapshot, server.sessionEpoch()));
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kStationTelemetrySessionProtocolMinor - 1, 6,
        QStringLiteral("older-client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    QVERIFY(!server.telemetryAvailable());
    QVERIFY(!server.sendTelemetry(snapshot, server.sessionEpoch()));
    QVERIFY(peer->isOpen());

    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    auto* newerStation = new LoopbackTransport(QStringLiteral("new-metrics-station"), this);
    auto* newerPeer = new LoopbackTransport(QStringLiteral("new-metrics-client"), this);
    newerStation->linkTo(newerPeer);
    client.startSession(newerPeer, server.token());
    server.acceptTransport(newerStation);
    QTRY_VERIFY(client.telemetryAvailable());
    QVERIFY(server.telemetryAvailable());
    QVERIFY(!server.mediaAvailable());
    QVERIFY(!client.mediaAvailable());
}

void TstStationSession::telemetryClientWaitsForCapabilityAndSnapshot()
{
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy samples(&client, &StationClient::telemetryReceived);
    auto* station = new LoopbackTransport(QStringLiteral("raw-metrics-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("raw-metrics-client"), this);
    station->linkTo(peer);
    client.startSession(peer, QStringLiteral("test-token"));
    SessionMessage sample;
    sample.kind = SessionMessageKind::StationTelemetry;
    sample.telemetry.sequence = 1;
    const auto send = [&](const SessionMessage& message) {
        station->sendText(SessionMessages::encode(message));
    };
    send(sample); // before hello/authentication
    send(SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                                QStringLiteral("station")));
    send(SessionMessages::authResult(true, {}, false));
    StationCapabilities caps;
    caps.stationTelemetryVersion = 1;
    send(SessionMessages::capabilities(caps.toUpdates()));
    send(sample); // capability present, but snapshot not ready
    QTRY_VERIFY(!peer->receivedKinds().isEmpty());
    QCoreApplication::processEvents();
    QCOMPARE(samples.count(), 0);
    QVERIFY(!client.telemetryAvailable());
    send(SessionMessages::snapshotComplete());
    QTRY_VERIFY(client.telemetryAvailable());
    send(sample);
    QTRY_COMPARE(samples.count(), 1);
}

namespace {
StationHostTelemetry hostSample()
{
    StationHostTelemetry host;
    host.systemCpuPercent = 23.5;
    host.processCpuPercent = 4.25;
    host.memoryAvailableKiB = 6500000;
    host.memoryTotalKiB = 8000000;
    host.processResidentKiB = 51234;
    host.hottestZoneCelsius = 52.5;
    host.hottestZoneName = QStringLiteral("bigcore0-thermal");
    return host;
}
} // namespace

// R-R3-32/33: a current GUI and Core negotiate minor 10 and telemetry
// version 2, and the host section arrives intact.
void TstStationSession::hostTelemetryReachesVersionTwoPeer()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("host-telemetry.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setTelemetryEnabled(true);
    QCOMPARE(server.buildCapabilities().stationTelemetryVersion, 2);
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy samples(&client, &StationClient::telemetryReceived);
    auto* station = new LoopbackTransport(QStringLiteral("host-metrics-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("host-metrics-client"), this);
    station->linkTo(peer);
    client.startSession(peer, server.token());
    server.acceptTransport(station);
    QTRY_VERIFY(client.telemetryAvailable());
    QCOMPARE(client.agreedMinor(), kCoreHostTelemetrySessionProtocolMinor);

    StationTelemetrySnapshot snapshot;
    snapshot.sequence = 1;
    snapshot.host = hostSample();
    QVERIFY(server.sendTelemetry(snapshot, server.sessionEpoch()));
    QTRY_COMPARE(samples.count(), 1);
    const auto received = qvariant_cast<StationTelemetrySnapshot>(samples.first().at(0));
    QCOMPARE(received.host.systemCpuPercent, std::optional<double>(23.5));
    QCOMPARE(received.host.processCpuPercent, std::optional<double>(4.25));
    QCOMPARE(received.host.memoryAvailableKiB, std::optional<qint64>(6500000));
    QCOMPARE(received.host.memoryTotalKiB, std::optional<qint64>(8000000));
    QCOMPARE(received.host.processResidentKiB, std::optional<qint64>(51234));
    QCOMPARE(received.host.hottestZoneCelsius, std::optional<double>(52.5));
    QCOMPARE(received.host.hottestZoneName, QStringLiteral("bigcore0-thermal"));
    client.disconnectFromStation(QStringLiteral("host telemetry complete"));
}

// A minor-9 GUI receives exactly today's telemetry: the same bytes a Core
// without host telemetry would have sent.
void TstStationSession::hostTelemetryIsOmittedForMinorNinePeer()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("host-telemetry-old.settings")));
    auto model = makeStationRadioModel(0);
    StationServer server(model.get(), settings, m_securityDir.path());
    server.setTelemetryEnabled(true);
    auto* station = new LoopbackTransport(QStringLiteral("minor9-metrics-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("minor9-metrics-client"), this);
    station->linkTo(peer);
    server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kCoreHostTelemetrySessionProtocolMinor - 1, 6,
        QStringLiteral("minor-9-client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.telemetryAvailable());
    peer->clearReceived();

    StationTelemetrySnapshot snapshot;
    snapshot.sequence = 2;
    snapshot.sampledElapsedMs = 1000;
    snapshot.host = hostSample();
    QVERIFY(server.sendTelemetry(snapshot, server.sessionEpoch()));
    QByteArray wire;
    QTRY_VERIFY([&] {
        for (const QByteArray& message : peer->received()) {
            if (QJsonDocument::fromJson(message).object().value(QStringLiteral("type"))
                    == QStringLiteral("station.metrics.v1")) {
                wire = message;
                return true;
            }
        }
        return false;
    }());
    const QByteArray golden =
        R"({"payload":{"audio":{"active":false,"contextGeneration":0},)"
        R"("radio":{"connected":false},"sampledElapsedMs":1000,"sequence":2},)"
        R"("type":"station.metrics.v1"})";
    QCOMPARE(wire, golden);
    SessionMessage withoutHost;
    withoutHost.kind = SessionMessageKind::StationTelemetry;
    withoutHost.telemetry = snapshot;
    withoutHost.telemetry.host = {};
    QCOMPARE(wire, SessionMessages::encode(withoutHost));
}

// The GUI accepts a host section only from a Core that negotiated both the
// minor and telemetry version 2; anything else is delivered without it.
void TstStationSession::clientKeepsHostTelemetryOnlyWhenNegotiated_data()
{
    QTest::addColumn<int>("minor");
    QTest::addColumn<int>("version");
    QTest::addColumn<bool>("kept");
    QTest::newRow("minor 10, version 2")
        << int(kCoreHostTelemetrySessionProtocolMinor) << 2 << true;
    QTest::newRow("minor 10, version 1")
        << int(kCoreHostTelemetrySessionProtocolMinor) << 1 << false;
    QTest::newRow("minor 9, version 2")
        << int(kCoreHostTelemetrySessionProtocolMinor - 1) << 2 << false;
}

void TstStationSession::clientKeepsHostTelemetryOnlyWhenNegotiated()
{
    QFETCH(int, minor);
    QFETCH(int, version);
    QFETCH(bool, kept);
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy samples(&client, &StationClient::telemetryReceived);
    auto* station = new LoopbackTransport(QStringLiteral("raw-host-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("raw-host-client"), this);
    station->linkTo(peer);
    client.startSession(peer, QStringLiteral("test-token"));
    const auto send = [&](const SessionMessage& message) {
        station->sendText(SessionMessages::encode(message));
    };
    send(SessionMessages::hello(kSessionProtocolMajor, static_cast<quint16>(minor), 6,
                                QStringLiteral("station")));
    send(SessionMessages::authResult(true, {}, false));
    StationCapabilities caps;
    caps.stationTelemetryVersion = version;
    send(SessionMessages::capabilities(caps.toUpdates()));
    send(SessionMessages::snapshotComplete());
    QTRY_VERIFY(client.telemetryAvailable());
    SessionMessage sample;
    sample.kind = SessionMessageKind::StationTelemetry;
    sample.telemetry.sequence = 1;
    sample.telemetry.host = hostSample();
    send(sample);
    QTRY_COMPARE(samples.count(), 1);
    const auto received = qvariant_cast<StationTelemetrySnapshot>(samples.first().at(0));
    QCOMPARE(!received.host.isEmpty(), kept);
    QCOMPARE(received.host.hottestZoneName.isEmpty(), !kept);
}

void TstStationSession::remoteTgxlClientRequiresHandshakeMinorAndCapability()
{
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy availabilityChanged(&remote, &RadioModel::stationLinkStateChanged);

    // A client with no negotiated station is inert; neither typed request
    // may fall through to a local accessory connection.
    QVERIFY(!client.remoteTgxlConfigAvailable());
    QVERIFY(!client.requestConfigureTgxl(QStringLiteral("192.0.2.10"), 9010).sent);
    QVERIFY(!client.requestDisconnectTgxl().sent);

    auto* station = new LoopbackTransport(QStringLiteral("tgxl-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("tgxl-client"), this);
    station->linkTo(peer);
    client.startSession(peer, QStringLiteral("test-token"));

    station->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
    station->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
    StationCapabilities caps;
    caps.remoteTgxlConfigVersion = 1;
    station->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
    station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
    QTRY_VERIFY(client.remoteTgxlConfigAvailable());
    QTRY_VERIFY(availabilityChanged.count() >= 1);

    const IStationLink::CommandOutcome configured =
        client.requestConfigureTgxl(QStringLiteral("192.0.2.10"), 9010);
    QVERIFY2(configured.sent, qPrintable(configured.reason));
    // The client sends on peer; LoopbackTransport delivers that wire to the
    // linked station endpoint.  Assert the actual receiving route rather
    // than the sender's inbound capture.
    QTRY_VERIFY(station->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
    const QList<QByteArray> sent = station->received();
    const SessionMessage command = decodeOrFail(sent.last());
    QCOMPARE(command.kind, SessionMessageKind::CommandInvoke);
    QCOMPARE(command.commandVerb, QByteArrayLiteral("configureTgxl"));
    QCOMPARE(command.arguments.size(), 2);
    QCOMPARE(command.arguments.at(0).name, QByteArrayLiteral("host"));
    QCOMPARE(command.arguments.at(0).kind, MirrorWireKind::Utf8);
    QCOMPARE(command.arguments.at(0).value.toString(), QStringLiteral("192.0.2.10"));
    QCOMPARE(command.arguments.at(1).name, QByteArrayLiteral("port"));
    QCOMPARE(command.arguments.at(1).kind, MirrorWireKind::Int64);
    QCOMPARE(command.arguments.at(1).value.toLongLong(), qint64(9010));

    const IStationLink::CommandOutcome disconnected = client.requestDisconnectTgxl();
    QVERIFY2(disconnected.sent, qPrintable(disconnected.reason));
    QTRY_VERIFY(station->receivedKinds().count(QByteArrayLiteral("command.invoke")) == 2);
    const QList<QByteArray> afterDisconnect = station->received();
    const SessionMessage disconnect = decodeOrFail(afterDisconnect.last());
    QCOMPARE(disconnect.kind, SessionMessageKind::CommandInvoke);
    QCOMPARE(disconnect.commandVerb, QByteArrayLiteral("disconnectTgxl"));
    QVERIFY(disconnect.arguments.isEmpty());

    const int availableSignalCount = availabilityChanged.count();
    station->closeLink(QStringLiteral("test teardown"));
    QTRY_VERIFY(!client.remoteTgxlConfigAvailable());
    QTRY_VERIFY(availabilityChanged.count() > availableSignalCount);

    RadioModel olderRemote(RadioModel::Role::Remote);
    SettingsProxy olderProxy;
    StationClient olderClient(&olderRemote, &olderProxy);
    auto* olderStation = new LoopbackTransport(QStringLiteral("tgxl-older-station"), this);
    auto* olderPeer = new LoopbackTransport(QStringLiteral("tgxl-older-client"), this);
    olderStation->linkTo(olderPeer);
    olderClient.startSession(olderPeer, QStringLiteral("test-token"));
    olderStation->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor,
        static_cast<quint16>(kRemoteTgxlConfigSessionProtocolMinor - 1),
        6, QStringLiteral("older-station"))));
    olderStation->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
    olderStation->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
    olderStation->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
    QTRY_VERIFY(olderClient.isHandshakeComplete());
    QVERIFY(!olderClient.remoteTgxlConfigAvailable());
}

void TstStationSession::remoteFourO3AClientRequiresHandshakeMinorAndCapability()
{
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);

    QVERIFY(!client.remoteFourO3AControlAvailable());
    QVERIFY(!client.requestFourO3AEnabled(true).sent);

    auto* station = new LoopbackTransport(QStringLiteral("four-o3a-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("four-o3a-client"), this);
    station->linkTo(peer);
    client.startSession(peer, QStringLiteral("test-token"));
    station->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
    station->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
    StationCapabilities caps;
    caps.remoteFourO3AControlVersion = 1;
    station->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
    station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
    QTRY_VERIFY(client.remoteFourO3AControlAvailable());

    const IStationLink::CommandOutcome requested = client.requestFourO3AEnabled(true);
    QVERIFY2(requested.sent, qPrintable(requested.reason));
    QTRY_VERIFY(station->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
    const SessionMessage command = decodeOrFail(station->received().last());
    QCOMPARE(command.commandVerb, QByteArrayLiteral("setFourO3AEnabled"));
    QCOMPARE(command.arguments.size(), 1);
    QCOMPARE(command.arguments.first().name, QByteArrayLiteral("enabled"));
    QCOMPARE(command.arguments.first().kind, MirrorWireKind::Bool);
    QVERIFY(command.arguments.first().value.toBool());
    // No command can start the Remote model's local listener.
    QVERIFY(!remote.smartSdrListener()->isListening());

    RadioModel olderRemote(RadioModel::Role::Remote);
    SettingsProxy olderProxy;
    StationClient olderClient(&olderRemote, &olderProxy);
    auto* olderStation = new LoopbackTransport(QStringLiteral("four-o3a-older-station"), this);
    auto* olderPeer = new LoopbackTransport(QStringLiteral("four-o3a-older-client"), this);
    olderStation->linkTo(olderPeer);
    olderClient.startSession(olderPeer, QStringLiteral("test-token"));
    olderStation->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor,
        static_cast<quint16>(kRemoteFourO3AControlSessionProtocolMinor - 1), 6,
        QStringLiteral("older-station"))));
    olderStation->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
    olderStation->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
    olderStation->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
    QTRY_VERIFY(olderClient.isHandshakeComplete());
    QVERIFY(!olderClient.remoteFourO3AControlAvailable());
}

void TstStationSession::remoteFourO3AServerRejectsPreAuthAndOldMinor()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("four-o3a.settings")));
    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    const SessionMessage request = SessionMessages::commandInvoke(
        "setFourO3AEnabled", 41,
        { MirrorUpdate{0, "enabled", MirrorWireKind::Bool, true} });

    auto* unauthStation = new LoopbackTransport(QStringLiteral("four-o3a-unauth-station"), this);
    auto* unauthPeer = new LoopbackTransport(QStringLiteral("four-o3a-unauth-peer"), this);
    unauthStation->linkTo(unauthPeer);
    server.acceptTransport(unauthStation);
    unauthPeer->sendText(SessionMessages::encode(request));
    QTRY_VERIFY(!unauthPeer->isOpen());

    auto* oldStation = new LoopbackTransport(QStringLiteral("four-o3a-old-station"), this);
    auto* oldPeer = new LoopbackTransport(QStringLiteral("four-o3a-old-peer"), this);
    oldStation->linkTo(oldPeer);
    server.acceptTransport(oldStation);
    oldPeer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor,
        static_cast<quint16>(kRemoteFourO3AControlSessionProtocolMinor - 1), 6,
        QStringLiteral("older-client"))));
    oldPeer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    oldPeer->clearReceived();
    oldPeer->sendText(SessionMessages::encode(request));
    QTRY_VERIFY(oldPeer->receivedKinds().contains(QByteArrayLiteral("command.result")));
    SessionMessage result;
    for (const QByteArray& wire : oldPeer->received()) {
        const SessionMessage candidate = decodeOrFail(wire);
        if (candidate.kind == SessionMessageKind::CommandResult
            && candidate.commandId == request.commandId) {
            result = candidate;
            break;
        }
    }
    QCOMPARE(result.kind, SessionMessageKind::CommandResult);
    QVERIFY(!result.accepted);
    QVERIFY(result.reason.contains(QStringLiteral("newer station protocol")));
}

void TstStationSession::remoteFourO3AAuthenticatedRoundTripMirrorsActualListenerState()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("four-o3a-roundtrip.settings")));
    auto stationModel = makeStationRadioModel(0);
    stationModel->enableStationAccessoryIdentity();
    stationModel->smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy finished(&remote, &RadioModel::stationFourO3ACommandFinished);
    auto* station = new LoopbackTransport(QStringLiteral("four-o3a-roundtrip-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("four-o3a-roundtrip-client"), this);
    station->linkTo(peer);
    client.startSession(peer, server.token());
    server.acceptTransport(station);

    QTRY_VERIFY(client.remoteFourO3AControlAvailable());
    QVERIFY(!remote.currentRadioMac().isEmpty());
    QVERIFY(!remote.fourO3AEnabled());
    QVERIFY(!remote.fourO3AListening());
    QVERIFY(remote.fourO3AListenerError().isEmpty());

    const IStationLink::CommandOutcome enabled = client.requestFourO3AEnabled(true);
    QVERIFY2(enabled.sent, qPrintable(enabled.reason));
    // The remote model owns no listener. It remains false until the Core
    // snapshot/delta arrives after the accepted CommandResult.
    QVERIFY(!remote.smartSdrListener()->isListening());
    QTRY_VERIFY(!finished.isEmpty());
    QCOMPARE(finished.last().at(0).toBool(), true);
    QTRY_VERIFY(stationModel->fourO3AEnabled());
    QTRY_VERIFY(stationModel->fourO3AListening());
    QTRY_VERIFY(remote.fourO3AEnabled());
    QTRY_VERIFY(remote.fourO3AListening());
    QVERIFY(remote.fourO3AListenerError().isEmpty());
    QVERIFY(!remote.smartSdrListener()->isListening());
    QCOMPARE(stationModel->peripheralValue(QStringLiteral("FourO3A_Enabled")),
             QStringLiteral("True"));

    // A real occupied endpoint is accepted as intent but mirrored as the
    // listener failure it is; no invented listening success is possible.
    const IStationLink::CommandOutcome disabled = client.requestFourO3AEnabled(false);
    QVERIFY2(disabled.sent, qPrintable(disabled.reason));
    QTRY_VERIFY(!remote.fourO3AEnabled());
    QTRY_VERIFY(!stationModel->fourO3AListening());
    QTcpServer blocker;
    QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
    stationModel->smartSdrListener()->setListenEndpointForTesting(
        QHostAddress::LocalHost, blocker.serverPort());
    const IStationLink::CommandOutcome bindFailure = client.requestFourO3AEnabled(true);
    QVERIFY2(bindFailure.sent, qPrintable(bindFailure.reason));
    QTRY_VERIFY(remote.fourO3AEnabled());
    QTRY_VERIFY(!remote.fourO3AListening());
    QTRY_VERIFY(!remote.fourO3AListenerError().isEmpty());
    QVERIFY(!remote.smartSdrListener()->isListening());

    // Link loss clears the remote-only snapshot cache, including a real
    // listener error, without starting a local listener or writing a local
    // setting on the GUI model.
    station->closeLink(QStringLiteral("four-o3a roundtrip teardown"));
    QTRY_VERIFY(!remote.fourO3AEnabled());
    QTRY_VERIFY(!remote.fourO3AListening());
    QTRY_VERIFY(remote.fourO3AListenerError().isEmpty());
    QVERIFY(!remote.smartSdrListener()->isListening());
}

void TstStationSession::remoteFourO3AUnansweredCommandDoesNotSurviveSession_data()
{
    QTest::addColumn<bool>("closeBeforeReplacement");
    QTest::newRow("link-loss") << true;
    QTest::newRow("direct-replacement") << false;
}

void TstStationSession::remoteFourO3AUnansweredCommandDoesNotSurviveSession()
{
    QFETCH(bool, closeBeforeReplacement);
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy finished(&remote, &RadioModel::stationFourO3ACommandFinished);
    const auto attach = [&]() {
        auto* station = new LoopbackTransport(QStringLiteral("four-o3a-command-station"), this);
        auto* peer = new LoopbackTransport(QStringLiteral("four-o3a-command-client"), this);
        station->linkTo(peer);
        client.startSession(peer, QStringLiteral("test-token"));
        station->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
        station->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
        StationCapabilities caps;
        caps.remoteFourO3AControlVersion = 1;
        station->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
        station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
        return station;
    };

    auto* firstStation = attach();
    QTRY_VERIFY(client.remoteFourO3AControlAvailable());
    QVERIFY(client.requestFourO3AEnabled(true).sent);
    QTRY_VERIFY(firstStation->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
    const SessionMessage abandoned = decodeOrFail(firstStation->received().last());
    QCOMPARE(abandoned.commandVerb, QByteArrayLiteral("setFourO3AEnabled"));
    QVERIFY(finished.isEmpty());
    // The old request never receives a result. Both a link loss and a
    // directly adopted replacement must retire its pending completion.
    if (closeBeforeReplacement) {
        firstStation->closeLink(QStringLiteral("lost before command result"));
        QTRY_VERIFY(!client.remoteFourO3AControlAvailable());
    }

    auto* currentStation = attach();
    QTRY_VERIFY(client.remoteFourO3AControlAvailable());
    QVERIFY(client.requestFourO3AEnabled(false).sent);
    QTRY_VERIFY(currentStation->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
    const SessionMessage current = decodeOrFail(currentStation->received().last());
    QCOMPARE(current.commandVerb, QByteArrayLiteral("setFourO3AEnabled"));
    QVERIFY(current.commandId != abandoned.commandId);
    currentStation->sendText(SessionMessages::encode(SessionMessages::commandResult(
        current.commandVerb, current.commandId, true, {}, {})));
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(finished.first().at(0).toBool());
    QVERIFY(!remote.smartSdrListener()->isListening());
}

void TstStationSession::remoteTgxlCommandIsGatedAtAuthenticatedServerBoundary()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    auto* station = new LoopbackTransport(QStringLiteral("tgxl-server"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("tgxl-peer"), this);
    station->linkTo(peer);
    server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, static_cast<quint16>(kRemoteTgxlConfigSessionProtocolMinor - 1),
        6, QStringLiteral("older-client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    peer->clearReceived();

    peer->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
        "configureTgxl", 17,
        { MirrorUpdate{ 0, "host", MirrorWireKind::Utf8, QStringLiteral("192.0.2.10") },
          MirrorUpdate{ 0, "port", MirrorWireKind::Int64, qint64(9010) } })));
    QTRY_VERIFY(peer->receivedKinds().contains(QByteArrayLiteral("command.result")));
    SessionMessage result;
    for (const QByteArray& wire : peer->received()) {
        const SessionMessage candidate = decodeOrFail(wire);
        if (candidate.kind == SessionMessageKind::CommandResult
            && candidate.commandId == quint32(17)) {
            result = candidate;
            break;
        }
    }
    QCOMPARE(result.kind, SessionMessageKind::CommandResult);
    QVERIFY(!result.accepted);
    QVERIFY(result.reason.contains(QStringLiteral("newer station protocol")));

    // With the negotiated minor this reaches the dispatcher and model
    // policy. The fixture intentionally lacks station accessory identity,
    // so it must refuse without mutating an accessory.
    auto* currentStation = new LoopbackTransport(QStringLiteral("tgxl-current-server"), this);
    auto* currentPeer = new LoopbackTransport(QStringLiteral("tgxl-current-peer"), this);
    currentStation->linkTo(currentPeer);
    server.acceptTransport(currentStation);
    currentPeer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("current-client"))));
    currentPeer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(currentPeer->receivedKinds().contains(QByteArrayLiteral("auth.result")));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    currentPeer->clearReceived();
    currentPeer->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
        "configureTgxl", 18,
        { MirrorUpdate{ 0, "host", MirrorWireKind::Utf8, QStringLiteral("192.0.2.10") },
          MirrorUpdate{ 0, "port", MirrorWireKind::Int64, qint64(9010) } })));
    QTRY_VERIFY(currentPeer->receivedKinds().contains(QByteArrayLiteral("command.result")));
    SessionMessage currentResult;
    for (const QByteArray& wire : currentPeer->received()) {
        const SessionMessage candidate = decodeOrFail(wire);
        if (candidate.kind == SessionMessageKind::CommandResult
            && candidate.commandId == quint32(18)) {
            currentResult = candidate;
            break;
        }
    }
    QCOMPARE(currentResult.kind, SessionMessageKind::CommandResult);
    QVERIFY(!currentResult.accepted);
    QVERIFY(currentResult.reason != QStringLiteral("unrecognised command verb"));
    QVERIFY(!currentResult.reason.contains(QStringLiteral("newer station protocol")));
}

void TstStationSession::remoteTgxlConfigureAcceptanceStartsIdentityOnly()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    // This is a genuine station-side admission path.  A listening test peer
    // lets us prove acceptance starts native identification, while withholding
    // the TGXL version/identity exchange proves accepted does not mean the
    // device state has been hydrated or declared connected.
    QTcpServer tgxl;
    QVERIFY(tgxl.listen(QHostAddress::LocalHost, 0));
    auto stationModel = makeStationRadioModel(0);
    stationModel->setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    stationModel->enableStationAccessoryIdentity();
    TunerModel* const tuner = stationModel->tunerModel();
    QVERIFY(tuner != nullptr);
    AppSettings::instance().save(); // Establish the pre-command on-disk state.
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    auto* station = new LoopbackTransport(QStringLiteral("tgxl-accepted-server"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("tgxl-accepted-peer"), this);
    station->linkTo(peer);
    server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("current-client"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    peer->clearReceived();

    peer->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
        "configureTgxl", 19,
        { MirrorUpdate{ 0, "host", MirrorWireKind::Utf8, QStringLiteral("127.0.0.1") },
          MirrorUpdate{ 0, "port", MirrorWireKind::Int64, qint64(tgxl.serverPort()) } })));
    QTRY_VERIFY(peer->receivedKinds().contains(QByteArrayLiteral("command.result")));
    SessionMessage result;
    for (const QByteArray& wire : peer->received()) {
        const SessionMessage candidate = decodeOrFail(wire);
        if (candidate.kind == SessionMessageKind::CommandResult
            && candidate.commandId == quint32(19)) {
            result = candidate;
            break;
        }
    }
    QCOMPARE(result.kind, SessionMessageKind::CommandResult);
    QVERIFY2(result.accepted, qPrintable(result.reason));
    QCOMPARE(stationModel->peripheralValue(QStringLiteral("TGXL_ManualIp")),
             QStringLiteral("127.0.0.1"));
    QCOMPARE(stationModel->peripheralValue(QStringLiteral("TGXL_ManualPort")),
             QString::number(tgxl.serverPort()));
    AppSettings persisted(AppSettings::instance().filePath());
    persisted.load();
    QCOMPARE(persisted.hardwareValue(stationModel->currentRadioMac(),
                  QStringLiteral("peripherals/TGXL_ManualIp")).toString(), QStringLiteral("127.0.0.1"));
    QCOMPARE(persisted.hardwareValue(stationModel->currentRadioMac(),
                  QStringLiteral("peripherals/TGXL_ManualPort")).toString(), QString::number(tgxl.serverPort()));
    QTRY_VERIFY(tgxl.hasPendingConnections());
    QVERIFY(stationModel->tgxlConnection()->identityInfo().serial.isEmpty());
    QVERIFY(!tuner->hasDirectConnection());
    QVERIFY(!tuner->isPresent());

    QString reason;
    QVERIFY2(stationModel->disconnectTgxlForStation(&reason), qPrintable(reason));
}

// ── TokenStore ───────────────────────────────────────────────────────────

void TstStationSession::tokenIsGeneratedNotChosenAndPersists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    TokenStore first(dir.path());
    QVERIFY2(first.isValid(), qPrintable(first.lastError()));
    QVERIFY(first.wasGeneratedThisRun());

    // 256 bits, base64url, no padding.
    QCOMPARE(first.token().size(), 43);
    QVERIFY(!first.token().contains(QLatin1Char('=')));
    QVERIFY(!first.token().contains(QLatin1Char('+')));
    QVERIFY(!first.token().contains(QLatin1Char('/')));

    // There is no setter, by design: the token is generated, never
    // user-chosen (parent design section 7.1). The observable form of that
    // is that a second construction against the same directory returns the
    // SAME token rather than a new one, and does not report itself as a
    // first run.
    TokenStore second(dir.path());
    QVERIFY(second.isValid());
    QVERIFY(!second.wasGeneratedThisRun());
    QCOMPARE(second.token(), first.token());

    // Two independent stores must not collide.
    QTemporaryDir other;
    QVERIFY(other.isValid());
    TokenStore elsewhere(other.path());
    QVERIFY(elsewhere.isValid());
    QVERIFY(elsewhere.token() != first.token());
}

void TstStationSession::tokenVerifyIsRateLimitedAfterRepeatedFailures()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    TokenStore store(dir.path());
    QVERIFY(store.isValid());

    // Short lockout so the slot does not cost a minute.
    store.setRateLimit(3, 200);

    QCOMPARE(store.verify(store.token()), TokenStore::VerifyResult::Accepted);

    for (int i = 0; i < 3; ++i) {
        QCOMPARE(store.verify(QStringLiteral("nope")), TokenStore::VerifyResult::Rejected);
    }
    QCOMPARE(store.consecutiveFailures(), 3);

    // Rate limited now -- and the CORRECT token is refused too, which is
    // the point: a rate limiter that let the right answer through would
    // not slow an attacker down at all.
    QCOMPARE(store.verify(QStringLiteral("nope")), TokenStore::VerifyResult::RateLimited);
    QCOMPARE(store.verify(store.token()), TokenStore::VerifyResult::RateLimited);

    QTest::qWait(250);
    QCOMPARE(store.verify(store.token()), TokenStore::VerifyResult::Accepted);
    QCOMPARE(store.consecutiveFailures(), 0);
}

// ── The connect sequence ─────────────────────────────────────────────────

void TstStationSession::handshakeCompletesInSectionSevenZeroOrder()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50001"));

    auto stationModel = makeStationRadioModel(1);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(!server.token().isEmpty(), "TokenStore did not provision");

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    // The client end is driven BY HAND here, so this slot asserts the
    // station's real output rather than what a client happened to make of
    // it.
    server.acceptTransport(stationEnd);

    QTRY_VERIFY(!clientEnd->received().isEmpty());
    const SessionMessage stationHello = decodeOrFail(clientEnd->received().first());
    QCOMPARE(stationHello.kind, SessionMessageKind::Hello);
    QCOMPARE(stationHello.protocolMajor, kSessionProtocolMajor);

    clientEnd->sendText(SessionMessages::encode(
        SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                               QStringLiteral("test-client"))));
    clientEnd->sendText(
        SessionMessages::encode(SessionMessages::authRequest(server.token())));

    QTRY_VERIFY(clientEnd->receivedKinds().contains(QByteArrayLiteral("snapshot.complete")));

    const QList<QByteArray> kinds = clientEnd->receivedKinds();

    // Parent design section 7.0: "TLS establish -> protocol hello carrying
    // a semantic version from both ends -> authentication -> capability
    // exchange -> state snapshot -> snapshot-complete marker."
    const int hello = indexOfKind(kinds, "hello");
    const int auth = indexOfKind(kinds, "auth.result");
    const int caps = indexOfKind(kinds, "capabilities");
    const int settings = indexOfKind(kinds, "settings.snapshot");
    const int schema = indexOfKind(kinds, "schema");
    const int create = indexOfKind(kinds, "object.create");
    const int done = indexOfKind(kinds, "snapshot.complete");

    QVERIFY2(hello == 0, qPrintable(QStringLiteral("kinds: %1")
                                        .arg(QString::fromUtf8(kinds.join(',')))));
    QVERIFY(auth > hello);
    QVERIFY(caps > auth);
    QVERIFY(settings > caps);
    QVERIFY(schema > settings);
    QVERIFY(create > schema);
    QVERIFY(done > create);
    // The marker closes the burst: no schema and no object.create may
    // follow it. Asserted this way rather than "the marker is the last
    // message", because an ordinary Delta legitimately can follow (the
    // station's flush timer runs from the moment the session is
    // established), and pinning the absolute last index would make this
    // slot fail on timing rather than on ordering.
    QVERIFY(kinds.lastIndexOf(QByteArrayLiteral("schema")) < done);
    QVERIFY(kinds.lastIndexOf(QByteArrayLiteral("object.create")) < done);
    QVERIFY(kinds.lastIndexOf(QByteArrayLiteral("capabilities")) < done);
    QVERIFY(kinds.lastIndexOf(QByteArrayLiteral("settings.snapshot")) < done);

    QVERIFY(server.hasAuthenticatedSession());
}

void TstStationSession::capabilitiesAdvertiseEffectiveNotBoardLimits()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    const StationCapabilities wide = server.buildCapabilities();
    QVERIFY(wide.boardMaxSlices > 1);
    // With no narrowing configured the effective value equals the board's.
    QCOMPARE(wide.effectiveMaxSlices, wide.boardMaxSlices);
    QCOMPARE(wide.board, HPSDRHW::HermesLite);
    QVERIFY(wide.radioConnected);
    // TX is R4 in its entirety; R2 must never advertise otherwise.
    QVERIFY(!wide.txPermitted);

    // Parent section 4.5: the descriptor advertises what the DAEMON can
    // sustain, and the client gates on that, never on the board value.
    server.setSustainableSliceLimit(2);
    const StationCapabilities narrowed = server.buildCapabilities();
    QCOMPARE(narrowed.effectiveMaxSlices, 2);
    QCOMPARE(narrowed.boardMaxSlices, wide.boardMaxSlices);
    QVERIFY(narrowed.effectiveMaxSlices < narrowed.boardMaxSlices);

    // A configured limit ABOVE what the radio has is clamped: advertising
    // more slices than exist is a worse failure than advertising fewer.
    server.setSustainableSliceLimit(wide.boardMaxSlices + 5);
    QCOMPARE(server.buildCapabilities().effectiveMaxSlices, wide.boardMaxSlices);

    // Round trip through the wire form.
    const QList<MirrorUpdate> encoded = narrowed.toUpdates();
    const StationCapabilities decoded = StationCapabilities::fromUpdates(encoded);
    QCOMPARE(decoded.effectiveMaxSlices, narrowed.effectiveMaxSlices);
    QCOMPARE(decoded.boardMaxSlices, narrowed.boardMaxSlices);
    QCOMPARE(decoded.board, narrowed.board);
    QCOMPARE(decoded.macAddress, narrowed.macAddress);
    QCOMPARE(server.buildCapabilities().remoteWidebandDisplayVersion, 0);
    QCOMPARE(server.buildCapabilities().remoteAudioStatusVersion, 0);
    QCOMPARE(StationCapabilities::fromUpdates(narrowed.toUpdates()).remoteAudioStatusVersion, 0);
    QCOMPARE(server.buildCapabilities().spectrumGrantVersion, 0);
    QCOMPARE(StationCapabilities::fromUpdates(narrowed.toUpdates()).spectrumGrantVersion, 0);
    server.setMediaEnabled(true);
    const auto mediaCaps = server.buildCapabilities();
    QCOMPARE(mediaCaps.remoteWidebandDisplayVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates(mediaCaps.toUpdates()).remoteWidebandDisplayVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates({}).remoteWidebandDisplayVersion, 0);
    QCOMPARE(mediaCaps.remoteAudioStatusVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates(mediaCaps.toUpdates()).remoteAudioStatusVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates({}).remoteAudioStatusVersion, 0);
    QCOMPARE(mediaCaps.spectrumGrantVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates(mediaCaps.toUpdates()).spectrumGrantVersion, 1);
    QCOMPARE(StationCapabilities::fromUpdates({}).spectrumGrantVersion, 0);
}

void TstStationSession::clientAppliesCapabilitiesAndDrivesConnected()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(2);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setSustainableSliceLimit(3);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    // Before the handshake: task 3's storage-backed isConnected() is
    // false, and maxSlices() is pinned at its disconnected default.
    QVERIFY(!clientModel.isConnected());
    QCOMPARE(clientModel.maxSlices(), 1);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy stateChanges(&clientModel, &RadioModel::connectionStateChanged);

    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);

    QTRY_COMPARE(completed.count(), 1);

    // Step 6, the step that makes three earlier tasks mean anything.
    QVERIFY(clientModel.isConnected());
    QVERIFY(!stateChanges.isEmpty());
    QCOMPARE(clientModel.maxSlices(), 3);
    QCOMPARE(clientModel.stationUserDdcCount(),
             stationModel->boardCapabilities().userDdcCount);
    QCOMPARE(clientModel.boardCapabilities().board, HPSDRHW::HermesLite);
    QCOMPARE(clientModel.currentRadioMac(), QStringLiteral("AA:BB:CC:DD:EE:01"));

    // The mirror carried every slice the station already held, under the
    // STATION's ids.
    QCOMPARE(clientModel.slices().size(), stationModel->slices().size());
    for (SliceModel* stationSlice : stationModel->slices()) {
        QVERIFY2(clientModel.sliceById(stationSlice->sliceIndex()) != nullptr,
                 qPrintable(QStringLiteral("client is missing slice id %1")
                                .arg(stationSlice->sliceIndex())));
    }
}

void TstStationSession::lateRadioRefreshUpdatesAuthenticatedClientWithoutReplayingMirror()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    // Start as the R-R3-27 daemon does when discovery has not yet found its
    // configured radio.  Keep a real slice in the mirror so the assertion
    // below proves the late identity update does not replace GUI objects.
    auto stationModel = makeStationRadioModel(0);
    stationModel->setConnectionStateForTest(ConnectionState::Disconnected);
    QCOMPARE(stationModel->addPanadapter(), 0);
    const QString mac = QStringLiteral("AA:BB:CC:DD:EE:01");
    stationSettings.setHardwareValue(
        mac, QStringLiteral("radioInfo/sampleRate"), QStringLiteral("192000"));

    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setSustainableSliceLimit(2);
    server.setMediaEnabled(true);

    RadioModel clientModel(RadioModel::Role::Remote);
    QCOMPARE(clientModel.addPanadapter(), 0);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy authenticated(&server, &StationServer::clientAuthenticated);
    QSignalSpy mediaStarted(&server, &StationServer::mediaSessionStarted);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    QVERIFY(!clientModel.isConnected());
    QVERIFY(clientModel.currentRadioMac().isEmpty());
    const int sliceId = stationModel->slices().first()->sliceIndex();
    SliceModel* const existingSlice = clientModel.sliceById(sliceId);
    const QList<PanadapterModel*> initialPans = clientModel.panadapters();
    QVERIFY(!initialPans.isEmpty());
    PanadapterModel* const existingPan = initialPans.first();
    QVERIFY(existingSlice != nullptr);
    QVERIFY(existingPan != nullptr);
    QCOMPARE(authenticated.count(), 1);
    QCOMPARE(mediaStarted.count(), 1);
    QVERIFY(!proxy.contains(QStringLiteral("hardware/%1/radioInfo/sampleRate").arg(mac)));

    clientEnd->clearReceived();
    stationModel->setConnectionStateForTest(ConnectionState::Connected);
    stationModel->emitCurrentRadioChangedForTest();

    QTRY_VERIFY(clientModel.isConnected());
    QCOMPARE(clientModel.currentRadioMac(), mac);
    QCOMPARE(clientModel.boardCapabilities().board, HPSDRHW::HermesLite);
    QCOMPARE(clientModel.maxSlices(), 2);
    QTRY_COMPARE(proxy.value(QStringLiteral("hardware/%1/radioInfo/sampleRate").arg(mac), QVariant{})
                     .toString(),
                 QStringLiteral("192000"));

    const QList<QByteArray> lateKinds = clientEnd->receivedKinds();
    QVERIFY(lateKinds.contains(QByteArrayLiteral("capabilities")));
    QVERIFY(lateKinds.contains(QByteArrayLiteral("settings.snapshot")));
    QVERIFY(!lateKinds.contains(QByteArrayLiteral("schema")));
    QVERIFY(!lateKinds.contains(QByteArrayLiteral("object.create")));
    QVERIFY(!lateKinds.contains(QByteArrayLiteral("snapshot.complete")));
    QCOMPARE(clientModel.sliceById(sliceId), existingSlice);
    const QList<PanadapterModel*> refreshedPans = clientModel.panadapters();
    QVERIFY(!refreshedPans.isEmpty());
    QCOMPARE(refreshedPans.first(), existingPan);
    QCOMPARE(completed.count(), 1);
    QCOMPARE(authenticated.count(), 1);
    QCOMPARE(mediaStarted.count(), 1);
}

// ── Version policy ───────────────────────────────────────────────────────

// The replacement race uses the same real session boundary as the ordinary
// loopback coverage above.
void TstStationSession::queuedLateRadioRefreshDoesNotReachReplacementSession()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    stationModel->setConnectionStateForTest(ConnectionState::Disconnected);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel oldClientModel(RadioModel::Role::Remote);
    SettingsProxy oldProxy;
    StationClient oldClient(&oldClientModel, &oldProxy);
    auto* oldStationEnd = new LoopbackTransport(QStringLiteral("old-station-end"), this);
    auto* oldClientEnd = new LoopbackTransport(QStringLiteral("old-client-end"), this);
    oldStationEnd->linkTo(oldClientEnd);
    QSignalSpy oldCompleted(&oldClient, &StationClient::handshakeComplete);
    oldClient.startSession(oldClientEnd, server.token());
    server.acceptTransport(oldStationEnd);
    QTRY_COMPARE(oldCompleted.count(), 1);
    oldClientEnd->clearReceived();

    // The deferred callback captures the old session here. Before its timer
    // may run, synchronously authenticate a replacement transport. This is
    // the reentrant boundary that a normal queued socket cannot reach in one
    // test turn, and it pins the epoch check against a replacement session.
    stationModel->setConnectionStateForTest(ConnectionState::Connected);
    stationModel->emitCurrentRadioChangedForTest();

    auto* replacementStation =
        new ImmediateTransport(QStringLiteral("replacement-station"), this);
    auto* replacementClient =
        new ImmediateTransport(QStringLiteral("replacement-client"), this);
    replacementStation->linkTo(replacementClient);
    server.acceptTransport(replacementStation);
    replacementClient->sendText(SessionMessages::encode(
        SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                               QStringLiteral("replacement-client"))));
    replacementClient->sendText(
        SessionMessages::encode(SessionMessages::authRequest(server.token())));

    QTRY_VERIFY(replacementClient->receivedKinds().contains(
        QByteArrayLiteral("snapshot.complete")));
    QTRY_VERIFY(oldClientEnd->receivedKinds().contains(QByteArrayLiteral("session.end")));

    const QList<QByteArray> replacementKinds = replacementClient->receivedKinds();
    QCOMPARE(replacementKinds.count(QByteArrayLiteral("capabilities")), 1);
    QCOMPARE(replacementKinds.count(QByteArrayLiteral("settings.snapshot")), 1);
    const QList<QByteArray> oldKinds = oldClientEnd->receivedKinds();
    QVERIFY(!oldKinds.contains(QByteArrayLiteral("capabilities")));
    QVERIFY(!oldKinds.contains(QByteArrayLiteral("settings.snapshot")));
    QCOMPARE(oldCompleted.count(), 1);
}

// Version policy (task 18 step 1)
void TstStationSession::majorVersionMismatchRefusesNamingBothVersions()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    server.acceptTransport(stationEnd);
    QTRY_VERIFY(!clientEnd->received().isEmpty());

    const quint16 wrongMajor = kSessionProtocolMajor + 1;
    clientEnd->sendText(SessionMessages::encode(SessionMessages::hello(
        wrongMajor, 4, 6, QStringLiteral("from-the-future"))));

    QTRY_VERIFY(clientEnd->receivedKinds().contains(QByteArrayLiteral("session.end")));

    QString reason;
    for (const QByteArray& wire : clientEnd->received()) {
        const SessionMessage message = decodeOrFail(wire);
        if (message.kind == SessionMessageKind::SessionEnd) {
            reason = message.reason;
        }
    }
    // Section 7.0: "refused with a message naming both versions rather
    // than failing obscurely". BOTH, not just the offending one.
    QVERIFY2(reason.contains(QStringLiteral("%1.%2")
                                 .arg(kSessionProtocolMajor)
                                 .arg(kSessionProtocolMinor)),
             qPrintable(reason));
    QVERIFY2(reason.contains(QStringLiteral("%1.%2").arg(wrongMajor).arg(4)),
             qPrintable(reason));

    QVERIFY(!server.hasAuthenticatedSession());

    // And the mirror-image case: a CLIENT pointed at a station whose major
    // it does not know refuses from its own side, also naming both.
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    auto* fakeStationEnd = new LoopbackTransport(QStringLiteral("fake-station"), this);
    auto* realClientEnd = new LoopbackTransport(QStringLiteral("client"), this);
    fakeStationEnd->linkTo(realClientEnd);
    client.startSession(realClientEnd, QStringLiteral("irrelevant"));
    fakeStationEnd->sendText(SessionMessages::encode(
        SessionMessages::hello(wrongMajor, 9, 6, QStringLiteral("future-station"))));

    QTRY_COMPARE(ended.count(), 1);
    const QString clientReason = ended.first().first().toString();
    QVERIFY2(clientReason.contains(QStringLiteral("%1.%2")
                                       .arg(kSessionProtocolMajor)
                                       .arg(kSessionProtocolMinor)),
             qPrintable(clientReason));
    QVERIFY2(clientReason.contains(QStringLiteral("%1.%2").arg(wrongMajor).arg(9)),
             qPrintable(clientReason));
    QVERIFY(!clientModel.isConnected());
}

void TstStationSession::minorVersionMismatchNegotiatesDown()
{
    // Section 7.0: "Equal major with differing minor negotiates down to
    // the lower, and each side gates optional behaviour on the agreed
    // value. A desktop GUI at R4 pointed at a Pi still running R2 is the
    // expected case, not an error case."
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* fakeStationEnd = new LoopbackTransport(QStringLiteral("fake-station"), this);
    auto* realClientEnd = new LoopbackTransport(QStringLiteral("client"), this);
    fakeStationEnd->linkTo(realClientEnd);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    client.startSession(realClientEnd, QStringLiteral("token"));
    const quint16 higherMinor = static_cast<quint16>(kSessionProtocolMinor + 7);
    fakeStationEnd->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, higherMinor, 6, QStringLiteral("newer-station"))));

    // The client answers rather than refusing, and settles on the LOWER of
    // the two minors.
    QTRY_VERIFY(fakeStationEnd->receivedKinds().contains(QByteArrayLiteral("hello")));
    QCOMPARE(client.agreedMinor(), kSessionProtocolMinor);
    QCOMPARE(ended.count(), 0);

    // The station side does not refuse a differing minor either: a full
    // session establishes against a client advertising one.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* rawClient = new LoopbackTransport(QStringLiteral("raw-client"), this);
    stationEnd->linkTo(rawClient);
    server.acceptTransport(stationEnd);
    QTRY_VERIFY(!rawClient->received().isEmpty());

    rawClient->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, higherMinor, 6, QStringLiteral("newer-client"))));
    rawClient->sendText(
        SessionMessages::encode(SessionMessages::authRequest(server.token())));

    QTRY_VERIFY(server.hasAuthenticatedSession());
    QVERIFY(!rawClient->receivedKinds().contains(QByteArrayLiteral("session.end")));
}

// ── Authentication ───────────────────────────────────────────────────────

void TstStationSession::badTokenIsRefusedAndThenRateLimited()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setAuthRateLimit(2, 60000);

    QStringList reasons;
    for (int attempt = 0; attempt < 3; ++attempt) {
        auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
        auto* rawClient = new LoopbackTransport(QStringLiteral("raw-client"), this);
        stationEnd->linkTo(rawClient);
        server.acceptTransport(stationEnd);
        QTRY_VERIFY(!rawClient->received().isEmpty());

        rawClient->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 6,
            QStringLiteral("wrong-token-client"))));
        rawClient->sendText(SessionMessages::encode(
            SessionMessages::authRequest(QStringLiteral("definitely-not-the-token"))));

        QTRY_VERIFY(rawClient->receivedKinds().contains(QByteArrayLiteral("auth.result")));
        for (const QByteArray& wire : rawClient->received()) {
            const SessionMessage message = decodeOrFail(wire);
            if (message.kind == SessionMessageKind::AuthResult) {
                QVERIFY(!message.accepted);
                reasons.append(message.reason);
            }
        }
        QVERIFY(!server.hasAuthenticatedSession());
    }

    QCOMPARE(reasons.size(), 3);
    // The first two are ordinary refusals; the third is the rate limiter,
    // which says something DIFFERENT on purpose. Collapsing the two would
    // leak, through the daemon's own answer, which guesses were close.
    QVERIFY2(reasons.at(0).contains(QStringLiteral("Authentication failed")),
             qPrintable(reasons.at(0)));
    QVERIFY2(reasons.at(1).contains(QStringLiteral("Authentication failed")),
             qPrintable(reasons.at(1)));
    QVERIFY2(reasons.at(2).contains(QStringLiteral("Too many failed")),
             qPrintable(reasons.at(2)));

    // Even the RIGHT token is refused while the lockout stands.
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* rawClient = new LoopbackTransport(QStringLiteral("raw-client"), this);
    stationEnd->linkTo(rawClient);
    server.acceptTransport(stationEnd);
    QTRY_VERIFY(!rawClient->received().isEmpty());
    rawClient->sendText(SessionMessages::encode(
        SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                               QStringLiteral("right-token-client"))));
    rawClient->sendText(
        SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(rawClient->receivedKinds().contains(QByteArrayLiteral("auth.result")));
    QVERIFY(!server.hasAuthenticatedSession());
}

// ── Session model ────────────────────────────────────────────────────────

void TstStationSession::secondAuthenticatedConnectionPreemptsAndSaysWhy()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QSignalSpy preempted(&server, &StationServer::sessionPreempted);

    // The description the station records for a peer is the STATION-side
    // transport's, since that is the object it was handed, so that is what
    // is named here.
    auto authenticate = [&](const QString& name, LoopbackTransport** clientEndOut) {
        auto* stationEnd = new LoopbackTransport(name, this);
        auto* clientEnd = new LoopbackTransport(name + QStringLiteral("-client"), this);
        stationEnd->linkTo(clientEnd);
        server.acceptTransport(stationEnd);
        QTRY_VERIFY(!clientEnd->received().isEmpty());
        clientEnd->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 6, name)));
        clientEnd->sendText(
            SessionMessages::encode(SessionMessages::authRequest(server.token())));
        QTRY_VERIFY(
            clientEnd->receivedKinds().contains(QByteArrayLiteral("snapshot.complete")));
        *clientEndOut = clientEnd;
    };

    LoopbackTransport* first = nullptr;
    authenticate(QStringLiteral("first"), &first);
    QVERIFY(server.hasAuthenticatedSession());
    QCOMPARE(server.peerCount(), 1);

    LoopbackTransport* second = nullptr;
    authenticate(QStringLiteral("second"), &second);

    // Parent section 7.1: "A second authenticated connection preempts the
    // existing session ... The displaced session is told why."
    QTRY_COMPARE(preempted.count(), 1);
    QCOMPARE(preempted.first().first().toString(), QStringLiteral("first"));

    QString displacedReason;
    for (const QByteArray& wire : first->received()) {
        const SessionMessage message = decodeOrFail(wire);
        if (message.kind == SessionMessageKind::SessionEnd) {
            displacedReason = message.reason;
        }
    }
    QVERIFY2(!displacedReason.isEmpty(),
             "the displaced session was closed without being told why");
    QVERIFY2(displacedReason.contains(QStringLiteral("second")),
             qPrintable(displacedReason));

    // Exactly one session survives, and it is the newcomer.
    QTRY_COMPARE(server.peerCount(), 1);
    QVERIFY(server.hasAuthenticatedSession());
    QVERIFY(second->isOpen());
}

// ── Heartbeat ────────────────────────────────────────────────────────────

void TstStationSession::heartbeatDetectsAPeerThatWentSilentWithoutClosing()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    // The production defaults are 20 s and 2 misses (StationServer.h has
    // the reasoning for both numbers). Driven down here so the slot costs
    // milliseconds; the MECHANISM under test is identical, and the numbers
    // are asserted against the configuration rather than hardcoded, so
    // this stays honest if the defaults ever move.
    server.setHeartbeatIntervalMs(20);
    server.setMaxMissedPongs(2);
    QCOMPARE(server.heartbeatIntervalMs(), 20);
    QCOMPARE(server.maxMissedPongs(), 2);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("silent-peer"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy timedOut(&server, &StationServer::peerHeartbeatTimeout);
    server.acceptTransport(stationEnd);
    QTRY_VERIFY(!clientEnd->received().isEmpty());

    // THE CASE THIS WHOLE STEP EXISTS FOR. The link stays nominally OPEN;
    // the peer simply stops answering. No close, no write error, nothing
    // for a close-driven implementation to notice. This is what a laptop
    // lid, a cell handoff and a NAT timeout all look like from here, and
    // it is why copying TciServer's ping-without-pong-tracking would not
    // have been enough.
    QVERIFY(clientEnd->isOpen());
    clientEnd->setAnswersPings(false);

    QTRY_COMPARE_WITH_TIMEOUT(timedOut.count(), 1, 5000);
    QCOMPARE(timedOut.first().first().toString(), QStringLiteral("silent-peer"));

    // The peer really was dropped, not merely reported.
    QTRY_COMPARE(server.peerCount(), 0);
    QVERIFY(!server.hasAuthenticatedSession());

    // The pings really did go out: without them the miss counter could
    // only ever have been driven by something other than the heartbeat.
    QVERIFY2(clientEnd->pingsSeen() >= server.maxMissedPongs(),
             qPrintable(QStringLiteral("only %1 pings reached the peer")
                            .arg(clientEnd->pingsSeen())));
}

void TstStationSession::heartbeatLeavesAnAnsweringPeerAlone()
{
    // The control for the slot above. A detector that fired on every peer
    // would pass that test and be worse than useless.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setHeartbeatIntervalMs(20);
    server.setMaxMissedPongs(2);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("healthy-client"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy timedOut(&server, &StationServer::peerHeartbeatTimeout);
    server.acceptTransport(stationEnd);

    // Long enough for many more than maxMissedPongs intervals to elapse.
    QTest::qWait(20 * 2 * 15);

    // The claim FIRST, the non-vacuity guard second. Ordered this way
    // deliberately: an implementation that stopped tracking pongs kills
    // this peer, which also stops the pings, so a pingsSeen() check placed
    // first would fire instead and the failure would name the wrong thing.
    QCOMPARE(timedOut.count(), 0);
    QCOMPARE(server.peerCount(), 1);
    QVERIFY2(clientEnd->pingsSeen() > server.maxMissedPongs(),
             "the heartbeat did not actually run during the wait, so the "
             "absence of a timeout above proves nothing");
}

// ── Mirror and settings ──────────────────────────────────────────────────

void TstStationSession::mirrorRoundTripsSliceStateAndDoesNotEcho()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    SliceModel* stationSlice = stationModel->slices().first();
    SliceModel* clientSlice = clientModel.sliceById(stationSlice->sliceIndex());
    QVERIFY(clientSlice != nullptr);

    // Station to client: an ordinary property delta.
    stationEnd->clearReceived();
    stationSlice->setFrequency(7123456.0);
    QTRY_COMPARE(clientSlice->frequency(), 7123456.0);

    // THE ECHO GUARD, asserted directly rather than inferred from message
    // volume. Applying an inbound value calls a real setter, which emits a
    // real NOTIFY, which the client's own outbound watcher would forward
    // straight back as a property.write. m_applyingInbound is what stops
    // it. Counting total traffic instead would NOT catch the guard being
    // removed: SliceModel's setters are emit-on-change, so the echo the
    // station receives carries the value it already holds and dies there
    // without generating a reply -- one wasted round trip, invisible in a
    // volume count, and a genuine correctness hole for any property whose
    // setter is not emit-on-change.
    QTest::qWait(StationClient::kDefaultWriteFlushMs * 4);
    QVERIFY2(!stationEnd->receivedKinds().contains(QByteArrayLiteral("property.write")),
             "the client echoed the station's own delta straight back to it");

    // Client to station: the inbound half of the mirror.
    const int before = clientEnd->received().size();
    clientSlice->setFrequency(14074000.0);
    QTRY_COMPARE(stationSlice->frequency(), 14074000.0);

    // And no echo storm: applying the station's answer must not produce
    // another outbound write, which would ping-pong forever. Give the
    // event loop several flush intervals to misbehave in.
    QTest::qWait(StationClient::kDefaultWriteFlushMs * 6);
    const int after = clientEnd->received().size();
    QVERIFY2(after - before < 5,
             qPrintable(QStringLiteral("station sent %1 messages after one client write")
                            .arg(after - before)));
    QCOMPARE(stationSlice->frequency(), 14074000.0);
    QCOMPARE(clientSlice->frequency(), 14074000.0);

    // The per-slice S-meter reaches the client. SliceModel::
    // signalStrengthDbm has no Q_PROPERTY WRITE, so this only works
    // through the applyMirroredValue hook task 12 built for exactly this.
    // This station has no WDSP channel, so its SliceMeterPump would
    // overwrite every reading with the no-reading value (R-R3-13) before
    // the mirror carries it. Stop it: these setters stand in for the pump.
    stopSliceMeterPump(stationModel.get());
    stationSlice->setSignalStrengthDbm(-73.0);
    QTRY_COMPARE(clientSlice->signalStrengthDbm(), -73.0);
    stationSlice->setSignalPeakDbm(-61.0);
    stationSlice->setSignalAverageDbm(-79.0);
    QTRY_COMPARE(clientSlice->signalPeakDbm(), -61.0);
    QTRY_COMPARE(clientSlice->signalAverageDbm(), -79.0);

    // Read-only telemetry must never turn into a client command, even if
    // code changes the client's local copy. Only Core is authoritative.
    stationEnd->clearReceived();
    clientSlice->setSignalPeakDbm(-20.0);
    clientSlice->setSignalAverageDbm(-30.0);
    // A real writable property is a flush barrier, avoiding a timed sleep.
    clientSlice->setFrequency(14075100.0);
    QTRY_COMPARE(stationSlice->frequency(), 14075100.0);
    for (const QByteArray& wire : stationEnd->received()) {
        const SessionMessage message = decodeOrFail(wire);
        if (message.kind != SessionMessageKind::PropertyWrite) { continue; }
        for (const MirrorUpdate& update : message.updates) {
            QVERIFY(update.name != "signalPeakDbm");
            QVERIFY(update.name != "signalAverageDbm");
        }
    }
    QCOMPARE(stationSlice->signalPeakDbm(), -61.0);
    QCOMPARE(stationSlice->signalAverageDbm(), -79.0);
}

void TstStationSession::autoAgcTelemetryFollowsCoreAcrossReconnect()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    AppSettings settings(directory.filePath(QStringLiteral("station.settings")));
    auto station = makeStationRadioModel(1);
    auto* first = station->slices().at(0);
    auto* second = station->slices().at(1);
    StationServer server(station.get(), settings, m_securityDir.path());
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    for (int session = 1; session <= 2; ++session) {
        first->setStationAutoAgcNoiseFloor(-113.0, true, session * 10);
        second->setStationAutoAgcNoiseFloor(-91.0, false, session * 10 + 1);
        auto* stationEnd = new LoopbackTransport(QStringLiteral("agc-station"), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("agc-client"), this);
        stationEnd->linkTo(clientEnd);
        client.startSession(clientEnd, server.token());
        server.acceptTransport(stationEnd);
        QTRY_COMPARE(completed.count(), session);
        auto* remoteFirst = remote.sliceById(first->sliceIndex());
        auto* remoteSecond = remote.sliceById(second->sliceIndex());
        QVERIFY(remoteFirst && remoteSecond);
        QCOMPARE(remoteFirst->stationAutoAgcNoiseFloorDbm(), -113.0);
        QVERIFY(remoteFirst->stationAutoAgcNoiseFloorValid());
        QCOMPARE(remoteFirst->stationAutoAgcNoiseFloorGeneration(), quint64(session * 10));
        QCOMPARE(remoteSecond->stationAutoAgcNoiseFloorDbm(), -91.0);
        QVERIFY(!remoteSecond->stationAutoAgcNoiseFloorValid());

        first->setStationAutoAgcNoiseFloor(-108.5, false, session * 10 + 2);
        QTRY_COMPARE(remoteFirst->stationAutoAgcNoiseFloorGeneration(), quint64(session * 10 + 2));
        QCOMPARE(remoteFirst->stationAutoAgcNoiseFloorDbm(), -108.5);
        QVERIFY(!remoteFirst->stationAutoAgcNoiseFloorValid());
        first->setStationAutoAgcNoiseFloor(-107.0, true, session * 10 + 2);
        QTRY_VERIFY(remoteFirst->stationAutoAgcNoiseFloorValid());
        QCOMPARE(remoteFirst->stationAutoAgcNoiseFloorDbm(), -107.0);
        QCOMPARE(remoteSecond->stationAutoAgcNoiseFloorDbm(), -91.0);

        stationEnd->clearReceived();
        remoteFirst->setStationAutoAgcNoiseFloor(-55.0, true, 999);
        // An actual operator write is the barrier for the client's write
        // flush. None of the telemetry notifies may join that outbound batch.
        remoteFirst->setFrequency(14080000.0 + session * 100.0);
        QTRY_COMPARE(first->frequency(), remoteFirst->frequency());
        for (const QByteArray& wire : stationEnd->received()) {
            const auto message = decodeOrFail(wire);
            if (message.kind != SessionMessageKind::PropertyWrite) { continue; }
            for (const auto& update : message.updates) {
                QVERIFY(!update.name.startsWith("stationAutoAgc"));
            }
        }
        QCOMPARE(first->stationAutoAgcNoiseFloorDbm(), -107.0);
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QVERIFY(!remoteFirst->stationAutoAgcNoiseFloorValid());
        QVERIFY(!remoteSecond->stationAutoAgcNoiseFloorValid());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void TstStationSession::filterTelemetryFollowsCoreAcrossReconnect()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings settings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto station = makeStationRadioModel(1);
    auto* slice0 = station->slices().at(0);
    auto* slice1 = station->slices().at(1);
    // The allocator/codec normally publishes these coordinates. This test
    // isolates their already-existing mirror from the physical routing tests.
    slice0->setStreamIndex(0);
    slice0->setChainIndex(0);
    slice1->setStreamIndex(1);
    slice1->setChainIndex(1);
    station->alexControllerMutable().setBpfMode(1, AlexController::BpfMode::ForceBypass);
    StationServer server(station.get(), settings, m_securityDir.path());
    RadioModel remote(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&remote, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QVERIFY(!remote.filterChainStateAvailable(1));
    QVERIFY(!station->applyStationFilterValue("rxFilter1Effective", 0));

    for (int session = 1; session <= 2; ++session) {
        auto* stationEnd = new LoopbackTransport(QStringLiteral("filter-station"), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("filter-client"), this);
        stationEnd->linkTo(clientEnd);
        auto snapshotSeen = std::make_shared<bool>(false);
        auto presentedEarly = std::make_shared<bool>(false);
        connect(clientEnd, &SessionTransport::textReceived, clientEnd,
                [snapshotSeen](const QByteArray& wire) {
            if (decodeOrFail(wire).kind == SessionMessageKind::SnapshotComplete) {
                *snapshotSeen = true;
            }
        });
        connect(&remote, &RadioModel::filterStateChanged, clientEnd,
                [&remote, snapshotSeen, presentedEarly] {
            if (!*snapshotSeen && remote.filterChainStateAvailable(1)) {
                *presentedEarly = true;
            }
        });
        client.startSession(clientEnd, server.token());
        QVERIFY(!remote.filterChainStateAvailable(1));
        server.acceptTransport(stationEnd);
        QTRY_COMPARE(completed.count(), session);
        QVERIFY(*snapshotSeen);
        QVERIFY(!*presentedEarly);
        QVERIFY(remote.filterChainStateAvailable(0));
        QVERIFY(remote.filterChainStateAvailable(1));
        QCOMPARE(remote.rxFilter1Reason(), station->rxFilter1Reason());
        QCOMPARE(remote.rxFilter1Effective(), int(AlexController::BpfEffective::Bypass));
        QCOMPARE(remote.sliceChainIndex(slice1->sliceIndex()), 1);
        QVERIFY(!remote.panBypassState({slice0->sliceIndex()}).bypassed);
        QVERIFY(remote.panBypassState({slice1->sliceIndex()}).bypassed);
        QVERIFY(remote.panBypassState({slice1->sliceIndex()}).reason.contains("Filter Policy"));

        // A client-local Alex change must not replace the station's answer.
        remote.alexControllerMutable().setWidebandActive(0, true);
        QVERIFY(!remote.panBypassState({slice0->sliceIndex()}).bypassed);
        station->alexControllerMutable().setWidebandActive(1, true);
        QTRY_COMPARE(remote.rxFilter1Effective(), int(AlexController::BpfEffective::WidebandLocked));
        QVERIFY(remote.panBypassState({slice1->sliceIndex()}).reason.contains("more spectrum"));
        station->alexControllerMutable().setWidebandActive(1, false);
        QTRY_COMPARE(remote.rxFilter1Effective(), int(AlexController::BpfEffective::Bypass));

        stationEnd->clearReceived();
        QVERIFY(remote.applyStationFilterValue("rxFilter1Reason", QStringLiteral("client-only")));
        QVERIFY(!remote.applyStationFilterValue("rxFilter1Effective", 99));
        auto* clientSlice = remote.sliceById(slice0->sliceIndex());
        QVERIFY(clientSlice);
        clientSlice->setFrequency(14075000.0 + session * 100.0);
        QTRY_COMPARE(slice0->frequency(), clientSlice->frequency());
        for (const QByteArray& wire : stationEnd->received()) {
            const auto message = decodeOrFail(wire);
            if (message.kind != SessionMessageKind::PropertyWrite) { continue; }
            for (const auto& update : message.updates) {
                QVERIFY(!update.name.startsWith("rxFilter"));
            }
        }
        QVERIFY(station->rxFilter1Reason() != QStringLiteral("client-only"));
        client.disconnectFromStation(QStringLiteral("operator disconnect"));
        QVERIFY(!remote.filterChainStateAvailable(1));
        QVERIFY(!remote.panBypassState({slice1->sliceIndex()}).bypassed);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
}

void TstStationSession::settingsProxyIsNotReadyBeforeTheSnapshot()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    // A Station-classified key with a distinctive value.
    stationSettings.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50123"));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    // Task 15's handoff, restated as an assertion: several model
    // constructors do contains()-then-seed against Station prefixes, and
    // the ONLY thing stopping them writing ship defaults into the
    // station's store is that writes are dropped while not ready.
    QVERIFY(!proxy.ready());
    QVERIFY(!proxy.hasReceivedSnapshot());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    QVERIFY(proxy.ready());
    QVERIFY(proxy.hasReceivedSnapshot());
    QVERIFY(proxy.setupDialogAllowed() || !proxy.hasNonEmptySnapshot());
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("50001")).toString(),
             QStringLiteral("50123"));

    // ── Fix round 1, Important 2 ─────────────────────────────────────────
    //
    // A client write must reach the station's own store THROUGH THE REAL
    // CLIENT. An earlier version of this slot hand-relayed the frame and
    // attributed the wiring to Task 20, which contradicted both records
    // that assign it here (SettingsProxy.h's own "for a live session (Task
    // 18) to relay over", and the plan's self-review). Nothing in src/
    // consumed outboundWriteRequested or outboundRemoveRequested, so the
    // operator-visible shape was: a remote GUI's Setup change updates the
    // optimistic cache, appears to take, never reaches the station, and is
    // silently reverted by the next snapshot.
    //
    // Hand-relaying here would pass against exactly that broken build,
    // which is why this now goes through proxy.setValue() and nothing else.
    QSignalSpy outbound(&proxy, &SettingsProxy::outboundWriteRequested);
    proxy.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50999"));
    QCOMPARE(outbound.count(), 1);
    QTRY_COMPARE(stationSettings.value(QStringLiteral("TciServerPort")).toString(),
                 QStringLiteral("50999"));

    // The removal half of the same seam.
    QVERIFY(stationSettings.contains(QStringLiteral("TciServerPort")));
    proxy.remove(QStringLiteral("TciServerPort"));
    QTRY_VERIFY(!stationSettings.contains(QStringLiteral("TciServerPort")));
}

// Whole-branch review, Important 4. A settings remove on the station used
// to arrive at the client as "set to empty string", because the daemon's
// change hook reports every mutation the same way: it emits
// outboundValueChanged(key, m_appSettings.value(key), ...) and value() on
// an absent key returns an INVALID QVariant, which StationServer then
// flattened with value.toString() into "". The client cached that, so
// contains() stayed true and value(key, someDefault) returned "" rather
// than the caller's default, while the station said absent.
//
// This is the same invariant an earlier fix round removed inside one
// process (AppSettings::remove's own "actually gone, not just hidden"),
// reintroduced at the wire seam. Reachable operator actions on a remote
// GUI: deleting a mic profile (roughly 91 keys), shrinking the notch list
// (whose own prune exists so a later grow cannot read stale values back,
// which the ghost defeats exactly), and Diagnostics cleanup on a
// non-BPF1 board.
//
// Driven end to end through the real StationServer, the real wire codec
// and the real StationClient, because the flattening happened in the
// relay and no unit-level test of either half could see it.
void TstStationSession::aRemovedStationSettingReachesTheClientAsAbsenceNotAnEmptyString()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50123"));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("fallback")).toString(),
             QStringLiteral("50123"));
    QVERIFY(proxy.contains(QStringLiteral("TciServerPort")));

    // ── Station to client ────────────────────────────────────────────────
    // A removal on the daemon, for any reason of its own.
    stationSettings.remove(QStringLiteral("TciServerPort"));

    QTRY_VERIFY2(!proxy.contains(QStringLiteral("TciServerPort")),
                 "the client still holds a key the station removed");
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("fallback")).toString(),
             QStringLiteral("fallback"));
    QVERIFY2(!proxy.handledKeys().contains(QStringLiteral("TciServerPort")),
             "a removed key must not still be listed");

    // ── Client to station, and the echo back ─────────────────────────────
    // The direction an operator actually triggers. The client removes it
    // locally and immediately, the station catches up, and then the
    // station's own broadcast for that removal comes BACK to this client.
    // That echo is what used to resurrect the key as an empty string,
    // undoing a removal the client had already performed correctly.
    stationSettings.setValue(QStringLiteral("Slice0/Locked"), QStringLiteral("True"));
    QTRY_COMPARE(proxy.value(QStringLiteral("Slice0/Locked"), QString()).toString(),
                 QStringLiteral("True"));

    proxy.remove(QStringLiteral("Slice0/Locked"));
    QVERIFY(!proxy.contains(QStringLiteral("Slice0/Locked")));
    QTRY_VERIFY(!stationSettings.contains(QStringLiteral("Slice0/Locked")));

    // Several flush intervals for the echo to land and misbehave in.
    QTest::qWait(StationClient::kDefaultWriteFlushMs * 6);
    QVERIFY2(!proxy.contains(QStringLiteral("Slice0/Locked")),
             "the station's echo resurrected the key the client just removed");
    QCOMPARE(proxy.value(QStringLiteral("Slice0/Locked"), QStringLiteral("fallback")).toString(),
             QStringLiteral("fallback"));
}

void TstStationSession::schemaSkewIsCaughtByNameComparison()
{
    // Task 18 step 8. MirrorSchema's ordinals are dense and per-class, so
    // two builds declaring different property sets assign DIFFERENT
    // ordinals to the SAME names. Comparing ordinals would therefore
    // report noise; comparing NAMES reports the actual difference, which
    // is why every MirrorUpdate carries its name as well as its ordinal.
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* fakeStationEnd = new LoopbackTransport(QStringLiteral("fake-station"), this);
    auto* realClientEnd = new LoopbackTransport(QStringLiteral("client"), this);
    fakeStationEnd->linkTo(realClientEnd);
    client.startSession(realClientEnd, QStringLiteral("token"));

    fakeStationEnd->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
    fakeStationEnd->sendText(
        SessionMessages::encode(
            SessionMessages::authResult(true, QString(), /*retryable=*/false)));

    StationCapabilities caps;
    caps.stationName = QStringLiteral("Skewed");
    caps.board = HPSDRHW::HermesLite;
    caps.radioConnected = true;
    caps.effectiveMaxSlices = 2;
    caps.boardMaxSlices = 5;
    caps.settingsSchemaVersion = 6;
    fakeStationEnd->sendText(
        SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));

    QTRY_VERIFY(client.mirroredObject(QByteArrayLiteral("radio")) != nullptr);

    // A schema for RadioModel carrying a property this build has never
    // heard of, and omitting one it does have.
    const MirrorSchema& local = MirrorSchema::forObject(&clientModel);
    QVERIFY(local.size() > 1);
    QList<SessionSchemaField> fields;
    quint16 ordinal = 0;
    for (const MirrorProperty& prop : local.properties()) {
        if (prop.name == local.properties().first().name) {
            continue; // omit exactly one real property
        }
        fields.append(SessionSchemaField{ ordinal++, prop.name, prop.kind });
    }
    fields.append(SessionSchemaField{ ordinal, QByteArrayLiteral("aPropertyFromTheFuture"),
                                      MirrorWireKind::Int64 });

    fakeStationEnd->sendText(SessionMessages::encode(
        SessionMessages::schema(QByteArrayLiteral("RadioModel"), fields)));

    QTRY_VERIFY(!client.schemaNamesOnlyOnStation().isEmpty());
    QVERIFY(client.schemaNamesOnlyOnStation().contains(
        QByteArrayLiteral("RadioModel.aPropertyFromTheFuture")));
    QVERIFY(client.schemaNamesOnlyLocal().contains(
        QByteArrayLiteral("RadioModel.") + local.properties().first().name));

    // Skew of the AppSettings schema version is caught at handshake too,
    // by the same kind of name-keyed comparison (both ends read the value
    // stored under the literal key "SettingsSchemaVersion" in their own
    // store), and is reported rather than refused.
    QVERIFY(client.stationSettingsSchemaVersion() == 6);
    QCOMPARE(client.hasSettingsSchemaSkew(),
             client.localSettingsSchemaVersion() != 6);
}

// ── Fix round 1 ──────────────────────────────────────────────────────────

void TstStationSession::reconnectSurvivesTheOldTransportClosing()
{
    // Important 3. attachTransport() used to overwrite m_transport with no
    // disconnect and no deleteLater, and onTransportClosed() took no
    // sender argument, so it could not tell WHICH link had closed.
    // WebSocketTransport::closeLink is asynchronous, so the real sequence
    // -- heartbeat timeout, reconnect from the slot, the old socket's
    // disconnected arrives a moment later -- drove the BRAND NEW session
    // to Disconnected, stopped both timers and called setReady(false).
    // Task 19 is reconnect and walks straight into it.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    // First session, established normally. Held through QPointers because
    // the whole point of the fix is that the client DESTROYS the stale
    // transport, so raw pointers here would dangle.
    auto* firstStation = new LoopbackTransport(QStringLiteral("station-1"), this);
    auto* firstClient = new LoopbackTransport(QStringLiteral("client-1"), this);
    QPointer<LoopbackTransport> staleStation(firstStation);
    QPointer<LoopbackTransport> staleClient(firstClient);
    firstStation->linkTo(firstClient);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(firstClient, server.token());
    server.acceptTransport(firstStation);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(clientModel.isConnected());

    // Reconnect on a fresh pair WITHOUT closing the old one first, which
    // is exactly what a reconnect-from-the-timeout-slot looks like.
    auto* secondStation = new LoopbackTransport(QStringLiteral("station-2"), this);
    auto* secondClient = new LoopbackTransport(QStringLiteral("client-2"), this);
    secondStation->linkTo(secondClient);
    client.startSession(secondClient, server.token());
    server.acceptTransport(secondStation);
    QTRY_COMPARE(completed.count(), 2);
    QVERIFY(clientModel.isConnected());
    QVERIFY(proxy.ready());

    // The stale transport must have been RELEASED, not merely orphaned.
    // attachTransport() disconnects it from this client, closes it and
    // deleteLater()s it; the old code overwrote m_transport and did none
    // of the three, which leaked a transport (and, over a real socket, a
    // QWebSocket still connected to onTransportText) on every reconnect.
    QTRY_VERIFY2(staleClient.isNull(),
                 "the stale transport was orphaned rather than released");

    // And if anything of the old link is still around to make noise, it
    // must not reach the live session. Under the fix there is nothing left
    // to poke, which is itself the assertion above; this covers the case
    // where a late close still arrives from the far end.
    if (!staleStation.isNull()) {
        staleStation->closeLink(QStringLiteral("stale link finally closing"));
    }
    QTest::qWait(StationClient::kDefaultWriteFlushMs * 4);

    QVERIFY2(client.isHandshakeComplete(),
             "a stale transport's close tore down the fresh session");
    QVERIFY2(clientModel.isConnected(),
             "a stale transport's close drove the fresh session to Disconnected");
    QVERIFY2(proxy.ready(),
             "a stale transport's close called setReady(false) on the fresh session");
}

void TstStationSession::heartbeatTimeoutReportsTheSessionAsEnded()
{
    // Important 4, the half that needs no socket. onTransportClosed() used
    // to emit sessionEnded only `if (m_handshakeComplete)`, and
    // disconnectFromStation() cleared that flag before the close handler
    // read it, so the client's own heartbeat timeout reported
    // stationHeartbeatTimeout and then went silent.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    // Keep the station's own heartbeat out of the way; this slot is about
    // the CLIENT's.
    server.setHeartbeatIntervalMs(0);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    client.setHeartbeatIntervalMs(20);
    client.setMaxMissedPongs(2);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy timedOut(&client, &StationClient::stationHeartbeatTimeout);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    // The station goes silent without closing.
    stationEnd->setAnswersPings(false);

    QTRY_COMPARE_WITH_TIMEOUT(timedOut.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 2000);
    QCOMPARE(ended.first().first().toString(), QStringLiteral("heartbeat timeout"));
    QVERIFY(!clientModel.isConnected());
    QVERIFY(!proxy.ready());

    // Exactly once, no matter how many close paths unwind afterwards.
    QTest::qWait(200);
    QCOMPARE(ended.count(), 1);
}

void TstStationSession::tunerPropertiesHydrateWithoutClientCommands()
{
    // R-R3-22/25: the station's whole tuner property bag is telemetry on the
    // client. In particular, isOperate/isBypass/antennaA must not be routed
    // through TunerModel::applyMirroredValue(), because that is the daemon's
    // command hook and calls the native TGXL command slots. isTuning=true is
    // also an ordinary snapshot value here, not permission to start a local
    // tune-carrier cycle.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    TunerModel* const stationTuner = stationModel->tunerModel();
    QVERIFY(stationTuner != nullptr);
    stationModel->tgxlConnection()->injectLineForTesting(QStringLiteral("V1.2.17"));
    stationTuner->applyStatus({
        {QStringLiteral("relayC1"), QStringLiteral("42")},
        {QStringLiteral("relayL"), QStringLiteral("199")},
        {QStringLiteral("relayC2"), QStringLiteral("88")},
        {QStringLiteral("operate"), QStringLiteral("1")},
        {QStringLiteral("bypass"), QStringLiteral("1")},
        {QStringLiteral("tuning"), QStringLiteral("1")},
        {QStringLiteral("antA"), QStringLiteral("2")},
        {QStringLiteral("3way"), QStringLiteral("1")},
        {QStringLiteral("model"), QStringLiteral("TunerGenius")},
        {QStringLiteral("ip"), QStringLiteral("192.0.2.34")},
        {QStringLiteral("fwd"), QStringLiteral("12.5")},
        {QStringLiteral("swr"), QStringLiteral("1.4")},
    });
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy clientTgxlFrames(clientModel.tgxlConnection(),
                                &TgxlConnection::testFrameWrittenForTesting);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    TunerModel* const clientTuner = clientModel.tunerModel();
    QVERIFY(clientTuner != nullptr);
    QCOMPARE(clientTuner->relayC1(), 42);
    QCOMPARE(clientTuner->relayL(), 199);
    QCOMPARE(clientTuner->relayC2(), 88);
    QVERIFY(clientTuner->isOperate());
    QVERIFY(clientTuner->isBypass());
    QVERIFY(clientTuner->isTuning());
    QCOMPARE(clientTuner->antennaA(), 2);
    QVERIFY(clientTuner->hasAntennaSwitch());
    QVERIFY(clientTuner->isPresent());
    QVERIFY(clientTuner->hasDirectConnection());
    QCOMPARE(clientTuner->tgxlIp(), QStringLiteral("192.0.2.34"));
    QCOMPARE(clientTuner->fwdPower(), 12.5f);
    QCOMPARE(clientTuner->swr(), 1.4f);
    QCOMPARE(clientTgxlFrames.count(), 0);

    const QSet<QByteArray> unapplied = client.unappliedProperties();
    for (const QByteArray& property : {
             QByteArrayLiteral("relayC1"), QByteArrayLiteral("relayL"),
             QByteArrayLiteral("relayC2"), QByteArrayLiteral("isOperate"),
             QByteArrayLiteral("isBypass"), QByteArrayLiteral("isTuning"),
             QByteArrayLiteral("antennaA"), QByteArrayLiteral("hasAntennaSwitch"),
             QByteArrayLiteral("isPresent"), QByteArrayLiteral("hasDirectConnection"),
             QByteArrayLiteral("tgxlIp"), QByteArrayLiteral("fwdPower"),
             QByteArrayLiteral("swr")}) {
        QVERIFY2(!unapplied.contains(QByteArrayLiteral("TunerModel.") + property),
                 property.constData());
    }

    // False and zero are state, not "missing" values. Exercise a live delta
    // after the non-default snapshot so the client must actively clear them.
    stationTuner->applyStatus({
        {QStringLiteral("relayC1"), QStringLiteral("0")},
        {QStringLiteral("relayL"), QStringLiteral("0")},
        {QStringLiteral("relayC2"), QStringLiteral("0")},
        {QStringLiteral("operate"), QStringLiteral("0")},
        {QStringLiteral("bypass"), QStringLiteral("0")},
        {QStringLiteral("tuning"), QStringLiteral("0")},
        {QStringLiteral("antA"), QStringLiteral("0")},
        {QStringLiteral("3way"), QStringLiteral("0")},
        {QStringLiteral("fwd"), QStringLiteral("0")},
        {QStringLiteral("swr"), QStringLiteral("0")},
    });
    QTRY_COMPARE(clientTuner->relayC1(), 0);
    QTRY_COMPARE(clientTuner->relayL(), 0);
    QTRY_COMPARE(clientTuner->relayC2(), 0);
    QTRY_VERIFY(!clientTuner->isOperate());
    QTRY_VERIFY(!clientTuner->isBypass());
    QTRY_VERIFY(!clientTuner->isTuning());
    QTRY_COMPARE(clientTuner->antennaA(), 0);
    QTRY_VERIFY(!clientTuner->hasAntennaSwitch());
    QTRY_COMPARE(clientTuner->fwdPower(), 0.0f);
    QTRY_COMPARE(clientTuner->swr(), 0.0f);
    QCOMPARE(clientTgxlFrames.count(), 0);

    // And the property that genuinely DOES land is not swept into the set
    // along with them: SliceModel::signalStrengthDbm reaches its own plain
    // setter through the hook, which is the one pair the client allowlists.
    SliceModel* stationSlice = stationModel->slices().first();
    SliceModel* clientSlice = clientModel.sliceById(stationSlice->sliceIndex());
    QVERIFY(clientSlice != nullptr);
    // No WDSP channel on this station: stop the pump that would write the
    // no-reading value over the reading this setter stands in for (R-R3-13).
    stopSliceMeterPump(stationModel.get());
    stationSlice->setSignalStrengthDbm(-91.0);
    QTRY_COMPARE(clientSlice->signalStrengthDbm(), -91.0);
    QVERIFY(!client.unappliedProperties().contains(
        QByteArrayLiteral("SliceModel.signalStrengthDbm")));
}

void TstStationSession::remoteTgxlStateClearsOnSessionLossRetainingConfiguredEndpoint()
{
    // The state arrives through the actual authenticated snapshot path.  A
    // remote GUI must not retain an admitted device or its live telemetry
    // after that session ends, but its configured endpoint remains a useful
    // draft for the next station connection.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    TunerModel* const stationTuner = stationModel->tunerModel();
    QVERIFY(stationTuner != nullptr);
    TunerModel::StationConnectionState state;
    state.configuredHost = QStringLiteral("tgxl.example.test");
    state.configuredPort = 9010;
    state.phase = TunerModel::ConnectionPhase::Connected;
    state.peerAddress = QStringLiteral("192.0.2.34");
    state.deviceModel = QStringLiteral("TunerGeniusXL");
    state.deviceSerial = QStringLiteral("241288-1");
    state.deviceVersion = QStringLiteral("1.2.17");
    state.deviceNickname = QStringLiteral("Station TGXL");
    stationTuner->setStationConnectionState(state);
    stationTuner->applyStatus({
        {QStringLiteral("relayC1"), QStringLiteral("42")},
        {QStringLiteral("relayL"), QStringLiteral("199")},
        {QStringLiteral("relayC2"), QStringLiteral("88")},
        {QStringLiteral("operate"), QStringLiteral("1")},
        {QStringLiteral("bypass"), QStringLiteral("1")},
        {QStringLiteral("tuning"), QStringLiteral("1")},
        {QStringLiteral("antA"), QStringLiteral("2")},
        {QStringLiteral("3way"), QStringLiteral("1")},
        {QStringLiteral("fwd"), QStringLiteral("12.5")},
        {QStringLiteral("swr"), QStringLiteral("1.4")},
    });
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("tgxl-state-station"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("tgxl-state-client"), this);
    stationEnd->linkTo(clientEnd);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_VERIFY(client.isHandshakeComplete());

    TunerModel* const clientTuner = clientModel.tunerModel();
    QVERIFY(clientTuner != nullptr);
    QTRY_VERIFY(clientTuner->hasDirectConnection());
    QVERIFY(clientTuner->isPresent());
    QCOMPARE(clientTuner->configuredHost(), state.configuredHost);
    QCOMPARE(clientTuner->configuredPort(), int(state.configuredPort));
    QCOMPARE(clientTuner->deviceSerial(), state.deviceSerial);
    QCOMPARE(clientTuner->fwdPower(), 12.5f);
    QCOMPARE(clientTuner->swr(), 1.4f);

    clientEnd->closeLink(QStringLiteral("station link lost"));
    QTRY_VERIFY(!client.isHandshakeComplete());
    QVERIFY(!clientTuner->hasDirectConnection());
    QVERIFY(!clientTuner->isPresent());
    QCOMPARE(clientTuner->connectionPhase(), TunerModel::ConnectionPhase::Disconnected);
    QCOMPARE(clientTuner->configuredHost(), state.configuredHost);
    QCOMPARE(clientTuner->configuredPort(), int(state.configuredPort));
    QVERIFY(clientTuner->connectionError().isEmpty());
    QVERIFY(clientTuner->deviceModel().isEmpty());
    QVERIFY(clientTuner->deviceSerial().isEmpty());
    QVERIFY(clientTuner->deviceVersion().isEmpty());
    QVERIFY(clientTuner->deviceNickname().isEmpty());
    QVERIFY(clientTuner->tgxlIp().isEmpty());
    QCOMPARE(clientTuner->relayC1(), 0);
    QCOMPARE(clientTuner->relayL(), 0);
    QCOMPARE(clientTuner->relayC2(), 0);
    QVERIFY(!clientTuner->isOperate());
    QVERIFY(!clientTuner->isBypass());
    QVERIFY(!clientTuner->isTuning());
    QCOMPARE(clientTuner->antennaA(), 0);
    QVERIFY(!clientTuner->hasAntennaSwitch());
    QCOMPARE(clientTuner->fwdPower(), 0.0f);
    QCOMPARE(clientTuner->swr(), 1.0f);
}

void TstStationSession::receiveOnlyStationBlocksRemoteBandRecall()
{
    // R-R3-25 must hold through the real authenticated property-write path,
    // not just for a direct unit-test call on the station slice. The daemon's
    // RadioModel is Role::Local because it owns the hardware, so the durable
    // receive-only station policy is what distinguishes it from desktop-local
    // direct mode.
    AppSettings& settings = AppSettings::instance();
    settings.setValue(QStringLiteral("TGXL_AutoTuneMemoryRecall"),
                      QStringLiteral("True"));

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    SliceModel* const stationSlice = stationModel->slices().first();
    stationSlice->setFrequency(14200000.0);
    stationModel->tuneMemoryStore()->store(
        TuneMemory{1, Band::Band40m, 4, 5, 6, 1});
    stationModel->tgxlConnection()->injectLineForTesting(QStringLiteral("V1.2.17"));
    QSignalSpy tgxlFrames(stationModel->tgxlConnection(),
                         &TgxlConnection::testFrameWrittenForTesting);

    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(stationModel->receiveOnlyStationPolicy(),
             "a receive-only StationServer must protect a standalone local-role model");

    MoxController* const stationMox = stationModel->moxController();
    QVERIFY(stationMox != nullptr);
    QSignalSpy moxRefused(stationMox, &MoxController::moxRejected);
    QSignalSpy tuneRefused(stationModel.get(), &RadioModel::tuneRefused);

    stationMox->setMox(true);
    QCOMPARE(moxRefused.count(), 1);
    QVERIFY(!stationMox->isMox());
    QVERIFY(stationMox->state() == MoxState::Rx);

    stationModel->setTune(true);
    QCOMPARE(tuneRefused.count(), 1);
    QVERIFY(!stationModel->isTune());
    QVERIFY(!stationMox->isManualMox());

    stationModel->startTgxlAutotune(/*fromHardware=*/false);
    QCOMPARE(tuneRefused.count(), 2);
    QVERIFY(!stationModel->isTune());
    QVERIFY(!stationMox->isMox());
    QCOMPARE(tgxlFrames.count(), 0);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    SliceModel* const clientSlice = clientModel.sliceById(stationSlice->sliceIndex());
    QVERIFY(clientSlice != nullptr);
    clientSlice->setFrequency(7100000.0);

    QTRY_COMPARE(stationSlice->frequency(), 7100000.0);
    QCOMPARE(tgxlFrames.count(), 0);

    // Session teardown must never lift the daemon's persistent policy.
    clientEnd->closeLink(QStringLiteral("test session complete"));
    QTRY_VERIFY(!clientModel.isConnected());
    QVERIFY(stationModel->receiveOnlyStationPolicy());
    stationMox->setMox(true);
    QCOMPARE(moxRefused.count(), 2);
    QVERIFY(!stationMox->isMox());

    // Restore the singleton keys this accessory fixture owns. Each test
    // process has an isolated profile, but leaving state behind inside the
    // same binary would make later slots order-dependent.
    stationModel->tuneMemoryStore()->clear(1, Band::Band40m);
    settings.remove(QStringLiteral("TGXL_AutoTuneMemoryRecall"));
}

void TstStationSession::receiveOnlyStationRefusesTransmitPropertyWrites()
{
    // R-R3-25 applies at the authenticated StationServer boundary too.
    // TransmitModel is mirrored bidirectionally for later phases, but an R3
    // receive-only server must reject those writes and return authoritative
    // accepted-state results rather than adopting the client's optimistic state.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY(stationModel->receiveOnlyStationPolicy());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    TransmitModel& stationTx = stationModel->transmitModel();
    TransmitModel& clientTx = clientModel.transmitModel();
    const int settledPower = stationTx.power();
    const int requestedPower = settledPower == 17 ? 18 : 17;
    QVERIFY(!stationTx.isMox());
    QVERIFY(!stationTx.isTune());
    QCOMPARE(clientTx.power(), settledPower);

    stationEnd->clearReceived();
    clientEnd->clearReceived();
    clientTx.setMox(true);
    clientTx.setTune(true);
    clientTx.setPower(requestedPower);

    QTRY_VERIFY(stationEnd->receivedKinds().contains(
        QByteArrayLiteral("property.write")));

    // These are model-state assertions. This fixture does not claim that a
    // radio socket emitted RF in the uncorrected implementation.
    QVERIFY(!stationTx.isMox());
    QVERIFY(!stationTx.isTune());
    QCOMPARE(stationTx.power(), settledPower);

    QTRY_VERIFY(clientEnd->receivedKinds().contains(QByteArrayLiteral("property.result")));
    QTRY_VERIFY(!clientTx.isMox());
    QTRY_VERIFY(!clientTx.isTune());
    QTRY_COMPARE(clientTx.power(), settledPower);
}

void TstStationSession::nr3CannotRunIsRefusedOnTheCoreAndInTheWindow()
{
    // Fix wave I3 (R-R3-21). A Core with no usable NR3 model file cannot run
    // NR3 (WDSP would pass the audio through unchanged). It says so: turning
    // NR3 on is refused with the plain sentence on the Core, and in a remote
    // window, which learns it through the mirrored nr3Runnable flag and
    // refuses without asking the Core. Other reducers still turn on. Once
    // the Core has a model again, the window turns NR3 on.
    DspAssetService::setBundledNr3ModelPathsForTest([](const QString&) { return QString(); });
    const auto restorePaths = qScopeGuard([] {
        DspAssetService::setBundledNr3ModelPathsForTest({});
    });
    const QString none =
        QStringLiteral("No NR3 model file was found on this Core, so NR3 cannot run.");
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    DspAssetService* core = stationModel->dspAssets();
    QVERIFY(!core->nr3Runnable());
    QCOMPARE(core->nr3ModelStatus(), none);

    SliceModel* coreSlice = stationModel->slices().constFirst();
    coreSlice->setActiveNr(NrSlot::Off);
    QSignalSpy coreRefused(coreSlice, &SliceModel::nrSelectionRefused);
    coreSlice->setActiveNr(NrSlot::NR3);
    QCOMPARE(coreSlice->activeNr(), NrSlot::Off);
    QCOMPARE(coreRefused.count(), 1);
    QCOMPARE(coreRefused.constFirst().at(0).toString(), none);
    QCOMPARE(coreSlice->nnrLastError(), none);

    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    DspAssetService* window = clientModel.dspAssets();
    QTRY_VERIFY(!window->nr3Runnable());
    QTRY_COMPARE(window->nr3ModelStatus(), none);
    QTRY_VERIFY(!clientModel.slices().isEmpty());
    SliceModel* windowSlice = clientModel.slices().constFirst();
    QSignalSpy windowRefused(windowSlice, &SliceModel::nrSelectionRefused);
    windowSlice->setActiveNr(NrSlot::NR3);
    QCOMPARE(windowSlice->activeNr(), NrSlot::Off);
    QCOMPARE(windowRefused.count(), 1);
    QCOMPARE(windowRefused.constFirst().at(0).toString(), none);
    QTest::qWait(100);
    QCOMPARE(coreSlice->activeNr(), NrSlot::Off);

    // Another reducer still turns on from the window.
    windowSlice->setActiveNr(NrSlot::NR2);
    QCOMPARE(windowSlice->activeNr(), NrSlot::NR2);
    QTRY_COMPARE(coreSlice->activeNr(), NrSlot::NR2);

    // The model comes back: the Core says so and the window turns NR3 on.
    DspAssetService::setBundledNr3ModelPathsForTest({});
    QVERIFY(core->applyNr3Model());
    QVERIFY(core->nr3Runnable());
    QTRY_VERIFY(window->nr3Runnable());
    windowSlice->setActiveNr(NrSlot::NR3);
    QCOMPARE(windowSlice->activeNr(), NrSlot::NR3);
    QTRY_COMPARE(coreSlice->activeNr(), NrSlot::NR3);
    QCOMPARE(windowRefused.count(), 1);

    windowSlice->setActiveNr(NrSlot::Off);
    QTRY_COMPARE(coreSlice->activeNr(), NrSlot::Off);
    AppSettings::instance().remove(QStringLiteral("DspAssets/Nr3Model"));
}

void TstStationSession::nr3ModelChoiceLoadsOnceOnTheCoreAndMirrors()
{
    // R-R3-21. A remote window chooses the Core's NR3 model with the
    // dspAssets.selectNr3Model command; the Core loads that file once, live,
    // and the choice and its plain-language status reach the window.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    QStringList loaded;
    stationModel->dspAssets()->setNr3ModelLoader(
        [&loaded](const QString& path) { loaded.append(path); });
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QCOMPARE(server.buildCapabilities().dspAssetVersion, 2);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    DspAssetService* remote = clientModel.dspAssets();
    QTRY_VERIFY(remote->nr3ModelsSupported());
    QTRY_COMPARE(remote->nr3ModelStatus(), stationModel->dspAssets()->nr3ModelStatus());

    const QString small = QString::fromLatin1(DspAssetService::kNr3BundledSmallId);
    QSignalSpy answered(remote, &DspAssetService::requestCompleted);
    const quint32 request = remote->request("dspAssets.selectNr3Model",
                                            {{QStringLiteral("id"), small}});
    QVERIFY(request != 0);
    QTRY_COMPARE(answered.count(), 1);
    QCOMPARE(answered.first().at(0).toUInt(), request);
    QVERIFY2(answered.first().at(1).toBool(), qPrintable(answered.first().at(2).toString()));

    QCOMPARE(loaded, QStringList{DspAssetService::bundledNr3ModelPath(small)});
    QCOMPARE(stationModel->dspAssets()->nr3ModelAsset(), small);
    QTRY_COMPARE(remote->nr3ModelAsset(), small);
    QTRY_COMPARE(remote->nr3ModelStatus(), QStringLiteral("Using the bundled small model."));
    // The window never loads a model itself.
    QVERIFY(!remote->applyNr3Model());
    QCOMPARE(loaded.size(), 1);

    AppSettings::instance().remove(QStringLiteral("DspAssets/Nr3Model"));
}

void TstStationSession::olderCoreLeavesTheNr3ModelUnchangeable()
{
    // R-R3-21. A Core advertising dspAssetVersion 1 has no NR3 models: the
    // window reports it cannot change the model and sends no NR3 request.
    // A version 2 Core on the same protocol minor enables it.
    for (const int version : {1, 2}) {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto* station = new LoopbackTransport(QStringLiteral("nr3-station"), this);
        auto* peer = new LoopbackTransport(QStringLiteral("nr3-client"), this);
        station->linkTo(peer);
        client.startSession(peer, QStringLiteral("test-token"));
        station->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
        station->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
        StationCapabilities caps;
        caps.propertyResultVersion = 1;
        caps.dspAssetVersion = version;
        station->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
        station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
        QTRY_VERIFY(client.isHandshakeComplete());

        DspAssetService* assets = remote.dspAssets();
        QCOMPARE(assets->nr3ModelsSupported(), version >= 2);
        QCOMPARE(client.remoteNr3ModelsAvailable(), version >= 2);
        const quint32 select = assets->request(
            "dspAssets.selectNr3Model",
            {{QStringLiteral("id"), QString::fromLatin1(DspAssetService::kNr3BundledLargeId)}});
        const quint32 upload = assets->request("dspAssets.beginImport", {
            {QStringLiteral("kind"), 2}, {QStringLiteral("label"), QStringLiteral("x")},
            {QStringLiteral("size"), qint64(1)}, {QStringLiteral("hash"), QString(64, QLatin1Char('a'))},
            {QStringLiteral("radioIdentity"), QString()}});
        QCOMPARE(select != 0, version >= 2);
        QCOMPARE(upload != 0, version >= 2);
        // The older Core's NNR requests are unaffected.
        QVERIFY(assets->request("dspAssets.list", {}) != 0);
    }
}

void TstStationSession::olderAppNr3ModelPathWriteIsRefused()
{
    // R-R3-21. An older app still writes Nr3ModelPath (a file on the app's
    // own computer). The Core owns its NR3 models now, so the write is
    // refused with a plain reason and the Core's value stays put.
    const QString key = QStringLiteral("Nr3ModelPath");
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(proxy.ready());

    QSignalSpy rejected(&proxy, &SettingsProxy::valueRejected);
    QSignalSpy toast(&clientModel, &RadioModel::sliceAddRejected);
    proxy.setValue(key, QStringLiteral("C:/Users/op/model.bin"));
    QTRY_COMPARE(rejected.count(), 1);
    QCOMPARE(rejected.first().at(0).toString(), key);
    QVERIFY(!stationSettings.contains(key));
    QCOMPARE(toast.count(), 1);
    QCOMPARE(toast.first().at(0).toString(),
             QStringLiteral("This Core keeps its own NR3 models. Update this app to choose one."));

    proxy.remove(key);
    QTRY_COMPARE(rejected.count(), 2);
    QCOMPARE(toast.count(), 2);
    QCOMPARE(toast.last().at(0).toString(),
             QStringLiteral("This Core keeps its own NR3 models. Update this app to choose one."));
}

namespace {

// Install a client SettingsProxy as AppSettings' remote backend for one
// scope, the way a remote window runs. Only around the window's own edit:
// the Core model shares this process's AppSettings in these tests.
struct RemoteSettingsScope {
    explicit RemoteSettingsScope(SettingsProxy* proxy)
    {
        AppSettings::instance().setRemoteBackend(proxy);
    }
    ~RemoteSettingsScope() { AppSettings::instance().setRemoteBackend(nullptr); }
};

void removeLocalNotchKeys()
{
    auto& s = AppSettings::instance();
    const QStringList keys = s.allKeys();
    for (const QString& key : keys) {
        if (key.startsWith(QStringLiteral("Notch"))) {
            s.remove(key);
        }
    }
}

void writeCoreNotchList(AppSettings& settings, const QList<Notch>& notches)
{
    settings.setValue(QStringLiteral("NotchCount"), QString::number(notches.size()));
    for (int i = 0; i < notches.size(); ++i) {
        settings.setValue(QStringLiteral("Notch%1Center").arg(i),
                          QString::number(notches.at(i).centerHz, 'f', 6));
        settings.setValue(QStringLiteral("Notch%1Width").arg(i),
                          QString::number(notches.at(i).widthHz, 'f', 6));
        settings.setValue(QStringLiteral("Notch%1Active").arg(i), QStringLiteral("True"));
    }
}


// A Core and a remote window joined over the loopback, for the notch tests.
// Members are destroyed in reverse order: the window side first.
struct NotchSession {
    QTemporaryDir dir;
    std::unique_ptr<AppSettings> stationSettings;
    std::unique_ptr<RadioModel> core;
    std::unique_ptr<StationServer> server;
    std::unique_ptr<RadioModel> window;
    std::unique_ptr<SettingsProxy> proxy;
    std::unique_ptr<StationClient> client;
    LoopbackTransport* stationEnd = nullptr;
    LoopbackTransport* clientEnd = nullptr;
};

// Build the Core first (so a test can seed its list), then join a window.
void joinNotchWindow(NotchSession& s, QObject* owner, const QString& securityDir)
{
    s.server = std::make_unique<StationServer>(s.core.get(), *s.stationSettings, securityDir);
    s.window = std::make_unique<RadioModel>(RadioModel::Role::Remote);
    s.proxy = std::make_unique<SettingsProxy>();
    s.client = std::make_unique<StationClient>(s.window.get(), s.proxy.get());
    s.stationEnd = new LoopbackTransport(QStringLiteral("station-end"), owner);
    s.clientEnd = new LoopbackTransport(QStringLiteral("client-end"), owner);
    s.stationEnd->linkTo(s.clientEnd);
    QSignalSpy completed(s.client.get(), &StationClient::handshakeComplete);
    s.client->startSession(s.clientEnd, s.server->token());
    s.server->acceptTransport(s.stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(s.client->remoteNotchControlAvailable());
    QVERIFY(s.window->notchModel()->mirrorMode());
}

void prepareNotchCore(NotchSession& s)
{
    QVERIFY(s.dir.isValid());
    s.stationSettings = std::make_unique<AppSettings>(
        s.dir.filePath(QStringLiteral("NereusSDR.settings")));
    s.core = makeStationRadioModel(0);
}

bool sameNotchList(const NotchModel* a, const NotchModel* b)
{
    if (a->notches().size() != b->notches().size()) {
        return false;
    }
    for (int i = 0; i < a->notches().size(); ++i) {
        const Notch& x = a->notches().at(i);
        const Notch& y = b->notches().at(i);
        if (x.id != y.id || x.centerHz != y.centerHz || x.widthHz != y.widthHz
            || x.active != y.active) {
            return false;
        }
    }
    return true;
}

bool hasNotchSettings(const AppSettings& settings)
{
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        if (key.startsWith(QStringLiteral("Notch"))) {
            return true;
        }
    }
    return false;
}

} // namespace

void TstStationSession::remoteNotchEditKeepsTheCoresWholeList()
{
    // R-R3-21 / R-R3-09, red first. The Core holds two notches. A remote
    // window starts with an empty notch list (it read its settings before
    // the Core's arrived), and its first notch add used to write the whole
    // Notch* set from that empty list: NotchCount 1 replaced the Core's two
    // saved notches, and the Core's live list never saw the add at all.
    // The Core now owns the list: the window's add reaches the Core's list
    // and every receiver, the Core's saved list is not rewritten by the
    // window, and the window shows the Core's list with the Core's ids.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    removeLocalNotchKeys();

    auto stationModel = makeStationRadioModel(0);
    NotchModel* core = stationModel->notchModel();
    QVERIFY(core->addNotch(7040000.0, 200.0) > 0);
    QVERIFY(core->addNotch(7050000.0, 300.0) > 0);
    QCOMPARE(core->notches().size(), 2);
    writeCoreNotchList(stationSettings, core->notches());
    removeLocalNotchKeys();

    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(proxy.ready());

    SliceModel* remoteSlice = clientModel.sliceById(0);
    QVERIFY(remoteSlice != nullptr);
    {
        RemoteSettingsScope scope(&proxy);
        clientModel.addNotchForSlice(remoteSlice, 7060000.0, 250.0);
    }

    // The Core's live list gains the notch; nothing replaced it. The
    // window's settings, when it wrote any, have landed by the time the
    // window has heard back from the Core.
    QTRY_VERIFY(stationEnd->receivedKinds().contains(QByteArrayLiteral("command.invoke"))
                || stationSettings.value(QStringLiteral("NotchCount")).toString()
                       != QStringLiteral("2"));
    QCOMPARE(stationSettings.value(QStringLiteral("NotchCount")).toString(),
             QStringLiteral("2"));
    QTRY_COMPARE(core->notches().size(), 3);
    QCOMPARE(core->notches().at(0).centerHz, 7040000.0);
    QCOMPARE(core->notches().at(1).centerHz, 7050000.0);
    QCOMPARE(core->notches().at(2).centerHz, 7060000.0);
    // The window wrote no notch settings over the Core's saved list.
    QCOMPARE(stationSettings.value(QStringLiteral("NotchCount")).toString(),
             QStringLiteral("2"));
    QCOMPARE(stationSettings.value(QStringLiteral("Notch1Center")).toDouble(), 7050000.0);
    // The window shows the Core's whole list, under the Core's ids.
    NotchModel* remote = clientModel.notchModel();
    QTRY_COMPARE(remote->notches().size(), 3);
    for (int i = 0; i < 3; ++i) {
        QCOMPARE(remote->notches().at(i).id, core->notches().at(i).id);
        QCOMPARE(remote->notches().at(i).centerHz, core->notches().at(i).centerHz);
        QCOMPARE(remote->notches().at(i).widthHz, core->notches().at(i).widthHz);
    }
    removeLocalNotchKeys();
}

void TstStationSession::remoteNotchMoveToggleAndDeleteReachTheCore()
{
    // R-R3-21 / R-R3-09. Every window edit is one request on one notch,
    // applied by the Core's own NotchModel; the window shows the result
    // under the Core's ids and writes no notch settings of its own.
    removeLocalNotchKeys();
    NotchSession s;
    prepareNotchCore(s);
    if (QTest::currentTestFailed()) { return; }
    NotchModel* core = s.core->notchModel();
    const int first = core->addNotch(7040000.0, 200.0);
    QVERIFY(first > 0);
    joinNotchWindow(s, this, m_securityDir.path());
    if (QTest::currentTestFailed()) { return; }
    NotchModel* remote = s.window->notchModel();
    QTRY_COMPARE(remote->notches().size(), 1);
    QCOMPARE(remote->notches().first().id, first);

    QVERIFY(remote->setCenter(first, 7041000.0));
    QTRY_COMPARE(core->notchById(first)->centerHz, 7041000.0);
    QVERIFY(remote->setWidth(first, 400.0));
    QTRY_COMPARE(core->notchById(first)->widthHz, 400.0);
    QVERIFY(remote->setActive(first, false));
    QTRY_VERIFY(!core->notchById(first)->active);
    QTRY_VERIFY(sameNotchList(core, remote));

    s.window->addNotchForSlice(s.window->sliceById(0), 7060000.0, 250.0);
    QTRY_COMPARE(core->notches().size(), 2);
    const int second = core->notches().at(1).id;
    QTRY_COMPARE(remote->notches().size(), 2);
    QCOMPARE(remote->notches().at(1).id, second);

    QVERIFY(remote->removeNotch(first));
    QTRY_COMPARE(core->notches().size(), 1);
    QCOMPARE(core->notches().first().id, second);
    QTRY_VERIFY(sameNotchList(core, remote));
    QTRY_COMPARE(remote->revision(), core->revision());

    // Only the Core wrote notch settings, and not through the window.
    QVERIFY(!hasNotchSettings(*s.stationSettings));
    removeLocalNotchKeys();
}

void TstStationSession::remoteNotchRefusalsAreInPlainWords()
{
    // R-R3-21 / R-R3-09. A full list and a notch another window already
    // removed are refused with plain reasons, and the window goes back to
    // the Core's list.
    removeLocalNotchKeys();
    auto& local = AppSettings::instance();
    local.setValue(QStringLiteral("NotchCount"), QString::number(NotchModel::kMaxNotches));
    for (int i = 0; i < NotchModel::kMaxNotches; ++i) {
        local.setValue(QStringLiteral("Notch%1Center").arg(i),
                       QString::number(7000000.0 + i * 100.0, 'f', 6));
        local.setValue(QStringLiteral("Notch%1Width").arg(i), QStringLiteral("50"));
        local.setValue(QStringLiteral("Notch%1Active").arg(i), QStringLiteral("True"));
    }
    NotchSession s;
    prepareNotchCore(s);
    if (QTest::currentTestFailed()) { return; }
    removeLocalNotchKeys();
    NotchModel* core = s.core->notchModel();
    QCOMPARE(core->notches().size(), NotchModel::kMaxNotches);
    joinNotchWindow(s, this, m_securityDir.path());
    if (QTest::currentTestFailed()) { return; }
    NotchModel* remote = s.window->notchModel();
    // The whole list travels, ids and all.
    QTRY_COMPARE(remote->notches().size(), NotchModel::kMaxNotches);
    QVERIFY(sameNotchList(core, remote));

    QSignalSpy addRefused(remote, &NotchModel::notchAddRejected);
    s.window->addNotchForSlice(s.window->sliceById(0), 14200000.0, 200.0);
    QTRY_COMPARE(addRefused.count(), 1);
    QCOMPARE(addRefused.first().at(0).toString(),
             QStringLiteral("Maximum of 1024 notches reached"));
    QCOMPARE(core->notches().size(), NotchModel::kMaxNotches);

    // Another window (here the Core itself) removes a notch this window
    // still shows; this window's toggle of it is refused and undone.
    const int gone = core->notches().at(5).id;
    QVERIFY(core->removeNotch(gone));
    QSignalSpy refused(remote, &NotchModel::notchRequestRefused);
    QVERIFY(remote->setActive(gone, false));
    QTRY_COMPARE(refused.count(), 1);
    QCOMPARE(refused.first().at(0).toString(),
             QStringLiteral("That notch is no longer on this Core."));
    QTRY_VERIFY(remote->notchById(gone) == nullptr);
    QTRY_VERIFY(sameNotchList(core, remote));

    // A malformed or stale request sent straight to the Core.
    QSignalSpy results(s.client.get(), &StationClient::commandResult);
    const quint32 stale = s.client->invokeCommand("notch.delete",
        {{0, "id", MirrorWireKind::Int64, qlonglong(gone)}});
    QVERIFY(stale != 0);
    const quint32 malformed = s.client->invokeCommand("notch.delete",
        {{0, "id", MirrorWireKind::Float64, double(gone)}});
    QVERIFY(malformed != 0);
    QTRY_COMPARE(results.count(), 2);
    for (const QList<QVariant>& args : std::as_const(results)) {
        QVERIFY(!args.at(1).toBool());
        QCOMPARE(args.at(2).toString(), args.at(0).toUInt() == stale
            ? QStringLiteral("That notch is no longer on this Core.")
            : QStringLiteral("This notch change is not one this Core understands."));
    }
    removeLocalNotchKeys();
}

void TstStationSession::coreNotchChangesReachTheWindow()
{
    // R-R3-21 / R-R3-09. A change made on the Core (a TCI rx_nf_enable, a
    // notch placed at the Core) reaches the window; the window's two
    // switches reach the Core.
    removeLocalNotchKeys();
    NotchSession s;
    prepareNotchCore(s);
    if (QTest::currentTestFailed()) { return; }
    joinNotchWindow(s, this, m_securityDir.path());
    if (QTest::currentTestFailed()) { return; }
    NotchModel* core = s.core->notchModel();
    NotchModel* remote = s.window->notchModel();
    QVERIFY(!core->globalEnabled());

    s.core->setRxNf(0, true);   // the TCI rx_nf_enable path
    QTRY_VERIFY(remote->globalEnabled());

    const int placed = core->addNotch(14074000.0, 300.0);
    QVERIFY(placed > 0);
    QTRY_COMPARE(remote->notches().size(), 1);
    QCOMPARE(remote->notches().first().id, placed);
    QCOMPARE(remote->notches().first().widthHz, 300.0);

    remote->setGlobalEnabled(false);
    QTRY_VERIFY(!core->globalEnabled());
    QVERIFY(core->autoIncrease());
    remote->setAutoIncrease(false);
    QTRY_VERIFY(!core->autoIncrease());
    QVERIFY(!hasNotchSettings(*s.stationSettings));
    removeLocalNotchKeys();
}

void TstStationSession::appNotchSettingsWritesAreRefused()
{
    // R-R3-21 / R-R3-09. An app's raw Notch* write or remove (what an older
    // app sends on every notch edit) is refused with a plain reason, so it
    // can no longer replace the Core's list. NotchVisualEnabled is a
    // display preference and still lands.
    removeLocalNotchKeys();
    NotchSession s;
    prepareNotchCore(s);
    if (QTest::currentTestFailed()) { return; }
    s.stationSettings->setValue(QStringLiteral("NotchCount"), QStringLiteral("2"));
    joinNotchWindow(s, this, m_securityDir.path());
    if (QTest::currentTestFailed()) { return; }
    QVERIFY(s.proxy->ready());

    const QString reason =
        QStringLiteral("This Core keeps its own notch list. Update this app to change notches.");
    QSignalSpy rejected(s.proxy.get(), &SettingsProxy::valueRejected);
    QSignalSpy toast(s.window.get(), &RadioModel::sliceAddRejected);
    s.proxy->setValue(QStringLiteral("NotchCount"), QStringLiteral("0"));
    QTRY_COMPARE(rejected.count(), 1);
    QCOMPARE(toast.last().at(0).toString(), reason);
    QCOMPARE(s.stationSettings->value(QStringLiteral("NotchCount")).toString(),
             QStringLiteral("2"));
    s.proxy->setValue(QStringLiteral("NotchGlobalEnabled"), QStringLiteral("True"));
    QTRY_COMPARE(rejected.count(), 2);
    QVERIFY(!s.stationSettings->contains(QStringLiteral("NotchGlobalEnabled")));
    s.proxy->remove(QStringLiteral("NotchCount"));
    QTRY_COMPARE(rejected.count(), 3);
    QCOMPARE(toast.last().at(0).toString(), reason);
    QCOMPARE(s.stationSettings->value(QStringLiteral("NotchCount")).toString(),
             QStringLiteral("2"));

    s.proxy->setValue(QStringLiteral("NotchVisualEnabled"), QStringLiteral("True"));
    QTRY_COMPARE(s.stationSettings->value(QStringLiteral("NotchVisualEnabled")).toString(),
                 QStringLiteral("True"));
    QCOMPARE(rejected.count(), 3);
    removeLocalNotchKeys();
}

void TstStationSession::olderCoreKeepsTodaysNotchBehaviour()
{
    // R-R3-21 / R-R3-09. Against a Core without notchControlVersion the
    // window keeps today's notches exactly: no mirror mode, a local add, no
    // notch request. With the version, the add becomes a request.
    for (const int version : {0, 1}) {
        removeLocalNotchKeys();
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto* station = new LoopbackTransport(QStringLiteral("notch-station"), this);
        auto* peer = new LoopbackTransport(QStringLiteral("notch-client"), this);
        station->linkTo(peer);
        client.startSession(peer, QStringLiteral("test-token"));
        station->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("station"))));
        station->sendText(SessionMessages::encode(SessionMessages::authResult(true, {}, false)));
        StationCapabilities caps;
        caps.propertyResultVersion = 1;
        caps.notchControlVersion = version;
        station->sendText(SessionMessages::encode(SessionMessages::capabilities(caps.toUpdates())));
        station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
        QTRY_VERIFY(client.isHandshakeComplete());

        NotchModel* notches = remote.notchModel();
        QCOMPARE(notches->mirrorMode(), version >= 1);
        QCOMPARE(client.remoteNotchControlAvailable(), version >= 1);
        station->clearReceived();
        const int added = remote.addNotchForSlice(nullptr, 7040000.0, 200.0);
        if (version == 0) {
            QVERIFY(added > 0);
            QCOMPARE(notches->notches().size(), 1);
            QCOMPARE(AppSettings::instance().value(QStringLiteral("NotchCount")).toString(),
                     QStringLiteral("1"));
            QTest::qWait(50);
            QVERIFY(!station->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
        } else {
            QCOMPARE(added, -1);
            QCOMPARE(notches->notches().size(), 0);
            QTRY_VERIFY(station->receivedKinds().contains(QByteArrayLiteral("command.invoke")));
            QVERIFY(!AppSettings::instance().contains(QStringLiteral("NotchCount")));
        }
    }
    removeLocalNotchKeys();
}

void TstStationSession::olderAppIgnoresTheNotchesObjectGolden()
{
    // R-R3-21 / R-R3-09, golden. An app that does not know
    // notchControlVersion (modelled by removing that entry: an older app
    // ignores it) is handed this Core's real burst plus a later notch
    // change. It ends in exactly the state it reaches from the same burst
    // with every `notches` message removed, which is what an older Core
    // sends: the new object changes nothing for it.
    removeLocalNotchKeys();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings stationSettings(dir.filePath(QStringLiteral("NereusSDR.settings")));
    auto core = makeStationRadioModel(0);
    QVERIFY(core->notchModel()->addNotch(7040000.0, 200.0) > 0);
    removeLocalNotchKeys();
    StationServer server(core.get(), stationSettings, m_securityDir.path());
    auto* station = new LoopbackTransport(QStringLiteral("golden-station"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("golden-peer"), this);
    station->linkTo(peer);
    server.acceptTransport(station);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 6, QStringLiteral("older-app"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(peer->receivedKinds().contains(QByteArrayLiteral("snapshot.complete")));
    QVERIFY(core->notchModel()->addNotch(7050000.0, 200.0) > 0);
    const auto hasNotchDelta = [peer]() {
        for (const QByteArray& wire : peer->received()) {
            const SessionMessage m = decodeOrFail(wire);
            if (m.kind == SessionMessageKind::Delta && m.objectKey == "notches") {
                return true;
            }
        }
        return false;
    };
    QTRY_VERIFY(hasNotchDelta());
    const QList<QByteArray> burst = peer->received();

    int notchMessages = 0;
    const auto replay = [&](bool keepNotches) {
        QList<QByteArray> out;
        for (const QByteArray& wire : burst) {
            SessionMessage m = decodeOrFail(wire);
            if (m.kind == SessionMessageKind::Capabilities) {
                QList<MirrorUpdate> kept;
                for (const MirrorUpdate& u : std::as_const(m.updates)) {
                    if (u.name != "notchControlVersion") { kept.append(u); }
                }
                m.updates = kept;
                out.append(SessionMessages::encode(m));
                continue;
            }
            const bool aboutNotches = m.objectKey == "notches"
                || (m.kind == SessionMessageKind::Schema && m.className == "NotchModel");
            if (aboutNotches) {
                if (keepNotches) {
                    ++notchMessages;
                    out.append(wire);
                    // Fix wave minor 6: more notch changes cost an older app
                    // no more lines. Each delta is sent three times.
                    if (m.kind == SessionMessageKind::Delta) {
                        out.append(wire);
                        out.append(wire);
                    }
                }
                continue;
            }
            out.append(wire);
        }
        return out;
    };

    struct Window {
        RadioModel model{RadioModel::Role::Remote};
        SettingsProxy proxy;
        std::unique_ptr<StationClient> client;
        LoopbackTransport* station = nullptr;
    };
    const auto run = [&](Window& w, const QList<QByteArray>& messages) {
        w.client = std::make_unique<StationClient>(&w.model, &w.proxy);
        w.station = new LoopbackTransport(QStringLiteral("replay-station"), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("replay-client"), this);
        w.station->linkTo(clientEnd);
        w.client->startSession(clientEnd, server.token());
        for (const QByteArray& wire : messages) {
            w.station->sendText(wire);
        }
    };
    // The Core's own notch settings share this process's store; a window
    // must start from the empty store a fresh remote window has.
    removeLocalNotchKeys();
    Window withNotches;
    Window without;
    // The only two lines the new object costs an older app: the tolerance
    // StationClient already has for any object it does not hold.
    QTest::ignoreMessage(QtWarningMsg,
        "Station named an object this client cannot construct: \"notches\" \"NotchModel\"");
    QTest::ignoreMessage(QtWarningMsg,
        "Delta for an object this client does not hold: \"notches\"");
    // Once per object: any further line for the same object fails the test.
    QTest::failOnWarning(QRegularExpression(
        QStringLiteral("^Delta for an object this client does not hold")));
    run(withNotches, replay(true));
    run(without, replay(false));
    QVERIFY(notchMessages >= 3);   // schema, object.create, delta
    QTRY_VERIFY(withNotches.client->isHandshakeComplete());
    QTRY_VERIFY(without.client->isHandshakeComplete());
    QTest::qWait(50);

    NotchModel* a = withNotches.model.notchModel();
    NotchModel* b = without.model.notchModel();
    QVERIFY(!a->mirrorMode());
    QVERIFY(!b->mirrorMode());
    QVERIFY(sameNotchList(a, b));
    QCOMPARE(a->notches().size(), 0);
    QCOMPARE(a->globalEnabled(), b->globalEnabled());
    QCOMPARE(a->autoIncrease(), b->autoIncrease());
    QCOMPARE(withNotches.model.slices().size(), without.model.slices().size());
    QCOMPARE(withNotches.station->receivedKinds(), without.station->receivedKinds());
    withNotches.client.reset();
    without.client.reset();
    removeLocalNotchKeys();
}

void TstStationSession::receiveOnlyStationRefusesTransmitDspOptionsSettingsWrites()
{
    // R-R3-21. The DSP > Options TX combos write station transmit settings:
    // DspOptions keys are Station-scoped (SettingsScope.cpp), so a remote
    // window's write would otherwise land in the Core's store. A receive-only
    // Core refuses them for the same reason it refuses direct TransmitModel
    // writes, and the GUI learns it through the ordinary settings rejection.
    // Receive DSP options are still the operator's to change.
    const QString txKey = QStringLiteral("DspOptionsBufferSizePhoneTx");
    const QString rxKey = QStringLiteral("DspOptionsBufferSizePhoneRx");

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(txKey, QStringLiteral("1024"));
    stationSettings.setValue(rxKey, QStringLiteral("1024"));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY(stationModel->receiveOnlyStationPolicy());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(proxy.ready());
    QCOMPARE(proxy.value(txKey, QString()).toString(), QStringLiteral("1024"));

    QSignalSpy rejected(&proxy, &SettingsProxy::valueRejected);
    QSignalSpy toast(&clientModel, &RadioModel::sliceAddRejected);
    proxy.setValue(txKey, QStringLiteral("2048"));
    QTRY_COMPARE(rejected.count(), 1);
    QCOMPARE(rejected.first().at(0).toString(), txKey);
    QCOMPARE(rejected.first().at(1).toString(), QStringLiteral("1024"));
    QCOMPARE(stationSettings.value(txKey).toString(), QStringLiteral("1024"));
    QCOMPARE(proxy.value(txKey, QString()).toString(), QStringLiteral("1024"));
    QCOMPARE(toast.count(), 1);
    QCOMPARE(toast.first().at(0).toString(),
             QStringLiteral("Transmit configuration is unavailable on this receive-only station."));

    proxy.setValue(rxKey, QStringLiteral("2048"));
    QTRY_COMPARE(stationSettings.value(rxKey).toString(), QStringLiteral("2048"));
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(stationSettings.value(txKey).toString(), QStringLiteral("1024"));
}

void TstStationSession::receiveOnlyStationRefusesTransmitDspOptionsSettingsRemoves()
{
    // R-R3-21. A remove resets a DSP > Options TX setting to its default,
    // so a receive-only Core refuses it exactly as it refuses a write to
    // the same key, and hands its own value back so the remote cache
    // settles on it. Removing a receive DSP option is still allowed.
    const QString txKey = QStringLiteral("DspOptionsBufferSizePhoneTx");
    const QString rxKey = QStringLiteral("DspOptionsBufferSizePhoneRx");

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(txKey, QStringLiteral("1024"));
    stationSettings.setValue(rxKey, QStringLiteral("1024"));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY(stationModel->receiveOnlyStationPolicy());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(proxy.ready());
    QCOMPARE(proxy.value(txKey, QString()).toString(), QStringLiteral("1024"));

    QSignalSpy rejected(&proxy, &SettingsProxy::valueRejected);
    QSignalSpy toast(&clientModel, &RadioModel::sliceAddRejected);
    proxy.remove(txKey);
    QVERIFY(!proxy.contains(txKey));
    QTRY_COMPARE(rejected.count(), 1);
    QCOMPARE(rejected.first().at(0).toString(), txKey);
    QCOMPARE(rejected.first().at(1).toString(), QStringLiteral("1024"));
    QVERIFY(stationSettings.contains(txKey));
    QCOMPARE(stationSettings.value(txKey).toString(), QStringLiteral("1024"));
    QCOMPARE(proxy.value(txKey, QString()).toString(), QStringLiteral("1024"));
    QCOMPARE(toast.count(), 1);
    QCOMPARE(toast.first().at(0).toString(),
             QStringLiteral("Transmit configuration is unavailable on this receive-only station."));

    proxy.remove(rxKey);
    QTRY_VERIFY(!stationSettings.contains(rxKey));
    QCOMPARE(rejected.count(), 1);
    QCOMPARE(stationSettings.value(txKey).toString(), QStringLiteral("1024"));
}

void TstStationSession::acceptedReceiveDspOptionsWriteAppliesToMatchingSlices()
{
    // R-R3-21. A DSP > Options RX write or remove from a remote window takes
    // effect on the Core at once: the Core re-runs the mode-change apply for
    // each slice in the key's mode group instead of waiting for the next
    // mode change. A refused TX key, an unrelated key and a local write to
    // the Core's own store apply nothing.
    const QString phoneRx = QStringLiteral("DspOptionsBufferSizePhoneRx");
    const QString cwRx = QStringLiteral("DspOptionsFilterSizeCwRx");
    const QString phoneTx = QStringLiteral("DspOptionsBufferSizePhoneTx");
    const QString unrelated = QStringLiteral("DspOptionsCacheImpulse");

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(phoneRx, QStringLiteral("1024"));
    stationSettings.setValue(cwRx, QStringLiteral("4096"));
    stationSettings.setValue(phoneTx, QStringLiteral("1024"));

    auto stationModel = makeStationRadioModel(1);
    QCOMPARE(stationModel->slices().size(), 2);
    SliceModel* phoneSlice = stationModel->slices().at(0);
    SliceModel* cwSlice = stationModel->slices().at(1);
    phoneSlice->setDspMode(DSPMode::USB);
    cwSlice->setDspMode(DSPMode::CWU);
    QList<QPair<int, DSPMode>> applied;
    stationModel->setDspOptionsApplyObserverForTest([&applied](int index, DSPMode mode) {
        applied.append(qMakePair(index, mode));
    });
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY(stationModel->receiveOnlyStationPolicy());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(proxy.ready());

    // Accepted RX write: one apply, to the Phone slice only.
    proxy.setValue(phoneRx, QStringLiteral("2048"));
    QTRY_COMPARE(stationSettings.value(phoneRx).toString(), QStringLiteral("2048"));
    QTRY_COMPARE(applied.size(), 1);
    QCOMPARE(applied.first(), qMakePair(phoneSlice->sliceIndex(), DSPMode::USB));

    // Refused TX write and an unrelated accepted key: nothing applied.
    QSignalSpy rejected(&proxy, &SettingsProxy::valueRejected);
    proxy.setValue(phoneTx, QStringLiteral("2048"));
    QTRY_COMPARE(rejected.count(), 1);
    proxy.setValue(unrelated, QStringLiteral("True"));
    QTRY_COMPARE(stationSettings.value(unrelated).toString(), QStringLiteral("True"));
    QTest::qWait(200);
    QCOMPARE(applied.size(), 1);

    // Accepted RX remove: the CW slice applies its default.
    proxy.remove(cwRx);
    QTRY_VERIFY(!stationSettings.contains(cwRx));
    QTRY_COMPARE(applied.size(), 2);
    QCOMPARE(applied.at(1), qMakePair(cwSlice->sliceIndex(), DSPMode::CWU));

    // Local half: the Core's own store changing is not a remote write.
    stationSettings.setValue(phoneRx, QStringLiteral("512"));
    QTest::qWait(200);
    QCOMPARE(applied.size(), 2);
}

void TstStationSession::receiveOnlyPolicySurvivesRadioTeardown()
{
    // Use the real non-null connection teardown path, without opening a
    // socket or starting DSP. A null connection returns before clearing the
    // ordinary local MOX check and would miss this lifecycle regression.
    P1RadioConnection connection;
    RadioModel station;
    station.injectConnectionForTest(&connection);
    station.setReceiveOnlyStationPolicy(true);
    station.disconnectFromRadio();
    QVERIFY(station.connection() == nullptr);
    QVERIFY(station.receiveOnlyStationPolicy());
    QSignalSpy rejected(station.moxController(), &MoxController::moxRejected);
    station.moxController()->setMox(true);
    QCOMPARE(rejected.count(), 1);
    QVERIFY(!station.moxController()->isMox());
}

void TstStationSession::handshakeDeadlineDropsASilentPeer()
{
    // Minor: a peer that opens a socket and answers pings but never
    // authenticates used to live forever, holding a slot.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setAuthDeadlineMs(60);
    QCOMPARE(server.authDeadlineMs(), 60);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("lurker"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("lurker-client"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy dropped(&server, &StationServer::peerDisconnected);
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(server.peerCount(), 1);

    // It answers pings (the default) but never says hello or authenticates.
    QTRY_COMPARE_WITH_TIMEOUT(server.peerCount(), 0, 3000);
    QCOMPARE(dropped.count(), 1);
    QVERIFY(!server.hasAuthenticatedSession());

    // A peer that DOES authenticate is not dropped by the same deadline.
    auto* goodStation = new LoopbackTransport(QStringLiteral("good"), this);
    auto* goodClient = new LoopbackTransport(QStringLiteral("good-client"), this);
    goodStation->linkTo(goodClient);
    server.acceptTransport(goodStation);
    QTRY_VERIFY(!goodClient->received().isEmpty());
    goodClient->sendText(SessionMessages::encode(
        SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                               QStringLiteral("good"))));
    goodClient->sendText(
        SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QTRY_VERIFY(server.hasAuthenticatedSession());
    QTest::qWait(200);  // well past the 60 ms deadline
    QVERIFY2(server.hasAuthenticatedSession(),
             "the handshake deadline fired on a peer that had authenticated");
}

void TstStationSession::peerLimitRefusesFurtherConnections()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    // Out of the way: this slot is about the cap, not the deadline.
    server.setAuthDeadlineMs(0);

    QList<LoopbackTransport*> clientEnds;
    for (int i = 0; i < StationServer::kMaxConcurrentPeers; ++i) {
        auto* stationEnd =
            new LoopbackTransport(QStringLiteral("peer-%1").arg(i), this);
        auto* clientEnd =
            new LoopbackTransport(QStringLiteral("peer-%1-client").arg(i), this);
        stationEnd->linkTo(clientEnd);
        server.acceptTransport(stationEnd);
        clientEnds.append(clientEnd);
    }
    QCOMPARE(server.peerCount(), StationServer::kMaxConcurrentPeers);

    auto* overflowStation = new LoopbackTransport(QStringLiteral("overflow"), this);
    auto* overflowClient = new LoopbackTransport(QStringLiteral("overflow-client"), this);
    overflowStation->linkTo(overflowClient);
    server.acceptTransport(overflowStation);

    QCOMPARE(server.peerCount(), StationServer::kMaxConcurrentPeers);
    // Refused with a reason on the wire, not an unexplained close.
    QTRY_VERIFY(overflowClient->receivedKinds().contains(QByteArrayLiteral("session.end")));
}

void TstStationSession::listenIsIdempotent()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));
    const quint16 port = server.serverPort();

    // A second call used to re-apply the SSL config and fail the bind into
    // lastError(), leaving a working listener described as broken.
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));
    QVERIFY(server.lastError().isEmpty());
    QCOMPARE(server.serverPort(), port);
    QVERIFY(server.isListening());

    server.close();
}

// ── Security fix round ───────────────────────────────────────────────────

void TstStationSession::firstRunPairingBannerNeverReachesTheLoggingHandler()
{
    // The banner used to go out through qCInfo(lcStation), which put it
    // into the hands of CoreInit's process-wide message handler. That
    // handler does two things to it: redactPii()'s MAC rule shredded the
    // 32-pair TLS fingerprint down to 7 surviving bytes, and the message
    // was written verbatim into the daemon's persistent log file -- the
    // file CONTRIBUTING.md tells operators to attach to a bug report, and
    // a worse medium for a shared secret than the AppSettings XML
    // TokenStore.h refuses to use for exactly that reason.
    //
    // A FRESH security directory, not the class-wide m_securityDir: this
    // slot needs TokenStore::wasGeneratedThisRun() to be true, and
    // m_securityDir already holds a token from an earlier slot. The price
    // is one extra RSA-3072 key generation for the run.
    QTemporaryDir freshSecurity;
    QVERIFY(freshSecurity.isValid());
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);

    QStringList captured;
    std::unique_ptr<StationServer> server;
    {
        LogCapture capture(&captured);
        server = std::make_unique<StationServer>(stationModel.get(), stationSettings,
                                                 freshSecurity.path());
    }

    const QString token = server->token();
    const QString fingerprint = server->certificateFingerprint();
    QVERIFY2(!token.isEmpty(), "no token was provisioned, so this slot proves nothing");
    QVERIFY2(!fingerprint.isEmpty(),
             "no certificate was provisioned, so this slot proves nothing");

    for (const QString& line : captured) {
        QVERIFY2(!line.contains(token),
                 qPrintable(QStringLiteral(
                                "the pairing token reached the Qt logging handler, "
                                "which writes it verbatim into the daemon's "
                                "persistent log file. Line: %1").arg(line)));
        QVERIFY2(!line.contains(fingerprint),
                 qPrintable(QStringLiteral(
                                "the TLS fingerprint reached the Qt logging handler, "
                                "whose redactPii() destroys 25 of its 32 bytes. "
                                "Line: %1").arg(line)));
    }

    // A first run must still leave a trace an operator can find, or the
    // fix trades one support problem for another.
    bool mentionsFirstRun = false;
    for (const QString& line : captured) {
        if (line.contains(QStringLiteral("First run"))) {
            mentionsFirstRun = true;
            break;
        }
    }
    QVERIFY2(mentionsFirstRun,
             "nothing in the log says a first run provisioned anything");

    // And what the operator IS shown carries both values intact. Asserted
    // against the formatter rather than by capturing stdout, so the check
    // is the same on every platform; writePairingBanner() is a single
    // fwrite of exactly this string.
    const QString banner = StationServer::formatPairingBanner(token, fingerprint,
                                                              freshSecurity.path());
    QVERIFY(banner.contains(token));
    QVERIFY(banner.contains(fingerprint));

    // The other half of the same defect, pinned here because this is where
    // the two meet: even a fingerprint logged from somewhere else now
    // survives redaction untouched.
    QCOMPARE(CoreInit::redactPiiForTest(fingerprint), fingerprint);
}

void TstStationSession::oversizedMessageIsRefusedBeforeAnyAuthentication()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // Critical 2. StationServer never capped the sockets it accepted, and
    // Qt's defaults are roughly INT_MAX, about 2 GiB per message, buffered
    // in full before textMessageReceived fires. The 30 s auth deadline
    // bounds TIME, not BYTES, so this was entirely pre-authentication.
    //
    // The assertion is on the CLOSE CODE, deliberately, not on the peer
    // going away. An uncapped station also drops this peer -- it buffers
    // the whole message, fails to decode it, and closes with
    // CloseCodeNormal and the reason "undecodable message". Only a capped
    // socket refuses the BYTES, which Qt reports as CloseCodeTooMuchData
    // (1009). Asserting on peerCount alone would pass either way.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

    // A RAW QWebSocket, not a StationClient: this peer is deliberately
    // hostile and must not be constrained by the client's own protocol.
    QWebSocket raw;
    connect(&raw, &QWebSocket::sslErrors, &raw,
            [&raw](const QList<QSslError>& errors) { raw.ignoreSslErrors(errors); });
    QSignalSpy rawConnected(&raw, &QWebSocket::connected);
    raw.open(QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort())));
    QTRY_COMPARE_WITH_TIMEOUT(rawConnected.count(), 1, 15000);
    QTRY_COMPARE(server.peerCount(), 1);

    // Comfortably past the cap and nowhere near Qt's default, so an
    // uncapped station accepts every byte of it.
    const qsizetype oversize =
        static_cast<qsizetype>(StationServer::kMaxIncomingMessageBytes) + 4096;
    raw.sendTextMessage(QString(oversize, QLatin1Char('x')));

    QTRY_COMPARE_WITH_TIMEOUT(server.peerCount(), 0, 15000);
    QVERIFY2(!server.hasAuthenticatedSession(),
             "an oversized message reached a peer that had authenticated");
    QTRY_COMPARE_WITH_TIMEOUT(raw.closeCode(),
                              QWebSocketProtocol::CloseCodeTooMuchData, 5000);

    server.close();
}

void TstStationSession::clientCapsWhatAStationCanMakeItAllocate()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // The other direction. A pinned certificate proves WHO the station is;
    // it promises nothing about how much the station will ask this GUI to
    // allocate. Asserted on the socket the production dial path actually
    // creates, through StationClient::transport(), rather than on a
    // hand-built WebSocketTransport -- the defect was that dialStation()
    // passed no cap, so constructing a transport by hand in the test would
    // have proved nothing about the call site.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);

    client.connectToStation(
        QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort())),
        server.token(), server.certificateFingerprint());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);

    auto* transport = qobject_cast<WebSocketTransport*>(client.transport());
    QVERIFY2(transport != nullptr, "the dial did not produce a WebSocketTransport");
    QVERIFY(transport->socket() != nullptr);
    QCOMPARE(transport->socket()->maxAllowedIncomingMessageSize(),
             StationClient::kMaxIncomingMessageBytes);
    QCOMPARE(transport->socket()->maxAllowedIncomingFrameSize(),
             StationClient::kMaxIncomingMessageBytes);

    // And the daemon's own accepted socket carries the smaller cap, so the
    // two constants are not accidentally the same number.
    QVERIFY(StationServer::kMaxIncomingMessageBytes
            < StationClient::kMaxIncomingMessageBytes);

    client.disconnectFromStation(QStringLiteral("test complete"));
    server.close();
}

void TstStationSession::wsSchemeIsRefusedWhenAFingerprintIsPinned()
{
    // Critical 3, instance 1. No TLS needed to prove it, which is the
    // point: RemoteStationOptions::isValidStationUrl accepts ws:// and its
    // rejection message advertises it, connectToStation() refused only an
    // EMPTY fingerprint, and QWebSocket::sslErrors cannot fire on a link
    // with no TLS under it. An operator who had pinned a fingerprint
    // correctly and typed ws:// therefore got no pin comparison anywhere,
    // and handleHello() then put the pre-shared token on the wire in
    // cleartext.
    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    const QString fingerprint =
        QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                       "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF");
    client.connectToStation(QUrl(QStringLiteral("ws://127.0.0.1:50100")),
                            QStringLiteral("the-shared-secret"), fingerprint);

    // Refused SYNCHRONOUSLY, before anything was dialed at all. The old
    // code reached dialStation(), built a QWebSocket and opened it, so
    // this count was 0 on return.
    QCOMPARE(ended.count(), 1);
    QVERIFY2(client.transport() == nullptr,
             "a transport was created for a scheme that cannot carry a pin");
    QVERIFY2(client.lastError().contains(QStringLiteral("wss://")),
             qPrintable(client.lastError()));
    // Deliberately NOT asserting isPinSatisfied() here: the refusal
    // returns before any transport is attached, so that flag describes no
    // attach at all and reads as its neutral default. What is being pinned
    // is that nothing was dialed, which the two checks above cover.

    // And nothing was latched for the automatic reconnect to redial: a
    // scheme refusal cannot converge by being retried.
    QVERIFY(!client.isReconnectPending());
}

void TstStationSession::tokenIsNeverSentOnALinkWhosePinWasNeverChecked()
{
    // Critical 3, instance 2. QWebSocket::sslErrors fires ONLY when the
    // handshake produced errors, and the pin comparison lived exclusively
    // inside that handler. A handshake the client's own trust store
    // already accepts -- a corporate or antivirus MITM root, a real DV
    // certificate for a dynamic-DNS station name -- therefore reached
    // handleHello() with no comparison having happened anywhere, and
    // handleHello() sent the pre-shared token.
    //
    // WHAT THIS SLOT DOES NOT DO, stated plainly. It does not stand up a
    // genuinely error-free TLS handshake. That is not reachable in-process
    // against this station: CertificateStore issues CN "nereusd" with no
    // subjectAltName, so any connection to 127.0.0.1 raises
    // HostNameMismatch, and QWebSocket exposes no per-socket
    // setPeerVerifyName() to redirect the check (that is a QSslSocket
    // member, not a QSslConfiguration one, on Qt 6.11). Turning peer
    // verification off in the default configuration was tried and does not
    // help: Qt still emits sslErrors and merely continues afterwards, so
    // the old code's handler still ran and the case stayed invisible.
    //
    // So the property is pinned where it is actually specified instead:
    // the token must not leave this process while the pin is unchecked,
    // whatever produced that state. A LoopbackTransport carries no TLS at
    // all, which is the strongest possible form of "no certificate was
    // ever compared" -- strictly worse than the trusted-MITM case, and
    // driven through the same handleHello() gate that case now goes
    // through. Before the gate existed, the client answered the station's
    // Hello with an auth.request carrying the token.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client"), this);
    stationEnd->linkTo(clientEnd);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy ended(&client, &StationClient::sessionEnded);
    QSignalSpy authenticated(&server, &StationServer::clientAuthenticated);

    server.acceptTransport(stationEnd);

    // The CORRECT token, so nothing but the pin can refuse this, and a
    // fingerprint the link has no way to satisfy.
    const QString pin =
        QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                       "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF");
    client.startSession(clientEnd, server.token(), pin);

    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
    QVERIFY(!client.isPinSatisfied());
    QVERIFY2(client.lastError().contains(QStringLiteral("no certificate")),
             qPrintable(client.lastError()));

    // The leak, asserted from both ends. On the wire: no auth.request ever
    // left the client. On the station: nobody proved they held the secret.
    QTest::qWait(200);
    QVERIFY2(!stationEnd->receivedKinds().contains(QByteArrayLiteral("auth.request")),
             "the client sent its pre-shared token over a link whose certificate "
             "fingerprint had never been compared");
    QCOMPARE(authenticated.count(), 0);

    // And a seam session with NO pin stated is unaffected, which is every
    // other slot in this file: the default argument means those keep
    // authenticating exactly as before.
    QVERIFY(StationClient(&clientModel, &proxy).isPinSatisfied());
}

void TstStationSession::transientRefusalsStayRetryableAndABadTokenDoesNot()
{
    // Important 4. Two station-side refusals an operator actually hits are
    // transient by nature, and both used to take
    // disconnectFromStation()'s default of attemptReconnect = false, which
    // PERMANENTLY disarms automatic reconnect:
    //
    //   - "Station is at its concurrent-connection limit", a cap
    //     StationServer.h sizes for "one client, and a couple of stale
    //     sockets from a reconnecting client" -- so it is expected to be
    //     hit BY a reconnecting client, which then gave up forever.
    //   - "Too many failed authentication attempts", where the rate
    //     limiter is GLOBAL rather than per-peer (TokenStore.h:44-48), so
    //     a stranger's five bad guesses inside 60 s refuse the operator's
    //     correct token too.
    //
    // A wrong token must stay permanent, because retrying a wrong secret
    // forever is how the rate limiter above gets fed.
    //
    // Asserted on the WIRE FLAG rather than on client timer state, because
    // that flag is the whole mechanism: the client must not be classifying
    // refusals by matching the station's English prose.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    server.setAuthDeadlineMs(0);  // out of the way; this slot is about refusals

    // ---- Peer-limit refusal ----
    QList<LoopbackTransport*> held;
    for (int i = 0; i < StationServer::kMaxConcurrentPeers; ++i) {
        auto* stationEnd = new LoopbackTransport(QStringLiteral("hold-%1").arg(i), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("hold-%1-c").arg(i), this);
        stationEnd->linkTo(clientEnd);
        server.acceptTransport(stationEnd);
        held.append(clientEnd);
    }
    auto* overflowStation = new LoopbackTransport(QStringLiteral("overflow"), this);
    auto* overflowClient = new LoopbackTransport(QStringLiteral("overflow-c"), this);
    overflowStation->linkTo(overflowClient);
    server.acceptTransport(overflowStation);

    QTRY_VERIFY(overflowClient->receivedKinds().contains(QByteArrayLiteral("session.end")));
    SessionMessage limitEnd;
    for (const QByteArray& wire : overflowClient->received()) {
        const SessionMessage m = decodeOrFail(wire);
        if (m.kind == SessionMessageKind::SessionEnd) {
            limitEnd = m;
            break;
        }
    }
    QCOMPARE(limitEnd.kind, SessionMessageKind::SessionEnd);
    QVERIFY(limitEnd.reason.contains(QStringLiteral("concurrent-connection limit")));
    QVERIFY2(limitEnd.retryable,
             "the concurrent-connection cap was sent as permanent, so a "
             "reconnecting client that hits it gives up forever");

    // ---- Bad token, then the rate limit it produces ----
    StationServer authServer(stationModel.get(), stationSettings, m_securityDir.path());
    authServer.setAuthDeadlineMs(0);
    authServer.setAuthRateLimit(2, 60000);

    // Returns the CLIENT end so the caller can QTRY on it: LoopbackTransport
    // delivers through the event loop, so reading received() straight after
    // sendText() sees nothing.
    auto refuse = [&](const QString& candidate) {
        auto* stationEnd = new LoopbackTransport(QStringLiteral("guess"), this);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("guess-c"), this);
        stationEnd->linkTo(clientEnd);
        authServer.acceptTransport(stationEnd);
        clientEnd->sendText(SessionMessages::encode(
            SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor, 6,
                                   QStringLiteral("guess"))));
        clientEnd->sendText(
            SessionMessages::encode(SessionMessages::authRequest(candidate)));
        return clientEnd;
    };

    auto refusalOn = [](LoopbackTransport* end) {
        SessionMessage result;
        for (const QByteArray& wire : end->received()) {
            const SessionMessage m = decodeOrFail(wire);
            if (m.kind == SessionMessageKind::AuthResult && !m.accepted) {
                result = m;
            }
        }
        return result;
    };

    LoopbackTransport* firstEnd = refuse(QStringLiteral("not-the-token"));
    QTRY_VERIFY(firstEnd->receivedKinds().contains(QByteArrayLiteral("auth.result")));
    const SessionMessage firstBad = refusalOn(firstEnd);
    QCOMPARE(firstBad.kind, SessionMessageKind::AuthResult);
    QCOMPARE(firstBad.reason, QStringLiteral("Authentication failed"));
    QVERIFY2(!firstBad.retryable,
             "a wrong token was marked retryable, so a client would redial it "
             "forever and feed the station's own rate limiter");

    LoopbackTransport* secondEnd = refuse(QStringLiteral("still-not-the-token"));
    QTRY_VERIFY(secondEnd->receivedKinds().contains(QByteArrayLiteral("auth.result")));
    const SessionMessage secondBad = refusalOn(secondEnd);
    QCOMPARE(secondBad.reason, QStringLiteral("Authentication failed"));
    QVERIFY(!secondBad.retryable);

    // Two failures at a limit of two: the next attempt is rate limited,
    // and it would be even with the CORRECT token, which is exactly the
    // lockout this flag has to let the operator recover from.
    LoopbackTransport* lockedEnd = refuse(authServer.token());
    QTRY_VERIFY(lockedEnd->receivedKinds().contains(QByteArrayLiteral("auth.result")));
    const SessionMessage locked = refusalOn(lockedEnd);
    QCOMPARE(locked.kind, SessionMessageKind::AuthResult);
    QVERIFY(locked.reason.contains(QStringLiteral("Too many failed")));
    QVERIFY2(locked.retryable,
             "a rate-limit lockout was sent as permanent, so a stranger's bad "
             "guesses lock the operator out with no automatic recovery");
}

void TstStationSession::lockedOutOperatorRetriesButABadTokenDoesNot()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // The client half of Important 4, end to end and over a real dial,
    // because only a dial latches a redial target for scheduleReconnect().
    //
    // The narrative, exactly as an operator meets it: a stranger who can
    // reach the port guesses wrong, the GLOBAL rate limiter trips
    // (TokenStore.h:44-48: a lockout refuses a connection "including one
    // carrying the correct token"), and the operator's own GUI is then
    // refused. Before this fix that refusal permanently disarmed automatic
    // reconnect, so the operator stayed locked out until they noticed and
    // reconnected by hand. Now the GUI backs off and comes back on its own
    // once the lockout expires.
    //
    // Both halves are discriminating. The stranger's wrong token must NOT
    // re-arm; the operator's rate-limited refusal MUST.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    // One failure trips the lockout, so the sequence below is two dials
    // rather than six.
    server.setAuthRateLimit(1, 60000);
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

    const QUrl url(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort()));

    // ---- The stranger ----
    RadioModel strangerModel(RadioModel::Role::Remote);
    SettingsProxy strangerProxy;
    StationClient stranger(&strangerModel, &strangerProxy);
    QSignalSpy strangerEnded(&stranger, &StationClient::sessionEnded);
    stranger.connectToStation(url, QStringLiteral("not-the-token"),
                              server.certificateFingerprint());
    QTRY_COMPARE_WITH_TIMEOUT(strangerEnded.count(), 1, 15000);
    QVERIFY(stranger.lastError().contains(QStringLiteral("Authentication failed")));
    QVERIFY2(!stranger.isReconnectPending(),
             "a wrong token re-armed automatic reconnect, which would hammer the "
             "station's rate limiter and keep the operator locked out");

    // ---- The operator, refused by the stranger's lockout ----
    RadioModel operatorModel(RadioModel::Role::Remote);
    SettingsProxy operatorProxy;
    StationClient op(&operatorModel, &operatorProxy);
    QSignalSpy opEnded(&op, &StationClient::sessionEnded);
    op.connectToStation(url, server.token(), server.certificateFingerprint());
    QTRY_COMPARE_WITH_TIMEOUT(opEnded.count(), 1, 15000);
    QVERIFY2(op.lastError().contains(QStringLiteral("Too many failed")),
             qPrintable(op.lastError()));
    QVERIFY2(op.isReconnectPending(),
             "the operator's own client gave up permanently on a lockout that "
             "expires on its own, so a stranger's failed guesses locked them out "
             "of their own station until they reconnected by hand");

    // Cancel the pending retry before teardown so it cannot fire into a
    // station this slot is about to close.
    op.disconnectFromStation(QStringLiteral("test complete"));
    QVERIFY(!op.isReconnectPending());

    server.close();
}

// ── TLS ──────────────────────────────────────────────────────────────────

void TstStationSession::wssListenerComesUpAndCompletesAHandshake()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(1);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));
    QVERIFY(server.isListening());
    QVERIFY(server.serverPort() != 0);
    QVERIFY(!server.certificateFingerprint().isEmpty());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    const QUrl url(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort()));
    client.connectToStation(url, server.token(), server.certificateFingerprint());

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
    QCOMPARE(ended.count(), 0);
    QVERIFY(clientModel.isConnected());
    QCOMPARE(clientModel.slices().size(), stationModel->slices().size());

    server.close();
}

void TstStationSession::wssRefusesAMismatchedCertificateFingerprint()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(
        settingsDir.filePath(QStringLiteral("NereusSDR.settings")));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    // Pinning is the entire identity check for a self-signed certificate
    // (parent design section 10.5). A client that connected anyway when
    // the fingerprint did not match would make the certificate model
    // decorative.
    const QString wrongFingerprint =
        QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                       "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF");
    const QUrl url(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort()));
    client.connectToStation(url, server.token(), wrongFingerprint);

    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 15000);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!clientModel.isConnected());
    QVERIFY2(client.lastError().contains(QStringLiteral("fingerprint")),
             qPrintable(client.lastError()));

    // And an EMPTY pin is refused outright rather than silently accepting
    // anything, which is the failure mode that looks like it works.
    RadioModel unpinnedModel(RadioModel::Role::Remote);
    SettingsProxy unpinnedProxy;
    StationClient unpinned(&unpinnedModel, &unpinnedProxy);
    QSignalSpy unpinnedEnded(&unpinned, &StationClient::sessionEnded);
    unpinned.connectToStation(url, server.token(), QString());
    QCOMPARE(unpinnedEnded.count(), 1);
    QVERIFY(!unpinnedModel.isConnected());

    server.close();
}

void TstStationSession::failedInitialConnectReportsPromptly()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // Important 4's headline case. errorOccurred set m_lastError and
    // logged but emitted NOTHING, and onTransportClosed emitted
    // sessionEnded only `if (m_handshakeComplete)`, so a station that was
    // down, a wrong port or a refused TLS handshake produced no signal at
    // all for a full 40 to 60 second heartbeat window. Only the
    // fingerprint-mismatch path emitted, which is why the existing TLS
    // slot passed while the ORDINARY failure was uncovered.
    //
    // v0.5.1 shipped "connection state stuck Connected on failed initial
    // connect". Same bug class, so it gets a test rather than a comment.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 deadPort = probe.serverPort();
    probe.close();  // nothing is listening there now

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy ended(&client, &StationClient::sessionEnded);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);

    const QString anyFingerprint =
        QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                       "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF");
    client.connectToStation(QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(deadPort)),
                            QStringLiteral("token"), anyFingerprint);

    // WELL inside one heartbeat interval, which is the entire point: the
    // old behaviour would have taken 40 to 60 seconds, and only then via
    // a mechanism that had nothing to do with the connect failing.
    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 5000);
    QCOMPARE(completed.count(), 0);
    QVERIFY(!clientModel.isConnected());
    QVERIFY(!ended.first().first().toString().isEmpty());

    // And exactly once, however many socket errors and closes unwind.
    QTest::qWait(300);
    QCOMPARE(ended.count(), 1);
}

QTEST_MAIN(TstStationSession)
#include "tst_station_session.moc"
