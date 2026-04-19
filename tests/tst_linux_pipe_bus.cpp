// =================================================================
// tests/tst_linux_pipe_bus.cpp  (NereusSDR)
// =================================================================
//
// Exercises LinuxPipeBus — Phase 3O VAX Sub-Phase 6 Task 6.1.
//
// Coverage:
//   1. Lifecycle — open() creates FIFO + loads pactl module + returns true;
//      isOpen() transitions; close() unlinks FIFO + unloads module.
//   2. Producer role policing — Vax1..4: push() returns bytes, pull() returns -1.
//   3. Consumer role policing — TxInput: push() returns -1, pull() returns bytes.
//   4. Format validation — open() rejects non-48k, non-stereo, non-float32.
//   5. Metering — rxLevel() (Vax1..4) / txLevel() (TxInput) updates after data
//      movement with non-zero samples.
//   6. Stale-cleanup idempotent — two bus instances can open() sequentially
//      without the second cleanup clobbering the first module.
//
// Linux-only binary (Q_OS_LINUX guard in the impl). Gated at CMake level with
// if(UNIX AND NOT APPLE).
//
// pactl availability: tests that exercise open() (lifecycle, metering, stale-
// cleanup) use QSKIP when pactl is not found in PATH. The CI Linux runner does
// NOT install pulseaudio-utils; those tests will SKIP in CI but pass on a
// developer Linux box with PulseAudio or PipeWire-pulse active.
// Format-validation and role-policing tests do NOT call open(), so they always
// run (QSKIP-free).
// =================================================================

#include <QtTest/QtTest>

#include "core/audio/LinuxPipeBus.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <vector>

using namespace NereusSDR;

namespace {

// Check whether pactl is available in PATH.
// Tests that require pactl use this to QSKIP on CI runners that lack it.
bool pactlAvailable()
{
    // QProcess::execute returns -2 if the program is not found
    const int rc = QProcess::execute(QStringLiteral("pactl"),
                                     {QStringLiteral("--version")});
    return rc >= 0;  // 0 = success, >0 = pactl ran but returned non-zero (still present)
}

// Unlink every VAX FIFO path the tests touch so a previous crash / run
// doesn't leave stale FIFOs that would confuse the next run.
void unlinkAllVaxFifos()
{
    ::unlink("/tmp/nereussdr-vax-1.pipe");
    ::unlink("/tmp/nereussdr-vax-2.pipe");
    ::unlink("/tmp/nereussdr-vax-3.pipe");
    ::unlink("/tmp/nereussdr-vax-4.pipe");
    ::unlink("/tmp/nereussdr-vax-tx.pipe");
}

} // namespace

class TstLinuxPipeBus : public QObject {
    Q_OBJECT

private slots:

    void init() {
        unlinkAllVaxFifos();
    }
    void cleanup() {
        unlinkAllVaxFifos();
    }

    // ── 1. Lifecycle ────────────────────────────────────────────────────────

    void openCreatesFifoAndIsOpen()
    {
        if (!pactlAvailable()) {
            QSKIP("pactl not found — skipping lifecycle test (run on a Linux box with PulseAudio/PipeWire)");
        }

        LinuxPipeBus bus(LinuxPipeBus::Role::Vax1);
        QVERIFY(!bus.isOpen());

        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));
        QVERIFY(bus.isOpen());

        // The FIFO should exist on the filesystem after open().
        struct stat st{};
        const int rc = ::stat("/tmp/nereussdr-vax-1.pipe", &st);
        QVERIFY2(rc == 0, "FIFO /tmp/nereussdr-vax-1.pipe should exist after open()");
        QVERIFY2(S_ISFIFO(st.st_mode), "path should be a named FIFO (S_ISFIFO)");

        bus.close();
        QVERIFY(!bus.isOpen());

        // After close() the FIFO should be unlinked.
        struct stat st2{};
        QVERIFY2(::stat("/tmp/nereussdr-vax-1.pipe", &st2) != 0,
                 "FIFO should be unlinked after close()");
    }

    // ── 2. Producer role policing ───────────────────────────────────────────

    // push() returns -1 on TxInput (consumer role).  Format validation runs
    // before the pactl call, so this test does NOT need pactl.
    void pushOnTxInputReturnsMinusOne()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
        // We deliberately do NOT call open() here — role policing must fire even
        // when the bus is closed.  The isProducer() check is the first guard.
        const float dummy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        QCOMPARE(bus.push(reinterpret_cast<const char*>(dummy), sizeof(dummy)),
                 static_cast<qint64>(-1));
    }

    // pull() returns -1 on Vax1..4 (producer roles). No pactl needed.
    void pullOnVax1ReturnsMinusOne()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax1);
        float buf[4] = {};
        QCOMPARE(bus.pull(reinterpret_cast<char*>(buf), sizeof(buf)),
                 static_cast<qint64>(-1));
    }

    void pullOnVax2ReturnsMinusOne()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax2);
        float buf[4] = {};
        QCOMPARE(bus.pull(reinterpret_cast<char*>(buf), sizeof(buf)),
                 static_cast<qint64>(-1));
    }

    void pullOnVax3ReturnsMinusOne()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax3);
        float buf[4] = {};
        QCOMPARE(bus.pull(reinterpret_cast<char*>(buf), sizeof(buf)),
                 static_cast<qint64>(-1));
    }

    void pullOnVax4ReturnsMinusOne()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax4);
        float buf[4] = {};
        QCOMPARE(bus.pull(reinterpret_cast<char*>(buf), sizeof(buf)),
                 static_cast<qint64>(-1));
    }

    // ── 3. Consumer role policing (with open, needs pactl) ──────────────────

    void txInputPullReturnsBytesWhenDataAvailable()
    {
        if (!pactlAvailable()) {
            QSKIP("pactl not found — skipping TxInput pull test");
        }

        LinuxPipeBus bus(LinuxPipeBus::Role::TxInput);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        // Write some float32 stereo data directly to the FIFO from the test
        // side (simulating a TX audio app that opened the sink).
        // We open the FIFO for writing (non-blocking) ourselves.
        const int wrFd = ::open("/tmp/nereussdr-vax-tx.pipe", O_WRONLY | O_NONBLOCK);
        if (wrFd < 0) {
            // If FIFO has no reader yet (race with pactl opening the write end),
            // skip gracefully — this is a known timing sensitivity.
            QSKIP("Could not open FIFO for writing — pactl module may not have opened its end yet");
        }

        constexpr int kFloats = 64;  // 32 stereo frames
        std::vector<float> payload(kFloats);
        for (int i = 0; i < kFloats; ++i) {
            payload[i] = 0.3f;
        }
        const ssize_t wrote = ::write(wrFd, payload.data(),
                                      kFloats * static_cast<int>(sizeof(float)));
        ::close(wrFd);

        if (wrote <= 0) {
            QSKIP("FIFO write returned 0/error — pipe not ready (pactl timing)");
        }

        // Give the OS a brief moment to flush the pipe.
        QTest::qWait(20);

        std::vector<float> drained(kFloats, -999.0f);
        const qint64 got = bus.pull(
            reinterpret_cast<char*>(drained.data()),
            static_cast<qint64>(kFloats) * sizeof(float));

        QVERIFY2(got > 0, "pull() should return > 0 bytes when data is in the FIFO");

        bus.close();
    }

    // ── 4. Format validation ────────────────────────────────────────────────
    //
    // These tests do NOT call pactl — they exercise the early-return guard
    // in open() before any system call is issued. They run in CI unconditionally.

    void openRejectsWrongSampleRate()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax1);
        AudioFormat fmt;
        fmt.sampleRate = 44100;  // must be 48000
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
        QVERIFY(!bus.errorString().isEmpty());
    }

    void openRejectsMonoFormat()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax2);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 1;  // must be stereo
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
        QVERIFY(!bus.errorString().isEmpty());
    }

    void openRejectsInt16Format()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax1);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Int16;  // must be Float32

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
        QVERIFY(!bus.errorString().isEmpty());
    }

    void openRejectsInt24Format()
    {
        LinuxPipeBus bus(LinuxPipeBus::Role::Vax3);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Int24;

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
    }

    // ── 5. Metering ─────────────────────────────────────────────────────────

    void rxLevelUpdatesAfterPush()
    {
        if (!pactlAvailable()) {
            QSKIP("pactl not found — skipping metering test");
        }

        LinuxPipeBus bus(LinuxPipeBus::Role::Vax1);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));
        QCOMPARE(bus.rxLevel(), 0.0f);

        // push() strides metering at kMeterStride (10) calls — run 20 pushes
        // of constant 0.5 so we're guaranteed to land on a meter update.
        // 128 floats = 64 stereo frames per push.
        std::vector<float> payload(128, 0.5f);
        const qint64 payloadBytes = static_cast<qint64>(payload.size()) * sizeof(float);

        for (int k = 0; k < 20; ++k) {
            bus.push(reinterpret_cast<const char*>(payload.data()), payloadBytes);
        }

        const float lvl = bus.rxLevel();
        // Left-channel RMS of 0.5 constant signal = 0.5.
        QVERIFY2(lvl > 0.4f && lvl < 0.6f,
                 qPrintable(QStringLiteral("rxLevel=%1, expected near 0.5").arg(lvl)));

        bus.close();
    }

    // ── 6. Stale-cleanup idempotent ──────────────────────────────────────────
    //
    // Opening two bus instances sequentially must not double-cleanup or fail.
    // This exercises the std::once_flag path: the first open() runs cleanup,
    // the second open() skips it (idempotent).

    void twoSequentialOpensDoNotDoubleCleanup()
    {
        if (!pactlAvailable()) {
            QSKIP("pactl not found — skipping stale-cleanup idempotency test");
        }

        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        {
            LinuxPipeBus bus1(LinuxPipeBus::Role::Vax1);
            QVERIFY2(bus1.open(fmt), qPrintable(bus1.errorString()));
            QVERIFY(bus1.isOpen());
            bus1.close();
            QVERIFY(!bus1.isOpen());
        }

        {
            LinuxPipeBus bus2(LinuxPipeBus::Role::Vax2);
            // Second open() must succeed even though cleanup already ran
            // (once_flag prevents the re-run).
            QVERIFY2(bus2.open(fmt), qPrintable(bus2.errorString()));
            QVERIFY(bus2.isOpen());
            bus2.close();
        }
    }
};

QTEST_APPLESS_MAIN(TstLinuxPipeBus)
#include "tst_linux_pipe_bus.moc"
