// no-port-check: NereusSDR-original. See StationTelemetry.h.
#include "core/session/StationTelemetry.h"

#include <array>
#include <cmath>
#include <limits>

namespace NereusSDR {
namespace {
constexpr double kMaxExactJsonInteger = 9007199254740991.0;

bool integer(const QJsonValue& value, double maximum, qint64* result)
{
    if (!value.isDouble()) { return false; }
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < 0 || number > maximum
        || std::floor(number) != number) {
        return false;
    }
    *result = static_cast<qint64>(number);
    return true;
}

bool optionalInteger(const QJsonObject& object, const QString& key,
                     std::optional<qint64>* result)
{
    if (!object.contains(key)) { result->reset(); return true; }
    qint64 number = 0;
    if (!integer(object.value(key), kMaxExactJsonInteger, &number)) { return false; }
    *result = number;
    return true;
}

bool optionalRate(const QJsonObject& object, const QString& key,
                  std::optional<double>* result)
{
    if (!object.contains(key)) { result->reset(); return true; }
    const QJsonValue value = object.value(key);
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() < 0) {
        return false;
    }
    *result = value.toDouble();
    return true;
}

using AudioMember = std::optional<double> StationAudioTelemetry::*;
constexpr std::array<std::pair<const char*, AudioMember>, 6> kAudioRates{{
    {"sourceFramesPerSecond", &StationAudioTelemetry::sourceFramesPerSecond},
    {"sourceDropsPerSecond", &StationAudioTelemetry::sourceDropsPerSecond},
    {"encodedPacketsPerSecond", &StationAudioTelemetry::encodedPacketsPerSecond},
    {"encodeFailuresPerSecond", &StationAudioTelemetry::encodeFailuresPerSecond},
    {"sendAcceptedPerSecond", &StationAudioTelemetry::sendAcceptedPerSecond},
    {"sendRejectedPerSecond", &StationAudioTelemetry::sendRejectedPerSecond},
}};

bool putRate(QJsonObject& object, const QString& key, std::optional<double> value)
{
    if (!value) { return true; }
    if (!std::isfinite(*value) || *value < 0) { return false; }
    object.insert(key, *value);
    return true;
}
}

bool StationTelemetryCodec::decode(const QJsonObject& object,
                                   StationTelemetrySnapshot* snapshot)
{
    if (!snapshot) { return false; }
    StationTelemetrySnapshot decoded;
    qint64 sequence = 0;
    if (!integer(object.value(QStringLiteral("sequence")),
                 std::numeric_limits<quint32>::max(), &sequence)
        || sequence == 0
        || !integer(object.value(QStringLiteral("sampledElapsedMs")),
                    kMaxExactJsonInteger, &decoded.sampledElapsedMs)
        || !object.value(QStringLiteral("radio")).isObject()
        || !object.value(QStringLiteral("audio")).isObject()) {
        return false;
    }
    decoded.sequence = static_cast<quint32>(sequence);
    const QJsonObject radio = object.value(QStringLiteral("radio")).toObject();
    const QJsonObject audio = object.value(QStringLiteral("audio")).toObject();
    qint64 context = 0;
    if (!radio.value(QStringLiteral("connected")).isBool()
        || !audio.value(QStringLiteral("active")).isBool()
        || !integer(audio.value(QStringLiteral("contextGeneration")),
                    std::numeric_limits<quint32>::max(), &context)) {
        return false;
    }
    decoded.radio.connected = radio.value(QStringLiteral("connected")).toBool();
    decoded.audio.active = audio.value(QStringLiteral("active")).toBool();
    decoded.audio.contextGeneration = static_cast<quint32>(context);
    if (decoded.audio.active && context == 0) { return false; }
    if (!optionalRate(radio, QStringLiteral("rxMbps"), &decoded.radio.rxMbps)
        || !optionalRate(radio, QStringLiteral("txMbps"), &decoded.radio.txMbps)
        || !optionalInteger(radio, QStringLiteral("rttMs"), &decoded.radio.rttMs)
        || !optionalInteger(radio, QStringLiteral("rttAgeMs"), &decoded.radio.rttAgeMs)
        || decoded.radio.rttMs.has_value() != decoded.radio.rttAgeMs.has_value()) {
        return false;
    }
    if (!decoded.radio.connected && (decoded.radio.rxMbps || decoded.radio.txMbps
                                     || decoded.radio.rttMs)) {
        return false;
    }
    for (const auto& [name, member] : kAudioRates) {
        if (!optionalRate(audio, QString::fromLatin1(name), &(decoded.audio.*member))
            || (!decoded.audio.active && (decoded.audio.*member).has_value())) {
            return false;
        }
    }
    *snapshot = decoded;
    return true;
}

std::optional<QJsonObject> StationTelemetryCodec::encode(
    const StationTelemetrySnapshot& snapshot)
{
    QJsonObject radio{{QStringLiteral("connected"), snapshot.radio.connected}};
    if (!putRate(radio, QStringLiteral("rxMbps"), snapshot.radio.rxMbps)
        || !putRate(radio, QStringLiteral("txMbps"), snapshot.radio.txMbps)) {
        return std::nullopt;
    }
    if (snapshot.radio.rttMs) {
        radio.insert(QStringLiteral("rttMs"), *snapshot.radio.rttMs);
    }
    if (snapshot.radio.rttAgeMs) {
        radio.insert(QStringLiteral("rttAgeMs"), *snapshot.radio.rttAgeMs);
    }
    QJsonObject audio{{QStringLiteral("active"), snapshot.audio.active},
                      {QStringLiteral("contextGeneration"),
                       static_cast<qint64>(snapshot.audio.contextGeneration)}};
    for (const auto& [name, member] : kAudioRates) {
        if (!putRate(audio, QString::fromLatin1(name), snapshot.audio.*member)) {
            return std::nullopt;
        }
    }
    const QJsonObject object{
        {QStringLiteral("sequence"), static_cast<qint64>(snapshot.sequence)},
        {QStringLiteral("sampledElapsedMs"), snapshot.sampledElapsedMs},
        {QStringLiteral("radio"), radio},
        {QStringLiteral("audio"), audio}};
    StationTelemetrySnapshot checked;
    if (!decode(object, &checked)) { return std::nullopt; }
    return object;
}
} // namespace NereusSDR
