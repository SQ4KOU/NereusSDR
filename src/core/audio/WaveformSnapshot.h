// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#pragma once
// no-port-check: NereusSDR-original full-rate observer DTOs.
#include "core/NereusCoreExport.h"
#include <QMetaType>
#include <QString>
#include <QVector>
#include <memory>
#include <vector>
namespace NereusSDR {
enum class WaveformKind { RawMicrophone, ConditionedMicrophone, DecodedSpeech };
enum class WaveformAvailability { Unknown, Available, Unavailable, Unsupported };
struct WaveformSegment {
    int offset = 0;
    int count = 0;
    quint64 firstSequence = 0;
    qint64 startNs = 0;
};
struct WaveformBatch {
    WaveformKind kind = WaveformKind::ConditionedMicrophone;
    quint64 token = 0;
    quint64 serial = 0;
    quint64 droppedFrames = 0;
    int sampleRate = 0;
    std::vector<float> samples;
    std::vector<WaveformSegment> segments;
};
struct WaveformEndpointState {
    quint64 sourceGeneration = 0;
    int sampleRateHz = 0;
    bool available = false;
    QString reason;
};
// A consumer must retain this strong publication lease for its entire read.
// Ack releases the event gate; only releasing every strong lease permits pool
// reuse. A saved weak_ptr is not a publication lease and must not be promoted
// later to read a slot that the producer may already be recycling.
using WaveformBatchPtr = std::shared_ptr<const WaveformBatch>;
struct WaveformSnapshot {
    WaveformKind kind = WaveformKind::ConditionedMicrophone;
    WaveformAvailability availability = WaveformAvailability::Unknown;
    QString reason;
    quint64 token = 0;
    quint64 revision = 0;
    quint64 droppedFrames = 0;
    int sampleRate = 0;
    qint64 asOfNs = 0;
    QVector<float> samples;
    QVector<WaveformSegment> segments;
};
using WaveformSnapshotPtr = std::shared_ptr<const WaveformSnapshot>;
NEREUS_CORE_EXPORT qint64 waveformNowNs() noexcept;
} // namespace NereusSDR
Q_DECLARE_METATYPE(NereusSDR::WaveformBatchPtr)
