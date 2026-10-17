// no-port-check: NereusSDR-original exact PCM observer wire format.
#include "WaveformCodec.h"
#include <QtEndian>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
namespace NereusSDR {
int WaveformCodec::sampleRate(WaveformKind kind) {
    switch (kind) {
    case WaveformKind::RawMicrophone: return 48000;
    case WaveformKind::ConditionedMicrophone: return 16000;
    case WaveformKind::DecodedSpeech: return 24000;
    }
    return 0;
}
QString WaveformCodec::kindName(WaveformKind kind) {
    switch (kind) {
    case WaveformKind::RawMicrophone: return QStringLiteral("rawMic");
    case WaveformKind::ConditionedMicrophone: return QStringLiteral("micConditioned");
    case WaveformKind::DecodedSpeech: return QStringLiteral("decodedSpeech");
    }
    return {};
}
std::optional<WaveformKind> WaveformCodec::parseKind(const QString& name) {
    if (name == QLatin1String("rawMic")) { return WaveformKind::RawMicrophone; }
    if (name == QLatin1String("micConditioned")) { return WaveformKind::ConditionedMicrophone; }
    if (name == QLatin1String("decodedSpeech")) { return WaveformKind::DecodedSpeech; }
    return std::nullopt;
}
std::optional<quint64> WaveformCodec::decimalIdentity(const QJsonValue& value) {
    if (!value.isString()) { return std::nullopt; }
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 20 || (text.size() > 1 && text[0] == QLatin1Char('0'))) { return std::nullopt; }
    quint64 result = 0;
    for (QChar c : text) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9')) { return std::nullopt; }
        const quint64 digit = quint64(c.unicode() - '0');
        if (result > (std::numeric_limits<quint64>::max() - digit) / 10) { return std::nullopt; }
        result = result * 10 + digit;
    }
    return result;
}
namespace {
quint8 wireKind(WaveformKind kind) {
    switch (kind) {
    case WaveformKind::RawMicrophone: return 1;
    case WaveformKind::ConditionedMicrophone: return 2;
    case WaveformKind::DecodedSpeech: return 3;
    }
    return 0;
}
std::optional<WaveformKind> localKind(quint8 kind) {
    switch (kind) {
    case 1: return WaveformKind::RawMicrophone;
    case 2: return WaveformKind::ConditionedMicrophone;
    case 3: return WaveformKind::DecodedSpeech;
    default: return std::nullopt;
    }
}
bool coordinatesValid(quint64 first, qint64 start, int count, int rate) {
    return count > 0 && count <= WaveformCodec::kMaxSamples && rate > 0 && start >= 0
        && first <= std::numeric_limits<quint64>::max() - quint64(count)
        && start <= std::numeric_limits<qint64>::max() - qint64(count) * 1'000'000'000 / rate;
}
}
std::optional<QByteArray> WaveformCodec::encode(const WaveformPacket& packet) {
    const WaveformBatch& batch = packet.batch;
    if (packet.sliceId < 0 || !packet.sourceGeneration || !packet.deliveryGeneration || !batch.serial
        || sampleRate(batch.kind) != batch.sampleRate || batch.segments.size() != 1
        || batch.samples.empty() || batch.samples.size() > kMaxSamples) { return std::nullopt; }
    const WaveformSegment& segment = batch.segments[0];
    if (segment.offset != 0 || segment.count != int(batch.samples.size())
        || !coordinatesValid(segment.firstSequence, segment.startNs, segment.count, batch.sampleRate)) { return std::nullopt; }
    for (float sample : batch.samples) { if (!std::isfinite(sample)) { return std::nullopt; } }
    QByteArray bytes(kHeaderBytes + 4 * segment.count, char(0));
    std::memcpy(bytes.data(), "NWF1", 4); bytes[4] = 1; bytes[5] = char(wireKind(batch.kind));
    const auto put32 = [&bytes](int at, quint32 value) { qToBigEndian(value, bytes.data() + at); };
    const auto put64 = [&bytes](int at, quint64 value) { qToBigEndian(value, bytes.data() + at); };
    put32(8, quint32(packet.sliceId)); put32(12, quint32(batch.sampleRate));
    put64(16, packet.deliveryGeneration); put64(24, packet.sourceGeneration);
    put64(32, batch.serial); put64(40, segment.firstSequence); put64(48, quint64(segment.startNs));
    put32(56, quint32(segment.count)); put64(64, batch.droppedFrames);
    for (int i = 0; i < segment.count; ++i) { put32(kHeaderBytes + 4 * i, std::bit_cast<quint32>(batch.samples[size_t(i)])); }
    return bytes;
}
std::optional<WaveformPacket> WaveformCodec::decode(const QByteArray& bytes) {
    if (bytes.size() < kHeaderBytes || bytes.size() > kMaxPacketBytes || bytes.first(4) != "NWF1"
        || quint8(bytes[4]) != 1 || bytes[6] != 0 || bytes[7] != 0) { return std::nullopt; }
    const auto kind = localKind(quint8(bytes[5]));
    if (!kind) { return std::nullopt; }
    const auto get32 = [&bytes](int at) { return qFromBigEndian<quint32>(bytes.constData() + at); };
    const auto get64 = [&bytes](int at) { return qFromBigEndian<quint64>(bytes.constData() + at); };
    const quint32 slice = get32(8), rate = get32(12), count = get32(56);
    const quint64 delivery = get64(16), source = get64(24), serial = get64(32), first = get64(40), start = get64(48);
    if (slice > quint32(std::numeric_limits<int>::max()) || rate != quint32(sampleRate(*kind))
        || !delivery || !source || !serial || count == 0 || count > kMaxSamples || get32(60) != 0
        || bytes.size() != kHeaderBytes + qsizetype(count) * 4
        || start > quint64(std::numeric_limits<qint64>::max())
        || !coordinatesValid(first, qint64(start), int(count), int(rate))) { return std::nullopt; }
    // Validate every sample before allocating a packet/history payload.
    for (quint32 i = 0; i < count; ++i) {
        if (!std::isfinite(std::bit_cast<float>(get32(kHeaderBytes + int(i) * 4)))) { return std::nullopt; }
    }
    WaveformPacket packet; packet.sliceId = int(slice); packet.deliveryGeneration = delivery; packet.sourceGeneration = source;
    packet.batch.kind = *kind; packet.batch.sampleRate = int(rate); packet.batch.token = delivery;
    packet.batch.serial = serial; packet.batch.droppedFrames = get64(64);
    packet.batch.samples.resize(count); packet.batch.segments.push_back({0,int(count),first,qint64(start)});
    for (quint32 i = 0; i < count; ++i) { packet.batch.samples[i] = std::bit_cast<float>(get32(kHeaderBytes + int(i) * 4)); }
    return packet;
}
std::optional<WaveformPacket> WaveformCodec::chunk(int slice, quint64 source, quint64 delivery, quint64 serial,
    const WaveformBatch& batch, int segmentIndex, int offset, quint64 observerDrops) {
    if (slice < 0 || !source || !delivery || !serial || sampleRate(batch.kind) != batch.sampleRate
        || segmentIndex < 0 || size_t(segmentIndex) >= batch.segments.size() || offset < 0) { return std::nullopt; }
    const WaveformSegment& segment = batch.segments[size_t(segmentIndex)];
    if (segment.offset < 0 || segment.count <= offset || segment.count <= 0
        || size_t(segment.offset) > batch.samples.size()
        || size_t(segment.count) > batch.samples.size() - size_t(segment.offset)
        || segment.firstSequence > UINT64_MAX - quint64(offset) || segment.startNs < 0) { return std::nullopt; }
    const int count = std::min(kMaxSamples, segment.count - offset);
    const qint64 advance = qint64(offset) * 1'000'000'000 / batch.sampleRate;
    if (segment.startNs > INT64_MAX - advance
        || !coordinatesValid(segment.firstSequence + quint64(offset), segment.startNs + advance, count, batch.sampleRate)
        || batch.droppedFrames > UINT64_MAX - observerDrops) { return std::nullopt; }
    WaveformPacket packet; packet.sliceId=slice; packet.sourceGeneration=source; packet.deliveryGeneration=delivery;
    packet.batch.kind=batch.kind; packet.batch.token=delivery; packet.batch.serial=serial; packet.batch.sampleRate=batch.sampleRate;
    packet.batch.droppedFrames=batch.droppedFrames + observerDrops;
    const auto begin = batch.samples.begin() + segment.offset + offset;
    for (auto it = begin; it != begin + count; ++it) { if (!std::isfinite(*it)) { return std::nullopt; } }
    packet.batch.samples.assign(begin, begin + count);
    packet.batch.segments.push_back({0,count,segment.firstSequence + quint64(offset),segment.startNs + advance});
    return packet;
}
} // namespace NereusSDR
