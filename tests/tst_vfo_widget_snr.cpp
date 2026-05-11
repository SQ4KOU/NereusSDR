// SPDX-License-Identifier: GPL-2.0-or-later
//
// NereusSDR - tst_vfo_widget_snr: VfoWidget SNR row contract (Phase 3R L1).
//
// NEW NereusSDR-native UI extension to VfoWidget. No upstream
// equivalent. Pins the contract that VfoWidget surfaces the active
// slice's snrDb Q_PROPERTY (set by RadeChannel via I5 routing) as a
// dim-grey / yellow / green text row. Row visibility tracks the
// slice's current DSPMode: only shown when mode is either RADE
// sideband (DSPMode::RADE_U or DSPMode::RADE_L).
//
// Cases:
//   nanShowsDashesInGrey      - default snrDb=NaN paints " -   - " in dim grey
//   lowSnrShowsYellow         - snrDb=3.0 paints "+3 dB" in yellow (#e6c200)
//   goodSnrShowsGreen         - snrDb=12.0 paints "+12 dB" in green (#4caf50)
//   negativeSnrFormats        - snrDb=-1.0 paints "-1 dB" (no double-sign)
//   snrHiddenInNonRadeMode    - mode=USB hides row; mode=RADE_U shows it.
//   snrVisibleInRadeLower     - mode=RADE_L also shows the row.

#include <QtTest>
#include <QSignalSpy>
#include <QLabel>

#include <cmath>
#include <limits>

#include "gui/widgets/VfoWidget.h"
#include "models/SliceModel.h"
#include "core/WdspTypes.h"

using namespace NereusSDR;

class TestVfoWidgetSnr : public QObject {
    Q_OBJECT
private slots:
    void nanShowsDashesInGrey();
    void lowSnrShowsYellow();
    void goodSnrShowsGreen();
    void negativeSnrFormats();
    void snrHiddenInNonRadeMode();
    void snrVisibleInRadeLower();
};

namespace {

// Helper: build a VfoWidget bound to a SliceModel and put it in RADE_U
// mode.  The widget is constructed parentless so QTest can show() it
// without dragging in MainWindow/SpectrumWidget.
void seedRadeMode(VfoWidget* vfo, SliceModel* slice) {
    slice->setDspMode(DSPMode::RADE_U);
    vfo->setSlice(slice);
    vfo->setMode(DSPMode::RADE_U);
}

} // namespace

void TestVfoWidgetSnr::nanShowsDashesInGrey() {
    SliceModel slice;
    VfoWidget vfo;
    seedRadeMode(&vfo, &slice);

    // Initial slice snrDb is NaN. The value label should show the
    // placeholder " -   - " in dim grey.
    slice.setSnrDb(std::numeric_limits<double>::quiet_NaN());

    QLabel* label = vfo.snrValueLabelForTest();
    QVERIFY(label != nullptr);
    QVERIFY(label->text().contains(QChar('-')));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#7a8088")));
}

void TestVfoWidgetSnr::lowSnrShowsYellow() {
    SliceModel slice;
    VfoWidget vfo;
    seedRadeMode(&vfo, &slice);

    slice.setSnrDb(3.0);

    QLabel* label = vfo.snrValueLabelForTest();
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), QStringLiteral("+3 dB"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#e6c200")));
}

void TestVfoWidgetSnr::goodSnrShowsGreen() {
    SliceModel slice;
    VfoWidget vfo;
    seedRadeMode(&vfo, &slice);

    slice.setSnrDb(12.0);

    QLabel* label = vfo.snrValueLabelForTest();
    QVERIFY(label != nullptr);
    QCOMPARE(label->text(), QStringLiteral("+12 dB"));
    QVERIFY(label->styleSheet().contains(QStringLiteral("#4caf50")));
}

void TestVfoWidgetSnr::negativeSnrFormats() {
    SliceModel slice;
    VfoWidget vfo;
    seedRadeMode(&vfo, &slice);

    slice.setSnrDb(-1.0);

    QLabel* label = vfo.snrValueLabelForTest();
    QVERIFY(label != nullptr);
    // Negative values format with single leading "-" (Qt's %+d handles
    // this naturally; no manual sign concatenation).
    QCOMPARE(label->text(), QStringLiteral("-1 dB"));
    // Negative SNR is below the 5 dB threshold so it stays yellow.
    QVERIFY(label->styleSheet().contains(QStringLiteral("#e6c200")));
}

void TestVfoWidgetSnr::snrHiddenInNonRadeMode() {
    SliceModel slice;
    VfoWidget vfo;

    // Start in USB. Row must be hidden because SNR is only meaningful
    // for the RADE neural codec.
    slice.setDspMode(DSPMode::USB);
    vfo.setSlice(&slice);
    vfo.setMode(DSPMode::USB);

    QLabel* valueLabel = vfo.snrValueLabelForTest();
    QLabel* nameLabel  = vfo.snrLabelForTest();
    QVERIFY(valueLabel != nullptr);
    QVERIFY(nameLabel  != nullptr);
    QVERIFY(!valueLabel->isVisibleTo(&vfo));
    QVERIFY(!nameLabel->isVisibleTo(&vfo));

    // Switch the slice to RADE_U: row becomes visible.
    slice.setDspMode(DSPMode::RADE_U);
    vfo.setMode(DSPMode::RADE_U);
    QVERIFY(valueLabel->isVisibleTo(&vfo));
    QVERIFY(nameLabel->isVisibleTo(&vfo));
}

void TestVfoWidgetSnr::snrVisibleInRadeLower() {
    SliceModel slice;
    VfoWidget vfo;

    // RADE_L (lower sideband) also reveals the SNR row.  Both RADE
    // sidebands share the same SNR semantics; the neural codec
    // produces the same SNR estimate regardless of which sideband
    // it serves.
    slice.setDspMode(DSPMode::RADE_L);
    vfo.setSlice(&slice);
    vfo.setMode(DSPMode::RADE_L);

    QLabel* valueLabel = vfo.snrValueLabelForTest();
    QLabel* nameLabel  = vfo.snrLabelForTest();
    QVERIFY(valueLabel != nullptr);
    QVERIFY(nameLabel  != nullptr);
    QVERIFY(valueLabel->isVisibleTo(&vfo));
    QVERIFY(nameLabel->isVisibleTo(&vfo));
}

QTEST_MAIN(TestVfoWidgetSnr)
#include "tst_vfo_widget_snr.moc"
