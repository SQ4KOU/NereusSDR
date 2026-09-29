// =================================================================
// tests/tst_remote_window_transmit.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test.
//
// iPhone app plan, the desktop remote window transmits end to end through a
// Core (R-IOS-13, R-R3-42). A real Core (StationServer on a Saturn model)
// and a real remote window (RadioModel Role::Remote, StationClient, the TX
// applet, the container buttons, RemoteMediaController, the window's TCI
// server) over the in-process loopback, the window signed in with its own
// paired key. Media runs over real DTLS/SRTP; the window's microphone is a
// paced test bus (never a real device) and the Core's radio is a static
// test model: nothing keys a real radio.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25 - Created for the desktop remote window's transmit
//                (R-IOS-13, R-R3-42). J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
//   2026-09-26 - Merge of Tasks 37 to 39: a keyed window reads the Core's
//                forward power, SWR, ALC and MIC through txState, and is
//                told the Core's time-out stopped its key. J.J. Boyd
//                (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-26: merge of the trunk into the transmit lane: RF Power and
//               Tune Power from the window reach the Core. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: Transmit group fix wave C1: a key without the microphone
//               line is refused and nothing keys. J.J. Boyd (KG4VCF),
//               with AI-assisted implementation via Anthropic Claude
//               Code.
//   2026-09-26: Transmit group fix wave 2 (M8): VOX shows disabled with the
//               plain reason while this computer has no microphone line to
//               the Core; the Core's refusal stays the backstop. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: Transmit group fix wave 2, the re-review's minors:
//               holderTransferring true while keys are refused for a
//               transfer's reasons (a dropped holder's fence, a transfer
//               ended with MOX on); stopEpoch names the key a stop ended so
//               a newer key is never ended by it; VOX at the Core listens
//               only to the device that armed it; the window says why MOX
//               and TUNE wait while another device holds. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: Task 77 fix wave, M2: the holder line for the radio. J.J.
//               Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  iPhone app plan Task 78 (R-IOS-02): a
//                                    paired window can take transmit, and
//                                    the holder line says how. AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-27: R-IOS-13: the program is looked for over the 800 ms it
//               lasts, as the smaller buffer plays everything in order.
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
//   2026-09-27: R-R3-49 load round: the presses that set up transmit go
//               through pressMoxUntilKeyed(). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSlider>
#include <QSignalSpy>
#include <QWebSocket>

#include <cmath>
#include <memory>
#include <vector>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/IAudioBus.h"
#include "core/MoxController.h"
#include "core/TciBinaryFrame.h"
#include "core/TciServer.h"
#include "core/safety/TransmitHolder.h"
#include "core/meters/TxMeterPump.h"
#include "core/safety/TxTimeOutTimer.h"
#include "core/session/RemoteTransmitClient.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/TransmitStateFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/RemoteMicReceiver.h"
#include "gui/RemoteMediaController.h"
#include "gui/RemoteTransmitForwarder.h"
#include "gui/applets/TxApplet.h"
#include "gui/meters/MeterItem.h"
#include "gui/meters/MeterPoller.h"
#include "gui/meters/MeterWidget.h"
#include "gui/containers/ContainerButtonDispatcher.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "OperatorWording.h"
#include "fakes/RemoteAudioSessionHarness.h"

using namespace NereusSDR;

namespace {

// The window's microphone: a tone paced at 48 kHz (never a real device).
class PacedMicrophone final : public IAudioBus {
public:
    PacedMicrophone(float amplitude, double hz) : m_amplitude(amplitude), m_hz(hz) {}
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
        // A negative amplitude: a microphone that gives nothing at all.
        if (!m_open || data == nullptr || maxBytes < 4 || m_amplitude < 0.0f) {
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
            out[i] = m_amplitude * static_cast<float>(std::sin(
                2.0 * 3.14159265358979323846 * m_hz * static_cast<double>(m_delivered + i)
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
    float m_amplitude;
    double m_hz;
    AudioFormat m_format{};
    bool m_open{false};
    QElapsedTimer m_clock;
    qint64 m_lastPullMs{0};
    qint64 m_delivered{0};
};

void attachMicrophone(Test::RemoteAudioSessionHarness& h, float amplitude)
{
    AudioFormat fmt{};
    fmt.sample = AudioFormat::Sample::Float32;
    fmt.channels = 1;
    fmt.sampleRate = 48000;
    auto bus = std::make_unique<PacedMicrophone>(amplitude, 1000.0);
    bus->open(fmt);
    h.remote.audioEngine()->setTxInputBusForTest(std::move(bus));
}

// A press of this window's MOX that keys the Core, for a case whose
// subject comes after it. The Core answers a voice key once the device's
// microphone line has filled its buffer and refuses it when that takes
// more than 250 ms (station link document, section 18.3), and the window
// starts its microphone only with the key: the whole start had to fit in
// 250 ms. R-R3-49 load round: on a loaded computer it did not, and the
// Core refused aProgramThroughTheWindowsTciKeysTheCore's setup press (1 of
// 24 runs at load 91; at load 82 to 130 the key was answered 52 to 133 ms
// after it was sent). The refusal tells the operator to wait a moment and
// try again, so after exactly that refusal the press is made again, up to
// kMaxPresses; any other refusal, or no answer, fails. Load findings 2
// (2026-09-28): the Core's 250 ms now runs from the line's first packet,
// with 1 s for that packet to come, so the line's cold start no longer
// counts against the fill; the retry stays for a refusal that still comes.
constexpr int kMaxPresses = 5;
bool pressMoxUntilKeyed(Test::RemoteAudioSessionHarness& h)
{
    QSignalSpy refused(h.client.remoteTransmit(), &RemoteTransmitClient::refused);
    for (int press = 0; press < kMaxPresses; ++press) {
        h.remote.setMoxFromButton(true);
        if (!QTest::qWaitFor([&] { return h.station.moxController()->isMox() || !refused.isEmpty(); },
                             5000)) {
            return false;
        }
        if (h.station.moxController()->isMox()) {
            return true;
        }
        const QList<QVariant> refusal = refused.takeFirst();
        if (refusal.at(0).toString() != TxRefusals::remoteMicNotReady().text) {
            qWarning("the press was refused: %s", qPrintable(refusal.at(0).toString()));
            return false;
        }
        if (!QTest::qWaitFor([&] { return !h.remote.moxController()->isMox(); }, 5000)) {
            return false;
        }
    }
    return false;
}

// The command.invoke messages of `verb` the Core received, in order.
QList<QJsonObject> commandsOf(const Test::LoopbackTransport* core, const QString& verb)
{
    QList<QJsonObject> out;
    for (const QByteArray& wire : core->received()) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("type")).toString() == QLatin1String("command.invoke")
            && o.value(QStringLiteral("verb")).toString() == verb) {
            out.append(o);
        }
    }
    return out;
}

QJsonValue argument(const QJsonObject& command, const QString& name)
{
    for (const QJsonValue& a : command.value(QStringLiteral("args")).toArray()) {
        if (a.toObject().value(QStringLiteral("name")).toString() == name) {
            return a.toObject().value(QStringLiteral("value"));
        }
    }
    return {};
}

// Each command went out three times as the same command (one id).
bool sentAsCopies(const QList<QJsonObject>& commands)
{
    if (commands.isEmpty() || commands.size() % RemoteTransmitClient::kCopies != 0) {
        return false;
    }
    for (int i = 0; i < commands.size(); i += RemoteTransmitClient::kCopies) {
        for (int c = 1; c < RemoteTransmitClient::kCopies; ++c) {
            if (commands.at(i + c) != commands.at(i)) {
                return false;
            }
        }
    }
    return commands.size() < RemoteTransmitClient::kCopies * 2
        || commands.at(0).value(QStringLiteral("id")) != commands.at(3).value(QStringLiteral("id"));
}

QPushButton* buttonNamed(TxApplet& applet, const QString& accessibleName)
{
    for (QPushButton* b : applet.findChildren<QPushButton*>()) {
        if (b->accessibleName() == accessibleName) {
            return b;
        }
    }
    return nullptr;
}

// The remote window's transmit controls, as MainWindow builds them.
struct WindowControls {
    TxApplet applet;
    ContainerButtonDispatcher container;
    QPushButton* mox{nullptr};
    QPushButton* tune{nullptr};
    QPushButton* vox{nullptr};

    explicit WindowControls(Test::RemoteAudioSessionHarness& h)
        : applet(&h.remote)
        , container(&h.remote, hooksFor(h))
    {
        mox = buttonNamed(applet, QStringLiteral("MOX transmit"));
        tune = buttonNamed(applet, QStringLiteral("Tune carrier"));
        vox = buttonNamed(applet, QStringLiteral("VOX voice-operated transmit"));
    }

    static ContainerButtonDispatcher::Hooks hooksFor(Test::RemoteAudioSessionHarness& h)
    {
        ContainerButtonDispatcher::Hooks hooks;
        StationClient* client = &h.client;
        hooks.transmitPermitted = [client] {
            return client->isHandshakeComplete() && client->remoteTransmitAvailable()
                && client->capabilities().txPermitted;
        };
        hooks.remoteTransmitReasonNow = [client] { return client->capabilities().txRefusalReason; };
        return hooks;
    }

    // As MainWindow's applyRemoteRoleGating does (fix wave 2, M8: VOX
    // also follows this computer's microphone line, `media`).
    void follow(StationClient& client, const RemoteMediaController* media = nullptr)
    {
        const bool permitted = client.isHandshakeComplete() && client.remoteTransmitAvailable()
            && client.capabilities().txPermitted;
        applet.setTransmitPermitted(permitted, client.capabilities().txRefusalReason);
        const bool line = media == nullptr || media->micLineOpen();
        applet.setVoxPermitted(line, line ? QString() : TxRefusals::micNotConnected().text);
    }
};

} // namespace

class TestRemoteWindowTransmit : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString profile = QStringLiteral("remote-window-transmit-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
        AppSettings::instance().clear();
        qRegisterMetaType<NereusSDR::TxRefusal>();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // ---- The hello --------------------------------------------------------

    // The window's own hello declares remoteTx 1, so the Core tells it
    // txPermitted, remoteTxVersion and, while not permitted, why.
    void theHelloDeclaresRemoteTransmit()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.isHandshakeComplete());
        bool declared = false;
        for (const QByteArray& wire : h.stationLink->received()) {
            const QJsonObject o = QJsonDocument::fromJson(wire).object();
            if (o.value(QStringLiteral("type")).toString() == QLatin1String("hello")) {
                declared = o.value(QStringLiteral("features")).toObject()
                               .value(QStringLiteral("remoteTx")).toInt() == 1;
            }
        }
        QVERIFY(declared);
        QCOMPARE(h.client.capabilities().remoteTxVersion, 2);  // Task 77: 2
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QVERIFY(h.client.remoteTransmitAvailable());
        QVERIFY(h.remote.remoteTransmitRouted());
        QVERIFY(h.client.capabilities().txRefusalReason.isEmpty());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A real Core accepts the typed tuner release on the primary. Its
    // result must reach RemoteTransmitClient before an independent TX
    // heartbeat can resume after the delivery fence.
    void acceptedTunerTuneOffReopensTheIndependentHeartbeat()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        RemoteTransmitClient* tx = h.client.remoteTransmit();
        QVERIFY(tx != nullptr);
        int primaryHeartbeats = 0;
        int independentHeartbeats = 0;
        tx->setSessionKeepalive([&](quint64, quint32) {
            ++primaryHeartbeats;
            return true;
        });
        tx->setChannelKeepalive([&](quint64, quint32) {
            ++independentHeartbeats;
            return true;
        });
        tx->setVoxArmed(true);
        QSignalSpy answered(&h.client, &StationClient::commandResponse);
        const int primaryBefore = primaryHeartbeats;
        tx->setTunerTune(false);
        const int independentBefore = independentHeartbeats;
        tx->keepaliveTick();
        QCOMPARE(independentHeartbeats, independentBefore);
        QCOMPARE(primaryHeartbeats, primaryBefore);
        QTRY_VERIFY(!commandsOf(h.stationLink, QStringLiteral("tx.tunerTune")).isEmpty());
        QTRY_VERIFY(!answered.isEmpty());
        bool acceptedOff = false;
        for (const QList<QVariant>& response : answered) {
            const SessionMessage message = response.at(0).value<SessionMessage>();
            if (message.commandVerb == QByteArrayLiteral("tx.tunerTune") && message.accepted) {
                acceptedOff = true;
            }
        }
        QVERIFY(acceptedOff);
        tx->keepaliveTick();
        QCOMPARE(independentHeartbeats, independentBefore + 1);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A receive-only Core: not permitted, and the controls say why in the
    // Core's words, disabled and never hidden.
    void aReceiveOnlyCoreSaysWhyInItsWords()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.server.setRemoteTransmitAllowed(false);
        h.connectSession();
        QTRY_VERIFY(h.client.isHandshakeComplete());
        QCOMPARE(h.client.capabilities().remoteTxVersion, 2);  // Task 77: 2
        QVERIFY(!h.client.capabilities().txPermitted);
        QCOMPARE(h.client.capabilities().txRefusalCode, QStringLiteral("stationReceiveOnly"));
        QCOMPARE(h.client.capabilities().txRefusalReason,
                 QStringLiteral("This Core is set to receive only."));
        WindowControls window(h);
        window.follow(h.client);
        QVERIFY(!window.mox->isEnabled());
        QVERIFY(!window.mox->isHidden());
        QCOMPARE(window.mox->toolTip(), QStringLiteral("This Core is set to receive only."));
        QVERIFY(!window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).available);
        QCOMPARE(window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).reason,
                 QStringLiteral("This Core is set to receive only."));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // ---- Keys -------------------------------------------------------------

    // MOX (the TX applet's and the container's) and TUNE key the Core's
    // MoxController through the holder, as this device, three copies per
    // command, the release naming the key's epoch; the window's own
    // controller never keys. Two-tone reaches the Core the same way.
    void moxTuneAndTwoToneKeyTheCoreThroughTheHolder()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        WindowControls window(h);
        window.follow(h.client);
        QVERIFY(window.mox && window.tune && window.vox);
        QVERIFY(window.mox->isEnabled());
        MoxController* coreMox = h.station.moxController();
        MoxController* windowMox = h.remote.moxController();
        QSignalSpy windowMoxChanged(windowMox, &MoxController::moxStateChanged);
        const QByteArray device = h.windowKey->fingerprint();

        // The TX applet's MOX.
        window.mox->click();
        QTRY_VERIFY(coreMox->isMox());
        QCOMPARE(h.station.keyedBy().deviceId, device);
        QCOMPARE(h.station.keyedBy().trigger, QByteArrayLiteral("screen"));
        QVERIFY(h.server.transmitHolder()->isHeldBy(device));
        QTRY_VERIFY(h.remote.isTransmitting());
        QVERIFY(window.mox->isChecked());
        const quint32 epoch = h.station.keyedBy().epoch;
        QTRY_COMPARE(h.client.remoteTransmit()->screenEpoch(), epoch);
        window.mox->click();
        QTRY_VERIFY(!coreMox->isMox());
        QTRY_VERIFY(!h.remote.isTransmitting());
        QVERIFY(!window.mox->isChecked());
        const QList<QJsonObject> keys = commandsOf(h.stationLink, QStringLiteral("tx.key"));
        const QList<QJsonObject> unkeys = commandsOf(h.stationLink, QStringLiteral("tx.unkey"));
        QCOMPARE(keys.size(), RemoteTransmitClient::kCopies);
        QVERIFY(sentAsCopies(keys));
        QCOMPARE(argument(keys.first(), QStringLiteral("trigger")).toString(),
                 QStringLiteral("screen"));
        QCOMPARE(unkeys.size(), RemoteTransmitClient::kCopies);
        QVERIFY(sentAsCopies(unkeys));
        QCOMPARE(argument(unkeys.first(), QStringLiteral("epoch")).toInteger(), qint64(epoch));

        // The container's MOX: a new press is a new command.
        QVERIFY(window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).available);
        QVERIFY(window.container.click(ContainerButtonDispatcher::Id::Mox, 0).isEmpty());
        QTRY_VERIFY(coreMox->isMox());
        QVERIFY(h.station.keyedBy().epoch > epoch);
        QTRY_VERIFY(window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).on);
        QVERIFY(window.container.click(ContainerButtonDispatcher::Id::Mox, 0).isEmpty());
        QTRY_VERIFY(!coreMox->isMox());
        const QList<QJsonObject> keys2 = commandsOf(h.stationLink, QStringLiteral("tx.key"));
        QCOMPARE(keys2.size(), 2 * RemoteTransmitClient::kCopies);
        QVERIFY(sentAsCopies(keys2));

        // TUNE: the Core's TUNE, on and off.
        window.tune->click();
        QTRY_VERIFY(h.station.isTune());
        QTRY_VERIFY(coreMox->isMox());
        QCOMPARE(h.station.keyedBy().deviceId, device);
        QTRY_VERIFY(window.tune->isChecked());
        QTRY_VERIFY(h.remote.transmitModel().isTune());
        window.tune->click();
        QTRY_VERIFY(!h.station.isTune());
        QTRY_VERIFY(!coreMox->isMox());
        const QList<QJsonObject> tunes = commandsOf(h.stationLink, QStringLiteral("tx.tune"));
        QCOMPARE(tunes.size(), 2 * RemoteTransmitClient::kCopies);
        QVERIFY(sentAsCopies(tunes));
        QVERIFY(argument(tunes.first(), QStringLiteral("on")).toBool());
        QVERIFY(!argument(tunes.last(), QStringLiteral("on")).toBool());

        // TUNE off pressed before the Core's TUNE reached the window: the
        // off still goes, and the Core ends up off.
        window.tune->click();
        window.tune->click();
        QTRY_COMPARE(commandsOf(h.stationLink, QStringLiteral("tx.tune")).size(),
                     4 * RemoteTransmitClient::kCopies);
        QVERIFY(!argument(commandsOf(h.stationLink, QStringLiteral("tx.tune")).last(),
                          QStringLiteral("on")).toBool());
        QTRY_VERIFY(!h.station.isTune());
        QTRY_VERIFY(!coreMox->isMox());
        QTest::qWait(200);
        QVERIFY(!h.station.isTune());
        QVERIFY(!coreMox->isMox());

        // Two-tone: the Core's test (this static test radio has no transmit
        // channel, so the Core refuses it and the window says so).
        QSignalSpy refused(&h.remote, &RadioModel::remoteTransmitRefused);
        window.applet.twoToneButton()->click();
        QTRY_COMPARE(commandsOf(h.stationLink, QStringLiteral("tx.twoTone")).size(),
                     RemoteTransmitClient::kCopies);
        QVERIFY(sentAsCopies(commandsOf(h.stationLink, QStringLiteral("tx.twoTone"))));
        QVERIFY(argument(commandsOf(h.stationLink, QStringLiteral("tx.twoTone")).first(),
                         QStringLiteral("on")).toBool());
        QTRY_COMPARE(refused.count(), 1);
        QVERIFY(!window.applet.twoToneButton()->isChecked());

        // The window's own MoxController keyed nothing, ever.
        QVERIFY(!windowMox->isMox());
        QCOMPARE(windowMoxChanged.count(), 0);
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A key the Core refuses shows the Core's reason where a local refusal
    // shows, and the button follows the Core. Another device holding
    // transmit (here the radio's own position): the Core's sentence and fix.
    void aRefusedKeyShowsTheCoresReason()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        WindowControls window(h);
        window.follow(h.client);
        QSignalSpy refused(&h.remote, &RadioModel::remoteTransmitRefused);
        QSignalSpy refusedCodes(h.client.remoteTransmit(), &RemoteTransmitClient::refused);

        // The band plan: slice A off the band.
        h.station.installBandPlanMoxCheckForTest();
        h.station.sliceById(h.sliceA)->setFrequency(14400000.0);
        window.mox->click();
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refusedCodes.last().at(1).toString(), QStringLiteral("bandPlan"));
        const QString reason = refused.last().first().toString();
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
        QVERIFY(!h.station.moxController()->isMox());
        QVERIFY(!window.mox->isChecked());
        QVERIFY(!h.client.remoteTransmit()->micKeyDown());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // The radio's own key holds transmit ("Radio"): the window is told why
    // it may not transmit, and a press that races it gets the Core's
    // sentence and its fix (taking transmit is Task 77's).
    void anotherHolderRefusesWithItsSentence()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        WindowControls window(h);
        window.follow(h.client);
        QSignalSpy refused(&h.remote, &RadioModel::remoteTransmitRefused);
        QSignalSpy refusedCodes(h.client.remoteTransmit(), &RemoteTransmitClient::refused);
        h.station.setMoxFromButton(true);
        QTRY_VERIFY(h.station.moxController()->isMox());
        QTRY_VERIFY(!h.client.capabilities().txPermitted);
        QCOMPARE(h.client.capabilities().txRefusalCode, QStringLiteral("otherDeviceHolds"));
        QCOMPARE(h.client.capabilities().txRefusalReason,
                 QStringLiteral("Radio has the transmitter."));
        QCOMPARE(h.client.capabilities().txRefusalFix, QStringLiteral("takeTransmit"));
        window.follow(h.client);
        QVERIFY(!window.mox->isEnabled());
        QCOMPARE(window.mox->toolTip(), QStringLiteral("Radio has the transmitter."));
        h.remote.setMoxFromButton(true);   // a press racing the refusal
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.last().first().toString(), QStringLiteral("Radio has the transmitter."));
        QCOMPARE(refusedCodes.last().at(1).toString(), QStringLiteral("otherDeviceHolds"));
        QCOMPARE(refusedCodes.last().at(2).toString(), QStringLiteral("takeTransmit"));
        QCOMPARE(h.station.keyedBy().trigger, QByteArrayLiteral("station"));
        h.station.setMoxFromButton(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        QVERIFY(!h.remote.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // A window whose Core does not offer remoteTxVersion keeps today's
    // behaviour: nothing goes to the Core, the local refusal says why.
    void aCoreWithoutRemoteTransmitKeepsTodaysBehaviour()
    {
        Test::RemoteAudioSessionHarness h;
        h.declareRemoteTx = false;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.isHandshakeComplete());
        QCOMPARE(h.client.capabilities().remoteTxVersion, 0);
        QVERIFY(!h.client.capabilities().txPermitted);
        QVERIFY(!h.client.remoteTransmitAvailable());
        QVERIFY(!h.remote.remoteTransmitRouted());
        // Merge of Tasks 38 and 39: no txState, so the Options page's
        // time-out settings wait for a newer Core.
        QCOMPARE(h.client.capabilities().txStateVersion, 0);
        QVERIFY(!h.client.transmitTimeOutAvailable());
        WindowControls window(h);
        window.follow(h.client);
        QVERIFY(!window.mox->isEnabled());
        QVERIFY(!window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).available);
        QCOMPARE(window.container.stateOf(ContainerButtonDispatcher::Id::Mox, 0).reason,
                 QStringLiteral("Remote transmit controls are not available from this Core."));
        // Even reached around the disabled button, the press stays here.
        QSignalSpy rejected(h.remote.moxController(), &MoxController::moxRejected);
        h.remote.setMoxFromButton(true);
        QCOMPARE(rejected.count(), 1);
        QTest::qWait(200);
        QVERIFY(commandsOf(h.stationLink, QStringLiteral("tx.key")).isEmpty());
        QVERIFY(!h.station.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // ---- The microphone ---------------------------------------------------

    // The media start offers the microphone line. The window sends its
    // microphone only while its key is down (and the Core keys from it,
    // once its buffer fills) and never otherwise, counted in packets.
    void theMicrophoneRunsOnlyWhileKeyed()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h, 0.3f);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        QSignalSpy coreControls(&h.server, &StationServer::mediaControlReceived);
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QVERIFY(remoteMedia.micLineNegotiated());
        bool offered = false;
        for (const auto& call : coreControls) {
            const QJsonObject c = call.at(0).toJsonObject();
            if (c.value(QStringLiteral("op")) == QLatin1String("start")) {
                offered = c.value(QStringLiteral("remoteTxVersion")).toInt() == 1;
            }
        }
        QVERIFY(offered);
        WindowControls window(h);
        window.follow(h.client);
        QTest::qWait(1500);
        QCOMPARE(remoteMedia.micPacketsSent(), quint64(0));
        QVERIFY(!remoteMedia.micUplinkRunning());

        // MOX down: the microphone goes, the Core keys on a filled buffer,
        // from this window's line.
        window.mox->click();
        QVERIFY(remoteMedia.micUplinkRunning());
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QCOMPARE(h.station.keyedBy().deviceId, h.windowKey->fingerprint());
        QVERIFY(h.station.remoteMicInUse());
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.micPacketsSent() >= 20, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver()->stats().accepted >= 20, 5000);

        // Released: nothing more goes, and the Core is unkeyed.
        window.mox->click();
        QVERIFY(!remoteMedia.micUplinkRunning());
        QTRY_VERIFY(!h.station.moxController()->isMox());
        const quint64 stopped = remoteMedia.micPacketsSent();
        QTest::qWait(400);
        QCOMPARE(remoteMedia.micPacketsSent(), stopped);
        QVERIFY(!h.remote.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // With no microphone audio the Core refuses the key micNotReady, and
    // the window says so.
    void aKeyWithoutMicrophoneAudioIsRefused()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        // A microphone that gives nothing.
        attachMicrophone(h, -1.0f);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy refusedCodes(h.client.remoteTransmit(), &RemoteTransmitClient::refused);
        h.remote.setMoxFromButton(true);
        QTRY_COMPARE_WITH_TIMEOUT(refusedCodes.count(), 1, 5000);
        QCOMPARE(refusedCodes.first().at(1).toString(), QStringLiteral("micNotReady"));
        QCOMPARE(refusedCodes.first().at(0).toString(),   // fix wave M4
                 QStringLiteral("No sound has reached the Core from this device's microphone. "
                                "Wait a moment and try again."));
        QVERIFY(!h.station.moxController()->isMox());
        QVERIFY(!remoteMedia.micUplinkRunning());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // Fix wave C1: a window with no microphone line (no media yet, as right
    // after a reconnect) is refused its voice key in plain words, and the
    // Core never keys on its own microphone; TUNE still keys.
    void aKeyWithoutTheMicrophoneLineIsRefusedAndNothingKeys()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QSignalSpy refusedCodes(h.client.remoteTransmit(), &RemoteTransmitClient::refused);
        h.remote.setMoxFromButton(true);
        QTRY_COMPARE_WITH_TIMEOUT(refusedCodes.count(), 1, 5000);
        QCOMPARE(refusedCodes.first().at(1).toString(), QStringLiteral("micNotReady"));
        QCOMPARE(refusedCodes.first().at(0).toString(), TxRefusals::micNotConnected().text);
        QVERIFY(!h.station.moxController()->isMox());
        QVERIFY(!h.station.remoteMicInUse());
        QTRY_VERIFY(!h.remote.moxController()->isMox());
        h.remote.setTune(true);
        QTRY_VERIFY(h.station.moxController()->isMox());
        h.remote.setTune(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // ---- VOX --------------------------------------------------------------

    // The window's VOX button arms the Core's VOX (transmit.voxEnabled); the
    // window then streams its microphone unkeyed, the Core's VOX keys from
    // it as this device, and disarming stops both.
    // Fix wave M6: VOX the window did not arm (turned on at the Core
    // itself, or by another device) never starts its microphone or its
    // keepalives.
    void voxArmedElsewhereDoesNotStreamTheWindowsMicrophone()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h, 0.3f);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        h.station.transmitModel().setVoxEnabled(true);
        QTRY_VERIFY(h.remote.transmitModel().voxEnabled());
        QVERIFY(!h.client.voxArmedHere());
        QTest::qWait(400);
        QVERIFY(!remoteMedia.micUplinkRunning());
        QCOMPARE(remoteMedia.micPacketsSent(), quint64(0));
        QVERIFY(!h.client.remoteTransmit()->keepaliveRunning());
        h.station.transmitModel().setVoxEnabled(false);
        QTRY_VERIFY(!h.remote.transmitModel().voxEnabled());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // Fix wave 2 (M8): until this computer's microphone line to the Core
    // opens, the VOX button shows disabled with the plain reason (not
    // refused on write); it comes alive when the media connection carries
    // the line, and goes back when the line closes. The Core's refusal
    // stays the backstop.
    void voxShowsDisabledWithItsReasonUntilTheMicrophoneLineOpens()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h, 0.3f);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        QSignalSpy line(&remoteMedia, &RemoteMediaController::micLineChanged);
        WindowControls window(h);
        QObject::connect(&remoteMedia, &RemoteMediaController::micLineChanged, &window.applet,
                         [&]() { window.follow(h.client, &remoteMedia); });
        auto daemonMedia = std::make_unique<DaemonMediaController>(&h.server, &h.station);
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        window.follow(h.client, &remoteMedia);
        if (!remoteMedia.micLineOpen()) {
            // Transmit is permitted, the line is not there yet.
            QVERIFY(!window.vox->isEnabled());
            QCOMPARE(window.vox->toolTip(), TxRefusals::micNotConnected().text);
            QVERIFY(window.mox->isEnabled());
        }
        QTRY_VERIFY_WITH_TIMEOUT(remoteMedia.micLineOpen(), 5000);
        QVERIFY(line.count() >= 1);
        QCOMPARE(line.last().first().toBool(), true);
        QTRY_VERIFY(window.vox->isEnabled());
        QVERIFY(window.vox->toolTip() != TxRefusals::micNotConnected().text);

        // The media connection goes (the line with it): disabled again,
        // with the reason, while MOX stays as it was.
        daemonMedia.reset();
        QTRY_VERIFY_WITH_TIMEOUT(!remoteMedia.micLineOpen(), 5000);
        QTRY_VERIFY(!window.vox->isEnabled());
        QCOMPARE(window.vox->toolTip(), TxRefusals::micNotConnected().text);
        QVERIFY(window.mox->isEnabled());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // With no media at all (a window whose media never starts), VOX stays
    // disabled with the reason while transmit is permitted.
    void voxWithNoMediaStaysDisabledWithTheReason()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        WindowControls window(h);
        window.follow(h.client, &remoteMedia);
        QVERIFY(!remoteMedia.micLineOpen());
        QVERIFY(!window.vox->isEnabled());
        QCOMPARE(window.vox->toolTip(), TxRefusals::micNotConnected().text);
        QCOMPARE(window.vox->accessibleDescription(), TxRefusals::micNotConnected().text);
        // Transmit refused as a whole: that reason shows instead.
        window.applet.setTransmitPermitted(false, QStringLiteral("This Core is set to receive only."));
        QCOMPARE(window.vox->toolTip(), QStringLiteral("This Core is set to receive only."));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void voxArmedFromTheWindowKeysTheCoreFromItsMicrophone()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h, 0.3f);
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        WindowControls window(h);
        window.follow(h.client);
        QVERIFY(window.vox->isEnabled());
        QTest::qWait(500);
        QCOMPARE(remoteMedia.micPacketsSent(), quint64(0));

        // iPhone app plan Task 77 (ruling 8.4): arming VOX needs holding
        // transmit. This window takes it with a press of its MOX (a
        // person's key on unheld transmit takes it), then arms VOX.
        QVERIFY(pressMoxUntilKeyed(h));
        h.remote.setMoxFromButton(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        QTRY_COMPARE(h.client.transmitHolderText(), QStringLiteral("This computer holds transmit."));
        window.vox->click();
        QTRY_VERIFY(h.station.transmitModel().voxEnabled());
        QTRY_VERIFY(remoteMedia.micUplinkRunning());
        QTRY_VERIFY(h.station.remoteMicInUse());
        QCOMPARE(h.station.remoteVoxDevice(), h.windowKey->fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver()->stats().accepted >= 20, 5000);
        QVERIFY(!h.station.moxController()->isMox());

        // The Core's VOX detector hears the window's microphone.
        h.station.moxController()->onVoxActive(true);
        QTRY_VERIFY(h.station.moxController()->isMox());
        QCOMPARE(h.station.keyedBy().deviceId, h.windowKey->fingerprint());
        QCOMPARE(h.station.keyedBy().trigger, QByteArrayLiteral("vox"));
        QTRY_VERIFY(h.remote.isTransmitting());
        h.station.moxController()->onVoxActive(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());

        // Disarmed from the window: the Core's VOX is off, nothing more goes.
        window.vox->click();
        QTRY_VERIFY(!h.station.transmitModel().voxEnabled());
        QTRY_VERIFY(!remoteMedia.micUplinkRunning());
        const quint64 stopped = remoteMedia.micPacketsSent();
        QTest::qWait(400);
        QCOMPARE(remoteMedia.micPacketsSent(), stopped);
        QVERIFY(!h.station.remoteMicInUse());
        QVERIFY(!h.remote.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // ---- TCI --------------------------------------------------------------

    // A program's trx through the window's TCI server keys the Core with
    // trigger tci (this window holding transmit), and its transmit audio
    // reaches the Core's transmit ring in place of the microphone.
    void aProgramThroughTheWindowsTciKeysTheCore()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        attachMicrophone(h, 0.0f);   // a silent microphone: only the program is heard
        RemoteMediaController remoteMedia(&h.client, &h.remote, nullptr);
        DaemonMediaController daemonMedia(&h.server, &h.station);
        TciServer tci(&h.remote);
        tci.setRemoteTransmit(remoteTransmitForwarder(h.client.remoteTransmit(), &remoteMedia));
        h.connectSession();
        QTRY_VERIFY_WITH_TIMEOUT(daemonMedia.micReceiver() != nullptr, 5000);
        QTRY_VERIFY(h.client.capabilities().txPermitted);

        // This window takes transmit with a press, and lets go.
        QVERIFY(pressMoxUntilKeyed(h));
        h.remote.setMoxFromButton(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        QVERIFY(h.server.transmitHolder()->isHeldBy(h.windowKey->fingerprint()));

        QVERIFY(tci.start(0));
        QWebSocket app;
        QSignalSpy text(&app, &QWebSocket::textMessageReceived);
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(tci.port())));
        QTRY_VERIFY_WITH_TIMEOUT(app.state() == QAbstractSocket::ConnectedState, 3000);
        const auto texts = [&text] {
            QStringList out;
            for (const auto& call : text) { out << call.at(0).toString(); }
            return out;
        };
        QTRY_VERIFY_WITH_TIMEOUT(texts().contains(QStringLiteral("ready;")), 3000);
        QVERIFY(texts().contains(QStringLiteral("receive_only:false;")));

        app.sendTextMessage(QStringLiteral("trx:0,true,tci;"));
        QTRY_VERIFY_WITH_TIMEOUT(h.station.moxController()->isMox(), 5000);
        QCOMPARE(h.station.keyedBy().trigger, QByteArrayLiteral("tci"));
        QCOMPARE(h.station.keyedBy().deviceId, h.windowKey->fingerprint());
        const QList<QJsonObject> keys = commandsOf(h.stationLink, QStringLiteral("tx.key"));
        QCOMPARE(argument(keys.last(), QStringLiteral("trigger")).toString(), QStringLiteral("tci"));
        QVERIFY(sentAsCopies(keys));
        QTRY_COMPARE_WITH_TIMEOUT(tci.activeTxClientCount(), 1, 3000);

        // The program's audio: a 0.4 tone at 48 kHz, stereo, 20 ms frames.
        RemoteMicFeed* feed = h.station.remoteMicFeed();
        QVERIFY(feed != nullptr && feed->inUse());
        for (int chunk = 0; chunk < 40; ++chunk) {
            std::vector<float> samples(960 * 2);
            for (int i = 0; i < 960; ++i) {
                const float v = 0.4f * static_cast<float>(std::sin(
                    2.0 * 3.14159265358979323846 * 1000.0 * (chunk * 960 + i) / 48000.0));
                samples[static_cast<size_t>(2 * i)] = v;
                samples[static_cast<size_t>(2 * i + 1)] = v;
            }
            app.sendBinaryMessage(TciBinaryFrame::buildStreamPayload(
                0, 48000, static_cast<int>(TciSampleType::Float32),
                static_cast<int>(samples.size()),
                static_cast<int>(TciStreamType::TxAudioStream), 2, samples.data()));
            QTest::qWait(20);
        }
        QTRY_VERIFY_WITH_TIMEOUT(feed->framesSinceInUse() >= 4 * RemoteMicConfig::kTargetDepthFrames,
                                 5000);
        // R-IOS-13 (2026-09-27): the buffer keeps everything that arrived
        // and plays it in order (it no longer drops the oldest past 120
        // ms), so the line's silence from before the program plays first;
        // the program follows within the 800 ms it lasts.
        std::vector<float> block(RemoteMicConfig::kPumpBlockFrames);
        double loudest = 0.0;
        double sum = 0.0;
        for (int b = 0; b < 600; ++b) {
            feed->pull(block.data(), RemoteMicConfig::kPumpBlockFrames);
            for (float v : block) { sum += double(v) * v; }
            if (b % 20 == 19) {
                loudest = std::max(loudest, std::sqrt(sum / (20.0 * block.size())));
                sum = 0.0;
            }
        }
        QVERIFY2(loudest > 0.1, qPrintable(QString::number(loudest)));   // the program, not silence

        app.sendTextMessage(QStringLiteral("trx:0,false;"));
        QTRY_VERIFY_WITH_TIMEOUT(!h.station.moxController()->isMox(), 5000);
        QVERIFY(!h.remote.moxController()->isMox());
        app.close();
        tci.stop();
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
    // ---- The Core's transmit state (merge of Tasks 37 to 39) --------------

    // Now that the window declares remoteTx, the Core sends it txState: a
    // keyed window's transmit meters read the Core's forward power, SWR,
    // ALC and MIC through the same meter items a local window uses.
    void aKeyedWindowReadsTheCoresTransmitMeters()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        // The Core's meters, as its transmit lane would read them keyed.
        h.server.transmitState()->meterPump()->setSource([]() {
            TxMeterReadings r;
            r.forwardPowerWatts = 50.0;
            r.reflectedPowerWatts = 2.0;
            r.swr = 1.5;
            r.alcDb = -3.0;
            r.micLevelDb = -12.0;
            return r;
        });
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QCOMPARE(h.client.capabilities().txStateVersion, 2);   // fix wave I4
        QVERIFY(h.client.transmitTimeOutAvailable());
        TransmitState* state = h.client.transmitState();
        QVERIFY(state != nullptr);

        MeterWidget bars;
        bars.resize(200, 200);
        QHash<int, TextItem*> items;
        const QList<int> bindings{MeterBinding::TxPower, MeterBinding::TxReversePower,
                                  MeterBinding::TxSwr, MeterBinding::TxAlc, MeterBinding::TxMic};
        for (int i = 0; i < bindings.size(); ++i) {
            auto* item = new TextItem(&bars);
            item->setBindingId(bindings.at(i));
            item->setRect(0.0f, 0.2f * i, 1.0f, 0.2f);
            bars.addItem(item);
            items.insert(bindings.at(i), item);
        }
        MeterPoller poller;
        poller.addTarget(&bars);
        poller.setRadioStatus(&h.remote.radioStatus());
        poller.setRemoteRadioModel(&h.remote, []() { return true; });
        // As MainWindow::wireRemoteTransmitMeters words it.
        StationClient* client = &h.client;
        poller.setRemoteTransmitState(state, [client]() -> QString {
            if (!client->isHandshakeComplete()) {
                return QStringLiteral("Connect to the Core to see transmit meters here.");
            }
            if (client->capabilities().txStateVersion < 1) {
                return QStringLiteral("This Core does not send transmit meters. Update the "
                                      "Core to see them here.");
            }
            return {};
        });
        const auto tick = [&poller]() {
            QVERIFY(QMetaObject::invokeMethod(&poller, "poll", Qt::DirectConnection));
        };
        tick();
        for (int binding : bindings) {
            QVERIFY2(bars.bindingUnavailableReason(binding).isEmpty(),
                     qPrintable(QString::number(binding)));
        }

        QVERIFY(pressMoxUntilKeyed(h));
        QTRY_VERIFY(state->keyed());
        QTRY_COMPARE(state->forwardPowerWatts(), 50.0);
        QTRY_COMPARE(state->alcDb(), -3.0);
        tick();
        QCOMPARE(items.value(MeterBinding::TxPower)->value(), 50.0);
        QCOMPARE(items.value(MeterBinding::TxReversePower)->value(), 2.0);
        // SWR from 50 W forward and 2 W reflected: rho 0.2, (1.2 / 0.8).
        QVERIFY2(qAbs(items.value(MeterBinding::TxSwr)->value() - 1.5) < 1e-9,
                 qPrintable(QString::number(items.value(MeterBinding::TxSwr)->value())));
        QCOMPARE(items.value(MeterBinding::TxAlc)->value(), -3.0);
        QCOMPARE(items.value(MeterBinding::TxMic)->value(), -12.0);
        QCOMPARE(h.remote.radioStatus().forwardPowerWatts(), 50.0);
        // The window's own MoxController never keyed: the readings are the
        // Core's.
        QVERIFY(!h.remote.moxController()->isMox());

        h.remote.setMoxFromButton(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        QTRY_VERIFY(!state->keyed());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // The Core's transmit time-out stops the window's key (a computer: the
    // Core's own MOX time-out, switched on here at 30 s) and the window is
    // told why in the Core's words.
    void theCoresTimeOutStopsTheWindowsKeyAndSaysWhy()
    {
        AppSettings::instance().setValue(QStringLiteral("MoxTimeOutEnabled"), QStringLiteral("True"));
        AppSettings::instance().setValue(QStringLiteral("MoxTimeOutSeconds"), 30);
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        qint64 timeOutNow = 0;
        h.station.txTimeOutTimer()->setClock([&timeOutNow]() { return timeOutNow; });
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        TransmitState* state = h.client.transmitState();

        QVERIFY(pressMoxUntilKeyed(h));
        QTRY_COMPARE(state->timeOutRemainingSeconds(), 30);
        timeOutNow = 30'000;
        h.station.txTimeOutTimer()->tick();
        QTRY_VERIFY(!h.station.moxController()->isMox());
        const QString text =
            QStringLiteral("Transmit stopped after 0:30, the Core's transmit time-out.");
        QTRY_COMPARE(state->stopSerial(), quint32(1));
        QCOMPARE(state->stopReason(), QStringLiteral("timeOut"));
        QCOMPARE(state->stopText(), text);
        // The stop and the unkey are separate notify groups; they may come
        // in different flushes.
        QTRY_VERIFY(!state->keyed());
        // The window forgot its key: the next press is a new command.
        QTRY_VERIFY(!h.remote.isTransmitting());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
        AppSettings::instance().remove(QStringLiteral("MoxTimeOutEnabled"));
        AppSettings::instance().remove(QStringLiteral("MoxTimeOutSeconds"));
    }

    // Merge of the trunk into the transmit lane: the window-only transmit
    // controls reach the Core through the parity lane's settings path. On a
    // Core with remote transmit, from a permitted window, the TX applet's
    // RF Power (the drive) and Tune Power sliders change the Core's.
    void tunePowerAndDriveFromTheWindowReachTheCore()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QVERIFY(h.client.transmitSettingsAvailable(2));
        WindowControls window(h);
        window.follow(h.client);
        // As MainWindow's applyRemoteRoleGating does for these settings.
        window.applet.setTransmitSettingsPermitted(true, {});
        window.applet.setTransmitChainSettingsPermitted(true, {});
        QSlider* drive = nullptr;
        QSlider* tune = nullptr;
        for (QSlider* slider : window.applet.findChildren<QSlider*>()) {
            if (slider->accessibleName() == QStringLiteral("RF power")) { drive = slider; }
            if (slider->accessibleName() == QStringLiteral("Tune power")) { tune = slider; }
        }
        QVERIFY(drive && tune);
        QVERIFY(drive->isEnabled());
        QVERIFY(tune->isEnabled());

        TransmitModel& core = h.station.transmitModel();
        const int driveWanted = core.power() == 37 ? 38 : 37;
        drive->setValue(driveWanted);
        QTRY_COMPARE(core.power(), driveWanted);

        const int tuneWanted = core.tunePowerForTxBand() == 23 ? 24 : 23;
        tune->setValue(tuneWanted);
        QTRY_COMPARE(core.tunePowerForTxBand(), tuneWanted);
        QTRY_COMPARE(h.remote.transmitModel().tunePowerForTxBand(), tuneWanted);
        // Nothing keyed.
        QVERIFY(!h.station.moxController()->isMox());
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // Fix wave I4: the window says who holds transmit on the Core, from
    // txState's holder: this window, the radio, another device, away, and
    // changing hands.
    void theWindowSaysWhoHoldsTransmit()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.openFakeMicrophoneLine();   // fix wave C1: no media here
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        QCOMPARE(h.client.capabilities().txStateVersion, 2);
        WindowControls window(h);
        const auto show = [&h, &window] {
            window.applet.setTransmitHolderText(h.client.transmitHolderText());
            return window.applet.transmitHolderText();
        };
        QCOMPARE(show(), QString());
        // This window keys: it holds transmit.
        QVERIFY(pressMoxUntilKeyed(h));
        QTRY_COMPARE(show(), QStringLiteral("This computer holds transmit."));
        h.remote.setMoxFromButton(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        QCOMPARE(show(), QStringLiteral("This computer holds transmit."));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void theWindowSaysTheRadioHoldsTransmit()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.capabilities().txPermitted);
        h.station.moxController()->onMicPttFromRadio(true);
        QTRY_VERIFY(h.station.moxController()->isMox());
        QTRY_COMPARE(h.client.transmitHolderText(),
                     QStringLiteral("The radio has the transmitter. Take it from this window to "
                                    "transmit."));
        h.station.moxController()->onMicPttFromRadio(false);
        QTRY_VERIFY(!h.station.moxController()->isMox());
        // Task 77 (ruling 8.1): the radio keeps transmit after its press.
        QTest::qWait(100);
        QCOMPARE(h.client.transmitHolderText(),
                 QStringLiteral("The radio has the transmitter. Take it from this window to "
                                    "transmit."));
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }

    // The words for another device, away, and a transfer, from the values a
    // Core sends.
    void theHolderWordsForAnotherDeviceAwayAndChangingHands()
    {
        Test::RemoteAudioSessionHarness h;
        h.pairWindow = true;
        h.makeTransmitReady();
        h.connectSession();
        QTRY_VERIFY(h.client.isHandshakeComplete());
        TransmitState* tx = h.client.transmitState();
        QVERIFY(tx != nullptr);
        // As the Core would send them (no later delta arrives in between).
        tx->applyStationValue("holderDeviceId", QStringLiteral("another"));
        tx->applyStationValue("holderName", QStringLiteral("Grant's iPhone"));
        // Task 78: a window that shares the Core as a device (signed in
        // with its own key) can take transmit, and says how.
        QTRY_VERIFY(h.client.transmitTakeAvailable());
        QCOMPARE(h.client.transmitHolderText(),
                 QStringLiteral("Grant's iPhone holds transmit. Take transmit to use MOX and "
                                "TUNE here."));
        tx->applyStationValue("holderAway", true);
        QCOMPARE(h.client.transmitHolderText(),
                 QStringLiteral("Grant's iPhone holds transmit and is away. Take transmit to use "
                                "MOX and TUNE here."));
        tx->applyStationValue("holderTransferring", true);
        QCOMPARE(h.client.transmitHolderText(), QStringLiteral("Transmit is changing hands."));
        // Fix wave 2: a transfer that ended with MOX still on leaves nobody
        // holding and every key refused with the Core's words.
        tx->applyStationValue("holderDeviceId", QString());
        QCOMPARE(h.client.transmitHolderText(),
                 QStringLiteral("The radio did not confirm it stopped transmitting."));
        // The device's name is the operator's own word, set aside here.
        for (const QString& text : {QStringLiteral("Another device holds transmit and is away. MOX "
                                                   "and TUNE here wait until it lets go."),
                                    QStringLiteral("Another device holds transmit and is away. Take "
                                                   "transmit to use MOX and TUNE here."),
                                    QStringLiteral("Transmit is changing hands."),
                                    QStringLiteral("The radio did not confirm it stopped transmitting."),
                                    QStringLiteral("This computer holds transmit.")}) {
            QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        }
        h.client.disconnectFromStation(QStringLiteral("test complete"));
    }
};

QTEST_MAIN(TestRemoteWindowTransmit)
#include "tst_remote_window_transmit.moc"
