// =================================================================
// src/core/session/media/DisplayBudget.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original accounting of NereusSDR display codecs.
//
// The constants below are derived from the bounded NSDC and PS3D wire formats;
// they are not a claim about a radio, link, or host's sustainable capacity.
//
// =================================================================

#pragma once

#include "core/session/Ps3DisplayCodec.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/SpectrumEndpoint.h"

#include <QList>

#include <optional>

namespace NereusSDR {

/// Largest positive integer that JSON's IEEE-754 number representation carries
/// exactly. Budget descriptors travel as JSON Int64-compatible values.
inline constexpr quint64 kDisplayBudgetJsonSafePositiveLimit = 9'007'199'254'740'991ULL;

/// Existing daemon media scheduler cadence (DaemonMediaController.cpp).
inline constexpr quint32 kDisplaySenderIntervalMs = 5;
inline constexpr qint64 kDisplaySenderIntervalNs = qint64{kDisplaySenderIntervalMs} * 1'000'000;
inline constexpr quint32 kDisplaySenderMessagesPerSecond =
    1'000 / kDisplaySenderIntervalMs;

/// Current NSDC schema-1 header and daemon subscribe validation maximum
/// (DisplayCodec.cpp and DaemonMediaController.cpp respectively).
inline constexpr quint32 kDisplayCodecHeaderBytes = DisplayCodecEncoder::kHeaderBytes;
inline constexpr quint32 kMaximumSpectrumDisplayFramesPerSecond = 60;

/// Existing remote PureSignal display producer cadence
/// (PureSignalSessionFacade.cpp).
inline constexpr quint32 kPs3DisplayPollIntervalMs = 100;
inline constexpr qint64 kPs3DisplayPollIntervalNs =
    qint64{kPs3DisplayPollIntervalMs} * 1'000'000;

constexpr quint32 displayCodecWorstCasePlaneBytes(quint32 samples)
{
    return samples == 0 ? 0
        : 3 + 5 * ((samples + 127) / 128) + samples;
}

/// Current bounded NSDC frame, using the actual endpoint/codec public maxima.
inline constexpr quint32 kMaximumSpectrumDisplayFrameBytes = kDisplayCodecHeaderBytes
    + 2 * displayCodecWorstCasePlaneBytes(DisplayCodecEncoder::kMaxSamplesPerPlane)
    + displayCodecWorstCasePlaneBytes(SpectrumEndpoint::kMaxWideSamples);
inline constexpr quint32 kMaximumSpectrumDisplayFrameSampleUnits =
    2 * DisplayCodecEncoder::kMaxSamplesPerPlane + SpectrumEndpoint::kMaxWideSamples;

/// PS3D's public message limit also fixes the common global-byte burst.
inline constexpr quint32 kMaximumPs3DisplayChunkBytes = Ps3DisplayCodec::kMaxChunkBytes;
inline constexpr quint32 kMaximumDisplayMessageBytes = kMaximumPs3DisplayChunkBytes;

struct DisplayBudgetLimits {
    quint64 applicationBytesPerSecond = 0;
    quint64 spectrumSampleUnitsPerSecond = 0;
    quint32 generation = 1;
    bool isValid() const;
    bool operator==(const DisplayBudgetLimits&) const = default;
};

struct DisplayBudgetCharge {
    quint64 applicationBytesPerSecond = 0;
    quint64 spectrumSampleUnitsPerSecond = 0;
    quint32 messagesPerSecond = 0;
    bool operator==(const DisplayBudgetCharge&) const = default;
};

struct SpectrumDisplayCost {
    DisplayBudgetCharge charge;
    quint32 maximumFrameBytes = 0;
    quint32 maximumFrameSampleUnits = 0;
};

std::optional<SpectrumDisplayCost> spectrumDisplayCost(int pixels, int fps,
                                                        bool includeWidePlane);
DisplayBudgetCharge ps3DisplayCharge();
std::optional<DisplayBudgetCharge> sumDisplayCharges(
    const QList<DisplayBudgetCharge>& charges);
bool displayChargeFits(const DisplayBudgetLimits&, const DisplayBudgetCharge&);

/// Session-scoped, fixed-burst token accounting for actual display sends.
/// It deliberately has no transport outcome: callers debit immediately before
/// every nonempty attempt, because a failed transport can already own bytes.
class DisplayBudgetPacer {
public:
    bool beginSession(quint64 epoch, DisplayBudgetLimits limits, qint64 nowNs);
    void endSession();
    bool update(DisplayBudgetLimits limits, DisplayBudgetCharge spectrumCharge,
                bool ps3Enabled, qint64 nowNs);

    bool canSpendSpectrum(quint64 bytes, quint64 samples, qint64 nowNs);
    bool spendSpectrum(quint64 bytes, quint64 samples, qint64 nowNs);
    bool canSpendPs3(quint64 bytes, qint64 nowNs);
    bool spendPs3(quint64 bytes, qint64 nowNs);

private:
    struct Bucket {
        quint64 credit = 0;
        quint64 rate = 0;
        quint64 remainder = 0;
        quint64 capacity = 0;
    };

    static void refill(Bucket& bucket, qint64 elapsedNs);
    void accrue(qint64 nowNs);
    bool canSpendSpectrumAfterAccrual(quint64 bytes, quint64 samples) const;
    bool canSpendPs3AfterAccrual(quint64 bytes) const;

    quint64 m_lastEpoch = 0;
    quint64 m_epoch = 0;
    qint64 m_lastNs = 0;
    bool m_active = false;
    bool m_spectrumActive = false;
    bool m_ps3Active = false;
    DisplayBudgetLimits m_limits;
    DisplayBudgetCharge m_spectrumCharge;
    Bucket m_globalBytes;
    Bucket m_spectrumBytes;
    Bucket m_ps3Bytes;
    Bucket m_spectrumSamples;
};

} // namespace NereusSDR
