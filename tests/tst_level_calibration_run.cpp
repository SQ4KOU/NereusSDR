// no-port-check: test-only. Thetis file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No Thetis logic is ported here; this file is NereusSDR-original.
//
// =================================================================
// LevelCalibrationRun against a fake receiver: a fake meter that reads a
// set level per preamp setting and a fake spectrum with one carrier.
// =================================================================
//
// The procedure is Thetis CalibrateLevel (console.cs:9856-10232
// [v2.10.3.15]). Every wait is zero here, so a run finishes in a few event
// loop turns. Nothing here touches a radio.
// =================================================================

#include <QtTest/QtTest>

#include <cmath>
#include <map>

#include "core/LevelCalibrationRun.h"

using namespace NereusSDR;

namespace {

constexpr int kFft = 4096;
constexpr double kRate = 192000.0;
constexpr double kCentre = 7100000.0;
constexpr double kBinWidth = kRate / kFft;  // 46.875 Hz

class FakeHost final : public LevelCalibrationHost {
public:
    // Receiver state.
    bool live = true;
    bool tx = false;
    bool alex = false;
    HPSDRModel boardModel = HPSDRModel::HERMES;
    double vfoHz = 14200000.0;
    bool ritOn = true;
    int ritHz = 250;
    DSPMode mode = DSPMode::USB;
    int phoneBuffer = 4096;
    bool att1 = true;
    bool att2 = true;
    PreampMode preamp1 = PreampMode::Minus20;
    bool preamp2 = false;
    std::optional<double> meterCal = 3.5;
    std::optional<double> displayCal;  // absent key: the model default
    double displayDefault = 1.25;
    std::array<float, 10> rx1Offsets{};
    std::map<int, float> rx2Offsets;
    int filterLo = -5000;
    int filterHi = 5000;

    // Fake signal.
    double signalHz = kCentre + 1500.0;
    float peakPower = 1.0f;       // |X|^2 of the carrier bin
    float noisePower = 1.0e-6f;   // |X|^2 of every other bin
    bool spectrumOn = true;
    std::map<PreampMode, float> meterByMode{
        {PreampMode::On, -73.0f},
        {PreampMode::Off, -93.0f},
        {PreampMode::Minus10, -83.5f},
        {PreampMode::Minus20, -93.25f},
        {PreampMode::Minus30, -103.0f},
        {PreampMode::Minus40, -113.0f},
        {PreampMode::Minus50, -122.5f},
    };

    // Observation.
    QStringList log;
    int meterReads = 0;
    int spectrumReads = 0;
    std::function<void()> onMeterRead;
    std::vector<std::optional<double>> meterCalDuringReads;

    bool radioLive() const override { return live; }
    bool transmitting() const override { return tx; }
    bool alexPresent() const override { return alex; }
    HPSDRModel model() const override { return boardModel; }

    double frequencyHz() const override { return vfoHz; }
    void setFrequencyHz(double hz) override { vfoHz = hz; log << QStringLiteral("vfo"); }
    bool ritEnabled() const override { return ritOn; }
    int ritOffsetHz() const override { return ritHz; }
    void setRit(bool on, int hz) override { ritOn = on; ritHz = hz; log << QStringLiteral("rit"); }
    DSPMode dspMode() const override { return mode; }
    void setDspMode(DSPMode m) override { mode = m; log << QStringLiteral("mode"); }
    int phoneRxBuffer() const override { return phoneBuffer; }
    void setPhoneRxBuffer(int size) override { phoneBuffer = size; log << QStringLiteral("buffer"); }
    bool rx1StepAttEnabled() const override { return att1; }
    bool rx2StepAttEnabled() const override { return att2; }
    void setStepAttEnabled(bool rx1, bool rx2) override { att1 = rx1; att2 = rx2; log << QStringLiteral("att"); }
    PreampMode rx1PreampMode() const override { return preamp1; }
    void setRx1PreampMode(PreampMode m) override { preamp1 = m; log << QStringLiteral("preamp1"); }
    bool rx2PreampOn() const override { return preamp2; }
    void setRx2PreampOn(bool on) override { preamp2 = on; log << QStringLiteral("preamp2"); }

    std::optional<double> meterCalOverride() const override { return meterCal; }
    std::optional<double> displayCalOverride() const override { return displayCal; }
    double meterCalDb() const override { return meterCal.value_or(0.0); }
    double displayCalDb() const override { return displayCal.value_or(displayDefault); }
    void setMeterCalOverride(std::optional<double> db) override { meterCal = db; }
    void setDisplayCalOverride(std::optional<double> db) override { displayCal = db; }
    float rx1PreampOffsetDb(PreampMode m) const override { return rx1Offsets[size_t(m)]; }
    void setRx1PreampOffsetDb(PreampMode m, float db) override { rx1Offsets[size_t(m)] = db; }
    void setRx2PreampOffsetDb(PreampMode m, float db) override { rx2Offsets[int(m)] = db; }

    int filterLowHz() const override { return filterLo; }
    int filterHighHz() const override { return filterHi; }

    float readSignalAverage() override
    {
        ++meterReads;
        meterCalDuringReads.push_back(meterCal);
        if (onMeterRead) {
            onMeterRead();
        }
        const auto it = meterByMode.find(preamp1);
        return it == meterByMode.end() ? -200.0f : it->second;
    }

    std::optional<LevelCalSpectrum> latestSpectrum() override
    {
        ++spectrumReads;
        if (!spectrumOn) {
            return std::nullopt;
        }
        LevelCalSpectrum s;
        s.centreHz = kCentre;
        s.sampleRateHz = kRate;
        s.binsLinear.fill(noisePower, kFft);
        const int bin = kFft / 2 + int(std::lround((signalHz - kCentre) / kBinWidth));
        s.binsLinear[bin] = peakPower;
        return s;
    }
};

LevelCalibrationRun::Timings instant()
{
    LevelCalibrationRun::Timings t;
    t.settleBeforeZeroBeatMs = 0;
    t.snapGapMs = 0;
    t.meterPreWaitMs = 0;
    t.meterReadGapMs = 0;
    t.preampSwitchMs = 0;
    t.finalSettleMs = 0;
    return t;
}

struct Outcome {
    bool ok = false;
    QString message;
};

Outcome runToEnd(LevelCalibrationRun& run, QSignalSpy& done)
{
    if (done.isEmpty()) {
        done.wait(5000);
    }
    Outcome o;
    if (!done.isEmpty()) {
        o.ok = done.first().at(0).toBool();
        o.message = done.first().at(1).toString();
    }
    Q_UNUSED(run);
    return o;
}

void verifyRestored(const FakeHost& h, double vfo)
{
    QCOMPARE(h.vfoHz, vfo);
    QCOMPARE(h.ritOn, true);
    QCOMPARE(h.ritHz, 250);
    QCOMPARE(h.mode, DSPMode::USB);
    QCOMPARE(h.phoneBuffer, 4096);
    QCOMPARE(h.att1, true);
    QCOMPARE(h.att2, true);
    QCOMPARE(h.preamp1, PreampMode::Minus20);
    QCOMPARE(h.preamp2, false);
}

} // namespace

class TestLevelCalibrationRun : public QObject {
    Q_OBJECT
private slots:
    // A strong carrier: the run finds it, sets the offsets from Thetis's
    // formulas and puts the receiver back as it found it.
    void signalFound_setsThetisOffsetsAndRestores()
    {
        FakeHost h;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        QSignalSpy progress(&run, &LevelCalibrationRun::progress);

        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        QVERIFY(run.isRunning());
        const Outcome o = runToEnd(run, done);
        QVERIFY2(o.ok, qPrintable(o.message));
        QVERIFY(!run.isRunning());

        // off_offset = avg2 - avg = -93 - (-73) = -20
        // (console.cs:9990-9995)
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Off)], 20.0f);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::On)], 0.0f);
        QCOMPARE(h.rx2Offsets.at(int(PreampMode::Off)), 20.0f);
        QCOMPARE(h.rx2Offsets.at(int(PreampMode::On)), 0.0f);
        // No Alex: the -10 to -50 steps are not measured.
        QCOMPARE(h.meterReads, 100);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus10)], 0.0f);

        // Meter: diff = level - (avg + 0 + offset[ON]) = -50 - (-73) = 23
        // (console.cs:10160-10163), on a cal zeroed first.
        QVERIFY(h.meterCal.has_value());
        QCOMPARE(*h.meterCal, 23.0);
        for (const auto& c : h.meterCalDuringReads) {
            QVERIFY(c.has_value());
            QCOMPARE(*c, 0.0);
        }
        // Display: avg2 = 10log10(maxsumsq / iterations / fft^2)
        // (console.cs:10156); the carrier bin is 1.0.
        const double avg2 = 10.0 * std::log10(1.0 / (double(kFft) * kFft));
        QVERIFY(h.displayCal.has_value());
        QVERIFY(std::abs(*h.displayCal - (-50.0 - avg2)) < 1e-3);

        // Zero beat moved the VFO onto the carrier (AM: delta = peak_hz),
        // then the restore put it back.
        verifyRestored(h, 14200000.0);
        QVERIFY(progress.count() >= 2);
        QCOMPARE(progress.first().at(0).toInt(), 0);
        // 100 reads of 120 without Alex (console.cs:9935-9943).
        QCOMPARE(progress.last().at(0).toInt(), 100 * 100 / 120);
    }

    // The receiver is set up as Thetis sets it before measuring: RIT off,
    // AM, a 16384 buffer, both step attenuators off, both preamps on, the
    // VFO on the calibration frequency and then zero beat onto the carrier.
    void setup_matchesThetis()
    {
        FakeHost h;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        struct Seen {
            bool rit; DSPMode mode; int buffer; bool a1; bool a2; bool p2; double vfo;
        };
        std::optional<Seen> seen;
        h.onMeterRead = [&h, &seen]() {
            if (!seen) {
                seen = Seen{h.ritOn, h.mode, h.phoneBuffer, h.att1, h.att2, h.preamp2, h.vfoHz};
            }
        };
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        QVERIFY(runToEnd(run, done).ok);
        QVERIFY(seen.has_value());
        QCOMPARE(seen->rit, false);
        QCOMPARE(seen->mode, DSPMode::AM);
        QCOMPARE(seen->buffer, 16384);
        QCOMPARE(seen->a1, false);
        QCOMPARE(seen->a2, false);
        QCOMPARE(seen->p2, true);
        // peak_hz = (int)((max_bucket - zero_hz_bucket) * hz_per_bucket)
        // (console.cs:36253): zero bucket 2048 + 21, carrier bucket 2080.
        QCOMPARE(seen->vfo, kCentre + 1000.0 + double(int((2080 - 2069) * kBinWidth)));
    }

    // A carrier less than 30 dB above the noise: Thetis's refusal, and
    // everything back as it was, the calibration included.
    void weakSignal_refusedAndRestored()
    {
        FakeHost h;
        h.peakPower = 1.0e-4f;  // 20 dB over the noise
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        const Outcome o = runToEnd(run, done);
        QVERIFY(!o.ok);
        QVERIFY2(o.message.contains(QStringLiteral("30 dB")), qPrintable(o.message));
        QCOMPARE(h.meterReads, 0);
        QCOMPARE(h.meterCal, std::optional<double>(3.5));
        QVERIFY(!h.displayCal.has_value());
        verifyRestored(h, 14200000.0);
    }

    // No spectrum at all: a plain refusal, and everything restored.
    void noSpectrum_refusedAndRestored()
    {
        FakeHost h;
        h.spectrumOn = false;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        const Outcome o = runToEnd(run, done);
        QVERIFY(!o.ok);
        QVERIFY(o.message.contains(QStringLiteral("spectrum")));
        QCOMPARE(h.meterCal, std::optional<double>(3.5));
        verifyRestored(h, 14200000.0);
    }

    // An Alex board outside the exclusion list measures -10 to -50 too
    // (console.cs:9997-10007), and only RX1 gets those offsets.
    void alexBoard_measuresTheMinusSettings()
    {
        FakeHost h;
        h.alex = true;
        h.boardModel = HPSDRModel::ANAN100;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        QSignalSpy progress(&run, &LevelCalibrationRun::progress);
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        QVERIFY(runToEnd(run, done).ok);
        QCOMPARE(h.meterReads, 350);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus10)], 10.5f);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus20)], 20.25f);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus30)], 30.0f);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus40)], 40.0f);
        QCOMPARE(h.rx1Offsets[size_t(PreampMode::Minus50)], 49.5f);
        QVERIFY(h.rx2Offsets.find(int(PreampMode::Minus10)) == h.rx2Offsets.end());
        QCOMPARE(progress.last().at(0).toInt(), 350 * 100 / 390);
    }

    // The boards Thetis leaves out of the -10 to -50 steps.
    void exclusionList_matchesThetis()
    {
        QVERIFY(!LevelCalibrationRun::measuresAlexSteps(false, HPSDRModel::ANAN100));
        QVERIFY(LevelCalibrationRun::measuresAlexSteps(true, HPSDRModel::HERMES));
        QVERIFY(LevelCalibrationRun::measuresAlexSteps(true, HPSDRModel::ANAN100));
        QVERIFY(LevelCalibrationRun::measuresAlexSteps(true, HPSDRModel::ANAN100D));
        QVERIFY(LevelCalibrationRun::measuresAlexSteps(true, HPSDRModel::ANAN200D));
        for (HPSDRModel m : {HPSDRModel::ANAN10, HPSDRModel::ANAN10E, HPSDRModel::ANAN7000D,
                             HPSDRModel::ANAN8000D, HPSDRModel::ORIONMKII, HPSDRModel::ANAN_G2E,
                             HPSDRModel::ANAN_G2, HPSDRModel::ANAN_G2_1K,
                             HPSDRModel::ANVELINAPRO3, HPSDRModel::REDPITAYA}) {
            QVERIFY(!LevelCalibrationRun::measuresAlexSteps(true, m));
        }
    }

    // An excluded Alex board (the G2) skips the steps; the progress divisor
    // stays 390 as in Thetis.
    void excludedAlexBoard_skipsTheMinusSettings()
    {
        FakeHost h;
        h.alex = true;
        h.boardModel = HPSDRModel::ANAN_G2;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        QSignalSpy progress(&run, &LevelCalibrationRun::progress);
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        QVERIFY(runToEnd(run, done).ok);
        QCOMPARE(h.meterReads, 100);
        QCOMPARE(progress.last().at(0).toInt(), 100 * 100 / 390);
    }

    // Cancel part way: the calibration and the receiver go back; offsets
    // already measured stay, as in Thetis.
    void cancel_restoresEverything()
    {
        FakeHost h;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        h.onMeterRead = [&h, &run]() {
            if (h.meterReads == 60) {
                run.cancel();
            }
        };
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        const Outcome o = runToEnd(run, done);
        QVERIFY(!o.ok);
        QVERIFY(o.message.contains(QStringLiteral("canceled")));
        QCOMPARE(done.count(), 1);
        QCOMPARE(h.meterReads, 60);
        QCOMPARE(h.meterCal, std::optional<double>(3.5));
        QVERIFY(!h.displayCal.has_value());
        verifyRestored(h, 14200000.0);
        QVERIFY(!run.isRunning());
    }

    // Refused before touching anything: radio off, transmitting, running.
    void start_refusals()
    {
        FakeHost h;
        h.live = false;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QVERIFY(run.start(-50.0f, kCentre).contains(QStringLiteral("radio")));
        h.live = true;
        h.tx = true;
        QVERIFY(run.start(-50.0f, kCentre).contains(QStringLiteral("transmitting")));
        QVERIFY(h.log.isEmpty());
        h.tx = false;
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        QVERIFY(run.start(-50.0f, kCentre).contains(QStringLiteral("already")));
        run.cancel();
    }

    // Transmitting part way through fails the run and restores.
    void transmitMidRun_failsAndRestores()
    {
        FakeHost h;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        h.onMeterRead = [&h]() {
            if (h.meterReads == 10) {
                h.tx = true;
            }
        };
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        const Outcome o = runToEnd(run, done);
        QVERIFY(!o.ok);
        QVERIFY(o.message.contains(QStringLiteral("transmitting")));
        QCOMPARE(h.meterCal, std::optional<double>(3.5));
        verifyRestored(h, 14200000.0);
    }

    // The radio going away part way fails the run.
    void radioLostMidRun_fails()
    {
        FakeHost h;
        LevelCalibrationRun run(&h);
        run.setTimings(instant());
        QSignalSpy done(&run, &LevelCalibrationRun::finished);
        h.onMeterRead = [&h]() {
            if (h.meterReads == 5) {
                h.live = false;
            }
        };
        QVERIFY(run.start(-50.0f, kCentre + 1000.0).isEmpty());
        const Outcome o = runToEnd(run, done);
        QVERIFY(!o.ok);
        QVERIFY(o.message.contains(QStringLiteral("disconnected")));
        QCOMPARE(h.meterCal, std::optional<double>(3.5));
    }
};

QTEST_MAIN(TestLevelCalibrationRun)
#include "tst_level_calibration_run.moc"
