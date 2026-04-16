#include <QtTest/QtTest>
#include "gui/widgets/VfoLevelBar.h"
using namespace NereusSDR;

class TestVfoLevelBar : public QObject {
    Q_OBJECT
private slots:
    void fillFractionAtFloor() {
        VfoLevelBar bar; bar.resize(200, 24);
        bar.setValue(-130.0f);
        QCOMPARE(bar.fillFraction(), 0.0);
    }
    void fillFractionAtCeiling() {
        VfoLevelBar bar; bar.resize(200, 24);
        bar.setValue(-20.0f);
        QCOMPARE(bar.fillFraction(), 1.0);
    }
    void fillFractionAtS9() {
        VfoLevelBar bar; bar.resize(200, 24);
        bar.setValue(-73.0f);  // S9 boundary
        QCOMPARE(bar.fillFraction(), (-73.0 - -130.0) / (-20.0 - -130.0));
    }
    void clampsBelowFloor() {
        VfoLevelBar bar; bar.setValue(-200.0f);
        QCOMPARE(bar.fillFraction(), 0.0);
    }
    void clampsAboveCeiling() {
        VfoLevelBar bar; bar.setValue(10.0f);
        QCOMPARE(bar.fillFraction(), 1.0);
    }
    void colorSwitchesAtS9() {
        VfoLevelBar bar;
        bar.setValue(-74.0f); QCOMPARE(bar.isAboveS9(), false);
        bar.setValue(-73.0f); QCOMPARE(bar.isAboveS9(), true);
    }
};
QTEST_MAIN(TestVfoLevelBar)
#include "tst_vfo_level_bar.moc"
