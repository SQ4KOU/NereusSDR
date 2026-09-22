// =================================================================
// tests/tst_telemetry_history.cpp  (NereusSDR)
// =================================================================
// R-R3-32/33 GUI telemetry-history regressions. The history is deliberately
// protocol-neutral: these tests use only monotonic timestamps, segments and
// optional values, never a station transport or media receiver.
// =================================================================

#include <QtTest/QtTest>
#include <limits>
#include <cmath>

#include "gui/TelemetryHistory.h"

using namespace NereusSDR;

namespace {

TelemetryHistory::Sample sample(qint64 timeMs, TelemetryHistory::Segment segment,
                                TelemetryHistory::Metric metric,
                                std::optional<double> value)
{
    TelemetryHistory::Sample result;
    result.monotonicMs = timeMs;
    result.sessionSegment = segment;
    result.values[static_cast<std::size_t>(metric)] = value;
    return result;
}

const TelemetryHistory::Point* pointWithValue(const TelemetryHistory::Series& series,
                                              double value)
{
    for (const TelemetryHistory::Point& point : series.points) {
        if (qFuzzyCompare(point.value + 1.0, value + 1.0)) {
            return &point;
        }
    }
    return nullptr;
}

} // namespace

class TstTelemetryHistory : public QObject {
    Q_OBJECT

private slots:
    void independentSourcesDoNotBreakEachOthersHistory()
    {
        TelemetryHistory history;
        constexpr auto radio = TelemetryHistory::Metric::RadioRxMbps;
        constexpr auto playback = TelemetryHistory::Metric::PlaybackDecodedPacketsPerSecond;
        TelemetryHistory::MetricMask radioOnly, playbackOnly;
        radioOnly.set(static_cast<std::size_t>(radio));
        playbackOnly.set(static_cast<std::size_t>(playback));
        history.append(sample(0, 1, radio, 2.0), radioOnly);
        history.append(sample(100, 1, playback, 25.0), playbackOnly);
        history.append(sample(1000, 1, radio, 3.0), radioOnly);
        const auto series = history.series(radio, 1000, 60);
        QCOMPARE(series.points.size(), 2);
        QVERIFY(!series.points.last().breakBefore);
    }

    void finiteInputsCannotOverflowCompactedMean()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::RadioRxMbps;
        const double high = std::numeric_limits<double>::max();
        for (int i = 0; i < 120; ++i) {
            history.append(sample(i * 1000, 1, metric, high));
        }
        const qint64 now = TelemetryHistory::kRawRetentionMs + 120000;
        history.append(sample(now, 1, metric, std::nullopt));
        const auto series = history.series(metric, now, 86400);
        QVERIFY(!series.points.isEmpty());
        for (const auto& point : series.points) {
            QVERIFY(std::isfinite(point.value));
            QCOMPARE(point.value, high);
        }
    }

    void missingDoesNotBecomeZeroAndForcesBreak()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::RadioRxMbps;
        history.append(sample(0, 1, metric, 5.0));
        history.append(sample(1000, 1, metric, std::nullopt));
        history.append(sample(2000, 1, metric, 7.0));

        const auto series = history.series(metric, 2000, 5 * 60);
        QCOMPARE(series.points.size(), 2);
        QCOMPARE(series.points[0].value, 5.0);
        QCOMPARE(series.points[1].value, 7.0);
        QVERIFY(series.points[1].breakBefore);
    }

    void zeroIsAValidMeasuredObservation()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::AudioSendRejectedPerSecond;
        history.append(sample(1000, 1, metric, 0.0));

        const auto series = history.series(metric, 1000, 5 * 60);
        QCOMPARE(series.points.size(), 1);
        QCOMPARE(series.points[0].value, 0.0);
    }

    void compactionKeepsObservationWeightsThroughQueryBucketing()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::SessionPayloadRxKbps;
        // Minute zero contains sixty continuous observations whose sum is 100;
        // minute one contains one further continuous observation of 100.
        for (int second = 0; second < 60; ++second) {
            history.append(sample(second * 1000, 1, metric, second == 59 ? 100.0 : 0.0));
        }
        history.append(sample(60000, 1, metric, 100.0));
        const qint64 trigger = TelemetryHistory::kRawRetentionMs + 61000;
        history.append(sample(trigger, 1, metric, 1.0));

        const auto series = history.series(metric, trigger, 7 * 24 * 60 * 60);
        const auto* weighted = pointWithValue(series, 200.0 / 61.0);
        QVERIFY2(weighted, "query aggregation must retain the compacted observations' weights");
    }

    void reconnectStartsNewLineEvenWhenTheTimeGapIsShort()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::PlaybackDecodedPacketsPerSecond;
        history.append(sample(0, 1, metric, 20.0));
        history.append(sample(1000, 1, metric, 21.0));
        history.append(sample(2000, 2, metric, 22.0));

        const auto series = history.series(metric, 2000, 5 * 60);
        QCOMPARE(series.points.size(), 3);
        QVERIFY(!series.points[1].breakBefore);
        QVERIFY(series.points[2].breakBefore);
    }

    void compactedMinuteCrossingBreakIsOmittedAndCannotGrowPerReconnect()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::PlaybackLatePacketsPerSecond;
        history.append(sample(0, 1, metric, 1.0));       // valid minute 0
        history.append(sample(60000, 1, metric, 2.0));
        history.append(sample(61000, 2, metric, 3.0));   // minute 1 crosses segment
        history.append(sample(4032000, 2, metric, 4.0)); // later long-query bucket
        const qint64 trigger = TelemetryHistory::kRawRetentionMs + 4033000;
        history.append(sample(trigger, 2, metric, 5.0)); // compacts minutes 0..2

        QCOMPARE(history.minuteObservationCount(metric), 3);
        const auto series = history.series(metric, trigger, 7 * 24 * 60 * 60);
        const auto* after = pointWithValue(series, 4.0);
        QVERIFY(after);
        QVERIFY(after->breakBefore);
        QVERIFY(!pointWithValue(series, 1.0));
        QVERIFY(!pointWithValue(series, 2.0));
        QVERIFY(!pointWithValue(series, 3.0));

        for (int i = 0; i < TelemetryHistory::kRawObservationCapacity * 3; ++i) {
            history.append(sample(trigger + 1 + i,
                                  static_cast<TelemetryHistory::Segment>(100 + i), metric, 1.0));
        }
        QVERIFY(history.rawObservationCount(metric) <= TelemetryHistory::kRawObservationCapacity);
        QVERIFY(history.minuteObservationCount(metric) <= TelemetryHistory::kMinuteObservationCapacity);

        const int rangeSeconds = 7 * 24 * 60 * 60;
        const auto bounded = history.series(metric, TelemetryHistory::kMinuteRetentionMs,
                                            rangeSeconds);
        QVERIFY(bounded.points.size()
                <= rangeSeconds * 1000LL / TelemetryHistory::bucketMsFor(rangeSeconds));
    }

    void sourceGapIsRetainedThroughCompactionAndQueryAggregation()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::PlaybackUnderflowsPerSecond;
        history.append(sample(0, 1, metric, 1.0));
        history.append(sample(65000, 1, metric, 2.0)); // No callbacks for > 3 periods.
        history.append(sample(4032000, 1, metric, 3.0));
        const qint64 trigger = TelemetryHistory::kRawRetentionMs + 4033000;
        history.append(sample(trigger, 1, metric, 4.0));

        const auto series = history.series(metric, trigger, 7 * 24 * 60 * 60);
        const auto* afterGap = pointWithValue(series, 3.0);
        QVERIFY(afterGap);
        QVERIFY(afterGap->breakBefore);
        QVERIFY(!pointWithValue(series, 1.0));
        QVERIFY(!pointWithValue(series, 2.0));
    }

    void forcedRecentMinuteCentreDoesNotEscapeTheVisibleRange()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::RadioTxMbps;
        for (int i = 0; i <= TelemetryHistory::kRawObservationCapacity; ++i) {
            history.append(sample(i * 2, 1, metric, 1.0));
        }
        const auto series = history.series(metric, 10000, 10 * 60);
        for (const auto& point : series.points) {
            QVERIFY(point.seconds >= 0.0);
            QVERIFY(point.seconds <= 10 * 60);
        }
    }

    void retentionDropsObservationsOlderThanSevenDays()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::SessionRttMs;
        history.append(sample(0, 1, metric, 1.0));
        const qint64 now = TelemetryHistory::kMinuteRetentionMs + 1000;
        history.append(sample(now, 1, metric, 2.0));

        QCOMPARE(history.minuteObservationCount(metric), 0);
        const auto series = history.series(metric, now, 5 * 60);
        QCOMPARE(series.points.size(), 1);
        QCOMPARE(series.points[0].value, 2.0);
    }

    void explicitPerMetricBreakStartsTheNextObservation()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::AudioSourceDropsPerSecond;
        history.append(sample(0, 1, metric, 1.0));
        history.breakMetric(metric);
        history.append(sample(1000, 1, metric, 2.0));

        const auto series = history.series(metric, 1000, 5 * 60);
        QCOMPARE(series.points.size(), 2);
        QVERIFY(series.points[1].breakBefore);
    }

    void longWindowPointsUseBucketCentres()
    {
        TelemetryHistory history;
        constexpr auto metric = TelemetryHistory::Metric::RadioRttMs;
        history.append(sample(6000, 1, metric, 12.0));

        // 10 minutes uses max(5 seconds, range / 300) = 5 seconds. The 6000 ms
        // observation belongs to [5000,10000), whose chart point is 7.5 s.
        const auto series = history.series(metric, 600000, 10 * 60);
        QCOMPARE(series.points.size(), 1);
        QCOMPARE(series.points[0].seconds, 7.5);
        QCOMPARE(series.points[0].value, 12.0);
    }
};

QTEST_GUILESS_MAIN(TstTelemetryHistory)
#include "tst_telemetry_history.moc"
