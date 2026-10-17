// =================================================================
// tests/tst_overlay_flyout_above_flags.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// An open overlay flyout stays above the pan's flags.
//
// Bench-reported 2026-10-11: with the Display pill open and the VFO near the
// pan's left edge, the slice flag drew over the right third of the pill and
// took the clicks meant for it, so nothing in the pill could be adjusted.
//
// The flyouts (the Display pill, BAND, ANT, VAX, ATT) and the flags are
// siblings under the SpectrumWidget. A flyout raised itself once, when it
// opened. SpectrumWidget::updateVfoPositions() raises every visible flag and
// its floating buttons on every placement pass, so the first pass after the
// flyout opened put the flag back over it. The pan now says when it has
// raised its flags (SpectrumWidget::flagsRaised) and the strip puts its open
// flyout back on top (SpectrumOverlayPanel::raiseOpenFlyout).
//
// Qt keeps sibling stacking in the parent's QObject::children() order (later
// is higher); raise() moves the entry. QWidget::childAt() and
// QApplication::widgetAt() walk that order, and a click sent to the pan's
// window is routed through it, so all of this is checked without a native
// window. What a native view tree does with the same raises is
// tst_overlay_flyout_above_flags_native's part.
//
// 2026-10-11 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//               Claude Code.
// =================================================================
#include <QtTest/QtTest>

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QList>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QWindow>

#include <functional>

#include "core/AppSettings.h"
#include "core/ControlRanges.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"
#include "gui/widgets/VfoWidget.h"

using namespace NereusSDR;

namespace {

constexpr double kCentreHz = 14'200'000.0;
constexpr double kSpanHz   =    192'000.0;
// An eighth of the way in from the pan's left edge: the flag has no room on
// the VFO's left, flips to its right, and lands across the flyouts, which
// open just right of the strip. The bench report's layout.
constexpr double kNearLeftHz = kCentreHz - kSpanHz / 2.0 + kSpanHz / 8.0;
constexpr double kSecondHz   = kNearLeftHz + 4'000.0;

using Tab = SpectrumOverlayPanel::DisplayPillTab;

// A direct child's place in its parent's stacking order; higher is in front.
int stackIndex(QWidget* w)
{
    QWidget* parent = w ? w->parentWidget() : nullptr;
    return parent ? int(parent->children().indexOf(w)) : -1;
}

bool isInside(const QWidget* w, const QWidget* ancestor)
{
    return w && ancestor && (w == ancestor || ancestor->isAncestorOf(w));
}

struct Harness {
    SpectrumWidget pan;
    SpectrumOverlayPanel* panel{nullptr};
    VfoWidget* flag{nullptr};

    explicit Harness(int height = 760)
    {
        // Nothing here needs a frame drawn; stacking and hit-testing do not
        // depend on one.
        pan.setUpdatesEnabled(false);
        pan.resize(1200, height);
        pan.setSampleRate(kSpanHz);
        pan.setDdcCenterFrequency(kCentreHz);
        pan.setFrequencyRange(kCentreHz, kSpanHz);
        panel = new SpectrumOverlayPanel(&pan);
        panel->move(4, 4);      // as MainWindow places it
        panel->show();
        flag = pan.addVfoWidget(0);
        flag->setFrequency(kNearLeftHz);
        pan.show();
        pan.updateVfoPositions();
    }

    QWidget* pill() { return pan.findChild<QWidget*>(QStringLiteral("displayPill")); }

    // The flag's close, lock, record and play buttons, and any other button
    // parented straight to the pan.
    QList<QPushButton*> floatingButtons()
    {
        return pan.findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
    }

    QWidget* zoomStrip()
    {
        const QList<QPushButton*> buttons = pan.findChildren<QPushButton*>();
        for (QPushButton* button : buttons) {
            if (button->toolTip().startsWith(QStringLiteral("Segment zoom"))) {
                return button->parentWidget();
            }
        }
        return nullptr;
    }

    QPushButton* stripButton(const QString& text)
    {
        const QList<QPushButton*> buttons =
            panel->findChildren<QPushButton*>(QString(), Qt::FindDirectChildrenOnly);
        for (QPushButton* button : buttons) {
            if (button->text() == text) {
                return button;
            }
        }
        return nullptr;
    }

    // The pan's direct children that are shown, flags and strip aside: the
    // open flyout is the one a strip button adds to this list.
    QList<QWidget*> shownSiblings()
    {
        QList<QWidget*> shown;
        const QList<QWidget*> children =
            pan.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (!child->isHidden()) {
                shown.append(child);
            }
        }
        return shown;
    }

    // A point, in the pan, on a control of `flyout` that the flag covers.
    // Null when the flag covers none.
    QPoint coveredControlPoint(QWidget* flyout, QAbstractButton** control = nullptr)
    {
        const QList<QAbstractButton*> buttons = flyout->findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons) {
            if (!button->isVisibleTo(flyout) || !button->isEnabled()) {
                continue;
            }
            const QPoint centre = button->mapTo(&pan, button->rect().center());
            if (flag->geometry().contains(centre)) {
                if (control) {
                    *control = button;
                }
                return centre;
            }
        }
        return QPoint();
    }

    bool flyoutIsAboveEveryFlagPart(QWidget* flyout)
    {
        if (stackIndex(flyout) < stackIndex(flag)) {
            return false;
        }
        const QList<QPushButton*> buttons = floatingButtons();
        for (QPushButton* button : buttons) {
            if (stackIndex(flyout) < stackIndex(button)) {
                return false;
            }
        }
        return true;
    }
};

} // namespace

class TstOverlayFlyoutAboveFlags : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void init() { AppSettings::instance().clear(); }

    // The bench report's layout, so the cases below test the real overlap.
    void theFlagLandsAcrossTheOpenPill()
    {
        Harness h;
        h.panel->openDisplayPill();
        QVERIFY(h.pill() && !h.pill()->isHidden());
        QVERIFY2(h.flag->geometry().intersects(h.pill()->geometry()),
                 "the flag does not overlap the pill, so nothing below tests the report");
        QAbstractButton* covered = nullptr;
        QVERIFY(!h.coveredControlPoint(h.pill(), &covered).isNull());
        QVERIFY(covered != nullptr);
    }

    void thePillStaysAboveTheFlagThroughWhateverRaisesTheFlags_data()
    {
        QTest::addColumn<int>("step");
        QTest::newRow("a placement pass") << 0;
        QTest::newRow("a tune (setVfoFrequency)") << 1;
        QTest::newRow("a new view window (setFrequencyRange)") << 2;
        QTest::newRow("a resize of the pan") << 3;
        QTest::newRow("refreshSliceFlags") << 4;
        QTest::newRow("a new front flag") << 5;
        QTest::newRow("a second flag added") << 6;
    }

    void thePillStaysAboveTheFlagThroughWhateverRaisesTheFlags()
    {
        QFETCH(int, step);
        Harness h;
        h.panel->openDisplayPill();
        QWidget* pill = h.pill();
        const QPoint point = h.coveredControlPoint(pill);
        QVERIFY(!point.isNull());

        // Opening raised the pill, so it starts on top.
        QVERIFY(h.flyoutIsAboveEveryFlagPart(pill));
        QVERIFY(isInside(h.pan.childAt(point), pill));

        QSignalSpy raised(&h.pan, &SpectrumWidget::flagsRaised);
        switch (step) {
        case 0: h.pan.updateVfoPositions(); break;
        case 1: h.pan.setVfoFrequency(kNearLeftHz + 10.0); break;
        case 2: h.pan.setFrequencyRange(kCentreHz + 100.0, kSpanHz); break;
        case 3: h.pan.resize(h.pan.width() + 20, h.pan.height()); break;
        case 4: h.pan.refreshSliceFlags(); break;
        case 5: h.pan.setFrontSliceIndex(0); break;
        case 6: {
            VfoWidget* second = h.pan.addVfoWidget(1);
            second->setFrequency(kSecondHz);
            break;
        }
        default: QFAIL("unknown step");
        }
        QCoreApplication::processEvents();
        QVERIFY2(raised.count() >= 1, "the step raised no flag, so it tested nothing");

        QVERIFY2(h.flyoutIsAboveEveryFlagPart(pill),
                 "the flag or one of its buttons is stacked above the open pill");
        QVERIFY2(isInside(h.pan.childAt(point), pill),
                 "the point on the pill's control now belongs to the flag");
        QVERIFY(isInside(QApplication::widgetAt(h.pan.mapToGlobal(point)), pill));
    }

    // A real control of the pill, moved the way an operator moves it. In a
    // GPU build the waterfall Low slider only repaints the pan and starts no
    // placement pass itself; the pass comes from whatever else is running
    // while the operator is in the pill (a tune, a new view from the Core, a
    // resize), and it must leave the pill on top.
    void movingAPillControlLeavesThePillOnTop()
    {
        Harness h;
        h.panel->openDisplayPill();
        h.panel->setDisplayPillTab(Tab::Levels);
        QWidget* pill = h.pill();
        auto* low = pill->findChild<QSlider*>(QStringLiteral("pillDisplayWfLowLevel"));
        QVERIFY(low != nullptr);
        // The approved Levels chooser owns Low; AGC off alone still leaves
        // the default requested Clarity mode in charge.
        auto* manual = pill->findChild<QPushButton*>(QStringLiteral("pillWaterfallLevelsManual"));
        QVERIFY(manual && manual->isVisibleTo(pill));
        QTest::mouseClick(manual, Qt::LeftButton);
        QCoreApplication::processEvents();
        QVERIFY(low->isEnabled());

        const float before = h.pan.wfLowThreshold();
        low->setFocus();
        QTest::keyClick(low, Qt::Key_Right);
        QTest::keyClick(low, Qt::Key_Right);
        QCoreApplication::processEvents();
        QVERIFY(!qFuzzyCompare(h.pan.wfLowThreshold(), before));

        // The next pass, whatever starts it.
        h.pan.updateVfoPositions();
        QCoreApplication::processEvents();
        QVERIFY2(h.flyoutIsAboveEveryFlagPart(pill),
                 "after a pill control moved, the flag is stacked above the open pill");
        const QPoint point = h.coveredControlPoint(pill);
        QVERIFY(!point.isNull());
        QVERIFY(isInside(h.pan.childAt(point), pill));
    }

    // A click sent to the pan's window at a point the flag covers is routed
    // by Qt's own hit-testing. It must reach the pill's control and leave
    // the pill open (a click on the flag is an outside click, which closes
    // it).
    void aClickWhereTheFlagOverlapsReachesThePillsControl()
    {
        Harness h;
        h.panel->openDisplayPill();
        h.panel->setDisplayPillTab(Tab::Spectrum);
        QWidget* pill = h.pill();
        QVERIFY(QTest::qWaitForWindowExposed(&h.pan));

        // The tab buttons run across the pill's top, under the flag.
        QAbstractButton* covered = nullptr;
        QPoint point;
        const QList<QAbstractButton*> buttons = pill->findChildren<QAbstractButton*>();
        for (QAbstractButton* button : buttons) {
            const QPoint centre = button->mapTo(&h.pan, button->rect().center());
            if (button->objectName().startsWith(QStringLiteral("displayTab"))
                && !button->isChecked() && h.flag->geometry().contains(centre)) {
                covered = button;
                point = centre;
            }
        }
        QVERIFY2(covered != nullptr, "no unselected tab button lies under the flag");
        const Tab tabBefore = h.panel->displayPillTab();

        h.pan.updateVfoPositions();
        QCoreApplication::processEvents();

        QVERIFY(h.pan.windowHandle() != nullptr);
        QTest::mouseClick(h.pan.windowHandle(), Qt::LeftButton, Qt::NoModifier, point);
        QCoreApplication::processEvents();

        QVERIFY2(h.panel->displayPillVisible(),
                 "the click went to the flag and closed the pill as an outside click");
        QVERIFY2(covered->isChecked(), "the click did not reach the pill's tab button");
        QVERIFY(h.panel->displayPillTab() != tabBefore);

        // And after one more pass, the next click still lands.
        h.pan.updateVfoPositions();
        QAbstractButton* back = pill->findChild<QAbstractButton*>(
            QStringLiteral("displayTabSpectrum"));
        QVERIFY(back != nullptr);
        const QPoint backPoint = back->mapTo(&h.pan, back->rect().center());
        QTest::mouseClick(h.pan.windowHandle(), Qt::LeftButton, Qt::NoModifier, backPoint);
        QCoreApplication::processEvents();
        QVERIFY(h.panel->displayPillVisible());
        QCOMPARE(h.panel->displayPillTab(), Tab::Spectrum);
    }

    void everyFlyoutStaysAboveTheFlag_data()
    {
        QTest::addColumn<QString>("button");
        QTest::newRow("BAND") << QStringLiteral("BAND");
        QTest::newRow("ANT") << QStringLiteral("ANT");
        QTest::newRow("Display") << QStringLiteral("Display");
        QTest::newRow("VAX") << QStringLiteral("VAX");
        QTest::newRow("ATT") << QStringLiteral("ATT");
    }

    void everyFlyoutStaysAboveTheFlag()
    {
        QFETCH(QString, button);
        Harness h;
        QPushButton* opener = h.stripButton(button);
        QVERIFY2(opener != nullptr, "the strip has no such button");

        const QList<QWidget*> before = h.shownSiblings();
        opener->click();
        QWidget* flyout = nullptr;
        const QList<QWidget*> after = h.shownSiblings();
        for (QWidget* shown : after) {
            if (!before.contains(shown)) {
                flyout = shown;
            }
        }
        QVERIFY2(flyout != nullptr, "the button opened no flyout");
        QVERIFY(h.flyoutIsAboveEveryFlagPart(flyout));

        h.pan.updateVfoPositions();
        QVERIFY2(h.flyoutIsAboveEveryFlagPart(flyout),
                 "after a placement pass the flag is stacked above the open flyout");

        const QRect overlap = flyout->geometry().intersected(h.flag->geometry());
        if (!overlap.isEmpty()) {
            QVERIFY(isInside(h.pan.childAt(overlap.center()), flyout));
        }
    }

    // Among the flags the order is the pass's own: the front flag last.
    void theFlagsKeepTheirOwnOrderUnderAnOpenPill()
    {
        Harness h;
        VfoWidget* second = h.pan.addVfoWidget(1);
        second->setFrequency(kSecondHz);
        h.pan.setFrontSliceIndex(0);
        h.panel->openDisplayPill();

        h.pan.updateVfoPositions();
        QVERIFY(stackIndex(h.flag) > stackIndex(second));
        QVERIFY(stackIndex(h.pill()) > stackIndex(h.flag));

        h.pan.setFrontSliceIndex(1);
        h.pan.updateVfoPositions();
        QVERIFY(stackIndex(second) > stackIndex(h.flag));
        QVERIFY(stackIndex(h.pill()) > stackIndex(second));
    }

    // With no flyout open a pass leaves the pan as it always did: the front
    // flag is the top sibling and no flyout moved.
    void withNoFlyoutOpenAPassMovesNoFlyout()
    {
        Harness h;
        VfoWidget* second = h.pan.addVfoWidget(1);
        second->setFrequency(kSecondHz);
        h.pan.setFrontSliceIndex(0);
        h.pan.updateVfoPositions();

        QWidget* pill = h.pill();
        QVERIFY(pill->isHidden());
        QList<QWidget*> hidden;
        const QList<QWidget*> children =
            h.pan.findChildren<QWidget*>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget* child : children) {
            if (child->isHidden() && !h.floatingButtons().contains(child)) {
                hidden.append(child);
            }
        }
        QVERIFY(hidden.contains(pill));
        // Lower index is further back; a raise would send one to the end.
        const auto order = [&]() {
            QList<QWidget*> sorted = hidden;
            std::sort(sorted.begin(), sorted.end(), [](QWidget* a, QWidget* b) {
                return stackIndex(a) < stackIndex(b);
            });
            return sorted;
        };
        const QList<QWidget*> orderBefore = order();

        h.pan.updateVfoPositions();
        QCOMPARE(h.pan.children().constLast(), static_cast<QObject*>(h.flag));
        QCOMPARE(order(), orderBefore);
        QVERIFY(stackIndex(pill) < stackIndex(h.flag));

        // A flyout that was open and is closed again is left alone too.
        h.panel->openDisplayPill();
        h.stripButton(QStringLiteral("Display"))->click();
        QVERIFY(!h.panel->displayPillVisible());
        h.pan.updateVfoPositions();
        QCOMPARE(h.pan.children().constLast(), static_cast<QObject*>(h.flag));
    }

    // Raising the flyout is the end of it: it starts no placement pass of
    // its own, in the call or once the event loop runs.
    void raisingTheFlyoutStartsNoPlacementPass()
    {
        Harness h;
        h.panel->openDisplayPill();
        QCoreApplication::processEvents();

        QSignalSpy raised(&h.pan, &SpectrumWidget::flagsRaised);
        h.pan.updateVfoPositions();
        QCOMPARE(raised.count(), 1);
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
        QCOMPARE(raised.count(), 1);

        // The slot on its own: no pass, and nothing to do once on top.
        h.panel->raiseOpenFlyout();
        h.panel->raiseOpenFlyout();
        QCoreApplication::processEvents();
        QCOMPARE(raised.count(), 1);
    }

    // A flag raises its close / lock / record / play buttons outside a
    // placement pass too: when it is shown again, when the station
    // presentation gate opens, and when its access line is refreshed. Each
    // put a 20 px column of buttons over the open flyout until the next
    // pass. The flag now says so and the pan passes it on.
    void thePillStaysAboveTheButtonsAFlagRaisesOutsideAPass_data()
    {
        QTest::addColumn<int>("path");
        QTest::newRow("the flag is shown again") << 0;
        QTest::newRow("station presentation is allowed again") << 1;
        QTest::newRow("the slice access changes") << 2;
        QTest::newRow("the pending access text changes") << 3;
    }

    void thePillStaysAboveTheButtonsAFlagRaisesOutsideAPass()
    {
        QFETCH(int, path);
        Harness h;
        h.panel->openDisplayPill();
        QWidget* pill = h.pill();
        QVERIFY(h.flyoutIsAboveEveryFlagPart(pill));

        // Set the path up, then open the count: only the raise is measured.
        if (path == 0) {
            h.flag->hide();
        } else if (path == 1) {
            // A flag that is shown while it may not be presented has its
            // buttons hidden; allowing it shows and raises them.
            h.flag->setStationPresentationAllowed(false);
            h.flag->show();
        }
        QVERIFY(h.flyoutIsAboveEveryFlagPart(pill));

        QSignalSpy raised(&h.pan, &SpectrumWidget::flagsRaised);
        QSignalSpy buttons(h.flag, &VfoWidget::floatingButtonsRaised);
        switch (path) {
        case 0: h.flag->show(); break;
        case 1: h.flag->setStationPresentationAllowed(true); break;
        case 2: {
            VfoWidget::SliceAccess access;
            access.state = VfoWidget::SliceAccess::State::Listening;
            h.flag->setSliceAccess(access);
            break;
        }
        case 3: h.flag->setSliceAccessPending(QStringLiteral("Asking the station")); break;
        default: QFAIL("unknown path");
        }
        QVERIFY2(buttons.count() >= 1, "the path raised no button, so it tested nothing");
        int shownButtons = 0;
        const QList<QPushButton*> floating = h.floatingButtons();
        for (QPushButton* button : floating) {
            shownButtons += button->isHidden() ? 0 : 1;
        }
        QVERIFY(shownButtons >= 1);

        QVERIFY2(h.flyoutIsAboveEveryFlagPart(pill),
                 "a floating button raised outside a pass is stacked above the open pill");
        // Once per raise of the column, not once per button, and no pass
        // follows it, in the call or once the event loop runs.
        QCOMPARE(raised.count(), buttons.count());
        const int count = raised.count();
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
        QCOMPARE(raised.count(), count);
        QVERIFY(h.flyoutIsAboveEveryFlagPart(pill));
    }

    // Inside a placement pass the flags raise their buttons too (and a flag
    // the pass shows again raises them twice). The pass still says
    // flagsRaised once, at its end.
    void aPassThatShowsAFlagAgainStillSaysFlagsRaisedOnce()
    {
        Harness h;
        VfoWidget* second = h.pan.addVfoWidget(1);
        second->setFrequency(kSecondHz);
        h.pan.updateVfoPositions();
        h.panel->openDisplayPill();

        h.flag->hide();
        second->hide();
        QSignalSpy raised(&h.pan, &SpectrumWidget::flagsRaised);
        QSignalSpy buttons(h.flag, &VfoWidget::floatingButtonsRaised);
        h.pan.updateVfoPositions();
        QVERIFY(!h.flag->isHidden());
        QVERIFY2(buttons.count() >= 2, "the pass did not show the flag again");
        QCOMPARE(raised.count(), 1);
        for (int i = 0; i < 5; ++i) {
            QCoreApplication::processEvents();
        }
        QCOMPARE(raised.count(), 1);
        QVERIFY(h.flyoutIsAboveEveryFlagPart(h.pill()));
    }

    // With no flyout open, a flag raising its buttons moves no flyout.
    void withNoFlyoutOpenAFlagRaisingItsButtonsMovesNoFlyout()
    {
        Harness h;
        QWidget* pill = h.pill();
        QVERIFY(pill->isHidden());
        const int pillIndex = stackIndex(pill);

        QSignalSpy buttons(h.flag, &VfoWidget::floatingButtonsRaised);
        h.flag->hide();
        h.flag->show();
        QVERIFY(buttons.count() >= 1);
        QVERIFY(stackIndex(pill) <= pillIndex);
        QVERIFY(stackIndex(pill) < stackIndex(h.flag));
        QVERIFY(!h.flyoutIsAboveEveryFlagPart(pill));
    }

    // The zoom strip raises itself whenever it is placed (a resize, or the
    // pill's own scale text size row). On a short pan the pill reaches down
    // to it.
    void theZoomStripDoesNotCoverAnOpenPill()
    {
        Harness h(420);
        h.panel->openDisplayPill();
        QWidget* pill = h.pill();
        QWidget* strip = h.zoomStrip();
        QVERIFY(strip != nullptr);
        QVERIFY2(strip->geometry().intersects(pill->geometry()),
                 "the zoom strip does not overlap the pill, so this tests nothing");
        const QPoint point = strip->geometry().intersected(pill->geometry()).center();

        h.pan.setFreqScaleFontPx(h.pan.freqScaleFontPx() + 2);
        QCoreApplication::processEvents();
        QVERIFY2(stackIndex(pill) > stackIndex(strip),
                 "the zoom strip is stacked above the open pill");
        QVERIFY(isInside(h.pan.childAt(point), pill));

        h.panel->repositionZoomButtons();
        QVERIFY(stackIndex(pill) > stackIndex(strip));
    }

    // 2026-10-11, "the flag is still jumping around when I click things in
    // the display pill": every control on every tab is worked the way an
    // operator works it, with a flag under the pill. None of them changes
    // the pan's view, so none may start a placement pass, and the pass a
    // paint runs in a build without the GPU path (one per paint) must put
    // the flag exactly where it was.
    //
    // The color and picture buttons answer through the strip's test
    // choosers (a color picked, the picture chooser canceled), so the
    // modal dialogs themselves are not opened.
    void noPillControlMovesOrRestacksAFlag_data()
    {
        // The FFT tab is worked once in each sizing mode (design D38): the
        // three Sizing buttons in all of them, and the FFT Size and Hz/bin
        // sliders in the mode that makes each live.
        using Mode = ControlRanges::DisplayFftSizingMode;
        QTest::addColumn<int>("tab");
        QTest::addColumn<int>("sizing");
        QTest::newRow("Spectrum") << int(Tab::Spectrum) << int(Mode::Auto);
        QTest::newRow("Levels") << int(Tab::Levels) << int(Mode::Auto);
        QTest::newRow("Waterfall") << int(Tab::Waterfall) << int(Mode::Auto);
        QTest::newRow("FFT, Sizing Auto") << int(Tab::Fft) << int(Mode::Auto);
        QTest::newRow("FFT, Sizing Hz/bin") << int(Tab::Fft) << int(Mode::HzPerBin);
        QTest::newRow("FFT, Sizing Fixed") << int(Tab::Fft) << int(Mode::Fixed);
        QTest::newRow("3D View") << int(Tab::View3D) << int(Mode::Auto);
    }

    void noPillControlMovesOrRestacksAFlag()
    {
        using Mode = ControlRanges::DisplayFftSizingMode;
        QFETCH(int, tab);
        QFETCH(int, sizing);
        Harness h;
        {
            SpectrumOverlayPanel::DisplayPillFftState state = h.panel->fftState();
            state.sizingMode = Mode(sizing);
            state.baselineSize = 16384;
            state.runningSize = 16384;
            state.sampleRateHz = 192000.0;
            state.hzPerBinTarget = Mode(sizing) == Mode::HzPerBin ? 12.0 : 0.0;
            state.lastHzPerBinTarget = 12.0;
            h.panel->setFftState(state);
        }
        QStringList workedNames;
        VfoWidget* second = h.pan.addVfoWidget(1);
        second->setFrequency(kSecondHz);
        h.panel->setPillColorChooser(
            [](const QColor&, const QString&) { return QColor(10, 200, 30); });
        h.panel->setPillPictureChooser([](const QString&) { return QString(); });
        h.panel->openDisplayPill();
        QWidget* pill = h.pill();
        QCoreApplication::processEvents();
        h.pan.updateVfoPositions();

        QSignalSpy raised(&h.pan, &SpectrumWidget::flagsRaised);
        const auto flagState = [&]() {
            return QStringLiteral("%1,%2 %3x%4 %5 | %6,%7 %8x%9 %10")
                .arg(h.flag->x()).arg(h.flag->y()).arg(h.flag->width()).arg(h.flag->height())
                .arg(h.flag->isVisible())
                .arg(second->x()).arg(second->y()).arg(second->width())
                .arg(second->height()).arg(second->isVisible());
        };
        const QString before = flagState();
        int worked = 0;
        const auto check = [&](const QString& what) {
            QCoreApplication::processEvents();
            ++worked;
            workedNames.append(what);
            if (raised.count() != 0) {
                QFAIL(qPrintable(what + QStringLiteral(" started a placement pass")));
            }
            if (flagState() != before) {
                QFAIL(qPrintable(what + QStringLiteral(" changed a flag: ") + flagState()));
            }
            // What a paint does in a build without the GPU path.
            h.pan.updateVfoPositions();
            if (flagState() != before) {
                QFAIL(qPrintable(what + QStringLiteral(": the next pass moved a flag: ")
                                 + flagState()));
            }
            if (!h.flyoutIsAboveEveryFlagPart(pill)) {
                QFAIL(qPrintable(what + QStringLiteral(": a flag is above the pill")));
            }
            raised.clear();
        };

        // The tab switch itself.
        h.panel->setDisplayPillTab(Tab(tab) == Tab::Spectrum ? Tab::Fft : Tab::Spectrum);
        check(QStringLiteral("a tab switch"));
        if (QTest::currentTestFailed()) { return; }
        h.panel->setDisplayPillTab(Tab(tab));
        check(QStringLiteral("a tab switch back"));
        if (QTest::currentTestFailed()) { return; }

        const QList<QWidget*> widgets = pill->findChildren<QWidget*>();
        for (QWidget* widget : widgets) {
            if (!widget->isVisibleTo(pill) || !widget->isEnabled()) {
                continue;
            }
            const QString name = widget->objectName().isEmpty()
                ? QString::fromLatin1(widget->metaObject()->className())
                : widget->objectName();
            if (auto* slider = qobject_cast<QSlider*>(widget)) {
                slider->setFocus();
                QTest::keyClick(slider, slider->value() < slider->maximum() ? Qt::Key_Right
                                                                           : Qt::Key_Left);
                check(QStringLiteral("slider ") + name);
            } else if (auto* combo = qobject_cast<QComboBox*>(widget)) {
                if (combo->count() > 1) {
                    combo->setFocus();
                    QTest::keyClick(combo, combo->currentIndex() + 1 < combo->count()
                                               ? Qt::Key_Down : Qt::Key_Up);
                    check(QStringLiteral("combo ") + name);
                }
            } else if (auto* button = qobject_cast<QAbstractButton*>(widget)) {
                if (button->objectName().startsWith(QStringLiteral("displayTab"))) {
                    continue;
                }
                QTest::mouseClick(button, Qt::LeftButton);
                check(QStringLiteral("button ") + name + QLatin1Char(' ') + button->text());
            }
            if (QTest::currentTestFailed()) { return; }
            if (!h.panel->displayPillVisible()) {
                QFAIL(qPrintable(name + QStringLiteral(" closed the pill")));
            }
        }
        QVERIFY2(worked > 4, "the tab showed almost no control to work");
        if (Tab(tab) == Tab::Levels) {
            // Working all five approved modes ends on Scale. Explicitly
            // choose Manual and exercise its Low editor under the same
            // placement/stacking assertions as every other control.
            auto* manual = pill->findChild<QPushButton*>(QStringLiteral("pillWaterfallLevelsManual"));
            QVERIFY(manual && manual->isVisibleTo(pill));
            QTest::mouseClick(manual, Qt::LeftButton);
            check(QStringLiteral("Manual waterfall choice"));
            if (QTest::currentTestFailed()) { return; }
            auto* low = pill->findChild<QSlider*>(QStringLiteral("pillDisplayWfLowLevel"));
            QVERIFY(low && low->isVisibleTo(pill) && low->isEnabled());
            low->setFocus();
            QTest::keyClick(low, low->value() < low->maximum() ? Qt::Key_Right : Qt::Key_Left);
            check(QStringLiteral("slider pillDisplayWfLowLevel"));
            if (QTest::currentTestFailed()) { return; }
            QVERIFY(workedNames.contains(QStringLiteral("slider pillDisplayWfLowLevel")));
        }
        if (Tab(tab) == Tab::Fft) {
            const auto workedOne = [&workedNames](const char* name) {
                return workedNames.contains(QLatin1String(name));
            };
            QVERIFY(workedOne("button pillDisplayFftSizingModeAuto Auto"));
            QVERIFY(workedOne("button pillDisplayFftSizingModeHzPerBin Hz/bin"));
            QVERIFY(workedOne("button pillDisplayFftSizingModeFixed Fixed"));
            // A grayed slider is not worked: it is live in its own mode only.
            QCOMPARE(workedOne("slider pillDisplayFftSize"), Mode(sizing) == Mode::Fixed);
            QCOMPARE(workedOne("slider pillDisplayHzPerBinTarget"),
                     Mode(sizing) == Mode::HzPerBin);
        }
        qInfo() << "controls worked:" << worked;
    }
};

QTEST_MAIN(TstOverlayFlyoutAboveFlags)
#include "tst_overlay_flyout_above_flags.moc"
