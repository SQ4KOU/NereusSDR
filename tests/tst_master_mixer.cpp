#include <QtTest/QtTest>
#include "core/audio/MasterMixer.h"
#include <array>

using namespace NereusSDR;

class TstMasterMixer : public QObject {
    Q_OBJECT
private slots:
    void emptyMixIsSilent() {
        MasterMixer mix;
        std::array<float, 16> out{};
        mix.mixInto(out.data(), 8);
        for (float s : out) { QCOMPARE(s, 0.0f); }
    }

    void singleSliceUnityGainMixesThrough() {
        MasterMixer mix;
        mix.setSliceGain(42, 1.0f, 0.0f);  // unity, center
        std::array<float, 4> in = {0.5f, 0.5f, -0.25f, -0.25f};  // 2 frames stereo
        mix.accumulate(42, in.data(), 2);
        std::array<float, 4> out{};
        mix.mixInto(out.data(), 2);
        QCOMPARE(out[0], 0.5f);
        QCOMPARE(out[1], 0.5f);
        QCOMPARE(out[2], -0.25f);
        QCOMPARE(out[3], -0.25f);
    }

    void twoSlicesSumLinearly() {
        MasterMixer mix;
        mix.setSliceGain(1, 1.0f, 0.0f);
        mix.setSliceGain(2, 1.0f, 0.0f);
        std::array<float, 2> a = {0.3f, 0.3f};
        std::array<float, 2> b = {0.4f, 0.4f};
        mix.accumulate(1, a.data(), 1);
        mix.accumulate(2, b.data(), 1);
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        QCOMPARE(out[0], 0.7f);
        QCOMPARE(out[1], 0.7f);
    }

    void panFullLeftSuppressesRight() {
        MasterMixer mix;
        mix.setSliceGain(1, 1.0f, -1.0f);  // full left
        std::array<float, 2> in = {0.5f, 0.5f};
        mix.accumulate(1, in.data(), 1);
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        QVERIFY(out[0] > 0.4f);
        QVERIFY(std::abs(out[1]) < 0.0001f);
    }

    void muteZerosContribution() {
        MasterMixer mix;
        mix.setSliceGain(1, 1.0f, 0.0f);
        mix.setSliceMuted(1, true);
        std::array<float, 2> in = {0.9f, 0.9f};
        mix.accumulate(1, in.data(), 1);
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        QCOMPARE(out[0], 0.0f);
        QCOMPARE(out[1], 0.0f);
    }

    void unknownSliceIsIgnored() {
        MasterMixer mix;
        std::array<float, 2> in = {0.9f, 0.9f};
        mix.accumulate(999, in.data(), 1);  // never registered
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        QCOMPARE(out[0], 0.0f);
        QCOMPARE(out[1], 0.0f);
    }

    void mixIntoResetsAccumulator() {
        MasterMixer mix;
        mix.setSliceGain(1, 1.0f, 0.0f);
        std::array<float, 2> in = {0.5f, 0.5f};
        mix.accumulate(1, in.data(), 1);
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        // Second mixInto with no accumulate → silence.
        std::array<float, 2> out2{};
        mix.mixInto(out2.data(), 1);
        QCOMPARE(out2[0], 0.0f);
        QCOMPARE(out2[1], 0.0f);
    }

    void removedSliceNoLongerMixes() {
        MasterMixer mix;
        mix.setSliceGain(1, 1.0f, 0.0f);
        mix.removeSlice(1);
        std::array<float, 2> in = {0.9f, 0.9f};
        mix.accumulate(1, in.data(), 1);
        std::array<float, 2> out{};
        mix.mixInto(out.data(), 1);
        QCOMPARE(out[0], 0.0f);
        QCOMPARE(out[1], 0.0f);
    }
};

QTEST_APPLESS_MAIN(TstMasterMixer)
#include "tst_master_mixer.moc"
