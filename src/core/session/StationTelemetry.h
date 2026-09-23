#pragma once
// no-port-check: NereusSDR-original. Observational Core/GUI telemetry (R-R3-32).

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <optional>

namespace NereusSDR {

// Units and ownership are part of the wire contract. An absent value has not
// been measured; a present zero is an actual measurement. No field grants
// admission, changes radio state or substitutes for heartbeat evidence.
struct StationRadioTelemetry {
    bool connected = false;
    std::optional<double> rxMbps;
    std::optional<double> txMbps;
    std::optional<qint64> rttMs;
    std::optional<qint64> rttAgeMs;
};

struct StationAudioTelemetry {
    bool active = false;
    quint32 contextGeneration = 0;
    std::optional<double> sourceFramesPerSecond;
    std::optional<double> sourceDropsPerSecond;
    std::optional<double> encodedPacketsPerSecond;
    std::optional<double> encodeFailuresPerSecond;
    // Acceptance by the media transport is NOT evidence of delivery.
    std::optional<double> sendAcceptedPerSecond;
    std::optional<double> sendRejectedPerSecond;
};

// Longest thermal zone name the codec accepts. Linux caps a zone type at
// 20 characters; the sampler truncates anything longer to this.
inline constexpr qsizetype kMaxHostZoneNameLength = 64;

// The Core computer's own load (R-R3-32, R-R3-33). Sent only to a peer that
// negotiated host telemetry (session minor 10, stationTelemetryVersion 2);
// a Core that does not measure a value, including every non-Linux Core,
// leaves it absent. On the wire the section is omitted when nothing in it
// was measured.
struct StationHostTelemetry {
    std::optional<double> systemCpuPercent;   // all CPUs, 0-100
    std::optional<double> processCpuPercent;  // nereusd share of all CPUs, 0-100
    std::optional<qint64> memoryAvailableKiB; // MemAvailable
    std::optional<qint64> memoryTotalKiB;     // MemTotal
    std::optional<qint64> processResidentKiB; // VmRSS
    std::optional<double> hottestZoneCelsius;
    QString hottestZoneName;                  // empty when absent

    bool isEmpty() const
    {
        return !systemCpuPercent && !processCpuPercent && !memoryAvailableKiB
            && !memoryTotalKiB && !processResidentKiB && !hottestZoneCelsius
            && hottestZoneName.isEmpty();
    }
};

// Most receivers the codec accepts in one sample. Slice IDs are WDSP channel
// ids, below WdspEngine::kMaxSliceChannels (5); the bound leaves room without
// letting a peer send an unbounded list.
inline constexpr qsizetype kMaxStationReceivers = 16;

// One receiver's processing on the Core over its latest load interval
// (R-R3-40, R-R3-32, R-R3-33). Sent only to a peer that negotiated receiver
// load (session minor 11, stationTelemetryVersion 3).
struct StationReceiverTelemetry {
    int sliceId = 0;                    // 0-65535, unique within a sample
    // Mean processing time per block as a percentage of the block's real
    // time: 100 means the receiver cannot keep up. Absent when the receiver
    // processed nothing in the interval, which is not proof of no load
    // (ReceiverDspLoad::idle).
    std::optional<double> loadPercent;
    qint64 inputDelayMs = 0;            // wait of its latest input batch
    qint64 skippedInputMs = 0;          // input skipped to bound that wait,
                                        // total since the Core started
                                        // processing its receivers
};

struct StationTelemetrySnapshot {
    quint32 sequence = 0;
    // Relative to the producer's session clock. Never subtract from a GUI
    // clock to claim one-way latency; receipt age is measured locally.
    qint64 sampledElapsedMs = 0;
    StationRadioTelemetry radio;
    StationAudioTelemetry audio;
    StationHostTelemetry host;
    // Absent: the Core did not measure its receivers (an older Core, or one
    // with no radio model). Present and empty: it measured, and no receiver
    // had a load reading yet. On the wire an absent list omits "receivers".
    std::optional<QVector<StationReceiverTelemetry>> receivers;
};

namespace StationTelemetryCodec {
// Both functions reject invalid required fields and invalid present optional
// values. Unknown optional JSON fields are ignored. Decode is transactional.
std::optional<QJsonObject> encode(const StationTelemetrySnapshot& snapshot);
bool decode(const QJsonObject& object, StationTelemetrySnapshot* snapshot);
}

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::StationTelemetrySnapshot)
