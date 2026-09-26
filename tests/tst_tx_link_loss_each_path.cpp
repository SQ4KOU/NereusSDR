// =================================================================
// tests/tst_tx_link_loss_each_path.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test file.
//
// iPhone app plan Task 37 (R-IOS-13; remote design section 12.1, "the test
// must include severing the path with MOX asserted on each relay rung
// separately"; pairing design section 9.7; spec section 4.6 items 1 and
// 2). A real Core and a real remote window (the desktop's, which keys and
// sends its keepalives as a phone does) key the Core's radio over each
// path that exists, and the path is severed with the key on: transmit
// stops within 500 ms of the last keepalive the Core heard.
//
//   - the session's WebSocket (the in-process loopback, severed without a
//     close, and closed);
//   - the media connection's "tx" data channel on a direct pair (real
//     DTLS/SCTP between two libdatachannel peers on this machine), severed
//     at the Core's end with the session still up;
//   - through TURN over UDP, and through the relay floor: the Part E
//     transports (the plan's Tasks 27 to 29) are not built yet. Those rows
//     are skipped with the reason until the Part E harness lands; the
//     watchdog takes their keepalives through the same call
//     (RemoteTxWatchdog::keepalive) and its rules do not change.
//
// Also here, with the same real Core and window: VOX a device armed goes
// off with its session and its link, and the Core never keys from its own
// microphone because of it; and a keyed device's microphone starving on a
// live link stops transmitting in AM and not in USB (FM transmit is not
// built yet, 3M-3b, so the Core refuses an FM key before any starvation).
//
// Nothing keys a real radio (the Core's radio is a static test model) and
// no real audio device opens (the window's microphone is a paced test
// bus). Real time throughout: labelled realtime.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original test for NereusSDR by J.J. Boyd (KG4VCF), iPhone
//               app plan Task 37 (R-IOS-13), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/IAudioBus.h"
#include "core/safety/RemoteTxWatchdog.h"
#include "core/safety/StarvationPolicy.h"
#include "core/session/RemoteTransmitClient.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/LibDataChannelMediaTransport.h"
#include "gui/RemoteMediaController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include "fakes/RemoteAudioSessionHarness.h"
#include "RealtimeTestLoad.h"

#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <memory>

using namespace NereusSDR;

namespace {

// The window's microphone: a tone paced at 48 kHz that the test can
// silence (never a real device).
class PacedMicrophone final : public IAudioBus {
public:
    explicit PacedMicrophone(std::shared_ptr<std::atomic<bool>> giving)
        : m_giving(std::move(giving))
    {
    }
    bool open(const AudioFormat& format) override
    {
        m_format = format;
        m_open = true;
        return true;
    }
    void close() override { m_open = false; }
    bool isOpen() const override { return m_open; }
    qint64 push(const char*, qint64) override { return 0; }
    void flush() override {}
    qint64 pull(char* data, qint64 maxBytes) override
    {
        if (!m_open || data == nullptr || maxBytes < 4 || !m_giving->load()) {
            m_clock.invalidate();
            return 0;
        }
        if (!m_clock.isValid() || m_clock.elapsed() - m_lastPullMs > 100) {
            m_clock.start();
            m_delivered = 0;
        }
        m_lastPullMs = m_clock.elapsed();
        const qint64 due = m_clock.elapsed() * 48;
        const qint64 frames = std::min(due - m_delivered, maxBytes / 4);
        if (frames <= 0) {
            return 0;
        }
        auto* out = reinterpret_cast<float*>(data);
        for (qint64 i = 0; i < frames; ++i) {
            out[i] = 0.3f * static_cast<float>(std::sin(
                2.0 * 3.14159265358979323846 * 1000.0 * static_cast<double>(m_delivered + i)
                / 48000.0));
        }
        m_delivered += frames;
        return frames * 4;
    }
    float rxLevel() const override { return 0.0f; }
    float txLevel() const override { return 0.0f; }
    QString backendName() const override { return QStringLiteral("PacedMicrophone"); }
    AudioFormat negotiatedFormat() const override { return m_format; }

private:
    std::shared_ptr<std::atomic<bool>> m_giving;
    AudioFormat m_format{};
    bool m_open{false};
    QElapsedTimer m_clock;
    qint64 m_lastPullMs{0};
    qint64 m_delivered{0};
};

std::shared_ptr<std::atomic<bool>> attachMicrophone(Test::RemoteAudioSessionHarness& h)
{
    auto giving = std::make_shared<std::atomic<bool>>(true);
    AudioFormat fmt{};
    fmt.sample = AudioFormat::Sample::Float32;
    fmt.channels = 1;
    fmt.sampleRate = 48000;
    auto bus = std::make_unique<PacedMicrophone>(giving);
    bus->open(fmt);
    h.remote.audioEngine()->setTxInputBusForTest(std::move(bus));
    return giving;
}

int commandsOf(const Test::LoopbackTransport* core, const QString& verb)
{
    int count = 0;
    for (const QByteArray& wire : core->received()) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("command.invoke")
            && o.value(QStringLiteral("verb")).toString() == verb) {
            ++count;
        }
    }
    return count;
}

// What the watchdog reported when it stopped.
struct Trip {
    bool linkClosed{false};
    qint64 silentMs{-1};
};

Trip tripOf(const QSignalSpy& spy)
{
    if (spy.isEmpty()) {
        return {};
    }
    return {spy.first().at(1).toBool(), spy.first().at(2).toLongLong()};
}

constexpr qint64 kBoundMs = 500;
const QString kStopSentence =
    QStringLiteral("The link to Shack MacBook went quiet, so the Core stopped transmitting.");

} // namespace

class TestTxLinkLossEachPath : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString profile = QStringLiteral("tx-link-loss-each-path-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void cleanup() { RealtimeTestLoad::printLoadAverageIfFailed(); }

    // ---- The session's WebSocket ------------------------------------------

    // Keyed over the session, keepalives on the session hold the key; the
    // path goes dead without closing and transmit stops 400 to 500 ms after
    // the last keepalive the Core heard.
    void webSocketSeveredStopsWithin500ms()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        RemoteTransmitClient* transmit = h.client.remoteTransmit();
        QSignalSpy tripped(h.server.txWatchdog(), &RemoteTxWatchdog::tripped);
        QSignalSpy stopped(&h.station, &RadioModel::transmitStopped);

        h.remote.setMoxFromButton(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QCOMPARE(h.station.keyedBy().deviceId, h.windowKey->fingerprint());
        QVERIFY(h.server.txWatchdog()->isWatching(h.windowKey->fingerprint()));

        // Two seconds keyed: the keepalives on the session hold it.
        QTest::qWait(2000);
        QVERIFY(h.station.moxController()->isMox());
        QCOMPARE(tripped.count(), 0);
        QVERIFY(transmit->sessionKeepalivesSent() >= 15);
        QCOMPARE(transmit->channelKeepalivesSent(), quint64(0));
        QVERIFY(commandsOf(h.stationLink, QStringLiteral("tx.keepalive")) >= 15);

        // Severed: nothing crosses either way, and nothing closes.
        h.stationLink->setSevered(true);
        QTRY_COMPARE_WITH_TIMEOUT(tripped.count(), 1, 3000);
        const Trip trip = tripOf(tripped);
        qInfo("WebSocket severed: stopped %lld ms after the last keepalive",
              static_cast<long long>(trip.silentMs));
        QVERIFY(!trip.linkClosed);
        QVERIFY2(trip.silentMs > RemoteTxWatchdog::kLinkLossDeadlineMs && trip.silentMs <= kBoundMs,
                 qPrintable(QString::number(trip.silentMs)));
        QVERIFY(!h.station.moxController()->isMox());
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.first().at(0).toString(), kStopSentence);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // The session closing while keyed stops transmitting at once, before
    // the holder's normal unkey.
    void webSocketClosingStopsAtOnce()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy tripped(h.server.txWatchdog(), &RemoteTxWatchdog::tripped);
        h.remote.setMoxFromButton(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QTest::qWait(300);

        // The Core's end closes (its socket saw the close): the stop runs
        // inside that close, not on a later turn.
        h.stationLink->closeLink(QStringLiteral("the app went away"));
        QCOMPARE(tripped.count(), 1);
        QVERIFY(tripOf(tripped).linkClosed);
        QVERIFY(!h.station.moxController()->isMox());
        QVERIFY(!h.server.txWatchdog()->isWatchingAny());
    }

    // ---- The media connection's "tx" data channel (a direct pair) --------

    // Keyed with the media connection up, the keepalives ride the "tx"
    // channel (none on the session); the channel's path dies at the Core's
    // end, the session still up, and transmit stops 400 to 500 ms after the
    // last keepalive the Core heard.
    void dataChannelSeveredStopsWithin500ms()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h);
        QPointer<LibDataChannelMediaTransport> coreTransport;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(
            &h.server, &h.station, nullptr, [&coreTransport](QObject* parent) {
                auto* transport = new LibDataChannelMediaTransport(parent);
                coreTransport = transport;
                return static_cast<IMediaTransport*>(transport);
            });
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QVERIFY(remoteMedia.micLineNegotiated());
        QVERIFY(!coreTransport.isNull());
        RemoteTransmitClient* transmit = h.client.remoteTransmit();
        QSignalSpy tripped(h.server.txWatchdog(), &RemoteTxWatchdog::tripped);
        QSignalSpy stopped(&h.station, &RadioModel::transmitStopped);
        // Give the "tx" channel time to open after media is ready.
        QTest::qWait(300);

        h.remote.setMoxFromButton(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QCOMPARE(h.station.keyedBy().deviceId, h.windowKey->fingerprint());
        QTest::qWait(2000);
        QVERIFY(h.station.moxController()->isMox());
        QCOMPARE(tripped.count(), 0);
        QVERIFY(transmit->channelKeepalivesSent() >= 15);
        QCOMPARE(transmit->sessionKeepalivesSent(), quint64(0));
        QCOMPARE(commandsOf(h.stationLink, QStringLiteral("tx.keepalive")), 0);

        // The media path dies at the Core's end: nothing arrives on it (the
        // keepalives and the microphone alike); the session stays up.
        coreTransport->setReceiveSeveredForTest(true);
        QTRY_COMPARE_WITH_TIMEOUT(tripped.count(), 1, 3000);
        const Trip trip = tripOf(tripped);
        qInfo("tx data channel severed: stopped %lld ms after the last keepalive",
              static_cast<long long>(trip.silentMs));
        QVERIFY(!trip.linkClosed);
        QVERIFY2(trip.silentMs > RemoteTxWatchdog::kLinkLossDeadlineMs && trip.silentMs <= kBoundMs,
                 qPrintable(QString::number(trip.silentMs)));
        QVERIFY(!h.station.moxController()->isMox());
        // USB: the microphone starved on the way (at 250 ms), which stops
        // nothing in USB; the watchdog did.
        QCOMPARE(stopped.count(), 1);
        QCOMPARE(stopped.first().at(0).toString(), kStopSentence);
        QVERIFY(h.client.isHandshakeComplete());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // ---- The Part E paths --------------------------------------------------

    void turnUdpSeveredStopsWithin500ms()
    {
        QSKIP("Through TURN over UDP: the rendezvous and relay transports (iPhone app plan "
              "Tasks 27 to 29) and the Part E harness are not built yet.");
    }

    void relayFloorSeveredStopsWithin500ms()
    {
        QSKIP("Through the relay floor: the relay transport (iPhone app plan Tasks 27 to 29) "
              "and the Part E harness are not built yet.");
    }

    // ---- VOX a device armed -------------------------------------------------

    // The window arms the Core's VOX with no microphone line: its
    // keepalives keep it armed, the Core's own VOX detector never keys from
    // the Core's own microphone because of it, and the window's session
    // ending turns it off.
    void voxArmedByADeviceNeverKeysTheCoresOwnMicrophone()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy tripped(h.server.txWatchdog(), &RemoteTxWatchdog::tripped);

        h.remote.transmitModel().setVoxEnabled(true);
        QTRY_VERIFY(h.station.transmitModel().voxEnabled());
        QCOMPARE(h.server.voxArmedBy(), h.windowKey->fingerprint());
        QVERIFY(h.server.txWatchdog()->isWatching(h.windowKey->fingerprint()));
        QVERIFY(h.station.remoteVoxDevice().isEmpty());  // no line

        // The window keeps VOX armed with its keepalives.
        QTest::qWait(1200);
        QCOMPARE(tripped.count(), 0);
        QVERIFY(h.station.transmitModel().voxEnabled());
        QVERIFY(h.client.remoteTransmit()->sessionKeepalivesSent() >= 8);

        // The Core's VOX detector fires on the Core's own microphone: VOX
        // this device armed never keys from it.
        h.station.moxController()->onVoxActive(true);
        QTest::qWait(100);
        QVERIFY(!h.station.moxController()->isMox());
        h.station.moxController()->onVoxActive(false);

        // The window's session ends: its VOX goes off.
        h.stationLink->closeLink(QStringLiteral("the app went away"));
        QVERIFY(!h.station.transmitModel().voxEnabled());
        QVERIFY(h.server.voxArmedBy().isEmpty());
        QVERIFY(!h.server.txWatchdog()->isWatchingAny());
    }

    // VOX armed with the line, the link goes quiet: VOX goes off within
    // 500 ms of the last keepalive.
    void voxArmedGoesOffWhenTheLinkGoesQuiet()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h);
        QPointer<LibDataChannelMediaTransport> coreTransport;
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(
            &h.server, &h.station, nullptr, [&coreTransport](QObject* parent) {
                auto* transport = new LibDataChannelMediaTransport(parent);
                coreTransport = transport;
                return static_cast<IMediaTransport*>(transport);
            });
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy tripped(h.server.txWatchdog(), &RemoteTxWatchdog::tripped);

        h.remote.transmitModel().setVoxEnabled(true);
        QTRY_VERIFY(h.station.transmitModel().voxEnabled());
        QTRY_COMPARE(h.station.remoteVoxDevice(), h.windowKey->fingerprint());
        QTest::qWait(1000);
        QCOMPARE(tripped.count(), 0);

        // The whole path dies: the session's and the media connection's.
        h.stationLink->setSevered(true);
        coreTransport->setReceiveSeveredForTest(true);
        QTRY_COMPARE_WITH_TIMEOUT(tripped.count(), 1, 3000);
        const Trip trip = tripOf(tripped);
        QVERIFY2(trip.silentMs > RemoteTxWatchdog::kLinkLossDeadlineMs && trip.silentMs <= kBoundMs,
                 qPrintable(QString::number(trip.silentMs)));
        QVERIFY(!h.station.transmitModel().voxEnabled());
        QVERIFY(h.server.voxArmedBy().isEmpty());
        QVERIFY(!h.station.moxController()->isMox());
    }

    // ---- The microphone starving on a live link ---------------------------

    void starvationOnALiveLinkStopsAmAndNotUsb_data()
    {
        QTest::addColumn<int>("mode");
        QTest::addColumn<bool>("stops");
        QTest::newRow("AM unkeys") << static_cast<int>(DSPMode::AM) << true;
        QTest::newRow("USB stays keyed") << static_cast<int>(DSPMode::USB) << false;
    }

    void starvationOnALiveLinkStopsAmAndNotUsb()
    {
        QFETCH(int, mode);
        QFETCH(bool, stops);
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.station.sliceById(h.sliceA)->setDspMode(static_cast<DSPMode>(mode));
        const auto giving = attachMicrophone(h);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy stopped(&h.station, &RadioModel::transmitStopped);
        QSignalSpy starved(daemonMedia.micReceiver(), &RemoteMicReceiver::starved);
        QTest::qWait(300);

        h.remote.setMoxFromButton(true);
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QTest::qWait(500);
        QVERIFY(h.station.moxController()->isMox());

        // The microphone stops giving audio; the link (and the keepalives)
        // stay up.
        QElapsedTimer quiet;
        giving->store(false);
        quiet.start();
        QTRY_VERIFY_WITH_TIMEOUT(!starved.isEmpty(), 3000);
        if (stops) {
            QTRY_VERIFY_WITH_TIMEOUT(!h.station.moxController()->isMox(), 2000);
            qInfo("microphone silent: stopped after %lld ms", static_cast<long long>(quiet.elapsed()));
            QCOMPARE(stopped.count(), 1);
            QCOMPARE(stopped.first().at(0).toString(),
                     QStringLiteral("No microphone audio arrived from Shack MacBook, so the Core "
                                    "stopped transmitting."));
        } else {
            QTest::qWait(1500);
            QVERIFY(h.station.moxController()->isMox());
            QCOMPARE(stopped.count(), 0);
            h.remote.setMoxFromButton(false);
            QTRY_VERIFY(!h.station.moxController()->isMox());
        }
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_MAIN(TestTxLinkLossEachPath)
#include "tst_tx_link_loss_each_path.moc"
