// =================================================================
// src/core/audio/RemoteVaxFeeder.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-44; see RemoteVaxFeeder.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/audio/RemoteVaxFeeder.h"

#include "core/AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cstring>

namespace NereusSDR {

namespace {

constexpr std::size_t kFrameBytes = 2 * sizeof(float);

qint64 steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

} // namespace

VaxOutputPort VaxOutputPort::forEngine(AudioEngine* engine, int channel)
{
    VaxOutputPort port;
    port.pacing = [engine, channel]() -> std::optional<IAudioBus::OutputPacing> {
        return engine ? engine->vaxOutputPacing(channel) : std::nullopt;
    };
    port.write = [engine, channel](const float* stereo, int frames) {
        return engine && engine->writeVaxOutput(channel, stereo, frames);
    };
    return port;
}

RemoteVaxFeeder::RemoteVaxFeeder(int channel, VaxOutputPort port, Clock clock)
    : m_channel(channel)
    , m_port(std::move(port))
    , m_clock(clock ? std::move(clock) : Clock(&steadyNowNs))
{
    m_chunk.resize(kInputChunkFrames * 2);
    m_direct.resize(kOutputBlockFrames * 2);
}

RemoteVaxFeeder::~RemoteVaxFeeder()
{
    stopWorker();
}

void RemoteVaxFeeder::setSourceSlice(int sliceId)
{
    // The pump drops what the previous slice left in the hand-off ring (up
    // to here; nothing of it follows once its stream is released) and
    // starts the rate matcher afresh.
    m_dropUntilBytes.store(m_pushedBytes.load(std::memory_order_acquire),
                           std::memory_order_release);
    m_sourceSlice.store(sliceId < 0 ? -1 : sliceId, std::memory_order_release);
    m_generation.fetch_add(1, std::memory_order_acq_rel);
    m_lastStopReason.clear();
}

void RemoteVaxFeeder::receiverAudioBlock(int sliceId, const float* interleavedStereo, int frames)
{
    // Receive worker, under the stream's lock: copy and return.
    if (interleavedStereo == nullptr || frames <= 0
        || sliceId != m_sourceSlice.load(std::memory_order_acquire)) {
        return;
    }
    const std::size_t bytes = std::size_t(frames) * kFrameBytes;
    // All or nothing, so the ring stays whole stereo frames. This thread is
    // the only writer, so the free space only grows until the push.
    if (m_handoff.usedBytes() + bytes > m_handoff.capacity() - 1) {
        m_droppedFrames.fetch_add(quint64(frames), std::memory_order_relaxed);
        return;
    }
    m_handoff.tryPushCopy(reinterpret_cast<const uint8_t*>(interleavedStereo),
                          qint64(bytes));
    m_pushedBytes.fetch_add(quint64(bytes), std::memory_order_release);
    m_receivedFrames.fetch_add(quint64(frames), std::memory_order_relaxed);
}

void RemoteVaxFeeder::receiverAudioStopped(int sliceId, const QString& reason)
{
    if (sliceId != m_sourceSlice.load(std::memory_order_acquire)) {
        return;
    }
    m_lastStopReason = reason;
    m_stopAtFrames = m_receivedFrames.load(std::memory_order_relaxed);
}

QString RemoteVaxFeeder::lastStopReason() const
{
    if (m_receivedFrames.load(std::memory_order_relaxed) > m_stopAtFrames) {
        return {};
    }
    return m_lastStopReason;
}

void RemoteVaxFeeder::startWorker()
{
    if (m_worker.joinable()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerStop = false;
    }
    m_worker = std::thread([this] {
        std::unique_lock<std::mutex> lock(m_workerMutex);
        while (!m_workerStop) {
            lock.unlock();
            pump();
            lock.lock();
            m_workerWake.wait_for(lock, std::chrono::nanoseconds(kPumpIntervalNs),
                                  [this] { return m_workerStop; });
        }
    });
}

void RemoteVaxFeeder::stopWorker()
{
    {
        std::lock_guard<std::mutex> lock(m_workerMutex);
        m_workerStop = true;
    }
    m_workerWake.notify_all();
    if (m_worker.joinable()) {
        m_worker.join();
    }
}

bool RemoteVaxFeeder::workerRunning() const
{
    return m_worker.joinable();
}

void RemoteVaxFeeder::dropHandoff()
{
    const std::size_t used = m_handoff.usedBytes();
    m_handoff.dropOldest(used);
    m_poppedBytes += used;
}

void RemoteVaxFeeder::popHandoff(void* into, std::size_t bytes)
{
    m_handoff.popInto(static_cast<uint8_t*>(into), qint64(bytes));
    m_poppedBytes += bytes;
}

qint64 RemoteVaxFeeder::pausedNs() const
{
    // Twice the largest read step the output has shown, at 48 kHz.
    const qint64 step = qint64(m_largestStep) * 1'000'000'000 / 48'000;
    return std::max(kPausedNs, 2 * step);
}

void RemoteVaxFeeder::restartMatcher()
{
    if (m_matcherReady) {
        m_matcher.reset();
    }
}

int RemoteVaxFeeder::drainHandoff(bool paced)
{
    int moved = 0;
    if (paced) {
        if (!m_matcherTried) {
            m_matcherTried = true;
            m_matcherReady = m_matcher.configure(kInputChunkFrames, kOutputBlockFrames,
                                                 kMatcherRingFrames);
        }
        if (!m_matcherReady) {
            // No rate matcher in this build: fall back to writing as the
            // audio arrives.
            return drainHandoff(false);
        }
        const std::size_t chunkBytes = std::size_t(kInputChunkFrames) * kFrameBytes;
        while (m_handoff.usedBytes() >= chunkBytes) {
            popHandoff(m_chunk.data(), chunkBytes);
            if (!m_matcher.push(m_chunk)) {
                ++m_restarts;
                restartMatcher();
                break;
            }
            moved += kInputChunkFrames;
        }
        return moved;
    }
    const std::size_t blockBytes = std::size_t(kOutputBlockFrames) * kFrameBytes;
    for (;;) {
        const std::size_t used = m_handoff.usedBytes();
        const std::size_t bytes = std::min(used - used % kFrameBytes, blockBytes);
        if (bytes == 0) {
            break;
        }
        popHandoff(m_direct.data(), bytes);
        const int frames = int(bytes / kFrameBytes);
        if (m_port.write && m_port.write(m_direct.constData(), frames)) {
            m_writtenFrames += quint64(frames);
        }
        moved += frames;
    }
    return moved;
}

void RemoteVaxFeeder::pump()
{
    const qint64 now = m_clock();
    const quint32 generation = m_generation.load(std::memory_order_acquire);
    if (generation != m_seenGeneration) {
        m_seenGeneration = generation;
        // Only what the previous slice left.
        const quint64 until = m_dropUntilBytes.load(std::memory_order_acquire);
        if (until > m_poppedBytes) {
            const std::size_t stale = std::size_t(until - m_poppedBytes);
            m_handoff.dropOldest(stale);
            m_poppedBytes += stale;
        }
        restartMatcher();
        m_state = State::WaitingForAudio;
        m_haveConsumed = false;
        m_lastInputNs = now;
    }
    if (m_sourceSlice.load(std::memory_order_acquire) < 0) {
        dropHandoff();
        m_state = State::Idle;
        publish(m_state, 0);
        return;
    }

    const std::optional<IAudioBus::OutputPacing> pacing =
        m_port.pacing ? m_port.pacing() : std::nullopt;
    if (!pacing) {
        // No playback timing to pace by (or the output is closed): write
        // what arrived as it arrives.
        const int moved = drainHandoff(false);
        if (moved > 0) {
            m_lastInputNs = now;
            m_state = State::Playing;
        } else if (m_state == State::Playing && now - m_lastInputNs > kQuietNs) {
            m_state = State::WaitingForAudio;
        }
        publish(m_state, 0);
        return;
    }

    // The output's clock: frames an app has taken from it.
    if (!m_haveConsumed || pacing->consumedFrames != m_lastConsumed) {
        if (m_haveConsumed) {
            // The largest step seen between two pumps: an app reading big
            // blocks needs that much queued, whatever the output reports.
            const quint64 step = pacing->consumedFrames - m_lastConsumed;
            const quint64 cap = quint64(std::max(0, pacing->capacityFrames / 4));
            m_largestStep = std::max(m_largestStep, int(std::min(step, cap)));
        }
        m_haveConsumed = true;
        m_lastConsumed = pacing->consumedFrames;
        m_lastProgressNs = now;
        if (m_state == State::NoReader) {
            // Something reads it again.
            restartMatcher();
            m_state = State::WaitingForAudio;
            m_lastInputNs = now;
        }
    } else if (m_state != State::NoReader && pacing->queuedFrames > 0
               && now - m_lastProgressNs > kNoReaderNs) {
        // Nothing has taken audio for half a second with audio waiting:
        // no app is reading this VAX output. Not a fault; stop writing.
        restartMatcher();
        m_state = State::NoReader;
    }
    if (m_state == State::NoReader) {
        dropHandoff();
        publish(m_state, pacing->queuedFrames);
        return;
    }
    const bool outputPaused = pacing->queuedFrames > 0 && now - m_lastProgressNs > pausedNs();
    if (outputPaused) {
        // The output stopped taking audio (an app closing it, or pausing).
        // Feeding the rate matcher now would only run it over; drop what
        // arrives and start afresh when the output moves again.
        dropHandoff();
        if (m_state == State::Playing) {
            restartMatcher();
            m_state = State::WaitingForAudio;
        }
        publish(m_state, pacing->queuedFrames);
        return;
    }

    const int moved = drainHandoff(true);
    if (!m_matcherReady) {
        // drainHandoff() fell back to direct writes.
        if (moved > 0) {
            m_lastInputNs = now;
            m_state = State::Playing;
        }
        publish(m_state, pacing->queuedFrames);
        return;
    }
    if (moved > 0) {
        m_lastInputNs = now;
        m_state = State::Playing;
    } else if (m_state == State::Playing && now - m_lastInputNs > kQuietNs) {
        // The Core went quiet: let the output run out rather than play the
        // rate matcher dry, and start afresh when audio returns.
        restartMatcher();
        m_state = State::WaitingForAudio;
    }
    if (m_state != State::Playing) {
        publish(m_state, pacing->queuedFrames);
        return;
    }

    // Keep one output block beyond the largest step the output takes at
    // once, and never less than 20 ms, as the remote speaker does.
    const int step = std::max(pacing->callbackFrames, m_largestStep);
    int target = std::max(2 * kOutputBlockFrames, step + kOutputBlockFrames);
    target = std::min(target, pacing->capacityFrames - kOutputBlockFrames);
    int queued = pacing->queuedFrames;
    const int maxBlocks = std::max(0, pacing->capacityFrames / kOutputBlockFrames);
    for (int i = 0; i < maxBlocks && queued < target; ++i) {
        const QVector<float> pcm = m_matcher.take();
        if (pcm.size() != kOutputBlockFrames * 2 || !m_port.write
            || !m_port.write(pcm.constData(), kOutputBlockFrames)) {
            break;
        }
        queued += kOutputBlockFrames;
        m_writtenFrames += kOutputBlockFrames;
    }
    const RemoteAudioRateMatcherStats matcherStats = m_matcher.stats();
    if (matcherStats.underflows > 0 || matcherStats.overflows > 0) {
        ++m_restarts;
        restartMatcher();
        m_state = State::WaitingForAudio;
    }
    publish(m_state, queued);
}

void RemoteVaxFeeder::publish(State state, int queuedFrames)
{
    RemoteVaxFeederStats stats;
    stats.state = state;
    stats.sourceSliceId = m_sourceSlice.load(std::memory_order_acquire);
    stats.receivedFrames = m_receivedFrames.load(std::memory_order_relaxed);
    stats.droppedFrames = m_droppedFrames.load(std::memory_order_relaxed);
    stats.writtenFrames = m_writtenFrames;
    stats.restarts = m_restarts;
    stats.queuedFrames = std::max(0, queuedFrames);
    stats.handoffFrames = int(m_handoff.usedBytes() / kFrameBytes);
    stats.paced = m_haveConsumed || state == State::NoReader;
    if (m_matcherReady && state == State::Playing) {
        const RemoteAudioRateMatcherStats matcherStats = m_matcher.stats();
        stats.matcherFillFrames = std::max(0, matcherStats.ringFillFrames);
        if (matcherStats.controlActive) {
            stats.ratio = matcherStats.currentRatio;
        }
    }
    std::lock_guard<std::mutex> lock(m_statsMutex);
    m_stats = stats;
}

RemoteVaxFeederStats RemoteVaxFeeder::stats() const
{
    std::lock_guard<std::mutex> lock(m_statsMutex);
    return m_stats;
}

} // namespace NereusSDR
