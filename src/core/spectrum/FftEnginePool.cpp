// =================================================================
// src/core/spectrum/FftEnginePool.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See FftEnginePool.h for what this
// replaces (MainWindow::createFftEngineForStream + m_fftEngines +
// m_fftThread) and why.
//
// Config application (applyConfigTo) calls the engine's setters directly,
// never marshalled through QMetaObject::invokeMethod onto the engine's
// own thread. That is deliberate, not an oversight: every setter touched
// here (setOutputFps / setFftSizeBaseline / setFftSize /
// setWindowFunction / setHzPerBinTarget) is a plain std::atomic store
// inside FFTEngine (see FFTEngine.cpp), so a direct cross-thread call is
// safe, and it is also the exact pattern MainWindow's own auto-zoom
// lambda already used on primaryFftEngine()->setFftSize()
// (MainWindow.cpp:3862, pre-extraction) -- this is precedent, not a new
// risk. It is also required for setConfig() to be observable
// synchronously: tests/tst_fft_engine_pool.cpp's
// configAppliesToExistingAndFutureEngines reads an existing engine's
// fftSize()/outputFps() immediately after setConfig() returns, with no
// event-loop wait, which only holds if the store already happened.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-02  J.J. Boyd / KG4VCF  Remote daemon R1, extraction 4 of 9.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  Parity Task 17 follow-up: setDecimation
//                                    applies Rendering > Decimation to every
//                                    engine. AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

// Modification history (NereusSDR):
// 2026-09-27: Use the approved shared decimation bounds.
// J.J. Boyd (KG4VCF), AI-assisted implementation via OpenAI Codex.
// 2026-10-10: Each spectrum thread registers with nereusd's thread
// placement, which lets it use the fast cores. J.J. Boyd (KG4VCF),
// AI-assisted implementation via Anthropic Claude Code.
// 2026-10-10: One setter per FFT setting and resetInputHistoryAll
// (R-DSP-30, R-DSP-33). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
// Claude Code.
// 2026-10-11: Follow-up wave F6: each purge has a generation, which the
// frames computed after it carry. J.J. Boyd (KG4VCF), AI-assisted via
// Anthropic Claude Code.

#include "core/ControlRanges.h"
#include "core/spectrum/FftEnginePool.h"

#include "core/LogCategories.h"
#include "core/audio/RealtimeAudioPriority.h"
#include "core/platform/ThreadPlacement.h"

#include <QThread>
#include <QtGlobal>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <utility>

namespace NereusSDR {

FftEnginePool::FftEnginePool(QObject* parent, bool forwardFrameReady)
    : QObject(parent)
    , m_forwardFrameReady(forwardFrameReady)
    , m_elevation(std::make_shared<ElevationState>())
{
}

FftEnginePool::~FftEnginePool()
{
    // Coordinator decision (Task 6 brief): every worker thread must be
    // fully stopped BEFORE any engine parked on it is deleted. A thread
    // that is still running could be mid-feedIQ() (or any other queued
    // slot call) on one of these engines at the exact moment this
    // destructor runs; deleting the engine out from under that call is a
    // use-after-free racing the pool's own teardown.
    //
    // wait() with no timeout blocks until the thread's run() has actually
    // returned (FFTEngine's slots are bounded, non-blocking compute --
    // no external I/O to get stuck in -- so quit() always drains
    // promptly), so by the time this loop completes, nothing can execute
    // on any pooled engine again and a direct delete is safe. This is
    // deliberately NOT deleteLater() connected to QThread::finished():
    // that idiom is correct for a thread that keeps running afterwards
    // (removeStream() below uses it for exactly that reason), but here
    // the thread is going away too, and a plain delete after a completed
    // wait() removes any dependency on event-loop delivery timing during
    // shutdown.
    for (QThread* thread : std::as_const(m_threadsByBucket)) {
        if (thread && thread->isRunning()) {
            thread->quit();
            thread->wait();
        }
    }
    qDeleteAll(m_engines);
    m_engines.clear();
    qDeleteAll(m_threadsByBucket);
    m_threadsByBucket.clear();
}

void FftEnginePool::applyConfigTo(FFTEngine* engine, const FftPoolConfig& cfg)
{
    engine->setOutputFps(cfg.fps);
    engine->setFftSizeBaseline(cfg.fftSize);
    engine->setFftSize(cfg.fftSize);
    engine->setWindowFunction(static_cast<WindowFunction>(cfg.windowType));
    engine->setHzPerBinTarget(cfg.hzPerBinTarget);
}

void FftEnginePool::setConfig(const FftPoolConfig& cfg)
{
    m_config = cfg;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            applyConfigTo(engine, m_config);
        }
    }
}

void FftEnginePool::setConfigForNewStreams(const FftPoolConfig& cfg)
{
    // Deliberately no engine loop -- see the header doc comment on this
    // method and on setConfig() for why. Only m_config changes, which
    // createEngine()'s applyConfigTo() call reads for the next stream;
    // nothing already parked in m_engines is touched.
    m_config = cfg;
}

void FftEnginePool::setDecimation(int factor)
{
    if (factor < ControlRanges::kDisplayDecimationMin
        || factor > ControlRanges::kDisplayDecimationMax) { return; }
    m_decimation = factor;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            engine->setDecimation(factor);
        }
    }
}

void FftEnginePool::setFftSizeBaseline(int size)
{
    // The sizes FFTEngine::setFftSizeBaseline takes.
    if (size < ControlRanges::kDisplayFftPlanMinSize || size > FFTEngine::maximumFftSize()
        || (size & (size - 1)) != 0) {
        qCWarning(lcSpectrum) << "FftEnginePool: FFT size baseline" << size
                              << "is not a power of two in"
                              << ControlRanges::kDisplayFftPlanMinSize << "to"
                              << FFTEngine::maximumFftSize() << "; ignored";
        return;
    }
    m_config.fftSize = size;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            engine->setFftSizeBaseline(size);
        }
    }
}

void FftEnginePool::setSizing(ControlRanges::DisplayFftSizingMode mode, int baseline,
                              double hzPerBinTarget)
{
    // The sizes FFTEngine::setFftSizeBaseline takes.
    if (baseline < ControlRanges::kDisplayFftPlanMinSize
        || baseline > FFTEngine::maximumFftSize() || (baseline & (baseline - 1)) != 0) {
        qCWarning(lcSpectrum) << "FftEnginePool: FFT sizing baseline" << baseline
                              << "is not a power of two in"
                              << ControlRanges::kDisplayFftPlanMinSize << "to"
                              << FFTEngine::maximumFftSize() << "; ignored";
        return;
    }
    // !(hz > 0) is also true for NaN.
    if (mode != ControlRanges::DisplayFftSizingMode::HzPerBin || !(hzPerBinTarget > 0.0)
        || !std::isfinite(hzPerBinTarget)) {
        hzPerBinTarget = 0.0;
    }
    const bool modeChanged = m_config.sizingMode != mode;
    m_config.sizingMode = mode;
    m_config.fftSize = baseline;
    m_config.hzPerBinTarget = hzPerBinTarget;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (!engine) {
            continue;
        }
        const bool baselineStays = engine->fftSizeBaseline() == baseline;
        // Says fftSizeBaselineChanged itself when the baseline moves.
        engine->setFftSizeBaseline(baseline);
        engine->setHzPerBinTarget(hzPerBinTarget);
        if (modeChanged && baselineStays) {
            engine->announceFftSizingChanged();
        }
    }
}

void FftEnginePool::setWindowType(int windowType)
{
    if (windowType < 0 || windowType >= static_cast<int>(WindowFunction::Count)) {
        qCWarning(lcSpectrum) << "FftEnginePool: FFT window" << windowType
                              << "is not a window; ignored";
        return;
    }
    m_config.windowType = windowType;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            engine->setWindowFunction(static_cast<WindowFunction>(windowType));
        }
    }
}

void FftEnginePool::setHzPerBinTarget(double hz)
{
    // !(hz > 0) is also true for NaN.
    if (!(hz > 0.0) || !std::isfinite(hz)) { hz = 0.0; }
    m_config.hzPerBinTarget = hz;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            engine->setHzPerBinTarget(hz);
        }
    }
}

void FftEnginePool::setOutputFps(int fps)
{
    // FFTEngine::setOutputFps's own clamp, so config() reads what runs.
    fps = qBound(1, fps, 60);
    m_config.fps = fps;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            engine->setOutputFps(fps);
        }
    }
}

void FftEnginePool::setThreadPriority(QThread::Priority priority)
{
    // R-R3-41: nereusd's placement owns these threads' priority there.
    if (ThreadPlacement::managesThreadPriority()) { return; }
    // Held across the calls below so a worker thread that is just starting
    // either elevated itself already or will see the choice and not try.
    const std::lock_guard<std::mutex> lock(m_elevation->mutex);
    m_elevation->priorityChosen = true;
    m_threadPriority = priority;
    for (QThread* thread : std::as_const(m_threadsByBucket)) {
        if (thread && thread->isRunning()) {
            thread->setPriority(priority);
        }
    }
}

void FftEnginePool::resetInputHistoryAll()
{
    // The engine takes the new generation in the same queued call that
    // drops its input, on its own thread, so the stamp on a frame says
    // which side of the purge its samples are from.
    const quint64 generation = ++m_inputGeneration;
    for (FFTEngine* engine : std::as_const(m_engines)) {
        if (engine) {
            QMetaObject::invokeMethod(engine, [engine, generation]() {
                engine->purgeInputForGeneration(generation);
            }, Qt::QueuedConnection);
        }
    }
}

FFTEngine* FftEnginePool::engineForStream(int streamIndex)
{
    if (streamIndex < 0) { return nullptr; }
    return engineForSource({streamIndex, FftTier::Wide}, m_config);
}

FFTEngine* FftEnginePool::engineForSource(const FftSourceKey& key,
                                          const FftPoolConfig& cfg)
{
    if (key.streamIndex < 0) { return nullptr; }
    if (FFTEngine* existing = m_engines.value(key, nullptr)) {
        return existing;
    }
    return createEngine(key, cfg);
}

FFTEngine* FftEnginePool::createEngine(const FftSourceKey& key,
                                       const FftPoolConfig& cfg)
{
    // No QObject parent: this pool owns the engine's lifetime directly
    // (removeStream()'s deleteLater(), or the destructor's quit-wait-then-
    // delete above), matching the ownership model
    // createFftEngineForStream used before this extraction.
    auto* engine = new FFTEngine(key.streamIndex);

    // Configured from the pool's current config, on the calling thread --
    // this runs before moveToThread() below, so it is a same-thread call
    // exactly like the old createFftEngineForStream's construction-time
    // setters were (all of them ran before that function's own
    // moveToThread call).
    applyConfigTo(engine, cfg);
    if (m_decimation) {
        engine->setDecimation(*m_decimation);
    }
    // An engine made after a purge holds nothing from before it.
    engine->setInputGeneration(m_inputGeneration);

    const int threadCount = qMax(1, m_config.threadCount);
    const int bucket = key.streamIndex % threadCount;
    QThread* thread = m_threadsByBucket.value(bucket, nullptr);
    const bool threadIsNew = (thread == nullptr);
    if (threadIsNew) {
        thread = new QThread(this);
        thread->setObjectName(QStringLiteral("SpectrumThread%1").arg(bucket));
        m_threadsByBucket.insert(bucket, thread);
    }

    engine->moveToThread(thread);

    if (threadIsNew) {
        // 2026-05-25/26 KG4VCF bench fix, carried over from MainWindow
        // (MainWindow.cpp:3105-3119 pre-extraction): elevate the FFT
        // thread's scheduling priority as soon as it starts, so compile
        // workers and other DEFAULT-QoS work consistently lose the
        // time-slice race against live spectrum production. The context
        // object must be `engine`, not `thread` -- a QThread instance's
        // own affinity is the thread that CREATED it, not the thread it
        // manages, so connecting with `thread` as context would deliver
        // this lambda on the wrong thread. `engine` was just moved onto
        // `thread` above, so Auto-connection resolves to Direct: the
        // lambda runs ON the worker thread once started() fires. One
        // connection per thread is enough for every engine parked on it
        // afterwards, same as the pre-extraction code's single
        // connection using stream 0's engine as context for the whole
        // (then-implicitly-one) shared thread.
        // nereusd runs several pools (one per spectrum source), each with
        // a bucket 0, so placement tells their threads apart by a number
        // of its own, taken once per thread.
        static std::atomic<int> nextPlacementId{0};
        const int placementId = nextPlacementId.fetch_add(1, std::memory_order_relaxed);
        // A priority setThreadPriority() chose replaces that elevation,
        // also when it is chosen between start() and this lambda: the two
        // take turns under m_elevation's mutex, because macOS refuses the
        // elevation on a thread whose priority was set first.
        connect(thread, &QThread::started, engine, [placementId, elevation = m_elevation]() {
            // R-R3-41: nereusd's placement owns this thread's cores and
            // skips the nice call there. 2026-10-10: where slower cores
            // run everything else it may use every fast core, at normal
            // priority, and is never held to one (ThreadPlacement.h).
            if (ThreadPlacement::managesThreadPriority()) {
                ThreadPlacement::instance().registerCurrentThread(
                    ThreadRole::SpectrumThread, placementId);
                return;
            }
            const std::lock_guard<std::mutex> lock(elevation->mutex);
            if (!elevation->priorityChosen) {
                elevateLatencyCriticalThreadPriority();
            }
        });
        // finished() is emitted on the thread itself, so a direct
        // connection forgets it before its ID can be reused.
        connect(thread, &QThread::finished, thread, []() {
            if (ThreadPlacement::managesThreadPriority()) {
                ThreadPlacement::instance().deregisterCurrentThread();
            }
        }, Qt::DirectConnection);
        thread->start(m_threadPriority.value_or(QThread::InheritPriority));
    }

    if (m_forwardFrameReady) {
        // Re-emit with the stream index attached, matching the shape
        // MainWindow::dispatchFftFrameToPans expects. fftReadyLinear and
        // fftFrameReady share the same parameter list (both carry the
        // engine's receiverId first), so this is a plain signal-to-signal
        // forward, no lambda needed.
        connect(engine, &FFTEngine::fftReadyLinear, this, &FftEnginePool::fftFrameReady);
    }

    m_engines.insert(key, engine);
    return engine;
}

void FftEnginePool::removeStream(int streamIndex)
{
    const QList<FftSourceKey> keys = m_engines.keys();
    for (const FftSourceKey& key : keys) {
        if (key.streamIndex == streamIndex) {
            removeSource(key);
        }
    }
}

void FftEnginePool::removeSource(const FftSourceKey& key)
{
    FFTEngine* engine = m_engines.take(key);
    if (!engine) { return; }
    // Disconnect first so a frame already in flight toward fftFrameReady
    // does not fire for a stream that is being removed right now (mirrors
    // MainWindow::disconnectPanadapter's "disconnect first, then remove"
    // pattern). deleteLater() is thread-safe to call from any thread --
    // it posts to the engine's OWN thread's queue regardless of which
    // thread calls it -- and that thread keeps running for whatever
    // other streams still share it (or, in the threadCount == 1 default,
    // for every other stream), so the deferred delete drains normally.
    engine->disconnect();
    engine->deleteLater();
}

QList<int> FftEnginePool::streams() const
{
    QSet<int> streamSet;
    for (const FftSourceKey& key : m_engines.keys()) {
        streamSet.insert(key.streamIndex);
    }
    QList<int> result = streamSet.values();
    std::sort(result.begin(), result.end());
    return result;
}

QList<FftSourceKey> FftEnginePool::sources() const
{
    return m_engines.keys();
}

int FftEnginePool::engineCount() const
{
    return m_engines.size();
}

} // namespace NereusSDR
