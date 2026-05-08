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
#include <QVector>

#include <atomic>
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

public slots:
    // ── Anti-VOX queued slots (3M-3a-iv) ─────────────────────────────────
    //
    // These four slots form the worker-thread proxy for anti-VOX state and
    // sample-feed.  Wired in RadioModel (Task 9) via QueuedConnection so
    // emissions from the main thread (MoxController, RxDspWorker) deliver
    // onto the worker's event queue and apply between waitForBlock cycles
    // (cf. the run() narrative on cross-thread queued setter delivery).
    //
    // Architecture: TxChannel itself lives on this worker thread once
    // RadioModel::connectToRadio() runs moveToThread().  These wrappers
    // exist on TxWorkerThread (a QThread) rather than TxChannel because
    // (a) TxChannel methods are not declared as Qt slots, and (b) the
    // m_antiVoxRun atomic gate is a worker-local optimisation that skips
    // the float→double conversion in TxChannel::sendAntiVoxData when the
    // user has anti-VOX off.

    // setAntiVoxRun: forwards run flag to TxChannel::setAntiVoxRun AND
    // mirrors it into m_antiVoxRun (release-store) so onAntiVoxSamplesReady
    // can short-circuit via acquire-load when anti-VOX is disabled.
    //
    // From Thetis cmaster.cs:208-209 [v2.10.3.13] — SetAntiVOXRun.
    void setAntiVoxRun(bool run);

    // onAntiVoxSamplesReady: receive RX audio fork from RxDspWorker and
    // pump it into TxChannel::sendAntiVoxData when anti-VOX is enabled.
    // Gates on m_antiVoxRun (acquire) so the float→double conversion in
    // TxChannel is skipped when the user has anti-VOX off.
    //
    // Single-RX equivalent of Thetis ChannelMaster aamix output stage
    // (cmaster.c:159-175 [v2.10.3.13]) — aamix mixes N RXs into one
    // anti-VOX stream and calls SendAntiVOXData; with one RX in 3M-3a-iv
    // we skip the mixer entirely and pump the single RX block directly.
    void onAntiVoxSamplesReady(int sliceId, const QVector<float>& interleaved, int sampleCount);

    // setAntiVoxBlockGeometry: queued slot for RxDspWorker::bufferSizesChanged.
    // Calls both TxChannel::setAntiVoxSize and TxChannel::setAntiVoxRate so
    // DEXP's antivox_size and antivox_rate stay aligned with the RX-side
    // post-decimation block.
    //
    // From Thetis cmaster.c:154-155 [v2.10.3.13] — DEXP create-time uses
    // pcm->audio_outsize / pcm->audio_outrate (the post-decimation audio
    // output dimensions) for anti-VOX detector geometry.
    void setAntiVoxBlockGeometry(int outSize, double outRate);

    // setAntiVoxDetectorTau: queued slot for MoxController::antiVoxDetectorTauRequested.
    // Pass-through to TxChannel::setAntiVoxDetectorTau (which calls WDSP
    // SetAntiVOXDetectorTau).  Tau here is in seconds; ms→s conversion
    // already done by MoxController.
    //
    // From Thetis setup.cs:18992-18996 [v2.10.3.13] —
    //   private void udAntiVoxTau_ValueChanged(...)
    //   { cmaster.SetAntiVOXDetectorTau(0, (double)udAntiVoxTau.Value / 1000.0); }
    void setAntiVoxDetectorTau(double seconds);

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

    // Anti-VOX run gate (3M-3a-iv).  Mirrors the most-recent
    // setAntiVoxRun(bool) call.  Read with acquire in
    // onAntiVoxSamplesReady, written with release in setAntiVoxRun.
    // Default false matches the existing TxChannel::m_antiVoxRunLast
    // default and the WDSP DEXP create-time `antivox_run = 0` argument
    // (cmaster.c:153 [v2.10.3.13]).
    //
    // Skipping the float→double conversion in TxChannel::sendAntiVoxData
    // when anti-VOX is off is a worker-local optimisation; DEXP itself
    // also has a state-gate at dexp.c:288-297 [v2.10.3.13]
    // (`if (a->state == DEXP_LOW && a->antivox_new != 0)`), but that
    // gate fires AFTER the buffer copy, so the outer atomic saves the
    // conversion + memcpy entirely.
    std::atomic<bool> m_antiVoxRun{false};
};

} // namespace NereusSDR
