// =================================================================
// tests/tst_core_audio_hal_bus.cpp  (NereusSDR)
// =================================================================
//
// Exercises CoreAudioHalBus — Phase 3O VAX Sub-Phase 5.3.
//
// Coverage:
//   1. Lifecycle — open() creates the shm segment, isOpen() transitions,
//      close() resets state. A second fd opened from the test process
//      proves the segment is visible to other would-be consumers (i.e.
//      the HAL plugin's shm_open path).
//   2. Producer round-trip — push() on a Vax1 instance writes to the ring;
//      raw mmap from the test side reads back the same bytes.
//   3. Consumer round-trip — test writes known samples to /nereussdr-vax-tx;
//      CoreAudioHalBus(TxInput)::pull() returns them.
//   4. Role policing — push() returns -1 on TxInput; pull() returns -1 on
//      Vax1..4.
//   5. Format validation — open() rejects non-48k/non-stereo/non-float32.
//   6. Metering — rxLevel() updates after push() with non-zero samples.
//
// macOS-only (shm_open + mmap + ftruncate); gated at the CMake level with
// `if(APPLE)` so non-Apple test builds simply skip this binary.
// =================================================================

#include <QtTest/QtTest>

#include "core/audio/CoreAudioHalBus.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <vector>

using namespace NereusSDR;

namespace {

constexpr const char* kVax1Path    = "/nereussdr-vax-1";
constexpr const char* kVax2Path    = "/nereussdr-vax-2";
constexpr const char* kTxInputPath = "/nereussdr-vax-tx";

// Unlink every VAX shm name the tests touch so a previous crash / run
// doesn't leak stale data into the next one.
void unlinkAllVaxSegments() {
    ::shm_unlink(kVax1Path);
    ::shm_unlink(kVax2Path);
    ::shm_unlink("/nereussdr-vax-3");
    ::shm_unlink("/nereussdr-vax-4");
    ::shm_unlink(kTxInputPath);
}

} // namespace

class TstCoreAudioHalBus : public QObject {
    Q_OBJECT

private slots:
    void init() {
        unlinkAllVaxSegments();
    }
    void cleanup() {
        unlinkAllVaxSegments();
    }

    // ── 1. Lifecycle ────────────────────────────────────────────────────────

    void openCreatesSegmentAndIsVisibleToOtherProcesses() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        QVERIFY(!bus.isOpen());

        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));
        QVERIFY(bus.isOpen());

        // A consumer (the HAL plugin, or a second test fd) should be able
        // to open the same segment read-only. If this fails, push() could
        // never reach the plugin.
        int fd = ::shm_open(kVax1Path, O_RDONLY, 0666);
        QVERIFY2(fd >= 0, "shm_open after bus.open() should return a valid fd");

        struct stat st{};
        QCOMPARE(::fstat(fd, &st), 0);
        QVERIFY2(static_cast<size_t>(st.st_size) >= sizeof(VaxShmBlock),
                 "shm segment should be sized at least sizeof(VaxShmBlock)");
        ::close(fd);

        bus.close();
        QVERIFY(!bus.isOpen());
    }

    // ── 2. Producer round-trip ──────────────────────────────────────────────

    void pushOnVax1WritesToSharedRing() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        AudioFormat fmt;
        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        // Open the same segment from the test side (simulating the plugin).
        int fd = ::shm_open(kVax1Path, O_RDWR, 0666);
        QVERIFY(fd >= 0);
        void* ptr = ::mmap(nullptr, sizeof(VaxShmBlock),
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        QVERIFY(ptr != MAP_FAILED);
        auto* consumerView = static_cast<VaxShmBlock*>(ptr);

        QVERIFY(consumerView->active.load(std::memory_order_acquire) == 1);

        // Write 1024 bytes (256 floats, 128 stereo frames) of a known pattern.
        constexpr int kFloats = 256;
        std::vector<float> payload(kFloats);
        for (int i = 0; i < kFloats; ++i) {
            payload[i] = static_cast<float>(i) * 0.01f;
        }
        const qint64 bytes = static_cast<qint64>(kFloats) * sizeof(float);
        QCOMPARE(bus.push(reinterpret_cast<const char*>(payload.data()), bytes), bytes);

        // writePos should have advanced by kFloats.
        const uint32_t wp = consumerView->writePos.load(std::memory_order_acquire);
        QCOMPARE(wp, static_cast<uint32_t>(kFloats));

        // Bytes at the head of the ring should match our pattern verbatim.
        for (int i = 0; i < kFloats; ++i) {
            QCOMPARE(consumerView->ringBuffer[i], payload[i]);
        }

        ::munmap(consumerView, sizeof(VaxShmBlock));
        ::close(fd);
        bus.close();
    }

    // ── 3. Consumer round-trip ──────────────────────────────────────────────

    void pullOnTxInputReadsFromSharedRing() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::TxInput);
        AudioFormat fmt;
        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        // Map the same segment from the test side and write samples into
        // the ring as if the HAL plugin had done so.
        int fd = ::shm_open(kTxInputPath, O_RDWR, 0666);
        QVERIFY(fd >= 0);
        void* ptr = ::mmap(nullptr, sizeof(VaxShmBlock),
                           PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        QVERIFY(ptr != MAP_FAILED);
        auto* pluginView = static_cast<VaxShmBlock*>(ptr);

        constexpr int kFloats = 128;
        std::vector<float> payload(kFloats);
        for (int i = 0; i < kFloats; ++i) {
            payload[i] = 0.25f - static_cast<float>(i) * 0.001f;
        }

        // Simulate the plugin's TX write path: copy into the ring, bump wp.
        for (int i = 0; i < kFloats; ++i) {
            pluginView->ringBuffer[i] = payload[i];
        }
        pluginView->writePos.store(kFloats, std::memory_order_release);
        // Plugin would also set active=1, but the bus sets it at open() time.

        // Bus should drain the samples back out.
        std::vector<float> drained(kFloats, -999.0f);
        const qint64 got = bus.pull(
            reinterpret_cast<char*>(drained.data()),
            static_cast<qint64>(kFloats) * sizeof(float));
        QCOMPARE(got, static_cast<qint64>(kFloats) * sizeof(float));
        for (int i = 0; i < kFloats; ++i) {
            QCOMPARE(drained[i], payload[i]);
        }

        // readPos should have advanced.
        QCOMPARE(pluginView->readPos.load(std::memory_order_acquire),
                 static_cast<uint32_t>(kFloats));

        ::munmap(pluginView, sizeof(VaxShmBlock));
        ::close(fd);
        bus.close();
    }

    // ── 4. Role policing ────────────────────────────────────────────────────

    void pushOnTxInputReturnsMinusOne() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::TxInput);
        AudioFormat fmt;
        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        const float dummy[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        QCOMPARE(bus.push(reinterpret_cast<const char*>(dummy), sizeof(dummy)),
                 static_cast<qint64>(-1));

        bus.close();
    }

    void pullOnVax1ReturnsMinusOne() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        AudioFormat fmt;
        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        float buf[4] = {};
        QCOMPARE(bus.pull(reinterpret_cast<char*>(buf), sizeof(buf)),
                 static_cast<qint64>(-1));

        bus.close();
    }

    // ── 5. Format validation ────────────────────────────────────────────────

    void openRejectsWrongSampleRate() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        AudioFormat fmt;
        fmt.sampleRate = 44100;  // HAL plugin is hard-pinned at 48 kHz
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
        QVERIFY(!bus.errorString().isEmpty());
    }

    void openRejectsWrongChannelCount() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax2);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 1;  // mono — HAL plugin is stereo
        fmt.sample     = AudioFormat::Sample::Float32;

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
    }

    void openRejectsWrongSampleFormat() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        AudioFormat fmt;
        fmt.sampleRate = 48000;
        fmt.channels   = 2;
        fmt.sample     = AudioFormat::Sample::Int16;  // HAL plugin is Float32

        QVERIFY(!bus.open(fmt));
        QVERIFY(!bus.isOpen());
    }

    // ── 6. Metering ─────────────────────────────────────────────────────────

    void rxLevelUpdatesAfterPush() {
        CoreAudioHalBus bus(CoreAudioHalBus::Role::Vax1);
        AudioFormat fmt;
        QVERIFY2(bus.open(fmt), qPrintable(bus.errorString()));

        QCOMPARE(bus.rxLevel(), 0.0f);

        // push() strides metering at kMeterStride (10) calls — walk a
        // handful of pushes so we're guaranteed to land on a meter update.
        std::vector<float> payload(128);
        for (int i = 0; i < 128; ++i) {
            payload[i] = 0.5f;  // constant 0.5 → RMS on left channel also 0.5
        }
        for (int k = 0; k < 20; ++k) {
            QCOMPARE(bus.push(reinterpret_cast<const char*>(payload.data()),
                              static_cast<qint64>(payload.size()) * sizeof(float)),
                     static_cast<qint64>(payload.size()) * sizeof(float));
        }

        const float lvl = bus.rxLevel();
        QVERIFY2(lvl > 0.4f && lvl < 0.6f,
                 qPrintable(QStringLiteral("rxLevel=%1 expected near 0.5").arg(lvl)));

        bus.close();
    }
};

QTEST_APPLESS_MAIN(TstCoreAudioHalBus)
#include "tst_core_audio_hal_bus.moc"
