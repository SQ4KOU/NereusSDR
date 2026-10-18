// =================================================================
// tests/tst_display_popup_binding.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Display pill plan Task 19 (D3, D33, S9; R-DSP-07, R-DSP-45; design
// docs/architecture/2026-10-09-display-pill-and-levels-design.md section
// 7): what the trimmed right-click pan menu is bound to.
//
// This file was the 3D stacked-trace plan's Task 20 test of the popup's
// thirteen connections to DisplaySettingsModel. The design moves the
// scheme, gain, black, fill and 3D rows into the pan's Display pill and
// makes the dB range the band's (S9), so those cases are rewritten:
//
//   - on a real SpectrumWidget, the popup is seeded with Ref Level, Dyn
//     Range and CTUN on open, from the display model, and follows a range
//     change made elsewhere while it is open, without emitting;
//   - in a real local MainWindow with two pans (no radio, no Core), the
//     menu's Ref Level and Dyn Range, and the dB strip's gestures, reach
//     the band store for the pan's band; the pan, its pill and the other
//     pan's model follow; and "Display settings..." opens that pan's pill.
//
// The removed rows themselves are pinned absent in
// tests/tst_dss_overlay_menu.cpp.
//
// Isolation: a profile of this process's own (initTestCase), cleared
// before and after every slot.
//
// 2026-10-10 - Rewritten for the trimmed menu and the band store.
//               J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCheckBox>
#include <QLoggingCategory>
#include <QMetaMethod>
#include <QMouseEvent>
#include <QPushButton>
#include <QSignalSpy>
#include <QSlider>
#include <QStandardPaths>
#include <QWheelEvent>
#include <cmath>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/HpsdrModel.h"
#include "fakes/MainWindowTestSettings.h"
#include "gui/MainWindow.h"
#include "gui/PanModels.h"
#include "gui/PanadapterStack.h"
#include "gui/SpectrumOverlayMenu.h"
#include "gui/SpectrumOverlayPanel.h"
#include "gui/SpectrumWidget.h"
#include "models/DisplaySettingsModel.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

namespace {

// A directly synthesized QMouseEvent via QApplication::sendEvent is this
// project's established way to drive SpectrumWidget's mousePressEvent
// headlessly.
void sendMouse(QWidget* w, QEvent::Type type, QPoint pos,
               Qt::MouseButton button, Qt::MouseButtons buttons)
{
    QMouseEvent me(type, QPointF(pos), w->mapToGlobal(QPointF(pos)),
                   button, buttons, Qt::NoModifier);
    QApplication::sendEvent(w, &me);
}

// One wheel notch up at a point of the widget.
void sendWheelUp(QWidget* w, QPoint pos)
{
    QWheelEvent wheel(QPointF(pos), w->mapToGlobal(QPointF(pos)), QPoint(),
                      QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(w, &wheel);
}

// Common fixture: resize/show/expose before any synthetic mouse event.
// setConnectionState(Connected) clears mousePressEvent's disconnected-click
// guard; setFrequencyRange gives xToHz() a span.
void setUpWidget(SpectrumWidget& w)
{
    w.resize(1000, 400);
    w.show();
    QVERIFY(QTest::qWaitForWindowExposed(&w));
    w.setConnectionState(ConnectionState::Connected);
    w.setFrequencyRange(14'200'000.0, 128'000.0);
}

// Right-clicks the empty pan (no notches or spots, so the click falls
// through to the overlay menu) and returns the popup that
// SpectrumWidget::mousePressEvent builds and wires.
SpectrumOverlayMenu* openPopup(SpectrumWidget& w, QPoint at = QPoint(600, 50))
{
    sendMouse(&w, QEvent::MouseButtonPress, at, Qt::RightButton, Qt::RightButton);
    return w.findChild<SpectrumOverlayMenu*>();
}

QSlider* refSlider(SpectrumOverlayMenu* menu)
{
    return menu->findChild<QSlider*>(QStringLiteral("refLevelSlider"));
}

QSlider* dynSlider(SpectrumOverlayMenu* menu)
{
    return menu->findChild<QSlider*>(QStringLiteral("dynRangeSlider"));
}

} // namespace

class TestDisplayPopupBinding : public QObject
{
    Q_OBJECT

    QString m_isolatedSettingsPath;

private slots:
    void initTestCase()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        QLoggingCategory::setFilterRules(QStringLiteral("nereus.*.debug=false"));
        const QString profile =
            QStringLiteral("display-popup-binding-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        m_isolatedSettingsPath = AppSettings::resolveSettingsPath(profile);
        QCOMPARE(AppSettings::instance().filePath(), m_isolatedSettingsPath);
    }

    void init() { AppSettings::instance().clear(); }

    void cleanup()
    {
        if (AppSettings::instance().filePath() == m_isolatedSettingsPath) {
            AppSettings::instance().clear();
        }
    }

    // Acceptance: "the widget still seeds Ref Level, Dyn Range and CTUN on
    // open." Each right-click seeds again, so a second open shows what
    // changed while the menu was closed.
    void openSeedsRefLevelDynRangeAndCtun()
    {
        SpectrumWidget w;
        setUpWidget(w);
        w.setReceiveDbmRange(-108.0f, -12.0f);
        w.setCtunEnabled(false);

        SpectrumOverlayMenu* menu = openPopup(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        QVERIFY(menu->isVisible());
        auto* ctun = menu->findChild<QCheckBox*>(QStringLiteral("ctunCheck"));
        QVERIFY(ctun);
        QCOMPARE(refSlider(menu)->value(), -12);
        QCOMPARE(dynSlider(menu)->value(), 96);
        QCOMPARE(ctun->isChecked(), false);

        menu->hide();
        w.setReceiveDbmRange(-120.0f, -40.0f);
        w.setCtunEnabled(true);
        QCOMPARE(refSlider(menu)->value(), -12);   // closed: not followed
        QCOMPARE(openPopup(w), menu);
        QCOMPARE(refSlider(menu)->value(), -40);
        QCOMPARE(dynSlider(menu)->value(), 80);
        QCOMPARE(ctun->isChecked(), true);
    }

    // The seed reads the display model, not a widget member that happens
    // to agree with it. The model-to-widget leg for the ceiling is severed
    // first, so the widget's own member is provably stale when the popup
    // opens.
    void seedingReadsTheModel_notAStaleWidget()
    {
        SpectrumWidget w;
        setUpWidget(w);
        DisplaySettingsModel* m = w.displaySettings();
        QVERIFY(QObject::disconnect(m, QMetaMethod::fromSignal(&DisplaySettingsModel::refLevelChanged),
                                     &w, QMetaMethod()));
        m->setRefLevel(-12.0f);
        QVERIFY2(std::abs(w.refLevel() - (-12.0f)) > 1.0f,
                 "test invariant: the severed connect must leave the widget stale");

        SpectrumOverlayMenu* menu = openPopup(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        QCOMPARE(refSlider(menu)->value(), -12);
    }

    // While the popup is open, a range change made elsewhere is shown at
    // once, and showing it emits nothing: neither the popup's own signals
    // nor a request for a range. Each end is changed and checked on its
    // own, so one connection cannot cover for the other.
    void liveRefresh_followsARangeChangeWithoutEmitting()
    {
        SpectrumWidget w;
        setUpWidget(w);
        w.setReceiveDbmRange(-116.0f, -48.0f);
        DisplaySettingsModel* m = w.displaySettings();
        SpectrumOverlayMenu* menu = openPopup(w);
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        QVERIFY(menu->isVisible());
        QSignalSpy asked(&w, &SpectrumWidget::bandRangeRequested);

        {
            QSignalSpy popupSpy(menu, &SpectrumOverlayMenu::refLevelChanged);
            QSignalSpy modelSpy(m, &DisplaySettingsModel::refLevelChanged);
            m->setRefLevel(-12.0f);
            QCOMPARE(refSlider(menu)->value(), -12);
            QCOMPARE(popupSpy.count(), 0);
            QCOMPARE(modelSpy.count(), 1);
        }
        {
            QSignalSpy popupSpy(menu, &SpectrumOverlayMenu::dynRangeChanged);
            QSignalSpy modelSpy(m, &DisplaySettingsModel::dynamicRangeChanged);
            m->setDynamicRange(96.0f);
            QCOMPARE(dynSlider(menu)->value(), 96);
            QCOMPARE(popupSpy.count(), 0);
            QCOMPARE(modelSpy.count(), 1);
        }
        {
            // The way the band store sets a pan.
            QSignalSpy popupSpy(menu, &SpectrumOverlayMenu::refLevelChanged);
            w.setReceiveDbmRange(-130.0f, -30.0f);
            QCOMPARE(refSlider(menu)->value(), -30);
            QCOMPARE(dynSlider(menu)->value(), 100);
            QCOMPARE(popupSpy.count(), 0);
        }
        QCOMPARE(asked.count(), 0);
    }

    // Acceptance: "with a PanModels model wired, the current band's stored
    // dB Max changes and the widget's range follows", in the window, where
    // the wiring is. Also the open finding of Task 9: a range set on the dB
    // strip is saved for the pan's band.
    void inTheWindow_menuAndStripRangesGoToTheBandStore()
    {
        Test::markAudioFirstRunDone();
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        window.show();
        RadioModel* radio = window.radioModel();
        radio->setBoardForTest(HPSDRHW::Saturn);
        radio->configureStreamPool(5, 5, 192000);
        radio->setConnectionStateForTest(ConnectionState::Connected);
        QVERIFY(radio->addSlice(QStringLiteral("pan-0")) >= 0);
        QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                          Q_ARG(QString, QStringLiteral("2v"))));
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* first = stack->spectrum(QStringLiteral("pan-0"));
        SpectrumWidget* second = stack->spectrum(QStringLiteral("pan-1"));
        QVERIFY(first);
        QVERIFY(second);
        QTRY_VERIFY(second->vfoWidget(1));
        auto* firstPill = first->findChild<SpectrumOverlayPanel*>();
        auto* secondPill = second->findChild<SpectrumOverlayPanel*>();
        QVERIFY(firstPill);
        QVERIFY(secondPill);
        PanModels* pans = window.panModels();
        QVERIFY(pans);
        PanadapterModel* firstModel = pans->model(QStringLiteral("pan-0"));
        PanadapterModel* secondModel = pans->model(QStringLiteral("pan-1"));
        QVERIFY(firstModel);
        QVERIFY(secondModel);
        const Band band = secondModel->band();
        const auto stored = [secondModel, band]() { return secondModel->perBandGrid(band); };
        QSlider* pillMax = second->findChild<QSlider*>(QStringLiteral("pillDisplayGridMax"));
        QSlider* pillMin = second->findChild<QSlider*>(QStringLiteral("pillDisplayGridMin"));
        QVERIFY(pillMax);
        QVERIFY(pillMin);

        // Nothing stored yet: the old range, and no band key.
        QCOMPARE(stored().dbMax, -48);
        QCOMPARE(stored().dbMin, -116);
        const auto bandKeys = [&s]() {
            int count = 0;
            for (const QString& key : s.allKeys()) {
                if (key.startsWith(QStringLiteral("DisplayGridMax_"))
                    || key.startsWith(QStringLiteral("DisplayGridMin_"))) {
                    ++count;
                }
            }
            return count;
        };
        QCOMPARE(bandKeys(), 0);

        // ---- The menu, opened by a real right-click on the second pan ----
        second->setConnectionState(ConnectionState::Connected);
        SpectrumOverlayMenu* menu =
            openPopup(*second, QPoint(second->width() * 4 / 5, 40));
        QVERIFY2(menu != nullptr, "right-click did not open the overlay menu");
        QVERIFY(menu->isVisible());
        QCOMPARE(refSlider(menu)->value(), -48);
        QCOMPARE(dynSlider(menu)->value(), 68);

        // Ref Level: the new ceiling, the span kept.
        refSlider(menu)->setValue(-60);
        QCOMPARE(stored().dbMax, -60);
        QCOMPARE(stored().dbMin, -128);
        QCOMPARE(second->refLevel(), -60.0f);
        QCOMPARE(second->dynamicRange(), 68.0f);
        QCOMPARE(refSlider(menu)->value(), -60);
        QCOMPARE(dynSlider(menu)->value(), 68);
        QCOMPARE(pillMax->value(), -60);
        QCOMPARE(pillMin->value(), -128);
        // One store for the band, whichever pan asks.
        QCOMPARE(firstModel->perBandGrid(band).dbMax, -60);
        QCOMPARE(firstModel->perBandGrid(band).dbMin, -128);
        QCOMPARE(bandKeys(), 2);

        // Dyn Range: the ceiling kept, the floor moved.
        dynSlider(menu)->setValue(80);
        QCOMPARE(stored().dbMax, -60);
        QCOMPARE(stored().dbMin, -140);
        QCOMPARE(second->refLevel(), -60.0f);
        QCOMPARE(second->dynamicRange(), 80.0f);
        QCOMPARE(pillMin->value(), -140);

        // A whole range below the old floor, in one request: the ends are
        // written in the order that never crosses them.
        emit second->bandRangeRequested(-170.0f, -150.0f);
        QCOMPARE(stored().dbMax, -150);
        QCOMPARE(stored().dbMin, -170);
        QCOMPARE(second->refLevel(), -150.0f);
        QCOMPARE(second->dynamicRange(), 20.0f);
        // And back above the old ceiling.
        emit second->bandRangeRequested(-116.0f, -48.0f);
        QCOMPARE(stored().dbMax, -48);
        QCOMPARE(stored().dbMin, -116);
        QCOMPARE(second->refLevel(), -48.0f);
        QCOMPARE(second->dynamicRange(), 68.0f);

        // An upside-down pair is not taken.
        emit second->bandRangeRequested(-40.0f, -90.0f);
        QCOMPARE(stored().dbMax, -48);
        QCOMPARE(stored().dbMin, -116);
        QCOMPARE(second->refLevel(), -48.0f);

        // The store already holds the pair asked for, but the pan's live
        // range has moved off it (as the grid following the noise floor
        // moves it): the pan is set from the store all the same.
        second->setDbmRange(-126.0f, -58.0f);
        QCOMPARE(second->refLevel(), -58.0f);
        emit second->bandRangeRequested(-116.0f, -48.0f);
        QCOMPARE(second->refLevel(), -48.0f);
        QCOMPARE(second->dynamicRange(), 68.0f);
        menu->hide();

        // ---- The dB strip ----
        // One wheel notch up on the strip: 5 dB less span, the floor kept
        // (the gesture is the widget's own and is not changed here). The
        // range it ends on is saved for the band.
        sendWheelUp(second, QPoint(second->width() - 10, 30));
        QCOMPARE(second->dynamicRange(), 63.0f);
        QCOMPARE(second->refLevel(), -53.0f);
        QCOMPARE(stored().dbMax, -53);
        QCOMPARE(stored().dbMin, -116);
        QCOMPARE(pillMax->value(), -53);
        QCOMPARE(firstModel->perBandGrid(band).dbMax, -53);

        // The end of a strip drag reports the range it ended on; the band
        // store holds whole dB.
        emit second->dbmRangeChangeRequested(-110.4f, -41.6f);
        QCOMPARE(stored().dbMax, -42);
        QCOMPARE(stored().dbMin, -110);
        QCOMPARE(second->refLevel(), -42.0f);
        QCOMPARE(second->dynamicRange(), 68.0f);
        QCOMPARE(pillMax->value(), -42);
        QCOMPARE(pillMin->value(), -110);

        // While the pan shows the transmit grid, a range set on it is the
        // transmitter's: the band's receive range is left alone.
        second->setMoxOverlay(true);
        emit second->dbmRangeChangeRequested(-80.0f, 10.0f);
        emit second->bandRangeRequested(-80.0f, 10.0f);
        QCOMPARE(stored().dbMax, -42);
        QCOMPARE(stored().dbMin, -110);

        // Keyed, the menu's two rows move the live (transmit) grid, as the
        // dB strip does: the pan follows, the slider stays where it was
        // put, nothing is asked of the band store and no band key is
        // written.
        {
            const int keysBefore = bandKeys();
            QCOMPARE(openPopup(*second, QPoint(second->width() * 4 / 5, 40)), menu);
            QVERIFY(menu->isVisible());
            QSignalSpy asked(second, &SpectrumWidget::bandRangeRequested);
            const int newRef = (refSlider(menu)->value() == -20) ? -30 : -20;
            refSlider(menu)->setValue(newRef);
            QCOMPARE(second->refLevel(), float(newRef));
            QCOMPARE(refSlider(menu)->value(), newRef);
            const int newDyn = (dynSlider(menu)->value() == 90) ? 80 : 90;
            dynSlider(menu)->setValue(newDyn);
            QCOMPARE(second->dynamicRange(), float(newDyn));
            QCOMPARE(dynSlider(menu)->value(), newDyn);
            QCOMPARE(second->refLevel(), float(newRef));
            QCOMPARE(asked.count(), 0);
            QCOMPARE(stored().dbMax, -42);
            QCOMPARE(stored().dbMin, -110);
            QCOMPARE(firstModel->perBandGrid(band).dbMax, -42);
            QCOMPARE(bandKeys(), keysBefore);
            QCOMPARE(pillMax->value(), -42);
            QCOMPARE(pillMin->value(), -110);
            menu->hide();
        }

        // Un-keyed, the band's stored receive range is back.
        second->setMoxOverlay(false);
        QCOMPARE(second->refLevel(), -42.0f);
        QCOMPARE(second->dynamicRange(), 68.0f);
        QCOMPARE(stored().dbMax, -42);
        QCOMPARE(stored().dbMin, -110);

        // ---- "Display settings..." ----
        // Acceptance: the line opens this pan's Display pill, and no other.
        QVERIFY(!firstPill->displayPillVisible());
        QVERIFY(!secondPill->displayPillVisible());
        QCOMPARE(openPopup(*second, QPoint(second->width() * 4 / 5, 40)), menu);
        QVERIFY(menu->isVisible());
        menu->findChild<QPushButton*>(QStringLiteral("displaySettingsButton"))->click();
        QVERIFY(!menu->isVisible());
        QVERIFY(secondPill->displayPillVisible());
        QVERIFY(!firstPill->displayPillVisible());
    }

    // Fix wave I4 (design 3.6, R-DSP-48): 3D Span and Slice Shadow are drawn
    // by the GPU mesh only, so each pan's pill follows whether that pan's
    // mesh is up, not just whether the build has the GPU path. This replaces
    // dssRowSpanSupportedStillPushedOnOpen, which pinned the same fact on
    // the old right-click menu's 3D Span row.
    void inTheWindow_pill3DRowsFollowThePansMesh()
    {
        Test::markAudioFirstRunDone();
        auto& s = AppSettings::instance();
        s.setValue(QStringLiteral("PanLayoutId"), QStringLiteral("1"));
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        window.resize(1280, 800);
        // Keep the synthetic readiness seam isolated from a shown
        // offscreen QRhiWidget, whose real no-QRhi lifecycle is allowed to
        // clear readiness asynchronously.
        QVERIFY(QMetaObject::invokeMethod(&window, "applyPanLayout",
                                          Q_ARG(QString, QStringLiteral("2v"))));
        PanadapterStack* stack = window.findChild<PanadapterStack*>();
        QVERIFY(stack);
        SpectrumWidget* first = stack->spectrum(QStringLiteral("pan-0"));
        SpectrumWidget* second = stack->spectrum(QStringLiteral("pan-1"));
        QVERIFY(first);
        QVERIFY(second);
        // These are 3D-only rows. Put both pans in 3D so mesh readiness is
        // the sole capability gate exercised below.
        first->setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode3D));
        second->setSpectrumRenderMode(static_cast<int>(SpectrumRenderMode::Mode3D));
        const auto span = [](SpectrumWidget* pan) {
            return pan->findChild<QSlider*>(QStringLiteral("pillDisplay3DSpan"));
        };
        const auto shadow = [](SpectrumWidget* pan) {
            return pan->findChild<QPushButton*>(QStringLiteral("pillDisplay3DSliceShadow"));
        };
        QVERIFY(span(first) && span(second) && shadow(first) && shadow(second));
        QCoreApplication::processEvents();

        // No renderer has built a mesh here (there is no GPU offscreen), so
        // neither pan can draw the two rows: greyed with the reason, never
        // hidden.
        QVERIFY(!first->dssMeshReady());
        QVERIFY(!second->dssMeshReady());
        for (SpectrumWidget* pan : {first, second}) {
            QVERIFY(!span(pan)->isEnabled());
            QVERIFY(!shadow(pan)->isEnabled());
            QVERIFY(!span(pan)->isHidden());
            QVERIFY(!shadow(pan)->isHidden());
            QVERIFY(!span(pan)->toolTip().isEmpty());
        }

        // The second pan's mesh comes up: its pill's rows follow (in a
        // CPU-only build the mesh never does, and they stay greyed). The
        // first pan's are its own.
        QSignalSpy moved(second, &SpectrumWidget::dssMeshReadyChanged);
        second->setDssMeshReadyForTest(true);
#ifdef NEREUS_GPU_SPECTRUM
        QVERIFY(second->dssMeshReady());
        QCOMPARE(moved.count(), 1);
        QCOMPARE(moved.first().at(0).toBool(), true);
        QTRY_VERIFY(span(second)->isEnabled());
        QVERIFY(shadow(second)->isEnabled());
#else
        QVERIFY(!second->dssMeshReady());
        QCOMPARE(moved.count(), 0);
        QCoreApplication::processEvents();
        QVERIFY(!span(second)->isEnabled());
        QVERIFY(!shadow(second)->isEnabled());
#endif
        QVERIFY(!span(first)->isEnabled());
        QVERIFY(!shadow(first)->isEnabled());

        // Told the same again, nothing is reported.
        second->setDssMeshReadyForTest(second->dssMeshReady());
        QCOMPARE(moved.count(), second->dssMeshReady() ? 1 : 0);

        // The mesh is lost (the renderer's resources released): greyed
        // again.
        second->setDssMeshReadyForTest(false);
        QVERIFY(!second->dssMeshReady());
        QTRY_VERIFY(!span(second)->isEnabled());
        QVERIFY(!shadow(second)->isEnabled());

        // The real shown-window state on a QRhi-unavailable test runner is
        // still covered: both controls remain present, disabled, and
        // explanatory after the renderer reports that no mesh exists.
        window.show();
        QCoreApplication::processEvents();
        QTRY_VERIFY(!first->dssMeshReady());
        QTRY_VERIFY(!second->dssMeshReady());
        for (SpectrumWidget* pan : {first, second}) {
            QTRY_VERIFY(!span(pan)->isEnabled());
            QVERIFY(!shadow(pan)->isEnabled());
            QVERIFY(!span(pan)->isHidden());
            QVERIFY(!shadow(pan)->isHidden());
            QVERIFY(!span(pan)->toolTip().isEmpty());
        }
        QVERIFY(!span(second)->isHidden());
    }
};

QTEST_MAIN(TestDisplayPopupBinding)
#include "tst_display_popup_binding.moc"
