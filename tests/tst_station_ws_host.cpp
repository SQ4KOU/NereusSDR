// =================================================================
// tests/tst_station_ws_host.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Core WebSocket opening requests (R-IOS-01, R-R3-26), found on the
// operator's live phone test on 2026-09-25: Apple's WebSocket API sends an
// IPv6 URL's Host without brackets ("Host: ::1"), Qt 6.11 cannot build a
// URL from that, and the Core's listener gave such a phone no answer.
// StationOpeningGate now reads every opening request first.
//
// Over real TLS sockets and the Core's real listener:
//
//   - every Host form a client sends (bracketed IPv6 with and without a
//     port, unbracketed IPv6 with and without a port, IPv4 with and
//     without a port, a host name) gets 101; no Host, two, or one that is
//     none of those forms gets 400 and the connection closes; all of it
//     well inside the opening deadline, never silence;
//   - a session opened with "Host: ::1" says hello and signs in like any
//     other;
//   - eight openings that never finish (four that never start TLS, four
//     that never send the request) do not keep a ninth device out: it
//     opens at once and the oldest of the eight is closed; one address
//     holds at most two, and its newest dial gets through;
//   - the status page answers, or refuses, each Host form as its own rule
//     says, and still refuses a name that is not this computer's.
//
// Deadlines are shortened; nothing waits the production 10 s.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QWebSocket>
#include <QWebSocketServer>

#include <memory>

#include "core/AppSettings.h"
#include "core/daemon/StationStatusPage.h"
#include "core/security/CertificateStore.h"
#include "core/session/LinkVersion.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationOpeningGate.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;

namespace {

constexpr int kShortDeadlineMs = 1500;

QByteArray openingRequest(const QByteArray& hostLines)
{
    return QByteArrayLiteral("GET / HTTP/1.1\r\n") + hostLines
           + QByteArrayLiteral("Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                               "Sec-WebSocket-Version: 13\r\n\r\n");
}

// A TLS client that has finished its handshake with the listener.
std::unique_ptr<QSslSocket> tlsClient(quint16 port, const QHostAddress& address = QHostAddress::LocalHost)
{
    auto client = std::make_unique<QSslSocket>();
    QSslConfiguration config = client->sslConfiguration();
    config.setPeerVerifyMode(QSslSocket::VerifyNone);
    client->setSslConfiguration(config);
    client->connectToHostEncrypted(address.toString(), port);
    // Event-driven, not waitForEncrypted(): the listener runs on this
    // thread's event loop and must be served while the client waits.
    if (!QTest::qWaitFor([&]() { return client->isEncrypted(); }, 5000)) {
        return {};
    }
    return client;
}

struct Answer {
    QByteArray statusLine; // empty: nothing came back
    QByteArray rest;       // what followed the head, if anything
    bool closed = false;
    qint64 elapsedMs = 0;
};

// Sends `request` and waits up to `waitMs` for the reply's head. When the
// status is not 101, also waits (inside the same bound) for the close.
Answer sendOpening(QSslSocket& client, const QByteArray& request, int waitMs)
{
    QElapsedTimer timer;
    timer.start();
    client.write(request);
    QByteArray received;
    Answer answer;
    (void)QTest::qWaitFor([&]() {
        received += client.readAll();
        answer.closed = client.state() != QAbstractSocket::ConnectedState;
        return received.contains("\r\n\r\n") || answer.closed;
    }, waitMs);
    const qsizetype end = received.indexOf("\r\n\r\n");
    answer.statusLine = received.left(received.indexOf("\r\n"));
    if (end >= 0) {
        answer.rest = received.mid(end + 4);
    }
    if (!answer.statusLine.startsWith("HTTP/1.1 101")) {
        (void)QTest::qWaitFor([&]() {
            answer.rest += client.readAll();
            return client.state() != QAbstractSocket::ConnectedState;
        }, qMax(1, waitMs - int(timer.elapsed())));
        answer.closed = client.state() != QAbstractSocket::ConnectedState;
    }
    answer.elapsedMs = timer.elapsed();
    return answer;
}

// ── A minimal RFC 6455 client over the raw socket ────────────────────────

void sendTextFrame(QSslSocket& socket, const QByteArray& payload)
{
    QByteArray frame;
    frame.append(char(0x81)); // FIN, text
    const quint64 size = quint64(payload.size());
    if (size < 126) {
        frame.append(char(0x80 | size));
    } else if (size <= 0xFFFF) {
        frame.append(char(0x80 | 126));
        frame.append(char((size >> 8) & 0xFF));
        frame.append(char(size & 0xFF));
    } else {
        frame.append(char(0x80 | 127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(char((size >> shift) & 0xFF));
        }
    }
    const char mask[4] = {0x12, 0x34, 0x56, 0x78};
    frame.append(mask, 4);
    for (qsizetype i = 0; i < payload.size(); ++i) {
        frame.append(char(payload.at(i) ^ mask[i % 4]));
    }
    socket.write(frame);
}

// Takes whole unmasked frames off the front of `buffer`; text messages
// (reassembled from continuations) go to `messages`.
void takeFrames(QByteArray& buffer, QByteArray& partial, QList<QByteArray>& messages)
{
    for (;;) {
        if (buffer.size() < 2) {
            return;
        }
        const quint8 b0 = quint8(buffer.at(0));
        const quint8 b1 = quint8(buffer.at(1));
        quint64 size = b1 & 0x7F;
        qsizetype offset = 2;
        if (size == 126) {
            if (buffer.size() < 4) {
                return;
            }
            size = (quint64(quint8(buffer.at(2))) << 8) | quint8(buffer.at(3));
            offset = 4;
        } else if (size == 127) {
            if (buffer.size() < 10) {
                return;
            }
            size = 0;
            for (int i = 0; i < 8; ++i) {
                size = (size << 8) | quint8(buffer.at(2 + i));
            }
            offset = 10;
        }
        if (quint64(buffer.size() - offset) < size) {
            return;
        }
        const QByteArray payload = buffer.mid(offset, qsizetype(size));
        buffer.remove(0, offset + qsizetype(size));
        const quint8 opcode = b0 & 0x0F;
        if (opcode == 0x1 || opcode == 0x0) {
            partial += payload;
            if ((b0 & 0x80) != 0) {
                messages.append(partial);
                partial.clear();
            }
        }
    }
}

QJsonObject firstOfType(const QList<QByteArray>& messages, const QString& type)
{
    for (const QByteArray& wire : messages) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("type")).toString() == type) {
            return o;
        }
    }
    return {};
}

// One Core on loopback, in scratch directories, with a short deadline.
struct Core {
    QTemporaryDir settingsDir;
    std::unique_ptr<AppSettings> settings;
    RadioModel model;
    std::unique_ptr<StationServer> server;

    explicit Core(const QString& securityDir, int openingDeadlineMs = kShortDeadlineMs)
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("station.settings")));
        server = std::make_unique<StationServer>(
            &model, *settings, NereusSDR::Test::seedUpgradedCoreToken(securityDir));
        server->setOpeningDeadlineMs(openingDeadlineMs);
    }
};

} // namespace

class TstStationWsHost : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_securityDir.isValid());
        AppSettings::setProfileOverride(
            QStringLiteral("ws-host-%1").arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("SettingsSchemaVersion"),
                                         QStringLiteral("1"));
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // ── The forms, without a socket ─────────────────────────────────────

    void canonicalHostTable_data()
    {
        QTest::addColumn<QString>("value");
        QTest::addColumn<QString>("canonical"); // empty: refused
        QTest::newRow("bracketed with port") << "[2001:db8::1]:47910" << "[2001:db8::1]:47910";
        QTest::newRow("bracketed") << "[::1]" << "[::1]";
        QTest::newRow("bare ipv6") << "::1" << "[::1]";
        QTest::newRow("bare ipv6 long") << "2001:db8::1" << "[2001:db8::1]";
        QTest::newRow("bare ipv6 with port") << "2001:db8::1:47910" << "[2001:db8::1]:47910";
        QTest::newRow("bare ipv6 zone") << "fe80::1%en0" << "[fe80::1]";
        QTest::newRow("bracketed zone") << "[fe80::1%en0]:47910" << "[fe80::1]:47910";
        QTest::newRow("ipv4 with port") << "192.0.2.7:47910" << "192.0.2.7:47910";
        QTest::newRow("ipv4") << "192.0.2.7" << "192.0.2.7";
        QTest::newRow("name") << "shack-core.local" << "shack-core.local";
        QTest::newRow("name with port") << "Shack_Core.lan:47910" << "Shack_Core.lan:47910";
        QTest::newRow("spaced") << "  ::1  " << "[::1]";
        QTest::newRow("empty") << "" << "";
        QTest::newRow("garbage") << "a b/c" << "";
        QTest::newRow("user part") << "user@core.local" << "";
        QTest::newRow("path") << "core.local/x" << "";
        QTest::newRow("port too big") << "core.local:99999" << "";
        QTest::newRow("port not a number") << "core.local:ab" << "";
        QTest::newRow("empty port") << "core.local:" << "";
        QTest::newRow("unclosed bracket") << "[::1" << "";
        QTest::newRow("bracketed not ipv6") << "[core.local]" << "";
        QTest::newRow("bracketed ipv4") << "[192.0.2.7]" << "";
        QTest::newRow("after bracket") << "[::1]x" << "";
        QTest::newRow("bare ipv6 bad tail") << "2001:db8::1:zz" << "";
        QTest::newRow("dots") << "core..local" << "";
    }

    void canonicalHostTable()
    {
        QFETCH(QString, value);
        QFETCH(QString, canonical);
        QCOMPARE(StationOpeningGate::canonicalHost(value), canonical);
    }

    void rewriteKeepsEverythingButTheHost()
    {
        const QByteArray head = openingRequest("Host: ::1\r\n");
        const StationOpeningGate::Rewrite rewrite = StationOpeningGate::rewriteRequestHead(head);
        QVERIFY(rewrite.ok);
        QByteArray expected = head;
        expected.replace("Host: ::1\r\n", "Host: [::1]\r\n");
        QCOMPARE(rewrite.head, expected);
        // Not an opening request, or two Hosts: refused.
        QVERIFY(!StationOpeningGate::rewriteRequestHead(
                     QByteArrayLiteral("GET / HTTP/1.1\r\nHost: ::1\r\n\r\n")).ok);
        QVERIFY(!StationOpeningGate::rewriteRequestHead(
                     openingRequest("Host: ::1\r\nHost: [::1]\r\n")).ok);
        QVERIFY(!StationOpeningGate::rewriteRequestHead(
                     openingRequest("Host: ::1\r\n").replace("GET", "PUT")).ok);
        QVERIFY(!StationOpeningGate::rewriteRequestHead(
                     openingRequest("Host: ::1\r\n").replace("dGhlIHNhbXBsZSBub25jZQ==", "short"))
                     .ok);
    }

    // ── The listener ─────────────────────────────────────────────────────

    void everyOpeningIsAnsweredWithinTheDeadline_data()
    {
        QTest::addColumn<QByteArray>("hostLines");
        QTest::addColumn<int>("status");
        QTest::newRow("bracketed with port") << QByteArray("Host: [::1]:47910\r\n") << 101;
        QTest::newRow("bracketed") << QByteArray("Host: [::1]\r\n") << 101;
        QTest::newRow("bare ipv6 with port") << QByteArray("Host: 2001:db8::1:47910\r\n") << 101;
        QTest::newRow("bare ipv6") << QByteArray("Host: ::1\r\n") << 101;
        QTest::newRow("bare ipv6 long") << QByteArray("Host: 2001:db8::1\r\n") << 101;
        QTest::newRow("ipv4 with port") << QByteArray("Host: 127.0.0.1:47910\r\n") << 101;
        QTest::newRow("ipv4") << QByteArray("Host: 127.0.0.1\r\n") << 101;
        QTest::newRow("name") << QByteArray("Host: shack-core.local\r\n") << 101;
        QTest::newRow("name with port") << QByteArray("Host: shack-core.local:47910\r\n") << 101;
        QTest::newRow("no host") << QByteArray() << 400;
        QTest::newRow("two hosts") << QByteArray("Host: ::1\r\nHost: [::1]\r\n") << 400;
        QTest::newRow("garbage") << QByteArray("Host: a b/c\r\n") << 400;
        QTest::newRow("user part") << QByteArray("Host: user@core.local\r\n") << 400;
        QTest::newRow("empty") << QByteArray("Host: \r\n") << 400;
    }

    void everyOpeningIsAnsweredWithinTheDeadline()
    {
        QFETCH(QByteArray, hostLines);
        QFETCH(int, status);
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path());
        QVERIFY2(core.server->listen(QHostAddress::LocalHost, 0),
                 qPrintable(core.server->lastError()));
        auto client = tlsClient(core.server->serverPort());
        QVERIFY(client);
        const Answer answer = sendOpening(*client, openingRequest(hostLines), kShortDeadlineMs);
        QVERIFY2(!answer.statusLine.isEmpty(), "no answer at all");
        QVERIFY2(answer.statusLine.startsWith("HTTP/1.1 " + QByteArray::number(status)),
                 answer.statusLine.constData());
        QVERIFY(answer.elapsedMs < kShortDeadlineMs);
        if (status == 101) {
            QTRY_COMPARE(core.server->peerCount(), 1);
        } else {
            QVERIFY2(answer.closed, "a refused request must also be closed");
            QVERIFY(answer.rest.contains("could not read"));
            QCOMPARE(core.server->peerCount(), 0);
        }
        QTRY_COMPARE(core.server->openingCount(), 0);
    }

    void aVersionTheCoreDoesNotSpeakIsRefusedByQtIn400()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path());
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        auto client = tlsClient(core.server->serverPort());
        QVERIFY(client);
        const Answer answer = sendOpening(
            *client,
            openingRequest("Host: ::1\r\n").replace("Sec-WebSocket-Version: 13",
                                                    "Sec-WebSocket-Version: 8"),
            kShortDeadlineMs);
        QVERIFY2(answer.statusLine.startsWith("HTTP/1.1 400"), answer.statusLine.constData());
        QVERIFY(answer.closed);
        QCOMPARE(core.server->peerCount(), 0);
        QTRY_COMPARE(core.server->openingCount(), 0);
    }

    void anUnfinishedRequestIsClosedAtTheDeadline()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path());
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        auto client = tlsClient(core.server->serverPort());
        QVERIFY(client);
        QElapsedTimer timer;
        timer.start();
        client->write("GET / HTTP/1.1\r\nHost: ::1\r\n"); // never finished
        QTRY_COMPARE_WITH_TIMEOUT(core.server->openingCount(), 1, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(client->state() != QAbstractSocket::ConnectedState,
                                 kShortDeadlineMs + 1500);
        QVERIFY(timer.elapsed() >= kShortDeadlineMs - 100);
        QCOMPARE(core.server->openingCount(), 0);
    }

    void aSessionOpenedWithTheBareIpv6FormSignsIn()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path());
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        auto client = tlsClient(core.server->serverPort());
        QVERIFY(client);
        // Exactly what NWProtocolWebSocket sends for ws://[::1]:port/.
        const Answer answer = sendOpening(*client, openingRequest("Host: ::1\r\n"), kShortDeadlineMs);
        QVERIFY2(answer.statusLine.startsWith("HTTP/1.1 101"), answer.statusLine.constData());

        QByteArray buffer = answer.rest;
        QByteArray partial;
        QList<QByteArray> messages;
        auto pump = [&]() {
            buffer += client->readAll();
            takeFrames(buffer, partial, messages);
        };

        sendTextFrame(*client, SessionMessages::encode(SessionMessages::hello(
                                   kSessionProtocolMajor, kSessionProtocolMinor, 0,
                                   QStringLiteral("NereusSDR iPhone"))));
        sendTextFrame(*client,
                      SessionMessages::encode(SessionMessages::authRequest(core.server->token())));
        QTRY_VERIFY((pump(), !firstOfType(messages, QStringLiteral("auth.result")).isEmpty()));
        QVERIFY(firstOfType(messages, QStringLiteral("auth.result"))
                    .value(QStringLiteral("accepted"))
                    .toBool());
        QVERIFY(!firstOfType(messages, QStringLiteral("hello")).isEmpty());
        QVERIFY(core.server->hasAuthenticatedSession());
    }

    void eightUnfinishedOpeningsDoNotKeepANinthDeviceOut()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        // The gate as the station builds it, with the station's limits
        // and certificate, but every connection counted as its own
        // address: loopback here is one address, and the per-address
        // limit is the next slot's subject.
        Core core(m_securityDir.path(), /*openingDeadlineMs=*/60000);
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QWebSocketServer target(QStringLiteral("test"), QWebSocketServer::SecureMode);
        target.setSslConfiguration(core.server->tlsConfiguration());
        int opened = 0;
        StationOpeningGate* gate = nullptr;
        connect(&target, &QWebSocketServer::newConnection, this, [&]() {
            while (QWebSocket* socket = target.nextPendingConnection()) {
                gate->markOpened(socket);
                ++opened;
            }
        });
        StationOpeningGate realGate(&target, StationServer::kMaxConcurrentPeers,
                                    StationServer::kMaxHandshakesPerAddress,
                                    [](const QString&) { return QString(); });
        gate = &realGate;
        realGate.setTlsConfiguration(core.server->tlsConfiguration());
        // Far longer than the test: the device must get in without anyone
        // waiting for the deadline.
        realGate.setOpeningDeadlineMs(60000);
        QVERIFY(realGate.listen(QHostAddress::LocalHost, 0));
        const quint16 port = realGate.serverPort();

        // Four that never start TLS, then four that finish TLS and never
        // ask. The oldest is the first silent one.
        std::vector<std::unique_ptr<QTcpSocket>> silent;
        for (int i = 0; i < 4; ++i) {
            auto socket = std::make_unique<QTcpSocket>();
            socket->connectToHost(QHostAddress::LocalHost, port);
            QVERIFY(QTest::qWaitFor([&]() { return socket->state() == QAbstractSocket::ConnectedState; }, 2000));
            QTRY_COMPARE(realGate.pendingCount(), i + 1);
            silent.push_back(std::move(socket));
        }
        std::vector<std::unique_ptr<QSslSocket>> mute;
        for (int i = 0; i < 4; ++i) {
            auto socket = tlsClient(port);
            QVERIFY(socket);
            mute.push_back(std::move(socket));
        }
        QTRY_COMPARE(realGate.pendingCount(), StationServer::kMaxConcurrentPeers);

        // The ninth, a real device, opens at once, and the oldest of the
        // eight is the one closed to make room.
        QElapsedTimer timer;
        timer.start();
        auto device = tlsClient(port);
        QVERIFY(device);
        const Answer answer = sendOpening(*device, openingRequest("Host: ::1\r\n"), 5000);
        QVERIFY2(answer.statusLine.startsWith("HTTP/1.1 101"), answer.statusLine.constData());
        QTRY_COMPARE(opened, 1);
        QVERIFY(timer.elapsed() < 5000);
        QTRY_VERIFY(silent.front()->state() != QAbstractSocket::ConnectedState);
        for (std::size_t i = 1; i < silent.size(); ++i) {
            QCOMPARE(silent.at(i)->state(), QAbstractSocket::ConnectedState);
        }
        QCOMPARE(realGate.pendingCount(), StationServer::kMaxConcurrentPeers - 1);
    }

    void oneAddressHoldsAtMostTwoOpeningsAndItsNewestGetsThrough()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path(), /*openingDeadlineMs=*/60000);
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        const quint16 port = core.server->serverPort();
        std::vector<std::unique_ptr<QSslSocket>> mute;
        for (int i = 0; i < StationServer::kMaxHandshakesPerAddress; ++i) {
            auto socket = tlsClient(port);
            QVERIFY(socket);
            mute.push_back(std::move(socket));
        }
        QTRY_COMPARE(core.server->openingCount(), StationServer::kMaxHandshakesPerAddress);
        // A third from the same address pushes the oldest out and opens.
        auto device = tlsClient(port);
        QVERIFY(device);
        QTRY_VERIFY(mute.front()->state() != QAbstractSocket::ConnectedState);
        QCOMPARE(mute.back()->state(), QAbstractSocket::ConnectedState);
        QCOMPARE(core.server->openingCount(), StationServer::kMaxHandshakesPerAddress);
        const Answer answer = sendOpening(*device, openingRequest("Host: ::1\r\n"), 5000);
        QVERIFY2(answer.statusLine.startsWith("HTTP/1.1 101"), answer.statusLine.constData());
        QTRY_COMPARE(core.server->peerCount(), 1);
        QCOMPARE(core.server->openingCount(), 1);
    }

    void theListenerClosesItsOpeningsWhenItCloses()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("Qt reports no working TLS backend on this machine");
        }
        Core core(m_securityDir.path(), /*openingDeadlineMs=*/60000);
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        auto client = tlsClient(core.server->serverPort());
        QVERIFY(client);
        QTRY_COMPARE(core.server->openingCount(), 1);
        core.server->close();
        QCOMPARE(core.server->openingCount(), 0);
        QVERIFY(!core.server->isListening());
        QTRY_VERIFY(client->state() != QAbstractSocket::ConnectedState);
    }

    // ── The status page ──────────────────────────────────────────────────

    void theStatusPageAnswersEachHostFormByItsOwnRule_data()
    {
        QTest::addColumn<QByteArray>("hostLines");
        QTest::addColumn<int>("status");
        QTest::newRow("bracketed with port") << QByteArray("Host: [::1]:47911\r\n") << 200;
        QTest::newRow("bracketed") << QByteArray("Host: [::1]\r\n") << 200;
        QTest::newRow("bare ipv6") << QByteArray("Host: ::1\r\n") << 200;
        QTest::newRow("bare ipv6 long") << QByteArray("Host: 2001:db8::1\r\n") << 200;
        // Refused, not answered: an unbracketed IPv6 address with a port is
        // not one of the page's forms (no browser sends it), and a 404
        // fails closed.
        QTest::newRow("bare ipv6 with port") << QByteArray("Host: 2001:db8::1:47911\r\n") << 404;
        QTest::newRow("ipv4 with port") << QByteArray("Host: 127.0.0.1:47911\r\n") << 200;
        QTest::newRow("ipv4") << QByteArray("Host: 127.0.0.1\r\n") << 200;
        QTest::newRow("foreign name") << QByteArray("Host: nereus.attacker.example\r\n") << 404;
        QTest::newRow("no host") << QByteArray() << 200;
        QTest::newRow("garbage") << QByteArray("Host: a b/c\r\n") << 404;
    }

    void theStatusPageAnswersEachHostFormByItsOwnRule()
    {
        QFETCH(QByteArray, hostLines);
        QFETCH(int, status);
        StationStatusPage::Sources sources;
        sources.label = []() { return QStringLiteral("Shack Core"); };
        StationStatusPage page(std::move(sources));
        QVERIFY(page.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, page.serverPort());
        QVERIFY(socket.waitForConnected(2000));
        socket.write("GET / HTTP/1.1\r\n" + hostLines + "\r\n");
        QByteArray received;
        QTRY_VERIFY((received += socket.readAll(), received.contains("\r\n")));
        QVERIFY2(received.startsWith("HTTP/1.1 " + QByteArray::number(status)),
                 received.left(received.indexOf("\r\n")).constData());
    }

private:
    QTemporaryDir m_securityDir;
};

QTEST_MAIN(TstStationWsHost)
#include "tst_station_ws_host.moc"
