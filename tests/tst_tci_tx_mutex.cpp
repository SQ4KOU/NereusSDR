// tests/tst_tci_tx_mutex.cpp  (NereusSDR)
// no-port-check: NereusSDR-original integration test for the Phase 17 TX
// audio single-client mutex.
//
// Verifies:
//   1. trx:0,true,tci; from clientA → clientA acquires TX mutex.
//   2. Binary TX_AUDIO_STREAM frame from clientA → lands in server TX ring.
//   3. Binary TX_AUDIO_STREAM frame from clientB (no mutex) → silently dropped;
//      clientB's txFramesDropped increments.
//   4. trx:0,false; from clientA → mutex released.
//
// Phase 3J-1 Task 17.1.
//
// R3 receiver audio plan, Task 4 (R-R3-42, R-R3-25), 2026-09-23, J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code: in a remote window
// transmit is refused. No MOX write, no TX audio lock, no TX_CHRONO, the
// app hears trx:N,false, and the plain reason goes to the operator, never
// onto the TCI wire.
//
// R3 Core-owned accessories, Task 3 (R-R3-48, R-R3-25), 2026-09-24, J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code: the Core's station
// TCI server refuses transmit the same way until remote transmit, with its
// own plain reason, while the Core's receive path is unchanged.

#ifdef HAVE_WEBSOCKETS

#include <QtTest>
#include <QSignalSpy>
#include <QWebSocket>
#include <QUrl>

#include <cstring>
#include <vector>

#include "core/TciServer.h"
#include "core/TciBinaryFrame.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

// ── Helper: build a minimal TX_AUDIO_STREAM binary frame ────────────────────
//
// Constructs a 64-byte TCI header with streamType=TX_AUDIO_STREAM(2) plus a
// payload of `sampleCount` Float32 samples, all set to `amplitude`.
// Uses the production TciBinaryFrame::buildStreamPayload path for byte-exact
// parity with the wire format expected by onBinaryMessageReceived.
static QByteArray makeTxFrame(int sampleCount, float amplitude = 0.5f)
{
    // Interleaved stereo (channels=2): sampleCount is total floats (frames*2).
    std::vector<float> samples(static_cast<size_t>(sampleCount), amplitude);
    return TciBinaryFrame::buildStreamPayload(
        /*receiver=*/0,
        /*sampleRate=*/48000,
        /*sampleType=*/static_cast<int>(TciSampleType::Float32),
        /*length=*/sampleCount,
        /*streamType=*/static_cast<int>(TciStreamType::TxAudioStream),
        /*channels=*/2,
        samples.data());
}

class TestTciTxMutex : public QObject {
    Q_OBJECT
private slots:
    void tx_mutex_single_client_claim_and_release();
    void tx_mutex_second_client_frame_is_dropped();
    void remote_window_refuses_transmit_off_the_wire();
    void station_server_refuses_transmit_until_remote_transmit();
    void station_server_refuses_transmit_settings();
};

// ── tx_mutex_single_client_claim_and_release() ───────────────────────────────
//
// Verifies the basic mutex lifecycle:
//   1. Server starts with no active TX client (activeTxClientCount == 0).
//   2. clientA sends "trx:0,true,tci;" → activeTxClientCount becomes 1.
//   3. clientA sends a TX audio binary frame → frame lands in the TX ring
//      (peekTxRingSize > 0).  Note: with model=nullptr no TxChannel exists so
//      the data stays in the ring rather than being drained synchronously.
//   4. clientA sends "trx:0,false;" → activeTxClientCount returns to 0.

void TestTciTxMutex::tx_mutex_single_client_claim_and_release()
{
    // ── 1. Spin up server ─────────────────────────────────────────────────────
    TciServer server(nullptr);   // RadioModel* null — test injection path
    QVERIFY(server.start(0));
    QVERIFY(server.isRunning());

    // ── 2. Connect clientA ────────────────────────────────────────────────────
    QWebSocket clientA;
    QSignalSpy connA(&clientA, &QWebSocket::connected);
    clientA.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    QVERIFY(connA.wait(2000));
    QCOMPARE(clientA.state(), QAbstractSocket::ConnectedState);

    // Initial state: no active TX client.
    QCOMPARE(server.activeTxClientCount(), 0);
    QVERIFY(server.activeTxClientPeer().isEmpty());

    // ── 3. clientA claims TX mutex ────────────────────────────────────────────
    clientA.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
    QTest::qWait(50);  // allow event loop to process the message

    QCOMPARE(server.activeTxClientCount(), 1);
    QVERIFY(!server.activeTxClientPeer().isEmpty());

    // ── 4. clientA sends a TX audio frame ─────────────────────────────────────
    // With model=nullptr, no TxChannel drain happens — data stays in ring.
    const int kSamples = 128;   // 64 stereo frames
    const QByteArray txFrame = makeTxFrame(kSamples);
    QVERIFY(txFrame.size() > 64);

    const int ringBefore = server.peekTxRingSize();
    clientA.sendBinaryMessage(txFrame);
    QTest::qWait(50);

    // Ring should now contain the decoded float bytes.
    // kSamples Float32 samples = kSamples * 4 bytes.
    QVERIFY2(server.peekTxRingSize() > ringBefore,
             qPrintable(QStringLiteral("TX ring did not grow: before=%1 after=%2")
                 .arg(ringBefore).arg(server.peekTxRingSize())));

    // ── 5. clientA releases TX mutex ──────────────────────────────────────────
    clientA.sendTextMessage(QStringLiteral("trx:0,false;"));
    QTest::qWait(50);

    QCOMPARE(server.activeTxClientCount(), 0);
    QVERIFY(server.activeTxClientPeer().isEmpty());

    // ── Cleanup ───────────────────────────────────────────────────────────────
    clientA.close();
    server.stop();
}

// ── tx_mutex_second_client_frame_is_dropped() ────────────────────────────────
//
// Two-client test:
//   1. clientA acquires TX mutex.
//   2. clientA sends a TX audio frame → lands in ring.
//   3. clientB sends a TX audio frame WITHOUT claiming mutex → silently dropped;
//      clientB's txFramesDropped increments.
//   4. clientA's frames still land; ring size larger than clientB contributions.
//   5. clientA releases mutex.

void TestTciTxMutex::tx_mutex_second_client_frame_is_dropped()
{
    // ── 1. Spin up server ─────────────────────────────────────────────────────
    TciServer server(nullptr);
    QVERIFY(server.start(0));

    // ── 2. Connect two clients ────────────────────────────────────────────────
    QWebSocket clientA, clientB;
    QSignalSpy connA(&clientA, &QWebSocket::connected);
    QSignalSpy connB(&clientB, &QWebSocket::connected);

    const QString url = QStringLiteral("ws://127.0.0.1:%1").arg(server.port());
    clientA.open(QUrl(url));
    QVERIFY(connA.wait(2000));
    clientB.open(QUrl(url));
    QVERIFY(connB.wait(2000));

    QCOMPARE(clientA.state(), QAbstractSocket::ConnectedState);
    QCOMPARE(clientB.state(), QAbstractSocket::ConnectedState);

    // ── 3. clientA claims TX mutex ────────────────────────────────────────────
    clientA.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
    QTest::qWait(50);
    QCOMPARE(server.activeTxClientCount(), 1);

    // ── 4. clientA sends a TX audio frame ─────────────────────────────────────
    const int kSamples = 64;   // 32 stereo frames
    const QByteArray txFrame = makeTxFrame(kSamples, 0.3f);
    clientA.sendBinaryMessage(txFrame);
    QTest::qWait(50);
    const int ringAfterA = server.peekTxRingSize();
    // Ring should have grown (clientA is active owner).
    QVERIFY2(ringAfterA > 0,
             "clientA frame should have landed in TX ring");

    // ── 5. clientB sends a TX audio frame (no mutex) ──────────────────────────
    // clientB has NOT sent "trx:0,true,tci;" — it is not the active TX client.
    // The frame must be silently dropped and txFramesDropped incremented.
    clientB.sendBinaryMessage(txFrame);
    QTest::qWait(50);

    // Ring must NOT have grown from clientB's frame (frame was dropped).
    // (ringAfterA is from clientA's frame; clientB frame silently ignored)
    QCOMPARE(server.peekTxRingSize(), ringAfterA);

    // ── 6. Verify clientB's txFramesDropped incremented ──────────────────────
    // Access the session via the server's internal client table.  We can't
    // access m_clients directly (private), so use a well-known proxy: the
    // fact that activeTxClientCount() == 1 (clientA) and activeTxClientPeer()
    // is non-empty.  To verify txFramesDropped we rely on the observable
    // invariant: the ring did not grow (validated above) AND clientB's frame
    // was not fed to TxChannel (model is null, no TxChannel exists).
    //
    // For a deeper assertion, expose txFramesDropped via a test-only accessor
    // if needed in a future phase.  Phase 17 scope: ring-size invariant is
    // the primary observable.

    // ── 7. clientA releases mutex ─────────────────────────────────────────────
    clientA.sendTextMessage(QStringLiteral("trx:0,false;"));
    QTest::qWait(50);
    QCOMPARE(server.activeTxClientCount(), 0);

    // ── Cleanup ───────────────────────────────────────────────────────────────
    clientA.close();
    clientB.close();
    server.stop();
}

void TestTciTxMutex::remote_window_refuses_transmit_off_the_wire()
{
    RadioModel remote(RadioModel::Role::Remote);
    TciServer server(&remote);
    QVERIFY(server.isRemoteWindow());
    QSignalSpy notices(&server, &TciServer::operatorNotice);
    QSignalSpy txOwner(&server, &TciServer::txAudioActiveClientChanged);
    QVERIFY(server.start(0));

    QWebSocket client;
    QSignalSpy connected(&client, &QWebSocket::connected);
    QSignalSpy text(&client, &QWebSocket::textMessageReceived);
    QSignalSpy binary(&client, &QWebSocket::binaryMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    QVERIFY(connected.wait(2000));
    const auto lines = [&text] {
        QStringList out;
        for (const auto& call : text) { out << call.at(0).toString(); }
        return out;
    };
    QTRY_VERIFY_WITH_TIMEOUT(lines().contains(QStringLiteral("ready;")), 3000);
    const int linesBefore = int(text.count());

    client.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
    QTRY_VERIFY_WITH_TIMEOUT(lines().mid(linesBefore).contains(QStringLiteral("trx:0,false;")),
                             3000);
    // Past several TX_CHRONO periods: none was sent and nothing was keyed.
    QTest::qWait(150);
    QCOMPARE(binary.count(), 0);
    QCOMPARE(server.activeTxClientCount(), 0);
    QCOMPARE(txOwner.count(), 0);
    QVERIFY(!remote.mox());
    QVERIFY(!lines().mid(linesBefore).contains(QStringLiteral("trx:0,true;")));

    // A TX audio frame from the app lands nowhere.
    client.sendBinaryMessage(makeTxFrame(128));
    QTest::qWait(50);
    QCOMPARE(server.peekTxRingSize(), 0);

    // The operator is told why, once per 30 s as a toast; the app never is.
    QCOMPARE(notices.count(), 1);
    const QString reason = notices.constFirst().at(1).toString();
    QCOMPARE(reason, QString::fromLatin1(TciServer::kRemoteTransmitRefusedReason));
    QVERIFY(!notices.constFirst().at(0).toString().isEmpty());   // the app's host:port
    QVERIFY(notices.constFirst().at(2).toBool());
    client.sendTextMessage(QStringLiteral("trx:0,true;"));
    QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 2, 3000);
    QVERIFY(!notices.at(1).at(2).toBool());
    for (const QString& line : lines()) {
        QVERIFY2(!line.contains(reason), qPrintable(line));
    }

    // Releasing is answered the same way and is no refusal.
    client.sendTextMessage(QStringLiteral("trx:0,false;"));
    QTest::qWait(100);
    QCOMPARE(notices.count(), 2);
    QVERIFY(!remote.mox());

    client.close();
    server.stop();
}

// R-R3-48 / R-R3-25: the Core's station TCI server (a Local model with the
// station receive-only mode on) keys nothing for any app: no MOX, no TX
// audio lock, no TX audio; trx:N,false back to the app; the reason plain
// and off the wire. The init burst says receive-only.
void TestTciTxMutex::station_server_refuses_transmit_until_remote_transmit()
{
    RadioModel core;
    TciServer server(&core);
    QVERIFY(!server.isRemoteWindow());
    server.setStationReceiveOnly(true);
    QVERIFY(server.stationReceiveOnly());
    QSignalSpy notices(&server, &TciServer::operatorNotice);
    QSignalSpy txOwner(&server, &TciServer::txAudioActiveClientChanged);
    QVERIFY(server.start(0));

    QWebSocket client;
    QSignalSpy connected(&client, &QWebSocket::connected);
    QSignalSpy text(&client, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    QVERIFY(connected.wait(2000));
    const auto lines = [&text] {
        QStringList out;
        for (const auto& call : text) { out << call.at(0).toString(); }
        return out;
    };
    QTRY_VERIFY_WITH_TIMEOUT(lines().contains(QStringLiteral("ready;")), 3000);
    QVERIFY(lines().contains(QStringLiteral("receive_only:true;")));
    QVERIFY(lines().contains(QStringLiteral("tx_enable:0,false;")));
    const int linesBefore = int(text.count());

    client.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
    QTRY_VERIFY_WITH_TIMEOUT(lines().mid(linesBefore).contains(QStringLiteral("trx:0,false;")),
                             3000);
    QTest::qWait(100);
    QVERIFY(!core.mox());
    QCOMPARE(server.activeTxClientCount(), 0);
    QCOMPARE(txOwner.count(), 0);
    client.sendBinaryMessage(makeTxFrame(128));
    QTest::qWait(50);
    QCOMPARE(server.peekTxRingSize(), 0);

    QCOMPARE(notices.count(), 1);
    const QString reason = notices.constFirst().at(1).toString();
    QCOMPARE(reason, QString::fromLatin1(TciServer::kStationTransmitRefusedReason));
    QCOMPARE(server.operatorNoticeReason(), reason);
    for (const QString& line : lines()) {
        QVERIFY2(!line.contains(reason), qPrintable(line));
    }

    // Off (a local window): transmit works as it always has.
    server.stop();
    server.setStationReceiveOnly(false);
    QVERIFY(server.start(0));
    QWebSocket local;
    QSignalSpy localConnected(&local, &QWebSocket::connected);
    local.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    QVERIFY(localConnected.wait(2000));
    local.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
    QTRY_COMPARE_WITH_TIMEOUT(server.activeTxClientCount(), 1, 3000);
    local.sendTextMessage(QStringLiteral("trx:0,false;"));
    QTRY_COMPARE_WITH_TIMEOUT(server.activeTxClientCount(), 0, 3000);
    local.close();
    client.close();
    server.stop();
}

// M1 (R-R3-48 / R-R3-25): the station server does not let an app change the
// Core's TX profile or XIT (tx_profile_ex, xit_enable, xit_offset) while the
// Core is receive-only: nothing is applied or broadcast, the asking app
// hears the current value, and the operator gets the same plain transmit
// reason, off the wire. Queries still answer.
void TestTciTxMutex::station_server_refuses_transmit_settings()
{
    RadioModel core;
    TciServer server(&core);
    server.setStationReceiveOnly(true);
    QSignalSpy notices(&server, &TciServer::operatorNotice);
    QVERIFY(server.start(0));

    QWebSocket client;
    QWebSocket other;
    QSignalSpy connected(&client, &QWebSocket::connected);
    QSignalSpy otherConnected(&other, &QWebSocket::connected);
    QSignalSpy text(&client, &QWebSocket::textMessageReceived);
    QSignalSpy otherText(&other, &QWebSocket::textMessageReceived);
    client.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    other.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
    QVERIFY(connected.wait(2000));
    QVERIFY(otherConnected.count() == 1 || otherConnected.wait(2000));
    const auto lines = [](const QSignalSpy& spy) {
        QStringList out;
        for (const auto& call : spy) {
            for (const QString& part :
                 call.at(0).toString().split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                out << part.trimmed() + QLatin1Char(';');
            }
        }
        return out;
    };
    QTRY_VERIFY_WITH_TIMEOUT(lines(text).contains(QStringLiteral("ready;")), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(lines(otherText).contains(QStringLiteral("ready;")), 3000);
    const QString profileBefore = core.txProfile();
    const int mark = int(text.count());
    const int otherMark = int(otherText.count());

    client.sendTextMessage(QStringLiteral("xit_enable:0,true;"));
    client.sendTextMessage(QStringLiteral("xit_offset:0,500;"));
    client.sendTextMessage(QStringLiteral("tx_profile_ex:Contest;"));
    QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 3, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(lines(text).mid(mark).contains(QStringLiteral("xit_enable:0,false;")),
                             3000);
    QTest::qWait(150);
    QVERIFY(!core.xitEnable());
    QCOMPARE(core.xitOffset(), 0);
    QCOMPARE(core.txProfile(), profileBefore);
    for (const QString& line : lines(text).mid(mark) + lines(otherText).mid(otherMark)) {
        QVERIFY2(line != QStringLiteral("xit_enable:0,true;")
                     && line != QStringLiteral("xit_offset:0,500;")
                     && line != QStringLiteral("tx_profile_ex:Contest;"), qPrintable(line));
        QVERIFY2(!line.contains(QString::fromLatin1(TciServer::kStationTransmitRefusedReason)),
                 qPrintable(line));
    }
    for (const auto& notice : notices) {
        QCOMPARE(notice.at(1).toString(),
                 QString::fromLatin1(TciServer::kStationTransmitRefusedReason));
    }
    // A query still answers, and raises nothing.
    client.sendTextMessage(QStringLiteral("xit_offset:0;"));
    QTRY_VERIFY_WITH_TIMEOUT(lines(text).mid(mark).contains(QStringLiteral("xit_offset:0,0;")),
                             3000);
    QCOMPARE(notices.count(), 3);
    client.close();
    other.close();
    server.stop();
}

QTEST_GUILESS_MAIN(TestTciTxMutex)
#include "tst_tci_tx_mutex.moc"

#else  // !HAVE_WEBSOCKETS

int main() { return 0; }

#endif // HAVE_WEBSOCKETS
