// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 J.J. Boyd (KG4VCF)
// no-port-check: NereusSDR-original ownership regression, no radio I/O.
// 2026-10-14: J.J. Boyd, AI-assisted via OpenAI Codex.

#include "core/RadioConnectionTeardown.h"

#include <QPointer>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>

using namespace NereusSDR;

namespace {

struct CleanupObservation {
    QSemaphore initialized;
    std::atomic<int> disconnects{0};
    std::atomic<int> destructions{0};
    std::atomic<int> childDestructions{0};
    std::atomic<int> order{0};
    std::atomic<int> disconnectOrder{0};
    std::atomic<int> destructionOrder{0};
    std::atomic<int> childDestructionOrder{0};
    std::atomic<QThread*> disconnectThread{nullptr};
    std::atomic<QThread*> destructionThread{nullptr};
    std::atomic<QThread*> childDestructionThread{nullptr};
    std::atomic<bool> cleanupOnAffinity{false};
    std::atomic<bool> destructionOnAffinity{false};
    std::atomic<bool> childDestructionOnAffinity{false};
    std::atomic<bool> timerWasActive{false};
};

class ObservedTimer final : public QTimer {
public:
    ObservedTimer(CleanupObservation& observation, QObject* parent)
        : QTimer(parent), m_observation(observation) {}

    ~ObservedTimer() override
    {
        m_observation.childDestructionThread = QThread::currentThread();
        m_observation.childDestructionOnAffinity = thread() == QThread::currentThread();
        m_observation.childDestructionOrder = ++m_observation.order;
        ++m_observation.childDestructions;
    }

private:
    CleanupObservation& m_observation;
};

// The actual RadioConnection base and teardown helper own the behavior under
// test. This concrete double replaces only protocol/device operations.
class OwnedConnection final : public RadioConnection {
public:
    explicit OwnedConnection(CleanupObservation& observation)
        : m_observation(observation) {}

    ~OwnedConnection() override
    {
        m_observation.destructionThread = QThread::currentThread();
        m_observation.destructionOnAffinity = thread() == QThread::currentThread();
        m_observation.destructionOrder = ++m_observation.order;
        ++m_observation.destructions;
    }

    void init() override
    {
        auto timer = std::make_unique<ObservedTimer>(m_observation, this);
        timer->start(60'000);
        m_timer = timer.release();
        m_observation.initialized.release();
    }

    void disconnect() override
    {
        m_observation.disconnectThread = QThread::currentThread();
        m_observation.cleanupOnAffinity = thread() == QThread::currentThread();
        m_observation.disconnectOrder = ++m_observation.order;
        ++m_observation.disconnects;
        if (m_timer) {
            m_observation.timerWasActive = m_timer->isActive();
            m_timer->stop();
        }
    }

    void connectToRadio(const RadioInfo&) override {}
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

private:
    CleanupObservation& m_observation;
    ObservedTimer* m_timer{nullptr}; // QObject child, owned by this.
};

std::atomic<int> g_warnings{0};
QtMessageHandler g_previousHandler{nullptr};

void captureWarnings(QtMsgType type, const QMessageLogContext& context,
                     const QString& message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        ++g_warnings;
    }
    if (g_previousHandler) {
        g_previousHandler(type, context, message);
    }
}

} // namespace

class TestConnectionTeardownOwnership : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { g_previousHandler = qInstallMessageHandler(captureWarnings); }
    void cleanupTestCase() { qInstallMessageHandler(g_previousHandler); }
    void init() { g_warnings = 0; }

    void releasesOwnedConnection_data()
    {
        QTest::addColumn<int>("lifecycle");
        QTest::newRow("never-started") << 0;
        QTest::newRow("already-finished-with-active-child-timer") << 1;
        QTest::newRow("running-with-active-child-timer") << 2;
    }

    void releasesOwnedConnection()
    {
        QFETCH(int, lifecycle);
        CleanupObservation observation;
        auto workerOwner = std::make_unique<QThread>();
        QThread* const originalWorker = workerOwner.get();
        auto connectionOwner = std::make_unique<OwnedConnection>(observation);
        QPointer<RadioConnection> survivingConnection(connectionOwner.get());
        QPointer<QThread> survivingWorker(originalWorker);
        connectionOwner->moveToThread(originalWorker);
        connect(originalWorker, &QThread::started,
                connectionOwner.get(), &RadioConnection::init);
        if (lifecycle != 0) {
            originalWorker->start();
            QVERIFY(observation.initialized.tryAcquire(1, 3000));
            if (lifecycle == 1) {
                originalWorker->quit();
                QVERIFY(originalWorker->wait(3000));
            }
        }

        RadioConnection* connection = connectionOwner.release();
        QThread* worker = workerOwner.release();
        teardownWorkerThreadedConnection(connection, worker);

        QVERIFY(connection == nullptr);
        QVERIFY(worker == nullptr);
        QVERIFY(survivingWorker.isNull());
        // Nulling the caller alone used to pass even when the object leaked.
        QVERIFY(survivingConnection.isNull());
        QCOMPARE(observation.disconnects.load(), 1);
        QCOMPARE(observation.destructions.load(), 1);
        QVERIFY(observation.cleanupOnAffinity.load());
        QVERIFY(observation.destructionOnAffinity.load());
        QVERIFY(observation.disconnectOrder.load() < observation.destructionOrder.load());
        QThread* const expectedThread = lifecycle == 2 ? originalWorker : QThread::currentThread();
        QCOMPARE(observation.disconnectThread.load(), expectedThread);
        QCOMPARE(observation.destructionThread.load(), expectedThread);
        if (lifecycle != 0) {
            QCOMPARE(observation.childDestructions.load(), 1);
            QVERIFY(observation.timerWasActive.load());
            QVERIFY(observation.childDestructionOnAffinity.load());
            QCOMPARE(observation.childDestructionThread.load(), expectedThread);
            QVERIFY(observation.destructionOrder.load() < observation.childDestructionOrder.load());
        }
        QCOMPARE(g_warnings.load(), 0);
    }
};

QTEST_GUILESS_MAIN(TestConnectionTeardownOwnership)
#include "tst_connection_teardown_ownership.moc"
