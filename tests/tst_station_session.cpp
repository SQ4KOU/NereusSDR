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
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
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
#include "core/security/CertificateStore.h"
#include "core/security/TokenStore.h"
#include "core/session/SessionMessages.h"
#include "core/session/SessionTransport.h"
#include "core/session/StateMirror.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

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

    // ---- TokenStore (task 18 step 3) ----
    void tokenIsGeneratedNotChosenAndPersists();
    void tokenVerifyIsRateLimitedAfterRepeatedFailures();

    // ---- The connect sequence, over a non-TLS in-process link ----
    void handshakeCompletesInSectionSevenZeroOrder();
    void capabilitiesAdvertiseEffectiveNotBoardLimits();
    void clientAppliesCapabilitiesAndDrivesConnected();

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
    void settingsProxyIsNotReadyBeforeTheSnapshot();
    void schemaSkewIsCaughtByNameComparison();

    // ---- Fix round 1 ----
    void reconnectSurvivesTheOldTransportClosing();
    void heartbeatTimeoutReportsTheSessionAsEnded();
    void tunerPropertiesAreCountedAsUnapplied();
    void handshakeDeadlineDropsASilentPeer();
    void peerLimitRefusesFurtherConnections();
    void listenIsIdempotent();

    // ---- Security fix round ----
    void firstRunPairingBannerNeverReachesTheLoggingHandler();
    void oversizedMessageIsRefusedBeforeAnyAuthentication();
    void clientCapsWhatAStationCanMakeItAllocate();

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

// ── Version policy ───────────────────────────────────────────────────────

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
    stationSlice->setSignalStrengthDbm(-73.0);
    QTRY_COMPARE(clientSlice->signalStrengthDbm(), -73.0);
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
        SessionMessages::encode(SessionMessages::authResult(true, QString())));

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

void TstStationSession::tunerPropertiesAreCountedAsUnapplied()
{
    // Important 5. TunerModel::applyMirroredValue answers isOperate /
    // isBypass / antennaA by calling COMMAND SENDERS that forward to a
    // bound TgxlConnection and no-op when there is none, while still
    // returning success. On a client that meant those properties reported
    // as APPLIED, changed nothing, read stale, and never entered
    // unappliedProperties() -- defeating the accessor Task 20's bench is
    // meant to trust.
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

    // The tuner is watched by the station, so its full property bag
    // arrived in the connect burst and every one of these was attempted.
    const QSet<QByteArray> unapplied = client.unappliedProperties();
    QVERIFY2(unapplied.contains(QByteArrayLiteral("TunerModel.isOperate")),
             "isOperate reported as applied while changing nothing");
    QVERIFY2(unapplied.contains(QByteArrayLiteral("TunerModel.isBypass")),
             "isBypass reported as applied while changing nothing");
    QVERIFY(unapplied.contains(QByteArrayLiteral("TunerModel.antennaA")));

    // And the property that genuinely DOES land is not swept into the set
    // along with them: SliceModel::signalStrengthDbm reaches its own plain
    // setter through the hook, which is the one pair the client allowlists.
    SliceModel* stationSlice = stationModel->slices().first();
    SliceModel* clientSlice = clientModel.sliceById(stationSlice->sliceIndex());
    QVERIFY(clientSlice != nullptr);
    stationSlice->setSignalStrengthDbm(-91.0);
    QTRY_COMPARE(clientSlice->signalStrengthDbm(), -91.0);
    QVERIFY(!client.unappliedProperties().contains(
        QByteArrayLiteral("SliceModel.signalStrengthDbm")));
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
