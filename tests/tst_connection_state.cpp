#include <QObject>
#include <QSignalSpy>
#include <QtTest>
#include "core/ConnectionState.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TstConnectionState : public QObject {
    Q_OBJECT
private slots:
    void initialStateIsDisconnected() {
        RadioModel m;
        QCOMPARE(m.connectionState(), ConnectionState::Disconnected);
    }

    void emitsSignalOnTransition() {
        RadioModel m;
        QSignalSpy spy(&m, &RadioModel::connectionStateChanged);
        m.setConnectionStateForTest(ConnectionState::Probing);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(m.connectionState(), ConnectionState::Probing);
    }

    void noSignalForRedundantTransition() {
        RadioModel m;
        m.setConnectionStateForTest(ConnectionState::Probing);
        QSignalSpy spy(&m, &RadioModel::connectionStateChanged);
        m.setConnectionStateForTest(ConnectionState::Probing);
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TstConnectionState)
#include "tst_connection_state.moc"
