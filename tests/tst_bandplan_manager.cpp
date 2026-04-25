// SPDX-License-Identifier: GPL-3.0-or-later

#include <QtTest>
#include <QSignalSpy>

#include "models/BandPlan.h"
#include "models/BandPlanManager.h"

using namespace NereusSDR;

class TestBandPlanManager : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Value types
    void segment_defaultIsEmpty();
    void spot_defaultIsEmpty();

    // Loader: bundled resources
    void loadPlans_findsAllFiveRegions();
    void loadPlans_arrlUsHasSegmentsAndSpots();
    void loadPlans_iaruRegion2HasSegments();

    // Active plan
    void setActivePlan_emitsPlanChanged();
    void setActivePlan_unknownNameDoesNothing();
    void setActivePlan_segmentsReflectActivePlan();

    // Default selection
    void loadPlans_defaultsToArrlUs();
};

void TestBandPlanManager::initTestCase()
{
    // Resource init runs at static-init time; nothing to do here.
}

void TestBandPlanManager::segment_defaultIsEmpty()
{
    BandSegment s;
    QCOMPARE(s.lowMhz, 0.0);
    QCOMPARE(s.highMhz, 0.0);
    QVERIFY(s.label.isEmpty());
    QVERIFY(!s.color.isValid());
}

void TestBandPlanManager::spot_defaultIsEmpty()
{
    BandSpot s;
    QCOMPARE(s.freqMhz, 0.0);
    QVERIFY(s.label.isEmpty());
}

void TestBandPlanManager::loadPlans_findsAllFiveRegions()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    const QStringList names = mgr.availablePlans();
    QCOMPARE(names.size(), 5);
    QVERIFY(names.contains("ARRL (US)"));
    QVERIFY(names.contains("IARU Region 1"));
    QVERIFY(names.contains("IARU Region 2"));
    QVERIFY(names.contains("IARU Region 3"));
    QVERIFY(names.contains("RAC (Canada)"));
}

void TestBandPlanManager::loadPlans_arrlUsHasSegmentsAndSpots()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("ARRL (US)");
    QVERIFY(mgr.segments().size() > 50);   // arrl-us.json has ~70 segments
    QVERIFY(mgr.spots().size()    > 50);   // arrl-us.json has ~80+ spots

    // 20m CW segment must exist (sanity-check round-trip of low/high/label/color)
    bool found20mCw = false;
    for (const auto& seg : mgr.segments()) {
        if (qFuzzyCompare(seg.lowMhz, 14.025) && seg.label == "CW" && seg.color.isValid()) {
            found20mCw = true;
            break;
        }
    }
    QVERIFY(found20mCw);
}

void TestBandPlanManager::loadPlans_iaruRegion2HasSegments()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("IARU Region 2");
    QVERIFY(mgr.segments().size() > 10);
}

void TestBandPlanManager::setActivePlan_emitsPlanChanged()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    QSignalSpy spy(&mgr, &BandPlanManager::planChanged);
    mgr.setActivePlan("IARU Region 1");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mgr.activePlanName(), QString("IARU Region 1"));
}

void TestBandPlanManager::setActivePlan_unknownNameDoesNothing()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    const QString before = mgr.activePlanName();
    QSignalSpy spy(&mgr, &BandPlanManager::planChanged);
    mgr.setActivePlan("Klingon Empire Bandplan");
    QCOMPARE(spy.count(), 0);
    QCOMPARE(mgr.activePlanName(), before);
}

void TestBandPlanManager::setActivePlan_segmentsReflectActivePlan()
{
    BandPlanManager mgr;
    mgr.loadPlans();
    mgr.setActivePlan("ARRL (US)");
    const int arrlCount = mgr.segments().size();
    mgr.setActivePlan("IARU Region 1");
    const int r1Count = mgr.segments().size();
    QVERIFY(arrlCount != r1Count);   // different region = different segment count
}

void TestBandPlanManager::loadPlans_defaultsToArrlUs()
{
    // First-launch default (no AppSettings key yet) is "ARRL (US)".
    // We can't easily reset AppSettings in a unit test, but we can verify
    // the ctor + loadPlans() leaves a non-empty active plan and that the
    // public API returns segments without a setActivePlan() call.
    BandPlanManager mgr;
    mgr.loadPlans();
    QVERIFY(!mgr.activePlanName().isEmpty());
    QVERIFY(!mgr.segments().isEmpty());
}

QTEST_MAIN(TestBandPlanManager)
#include "tst_bandplan_manager.moc"
