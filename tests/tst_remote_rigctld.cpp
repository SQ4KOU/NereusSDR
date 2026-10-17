// =================================================================
// tests/tst_remote_rigctld.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. A remote window's rigctld
// (RemoteRigctlServer): Hamlib's f/F/m/M/t/T on the slice the window
// transmits on, keys as the window's program through the Core's remote
// transmit, and the VAX TX device's audio on the key.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                Claude Code.
//   2026-10-15 - Review fixes: a key asked again before the Core answers,
//                a key refused inside key(), the forwarder going while
//                keyed, and the VAX TX device read only after key().
//   2026-10-15 - The pump reads only while the program-audio key is on; the
//                Core ending the key ends it here.
//   2026-10-17 - The pump reads keeping the Core's deepest buffer; the
//                drain before a key does not.
// =================================================================

#include <QtTest/QtTest>

#include <QTcpSocket>

#include "core/AudioEngine.h"
#include "core/IAudioBus.h"
#include "core/TciServer.h"
#include "core/session/media/RemoteMicReceiver.h"
#include "gui/RemoteRigctlServer.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "fakes/FakeAudioBus.h"

#include <functional>
#include <memory>

using namespace NereusSDR;

namespace {

// The Core's side of the window's transmit, faked as tst_tci_remote_window
// fakes it: every key and unkey recorded, a key answered when the test says.
struct FakeCoreTransmit {
    int keys = 0;
    QList<bool> programAudio;
    QList<quint32> unkeys;
    QVector<float> audio;
    QList<int> audioRates;
    std::function<void(const TciServer::RemoteKeyAnswer&)> pending;
    // Refuses inside key(), as RemoteTransmitClient::keyForProgram does
    // when another program's key is on.
    bool refuseInsideKey = false;
    // Runs inside key(), where keyForProgram stops the window's VAX feed.
    std::function<void()> duringKey;

    TciServer::RemoteTransmit forwarder()
    {
        TciServer::RemoteTransmit forward;
        forward.key = [this](bool withProgramAudio,
                             std::function<void(const TciServer::RemoteKeyAnswer&)> answer) {
            ++keys;
            programAudio << withProgramAudio;
            if (duringKey) {
                duringKey();
            }
            if (refuseInsideKey) {
                TciServer::RemoteKeyAnswer refused;
                refused.reason = QStringLiteral("Another program is transmitting.");
                answer(refused);
                return;
            }
            pending = std::move(answer);
        };
        forward.unkey = [this](quint32 epoch) { unkeys << epoch; };
        forward.audio = [this](const float* samples, int frames, int channels, int rate) {
            for (int i = 0; i < frames * channels; ++i) {
                audio << samples[i];
            }
            audioRates << rate;
        };
        return forward;
    }

    void accept(quint32 epoch)
    {
        TciServer::RemoteKeyAnswer answer;
        answer.accepted = true;
        answer.epoch = epoch;
        auto reply = std::move(pending);
        pending = {};
        reply(answer);
    }

    void refuse(const QString& reason)
    {
        TciServer::RemoteKeyAnswer answer;
        answer.reason = reason;
        auto reply = std::move(pending);
        pending = {};
        reply(answer);
    }
};

struct Window {
    RadioModel model{RadioModel::Role::Remote};
    FakeCoreTransmit core;
    RemoteRigctlServer server{&model};
    QTcpSocket socket;

    Window()
    {
        // Two of the Core's slices; the window transmits on the second.
        model.addSliceWithStationId(0);
        model.addSliceWithStationId(1);
        model.sliceById(0)->setFrequency(7074000.0);
        model.sliceById(1)->setFrequency(3870000.0);
        model.sliceById(1)->setTxSlice(true);
        server.setRemoteTransmit(core.forwarder());
    }

    bool connect()
    {
        if (!server.start(QHostAddress::LocalHost, 0)) {
            return false;
        }
        socket.connectToHost(QHostAddress::LocalHost, server.port());
        return socket.waitForConnected(3000);
    }

    QByteArray ask(const QByteArray& line, int lines = 1)
    {
        socket.write(line);
        QByteArray got;
        QTest::qWaitFor([&] {
            got += socket.readAll();
            return got.count('\n') >= lines;
        }, 3000);
        return got;
    }
};

} // namespace

class TstRemoteRigctld : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void readsAndTunesTheSliceTheWindowTransmitsOn()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("f\n"), QByteArray("3870000\n"));
        QCOMPARE(w.ask("F 14074000\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.model.sliceById(1)->frequency(), 14074000.0);
        QCOMPARE(w.model.sliceById(0)->frequency(), 7074000.0);
        QCOMPARE(w.ask("M PKTUSB 3000\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.model.sliceById(1)->dspMode(), DSPMode::DIGU);
        const QByteArray mode = w.ask("m\n", 2);
        QVERIFY2(mode.startsWith("PKTUSB\n"), mode.constData());
        QCOMPARE(w.ask("s\n", 2), QByteArray("0\nVFOA\n"));
    }

    void aSliceTheWindowOnlyListensToIsNeverTuned()
    {
        Window w;
        w.model.sliceById(1)->setReadOnlyListener(true, QStringLiteral("Another device"));
        w.model.sliceById(0)->setReadOnlyListener(true, QStringLiteral("Another device"));
        QVERIFY(w.connect());
        QCOMPARE(w.ask("f\n"), QByteArray("RPRT -12\n"));
        QCOMPARE(w.ask("F 14074000\n"), QByteArray("RPRT -12\n"));
        QCOMPARE(w.model.sliceById(1)->frequency(), 3870000.0);
        // With the TX slice only listened to, VFO A is the next slice the
        // window controls.
        w.model.sliceById(0)->setReadOnlyListener(false, QString());
        QCOMPARE(w.ask("f\n"), QByteArray("7074000\n"));
    }

    void pttKeysAsTheWindowsProgramAndReleasesItsEpoch()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.core.keys, 1);
        QCOMPARE(w.core.programAudio, QList<bool>{true});
        QCOMPARE(w.ask("t\n"), QByteArray("1\n"));
        // A second T 1 asks nothing more of the Core.
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.core.keys, 1);
        w.core.accept(41);
        // The slice it transmits on is frozen while the key is on, as the
        // Core freezes it (RPRT -9, not an authority refusal).
        QCOMPARE(w.ask("F 7074000\n"), QByteArray("RPRT -9\n"));
        QCOMPARE(w.ask("T 0\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.core.unkeys, QList<quint32>{41});
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
    }

    void releaseBeforeTheAnswerUnkeysTheAnsweredKey()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.ask("T 0\n"), QByteArray("RPRT 0\n"));
        QVERIFY(w.core.unkeys.isEmpty());
        w.core.accept(9);
        QCOMPARE(w.core.unkeys, QList<quint32>{9});
        QVERIFY(!w.server.keyed());
    }

    void aRefusedKeyReadsBackAsReceive()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        w.core.refuse(QStringLiteral("Another device is transmitting."));
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
        QVERIFY(w.core.unkeys.isEmpty());
    }

    void noRemoteTransmitRefusesPtt()
    {
        Window w;
        w.server.setRemoteTransmit(TciServer::RemoteTransmit{});
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT -9\n"));
        QCOMPARE(w.core.keys, 0);
    }

    void theClientThatKeyedLeavingUnkeys()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        w.core.accept(5);
        w.socket.disconnectFromHost();
        QTRY_COMPARE_WITH_TIMEOUT(w.core.unkeys, QList<quint32>{5}, 3000);
        QVERIFY(!w.server.keyed());
    }

    void aKeyAskedAgainBeforeTheCoreAnswersStaysOn()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.ask("T 0\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.core.keys, 1);
        w.core.accept(5);
        QVERIFY(w.core.unkeys.isEmpty());
        QCOMPARE(w.ask("t\n"), QByteArray("1\n"));
        QCOMPARE(w.ask("T 0\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(w.core.unkeys, QList<quint32>{5});
    }

    void aKeyRefusedInsideKeyAnswersRprtMinus9()
    {
        Window w;
        w.core.refuseInsideKey = true;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT -9\n"));
        QCOMPARE(w.core.keys, 1);
        QVERIFY(!w.server.keyed());
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
    }

    void remoteTransmitGoingWhileKeyedReleasesTheKey()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        w.core.accept(6);
        QVERIFY(w.server.keyed());
        // As TciServer does: released through the old forwarder.
        w.server.setRemoteTransmit(TciServer::RemoteTransmit{});
        QCOMPARE(w.core.unkeys, QList<quint32>{6});
        QVERIFY(!w.server.keyed());
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
    }

    void theCoreEndingTheKeyEndsItHere()
    {
        Window w;
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        w.core.accept(7);
        QVERIFY(w.model.applyMirroredValue("transmitting", true).isEmpty());
        QVERIFY(w.server.keyed());
        QVERIFY(w.model.applyMirroredValue("transmitting", false).isEmpty());
        QVERIFY(!w.server.keyed());
        QVERIFY(w.core.unkeys.isEmpty());
        QCOMPARE(w.ask("t\n"), QByteArray("0\n"));
    }

    void thePumpReadsOnlyWhileTheProgramAudioKeyIsOn()
    {
        Window w;
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        auto bus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeVaxTx"));
        bus->open(fmt);
        FakeAudioBus* vaxTx = bus.get();
        w.model.localAudioDevices()->setVaxTxBusForTest(std::move(bus));
        bool programAudioKey = true;
        w.server.setProgramAudioKey([&programAudioKey] { return programAudioKey; });
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        w.core.accept(4);
        QVector<float> tone(2 * 128, 0.5f);
        const QByteArray bytes(reinterpret_cast<const char*>(tone.constData()),
                               tone.size() * int(sizeof(float)));
        // The Core ended the program's key (its stop, a refusal, the link)
        // and the window's VAX feed reads the device again: the pump never
        // reads it, even before this server hears that the key is gone.
        programAudioKey = false;
        vaxTx->setPullData(bytes);
        const int pulls = vaxTx->pullCount();
        w.server.pumpAudio();
        QCOMPARE(vaxTx->pullCount(), pulls);
        QVERIFY(w.core.audio.isEmpty());
        programAudioKey = true;
        w.server.pumpAudio();
        QCOMPARE(w.core.audio.size(), 128);
    }

    void thePumpKeepsTheCoresDeepestBufferTheDrainDoesNot()
    {
        // A late GUI tick finds more than the Mac bus's 16 ms guard; the
        // pump keeps up to what the Core's transmit side holds. The drain
        // before a key throws everything away, so it reads as pull().
        Window w;
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        auto bus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeVaxTx"));
        bus->open(fmt);
        FakeAudioBus* vaxTx = bus.get();
        w.model.localAudioDevices()->setVaxTxBusForTest(std::move(bus));
        QVector<float> stale(2 * 64, 0.9f);
        vaxTx->setPullData(QByteArray(reinterpret_cast<const char*>(stale.constData()),
                                      stale.size() * int(sizeof(float))));
        QVERIFY(w.connect());
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        // The stale audio went, and not through the backlog read (the pump's
        // timer may tick meanwhile, finding nothing).
        QCOMPARE(vaxTx->pullCount(), 1);
        QCOMPARE(vaxTx->keepingBacklogReads(), 0);
        w.core.accept(5);
        QVector<float> tone(2 * 128, 0.5f);
        vaxTx->setPullData(QByteArray(reinterpret_cast<const char*>(tone.constData()),
                                      tone.size() * int(sizeof(float))));
        w.server.pumpAudio();
        QCOMPARE(vaxTx->keepingBacklogReads(), 1);
        // Stereo float32 frames, in bytes.
        QCOMPARE(vaxTx->lastMaxBacklogBytes(),
                 qint64(RemoteMicConfig::kMaxDepthFrames) * 2 * qint64(sizeof(float)));
        QCOMPARE(w.core.audio.size(), 128);
    }

    void theKeySendsTheVaxTxDevicesAudioNeverWhatCameBefore()
    {
        Window w;
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels = 2;
        fmt.sample = AudioFormat::Sample::Float32;
        auto bus = std::make_unique<FakeAudioBus>(QStringLiteral("FakeVaxTx"));
        bus->open(fmt);
        FakeAudioBus* vaxTx = bus.get();
        AudioEngine* engine = w.model.localAudioDevices();
        QVERIFY(engine != nullptr);
        engine->setVaxTxBusForTest(std::move(bus));
        // Played before the key: never sent.
        QVector<float> stale(2 * 64, 0.9f);
        vaxTx->setPullData(QByteArray(reinterpret_cast<const char*>(stale.constData()),
                                      stale.size() * int(sizeof(float))));
        QVERIFY(w.connect());
        // Nothing reads the device until key() returns: inside it the
        // window's own VAX feed is still stopping (one reader at a time).
        int pullsDuringKey = -1;
        w.core.duringKey = [&] { pullsDuringKey = vaxTx->pullCount(); };
        QCOMPARE(w.ask("T 1\n"), QByteArray("RPRT 0\n"));
        QCOMPARE(pullsDuringKey, 0);
        QVERIFY(vaxTx->pullCount() > 0);
        w.core.accept(3);
        w.server.pumpAudio();
        QVERIFY(w.core.audio.isEmpty());
        // Played during the key: sent as the program's mono 48 kHz audio,
        // the left and right averaged as pullVaxTxMic folds them.
        QVector<float> tone;
        for (int frame = 0; frame < 128; ++frame) {
            tone << 0.25f << 0.75f;
        }
        vaxTx->setPullData(QByteArray(reinterpret_cast<const char*>(tone.constData()),
                                      tone.size() * int(sizeof(float))));
        w.server.pumpAudio();
        QCOMPARE(w.core.audio.size(), 128);
        QCOMPARE(w.core.audio.front(), 0.5f);
        QCOMPARE(w.core.audioRates.front(), 48000);
        // After the key, nothing more is sent.
        QCOMPARE(w.ask("T 0\n"), QByteArray("RPRT 0\n"));
        vaxTx->setPullData(QByteArray(reinterpret_cast<const char*>(tone.constData()),
                                      tone.size() * int(sizeof(float))));
        w.server.pumpAudio();
        QCOMPARE(w.core.audio.size(), 128);
    }
};

QTEST_MAIN(TstRemoteRigctld)
#include "tst_remote_rigctld.moc"
