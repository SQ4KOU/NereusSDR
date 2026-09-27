// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_data_channel_transport.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 28 (R-IOS-16): the control session over a data
// channel (DataChannelTransport), on this computer:
//
//   - the chunking and heartbeat bytes (ControlFraming) and the framing
//     conformance fixtures (tests/data/link/v1/framing/);
//   - a message both ways, a 300 KiB settings snapshot in chunks, a message
//     over each end's cap ending the connection, frames a conforming sender
//     never makes;
//   - a whole session between a StationServer and a StationClient signed in
//     by device key over the channel, the Core presenting its own
//     certificate in DTLS;
//   - a Core whose DTLS certificate is not the one its identity binds,
//     refused by the desktop before it sends anything;
//   - the heartbeat declaring the link dead after two missed pongs, at the
//     Core and at the desktop.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: Start the short heartbeat test deadline after sign-in.
//               J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-27: each end keeps the far end's description before its ICE
//               agent takes it (R-R3-49). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QDir>
#include <QElapsedTimer>
#include <QPointer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QHostAddress>
#include <QMutex>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSslSocket>
#include <QTemporaryDir>

#include <chrono>
#include <memory>
#include <optional>
#include <thread>

#include <openssl/err.h>

#include "core/AppSettings.h"
#include "core/security/CertificateStore.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/DataChannelTransport.h"
#include "core/session/RendezvousWire.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"

#include "LinkFixtures.h"
#include "fakes/DataChannelPair.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;
using NereusSDR::Test::LinkFixtures;
using NereusSDR::Test::startDataChannelPair;
using NereusSDR::Test::waitFor;

namespace {

constexpr quint64 kStationCap = StationServer::kMaxIncomingMessageBytes;
constexpr quint64 kClientCap = StationClient::kMaxIncomingMessageBytes;

QByteArray patterned(qsizetype size)
{
    QByteArray bytes(size, Qt::Uninitialized);
    for (qsizetype index = 0; index < size; ++index) {
        bytes[index] = static_cast<char>('a' + (index * 7) % 26);
    }
    return bytes;
}

// A Core with its StationServer and its own certificate.
struct Core {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;

    Core()
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        settings->setValue(QStringLiteral("StationCallsign"), QStringLiteral("KG4VCF"));
        model = std::make_unique<RadioModel>();
        const QString security = NereusSDR::Test::seedCoreIdentity(securityDir.path());
        server = std::make_unique<StationServer>(model.get(), *settings, security);
        server->setHeartbeatIntervalMs(0);
    }

    ~Core() { server.reset(); }

    bool pairComputer(const ClientDeviceIdentity& key)
    {
        PairedDevice device;
        device.id = key.fingerprint();
        device.publicKeySpki = key.publicKeySpki();
        device.name = QStringLiteral("Shack MacBook");
        device.kind = QStringLiteral("computer");
        return server->deviceStore()->add(device);
    }
};

// Two transports on this computer, open.
struct OpenPair {
    std::unique_ptr<DataChannelTransport> offerer = std::make_unique<DataChannelTransport>();
    std::unique_ptr<DataChannelTransport> answerer = std::make_unique<DataChannelTransport>();

    bool open(const QString& certificate = QString(), const QString& key = QString())
    {
        if (!startDataChannelPair(offerer.get(), answerer.get(), kClientCap, kStationCap,
                                  certificate, key)) {
            return false;
        }
        return waitFor([this] { return offerer->isOpen() && answerer->isOpen(); }, 15000);
    }
};

} // namespace

class TstDataChannelTransport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
    }

    // ── The bytes ─────────────────────────────────────────────────────

    void aMessageIsCutIntoFullChunksThenTheRest()
    {
        const QByteArray message = patterned(ControlFraming::kMaxChunkPayloadBytes * 2 + 5);
        const QList<QByteArray> chunks = ControlFraming::chunk(message);
        QCOMPARE(chunks.size(), 3);
        QCOMPARE(chunks.at(0).size(), ControlFraming::kMaxChunkBytes);
        QCOMPARE(chunks.at(1).size(), ControlFraming::kMaxChunkBytes);
        QCOMPARE(chunks.at(2).size(), qsizetype(6));
        QCOMPARE(quint8(chunks.at(0).at(0)), ControlFraming::kChunkMore);
        QCOMPARE(quint8(chunks.at(1).at(0)), ControlFraming::kChunkMore);
        QCOMPARE(quint8(chunks.at(2).at(0)), ControlFraming::kChunkLast);
        // Exactly a chunk's worth is one chunk, the last.
        const QList<QByteArray> one =
            ControlFraming::chunk(patterned(ControlFraming::kMaxChunkPayloadBytes));
        QCOMPARE(one.size(), 1);
        QCOMPARE(quint8(one.at(0).at(0)), ControlFraming::kChunkLast);
        QCOMPARE(ControlFraming::ping(0x01020304), QByteArray::fromHex("1001020304"));
        QCOMPARE(ControlFraming::pong(0xa0b0c0d0), QByteArray::fromHex("11a0b0c0d0"));

        ControlFraming::Reassembler joiner(kStationCap);
        QCOMPARE(joiner.feed(chunks.at(0)), ControlFraming::Reassembler::Result::Pending);
        // A ping between two chunks leaves the message being joined alone.
        QCOMPARE(joiner.feed(ControlFraming::ping(7)), ControlFraming::Reassembler::Result::Ping);
        QCOMPARE(joiner.id(), quint32(7));
        QCOMPARE(joiner.feed(chunks.at(1)), ControlFraming::Reassembler::Result::Pending);
        QCOMPARE(joiner.feed(ControlFraming::pong(9)), ControlFraming::Reassembler::Result::Pong);
        QCOMPARE(joiner.feed(chunks.at(2)), ControlFraming::Reassembler::Result::Message);
        QCOMPARE(joiner.message(), message);
        QCOMPARE(joiner.pendingBytes(), qsizetype(0));
    }

    void framesAConformingSenderNeverMakesEndTheConnection_data()
    {
        QTest::addColumn<QByteArray>("frame");
        QTest::newRow("empty") << QByteArray();
        QTest::newRow("a chunk with nothing in it") << QByteArray::fromHex("02");
        QTest::newRow("an unknown kind") << QByteArray::fromHex("0361");
        QTest::newRow("a short ping") << QByteArray::fromHex("10010203");
        QTest::newRow("a long pong") << QByteArray::fromHex("110102030405");
        QTest::newRow("a chunk past the size")
            << (QByteArray(1, char(ControlFraming::kChunkLast))
                + patterned(ControlFraming::kMaxChunkBytes));
    }

    void framesAConformingSenderNeverMakesEndTheConnection()
    {
        QFETCH(QByteArray, frame);
        ControlFraming::Reassembler joiner(kStationCap);
        QCOMPARE(joiner.feed(frame), ControlFraming::Reassembler::Result::Refused);
        QVERIFY(!joiner.reason().isEmpty());
        // Refused for good.
        QCOMPARE(joiner.feed(ControlFraming::chunk("{}").first()),
                 ControlFraming::Reassembler::Result::Refused);
    }

    void aMessageOverTheCapIsRefusedTheMomentItPassesIt()
    {
        ControlFraming::Reassembler joiner(100);
        QCOMPARE(joiner.feed(QByteArray(1, char(ControlFraming::kChunkMore)) + patterned(60)),
                 ControlFraming::Reassembler::Result::Pending);
        // 60 + 40 is the cap exactly, still more to come: fine.
        QCOMPARE(joiner.feed(QByteArray(1, char(ControlFraming::kChunkMore)) + patterned(40)),
                 ControlFraming::Reassembler::Result::Pending);
        // One more byte passes it, before the last chunk arrives.
        QCOMPARE(joiner.feed(QByteArray(1, char(ControlFraming::kChunkMore)) + patterned(1)),
                 ControlFraming::Reassembler::Result::Refused);
        ControlFraming::Reassembler exact(100);
        QCOMPARE(exact.feed(QByteArray(1, char(ControlFraming::kChunkLast)) + patterned(100)),
                 ControlFraming::Reassembler::Result::Message);
    }

    // The framing fixtures (the link document, section 16.1): each one's
    // chunks, as a sender makes them, and what a receiver with that end's
    // cap does with its frames.
    void framingFixtures_data()
    {
        QTest::addColumn<QString>("file");
        QString error;
        const QJsonObject manifest = LinkFixtures::readObject(
            QDir(LinkFixtures::dataDirectory()).filePath(QStringLiteral("manifest.json")), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QList<LinkFixtures::Entry> entries =
            LinkFixtures::entries(manifest, QStringLiteral("framing"));
        QVERIFY(entries.size() >= 8);
        for (const LinkFixtures::Entry& entry : entries) {
            QTest::newRow(qPrintable(entry.id)) << entry.file;
        }
    }

    void framingFixtures()
    {
        QFETCH(QString, file);
        QString error;
        const QJsonObject fixture = LinkFixtures::readObject(
            QDir(LinkFixtures::dataDirectory()).filePath(file), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        const QString failure = LinkFixtures::runFraming(fixture);
        QVERIFY2(failure.isEmpty(), qPrintable(failure));
    }

    // ── Over a real connection ────────────────────────────────────────

    void aMessageCrossesBothWays()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy atCore(pair.answerer.get(), &SessionTransport::textReceived);
        QSignalSpy atDevice(pair.offerer.get(), &SessionTransport::textReceived);
        pair.offerer->sendText(QByteArrayLiteral("{\"type\":\"hello\"}"));
        pair.answerer->sendText(QByteArrayLiteral("{\"type\":\"auth.result\"}"));
        QTRY_COMPARE(atCore.count(), 1);
        QTRY_COMPARE(atDevice.count(), 1);
        QCOMPARE(atCore.first().first().toByteArray(), QByteArrayLiteral("{\"type\":\"hello\"}"));
        QCOMPARE(atDevice.first().first().toByteArray(),
                 QByteArrayLiteral("{\"type\":\"auth.result\"}"));
        // On one computer the far end has an address of its own.
        QVERIFY(!pair.answerer->peerAddress().isEmpty());
        QVERIFY(pair.offerer->telemetry().has_value());
    }

    // R-R3-49: on a busy computer the connection failed with "DTLS alert:
    // unknown CA" at both ends. libdatachannel v0.24.5 gave the ICE agent
    // the far end's description before keeping it for the DTLS fingerprint
    // check, so a handshake that ran in between failed that check (the
    // Offerer taking its answer while the Answerer's checks and DTLS were
    // already under way). NereusSDR compiles it with the two in the other
    // order (cmake/patches/libdatachannel-keep-remote-description-first.cpp).
    // What each end logs shows the order: the description kept, then the
    // ICE agent adding the description's candidates, on the same thread.
    void eachEndKeepsTheFarEndsDescriptionBeforeIceTakesIt()
    {
        QMutex mutex;
        QList<QPair<quintptr, QString>> lines;
        DataChannelTransport::setLibraryLogForTest([&](quintptr thread, const QString& line) {
            const QMutexLocker lock(&mutex);
            lines.append({thread, line});
        });
        const auto logOff = qScopeGuard([] { DataChannelTransport::setLibraryLogForTest({}); });
        OpenPair pair;
        QVERIFY(pair.open());
        DataChannelTransport::setLibraryLogForTest({});

        const QMutexLocker lock(&mutex);
        int kept = 0;
        int taken = 0;
        QHash<quintptr, int> keptOnThread;
        for (const auto& [thread, line] : std::as_const(lines)) {
            if (line.contains(QLatin1String("Remote description kept before the ICE agent takes it"))) {
                ++kept;
                ++keptOnThread[thread];
            } else if (line.contains(QLatin1String("candidates from remote description"))) {
                ++taken;
                // The agent takes a description only after it was kept.
                QVERIFY2(keptOnThread.value(thread) > 0, qPrintable(line));
                --keptOnThread[thread];
            }
        }
        // One description each way: the offer, then the answer.
        QCOMPARE(kept, 2);
        QCOMPARE(taken, 2);
    }

    void a300KiBSettingsSnapshotCrossesInChunks()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QJsonObject settings;
        int index = 0;
        QByteArray wire;
        while (wire.size() < 300 * 1024) {
            for (int batch = 0; batch < 200; ++batch, ++index) {
                settings.insert(QStringLiteral("StationSetting%1").arg(index, 5, 10, QLatin1Char('0')),
                                QString::fromLatin1(patterned(64)));
            }
            wire = QJsonDocument(QJsonObject{{QStringLiteral("type"),
                                              QStringLiteral("settings.snapshot")},
                                             {QStringLiteral("settings"), settings}})
                       .toJson(QJsonDocument::Compact);
        }
        QVERIFY(wire.size() >= 300 * 1024);
        QSignalSpy atDevice(pair.offerer.get(), &SessionTransport::textReceived);
        const quint64 chunksBefore = pair.answerer->countsForTest().chunksSent;
        pair.answerer->sendText(wire);
        QTRY_COMPARE_WITH_TIMEOUT(atDevice.count(), 1, 15000);
        QCOMPARE(atDevice.first().first().toByteArray(), wire);
        const quint64 expected = static_cast<quint64>(
            (wire.size() + ControlFraming::kMaxChunkPayloadBytes - 1)
            / ControlFraming::kMaxChunkPayloadBytes);
        QCOMPARE(pair.answerer->countsForTest().chunksSent - chunksBefore, expected);
        QVERIFY(expected >= 5);
        QVERIFY(QJsonDocument::fromJson(atDevice.first().first().toByteArray()).isObject());
    }

    void aMessageOverTheCoresCapEndsTheConnection()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy atCore(pair.answerer.get(), &SessionTransport::textReceived);
        QSignalSpy coreClosed(pair.answerer.get(), &SessionTransport::closed);
        QSignalSpy deviceClosed(pair.offerer.get(), &SessionTransport::closed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("larger than")));
        pair.offerer->sendText(patterned(static_cast<qsizetype>(kStationCap) + 1));
        QTRY_COMPARE_WITH_TIMEOUT(coreClosed.count(), 1, 15000);
        QTRY_COMPARE_WITH_TIMEOUT(deviceClosed.count(), 1, 15000);
        QCOMPARE(atCore.count(), 0);
        QVERIFY(!pair.answerer->isOpen());
    }

    void aMessageOverTheDesktopsCapEndsTheConnection()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy atDevice(pair.offerer.get(), &SessionTransport::textReceived);
        QSignalSpy deviceClosed(pair.offerer.get(), &SessionTransport::closed);
        QSignalSpy coreClosed(pair.answerer.get(), &SessionTransport::closed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("larger than")));
        // The Core may send what the Core may not receive: the desktop's cap
        // is eight times the Core's.
        pair.answerer->sendText(patterned(static_cast<qsizetype>(kClientCap) + 1));
        QTRY_COMPARE_WITH_TIMEOUT(deviceClosed.count(), 1, 30000);
        QTRY_COMPARE_WITH_TIMEOUT(coreClosed.count(), 1, 15000);
        QCOMPARE(atDevice.count(), 0);
    }

    // Review Minor 3: whole messages waiting for a thread that has stopped
    // are bounded by bytes (kMaxQueuedCaps times the cap), not only by how
    // many there are: the connection ends past it.
    void messagesWaitingForABusyThreadAreBoundedByBytes()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy atCore(pair.answerer.get(), &SessionTransport::textReceived);
        QSignalSpy coreClosed(pair.answerer.get(), &SessionTransport::closed);
        const QByteArray message = patterned(static_cast<qsizetype>(kStationCap) - 64);
        const int count = static_cast<int>(DataChannelTransport::kMaxQueuedCaps) + 2;
        for (int i = 0; i < count; ++i) {
            pair.offerer->sendText(message);
        }
        // This thread stays busy (no events run) while the library's
        // threads carry the messages in; they wait for it, up to the bound.
        QElapsedTimer busy;
        busy.start();
        quint64 highest = 0;
        while (busy.elapsed() < 8000) {
            highest = std::max(highest, pair.answerer->pendingBytesForTest());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        QVERIFY(highest <= kStationCap * DataChannelTransport::kMaxQueuedCaps);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("fell behind")));
        QTRY_COMPARE_WITH_TIMEOUT(coreClosed.count(), 1, 15000);
        QCOMPARE(atCore.count(), 0);
    }

    // Review Minor 3: messages held until something listens are bounded
    // by bytes too.
    void messagesHeldForNoListenerAreBoundedByBytes()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        // Nothing listens to the Core's end.
        QSignalSpy coreClosed(pair.answerer.get(), &SessionTransport::closed);
        const QByteArray message = patterned(static_cast<qsizetype>(kStationCap) - 64);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("past the bound")));
        const int count = static_cast<int>(DataChannelTransport::kMaxQueuedCaps) + 2;
        for (int i = 0; i < count; ++i) {
            pair.offerer->sendText(message);
            QTest::qWait(50);
        }
        QTRY_COMPARE_WITH_TIMEOUT(coreClosed.count(), 1, 20000);
    }

    // Review Minor 7: what was sent before closeLink() arrives, even behind
    // a backlog and with the transport deleted at once, as
    // StationServer::dropPeer does after its session.end.
    void theLastMessagesArriveWhenTheTransportIsDeletedAtItsClose()
    {
        auto offerer = std::make_unique<DataChannelTransport>();
        auto* answerer = new DataChannelTransport();
        QVERIFY(startDataChannelPair(offerer.get(), answerer, kClientCap, kStationCap, QString(),
                                     QString()));
        QVERIFY(waitFor([&] { return offerer->isOpen() && answerer->isOpen(); }, 15000));
        QSignalSpy atDevice(offerer.get(), &SessionTransport::textReceived);
        const QByteArray backlog = patterned(static_cast<qsizetype>(kStationCap) - 64);
        const QByteArray last = QByteArrayLiteral("{\"type\":\"session.end\"}");
        for (int i = 0; i < 3; ++i) {
            answerer->sendText(backlog);
        }
        answerer->sendText(last);
        answerer->closeLink(QStringLiteral("test"));
        delete answerer;
        QTRY_COMPARE_WITH_TIMEOUT(atDevice.count(), 4, 20000);
        QCOMPARE(atDevice.last().first().toByteArray(), last);
    }

    void anUnknownFrameEndsTheConnection()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy coreClosed(pair.answerer.get(), &SessionTransport::closed);
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("unknown kind")));
        QVERIFY(pair.offerer->sendRawFrameForTest(QByteArray::fromHex("0361")));
        QTRY_COMPARE_WITH_TIMEOUT(coreClosed.count(), 1, 15000);
    }

    void aPingIsAnsweredWithItsId()
    {
        OpenPair pair;
        QVERIFY(pair.open());
        QSignalSpy pongs(pair.offerer.get(), &SessionTransport::pongReceived);
        pair.offerer->ping();
        QTRY_COMPARE(pongs.count(), 1);
        // A pong for a ping never sent is not evidence of anything.
        QVERIFY(pair.answerer->sendRawFrameForTest(ControlFraming::pong(4000)));
        pair.offerer->ping();
        QTRY_COMPARE(pongs.count(), 2);
        QTest::qWait(50);
        QCOMPARE(pongs.count(), 2);
        pair.answerer->setAnswersPingsForTest(false);
        pair.offerer->ping();
        QVERIFY(waitFor([&pair] { return pair.answerer->countsForTest().pingsReceived == 3; },
                        5000));
        QCOMPARE(pongs.count(), 2);
    }

    // Loading the Core's certificate leaves nothing in this thread's
    // OpenSSL error queue, where Qt's OpenSSL TLS backend would read it as
    // its own error and end a wss:// connection on this thread.
    void presentingTheCoresCertificateLeavesNoOpenSslError()
    {
        Core core;
        ERR_clear_error();
        DataChannelTransport answerer;
        DataChannelTransport::Options options;
        options.role = DataChannelTransport::Role::Answerer;
        options.maxIncomingBytes = kStationCap;
        options.certificatePemPath = core.server->certificatePemPath();
        options.privateKeyPemPath = core.server->privateKeyPemPath();
        QVERIFY(answerer.start(options));
        QCOMPARE(ERR_peek_error(), 0UL);
    }

    // A certificate file that cannot be read makes the peer throw while
    // the error is queued; the transport leaves the queue empty on that
    // path too.
    void aCertificateThatCannotBeReadLeavesNoOpenSslError()
    {
        QTemporaryDir dir;
        const QString bad = dir.filePath(QStringLiteral("not-a-certificate.pem"));
        QFile file(bad);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("not a certificate\n");
        file.close();
        ERR_clear_error();
        DataChannelTransport answerer;
        DataChannelTransport::Options options;
        options.role = DataChannelTransport::Role::Answerer;
        options.maxIncomingBytes = kStationCap;
        options.certificatePemPath = bad;
        options.privateKeyPemPath = bad;
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("The control connection could not "
                                                               "start.*")));
        QVERIFY(!answerer.start(options));
        QCOMPARE(ERR_peek_error(), 0UL);
    }

    // A device's key whose point is not on the curve, and a signature
    // with r = 0, reach StationIdentity before anyone has signed in (a
    // pair.start, an auth.request, an introduction through the service).
    // Each makes OpenSSL queue an error; none is left behind, and a
    // wss:// session on this same thread goes on working after them.
    void aBadDeviceKeyOrSignatureLeavesNoOpenSslErrorAndAWssSessionSurvives()
    {
        Core core;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));
        QVERIFY2(core.server->listen(QHostAddress::LocalHost, 0),
                 qPrintable(core.server->lastError()));
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        window.connectToStation(
            QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(core.server->serverPort())),
            QString(), QString(), false, core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        qInfo() << "TLS backend:" << QSslSocket::activeBackend();

        const QByteArray good = key->publicKeySpki();
        QCOMPARE(good.size(), StationIdentity::kSpkiBytes);
        QVERIFY(StationIdentity::isP256Spki(good));
        // The last byte of y changed: the point is off the curve.
        QByteArray offCurve = good;
        offCurve[offCurve.size() - 1] = static_cast<char>(offCurve.at(offCurve.size() - 1) ^ 0x01);
        // r = 0, s = 1.
        QByteArray zeroR(StationIdentity::kSignatureBytes, '\0');
        zeroR[zeroR.size() - 1] = 1;
        const QByteArray message = QByteArrayLiteral("a device's transcript");

        ERR_clear_error();
        QVERIFY(!StationIdentity::isP256Spki(offCurve));
        QCOMPARE(ERR_peek_error(), 0UL);
        QVERIFY(!StationIdentity::verify(offCurve, message, zeroR));
        QCOMPARE(ERR_peek_error(), 0UL);
        QVERIFY(!StationIdentity::verify(good, message, zeroR));
        QCOMPARE(ERR_peek_error(), 0UL);

        // The session on this thread still carries traffic both ways: a
        // heartbeat from the Core, answered.
        core.server->setHeartbeatIntervalMs(200);
        QTest::qWait(1500);
        QVERIFY(window.isHandshakeComplete());
        QVERIFY(core.server->hasAuthenticatedSession());
        window.disconnectFromStation(QStringLiteral("test done"));
        QTRY_VERIFY_WITH_TIMEOUT(!core.server->hasAuthenticatedSession(), 10000);
    }

    // Task 28 fix wave (review Important 4): media keeps this end's relay
    // only when the control connection's path is relayed.
    void mediaUsesTheRelayOnlyWhenTheControlPathIsRelayed()
    {
        IceConfiguration control = IceConfiguration::throughRendezvous(
            {QStringLiteral("stun:192.0.2.1:3478")}, true, AddressFamilies{true, false},
            HostFamilies{});
        RendezvousWire::Turn turn;
        turn.urls = {QStringLiteral("turn:192.0.2.1:3478?transport=udp")};
        turn.username = QStringLiteral("1790000000:abcdefghijklmnopqrstuvwxyz");
        turn.password = QStringLiteral("secret");
        QCOMPARE(control.setRelay(turn, 1), 1);

        const auto path = [](const QString& local, const QString& remote) {
            MediaIcePath p;
            p.localType = local;
            p.remoteType = remote;
            p.localAddress = QStringLiteral("198.51.100.2");
            p.remoteAddress = QStringLiteral("203.0.113.9");
            p.remotePort = 50000;
            return std::optional<MediaIcePath>(p);
        };
        // Direct (host or server-reflexive): no relay of this end's own.
        for (const auto& direct : {path(QStringLiteral("host"), QStringLiteral("host")),
                                   path(QStringLiteral("srflx"), QStringLiteral("prflx"))}) {
            const std::optional<IceConfiguration> media =
                DataChannelTransport::mediaIceFor(control, direct);
            QVERIFY(media.has_value());
            QCOMPARE(media->relayServers().size(), 0);
            QCOMPARE(media->stunServer(), control.stunServer());
            QVERIFY(media->relayAllowed());
            QVERIFY(media->relayKnown());
        }
        // Relayed at either end, or through the far end's relay: kept.
        std::optional<MediaIcePath> viaFarRelay = path(QStringLiteral("srflx"), QStringLiteral("prflx"));
        viaFarRelay->farEndRelays.append(qMakePair(QStringLiteral("203.0.113.9"), quint16(50000)));
        for (const auto& relayed : {path(QStringLiteral("relay"), QStringLiteral("host")),
                                    path(QStringLiteral("host"), QStringLiteral("relay")),
                                    viaFarRelay}) {
            const std::optional<IceConfiguration> media =
                DataChannelTransport::mediaIceFor(control, relayed);
            QVERIFY(media.has_value());
            QCOMPARE(media->relayServers(), control.relayServers());
        }
        // No path yet: kept. Not through the service: none.
        QCOMPARE(DataChannelTransport::mediaIceFor(control, std::nullopt)->relayServers().size(), 1);
        QVERIFY(!DataChannelTransport::mediaIceFor(std::nullopt, path(QStringLiteral("host"),
                                                                     QStringLiteral("host")))
                     .has_value());
    }

    // ── A session over the channel ─────────────────────────────────────

    void aWholeSessionRunsOverTheChannel()
    {
        Core core;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));
        auto* offerer = new DataChannelTransport();
        auto* answerer = new DataChannelTransport();
        QObject::connect(answerer, &DataChannelTransport::opened, core.server.get(),
                         [&core, answerer] { core.server->acceptTransport(answerer); });
        QVERIFY(startDataChannelPair(offerer, answerer, kClientCap, kStationCap,
                                     core.server->certificatePemPath(),
                                     core.server->privateKeyPemPath()));
        QVERIFY(waitFor([offerer] { return offerer->isOpen(); }, 15000));
        // The certificate the Core presented in DTLS is its own.
        QString hex = core.server->certificateFingerprint();
        hex.remove(QLatin1Char(':'));
        QCOMPARE(offerer->peerCertificateSha256(), QByteArray::fromHex(hex.toLatin1()));
        QVERIFY(answerer->peerCertificateSha256().isEmpty());

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        window.startSession(offerer, QString(), QString(),
                            core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QVERIFY(core.server->hasAuthenticatedSession());
        window.disconnectFromStation(QStringLiteral("test done"));
        QTRY_VERIFY_WITH_TIMEOUT(!core.server->hasAuthenticatedSession(), 10000);
    }

    void aCoreThatIsNotTheBoundOneIsRefusedBeforeAnythingIsSent()
    {
        Core core;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));
        // Another certificate: the hello's binding does not cover it.
        QTemporaryDir otherDir;
        CertificateStore other(otherDir.path());
        QVERIFY(other.isValid());
        QVERIFY(other.fingerprintSha256() != core.server->certificateFingerprint());

        auto* offerer = new DataChannelTransport();
        auto* answerer = new DataChannelTransport();
        QObject::connect(answerer, &DataChannelTransport::opened, core.server.get(),
                         [&core, answerer] { core.server->acceptTransport(answerer); });
        QVERIFY(startDataChannelPair(offerer, answerer, kClientCap, kStationCap,
                                     other.certificatePath(), other.privateKeyPath()));
        QVERIFY(waitFor([offerer] { return offerer->isOpen(); }, 15000));
        const QPointer<DataChannelTransport> coreEnd(answerer);

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        QSignalSpy ended(&window, &StationClient::sessionEnded);
        // What the desktop's end had sent when the session ended (read
        // then: the transport may go once it has).
        std::optional<quint64> sentByDevice;
        const QPointer<DataChannelTransport> deviceEnd(offerer);
        QObject::connect(&window, &StationClient::sessionEnded, &window, [&sentByDevice, deviceEnd] {
            if (deviceEnd) {
                sentByDevice = deviceEnd->countsForTest().messagesSent;
            }
        });
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral(".*")));
        window.startSession(offerer, QString(), QString(),
                            core.server->stationIdentity().fingerprint());
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 20000);
        QCOMPARE(window.lastEndReport().kind, StationEndReport::Kind::IdentityChanged);
        QVERIFY(!window.isHandshakeComplete());
        // Nothing reached the Core from the desktop: not its hello, not its
        // sign-in.
        QVERIFY(sentByDevice.has_value());
        QCOMPARE(*sentByDevice, quint64(0));
        QVERIFY(coreEnd.isNull() || coreEnd->countsForTest().messagesDelivered == 0);
        QVERIFY(!core.server->hasAuthenticatedSession());
    }

    void theCoreDeclaresTheLinkDeadAfterTwoMissedPongs()
    {
        Core core;
        core.server->setHeartbeatIntervalMs(100);
        auto offerer = std::make_unique<DataChannelTransport>();
        auto* answerer = new DataChannelTransport();
        QObject::connect(answerer, &DataChannelTransport::opened, core.server.get(),
                         [&core, answerer] { core.server->acceptTransport(answerer); });
        QVERIFY(startDataChannelPair(offerer.get(), answerer, kClientCap, kStationCap,
                                     core.server->certificatePemPath(),
                                     core.server->privateKeyPemPath()));
        QSignalSpy received(offerer.get(), &SessionTransport::textReceived);
        QSignalSpy closed(offerer.get(), &SessionTransport::closed);
        QVERIFY(waitFor([&offerer] { return offerer->isOpen(); }, 15000));
        // A device that has gone silent without closing.
        offerer->setAnswersPingsForTest(false);
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 10000);
        QString last;
        for (const QList<QVariant>& message : std::as_const(received)) {
            last = QString::fromUtf8(message.first().toByteArray());
        }
        QVERIFY2(last.contains(QStringLiteral("session.end"))
                     && last.contains(QStringLiteral("stopped answering")),
                 qPrintable(last));
        // It was the third ping's tick: two went unanswered.
        QVERIFY(offerer->countsForTest().pingsReceived >= 2);
    }

    void theDesktopDeclaresTheLinkDeadAfterTwoMissedPongs()
    {
        Core core;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));
        auto* offerer = new DataChannelTransport();
        auto* answerer = new DataChannelTransport();
        QObject::connect(answerer, &DataChannelTransport::opened, core.server.get(),
                         [&core, answerer] { core.server->acceptTransport(answerer); });
        QVERIFY(startDataChannelPair(offerer, answerer, kClientCap, kStationCap,
                                     core.server->certificatePemPath(),
                                     core.server->privateKeyPemPath()));
        QVERIFY(waitFor([offerer] { return offerer->isOpen(); }, 15000));
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        QSignalSpy ended(&window, &StationClient::sessionEnded);
        window.startSession(offerer, QString(), QString(),
                            core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        // Allow normal sign-in before testing the deliberately short liveness
        // deadline. A busy test host must not time out during the snapshot.
        window.setHeartbeatIntervalMs(100);
        // The Core goes silent without closing.
        answerer->setAnswersPingsForTest(false);
        QTRY_COMPARE_WITH_TIMEOUT(ended.count(), 1, 10000);
        QCOMPARE(ended.first().first().toString(), QStringLiteral("heartbeat timeout"));
    }
};

QTEST_MAIN(TstDataChannelTransport)
#include "tst_data_channel_transport.moc"
