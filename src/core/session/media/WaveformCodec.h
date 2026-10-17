#pragma once
// no-port-check: NereusSDR-original exact PCM observer wire format.
#include "core/NereusCoreExport.h"
#include "core/audio/WaveformSnapshot.h"
#include <QByteArray>
#include <QJsonValue>
#include <optional>
namespace NereusSDR {
struct WaveformPacket {
    int sliceId = -1;
    quint64 sourceGeneration = 0;
    quint64 deliveryGeneration = 0;
    WaveformBatch batch;
};
class NEREUS_CORE_EXPORT WaveformCodec {
public:
    static constexpr int kHeaderBytes = 72;
    static constexpr int kMaxSamples = 1024;
    static constexpr int kMaxPacketBytes = kHeaderBytes + 4 * kMaxSamples;
    static int sampleRate(WaveformKind kind);
    static QString kindName(WaveformKind kind);
    static std::optional<WaveformKind> parseKind(const QString& name);
    static std::optional<quint64> decimalIdentity(const QJsonValue& value);
    static std::optional<QByteArray> encode(const WaveformPacket& packet);
    static std::optional<WaveformPacket> decode(const QByteArray& bytes);
    // One packet cannot bridge a gap. Offset is relative to this segment.
    static std::optional<WaveformPacket> chunk(int sliceId, quint64 source,
        quint64 delivery, quint64 serial, const WaveformBatch& batch,
        int segmentIndex, int segmentOffset, quint64 observerDrops = 0);
};
} // namespace NereusSDR
