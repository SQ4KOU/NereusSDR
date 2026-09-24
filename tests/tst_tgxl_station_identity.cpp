// no-port-check: NereusSDR-original. R-R3-22/25 Core accessory admission.
// Loopback TCP peers and captured discovery/info grammar; no RF or hardware.
// 2026-09-24: R-R3-47 / R-R3-22: a window's requests for the tuner's own
// settings reach the (fake) tuner as the local page's own commands. J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "core/AppSettings.h"
#include "core/LanDiscovery.h"
#include "models/AccessorySettingsModel.h"
#include "models/RadioModel.h"
#include "models/TunerModel.h"

using namespace NereusSDR;

class TgxlStationIdentityTest : public QObject {
    Q_OBJECT
    static void prepare(RadioModel& model)
    {
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo radio;
        radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:44");
        model.setLastRadioInfoForTest(radio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    }
    static void announce(LanDiscovery* discovery, quint16 port,
                         const QString& product, const QString& serial)
    {
        announceAt(discovery, QStringLiteral("127.0.0.1"), port, product, serial);
    }
    static void announceAt(LanDiscovery* discovery, const QString& ip,
                           quint16 port, const QString& product,
                           const QString& serial)
    {
        discovery->injectDatagramForTesting(
            QStringLiteral("%1 ip=%2 v=1.2.17 serial=%3 nickname=Tuner_Genius_XL")
                .arg(product, ip, serial), port);
    }
    static quint32 infoSequence(const QSignalSpy& frames)
    {
        const QRegularExpression rx(QStringLiteral("^C(\\d+)\\|info$"));
        for (const auto& args : frames) {
            const auto match = rx.match(args.first().toString());
            if (match.hasMatch()) { return match.captured(1).toUInt(); }
        }
        return 0;
    }
    static void sendInfo(QTcpSocket* peer, quint32 sequence, const QString& serial)
    {
        peer->write(QStringLiteral("R%1|0|info serial=%2 version=1.2.17 nickname=Tuner_Genius_XL 3way=1\n")
                        .arg(sequence).arg(serial).toUtf8());
        peer->flush();
    }
    static void seedScope(const QString& mac, bool enabled,
                          const QString& host, quint16 port = 9010)
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("PeripheralsMigrationDone"),
                          QStringLiteral("True"));
        settings.setHardwareValue(mac,
            QStringLiteral("peripherals/FourO3A_Enabled"),
            enabled ? QStringLiteral("True") : QStringLiteral("False"));
        settings.setHardwareValue(mac,
            QStringLiteral("peripherals/TGXL_ManualIp"), host);
        settings.setHardwareValue(mac,
            QStringLiteral("peripherals/TGXL_ManualPort"), QString::number(port));
    }
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void validatesWholeEndpointBeforeChangingAnything()
    {
        RadioModel model;
        prepare(model);
        model.setPeripheralValue(QStringLiteral("TGXL_ManualIp"), QStringLiteral("saved.example"));
        model.setPeripheralValue(QStringLiteral("TGXL_ManualPort"), QStringLiteral("9010"));
        const auto token = model.tgxlConnection()->socketAttemptToken();
        QString reason;
        for (const auto& host : {QString(), QStringLiteral("bad host"),
                                QStringLiteral("http://127.0.0.1"),
                                QStringLiteral("127.0.0.1\nstatus"), QStringLiteral("-invalid.example")}) {
            QVERIFY(!model.configureTgxlForStation(host, 9010, &reason));
            QVERIFY(!reason.isEmpty());
            QCOMPARE(model.peripheralValue(QStringLiteral("TGXL_ManualIp")), QStringLiteral("saved.example"));
            QCOMPARE(model.peripheralValue(QStringLiteral("TGXL_ManualPort")), QStringLiteral("9010"));
            QCOMPARE(model.tgxlConnection()->socketAttemptToken(), token);
        }
        QVERIFY(!model.configureTgxlForStation(QStringLiteral("127.0.0.1"), 0, &reason));
        model.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("False"));
        QVERIFY(!model.configureTgxlForStation(QStringLiteral("127.0.0.1"), 9010, &reason));
        model.setConnectionStateForTest(ConnectionState::Disconnected);
        QVERIFY(!model.configureTgxlForStation(QStringLiteral("127.0.0.1"), 9010, &reason));
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }

    void coldDisabledScopePublishesSavedEndpoint()
    {
        const QString mac = QStringLiteral("aa:bb:cc:dd:ee:51");
        seedScope(mac, false, QStringLiteral("saved-tuner.example"), 9010);
        RadioModel model;
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo radio;
        radio.macAddress = mac;
        model.setLastRadioInfoForTest(radio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.applyPeripheralsForTest();

        QCOMPARE(model.tunerModel()->configuredHost(),
                 QStringLiteral("saved-tuner.example"));
        QCOMPARE(model.tunerModel()->configuredPort(), 9010);
        QCOMPARE(model.tunerModel()->connectionPhase(),
                 TunerModel::ConnectionPhase::Disabled);
        QVERIFY(!model.tgxlConnection()->isConnected());
        QCOMPARE(model.tgxlConnection()->socketAttemptToken(), quint64(0));
    }

    void changingMacScopeClearsRetiredEndpointBeforeDisabledScope()
    {
        QTcpServer oldServer;
        QVERIFY(oldServer.listen(QHostAddress::LocalHost, 0));
        const QString oldMac = QStringLiteral("aa:bb:cc:dd:ee:52");
        const QString newMac = QStringLiteral("aa:bb:cc:dd:ee:53");
        seedScope(oldMac, true, QStringLiteral("127.0.0.1"),
                  oldServer.serverPort());
        seedScope(newMac, false, QString{}, 9010);

        RadioModel model;
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo oldRadio;
        oldRadio.macAddress = oldMac;
        model.setLastRadioInfoForTest(oldRadio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.applyPeripheralsForTest();
        QTRY_VERIFY_WITH_TIMEOUT(oldServer.hasPendingConnections(), 1500);
        auto* oldPeer = oldServer.nextPendingConnection();
        QVERIFY(oldPeer);
        const quint64 retiredAttempt = model.tgxlConnection()->socketAttemptToken();

        model.setConnectionStateForTest(ConnectionState::Disconnected);
        model.teardownPeripheralsForTest();
        QVERIFY(!model.tgxlConnection()->testReconnectPending());
        RadioInfo newRadio;
        newRadio.macAddress = newMac;
        model.setLastRadioInfoForTest(newRadio);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.applyPeripheralsForTest();

        QCOMPARE(model.tunerModel()->configuredHost(), QString{});
        QCOMPARE(model.tunerModel()->configuredPort(), 9010);
        QCOMPARE(model.tunerModel()->connectionPhase(),
                 TunerModel::ConnectionPhase::Disabled);
        model.tgxlConnection()->testInjectFailureForSocketAttempt(retiredAttempt);
        QTest::qWait(50);
        QVERIFY(!model.tgxlConnection()->testReconnectPending());
        QVERIFY(!oldServer.hasPendingConnections());
    }

    void admittedOnlyByMatchingDiscoveryAndNativeInfo_data()
    {
        QTest::addColumn<QString>("product");
        QTest::newRow("observed-alias") << QStringLiteral("TunerGenius");
        QTest::newRow("canonical-alias") << QStringLiteral("TunerGeniusXL");
    }
    void admittedOnlyByMatchingDiscoveryAndNativeInfo()
    {
        QFETCH(QString, product);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        QSignalSpy frames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY2(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), server.serverPort(), &reason), qPrintable(reason));
        QCOMPARE(model.peripheralValue(QStringLiteral("TGXL_ManualPort")), QString::number(server.serverPort()));
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1500);
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY_WITH_TIMEOUT(infoSequence(frames) != 0, 1500);
        auto* discovery = model.findChild<LanDiscovery*>();
        QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_COMPARE(model.tgxlConnection()->identityInfo().serial, QStringLiteral("241288-1"));
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        QVERIFY(!model.tunerModel()->isPresent());
        announce(discovery, server.serverPort(), product, QStringLiteral("241288-1"));
        QTRY_VERIFY(model.tunerModel()->hasDirectConnection());
        QVERIFY(model.tunerModel()->isPresent());
        QVERIFY(model.tunerModel()->hasAntennaSwitch());
        QCOMPARE(model.tunerModel()->connectionPhase(), TunerModel::ConnectionPhase::Connected);
        QCOMPARE(model.tunerModel()->deviceModel(), product);
        QCOMPARE(model.tunerModel()->deviceSerial(), QStringLiteral("241288-1"));
        QCOMPARE(model.tunerModel()->tgxlIp(), QStringLiteral("127.0.0.1"));
        QVERIFY(model.disconnectTgxlForStation(&reason));
        QVERIFY(!model.tunerModel()->isPresent());
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        QCOMPARE(model.tunerModel()->antennaA(), 0);
    }

    void rejectsWrongIdentity_data()
    {
        QTest::addColumn<QString>("product");
        QTest::addColumn<QString>("serial");
        QTest::newRow("amplifier") << QStringLiteral("PowerGeniusXL") << QStringLiteral("241288-1");
        QTest::newRow("different-serial") << QStringLiteral("TunerGenius") << QStringLiteral("different-device");
    }
    void rejectsWrongIdentity()
    {
        QFETCH(QString, product); QFETCH(QString, serial);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        QSignalSpy frames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), server.serverPort(), &reason));
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY(infoSequence(frames) != 0);
        auto* discovery = model.findChild<LanDiscovery*>(); QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_VERIFY(!model.tgxlConnection()->identityInfo().serial.isEmpty());
        announce(discovery, server.serverPort(), product, serial);
        QTRY_VERIFY(!model.tunerModel()->connectionError().isEmpty());
        QVERIFY(!model.tgxlConnection()->isConnected());
        QVERIFY(!model.tunerModel()->isPresent());
        for (const auto& args : frames) { QVERIFY(args.first().toString().endsWith(QStringLiteral("|info"))); }
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }

    void pendingLifecycleCancellation_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("disconnect") << 0;
        QTest::newRow("master-disable") << 1;
        QTest::newRow("radio-teardown") << 2;
    }

    void discoveryMustMatchActualPeerPortAndHasBoundedFailure()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        QSignalSpy frames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("localhost"), server.serverPort(), &reason));
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY(infoSequence(frames) != 0);
        auto* discovery = model.findChild<LanDiscovery*>(); QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_VERIFY(!model.tgxlConnection()->identityInfo().serial.isEmpty());
        announce(discovery, server.serverPort() == 9010 ? 9008 : 9010,
                 QStringLiteral("TunerGenius"), QStringLiteral("241288-1"));
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        QVERIFY(QMetaObject::invokeMethod(discovery, "onTimeout", Qt::DirectConnection));
        QVERIFY(model.tunerModel()->connectionError().contains(QStringLiteral("No matching TGXL")));
        QVERIFY(!model.tunerModel()->isPresent());
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }

    void sameSerialWrongEndpointCannotSuppressMatchingDiscovery_data()
    {
        QTest::addColumn<QString>("wrongIp");
        QTest::addColumn<bool>("wrongPort");
        QTest::newRow("wrong IP first") << QStringLiteral("127.0.0.2") << false;
        QTest::newRow("wrong port first") << QStringLiteral("127.0.0.1") << true;
    }

    void sameSerialWrongEndpointCannotSuppressMatchingDiscovery()
    {
        QFETCH(QString, wrongIp);
        QFETCH(bool, wrongPort);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        QSignalSpy frames(model.tgxlConnection(),
                          &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"),
                                              server.serverPort(), &reason));
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY(infoSequence(frames) != 0);
        auto* discovery = model.findChild<LanDiscovery*>();
        QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_COMPARE(model.tgxlConnection()->identityInfo().serial,
                     QStringLiteral("241288-1"));

        const quint16 firstPort = wrongPort
            ? quint16(server.serverPort() == 9010 ? 9008 : 9010)
            : server.serverPort();
        announceAt(discovery, wrongIp, firstPort,
                   QStringLiteral("TunerGenius"), QStringLiteral("241288-1"));
        QVERIFY(!model.tunerModel()->hasDirectConnection());

        // Identity-sensitive discovery must not let the same-serial record at
        // another endpoint consume the valid current-peer announcement.
        announce(discovery, server.serverPort(), QStringLiteral("TunerGenius"),
                 QStringLiteral("241288-1"));
        QTRY_VERIFY_WITH_TIMEOUT(model.tunerModel()->hasDirectConnection(), 1000);
        QCOMPARE(model.tunerModel()->connectionPhase(),
                 TunerModel::ConnectionPhase::Connected);
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }

    void provisionalPeerDropRemainsRetryingUntilCancelled()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        model.tgxlConnection()->testSetReconnectBackoffUnitMs(500);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"),
                                              server.serverPort(), &reason));
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_COMPARE(model.tunerModel()->connectionPhase(),
                     TunerModel::ConnectionPhase::Identifying);

        // Closing before native info produces Qt's paired error/disconnected
        // callbacks. The owned retry is authoritative even if disconnected is
        // delivered after connectionFailed/reconnectAttempt.
        peer->disconnectFromHost();
        peer->deleteLater();
        QTRY_VERIFY_WITH_TIMEOUT(model.tgxlConnection()->testReconnectPending(),
                                 1500);
        QCOMPARE(model.tunerModel()->connectionPhase(),
                 TunerModel::ConnectionPhase::Retrying);

        QVERIFY(model.disconnectTgxlForStation(&reason));
        QVERIFY(!model.tgxlConnection()->testReconnectPending());
        QCOMPARE(model.tunerModel()->connectionPhase(),
                 TunerModel::ConnectionPhase::Disconnected);
        QTest::qWait(550);
        QVERIFY(!server.hasPendingConnections());
    }

    void endpointReplacementRejectsStaleDiscoveryAndInfo()
    {
        QTcpServer oldServer, newServer;
        QVERIFY(oldServer.listen(QHostAddress::LocalHost, 0));
        QVERIFY(newServer.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        QSignalSpy frames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), oldServer.serverPort(), &reason));
        QTRY_VERIFY(oldServer.hasPendingConnections());
        auto* oldPeer = oldServer.nextPendingConnection();
        oldPeer->write("V1.2.17\n"); oldPeer->flush();
        QTRY_VERIFY(infoSequence(frames) != 0);
        QPointer<LanDiscovery> oldDiscovery = model.findChild<LanDiscovery*>(); QVERIFY(oldDiscovery);
        const auto oldToken = model.tgxlConnection()->socketAttemptToken();
        const auto oldSequence = infoSequence(frames);
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), newServer.serverPort(), &reason));
        // A callback already in flight from the old scan cannot admit B.
        if (oldDiscovery) {
            announce(oldDiscovery, newServer.serverPort(), QStringLiteral("TunerGenius"), QStringLiteral("241288-1"));
        }
        model.tgxlConnection()->testInjectLineForSocketAttempt(
            QStringLiteral("R%1|0|info serial=241288-1 version=1.2.17 nickname=Tuner_Genius_XL")
                .arg(oldSequence), oldToken);
        QVERIFY(!model.tunerModel()->isPresent());
        frames.clear();
        QTRY_VERIFY(newServer.hasPendingConnections());
        auto* peer = newServer.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY(infoSequence(frames) != 0);
        QTRY_VERIFY(oldDiscovery.isNull());
        auto* discovery = model.findChild<LanDiscovery*>(); QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_VERIFY(!model.tgxlConnection()->identityInfo().serial.isEmpty());
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        announce(discovery, newServer.serverPort(), QStringLiteral("TunerGenius"), QStringLiteral("241288-1"));
        QTRY_VERIFY(model.tunerModel()->hasDirectConnection());
        QCOMPARE(model.tunerModel()->configuredPort(), int(newServer.serverPort()));
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }
    // R-R3-47 / R-R3-22: a window's requests for the tuner's own settings
    // (name, network, Save & Reboot, Revert). The Core sends each as the
    // local Advanced page's own command (the fake tuner records the
    // bytes), matches the answer and publishes it in plain words; the
    // tuner's name read back is saved as the local page saves it.
    void deviceSettingsReachTheTunerAsTheLocalPageSendsThem()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        const AccessorySettingsModel* settings = model.accessorySettingsModel();
        QSignalSpy frames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
        QString reason;
        QVERIFY(!model.setTgxlNameForStation(QStringLiteral("Tuner"), &reason));
        QCOMPARE(reason, QStringLiteral("The Core is not connected to the Tuner Genius."));
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), server.serverPort(),
                                              &reason));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 1500);
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY_WITH_TIMEOUT(infoSequence(frames) != 0, 1500);
        auto* discovery = model.findChild<LanDiscovery*>();
        QVERIFY(discovery);
        sendInfo(peer, infoSequence(frames), QStringLiteral("241288-1"));
        QTRY_COMPARE(model.tgxlConnection()->identityInfo().serial, QStringLiteral("241288-1"));
        QVERIFY(!model.saveTgxlSettingsForStation(&reason));   // not admitted yet
        announce(discovery, server.serverPort(), QStringLiteral("TunerGeniusXL"),
                 QStringLiteral("241288-1"));
        QTRY_VERIFY(model.tgxlConnection()->isConnected());

        QByteArray pending;
        QStringList lines;
        const auto waitFor = [&](const QString& command) {
            quint32 seq = 0;
            (void)QTest::qWaitFor([&] {   // 0 when it never arrives
                pending += peer->readAll();
                qsizetype nl = 0;
                while ((nl = pending.indexOf('\n')) >= 0) {
                    lines.append(QString::fromUtf8(pending.left(nl)));
                    pending.remove(0, nl + 1);
                }
                for (const QString& line : lines) {
                    const qsizetype bar = line.indexOf(QLatin1Char('|'));
                    if (bar > 1 && line.mid(bar + 1) == command) {
                        seq = line.mid(1, bar - 1).toUInt();
                        lines.removeOne(line);
                        return true;
                    }
                }
                return false;
            }, 3000);
            return seq;
        };
        const auto answer = [&](quint32 seq, const QString& codeAndBody) {
            peer->write(QStringLiteral("R%1|%2\n").arg(seq).arg(codeAndBody).toUtf8());
            peer->flush();
        };

        QVERIFY(model.setTgxlNameForStation(QStringLiteral("Shack Tuner"), &reason));
        quint32 seq = waitFor(QStringLiteral("setup nickname=Shack Tuner"));
        QVERIFY(seq != 0);
        answer(seq, QStringLiteral("0|"));
        QTRY_COMPARE(settings->tgxlNickname(), QStringLiteral("Shack Tuner"));
        QCOMPARE(settings->tgxlAnswer(), QStringLiteral("The Tuner Genius took the new name."));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("TGXL_Nickname")).toString(),
                 QStringLiteral("Shack Tuner"));

        QVERIFY(model.setTgxlNetworkForStation(true, QString(), QString(), QString(), &reason));
        seq = waitFor(QStringLiteral("ifconf address= netmask= gateway= dhcp=true"));
        QVERIFY(seq != 0);
        answer(seq, QStringLiteral("1|"));
        QTRY_COMPARE(settings->tgxlAnswer(),
                     QStringLiteral("The Tuner Genius did not take the new network settings."));
        QVERIFY(!settings->tgxlNetworkKnown());

        // Revert: the tuner's name read back is saved (TgxlAdvancedPage's
        // onSetupResponse does the same).
        QVERIFY(model.readTgxlSettingsForStation(&reason));
        const quint32 setupSeq = waitFor(QStringLiteral("setup read"));
        const quint32 ifconfSeq = waitFor(QStringLiteral("ifconf read"));
        QVERIFY(setupSeq != 0 && ifconfSeq != 0);
        answer(setupSeq, QStringLiteral("0|nickname=Tuner_Genius_XL"));
        answer(ifconfSeq, QStringLiteral("0|dhcp=0 ip=192.168.1.60 netmask=255.255.255.0 "
                                         "gateway=192.168.1.1"));
        QTRY_COMPARE(settings->tgxlAddress(), QStringLiteral("192.168.1.60"));
        QCOMPARE(settings->tgxlNickname(), QStringLiteral("Tuner_Genius_XL"));
        QCOMPARE(AppSettings::instance().value(QStringLiteral("TGXL_Nickname")).toString(),
                 QStringLiteral("Tuner_Genius_XL"));
        QVERIFY(!settings->tgxlDhcp());

        QVERIFY(model.saveTgxlSettingsForStation(&reason));
        seq = waitFor(QStringLiteral("save"));
        QVERIFY(seq != 0);
        answer(seq, QStringLiteral("0|saving"));
        QTRY_COMPARE(settings->tgxlAnswer(),
                     QStringLiteral("The Tuner Genius is saving its settings and restarting."));
        // The Power Genius's record is untouched.
        QCOMPARE(settings->pgxlAnswerCount(), qint64(0));
        // Nothing operated the tuner or moved its antenna.
        for (const auto& row : frames) {
            const QString frame = row.first().toString();
            QVERIFY2(!frame.contains(QStringLiteral("operate"))
                         && !frame.contains(QStringLiteral("bypass"))
                         && !frame.contains(QStringLiteral("antenna")), qPrintable(frame));
        }
        QVERIFY(model.disconnectTgxlForStation(&reason));
    }

    void pendingLifecycleCancellation()
    {
        QFETCH(int, action);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        RadioModel model; prepare(model);
        model.tgxlConnection()->testSetReconnectBackoffUnitMs(10);
        QString reason;
        QVERIFY(model.configureTgxlForStation(QStringLiteral("127.0.0.1"), server.serverPort(), &reason));
        QTRY_VERIFY(server.hasPendingConnections());
        auto* peer = server.nextPendingConnection();
        peer->write("V1.2.17\n"); peer->flush();
        QTRY_VERIFY(model.findChild<LanDiscovery*>());
        const auto token = model.tgxlConnection()->socketAttemptToken();
        if (action == 0) { QVERIFY(model.disconnectTgxlForStation(&reason)); }
        if (action == 1) { model.setFourO3AEnabled(false); }
        if (action == 2) { model.teardownPeripheralsForTest(); }
        model.tgxlConnection()->testInjectFailureForSocketAttempt(token);
        QTest::qWait(80);
        QVERIFY(!server.hasPendingConnections());
        QVERIFY(!model.tgxlConnection()->testReconnectPending());
        QVERIFY(!model.tunerModel()->hasDirectConnection());
        QVERIFY(!model.tunerModel()->isPresent());
    }
};
QTEST_GUILESS_MAIN(TgxlStationIdentityTest)
#include "tst_tgxl_station_identity.moc"
