// =================================================================
// src/core/audio/LinuxPipeBus.cpp  (NereusSDR)
// =================================================================
//
// Ported from AetherSDR source:
//   src/core/PipeWireAudioBridge.{h,cpp}
//
// AetherSDR is licensed under the GNU General Public License v3; see
// https://github.com/ten9876/AetherSDR for the contributor list and
// project-level LICENSE. NereusSDR is also GPLv3. AetherSDR source
// files carry no per-file GPL header; attribution is at project level
// per AetherSDR convention.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-19 — Ported/adapted in C++20 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Adapted to NereusSDR IAudioBus contract:
//                 monolithic PipeWireAudioBridge decomposed into per-endpoint
//                 LinuxPipeBus instances (Role enum for Vax1..4 / TxInput),
//                 QObject/signals dropped in favour of atomic metering,
//                 silence-fill + TX poll timers + per-channel/TX gain deferred
//                 to Phase 3M. Pipe paths: /tmp/aethersdr-dax-* →
//                 /tmp/nereussdr-vax-*. Sample rate/format: 24 kHz mono int16
//                 → 48 kHz stereo float32 (spec §8.1).
//   2026-10-14: Protect each VAX role with a generation ownership receipt.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// =================================================================

#include "LinuxPipeBus.h"

#include "core/LogCategories.h"

#include <QLoggingCategory>
#include <QProcess>

#ifdef Q_OS_LINUX
#include "LinuxPipeOwnership.h"
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>


namespace NereusSDR {

namespace {

// ── FIFO path table ──────────────────────────────────────────────────────────
//
// Pipe paths agreed in spec §8.1 / Phase 3O VAX plan Sub-Phase 6.
// Stored as string literals so the .h can hand out a `const char*`
// without owning a QString or QByteArray.
//
// Note: path suffix differs from CoreAudioHalBus shm names (/nereussdr-vax-tx
// shm vs /tmp/nereussdr-vax-tx.pipe FIFO). Do not cross-wire.
constexpr const char* kPipePathVax1   = "/tmp/nereussdr-vax-1.pipe";
constexpr const char* kPipePathVax2   = "/tmp/nereussdr-vax-2.pipe";
constexpr const char* kPipePathVax3   = "/tmp/nereussdr-vax-3.pipe";
constexpr const char* kPipePathVax4   = "/tmp/nereussdr-vax-4.pipe";
constexpr const char* kPipePathTxIn   = "/tmp/nereussdr-vax-tx.pipe";

const char* pipePathForRole(LinuxPipeBus::Role role) {
    switch (role) {
        case LinuxPipeBus::Role::Vax1:    return kPipePathVax1;
        case LinuxPipeBus::Role::Vax2:    return kPipePathVax2;
        case LinuxPipeBus::Role::Vax3:    return kPipePathVax3;
        case LinuxPipeBus::Role::Vax4:    return kPipePathVax4;
        case LinuxPipeBus::Role::TxInput: return kPipePathTxIn;
    }
    return kPipePathVax1;
}

// ── pactl helper ─────────────────────────────────────────────────────────────
//
// Runs `pactl <args>` and returns the stdout (trimmed). Returns an empty
// QByteArray on timeout or non-zero exit. Works with both PulseAudio and
// PipeWire-via-pipewire-pulse — pactl is the stable interface on both.
// Ported verbatim structure from AetherSDR PipeWireAudioBridge::runPactl().
struct PactlResult {
    QByteArray output;
    bool neverStarted{false};
};

static PactlResult runPactl(const QStringList& args)
{
    QProcess proc;
    proc.start(QStringLiteral("pactl"), args);
    if (!proc.waitForStarted(1000)) {
        qCWarning(lcAudio) << "LinuxPipeBus: pactl did not start:" << proc.errorString();
        return {{}, proc.error() == QProcess::FailedToStart};
    }
    if (!proc.waitForFinished(5000)) {
        qCWarning(lcAudio) << "LinuxPipeBus: pactl timed out:" << args;
        return {};
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        qCWarning(lcAudio) << "LinuxPipeBus: pactl failed:"
                           << proc.readAllStandardError().trimmed();
        return {};
    }
    return {proc.readAllStandardOutput().trimmed(), false};
}

// ── Stale-module cleanup ──────────────────────────────────────────────────────
//
// Unload any stale nereussdr-vax pipe modules left over from a previous
// crashed session. Ported from AetherSDR PipeWireAudioBridge::cleanupStaleModules()
// with the module name prefix rebrand aethersdr- → nereussdr-.
//
// Runs ONCE per process, gated by a std::once_flag, so that creating multiple
// LinuxPipeBus instances (one per Role) doesn't re-run the scan and
// accidentally unload sibling buses' just-loaded modules.
// [original inline comment from LinuxPipeBus.cpp:107-134]
// The former once-per-process implementation has been replaced by per-role
// ownership; the historical comments above describe the unsafe original.
// Exposed as a private call from open() — call_once ensures it runs at most once.

} // namespace

// ── Constructor / Destructor ─────────────────────────────────────────────────

LinuxPipeBus::LinuxPipeBus(Role role)
    : m_role(role)
    , m_pipePath(pipePathForRole(role))
    , m_backendName(QStringLiteral("PulseAudio/PipeWire (VAX)"))
{}

LinuxPipeBus::~LinuxPipeBus() {
    close();
}

// ── open() ───────────────────────────────────────────────────────────────────

bool LinuxPipeBus::open(const AudioFormat& format) {
#ifndef Q_OS_LINUX
    (void)format;
    m_err = QStringLiteral("LinuxPipeBus is Linux-only");
    return false;
#else
    if (m_open) {
        return true;
    }

    // Fixed format — the pactl module-pipe-source/sink is loaded with
    // format=float32le rate=48000 channels=2. Reject anything else cleanly
    // rather than silently mis-routing samples.
    if (format.sampleRate != 48000
     || format.channels   != 2
     || format.sample     != AudioFormat::Sample::Float32) {
        m_err = QStringLiteral("LinuxPipeBus requires 48000 Hz stereo float32 "
                               "(got %1 Hz, %2 ch, sample=%3)")
                    .arg(format.sampleRate)
                    .arg(format.channels)
                    .arg(static_cast<int>(format.sample));
        return false;
    }

    // [original inline comment from LinuxPipeBus.cpp:176-185]
    // Stale-module cleanup runs once per process before the first module load.
    // Using std::call_once so sibling bus instances don't re-run the scan
    // and accidentally clobber each other's freshly-loaded modules.
    // Remove any leftover FIFO from a previous session.
    // Create the named FIFO that the pactl module will read from (RX)
    // or write to (TX).
    // These comments describe the former global cleanup/precreated FIFO.
    // Admission now precedes every resource action; the server creates the
    // FIFO in a private generation directory (PulseAudio v17 pipe-source).
    m_ownership = std::make_unique<LinuxPipeOwnership>(static_cast<int>(m_role));
    if (!m_ownership->begin()) {
        m_err = m_ownership->errorString();
        m_ownership.reset();
        return false;
    }
    m_generationPath = m_ownership->pipePath();
    m_pipePath = m_generationPath.constData();

    // Load the appropriate pactl module. The module index returned on stdout
    // is what we need to unload in close().
    QString fileValue = QString::fromLocal8Bit(m_pipePath);
    fileValue.replace('\\', QStringLiteral("\\\\"));
    fileValue.replace('"', QStringLiteral("\\\""));
    const QString fileArgument = QStringLiteral("file=\"%1\"").arg(fileValue);
    QStringList pactlArgs;
    if (isProducer()) {
        // Role::Vax1..4 — pipe-source: NereusSDR writes, apps (WSJT-X etc.) read.
        const int vaxNum = static_cast<int>(m_role);
        const QString sourceName = QStringLiteral("nereussdr-vax-%1").arg(vaxNum);
        const QString sourceDesc = QStringLiteral("NereusSDR VAX %1").arg(vaxNum);
        pactlArgs = {
            QStringLiteral("load-module"),
            QStringLiteral("module-pipe-source"),
            fileArgument,
            QStringLiteral("source_name=%1").arg(sourceName),
            QStringLiteral("source_properties=device.description=\"%1\"").arg(sourceDesc),
            QStringLiteral("format=float32le"),
            QStringLiteral("rate=48000"),
            QStringLiteral("channels=2"),
        };
    } else {
        // Role::TxInput — pipe-sink: apps write TX audio, NereusSDR reads.
        // Small pipe_size (~1024 bytes at 48 kHz stereo float32 ≈ 2.7ms)
        // keeps TX latency low for digital modes like FT8/FT4.
        pactlArgs = {
            QStringLiteral("load-module"),
            QStringLiteral("module-pipe-sink"),
            fileArgument,
            QStringLiteral("sink_name=nereussdr-vax-tx"),
            QStringLiteral("sink_properties=device.description=\"NereusSDR TX\""),
            QStringLiteral("format=float32le"),
            QStringLiteral("rate=48000"),
            QStringLiteral("channels=2"),
            QStringLiteral("pipe_size=1024"),
        };
    }

    if (!m_ownership->markLoadAttempt()) {
        m_err = m_ownership->errorString();
        close();
        return false;
    }
    const PactlResult load = runPactl(pactlArgs);
    if (load.neverStarted && !m_ownership->cancelUnstartedLoad()) {
        qCWarning(lcAudio) << m_ownership->errorString();
    }
    const QByteArray modIdxBytes = load.output;
    if (modIdxBytes.isEmpty()) {
        m_err = QStringLiteral("pactl load-module failed for %1")
                    .arg(QString::fromUtf8(m_pipePath));
        qCWarning(lcAudio) << "LinuxPipeBus:" << m_err;
        close();
        return false;
    }

    if (!m_ownership->captureFifo()) {
        m_err = m_ownership->errorString();
        close();
        return false;
    }

    bool ok = false;
    const uint32_t modIdx = modIdxBytes.toUInt(&ok);
    // Only the 'ok' bool gates validity — index 0 is a legitimate module index
    // on a freshly-started PulseAudio / PipeWire-pulse daemon. The old guard
    // "!ok || modIdx == 0" would incorrectly reject a valid module-0 assignment.
    if (!ok) {
        m_err = QStringLiteral("pactl returned unexpected module index '%1' for %2")
                    .arg(QString::fromUtf8(modIdxBytes))
                    .arg(QString::fromUtf8(m_pipePath));
        qCWarning(lcAudio) << "LinuxPipeBus:" << m_err;
        close();
        return false;
    }

    if (!m_ownership->confirmModule(modIdx)) {
        m_err = m_ownership->errorString();
        close();
        return false;
    }

    // Open the FIFO file descriptor.
    // Producers write (O_WRONLY | O_NONBLOCK) — non-blocking so we don't
    // hang if the pactl module hasn't opened its read end yet.
    // Consumers read (O_RDONLY | O_NONBLOCK) — same rationale.
    const int flags = isProducer()
        ? (O_WRONLY | O_NONBLOCK)
        : (O_RDONLY | O_NONBLOCK);

    const int fd = ::open(m_pipePath, flags | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) {
        m_err = QStringLiteral("open(%1) failed: errno=%2")
                    .arg(QString::fromUtf8(m_pipePath)).arg(errno);
        qCWarning(lcAudio) << "LinuxPipeBus:" << m_err;
        close();
        return false;
    }
    if (!m_ownership->acceptFd(fd)) {
        ::close(fd);
        m_err = m_ownership->errorString();
        close();
        return false;
    }

    m_pipeFd       = fd;
    m_moduleIndex  = modIdx;
    m_moduleLoaded = true;   // arm close() to unload; index 0 is valid here
    m_negFormat    = format;
    m_open         = true;
    m_err.clear();

    qCInfo(lcAudio) << "LinuxPipeBus: opened" << m_pipePath
                    << "module=" << modIdx
                    << "role=" << static_cast<int>(m_role);
    return true;
#endif
}

// ── close() ──────────────────────────────────────────────────────────────────

void LinuxPipeBus::close() {
#ifdef Q_OS_LINUX
    if (m_pipeFd >= 0) {
        ::close(m_pipeFd);
        m_pipeFd = -1;
    }
    if (m_ownership) {
        if (!m_ownership->cleanup()) {
            qCWarning(lcAudio) << "LinuxPipeBus cleanup preserved unresolved generation:"
                               << m_ownership->errorString();
            // Fds are closed and this bus will never use the generation again.
            // Persist that fact before releasing the claim, allowing a later
            // owner to retry only this proven-retired generation.
            if (!m_ownership->retire()) {
                qCWarning(lcAudio) << "LinuxPipeBus could not persist generation retirement:"
                                   << m_ownership->errorString();
            }
        }
        // Release the role claim only after all cleanup has finished.
        m_ownership.reset();
    }
    m_moduleLoaded = false;
    m_moduleIndex = 0;
#endif
    m_open = false;
    m_rxLevel.store(0.0f, std::memory_order_release);
    m_txLevel.store(0.0f, std::memory_order_release);
    m_meterCounter = 0;
}

// ── push() (producer: Role::Vax1..4) ─────────────────────────────────────────

qint64 LinuxPipeBus::push(const char* data, qint64 bytes) {
    // TODO(phase3M): Re-add TX-driven silence fill so the pipe-source clock
    // keeps advancing during TX state. See AetherSDR PipeWireAudioBridge::
    // setTransmitting() / feedSilenceToAllPipes() for the pattern.
    if (!isProducer()) {
        return -1;
    }
    if (!m_open || m_pipeFd < 0 || data == nullptr || bytes <= 0) {
        return 0;
    }

#ifdef Q_OS_LINUX
    // Write float32 stereo PCM directly to the FIFO — pactl module-pipe-source
    // reads it as-is (format=float32le rate=48000 channels=2).
    // Non-blocking write: EAGAIN/EWOULDBLOCK means the pipe is full (reader not
    // keeping up); we drop the block rather than stall the audio thread.
    // Other errors (EBADF, EIO, EPIPE) are genuine failures and are logged.
    const ssize_t written = ::write(m_pipeFd, data, static_cast<size_t>(bytes));
    if (written < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            qCWarning(lcAudio) << "LinuxPipeBus: write error on"
                               << m_pipePath
                               << "errno=" << errno
                               << "(" << strerror(errno) << ")";
        }
        return 0;
    }
    const qint64 actualBytes = static_cast<qint64>(written);

    // RMS meter on the left channel (indices 0, 2, 4, …). Strided so the hot
    // path doesn't walk the full block every call.
    if (++m_meterCounter % kMeterStride == 0 && actualBytes > 0) {
        const auto* samples = reinterpret_cast<const float*>(data);
        const int numSamples = static_cast<int>(actualBytes / sizeof(float));
        float sum = 0.0f;
        int count = 0;
        for (int i = 0; i < numSamples; i += 2) {
            sum += samples[i] * samples[i];
            ++count;
        }
        const float rms = (count > 0) ? std::sqrt(sum / static_cast<float>(count)) : 0.0f;
        m_rxLevel.store(rms, std::memory_order_release);
    }

    return actualBytes;
#else
    (void)data;
    return 0;
#endif
}

// ── pull() (consumer: Role::TxInput) ─────────────────────────────────────────

qint64 LinuxPipeBus::pull(char* data, qint64 maxBytes) {
    // TODO(phase3M): TX-side poll timer — AetherSDR polled the TX FIFO every
    // 5 ms via QTimer inside PipeWireAudioBridge. Here, pull() is driven on
    // demand from the caller. Re-add the timer when AudioEngine is wired.
    if (isProducer()) {
        return -1;
    }
    if (!m_open || m_pipeFd < 0 || data == nullptr || maxBytes <= 0) {
        return 0;
    }

#ifdef Q_OS_LINUX
    // Non-blocking read from the FIFO — EAGAIN/EWOULDBLOCK means no TX audio
    // available yet; caller should retry. Other errors (EBADF, EIO) are genuine
    // failures and are logged.
    // Ported from AetherSDR PipeWireAudioBridge::readTxPipe(), adapted for the
    // on-demand pull() contract (no internal loop here; caller may loop).
    const ssize_t n = ::read(m_pipeFd, data, static_cast<size_t>(maxBytes));
    if (n < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            qCWarning(lcAudio) << "LinuxPipeBus: read error on"
                               << m_pipePath
                               << "errno=" << errno
                               << "(" << strerror(errno) << ")";
        }
        return 0;
    }
    if (n == 0) {
        return 0;  // EOF / pipe writer closed; treat as no data
    }

    const qint64 actualBytes = static_cast<qint64>(n);

    // TX level meter (strided).
    if (++m_meterCounter % kMeterStride == 0 && actualBytes > 0) {
        const auto* samples = reinterpret_cast<const float*>(data);
        const int numSamples = static_cast<int>(actualBytes / sizeof(float));
        float sum = 0.0f;
        int count = 0;
        // Left channel (index 0, 2, 4, …) used for meter — stereo float32.
        for (int i = 0; i < numSamples; i += 2) {
            sum += samples[i] * samples[i];
            ++count;
        }
        const float rms = (count > 0) ? std::sqrt(sum / static_cast<float>(count)) : 0.0f;
        m_txLevel.store(rms, std::memory_order_release);
    }

    return actualBytes;
#else
    (void)data;
    (void)maxBytes;
    return 0;
#endif
}

} // namespace NereusSDR
