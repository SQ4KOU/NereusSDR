// no-port-check: NereusSDR-original test file. Exercises new ConnectFailure
// enum and connectFailed() signal added in Phase 3Q Task 3. No Thetis logic
// is ported here; the tested infrastructure is NereusSDR-original.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QHostAddress>
#include "core/P1RadioConnection.h"
#include "core/RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestRadioConnectionFailure : public QObject {
    Q_OBJECT

private:
    // Build a RadioInfo pointing at an RFC 5737 unreachable address so
    // connectToRadio() can never hear a reply.
    RadioInfo unreachableInfo() const {
        RadioInfo info;
        info.address         = QHostAddress(QStringLiteral("192.0.2.1"));
        info.port            = 1024;
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.macAddress      = QStringLiteral("00:00:00:00:00:00");
        info.firmwareVersion = 72;
        info.name            = QStringLiteral("Unreachable");
        return info;
    }

private slots:
    // After connectToRadio() to an unreachable host, connectFailed(Timeout, ...)
    // must be emitted within the 2-second connect-watchdog budget.
    // Budget for spy.wait(): 2000 ms connect watchdog + 1000 ms slack = 3000 ms.
    void emitsTypedFailureOnUnreachable() {
        P1RadioConnection conn;
        conn.init();

        QSignalSpy spy(&conn, &RadioConnection::connectFailed);

        conn.connectToRadio(unreachableInfo());

        QVERIFY(spy.wait(3000));
        QCOMPARE(spy.count(), 1);

        const auto reason = spy.takeFirst().at(0).value<ConnectFailure>();
        QCOMPARE(reason, ConnectFailure::Timeout);
    }

    // connectFailed must NOT fire when an intentional disconnect() is called —
    // that is a user-initiated state change, not a failure.
    void noFailureOnIntentionalDisconnect() {
        P1RadioConnection conn;
        conn.init();

        QSignalSpy spy(&conn, &RadioConnection::connectFailed);

        conn.connectToRadio(unreachableInfo());
        // Immediately disconnect before the connect watchdog can fire.
        conn.disconnect();

        // Give the event loop a tick — if connectFailed was wrongly queued it
        // would arrive here.
        QTest::qWait(100);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestRadioConnectionFailure)
#include "tst_radio_connection_failure.moc"
