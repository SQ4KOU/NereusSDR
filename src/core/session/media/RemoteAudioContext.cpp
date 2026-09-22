// =================================================================
// src/core/session/media/RemoteAudioContext.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See RemoteAudioContext.h.
// =================================================================

#include "core/session/media/RemoteAudioContext.h"

#include <cmath>
#include <initializer_list>
#include <limits>

namespace NereusSDR {
namespace {

constexpr qsizetype kLegacyContextKeys = 8;
constexpr qsizetype kDetailContextKeys = 9;
constexpr qsizetype kEncoderKeys = 6;
constexpr double kMaxU32 = static_cast<double>(std::numeric_limits<quint32>::max());
constexpr double kMaxSequence = static_cast<double>(std::numeric_limits<quint16>::max());
constexpr int kMinimumTargetBitrate = 6'000;
constexpr int kMaximumTargetBitrate = 510'000;

// An integral JSON number within [low, high]. Strings, booleans and
// fractional or non-finite values are refused, the same test the GUI's
// audio-context parser has always applied.
bool integral(const QJsonValue& value, double low, double high, double& result)
{
    if (!value.isDouble()) {
        return false;
    }
    result = value.toDouble();
    return std::isfinite(result) && result >= low && result <= high
        && std::floor(result) == result;
}

bool knownAudioBandwidthHz(int hz)
{
    for (int known : {4'000, 6'000, 8'000, 12'000, 20'000}) {
        if (hz == known) {
            return true;
        }
    }
    return false;
}

} // namespace

QString remoteAudioOffReasonToWire(RemoteAudioOffReason reason)
{
    switch (reason) {
    case RemoteAudioOffReason::ClientDisabled:
        return QStringLiteral("client-disabled");
    case RemoteAudioOffReason::MediaNotReady:
        return QStringLiteral("media-not-ready");
    case RemoteAudioOffReason::RadioOffline:
        return QStringLiteral("radio-offline");
    case RemoteAudioOffReason::EncoderUnavailable:
        return QStringLiteral("encoder-unavailable");
    }
    return {};
}

std::optional<RemoteAudioOffReason> remoteAudioOffReasonFromWire(const QJsonValue& value)
{
    if (!value.isString()) {
        return std::nullopt;
    }
    const QString wire = value.toString();
    for (RemoteAudioOffReason reason :
         {RemoteAudioOffReason::ClientDisabled, RemoteAudioOffReason::MediaNotReady,
          RemoteAudioOffReason::RadioOffline, RemoteAudioOffReason::EncoderUnavailable}) {
        if (wire == remoteAudioOffReasonToWire(reason)) {
            return reason;
        }
    }
    return std::nullopt;
}

QJsonObject remoteAudioEncoderToJson(const OpusEncoderProfile& profile)
{
    return {
        {QStringLiteral("codec"), QStringLiteral("opus")},
        {QStringLiteral("sampleRate"), static_cast<qint64>(profile.sampleRate)},
        {QStringLiteral("channels"), static_cast<qint64>(profile.channels)},
        {QStringLiteral("frameSamples"), static_cast<qint64>(profile.frameSamples)},
        {QStringLiteral("targetBitrate"), static_cast<qint64>(profile.targetBitrate)},
        {QStringLiteral("audioBandwidthHz"), static_cast<qint64>(profile.audioBandwidthHz)},
    };
}

std::optional<OpusEncoderProfile> remoteAudioEncoderFromJson(const QJsonValue& value)
{
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject object = value.toObject();
    const QJsonValue codec = object.value(QStringLiteral("codec"));
    double sampleRate = 0.0;
    double channels = 0.0;
    double frameSamples = 0.0;
    double targetBitrate = 0.0;
    double audioBandwidthHz = 0.0;
    // Six keys, each present and valid, is exactly the encoder key set.
    if (object.size() != kEncoderKeys || !codec.isString()
        || codec.toString() != QLatin1String("opus")
        || !integral(object.value(QStringLiteral("sampleRate")),
                     OpusAudioCodecConfig::kSampleRate, OpusAudioCodecConfig::kSampleRate,
                     sampleRate)
        || !integral(object.value(QStringLiteral("channels")),
                     OpusAudioCodecConfig::kChannels, OpusAudioCodecConfig::kChannels,
                     channels)
        || !integral(object.value(QStringLiteral("frameSamples")),
                     OpusAudioCodecConfig::kFrameSamples, OpusAudioCodecConfig::kFrameSamples,
                     frameSamples)
        || !integral(object.value(QStringLiteral("targetBitrate")),
                     kMinimumTargetBitrate, kMaximumTargetBitrate, targetBitrate)
        || !integral(object.value(QStringLiteral("audioBandwidthHz")),
                     4'000.0, 20'000.0, audioBandwidthHz)
        || !knownAudioBandwidthHz(static_cast<int>(audioBandwidthHz))) {
        return std::nullopt;
    }
    OpusEncoderProfile profile;
    profile.sampleRate = static_cast<int>(sampleRate);
    profile.channels = static_cast<int>(channels);
    profile.frameSamples = static_cast<int>(frameSamples);
    profile.targetBitrate = static_cast<int>(targetBitrate);
    profile.audioBandwidthHz = static_cast<int>(audioBandwidthHz);
    return profile;
}

QJsonObject encodeRemoteAudioContext(const RemoteAudioContextMessage& message,
                                     bool detailNegotiated)
{
    // The minor-7 context, key for key and number type for number type.
    QJsonObject payload{
        {QStringLiteral("op"), QStringLiteral("audio-context")},
        {QStringLiteral("connectionId"), message.connectionId},
        {QStringLiteral("revision"), static_cast<qint64>(message.revision)},
        {QStringLiteral("generation"), static_cast<qint64>(message.generation)},
        {QStringLiteral("enabled"), message.enabled},
        {QStringLiteral("ssrc"), static_cast<qint64>(message.ssrc)},
        {QStringLiteral("firstSequence"), static_cast<qint64>(message.firstSequence)},
        {QStringLiteral("firstTimestamp"), static_cast<qint64>(message.firstTimestamp)},
    };
    if (!detailNegotiated) {
        return payload;
    }
    if (message.enabled && message.encoder) {
        payload.insert(QStringLiteral("encoder"), remoteAudioEncoderToJson(*message.encoder));
    } else if (!message.enabled && message.offReason) {
        payload.insert(QStringLiteral("reason"), remoteAudioOffReasonToWire(*message.offReason));
    }
    return payload;
}

std::optional<RemoteAudioContextMessage> decodeRemoteAudioContext(const QJsonObject& payload,
                                                                  bool detailNegotiated)
{
    const QJsonValue op = payload.value(QStringLiteral("op"));
    const QJsonValue connectionId = payload.value(QStringLiteral("connectionId"));
    const QJsonValue enabled = payload.value(QStringLiteral("enabled"));
    double revision = 0.0;
    double generation = 0.0;
    double ssrc = 0.0;
    double firstSequence = 0.0;
    double firstTimestamp = 0.0;
    // With the key count fixed, the eight keys each present and valid means
    // the legacy shape has no other key, and the detail shape has one more.
    if (payload.size() != (detailNegotiated ? kDetailContextKeys : kLegacyContextKeys)
        || !op.isString() || op.toString() != QLatin1String("audio-context")
        || !connectionId.isString() || !enabled.isBool()
        || !integral(payload.value(QStringLiteral("revision")), 1.0, kMaxU32, revision)
        || !integral(payload.value(QStringLiteral("generation")), 1.0, kMaxU32, generation)
        || !integral(payload.value(QStringLiteral("ssrc")), 1.0, kMaxU32, ssrc)
        || !integral(payload.value(QStringLiteral("firstSequence")), 0.0, kMaxSequence,
                     firstSequence)
        || !integral(payload.value(QStringLiteral("firstTimestamp")), 0.0, kMaxU32,
                     firstTimestamp)) {
        return std::nullopt;
    }
    RemoteAudioContextMessage message;
    message.connectionId = connectionId.toString();
    message.revision = static_cast<quint32>(revision);
    message.generation = static_cast<quint32>(generation);
    message.enabled = enabled.toBool();
    message.ssrc = static_cast<quint32>(ssrc);
    message.firstSequence = static_cast<quint16>(firstSequence);
    message.firstTimestamp = static_cast<quint32>(firstTimestamp);
    if (!detailNegotiated) {
        return message;
    }
    if (message.enabled) {
        if (payload.contains(QStringLiteral("reason"))) {
            return std::nullopt;
        }
        message.encoder = remoteAudioEncoderFromJson(payload.value(QStringLiteral("encoder")));
        if (!message.encoder) {
            return std::nullopt;
        }
    } else {
        if (payload.contains(QStringLiteral("encoder"))) {
            return std::nullopt;
        }
        message.offReason = remoteAudioOffReasonFromWire(payload.value(QStringLiteral("reason")));
        if (!message.offReason) {
            return std::nullopt;
        }
    }
    return message;
}

} // namespace NereusSDR
