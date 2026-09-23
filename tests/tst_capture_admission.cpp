// =================================================================
// tests/tst_capture_admission.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  PC-microphone MOX admission tests
// (R-R3-36, R-R3-21): keying that would read the PC microphone is refused
// before any RF effect while capture is not Ready, and never queued; Radio
// mic, VAX, TCI audio, Tune and two-tone key normally; losing capture while
// keyed releases MOX once, the ordinary way; unkey is never refused.  The
// capture helper is always the scripted fake, run by re-executing this
// binary with --fake-capture-child <scenario>; no real microphone is opened
// and no radio is keyed (the connection is a counting mock).
//
// Modification history (NereusSDR):
//   2026-09-22: J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QRegularExpression>
#include <QSignalSpy>

#include <cstring>
#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/MoxController.h"
#include "core/RadioConnection.h"
#include "core/TwoToneController.h"
#include "core/TxChannel.h"
#include "core/audio/CaptureSupervisor.h"
#include "core/safety/BandPlanGuard.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"

#include "fakes/FakeCaptureChild.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <csignal>
#include <sys/types.h>
#endif

using namespace NereusSDR;
using State = CaptureSupervisor::Status::State;

namespace {

const QString kRefusal =
    QStringLiteral("Microphone is not ready. Check Audio settings and retry.");

CaptureSupervisor::Options fakeOptions(const QString& scenario)
{
    CaptureSupervisor::Options options;
    options.program = QCoreApplication::applicationFilePath();
    options.arguments = {QStringLiteral("--fake-capture-child"), scenario};
    return options;
}

void killProcess(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE handle = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid));
    if (handle != nullptr) {
        TerminateProcess(handle, 9);
        CloseHandle(handle);
    }
#else
    ::kill(static_cast<pid_t>(pid), SIGKILL);
#endif
}

// Connection that records every RF-relevant command RadioModel sends.
class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit MockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }
    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override { ++antennaCalls; }
    void setMox(bool on) override { on ? ++moxOnCalls : ++moxOffCalls; }
    void setTrxRelay(bool) override { ++trxRelayCalls; }
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
    void sendTxIq(const float*, int) override {}

    int antennaCalls = 0;
    int moxOnCalls = 0;
    int moxOffCalls = 0;
    int trxRelayCalls = 0;
};

enum class Ptt { Mox, RadioMicPtt, Cat, Vox, Space, X2 };

void pressPtt(MoxController* mox, Ptt source)
{
    switch (source) {
    case Ptt::Mox:         mox->setMox(true); break;
    case Ptt::RadioMicPtt: mox->onMicPttFromRadio(true); break;
    case Ptt::Cat:         mox->onCatPtt(true); break;
    case Ptt::Vox:         mox->onVoxActive(true); break;
    case Ptt::Space:       mox->onSpacePtt(true); break;
    case Ptt::X2:          mox->onX2Ptt(true); break;
    }
}

// A local RadioModel standing in for a connected station: counting mock
// connection, a TxChannel with no WDSP channel behind it, the production
// MOX pre-check, zero MOX timers and one USB slice in the 20 m phone band.
// The capture helper is the fake; the test takes capture demand itself.
struct Rig {
    MockConnection conn;
    TxChannel tx{/*channelId=*/1};
    std::unique_ptr<RadioModel> model;
    CaptureSupervisor::Lease lease;

    explicit Rig(const QString& scenario)
        : model(std::make_unique<RadioModel>())
    {
        model->configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5, 192000);
        model->moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model->audioEngine()->setCaptureSupervisorOptionsForTest(fakeOptions(scenario));
        model->injectConnectionForTest(&conn);
        model->injectTxChannelForTest(&tx);
        model->installBandPlanMoxCheckForTest();
        const int id = model->addSlice();
        SliceModel* const slice = model->sliceById(id);
        Q_ASSERT(slice);
        slice->setDspMode(DSPMode::USB);
        slice->setFrequency(14'200'000.0);
        model->setActiveSliceById(id);
        QCoreApplication::processEvents();
    }

    ~Rig()
    {
        if (model->moxController()->isMox()) {
            model->moxController()->setMox(false);
            QTest::qWait(20);
        }
        lease.release();
        model->injectTxChannelForTest(nullptr);
        model->injectConnectionForTest(nullptr);
        model.reset();
    }

    MoxController* mox() const { return model->moxController(); }
    AudioEngine* engine() const { return model->audioEngine(); }

    void takeDemand()
    {
        lease = engine()->acquireCaptureDemand(CaptureSupervisor::Demand::LocalSession);
    }
};

// Brings the rig's capture to `state` ("closed" takes no demand).
bool reachCaptureState(Rig& rig, const QString& state)
{
    if (state == QLatin1String("closed")) {
        return rig.engine()->captureStatus().state == State::Closed;
    }
    rig.takeDemand();
    const State want = state == QLatin1String("ready")     ? State::Ready
                     : state == QLatin1String("opening")   ? State::Opening
                                                            : State::Failed;
    return QTest::qWaitFor([&] { return rig.engine()->captureStatus().state == want; }, 8000);
}

QString scenarioFor(const QString& state)
{
    if (state == QLatin1String("opening")) {
        return QStringLiteral("hang-open");
    }
    if (state == QLatin1String("failed")) {
        // An unknown scenario makes the fake exit at once with code 2.
        return QStringLiteral("no-such-scenario");
    }
    return QStringLiteral("ready");
}

} // namespace

class TstCaptureAdmission : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        AppSettings::instance().clear();
        AppSettings::instance().setValue(
            QStringLiteral("BandPlanRegion"),
            QString::number(static_cast<int>(safety::Region::UnitedStates)));
    }

    void cleanup() { AppSettings::instance().clear(); }

    // PC mic selected, capture not Ready: every PTT source is refused with
    // the exact text and nothing reaches the radio or the state machine.
    void refusalHasNoRfEffect_data()
    {
        QTest::addColumn<int>("ptt");
        QTest::addColumn<QString>("capture");
        QTest::newRow("mox/closed")          << int(Ptt::Mox)         << "closed";
        QTest::newRow("radio-mic-ptt/closed") << int(Ptt::RadioMicPtt) << "closed";
        QTest::newRow("cat/closed")          << int(Ptt::Cat)         << "closed";
        QTest::newRow("vox/closed")          << int(Ptt::Vox)         << "closed";
        QTest::newRow("space/closed")        << int(Ptt::Space)       << "closed";
        QTest::newRow("x2/closed")           << int(Ptt::X2)          << "closed";
        QTest::newRow("mox/opening")         << int(Ptt::Mox)         << "opening";
        QTest::newRow("mox/failed")          << int(Ptt::Mox)         << "failed";
        QTest::newRow("vox/failed")          << int(Ptt::Vox)         << "failed";
    }
    void refusalHasNoRfEffect()
    {
        QFETCH(int, ptt);
        QFETCH(QString, capture);
        Rig rig(scenarioFor(capture));
        QCOMPARE(rig.model->transmitModel().micSource(), MicSource::Pc);
        QVERIFY(rig.model->pcCaptureRequired());
        QVERIFY(reachCaptureState(rig, capture));

        const int antenna0 = rig.conn.antennaCalls;
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        QSignalSpy changing(rig.mox(), &MoxController::moxChanging);
        QSignalSpy aboutToBegin(rig.mox(), &MoxController::txAboutToBegin);
        QSignalSpy flipped(rig.mox(), &MoxController::hardwareFlipped);
        QSignalSpy stateChanged(rig.mox(), &MoxController::stateChanged);
        QSignalSpy txReady(rig.mox(), &MoxController::txReady);

        pressPtt(rig.mox(), static_cast<Ptt>(ptt));
        QTest::qWait(50);

        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), kRefusal);
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(rig.mox()->state(), MoxState::Rx);
        QCOMPARE(changing.count(), 0);
        QCOMPARE(aboutToBegin.count(), 0);
        QCOMPARE(flipped.count(), 0);
        QCOMPARE(stateChanged.count(), 0);
        QCOMPARE(txReady.count(), 0);
        QCOMPARE(rig.conn.antennaCalls, antenna0);
        QCOMPARE(rig.conn.moxOnCalls, 0);
        QCOMPARE(rig.conn.trxRelayCalls, 0);

        // Unkey is never refused.
        rig.mox()->setMox(false);
        QCOMPARE(rejected.count(), 1);
    }

    // A refused press is not queued: capture reaching Ready afterwards
    // does not key. A new press with Ready keys through the same check.
    void readyLaterDoesNotKeyANewPressDoes_data()
    {
        QTest::addColumn<int>("ptt");
        QTest::newRow("mox") << int(Ptt::Mox);
        QTest::newRow("vox") << int(Ptt::Vox);
    }
    void readyLaterDoesNotKeyANewPressDoes()
    {
        QFETCH(int, ptt);
        Rig rig(QStringLiteral("ready"));
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        QSignalSpy flipped(rig.mox(), &MoxController::hardwareFlipped);

        pressPtt(rig.mox(), static_cast<Ptt>(ptt));
        QCOMPARE(rejected.count(), 1);

        QVERIFY(reachCaptureState(rig, QStringLiteral("ready")));
        QTest::qWait(300);
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(flipped.count(), 0);
        QCOMPARE(rig.conn.moxOnCalls, 0);

        pressPtt(rig.mox(), static_cast<Ptt>(ptt));
        QTest::qWait(50);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(rig.mox()->isMox());
        QCOMPARE(flipped.count(), 1);
        QCOMPARE(rig.conn.moxOnCalls, 1);

        rig.mox()->setMox(false);
        QTest::qWait(50);
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(rejected.count(), 1);
    }

    // Keying that does not read the PC microphone keys normally with
    // capture Closed or Failed, and none of it changes the session's
    // capture requirement (so the session demand does not open and close
    // with Tune, two-tone or TCI audio).
    void exemptKeyingKeysWithCaptureNotReady_data()
    {
        QTest::addColumn<QString>("keying");
        QTest::addColumn<QString>("capture");
        for (const char* keying : {"radio-mic", "vax", "tci-audio", "tune", "two-tone"}) {
            for (const char* capture : {"closed", "failed"}) {
                QTest::newRow(qPrintable(QStringLiteral("%1/%2").arg(QLatin1String(keying),
                                                                    QLatin1String(capture))))
                    << QString::fromLatin1(keying) << QString::fromLatin1(capture);
            }
        }
    }
    void exemptKeyingKeysWithCaptureNotReady()
    {
        QFETCH(QString, keying);
        QFETCH(QString, capture);
        Rig rig(scenarioFor(capture));
        QVERIFY(reachCaptureState(rig, capture));
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        TwoToneController* const twoTone = rig.model->twoToneController();

        if (keying == QLatin1String("radio-mic")) {
            rig.model->transmitModel().setMicSource(MicSource::Radio);
            QVERIFY(!rig.model->pcCaptureRequired());
            rig.mox()->setMox(true);
        } else if (keying == QLatin1String("vax")) {
            rig.model->transmitModel().setMicSource(MicSource::Vax);
            QVERIFY(!rig.model->pcCaptureRequired());
            rig.mox()->setMox(true);
        } else if (keying == QLatin1String("tci-audio")) {
            rig.tx.setTciAudioActive(true);
            rig.mox()->setMox(true);
            QVERIFY(rig.model->pcCaptureRequired());
        } else if (keying == QLatin1String("tune")) {
            rig.model->setTune(true);
            QVERIFY(rig.mox()->isManualMox());
            QVERIFY(rig.model->pcCaptureRequired());
        } else {
            twoTone->setTxChannel(&rig.tx);
            twoTone->setSettleDelaysMs(0, 0);
            twoTone->setActive(true);
            QVERIFY(twoTone->isActive());
            QVERIFY(rig.model->pcCaptureRequired());
        }
        QTest::qWait(50);

        QCOMPARE(rejected.count(), 0);
        QVERIFY(rig.mox()->isMox());
        QCOMPARE(rig.conn.moxOnCalls, 1);

        if (keying == QLatin1String("tune")) {
            rig.model->setTune(false);
        } else if (keying == QLatin1String("two-tone")) {
            twoTone->setActive(false);
        } else {
            rig.mox()->setMox(false);
        }
        QTRY_VERIFY_WITH_TIMEOUT(!rig.mox()->isMox(), 2000);
        QTest::qWait(250);  // let the tune-off and two-tone settle timers run
        rig.tx.setTciAudioActive(false);
        twoTone->setTxChannel(nullptr);
    }

    // Losing capture while PC-mic keyed releases MOX once, the ordinary
    // way; the TX input reads nothing while it is not Ready, and capture
    // coming back does not key again.
    void inputLossWhileKeyedReleasesOnce_data()
    {
        QTest::addColumn<QString>("loss");
        QTest::newRow("helper-killed") << "kill";
        QTest::newRow("demand-released") << "release";
    }
    void inputLossWhileKeyedReleasesOnce()
    {
        QFETCH(QString, loss);
        Rig rig(QStringLiteral("ready"));
        rig.engine()->onMicSourceChanged(true);
        QVERIFY(reachCaptureState(rig, QStringLiteral("ready")));
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        QSignalSpy aboutToEnd(rig.mox(), &MoxController::txAboutToEnd);
        QSignalSpy aboutToBegin(rig.mox(), &MoxController::txAboutToBegin);
        QSignalSpy status(rig.engine(), &AudioEngine::captureStatusChanged);

        rig.mox()->onVoxActive(true);
        QTest::qWait(50);
        QVERIFY(rig.mox()->isMox());
        QCOMPARE(aboutToBegin.count(), 1);

        // The release is logged once (the raw state stays in the log).
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral(
                                 "^PC microphone left Ready while keyed; releasing MOX\\.")));
        if (loss == QLatin1String("kill")) {
            const qint64 pid = rig.engine()->captureHelperProcessIdForTest();
            QVERIFY(pid > 0);
            killProcess(pid);
        } else {
            rig.lease.release();
        }
        QTRY_VERIFY_WITH_TIMEOUT(!rig.mox()->isMox(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(
            rig.engine()->captureStatus().state == State::Failed
                || rig.engine()->captureStatus().state == State::Closed,
            5000);
        QTest::qWait(300);
        QVERIFY(status.count() >= 1);
        QCOMPARE(aboutToEnd.count(), 1);
        QCOMPARE(rig.conn.moxOffCalls, 1);
        QCOMPARE(rejected.count(), 0);
        QCOMPARE(rig.mox()->state(), MoxState::Rx);

        // The TX input reads nothing while capture is not Ready.
        QVERIFY(!rig.engine()->isPcMicOverrideActive());
        float buffer[64] = {};
        QCOMPARE(rig.engine()->pullTxMic(buffer, 64), 0);

        // Capture coming back does not key.
        if (loss == QLatin1String("kill")) {
            rig.engine()->retryCapture();
        } else {
            rig.takeDemand();
        }
        QTRY_COMPARE_WITH_TIMEOUT(rig.engine()->captureStatus().state, State::Ready, 8000);
        QTest::qWait(300);
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(aboutToBegin.count(), 1);
        QCOMPARE(rig.conn.moxOnCalls, 1);
    }

    // Keying that does not read the PC microphone is not released by a
    // capture change.
    void captureLossDoesNotReleaseTune()
    {
        Rig rig(QStringLiteral("ready"));
        QVERIFY(reachCaptureState(rig, QStringLiteral("ready")));
        rig.model->setTune(true);
        QTest::qWait(50);
        QVERIFY(rig.mox()->isMox());
        QSignalSpy aboutToEnd(rig.mox(), &MoxController::txAboutToEnd);

        killProcess(rig.engine()->captureHelperProcessIdForTest());
        QTRY_COMPARE_WITH_TIMEOUT(rig.engine()->captureStatus().state, State::Failed, 5000);
        QTest::qWait(100);
        QVERIFY(rig.mox()->isMox());
        QCOMPARE(aboutToEnd.count(), 0);

        rig.model->setTune(false);
        QTRY_VERIFY_WITH_TIMEOUT(!rig.mox()->isMox(), 2000);
        QTest::qWait(250);
    }

    // PC-mic keying with capture Ready is admitted, and unkey is never
    // refused.
    void readyCaptureKeysAndUnkeys()
    {
        Rig rig(QStringLiteral("ready"));
        QVERIFY(reachCaptureState(rig, QStringLiteral("ready")));
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);

        rig.mox()->setMox(true);
        QTest::qWait(50);
        QVERIFY(rig.mox()->isMox());
        QCOMPARE(rig.conn.moxOnCalls, 1);
        QCOMPARE(rig.conn.trxRelayCalls, 1);

        rig.mox()->setMox(false);
        QTest::qWait(50);
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(rig.conn.moxOffCalls, 1);
        QCOMPARE(rejected.count(), 0);
    }
};

int main(int argc, char* argv[])
{
    if (argc > 2 && std::strcmp(argv[1], "--fake-capture-child") == 0) {
        return NereusSDR::Test::runFakeCaptureChild(QString::fromLocal8Bit(argv[2]));
    }
    QCoreApplication app(argc, argv);
    TstCaptureAdmission test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_capture_admission.moc"
