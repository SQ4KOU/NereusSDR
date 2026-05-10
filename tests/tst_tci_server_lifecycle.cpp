// tests/tst_tci_server_lifecycle.cpp  (NereusSDR)
// NereusSDR-original — no Thetis upstream port in this file.
//
// Phase 3J-1 Task 2.1: TciServer + TciClientSession skeleton.
// Covers the connect/disconnect lifecycle contract:
//   - start(0) returns true and emits serverStarted
//   - double-start is rejected (returns false)
//   - stop() clears running state and emits serverStopped

#ifdef HAVE_WEBSOCKETS

#include <QtTest>
#include <QSignalSpy>

#include "core/TciServer.h"

using namespace NereusSDR;

class TestTciServerLifecycle : public QObject {
    Q_OBJECT
private slots:
    void start_listens_and_emits_signal();
    void start_called_twice_returns_false();
    void stop_clears_running();
};

void TestTciServerLifecycle::start_listens_and_emits_signal()
{
    TciServer server(nullptr);   // RadioModel* not needed for lifecycle tests
    QSignalSpy startedSpy(&server, &TciServer::serverStarted);
    QVERIFY(!server.isRunning());
    QVERIFY(server.start(0));    // 0 = OS-assigned ephemeral port
    QVERIFY(server.isRunning());
    QCOMPARE(startedSpy.count(), 1);
    QVERIFY(server.port() != 0); // OS assigned a real port
    server.stop();
}

void TestTciServerLifecycle::start_called_twice_returns_false()
{
    TciServer server(nullptr);
    QVERIFY(server.start(0));
    QVERIFY(!server.start(0));   // double-start rejected per plan Q7 + NereusSDR contract
    server.stop();
}

void TestTciServerLifecycle::stop_clears_running()
{
    TciServer server(nullptr);
    QSignalSpy stoppedSpy(&server, &TciServer::serverStopped);
    QVERIFY(server.start(0));
    server.stop();
    QVERIFY(!server.isRunning());
    QCOMPARE(server.port(), 0);
    QCOMPARE(stoppedSpy.count(), 1);
}

QTEST_GUILESS_MAIN(TestTciServerLifecycle)
#include "tst_tci_server_lifecycle.moc"

#else
// WebSockets not available — test file must still compile and produce a
// no-op binary so CTest doesn't report a missing executable.
int main() { return 0; }
#endif // HAVE_WEBSOCKETS
