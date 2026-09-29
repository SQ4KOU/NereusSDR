// no-port-check: NereusSDR-original. R-R3-46 / R-R3-49 (remote-window
// parity Task 14): HL2 Options' I2C tool and Pin Control, and the Alex-1
// tab's three transmit high-pass switches, from a remote window through the
// Core (radioHardwareVersion 7), as from a local window.
//
// What the tests hold to (mi0bot-Thetis @c26a8a4, setup.cs):
// - btnI2CRead_MouseDown: a read is answered with the four bytes the radio
//   returned, or given up after 20 one-millisecond polls past the first
//   (RadioModel::kIoBoardI2cAnswerMs);
// - btnI2CWrite_MouseDown and ucOutPinsLedStripHF_MouseDown: a write goes
//   to the register named, a pin click writes register 169 at 0x1d on bus 1
//   with the pin changed and reads 169 back for the strip.
// The on-air rule is the parity plan's for this task: an I2C write and an
// output pin are refused while the radio is on the air (they reach the
// N2ADR filter board in the transmit path); a read is not. The three
// high-pass switches have no on-air rule in a local window, as Thetis sets
// them with no MOX check (console.cs:18719-18803 [v2.10.3.15]); from a
// remote window they follow the TX antennas' rule since the trunk merge of
// remote transmit: the device holding transmit changes them on the air,
// and another device's change waits (the Core's own key is a holder).
//
// Loopback link, no RF and no hardware: nothing here keys a radio. The
// "radio" is the Core's own IoBoardHl2 queue, drained by a real P1CodecHl2
// as the P1 connection drains it, and answered (or not) by the test the way
// the HL2's EP6 I2C response would be. "On the air" keys the Core's own
// MoxController against a test TxChannel with the receive-only MOX
// pre-check lifted. No audio device is opened.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-49 (parity Task 14).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-29  J.J. Boyd / KG4VCF  The input pin strip, local and
//                                    remote (UpdateIOLedStrip). AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  Trunk merge of remote transmit: the
//                                    three switches from a window wait
//                                    while the Core's own key is on the
//                                    air, as the TX antennas do.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QCoreApplication>
#include <QFile>
#include <QLoggingCategory>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <array>
#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/HardwareProfile.h"
#include "core/IoBoardHl2.h"
#include "core/IoBoardHl2Facade.h"
#include "core/MoxController.h"
#include "core/P1RadioConnection.h"
#include "core/StepAttenuatorController.h"
#include "core/TxChannel.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/codec/P1CodecHl2.h"
#include "core/session/IStationLink.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "gui/setup/HardwarePage.h"
#include "gui/setup/hardware/Hl2OptionsTab.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"
#include "OperatorWording.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

const QString kOnAir = QStringLiteral("The radio is on the air. Try again when it stops.");
const QString kNoAnswer = QStringLiteral("The radio did not answer the I2C request.");
const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:14");
// The four byte boxes left to right, as mi0bot lays them out
// (setup.designer.cs txtI2CByte3..txtI2CByte0 [@c26a8a4]): C1 at register+3
// on the left, C4 at the register itself on the right.
const QStringList kByteToolTips = {QStringLiteral("Data at Reg/Ctrl + 3"),
                                   QStringLiteral("Data at Reg/Ctrl + 2"),
                                   QStringLiteral("Data at Reg/Ctrl + 1"),
                                   QStringLiteral("Data at Reg/Ctrl")};
constexpr quint8 kOutputRegister = 169;

QString hw(const QString& rest)
{
    return QStringLiteral("hardware/%1/%2").arg(kMac, rest);
}

std::unique_ptr<RadioModel> makeHl2Model()
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    model->setHpsdrModelForTest(HPSDRModel::HERMESLITE);
    RadioInfo info;
    info.macAddress = kMac;
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    return model;
}

// One composed I2C frame, as the HL2's P1 connection composes it.
struct Frame {
    bool composed = false;
    std::array<quint8, 5> bytes{};
};

Frame compose(IoBoardHl2& board)
{
    P1CodecHl2 codec;
    codec.setIoBoard(&board);
    Frame frame;
    quint8 out[5] = {};
    frame.composed = codec.tryComposeI2cFrame(out, /*mox=*/false);
    for (int i = 0; i < 5; ++i) {
        frame.bytes[static_cast<std::size_t>(i)] = out[i];
    }
    return frame;
}

// The HL2's EP6 answer to the oldest read: C0 has bit 7 set (the I2C bus
// 1 controller, 0x3d << 1, as the request named it).
void answer(IoBoardHl2& board, quint8 c1, quint8 c2, quint8 c3, quint8 c4)
{
    board.applyI2cReadResponse(quint8(0x80 | (0x3d << 1)), c1, c2, c3, c4);
}

// The poll's read of the input pins (register 6 at 0x1d), sent and
// answered: C4 lands in register 6 (the pins), C2 in register 8 (the fault,
// left 0 here).
void readInputPins(IoBoardHl2& board, quint8 pins)
{
    IoBoardHl2::I2cTxn txn;
    txn.bus = IoBoardHl2::kI2cBusIndex;
    txn.address = IoBoardHl2::kI2cAddrGeneral;
    txn.control = static_cast<quint8>(IoBoardHl2::Register::REG_INPUT_PINS);
    txn.isRead = true;
    txn.needsResponse = true;
    board.enqueueI2c(txn);
    QVERIFY(compose(board).composed);
    answer(board, 0x01, 0x00, 0x00, pins);
}

// A local HL2 (the Core's own radio in the Session below, or a local
// window's) whose I2C bus is reachable: a P1 connection, not started.
struct LocalHl2 {
    LocalHl2() : txChannel(/*channelId=*/1)
    {
        model = makeHl2Model();
        // The Core's step attenuator, so its hardware objects are offered
        // (StationServer::radioHardwareVersion).
        stepAtt = std::make_unique<StepAttenuatorController>();
        model->setStepAttController(stepAtt.get());
        model->wireTransmitChainForTest(&txChannel);
        p1.setBoardForTest(HPSDRHW::HermesLite);
        model->injectConnectionForTest(&p1);
    }
    ~LocalHl2()
    {
        model->injectConnectionForTest(nullptr);
        model->injectTxChannelForTest(nullptr);
        model->setStepAttController(nullptr);
    }
    void key()
    {
        MoxController* const mox = model->moxController();
        mox->setMoxCheck({});
        mox->setMox(true);
    }
    void unkey() { model->moxController()->setMox(false); }

    TxChannel txChannel;
    P1RadioConnection p1;
    std::unique_ptr<StepAttenuatorController> stepAtt;
    std::unique_ptr<RadioModel> model;
};

// A receive-only Core with an HL2 and one window, handshake complete. The
// Core's store is the process-wide AppSettings, so its hardware apply step
// reads what the server stored.
struct Session {
    explicit Session(const QString& securityDir, QObject* parent)
        : settings(AppSettings::instance())
    {
        settings.setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("7"));
        server = std::make_unique<StationServer>(
            core.model.get(), settings, NereusSDR::Test::seedUpgradedCoreToken(securityDir));
        client = std::make_unique<StationClient>(&window, &proxy);
        coreEnd = new LoopbackTransport(QStringLiteral("station-end"), parent);
        windowEnd = new LoopbackTransport(QStringLiteral("client-end"), parent);
        coreEnd->linkTo(windowEnd);
    }
    ~Session()
    {
        AppSettings::instance().setRemoteBackend(nullptr);
        client.reset();
        server.reset();
    }
    bool connect()
    {
        QSignalSpy completed(client.get(), &StationClient::handshakeComplete);
        client->startSession(windowEnd, server->token());
        server->acceptTransport(coreEnd);
        return completed.wait(5000) || completed.count() == 1;
    }
    IoBoardHl2& board() { return core.model->ioBoardMutable(); }

    LocalHl2 core;
    AppSettings& settings;
    std::unique_ptr<StationServer> server;
    RadioModel window{RadioModel::Role::Remote};
    SettingsProxy proxy;
    std::unique_ptr<StationClient> client;
    LoopbackTransport* coreEnd = nullptr;
    LoopbackTransport* windowEnd = nullptr;
};

// MOX, TUNE, the two-tone test and VOX all stay off on `model`.
bool nothingKeyed(RadioModel& model, QString* what)
{
    const TransmitModel& tx = model.transmitModel();
    const auto fail = [what](const char* name) {
        if (what) { *what = QString::fromLatin1(name); }
        return false;
    };
    if (model.mox() || tx.isMox()) { return fail("MOX"); }
    if (model.tune() || model.isTune() || tx.isTune()) { return fail("TUNE"); }
    if (tx.isTwoToneActive()) { return fail("two-tone"); }
    if (tx.voxEnabled()) { return fail("VOX"); }
    return true;
}

struct Outcome {
    bool called = false;
    bool ok = false;
    qint64 value = -1;
    QString reason;
    RadioModel::IoBoardI2cDone done()
    {
        return [this](bool okIn, qint64 valueIn, const QString& reasonIn) {
            called = true;
            ok = okIn;
            value = valueIn;
            reason = reasonIn;
        };
    }
};

Hl2OptionsTab* optionsTab(HardwarePage& page)
{
    return qobject_cast<Hl2OptionsTab*>(page.tabWidgetForTest(HardwarePage::Tab::Hl2Options));
}

}  // namespace

class TstRemoteHl2Io : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void cleanup();

    void remoteReadShowsTheRadiosBytes();
    void remoteProbeAdvancesOnTheAnswer();
    void remoteReadTheRadioDoesNotAnswerIsRefused();
    void remoteWriteAndPinReachTheBoardOffTheAir();
    void remoteWriteAndPinAreRefusedOnTheAirReadsAreNot();
    void remoteWindowClosesWriteAndPinControlOnTheAir();
    void localWindowDoesTheSame();
    void olderCoreKeepsTheToolAndSwitchesClosedWithItsReason();
    void alexHpfSwitchesFromARemoteWindowOnAndOffTheAir();
    void localAlexHpfSwitchesStayLiveOnTheAir();
    void reasonsArePlain();
    void inputStripFollowsThePinsAndTransmitLocalAndRemote();
    void toolReadsPauseThePollUntilAnswered();

private:
    QTemporaryDir m_securityDir;
};

void TstRemoteHl2Io::initTestCase()
{
    QVERIFY(m_securityDir.isValid());
    QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false\ndefault.debug=false"));
    AppSettings::setProfileOverride(QStringLiteral("remote-hl2-io-%1")
                                        .arg(QCoreApplication::applicationPid()));
    AppSettings::instance().clear();
}

void TstRemoteHl2Io::cleanupTestCase()
{
    AppSettings::instance().setRemoteBackend(nullptr);
    const QString path = AppSettings::instance().filePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

void TstRemoteHl2Io::cleanup()
{
    AppSettings::instance().setRemoteBackend(nullptr);
    AppSettings::instance().clear();
}

// B4.4: the tool's Read reads through the Core's radio and shows the value.
void TstRemoteHl2Io::remoteReadShowsTheRadiosBytes()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    QCOMPARE(s.client->capabilities().radioHardwareVersion, 9);
    QVERIFY(s.client->radioHardwareAvailable(7));
    s.window.alexAntennaFacade()->setWindowAvailability(true, {});
    HardwarePage page(&s.window);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);
    QVERIFY(!tab->isI2cReadEnabledForTest());   // I2C Enable starts off

    // The I/O board's hardware version register (0x41, register 0).
    tab->readI2cForTest(0x41, 0x00);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    const Frame frame = compose(s.board());
    QVERIFY(frame.composed);
    QCOMPARE(frame.bytes[1], quint8(0x07));          // read
    QCOMPARE(frame.bytes[2], quint8(0x80 | 0x41));   // stop | address
    QCOMPARE(frame.bytes[3], quint8(0x00));          // register
    answer(s.board(), 0x01, 0x02, 0x03, IoBoardHl2::kHardwareVersion1);
    QTRY_COMPARE(tab->i2cResponseTextForTest(), QStringLiteral("01 02 03 F1"));
    QCOMPARE(tab->i2cByteToolTipsForTest(), kByteToolTips);
    QVERIFY(tab->i2cStatusTextForTest().isEmpty());
    // The board answered as an I/O board: the Core and the window say so.
    QVERIFY(s.board().isDetected());
    QTRY_VERIFY(s.window.ioBoardFacade()->detected());

    // Straight through the model, the value carries all four bytes.
    Outcome outcome;
    RadioModel::IoBoardI2cRequest request;
    request.address = IoBoardHl2::kI2cAddrGeneral;
    request.reg = static_cast<int>(IoBoardHl2::Register::REG_FIRMWARE_MAJOR);
    s.window.requestIoBoardI2c(request, outcome.done());
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    QVERIFY(compose(s.board()).composed);
    answer(s.board(), 0xAA, 0xBB, 0xCC, 0x07);
    QTRY_VERIFY(outcome.called);
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.value, qint64(0xAABBCC07));
    QString keyed;
    QVERIFY2(nothingKeyed(*s.core.model, &keyed), qPrintable(keyed));
}

// Follow-up (R-R3-46): a remote window's Probe runs the Core's probe to the
// end although the firmware's answer names the I2C controller in C0, not
// the device. mi0bot networkproto1.c:478-493 [@c26a8a4] takes any C0 with
// bit 7 set as the answer to the one read outstanding.
void TstRemoteHl2Io::remoteProbeAdvancesOnTheAnswer()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    s.core.p1.setIoBoard(&s.board());
    QVERIFY(s.window.requestIoBoardProbe().sent);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    Frame frame = compose(s.board());
    QCOMPARE(frame.bytes[2], quint8(0x80 | IoBoardHl2::kI2cAddrHwVersion));
    answer(s.board(), 0, 0, 0, IoBoardHl2::kHardwareVersion1);
    QVERIFY(s.board().isDetected());
    // Firmware major, then minor, then the board is switched on.
    QCOMPARE(s.board().i2cQueueDepth(), 1);
    frame = compose(s.board());
    QCOMPARE(frame.bytes[3],
             quint8(static_cast<int>(IoBoardHl2::Register::REG_FIRMWARE_MAJOR)));
    answer(s.board(), 0, 0, 0, 0x01);
    QCOMPARE(s.board().i2cQueueDepth(), 1);
    frame = compose(s.board());
    QCOMPARE(frame.bytes[3],
             quint8(static_cast<int>(IoBoardHl2::Register::REG_FIRMWARE_MINOR)));
    answer(s.board(), 0, 0, 0, 0x02);
    QCOMPARE(s.board().i2cQueueDepth(), 1);
    frame = compose(s.board());
    QCOMPARE(frame.bytes[1], quint8(0x06));   // write
    QCOMPARE(frame.bytes[3], quint8(static_cast<int>(IoBoardHl2::Register::REG_CONTROL)));
    QCOMPARE(frame.bytes[4], quint8(1));
    QTRY_VERIFY(s.window.ioBoardFacade()->detected());
    s.core.p1.setIoBoard(nullptr);
    QString keyed;
    QVERIFY2(nothingKeyed(*s.core.model, &keyed), qPrintable(keyed));
}

// A read the radio does not answer in time is refused with the reason, in
// the window's own words.
void TstRemoteHl2Io::remoteReadTheRadioDoesNotAnswerIsRefused()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    s.window.alexAntennaFacade()->setWindowAvailability(true, {});
    HardwarePage page(&s.window);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);

    tab->readI2cForTest(0x1D, 0x09);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    QVERIFY(compose(s.board()).composed);
    // No answer: the Core gives up after the tool's own budget.
    QTRY_COMPARE(tab->i2cStatusTextForTest(), kNoAnswer);
    QCOMPARE(tab->i2cResponseTextForTest(), QStringLiteral("-- -- -- --"));

    // A read the Core never sent (no radio frame yet) is not given up on
    // while the radio stays connected; when it goes, it is.
    Outcome waiting;
    RadioModel::IoBoardI2cRequest request;
    request.address = 0x1D;
    request.reg = 0x0A;
    s.window.requestIoBoardI2c(request, waiting.done());
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    QTest::qWait(3 * RadioModel::kIoBoardI2cAnswerMs);
    QVERIFY(!waiting.called);
    s.core.model->injectConnectionForTest(nullptr);
    QTRY_VERIFY(waiting.called);
    QVERIFY(!waiting.ok);
    QCOMPARE(waiting.reason, kNoAnswer);
    s.core.model->injectConnectionForTest(&s.core.p1);
}

// B4.4: Write writes through the Core's radio; Pin Control switches the
// Core's outputs, and the strip follows `outputs`.
void TstRemoteHl2Io::remoteWriteAndPinReachTheBoardOffTheAir()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    s.window.alexAntennaFacade()->setWindowAvailability(true, {});
    HardwarePage page(&s.window);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);

    tab->writeI2cForTest(0x1D, 0x20, 0x5A);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    Frame frame = compose(s.board());
    QVERIFY(frame.composed);
    QCOMPARE(frame.bytes[1], quint8(0x06));          // write
    QCOMPARE(frame.bytes[2], quint8(0x80 | 0x1D));
    QCOMPARE(frame.bytes[3], quint8(0x20));
    QCOMPARE(frame.bytes[4], quint8(0x5A));
    QVERIFY(tab->i2cStatusTextForTest().isEmpty());

    // Pin Control needs the board: until it is found, the Core says so,
    // once, on the tab (not also through the window's general refusal
    // notice).
    QSignalSpy generalNotice(&s.window, &RadioModel::sliceAddRejected);
    tab->clickOutputPinForTest(3);
    QTRY_COMPARE(tab->i2cStatusTextForTest(),
                 QStringLiteral("The radio's I/O board was not found."));
    QCOMPARE(s.board().i2cQueueDepth(), 0);
    QTest::qWait(50);
    QCOMPARE(generalNotice.count(), 0);

    s.board().setDetected(true);
    QTRY_VERIFY(s.window.ioBoardFacade()->detected());
    tab->clickOutputPinForTest(3);
    // Register 169 at 0x1d written with pin 3 on, then read back.
    QTRY_COMPARE(s.board().i2cQueueDepth(), 2);
    frame = compose(s.board());
    QCOMPARE(frame.bytes[1], quint8(0x06));
    QCOMPARE(frame.bytes[2], quint8(0x80 | IoBoardHl2::kI2cAddrGeneral));
    QCOMPARE(frame.bytes[3], kOutputRegister);
    QCOMPARE(frame.bytes[4], quint8(0x08));
    frame = compose(s.board());
    QCOMPARE(frame.bytes[1], quint8(0x07));
    QCOMPARE(frame.bytes[3], kOutputRegister);
    QCOMPARE(tab->outputBitsForTest(), quint8(0));   // only the read-back moves it
    answer(s.board(), 0x00, 0x00, 0x00, 0x08);
    QCOMPARE(s.core.model->ioBoardFacade()->outputs(), 0x08);
    QTRY_COMPARE(s.window.ioBoardFacade()->outputs(), 0x08);
    QTRY_COMPARE(tab->outputBitsForTest(), quint8(0x08));

    // The same pin again turns it off.
    tab->clickOutputPinForTest(3);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 2);
    QCOMPARE(compose(s.board()).bytes[4], quint8(0x00));
    QVERIFY(compose(s.board()).composed);
    answer(s.board(), 0x00, 0x00, 0x00, 0x00);
    QTRY_COMPARE(tab->outputBitsForTest(), quint8(0));

    // A write to 169 reads the register back too (btnI2CWrite_MouseDown).
    tab->writeI2cForTest(0x1D, kOutputRegister, 0x81);
    QTRY_COMPARE(s.board().i2cQueueDepth(), 2);
    QCOMPARE(compose(s.board()).bytes[4], quint8(0x81));
    QCOMPARE(compose(s.board()).bytes[1], quint8(0x07));
    answer(s.board(), 0x00, 0x00, 0x00, 0x81);
    QTRY_COMPARE(tab->outputBitsForTest(), quint8(0x81));
    QString keyed;
    QVERIFY2(nothingKeyed(*s.core.model, &keyed), qPrintable(keyed));
}

// The on-air rule on the Core: a write and a pin are refused with the
// reason and nothing reaches the board; a read goes through.
void TstRemoteHl2Io::remoteWriteAndPinAreRefusedOnTheAirReadsAreNot()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    s.board().setDetected(true);
    s.core.key();
    QVERIFY(s.core.model->stationOnAirRefusal(nullptr));
    // Each refusal reaches the one that asked, and nowhere else.
    QSignalSpy generalNotice(&s.window, &RadioModel::sliceAddRejected);

    Outcome write;
    RadioModel::IoBoardI2cRequest request;
    request.address = 0x1D;
    request.reg = kOutputRegister;
    request.write = true;
    request.value = 0x01;
    s.window.requestIoBoardI2c(request, write.done());
    QTRY_VERIFY(write.called);
    QVERIFY(!write.ok);
    QCOMPARE(write.reason, kOnAir);

    Outcome pin;
    s.window.setIoBoardOutput(0, true, pin.done());
    QTRY_VERIFY(pin.called);
    QVERIFY(!pin.ok);
    QCOMPARE(pin.reason, kOnAir);
    QCOMPARE(s.board().i2cQueueDepth(), 0);
    QCOMPARE(generalNotice.count(), 0);

    Outcome read;
    request.write = false;
    s.window.requestIoBoardI2c(request, read.done());
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    QVERIFY(compose(s.board()).composed);
    answer(s.board(), 0, 0, 0, 0x40);
    QTRY_VERIFY(read.called);
    QVERIFY(read.ok);
    QCOMPARE(read.value & 0xFF, qint64(0x40));

    s.core.unkey();
    QTRY_COMPARE(s.core.model->moxController()->state(), MoxState::Rx);
    Outcome after;
    s.window.setIoBoardOutput(0, true, after.done());
    QTRY_VERIFY(after.called);
    QVERIFY(after.ok);
}

// In the window, the Write button and Pin Control close with the reason
// while the Core reports it is on the air; Read stays open.
void TstRemoteHl2Io::remoteWindowClosesWriteAndPinControlOnTheAir()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    s.window.alexAntennaFacade()->setWindowAvailability(true, {});
    HardwarePage page(&s.window);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);
    tab->writeI2cForTest(0x1D, 0x20, 0x00);   // opens both gates
    QTRY_COMPARE(s.board().i2cQueueDepth(), 1);
    QVERIFY(compose(s.board()).composed);
    QVERIFY(tab->isI2cWriteEnabledForTest());
    QVERIFY(tab->isPinControlEnabledForTest());

    s.core.key();
    QTRY_VERIFY(s.window.isCoreOnAir());
    QVERIFY(!tab->isI2cWriteEnabledForTest());
    QCOMPARE(tab->i2cWriteToolTipForTest(), kOnAir);
    QVERIFY(!tab->isPinControlEnabledForTest());
    QCOMPARE(tab->pinControlToolTipForTest(), kOnAir);
    QVERIFY(tab->isI2cReadEnabledForTest());

    s.core.unkey();
    QTRY_VERIFY(!s.window.isCoreOnAir());
    QVERIFY(tab->isI2cWriteEnabledForTest());
    QVERIFY(tab->isPinControlEnabledForTest());
}

// Parity the other way: a local window's tool does what the Core does.
void TstRemoteHl2Io::localWindowDoesTheSame()
{
    LocalHl2 local;
    HardwarePage page(local.model.get());
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);
    IoBoardHl2& board = local.model->ioBoardMutable();

    tab->readI2cForTest(0x1D, 0x0A);
    QCOMPARE(board.i2cQueueDepth(), 1);
    QVERIFY(compose(board).composed);
    answer(board, 0x10, 0x20, 0x30, 0x40);
    QCOMPARE(tab->i2cResponseTextForTest(), QStringLiteral("10 20 30 40"));
    QCOMPARE(tab->i2cByteToolTipsForTest(), kByteToolTips);

    tab->readI2cForTest(0x1D, 0x0B);
    QVERIFY(compose(board).composed);
    QTRY_COMPARE(tab->i2cStatusTextForTest(), kNoAnswer);
    // The radio's late answer lands in the register mirror only; the tool
    // already gave up on it.
    answer(board, 0x99, 0x99, 0x99, 0x99);
    QCOMPARE(tab->i2cResponseTextForTest(), QStringLiteral("10 20 30 40"));

    board.setDetected(true);
    tab->clickOutputPinForTest(5);
    QCOMPARE(board.i2cQueueDepth(), 2);
    QCOMPARE(compose(board).bytes[4], quint8(0x20));
    QVERIFY(compose(board).composed);
    answer(board, 0, 0, 0, 0x20);
    QCOMPARE(tab->outputBitsForTest(), quint8(0x20));

    local.key();
    QTRY_VERIFY(local.model->isCoreOnAir());
    QVERIFY(!tab->isI2cWriteEnabledForTest());
    QCOMPARE(tab->i2cWriteToolTipForTest(), kOnAir);
    QVERIFY(!tab->isPinControlEnabledForTest());
    QVERIFY(tab->isI2cReadEnabledForTest());
    Outcome pin;
    QSignalSpy generalNotice(local.model.get(), &RadioModel::sliceAddRejected);
    local.model->setIoBoardOutput(5, false, pin.done());
    QVERIFY(pin.called && !pin.ok);
    QCOMPARE(pin.reason, kOnAir);
    QCOMPARE(generalNotice.count(), 0);
    QCOMPARE(board.i2cQueueDepth(), 0);
    local.unkey();
    QTRY_VERIFY(!local.model->isCoreOnAir());
    QVERIFY(tab->isPinControlEnabledForTest());
}

// A remote window whose Core does not offer version 7 keeps the tool and
// the three switches disabled with its reason, never hidden.
void TstRemoteHl2Io::olderCoreKeepsTheToolAndSwitchesClosedWithItsReason()
{
    RadioModel remote(RadioModel::Role::Remote);
    remote.alexAntennaFacade()->setWindowAvailability(true, {});
    HardwarePage page(&remote);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);
    tab->writeI2cForTest(0x1D, 0x20, 0x00);
    QVERIFY(!tab->isI2cReadEnabledForTest());
    QVERIFY(!tab->isI2cWriteEnabledForTest());
    QVERIFY(!tab->isPinControlEnabledForTest());
    QCOMPARE(tab->i2cWriteToolTipForTest(), IStationLink::ioBoardI2cUnavailableReason());
    QCOMPARE(tab->pinControlToolTipForTest(), IStationLink::ioBoardI2cUnavailableReason());
    for (const QString& name : {QStringLiteral("alexHpfBypassOnTx"),
                                QStringLiteral("alexHpfBypassOnPs"),
                                QStringLiteral("alexDisable6mLnaOnTx")}) {
        auto* w = page.findChild<QWidget*>(name);
        QVERIFY2(w != nullptr, qPrintable(name));
        QVERIFY2(!w->isHidden() && !w->isEnabled(), qPrintable(name));
        QCOMPARE(w->toolTip(), IStationLink::alexHpfSwitchesUnavailableReason());
    }
}

// The carried item: HPF bypass on TX, HPF bypass on PureSignal and
// Disable 6 m LNA on TX from a remote window reach the Core's connection.
// Trunk merge of remote transmit (join d): on the air they follow the TX
// antennas' rule. While the Core's own key is on the air this window is
// another device, so its change waits and goes ahead after the key ends.
void TstRemoteHl2Io::alexHpfSwitchesFromARemoteWindowOnAndOffTheAir()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    QVERIFY(s.proxy.ready());
    QVERIFY(s.core.model->receiveOnlyStationPolicy());
    QStringList reloads;
    s.core.model->setHardwareApplyObserverForTest(
        [&reloads](const QString& name) { reloads << name; });
    QSignalSpy rejected(&s.proxy, &SettingsProxy::valueRejected);

    // Thetis's defaults on the Core's connection before any write.
    s.core.model->applyAlexHpfSwitchSettings();
    QVERIFY(!s.core.p1.hpfBypassOnTx());

    s.proxy.setValue(hw(QStringLiteral("alex/master/hpfBypassOnTx")), QStringLiteral("True"));
    QTRY_VERIFY(s.core.p1.hpfBypassOnTx());
    QVERIFY(reloads.contains(QStringLiteral("alex")));

    // On the air, keyed by the Core's own key: this window's change waits.
    s.core.key();
    QVERIFY(s.core.model->stationOnAirRefusal(nullptr));
    s.proxy.setValue(hw(QStringLiteral("alex/master/hpfBypassOnTx")), QStringLiteral("False"));
    QTRY_COMPARE(rejected.count(), 1);
    QVERIFY(s.core.p1.hpfBypassOnTx());
    QCOMPARE(s.settings.value(hw(QStringLiteral("alex/master/hpfBypassOnTx"))).toString(),
             QStringLiteral("True"));

    // The window's switches stay open on the air, with no reason: the
    // Core's refusal is the one answer, as for the TX antennas.
    s.window.alexAntennaFacade()->setWindowAvailability(true, {});
    {
        HardwarePage page(&s.window);
        QTRY_VERIFY(s.window.isCoreOnAir());
        for (const QString& name : {QStringLiteral("alexHpfBypassOnTx"),
                                    QStringLiteral("alexHpfBypassOnPs"),
                                    QStringLiteral("alexDisable6mLnaOnTx")}) {
            auto* w = page.findChild<QWidget*>(name);
            QVERIFY2(w != nullptr && w->isEnabled(), qPrintable(name));
            QVERIFY2(w->toolTip() != kOnAir, qPrintable(name));
        }
    }
    s.core.unkey();
    QTRY_COMPARE(s.core.model->moxController()->state(), MoxState::Rx);

    // Off the air again: taken and applied at once.
    s.proxy.setValue(hw(QStringLiteral("alex/master/disable6mLnaOnTx")), QStringLiteral("False"));
    s.proxy.setValue(hw(QStringLiteral("alex/master/hpfBypassOnPs")), QStringLiteral("False"));
    s.proxy.setValue(hw(QStringLiteral("alex/master/hpfBypassOnTx")), QStringLiteral("False"));
    QTRY_VERIFY(!s.core.p1.hpfBypassOnTx());
    QTRY_COMPARE(s.settings.value(hw(QStringLiteral("alex/master/disable6mLnaOnTx"))).toString(),
                 QStringLiteral("False"));
    QCOMPARE(s.settings.value(hw(QStringLiteral("alex/master/hpfBypassOnPs"))).toString(),
             QStringLiteral("False"));
    QCOMPARE(rejected.count(), 1);
    // The LPF band edges stay refused.
    s.proxy.setValue(hw(QStringLiteral("alex/lpf/20m/start")), QStringLiteral("10.0"));
    QTRY_COMPARE(rejected.count(), 2);
}

void TstRemoteHl2Io::localAlexHpfSwitchesStayLiveOnTheAir()
{
    LocalHl2 local;
    HardwarePage page(local.model.get());
    local.key();
    QTRY_VERIFY(local.model->isCoreOnAir());
    for (const QString& name : {QStringLiteral("alexHpfBypassOnTx"),
                                QStringLiteral("alexHpfBypassOnPs"),
                                QStringLiteral("alexDisable6mLnaOnTx")}) {
        auto* w = page.findChild<QWidget*>(name);
        QVERIFY2(w != nullptr && w->isEnabled(), qPrintable(name));
    }
    local.unkey();
}

void TstRemoteHl2Io::reasonsArePlain()
{
    for (const QString& text : {RadioModel::ioBoardNoAnswerReason(),
                                IStationLink::ioBoardI2cUnavailableReason(),
                                IStationLink::alexHpfSwitchesUnavailableReason()}) {
        QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
        QVERIFY2(OperatorWording::coreCalledStationIn(text).isEmpty(), qPrintable(text));
        QVERIFY2(!text.contains(QChar(0x2014)), qPrintable(text));
    }
}

// mi0bot console.cs:25887 [@c26a8a4]: after each read of the input pins,
// SetupForm.UpdateIOLedStrip(MOX, readRegister(REG_INPUT_PINS)); the strip
// draws its lit pins orange-red while MOX (setup.cs:22606-22610,
// ucOCLedStrip.cs:101-111). A remote window shows the Core's pins and the
// Core's transmit state, as a local window shows its own.
void TstRemoteHl2Io::inputStripFollowsThePinsAndTransmitLocalAndRemote()
{
    {
        LocalHl2 local;
        HardwarePage page(local.model.get());
        Hl2OptionsTab* tab = optionsTab(page);
        QVERIFY(tab != nullptr);
        IoBoardHl2& board = local.model->ioBoardMutable();
        readInputPins(board, 0x2A);
        QCOMPARE(tab->inputBitsForTest(), quint8(0x2A));
        QVERIFY(!tab->inputStripTxForTest());
        local.key();
        QTRY_VERIFY(tab->inputStripTxForTest());
        readInputPins(board, 0x05);
        QCOMPARE(tab->inputBitsForTest(), quint8(0x05));
        local.unkey();
        QTRY_VERIFY(!tab->inputStripTxForTest());
    }

    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    HardwarePage page(&s.window);
    Hl2OptionsTab* tab = optionsTab(page);
    QVERIFY(tab != nullptr);
    QCOMPARE(tab->inputBitsForTest(), quint8(0));
    readInputPins(s.board(), 0x2A);
    QTRY_COMPARE(tab->inputBitsForTest(), quint8(0x2A));
    QVERIFY(!tab->inputStripTxForTest());
    s.core.key();
    QTRY_VERIFY(tab->inputStripTxForTest());
    readInputPins(s.board(), 0x11);
    QTRY_COMPARE(tab->inputBitsForTest(), quint8(0x11));
    s.core.unkey();
    QTRY_VERIFY(!tab->inputStripTxForTest());
}


// mi0bot setup.cs:21457/21501 (btnI2CRead), 21516/21529 (btnI2CWrite) and
// 30014/30035, 30048/30052 (the output strip) [@c26a8a4] bracket each
// transaction with console.SetI2CPollingPause(true) ... (false), and the
// poll waits while it is held (console.cs:25930-25935). Here the pause is
// held while a read the tool waits on is outstanding: answered or given up,
// it is released. A remote window's read pauses the Core's poll.
void TstRemoteHl2Io::toolReadsPauseThePollUntilAnswered()
{
    {
        LocalHl2 local;
        IoBoardHl2& board = local.model->ioBoardMutable();
        board.setDetected(true);
        local.p1.setIoBoard(&board);
        QVERIFY(!board.isPollingPaused());

        Outcome read;
        RadioModel::IoBoardI2cRequest request;
        request.address = 0x1D;
        request.reg = 0x0A;
        local.model->requestIoBoardI2c(request, read.done());
        QVERIFY(board.isPollingPaused());
        // The poll waits on its step.
        const int step = board.currentStep();
        local.p1.ioBoardPollTickForTest();
        QCOMPARE(board.currentStep(), step);
        QCOMPARE(board.i2cQueueDepth(), 1);
        QVERIFY(compose(board).composed);
        QVERIFY(board.isPollingPaused());
        answer(board, 0x00, 0x00, 0x00, 0x40);
        QVERIFY(read.called && read.ok);
        QVERIFY(!board.isPollingPaused());
        local.p1.ioBoardPollTickForTest();
        QCOMPARE(board.currentStep(), (step + 1) % IoBoardHl2::kStateMachineSteps);
        // Send what the poll queued, answering its reads so the next
        // answer below belongs to the tool.
        while (!board.i2cQueueIsEmpty()) {
            if (compose(board).bytes[1] == 0x07) {
                answer(board, 0x00, 0x00, 0x00, 0x00);
            }
        }

        // An output pin pauses it until the register is read back.
        Outcome pin;
        local.model->setIoBoardOutput(2, true, pin.done());
        QVERIFY(pin.called && pin.ok);
        QVERIFY(board.isPollingPaused());
        QCOMPARE(compose(board).bytes[1], quint8(0x06));   // the write
        QVERIFY(board.isPollingPaused());
        QCOMPARE(compose(board).bytes[1], quint8(0x07));   // the readback
        answer(board, 0x00, 0x00, 0x00, 0x04);
        QVERIFY(!board.isPollingPaused());
        QCOMPARE(local.model->ioBoardFacade()->outputs(), 0x04);

        // A read the radio does not answer releases it when the tool gives
        // up.
        Outcome lost;
        request.reg = 0x0B;
        local.model->requestIoBoardI2c(request, lost.done());
        QVERIFY(board.isPollingPaused());
        QVERIFY(compose(board).composed);
        QTRY_VERIFY(lost.called);
        QVERIFY(!lost.ok);
        QVERIFY(!board.isPollingPaused());

        // A write alone does not hold it: the queue keeps its order.
        Outcome write;
        request.write = true;
        request.reg = 0x20;
        request.value = 0x01;
        local.model->requestIoBoardI2c(request, write.done());
        QVERIFY(write.called && write.ok);
        QVERIFY(!board.isPollingPaused());
        QVERIFY(compose(board).composed);

        // The read given up does not take the next read's answer.
        Outcome next;
        request.write = false;
        request.reg = 0x0C;
        request.value = 0;
        local.model->requestIoBoardI2c(request, next.done());
        QVERIFY(compose(board).composed);
        answer(board, 0x00, 0x00, 0x00, 0x33);
        QVERIFY(next.called);
        QVERIFY2(next.ok, qPrintable(next.reason));
        QCOMPARE(next.value, qint64(0x33));
        QVERIFY(!board.isPollingPaused());
        local.p1.setIoBoard(nullptr);
    }

    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    QVERIFY(!s.board().isPollingPaused());
    Outcome remote;
    RadioModel::IoBoardI2cRequest request;
    request.address = 0x1D;
    request.reg = 0x0A;
    s.window.requestIoBoardI2c(request, remote.done());
    QTRY_VERIFY(s.board().isPollingPaused());
    QVERIFY(compose(s.board()).composed);
    answer(s.board(), 0x00, 0x00, 0x00, 0x11);
    QTRY_VERIFY(remote.called);
    QVERIFY(remote.ok);
    QVERIFY(!s.board().isPollingPaused());
    QString keyed;
    QVERIFY2(nothingKeyed(*s.core.model, &keyed), qPrintable(keyed));
}

QTEST_MAIN(TstRemoteHl2Io)
#include "tst_remote_hl2_io.moc"
