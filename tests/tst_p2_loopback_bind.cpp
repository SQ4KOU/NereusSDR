// tests/tst_p2_loopback_bind.cpp
//
// Real-loopback check that the Protocol 2 receive socket listens on the
// address the radio answers, not on every address. A socket bound to every
// address can be given a port that another socket holds on the radio's
// answer address, and that other socket then takes the radio's frames
// (see P2RadioConnection::bindToRadioFacingAddress). Checked on the first
// connect and again after a disconnect, which closes the socket.

#include <QtTest/QtTest>

#include <QEventLoop>
#include <QSignalSpy>
#include <QTimer>
#include <QUdpSocket>

#include "core/P2RadioConnection.h"
#include "core/RadioConnection.h"
#include "fakes/P2FakeRadio.h"

using namespace NereusSDR;
using NereusSDR::Test::P2FakeRadio;

namespace {

// Connects and returns once the connection reports Connected, sending DDCs
// once the fake has heard from the client. 3 s bounds a connect that never
// comes.
bool establish(P2RadioConnection& connection, P2FakeRadio& fake)
{
    QEventLoop loop;
    bool connected = false;
    const QMetaObject::Connection onState = QObject::connect(
        &connection, &RadioConnection::connectionStateChanged, &loop,
        [&loop, &connected](ConnectionState state) {
            if (state == ConnectionState::Connected) {
                connected = true;
                loop.quit();
            }
        });
    const QMetaObject::Connection onFailed = QObject::connect(
        &connection, &RadioConnection::connectFailed, &loop, &QEventLoop::quit);
    // Sent on every tick until Connected: after a reconnect the fake may
    // still hold the previous host port until the new one's first packet.
    QTimer ddc;
    ddc.setInterval(10);
    QObject::connect(&ddc, &QTimer::timeout, &loop, [&fake]() {
        if (fake.hasClient()) {
            fake.sendDdc(2);
        }
    });
    QTimer limit;
    limit.setSingleShot(true);
    QObject::connect(&limit, &QTimer::timeout, &loop, &QEventLoop::quit);

    connection.connectToRadio(fake.radioInfo());
    if (connection.state() != ConnectionState::Connected) {
        ddc.start();
        limit.start(3000);
        loop.exec();
    }
    QObject::disconnect(onState);
    QObject::disconnect(onFailed);
    return connected || connection.state() == ConnectionState::Connected;
}

} // namespace

class TestP2LoopbackBind final : public QObject {
    Q_OBJECT

private slots:
    void anotherSocketOnTheRadiosAddressCannotTakeItsFrames()
    {
        P2FakeRadio fake;
        QVERIFY(fake.start());

        P2RadioConnection connection;
        connection.setPortBasesForTest(fake.outboundPortBase(),
                                       fake.inputRolePortBase());
        connection.init();

        QSignalSpy iq(&connection, &RadioConnection::iqDataReceived);

        for (int attempt = 0; attempt < 2; ++attempt) {
            if (attempt > 0) {
                connection.disconnect();
                QCOMPARE(connection.state(), ConnectionState::Disconnected);
            }
            QVERIFY2(establish(connection, fake),
                     qPrintable(QStringLiteral("connect %1").arg(attempt + 1)));
            QTRY_VERIFY_WITH_TIMEOUT(fake.hasClient(), 2000);
            const quint16 hostPort = fake.clientPort();
            QVERIFY(hostPort != 0);

            // Another program's socket on the radio's answer address and
            // the host's port. Sharing is requested, as a program that
            // wants the port would; only a socket bound to that address
            // itself keeps it out.
            QUdpSocket other;
            const bool otherBound = other.bind(fake.localAddress(), hostPort,
                                               QAbstractSocket::ShareAddress);

            iq.clear();
            for (int i = 0; i < 5; ++i) {
                fake.sendDdc(2);
            }
            QTRY_VERIFY_WITH_TIMEOUT(iq.count() >= 5, 2000);
            QVERIFY2(!otherBound || !other.hasPendingDatagrams(),
                     "another socket took the radio's frames");
        }
        connection.disconnect();
    }
};

QTEST_MAIN(TestP2LoopbackBind)
#include "tst_p2_loopback_bind.moc"
