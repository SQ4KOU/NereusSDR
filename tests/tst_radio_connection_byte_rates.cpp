// no-port-check: test-only — exercises RadioConnection base-class rolling-window
// byte-rate counters. No Thetis logic is ported here; NereusSDR-original.
#include <QtTest/QtTest>
#include "core/RadioConnection.h"

#include <QElapsedTimer>
#include <QScopeGuard>

#include <atomic>
#include <thread>

using namespace NereusSDR;

// Minimal concrete subclass so we can instantiate the abstract base.
class NullRadioConnection : public RadioConnection {
    Q_OBJECT
public:
    NullRadioConnection() : RadioConnection() {}
    void init() override {}
    void connectToRadio(const RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void setMox(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void sendTxIq(const float*, int) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
    void setWatchdogEnabled(bool) override {}
};

class TstRadioConnectionByteRates : public QObject {
    Q_OBJECT

private slots:
    void initialRatesAreZero() {
        NullRadioConnection rc;
        QCOMPARE(rc.txByteRate(1000), 0.0);
        QCOMPARE(rc.rxByteRate(1000), 0.0);
    }

    void recordBytesAccumulatesInWindow() {
        NullRadioConnection rc;
        rc.recordBytesReceived(1000);
        rc.recordBytesReceived(2000);
        // 3000 bytes within last 1 second → 24 kbps → 0.024 Mbps
        const double mbps = rc.rxByteRate(1000);
        QVERIFY(mbps >= 0.020 && mbps <= 0.030);
    }

    void recordBytesAgesOutOfWindow() {
        NullRadioConnection rc;
        rc.recordBytesReceived(10000);
        QTest::qWait(1100);
        QCOMPARE(rc.rxByteRate(1000), 0.0);
    }

    // The connection thread records while the main thread reads the rates
    // (the readouts): every byte recorded is counted, and under
    // ThreadSanitizer the two do not race.
    // The count is small because each read scans every sample under the
    // lock, so a large count ran past the 5 s window on a loaded CI runner.
    void recordAndReadFromTwoThreads() {
        NullRadioConnection rc;
        constexpr int kRecords = 2000;
        constexpr int kWindowMs = 5000;
        // Every sample is still inside the window if the last rate read
        // happened less than this long after the first record.
        constexpr qint64 kMarginMs = 4000;
        std::atomic<bool> go{false};
        std::atomic<bool> done{false};
        QElapsedTimer elapsed;
        elapsed.start();
        // The writer starts only once the reader is about to read, so the
        // two overlap even if this thread is preempted after the spawn.
        std::thread writer([&] {
            while (!go.load()) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kRecords; ++i) {
                rc.recordBytesSent(10);
                rc.recordBytesReceived(20);
            }
            done.store(true);
        });
        // A failed QVERIFY returns early: release and join the writer on
        // every exit, or the joinable std::thread calls std::terminate.
        const auto joinWriter = qScopeGuard([&] {
            go.store(true);
            if (writer.joinable()) {
                writer.join();
            }
        });
        int reads = 0;
        go.store(true);
        do {
            QVERIFY(rc.txByteRate(kWindowMs) >= 0.0);
            QVERIFY(rc.rxByteRate(kWindowMs) >= 0.0);
            ++reads;
        } while (!done.load());
        writer.join();
        const double txMbps = rc.txByteRate(kWindowMs);
        const double rxMbps = rc.rxByteRate(kWindowMs);
        // Read the clock after the rates, so a pass here means no sample
        // had aged out when they were taken.
        const qint64 tookMs = elapsed.elapsed();
        QVERIFY2(tookMs < kMarginMs,
                 qPrintable(QStringLiteral("record and read took %1 ms, past the "
                                           "%2 ms margin inside the %3 ms window; "
                                           "early samples may have aged out")
                                .arg(tookMs).arg(kMarginMs).arg(kWindowMs)));
        // All of it inside the 5 s window: Mbps = bytes * 8 / 5000 / 1e3.
        QCOMPARE(txMbps, kRecords * 10 * 8.0 / (kWindowMs * 1000.0));
        QCOMPARE(rxMbps, kRecords * 20 * 8.0 / (kWindowMs * 1000.0));
        QVERIFY(reads > 0);
    }

    void txAndRxAreIndependent() {
        NullRadioConnection rc;
        rc.recordBytesSent(5000);
        rc.recordBytesReceived(2000);
        QVERIFY(rc.txByteRate(1000) > rc.rxByteRate(1000));
    }
};

QTEST_APPLESS_MAIN(TstRadioConnectionByteRates)
#include "tst_radio_connection_byte_rates.moc"
