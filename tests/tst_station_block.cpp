// tests/tst_station_block.cpp
#include <QtTest/QtTest>
#include <QSignalSpy>

#include "gui/widgets/StationBlock.h"

using namespace NereusSDR;

class TstStationBlock : public QObject {
    Q_OBJECT

private slots:
    void defaultsToDisconnectedAppearance() {
        StationBlock s;
        QVERIFY(!s.isConnectedAppearance());
        QVERIFY(s.radioName().isEmpty());
    }

    void setRadioNameSwitchesToConnected() {
        StationBlock s;
        s.setRadioName(QStringLiteral("ANAN-G2 (Saturn)"));
        QVERIFY(s.isConnectedAppearance());
        QCOMPARE(s.radioName(), QStringLiteral("ANAN-G2 (Saturn)"));
        // Cyan-border style applied
        QVERIFY(s.styleSheet().contains(QStringLiteral("rgba(0,180,216,80)")));
    }

    void emptyNameRevertsToDisconnected() {
        StationBlock s;
        s.setRadioName(QStringLiteral("X"));
        s.setRadioName(QString());
        QVERIFY(!s.isConnectedAppearance());
        // Dashed red style applied
        QVERIFY(s.styleSheet().contains(QStringLiteral("dashed")));
    }

    void leftClickEmitsClickedInBothAppearances() {
        StationBlock s;
        s.resize(180, 22);
        QSignalSpy spy(&s, &StationBlock::clicked);

        // Disconnected
        QTest::mouseClick(&s, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);

        // Connected
        s.setRadioName(QStringLiteral("ANAN-G2"));
        QTest::mouseClick(&s, Qt::LeftButton);
        QCOMPARE(spy.count(), 2);
    }

    void rightClickEmitsContextMenuOnlyWhenConnected() {
        StationBlock s;
        s.resize(180, 22);
        QSignalSpy spy(&s, &StationBlock::contextMenuRequested);

        // Disconnected — right-click should NOT emit
        QTest::mouseClick(&s, Qt::RightButton);
        QCOMPARE(spy.count(), 0);

        // Connected — right-click SHOULD emit
        s.setRadioName(QStringLiteral("ANAN-G2"));
        QTest::mouseClick(&s, Qt::RightButton);
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TstStationBlock)
#include "tst_station_block.moc"
