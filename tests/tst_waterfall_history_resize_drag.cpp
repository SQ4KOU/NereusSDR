// SPDX-License-Identifier: GPL-3.0-or-later
// NereusSDR — Copyright (C) 2026 JJ Boyd / KG4VCF
//
// NereusSDR-original. Dragging a dock edge (or the window's side border)
// changes the spectrum's width on every mouse move while waterfall rows
// keep arriving. The rewind history is plot width by up to 16384 rows, so
// rescaling it is far too heavy to do once per row; the 250 ms resize
// debounce exists to do it once, after the drag rests. These tests count
// whole-history rescales through a drag and check that the rows captured
// before and during the drag are still in the history afterward.

#include <QElapsedTimer>
#include <QImage>
#include <QTest>
#include <QVector>

#include <array>

#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

constexpr int kBins = 768;
constexpr float kColdDbm = -140.0f;
constexpr float kHotDbm = -20.0f;

// A row that is cold everywhere but one quarter of its width. Which quarter
// is hot names the row, at any width it is later resampled to.
QVector<float> quarterRow(int hotQuarter)
{
    QVector<float> bins(kBins, kColdDbm);
    const int from = hotQuarter * kBins / 4;
    const int to = (hotQuarter + 1) * kBins / 4;
    for (int i = from; i < to; ++i) {
        bins[i] = kHotDbm;
    }
    return bins;
}

// The pixel at the middle of each quarter of a scanline.
std::array<QRgb, 4> quarterMiddles(const QImage& image, int y)
{
    std::array<QRgb, 4> out{};
    const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
    for (int q = 0; q < 4; ++q) {
        out[q] = line[image.width() * (2 * q + 1) / 8];
    }
    return out;
}

// Which quarter of the row is the hot one, or -1 when the row does not read
// as exactly one hot quarter and three cold ones.
int hotQuarterOf(const std::array<QRgb, 4>& px, QRgb hot, QRgb cold)
{
    int found = -1;
    for (int q = 0; q < 4; ++q) {
        if (px[q] == hot) {
            if (found >= 0) {
                return -1;
            }
            found = q;
        } else if (px[q] != cold) {
            return -1;
        }
    }
    return found;
}

int historyRowForAge(const SpectrumWidget& w, int ageRows)
{
    const QImage& history = w.waterfallHistoryForTest();
    return (w.waterfallHistoryWriteRowForTest() + ageRows) % history.height();
}

}  // namespace

class TestWaterfallHistoryResizeDrag : public QObject {
    Q_OBJECT

private:
    // A shown widget with the ticker held, fixed waterfall levels (so a row's
    // colors depend on that row alone) and some rows already in the history.
    void prepare(SpectrumWidget& w, int width, int height, int rowsBefore)
    {
        w.setWaterfallTickerPausedForTest(true);
        w.setWfAgcEnabled(false);
        w.setWfLowThresholdForTest(-130.0f);
        w.setWfHighThresholdForTest(-40.0f);
        w.resize(width, height);
        w.show();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(!w.liveWaterfallForTest().isNull());
        // Let the debounce from the first show finish, so the drag below
        // starts from a history at rest.
        w.pushWaterfallRowForTest(quarterRow(0));
        QTRY_VERIFY(!w.waterfallHistoryResizePendingForTest());
        for (int i = 1; i < rowsBefore; ++i) {
            w.pushWaterfallRowForTest(quarterRow(i % 4));
        }
        QCOMPARE(w.waterfallHistoryRowsForTest(), rowsBefore);
        QCOMPARE(w.waterfallHistoryForTest().width(),
                 w.liveWaterfallForTest().width());
    }

private slots:
    // The fault: one whole-history rescale for every row that arrived while
    // the width was changing. The cure: none during the drag, one after it.
    void widthDrag_rescalesTheHistoryOnceAfterItRests()
    {
        constexpr int kRowsBefore = 40;
        constexpr int kSteps = 60;
        SpectrumWidget w;
        prepare(w, 1100, 500, kRowsBefore);
        if (QTest::currentTestFailed()) {
            return;
        }

        const int startWidth = w.liveWaterfallForTest().width();
        const quint64 rescalesBefore = w.waterfallHistoryRescalesForTest();
        const QImage& history = w.waterfallHistoryForTest();
        const QRgb hot = quarterMiddles(history, historyRowForAge(w, 0))
            [(kRowsBefore - 1) % 4];
        const QRgb cold = quarterMiddles(history, historyRowForAge(w, 0))
            [kRowsBefore % 4];
        QVERIFY(hot != cold);

        QElapsedTimer clock;
        clock.start();
        for (int step = 1; step <= kSteps; ++step) {
            w.resize(1100 - 3 * step, 500);
            w.pushWaterfallRowForTest(quarterRow(step % 4));
        }
        const qint64 dragMs = clock.elapsed();
        qInfo().nospace() << "width drag: " << kSteps << " steps in " << dragMs
                          << " ms (" << double(dragMs) / kSteps
                          << " ms per step), history "
                          << history.width() << " x " << history.height();

        const int endWidth = w.liveWaterfallForTest().width();
        QCOMPARE(endWidth, startWidth - 3 * kSteps);
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(0));
        // The history keeps its width until the drag rests.
        QCOMPARE(w.waterfallHistoryForTest().width(), startWidth);
        QCOMPARE(w.waterfallHistoryRowsForTest(), kRowsBefore + kSteps);
        QVERIFY(w.waterfallHistoryResizePendingForTest());

        QTRY_VERIFY(!w.waterfallHistoryResizePendingForTest());
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(1));
        QCOMPARE(w.waterfallHistoryForTest().width(), endWidth);
        QCOMPARE(w.waterfallHistoryRowsForTest(), kRowsBefore + kSteps);

        // Every row is still there and still reads as itself: the rows
        // captured during the drag (each at a different width) and the rows
        // from before it.
        for (int age = 0; age < kSteps; ++age) {
            const int step = kSteps - age;
            QCOMPARE(hotQuarterOf(quarterMiddles(w.waterfallHistoryForTest(),
                                                 historyRowForAge(w, age)),
                                  hot, cold),
                     step % 4);
        }
        for (int age = kSteps; age < kSteps + kRowsBefore; ++age) {
            const int i = kRowsBefore - 1 - (age - kSteps);
            QCOMPARE(hotQuarterOf(quarterMiddles(w.waterfallHistoryForTest(),
                                                 historyRowForAge(w, age)),
                                  hot, cold),
                     i % 4);
        }

        // The settle rebuilt the live picture from the history, newest first.
        const QImage& live = w.liveWaterfallForTest();
        const int shown = qMin(live.height(), kSteps + kRowsBefore);
        for (int y = 0; y < shown; ++y) {
            QCOMPARE(quarterMiddles(live, y),
                     quarterMiddles(w.waterfallHistoryForTest(),
                                    historyRowForAge(w, y)));
        }
    }

    // A row written during the drag lands in the history at the history's
    // width, across the whole row: no stale pixels past a narrower live row
    // and no read past the end of a wider history row.
    void rowDuringDrag_fillsTheHistoryRowAtTheHistoryWidth()
    {
        SpectrumWidget w;
        prepare(w, 900, 400, 8);
        if (QTest::currentTestFailed()) {
            return;
        }
        const int startWidth = w.liveWaterfallForTest().width();

        for (int widgetWidth : {600, 1300}) {
            w.resize(widgetWidth, 400);
            const QImage& live = w.liveWaterfallForTest();
            QVERIFY(live.width() != startWidth);
            w.pushWaterfallRowForTest(quarterRow(2));

            const QImage& history = w.waterfallHistoryForTest();
            QCOMPARE(history.width(), startWidth);
            const auto* liveRow = reinterpret_cast<const QRgb*>(
                live.constScanLine(w.liveWaterfallWriteRowForTest()));
            const auto* historyRow = reinterpret_cast<const QRgb*>(
                history.constScanLine(historyRowForAge(w, 0)));
            for (int x = 0; x < history.width(); ++x) {
                const int src = int(qint64(x) * live.width() / history.width());
                if (historyRow[x] != liveRow[src]) {
                    QFAIL(qPrintable(QStringLiteral(
                        "history pixel %1 differs from live pixel %2 at widget width %3")
                        .arg(x).arg(src).arg(widgetWidth)));
                }
            }
        }
    }

    // Paused in scrollback, every arriving row rebuilds the picture from
    // the history. That rebuild must not rescale the history either, and
    // the paused picture must stay whole through the drag.
    void widthDragWhilePaused_rescalesOnceAndKeepsThePicture()
    {
        constexpr int kRowsBefore = 400;
        constexpr int kSteps = 40;
        SpectrumWidget w;
        prepare(w, 1100, 400, kRowsBefore);
        if (QTest::currentTestFailed()) {
            return;
        }
        QVERIFY(w.liveWaterfallForTest().height() < kRowsBefore);
        const QImage& history = w.waterfallHistoryForTest();
        const QRgb hot = quarterMiddles(history, historyRowForAge(w, 0))
            [(kRowsBefore - 1) % 4];
        const QRgb cold = quarterMiddles(history, historyRowForAge(w, 0))
            [kRowsBefore % 4];
        QVERIFY(hot != cold);

        w.setWaterfallLiveForTest(false);
        const int startWidth = w.liveWaterfallForTest().width();
        const quint64 rescalesBefore = w.waterfallHistoryRescalesForTest();

        // The paused picture: row y shows the history row of age offset + y.
        auto pictureMatchesHistory = [&]() {
            const QImage& live = w.liveWaterfallForTest();
            const int offset = w.waterfallHistoryOffsetRowsForTest();
            for (int y = 0; y < live.height(); ++y) {
                const int shown = hotQuarterOf(quarterMiddles(live, y), hot, cold);
                const int kept = hotQuarterOf(
                    quarterMiddles(w.waterfallHistoryForTest(),
                                   historyRowForAge(w, offset + y)),
                    hot, cold);
                if (shown < 0 || shown != kept) {
                    qWarning() << "picture row" << y << "shows quarter" << shown
                               << "history holds" << kept;
                    return false;
                }
            }
            return true;
        };

        QElapsedTimer clock;
        clock.start();
        for (int step = 1; step <= kSteps; ++step) {
            w.resize(1100 - 3 * step, 400);
            w.pushWaterfallRowForTest(quarterRow(step % 4));
            QCOMPARE(w.waterfallHistoryOffsetRowsForTest(), step);
            if (step % 10 == 0) {
                QVERIFY(pictureMatchesHistory());
            }
        }
        const qint64 dragMs = clock.elapsed();
        qInfo().nospace() << "paused width drag: " << kSteps << " steps in "
                          << dragMs << " ms (" << double(dragMs) / kSteps
                          << " ms per step)";

        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(0));
        QCOMPARE(w.waterfallHistoryForTest().width(), startWidth);

        QTRY_VERIFY(!w.waterfallHistoryResizePendingForTest());
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(1));
        QCOMPARE(w.waterfallHistoryForTest().width(),
                 w.liveWaterfallForTest().width());
        QCOMPARE(w.waterfallHistoryOffsetRowsForTest(), kSteps);
        QVERIFY(pictureMatchesHistory());
    }

    // While an edge moves, the live waterfall is refilled from the history
    // on every step. It used to be emptied to its background on each step
    // and stay dark until the debounce, 250 ms after the drag came to rest.
    void drag_keepsTheLiveWaterfallLit_data()
    {
        QTest::addColumn<int>("widthStep");
        QTest::addColumn<int>("heightStep");
        QTest::newRow("width") << -3 << 0;
        QTest::newRow("height") << 0 << -3;
        QTest::newRow("both, growing") << 4 << 2;
    }

    void drag_keepsTheLiveWaterfallLit()
    {
        QFETCH(int, widthStep);
        QFETCH(int, heightStep);
        constexpr int kRowsBefore = 600;
        constexpr int kSteps = 30;
        SpectrumWidget w;
        prepare(w, 1000, 500, kRowsBefore);
        if (QTest::currentTestFailed()) {
            return;
        }
        const QImage& history = w.waterfallHistoryForTest();
        const QRgb hot = quarterMiddles(history, historyRowForAge(w, 0))
            [(kRowsBefore - 1) % 4];
        const QRgb cold = quarterMiddles(history, historyRowForAge(w, 0))
            [kRowsBefore % 4];
        QVERIFY(hot != cold);
        const quint64 rescalesBefore = w.waterfallHistoryRescalesForTest();

        // Every row of the live picture shows the history row of its age.
        auto litFromHistory = [&]() {
            const QImage& live = w.liveWaterfallForTest();
            const int top = w.liveWaterfallWriteRowForTest();
            for (int age = 0; age < live.height(); ++age) {
                const int y = (top + age) % live.height();
                const int shown = hotQuarterOf(quarterMiddles(live, y), hot, cold);
                const int kept = hotQuarterOf(
                    quarterMiddles(w.waterfallHistoryForTest(),
                                   historyRowForAge(w, age)),
                    hot, cold);
                if (shown < 0 || shown != kept) {
                    qWarning() << "live row of age" << age << "shows quarter"
                               << shown << "history holds" << kept;
                    return false;
                }
            }
            return true;
        };

        for (int step = 1; step <= kSteps; ++step) {
            w.resize(1000 + widthStep * step, 500 + heightStep * step);
            QVERIFY(w.liveWaterfallForTest().height() < kRowsBefore);
            QVERIFY2(litFromHistory(), "right after the resize step");
            w.pushWaterfallRowForTest(quarterRow(step % 4));
            QVERIFY2(litFromHistory(), "after the next row");
        }
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(0));

        QTRY_VERIFY(!w.waterfallHistoryResizePendingForTest());
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore,
                 quint64(widthStep != 0 ? 1 : 0));
        QVERIFY(litFromHistory());
        QCOMPARE(w.waterfallHistoryRowsForTest(), kRowsBefore + kSteps);
    }

    // A height-only change (a top or bottom dock edge) leaves the history's
    // size alone: no rescale then or later.
    void heightDrag_neverRescalesTheHistory()
    {
        SpectrumWidget w;
        prepare(w, 900, 500, 8);
        if (QTest::currentTestFailed()) {
            return;
        }
        const quint64 rescalesBefore = w.waterfallHistoryRescalesForTest();
        for (int step = 1; step <= 20; ++step) {
            w.resize(900, 500 - 4 * step);
            w.pushWaterfallRowForTest(quarterRow(step % 4));
        }
        QTRY_VERIFY(!w.waterfallHistoryResizePendingForTest());
        QCOMPARE(w.waterfallHistoryRescalesForTest() - rescalesBefore, quint64(0));
        QCOMPARE(w.waterfallHistoryRowsForTest(), 28);
    }
};

QTEST_MAIN(TestWaterfallHistoryResizeDrag)
#include "tst_waterfall_history_resize_drag.moc"
