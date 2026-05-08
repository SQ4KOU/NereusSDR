// tests/tst_rx_dsp_worker_buffer_sizing.cpp
//
// no-port-check: NereusSDR-original test. The setup.cs reference below is
// contextual only (explains the bug's history), not a derivation. Thetis
// has no equivalent unit test for the fexchange2 input-sizing contract.
//
// Regression test for the rate-dependent buffer-sizing bug. Prior to this
// fix, RxDspWorker hardcoded its accumulator drain size to 1024 samples
// (the in_size for a 768 kHz wire rate via the Thetis formula
// in_size = 64 * rate / 48000). When PR #44 (sample-rate-wiring)
// changed the default wire rate to 192 kHz per setup.cs:866, RadioModel
// began opening WDSP channels with in_size = 256 — but the worker still
// fed fexchange2 chunks of 1024, violating WDSP's API contract that each
// fexchange2 call receives exactly the in_size set at OpenChannel.
// Symptom: clean audio at 768 kHz only; jittery at 48/96/192/384/1536 kHz.
//
// This test pins the new contract: the worker's drain chunk size must
// follow whatever setBufferSizes() sets, not a compile-time constant.

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QSignalSpy>
#include <QThread>
#include <QVector>

#include "models/RxDspWorker.h"

using namespace NereusSDR;

class TestRxDspWorkerBufferSizing : public QObject {
    Q_OBJECT

private slots:
    // After setBufferSizes(256, 64), feeding 1024 IQ pairs (= 2048 floats
    // interleaved) must drain the accumulator in 4 chunks of 256 samples,
    // not 1 chunk of 1024. Each chunk emits chunkDrained(int sampleCount).
    void drainsAccumulatorAtConfiguredInSize_192kHz();

    // After setBufferSizes(2048, 64) (1.536 MHz), feeding 4096 IQ pairs
    // must drain in 2 chunks of 2048, proving the worker scales up too.
    void drainsAccumulatorAtConfiguredInSize_1536kHz();

    // Default behavior unchanged: with no setBufferSizes call, the worker
    // still drains at the 1024-sample default (matches the pre-fix
    // behavior so existing call sites keep working until updated).
    void defaultInSizeRemains1024_for_backCompat();

    // Phase 3M-3a-iv: setBufferSizes() must emit bufferSizesChanged with
    // (outSize, sampleRate). Consumed by TxWorkerThread to align DEXP
    // anti-VOX detector dimensions with RX block geometry.
    void setBufferSizes_emitsBufferSizesChanged();

    // Idempotency: if setBufferSizes() is called twice with the same
    // (in, out) pair, only the first call fires bufferSizesChanged. This
    // avoids spamming TxWorkerThread::setAntiVoxBlockGeometry with no-op
    // SetAntiVOXSize/SetAntiVOXRate calls during steady-state operation.
    void setBufferSizes_idempotent_noDuplicateEmit();
};

namespace {

struct WorkerHarness {
    QThread*     thread{nullptr};
    RxDspWorker* worker{nullptr};

    void teardown()
    {
        if (thread != nullptr) {
            thread->quit();
            QVERIFY2(thread->wait(5000), "worker thread did not exit");
            delete worker;
            delete thread;
            worker = nullptr;
            thread = nullptr;
        }
    }
};

WorkerHarness makeHarness()
{
    WorkerHarness h;
    h.thread = new QThread();
    h.thread->setObjectName(QStringLiteral("TestDspBufferSizingThread"));
    h.worker = new RxDspWorker();
    h.worker->moveToThread(h.thread);
    h.thread->start();
    return h;
}

// Feed the worker via QueuedConnection and wait for chunkDrained to fire
// expectedChunks times. Returns the spy so the caller can inspect args.
QSignalSpy* feedAndCollectChunks(WorkerHarness& h,
                                  int iqPairs,
                                  int expectedChunks)
{
    auto* spy = new QSignalSpy(h.worker, &RxDspWorker::chunkDrained);
    QVector<float> samples(iqPairs * 2, 0.25f);
    QMetaObject::invokeMethod(h.worker, "processIqBatch",
                              Qt::QueuedConnection,
                              Q_ARG(int, 0),
                              Q_ARG(QVector<float>, samples));
    // Drain on the main event loop until we have the chunks (or time out).
    QElapsedTimer t; t.start();
    while (spy->count() < expectedChunks && t.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    return spy;
}

} // namespace

void TestRxDspWorkerBufferSizing::drainsAccumulatorAtConfiguredInSize_192kHz()
{
    WorkerHarness h = makeHarness();
    h.worker->setBufferSizes(256, 64);

    QSignalSpy* spy = feedAndCollectChunks(h, /*iqPairs=*/1024, /*expectedChunks=*/4);
    QCOMPARE(spy->count(), 4);
    for (int i = 0; i < spy->count(); ++i) {
        QCOMPARE(spy->at(i).at(0).toInt(), 256);
    }
    delete spy;

    h.teardown();
}

void TestRxDspWorkerBufferSizing::drainsAccumulatorAtConfiguredInSize_1536kHz()
{
    WorkerHarness h = makeHarness();
    h.worker->setBufferSizes(2048, 64);

    QSignalSpy* spy = feedAndCollectChunks(h, /*iqPairs=*/4096, /*expectedChunks=*/2);
    QCOMPARE(spy->count(), 2);
    for (int i = 0; i < spy->count(); ++i) {
        QCOMPARE(spy->at(i).at(0).toInt(), 2048);
    }
    delete spy;

    h.teardown();
}

void TestRxDspWorkerBufferSizing::defaultInSizeRemains1024_for_backCompat()
{
    WorkerHarness h = makeHarness();
    // No setBufferSizes() call — should use the historical default.

    QSignalSpy* spy = feedAndCollectChunks(h, /*iqPairs=*/2048, /*expectedChunks=*/2);
    QCOMPARE(spy->count(), 2);
    for (int i = 0; i < spy->count(); ++i) {
        QCOMPARE(spy->at(i).at(0).toInt(), 1024);
    }
    delete spy;

    h.teardown();
}

void TestRxDspWorkerBufferSizing::setBufferSizes_emitsBufferSizesChanged()
{
    // Direct in-thread test: no QThread/queued-connection harness needed
    // because setBufferSizes() is documented as safe to call from any
    // thread, and the emit happens synchronously inside the call.
    RxDspWorker worker;
    worker.setSampleRate(48000.0);
    QSignalSpy spy(&worker, &RxDspWorker::bufferSizesChanged);

    worker.setBufferSizes(2048, 1024);

    QCOMPARE(spy.count(), 1);
    const auto args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 1024);
    QCOMPARE(args.at(1).toDouble(), 48000.0);
}

void TestRxDspWorkerBufferSizing::setBufferSizes_idempotent_noDuplicateEmit()
{
    RxDspWorker worker;
    worker.setSampleRate(48000.0);
    worker.setBufferSizes(2048, 1024);  // primes the last-emitted state
    QSignalSpy spy(&worker, &RxDspWorker::bufferSizesChanged);

    worker.setBufferSizes(2048, 1024);  // identical — must not fire

    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(TestRxDspWorkerBufferSizing)
#include "tst_rx_dsp_worker_buffer_sizing.moc"
