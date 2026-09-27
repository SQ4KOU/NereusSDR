// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_relay_leg.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 29 step 2b (R-IOS-16, R-IOS-08; the rendezvous
// document, section 12.8): the Core's relay-leg runner. It plays the relay
// towards a RelayLeg on the connection named `core` of every fixture in
// rendezvous/conformance/v1/relay/ whose `runs` holds "core", with the
// leg's two ICE agents played by loopback UDP sockets behind its shim, and
// checks the leg's own rules besides: the lanes' queues drop their oldest,
// a datagram over 1500 bytes is never sent, each END code's words are
// plain, and a connection's candidate source claims and lets go its lane.
//
// Loopback only; nothing leaves this computer.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QPointer>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QUdpSocket>
#include <QWebSocket>
#include <QWebSocketServer>

#include <memory>

#include "OperatorWording.h"
#include "core/security/StationIdentity.h"
#include "core/session/RelayLeg.h"

using namespace NereusSDR;

namespace {

const QString kSuite = QStringLiteral(NEREUS_SOURCE_DIR "/rendezvous/conformance/v1/relay");

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    for (int i = 0; i < count; ++i) {
        bytes[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }
    return bytes;
}

// One connection the runner accepted from the leg, with what it received.
struct Accepted {
    QPointer<QWebSocket> socket;
    QList<QByteArray> received;
    bool endSent = false;
};

// Plays the relay towards one leg (section 12.8's core runner).
class RelayPlayer {
public:
    RelayPlayer()
    {
        server = std::make_unique<QWebSocketServer>(QStringLiteral("relay"),
                                                    QWebSocketServer::NonSecureMode);
        server->listen(QHostAddress::LocalHost, 0);
        QObject::connect(server.get(), &QWebSocketServer::newConnection, server.get(), [this] {
            while (QWebSocket* socket = server->nextPendingConnection()) {
                auto accepted = std::make_shared<Accepted>();
                accepted->socket = socket;
                QObject::connect(socket, &QWebSocket::binaryMessageReceived, socket,
                                 [accepted](const QByteArray& message) {
                                     accepted->received.append(message);
                                 });
                pending.append(accepted);
            }
        });
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1/v1/relay").arg(server->serverPort()));
    }

    std::shared_ptr<Accepted> waitForConnection(int ms = 8000)
    {
        if (!QTest::qWaitFor([this] { return !pending.isEmpty(); }, ms)) {
            return nullptr;
        }
        return pending.takeFirst();
    }

    std::unique_ptr<QWebSocketServer> server;
    QList<std::shared_ptr<Accepted>> pending;
};

// A token for a placeholder key, the same every time (section 12.8).
QString tokenFor(QHash<QString, QString>& tokens, const QString& key)
{
    auto it = tokens.find(key);
    if (it == tokens.end()) {
        it = tokens.insert(key, StationIdentity::toBase64Url(randomBytes(62)));
    }
    return it.value();
}

// The bytes of a step's `binary` parts, filling placeholders; `sent`
// records `$bytes` values made here.
QByteArray bytesOf(const QJsonArray& parts, QHash<QString, QString>& tokens,
                   QHash<QString, QByteArray>& recorded)
{
    QByteArray out;
    for (const QJsonValue& value : parts) {
        const QString part = value.toString();
        if (part.startsWith(QLatin1String("$token:"))) {
            out.append(tokenFor(tokens, part.mid(7)).toLatin1());
        } else if (part.startsWith(QLatin1String("$bytes:"))) {
            const QStringList fields = part.split(QLatin1Char(':'));
            const QString name = fields.value(2);
            if (!recorded.contains(name)) {
                recorded.insert(name, randomBytes(fields.value(1).toInt()));
            }
            out.append(recorded.value(name));
        } else if (part.startsWith(QLatin1String("$ref:"))) {
            out.append(recorded.value(part.mid(5)));
        } else {
            out.append(QByteArray::fromHex(part.toLatin1()));
        }
    }
    return out;
}

} // namespace

class TstRelayLeg final : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void coreFixtures_data()
    {
        QTest::addColumn<QString>("file");
        const QJsonArray fixtures =
            readJson(kSuite + QStringLiteral("/manifest.json")).value(QStringLiteral("fixtures")).toArray();
        int count = 0;
        for (const QJsonValue& entry : fixtures) {
            const QString file = entry.toObject().value(QStringLiteral("file")).toString();
            const QJsonArray runs = readJson(kSuite + QLatin1Char('/') + file)
                                        .value(QStringLiteral("runs"))
                                        .toArray();
            if (runs.contains(QJsonValue(QStringLiteral("core")))) {
                QTest::newRow(qPrintable(file)) << file;
                ++count;
            }
        }
        // Every fixture the manifest lists for a Core's leg runs (19 in
        // frame version 1).
        QVERIFY2(count >= 19, qPrintable(QString::number(count)));
    }

    void coreFixtures()
    {
        QFETCH(QString, file);
        const QJsonObject fixture = readJson(kSuite + QLatin1Char('/') + file);
        const QJsonArray steps = fixture.value(QStringLiteral("steps")).toArray();

        RelayPlayer relay;
        QVERIFY(relay.server->isListening());
        RelayLeg leg;
        QVERIFY(leg.bindLanes());
        // The leg's two ICE agents.
        QUdpSocket agents[2];
        for (int lane = 0; lane < 2; ++lane) {
            QVERIFY(agents[lane].bind(QHostAddress::LocalHost, 0));
            leg.setAgentForTest(lane + 1, QHostAddress::LocalHost, agents[lane].localPort());
        }
        QHash<QString, QString> tokens;
        QHash<QString, QByteArray> recorded;
        // The grant: the first token of the leg's own connection.
        QString grantKey;
        for (const QJsonValue& value : steps) {
            const QJsonObject step = value.toObject();
            if (step.value(QStringLiteral("from")).toString() == QLatin1String("core")) {
                for (const QJsonValue& part : step.value(QStringLiteral("binary")).toArray()) {
                    if (part.toString().startsWith(QLatin1String("$token:"))) {
                        grantKey = part.toString().mid(7);
                        break;
                    }
                }
            }
            if (!grantKey.isEmpty()) {
                break;
            }
        }
        if (grantKey.isEmpty()) {
            grantKey = QStringLiteral("core:s:a");
        }
        leg.open(relay.url(), tokenFor(tokens, grantKey));

        std::shared_ptr<Accepted> current;
        int read = 0; // messages of `current` already matched
        const auto nextMessage = [&current, &read](QByteArray* out) {
            if (!current) {
                return false;
            }
            if (!QTest::qWaitFor([&current, &read] { return current->received.size() > read; },
                                 5000)) {
                return false;
            }
            *out = current->received.at(read++);
            return true;
        };
        for (int index = 0; index < steps.size(); ++index) {
            const QJsonObject step = steps.at(index).toObject();
            const QString where = QStringLiteral("%1 step %2").arg(file).arg(index);
            if (step.contains(QStringLiteral("connect"))) {
                if (step.value(QStringLiteral("connect")).toString() != QLatin1String("core")) {
                    continue;
                }
                current = relay.waitForConnection();
                QVERIFY2(current, qPrintable(where + QStringLiteral(": the leg did not connect")));
                read = 0;
                continue;
            }
            if (step.contains(QStringLiteral("from"))) {
                const QString from = step.value(QStringLiteral("from")).toString();
                const QByteArray bytes =
                    bytesOf(step.value(QStringLiteral("binary")).toArray(), tokens, recorded);
                if (from != QLatin1String("core")
                    || step.value(QStringLiteral("role")).toString() != QLatin1String("behaviour")) {
                    continue; // another connection's, or not the leg's to send
                }
                const auto tag = static_cast<quint8>(bytes.at(0));
                if (tag == RelayLeg::kTagControl || tag == RelayLeg::kTagMedia) {
                    // The runner makes the leg send it: the agent writes the
                    // payload into the lane's socket.
                    agents[tag - 1].writeDatagram(bytes.mid(1), QHostAddress::LocalHost,
                                                  leg.lanePort(tag));
                }
                QByteArray message;
                QVERIFY2(nextMessage(&message),
                         qPrintable(where + QStringLiteral(": the leg sent nothing")));
                QVERIFY2(message == bytes,
                         qPrintable(where + QStringLiteral(": got %1").arg(
                                                QString::fromLatin1(message.left(8).toHex()))));
                continue;
            }
            if (step.contains(QStringLiteral("to"))) {
                const QByteArray bytes =
                    bytesOf(step.value(QStringLiteral("binary")).toArray(), tokens, recorded);
                if (step.value(QStringLiteral("to")).toString() != QLatin1String("core")) {
                    continue;
                }
                QVERIFY2(current && current->socket,
                         qPrintable(where + QStringLiteral(": no connection")));
                current->socket->sendBinaryMessage(bytes);
                const auto tag = static_cast<quint8>(bytes.at(0));
                if (tag == RelayLeg::kTagEnd) {
                    current->endSent = true;
                }
                if (tag == RelayLeg::kTagControl || tag == RelayLeg::kTagMedia) {
                    // The leg writes exactly the payload to that agent, and
                    // nothing reached either agent before it.
                    QUdpSocket& agent = agents[tag - 1];
                    QVERIFY2(QTest::qWaitFor([&agent] { return agent.hasPendingDatagrams(); },
                                             5000),
                             qPrintable(where + QStringLiteral(": nothing reached the agent")));
                    const QNetworkDatagram datagram = agent.receiveDatagram();
                    QVERIFY2(datagram.data() == bytes.mid(1),
                             qPrintable(where + QStringLiteral(": another payload reached it")));
                    QVERIFY2(!agents[2 - tag].hasPendingDatagrams(),
                             qPrintable(where + QStringLiteral(": the other agent got something")));
                }
                continue;
            }
            if (step.contains(QStringLiteral("drop"))) {
                if (step.value(QStringLiteral("drop")).toString() == QLatin1String("core")
                    && current && current->socket) {
                    current->socket->abort();
                    current.reset();
                }
                continue;
            }
            if (step.contains(QStringLiteral("disconnect"))) {
                if (step.value(QStringLiteral("disconnect")).toString() == QLatin1String("core")
                    && current && current->socket) {
                    current->socket->close();
                    current.reset();
                }
                continue;
            }
            if (step.contains(QStringLiteral("expectClosed"))) {
                if (step.value(QStringLiteral("expectClosed")).toString() == QLatin1String("core")
                    && current && current->socket) {
                    const int code = step.value(QStringLiteral("code")).toInt(1000);
                    current->socket->close(static_cast<QWebSocketProtocol::CloseCode>(code));
                    current.reset();
                }
                continue;
            }
            if (step.contains(QStringLiteral("expectSilent"))) {
                if (step.value(QStringLiteral("expectSilent")).toString() != QLatin1String("core")) {
                    continue;
                }
                const int before = current ? static_cast<int>(current->received.size()) : 0;
                QTest::qWait(1000);
                if (current && !current->endSent) {
                    QVERIFY2(current->received.size() == before,
                             qPrintable(where + QStringLiteral(": the leg sent something")));
                }
                QVERIFY2(relay.pending.isEmpty(),
                         qPrintable(where + QStringLiteral(": the leg opened a connection")));
                continue;
            }
            // advanceMs and shutdown: the leg keeps no clock the runner
            // moves; shutdown's END is the next step's.
        }
        // A datagram the leg had to drop reached no agent.
        QTest::qWait(100);
        QVERIFY2(!agents[0].hasPendingDatagrams() && !agents[1].hasPendingDatagrams(),
                 qPrintable(file + QStringLiteral(": a dropped datagram reached an agent")));
        leg.close();
    }

    // The words each END code shows are plain, and codes that show none
    // show none.
    void everyEndCodesWordsArePlain()
    {
        for (const char* code : {"protocolError", "timeout", "badToken", "expired", "ended",
                                 "full", "tooManyConnections", "tooManySessions", "peerGone",
                                 "shuttingDown", "lost", "somethingNew"}) {
            const QString words = RelayLeg::wordsFor(QLatin1String(code));
            QVERIFY2(!words.isEmpty(), code);
            QVERIFY2(OperatorWording::isPlain(words), qPrintable(words));
        }
        QVERIFY(RelayLeg::wordsFor(QStringLiteral("replaced")).isEmpty());
        QVERIFY(RelayLeg::wordsFor(QStringLiteral("idle")).isEmpty());
    }

    // What the leg will not send, and the lanes' bounded queues: a
    // datagram over 1500 bytes is dropped, never sent; with the relay not
    // reading, each lane holds at most its bound and drops its oldest.
    void theLegSendsNothingOversizeAndBoundsItsQueues()
    {
        RelayPlayer relay;
        RelayLeg leg;
        QVERIFY(leg.bindLanes());
        QUdpSocket agent;
        QVERIFY(agent.bind(QHostAddress::LocalHost, 0));
        leg.open(relay.url(), QStringLiteral("tok"));
        std::shared_ptr<Accepted> current = relay.waitForConnection();
        QVERIFY(current);
        QTRY_VERIFY(!current->received.isEmpty()); // JOIN
        agent.writeDatagram(QByteArray(RelayLeg::kMaxDatagramBytes + 1, 'x'), QHostAddress::LocalHost,
                            leg.lanePort(1));
        QTRY_COMPARE(leg.droppedOversize(), quint64(1));
        agent.writeDatagram(QByteArray(RelayLeg::kMaxDatagramBytes, 'y'), QHostAddress::LocalHost,
                            leg.lanePort(1));
        QTRY_COMPARE(current->received.size(), qsizetype(2));
        QCOMPARE(current->received.at(1).size(), RelayLeg::kMaxDatagramBytes + 1);
        QCOMPARE(static_cast<quint8>(current->received.at(1).at(0)), RelayLeg::kTagControl);

        // A relay that takes the connection and never answers the
        // WebSocket upgrade: nothing can be written, so each lane keeps
        // its newest kQueueFrames and drops the rest, oldest first.
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost));
        RelayLeg stuck;
        QVERIFY(stuck.bindLanes());
        stuck.open(QUrl(QStringLiteral("ws://127.0.0.1:%1/v1/relay").arg(silent.serverPort())),
                   QStringLiteral("tok"));
        QTRY_VERIFY(silent.hasPendingConnections());
        const int sent = RelayLeg::kQueueFrames + 36;
        for (int i = 0; i < sent; ++i) {
            agent.writeDatagram(QByteArray(20, 'z'), QHostAddress::LocalHost, stuck.lanePort(2));
        }
        QTRY_COMPARE(stuck.droppedQueueFull(), quint64(36));
        QCOMPARE(stuck.datagramsSent(), quint64(0));
    }

    // A connection's candidate source gives its agent the lane socket's
    // candidate, at the lowest priority; a newer connection on the lane
    // takes it over and the older one's stop leaves it alone.
    void aSourceClaimsItsLane()
    {
        std::shared_ptr<RelayLeg> leg = RelayLeg::create();
        QVERIFY(leg);
        IceConfiguration ice = IceConfiguration::throughRendezvous(
            {}, /*relayAllowed=*/true, IceConfiguration::localAddressFamilies(), HostFamilies{});
        ice.setCandidateSourceFactory(RelayLeg::factoryFor(leg), /*needsRelay=*/true);
        auto first = ice.makeCandidateSource(IceConfiguration::kMediaLane);
        QVERIFY(first);
        QString candidate;
        first->start([&candidate](const QString& line) { candidate = line; });
        QCOMPARE(candidate, QStringLiteral("candidate:wsrelay2 1 UDP 1 127.0.0.1 %1 typ host")
                                .arg(leg->lanePort(2)));
        auto second = ice.makeCandidateSource(IceConfiguration::kMediaLane);
        second->start([](const QString&) {});
        first->stop();
        // `relay = deny` stops the web relay's sources as it stops TURN.
        IceConfiguration denied = IceConfiguration::throughRendezvous(
            {}, /*relayAllowed=*/false, IceConfiguration::localAddressFamilies(), HostFamilies{});
        denied.setCandidateSourceFactory(RelayLeg::factoryFor(leg), /*needsRelay=*/true);
        QVERIFY(!denied.makeCandidateSource(IceConfiguration::kControlLane));
        second->stop();
    }
};

QTEST_MAIN(TstRelayLeg)
#include "tst_relay_leg.moc"
