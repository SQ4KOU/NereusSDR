// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#pragma once
// no-port-check: NereusSDR-original bounded queued observer.
#include "WaveformSnapshot.h"
#include "core/NereusCoreExport.h"
#include <QObject>
#include <array>
#include <atomic>
#include <cstdint>
namespace NereusSDR {
class NEREUS_CORE_EXPORT WaveformPublisher : public QObject
{
    Q_OBJECT
  public:
    WaveformPublisher(WaveformKind kind, int sampleRate, QObject* parent = nullptr);
    int sampleRate() const noexcept
    {
        return m_rate;
    }
    WaveformKind kind() const noexcept
    {
        return m_kind;
    }
    void setBindingToken(quint64 token) noexcept
    {
        m_token.store(token, std::memory_order_release);
    }
    quint64 bindingToken() const noexcept
    {
        return m_token.load(std::memory_order_acquire);
    }
    void acknowledge(quint64 serial) noexcept
    {
        m_ack.store(serial, std::memory_order_release);
    }
    bool appendFloat(quint64 token, const float* pcm, int frames, int stride, qint64 atNs,
                     bool valid) noexcept;
    bool appendDouble(quint64 token, const double* pcm, int frames, int stride, qint64 atNs,
                      bool valid) noexcept;
    bool appendInt16(quint64 token, const int16_t* pcm, int frames, int stride, qint64 atNs,
                     bool valid) noexcept;
  signals:
    void batchReady(NereusSDR::WaveformBatchPtr batch);

  private:
    template <class T>
    bool append(quint64 token, const T* pcm, int frames, int stride, qint64 atNs, bool valid) noexcept;
    void publish(qint64 atNs) noexcept;
    void discardPrefix(int frames) noexcept;
    WaveformKind m_kind;
    int m_rate;
    int m_capacity;
    std::atomic<quint64> m_token{0};
    std::atomic<quint64> m_ack{0};
    quint64 m_producerToken = 0;
    quint64 m_sequence = 0;
    quint64 m_serial = 0;
    quint64 m_inFlight = 0;
    quint64 m_dropped = 0;
    qint64 m_lastPublishNs = 0;
    int m_head = 0;
    int m_count = 0;
    int m_batchCapacity = 0;
    static constexpr int kSegmentCapacity = 4096;
    QVector<float> m_pending;
    std::array<WaveformSegment, kSegmentCapacity> m_pendingSegments{};
    int m_segmentHead = 0;
    int m_segmentCount = 0;
    std::array<std::shared_ptr<WaveformBatch>, 3> m_pool;
};
} // namespace NereusSDR
