// =================================================================
// src/core/session/media/OpusAudioCodec.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote daemon R3 Task 5.
// Profile values are pinned by
// docs/architecture/2026-09-20-remote-daemon-r3-verification/opus-profile-probe.c
// against Opus 940d4e5af64351ca8ba8390df3f555484c567fbb.
//
// =================================================================

#include "core/session/media/OpusAudioCodec.h"

#include <opus.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace NereusSDR {
namespace {

bool validConfig(const OpusAudioCodecConfig& config)
{
    return config.bitrate == 24'000 || config.bitrate == 48'000;
}

bool finitePcm(const QVector<float>& pcm)
{
    return std::all_of(pcm.cbegin(), pcm.cend(), [](float sample) {
        return std::isfinite(sample);
    });
}

void appendU16(QByteArray& bytes, quint16 value)
{
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
}

void appendU32(QByteArray& bytes, quint32 value)
{
    bytes.append(static_cast<char>(value >> 24));
    bytes.append(static_cast<char>(value >> 16));
    bytes.append(static_cast<char>(value >> 8));
    bytes.append(static_cast<char>(value));
}

quint16 readU16(const QByteArray& bytes, int offset)
{
    return (static_cast<quint16>(static_cast<unsigned char>(bytes.at(offset))) << 8)
        | static_cast<unsigned char>(bytes.at(offset + 1));
}

quint32 readU32(const QByteArray& bytes, int offset)
{
    return (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset))) << 24)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 1))) << 16)
        | (static_cast<quint32>(static_cast<unsigned char>(bytes.at(offset + 2))) << 8)
        | static_cast<unsigned char>(bytes.at(offset + 3));
}

struct ParsedRtp {
    quint16 sequence {0};
    quint32 timestamp {0};
    quint32 ssrc {0};
    QByteArray payload;
};

OpusAudioCodecStatus parseRtp(const QByteArray& packet, ParsedRtp& parsed)
{
    if (packet.size() > OpusAudioCodecConfig::kMaxRtpPacketBytes) {
        return OpusAudioCodecStatus::Oversized;
    }
    if (packet.size() < OpusAudioCodecConfig::kRtpHeaderBytes) {
        return OpusAudioCodecStatus::MalformedRtp;
    }
    const quint8 first = static_cast<unsigned char>(packet.at(0));
    const quint8 second = static_cast<unsigned char>(packet.at(1));
    if ((first >> 6) != 2 || (second & 0x7f) != OpusAudioCodecConfig::kPayloadType) {
        return OpusAudioCodecStatus::MalformedRtp;
    }
    int headerBytes = OpusAudioCodecConfig::kRtpHeaderBytes + (first & 0x0f) * 4;
    if (headerBytes > packet.size()) {
        return OpusAudioCodecStatus::MalformedRtp;
    }
    if ((first & 0x10) != 0) {
        if (packet.size() - headerBytes < 4) {
            return OpusAudioCodecStatus::MalformedRtp;
        }
        const quint16 extensionWords = readU16(packet, headerBytes + 2);
        const int extensionBytes = 4 + static_cast<int>(extensionWords) * 4;
        if (extensionBytes > packet.size() - headerBytes) {
            return OpusAudioCodecStatus::MalformedRtp;
        }
        headerBytes += extensionBytes;
    }
    int payloadBytes = packet.size() - headerBytes;
    if ((first & 0x20) != 0) {
        if (payloadBytes == 0) {
            return OpusAudioCodecStatus::MalformedRtp;
        }
        const int padding = static_cast<unsigned char>(packet.back());
        if (padding <= 0 || padding > payloadBytes) {
            return OpusAudioCodecStatus::MalformedRtp;
        }
        payloadBytes -= padding;
    }
    if (payloadBytes <= 0 || payloadBytes > OpusAudioCodecConfig::kMaxPayloadBytes) {
        return OpusAudioCodecStatus::MalformedRtp;
    }
    parsed.sequence = readU16(packet, 2);
    parsed.timestamp = readU32(packet, 4);
    parsed.ssrc = readU32(packet, 8);
    parsed.payload = packet.mid(headerBytes, payloadBytes);
    return OpusAudioCodecStatus::Accepted;
}

OpusPacketInfo packetInfo(const QByteArray& payload)
{
    OpusPacketInfo info;
    info.channels = opus_packet_get_nb_channels(
        reinterpret_cast<const unsigned char*>(payload.constData()));
    info.bandwidth = opus_packet_get_bandwidth(
        reinterpret_cast<const unsigned char*>(payload.constData()));
    info.samplesPerChannel = opus_packet_get_nb_samples(
        reinterpret_cast<const unsigned char*>(payload.constData()), payload.size(),
        OpusAudioCodecConfig::kSampleRate);
    return info;
}

bool validPacketInfo(const OpusPacketInfo& info)
{
    return info.channels == OpusAudioCodecConfig::kChannels
        && info.samplesPerChannel == OpusAudioCodecConfig::kFrameSamples
        && info.bandwidth > OPUS_BANDWIDTH_NARROWBAND;
}

} // namespace

OpusRtpInspection inspectOpusRtp(const QByteArray& packet, quint32 expectedSsrc)
{
    OpusRtpInspection result;
    ParsedRtp parsed;
    result.status = parseRtp(packet, parsed);
    if (result.status != OpusAudioCodecStatus::Accepted) { return result; }
    if (parsed.ssrc != expectedSsrc) {
        result.status = OpusAudioCodecStatus::UnexpectedSsrc;
        return result;
    }
    result.packetInfo = packetInfo(parsed.payload);
    if (!validPacketInfo(result.packetInfo)) {
        result.status = OpusAudioCodecStatus::MalformedRtp;
        return result;
    }
    result.sequence = parsed.sequence;
    result.timestamp = parsed.timestamp;
    result.payloadBytes = parsed.payload.size();
    return result;
}

struct OpusAudioEncoder::State {
    OpusEncoder* encoder {nullptr};
    OpusAudioCodecConfig config;

    ~State() { opus_encoder_destroy(encoder); }
};

struct OpusAudioDecoder::State {
    OpusDecoder* decoder {nullptr};
    OpusAudioCodecConfig config;

    ~State() { opus_decoder_destroy(decoder); }
};

OpusAudioEncoder::OpusAudioEncoder(const OpusAudioCodecConfig& config)
{
    if (!validConfig(config)) {
        return;
    }
    int error = OPUS_OK;
    std::unique_ptr<State> state = std::make_unique<State>();
    state->config = config;
    state->encoder = opus_encoder_create(OpusAudioCodecConfig::kSampleRate,
                                         OpusAudioCodecConfig::kChannels,
                                         OPUS_APPLICATION_AUDIO, &error);
    if (state->encoder == nullptr || error != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_BANDWIDTH(OPUS_BANDWIDTH_WIDEBAND)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_BITRATE(config.bitrate)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_VBR(1)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_VBR_CONSTRAINT(1)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_COMPLEXITY(10)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_INBAND_FEC(0)) != OPUS_OK
        || opus_encoder_ctl(state->encoder, OPUS_SET_DTX(0)) != OPUS_OK) {
        return;
    }
    m_state = std::move(state);
}

OpusAudioEncoder::~OpusAudioEncoder() = default;

bool OpusAudioEncoder::isReady() const
{
    return m_state && m_state->encoder;
}

OpusRtpEncodeResult OpusAudioEncoder::encode(const QVector<float>& pcmInterleaved,
                                              quint16 sequence, quint32 timestamp,
                                              quint32 ssrc)
{
    OpusRtpEncodeResult result;
    if (!isReady() || pcmInterleaved.size()
        != OpusAudioCodecConfig::kFrameSamples * OpusAudioCodecConfig::kChannels
        || !finitePcm(pcmInterleaved)) {
        return result;
    }
    QByteArray payload(OpusAudioCodecConfig::kMaxPayloadBytes, '\0');
    const int encoded = opus_encode_float(m_state->encoder, pcmInterleaved.constData(),
                                          OpusAudioCodecConfig::kFrameSamples,
                                          reinterpret_cast<unsigned char*>(payload.data()),
                                          payload.size());
    if (encoded <= 0 || encoded > OpusAudioCodecConfig::kMaxPayloadBytes) {
        result.status = OpusAudioCodecStatus::EncodeFailed;
        return result;
    }
    payload.truncate(encoded);
    result.packetInfo = packetInfo(payload);
    if (!validPacketInfo(result.packetInfo)) {
        result.status = OpusAudioCodecStatus::EncodeFailed;
        return result;
    }
    result.packet.reserve(OpusAudioCodecConfig::kRtpHeaderBytes + encoded);
    result.packet.append(static_cast<char>(0x80)); // V2, no CSRC/extension/padding
    result.packet.append(static_cast<char>(OpusAudioCodecConfig::kPayloadType));
    appendU16(result.packet, sequence);
    appendU32(result.packet, timestamp);
    appendU32(result.packet, ssrc);
    result.packet.append(payload);
    result.status = OpusAudioCodecStatus::Accepted;
    return result;
}

void OpusAudioEncoder::reset()
{
    if (isReady()) {
        opus_encoder_ctl(m_state->encoder, OPUS_RESET_STATE);
    }
}

OpusAudioDecoder::OpusAudioDecoder(const OpusAudioCodecConfig& config)
{
    if (!validConfig(config)) {
        return;
    }
    int error = OPUS_OK;
    std::unique_ptr<State> state = std::make_unique<State>();
    state->config = config;
    state->decoder = opus_decoder_create(OpusAudioCodecConfig::kSampleRate,
                                         OpusAudioCodecConfig::kChannels, &error);
    if (state->decoder == nullptr || error != OPUS_OK) {
        return;
    }
    m_state = std::move(state);
}

OpusAudioDecoder::~OpusAudioDecoder() = default;

bool OpusAudioDecoder::isReady() const
{
    return m_state && m_state->decoder;
}

OpusRtpDecodeResult OpusAudioDecoder::decodeRtp(const QByteArray& packet, quint32 expectedSsrc)
{
    OpusRtpDecodeResult result;
    if (!isReady()) {
        return result;
    }
    ParsedRtp parsed;
    result.status = parseRtp(packet, parsed);
    if (result.status != OpusAudioCodecStatus::Accepted) {
        return result;
    }
    if (parsed.ssrc != expectedSsrc) {
        result.status = OpusAudioCodecStatus::UnexpectedSsrc;
        return result;
    }
    result.packetInfo = packetInfo(parsed.payload);
    if (!validPacketInfo(result.packetInfo)) {
        result.status = OpusAudioCodecStatus::MalformedRtp;
        return result;
    }
    result.pcmInterleaved.resize(OpusAudioCodecConfig::kFrameSamples
                                 * OpusAudioCodecConfig::kChannels);
    const int decoded = opus_decode_float(m_state->decoder,
                                          reinterpret_cast<const unsigned char*>(parsed.payload.constData()),
                                          parsed.payload.size(), result.pcmInterleaved.data(),
                                          OpusAudioCodecConfig::kFrameSamples, 0);
    if (decoded != OpusAudioCodecConfig::kFrameSamples || !finitePcm(result.pcmInterleaved)) {
        result.pcmInterleaved.clear();
        result.status = OpusAudioCodecStatus::DecodeFailed;
        return result;
    }
    result.sequence = parsed.sequence;
    result.timestamp = parsed.timestamp;
    result.status = OpusAudioCodecStatus::Accepted;
    return result;
}

OpusRtpDecodeResult OpusAudioDecoder::decodeMissing()
{
    OpusRtpDecodeResult result;
    if (!isReady()) {
        return result;
    }
    result.pcmInterleaved.resize(OpusAudioCodecConfig::kFrameSamples
                                 * OpusAudioCodecConfig::kChannels);
    const int decoded = opus_decode_float(m_state->decoder, nullptr, 0,
                                          result.pcmInterleaved.data(),
                                          OpusAudioCodecConfig::kFrameSamples, 0);
    if (decoded != OpusAudioCodecConfig::kFrameSamples || !finitePcm(result.pcmInterleaved)) {
        result.pcmInterleaved.clear();
        result.status = OpusAudioCodecStatus::DecodeFailed;
        return result;
    }
    result.status = OpusAudioCodecStatus::Concealed;
    return result;
}

void OpusAudioDecoder::reset()
{
    if (isReady()) {
        opus_decoder_ctl(m_state->decoder, OPUS_RESET_STATE);
    }
}

} // namespace NereusSDR
