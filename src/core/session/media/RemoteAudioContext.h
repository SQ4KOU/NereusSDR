// =================================================================
// src/core/session/media/RemoteAudioContext.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The one wire codec Core and GUI share
// for the remote audio context, in both the minor-7 and minor-8 shapes; it
// holds no session identity or playback policy.
// =================================================================

#pragma once

#include "core/session/media/OpusAudioCodec.h"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace NereusSDR {

/// Why Core is not sending audio, as a minor-8 audio context reports it.
enum class RemoteAudioOffReason { ClientDisabled, MediaNotReady, RadioOffline, EncoderUnavailable };

/// client-disabled, media-not-ready, radio-offline or encoder-unavailable.
QString remoteAudioOffReasonToWire(RemoteAudioOffReason reason);
/// One of the four wire strings exactly; nullopt for any other string and
/// for any value that is not a string.
std::optional<RemoteAudioOffReason> remoteAudioOffReasonFromWire(const QJsonValue& value);

/// {"codec":"opus","sampleRate","channels","frameSamples","targetBitrate",
/// "audioBandwidthHz"}, every number an integral JSON number.
QJsonObject remoteAudioEncoderToJson(const OpusEncoderProfile& profile);
/// Accepts only that exact key set describing a profile this build can
/// decode: codec "opus", 48000 Hz, 2 channels, 1920-sample frames, a target
/// of 6000..510000 bit/s and one of the five Opus audio bandwidths.
std::optional<OpusEncoderProfile> remoteAudioEncoderFromJson(const QJsonValue& value);

struct RemoteAudioContextMessage {
    QString connectionId;
    quint32 revision = 0;
    quint32 generation = 0;
    bool enabled = false;
    quint32 ssrc = 0;
    quint16 firstSequence = 0;
    quint32 firstTimestamp = 0;
    std::optional<OpusEncoderProfile> encoder;     // set only when enabled and detail negotiated
    std::optional<RemoteAudioOffReason> offReason; // set only when disabled and detail negotiated
};

// detailNegotiated=false: exactly today's eight keys (op, connectionId,
// revision, generation, enabled, ssrc, firstSequence, firstTimestamp) with
// today's JSON number types; encoder/offReason are not written.
// detailNegotiated=true: those eight plus "encoder" (enabled) or "reason" (disabled).
QJsonObject encodeRemoteAudioContext(const RemoteAudioContextMessage& message,
                                     bool detailNegotiated);

// Shape and field validation only (identity checks stay with the caller).
// Accepts exactly the shape selected by detailNegotiated; nullopt otherwise.
// Both shapes take revision, generation and ssrc as integral 1..4294967295,
// as the GUI's parser always has, and firstSequence 0..65535 and
// firstTimestamp 0..4294967295.
std::optional<RemoteAudioContextMessage> decodeRemoteAudioContext(const QJsonObject& payload,
                                                                  bool detailNegotiated);

} // namespace NereusSDR
