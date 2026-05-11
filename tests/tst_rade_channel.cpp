// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - tst_rade_channel: tests for the Phase 3R RadeChannel wrapper.
//
// I1 skeleton contracts (lifecycle only):
//
//   1. initialState              fresh RadeChannel reports !isActive() && !isSynced()
//   2. startStop                 start(<real path>) returns true and flips isActive()
//                                back to false after stop()
//   3. modelLoadFailureDisables  start(<nonexistent path>) returns false and leaves
//                                isActive() at false
//
// I2 RX-path contracts:
//
//   4. startInitializesRade              start("dummy") opens librade and isActive() flips true
//   5. processIqEmitsSyncFalseOnNoise    feeding random I/Q noise emits syncChanged(false)
//                                        and no rxSpeechReady chunks
//   6. processIqAccumulatesAcrossChunks  small chunks accumulate; rade_rx fires only
//                                        once a rade_nin()-sized buffer is ready
//   7. stopReleasesResources             start("dummy") then stop() tears down cleanly
//
// I3 TX path and I4 text channel lands in their own tasks.
//
// See src/core/RadeChannel.h for the upstream license headers and
// modification-history block (verbatim freedv-gui BSD-2-Clause-style
// header + AetherSDR project-level attribution per
// docs/attribution/HOW-TO-PORT.md rule 6).

#include <QtTest/QtTest>
#include <QTemporaryFile>
#include <QSignalSpy>
#include <QByteArray>
#include <random>

#include "core/RadeChannel.h"

using namespace NereusSDR;

class TestRadeChannel : public QObject {
    Q_OBJECT

private slots:
    void initialState();
    void startStop();
    void modelLoadFailureDisablesChannel();

    // I2 RX-path tests
    void startInitializesRade();
    void processIqEmitsSyncFalseOnNoise();
    void processIqAccumulatesAcrossMultipleChunks();
    void stopReleasesResources();
};

void TestRadeChannel::initialState()
{
    // A freshly constructed RadeChannel must not advertise itself as
    // active or in sync. Active flips on start(); sync flips on the
    // RADE decoder's sync indication once I2 wires it up.
    RadeChannel ch;
    QVERIFY(!ch.isActive());
    QVERIFY(!ch.isSynced());
}

void TestRadeChannel::startStop()
{
    // Create a real fixture file so start()'s skeleton path-exists
    // check passes. The byte contents are irrelevant for I1; I2 will
    // teach start() to actually load the .f32 model via rade_open().
    QTemporaryFile fixture;
    QVERIFY(fixture.open());
    fixture.write("rade-model-skeleton-fixture");
    fixture.flush();

    RadeChannel ch;
    QVERIFY(ch.start(fixture.fileName()));
    QVERIFY(ch.isActive());

    ch.stop();
    QVERIFY(!ch.isActive());
}

void TestRadeChannel::modelLoadFailureDisablesChannel()
{
    // Nonexistent path must not flip isActive(). I2 will further
    // require the file's contents to be a valid RADE model and will
    // close + reset on rade_open() failure as well; I1 just guards
    // the path-exists precondition.
    RadeChannel ch;
    QVERIFY(!ch.start("/nonexistent/path.f32"));
    QVERIFY(!ch.isActive());
}

// =========================================================================
// I2 RX-path tests
// =========================================================================

namespace {

// Build a buffer of N interleaved I/Q float32 samples filled with
// deterministic low-amplitude noise. RADE will not sync on this input,
// so the pipeline runs through resample + rade_rx + FARGAN without
// emitting a decoded speech chunk. Seed is fixed so the test is
// deterministic. See tests/fixtures/rade/README.md for the rationale.
QByteArray makeSyntheticIq(int nSamples)
{
    QByteArray buf(nSamples * 2 * static_cast<int>(sizeof(float)), Qt::Uninitialized);
    auto* p = reinterpret_cast<float*>(buf.data());
    std::mt19937 rng(0xC0DEC0DEu);
    std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
    for (int i = 0; i < nSamples; ++i) {
        p[2 * i]     = noise(rng);  // I
        p[2 * i + 1] = noise(rng);  // Q
    }
    return buf;
}

}  // namespace

void TestRadeChannel::startInitializesRade()
{
    // "dummy" is the radae_nopy convention: the model_file argument
    // is ignored, the built-in weights compiled into librade are
    // used. RadeChannel::start treats "dummy" as a sentinel and
    // bypasses the path-exists check.
    RadeChannel ch;
    QVERIFY(ch.start("dummy"));
    QVERIFY(ch.isActive());

    // Calling start() a second time on the same wrapper is idempotent.
    QVERIFY(ch.start("dummy"));
    QVERIFY(ch.isActive());

    ch.stop();
    QVERIFY(!ch.isActive());

    // Non-existent path still rejected.
    QVERIFY(!ch.start("/nonexistent/path.f32"));
    QVERIFY(!ch.isActive());
}

void TestRadeChannel::processIqEmitsSyncFalseOnNoise()
{
    RadeChannel ch;
    QVERIFY(ch.start("dummy"));

    QSignalSpy syncSpy(&ch, &RadeChannel::syncChanged);
    QSignalSpy speechSpy(&ch, &RadeChannel::rxSpeechReady);

    // Feed roughly 1 second of synthetic I/Q noise at 24 kHz in
    // several smaller chunks. rade_nin() at the RADE-v1 default
    // is a few thousand RADE_COMP samples at 8 kHz, so a 24kHz
    // input of ~24000 samples is more than enough to trigger
    // multiple rade_rx() invocations. The codec will not sync
    // on noise, so syncChanged is only ever emitted as false
    // (or stays unemitted if it was false at construction).
    constexpr int kChunkSamples = 4096;
    constexpr int kNumChunks    = 8;
    for (int chunk = 0; chunk < kNumChunks; ++chunk) {
        ch.processIq(makeSyntheticIq(kChunkSamples));
    }

    // We made at least one rade_rx() call (otherwise sync state
    // never refreshed). Per the AetherSDR pattern, syncChanged is
    // only emitted on a state transition. Since the wrapper starts
    // with m_synced = false and noise input keeps it false, the
    // signal should not fire at all - but if for any reason
    // librade flips sync briefly and back, we accept that too.
    // What we cannot accept is a sync=true that persists.
    QVERIFY2(!ch.isSynced(),
             "noise input must not produce a sustained RADE sync indication");

    // syncChanged may fire zero or more times, but every emitted
    // value should be false (no spurious sync on noise).
    for (const auto& args : syncSpy) {
        QVERIFY2(args.value(0).toBool() == false,
                 "syncChanged emitted true on pure-noise input");
    }

    // No rxSpeechReady chunks should be emitted because the codec
    // never synced and so never produced decoded features.
    QCOMPARE(speechSpy.count(), 0);

    // At least one rade_rx() must have been called given the input
    // volume; otherwise the accumulator is broken.
    QVERIFY2(ch.radeRxCallCountForTest() >= 1,
             qPrintable(QString("rade_rx() never invoked across %1 input chunks")
                            .arg(kNumChunks)));

    ch.stop();
}

void TestRadeChannel::processIqAccumulatesAcrossMultipleChunks()
{
    RadeChannel ch;
    QVERIFY(ch.start("dummy"));

    // Feed five chunks of 64 samples each (320 I/Q samples total,
    // = 640 floats). At 24 kHz -> 8 kHz this is ~107 RADE_COMP
    // samples after downsampling, well under rade_nin()'s typical
    // few-thousand-sample threshold. rade_rx() should NOT be
    // called yet.
    for (int i = 0; i < 5; ++i) {
        ch.processIq(makeSyntheticIq(64));
    }
    QCOMPARE(ch.radeRxCallCountForTest(), 0);

    // Now feed a large chunk that pushes the accumulator past
    // rade_nin(). 24000 samples at 24 kHz -> ~8000 samples at
    // 8 kHz, comfortably above rade_nin()'s threshold.
    ch.processIq(makeSyntheticIq(24000));
    QVERIFY2(ch.radeRxCallCountForTest() >= 1,
             qPrintable(QString("rade_rx() count was %1 after pushing 24000-sample chunk")
                            .arg(ch.radeRxCallCountForTest())));

    ch.stop();
}

void TestRadeChannel::stopReleasesResources()
{
    // Start -> stop -> destructor must all run cleanly without
    // tripping ASan, leaks, or hangs. Multiple start/stop cycles
    // exercise the cleanup-and-reallocation paths.
    for (int cycle = 0; cycle < 3; ++cycle) {
        RadeChannel ch;
        QVERIFY(ch.start("dummy"));
        QVERIFY(ch.isActive());

        // Feed a small chunk so the accumulators have content
        // before stop() tears them down.
        ch.processIq(makeSyntheticIq(256));

        ch.stop();
        QVERIFY(!ch.isActive());
        QVERIFY(!ch.isSynced());
    }

    // One more: rely on the destructor to call stop() implicitly.
    {
        RadeChannel ch;
        QVERIFY(ch.start("dummy"));
        ch.processIq(makeSyntheticIq(256));
        // Implicit destructor call here.
    }
}

QTEST_GUILESS_MAIN(TestRadeChannel)
#include "tst_rade_channel.moc"
