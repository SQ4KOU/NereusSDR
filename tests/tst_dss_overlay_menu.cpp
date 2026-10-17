// =================================================================
// tests/tst_dss_overlay_menu.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Display pill plan Task 19 (D3, D33, S9; R-DSP-07, R-DSP-45; design
// docs/architecture/2026-10-09-display-pill-and-levels-design.md section
// 7): the right-click pan menu, trimmed.
//
// This file was the 3D stacked-trace plan's Task 13 test of the menu's
// "3D VIEW" section. The design moves that section, and the waterfall and
// fill rows, into the pan's Display pill, so every case that pinned them
// is rewritten here to the trimmed menu: it holds exactly Ref Level, Dyn
// Range, CTUN, "Add notch here" and "Display settings...", and nothing of
// the removed rows is left, neither a control nor a signal.
//
// The cases at the bottom reuse the right-click idiom of
// tst_notch_hit_test.cpp's
// overlay_menu_add_notch_forwards_the_cursor_frequency: a real
// QMouseEvent, SpectrumWidget's own mousePressEvent building and wiring
// the menu, then the menu found as a child and driven. That is the only
// way to prove the connects in SpectrumWidget.cpp are the real wiring.
//
// 2026-10-10 - Rewritten for the trimmed menu. J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStringList>

#include "core/ConnectionState.h"
#include "gui/SpectrumOverlayMenu.h"
#include "gui/SpectrumWidget.h"

using namespace NereusSDR;

namespace {

// Mirrors tst_notch_hit_test.cpp's sendMouse(): this project's idiom for
// driving SpectrumWidget's mousePressEvent in headless tests is a directly
// synthesized QMouseEvent via QApplication::sendEvent.
void sendMouse(QWidget* w, QEvent::Type type, QPoint pos,
               Qt::MouseButton button, Qt::MouseButtons buttons,
               Qt::KeyboardModifiers mods = Qt::NoModifier)
{
    QMouseEvent me(type, QPointF(pos), w->mapToGlobal(QPointF(pos)),
                   button, buttons, mods);
    QApplication::sendEvent(w, &me);
}

// A pan as the right-click needs it: shown, connected, with a span.
void setUpWidget(SpectrumWidget& w)
{
    w.resize(1000, 400);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.setConnectionState(ConnectionState::Connected);
    w.setFrequencyRange(14'200'000.0, 128'000.0);
}

// No notches or spots are pushed, so a right-click on the empty pan falls
// through to the overlay menu.
SpectrumOverlayMenu* openMenu(SpectrumWidget& w)
{
    sendMouse(&w, QEvent::MouseButtonPress, QPoint(600, 50),
              Qt::RightButton, Qt::RightButton);
    return w.findChild<SpectrumOverlayMenu*>();
}

bool hasSignal(const char* normalizedSignature)
{
    return SpectrumOverlayMenu::staticMetaObject.indexOfSignal(normalizedSignature) >= 0;
}

} // namespace

class TestDssOverlayMenu : public QObject {
    Q_OBJECT

private slots:
    // Acceptance: the menu holds exactly CTUN, Add notch here, Ref Level,
    // Dyn Range and "Display settings..."; no scheme, gain, black, fill or
    // 3D control exists in it (findChildren by type and name).
    void holdsExactlyTheFiveControls() {
        SpectrumOverlayMenu m;

        const auto sliders = m.findChildren<QSlider*>();
        const auto checks  = m.findChildren<QCheckBox*>();
        const auto buttons = m.findChildren<QPushButton*>();
        QCOMPARE(sliders.size(), 2);
        QCOMPARE(checks.size(), 1);
        QCOMPARE(buttons.size(), 2);
        QCOMPARE(m.findChildren<QComboBox*>().size(), 0);

        auto* ref = m.findChild<QSlider*>(QStringLiteral("refLevelSlider"));
        auto* dyn = m.findChild<QSlider*>(QStringLiteral("dynRangeSlider"));
        QVERIFY(ref);
        QVERIFY(dyn);
        // The ranges the two rows had before the trim.
        QCOMPARE(ref->minimum(), -160);
        QCOMPARE(ref->maximum(), 20);
        QCOMPARE(dyn->minimum(), 20);
        QCOMPARE(dyn->maximum(), 160);

        auto* ctun = m.findChild<QCheckBox*>(QStringLiteral("ctunCheck"));
        QVERIFY(ctun);
        QCOMPARE(ctun->text(), QStringLiteral("CTUN (independent pan)"));

        auto* notch = m.findChild<QPushButton*>(QStringLiteral("notchAddButton"));
        QVERIFY(notch);
        QCOMPARE(notch->text(), QStringLiteral("Add notch here"));

        auto* display = m.findChild<QPushButton*>(QStringLiteral("displaySettingsButton"));
        QVERIFY(display);
        QCOMPARE(display->text(), QStringLiteral("Display settings..."));
        QVERIFY(display->isEnabled());

        // By name: the seven 3D controls the menu used to carry.
        for (const char* gone : {"spectrumRenderModeCombo", "dssFloorDepthSlider",
                                 "dssGainSlider", "dssRowSpanSlider", "dssAngleSlider",
                                 "dssSpeedSlider", "dssSliceShadowCheck", "dssSpeedLabel"}) {
            QVERIFY2(m.findChild<QWidget*>(QString::fromLatin1(gone)) == nullptr, gone);
        }

        // By label: no row or section title of the removed rows is left,
        // and the kept ones are there.
        QStringList labels;
        for (const QLabel* label : m.findChildren<QLabel*>()) {
            labels.append(label->text());
        }
        for (const char* gone : {"Waterfall", "Color Scheme", "Color Gain", "Black Level",
                                 "Spectrum", "Fill Alpha", "3D VIEW", "Spectrum:",
                                 "3D Floor:", "3D Gain:", "3D Span:", "3D Angle:",
                                 "3D Speed:"}) {
            QVERIFY2(!labels.contains(QString::fromLatin1(gone)), gone);
        }
        for (const char* kept : {"Display Range", "Ref Level", "Dyn Range", "Tuning", "Notch"}) {
            QVERIFY2(labels.contains(QString::fromLatin1(kept)), kept);
        }
        for (const QCheckBox* check : checks) {
            QVERIFY(check->text() != QStringLiteral("Fill spectrum trace"));
            QVERIFY(check->text() != QStringLiteral("3D Slice Shadow"));
        }
    }

    // The scheme, gain, black, fill and seven 3D signals are removed; the
    // four that stay and the new one are declared.
    void removedSignalsAreGone_keptOnesStay() {
        for (const char* gone : {"wfColorGainChanged(int)", "wfBlackLevelChanged(int)",
                                 "wfColorSchemeChanged(int)", "fillAlphaChanged(float)",
                                 "panFillChanged(bool)", "spectrumRenderModeChanged(int)",
                                 "dssFloorDepthChanged(int)", "dssGainChanged(int)",
                                 "dssRowSpanChanged(int)", "dssAngleChanged(int)",
                                 "dssRowDividerChanged(int)", "dssSliceShadowChanged(bool)"}) {
            QVERIFY2(!hasSignal(gone), gone);
        }
        for (const char* kept : {"refLevelChanged(float)", "dynRangeChanged(float)",
                                 "ctunChanged(bool)", "notchAddRequested(double)",
                                 "displaySettingsRequested()"}) {
            QVERIFY2(hasSignal(kept), kept);
        }
    }

    // setValues seeds the three values before showing without echoing
    // back out, or opening the menu would move the operator's range.
    void setValues_seedsWithoutEmitting() {
        SpectrumOverlayMenu m;
        QSignalSpy refSpy(&m, &SpectrumOverlayMenu::refLevelChanged);
        QSignalSpy dynSpy(&m, &SpectrumOverlayMenu::dynRangeChanged);
        QSignalSpy ctunSpy(&m, &SpectrumOverlayMenu::ctunChanged);

        m.setValues(-30.0f, 90.0f, false);

        QCOMPARE(refSpy.count(), 0);
        QCOMPARE(dynSpy.count(), 0);
        QCOMPARE(ctunSpy.count(), 0);
        QCOMPARE(m.findChild<QSlider*>(QStringLiteral("refLevelSlider"))->value(), -30);
        QCOMPARE(m.findChild<QSlider*>(QStringLiteral("dynRangeSlider"))->value(), 90);
        QCOMPARE(m.findChild<QCheckBox*>(QStringLiteral("ctunCheck"))->isChecked(), false);

        // The third argument defaults to CTUN on.
        m.setValues(-48.0f, 68.0f);
        QCOMPARE(m.findChild<QCheckBox*>(QStringLiteral("ctunCheck"))->isChecked(), true);
        QCOMPARE(ctunSpy.count(), 0);
    }

    void movingAKeptControl_emitsItsSignalOnce() {
        SpectrumOverlayMenu m;
        m.setValues(-48.0f, 68.0f, true);
        QSignalSpy refSpy(&m, &SpectrumOverlayMenu::refLevelChanged);
        QSignalSpy dynSpy(&m, &SpectrumOverlayMenu::dynRangeChanged);
        QSignalSpy ctunSpy(&m, &SpectrumOverlayMenu::ctunChanged);

        m.findChild<QSlider*>(QStringLiteral("refLevelSlider"))->setValue(-60);
        m.findChild<QSlider*>(QStringLiteral("dynRangeSlider"))->setValue(80);
        m.findChild<QCheckBox*>(QStringLiteral("ctunCheck"))->setChecked(false);

        QCOMPARE(refSpy.count(), 1);
        QCOMPARE(refSpy.at(0).at(0).toFloat(), -60.0f);
        QCOMPARE(dynSpy.count(), 1);
        QCOMPARE(dynSpy.at(0).at(0).toFloat(), 80.0f);
        QCOMPARE(ctunSpy.count(), 1);
        QCOMPARE(ctunSpy.at(0).at(0).toBool(), false);
    }

    // Acceptance: choosing "Display settings..." emits
    // displaySettingsRequested(). The popup closes itself first, so it is
    // not left over the pill it opened.
    void displaySettingsLine_emitsOnceAndClosesTheMenu() {
        SpectrumOverlayMenu m;
        m.show();
        QVERIFY(m.isVisible());
        QSignalSpy spy(&m, &SpectrumOverlayMenu::displaySettingsRequested);
        bool visibleWhenEmitted = true;
        connect(&m, &SpectrumOverlayMenu::displaySettingsRequested, &m,
                [&m, &visibleWhenEmitted]() { visibleWhenEmitted = m.isVisible(); });

        m.findChild<QPushButton*>(QStringLiteral("displaySettingsButton"))->click();

        QCOMPARE(spy.count(), 1);
        QVERIFY(!visibleWhenEmitted);
        QVERIFY(!m.isVisible());
    }

    // Acceptance: from a real SpectrumWidget the line emits
    // displayPillRequested().
    void fromTheWidget_theLineAsksForThePill() {
        SpectrumWidget w;
        setUpWidget(w);
        SpectrumOverlayMenu* menu = openMenu(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        QVERIFY(menu->isVisible());
        QSignalSpy spy(&w, &SpectrumWidget::displayPillRequested);

        menu->findChild<QPushButton*>(QStringLiteral("displaySettingsButton"))->click();

        QCOMPARE(spy.count(), 1);
        QVERIFY(!menu->isVisible());
    }

    // Acceptance: changing Ref Level in the menu emits bandRangeRequested
    // with the new ceiling and the unchanged span. Dyn Range keeps the
    // ceiling and moves the floor. The widget does not store the range
    // itself: with nobody answering, its range stays and the menu goes
    // back to it.
    void fromTheWidget_theRangeRowsAskAndTheWidgetStoresNothing() {
        SpectrumWidget w;
        setUpWidget(w);
        w.setReceiveDbmRange(-116.0f, -48.0f);
        SpectrumOverlayMenu* menu = openMenu(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        auto* ref = menu->findChild<QSlider*>(QStringLiteral("refLevelSlider"));
        auto* dyn = menu->findChild<QSlider*>(QStringLiteral("dynRangeSlider"));
        QVERIFY(ref);
        QVERIFY(dyn);
        QCOMPARE(ref->value(), -48);
        QCOMPARE(dyn->value(), 68);

        QSignalSpy asked(&w, &SpectrumWidget::bandRangeRequested);
        QSignalSpy strip(&w, &SpectrumWidget::dbmRangeChangeRequested);

        ref->setValue(-60);
        QCOMPARE(asked.count(), 1);
        QCOMPARE(asked.at(0).at(0).toFloat(), -128.0f);   // floor: ceiling less the 68 dB span
        QCOMPARE(asked.at(0).at(1).toFloat(), -60.0f);    // the new ceiling
        QCOMPARE(w.refLevel(), -48.0f);
        QCOMPARE(w.dynamicRange(), 68.0f);
        QCOMPARE(ref->value(), -48);                      // not taken: back to the pan

        dyn->setValue(80);
        QCOMPARE(asked.count(), 2);
        QCOMPARE(asked.at(1).at(0).toFloat(), -128.0f);   // floor: the ceiling less 80
        QCOMPARE(asked.at(1).at(1).toFloat(), -48.0f);    // the ceiling stays
        QCOMPARE(w.refLevel(), -48.0f);
        QCOMPARE(w.dynamicRange(), 68.0f);
        QCOMPARE(dyn->value(), 68);

        // The menu is not the dB strip: it does not use the strip's signal.
        QCOMPARE(strip.count(), 0);
    }

    // A receiver that takes the range (as the window's band store does,
    // through setReceiveDbmRange) leaves the menu on the value asked for.
    void fromTheWidget_aTakenRangeStaysInTheMenu() {
        SpectrumWidget w;
        setUpWidget(w);
        w.setReceiveDbmRange(-116.0f, -48.0f);
        connect(&w, &SpectrumWidget::bandRangeRequested, &w,
                [&w](float minDbm, float maxDbm) { w.setReceiveDbmRange(minDbm, maxDbm); });
        SpectrumOverlayMenu* menu = openMenu(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        auto* ref = menu->findChild<QSlider*>(QStringLiteral("refLevelSlider"));
        auto* dyn = menu->findChild<QSlider*>(QStringLiteral("dynRangeSlider"));
        QSignalSpy asked(&w, &SpectrumWidget::bandRangeRequested);

        ref->setValue(-60);
        QCOMPARE(asked.count(), 1);                       // one request, no echo
        QCOMPARE(w.refLevel(), -60.0f);
        QCOMPARE(w.dynamicRange(), 68.0f);
        QCOMPARE(ref->value(), -60);
        QCOMPARE(dyn->value(), 68);

        dyn->setValue(80);
        QCOMPARE(asked.count(), 2);
        QCOMPARE(w.refLevel(), -60.0f);
        QCOMPARE(w.dynamicRange(), 80.0f);
        QCOMPARE(dyn->value(), 80);
    }
};

QTEST_MAIN(TestDssOverlayMenu)
#include "tst_dss_overlay_menu.moc"
