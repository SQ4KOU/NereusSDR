#pragma once
// no-port-check: NereusSDR-original. Observational Core/GUI telemetry (R-R3-32).

#include <QJsonObject>
#include <QMetaType>
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

struct StationTelemetrySnapshot {
    quint32 sequence = 0;
    // Relative to the producer's session clock. Never subtract from a GUI
    // clock to claim one-way latency; receipt age is measured locally.
    qint64 sampledElapsedMs = 0;
    StationRadioTelemetry radio;
    StationAudioTelemetry audio;
};

namespace StationTelemetryCodec {
// Both functions reject invalid required fields and invalid present optional
// values. Unknown optional JSON fields are ignored. Decode is transactional.
std::optional<QJsonObject> encode(const StationTelemetrySnapshot& snapshot);
bool decode(const QJsonObject& object, StationTelemetrySnapshot* snapshot);
}

} // namespace NereusSDR

Q_DECLARE_METATYPE(NereusSDR::StationTelemetrySnapshot)
