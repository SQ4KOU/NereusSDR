// tests/tst_level_cal_grid_follow_guard.cpp  (NereusSDR)
// no-port-check: test file, no Thetis attribution required.
//
// Level Cal: while a level calibration runs, the window's grid does not
// follow the noise floor, and afterwards it is put back as it was.
// From Thetis console.cs:9872-9876 and 10230-10231 [v2.10.3.15]
// (CalibrateLevel saves GridMinFollowsNFRX1/RX2, turns them off, and
// restores them at the end).

#include "gui/LevelCalGridFollowGuard.h"
#include "models/RadioModel.h"

#include <QtTest/QtTest>

using NereusSDR::LevelCalGridFollowGuard;
using NereusSDR::RadioModel;

namespace {

struct Grid {
    bool follow = true;
    QList<bool> writes;
};

void attach(LevelCalGridFollowGuard& guard, Grid& grid)
{
    guard.setAccess([&grid]() { return grid.follow; },
                    [&grid](bool on) { grid.follow = on; grid.writes << on; });
}

} // namespace

class TstLevelCalGridFollowGuard : public QObject {
    Q_OBJECT

private slots:
    void offWhileRunningThenRestored()
    {
        RadioModel model(RadioModel::Role::Remote);
        LevelCalGridFollowGuard guard(&model);
        Grid grid;
        attach(guard, grid);
        QVERIFY(model.applyStationLevelCalValue("levelCalRunning", true));
        QCOMPARE(grid.follow, false);
        // Progress while it runs changes nothing more.
        QVERIFY(model.applyStationLevelCalValue("levelCalPercent", 50));
        QCOMPARE(grid.writes, QList<bool>{false});
        QVERIFY(model.applyStationLevelCalValue("levelCalRunning", false));
        QCOMPARE(grid.follow, true);
        QCOMPARE(grid.writes, (QList<bool>{false, true}));
    }

    void offStaysOff()
    {
        RadioModel model(RadioModel::Role::Remote);
        LevelCalGridFollowGuard guard(&model);
        Grid grid;
        grid.follow = false;
        attach(guard, grid);
        QVERIFY(model.applyStationLevelCalValue("levelCalRunning", true));
        QVERIFY(model.applyStationLevelCalValue("levelCalRunning", false));
        QCOMPARE(grid.follow, false);
    }

    void nothingRunsNothingChanges()
    {
        RadioModel model(RadioModel::Role::Remote);
        LevelCalGridFollowGuard guard(&model);
        Grid grid;
        attach(guard, grid);
        QVERIFY(model.applyStationLevelCalValue("levelCalMessage", QStringLiteral("x")));
        QVERIFY(grid.writes.isEmpty());
    }

    // The session ends in the middle of a run: the grid comes back.
    void sessionEndRestores()
    {
        RadioModel model(RadioModel::Role::Remote);
        LevelCalGridFollowGuard guard(&model);
        Grid grid;
        attach(guard, grid);
        QVERIFY(model.applyStationLevelCalValue("levelCalRunning", true));
        model.clearStationLevelCal();
        QCOMPARE(grid.follow, true);
    }
};

QTEST_MAIN(TstLevelCalGridFollowGuard)
#include "tst_level_cal_grid_follow_guard.moc"
