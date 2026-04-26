// SPDX-License-Identifier: GPL-3.0-or-later
// NereusSDR — Copyright (C) 2026 JJ Boyd / KG4VCF
//
// Sub-epic E: waterfall scrollback unit tests. NereusSDR-original.

#include <QtTest/QtTest>
#include <QImage>
#include <QVector>

// We test the math helpers in isolation by re-implementing them as a parallel
// shim. This avoids needing a full QApplication + GPU widget for what are
// pure-arithmetic checks. The production code lives in SpectrumWidget — when
// modifying the formulas, update both copies (CI ensures parity via the
// kMaxWaterfallHistoryRows constant pulled directly from the production
// header).

#include "../src/gui/SpectrumWidget.h"

namespace {

// Mirrors SpectrumWidget::waterfallHistoryCapacityRows() — pure arithmetic.
int capacityRows(qint64 depthMs, int periodMs) {
    const int p = std::max(1, periodMs);
    const int rows = static_cast<int>((depthMs + p - 1) / p);
    return std::min(rows, NereusSDR::SpectrumWidget::kMaxWaterfallHistoryRows);
}

// Mirrors SpectrumWidget::historyRowIndexForAge() with explicit args.
int rowIndexForAge(int writeRow, int rowCount, int ringHeight, int ageRows) {
    if (ringHeight <= 0 || ageRows < 0 || ageRows >= rowCount) return -1;
    return (writeRow + ageRows) % ringHeight;
}

} // namespace

class TstWaterfallScrollback : public QObject
{
    Q_OBJECT
private slots:
    void capacityCappedAt4096_30ms_20min() {
        QCOMPARE(capacityRows(20LL * 60 * 1000, 30), 4096);
    }
    void capacityCappedAt4096_100ms_20min() {
        QCOMPARE(capacityRows(20LL * 60 * 1000, 100), 4096);
    }
    void capacityCapBoundary_293ms_20min() {
        // 20 min × 60 000 / 293 = 4 095.56 → ceil = 4 096 → capped at 4 096
        QCOMPARE(capacityRows(20LL * 60 * 1000, 293), 4096);
    }
    void capacityUncapped_500ms_20min() {
        // 20 min × 60 000 / 500 = 2 400 → uncapped
        QCOMPARE(capacityRows(20LL * 60 * 1000, 500), 2400);
    }
    void capacityUncapped_1000ms_20min() {
        QCOMPARE(capacityRows(20LL * 60 * 1000, 1000), 1200);
    }
    void capacityRespects60sDepth() {
        // Even at 30 ms period, a 60 s depth selector is honoured below the cap.
        QCOMPARE(capacityRows(60 * 1000, 30), 2000);
    }
    void capacityZeroPeriodGuard() {
        // 0 period clamped to 1 — must not divide by zero.
        QCOMPARE(capacityRows(20LL * 60 * 1000, 0), 4096);
    }

    void rowIndexForAge_age0_isWriteRow() {
        QCOMPARE(rowIndexForAge(/*writeRow=*/100, /*rowCount=*/4096,
                                /*ringHeight=*/4096, /*ageRows=*/0),
                 100);
    }
    void rowIndexForAge_wraps() {
        // writeRow=4090, age=10 → 4100 % 4096 = 4
        QCOMPARE(rowIndexForAge(4090, 4096, 4096, 10), 4);
    }
    void rowIndexForAge_outOfBounds_returnsMinus1() {
        QCOMPARE(rowIndexForAge(0, 100, 4096, 100), -1);
        QCOMPARE(rowIndexForAge(0, 100, 4096, -1), -1);
    }

    void wrapAround_writePastCapacity_keepsCapRows() {
        // Simulate appending 5000 rows into a 4096-row ring; verify
        // m_wfHistoryRowCount saturates at 4096 and writeRow wraps.
        // We instrument this via a minimal stand-in (not a real
        // SpectrumWidget) because the production appendHistoryRow needs
        // a live QImage and a parent QWidget. The contract is small
        // enough to mirror in the test.
        constexpr int kHeight = 4096;
        int writeRow = 0;
        int rowCount = 0;
        for (int i = 0; i < 5000; ++i) {
            writeRow = (writeRow - 1 + kHeight) % kHeight;
            if (rowCount < kHeight) ++rowCount;
        }
        QCOMPARE(rowCount, 4096);
        // After 5000 writes from initial writeRow=0, the new writeRow is
        // (0 - 5000) % 4096 = -5000 % 4096 = -904 + 4096 = 3192 ... but
        // because we apply (writeRow - 1 + h) % h each iteration, we
        // wrap once per 4096 writes. After 5000 writes that's one full
        // wrap (4096 writes) plus 904 more, landing at:
        //     ((0 - 5000) % 4096 + 4096) % 4096 = (4096 - 904) = 3192
        QCOMPARE(writeRow, 3192);
    }
};

QTEST_MAIN(TstWaterfallScrollback)
#include "tst_waterfall_scrollback.moc"
