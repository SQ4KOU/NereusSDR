// tst_radio_model_pa_tripped.cpp
//
// no-port-check: Test file exercises NereusSDR API; Thetis behavior is
// cited in RadioModel.cpp — no C# is translated here.
//
// Unit tests for RadioModel::paTripped() live state and the Ganymede PA
// trip handler. Exercises:
//   - initial_paTripped_isFalse     — default state is clear
//   - ganymedeTripMessage_setsPaTripped  — non-zero tripState latches trip
//   - ganymedeReset_clearsPaTripped — resetGanymedePa() clears the latch
//   - ganymedeAbsent_clearsPaTripped— setGanymedePresent(false) clears latch
//   - ganymedeTripMessage_dropsMoxIfActive — safety MOX drop on trip latch
//                                         per Andromeda.cs:920 [v2.10.3.13]
//
// From Thetis Andromeda/Andromeda.cs:914-948 [v2.10.3.13]
// (CATHandleAmplifierTripMessage + GanymedeResetPressed).
// G8NJJ: handlers for Ganymede 500W PA protection.

#include <QSignalSpy>
#include <QtTest/QtTest>

#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

class TestRadioModelPaTripped : public QObject {
    Q_OBJECT

private slots:
    void initial_paTripped_isFalse();
    void ganymedeTripMessage_setsPaTripped();
    void ganymedeReset_clearsPaTripped();
    void ganymedeAbsent_clearsPaTripped();
    void ganymedeTripMessage_dropsMoxIfActive();
};

// ── Test implementations ─────────────────────────────────────────────────────

void TestRadioModelPaTripped::initial_paTripped_isFalse()
{
    RadioModel model;
    QVERIFY(!model.paTripped());
}

void TestRadioModelPaTripped::ganymedeTripMessage_setsPaTripped()
{
    RadioModel model;
    QSignalSpy spy(&model, &RadioModel::paTrippedChanged);

    model.handleGanymedeTrip(0x01);

    QVERIFY(model.paTripped());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), true);
}

void TestRadioModelPaTripped::ganymedeReset_clearsPaTripped()
{
    RadioModel model;
    model.handleGanymedeTrip(0x01);
    QVERIFY(model.paTripped());

    QSignalSpy spy(&model, &RadioModel::paTrippedChanged);
    model.resetGanymedePa();

    QVERIFY(!model.paTripped());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestRadioModelPaTripped::ganymedeAbsent_clearsPaTripped()
{
    RadioModel model;
    model.handleGanymedeTrip(0x08); // heatsink trip
    QVERIFY(model.paTripped());

    QSignalSpy spy(&model, &RadioModel::paTrippedChanged);
    model.setGanymedePresent(false);

    QVERIFY(!model.paTripped());
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toBool(), false);
}

void TestRadioModelPaTripped::ganymedeTripMessage_dropsMoxIfActive()
{
    // Safety side-effect from Andromeda.cs:920 [v2.10.3.13]:
    //   if (_ganymede_pa_issue && MOX) MOX = false; //if there is a fault, undo mox if active
    // G8NJJ: handlers for Ganymede 500W PA protection
    RadioModel model;
    model.transmitModel().setMox(true);
    QVERIFY(model.transmitModel().isMox());

    model.handleGanymedeTrip(0x01);
    QVERIFY(model.paTripped());
    QVERIFY(!model.transmitModel().isMox()); // safety drop per Andromeda.cs:920
}

QTEST_MAIN(TestRadioModelPaTripped)
#include "tst_radio_model_pa_tripped.moc"
