// =================================================================
// tests/tst_spectrum_overlay_panel.cpp  (NereusSDR)
// =================================================================
//
// Smoke tests for SpectrumOverlayPanel::setRadioModel() — Phase 3O
// Sub-Phase 9 Task 9.2c (issue #70 fold-in).
//
// Coverage: the VAX Ch combo on the left-edge overlay is wired
// bidirectionally to slice 0's vaxChannel() with echo prevention.
// The IQ Ch combo stays disabled (feature-flagged per design spec
// §6.7/§11.3).
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QComboBox>

#include "core/AppSettings.h"
#include "gui/SpectrumOverlayPanel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TstSpectrumOverlayPanel : public QObject {
    Q_OBJECT

private:
    // SpectrumOverlayPanel parents its flyouts to `parentWidget()` (the
    // host SpectrumWidget in production — a bare QWidget in these tests).
    // Give it a host parent and search from there so findChild reaches the
    // combo's setObjectName("vaxCombo") / ("vaxIqCombo") in buildVaxFlyout.
    struct PanelHarness {
        QWidget host;
        SpectrumOverlayPanel* panel{nullptr};
        PanelHarness() {
            panel = new SpectrumOverlayPanel(&host);
        }
    };

    QComboBox* vaxCombo(PanelHarness& h) {
        return h.host.findChild<QComboBox*>(QStringLiteral("vaxCombo"));
    }

    QComboBox* vaxIqCombo(PanelHarness& h) {
        return h.host.findChild<QComboBox*>(QStringLiteral("vaxIqCombo"));
    }

private slots:

    void init() {
        AppSettings::instance().clear();
    }

    // ── 1. setRadioModel enables the VAX combo once slice 0 exists ─────
    void setRadioModelEnablesVaxCombo() {
        RadioModel radio;
        radio.addSlice();  // slice 0

        PanelHarness h;
        QComboBox* combo = vaxCombo(h);
        QVERIFY(combo);
        QVERIFY(!combo->isEnabled());  // unbound → disabled

        h.panel->setRadioModel(&radio);

        QVERIFY(combo->isEnabled());
        QVERIFY(combo->toolTip().contains("VAX", Qt::CaseInsensitive));
    }

    // ── 2. User picking an index writes through to slice 0 ─────────────
    void vaxComboWritesToSlice() {
        RadioModel radio;
        radio.addSlice();
        SliceModel* s = radio.sliceAt(0);
        QVERIFY(s);

        PanelHarness h;
        h.panel->setRadioModel(&radio);

        QComboBox* combo = vaxCombo(h);
        QVERIFY(combo);

        combo->setCurrentIndex(2);  // "2" → vaxChannel 2
        QCOMPARE(s->vaxChannel(), 2);

        combo->setCurrentIndex(0);  // back to Off
        QCOMPARE(s->vaxChannel(), 0);
    }

    // ── 3. Setting vaxChannel on the model echoes into the combo ───────
    void sliceUpdateEchoesToCombo() {
        RadioModel radio;
        radio.addSlice();
        SliceModel* s = radio.sliceAt(0);
        QVERIFY(s);

        PanelHarness h;
        h.panel->setRadioModel(&radio);

        QComboBox* combo = vaxCombo(h);
        QVERIFY(combo);

        s->setVaxChannel(3);
        QCOMPARE(combo->currentIndex(), 3);

        s->setVaxChannel(0);
        QCOMPARE(combo->currentIndex(), 0);
    }

    // ── 4. No feedback loop on echo (exactly one emission per setVax) ──
    void noFeedbackLoopOnEcho() {
        RadioModel radio;
        radio.addSlice();
        SliceModel* s = radio.sliceAt(0);
        QVERIFY(s);

        PanelHarness h;
        h.panel->setRadioModel(&radio);

        QSignalSpy spy(s, &SliceModel::vaxChannelChanged);

        s->setVaxChannel(2);

        // If the combo's currentIndexChanged re-entered into
        // setVaxChannel, we would see 2 emissions here (second one
        // would no-op at the guard in SliceModel::setVaxChannel, but
        // the sequence would still pass through the combo callback
        // twice). The m_updatingFromModel flag short-circuits the
        // widget→model path so exactly one emission reaches the spy.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
    }

    // ── 5. IQ Ch combo stays disabled (feature-flagged) ────────────────
    void iqComboRemainsDisabled() {
        RadioModel radio;
        radio.addSlice();

        PanelHarness h;
        h.panel->setRadioModel(&radio);

        QComboBox* iq = vaxIqCombo(h);
        QVERIFY(iq);
        QVERIFY(!iq->isEnabled());
        QVERIFY(iq->toolTip().contains("reserved for future phase",
                                       Qt::CaseInsensitive));
    }

    // ── 6. setRadioModel before any slice exists defers the bind ───────
    void setRadioModelDefersWhenNoSlice() {
        RadioModel radio;  // no slices yet

        PanelHarness h;
        h.panel->setRadioModel(&radio);

        QComboBox* combo = vaxCombo(h);
        QVERIFY(combo);
        QVERIFY(!combo->isEnabled());  // still disabled — waiting

        // Now add slice 0 — sliceAdded should trigger the deferred bind.
        radio.addSlice();

        QVERIFY(combo->isEnabled());

        // Sanity: forward path works after the deferred bind.
        SliceModel* s = radio.sliceAt(0);
        QVERIFY(s);
        combo->setCurrentIndex(4);
        QCOMPARE(s->vaxChannel(), 4);
    }
};

QTEST_MAIN(TstSpectrumOverlayPanel)
#include "tst_spectrum_overlay_panel.moc"
