#pragma once
// =================================================================
// src/core/session/media/OpusAudioCodec.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote daemon R3 Task 5. Uses the
// existing RADE-pinned Opus source; it owns no audio device, jitter queue,
// transport, or session identity.
//
// =================================================================

#include <QByteArray>
#include <QVector>

#include <memory>
#include <optional>

namespace NereusSDR {

struct OpusAudioCodecConfig {
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 2;
    static constexpr int kFrameSamples = 1'920; // 40 ms at 48 kHz
    static constexpr int kPayloadType = 111;
    static constexpr int kRtpHeaderBytes = 12;
    static constexpr int kMaxRtpPacketBytes = 940;
    static constexpr int kMaxPayloadBytes = kMaxRtpPacketBytes - kRtpHeaderBytes;

    int bitrate {24'000}; // the measured default; 48 kbit/s is the only alternate
};

enum class OpusAudioCodecStatus {
    Accepted,
    Concealed,
    InvalidInput,
    EncodeFailed,
    DecodeFailed,
    MalformedRtp,
    UnexpectedSsrc,
    Oversized,
};

struct OpusPacketInfo {
    int channels {0};
    int bandwidth {0}; // Opus OPUS_BANDWIDTH_* value
    int samplesPerChannel {0};
};

struct OpusRtpEncodeResult {
    OpusAudioCodecStatus status {OpusAudioCodecStatus::InvalidInput};
    QByteArray packet;
    OpusPacketInfo packetInfo;
};

struct OpusRtpDecodeResult {
    OpusAudioCodecStatus status {OpusAudioCodecStatus::InvalidInput};
    quint16 sequence {0};
    quint32 timestamp {0};
    QVector<float> pcmInterleaved;
    OpusPacketInfo packetInfo;
};

struct OpusRtpInspection {
    OpusAudioCodecStatus status {OpusAudioCodecStatus::InvalidInput};
    quint16 sequence {0};
    quint32 timestamp {0};
    // Validated Opus payload only: RTP CSRC, extension and padding bytes are
    // excluded by the parser before this observer-facing value is published.
    qsizetype payloadBytes {0};
    OpusPacketInfo packetInfo;
};

// Validates the same RTP/profile boundary as decodeRtp without advancing
// decoder history, so packets can be ordered before decoding.
OpusRtpInspection inspectOpusRtp(const QByteArray& packet, quint32 expectedSsrc);

/// The profile an encoder is actually running, as Core reports it to a GUI.
struct OpusEncoderProfile {
    int sampleRate {0};       // RTP clock and decoder rate, Hz
    int channels {0};
    int frameSamples {0};     // per channel per packet
    int targetBitrate {0};    // constrained-VBR encoder target, bit/s; not measured traffic
    int audioBandwidthHz {0}; // coded audio bandwidth limit
    friend bool operator==(const OpusEncoderProfile&, const OpusEncoderProfile&) = default;
};

/// RAII encoder for the approved 48 kHz, stereo, 40 ms AUDIO/MUSIC profile.
/// Caller owns the RTP sequence, timestamp, and SSRC/session generation.
class OpusAudioEncoder {
public:
    explicit OpusAudioEncoder(const OpusAudioCodecConfig& config = {});
    ~OpusAudioEncoder();
    OpusAudioEncoder(const OpusAudioEncoder&) = delete;
    OpusAudioEncoder& operator=(const OpusAudioEncoder&) = delete;

    bool isReady() const;
    /// Sample rate and target bitrate are read back from libopus. The coded
    /// bandwidth is the forced bandwidth the constructor configured:
    /// OPUS_GET_BANDWIDTH describes the last encoded frame instead, and reads
    /// FULLBAND after construction and after reset(), which is exactly when
    /// Core announces a new audio context. Valid immediately after
    /// construction or reset(), before any encode.
    std::optional<OpusEncoderProfile> profile() const; // nullopt when !isReady()
    OpusRtpEncodeResult encode(const QVector<float>& pcmInterleaved,
                               quint16 sequence, quint32 timestamp,
                               quint32 ssrc);
    void reset();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

/// RAII decoder. `decodeMissing()` is explicit packet-loss concealment; a bad
/// RTP packet is never converted into PLC by this boundary.
class OpusAudioDecoder {
public:
    explicit OpusAudioDecoder(const OpusAudioCodecConfig& config = {});
    ~OpusAudioDecoder();
    OpusAudioDecoder(const OpusAudioDecoder&) = delete;
    OpusAudioDecoder& operator=(const OpusAudioDecoder&) = delete;

    bool isReady() const;
    OpusRtpDecodeResult decodeRtp(const QByteArray& packet, quint32 expectedSsrc);
    OpusRtpDecodeResult decodeMissing();
    void reset();

private:
    struct State;
    std::unique_ptr<State> m_state;
};

} // namespace NereusSDR
