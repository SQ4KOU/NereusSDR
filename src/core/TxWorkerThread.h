// =================================================================
// src/core/TxWorkerThread.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file.  QThread that drives the TX DSP pump off
// the main thread, mirroring Thetis's `cm_main` worker-thread model
// from Project Files/Source/ChannelMaster/cmbuffs.c:151-168 [v2.10.3.13]
// and the block-size invariant from cmaster.c:460-487 [v2.10.3.13].
//
// Why this exists:
//   The 3M-1c session shipped a D.1/E.1/L.4 pump chain (720-sample
//   accumulator → MicReBlocker → push slot) that fought Thetis's
//   architecture and produced a "gravelly" SSB voice TX bench
//   regression.  Architectural review traced the root cause to a
//   misread of `cmInboundSize[5]=720` (network arrival block size,
//   not DSP block size).  Thetis's actual DSP block size is
//   `xcm_insize == r1_outsize == in_size == 64` (Thetis cmaster.c:
//   460-487 [v2.10.3.13]), uniform end-to-end with no re-blocking.
//
// NereusSDR's r2-ring divisibility constraint (2048 % size == 0)
// forces a block size of 256 (rather than Thetis's 64).  The
// invariant is preserved: m_inputBufferSize=256 in TxChannel ==
// pump tick block size == fexchange2 in_size.
//
// Architecture (mirrors Thetis cm_main):
//
//   QTimer fires at kPumpIntervalMs (~5 ms — natural PC mic
//   256-samples-per-5.33-ms delivery cadence).  Each tick:
//     1. Pull up to kBlockFrames=256 samples from
//        AudioEngine::pullTxMic.
//     2. Zero-fill the gap if pull returns < 256 (silence path falls
//        out for free — TUNE-tone PostGen still produces output).
//     3. Call TxChannel::driveOneTxBlock(samples, 256) — runs
//        fexchange2 + sendTxIq on this worker thread.
//
// One pull, one fexchange2, one block — exactly the
// `r1_outsize == xcm_insize == in_size` invariant from
// Thetis cmaster.c:460-487 [v2.10.3.13].
//
// Future polish: replace QTimer with a QSemaphore-based wake from
// the audio thread (closer to Thetis's `Inbound() → ReleaseSemaphore`
// pattern in cmbuffs.c:89-121 [v2.10.3.13]).  Functionally
// equivalent to the polling approximation here.
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.  Phase 3M-1c TX pump
//                 architecture redesign — replaces D.1 (AudioEngine
//                 720-sample accumulator) + E.1 (push-driven slot)
//                 + L.4 (MicReBlocker) + bench-fix-A (AudioEngine
//                 pumpMic timer) + bench-fix-B (TxChannel
//                 silence-drive timer).
//                 Plan: docs/architecture/phase3m-1c-tx-pump-architecture-plan.md
// =================================================================

// no-port-check: NereusSDR-original file.  The Thetis cmbuffs.c /
// cmaster.c citations identify the architectural pattern this class
// mirrors (worker thread + matching block size); no Thetis logic is
// line-for-line ported here.

#pragma once

#include <QObject>
#include <QThread>

class QTimer;

namespace NereusSDR {

class AudioEngine;
class TxChannel;

// ---------------------------------------------------------------------------
// TxWorkerThread — dedicated QThread for TX DSP pump.
//
// Mirrors Thetis's `cm_main` worker-thread pattern from
// `Project Files/Source/ChannelMaster/cmbuffs.c:151-168 [v2.10.3.13]`:
//
//   void cm_main (void *pargs) {
//       ... promote thread to Pro Audio priority ...
//       while (run) {
//           WaitForSingleObject(Sem_BuffReady, INFINITE);
//           cmdata (id, in[id]);     // drain one r1_outsize-block
//           xcmaster(id);            // run fexchange + surrounds
//       }
//   }
//
// NereusSDR's QTimer-based polling approximates the semaphore wake;
// the cadence (~5 ms) matches the natural PC mic 256-samples /
// 5.33 ms delivery rate.  See class doc-comment for the full
// architectural rationale.
//
// Lifecycle:
//   1. Construct with parent (typically RadioModel).
//   2. setTxChannel + setAudioEngine — both must be non-null before
//      startPump().  The TxChannel must already be moveToThread()'d
//      to this worker.
//   3. startPump() — calls QThread::start() (which invokes run() →
//      sets up the QTimer on this worker thread → enters event loop).
//   4. stopPump() — quits the event loop, waits for thread exit.
//      Safe to call multiple times; idempotent.
//   5. Destruct (after stopPump and after TxChannel is moved back to
//      its original thread by RadioModel).
//
// Thread safety:
//   - setTxChannel / setAudioEngine: main thread only, BEFORE startPump().
//   - startPump / stopPump: main thread only.
//   - onPumpTick: runs on this thread (the worker).  Reads
//     m_txChannel / m_audioEngine raw pointers; assumes RadioModel
//     never tears them down while the pump is running (stopPump
//     completes first).
//
// Cross-thread parameter mutation:
//   TxChannel public setters that need to mutate WDSP state from the
//   main thread are marked `public slots:` and connected via
//   Qt::AutoConnection (which auto-resolves to QueuedConnection
//   when receiver is on this worker).  See TxChannel.h for the
//   per-setter audit table.
class TxWorkerThread : public QThread {
    Q_OBJECT

public:
    explicit TxWorkerThread(QObject* parent = nullptr);
    ~TxWorkerThread() override;

    /// Set the components the worker drives.  Both must be non-null
    /// before startPump().  Called from the main thread before the
    /// thread starts (or after stopPump).
    void setTxChannel(TxChannel* ch);
    void setAudioEngine(AudioEngine* engine);

    /// Start the pump.  Internally calls QThread::start() which
    /// invokes run() on the new thread; run() creates the QTimer on
    /// this thread and enters Qt's event loop.
    /// Idempotent: a second call while running is a no-op.
    void startPump();

    /// Stop the pump.  Calls quit() on the worker's event loop then
    /// wait()s for the thread to exit.  Safe to call from the main
    /// thread.  Idempotent: a second call when already stopped is
    /// a no-op.
    void stopPump();

    /// Block size, in samples, of one pump tick.  Matches
    /// TxChannel::m_inputBufferSize default and the WDSP r2-ring
    /// divisibility constraint (2048 % 256 == 0).
    static constexpr int kBlockFrames = 256;

    /// Pump tick interval in milliseconds.  ~5 ms matches the natural
    /// PC mic 256-samples-per-5.33-ms delivery cadence (256/48000 ≈
    /// 5.33 ms).
    static constexpr int kPumpIntervalMs = 5;

#ifdef NEREUS_BUILD_TESTS
    /// Test seam — invoke onPumpTick synchronously from the caller's
    /// thread.  Used by tst_tx_worker_thread to verify pump logic
    /// without managing a real worker thread + event loop.
    /// NOT a Q_INVOKABLE — direct call only.
    void tickForTest() { onPumpTick(); }
#endif

protected:
    /// QThread entry point.  Creates the QTimer on this thread and
    /// enters the event loop via QThread::exec().
    void run() override;

private slots:
    /// Pump tick — pulls up to kBlockFrames samples from
    /// AudioEngine::pullTxMic, zero-fills the gap if short, and calls
    /// TxChannel::driveOneTxBlock(samples, kBlockFrames).  Runs on
    /// this worker thread (the QTimer's affinity).
    void onPumpTick();

private:
    QTimer*      m_pumpTimer{nullptr};   // owned by this thread; created in run()
    TxChannel*   m_txChannel{nullptr};   // not owned
    AudioEngine* m_audioEngine{nullptr}; // not owned
};

} // namespace NereusSDR
