// SPDX-License-Identifier: GPL-2.0-or-later
//
// no-port-check: Test references real DXCC entity callsigns as
// fixtures. Precedent: B2-B6, C1-C4, D1-D2.

#include <QtTest>
#include <QSignalSpy>

#include "models/FreeDVStationModel.h"
#include "core/FreeDVStation.h"

using namespace NereusSDR;

class TestFreeDVStationModel : public QObject {
    Q_OBJECT
private slots:
    void initialState();
    void onStationAddedEmits();
    void onStationUpdatedReplaces();
    void onStationRemovedDeletes();
    void clearEmitsCleared();
    void distanceComputedFromGrid();
    void stationBySidLookup();
};

static FreeDVStation makeStation(const QString& sid, const QString& call, const QString& grid) {
    FreeDVStation s;
    s.sid = sid;
    s.callsign = call;
    s.gridSquare = grid;
    return s;
}

void TestFreeDVStationModel::initialState() {
    FreeDVStationModel m;
    QCOMPARE(m.stationCount(), 0);
}

void TestFreeDVStationModel::onStationAddedEmits() {
    FreeDVStationModel m;
    QSignalSpy spy(&m, &FreeDVStationModel::stationAdded);
    m.onStationAdded("sid1", makeStation("sid1", "DL2RD", "JO31"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.stationCount(), 1);
}

void TestFreeDVStationModel::onStationUpdatedReplaces() {
    FreeDVStationModel m;
    m.onStationAdded("sid1", makeStation("sid1", "DL2RD", "JO31"));
    QSignalSpy spy(&m, &FreeDVStationModel::stationUpdated);
    auto updated = makeStation("sid1", "DL2RD", "JO31");
    updated.frequencyHz = 7180000;
    m.onStationUpdated("sid1", updated);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.stationBySid("sid1").frequencyHz, quint64(7180000));
}

void TestFreeDVStationModel::onStationRemovedDeletes() {
    FreeDVStationModel m;
    m.onStationAdded("sid1", makeStation("sid1", "DL2RD", "JO31"));
    QSignalSpy spy(&m, &FreeDVStationModel::stationRemoved);
    m.onStationRemoved("sid1");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.stationCount(), 0);
}

void TestFreeDVStationModel::clearEmitsCleared() {
    FreeDVStationModel m;
    m.onStationAdded("sid1", makeStation("sid1", "DL2RD", "JO31"));
    m.onStationAdded("sid2", makeStation("sid2", "G4XXX", "IO91"));

    QSignalSpy spy(&m, &FreeDVStationModel::cleared);
    m.clear();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m.stationCount(), 0);
}

void TestFreeDVStationModel::distanceComputedFromGrid() {
    FreeDVStationModel m;
    m.setOurGridSquare("FN42");  // Boston-ish

    auto info = makeStation("sid1", "DL2RD", "JO31");  // Frankfurt-ish
    m.onStationAdded("sid1", info);

    auto stored = m.stationBySid("sid1");
    // Boston FN42 to Frankfurt JO31 is roughly 6000-6500 km via great circle
    QVERIFY(stored.distanceKm > 5500);
    QVERIFY(stored.distanceKm < 7000);
    QVERIFY(stored.headingDeg >= 0);
    QVERIFY(stored.headingDeg <= 360);
}

void TestFreeDVStationModel::stationBySidLookup() {
    FreeDVStationModel m;
    m.onStationAdded("sid42", makeStation("sid42", "VK3FOO", "QF22"));
    QCOMPARE(m.stationBySid("sid42").callsign, QStringLiteral("VK3FOO"));
    QCOMPARE(m.stationBySid("nonexistent").callsign, QString());
}

QTEST_GUILESS_MAIN(TestFreeDVStationModel)
#include "tst_freedv_station_model.moc"
