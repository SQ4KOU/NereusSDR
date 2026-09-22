// no-port-check: NereusSDR-original daemon lifecycle regression tests.
#include <QtTest/QtTest>
#include <QFile>
#include <QPointer>
#include <QThread>
#include <QSemaphore>
#include <QScopeGuard>
#include <QTimer>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/RadioConnection.h"
#include "core/P2RadioConnection.h"
#include "core/WidebandFrameAccumulator.h"
#include "core/WidebandFftEngine.h"
#include "core/WdspEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#define private public
#include "core/daemon/DaemonApp.h"
#undef private
#include "fakes/FakeAudioBus.h"
#include "fakes/P1FakeRadio.h"
#include "fakes/P2FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::P1FakeRadio;

namespace {
void installOpenAudioBuses(AudioEngine& engine)
{
    AudioFormat format;
    format.sampleRate = 48000;
    format.channels = 2;
    format.sample = AudioFormat::Sample::Float32;

    auto speakers = std::make_unique<FakeAudioBus>(QStringLiteral("Fake speakers"));
    auto txInput = std::make_unique<FakeAudioBus>(QStringLiteral("Fake TX input"));
    const bool speakersOpened = speakers->open(format);
    const bool txInputOpened = txInput->open(format);
    Q_ASSERT(speakersOpened);
    Q_ASSERT(txInputOpened);
    Q_UNUSED(speakersOpened);
    Q_UNUSED(txInputOpened);

    engine.setSpeakersBusForTest(std::move(speakers));
    engine.setTxInputBusForTest(std::move(txInput));
}

void configureAudioForEachStart(RadioModel* model)
{
    Q_ASSERT(model);
    Q_ASSERT(model->audioEngine());
    model->audioEngine()->setStartInitializerForTest(installOpenAudioBuses);
}

RadioInfo infoFor(const P1FakeRadio& fake)
{
    RadioInfo info;
    info.address = fake.localAddress();
    info.port = fake.localPort();
    info.boardType = HPSDRHW::HermesLite;
    info.protocol = ProtocolVersion::Protocol1;
    info.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
    info.firmwareVersion = 72;
    return info;
}

void prepare(DaemonApp& app)
{
    app.m_synchronousWdspForTest = true;
    app.m_radioInitializerForTest = configureAudioForEachStart;
    app.m_radioRetryInitialMs = 10;
    app.m_radioRetryMaximumMs = 40;
    RadioDiscovery::clearHoldOffForTest();
}

// Exercise the real P2 assembler-to-FFT boundary. UDP parsing has its own
// focused coverage; this fixture controls delivery around the two queues.
void feedWidebandBurst(P2RadioConnection* connection)
{
    connection->setWidebandEnabled(0, true);
    const auto accumulators = connection->findChildren<WidebandFrameAccumulator*>();
    Q_ASSERT(accumulators.size() == 8);
    const QByteArray payload(1024, char(0x20));
    for (int sequence = 0; sequence < 32; ++sequence) {
        accumulators.first()->pushPacket(sequence, payload);
    }
}
}

class TestDaemonRadioRecovery : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        // The process-wide singleton otherwise shares the Qt test sandbox
        // with concurrent executables. Give this lifecycle fixture its own
        // file before the first AppSettings::instance() access.
        AppSettings::setProfileOverride(QStringLiteral("daemon-radio-recovery-%1")
                                        .arg(QCoreApplication::applicationPid()));
    }

    void init()
    {
        // A prior slot may persist receive membership for the same fake MAC.
        // Empty both the singleton and its file so every scenario starts from
        // an explicit no-manifest state.
        AppSettings::instance().clear();
        QString error;
        QVERIFY2(AppSettings::instance().save(&error), qPrintable(error));
    }

    void cleanupTestCase()
    {
        QFile::remove(AppSettings::instance().filePath());
    }

    void realP2SilenceRebuildsOnlySelectedRadio()
    {
        NereusSDR::Test::P2FakeRadio fake;
        QVERIFY(fake.start());
        const RadioInfo info = fake.radioInfo();
        std::atomic<bool> available {true};
        DaemonApp app;
        prepare(app);
        app.m_discoveryProviderForTest = [&]() {
            return available.load() ? QList<RadioInfo>{info} : QList<RadioInfo>{};
        };
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.radioMac = info.macAddress;
        cfg.sliceCount = 2;
        cfg.sampleRateHz = 48000;
        QVERIFY(app.start(cfg));
        RadioModel* const model = app.m_radioModel.get();
        QVERIFY(!model->connection()); // discovery is scheduled after start returns.
        model->configureP2TransportForTest(fake.outboundPortBase(),
                                          fake.inputRolePortBase(), 1000, 200);
        QTimer ingress;
        ingress.setInterval(5);
        connect(&ingress, &QTimer::timeout, &app, [&]() {
            if (fake.hasClient()) {
                fake.sendDdc(2);
                fake.sendDdc(3);
                fake.sendStatus();
            }
        });
        ingress.start();
        QTRY_VERIFY_WITH_TIMEOUT(model->isConnected(), 15000);
        QTRY_COMPARE(app.sliceCount(), 2);
        QPointer<RadioConnection> oldConnection(model->connection());
        SliceModel* const a = model->slices().first();
        SliceModel* const b = model->slices().last();
        a->setFrequency(3865100);
        b->setFrequency(14225000);
        b->setDspMode(DSPMode::USB);
        b->setPanKey(QStringLiteral("pan-1"));
        model->setActiveSlice(b->sliceIndex());
        QVERIFY(model->wdspEngine()->isInitialized());
        QVERIFY(fake.totalEgressDatagrams() > 0);
        // Queue old-radio wideband work behind a blocked dispatch thread.
        // Recovery must retire it, including work already off the UDP thread.
        QSignalSpy widebandFrames(model, &RadioModel::widebandSpectrumReady);
        QSemaphore entered;
        QSemaphore release;
        QSemaphore frameProcessed;
        std::atomic<bool> oldWorkDrained {false};
        auto releaseOnExit = qScopeGuard([&]() {
            release.release();
            // Failure paths must join before the queued callbacks' stack
            // captures are destroyed, including when an assertion returns.
            app.widebandThread()->quit();
            app.widebandThread()->wait();
        });
        auto* blocker = new QObject;
        blocker->moveToThread(app.widebandThread());
        connect(app.widebandThread(), &QThread::finished, blocker, &QObject::deleteLater);
        QMetaObject::invokeMethod(blocker, [&]() {
            entered.release();
            release.acquire();
        }, Qt::QueuedConnection);
        QVERIFY(entered.tryAcquire(1, 1000));
        auto* const oldP2 = qobject_cast<P2RadioConnection*>(model->connection());
        QVERIFY(oldP2);
        QVERIFY(QMetaObject::invokeMethod(oldP2, [oldP2]() {
            feedWidebandBurst(oldP2);
        }, Qt::BlockingQueuedConnection));
        available = false;
        fake.stopIngress();
        QTRY_VERIFY_WITH_TIMEOUT(!model->connection(), 5000);
        QTRY_VERIFY(oldConnection.isNull());
        QCOMPARE(model->connectionState(), ConnectionState::Disconnected);
        QVERIFY(!model->wdspEngine()->isInitialized());
        QVERIFY(a->streamIndex() < 0);
        QVERIFY(b->streamIndex() < 0);
        QCOMPARE(fake.lastHighPriorityFlags() & 3, 0);
        QCOMPARE(fake.moxAssertedCount(), 0);
        QCOMPARE(model->activeSlice(), b);
        QCOMPARE(model->slices().first(), a);
        QCOMPARE(model->slices().last(), b);
        release.release();
        QMetaObject::invokeMethod(blocker, [&]() { oldWorkDrained = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(oldWorkDrained.load());
        QCoreApplication::processEvents();
        QCOMPARE(widebandFrames.count(), 0);

        fake.resumeIngress();
        available = true;
        RadioDiscovery::clearHoldOffForTest();
        app.m_radioRetryTimer->start(0);
        QTRY_VERIFY_WITH_TIMEOUT(model->isConnected(), 15000);
        QCOMPARE(model->currentRadioMac(), info.macAddress);
        QCOMPARE(model->activeSlice(), b);
        QCOMPARE(model->slices().first(), a);
        QCOMPARE(model->slices().last(), b);
        QCOMPARE(a->frequency(), 3865100.0);
        QCOMPARE(b->frequency(), 14225000.0);
        QCOMPARE(b->panKey(), QStringLiteral("pan-1"));
        QVERIFY(a->streamIndex() >= 0);
        QVERIFY(b->streamIndex() >= 0);
        QVERIFY(model->wdspEngine()->isInitialized());
        QVERIFY(model->receiveOnlyStationPolicy());
        QCOMPARE(fake.moxAssertedCount(), 0);
        auto* const freshP2 = qobject_cast<P2RadioConnection*>(model->connection());
        QVERIFY(freshP2);
        // A remote view can acknowledge the real capture identity and paint
        // empty wings before any ADC samples arrive. DDC and ADC cadence are
        // independent; waiting for the first survey would stall first zoom.
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            freshP2->setWidebandEnabled(0, true);
        }, Qt::BlockingQueuedConnection));
        QTRY_VERIFY(model->widebandSourceDescriptor(0));
        const auto preparedSource = model->widebandSourceDescriptor(0);
        QVERIFY(preparedSource->sourceGeneration != 0);
        QVERIFY(!model->latestWidebandSpectrum(0));
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        QTRY_COMPARE(widebandFrames.count(), 1);

        const auto firstWideband = model->latestWidebandSpectrum(0);
        QVERIFY(firstWideband);
        QCOMPARE(firstWideband->source, *preparedSource);
        QCOMPARE(firstWideband->source.physicalAdcIndex, 0);
        QCOMPARE(firstWideband->source.adcRateHz, 122880000.0);
        QVERIFY(firstWideband->source.sourceGeneration != 0);
        QVERIFY(firstWideband->producedAtNs > 0);
        QCOMPARE(firstWideband->rawDbBins.size(), WidebandFftEngine::kOutputBins);
        QVERIFY(!model->latestWidebandSpectrum(1));
        QVERIFY(!model->widebandAdcRateHz(-1));
        QVERIFY(!model->widebandAdcRateHz(2));

        // A still-connected radio can also retire an ADC capture. A row
        // queued before disable/re-enable must not enter the next capture.
        widebandFrames.clear();
        QMetaObject::invokeMethod(blocker, [&]() {
            entered.release();
            release.acquire();
        }, Qt::QueuedConnection);
        QVERIFY(entered.tryAcquire(1, 1000));
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
            freshP2->setWidebandEnabled(0, false);
            freshP2->setWidebandEnabled(0, true);
        }, Qt::BlockingQueuedConnection));
        // The owner-thread retirement notification is still queued. A read
        // must already refuse the old cached frame using the retained token.
        QVERIFY(!model->latestWidebandSpectrum(0));
        release.release();
        QMetaObject::invokeMethod(blocker, [&]() {
            frameProcessed.release();
        }, Qt::QueuedConnection);
        QVERIFY(frameProcessed.tryAcquire(1, 1000));
        QCoreApplication::processEvents();
        QCOMPARE(widebandFrames.count(), 0);

        // Retirement after FFT but before owner-thread publication must also
        // drop that row. The valid row afterward proves capture still works.
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        QMetaObject::invokeMethod(blocker, [&]() {
            frameProcessed.release();
        }, Qt::QueuedConnection);
        QVERIFY(frameProcessed.tryAcquire(1, 1000));
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            freshP2->setWidebandEnabled(0, false);
            freshP2->setWidebandEnabled(0, true);
        }, Qt::BlockingQueuedConnection));
        QCoreApplication::processEvents();
        QCOMPARE(widebandFrames.count(), 0);
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        QTRY_COMPARE(widebandFrames.count(), 1);

        const auto renewed = model->latestWidebandSpectrum(0);
        QVERIFY(renewed);
        QVERIFY(renewed->source.sourceGeneration > firstWideband->source.sourceGeneration);

        // Geometry changes retire both cached rows and frames waiting before
        // FFT, even if the configured rate is changed back before dispatch.
        widebandFrames.clear();
        QMetaObject::invokeMethod(blocker, [&]() {
            entered.release();
            release.acquire();
        }, Qt::QueuedConnection);
        QVERIFY(entered.tryAcquire(1, 1000));
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        model->widebandFftEngine(0)->setAdcSampleRateHz(61440000.0);
        model->widebandFftEngine(0)->setAdcSampleRateHz(122880000.0);
        QVERIFY(!model->latestWidebandSpectrum(0));
        release.release();
        QMetaObject::invokeMethod(blocker, [&]() { frameProcessed.release(); }, Qt::QueuedConnection);
        QVERIFY(frameProcessed.tryAcquire(1, 1000));
        QCoreApplication::processEvents();
        QCOMPARE(widebandFrames.count(), 0);

        // Host production time predates FFT queueing; it cannot be refreshed
        // merely because a blocked worker eventually finishes the transform.
        QMetaObject::invokeMethod(blocker, [&]() {
            entered.release();
            release.acquire();
        }, Qt::QueuedConnection);
        QVERIFY(entered.tryAcquire(1, 1000));
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        const qint64 beforeRelease = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        release.release();
        QTRY_COMPARE(widebandFrames.count(), 1);
        const auto delayed = model->latestWidebandSpectrum(0);
        QVERIFY(delayed);
        QVERIFY(delayed->producedAtNs <= beforeRelease);
        QVERIFY(delayed->source.sourceGeneration > renewed->source.sourceGeneration);

        const int demandSlice = model->slices().first()->sliceIndex();
        const auto demandOwner = model->acquireWidebandDemand(demandSlice);
        QVERIFY(demandOwner != 0);
        QVERIFY(model->setWidebandDemandActive(demandOwner, true));

        // Complete another FFT while the owner thread is synchronously
        // waiting. Its publication is now queued here but not delivered.
        // Retiring the connection must also reject this later race window.
        widebandFrames.clear();
        QVERIFY(QMetaObject::invokeMethod(freshP2, [freshP2]() {
            feedWidebandBurst(freshP2);
        }, Qt::BlockingQueuedConnection));
        QVERIFY(QMetaObject::invokeMethod(blocker, [&]() {
            frameProcessed.release();
        }, Qt::QueuedConnection));
        QVERIFY(frameProcessed.tryAcquire(1, 1000));
        int retirementNotifications = 0;
        bool retiredSourceOffered = false;
        bool retiredDemandAdmitted = false;
        connect(model, &RadioModel::widebandSourceChanged, model, [&](int) {
            ++retirementNotifications;
            retiredSourceOffered |= model->widebandAdcRateHz(0).has_value()
                || model->widebandAdcRateHz(1).has_value();
            retiredDemandAdmitted |= model->acquireWidebandDemand(demandSlice) != 0
                || model->setWidebandDemandActive(demandOwner, true);
        }, Qt::DirectConnection);
        model->disconnectFromRadio();
        QVERIFY(retirementNotifications > 0);
        QVERIFY(!retiredSourceOffered);
        QVERIFY(!retiredDemandAdmitted);
        QVERIFY(!model->setWidebandDemandActive(demandOwner, true));
        QVERIFY(!model->latestWidebandSpectrum(0));
        QVERIFY(!model->widebandAdcRateHz(0));
        QCoreApplication::processEvents();
        QCOMPARE(widebandFrames.count(), 0);
        app.stop();
    }

    void lateSelectedRadioKeepsControlResponsive()
    {
        P1FakeRadio fake;
        fake.start();
        const RadioInfo info = infoFor(fake);
        RadioInfo wrong = info;
        wrong.macAddress = QStringLiteral("aa:bb:cc:99:99:99");
        std::atomic<int> scans {0};
        std::atomic<bool> available {false};
        DaemonApp app;
        prepare(app);
        app.m_discoveryProviderForTest = [&]() {
            ++scans;
            return available.load() ? QList<RadioInfo>{wrong, info} : QList<RadioInfo>{wrong};
        };
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.radioMac = info.macAddress.toUpper();
        cfg.sliceCount = 2;
        QVERIFY(app.start(cfg));
        SliceModel* const initial = app.m_radioModel->slices().first();
        QTRY_VERIFY_WITH_TIMEOUT(scans >= 2, 1000);
        QVERIFY(!app.m_radioModel->connection());
        QVERIFY(!fake.isRunning());
        QVERIFY(app.m_radioModel->receiveOnlyStationPolicy());
        available = true;
        QTRY_VERIFY_WITH_TIMEOUT(app.m_radioModel->isConnected(), 10000);
        QTRY_COMPARE(app.sliceCount(), 2);
        QCOMPARE(app.m_radioModel->slices().first(), initial);
        QCOMPARE(app.m_radioModel->currentRadioMac(), info.macAddress);
        QVERIFY(fake.isRunning());
        QVERIFY(!app.m_radioModel->mox());
        app.stop();
        QCOMPARE(app.sliceCount(), 0);
        QVERIFY(!app.m_radioDiscoveryThread);
        QVERIFY(!app.m_radioRetryTimer->isActive());
    }

    void quietPeriodAndBusyRadioDoNotStartAConnection()
    {
        std::atomic<int> scans {0};
        DaemonApp app;
        prepare(app);
        RadioInfo busy;
        busy.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
        busy.inUse = true;
        app.m_discoveryProviderForTest = [&]() {
            ++scans;
            return QList<RadioInfo>{busy};
        };
        RadioDiscovery guard;
        guard.holdOffScans(std::chrono::seconds(10));
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.radioMac = busy.macAddress;
        QVERIFY(app.start(cfg));
        QTRY_VERIFY(app.m_radioRetryTimer->interval() > 1000);
        QCOMPARE(scans.load(), 0);
        QVERIFY(!app.m_radioDiscoveryThread);
        RadioDiscovery::clearHoldOffForTest();
        app.m_radioRetryTimer->start(0);
        QTRY_VERIFY(scans >= 2);
        QVERIFY(!app.m_radioModel->connection());
        QVERIFY(app.m_radioRetryNextMs <= app.m_radioRetryMaximumMs);
        app.stop();
        QVERIFY(!app.m_radioRetryTimer->isActive());
    }

    void workerAndQueuedResultsCancelOnStop()
    {
        std::atomic<bool> entered {false};
        std::atomic<bool> workerThread {false};
        DaemonApp app;
        prepare(app);
        app.m_discoveryProviderForTest = [&]() {
            workerThread = QThread::currentThread() != app.thread();
            entered = true;
            while (!QThread::currentThread()->isInterruptionRequested()) {
                QThread::msleep(1);
            }
            RadioInfo late;
            late.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
            return QList<RadioInfo>{late};
        };
        int ticks = 0;
        QTimer pulse;
        pulse.setInterval(1);
        connect(&pulse, &QTimer::timeout, &app, [&]() { ++ticks; });
        pulse.start();
        QVERIFY(app.start(DaemonConfig::defaults()));
        QTRY_VERIFY(entered.load() && ticks > 2);
        QVERIFY(workerThread.load());
        app.stop();
        QCoreApplication::processEvents();
        QVERIFY(!app.m_radioModel);
        QVERIFY(!app.m_radioDiscoveryThread);
        QVERIFY(!app.m_radioRetryTimer->isActive());

        app.primeBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(app.start(DaemonConfig::defaults()));
        QCoreApplication::processEvents();
        QVERIFY(!app.m_radioModel->connection());
        QVERIFY(!app.m_radioDiscoveryThread);
        app.stop();
    }

    void stopDuringConnectionSetupDoesNotDialAfterCancellation()
    {
        P1FakeRadio fake;
        fake.start();
        const RadioInfo info = infoFor(fake);
        DaemonApp app;
        prepare(app);
        app.m_discoveryProviderForTest = [info]() { return QList<RadioInfo>{info}; };
        QVERIFY(app.start(DaemonConfig::defaults()));
        bool stopReached = false;
        connect(app.m_radioModel->wdspEngine(), &WdspEngine::initializedChanged,
                &app, [&](bool ready) {
            if (ready) {
                stopReached = true;
                app.stop();
                // The current connect stack still owns this model until it
                // returns, even though all recovery work is cancelled.
                QVERIFY(app.m_radioModel);
                QVERIFY(app.m_stopDeferred);
            }
        });
        QTRY_VERIFY_WITH_TIMEOUT(stopReached, 10000);
        QTRY_VERIFY(!app.m_radioModel);
        QVERIFY(!fake.isRunning());
        QVERIFY(!app.m_radioRetryTimer->isActive());
        QVERIFY(!app.m_radioDiscoveryThread);
    }

    void lossRetiresPipelinePreservesSlicesAndPinnedIdentity()
    {
        P1FakeRadio fake;
        fake.start();
        const RadioInfo info = infoFor(fake);
        std::atomic<bool> offerSelected {true};
        std::atomic<int> scans {0};
        DaemonApp app;
        prepare(app);
        app.m_discoveryProviderForTest = [&]() {
            ++scans;
            RadioInfo candidate = info;
            if (!offerSelected.load()) {
                candidate.macAddress = QStringLiteral("aa:bb:cc:99:99:99");
            }
            return QList<RadioInfo>{candidate};
        };
        DaemonConfig cfg = DaemonConfig::defaults();
        cfg.sliceCount = 2;
        QVERIFY(app.start(cfg));
        QTRY_VERIFY_WITH_TIMEOUT(app.m_radioModel->isConnected(), 10000);
        QTRY_COMPARE(app.sliceCount(), 2);
        RadioModel* const model = app.m_radioModel.get();
        const QPointer<SliceModel> a = model->slices().first();
        const QPointer<SliceModel> b = model->slices().last();
        a->setFrequency(3865100);
        b->setFrequency(14225000);
        b->setDspMode(DSPMode::USB);
        model->setActiveSlice(b->sliceIndex());
        offerSelected = false;
        // P2 UDP loss detection has its own real-wire regression. Here inject
        // its model-state boundary to exercise actual daemon/DSP retirement.
        model->onConnectionStateChangedForTest(ConnectionState::LinkLost);
        QTRY_VERIFY(!model->connection());
        QCOMPARE(model->connectionState(), ConnectionState::Disconnected);
        QVERIFY(!model->wdspEngine()->isInitialized());
        QVERIFY2(a, "loss retirement deleted the original Slice A object");
        QVERIFY2(b, "loss retirement deleted the original Slice B object");
        QVERIFY(a->streamIndex() < 0);
        QVERIFY(b->streamIndex() < 0);
        QCOMPARE(app.m_selectedRadioMac, info.macAddress);
        QCOMPARE(model->activeSlice(), b.data());
        const int afterLoss = scans.load();
        RadioDiscovery::clearHoldOffForTest();
        app.m_radioRetryTimer->start(0);
        QTRY_VERIFY(scans > afterLoss);
        QVERIFY(!model->connection());
        offerSelected = true;
        QTRY_VERIFY_WITH_TIMEOUT(model->isConnected(), 10000);
        QCOMPARE(model->slices().size(), 2);
        QVERIFY2(a, "recovery deleted the original Slice A object");
        QVERIFY2(b, "recovery deleted the original Slice B object");
        QCOMPARE(model->slices().first(), a.data());
        QCOMPARE(model->slices().last(), b.data());
        QCOMPARE(model->activeSlice(), b.data());
        QCOMPARE(a->frequency(), 3865100.0);
        QCOMPARE(b->frequency(), 14225000.0);
        QVERIFY(model->receiveOnlyStationPolicy());
        QVERIFY(!model->mox());

        model->disconnectFromRadio();
        QVERIFY(!app.m_radioRecoveryEnabled);
        QVERIFY(!app.m_radioRetryTimer->isActive());
        QCoreApplication::processEvents();
        QVERIFY(!model->connection());
        app.stop();
    }
};

QTEST_MAIN(TestDaemonRadioRecovery)
#include "tst_daemon_radio_recovery.moc"
