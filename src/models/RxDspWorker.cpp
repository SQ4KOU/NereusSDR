#include "RxDspWorker.h"

#include "core/AudioEngine.h"
#include "core/RxChannel.h"
#include "core/WdspEngine.h"

namespace NereusSDR {

RxDspWorker::RxDspWorker(QObject* parent)
    : QObject(parent)
{
    m_iqAccumI.reserve(kWdspBufSize * 2);
    m_iqAccumQ.reserve(kWdspBufSize * 2);
}

RxDspWorker::~RxDspWorker() = default;

void RxDspWorker::setEngines(WdspEngine* wdsp, AudioEngine* audio)
{
    m_wdspEngine  = wdsp;
    m_audioEngine = audio;
}

void RxDspWorker::processIqBatch(int receiverIndex,
                                 const QVector<float>& interleavedIQ)
{
    Q_UNUSED(receiverIndex);

    // No engines wired (test fixtures, or torn down): drop and signal.
    if (m_wdspEngine == nullptr || m_audioEngine == nullptr) {
        emit batchProcessed();
        return;
    }

    // Deinterleave and append to accumulation buffers.
    const int numSamples = interleavedIQ.size() / 2;
    for (int i = 0; i < numSamples; ++i) {
        m_iqAccumI.append(interleavedIQ[i * 2]);
        m_iqAccumQ.append(interleavedIQ[i * 2 + 1]);
    }

    // Process whole 1024-sample chunks through WDSP.
    while (m_iqAccumI.size() >= kWdspBufSize) {
        RxChannel* rxCh = m_wdspEngine->rxChannel(0);
        if (rxCh == nullptr) {
            m_iqAccumI.clear();
            m_iqAccumQ.clear();
            emit batchProcessed();
            return;
        }

        QVector<float> outI(kWdspBufSize);
        QVector<float> outQ(kWdspBufSize);
        rxCh->processIq(m_iqAccumI.data(), m_iqAccumQ.data(),
                        outI.data(), outQ.data(), kWdspBufSize);

        // WDSP outputs out_size samples (64 at 768k→48k decimation).
        // outI == outQ because SetRXAPanelBinaural(channel, 0) puts the
        // RXA patch panel in dual-mono mode in WdspEngine::createRxChannel.
        m_audioEngine->feedAudio(0, outI.data(), outQ.data(), kWdspOutSize);

        m_iqAccumI.remove(0, kWdspBufSize);
        m_iqAccumQ.remove(0, kWdspBufSize);
    }

    emit batchProcessed();
}

void RxDspWorker::resetAccumulator()
{
    m_iqAccumI.clear();
    m_iqAccumQ.clear();
}

} // namespace NereusSDR
