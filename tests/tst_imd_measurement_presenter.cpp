// no-port-check: NereusSDR-original tests of synthetic visible measurement planes.
#include <QtTest>
#include "gui/ImdMeasurementPresenter.h"
#include <cmath>
#include <limits>
using namespace NereusSDR;
namespace {
QVector<float> spectrum(float tone = -22.0f)
{
    QVector<float> pixels(800, -120.0f);
    pixels[100] = -88.0f; pixels[220] = -65.0f;
    pixels[340] = tone; pixels[460] = tone;
    pixels[580] = -65.0f; pixels[700] = -88.0f;
    return pixels;
}
ImdInputContext input(quint64 sequence = 1)
{
    return {sequence, 1, 100, 14176000.0, 48000.0 / 799.0,
            true, true, true, true, true};
}
}
class TestImdMeasurementPresenter : public QObject {
    Q_OBJECT
private slots:
    void sameFrameDoesNotAdvanceEma()
    {
        ImdMeasurementPresenter presenter;
        presenter.acceptVisibleSpectrum(spectrum(), input());
        const ImdMeasurementFrame second = presenter.acceptVisibleSpectrum(spectrum(-26.0f), input(2));
        QVERIFY(second.summary);
        QVERIFY(std::abs(second.summary->f0L + 22.4f) < 0.001f);
        for (int i = 0; i < 20; ++i) {
            QCOMPARE(presenter.acceptVisibleSpectrum(spectrum(-26.0f), input(2)).summary->f0L,
                     second.summary->f0L);
        }
        QCOMPARE(presenter.acceptVisibleSpectrum(spectrum(-30.0f), input(1)).summary->f0L,
                 second.summary->f0L);
    }
    void missingProductClearsAggregateAndRecoveryReseeds()
    {
        ImdMeasurementPresenter presenter;
        presenter.acceptVisibleSpectrum(spectrum(), input());
        QVector<float> missing = spectrum(); missing[700] = -120.0f;
        const auto& partial = presenter.acceptVisibleSpectrum(missing, input(2));
        QCOMPARE(partial.status, ImdStatus::ProductsMissing);
        QVERIFY(!partial.summary);
        QVERIFY(!partial.peaks[5]);
        for (int i = 0; i < 5; ++i) { QVERIFY(partial.peaks[i]); }
        QVERIFY(partial.expectedFrequencyHz[5]);
        const auto& recovered = presenter.acceptVisibleSpectrum(spectrum(-30.0f), input(3));
        QCOMPARE(recovered.status, ImdStatus::Valid);
        QCOMPARE(recovered.summary->f0L, -30.0f);
    }
    void unequalToneReferencesStaySourceFaithful()
    {
        ImdMeasurementPresenter presenter;
        QVector<float> pixels = spectrum();
        pixels[340] = -20.0f; pixels[460] = -22.0f;
        pixels[220] = -68.0f; pixels[580] = -70.0f;
        const auto& frame = presenter.acceptVisibleSpectrum(pixels, input());
        QCOMPARE(frame.status, ImdStatus::Valid);
        QCOMPARE(*frame.peaks[2]->relativeDbc, -48.0f);
        QCOMPARE(frame.summary->worstImd3DBc, 46.0f);
        QCOMPARE(frame.summary->oip3, 1.0f);
        QCOMPARE(frame.summary->oip5, 11.0f);
    }
    void gates_data()
    {
        QTest::addColumn<int>("gate"); QTest::addColumn<ImdStatus>("status");
        QTest::newRow("disabled") << 0 << ImdStatus::Disabled;
        QTest::newRow("no-tx") << 1 << ImdStatus::WaitingForTx;
        QTest::newRow("no-two-tone") << 2 << ImdStatus::WaitingForTx;
        QTest::newRow("dup-off") << 3 << ImdStatus::DuplexRequired;
        QTest::newRow("digital-tx-or-unavailable") << 4 << ImdStatus::WaitingForSpectrum;
        QTest::newRow("empty") << 5 << ImdStatus::WaitingForSpectrum;
        QTest::newRow("nonfinite") << 6 << ImdStatus::WaitingForSpectrum;
        QTest::newRow("bad-frequency-grid") << 7 << ImdStatus::WaitingForSpectrum;
        QTest::newRow("too-close") << 8 << ImdStatus::FundamentalsUnresolved;
        QTest::newRow("one-fundamental") << 9 << ImdStatus::FundamentalsUnresolved;
    }
    void gates()
    {
        QFETCH(int, gate); QFETCH(ImdStatus, status);
        ImdMeasurementPresenter presenter;
        presenter.acceptVisibleSpectrum(spectrum(), input());
        auto context = input(2); auto pixels = spectrum();
        switch (gate) {
        case 0: context.measurementsEnabled = false; break;
        case 1: context.mox = false; break;
        case 2: context.testing = false; break;
        case 3: context.duplex = false; break;
        case 4: context.sourceEligible = false; break;
        case 5: pixels.clear(); break;
        case 6: pixels[3] = std::numeric_limits<float>::quiet_NaN(); break;
        case 7: context.sampleStepHz = 0; break;
        case 8: pixels.fill(-120); pixels[340] = -22; pixels[350] = -22; break;
        case 9: pixels.fill(-120); pixels[340] = -22; break;
        }
        const auto& frame = presenter.acceptVisibleSpectrum(pixels, context);
        QCOMPARE(frame.status, status); QVERIFY(!frame.summary);
        for (const auto& expected : frame.expectedFrequencyHz) { QVERIFY(!expected); }
        const auto& recovered = presenter.acceptVisibleSpectrum(spectrum(-30), input(3));
        QCOMPARE(recovered.summary->f0L, -30.0f);
    }
    void expectedProductsOutsideViewHaveNoMeasuredPeak()
    {
        QVector<float> pixels(800, -120.0f);
        pixels[40] = -22; pixels[200] = -22; pixels[360] = -65; pixels[520] = -88;
        ImdMeasurementPresenter presenter;
        const auto& frame = presenter.acceptVisibleSpectrum(pixels, input());
        QCOMPARE(frame.status, ImdStatus::ProductsOutsideView);
        QVERIFY(!frame.summary); QVERIFY(!frame.peaks[2]); QVERIFY(!frame.peaks[4]);
        QVERIFY(frame.peaks[3]); QVERIFY(frame.peaks[5]);
        QVERIFY(*frame.expectedFrequencyHz[2] < frame.context.firstSampleHz);
    }
    void invalidationCannotReplayAcceptedSequence()
    {
        ImdMeasurementPresenter presenter;
        presenter.acceptVisibleSpectrum(spectrum(), input());
        presenter.invalidate(ImdStatus::WaitingForSpectrum, input());
        const auto& replay = presenter.acceptVisibleSpectrum(spectrum(), input());
        QCOMPARE(replay.status, ImdStatus::WaitingForSpectrum);
        QVERIFY(!replay.summary);
        QCOMPARE(presenter.acceptVisibleSpectrum(spectrum(-30), input(2)).summary->f0L, -30.0f);
    }
    void contextAndMappingChangesReseed()
    {
        ImdMeasurementPresenter presenter;
        presenter.acceptVisibleSpectrum(spectrum(), input());
        auto context = input(1); context.contextRevision = 2;
        QCOMPARE(presenter.acceptVisibleSpectrum(spectrum(-30), context).summary->f0L, -30.0f);
        context.frameSequence = 2; context.firstSampleHz += 12000;
        const auto& moved = presenter.acceptVisibleSpectrum(spectrum(-35), context);
        QCOMPARE(moved.summary->f0L, -35.0f);
        QCOMPARE(moved.peaks[0]->frequencyHz, 14188000.0 + 340 * 48000.0 / 799.0);
        presenter.invalidate(ImdStatus::WaitingForSpectrum, context);
        QVERIFY(!presenter.frame().summary);
        context.frameSequence = 3;
        QCOMPARE(presenter.acceptVisibleSpectrum(spectrum(-40), context).summary->f0L, -40.0f);
    }
};
QTEST_MAIN(TestImdMeasurementPresenter)
#include "tst_imd_measurement_presenter.moc"
