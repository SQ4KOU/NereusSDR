// no-port-check: NereusSDR-original. R-R3-47 / R-R3-22 / R-R3-25 Core-owned
// Power Genius XL: identity before admission, pairing only after it, band
// follow, and cancellation in every phase. Station network filter on the
// identity announcement (R-R3-22, 2026-09-24).
//
// Loopback TCP peers stand in for the amp; discovery is injected. The
// discovery announcement and the `info` reply are the real amp's, captured
// on the bench (StationPgxlController.h names the capture files). No real
// accessory is contacted and nothing is sent to hardware.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpServer>
#include <QTcpSocket>

#include "core/AppSettings.h"
#include "core/LanDiscovery.h"
#include "core/PgxlConnection.h"
#include "core/SmartSdrApiListener.h"
#include "core/StationPgxlController.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;
using Phase = AmplifierModel::ConnectionPhase;

namespace {

// Captured from the real amp (192.168.109.235), with the address and port
// replaced by the loopback fixture's.
constexpr const char* kSerial = "10-200/24-0046";
constexpr const char* kInfoReply = "serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240";
// The real Tuner Genius's replies (captures/flex-tgxl-direct-NOTES.md and
// the discovery capture): what answers when the amp's address points at it.
constexpr const char* kTunerInfoReply =
    "info serial=241288-1 version=1.2.17 nickname=Tuner_Genius_XL";

quint16 closedLoopbackPort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { return 0; }
    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

quint32 sequenceOf(const QSignalSpy& frames, const QString& commandPattern)
{
    const QRegularExpression rx(QStringLiteral("^C(\\d+)\\|") + commandPattern);
    for (const auto& row : frames) {
        const auto match = rx.match(row.first().toString());
        if (match.hasMatch()) { return match.captured(1).toUInt(); }
    }
    return 0;
}

QStringList commandsOf(const QSignalSpy& frames)
{
    QStringList commands;
    for (const auto& row : frames) {
        const QString frame = row.first().toString();
        commands.append(frame.mid(frame.indexOf(QLatin1Char('|')) + 1));
    }
    return commands;
}

} // namespace

class StationPgxlControllerTest : public QObject {
    Q_OBJECT

    static void prepare(RadioModel& model, const QString& mac = QStringLiteral("aa:bb:cc:dd:ee:71"))
    {
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo radio;
        radio.macAddress = mac;
        model.setLastRadioInfoForTest(radio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
        model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    }

    static LanDiscovery* discoveryOf(RadioModel& model)
    {
        auto* controller = model.findChild<StationPgxlController*>();
        return controller ? controller->findChild<LanDiscovery*>() : nullptr;
    }

    static void announce(RadioModel& model, quint16 port, const QString& product,
                         const QString& serial, const QString& nickname = QStringLiteral("PowerGeniusXL"))
    {
        discoveryOf(model)->injectDatagramForTesting(
            QStringLiteral("%1 ip=127.0.0.1 v=3.8.9 serial=%2 nickname=%3")
                .arg(product, serial, nickname), port);
    }

    // Dial through the Core, answer the V banner and the `info` request.
    static QTcpSocket* answerUpToInfo(RadioModel& model, QTcpServer& server,
                                      const QSignalSpy& frames, const char* banner,
                                      const char* infoBody)
    {
        QString reason;
        if (!model.configurePgxlForStation(QStringLiteral("127.0.0.1"), server.serverPort(),
                                           &reason)) {
            qWarning() << "configure refused:" << reason;
            return nullptr;
        }
        if (!QTest::qWaitFor([&] { return server.hasPendingConnections(); }, 2000)) {
            return nullptr;
        }
        QTcpSocket* peer = server.nextPendingConnection();
        peer->write(QByteArray(banner) + '\n');
        peer->flush();
        if (!QTest::qWaitFor([&] { return sequenceOf(frames, QStringLiteral("info$")) != 0; },
                             2000)) {
            return nullptr;
        }
        peer->write(QStringLiteral("R%1|0|%2\n")
                        .arg(sequenceOf(frames, QStringLiteral("info$")))
                        .arg(QString::fromLatin1(infoBody)).toUtf8());
        peer->flush();
        if (!QTest::qWaitFor([&] { return discoveryOf(model) != nullptr; }, 2000)) {
            return nullptr;
        }
        return peer;
    }

private slots:
    void init()
    {
        AppSettings::instance().clear();
        AppSettings::instance().setValue(QStringLiteral("PeripheralsMigrationDone"),
                                         QStringLiteral("True"));
    }
    void cleanup() { AppSettings::instance().clear(); }

    // The whole address is checked before anything is saved or dialled.
    void validatesAddressBeforeChangingAnything()
    {
        RadioModel model;
        prepare(model);
        model.setPeripheralValue(QStringLiteral("PGXL_ManualIp"), QStringLiteral("saved.example"));
        model.setPeripheralValue(QStringLiteral("PGXL_ManualPort"), QStringLiteral("9008"));
        const quint64 token = model.pgxlConnection()->socketAttemptToken();
        QString reason;
        for (const auto& host : {QString(), QStringLiteral("bad host"),
                                QStringLiteral("http://127.0.0.1"),
                                QStringLiteral("127.0.0.1\nstatus")}) {
            QVERIFY(!model.configurePgxlForStation(host, 9008, &reason));
            QVERIFY(!reason.isEmpty());
            QCOMPARE(model.peripheralValue(QStringLiteral("PGXL_ManualIp")),
                     QStringLiteral("saved.example"));
            QCOMPARE(model.pgxlConnection()->socketAttemptToken(), token);
        }
        QVERIFY(!model.configurePgxlForStation(QStringLiteral("127.0.0.1"), 0, &reason));
        model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("False"));
        QVERIFY(!model.configurePgxlForStation(QStringLiteral("127.0.0.1"), 9008, &reason));
        QCOMPARE(reason, QStringLiteral("Turn on 4O3A on the Core before connecting the Power Genius."));
        model.setConnectionStateForTest(ConnectionState::Disconnected);
        QVERIFY(!model.configurePgxlForStation(QStringLiteral("127.0.0.1"), 9008, &reason));
        QVERIFY(model.disconnectPgxlForStation(&reason));

        // A window with no Core controller has nothing to configure.
        RadioModel local;
        QVERIFY(!local.configurePgxlForStation(QStringLiteral("127.0.0.1"), 9008, &reason));
        QCOMPARE(reason, QStringLiteral("This Core cannot change its amplifier and tuner settings."));
    }

    // A real Power Genius (captured discovery plus the same serial in its own
    // info reply) is admitted, then paired, then follows the band.
    void realPowerGeniusIsAdmittedThenPairedThenFollowsBand()
    {
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        QVERIFY(a >= 0);
        model.sliceById(a)->setFrequency(14200000);
        auto* pgxl = model.pgxlConnection();
        auto* ampModel = model.amplifierModel();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QSignalSpy paired(pgxl, &PgxlConnection::pairingResult);

        QTcpSocket* peer = answerUpToInfo(model, amp, frames, "V3.8.9", kInfoReply);
        QVERIFY(peer);
        QCOMPARE(ampModel->connectionPhase(), Phase::Identifying);
        QVERIFY(!pgxl->isConnected());
        // Before admission the amp was asked who it is, and nothing else.
        QCOMPARE(commandsOf(frames), QStringList{QStringLiteral("info")});

        announce(model, amp.serverPort(), QStringLiteral("PowerGeniusXL"),
                 QString::fromLatin1(kSerial));
        QTRY_VERIFY(pgxl->isConnected());
        QCOMPARE(ampModel->connectionPhase(), Phase::Connected);
        QCOMPARE(ampModel->deviceModel(), QStringLiteral("PowerGeniusXL"));
        QCOMPARE(ampModel->deviceSerial(), QString::fromLatin1(kSerial));
        QCOMPARE(ampModel->deviceVersion(), QStringLiteral("3.8.9"));
        QCOMPARE(ampModel->deviceNickname(), QStringLiteral("PowerGeniusXL"));
        QCOMPARE(ampModel->configuredHost(), QStringLiteral("127.0.0.1"));
        QCOMPARE(ampModel->configuredPort(), int(amp.serverPort()));
        QCOMPARE(model.peripheralValue(QStringLiteral("PGXL_ManualPort")),
                 QString::number(amp.serverPort()));

        // Admitted: now paired automatically.
        const QStringList sent = commandsOf(frames);
        QVERIFY2(sent.filter(QStringLiteral("amplifier create ")).size() == 1, qPrintable(sent.join('\n')));
        QVERIFY2(sent.filter(QStringLiteral("flexradio ampslice=A serial=")).size() == 1,
                 qPrintable(sent.join('\n')));
        QVERIFY(sent.contains(QStringLiteral("keepalive enable")));
        // QStringList::indexOf(QRegularExpression) matches the whole string.
        const qsizetype createAt = sent.indexOf(QRegularExpression(
            QStringLiteral("amplifier create .*")));
        QVERIFY(createAt > sent.indexOf(QStringLiteral("info")));
        QCOMPARE(sent.indexOf(QStringLiteral("info")), 0);

        // Band follow after the amp accepts the pairing.
        const quint32 pairSeq = sequenceOf(frames, QStringLiteral("flexradio ampslice=A serial=\\S+ txant"));
        QVERIFY(pairSeq != 0);
        peer->write(QStringLiteral("R%1|0|\n").arg(pairSeq).toUtf8());
        peer->flush();
        QTRY_COMPARE(paired.count(), 1);
        QVERIFY(paired.first().first().toBool());
        QTRY_VERIFY(!commandsOf(frames).filter(QStringLiteral(" band=14200000")).isEmpty());

        QString reason;
        QVERIFY(model.disconnectPgxlForStation(&reason));
        QCOMPARE(ampModel->connectionPhase(), Phase::Disconnected);
        QVERIFY(!pgxl->isConnected());
    }

    // A Tuner Genius answering at the amp's address: never admitted, never
    // paired, asked for nothing but `info`.
    void tunerGeniusAtAmpAddressIsNeverAdmitted()
    {
        AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"), QStringLiteral("False"));
        QTcpServer tuner;
        QVERIFY(tuner.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QSignalSpy connected(pgxl, &PgxlConnection::connected);
        QTcpSocket* peer = answerUpToInfo(model, tuner, frames, "V1.2.17", kTunerInfoReply);
        QVERIFY(peer);
        announce(model, tuner.serverPort(), QStringLiteral("TunerGenius"),
                 QStringLiteral("241288-1"), QStringLiteral("Tuner_Genius_XL"));
        QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Error);
        QVERIFY(model.amplifierModel()->connectionError().contains(QStringLiteral("TunerGenius")));
        QCOMPARE(model.amplifierModel()->deviceModel(), QStringLiteral("TunerGenius"));
        QTest::qWait(200);
        QCOMPARE(connected.count(), 0);
        QVERIFY(!pgxl->isConnected());
        QVERIFY(!model.amplifierModel()->present());
        QCOMPARE(commandsOf(frames), QStringList{QStringLiteral("info")});
    }

    // A Power Genius announcement with a different serial: not admitted.
    void serialMismatchIsNeverAdmitted()
    {
        AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"), QStringLiteral("False"));
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QVERIFY(answerUpToInfo(model, amp, frames, "V3.8.9", kInfoReply));
        announce(model, amp.serverPort(), QStringLiteral("PowerGeniusXL"),
                 QStringLiteral("10-200/24-0047"));
        QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Error);
        QVERIFY(model.amplifierModel()->connectionError().startsWith(
            QStringLiteral("PGXL identity serial mismatch")));
        QVERIFY(!pgxl->isConnected());
        QCOMPARE(commandsOf(frames), QStringList{QStringLiteral("info")});
    }

    // R-R3-22 / R-R3-47: with the Core's station rule set, the identity
    // announcement is heard only from the station network (here this
    // computer, station_bind = 127.0.0.1): the same announcement from
    // another network admits nothing.
    void coreHearsIdentityOnlyFromTheStationNetwork()
    {
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        model.setStationBind(QStringLiteral("127.0.0.1"));
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QVERIFY(answerUpToInfo(model, amp, frames, "V3.8.9", kInfoReply));
        const QString line = QStringLiteral("PowerGeniusXL ip=127.0.0.1 v=3.8.9 serial=%1 nickname=PowerGeniusXL")
                                 .arg(QString::fromLatin1(kSerial));
        discoveryOf(model)->injectDatagramForTesting(line, amp.serverPort(),
                                                     QHostAddress(QStringLiteral("192.168.1.43")));
        QTest::qWait(100);
        QVERIFY(!pgxl->isConnected());
        QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Identifying);
        discoveryOf(model)->injectDatagramForTesting(line, amp.serverPort(),
                                                     QHostAddress(QHostAddress::LocalHost));
        QTRY_VERIFY(pgxl->isConnected());
        QString reason;
        QVERIFY(model.disconnectPgxlForStation(&reason));
    }

    // Anything that answers V but never says who it is times out unpaired.
    void silentPeerTimesOutUnpaired()
    {
        AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"), QStringLiteral("False"));
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        pgxl->testSetIdentityTimeoutMs(200);
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), amp.serverPort(), &reason));
        QTRY_VERIFY(amp.hasPendingConnections());
        QTcpSocket* peer = amp.nextPendingConnection();
        peer->write("V3.8.9\n");
        peer->flush();
        QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Error);
        QCOMPARE(model.amplifierModel()->connectionError(),
                 QStringLiteral("PGXL native identity timed out"));
        QVERIFY(!pgxl->isConnected());
        QCOMPARE(commandsOf(frames), QStringList{QStringLiteral("info")});
    }

    // Disabling or disconnecting in any phase never redials the old address.
    void cancelInEveryPhaseNeverRedials_data()
    {
        QTest::addColumn<QString>("phase");
        QTest::addColumn<bool>("switchOff");
        for (const char* phase : {"connecting", "identifying", "connected", "retrying"}) {
            QTest::newRow(QByteArray(phase).append("-disconnect").constData())
                << QString::fromLatin1(phase) << false;
            QTest::newRow(QByteArray(phase).append("-switch-off").constData())
                << QString::fromLatin1(phase) << true;
        }
    }
    void cancelInEveryPhaseNeverRedials()
    {
        QFETCH(QString, phase);
        QFETCH(bool, switchOff);
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        pgxl->testSetReconnectBackoffUnitMs(100);
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QTcpServer amp;
        quint16 port = 0;
        if (phase == QStringLiteral("retrying")) {
            port = closedLoopbackPort();
            QVERIFY(port != 0);
            QVERIFY(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), port, &reason));
            QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Retrying);
            QVERIFY(pgxl->testReconnectPending());
        } else {
            QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
            port = amp.serverPort();
            if (phase == QStringLiteral("connecting")) {
                QVERIFY(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), port, &reason));
                QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Connecting);
            } else {
                QTcpSocket* peer = answerUpToInfo(model, amp, frames, "V3.8.9", kInfoReply);
                QVERIFY(peer);
                if (phase == QStringLiteral("connected")) {
                    announce(model, port, QStringLiteral("PowerGeniusXL"),
                             QString::fromLatin1(kSerial));
                    QTRY_VERIFY(pgxl->isConnected());
                } else {
                    QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Identifying);
                }
            }
        }

        if (switchOff) {
            QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
            QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Disabled);
        } else {
            QVERIFY(model.disconnectPgxlForStation(&reason));
            QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Disconnected);
        }
        QVERIFY(!pgxl->testReconnectPending());
        QVERIFY(!pgxl->isConnected());

        // Whatever the old address was, it is never dialled again.
        amp.close();
        QTcpServer old;
        QVERIFY(old.listen(QHostAddress::LocalHost, port));
        QSignalSpy redials(&old, &QTcpServer::newConnection);
        QTest::qWait(700);
        QCOMPARE(redials.count(), 0);
        QVERIFY(!pgxl->testReconnectPending());
        QVERIFY(model.amplifierModel()->connectionPhase() == Phase::Disabled
                || model.amplifierModel()->connectionPhase() == Phase::Disconnected);
    }

    // Replacing A with B while A is being identified leaves only B: A's late
    // reply cannot act, A is not redialled, B is admitted.
    void replacingPendingAWithBLeavesOnlyB()
    {
        QTcpServer ampA;
        QTcpServer ampB;
        QVERIFY(ampA.listen(QHostAddress::LocalHost, 0));
        QVERIFY(ampB.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        QSignalSpy connectionsA(&ampA, &QTcpServer::newConnection);

        // A: V answered, its info reply held back.
        QString reason;
        QVERIFY(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), ampA.serverPort(), &reason));
        QTRY_VERIFY(ampA.hasPendingConnections());
        QPointer<QTcpSocket> peerA = ampA.nextPendingConnection();
        peerA->write("V3.8.9\n");
        peerA->flush();
        QTRY_VERIFY(sequenceOf(frames, QStringLiteral("info$")) != 0);
        const quint32 infoA = sequenceOf(frames, QStringLiteral("info$"));
        QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Identifying);

        // B replaces A.
        frames.clear();
        QTcpSocket* peerB = answerUpToInfo(model, ampB, frames, "V3.8.9", kInfoReply);
        QVERIFY(peerB);
        if (peerA) {
            peerA->write(QStringLiteral("R%1|0|%2\n").arg(infoA)
                             .arg(QString::fromLatin1(kInfoReply)).toUtf8());
            peerA->flush();
        }
        announce(model, ampA.serverPort(), QStringLiteral("PowerGeniusXL"),
                 QString::fromLatin1(kSerial));
        QTest::qWait(100);
        QVERIFY(!pgxl->isConnected());
        announce(model, ampB.serverPort(), QStringLiteral("PowerGeniusXL"),
                 QString::fromLatin1(kSerial));
        QTRY_VERIFY(pgxl->isConnected());
        QCOMPARE(pgxl->peerPort(), ampB.serverPort());
        QCOMPARE(model.amplifierModel()->configuredPort(), int(ampB.serverPort()));
        QCOMPARE(model.peripheralValue(QStringLiteral("PGXL_ManualPort")),
                 QString::number(ampB.serverPort()));
        QTest::qWait(300);
        QCOMPARE(connectionsA.count(), 1);
        QVERIFY(pgxl->isConnected());
        QVERIFY(model.disconnectPgxlForStation(&reason));
    }

    // Connection settings are saved on the Core and applied to the running
    // connection; a bad value changes nothing.
    void connectionSettingsAreSavedAndApplied()
    {
        const quint16 deadPort = closedLoopbackPort();
        QVERIFY(deadPort != 0);
        RadioModel model;
        prepare(model);
        auto* pgxl = model.pgxlConnection();
        pgxl->testSetReconnectBackoffUnitMs(100);
        QString reason;
        QVERIFY(!model.setPgxlConnectionSettingsForStation(true, 0, 10, &reason));
        QVERIFY(!model.setPgxlConnectionSettingsForStation(true, 30, -1, &reason));
        QVERIFY(!model.setPgxlConnectionSettingsForStation(true, 30, 3601, &reason));
        QVERIFY(AppSettings::instance().value(QStringLiteral("PGXL_KeepaliveSec")).isNull());

        QVERIFY(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), deadPort, &reason));
        QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Retrying);
        QVERIFY(model.setPgxlConnectionSettingsForStation(false, 45, 20, &reason));
        QVERIFY(reason.isEmpty());
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(QStringLiteral("PGXL_AutoReconnect")).toString(), QStringLiteral("False"));
        QCOMPARE(s.value(QStringLiteral("PGXL_KeepaliveSec")).toString(), QStringLiteral("45"));
        QCOMPARE(s.value(QStringLiteral("PGXL_PingSec")).toString(), QStringLiteral("20"));
        QCOMPARE(pgxl->autoPingIntervalSec(), 20);
        // Automatic retry off: the pending retry is gone and the phase says so.
        QVERIFY(!pgxl->testReconnectPending());
        QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Disconnected);
        QVERIFY(model.disconnectPgxlForStation(&reason));
    }

    // A radio's saved address on the Core: dialled through the identity
    // check at radio connect, and a new radio's scope retires the old one.
    void radioScopeDialsThroughIdentityCheck()
    {
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:72");
        auto& settings = AppSettings::instance();
        settings.setHardwareValue(mac, QStringLiteral("peripherals/FourO3A_Enabled"),
                                  QStringLiteral("True"));
        settings.setHardwareValue(mac, QStringLiteral("peripherals/PGXL_ManualIp"),
                                  QStringLiteral("127.0.0.1"));
        settings.setHardwareValue(mac, QStringLiteral("peripherals/PGXL_ManualPort"),
                                  QString::number(amp.serverPort()));
        RadioModel model;
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
        RadioInfo radio;
        radio.macAddress = mac;
        model.setLastRadioInfoForTest(radio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        model.applyPeripheralsForTest();
        QCOMPARE(model.amplifierModel()->configuredHost(), QStringLiteral("127.0.0.1"));
        QTRY_VERIFY(amp.hasPendingConnections());
        QTcpSocket* peer = amp.nextPendingConnection();
        peer->write("V3.8.9\n");
        peer->flush();
        QTRY_COMPARE(model.amplifierModel()->connectionPhase(), Phase::Identifying);
        QVERIFY(!pgxl->isConnected());
        QCOMPARE(commandsOf(frames), QStringList{QStringLiteral("info")});

        model.setConnectionStateForTest(ConnectionState::Disconnected);
        model.teardownPeripheralsForTest();
        QCOMPARE(model.amplifierModel()->connectionPhase(), Phase::Disconnected);
        QVERIFY(!pgxl->testReconnectPending());
    }
};

QTEST_GUILESS_MAIN(StationPgxlControllerTest)
#include "tst_station_pgxl_controller.moc"
