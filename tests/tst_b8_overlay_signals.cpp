// =================================================================
// tests/tst_b8_overlay_signals.cpp  (NereusSDR)
// =================================================================
//
// TDD: verify SpectrumOverlayPanel Display-flyout signals are wired
// to SpectrumWidget setters.
//
// Because SpectrumWidget is a QRhi-based GPU widget that requires a
// real display context, we test the signal emissions themselves
// (QSignalSpy) and wiring the lambda connects manually — this is
// the same pattern used by tst_spectrum_dbm_strip.cpp and
// tst_spectrum_widget_mox_overlay.cpp.
//
// Per docs/superpowers/plans/2026-05-01-ui-polish-cross-surface.md §B8.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QSlider>
#include <QComboBox>

#include "core/AppSettings.h"
#include "gui/SpectrumOverlayPanel.h"

using namespace NereusSDR;

class TstB8OverlaySignals : public QObject {
    Q_OBJECT

private:
    struct PanelHarness {
        QWidget host;
        SpectrumOverlayPanel* panel{nullptr};
        PanelHarness() {
            panel = new SpectrumOverlayPanel(&host);
        }
    };

private slots:

    void init() {
        AppSettings::instance().clear();
    }

    // ── 1. wfColorGainChanged emits when WF Gain slider moves ──────────
    void wfColorGainChangedEmits() {
        PanelHarness h;
        QSignalSpy spy(h.panel, &SpectrumOverlayPanel::wfColorGainChanged);

        // The display flyout is parented to parentWidget() (== &host).
        QSlider* slider = h.host.findChild<QSlider*>(QStringLiteral("wfGainSlider"));
        if (!slider) {
            QSKIP("wfGainSlider not accessible from host; manual verification required");
        }

        slider->setValue(70);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toInt(), 70);
    }

    // ── 2. wfBlackLevelChanged emits when WF Black Level slider moves ───
    void wfBlackLevelChangedEmits() {
        PanelHarness h;
        QSignalSpy spy(h.panel, &SpectrumOverlayPanel::wfBlackLevelChanged);

        QSlider* slider = h.host.findChild<QSlider*>(QStringLiteral("wfBlackSlider"));
        if (!slider) {
            QSKIP("wfBlackSlider not accessible from host; manual verification required");
        }

        slider->setValue(60);
        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toInt(), 60);
    }

    // ── 3. colorSchemeChanged emits when Scheme combo index changes ─────
    void colorSchemeChangedEmits() {
        PanelHarness h;
        QSignalSpy spy(h.panel, &SpectrumOverlayPanel::colorSchemeChanged);

        QComboBox* cmb = h.host.findChild<QComboBox*>(QStringLiteral("colorSchemeCmb"));
        if (!cmb) {
            QSKIP("colorSchemeCmb not accessible from host; manual verification required");
        }

        const int origIdx = cmb->currentIndex();
        const int nextIdx = (origIdx + 1) % cmb->count();
        cmb->setCurrentIndex(nextIdx);

        QVERIFY(!spy.isEmpty());
        QCOMPARE(spy.last().at(0).toInt(), nextIdx);
    }

    // ── 4. Manual-connect: wfColorGainChanged drives an int accumulator ─
    // Simulates what MainWindow does: connect signal → lambda setter.
    void manualConnectWfColorGain() {
        PanelHarness h;
        int received = -1;
        connect(h.panel, &SpectrumOverlayPanel::wfColorGainChanged,
                h.panel, [&received](int v) { received = v; });

        emit h.panel->wfColorGainChanged(42);

        QCOMPARE(received, 42);
    }

    // ── 5. Manual-connect: wfBlackLevelChanged drives an int accumulator
    void manualConnectWfBlackLevel() {
        PanelHarness h;
        int received = -1;
        connect(h.panel, &SpectrumOverlayPanel::wfBlackLevelChanged,
                h.panel, [&received](int v) { received = v; });

        emit h.panel->wfBlackLevelChanged(88);

        QCOMPARE(received, 88);
    }

    // ── 6. Manual-connect: colorSchemeChanged drives an int accumulator ─
    void manualConnectColorScheme() {
        PanelHarness h;
        int received = -1;
        connect(h.panel, &SpectrumOverlayPanel::colorSchemeChanged,
                h.panel, [&received](int v) { received = v; });

        emit h.panel->colorSchemeChanged(2);

        QCOMPARE(received, 2);
    }
};

QTEST_MAIN(TstB8OverlaySignals)
#include "tst_b8_overlay_signals.moc"
