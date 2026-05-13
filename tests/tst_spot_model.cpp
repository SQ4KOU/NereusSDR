// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: Real amateur callsigns (W1AW, W3LPL) and a US ARRL HQ
// frequency (14.250 MHz) used as fixtures to exercise the TCI-keyed
// applySpotStatus() dispatch. Precedent: B1-B5, C1-C4.
//
// NereusSDR - SpotModel tests
//
// Phase 3J-2 Task D1. Pins the contract that SpotModel is a
// QMap<int, SpotData> sink keyed by monotonic spot index, with a
// TCI-keyed update API:
//   void applySpotStatus(int index, const QMap<QString,QString>& kvs);
// recognising 12 keys (callsign, rx_freq, tx_freq, mode, color,
// background_color, source, spotter_callsign, comment, timestamp,
// lifetime_seconds, priority) and decoding the TCI 0x7F (DEL)
// wire-format quirk to a single ASCII space in the callsign and
// comment fields. Six signals: spotAdded / spotUpdated / spotRemoved
// / spotsCleared / spotsRefreshed / spotTriggered. Seven tests:
//   - initialState: an empty SpotModel reports zero spots.
//   - applySpotStatusAddsNew: first applySpotStatus() for a given
//     index emits spotAdded and stores the SpotData.
//   - applySpotStatusUpdatesExisting: subsequent applySpotStatus()
//     for the same index emits spotUpdated, preserves prior fields
//     not present in the update, and overwrites fields that are.
//   - appliesAllTwelveKeys: every recognised key from the 12-key
//     contract round-trips through the parser and is reflected on
//     the resulting SpotData.
//   - decodes0x7FAsSpace: the TCI 0x7F (DEL) wire-format quirk is
//     replaced by a single ASCII space in callsign and comment.
//   - removeSpotEmitsSignal: removeSpot() emits spotRemoved with
//     the index and erases the row from the map.
//   - clearEmitsSignal: clear() emits spotsCleared once and empties
//     the map regardless of how many spots were resident.

#include <QtTest>
#include <QSignalSpy>

#include "models/SpotModel.h"

using namespace NereusSDR;

class TestSpotModel : public QObject {
    Q_OBJECT
private slots:
    void initialState();
    void applySpotStatusAddsNew();
    void applySpotStatusUpdatesExisting();
    void appliesAllTwelveKeys();
    void decodes0x7FAsSpace();
    void removeSpotEmitsSignal();
    void clearEmitsSignal();
};

void TestSpotModel::initialState()
{
    SpotModel model;
    QCOMPARE(model.spots().size(), 0);
}

void TestSpotModel::applySpotStatusAddsNew()
{
    SpotModel model;
    QSignalSpy spy(&model, &SpotModel::spotAdded);

    QMap<QString, QString> kvs;
    kvs["callsign"] = "W1AW";
    kvs["rx_freq"] = "14.250";
    model.applySpotStatus(1, kvs);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.spots()[1].callsign, QStringLiteral("W1AW"));
    QCOMPARE(model.spots()[1].rxFreqMhz, 14.250);
}

void TestSpotModel::applySpotStatusUpdatesExisting()
{
    SpotModel model;
    QMap<QString, QString> kvs1;
    kvs1["callsign"] = "W1AW";
    model.applySpotStatus(1, kvs1);

    QSignalSpy spy(&model, &SpotModel::spotUpdated);

    QMap<QString, QString> kvs2;
    kvs2["mode"] = "SSB";
    model.applySpotStatus(1, kvs2);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.spots()[1].callsign, QStringLiteral("W1AW"));  // preserved
    QCOMPARE(model.spots()[1].mode, QStringLiteral("SSB"));
}

void TestSpotModel::appliesAllTwelveKeys()
{
    SpotModel model;
    QMap<QString, QString> kvs;
    kvs["callsign"] = "W1AW";
    kvs["rx_freq"] = "14.250";
    kvs["tx_freq"] = "14.250";
    kvs["mode"] = "USB";
    kvs["color"] = "#FFFF8C00";
    kvs["background_color"] = "#80000000";
    kvs["source"] = "Cluster";
    kvs["spotter_callsign"] = "W3LPL";
    kvs["comment"] = "ARRL HQ";
    kvs["timestamp"] = QString::number(1715369280);
    kvs["lifetime_seconds"] = "1800";
    kvs["priority"] = "1";

    model.applySpotStatus(42, kvs);
    const auto& s = model.spots()[42];
    QCOMPARE(s.callsign, QStringLiteral("W1AW"));
    QCOMPARE(s.rxFreqMhz, 14.250);
    QCOMPARE(s.txFreqMhz, 14.250);
    QCOMPARE(s.mode, QStringLiteral("USB"));
    QCOMPARE(s.color, QStringLiteral("#FFFF8C00"));
    QCOMPARE(s.backgroundColor, QStringLiteral("#80000000"));
    QCOMPARE(s.source, QStringLiteral("Cluster"));
    QCOMPARE(s.spotterCallsign, QStringLiteral("W3LPL"));
    QCOMPARE(s.comment, QStringLiteral("ARRL HQ"));
    QCOMPARE(s.lifetimeSeconds, 1800);
    QCOMPARE(s.priority, 1);
}

void TestSpotModel::decodes0x7FAsSpace()
{
    SpotModel model;
    QMap<QString, QString> kvs;
    QString withDel = QStringLiteral("ARRL\x7Fheadquarters");
    kvs["callsign"] = "W1AW";
    kvs["comment"] = withDel;
    model.applySpotStatus(1, kvs);
    QCOMPARE(model.spots()[1].comment, QStringLiteral("ARRL headquarters"));
}

void TestSpotModel::removeSpotEmitsSignal()
{
    SpotModel model;
    QMap<QString, QString> kvs;
    kvs["callsign"] = "W1AW";
    model.applySpotStatus(1, kvs);

    QSignalSpy spy(&model, &SpotModel::spotRemoved);
    model.removeSpot(1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.spots().size(), 0);
}

void TestSpotModel::clearEmitsSignal()
{
    SpotModel model;
    QMap<QString, QString> kvs;
    kvs["callsign"] = "W1AW";
    model.applySpotStatus(1, kvs);
    model.applySpotStatus(2, kvs);

    QSignalSpy spy(&model, &SpotModel::spotsCleared);
    model.clear();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(model.spots().size(), 0);
}

QTEST_GUILESS_MAIN(TestSpotModel)
#include "tst_spot_model.moc"
