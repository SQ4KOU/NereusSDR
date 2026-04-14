#pragma once

#include <QObject>
#include <QVector>

namespace NereusSDR {

class WdspEngine;
class AudioEngine;

// RxDspWorker runs the per-receiver I/Q → WDSP → audio processing step
// on a dedicated DSP thread, off the GUI main thread.
//
// Why: WDSP's fexchange2() (called via RxChannel::processIq) is opened
// with bfo=1, which makes it block on Sem_OutReady whenever the WDSP
// channel's internal DSP loop hasn't replenished its output ring. When
// that call ran on the GUI main thread it produced a deterministic
// two-way deadlock: fexchange2 waited on Sem_OutReady, the WDSP worker
// (wdspmain) waited on Sem_BuffReady, and because the main thread was
// blocked the Qt event loop stopped delivering more I/Q events to feed
// fexchange2 — so wdspmain never received another input batch and never
// signalled Sem_OutReady. Moving fexchange2 to its own thread keeps the
// blocking semantics local to that thread and lets the GUI event loop
// keep dispatching I/Q events.
//
// The worker is owned by RadioModel. It is constructed on the main
// thread, given non-owning WdspEngine/AudioEngine pointers via
// setEngines(), moved to RadioModel::m_dspThread, then driven by a
// Qt::QueuedConnection from ReceiverManager::iqDataForReceiver.
class RxDspWorker : public QObject {
    Q_OBJECT

public:
    // From RadioModel — Thetis formula: in_size = 64 * rate / 48000
    // → 1024 at 768 kHz; out_size = in_size * out_rate / in_rate
    // → 64 at 768k→48k. Kept here so the worker is self-contained.
    static constexpr int kWdspBufSize = 1024;
    static constexpr int kWdspOutSize = 64;

    explicit RxDspWorker(QObject* parent = nullptr);
    ~RxDspWorker() override;

    // Set non-owning engine pointers. Must be called on the main
    // thread before moveToThread(). The engines must outlive this
    // worker — RadioModel guarantees this by tearing the worker down
    // before destroying the engines.
    void setEngines(WdspEngine* wdsp, AudioEngine* audio);

public slots:
    // Receive a batch of interleaved I/Q from ReceiverManager. Runs
    // on m_dspThread via Qt::QueuedConnection. Accumulates samples
    // into in_size chunks, hands each chunk to RxChannel::processIq,
    // and forwards the decoded audio to AudioEngine.
    void processIqBatch(int receiverIndex,
                        const QVector<float>& interleavedIQ);

    // Drop any partial accumulator state. Called from RadioModel
    // teardown via Qt::BlockingQueuedConnection so it executes on
    // the worker thread before the WDSP channel is destroyed.
    void resetAccumulator();

signals:
    // Emitted at the end of every processIqBatch invocation, on the
    // DSP thread. Used by tests to observe that work happens off the
    // main thread without requiring a real WDSP build.
    void batchProcessed();

private:
    WdspEngine*    m_wdspEngine{nullptr};
    AudioEngine*   m_audioEngine{nullptr};
    QVector<float> m_iqAccumI;
    QVector<float> m_iqAccumQ;
};

} // namespace NereusSDR
