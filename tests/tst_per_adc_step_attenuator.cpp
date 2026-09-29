// no-port-check: NereusSDR-original test.
// =================================================================
// tests/tst_per_adc_step_attenuator.cpp  (NereusSDR)
// =================================================================
//
// R-R3-46 / R-R3-11: a step attenuator per receive ADC, as Thetis keeps
// RX1's and RX2's and sends each to the ADC that receiver uses
// (console.cs RX1AttenuatorData / RX2AttenuatorData, GetADCInUse), and a
// receive offset per ADC (console.cs RXOffset(rx)). Slice A carries RX1's
// value; the other ADC in use carries its own, following the band of the
// lowest-numbered slice on it; slices on one ADC share its value; linked
// diversity forces one value on both.
//
// The bench bug this pins: with 20 dB on ADC0 (slice A), a slice on EXT1
// (ADC1) read 20 dB too high, and ADC1's attenuator byte was never sent.
//
// Receive side only: nothing here keys a radio or opens a socket to one.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-28: original test for NereusSDR by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/P1RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/RadioConnection.h"
#include "core/StepAttenuatorController.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr double k20mHz = 14.200e6;
constexpr double k40mHz = 7.100e6;
constexpr double k17mHz = 18.100e6;

// CmdHighPriority step attenuator bytes (Thetis network.c CmdHighPriority).
constexpr int kAdc0AttByte = 1443;
constexpr int kAdc1AttByte = 1442;

using Sends = QList<QPair<int, int>>;

// Records every per-ADC attenuator send.
class RecordingConnection final : public RadioConnection {
    Q_OBJECT
public:
    explicit RecordingConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }

    Sends sends;
    QList<bool> preamp;

    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int dB) override { sends.append({0, dB}); }
    void setAttenuatorForAdc(int adc, int dB) override { sends.append({adc, dB}); }
    void setPreamp(bool on) override { preamp.append(on); }
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};

void startFromNothing(const QString& mac)
{
    AppSettings::instance().clearHardwareValues(mac);
    AppSettings::instance().save();
}

// A controller with its settings loaded for `mac` (band memory live).
void loadController(StepAttenuatorController& ctrl, const QString& mac)
{
    startFromNothing(mac);
    ctrl.setTickTimerEnabled(false);
    ctrl.setMinAttenuation(0);
    ctrl.setMaxAttenuation(31);
    ctrl.loadSettings(mac);
}

// Thetis BUFLEN, the CmdHighPriority packet length.
constexpr int kHighPriorityLen = 1444;

QByteArray highPriority(const P2RadioConnection& conn)
{
    quint8 buf[kHighPriorityLen] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return QByteArray(reinterpret_cast<const char*>(buf), kHighPriorityLen);
}

int byteAt(const QByteArray& buf, int index)
{
    return static_cast<quint8>(buf.at(index));
}

// An ANAN-G2 (two ADCs) on Protocol 2 with a step attenuator following
// slice A, and slices A and B bound to streams 0 and 1.
struct G2Station {
    P2RadioConnection conn;
    RadioModel model;
    StepAttenuatorController ctrl;
    SliceModel* sliceA{nullptr};
    SliceModel* sliceB{nullptr};

    explicit G2Station(const QString& mac)
    {
        // The ANAN-G2's own codec on the connection, so the model's DDC
        // assignment runs and puts a slice on the ADC its antenna feeds.
        conn.setBoardForTest(HPSDRHW::Saturn);
        model.injectConnectionForTest(&conn);
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        loadController(ctrl, mac);
        model.setStepAttController(&ctrl);
        sliceA = model.sliceById(model.addSlice());
        sliceB = model.sliceById(model.addSlice());
        if (sliceA) { sliceA->setFrequency(k20mHz); }
        if (sliceB) { sliceB->setFrequency(k40mHz); }
        model.followReceiveSliceWithStepAttenuator();
        ctrl.setRadioConnection(&conn);
    }

    ~G2Station()
    {
        ctrl.setRadioConnection(nullptr);
        model.setStepAttController(nullptr);
    }

    // Each slice on the ADC given, the way the operator does it: an
    // RX-only input (EXT1) puts a slice on ADC1, ANT1 on ADC0
    // (P2CodecOrionMkII's antenna-to-ADC rule, run by the model's own
    // DDC assignment).
    // The antenna is kept per band and synced onto the active slice, so
    // each slice is selected before its antenna is picked, as the operator
    // does.
    void route(int adcA, int adcB)
    {
        pick(sliceA, adcA);
        pick(sliceB, adcB);
    }
    void pick(SliceModel* slice, int adc)
    {
        model.setActiveSlice(slice->sliceIndex());
        slice->setRxAntenna(adc == 1 ? QStringLiteral("EXT1") : QStringLiteral("ANT1"));
    }
};

} // namespace

class TstPerAdcStepAttenuator : public QObject {
    Q_OBJECT

private slots:
    // The bench bug: 20 dB on ADC0 (slice A), slice B on EXT1 (ADC1) with
    // no attenuation of its own reads with 0 dB of offset, not 20.
    void sliceOnAdc1ReadsItsOwnAdcsOffset()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:01"));
        QVERIFY(s.sliceA && s.sliceB);
        QVERIFY(s.sliceA->streamIndex() == 0 && s.sliceB->streamIndex() == 1);
        s.route(0, 1);
        QCOMPARE(s.model.sliceAdcIndex(s.sliceA->sliceIndex()), 0);
        QCOMPARE(s.model.sliceAdcIndex(s.sliceB->sliceIndex()), 1);
        QCOMPARE(s.ctrl.rx1Adc(), 0);
        QCOMPARE(s.ctrl.rx2Adc(), 1);

        s.ctrl.setAttenuation(20);
        const double cal = s.model.rxMeterCalOffsetDb();
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceA->sliceIndex()), cal + 20.0);
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceB->sliceIndex()), cal);
        // Each stream's spectrum the same way.
        QCOMPARE(s.model.rxMeterOffsetDbForStream(s.sliceA->streamIndex()), cal + 20.0);
        QCOMPARE(s.model.rxMeterOffsetDbForStream(s.sliceB->streamIndex()), cal);
        // Slice A's reading (RX1's) is unchanged.
        QCOMPARE(s.model.rxMeterOffsetDb(), cal + 20.0);

        // ADC1's own attenuation moves only slice B's offset.
        s.ctrl.setRx2Attenuation(12);
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceA->sliceIndex()), cal + 20.0);
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceB->sliceIndex()), cal + 12.0);
    }

    // ADC1 receives its own attenuator byte; ADC0 keeps slice A's.
    void adc1GetsItsOwnByteOnTheWire()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:02"));
        QVERIFY(s.sliceA && s.sliceB);
        s.route(0, 1);
        s.ctrl.setAttenuation(20);
        s.ctrl.setRx2Attenuation(12);
        QByteArray hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 20);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 12);

        // Through the slice's ADC: B's edit lands on ADC1 only.
        s.ctrl.setAttenuationForAdc(s.model.sliceAdcIndex(s.sliceB->sliceIndex()), 7);
        hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 20);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 7);
        QCOMPARE(s.ctrl.attenuatorDb(), 20);
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 7);
    }

    // The connection's own per-ADC setter writes the ADC1 byte.
    void p2SetterWritesTheAdc1Byte()
    {
        P2RadioConnection conn;
        conn.setAttenuatorForAdc(1, 9);
        conn.setAttenuatorForAdc(0, 4);
        const QByteArray hp = highPriority(conn);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 9);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 4);
    }

    // Protocol 1 carries ADC1's attenuator in bank 12, ADC0's in bank 11.
    void p1SetterWritesBank12()
    {
        P1RadioConnection conn;
        conn.setAttenuatorForAdc(1, 17);
        conn.setAttenuatorForAdc(0, 5);
        QCOMPARE(conn.currentAttenForAdcForTest(1), 17);
        QCOMPARE(conn.currentAttenForAdcForTest(0), 5);
        quint8 bank12[5] = {};
        conn.composeCcForBankForTest(12, bank12);
        QCOMPARE(int(bank12[1]), 17 | 0x20);
        quint8 bank11[5] = {};
        conn.composeCcForBankForTest(11, bank11);
        QCOMPARE(int(bank11[4]), 5 | 0x20);
    }

    // Two slices on ADC0 share one attenuator: one value, one offset, one
    // byte, and ADC1's byte is left alone.
    void twoSlicesOnAdc0ShareOneAttenuator()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:03"));
        QVERIFY(s.sliceA && s.sliceB);
        s.route(0, 0);
        QCOMPARE(s.ctrl.rx2Adc(), -1);
        QVERIFY(s.ctrl.adcUsesRx1Attenuator(0));

        s.ctrl.setAttenuationForAdc(s.model.sliceAdcIndex(s.sliceB->sliceIndex()), 15);
        QCOMPARE(s.ctrl.attenuatorDb(), 15);
        QCOMPARE(s.ctrl.attenuatorDbForAdc(0), 15);
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceA->sliceIndex()),
                 s.model.rxMeterOffsetDbForSlice(s.sliceB->sliceIndex()));
        const QByteArray hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 15);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 0);
    }

    // Diversity links the two ADCs: both take slice A's value, and an edit
    // of either sets both.
    void diversityForcesEqualValues()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:04"));
        QVERIFY(s.sliceA && s.sliceB);
        s.route(0, 1);
        s.ctrl.setAttenuation(20);
        s.ctrl.setRx2Attenuation(12);
        QVERIFY(!s.ctrl.adcAttenuatorsLinked());

        s.model.setDdcContextForTest(false, false, true);
        s.model.syncStepAttenuatorAdcRouting();
        QVERIFY(s.ctrl.adcAttenuatorsLinked());
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 20);
        QCOMPARE(s.ctrl.attenuatorDbForAdc(1), 20);
        QByteArray hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 20);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 20);
        const double cal = s.model.rxMeterCalOffsetDb();
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceB->sliceIndex()), cal + 20.0);

        // The other ADC's edit sets RX1's, and both go out.
        s.ctrl.setRx2Attenuation(5);
        QCOMPARE(s.ctrl.attenuatorDb(), 5);
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 5);
        hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 5);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 5);

        // And slice A's.
        s.ctrl.setAttenuation(9);
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 9);
        hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 9);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 9);
    }

    // Slice A moving to ADC1 (an RX-only input) takes RX1's value with it;
    // slice B, now on ADC0, gets the other ADC's value there.
    void aSwapMovesEachValueToItsNewAdc()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:05"));
        QVERIFY(s.sliceA && s.sliceB);
        s.route(0, 1);
        s.ctrl.setAttenuation(20);
        s.ctrl.setRx2Attenuation(12);

        s.route(1, 0);
        QCOMPARE(s.ctrl.rx1Adc(), 1);
        QCOMPARE(s.ctrl.rx2Adc(), 0);
        const QByteArray hp = highPriority(s.conn);
        QCOMPARE(byteAt(hp, kAdc1AttByte), 20);
        QCOMPARE(byteAt(hp, kAdc0AttByte), 12);
        const double cal = s.model.rxMeterCalOffsetDb();
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceA->sliceIndex()), cal + 20.0);
        QCOMPARE(s.model.rxMeterOffsetDbForSlice(s.sliceB->sliceIndex()), cal + 12.0);
    }

    // The other ADC's attenuator keeps its own band memory, following the
    // band of the slice on it (Thetis rx2_step_attenuator_by_band).
    void theOtherAdcRemembersItsValuePerBand()
    {
        G2Station s(QStringLiteral("02:00:00:00:ad:06"));
        QVERIFY(s.sliceA && s.sliceB);
        s.route(0, 1);
        QCOMPARE(s.ctrl.rx2Band(), Band::Band40m);
        s.ctrl.setRx2Attenuation(12);

        s.sliceB->setFrequency(k17mHz);
        s.pick(s.sliceB, 1);  // the antenna is kept per band
        QCOMPARE(s.model.sliceAdcIndex(s.sliceB->sliceIndex()), 1);
        QCOMPARE(s.ctrl.rx2Band(), Band::Band17m);
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 12);  // never visited: kept
        s.ctrl.setRx2Attenuation(3);
        QCOMPARE(byteAt(highPriority(s.conn), kAdc1AttByte), 3);

        s.sliceB->setFrequency(k40mHz);
        QCOMPARE(s.model.sliceAdcIndex(s.sliceB->sliceIndex()), 1);
        QCOMPARE(s.ctrl.rx2Band(), Band::Band40m);
        QCOMPARE(s.ctrl.rx2AttenuatorDb(), 12);
        QCOMPARE(byteAt(highPriority(s.conn), kAdc1AttByte), 12);
        // Slice A's attenuator never moved.
        QCOMPARE(s.ctrl.attenuatorDb(), 0);
        QCOMPARE(byteAt(highPriority(s.conn), kAdc0AttByte), 0);
    }

    // The other ADC's value and band memory survive a restart, and the
    // restored value reaches the radio.
    void theOtherAdcsValueSurvivesARestart()
    {
        const QString mac = QStringLiteral("02:00:00:00:ad:07");
        startFromNothing(mac);
        {
            StepAttenuatorController ctrl;
            ctrl.setTickTimerEnabled(false);
            ctrl.setMaxAttenuation(31);
            ctrl.loadSettings(mac);
            ctrl.setAdcRouting(0, 1, Band::Band40m, false);
            ctrl.setRx2Attenuation(14);
            ctrl.setAdcRouting(0, 1, Band::Band20m, false);
            ctrl.setRx2Attenuation(6);
            ctrl.saveSettings(mac);
        }
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setMaxAttenuation(31);
        RecordingConnection radio;
        ctrl.setAdcRouting(0, 1, Band::Band20m, false);
        ctrl.setRadioConnection(&radio);
        radio.sends.clear();
        ctrl.loadSettings(mac);
        QCOMPARE(ctrl.rx2AttenuatorDb(), 6);
        QVERIFY(radio.sends.contains(QPair<int, int>(1, 6)));

        radio.sends.clear();
        ctrl.setBandRestoreToRadio(true);
        ctrl.setAdcRouting(0, 1, Band::Band40m, false);
        QCOMPARE(ctrl.rx2AttenuatorDb(), 14);
        QCOMPARE(radio.sends, (Sends{{1, 14}}));
        ctrl.setRadioConnection(nullptr);
    }

    // Slice A's attenuation and preamp restored at connect reach the radio
    // then, as Thetis sends RX1's (and RX2's) stored values when the radio
    // starts (console.cs InitConsole, 2175-2178, and SetComboPreampForHPSDR).
    void theValuesRestoredAtConnectReachTheRadio()
    {
        const QString mac = QStringLiteral("02:00:00:00:ad:09");
        startFromNothing(mac);
        {
            StepAttenuatorController ctrl;
            ctrl.setTickTimerEnabled(false);
            ctrl.setMaxAttenuation(31);
            ctrl.loadSettings(mac);
            ctrl.setAttenuation(17);
            ctrl.setPreampMode(PreampMode::On);
            ctrl.saveSettings(mac);
        }
        StepAttenuatorController ctrl;
        ctrl.setTickTimerEnabled(false);
        ctrl.setMaxAttenuation(31);
        RecordingConnection radio;
        ctrl.setRadioConnection(&radio);
        radio.sends.clear();
        radio.preamp.clear();
        ctrl.loadSettings(mac);
        QCOMPARE(ctrl.attenuatorDb(), 17);
        QVERIFY(radio.sends.contains(QPair<int, int>(0, 17)));
        QCOMPARE(radio.preamp, QList<bool>{true});
        ctrl.setRadioConnection(nullptr);
    }

    // Controller sends: slice A's value only to slice A's ADC, the other
    // ADC's only to it, and a routing call that moves nothing sends nothing.
    void eachValueGoesOnlyToItsAdc()
    {
        StepAttenuatorController ctrl;
        loadController(ctrl, QStringLiteral("02:00:00:00:ad:08"));
        RecordingConnection radio;
        ctrl.setRadioConnection(&radio);
        ctrl.setAdcRouting(0, 1, Band::Band20m, false);
        radio.sends.clear();

        ctrl.setAttenuation(18);
        QCOMPARE(radio.sends, (Sends{{0, 18}}));
        radio.sends.clear();
        ctrl.setRx2Attenuation(11);
        QCOMPARE(radio.sends, (Sends{{1, 11}}));
        radio.sends.clear();
        ctrl.setAdcRouting(0, 1, Band::Band20m, false);
        QVERIFY(radio.sends.isEmpty());

        // A single-ADC radio (every slice on ADC0, the Hermes Lite 2 among
        // them): the other ADC's value is never sent.
        ctrl.setAdcRouting(0, -1, Band::Band20m, false);
        radio.sends.clear();
        ctrl.setAttenuationForAdc(0, 9);
        QCOMPARE(radio.sends, (Sends{{0, 9}}));
        QCOMPARE(ctrl.attenuatorDbForAdc(1), 9);  // an unused ADC reads RX1's
        ctrl.setRadioConnection(nullptr);
    }
};

QTEST_MAIN(TstPerAdcStepAttenuator)
#include "tst_per_adc_step_attenuator.moc"
