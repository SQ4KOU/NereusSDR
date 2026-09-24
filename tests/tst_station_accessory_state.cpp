// no-port-check: NereusSDR-original. R-R3-22 Core accessory ownership tests.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via OpenAI Codex.
// 2026-09-23: R-R3-47 / R-R3-22 status objects (`amplifier`, `rfkit`): the
// one Power Genius gauge conversion from captured status lines, the RF-Kit
// readings from its REST replies, the connection phases, what a current and
// an older app are offered, the read-only refusal, and the control
// document's fixtures (tests/fixtures/accessories). J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaEnum>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cmath>
#include "core/AppSettings.h"
#include "core/SmartSdrApiListener.h"
#include "core/LanDiscovery.h"
#include "core/StationPgxlController.h"
#include "core/PgxlConnection.h"
#include "core/PgxlStatusGauges.h"
#include "core/Rf2ksConnection.h"
#include "core/TxSliceArbiter.h"
#include "core/session/SessionMessages.h"
#include "core/session/StateMirror.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationServer.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"
#include "models/RfKitModel.h"
#include "models/StationTciModel.h"
#include "models/SliceModel.h"

#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

// Status lines as captured or as the repository's PGXL parser tests carry
// them (tst_pgxl_connection_parse, tst_pgxl_connection_setup and the design
// doc's setup read reply). The transmit frames use the same keys with a
// 60 dBm (1000 W) peak and the -24.5 dB return loss the conversion's own
// bench note uses (SWR 1.13).
constexpr const char* kPgxlOperate = "R1|0|state=OPERATE temp=42.5 vac=240 fwd=1480.0 swr=2.1";
constexpr const char* kPgxlTransmit = "S0|status state=TRANSMIT_A peakfwd=60.0 swr=-24.5 id=22.5";
constexpr const char* kPgxlStandby = "S0|status state=STANDBY peakfwd=60.0 swr=-24.5 id=0.0";
constexpr const char* kPgxlFault = "S0|state state=FAULT fwd=1820.0 swr=2.85 temp=78.0";
constexpr const char* kPgxlSetup = "R2|0|nickname=ShackAmp fan=auto meffa=off led=65";

// REST replies as tst_rf2ks_connection_parse carries them.
constexpr const char* kRfKitInfo =
    R"({"device":"RF2K-S","software_version":{"GUI":200,"controller":267},"custom_device_name":"KG4VCF"})";
constexpr const char* kRfKitPower =
    R"({"temperature":{"value":27.0,"unit":"°C"},"voltage":{"value":52.7,"unit":"V"},"current":{"value":0.0,"unit":"A"},"forward":{"value":850,"max_value":1200,"unit":"W"},"reflected":{"value":3,"max_value":20,"unit":"W"},"swr":{"value":1.4,"max_value":2.1,"unit":""}})";
constexpr const char* kRfKitPowerIdle =
    R"({"temperature":{"value":0.0,"unit":"°C"},"voltage":{"value":0.0,"unit":"V"},"current":{"value":0.0,"unit":"A"},"forward":{"value":0,"max_value":0,"unit":"W"},"reflected":{"value":0,"max_value":0,"unit":"W"},"swr":{"value":1.0,"max_value":1.0,"unit":""}})";

QString fixturePath(const QString& name)
{
    return QStringLiteral(NEREUS_SOURCE_DIR "/tests/fixtures/accessories/") + name;
}

// Every message a StateMirror sends for one watched object: its schema, its
// object, the snapshot marker, and then each later delta, one per line.
class MirrorRecorder {
public:
    MirrorRecorder(const QByteArray& key, QObject* object)
    {
        m_mirror.watch(key, object);
        QObject::connect(&m_mirror, &StateMirror::sessionMessageReady, &m_mirror,
                         [this](const SessionMessage& message) {
            m_lines.append(SessionMessages::encode(message));
        });
        m_mirror.attachSession();
    }
    void flush() { m_mirror.flushCoalescedDeltas(); }
    QByteArray text() const { return m_lines.join('\n') + '\n'; }

private:
    StateMirror m_mirror;
    QList<QByteArray> m_lines;
};

// The fixture is the recorded text. NEREUS_WRITE_ACCESSORY_FIXTURES=1
// rewrites it from the objects (after a deliberate contract change).
bool matchesFixture(const QString& name, const QByteArray& recorded, QString* why)
{
    if (qEnvironmentVariableIntValue("NEREUS_WRITE_ACCESSORY_FIXTURES") == 1) {
        QFile out(fixturePath(name));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            *why = QStringLiteral("cannot write ") + out.fileName();
            return false;
        }
        out.write(recorded);
        return true;
    }
    QFile in(fixturePath(name));
    if (!in.open(QIODevice::ReadOnly)) {
        *why = QStringLiteral("missing ") + in.fileName();
        return false;
    }
    const QByteArray expected = in.readAll();
    if (expected != recorded) {
        *why = QStringLiteral("fixture %1 differs; recorded:\n%2")
                   .arg(name, QString::fromUtf8(recorded));
        return false;
    }
    return true;
}

QList<SessionMessage> decodeLines(const QByteArray& text)
{
    QList<SessionMessage> messages;
    for (const QByteArray& line : text.split('\n')) {
        if (line.trimmed().isEmpty()) {
            continue;
        }
        SessionMessage message;
        if (!SessionMessages::decode(line, &message)) {
            return {};
        }
        messages.append(message);
    }
    return messages;
}

LoopbackTransport* connectRawPeer(QObject* owner, StationServer& server, quint16 minor,
                                  LoopbackTransport** peerOut)
{
    auto* core = new LoopbackTransport(QStringLiteral("core"), owner);
    auto* peer = new LoopbackTransport(QStringLiteral("raw-gui"), owner);
    core->linkTo(peer);
    server.acceptTransport(core);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, minor, 0, QStringLiteral("accessory-test"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    *peerOut = peer;
    return core;
}

QList<SessionMessage> receivedMessages(const LoopbackTransport* peer)
{
    QList<SessionMessage> messages;
    for (const QByteArray& wire : peer->received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)) {
            messages.append(message);
        }
    }
    return messages;
}

bool snapshotDone(const LoopbackTransport* peer)
{
    for (const SessionMessage& m : receivedMessages(peer)) {
        if (m.kind == SessionMessageKind::SnapshotComplete) {
            return true;
        }
    }
    return false;
}

bool namesAccessory(const SessionMessage& m)
{
    return m.objectKey == "amplifier" || m.objectKey == "rfkit"
        || m.className == "AmplifierModel" || m.className == "RfKitModel";
}

} // namespace

class StationAccessoryStateTest : public QObject {
    Q_OBJECT
    static void prepare(RadioModel& model)
    {
        model.enableStationAccessoryIdentity();
        model.setReceiveOnlyStationPolicy(true);
        RadioInfo info;
        info.macAddress = QStringLiteral("aa:bb:cc:dd:ee:61");
        model.setLastRadioInfoForTest(info);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    }
private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    void masterRequiresStationAndLiveMac()
    {
        RadioModel model;
        QString reason;
        QVERIFY(!model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(!reason.isEmpty());
        model.enableStationAccessoryIdentity();
        QVERIFY(!model.setFourO3AEnabledForStation(true, &reason));
        model.setFourO3AEnabled(true);
        QVERIFY(!model.smartSdrListener()->isListening());
        QVERIFY(!model.fourO3AEnabled());

        RadioModel remote(RadioModel::Role::Remote);
        QVERIFY(!remote.setFourO3AEnabledForStation(true, &reason));
        remote.setFourO3AEnabled(true);
        QVERIFY(!remote.smartSdrListener()->isListening());
    }

    void bindFailureIsObservedAndRepeatedEnableRetries()
    {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        const auto port = blocker.serverPort();
        RadioModel model;
        prepare(model);
        model.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, port);
        QSignalSpy status(&model, &RadioModel::fourO3AStatusChanged);
        QString reason;
        // Acceptance is persisted intent; actual bind failure is a separate
        // state. A checkbox must never masquerade as a listening socket.
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(reason.isEmpty());
        QVERIFY(model.fourO3AEnabled());
        QVERIFY(!model.fourO3AListening());
        QVERIFY(!model.fourO3AListenerError().isEmpty());
        QVERIFY(!status.isEmpty());
        AppSettings disk(AppSettings::instance().filePath());
        disk.load();
        QCOMPARE(disk.hardwareValue(model.currentRadioMac(),
            QStringLiteral("peripherals/FourO3A_Enabled")).toString(), QStringLiteral("True"));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QVERIFY(model.fourO3AListenerError().isEmpty());
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(!model.fourO3AListenerError().isEmpty());
        blocker.close();
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QVERIFY(model.fourO3AListening());
        QVERIFY(model.fourO3AListenerError().isEmpty());
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QVERIFY(!model.fourO3AListening());
        QVERIFY(!model.fourO3AEnabled());
        QVERIFY(model.fourO3AListenerError().isEmpty());
    }

    void remoteObservationCannotStartListenerOrWriteSettings()
    {
        RadioModel remote(RadioModel::Role::Remote);
        QSignalSpy enabled(&remote, &RadioModel::fourO3AEnabledChanged);
        QVERIFY(remote.applyMirroredValue("fourO3AEnabled", true).isEmpty());
        QVERIFY(remote.applyMirroredValue("fourO3AListening", true).isEmpty());
        QVERIFY(remote.fourO3AEnabled());
        QVERIFY(remote.fourO3AListening());
        QVERIFY(!remote.smartSdrListener()->isListening());
        QVERIFY(!remote.applyMirroredValue("fourO3AEnabled", QStringLiteral("True")).isEmpty());
        QVERIFY(remote.applyMirroredValue("fourO3AListenerError", QStringLiteral("bind failed")).isEmpty());
        QCOMPARE(remote.fourO3AListenerError(), QStringLiteral("bind failed"));
        remote.clearRemoteFourO3AState();
        QVERIFY(!remote.fourO3AEnabled());
        QVERIFY(!remote.fourO3AListening());
        QVERIFY(remote.fourO3AListenerError().isEmpty());
        QCOMPARE(enabled.count(), 2);
        QVERIFY(AppSettings::instance().hardwareValue(QStringLiteral("aa:bb:cc:dd:ee:61"),
            QStringLiteral("peripherals/FourO3A_Enabled")).toString().isEmpty());
    }

    void debouncedPgxlBandUsesCurrentBinding()
    {
        QTcpServer amp;
        QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
        RadioModel model;
        prepare(model);
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        auto* sliceA = model.sliceById(a);
        auto* sliceB = model.sliceById(b);
        QVERIFY(sliceA && sliceB);
        sliceA->setFrequency(14200000);
        sliceB->setFrequency(3700000);
        QString reason;
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        auto* pgxl = model.pgxlConnection();
        QSignalSpy frames(pgxl, &PgxlConnection::testFrameWrittenForTesting);
        // R-R3-47: the Core admits the amp (discovery plus the same serial
        // in its own info reply, as captured) and only then pairs it. The
        // loopback fixture stands in for the amp; nothing real is contacted.
        QSignalSpy paired(pgxl, &PgxlConnection::pairingResult);
        QVERIFY2(model.configurePgxlForStation(QStringLiteral("127.0.0.1"), amp.serverPort(),
                                               &reason), qPrintable(reason));
        QTRY_VERIFY(amp.hasPendingConnections());
        auto* peer = amp.nextPendingConnection();
        peer->write("V3.8.9\n"); peer->flush();
        const auto sequenceOf = [&](const QString& command) -> quint32 {
            const QRegularExpression rx(QStringLiteral("^C(\\d+)\\|") + command);
            for (const auto& row : frames) {
                const auto match = rx.match(row.first().toString());
                if (match.hasMatch()) { return match.captured(1).toUInt(); }
            }
            return 0;
        };
        QTRY_VERIFY(sequenceOf(QStringLiteral("info$")) != 0);
        peer->write(QStringLiteral("R%1|0|serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240\n")
                        .arg(sequenceOf(QStringLiteral("info$"))).toUtf8());
        peer->flush();
        auto* controller = model.findChild<StationPgxlController*>();
        QVERIFY(controller);
        QTRY_VERIFY(controller->findChild<LanDiscovery*>() != nullptr);
        controller->findChild<LanDiscovery*>()->injectDatagramForTesting(
            QStringLiteral("PowerGeniusXL ip=127.0.0.1 v=3.8.9 serial=10-200/24-0046 nickname=PowerGeniusXL"),
            amp.serverPort());
        QTRY_VERIFY(pgxl->isConnected());
        QTRY_VERIFY(sequenceOf(QStringLiteral("flexradio ampslice=A serial=")) != 0);
        peer->write(QStringLiteral("R%1|0|\n")
                        .arg(sequenceOf(QStringLiteral("flexradio ampslice=A serial="))).toUtf8());
        peer->flush();
        QTRY_COMPARE(paired.count(), 1);
        QVERIFY(paired.first().first().toBool());
        const auto hasInitialBand = [&] {
            for (const auto& frame : frames) {
                if (frame.first().toString().contains(QStringLiteral("band=14200000"))) { return true; }
            }
            return false;
        };
        QTRY_VERIFY(hasInitialBand());
        frames.clear();
        sliceA->setFrequency(14210000); // schedules old binding's debounce
        QVERIFY(model.txSliceArbiter()->requestHandoff(b));
        sliceB->setFrequency(3750000);
        model.setActiveSlice(a);
        sliceA->setFrequency(14220000); // viewed, unrelated receiver
        const auto bands = [&] {
            QStringList result;
            for (const auto& row : frames) {
                const QString frame = row.first().toString();
                if (frame.contains(QStringLiteral(" band="))) { result.append(frame); }
            }
            return result;
        };
        QTRY_VERIFY_WITH_TIMEOUT(bands().join(QLatin1Char(' ')).contains(QStringLiteral("band=3750000")), 800);
        QTest::qWait(250);
        QVERIFY(!bands().join(QLatin1Char(' ')).contains(QStringLiteral("band=142")));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
    }

    void headlessListenerFollowsBoundSliceAcrossRetuneModeAndRemoval()
    {
        RadioModel model;
        prepare(model);
        model.configureStreamPool(5, 5, 192000);
        const int a = model.addSlice();
        const int b = model.addSlice();
        QVERIFY(a >= 0 && b >= 0);
        auto* sliceA = model.sliceById(a);
        auto* sliceB = model.sliceById(b);
        sliceA->setFrequency(14200000);
        sliceA->setDspMode(DSPMode::USB);
        sliceB->setFrequency(3700000);
        sliceB->setDspMode(DSPMode::LSB);
        model.setActiveSlice(b); // Looking at B must not change TX-bound A.
        QString reason;
        QVERIFY(model.setFourO3AEnabledForStation(true, &reason));
        QTcpSocket subscriber;
        QByteArray received;
        connect(&subscriber, &QTcpSocket::readyRead, &subscriber,
                [&] { received += subscriber.readAll(); });
        subscriber.connectToHost(QHostAddress::LocalHost, model.smartSdrListener()->serverPort());
        QTRY_COMPARE(subscriber.state(), QAbstractSocket::ConnectedState);
        subscriber.write("C1|sub slice all\n");
        subscriber.flush();
        QTRY_VERIFY(received.contains("RF_frequency=14.200000"));
        QVERIFY(received.contains("mode=USB"));
        received.clear();
        sliceB->setFrequency(3750000);
        QTest::qWait(60);
        QVERIFY(!received.contains("RF_frequency=3.750000"));
        sliceA->setFrequency(14250000);
        QTRY_VERIFY(received.contains("RF_frequency=14.250000"));
        received.clear();
        sliceA->setDspMode(DSPMode::AM);
        QTRY_VERIFY(received.contains("mode=AM"));
        received.clear();
        QVERIFY(model.txSliceArbiter()->requestHandoff(b));
        QTRY_VERIFY(received.contains("RF_frequency=3.750000"));
        QTRY_VERIFY(received.contains("mode=LSB"));
        received.clear();
        sliceA->setFrequency(14300000);
        QTest::qWait(60);
        QVERIFY(!received.contains("RF_frequency=14.300000"));
        model.removeSlice(b);
        QTRY_VERIFY(received.contains("RF_frequency=14.300000"));
        QTRY_VERIFY(received.contains("mode=AM"));
        QVERIFY(model.setFourO3AEnabledForStation(false, &reason));
        QTRY_COMPARE(subscriber.state(), QAbstractSocket::UnconnectedState);
    }

    // R-R3-47: the one Power Genius conversion, from the captured lines.
    // Same arithmetic and transmit gate as MainWindow's former handler.
    void pgxlGaugesFromCapturedLines()
    {
        PgxlConnection conn;
        AmplifierModel amp;
        connect(&conn, &PgxlConnection::statusUpdated, &amp, &AmplifierModel::applyStatusFrame);
        QVERIFY(!amp.present());

        conn.injectLineForTesting(QString::fromLatin1(kPgxlOperate));
        QVERIFY(amp.present());
        QCOMPARE(amp.state(), AmplifierModel::State::Operate);
        QCOMPARE(amp.deviceState(), QStringLiteral("OPERATE"));
        QVERIFY(amp.operate());
        QVERIFY(!amp.transmitting());
        QCOMPARE(amp.temperatureC(), 42.5);
        QCOMPARE(amp.mainsVoltageV(), 240.0);
        // Not transmitting: the latched peak is not shown.
        QCOMPARE(amp.forwardPowerW(), 0.0);
        QCOMPARE(amp.swr(), 1.0);

        conn.injectLineForTesting(QString::fromLatin1(kPgxlTransmit));
        QCOMPARE(amp.state(), AmplifierModel::State::TransmitA);
        QVERIFY(amp.transmitting());
        QVERIFY(std::abs(amp.forwardPowerW() - 1000.0) < 1e-3);
        QVERIFY(std::abs(amp.swr() - 1.12668) < 1e-4);
        QCOMPARE(amp.drainCurrentA(), 22.5);

        // A frame without a state keeps the last state's transmit gate.
        conn.injectLineForTesting(QStringLiteral("S0|status peakfwd=57.0 swr=-20.0"));
        QVERIFY(std::abs(amp.forwardPowerW() - 501.187) < 1e-2);
        QVERIFY(std::abs(amp.swr() - 1.22222) < 1e-4);

        // Leaving transmit reads 0 W and 1.0 even with the latched peak in
        // the same frame; false and zero are values, not gaps.
        conn.injectLineForTesting(QString::fromLatin1(kPgxlStandby));
        QCOMPARE(amp.state(), AmplifierModel::State::Standby);
        QVERIFY(!amp.operate());
        QVERIFY(!amp.transmitting());
        QCOMPARE(amp.forwardPowerW(), 0.0);
        QCOMPARE(amp.swr(), 1.0);
        QCOMPARE(amp.drainCurrentA(), 0.0);

        conn.injectLineForTesting(QString::fromLatin1(kPgxlFault));
        QCOMPARE(amp.state(), AmplifierModel::State::Fault);
        QCOMPARE(amp.temperatureC(), 78.0);
        QCOMPARE(amp.swr(), 1.0);

        conn.injectLineForTesting(QString::fromLatin1(kPgxlSetup));
        QCOMPARE(amp.efficiencyText(), QStringLiteral("off"));

        // The helpers RadioModel's meter and fault paths share.
        QCOMPARE(pgxlReturnLossToSwr(0.0f), 99.0f);
        QCOMPARE(pgxlReturnLossToSwr(2.85f), 99.0f);
        QCOMPARE(pgxlReturnLossToSwr(-0.001f), 99.0f);
        QVERIFY(std::abs(pgxlDbmToWatts(30.0f) - 1.0f) < 1e-6f);
        QVERIFY(pgxlStateIsOperate(QStringLiteral("IDLE")));
        QVERIFY(!pgxlStateIsOperate(QStringLiteral("STANDBY")));
        QCOMPARE(AmplifierModel::stateFromWord(QStringLiteral("FAULT_TEMP")),
                 AmplifierModel::State::Fault);
        QCOMPARE(AmplifierModel::stateFromWord(QStringLiteral("SOMETHING_NEW")),
                 AmplifierModel::State::Unknown);
    }

    // R-R3-47: the Tuner Genius's connection-state shape, driven by the
    // amp's connection. Loopback only; no amp is contacted.
    void amplifierPhaseFollowsItsConnection()
    {
        AppSettings::instance().setValue(QStringLiteral("PGXL_AutoReconnect"),
                                         QStringLiteral("False"));
        QTcpServer ampServer;
        QVERIFY(ampServer.listen(QHostAddress::LocalHost, 0));
        PgxlConnection conn;
        AmplifierModel amp;
        amp.bindConnection(&conn);
        using Phase = TunerModel::ConnectionPhase;
        amp.setAccessoryEnabled(false);
        QCOMPARE(amp.connectionPhase(), Phase::Disabled);
        amp.setAccessoryEnabled(true);
        QCOMPARE(amp.connectionPhase(), Phase::Disconnected);

        conn.connectToPgxl(QStringLiteral("127.0.0.1"), ampServer.serverPort());
        QTRY_VERIFY(ampServer.hasPendingConnections());
        QTcpSocket* device = ampServer.nextPendingConnection();
        device->write("V3.8.9\nR9|0|state=STANDBY temp=30.0 vac=230 id=0.0\n");
        device->flush();
        QTRY_COMPARE(amp.connectionPhase(), Phase::Connected);
        QCOMPARE(amp.configuredHost(), QStringLiteral("127.0.0.1"));
        QCOMPARE(amp.configuredPort(), int(ampServer.serverPort()));
        QCOMPARE(amp.deviceVersion(), QStringLiteral("3.8.9"));
        QTRY_VERIFY(amp.present());
        QCOMPARE(amp.state(), AmplifierModel::State::Standby);

        // The amp goes away: the phase says so, present says the readings
        // are no longer live, and the last readings stay.
        device->close();
        QTRY_COMPARE(amp.connectionPhase(), Phase::Disconnected);
        QVERIFY(!amp.present());
        QCOMPARE(amp.temperatureC(), 30.0);
        QCOMPARE(amp.mainsVoltageV(), 230.0);

        // The station's switch off reads Disabled.
        amp.setAccessoryEnabled(false);
        QCOMPARE(amp.connectionPhase(), Phase::Disabled);
    }

    // R-R3-47: the RF-Kit's readings from its REST replies.
    void rfKitReadingsFromRestReplies()
    {
        Rf2ksConnection conn;
        RfKitModel rfKit;
        rfKit.bindConnection(&conn);
        conn.injectJsonForTesting(QStringLiteral("/info"), kRfKitInfo);
        QCOMPARE(rfKit.deviceModel(), QStringLiteral("RF2K-S"));
        QCOMPARE(rfKit.deviceVersion(), QStringLiteral("G200C267"));
        QCOMPARE(rfKit.deviceNickname(), QStringLiteral("KG4VCF"));
        QVERIFY(!rfKit.present());

        conn.injectJsonForTesting(QStringLiteral("/power"), kRfKitPower);
        conn.injectJsonForTesting(QStringLiteral("/operate-mode"),
                                  R"({"operate_mode":"OPERATE"})");
        QVERIFY(rfKit.present());
        QVERIFY(rfKit.operate());
        QCOMPARE(rfKit.forwardPowerW(), 850.0);
        QCOMPARE(rfKit.reflectedPowerW(), 3.0);
        QVERIFY(std::abs(rfKit.swr() - 1.4) < 1e-6);
        QCOMPARE(rfKit.temperatureC(), 27.0);
        QVERIFY(std::abs(rfKit.voltageV() - 52.7) < 1e-5);
        QCOMPARE(rfKit.currentA(), 0.0);

        // Standby and an idle amp: false and zeros are readings too.
        QSignalSpy changed(&rfKit, &RfKitModel::statusChanged);
        conn.injectJsonForTesting(QStringLiteral("/operate-mode"),
                                  R"({"operate_mode":"STANDBY"})");
        conn.injectJsonForTesting(QStringLiteral("/power"), kRfKitPowerIdle);
        QVERIFY(changed.count() >= 2);
        QVERIFY(!rfKit.operate());
        QCOMPARE(rfKit.forwardPowerW(), 0.0);
        QCOMPARE(rfKit.swr(), 1.0);
        QCOMPARE(rfKit.voltageV(), 0.0);

        using Phase = TunerModel::ConnectionPhase;
        rfKit.setAccessoryEnabled(false);
        QCOMPARE(rfKit.connectionPhase(), Phase::Disabled);
        rfKit.setAccessoryEnabled(true);
        QCOMPARE(rfKit.connectionPhase(), Phase::Disconnected);
    }

    // R-R3-47 / R-R3-22: a current app is offered both objects and the two
    // versions; an older app gets neither, byte for byte what it got
    // before; a Core that does not own its accessories offers nothing; and
    // a write to either object is refused in plain words.
    void coreOffersStatusObjectsOnlyToCurrentApps()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto run = [&](bool owns, quint16 minor, QList<SessionMessage>* out) {
            RadioModel station;
            if (owns) {
                station.enableStationAccessoryIdentity();
            }
            // The Core's amp has sent one reading (the captured operate line).
            station.amplifierModel()->applyStatusFrame(
                {{QStringLiteral("state"), QStringLiteral("OPERATE")},
                 {QStringLiteral("temp"), QStringLiteral("42.5")},
                 {QStringLiteral("vac"), QStringLiteral("240")}});
            AppSettings settings(dir.filePath(QStringLiteral("s-%1-%2.settings")
                                                  .arg(owns).arg(minor)));
            StationServer server(&station, settings, dir.path());
            LoopbackTransport* peer = nullptr;
            connectRawPeer(this, server, minor, &peer);
            QTRY_VERIFY(snapshotDone(peer));
            if (minor >= kRadioIdentitySessionProtocolMinor && owns) {
                peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
                    "amplifier",
                    {MirrorUpdate{0, "operate", MirrorWireKind::Bool, QVariant(true)}}, 7)));
                peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
                    "rfkit",
                    {MirrorUpdate{0, "operate", MirrorWireKind::Bool, QVariant(true)}}, 8)));
                QTRY_VERIFY([&] {
                    int results = 0;
                    for (const SessionMessage& m : receivedMessages(peer)) {
                        results += m.kind == SessionMessageKind::PropertyResult ? 1 : 0;
                    }
                    return results == 2;
                }());
            }
            *out = receivedMessages(peer);
        };

        QList<SessionMessage> current;
        run(true, kRadioIdentitySessionProtocolMinor, &current);
        StationCapabilities caps;
        bool sawAmplifier = false;
        bool sawRfKit = false;
        QStringList refusals;
        for (const SessionMessage& m : current) {
            if (m.kind == SessionMessageKind::Capabilities) {
                caps = StationCapabilities::fromUpdates(m.updates);
                QCOMPARE(m.updates.at(m.updates.size() - 3).name,
                         QByteArrayLiteral("remotePgxlControlVersion"));
                QCOMPARE(m.updates.at(m.updates.size() - 2).name,
                         QByteArrayLiteral("remoteRfKitControlVersion"));
                // R-R3-48: the station TCI server's version travels last.
                QCOMPARE(m.updates.constLast().name,
                         QByteArrayLiteral("stationTciVersion"));
            }
            if (m.kind == SessionMessageKind::ObjectCreate && m.objectKey == "amplifier") {
                sawAmplifier = true;
                QCOMPARE(m.className, QByteArrayLiteral("AmplifierModel"));
                for (const MirrorUpdate& u : m.updates) {
                    if (u.name == "state") {
                        QCOMPARE(u.kind, MirrorWireKind::Enum);
                        QCOMPARE(u.value.toLongLong(), 4LL);
                    }
                    if (u.name == "temperatureC") {
                        QCOMPARE(u.value.toDouble(), 42.5);
                    }
                }
            }
            if (m.kind == SessionMessageKind::ObjectCreate && m.objectKey == "rfkit") {
                sawRfKit = true;
            }
            if (m.kind == SessionMessageKind::PropertyResult) {
                QCOMPARE(m.propertyResults.size(), 1);
                QVERIFY(!m.propertyResults.first().accepted);
                refusals.append(m.propertyResults.first().reason);
            }
        }
        // R-R3-47: 2 once the Core's PGXL commands are offered (Task 2).
        QCOMPARE(caps.remotePgxlControlVersion, 2);
        // R-R3-47: 2 once the Core's RF-Kit commands are offered (Task 3).
        QCOMPARE(caps.remoteRfKitControlVersion, 2);
        QVERIFY(sawAmplifier);
        QVERIFY(sawRfKit);
        refusals.sort();
        // R-R3-25 / R-R3-47: the Core is receive-only (StationServer sets
        // it), so the amp's `operate` gets the receive-only reason.
        QStringList expected{AmplifierModel::receiveOnlyOperateReason(),
                             RfKitModel::readOnlyReason()};
        expected.sort();
        QCOMPARE(refusals, expected);

        // An older app: neither object, neither version.
        QList<SessionMessage> older;
        run(true, quint16(kRadioIdentitySessionProtocolMinor - 1), &older);
        for (const SessionMessage& m : older) {
            QVERIFY2(!namesAccessory(m), m.objectKey.constData());
            for (const MirrorUpdate& u : m.updates) {
                QVERIFY(u.name != "remotePgxlControlVersion");
                QVERIFY(u.name != "remoteRfKitControlVersion");
                QVERIFY(u.name != "stationTciVersion");
            }
        }

        // A Core that does not own its accessories: version 0, no objects.
        QList<SessionMessage> notOwning;
        run(false, kRadioIdentitySessionProtocolMinor, &notOwning);
        for (const SessionMessage& m : notOwning) {
            QVERIFY(!namesAccessory(m));
            if (m.kind == SessionMessageKind::Capabilities) {
                const StationCapabilities c = StationCapabilities::fromUpdates(m.updates);
                QCOMPARE(c.remotePgxlControlVersion, 0);
                QCOMPARE(c.remoteRfKitControlVersion, 0);
            }
        }
    }

    // R-R3-47: the control document's fixtures are what the objects send.
    // Each file is a list of session messages, one JSON object per line:
    // the schema, the object, the snapshot marker, then deltas.
    void amplifierFixtureIsWhatTheCoreSends()
    {
        PgxlConnection conn;
        AmplifierModel amp;
        connect(&conn, &PgxlConnection::statusUpdated, &amp, &AmplifierModel::applyStatusFrame);
        TunerModel::StationConnectionState state;
        state.configuredHost = QStringLiteral("192.0.2.40");
        state.configuredPort = 9008;
        state.phase = TunerModel::ConnectionPhase::Connected;
        state.deviceVersion = QStringLiteral("3.8.9");
        amp.setStationConnectionState(state);
        conn.injectLineForTesting(QString::fromLatin1(kPgxlOperate));
        conn.injectLineForTesting(QString::fromLatin1(kPgxlSetup));

        MirrorRecorder recorder("amplifier", &amp);
        conn.injectLineForTesting(QString::fromLatin1(kPgxlTransmit));
        recorder.flush();
        conn.injectLineForTesting(QString::fromLatin1(kPgxlStandby));
        recorder.flush();
        // R-R3-48: paired with the radio, the amp follows its band.
        amp.setBandFollow(TunerModel::BandFollow::Following);
        recorder.flush();
        QString why;
        QVERIFY2(matchesFixture(QStringLiteral("amplifier.jsonl"), recorder.text(), &why),
                 qPrintable(why));

        // The fixture parses, and a remote window's object applies it.
        QFile file(fixturePath(QStringLiteral("amplifier.jsonl")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QList<SessionMessage> messages = decodeLines(file.readAll());
        QCOMPARE(messages.size(), 6);
        QCOMPARE(messages.at(0).kind, SessionMessageKind::Schema);
        QCOMPARE(messages.at(1).kind, SessionMessageKind::ObjectCreate);
        QCOMPARE(messages.at(2).kind, SessionMessageKind::SnapshotComplete);
        AmplifierModel window;
        for (const SessionMessage& m : messages) {
            for (const MirrorUpdate& u : m.updates) {
                QVERIFY2(window.applyStationValue(u.name, u.value), u.name.constData());
            }
        }
        QCOMPARE(window.connectionPhase(), TunerModel::ConnectionPhase::Connected);
        QCOMPARE(window.configuredHost(), QStringLiteral("192.0.2.40"));
        QCOMPARE(window.state(), AmplifierModel::State::Standby);
        QCOMPARE(window.forwardPowerW(), 0.0);
        QCOMPARE(window.efficiencyText(), QStringLiteral("off"));
        QCOMPARE(window.bandFollow(), TunerModel::BandFollow::Following);
    }

    void rfKitFixtureIsWhatTheCoreSends()
    {
        Rf2ksConnection conn;
        RfKitModel rfKit;
        rfKit.bindConnection(&conn);
        TunerModel::StationConnectionState state;
        state.configuredHost = QStringLiteral("192.0.2.41");
        state.configuredPort = 8080;
        state.phase = TunerModel::ConnectionPhase::Connected;
        rfKit.setStationConnectionState(state);
        conn.injectJsonForTesting(QStringLiteral("/info"), kRfKitInfo);
        conn.injectJsonForTesting(QStringLiteral("/power"), kRfKitPower);
        conn.injectJsonForTesting(QStringLiteral("/operate-mode"),
                                  R"({"operate_mode":"OPERATE"})");
        // R-R3-47: the interface, antenna and tuner rows (bodies as in
        // tst_rf2ks_connection_parse).
        conn.injectJsonForTesting(QStringLiteral("/operational-interface"),
                                  R"({"operational_interface":"UDP","error":""})");
        conn.injectJsonForTesting(QStringLiteral("/antennas"),
            R"({"antennas":[{"type":"INTERNAL","number":1,"state":"ACTIVE"},{"type":"INTERNAL","number":2,"state":"AVAILABLE"},{"type":"INTERNAL","number":3,"state":"DISABLED"},{"type":"EXTERNAL","state":"AVAILABLE"}]})");
        conn.injectJsonForTesting(QStringLiteral("/antennas/active"),
                                  R"({"type":"INTERNAL","number":1})");

        MirrorRecorder recorder("rfkit", &rfKit);
        conn.injectJsonForTesting(QStringLiteral("/operate-mode"),
                                  R"({"operate_mode":"STANDBY"})");
        conn.injectJsonForTesting(QStringLiteral("/power"), kRfKitPowerIdle);
        recorder.flush();
        // R-R3-47 / R-R3-48: a tune lands, the amp is switched to TCI and
        // the band-follow line names the address to enter on it.
        conn.injectJsonForTesting(QStringLiteral("/tuner"),
            R"({"mode":"AUTO","setup":"LC","L":{"value":1200,"unit":"nH"},"C":{"value":345,"unit":"pF"},"tuned_frequency":{"value":3891,"unit":"kHz"},"segment_size":{"value":9,"unit":"kHz"}})");
        conn.injectJsonForTesting(QStringLiteral("/operational-interface"),
                                  R"({"operational_interface":"TCI","error":""})");
        conn.injectJsonForTesting(QStringLiteral("/antennas/active"),
                                  R"({"type":"INTERNAL","number":2})");
        rfKit.setBandFollow(TunerModel::BandFollow::Waiting, QStringLiteral("192.0.2.10"), 50001);
        recorder.flush();
        QString why;
        QVERIFY2(matchesFixture(QStringLiteral("rfkit.jsonl"), recorder.text(), &why),
                 qPrintable(why));

        QFile file(fixturePath(QStringLiteral("rfkit.jsonl")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QList<SessionMessage> messages = decodeLines(file.readAll());
        QCOMPARE(messages.size(), 5);
        RfKitModel window;
        for (const SessionMessage& m : messages) {
            for (const MirrorUpdate& u : m.updates) {
                QVERIFY2(window.applyStationValue(u.name, u.value), u.name.constData());
            }
        }
        QCOMPARE(window.deviceModel(), QStringLiteral("RF2K-S"));
        QVERIFY(window.present());
        QVERIFY(!window.operate());
        QCOMPARE(window.forwardPowerW(), 0.0);
        QCOMPARE(window.operationalInterface(), QStringLiteral("TCI"));
        QCOMPARE(window.antennaPresentMask(), 0b0111);
        QCOMPARE(window.antennaDisabledMask(), 0b0100);
        QCOMPARE(window.activeAntennaNumber(), 2);
        QVERIFY(!window.activeAntennaExternal());
        QCOMPARE(window.tunerMode(), RfKitModel::TunerMode::Auto);
        QCOMPARE(window.tunerInductanceNh(), 1200);
        QCOMPARE(window.tunerCapacitancePf(), 345);
        QCOMPARE(window.tunerFrequencyKhz(), 3891);
        QCOMPARE(window.tunerSegmentKhz(), 9);
        QCOMPARE(window.tunerSetup(), QStringLiteral("LC"));
        QCOMPARE(window.bandFollow(), TunerModel::BandFollow::Waiting);
        QCOMPARE(window.bandFollowAddress(), QStringLiteral("192.0.2.10"));
        QCOMPARE(window.bandFollowPort(), 50001);
        QCOMPARE(window.bandFollowText(),
                 QStringLiteral("Band follow: enter 192.0.2.10, port 50001 as the TCI server on "
                                "the amplifier."));
    }

    // The fixed enum values, as the document and enums.json list them.
    void enumFixtureMatchesTheEnums()
    {
        QFile file(fixturePath(QStringLiteral("enums.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const auto check = [&](const char* name, const QMetaEnum& meta) {
            const QJsonObject values = root.value(QLatin1String(name)).toObject();
            QCOMPARE(values.size(), meta.keyCount());
            for (int i = 0; i < meta.keyCount(); ++i) {
                QString key = QString::fromLatin1(meta.key(i));
                key[0] = key[0].toLower();
                QVERIFY2(values.contains(key), qPrintable(key));
                QCOMPARE(values.value(key).toInt(-1), meta.value(i));
            }
        };
        check("connectionPhase", QMetaEnum::fromType<TunerModel::ConnectionPhase>());
        check("amplifierState", QMetaEnum::fromType<AmplifierModel::State>());
        // R-R3-47 / R-R3-48: band follow and the RF-Kit tuner's mode.
        check("bandFollow", QMetaEnum::fromType<TunerModel::BandFollow>());
        check("rfkitTunerMode", QMetaEnum::fromType<RfKitModel::TunerMode>());
        const QJsonObject reasons = root.value(QLatin1String("refusals")).toObject();
        QCOMPARE(reasons.value(QLatin1String("amplifierReadOnly")).toString(),
                 AmplifierModel::readOnlyReason());
        QCOMPARE(reasons.value(QLatin1String("rfkitReadOnly")).toString(),
                 RfKitModel::readOnlyReason());
        QCOMPARE(reasons.value(QLatin1String("stationTciReadOnly")).toString(),
                 StationTciModel::readOnlyReason());
    }
};
QTEST_GUILESS_MAIN(StationAccessoryStateTest)
#include "tst_station_accessory_state.moc"
