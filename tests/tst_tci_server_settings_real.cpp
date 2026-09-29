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
};

QTEST_GUILESS_MAIN(TestTciServerSettingsReal)
#include "tst_tci_server_settings_real.moc"
