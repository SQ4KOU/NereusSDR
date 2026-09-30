// no-port-check: NereusSDR-original test. The logic it checks is ported
// from Thetis TCIServer.cs and cmaster.cs [v2.10.3.15] (cited at each case).
// =================================================================
// tests/tst_tci_server_settings_real.cpp  (NereusSDR)
// =================================================================
//
// The TCI Server page's settings that used to be saved and never read
// (tci-extras) do what Thetis's TCI server does with each.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-29: the three second-receiver VFO options. J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QSignalSpy>
#include <QWebSocket>

#include <vector>

#include "core/AppSettings.h"
#include "core/TciBinaryFrame.h"
#include "core/TciClientSession.h"
#include "core/TciProtocol.h"
#include "core/TciServer.h"
#include "TestMockRadioModel.h"

using namespace NereusSDR;

namespace {

// An app's stereo transmit audio frame: `frames` frames of left and right.
QByteArray stereoTxFrame(int frames, float left, float right)
{
    std::vector<float> samples;
    for (int i = 0; i < frames; ++i) {
        samples.push_back(left);
        samples.push_back(right);
    }
    return TciBinaryFrame::buildStreamPayload(
        /*receiver=*/0, /*sampleRate=*/48000,
        /*sampleType=*/static_cast<int>(TciSampleType::Float32),
        /*length=*/frames * 2,
        /*streamType=*/static_cast<int>(TciStreamType::TxAudioStream),
        /*channels=*/2, samples.data());
}

// A radio whose receivers have a centre apart from their VFO, so an if
// line shows which receiver it was read from.
class CentredMockRadio : public TestMockRadioModel {
    Q_OBJECT
public:
    Q_INVOKABLE qint64 ddsHz(int slice) const { return slice == 0 ? m_centre0 : m_centre1; }
    qint64 m_centre0 = 0;
    qint64 m_centre1 = 0;
};

// Every line the VFO path queued, in order.
QStringList drainLines(TciProtocol& protocol)
{
    protocol.drainCoalescedNotifications();
    QStringList out;
    while (protocol.hasPendingNotification()) {
        out << protocol.takePendingNotification();
    }
    return out;
}

void setOption(const char* key, bool on)
{
    AppSettings::instance().setValue(QLatin1String(key),
                                     on ? QStringLiteral("True") : QStringLiteral("False"));
}

// Receiver 0 at 7.100 MHz (centre 7.090), receiver 1 at 14.200 MHz
// (centre 14.150), RX2 on or off.
void tuneTwoReceivers(CentredMockRadio& radio, bool rx2On)
{
    radio.setRx2Enabled(rx2On);
    radio.setVfoHz(0, 0, 7'100'000);
    radio.setVfoHz(0, 1, 7'100'000);
    radio.setVfoHz(1, 0, 14'200'000);
    radio.setVfoHz(1, 1, 14'200'000);
    radio.m_centre0 = 7'090'000;
    radio.m_centre1 = 14'150'000;
}

// What a receiver's VFO move sent before these options did anything:
// if and vfo for channel 0, then channel 1.
QStringList bothChannels(int rx, qint64 hz, int ifHz)
{
    return {QStringLiteral("if:%1,0,%2;").arg(rx).arg(ifHz),
            QStringLiteral("vfo:%1,0,%2;").arg(rx).arg(hz),
            QStringLiteral("if:%1,1,%2;").arg(rx).arg(ifHz),
            QStringLiteral("vfo:%1,1,%2;").arg(rx).arg(hz)};
}

} // namespace

class TestTciServerSettingsReal : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // Thetis TCIServer.cs:4003-4025 [v2.10.3.15]: with "CW becomes CWU if
    // 10MHz and above" on, a client's `modulation:rx,cw` sets CWU when the
    // transmitting VFO is at 10 MHz or above and CWL below; off, `cw` is
    // always CWL.
    void cwBecomesCwuAtAndAbove10MHz_data()
    {
        QTest::addColumn<bool>("on");
        QTest::addColumn<qint64>("txHz");
        QTest::addColumn<QString>("expected");
        QTest::newRow("on, 14.074 MHz") << true << qint64(14'074'000) << QStringLiteral("CWU");
        QTest::newRow("on, exactly 10 MHz") << true << qint64(10'000'000) << QStringLiteral("CWU");
        QTest::newRow("on, 7.030 MHz") << true << qint64(7'030'000) << QStringLiteral("CWL");
        QTest::newRow("off, 14.074 MHz") << false << qint64(14'074'000) << QStringLiteral("CWL");
    }
    void cwBecomesCwuAtAndAbove10MHz()
    {
        QFETCH(bool, on);
        QFETCH(qint64, txHz);
        QFETCH(QString, expected);
        AppSettings::instance().setValue(QStringLiteral("TciCwBecomesCwuAbove10mhz"),
                                         on ? QStringLiteral("True") : QStringLiteral("False"));
        for (int rx = 0; rx < 2; ++rx) {
            TestMockRadioModel mock;
            mock.setTransmitVfoHzForTest(txHz);
            mock.setVfoHz(rx, 0, 3'500'000);   // the receiver's own VFO does not decide
            TciProtocol protocol(&mock);
            protocol.handleCommand(QStringLiteral("modulation:%1,cw;").arg(rx));
            QCOMPARE(mock.mode(rx), expected);
        }
    }

    // Thetis cmaster.cs:1401-1427 [v2.10.3.15]: a stereo block becomes
    // mono by the TX channel: Left, Right, or Both averaged.
    void txChannelFoldsStereoToMono()
    {
        const std::vector<float> stereo{0.2f, 0.6f, -0.4f, 0.0f};
        for (const auto& [mode, first, second] :
             {std::tuple{TciServer::TxStereoInputMode::Left, 0.2f, -0.4f},
              std::tuple{TciServer::TxStereoInputMode::Right, 0.6f, 0.0f},
              std::tuple{TciServer::TxStereoInputMode::Both, 0.4f, -0.2f}}) {
            std::vector<float> samples = stereo;
            TciServer::foldTxStereoToMono(samples.data(), 2, mode);
            QCOMPARE(samples.at(0), first);
            QCOMPARE(samples.at(1), second);
        }
        QCOMPARE(TciServer::txStereoInputModeFromText(QStringLiteral("Right")),
                 TciServer::TxStereoInputMode::Right);
        QCOMPARE(TciServer::txStereoInputModeFromText(QStringLiteral("Left")),
                 TciServer::TxStereoInputMode::Left);
        QCOMPARE(TciServer::txStereoInputModeFromText(QString()),
                 TciServer::TxStereoInputMode::Both);
    }

    // ...and the server reads the TX channel when it starts and sends the
    // transmitter one mono sample per stereo frame.
    void theServerSendsTheTxChannelItReadsAtStart()
    {
        AppSettings::instance().setValue(QStringLiteral("TciTxChannel"), QStringLiteral("Right"));
        TciServer server(nullptr);
        QVERIFY(server.start(0));
        QCOMPARE(server.txStereoInputMode(), TciServer::TxStereoInputMode::Right);
        QWebSocket app;
        QSignalSpy connected(&app, &QWebSocket::connected);
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
        QVERIFY(connected.wait(2000));
        app.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
        QTRY_COMPARE(server.activeTxClientCount(), 1);
        const int before = server.peekTxRingSize();
        app.sendBinaryMessage(stereoTxFrame(64, 0.25f, 0.75f));
        QTRY_VERIFY(server.peekTxRingSize() > before);
        // 64 mono float samples, not 128 stereo ones.
        QCOMPARE(server.peekTxRingSize() - before, 64 * int(sizeof(float)));
        app.sendTextMessage(QStringLiteral("trx:0,false;"));
        QTRY_COMPARE(server.activeTxClientCount(), 0);
        app.close();
        server.stop();
    }

    // The RX and TX sensor intervals an app gets when its
    // rx_sensors_enable / tx_sensors_enable names none: Thetis's 200 ms
    // (TCIServer.cs:486-487, 4636, 4647 [v2.10.3.15]) is Setup's value,
    // held to 30 to 1000 (clampIntervalMs, TCIServer.cs:500-505).
    void sensorIntervalsAreTheAppsDefault()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("TciRxSensorIntervalMs"), QStringLiteral("500"));
        settings.setValue(QStringLiteral("TciTxSensorIntervalMs"), QStringLiteral("5"));
        TciServer server(nullptr);
        QVERIFY(server.start(0));
        QWebSocket app;
        QSignalSpy connected(&app, &QWebSocket::connected);
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
        QVERIFY(connected.wait(2000));
        QTRY_COMPARE(server.clients().size(), 1);
        const std::shared_ptr<TciClientSession> session = server.clients().cbegin().value();
        QCOMPARE(session->rxSensorIntervalMs, 500);
        QCOMPARE(session->txSensorIntervalMs, 30);
        // Enabled with no interval: the default holds.
        app.sendTextMessage(QStringLiteral("rx_sensors_enable:true;"));
        QTRY_VERIFY(session->rxSensorsEnabled);
        QCOMPARE(session->rxSensorIntervalMs, 500);
        // An app that names one gets it.
        app.sendTextMessage(QStringLiteral("rx_sensors_enable:true,300;"));
        QTRY_COMPARE(session->rxSensorIntervalMs, 300);
        app.close();
        server.stop();
    }
    // Thetis keeps each app's audio stream channel count, 2 until the app
    // sends audio_stream_channels:1 or :2 (m_audioStreamChannels = 2,
    // TCIServer.cs:781; handleAudioStreamChannels, TCIServer.cs:6340-6354
    // [v2.10.3.15]) and announces it in the app's first lines
    // (sendAudioStreamChannels, TCIServer.cs:2645). Setup > Audio > TCI's
    // Channels is that starting count; anything but 1 or 2 is Thetis's 2.
    void streamChannelsAreTheAppsStartingCount_data()
    {
        QTest::addColumn<QString>("saved");
        QTest::addColumn<int>("expected");
        QTest::newRow("mono") << QStringLiteral("1") << 1;
        QTest::newRow("stereo") << QStringLiteral("2") << 2;
        QTest::newRow("not saved") << QString() << 2;
        QTest::newRow("out of range") << QStringLiteral("3") << 2;
    }
    void streamChannelsAreTheAppsStartingCount()
    {
        QFETCH(QString, saved);
        QFETCH(int, expected);
        if (!saved.isEmpty()) {
            AppSettings::instance().setValue(QStringLiteral("TciAudioStreamChannels"), saved);
        }
        TciServer server(nullptr);
        QVERIFY(server.start(0));
        QWebSocket app;
        QStringList lines;
        connect(&app, &QWebSocket::textMessageReceived, &app,
                [&lines](const QString& text) { lines << text; });
        QSignalSpy connected(&app, &QWebSocket::connected);
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(server.port())));
        QVERIFY(connected.wait(2000));
        QTRY_COMPARE(server.clients().size(), 1);
        const std::shared_ptr<TciClientSession> session = server.clients().cbegin().value();
        QCOMPARE(session->audioStreamChannels, expected);
        const QString announced = QStringLiteral("audio_stream_channels:%1;").arg(expected);
        QTRY_VERIFY2(lines.contains(announced), qPrintable(lines.join(QLatin1Char(' '))));
        // The app's own choice still wins, as in Thetis.
        const int other = expected == 1 ? 2 : 1;
        app.sendTextMessage(QStringLiteral("audio_stream_channels:%1;").arg(other));
        QTRY_COMPARE(session->audioStreamChannels, other);
        app.close();
        server.stop();
    }

    // ── The second-receiver VFO options ──────────────────────────────────

    // The defaults (JJ's ruling, 2026-09-29): Copy on and Forget off as in Thetis
    // (setup.cs:380-382 [v2.10.3.15]); Use RX1 VFO A off, a recorded
    // divergence (Thetis turns it on).
    void rx2VfoOptionDefaults()
    {
        QCOMPARE(kTciCopyRx2VfobToVfoaDefault, true);
        QCOMPARE(kTciForgetRx2VfobDefault, false);
        QCOMPARE(kTciUseRx1VfoaForRx2VfoaDefault, false);
        QCOMPARE(TciProtocol::copyRx2VfobToVfoaSetting(), true);
        QCOMPARE(TciProtocol::forgetRx2VfobSetting(), false);
        QCOMPARE(TciProtocol::useRx1VfoaForRx2VfoaSetting(), false);
    }

    // Under the defaults an app sees what it saw before, with two Thetis
    // corrections: with RX2 on, the second receiver's move goes out as
    // channel 1 then channel 0 (TCIServer.cs:1386-1392 [v2.10.3.15]); with
    // RX2 off, a set for the second receiver is ignored and not echoed
    // (TCIServer.cs:3897-3899 [v2.10.3.15]). The vfo commands act on each
    // receiver's own slice, and the first lines are unchanged.
    void rx2VfoOptionDefaultsKeepTheWire_data()
    {
        QTest::addColumn<bool>("rx2On");
        QTest::newRow("RX2 on") << true;
        QTest::newRow("RX2 off") << false;
    }
    void rx2VfoOptionDefaultsKeepTheWire()
    {
        QFETCH(bool, rx2On);
        CentredMockRadio radio;
        tuneTwoReceivers(radio, rx2On);
        TciProtocol protocol(&radio);

        protocol.enqueueLocalBroadcastVfo(0, 7'100'000, false);
        QCOMPARE(drainLines(protocol), bothChannels(0, 7'100'000, 10'000));
        protocol.enqueueLocalBroadcastVfo(1, 14'200'000, false);
        const QStringList rx1Both = bothChannels(1, 14'200'000, 50'000);
        QCOMPARE(drainLines(protocol),
                 rx2On ? rx1Both.mid(2) + rx1Both.mid(0, 2) : rx1Both);

        QCOMPARE(protocol.handleCommand(QStringLiteral("vfo:1,0;")),
                 QStringLiteral("vfo:1,0,14200000;"));
        QCOMPARE(protocol.handleCommand(QStringLiteral("vfo:0,0;")),
                 QStringLiteral("vfo:0,0,7100000;"));
        protocol.handleCommand(QStringLiteral("vfo:1,0,14210000;"));
        const qint64 rx1Hz = rx2On ? 14'210'000 : 14'200'000;
        QCOMPARE(radio.vfoHz(1, 0), rx1Hz);
        QCOMPARE(radio.vfoHz(0, 0), qint64(7'100'000));
        QCOMPARE(drainLines(protocol),
                 rx2On ? QStringList{QStringLiteral("vfo:1,0,14210000;")} : QStringList{});

        const QStringList burst = protocol.buildInitBurst();
        // Both channels of a receiver read its slice (channel 0).
        QVERIFY(burst.contains(QStringLiteral("vfo:1,0,%1;").arg(rx1Hz)));
        QVERIFY(burst.contains(QStringLiteral("vfo:1,1,%1;").arg(rx1Hz)));
        QVERIFY(burst.contains(QStringLiteral("vfo:0,0,7100000;")));
    }

    // Duplicate RX2 VFO B to RX2 VFO A, and Forget RX2 VFO B, from Thetis
    // TCIServer.cs:7293-7294 and 1385-1398 [v2.10.3.15]: with RX2 on, a
    // move of the second receiver's VFO goes out on channel 1, plus a
    // copy on channel 0 with Copy on; Forget (only with Copy on) drops
    // channel 1 and keeps the copy.
    void copyAndForgetShapeTheSecondReceiversVfo_data()
    {
        QTest::addColumn<bool>("copy");
        QTest::addColumn<bool>("forget");
        QTest::addColumn<QStringList>("expected");
        const QStringList ch0{QStringLiteral("if:1,0,50000;"),
                              QStringLiteral("vfo:1,0,14200000;")};
        const QStringList ch1{QStringLiteral("if:1,1,50000;"),
                              QStringLiteral("vfo:1,1,14200000;")};
        QTest::newRow("copy off") << false << false << ch1;
        QTest::newRow("copy off, forget on") << false << true << ch1;
        // Thetis's order: channel 1, then its copy on channel 0
        // (TCIServer.cs:1386-1392 [v2.10.3.15]); a client acting on the
        // last frame lands on channel 0.
        QTest::newRow("copy on") << true << false << ch1 + ch0;
        QTest::newRow("copy on, forget on") << true << true << ch0;
    }
    void copyAndForgetShapeTheSecondReceiversVfo()
    {
        QFETCH(bool, copy);
        QFETCH(bool, forget);
        QFETCH(QStringList, expected);
        setOption("TciCopyRx2VfobToVfoa", copy);
        setOption("TciForgetRx2VfoBOnDisconnect", forget);
        CentredMockRadio radio;
        tuneTwoReceivers(radio, /*rx2On=*/true);
        TciProtocol protocol(&radio);
        protocol.enqueueLocalBroadcastVfo(1, 14'200'000, false);
        QCOMPARE(drainLines(protocol), expected);
        // The first receiver is not touched by either option.
        protocol.enqueueLocalBroadcastVfo(0, 7'100'000, false);
        QCOMPARE(drainLines(protocol), bothChannels(0, 7'100'000, 10'000));
    }

    // Use RX1 VFO A for RX2 VFO A, from Thetis TCIServer.cs:7256-7267
    // [v2.10.3.15]: with RX2 on, the first receiver's VFO goes out as
    // receiver 1 channel 0 (never vfo:0,0), its if read from receiver 0.
    // Off, or with RX2 off, nothing changes.
    void useRx1VfoaSendsTheFirstReceiverAsRx2Vfoa_data()
    {
        QTest::addColumn<bool>("on");
        QTest::addColumn<bool>("rx2On");
        QTest::addColumn<QStringList>("expected");
        // Channel 0 only: with RX2 on, Thetis's VFO B is RX2's, so nothing
        // goes out as vfo:0,1 (console.cs:32951-32954 [v2.10.3.15]).
        QTest::newRow("on, RX2 on")
            << true << true
            << QStringList{QStringLiteral("if:1,0,10000;"),
                           QStringLiteral("vfo:1,0,7100000;")};
        QTest::newRow("on, RX2 off") << true << false << bothChannels(0, 7'100'000, 10'000);
        QTest::newRow("off, RX2 on") << false << true << bothChannels(0, 7'100'000, 10'000);
    }
    void useRx1VfoaSendsTheFirstReceiverAsRx2Vfoa()
    {
        QFETCH(bool, on);
        QFETCH(bool, rx2On);
        QFETCH(QStringList, expected);
        setOption("TciUseRx1VfoaForRx2Vfoa", on);
        CentredMockRadio radio;
        tuneTwoReceivers(radio, rx2On);
        TciProtocol protocol(&radio);
        protocol.enqueueLocalBroadcastVfo(0, 7'100'000, false);
        QCOMPARE(drainLines(protocol), expected);
    }

    // ...and on the way in, from Thetis handleVFOMessage,
    // TCIServer.cs:3858-3967 [v2.10.3.15]: vfo:1,0 sets and reads the
    // first receiver's VFO, and a query's answer names receiver 1.
    void useRx1VfoaTakesRx2VfoaCommands_data()
    {
        QTest::addColumn<bool>("on");
        QTest::addColumn<bool>("rx2On");
        QTest::newRow("on, RX2 on") << true << true;
        QTest::newRow("on, RX2 off") << true << false;
        QTest::newRow("off, RX2 on") << false << true;
    }
    void useRx1VfoaTakesRx2VfoaCommands()
    {
        QFETCH(bool, on);
        QFETCH(bool, rx2On);
        setOption("TciUseRx1VfoaForRx2Vfoa", on);
        CentredMockRadio radio;
        tuneTwoReceivers(radio, rx2On);
        TciProtocol protocol(&radio);
        const bool acts = on && rx2On;

        QCOMPARE(protocol.handleCommand(QStringLiteral("vfo:1,0;")),
                 acts ? QStringLiteral("vfo:1,0,7100000;")
                      : QStringLiteral("vfo:1,0,14200000;"));
        QCOMPARE(protocol.handleCommand(QStringLiteral("vfo:1,1;")),
                 QStringLiteral("vfo:1,1,14200000;"));
        // Thetis relabels every query's answer as receiver 1.
        QCOMPARE(protocol.handleCommand(QStringLiteral("vfo:0,0;")),
                 acts ? QStringLiteral("vfo:1,0,7100000;")
                      : QStringLiteral("vfo:0,0,7100000;"));

        protocol.handleCommand(QStringLiteral("vfo:1,0,7150000;"));
        QCOMPARE(radio.vfoHz(0, 0), acts ? qint64(7'150'000) : qint64(7'100'000));
        // With RX2 off the set does nothing (the next test).
        QCOMPARE(radio.vfoHz(1, 0), acts || !rx2On ? qint64(14'200'000) : qint64(7'150'000));
        QCOMPARE(drainLines(protocol),
                 rx2On ? QStringList{QStringLiteral("vfo:1,0,7150000;")} : QStringList{});
    }

    // A set for the second receiver while RX2 is off is ignored and not
    // echoed, as in Thetis handleVFOMessage, TCIServer.cs:3897-3899
    // [v2.10.3.15]. The first receiver's set still acts.
    void secondReceiverVfoSetIgnoredWhileRx2Off()
    {
        CentredMockRadio radio;
        tuneTwoReceivers(radio, /*rx2On=*/false);
        TciProtocol protocol(&radio);
        for (const QString& set : {QStringLiteral("vfo:1,0,7150000;"),
                                   QStringLiteral("vfo:1,1,7160000;")}) {
            QCOMPARE(protocol.handleCommand(set), QString());
        }
        QCOMPARE(radio.vfoHz(1, 0), qint64(14'200'000));
        QCOMPARE(radio.vfoHz(1, 1), qint64(14'200'000));
        QCOMPARE(drainLines(protocol), QStringList{});
        protocol.handleCommand(QStringLiteral("vfo:0,0,7120000;"));
        QCOMPARE(radio.vfoHz(0, 0), qint64(7'120'000));
        QCOMPARE(drainLines(protocol), QStringList{QStringLiteral("vfo:0,0,7120000;")});

        // With RX2 on the same set acts.
        tuneTwoReceivers(radio, /*rx2On=*/true);
        protocol.handleCommand(QStringLiteral("vfo:1,1,14210000;"));
        QCOMPARE(radio.vfoHz(1, 1), qint64(14'210'000));
        QCOMPARE(drainLines(protocol), QStringList{QStringLiteral("vfo:1,1,14210000;")});
    }

    // ...and in the first lines, from Thetis sendVFO,
    // TCIServer.cs:2101-2122 [v2.10.3.15]: vfo:1,0 carries the first
    // receiver's VFO; vfo:1,1 and the if lines are unchanged.
    void useRx1VfoaInTheFirstLines_data()
    {
        QTest::addColumn<bool>("on");
        QTest::addColumn<bool>("rx2On");
        QTest::newRow("on, RX2 on") << true << true;
        QTest::newRow("on, RX2 off") << true << false;
        QTest::newRow("off, RX2 on") << false << true;
    }
    void useRx1VfoaInTheFirstLines()
    {
        QFETCH(bool, on);
        QFETCH(bool, rx2On);
        setOption("TciUseRx1VfoaForRx2Vfoa", on);
        CentredMockRadio radio;
        tuneTwoReceivers(radio, rx2On);
        TciProtocol protocol(&radio);
        const QStringList burst = protocol.buildInitBurst();
        const bool acts = on && rx2On;
        QVERIFY(burst.contains(acts ? QStringLiteral("vfo:1,0,7100000;")
                                    : QStringLiteral("vfo:1,0,14200000;")));
        QVERIFY(burst.contains(QStringLiteral("vfo:1,1,14200000;")));
        QVERIFY(burst.contains(QStringLiteral("vfo:0,0,7100000;")));
        QVERIFY(burst.contains(QStringLiteral("if:1,0,50000;")));
    }
};

QTEST_GUILESS_MAIN(TestTciServerSettingsReal)
#include "tst_tci_server_settings_real.moc"
