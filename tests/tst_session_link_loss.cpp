// =================================================================
// tests/tst_session_link_loss.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Remote Daemon R2 Task 19: link loss, daemon restart, and reconnect.
//
// Task 18 built the session; nothing before this task defined what the
// client does with a mirror of objects that no longer exist. Parent design
// section 13 ("Error handling and reconnect"): "The client retains
// last-known state, indicates staleness rather than showing stale values
// as live, and reconnects with exponential backoff."
//
// ── HOW THIS FILE IS SPLIT, AND WHY ──────────────────────────────────────
//
// The link-loss CONTRACT (mirror teardown, the defined stale state, the
// settings-proxy cache-reads/drops-writes behaviour, the session epoch,
// and reconnect CONVERGENCE) is proven over an in-process, non-TLS
// SessionTransport (fakes/LoopbackTransport.h), matching
// tst_station_session.cpp's own split, and therefore runs on every build.
//
// The automatic-reconnect TIMER MECHANISM itself (owned, cancellable,
// exponential backoff, latched host/port) is new in this task and can only
// be exercised through connectToStation(), which always creates a real
// QWebSocket -- so those slots QSKIP when QSslSocket::supportsSsl() is
// false, naming the active Qt TLS backend, exactly like
// tst_station_session.cpp's TLS-specific slots.
//
// ── THE ONE THAT MATTERS MOST ────────────────────────────────────────────
//
// silentlyDeadPeerIsDetectedNotJustACleanClose. Killing the daemon (the
// first thing anyone does on a bench) produces a clean TCP close, and the
// close does all the work -- that is the easy case, covered by
// killedDaemonEntersDefinedStaleStateThenReconnectsToARestartedDaemonWith-
// DifferentState below. This slot covers the case that motivated pulling
// the heartbeat into R2 at all: a peer that stops responding WITHOUT
// closing, which is what a laptop lid, a cell handoff, or a NAT timeout
// actually produces. A test that only kills the process proves nothing
// about this path, because the close does the work.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 19: link loss,
//                                    daemon restart and reconnect. AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QByteArray>
#include <QHostAddress>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/security/CertificateStore.h"
#include "core/session/SessionTransport.h"
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
/// without a socket. Same seam tst_station_session.cpp's own
/// makeStationRadioModel() uses (file-local there, so duplicated here
/// rather than shared); connectToRadio() is unusable from a test.
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

/// An arbitrary fingerprint string, used only where the test never lets
/// the TLS handshake reach the point of comparing it (a dead-port dial
/// errors before any certificate is ever presented).
QString placeholderFingerprint()
{
    return QStringLiteral("00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:"
                          "00:11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF");
}

} // namespace

class TstSessionLinkLoss : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // ---- Step 1 + step 4: the link-loss contract and reconnect ----
    void killedDaemonEntersDefinedStaleStateThenReconnectsToARestartedDaemonWithDifferentState();

    // ---- Step 3a ----
    void silentlyDeadPeerIsDetectedNotJustACleanClose();

    // ---- Step 3's mechanism (TLS-specific: QSKIP when unusable) ----
    void autoReconnectUsesOwnedCancellableTimerWithExponentialBackoff();
    void staleTransportErrorDoesNotTearDownAFreshlyAttachedSession();
    void daemonRefusalDoesNotArmAutomaticReconnect();

private:
    /// One temp dir for the whole class so the RSA-3072 key pair is
    /// generated once and every later StationServer loads it back, rather
    /// than paying key generation per slot. Same rationale as
    /// tst_station_session.cpp's own member of this name.
    QTemporaryDir m_securityDir;
};

void TstSessionLinkLoss::initTestCase()
{
    QVERIFY(m_securityDir.isValid());
}

// ── Step 1 + step 4 ──────────────────────────────────────────────────────

void TstSessionLinkLoss::killedDaemonEntersDefinedStaleStateThenReconnectsToARestartedDaemonWithDifferentState()
{
    // ---- First daemon ----
    QTemporaryDir settingsDir1;
    QVERIFY(settingsDir1.isValid());
    AppSettings stationSettings1(settingsDir1.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings1.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50123"));

    auto stationModel1 = makeStationRadioModel(0);
    stationModel1->slices().first()->setFrequency(7100000.0);
    StationServer server1(stationModel1.get(), stationSettings1, m_securityDir.path());

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);

    auto* stationEnd1 = new LoopbackTransport(QStringLiteral("station-1"), this);
    auto* clientEnd1 = new LoopbackTransport(QStringLiteral("client-1"), this);
    stationEnd1->linkTo(clientEnd1);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy ended(&client, &StationClient::sessionEnded);
    client.startSession(clientEnd1, server1.token());
    server1.acceptTransport(stationEnd1);
    QTRY_COMPARE(completed.count(), 1);

    QVERIFY(clientModel.isConnected());
    QVERIFY2(!client.isStale(), "a freshly established session read as stale");
    QCOMPARE(client.sessionEpoch(), quint32(1));

    const int sliceId = stationModel1->slices().first()->sliceIndex();
    SliceModel* clientSliceBeforeKill = clientModel.sliceById(sliceId);
    QVERIFY(clientSliceBeforeKill != nullptr);
    QCOMPARE(clientSliceBeforeKill->frequency(), 7100000.0);
    QVERIFY(!client.mirroredObjectKeys().isEmpty());

    QVERIFY(proxy.ready());
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("0")).toString(),
             QStringLiteral("50123"));

    // ---- Kill the daemon: a clean TCP close, no SessionEnd message ----
    //
    // This is what Ctrl-C on nereusd produces: the process is gone before
    // it can send anything application-level, so the client only ever
    // sees the transport's closed() signal. Simulated by closing the
    // STATION-side transport, which is exactly what an OS-level socket
    // teardown looks like from the client's perspective.
    stationEnd1->closeLink(QStringLiteral("simulated: nereusd killed"));

    QTRY_COMPARE(ended.count(), 1);
    QCOMPARE(ended.first().first().toString(), QStringLiteral("link closed"));

    // ---- Step 1's contract ----

    QVERIFY2(client.mirroredObjectKeys().isEmpty(),
             "the client-side mirror registry was not torn down");
    QCOMPARE(clientModel.connectionState(), ConnectionState::Disconnected);
    QVERIFY(!clientModel.isConnected());
    QVERIFY2(client.isStale(),
             "isStale() did not enter the defined stale state on link loss");

    // RadioModel's own state is RETAINED, not reset (design doc section
    // 13): the client is showing last-known values, not zeros, and it is
    // the SAME SliceModel object -- nothing was destroyed.
    QCOMPARE(clientModel.sliceById(sliceId), clientSliceBeforeKill);
    QCOMPARE(clientSliceBeforeKill->frequency(), 7100000.0);

    // SettingsProxy: keeps serving the cache for reads, drops writes.
    QVERIFY(!proxy.ready());
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("0")).toString(),
             QStringLiteral("50123"));
    QSignalSpy outboundWhileOffline(&proxy, &SettingsProxy::outboundWriteRequested);
    proxy.setValue(QStringLiteral("TciServerPort"), QStringLiteral("60000"));
    QCOMPARE(outboundWhileOffline.count(), 0);
    // The LOCAL cache still updates optimistically (SettingsProxy.h's own
    // "offline behaviour": ready() gates the OUTBOUND side only) -- this
    // is what keeps a remote GUI's Setup page interactive while stale.
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("0")).toString(),
             QStringLiteral("60000"));
    QVERIFY(proxy.droppedWhileOffline().contains(QStringLiteral("TciServerPort")));

    // No automatic reconnect: this session was established via
    // startSession(), which never latches a URL.
    QVERIFY(!client.isReconnectPending());

    // ---- A restarted daemon, with DIFFERENT state ----
    QTemporaryDir settingsDir2;
    QVERIFY(settingsDir2.isValid());
    AppSettings stationSettings2(settingsDir2.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings2.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50999"));

    auto stationModel2 = makeStationRadioModel(0);
    stationModel2->slices().first()->setFrequency(14200000.0);
    StationServer server2(stationModel2.get(), stationSettings2, m_securityDir.path());

    auto* stationEnd2 = new LoopbackTransport(QStringLiteral("station-2"), this);
    auto* clientEnd2 = new LoopbackTransport(QStringLiteral("client-2"), this);
    stationEnd2->linkTo(clientEnd2);
    client.startSession(clientEnd2, server2.token());
    server2.acceptTransport(stationEnd2);

    QTRY_COMPARE(completed.count(), 2);

    // ---- Step 4: converges without a client relaunch ----

    QVERIFY(clientModel.isConnected());
    QVERIFY2(!client.isStale(), "a fresh reconnect still read as stale");
    QCOMPARE(client.sessionEpoch(), quint32(2));
    QVERIFY(!client.mirroredObjectKeys().isEmpty());

    // The SAME SliceModel object, adopted under the station's id -- proof
    // that no client relaunch and no rebinding was needed for a GUI
    // holding this pointer.
    SliceModel* clientSliceAfterReconnect = clientModel.sliceById(sliceId);
    QCOMPARE(clientSliceAfterReconnect, clientSliceBeforeKill);
    // ...but its value now reflects the fresh, DIFFERENT daemon's state.
    QCOMPARE(clientSliceAfterReconnect->frequency(), 14200000.0);

    QVERIFY(proxy.ready());
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("0")).toString(),
             QStringLiteral("50999"));
}

// ── Step 3a ──────────────────────────────────────────────────────────────

void TstSessionLinkLoss::silentlyDeadPeerIsDetectedNotJustACleanClose()
{
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    stationSettings.setValue(QStringLiteral("TciServerPort"), QStringLiteral("50123"));

    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    // Keep the STATION's own heartbeat out of the way; this slot is about
    // the CLIENT's detection of a silent STATION.
    server.setHeartbeatIntervalMs(0);

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    // Task 18's own production defaults are 20000 ms / 2 misses
    // (StationClient::kDefaultHeartbeatIntervalMs /
    // kDefaultMaxMissedPongs). Driven down here so the slot costs
    // milliseconds; every assertion below reads the LIVE configured
    // values back through the class's own accessors rather than
    // hardcoding a duplicate number, so this stays honest if the
    // defaults ever move.
    client.setHeartbeatIntervalMs(20);
    client.setMaxMissedPongs(2);
    QCOMPARE(client.heartbeatIntervalMs(), 20);
    QCOMPARE(client.maxMissedPongs(), 2);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("silent-station"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);

    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy timedOut(&client, &StationClient::stationHeartbeatTimeout);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    // Captured SYNCHRONOUSLY inside the stationHeartbeatTimeout handler
    // (onHeartbeatTick() emits it BEFORE disconnectFromStation() touches
    // anything), rather than read from `stationEnd` after the fact: the
    // station's own peer cleanup (StationServer::dropPeer(), reacting to
    // the close disconnectFromStation() provokes) deleteLater()s
    // `stationEnd` once its forwarded close arrives, and the SECOND
    // QTRY_COMPARE_WITH_TIMEOUT below pumps enough event-loop turns for
    // that deferred delete to run before a later read would see it -- a
    // real use-after-free this test hit while being written, distinct
    // from anything in the implementation under test.
    int pingsSeenAtTimeout = -1;
    connect(&client, &StationClient::stationHeartbeatTimeout, this,
            [&]() { pingsSeenAtTimeout = stationEnd->pingsSeen(); });

    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QTRY_COMPARE(completed.count(), 1);

    // THE CASE THIS STEP EXISTS FOR. The link stays nominally OPEN; the
    // station simply stops answering. No close, no application-level
    // SessionEnd, nothing for a close-driven implementation to notice --
    // exactly what a laptop lid, a cell handoff, or a NAT timeout looks
    // like from here.
    QVERIFY(stationEnd->isOpen());
    stationEnd->setAnswersPings(false);

    // Detection happens WITHIN the configured interval/miss count, not
    // never. 5000/2000 ms are generous ceilings for QTRY's own polling,
    // not the expected time -- the real interval was driven down above.
    QTRY_COMPARE_WITH_TIMEOUT(timedOut.count(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 2000);
    QCOMPARE(ended.first().first().toString(), QStringLiteral("heartbeat timeout"));

    // The pings really went out, against the LIVE configured value:
    // without them the miss counter could only have been driven by
    // something other than the heartbeat mechanism under test. Pings are
    // SENT by the client (client.m_transport is clientEnd) and RECEIVED
    // by the peer, so the count accumulates on stationEnd, not clientEnd.
    QVERIFY2(pingsSeenAtTimeout >= client.maxMissedPongs(),
             qPrintable(QStringLiteral("only %1 pings reached the station")
                            .arg(pingsSeenAtTimeout)));

    // Task 19's full link-loss contract holds via THIS detection path,
    // not only for a clean close (the previous slot).
    QVERIFY2(client.mirroredObjectKeys().isEmpty(),
             "the mirror registry survived a heartbeat-detected link loss");
    QVERIFY(!clientModel.isConnected());
    QVERIFY2(client.isStale(), "isStale() was not entered on heartbeat detection");
    QVERIFY(!proxy.ready());
    QCOMPARE(proxy.value(QStringLiteral("TciServerPort"), QStringLiteral("0")).toString(),
             QStringLiteral("50123"));

    // And it recovers: a reconnect (manual here -- this session has no
    // latched URL to auto-redial) converges.
    auto* stationEnd2 = new LoopbackTransport(QStringLiteral("station-2"), this);
    auto* clientEnd2 = new LoopbackTransport(QStringLiteral("client-2"), this);
    stationEnd2->linkTo(clientEnd2);
    client.startSession(clientEnd2, server.token());
    server.acceptTransport(stationEnd2);
    QTRY_COMPARE(completed.count(), 2);
    QVERIFY(!client.isStale());
    QVERIFY(clientModel.isConnected());
}

// ── Step 3's mechanism ───────────────────────────────────────────────────

void TstSessionLinkLoss::autoReconnectUsesOwnedCancellableTimerWithExponentialBackoff()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // A guaranteed-refused port: bind then close, same trick
    // tst_station_session.cpp's failedInitialConnectReportsPromptly uses.
    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 deadPort = probe.serverPort();
    probe.close();

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    // Shrinks the whole schedule proportionally (1/2/5/10/30/60 s becomes
    // 200/400/1000/2000/6000/12000 ms) so the MECHANISM -- schedule shape,
    // growth, cancellability -- can be exercised without a multi-minute
    // wait. The mechanism itself is identical to production; only the
    // unit changes. Kept well above QTest's own polling granularity
    // (QTRY_COMPARE_WITH_TIMEOUT below) so the "is it currently pending"
    // checks land inside the wait window rather than racing the next
    // scheduled attempt.
    client.setReconnectBackoffUnitMs(200);
    QCOMPARE(client.reconnectBackoffUnitMs(), 200);

    QSignalSpy scheduled(&client, &StationClient::reconnectScheduled);
    const QUrl deadUrl(QStringLiteral("wss://127.0.0.1:%1").arg(deadPort));
    client.connectToStation(deadUrl, QStringLiteral("token"), placeholderFingerprint());

    QTRY_COMPARE_WITH_TIMEOUT(scheduled.count(), 1, 5000);
    QCOMPARE(scheduled.at(0).at(0).toInt(), 1);
    QCOMPARE(scheduled.at(0).at(1).toInt(), 200);   // backoff step 1 * unit 200
    QVERIFY2(client.isReconnectPending(), "the retry timer was not armed");

    QTRY_COMPARE_WITH_TIMEOUT(scheduled.count(), 2, 5000);
    QCOMPARE(scheduled.at(1).at(0).toInt(), 2);
    QCOMPARE(scheduled.at(1).at(1).toInt(), 400);   // backoff step 2 * unit 200

    QTRY_COMPARE_WITH_TIMEOUT(scheduled.count(), 3, 5000);
    QCOMPARE(scheduled.at(2).at(0).toInt(), 3);
    QCOMPARE(scheduled.at(2).at(1).toInt(), 1000);  // backoff step 5 * unit 200

    // Cancellable: an operator disconnect DURING the backoff wait must
    // win, even though no session is active right now (parent design
    // section 13: "ICE restart and operator-initiated disconnect both
    // need [cancellability]").
    QVERIFY(client.isReconnectPending());
    client.disconnectFromStation(QStringLiteral("operator disconnect"));
    QVERIFY2(!client.isReconnectPending(),
             "disconnectFromStation() did not cancel the pending retry");

    const int scheduledAfterCancel = scheduled.count();
    // Comfortably shorter than the NEXT step (backoff step 10 * unit 200
    // = 2000 ms) this attempt count would have used had it fired, so this
    // proves the cancel held rather than merely landing in a lucky gap.
    QTest::qWait(800);
    QCOMPARE(scheduled.count(), scheduledAfterCancel);
    QVERIFY(!client.isReconnectPending());
}

void TstSessionLinkLoss::staleTransportErrorDoesNotTearDownAFreshlyAttachedSession()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // Task 19 controller notes, "the defect you will hit inside the first
    // reconnect test you write." StationClient.cpp's sslErrors/
    // errorOccurred lambdas were connected with the QWebSocket as sender,
    // not the SessionTransport wrapping it, so attachTransport()'s
    // disconnect(stale, ...) release (Task 18 fix round 1) did not cover
    // them. An asynchronous error from a closing OLD socket, delivered
    // after a NEW transport has already been attached, used to reach
    // endSession() unconditionally and tear the fresh session down. Fixed
    // in dialStation() by capturing the transport in the lambda and
    // returning early when it is no longer m_transport -- the same guard
    // onTransportClosed() already uses, for the identical reason.
    //
    // NON-VACUITY, STATED HONESTLY: this slot exercises the exact code
    // path the fix lives in -- two real dials, the first torn down out
    // from under an open client socket while the second supersedes it --
    // and passes with the fix in place. It does NOT independently prove
    // the fix is necessary: sabotaging the guard back out (both the
    // errorOccurred and the sslErrors lambda, tried separately, each with
    // several shapes -- a doomed dial to a dead port; a brief wait then
    // supersede; this abrupt-daemon-destruction shape, both single-shot
    // and hammered across 8 rapid redials; a deliberately mismatched
    // fingerprint on the stale dial to force its OWN error path) did not
    // reproduce an observable failure here. A diagnostic pass confirmed
    // WebSocketTransport::closeLink()'s m_socket->close() does not
    // reliably produce an asynchronous errorOccurred on this platform
    // (Qt 6.11, macOS, loopback) for a locally-initiated close, and an
    // sslErrors mismatch needs enough wall-clock time to reach the
    // certificate-comparison point that it is no longer racing by the
    // time it fires. The fix itself is verified correct by construction
    // (it is the literally prescribed remedy, capturing the transport and
    // comparing against m_transport) and by mirroring a pattern already
    // proven necessary and already covered by sabotage:
    // onTransportClosed()'s identical guard, whose removal IS caught by
    // reconnectSurvivesTheOldTransportClosing in tst_station_session.cpp
    // (Task 18 fix round 1, finding F3). This is recorded here rather
    // than a false claim of a passing sabotage run.
    QTemporaryDir settingsDir1;
    QVERIFY(settingsDir1.isValid());
    AppSettings stationSettings1(settingsDir1.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel1 = makeStationRadioModel(0);
    auto server1 = std::make_unique<StationServer>(stationModel1.get(), stationSettings1,
                                                    m_securityDir.path());
    QVERIFY2(server1->listen(QHostAddress::LocalHost, 0), qPrintable(server1->lastError()));

    QTemporaryDir settingsDir2;
    QVERIFY(settingsDir2.isValid());
    AppSettings stationSettings2(settingsDir2.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel2 = makeStationRadioModel(0);
    // A distinct MAC from server1's, so which session actually completed
    // can be told apart below rather than the two being indistinguishable.
    RadioInfo info2;
    info2.macAddress = QStringLiteral("AA:BB:CC:DD:EE:02");
    info2.name = QStringLiteral("Bench HL2 #2");
    info2.boardType = HPSDRHW::HermesLite;
    stationModel2->setLastRadioInfoForTest(info2);
    StationServer server2(stationModel2.get(), stationSettings2, m_securityDir.path());
    QVERIFY2(server2.listen(QHostAddress::LocalHost, 0), qPrintable(server2.lastError()));

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    QSignalSpy ended(&client, &StationClient::sessionEnded);

    // First dial: let it establish fully -- a real live socket, completed
    // TLS handshake, a genuine application session.
    client.connectToStation(QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server1->serverPort())),
                            server1->token(), server1->certificateFingerprint());
    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
    QVERIFY(clientModel.isConnected());

    // Destroy the daemon's entire station-server side out from under the
    // still-open client socket -- its QWebSocketServer, and every
    // accepted peer connection including the one the client is still
    // holding, go away in the SAME call. Immediately afterward, and with
    // NO event-loop turn in between (so the client's socket cannot yet
    // know its peer is gone), supersede with the second real dial. Both
    // "the daemon vanished" and "we are closing this socket ourselves"
    // are now true at once, racing to be whichever asynchronous signal
    // Qt's socket layer reports first, exactly the ambiguity the fix
    // exists to make safe regardless of which one wins.
    server1.reset();
    client.connectToStation(QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server2.serverPort())),
                            server2.token(), server2.certificateFingerprint());

    // Hammer it: several MORE back-to-back redials to the same real
    // server, each with no event-loop turn before the next, so several
    // stale sockets are in flight (mid-TCP-connect, mid-TLS-handshake, or
    // freshly established) at once when superseded. A single race attempt
    // narrows the window to one specific moment in the socket's
    // lifecycle; this widens it across many.
    const QUrl server2Url(QStringLiteral("wss://127.0.0.1:%1").arg(server2.serverPort()));
    for (int i = 0; i < 8; ++i) {
        client.connectToStation(server2Url, server2.token(), server2.certificateFingerprint());
    }

    QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 2, 15000);
    QVERIFY(clientModel.isConnected());
    // The session that completed SECOND is server2's, not server1's
    // leftover: this pins WHICH session actually finished last.
    QCOMPARE(client.capabilities().macAddress, stationModel2->currentRadioMac());

    // Now give whatever asynchronous signal the first (stale, abruptly
    // closed) socket produces every chance to arrive, well after the
    // second session succeeded.
    QTest::qWait(2000);

    QVERIFY2(client.isHandshakeComplete(),
             "a stale transport's asynchronous error tore down the freshly attached session");
    QVERIFY2(clientModel.isConnected(),
             "a stale transport's asynchronous error drove the fresh session to Disconnected");
    QCOMPARE(completed.count(), 2);
    // The fix means the stale signal is silently swallowed, not merely
    // survived: the session that actually ended (if the guard were
    // missing) would be the CURRENT one (server2's), so sessionEnded
    // would fire for it. It never should here.
    QCOMPARE(ended.count(), 0);

    // server1 was already destroyed above, mid-test.
    server2.close();
}

void TstSessionLinkLoss::daemonRefusalDoesNotArmAutomaticReconnect()
{
    if (!QSslSocket::supportsSsl()) {
        QSKIP(qPrintable(tlsSkipMessage()));
    }

    // Task 18 review, carried item: StationServer::kMaxConcurrentPeers
    // counts the authenticated session, so a client that auto-retried
    // against every daemon-spoken refusal (a version mismatch, a bad
    // token, or -- sharpest -- being PREEMPTED by a newer session) could
    // fight the very session that just took over for the length of the
    // auth deadline. This slot proves the general mechanism that
    // prevents it: a closure carrying an explicit reason FROM THE DAEMON
    // is never retried. Authentication refusal is the cheapest one of
    // those to set up deterministically; version mismatch, preemption and
    // the peer-limit refusal all funnel through the identical
    // disconnectFromStation(reason) call with the same implicit
    // attemptReconnect=false default, so this one slot covers all four by
    // construction, not by re-running each scenario.
    QTemporaryDir settingsDir;
    QVERIFY(settingsDir.isValid());
    AppSettings stationSettings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    auto stationModel = makeStationRadioModel(0);
    StationServer server(stationModel.get(), stationSettings, m_securityDir.path());
    QVERIFY2(server.listen(QHostAddress::LocalHost, 0), qPrintable(server.lastError()));

    RadioModel clientModel(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&clientModel, &proxy);
    // If the design were wrong, make the leak show up fast rather than
    // needing the full 1 s production first step.
    client.setReconnectBackoffUnitMs(5);
    QSignalSpy ended(&client, &StationClient::sessionEnded);
    QSignalSpy scheduled(&client, &StationClient::reconnectScheduled);

    const QUrl url(QStringLiteral("wss://127.0.0.1:%1").arg(server.serverPort()));
    client.connectToStation(url, QStringLiteral("definitely-not-the-token"),
                            server.certificateFingerprint());

    QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 15000);
    QVERIFY2(!client.isReconnectPending(),
             "a daemon-refused authentication armed an automatic reconnect");
    QCOMPARE(scheduled.count(), 0);

    QTest::qWait(200);  // many multiples of the shrunk backoff unit
    QCOMPARE(ended.count(), 1);
    QCOMPARE(scheduled.count(), 0);
    QVERIFY(!client.isReconnectPending());

    server.close();
}

QTEST_MAIN(TstSessionLinkLoss)
#include "tst_session_link_loss.moc"
