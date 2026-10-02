// no-port-check: NereusSDR-original test file.  All Thetis source cites
// for the underlying TransmitModel properties live in TransmitModel.h
// and the dialog source itself.
// =================================================================
// tests/tst_tx_eq_dialog.cpp  (NereusSDR)
// =================================================================
//
// Phase 3M-3a-i Batch 3 (Task A.1) — TxEqDialog scaffold smoke tests.
// Phase 3M-3a-ii follow-up Batch 9 — chkLegacyEQ + parametric panel
// + slider styling fix.
//
// TxEqDialog is the modeless TX EQ dialog launched from the
// TxApplet's [EQ] right-click and the Tools → TX Equalizer menu.
// The legacy panel is bidirectionally bound to RadioModel::transmit-
// Model() with an m_updatingFromModel echo guard.  The parametric
// panel embeds a ParametricEqWidget (Tasks 1-5) and round-trips its
// points through TransmitModel.txEqParaEqData (Task 6 Thetis gzip+
// base64url envelope).
//
// Tests:
//   1. Dialog constructs without crash (RadioModel default ctor).
//   2. Initial values populate from TransmitModel defaults
//      (preamp=0, band[0]=-12, freq[0]=32, enable=false, Nc=2048).
//   3. Move preamp slider → TransmitModel.txEqPreampChanged emitted.
//   4. Move band 0 slider → TransmitModel.txEqBandChanged emitted with
//      idx=0 + new value.
//   5. Move freq 0 spinbox → TransmitModel.txEqFreqChanged emitted.
//   6. Toggle enable checkbox → TransmitModel.txEqEnabledChanged emitted.
//   7. setTxEqPreamp(N) external setter → dialog preamp slider/spin
//      updates to N (round-trip via syncFromModel).
//   8. Echo guard: setting a TransmitModel value that triggers UI
//      update doesn't cause a re-emit storm (no infinite loop —
//      each setter only fires its own signal once).
//   9. Singleton: TxEqDialog::instance(...) returns the same pointer
//      on repeated calls.
//
// Batch 9 contracts:
//  10. Dialog contains chkLegacyEQ checkbox at top.
//  11. Toggling chkLegacyEQ flips the visible panel between legacy
//      sliders and the parametric panel via QStackedWidget.
//  12. Parametric panel embeds a ParametricEqWidget instance.
//  13. Parametric panel exposes 5/10/18 band-count radios.
//  14. Dialog does NOT contain a profile combo (profile mgmt lives
//      on TxApplet).
//  15. Dialog does NOT contain Save / Save As / Delete buttons.
//  16. Legacy band-column sliders carry the Style::sliderVStyle()
//      stylesheet (regression guard).
//  17. closeEvent hides the dialog instead of destroying it
//      (keeps the singleton alive for fast re-show).
//
// R-IOS-13 / R-R3-49 (2026-09-28, J.J. Boyd (KG4VCF), AI-assisted via
// Anthropic Claude Code):
//  18. transmit.txEqCurve equals the parametric panel for every factory
//      profile, a custom curve, an out-of-order curve and odd values
//      (unrounded, missing fields, out of range); an unreadable
//      value is "unavailable" while the panel shows the Core's fallback.
//
// =================================================================

#include <QtTest/QtTest>
#include <cmath>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QComboBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>

#include "core/AppSettings.h"
#include "core/MicProfileManager.h"
#include "core/ParaEqCurve.h"
#include "core/ParaEqEnvelope.h"
#include "gui/applets/TxEqDialog.h"
#include "gui/widgets/ParametricEqWidget.h"
#include "models/RadioModel.h"
#include "models/TransmitModel.h"

using namespace NereusSDR;

namespace {

// A custom five-point curve with Q factors off, as the dialog's widget
// saves one (values already at the panel's rounding).
QString customCurveJson()
{
    QJsonArray pts;
    const double f[] = {80.0, 400.0, 1250.5, 2200.0, 3100.0};
    const double g[] = {-8.5, 2.0, 0.0, 6.5, -3.0};
    const double q[] = {1.25, 3.0, 4.0, 2.5, 6.0};
    for (int i = 0; i < 5; ++i) {
        pts.append(QJsonObject{{QStringLiteral("frequency_hz"), f[i]},
                               {QStringLiteral("gain_db"), g[i]},
                               {QStringLiteral("q"), q[i]}});
    }
    const QJsonObject root{{QStringLiteral("band_count"), 5},
                           {QStringLiteral("parametric_eq"), false},
                           {QStringLiteral("global_gain_db"), -1.5},
                           {QStringLiteral("frequency_min_hz"), 80.0},
                           {QStringLiteral("frequency_max_hz"), 3100.0},
                           {QStringLiteral("points"), pts}};
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented));
}

QJsonObject curveOf(const TransmitModel& tx)
{
    return QJsonDocument::fromJson(tx.txEqCurve().toUtf8()).object();
}

// Half of the 0.001 Hz step a saved frequency is rounded to.
constexpr double kSavedFreqToleranceHz = 0.0005;

// The curve's every field against what the widget holds and draws.
// freqToleranceHz > 0 is for a curve the dialog saved after a range
// change: the saved frequencies are the widget's rounded to 0.001 Hz
// (Thetis ucParametricEq.cs:1474 [v2.10.3.15], SaveToJson, which the
// widget's saveToJson ports; SaveToJsonFromPoints rounds the same way at
// ucParametricEq.cs:1375), and a rescale leaves the widget's unrounded.
void compareToWidget(const QJsonObject& curve, const ParametricEqWidget* w,
                     double freqToleranceHz = 0.0)
{
    QVERIFY(w);
    QCOMPARE(curve.value(QStringLiteral("parametric")).toBool(), w->parametricEq());
    QCOMPARE(curve.value(QStringLiteral("preampDb")).toDouble(), w->globalGainDb());
    QCOMPARE(curve.value(QStringLiteral("minHz")).toDouble(), w->frequencyMinHz());
    QCOMPARE(curve.value(QStringLiteral("maxHz")).toDouble(), w->frequencyMaxHz());
    const QJsonArray points = curve.value(QStringLiteral("points")).toArray();
    QCOMPARE(points.size(), w->points().size());
    for (int i = 0; i < points.size(); ++i) {
        const QJsonObject p = points.at(i).toObject();
        const ParametricEqWidget::EqPoint& e = w->points().at(i);
        if (freqToleranceHz > 0.0) {
            QVERIFY2(std::abs(p.value(QStringLiteral("frequencyHz")).toDouble()
                              - e.frequencyHz) <= freqToleranceHz,
                     qPrintable(QStringLiteral("point %1").arg(i)));
        } else {
            QCOMPARE(p.value(QStringLiteral("frequencyHz")).toDouble(), e.frequencyHz);
        }
        QCOMPARE(p.value(QStringLiteral("gainDb")).toDouble(), e.gainDb);
        QCOMPARE(p.value(QStringLiteral("q")).toDouble(), e.q);
    }
}

} // namespace

class TestTxEqDialog : public QObject {
    Q_OBJECT

private slots:

    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
        AppSettings::instance().clear();
    }

    void cleanup()
    {
        AppSettings::instance().clear();
    }

    // ── 1. Construct ────────────────────────────────────────────────
    void constructsWithoutCrash()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        QVERIFY(dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk")));
        QVERIFY(dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider")));
    }

    // ── 2. Initial values populate from TransmitModel defaults ─────
    void initialValues_matchTransmitModelDefaults()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        TransmitModel& tx = rm.transmitModel();

        // Enable default off.
        auto* en = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk"));
        QVERIFY(en);
        QCOMPARE(en->isChecked(), tx.txEqEnabled());
        QCOMPARE(en->isChecked(), false);

        // Preamp default 0.
        auto* pre = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        auto* preSpin = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqPreampSpin"));
        QVERIFY(pre);
        QVERIFY(preSpin);
        QCOMPARE(pre->value(), tx.txEqPreamp());
        QCOMPARE(preSpin->value(), tx.txEqPreamp());
        QCOMPARE(pre->value(), 0);

        // Band 0 default -12 (matches TransmitModel m_txEqBand init).
        auto* b0 = dlg.findChild<QSlider*>(QStringLiteral("TxEqBandSlider0"));
        auto* b0s = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqBandSpin0"));
        QVERIFY(b0);
        QVERIFY(b0s);
        QCOMPARE(b0->value(), tx.txEqBand(0));
        QCOMPARE(b0s->value(), tx.txEqBand(0));
        QCOMPARE(b0->value(), -12);

        // Freq 0 default 32 Hz.
        auto* f0 = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqFreqSpin0"));
        QVERIFY(f0);
        QCOMPARE(f0->value(), tx.txEqFreq(0));
        QCOMPARE(f0->value(), 32);

        // Nc default 2048.
        auto* nc = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqNcSpin"));
        QVERIFY(nc);
        QCOMPARE(nc->value(), tx.txEqNc());
        QCOMPARE(nc->value(), 2048);

        // Mp default off.
        auto* mp = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqMpChk"));
        QVERIFY(mp);
        QCOMPARE(mp->isChecked(), tx.txEqMp());
        QCOMPARE(mp->isChecked(), false);

        // Ctfmode default 0, Wintype default 0.
        auto* ctf = dlg.findChild<QComboBox*>(QStringLiteral("TxEqCtfmodeCombo"));
        auto* win = dlg.findChild<QComboBox*>(QStringLiteral("TxEqWintypeCombo"));
        QVERIFY(ctf);
        QVERIFY(win);
        QCOMPARE(ctf->currentIndex(), tx.txEqCtfmode());
        QCOMPARE(win->currentIndex(), tx.txEqWintype());
        QCOMPARE(ctf->currentIndex(), 0);
        QCOMPARE(win->currentIndex(), 0);
    }

    // ── 3. Preamp slider → txEqPreampChanged ────────────────────────
    void preampSlider_emitsTxEqPreampChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqPreampChanged);

        auto* pre = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        QVERIFY(pre);
        pre->setValue(7);

        // Slider emits valueChanged → onPreampChanged → setTxEqPreamp → signal.
        // Note: the model→UI sync handler also fires syncFromModel which
        // re-sets the spinbox; but setTxEqPreamp itself only fires once.
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toInt(), 7);
        QCOMPARE(tx.txEqPreamp(), 7);
    }

    // ── 4. Band 0 slider → txEqBandChanged with idx=0 ───────────────
    void band0Slider_emitsTxEqBandChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqBandChanged);

        auto* b0 = dlg.findChild<QSlider*>(QStringLiteral("TxEqBandSlider0"));
        QVERIFY(b0);
        b0->setValue(5);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 0);
        QCOMPARE(args.at(1).toInt(), 5);
        QCOMPARE(tx.txEqBand(0), 5);
    }

    // ── 5. Freq 0 spinbox → txEqFreqChanged with idx=0 ──────────────
    void freq0Spin_emitsTxEqFreqChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqFreqChanged);

        auto* f0 = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqFreqSpin0"));
        QVERIFY(f0);
        f0->setValue(75);

        QCOMPARE(spy.count(), 1);
        const QList<QVariant> args = spy.takeFirst();
        QCOMPARE(args.at(0).toInt(), 0);
        QCOMPARE(args.at(1).toInt(), 75);
        QCOMPARE(tx.txEqFreq(0), 75);
    }

    // ── 6. Enable checkbox → txEqEnabledChanged ─────────────────────
    void enableCheckbox_emitsTxEqEnabledChanged()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqEnabledChanged);

        auto* en = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqEnableChk"));
        QVERIFY(en);
        en->setChecked(true);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.takeFirst().at(0).toBool(), true);
        QCOMPARE(tx.txEqEnabled(), true);
    }

    // ── 7. External setTxEqPreamp(N) → dialog UI updates ────────────
    void externalSetTxEqPreamp_updatesDialogPreamp()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();

        auto* pre     = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        auto* preSpin = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqPreampSpin"));
        QVERIFY(pre && preSpin);
        QCOMPARE(pre->value(), 0);

        tx.setTxEqPreamp(11);
        QCOMPARE(pre->value(), 11);
        QCOMPARE(preSpin->value(), 11);
    }

    // ── 8. Echo guard — model setter fires signal exactly once ──────
    // If the echo guard were missing, the slider valueChanged from
    // syncFromModel would call back into setTxEqPreamp, which would
    // emit again, etc.  Verify the signal count stays at 1.
    void echoGuard_externalSetterDoesNotReEmit()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        QSignalSpy spy(&tx, &TransmitModel::txEqPreampChanged);

        tx.setTxEqPreamp(4);   // single emit expected
        QCOMPARE(spy.count(), 1);

        tx.setTxEqPreamp(4);   // no-op — value unchanged, no re-emit
        QCOMPARE(spy.count(), 1);
    }

    // ── 9. Singleton — instance() returns same pointer ──────────────
    void singleton_returnsSameInstance()
    {
        RadioModel rm;
        TxEqDialog* a = TxEqDialog::instance(&rm);
        TxEqDialog* b = TxEqDialog::instance(&rm);
        QVERIFY(a != nullptr);
        QCOMPARE(a, b);
        // Cleanup — the singleton survives across tests, so delete it
        // explicitly to avoid leaks across test-method boundaries.
        delete a;
    }

    // =====================================================================
    // Phase 3M-3a-ii follow-up Batch 9 — chkLegacyEQ + parametric panel
    // =====================================================================

    // ── 10. Dialog contains chkLegacyEQ checkbox ───────────────────
    void dialogContainsLegacyToggle()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        auto* tog = dlg.findChild<QCheckBox*>(QStringLiteral("TxEqLegacyToggle"));
        QVERIFY(tog);
        QCOMPARE(tog, dlg.legacyToggle());
        // Default checked (per Thetis eqform.cs:972-973 [v2.10.3.13]).
        QCOMPARE(tog->isChecked(), true);
    }

    // ── 11. Toggling chkLegacyEQ flips the visible panel ────────────
    void legacyTogglesBetweenLegacyAndParametricPanels()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        auto* stack = dlg.panelStack();
        QVERIFY(stack);
        QVERIFY(dlg.legacyPanel());
        QVERIFY(dlg.parametricPanel());

        // Default: legacy shown (index 0).
        QCOMPARE(dlg.legacyToggle()->isChecked(), true);
        QCOMPARE(stack->currentWidget(), dlg.legacyPanel());

        // Uncheck — parametric panel shown.
        dlg.legacyToggle()->setChecked(false);
        QCOMPARE(stack->currentWidget(), dlg.parametricPanel());

        // Re-check — legacy panel shown.
        dlg.legacyToggle()->setChecked(true);
        QCOMPARE(stack->currentWidget(), dlg.legacyPanel());
    }

    // ── 11b. chkLegacyEQ state persists in AppSettings ──────────────
    void legacyToggleStatePersistsInAppSettings()
    {
        RadioModel rm;
        {
            TxEqDialog dlg(&rm);
            QCOMPARE(dlg.legacyToggle()->isChecked(), true);
            dlg.legacyToggle()->setChecked(false);
            // Persistence is synchronous via setValue.
        }

        // Reconstruct — the new dialog should pick up the persisted False.
        {
            TxEqDialog dlg2(&rm);
            QCOMPARE(dlg2.legacyToggle()->isChecked(), false);
            QCOMPARE(dlg2.panelStack()->currentWidget(), dlg2.parametricPanel());
        }
    }

    // ── 12. Parametric panel embeds ParametricEqWidget ──────────────
    void parametricPanelContainsParametricEqWidget()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        auto* w = dlg.parametricWidget();
        QVERIFY(w);
        QVERIFY(dlg.findChild<ParametricEqWidget*>(
                   QStringLiteral("TxEqParametricWidget")));
        // Limits match Thetis ucParametricEq1 widget property block
        // at eqform.cs:928-967 [v2.10.3.13]. The range is the curve the
        // model's empty txEqParaEqData stands for: GetDefaults' 0 to 4000
        // Hz, as Thetis's ParaEQTXData setter loads it
        // (eqform.cs:3312-3317 [v2.10.3.15]), not the designer's 2700.
        QCOMPARE(w->dbMin(),         -24.0);
        QCOMPARE(w->dbMax(),          24.0);
        QCOMPARE(w->frequencyMinHz(),  0.0);
        QCOMPARE(w->frequencyMaxHz(), 4000.0);
        QCOMPARE(w->qMin(),            0.2);
        QCOMPARE(w->qMax(),           20.0);
        QCOMPARE(w->bandCount(),      10);
        QCOMPARE(w->parametricEq(),   true);
    }

    // ── 13. Parametric panel exposes 5/10/18 band-count radios ──────
    void parametricPanelContainsBandCountRadios_5_10_18()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        auto* r5  = dlg.findChild<QRadioButton*>(
                       QStringLiteral("TxEqParaBands5Radio"));
        auto* r10 = dlg.findChild<QRadioButton*>(
                       QStringLiteral("TxEqParaBands10Radio"));
        auto* r18 = dlg.findChild<QRadioButton*>(
                       QStringLiteral("TxEqParaBands18Radio"));
        QVERIFY(r5);
        QVERIFY(r10);
        QVERIFY(r18);
        // Default 10-band (per Thetis eqform.cs:507 radParaEQ_10.Checked = true).
        QCOMPARE(r10->isChecked(), true);
        QCOMPARE(r5->isChecked(),  false);
        QCOMPARE(r18->isChecked(), false);

        // Group exposes the band counts as button IDs.
        auto* grp = dlg.bandCountGroup();
        QVERIFY(grp);
        QCOMPARE(grp->id(r5),  5);
        QCOMPARE(grp->id(r10), 10);
        QCOMPARE(grp->id(r18), 18);
    }

    // ── 14. Dialog does NOT contain a profile combo ─────────────────
    void dialogDoesNotContainProfileCombo()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        // No combo with the old TxEqProfileCombo object name.
        QVERIFY(!dlg.findChild<QComboBox*>(QStringLiteral("TxEqProfileCombo")));

        // The only QComboBoxes in the dialog should be the WDSP filter
        // controls (Cutoff, Window) — exactly two, no more.
        const auto combos = dlg.findChildren<QComboBox*>();
        QCOMPARE(combos.size(), 2);
        QStringList names;
        for (auto* c : combos) { names << c->objectName(); }
        QVERIFY2(names.contains(QStringLiteral("TxEqCtfmodeCombo")),
                 qPrintable(names.join(QStringLiteral(", "))));
        QVERIFY2(names.contains(QStringLiteral("TxEqWintypeCombo")),
                 qPrintable(names.join(QStringLiteral(", "))));
    }

    // ── 15. Dialog does NOT contain Save / Save As / Delete buttons ─
    void dialogDoesNotContainSaveSaveAsDelete()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        QVERIFY(!dlg.findChild<QPushButton*>(QStringLiteral("TxEqProfileSaveBtn")));
        QVERIFY(!dlg.findChild<QPushButton*>(QStringLiteral("TxEqProfileSaveAsBtn")));
        QVERIFY(!dlg.findChild<QPushButton*>(QStringLiteral("TxEqProfileDeleteBtn")));

        // Sanity: no QPushButton in the dialog has the strings "Save" /
        // "Save As" / "Delete" as its caption.
        const auto btns = dlg.findChildren<QPushButton*>();
        for (auto* b : btns) {
            const QString t = b->text();
            QVERIFY2(t != QStringLiteral("Save"),
                     qPrintable(QStringLiteral("rogue Save button: ") + b->objectName()));
            QVERIFY2(t != QStringLiteral("Save As..."),
                     qPrintable(QStringLiteral("rogue Save As... button: ") + b->objectName()));
            QVERIFY2(t != QStringLiteral("Delete"),
                     qPrintable(QStringLiteral("rogue Delete button: ") + b->objectName()));
        }
    }

    // ── 16. Legacy band-column slider style includes "QSlider" ──────
    // Regression guard for Batch 9's slider/spinbox styling fix —
    // confirms the per-column sliders pick up the project's vertical
    // slider stylesheet.  Style::sliderVStyle() always emits a
    // QSlider::groove:vertical / QSlider::handle:vertical block, so
    // any non-default project styling will contain "QSlider" in the
    // applied stylesheet.
    void legacyBandColumnSlidersUseSliderVStyle()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);

        // Pick a representative band slider.
        auto* b0 = dlg.findChild<QSlider*>(QStringLiteral("TxEqBandSlider0"));
        QVERIFY(b0);
        const QString css = b0->styleSheet();
        QVERIFY2(css.contains(QStringLiteral("QSlider")),
                 qPrintable(QStringLiteral("band slider stylesheet missing: ") + css));
        QVERIFY2(css.contains(QStringLiteral("vertical")),
                 qPrintable(QStringLiteral("band slider stylesheet missing 'vertical': ") + css));

        // Spinboxes too (kSpinBoxStyle has "QSpinBox" in the rule head).
        auto* b0s = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqBandSpin0"));
        QVERIFY(b0s);
        const QString spinCss = b0s->styleSheet();
        QVERIFY2(spinCss.contains(QStringLiteral("QSpinBox")),
                 qPrintable(QStringLiteral("band db spinbox stylesheet missing: ") + spinCss));

        auto* f0 = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqFreqSpin0"));
        QVERIFY(f0);
        QVERIFY2(f0->styleSheet().contains(QStringLiteral("QSpinBox")),
                 qPrintable(QStringLiteral("band freq spinbox stylesheet missing")));

        // Preamp column too (special case: bandIndex=-1).
        auto* pre = dlg.findChild<QSlider*>(QStringLiteral("TxEqPreampSlider"));
        QVERIFY(pre);
        QVERIFY(pre->styleSheet().contains(QStringLiteral("QSlider")));
    }

    // ── 16b. Parametric edit stores Thetis gzip/base64url envelope ────
    //         AND leaves legacy scalar fields untouched.
    void parametricEditEncodesEnvelopeAndPreservesLegacyScalars()
    {
        RadioModel rm;
        TransmitModel& tx = rm.transmitModel();

        // Capture legacy field defaults BEFORE the dialog constructs,
        // since TransmitModel's defaults aren't necessarily zero
        // (txEqBand defaults to -12 at the slider min, for example).
        const int defaultPreamp = tx.txEqPreamp();
        std::array<int, 10> defaultBands;
        for (int i = 0; i < 10; ++i) {
            defaultBands[i] = tx.txEqBand(i);
        }

        TxEqDialog dlg(&rm);
        ParametricEqWidget* w = dlg.parametricWidget();
        QVERIFY(w);

        w->setGlobalGainDb(3.0);

        // 1. Blob is encoded (gzip+base64url envelope, not raw JSON) and
        //    decodes back to JSON containing the new global gain --
        //    Codex P1 #1 envelope-encoding fix from f5b24ef.
        const QString blob = tx.txEqParaEqData();
        QVERIFY(!blob.isEmpty());
        QVERIFY(!blob.trimmed().startsWith(QLatin1Char('{')));

        const std::optional<QString> decoded = ParaEqEnvelope::decode(blob);
        QVERIFY(decoded.has_value());
        QVERIFY(decoded->contains(QStringLiteral("\"global_gain_db\": 3")));

        // 2. Legacy scalar fields stay at their pre-edit values --
        //    parametric edits push the curve directly to WDSP via
        //    TxChannel::setTxEqProfile (see 9e6de26 commit message), NOT
        //    via the legacy txEqPreamp/txEqBand setter chain.  Mutating
        //    legacy scalars here would corrupt the user's legacy-mode
        //    settings on toggle-back to chkLegacyEQ.  The earlier
        //    dd03b70 approach DID push to legacy scalars and lost
        //    parametric precision (4.6 dB rounded to 5) plus discarded
        //    parametric band centers (sampled at the legacy ISO grid).
        QCOMPARE(tx.txEqPreamp(), defaultPreamp);
        for (int i = 0; i < 10; ++i) {
            QCOMPARE(tx.txEqBand(i), defaultBands[i]);
        }
    }

    // ── 16c. Encoded model blob hydrates the parametric widget ─────────
    void encodedTxParaEqDataHydratesParametricWidget()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        ParametricEqWidget* w = dlg.parametricWidget();
        QVERIFY(w);
        QCOMPARE(w->globalGainDb(), 0.0);

        ParametricEqWidget saved;
        saved.setGlobalGainDb(7.0);
        const QString blob = ParaEqEnvelope::encode(saved.saveToJson());
        QVERIFY(!blob.isEmpty());

        tx.setTxEqParaEqData(blob);
        QCOMPARE(w->globalGainDb(), 7.0);
    }

    // ── 18. R-IOS-13 / R-R3-49: the curve on the link is the curve the
    //        dialog shows (transmit.txEqCurve, ParaEqCurve::txEqCurveJson)
    //        for every factory profile, a custom curve, an out-of-order
    //        curve, and says "unavailable" where the panel falls back.
    void curveMatchesDialogForEveryFactoryProfile()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        ParametricEqWidget* w = dlg.parametricWidget();
        QVERIFY(w);

        MicProfileManager mgr;
        mgr.setMacAddress(QStringLiteral("aa:bb:cc:dd:ee:18"));
        mgr.load();
        const QStringList names = mgr.profileNames();
        QVERIFY(names.size() >= 22);
        for (const QString& name : names) {
            // A custom curve first, so each profile's value has to move
            // the dialog.
            tx.setTxEqParaEqData(ParaEqEnvelope::encode(customCurveJson()));
            QVERIFY(mgr.setActiveProfile(name, &tx));
            // Every factory profile saves an empty TXParaEQData
            // (database.cs AddTXProfileTable [v2.10.3.15]).
            QCOMPARE(tx.txEqParaEqData(), QString());
            const QJsonObject curve = curveOf(tx);
            QCOMPARE(curve.value(QStringLiteral("state")).toString(), QStringLiteral("default"));
            compareToWidget(curve, w);
            if (QTest::currentTestFailed()) {
                qWarning() << "profile" << name;
                return;
            }
        }
    }

    void curveMatchesDialogForACustomCurve()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(customCurveJson()));
        const QJsonObject curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("state")).toString(), QStringLiteral("saved"));
        QCOMPARE(curve.value(QStringLiteral("points")).toArray().size(), 5);
        compareToWidget(curve, dlg.parametricWidget());
        // The panel's controls follow the loaded curve (setParaEQData).
        QCOMPARE(dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"))->value(), 80);
        QCOMPARE(dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"))->value(), 3100);
        QCOMPARE(dlg.findChild<QRadioButton*>(QStringLiteral("TxEqParaBands5Radio"))->isChecked(),
                 true);
        QCOMPARE(dlg.findChild<QCheckBox*>(QStringLiteral("TxEqParaUseQFactorsChk"))->isChecked(),
                 false);
        // Loading moved nothing back into the model.
        QCOMPARE(tx.txEqParaEqData(), ParaEqEnvelope::encode(customCurveJson()));
    }

    void curveMatchesDialogForAnOutOfOrderCurve()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        // Saved out of order, two points 2 Hz apart, an 18-band count:
        // the panel sorts and spaces them (enforceOrdering).
        QJsonArray pts;
        for (int i = 0; i < 18; ++i) {
            const double f = i == 5 ? 902.0 : i == 6 ? 900.0 : 150.0 * i;
            pts.append(QJsonObject{{QStringLiteral("frequency_hz"), f},
                                   {QStringLiteral("gain_db"), (i % 5) - 2.0},
                                   {QStringLiteral("q"), 1.0 + i}});
        }
        const QJsonObject root{{QStringLiteral("band_count"), 18},
                               {QStringLiteral("parametric_eq"), true},
                               {QStringLiteral("global_gain_db"), 1.5},
                               {QStringLiteral("frequency_min_hz"), 0.0},
                               {QStringLiteral("frequency_max_hz"), 2550.0},
                               {QStringLiteral("points"), pts}};
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(
            QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact))));
        const QJsonObject curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("state")).toString(), QStringLiteral("saved"));
        const QJsonArray shown = curve.value(QStringLiteral("points")).toArray();
        QCOMPARE(shown.at(5).toObject().value(QStringLiteral("frequencyHz")).toDouble(), 900.0);
        QCOMPARE(shown.at(6).toObject().value(QStringLiteral("frequencyHz")).toDouble(), 905.0);
        compareToWidget(curve, dlg.parametricWidget());
        QCOMPARE(dlg.findChild<QRadioButton*>(QStringLiteral("TxEqParaBands18Radio"))->isChecked(),
                 true);
    }

    // Odd saved values: the panel and the Core's curve read them by one
    // parser (ParaEqCurve), so they agree on rounding, missing fields and
    // out-of-range points. Each value is loaded after a different curve,
    // so nothing is left over from the widget's earlier state.
    void curveMatchesDialogForOddValues()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        const QString odd[] = {
            // Unrounded: PointsFromJson rounds F to 0.001 Hz, G and the
            // preamp to 0.1 dB, Q to 0.01.
            QStringLiteral(R"({"band_count":5,"parametric_eq":true,"global_gain_db":2.345,)"
                           R"("frequency_min_hz":20.00049,"frequency_max_hz":3333.3333,)"
                           R"("points":[{"frequency_hz":20,"gain_db":1.26,"q":7.777},)"
                           R"({"frequency_hz":500.0004,"gain_db":-3.14159,"q":1.234},)"
                           R"({"frequency_hz":1234.5678,"gain_db":0.05,"q":2.005},)"
                           R"({"frequency_hz":2000.0006,"gain_db":0.15,"q":3.335},)"
                           R"({"frequency_hz":3333,"gain_db":-0.25,"q":9.999}]})"),
            // Missing fields: no frequency_min_hz (0), parametric_eq,
            // band_count or global_gain_db; a point with only a frequency.
            QStringLiteral(R"({"frequency_max_hz":1800,)"
                           R"("points":[{"frequency_hz":100,"gain_db":3,"q":2},)"
                           R"({"frequency_hz":700},)"
                           R"({"gain_db":-5,"q":4},)"
                           R"({"frequency_hz":1800,"gain_db":1,"q":1}]})"),
            // Out of range: a point above the range, one below, gains and
            // Qs past the panel's limits, the preamp too.
            QStringLiteral(R"({"band_count":5,"parametric_eq":true,"global_gain_db":50,)"
                           R"("frequency_min_hz":100,"frequency_max_hz":2600,)"
                           R"("points":[{"frequency_hz":100,"gain_db":40,"q":0},)"
                           R"({"frequency_hz":9000,"gain_db":-40,"q":99},)"
                           R"({"frequency_hz":-50,"gain_db":12,"q":0.1},)"
                           R"({"frequency_hz":1300,"gain_db":24.04,"q":20.004},)"
                           R"({"frequency_hz":2600,"gain_db":-24.06,"q":0.199}]})"),
            // A range wholly above the panel's previous one.
            QStringLiteral(R"({"band_count":3,"parametric_eq":true,"global_gain_db":0,)"
                           R"("frequency_min_hz":5000,"frequency_max_hz":9000,)"
                           R"("points":[{"frequency_hz":5000,"gain_db":1,"q":1},)"
                           R"({"frequency_hz":7000,"gain_db":2,"q":2},)"
                           R"({"frequency_hz":9000,"gain_db":3,"q":3}]})"),
        };
        for (const QString& json : odd) {
            tx.setTxEqParaEqData(ParaEqEnvelope::encode(customCurveJson()));
            tx.setTxEqParaEqData(ParaEqEnvelope::encode(json));
            const QJsonObject curve = curveOf(tx);
            QCOMPARE(curve.value(QStringLiteral("state")).toString(), QStringLiteral("saved"));
            compareToWidget(curve, dlg.parametricWidget());
            if (QTest::currentTestFailed()) {
                qWarning() << json;
                return;
            }
        }
        // Spot checks on the last three, worked from PointsFromJson.
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(odd[0]));
        QJsonObject c = curveOf(tx);
        QCOMPARE(c.value(QStringLiteral("preampDb")).toDouble(), 2.3);
        QCOMPARE(c.value(QStringLiteral("minHz")).toDouble(), 20.0);
        QCOMPARE(c.value(QStringLiteral("points")).toArray().at(1).toObject()
                     .value(QStringLiteral("gainDb")).toDouble(), -3.1);
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(odd[1]));
        c = curveOf(tx);
        QCOMPARE(c.value(QStringLiteral("minHz")).toDouble(), 0.0);
        QCOMPARE(c.value(QStringLiteral("parametric")).toBool(), false);
        // The point with no frequency reads 0 Hz and sorts second, moved
        // to 5 Hz; the one with only a frequency (700 Hz) is third, gain
        // 0 and Q 0 clamped to 0.2.
        QCOMPARE(c.value(QStringLiteral("points")).toArray().at(1).toObject()
                     .value(QStringLiteral("frequencyHz")).toDouble(), 5.0);
        QCOMPARE(c.value(QStringLiteral("points")).toArray().at(2).toObject()
                     .value(QStringLiteral("q")).toDouble(), 0.2);
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(odd[2]));
        c = curveOf(tx);
        QCOMPARE(c.value(QStringLiteral("preampDb")).toDouble(), 24.0);
        QCOMPARE(c.value(QStringLiteral("points")).toArray().at(0).toObject()
                     .value(QStringLiteral("gainDb")).toDouble(), 24.0);
        QCOMPARE(dlg.parametricWidget()->frequencyMinHz(), 100.0);
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(odd[3]));
        QCOMPARE(dlg.parametricWidget()->frequencyMinHz(), 5000.0);
        QCOMPARE(dlg.parametricWidget()->frequencyMaxHz(), 9000.0);
    }

    void unreadableCurveIsUnavailableAndDialogShowsTheCoresFallback()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        tx.setTxEqParaEqData(ParaEqEnvelope::encode(customCurveJson()));
        tx.setTxEqParaEqData(QStringLiteral("not a curve"));
        QCOMPARE(tx.txEqCurve(), QStringLiteral("{\"state\":\"unavailable\"}"));
        // The panel shows what the Core applies in its place, Thetis's
        // GetDefaults (eqform.cs:3312-3315 [v2.10.3.15]), not the
        // previous curve.
        compareToWidget(QJsonDocument::fromJson(
                            ParaEqCurve::txEqCurveJson(QString()).toUtf8()).object(),
                        dlg.parametricWidget());
    }

    // ── 16d. Low / High spread guard ───────────────────────────────
    // From Thetis eqform.cs:3539-3577 [v2.10.3.15]: a Low within 1000 Hz
    // of High is set to High - 1000 with the handler still attached, so
    // ValueChanged re-fires and the clamped value reaches the curve
    // (FrequencyMinHz), whose PointsChanged stores the rescaled points.
    void lowWithinSpreadClampsAndReachesCurve()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        ParametricEqWidget* w = dlg.parametricWidget();
        auto* low  = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"));
        auto* high = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"));
        QVERIFY(w && low && high);
        high->setValue(3000);
        QCOMPARE(w->frequencyMaxHz(), 3000.0);

        QSignalSpy spy(&tx, &TransmitModel::txEqParaEqDataChanged);
        low->setValue(2500);
        QCOMPARE(low->value(), 2000);
        QCOMPARE(w->frequencyMinHz(), 2000.0);
        QCOMPARE(spy.count(), 1);
        const QJsonObject curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("minHz")).toDouble(), 2000.0);
        compareToWidget(curve, w, kSavedFreqToleranceHz);
    }

    void highWithinSpreadClampsAndReachesCurve()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        ParametricEqWidget* w = dlg.parametricWidget();
        auto* low  = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"));
        auto* high = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"));
        QVERIFY(w && low && high);
        low->setValue(1500);
        QCOMPARE(w->frequencyMinHz(), 1500.0);

        QSignalSpy spy(&tx, &TransmitModel::txEqParaEqDataChanged);
        high->setValue(2000);
        QCOMPARE(high->value(), 2500);
        QCOMPARE(w->frequencyMaxHz(), 2500.0);
        QCOMPARE(spy.count(), 1);
        const QJsonObject curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("maxHz")).toDouble(), 2500.0);
        compareToWidget(curve, w, kSavedFreqToleranceHz);
    }

    // A Low or High outside the spread moves the curve's range, rescales
    // its points (ucParametricEq.cs:606-645 [v2.10.3.15], FrequencyMinHz /
    // FrequencyMaxHz), and
    // the rescaled points reach the model once (eqform.cs:3197-3213
    // [v2.10.3.15], ucParametricEq1_PointsChanged).
    void lowHighRescaleReachesModelOnce()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        ParametricEqWidget* w = dlg.parametricWidget();
        auto* low  = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"));
        auto* high = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"));
        QVERIFY(w && low && high);

        QSignalSpy spy(&tx, &TransmitModel::txEqParaEqDataChanged);
        high->setValue(3000);
        QCOMPARE(spy.count(), 1);
        QJsonObject curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("maxHz")).toDouble(), 3000.0);
        compareToWidget(curve, w, kSavedFreqToleranceHz);

        low->setValue(200);
        QCOMPARE(spy.count(), 2);
        curve = curveOf(tx);
        QCOMPARE(curve.value(QStringLiteral("minHz")).toDouble(), 200.0);
        compareToWidget(curve, w, kSavedFreqToleranceHz);
        // The spin boxes still show the curve's range.
        QCOMPARE(low->value(), 200);
        QCOMPARE(high->value(), 3000);
    }

    // Typed input: Thetis's udParaEQ_low / udParaEQ_high are NumericUpDowns
    // (NumericUpDownTS overrides no text handling, numericupdownts.cs:33),
    // which raise ValueChanged only when the typed text is committed, so
    // eqform.cs:3539-3577 [v2.10.3.15] runs once per typed value. Typing
    // "3000" into High must not clamp at the "3" or push per keystroke.
    void typedHighCommitsOnEnterOnce()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        dlg.legacyToggle()->setChecked(false);
        dlg.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dlg));
        ParametricEqWidget* w = dlg.parametricWidget();
        auto* low  = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"));
        auto* high = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"));
        QVERIFY(w && low && high);
        QCOMPARE(low->value(), 0);

        QSignalSpy spy(&tx, &TransmitModel::txEqParaEqDataChanged);
        high->setFocus();
        high->selectAll();
        QTest::keyClicks(high, QStringLiteral("3000"));
        QCOMPARE(spy.count(), 0);
        QTest::keyClick(high, Qt::Key_Return);
        QCOMPARE(high->value(), 3000);
        QCOMPARE(low->value(), 0);
        QCOMPARE(w->frequencyMaxHz(), 3000.0);
        QCOMPARE(spy.count(), 1);
        compareToWidget(curveOf(tx), w, kSavedFreqToleranceHz);
    }

    // The same, committed by leaving the box with Tab.
    void typedLowCommitsOnTabOnce()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        TransmitModel& tx = rm.transmitModel();
        dlg.legacyToggle()->setChecked(false);
        dlg.show();
        dlg.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&dlg));
        ParametricEqWidget* w = dlg.parametricWidget();
        auto* low  = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaLowSpin"));
        auto* high = dlg.findChild<QSpinBox*>(QStringLiteral("TxEqParaHighSpin"));
        QVERIFY(w && low && high);

        QSignalSpy spy(&tx, &TransmitModel::txEqParaEqDataChanged);
        low->setFocus();
        QVERIFY(low->hasFocus());
        low->selectAll();
        QTest::keyClicks(low, QStringLiteral("500"));
        QCOMPARE(spy.count(), 0);
        QTest::keyClick(low, Qt::Key_Tab);
        QVERIFY(!low->hasFocus());
        QCOMPARE(low->value(), 500);
        QCOMPARE(high->value(), 4000);
        QCOMPARE(w->frequencyMinHz(), 500.0);
        QCOMPARE(spy.count(), 1);
        compareToWidget(curveOf(tx), w, kSavedFreqToleranceHz);
    }

    // ── 17. closeEvent hides instead of destroying ──────────────────
    void closeEventHidesInsteadOfDestroying()
    {
        RadioModel rm;
        TxEqDialog dlg(&rm);
        dlg.show();
        QCOMPARE(dlg.isVisible(), true);

        QCloseEvent ev;
        ev.setAccepted(true);   // default; closeEvent should override
        QApplication::sendEvent(&dlg, &ev);

        QCOMPARE(ev.isAccepted(), false);   // event ignored
        QCOMPARE(dlg.isVisible(), false);   // dialog hidden
        // Pointer still valid — singleton lifecycle preserved.
        QVERIFY(dlg.legacyToggle());
    }
};

QTEST_MAIN(TestTxEqDialog)
#include "tst_tx_eq_dialog.moc"
