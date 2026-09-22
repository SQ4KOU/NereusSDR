// =================================================================
// src/gui/TelemetryHistory.h  (NereusSDR)
// =================================================================
//
// Ported and adapted from AetherSDR's in-dialog network-diagnostics history:
//   src/gui/NetworkDiagnosticsDialog.{h,cpp}, MemoryHistoryRing.h
//   @ 0dea0dd7d73e25a40c8c01d46873af5834e23921
//   https://github.com/ten9876/AetherSDR
// AetherSDR project lead: Jeremy Fielder (KK7GWY, ten9876).
// The upstream files have no top-of-file licence header; AetherSDR's
// project-level GNU GPLv3 LICENSE applies.  This Nereus adaptation is a
// protocol-neutral GUI history: it has no Core, StationClient, or media type.
//
// Modification history (NereusSDR):
//   2026-09-21 -- R-R3-32/33: added per-metric optional observations, session
//                 discontinuities, weighted compaction and fixed storage caps
//                 by J.J. Boyd (KG4VCF), with AI-assisted adaptation via
//                 OpenAI Codex.
// =================================================================

#pragma once

#include <array>
#include <bitset>
#include <cstddef>
#include <deque>
#include <optional>

#include <QVector>
#include <QtGlobal>

namespace NereusSDR {

class TelemetryHistory {
public:
    enum class Metric : quint8 {
        RadioRxMbps,
        RadioTxMbps,
        RadioRttMs,
        SessionPayloadRxKbps,
        SessionPayloadTxKbps,
        SessionRttMs,
        AudioSourceFramesPerSecond,
        AudioEncodedPacketsPerSecond,
        AudioSendAcceptedPerSecond,
        AudioSendRejectedPerSecond,
        AudioSourceDropsPerSecond,
        PlaybackDecodedPacketsPerSecond,
        PlaybackConcealedPacketsPerSecond,
        PlaybackLatePacketsPerSecond,
        PlaybackUnderflowsPerSecond,
        PlaybackOverflowsPerSecond,
        PlaybackPacketAgeMs,
        Count,
    };

    static constexpr std::size_t kMetricCount = static_cast<std::size_t>(Metric::Count);
    static constexpr qint64 kExpectedSampleMs = 1000;
    static constexpr qint64 kRawRetentionMs = 60LL * 60LL * 1000LL;
    static constexpr qint64 kMinuteRetentionMs = 7LL * 24LL * 60LL * 60LL * 1000LL;
    static constexpr int kRawObservationCapacity = 3600;
    static constexpr int kMinuteObservationCapacity = 7 * 24 * 60;

    using Segment = quint64;
    using MetricValues = std::array<std::optional<double>, kMetricCount>;
    using MetricMask = std::bitset<kMetricCount>;

    struct Sample {
        qint64 monotonicMs{0};
        Segment sessionSegment{0};
        MetricValues values{};
    };

    struct Point {
        double seconds{0.0};
        double value{0.0};
        bool breakBefore{false};
    };

    struct Series {
        QVector<Point> points;
        double maxConnectGapSeconds{0.0};
    };

    // Appends only finite optional observations. A missing or non-finite field
    // makes that metric's next observation start a new graph path; zero remains
    // a valid measured value and is never substituted for missing data.
    // Independent sources arrive at different times. Only updated metrics
    // participate; an updated-but-absent value still records a discontinuity.
    void append(const Sample& sample, MetricMask updated = MetricMask{}.set());

    // Records an explicit discontinuity without storing a synthetic point. It
    // bounds reconnect-only churn while ensuring the next real observation
    // starts a new path.
    void breakMetric(Metric metric);

    Series series(Metric metric, qint64 nowMonotonicMs, int rangeSeconds) const;

    static qint64 bucketMsFor(int rangeSeconds);
    static double connectGapSecondsFor(int rangeSeconds);

    // Compact introspection for diagnostics and bounded-storage tests.
    int rawObservationCount(Metric metric) const;
    int minuteObservationCount(Metric metric) const;

private:
    struct Observation {
        qint64 timestampMs{0};
        Segment segment{0};
        bool breakBefore{false};
        bool invalidMinute{false};
        double mean{0.0};
        quint64 weight{0};
    };

    struct MetricHistory {
        std::deque<Observation> raw;
        std::deque<Observation> minutes;
        std::optional<Segment> lastSegment;
        std::optional<qint64> lastObservationMs;
        bool pendingBreak{false};
    };

    static std::size_t metricIndex(Metric metric);
    static qint64 minuteStartMs(qint64 monotonicMs);

    void compactAndPrune(MetricHistory& history, qint64 latestMs);
    void compactOne(MetricHistory& history);
    static void appendMinute(MetricHistory& history, Observation observation);

    std::array<MetricHistory, kMetricCount> m_metrics;
    qint64 m_latestMonotonicMs{-1};
};

} // namespace NereusSDR
