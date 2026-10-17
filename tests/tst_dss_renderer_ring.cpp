#include <QTest>
#include <QVector>
#include <cmath>

#include "gui/DssRenderer.h"
#include "gui/DssQuietOverhang.h"
#include <limits>

using namespace NereusSDR;

namespace {

QVector<float> flatRow(int n, float dbm)
{
    QVector<float> v(n, dbm);
    return v;
}

}  // namespace

class TestDssRendererRing : public QObject {
    Q_OBJECT

private slots:
    void quietBaselineUsesFiniteThirtyPercentOfStrideEightFoldedSamples() {
        // Un-sampled columns are strong carriers: the accepted reference
        // samples x=0,8,... before selecting floor(count*0.3), not a mean.
        QVector<float> folded(80, -40.0f);
        for (int i = 0; i < 10; ++i) { folded[i * 8] = -140.0f + i; }
        const auto quiet = dssQuietLevelFromFoldedRow(folded);
        QVERIFY(quiet.has_value());
        QCOMPARE(*quiet, -137.0f);
        folded[0] = std::numeric_limits<float>::quiet_NaN();
        folded[8] = std::numeric_limits<float>::infinity();
        const auto finiteQuiet = dssQuietLevelFromFoldedRow(folded);
        QVERIFY(finiteQuiet.has_value());
        QCOMPARE(*finiteQuiet, -136.0f);
        QVERIFY(!dssQuietLevelFromFoldedRow({}).has_value());
        QVERIFY(!dssQuietLevelFromFoldedRow(QVector<float>(80,
            std::numeric_limits<float>::quiet_NaN())).has_value());
    }

    void quietBaselineRetainsItsOwnRowAcrossAgeWrapAndReset() {
        DssRenderer renderer;
        renderer.pushRow(flatRow(kDssCols, -130.0f), 14.2, 0.192);
        renderer.pushRow(flatRow(kDssCols, -105.0f), 14.2, 0.192);
        QVERIFY(renderer.rowQuietDbmAtAge(0).has_value());
        QVERIFY(renderer.rowQuietDbmAtAge(1).has_value());
        QCOMPARE(*renderer.rowQuietDbmAtAge(0), -105.0f);
        QCOMPARE(*renderer.rowQuietDbmAtAge(1), -130.0f);
        // Capture from the folded row before the renderer's existing
        // temporal smoothing, preserving the exact row-specific baseline.
        for (int i = 0; i < kDssRows + 7; ++i) {
            renderer.pushRow(flatRow(kDssCols, -190.0f + i), 14.2, 0.192);
        }
        QCOMPARE(*renderer.rowQuietDbmAtAge(0), -190.0f + kDssRows + 6);
        QCOMPARE(*renderer.rowQuietDbmAtAge(kDssRows - 1), -183.0f);
        QVERIFY(!renderer.rowQuietDbmAtAge(-1).has_value());
        QVERIFY(!renderer.rowQuietDbmAtAge(kDssRows).has_value());
        renderer.clear();
        QVERIFY(!renderer.rowQuietDbmAtAge(0).has_value());
    }

    void quietFallbackPreservesCapturedDataAndOnlyFillsOverhang() {
        const DssHistorySample exact = dssSampleQuietOverhang(1.2f, -60.0f, -40.0f, -135.0f, -150.0f);
        QCOMPARE(exact.dbm, -60.0f); QVERIFY(exact.covered);
        const DssHistorySample wide = dssSampleQuietOverhang(1.2f, std::nullopt, -40.0f, -135.0f, -150.0f);
        QCOMPARE(wide.dbm, -40.0f); QVERIFY(wide.covered);
        const DssHistorySample quiet = dssSampleQuietOverhang(1.2f, std::nullopt, std::nullopt, -135.0f, -150.0f);
        QCOMPARE(quiet.dbm, -135.0f); QVERIFY(quiet.covered);
        const DssHistorySample inside = dssSampleQuietOverhang(0.5f, std::nullopt, std::nullopt, -135.0f, -150.0f);
        QCOMPARE(inside.dbm, -150.0f); QVERIFY(!inside.covered);
        const DssHistorySample unknown = dssSampleQuietOverhang(-0.2f, std::nullopt, std::nullopt, std::nullopt, -150.0f);
        QCOMPARE(unknown.dbm, -150.0f); QVERIFY(!unknown.covered);
        const DssHistorySample invalid = dssSampleQuietOverhang(1.2f, std::nullopt, std::nullopt,
            std::numeric_limits<float>::quiet_NaN(), -150.0f);
        QCOMPARE(invalid.dbm, -150.0f); QVERIFY(!invalid.covered);
    }

    void emptyRenderer_hasNoData() {
        DssRenderer r;
        QVERIFY(!r.hasData());
        QCOMPARE(r.rowCount(), 0);
        QCOMPARE(r.cols(), kDssCols);
        QCOMPARE(r.rows(), kDssRows);
    }

    // A single-bin carrier must survive the squeeze from full FFT width to
    // 768 columns. A mean-based downsample would bury it; the reduction is
    // peak-preserving precisely so it cannot.
    void singleBinCarrier_survivesDownsample() {
        DssRenderer r;
        QVector<float> bins = flatRow(8192, -130.0f);
        bins[4096] = -40.0f;
        // Three identical pushes clear the median-of-3 impulse rejector,
        // which would otherwise treat a one-frame spike as interference.
        for (int i = 0; i < 3; ++i) {
            r.pushRow(bins, 14.2, 0.192);
        }
        const float* row = r.rowDataRing(r.headRing());
        float peak = -1000.0f;
        int peakCol = -1;
        for (int c = 0; c < kDssCols; ++c) {
            if (row[c] > peak) { peak = row[c]; peakCol = c; }
        }
        // Two independent properties, because either alone is weak.
        //
        // The carrier must still be the loudest column, and at the column it
        // belongs in. This is blur-independent and is how upstream's own
        // dss_renderer_test.cpp checks the same thing.
        const int expectedCol = 4096 * kDssCols / 8192;
        QCOMPARE(peakCol, expectedCol);
        //
        // And it must still stand well clear of the floor in absolute terms,
        // which is what actually catches a mean-based downsample. Do not
        // tighten this past -85: the unconditional 1-2-1 spatial blur maps an
        // isolated column to 0.25*floor + 0.5*carrier + 0.25*floor, so a
        // -40 dBm carrier on a -130 dBm floor converges to exactly -85.0 and
        // no number of further pushes moves it. A mean-based downsample would
        // land near -125.8 instead, so -100 discriminates with 15 dB of margin
        // above the true value and 25 dB below the broken one.
        QVERIFY2(peak > -100.0f,
                 qPrintable(QStringLiteral("carrier lost, peak=%1").arg(peak)));
    }

    // Broadband impulse noise lasting one frame is rejected by median-of-3.
    void singleFrameImpulse_isRejected() {
        DssRenderer r;
        const QVector<float> quiet = flatRow(768, -130.0f);
        r.pushRow(quiet, 14.2, 0.192);
        r.pushRow(quiet, 14.2, 0.192);
        r.pushRow(flatRow(768, -20.0f), 14.2, 0.192);   // one-frame burst
        const float* row = r.rowDataRing(r.headRing());
        QVERIFY2(row[384] < -100.0f,
                 qPrintable(QStringLiteral("impulse not rejected: %1")
                                .arg(row[384])));
    }

    void ringWrap_keepsCountBounded() {
        DssRenderer r;
        for (int i = 0; i < kDssRows + 20; ++i) {
            r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);
        }
        QCOMPARE(r.rowCount(), kDssRows);
        QCOMPARE(r.visibleRowCount(), kDssVisibleRows);
        QVERIFY(r.headRing() >= 0 && r.headRing() < kDssRows);
    }

    void pushRow_stampsFrequencyFrame() {
        DssRenderer r;
        r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);
        QCOMPARE(r.rowCenterMhzAtAge(0),    14.2);
        QCOMPARE(r.rowBandwidthMhzAtAge(0), 0.192);
        r.pushRow(flatRow(768, -130.0f), 7.1, 0.096);
        QCOMPARE(r.rowCenterMhzAtAge(0),    7.1);
        QCOMPARE(r.rowCenterMhzAtAge(1),    14.2);
        QCOMPARE(r.rowBandwidthMhzAtAge(1), 0.192);
    }

    // A retune must not blend the new row against the previous one: adjacent
    // bin indices no longer represent the same frequencies, and blending
    // smears every signal in the direction of the pan.
    void frameChange_dropsTemporalBlend() {
        DssRenderer r;
        for (int i = 0; i < 4; ++i) {
            r.pushRow(flatRow(768, -60.0f), 14.2, 0.192);
        }
        r.pushRow(flatRow(768, -130.0f), 21.0, 0.192);   // retune
        const float* row = r.rowDataRing(r.headRing());
        QVERIFY2(row[384] < -125.0f,
                 qPrintable(QStringLiteral("blended across retune: %1")
                                .arg(row[384])));
    }

    void pushRow_withoutWide_marksWideUncovered() {
        DssRenderer r;
        r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);
        const quint8* cov = r.rowWideCoverageRing(r.headRing());
        QCOMPARE(cov[0],   quint8(0));
        QCOMPARE(cov[384], quint8(0));
        QCOMPARE(r.newestWideBandwidthMhz(0.192), 0.0);
    }

    void pushRowWithWide_recordsBothChannels() {
        DssRenderer r;
        r.pushRowWithWide(flatRow(768, -130.0f), 14.2, 0.192,
                          flatRow(768, -140.0f), 14.2, 0.500);
        const quint8* exact = r.rowCoverageRing(r.headRing());
        const quint8* wide  = r.rowWideCoverageRing(r.headRing());
        QCOMPARE(exact[384], quint8(1));
        QCOMPARE(wide[384],  quint8(1));
        QCOMPARE(r.rowWideBandwidthMhzAtAge(0), 0.500);
        QCOMPARE(r.newestWideBandwidthMhz(0.192), 0.500);
    }

    // The front row is deliberately not authoritative for the wide channel:
    // a producer that appends without a wide slice must not collapse the
    // overhang to nothing while rows carrying it are still on screen.
    void newestWideBandwidth_looksPastBareFrontRows() {
        DssRenderer r;
        r.pushRowWithWide(flatRow(768, -130.0f), 14.2, 0.192,
                          flatRow(768, -140.0f), 14.2, 0.500);
        r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);   // no wide slice
        QCOMPARE(r.newestWideBandwidthMhz(0.192), 0.500);
    }

    void clear_resetsEverything() {
        DssRenderer r;
        r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);
        QVERIFY(r.hasData());
        r.clear();
        QVERIFY(!r.hasData());
        QCOMPARE(r.rowCount(), 0);
    }

    // clear() must also wipe the per-row frequency stamps, because
    // ringAtAge() clamps to ring bounds WITHOUT the m_count guard that
    // upstream's age accessors carry. Without the wipe, a post-clear read
    // hands back a stale stamp.
    //
    // Filling the whole ring first is what makes this test bind, and it is
    // not optional. m_head walks BACKWARD on push and clear() resets it to 0,
    // so after a single push the written slot is index kDssRows-1 while
    // ringAtAge(0) reads index 0 -- a slot that was never written and is
    // already value-initialised to 0.0. The assertions would then pass
    // against a clear() that wipes nothing at all. Verified by mutation:
    // with the fills commented out, the single-push form still reported
    // 13/13 passing. Push kDssRows rows so every slot carries the stamp and
    // index 0 is genuinely dirty.
    void clear_wipesFrameStampsAcrossTheWholeRing() {
        DssRenderer r;
        for (int i = 0; i < kDssRows; ++i) {
            r.pushRowWithWide(flatRow(768, -130.0f), 14.2, 0.192,
                              flatRow(768, -140.0f), 14.2, 0.500);
        }
        QCOMPARE(r.rowCenterMhzAtAge(0), 14.2);   // precondition: slot is dirty
        r.clear();
        QCOMPARE(r.rowCenterMhzAtAge(0),        0.0);
        QCOMPARE(r.rowBandwidthMhzAtAge(0),     0.0);
        QCOMPARE(r.rowWideCenterMhzAtAge(0),    0.0);
        QCOMPARE(r.rowWideBandwidthMhzAtAge(0), 0.0);
        QCOMPARE(r.rowWideCoverageRing(r.headRing())[0], quint8(0));
    }

    void rowGeneration_advancesOnEveryPush() {
        DssRenderer r;
        const quint64 before = r.rowGeneration();
        r.pushRow(flatRow(768, -130.0f), 14.2, 0.192);
        QVERIFY(r.rowGeneration() > before);
    }
};

QTEST_APPLESS_MAIN(TestDssRendererRing)
#include "tst_dss_renderer_ring.moc"
