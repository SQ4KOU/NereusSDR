// =================================================================
// src/core/TxWorkerThread.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original file.  QThread that drives the TX DSP pump off
// the main thread, mirroring Thetis's `cm_main` worker-thread loop
// from Project Files/Source/ChannelMaster/cmbuffs.c:151-168
// [v2.10.3.13] one-to-one.
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-29 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.  Phase 3M-1c TX pump
//                 architecture redesign v2 (QTimer-driven, fexchange2,
//                 256-block).  Rewritten by the same author the same
//                 day for v3 (semaphore-wake, fexchange0, 64-block,
//                 cadence sourced from radio mic frames via
//                 TxMicSource).  Plan:
//                 docs/architecture/phase3m-1c-tx-pump-architecture-plan.md
// =================================================================

// no-port-check: NereusSDR-original file.  The Thetis cmbuffs.c /
// cmaster.c citations identify the architectural pattern this class
// mirrors (worker thread + semaphore-wake + uniform block size); no
// Thetis logic is line-for-line ported here (the CMB primitives
// themselves live in src/core/audio/TxMicSource.{h,cpp}).

#pragma once

#include <QThread>

#include <vector>

namespace NereusSDR {

class AudioEngine;
class TxChannel;
class TxMicSource;

// ---------------------------------------------------------------------------
// TxWorkerThread — dedicated QThread for the TX DSP pump.
//
// Mirrors Thetis cm_main at cmbuffs.c:151-168 [v2.10.3.13]:
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
// NereusSDR mapping:
//   WaitForSingleObject     <==>  m_micSource->waitForBlock(-1)
//   cmdata                  <==>  m_micSource->drainBlock(m_in.data())
//   xcmaster (TX branch)    <==>  m_txChannel->driveOneTxBlockFromInterleaved
//   pcm->in[stream]          <==>  m_in (interleaved I/Q double, 128 elems)
//
// PC mic override (Thetis cmaster.c:379 — `asioIN(pcm->in[stream])`):
//   When AudioEngine::isPcMicOverrideActive() returns true (the user
//   selected MicSource::Pc AND m_txInputBus is open), the worker
//   overwrites the radio mic samples in m_in with PC mic samples
//   pulled via AudioEngine::pullTxMic.  Partial pulls (< kBlockFrames)
//   leave the remaining slots filled with the radio mic data — a
//   "smooth degradation" rather than a hard zero-fill.
//
// VOX/DEXP gating (Thetis cmaster.c:388 — `xdexp(tx)`) is deferred until
// create_dexp is ported (separate follow-up).  VOX setters in TxChannel
// are guarded with pdexp[ch]==nullptr null-checks so attempts to enable
// VOX from the UI are no-ops rather than crashes.
//
// Lifecycle:
//   1. Construct (parent = RadioModel).
//   2. setMicSource / setTxChannel / setAudioEngine — all required
//      before startPump().  TxChannel must already be moveToThread()'d
//      to this worker.
//   3. startPump() — calls QThread::start().  The new thread enters
//      run(), which loops on the semaphore until isRunning() goes false.
//   4. stopPump() — calls m_micSource->stop() (which posts the poison
//      semaphore release that breaks the worker out of waitForBlock),
//      then QThread::wait()s for the thread to exit.  Idempotent.
//   5. Destruct (after stopPump and after TxChannel is moved back to
//      its original thread by RadioModel).
// ---------------------------------------------------------------------------
class TxWorkerThread : public QThread {
    Q_OBJECT

public:
    explicit TxWorkerThread(QObject* parent = nullptr);
    ~TxWorkerThread() override;

    /// Set the components the worker drives.  All three must be non-null
    /// before startPump().
    void setTxChannel(TxChannel* ch);
    void setAudioEngine(AudioEngine* engine);
    void setMicSource(TxMicSource* src);

    /// Start the worker.  Internally calls QThread::start().  Idempotent.
    void startPump();

    /// Stop the worker.  Calls m_micSource->stop() first (so the worker
    /// returns from waitForBlock with isRunning()==false), then waits
    /// for the thread to exit.  Idempotent.
    void stopPump();

    /// Block size in mono frames per pump tick.  Mirrors Thetis
    /// getbuffsize(48000) at cmsetup.c:106-110 [v2.10.3.13].
    static constexpr int kBlockFrames = 64;

#ifdef NEREUS_BUILD_TESTS
    /// Test seam — drive one pump tick synchronously without standing up
    /// the QThread + semaphore wait infrastructure.  Drains one block
    /// from m_micSource (must have been pre-loaded via inbound + a
    /// successful waitForBlock by the caller), applies PC mic override
    /// if active, dispatches fexchange0.
    void tickForTest();
#endif

protected:
    /// QThread entry point.  Runs the cm_main-equivalent loop:
    ///   while (m_micSource->isRunning())
    ///     waitForBlock; drainBlock; PcMicOverride?; fexchange0;
    void run() override;

private:
    /// Body of one pump tick.  Used by both run() and tickForTest().
    /// Assumes the caller has already drained the block into m_in
    /// (or the caller is the worker loop, which does the drain itself).
    /// Performs PC mic override + driveOneTxBlockFromInterleaved.
    void dispatchOneBlock();

    TxChannel*    m_txChannel{nullptr};   // not owned
    AudioEngine*  m_audioEngine{nullptr}; // not owned
    TxMicSource*  m_micSource{nullptr};   // not owned

    // Worker-thread scratch — interleaved I/Q double buffer drained from
    // m_micSource each tick.  Sized 2 * kBlockFrames doubles.
    std::vector<double> m_in;

    // PC-mic-override scratch — float buffer for AudioEngine::pullTxMic.
    // Sized kBlockFrames floats.
    std::vector<float> m_pcMicBuf;
};

} // namespace NereusSDR
