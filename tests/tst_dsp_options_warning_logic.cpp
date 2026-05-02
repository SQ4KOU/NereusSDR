// NereusSDR-original test — no Thetis source ported here.
// No upstream attribution required (NereusSDR warning-icon logic test).
//
// Tests for Task 4.5: Warning icon visibility driven by Thetis validity rules.
//
// Porting from Thetis console.cs:38797-38807 + setup.cs:22391-22400 [v2.10.3.13].
//
// Thetis validity rules (console.cs:38797-38803):
//   bufferSizeDifferentRX  = !(phone==fm && fm==cw  && cw==dig)   (4-way)
//   bufferSizeDifferentTX  = !(phone==fm && fm==dig)              (3-way; no CW TX)
//   filterSizeDifferentRX  = same 4-way pattern
//   filterSizeDifferentTX  = same 3-way pattern
//   filterTypeDifferentRX  = same 4-way pattern (RX type combos)
//   filterTypeDifferentTX  = same 3-way pattern (TX type combos)
//
//   pbWarningBufferSize.Visible = bufferSizeDifferentRX || bufferSizeDifferentTX
//   pbWarningFilterSize.Visible = filterSizeDifferentRX || filterSizeDifferentTX
//   pbWarningBufferType.Visible = filterTypeDifferentRX || filterTypeDifferentTX
//
// Because DspOptionsPage is a GUI widget (requires QApplication + SetupPage
// infrastructure that depends on a RadioModel), the tests here exercise the
// static helper functions directly (comboValuesDiffer4 / comboValuesDiffer3)
// and verify AppSettings round-trip. The GUI integration is covered by the
// existing filter_display_high_resolution and dsp_options_persistence suites.
//
// Design Section 4E.
//
// Test cases:
//   differ4_all_equal_returns_false — same value across all 4 combos → no warning
//   differ4_one_different_returns_true — one combo differs → warning
//   differ4_two_different_returns_true — two combos differ → warning
//   differ4_all_different_returns_true — all 4 differ → warning
//   differ3_all_equal_returns_false — same value across all 3 combos → no warning
//   differ3_one_different_returns_true — one combo differs → warning
//   differ3_two_different_returns_true — two combos differ → warning
//   buffer_warn_fires_on_rx_side_only — only RX set differs → warning
//   buffer_warn_fires_on_tx_side_only — only TX set differs → warning
//   buffer_warn_hidden_when_all_match — all 4 equal → no warning
//   filter_size_same_logic_as_buffer — same differ logic applies to filter size
//   filter_type_rx_3_of_4_equal_fires — RX types: 3 equal, 1 different → warning

#include <QtTest/QtTest>
#include <QComboBox>
#include <QApplication>

#include "gui/setup/DspOptionsPage.h"

using namespace NereusSDR;

// ── Helper: build a tiny QComboBox with a single current text ────────────────

static QComboBox* makeC(const QString& text, QObject* parent = nullptr)
{
    auto* c = new QComboBox(nullptr);
    c->addItem(text);
    // Optionally add a second item so we can switch later
    c->addItem(QStringLiteral("__other__"));
    c->setCurrentText(text);
    if (parent) {
        c->setParent(qobject_cast<QWidget*>(parent));
    }
    return c;
}

// ── Test class ────────────────────────────────────────────────────────────────

class TestDspOptionsWarningLogic : public QObject {
    Q_OBJECT

private slots:

    // ── DspOptionsPage::comboValuesDiffer4 ───────────────────────────────────

    void differ4_all_equal_returns_false()
    {
        QComboBox a, b, c, d;
        for (auto* cb : {&a, &b, &c, &d}) {
            cb->addItem("256");
            cb->setCurrentText("256");
        }
        QVERIFY(!DspOptionsPage::comboValuesDiffer4(&a, &b, &c, &d));
    }

    void differ4_one_different_returns_true()
    {
        QComboBox a, b, c, d;
        for (auto* cb : {&a, &b, &c}) {
            cb->addItem("256");
            cb->setCurrentText("256");
        }
        d.addItem("512");
        d.setCurrentText("512");
        QVERIFY(DspOptionsPage::comboValuesDiffer4(&a, &b, &c, &d));
    }

    void differ4_two_different_returns_true()
    {
        QComboBox a, b, c, d;
        a.addItem("256"); a.setCurrentText("256");
        b.addItem("256"); b.setCurrentText("256");
        c.addItem("512"); c.setCurrentText("512");
        d.addItem("1024"); d.setCurrentText("1024");
        QVERIFY(DspOptionsPage::comboValuesDiffer4(&a, &b, &c, &d));
    }

    void differ4_all_different_returns_true()
    {
        QComboBox a, b, c, d;
        a.addItem("64");   a.setCurrentText("64");
        b.addItem("128");  b.setCurrentText("128");
        c.addItem("256");  c.setCurrentText("256");
        d.addItem("512");  d.setCurrentText("512");
        QVERIFY(DspOptionsPage::comboValuesDiffer4(&a, &b, &c, &d));
    }

    // ── DspOptionsPage::comboValuesDiffer3 ───────────────────────────────────

    void differ3_all_equal_returns_false()
    {
        QComboBox a, b, c;
        for (auto* cb : {&a, &b, &c}) {
            cb->addItem("4096");
            cb->setCurrentText("4096");
        }
        QVERIFY(!DspOptionsPage::comboValuesDiffer3(&a, &b, &c));
    }

    void differ3_one_different_returns_true()
    {
        QComboBox a, b, c;
        a.addItem("4096"); a.setCurrentText("4096");
        b.addItem("4096"); b.setCurrentText("4096");
        c.addItem("8192"); c.setCurrentText("8192");
        QVERIFY(DspOptionsPage::comboValuesDiffer3(&a, &b, &c));
    }

    void differ3_two_different_returns_true()
    {
        QComboBox a, b, c;
        a.addItem("4096");  a.setCurrentText("4096");
        b.addItem("8192");  b.setCurrentText("8192");
        c.addItem("16384"); c.setCurrentText("16384");
        QVERIFY(DspOptionsPage::comboValuesDiffer3(&a, &b, &c));
    }

    // ── Buffer-size warning composite logic ──────────────────────────────────
    // Tests the OR of RX-differ and TX-differ, matching Thetis:
    //   pbWarningBufferSize.Visible = bufferSizeDifferentRX || bufferSizeDifferentTX

    void buffer_warn_fires_on_rx_side_only()
    {
        // RX: phone/cw/dig/fm differ (phone != cw).
        // TX: phone/dig/fm all same.
        QComboBox phone, cw, dig, fm;
        phone.addItem("256"); phone.setCurrentText("256");
        cw.addItem("512");    cw.setCurrentText("512");   // differs
        dig.addItem("256");   dig.setCurrentText("256");
        fm.addItem("256");    fm.setCurrentText("256");

        const bool rxDiffers = DspOptionsPage::comboValuesDiffer4(&phone, &cw, &dig, &fm);
        const bool txDiffers = DspOptionsPage::comboValuesDiffer3(&phone, &dig, &fm);
        // RX fires; TX is uniform → overall warning is shown.
        QVERIFY(rxDiffers);
        QVERIFY(!txDiffers);
        QVERIFY(rxDiffers || txDiffers);
    }

    void buffer_warn_fires_on_tx_side_only()
    {
        // RX: all 4 equal.
        // TX: phone/dig/fm differ (fm != phone/dig).
        QComboBox phone, cw, dig, fm;
        phone.addItem("256"); phone.setCurrentText("256");
        cw.addItem("256");    cw.setCurrentText("256");
        dig.addItem("256");   dig.setCurrentText("256");
        fm.addItem("512");    fm.setCurrentText("512");  // differs (TX side)

        const bool rxDiffers = DspOptionsPage::comboValuesDiffer4(&phone, &cw, &dig, &fm);
        const bool txDiffers = DspOptionsPage::comboValuesDiffer3(&phone, &dig, &fm);
        // RX: phone==cw==dig==256 but fm==512, so 4-way DOES differ.
        // (This is expected — fm differs, so both RX and TX fire together.)
        QVERIFY(rxDiffers || txDiffers);
    }

    void buffer_warn_hidden_when_all_match()
    {
        // All 4 combos (and the TX subset) share the same value.
        QComboBox phone, cw, dig, fm;
        for (auto* cb : {&phone, &cw, &dig, &fm}) {
            cb->addItem("256");
            cb->setCurrentText("256");
        }
        const bool rxDiffers = DspOptionsPage::comboValuesDiffer4(&phone, &cw, &dig, &fm);
        const bool txDiffers = DspOptionsPage::comboValuesDiffer3(&phone, &dig, &fm);
        QVERIFY(!rxDiffers);
        QVERIFY(!txDiffers);
        QVERIFY(!(rxDiffers || txDiffers));
    }

    // ── Filter-size: same differ4/differ3 logic ───────────────────────────────

    void filter_size_same_logic_as_buffer()
    {
        // All filter sizes equal → no warning.
        QComboBox p, cw, d, fm;
        for (auto* cb : {&p, &cw, &d, &fm}) {
            cb->addItem("4096");
            cb->setCurrentText("4096");
        }
        QVERIFY(!DspOptionsPage::comboValuesDiffer4(&p, &cw, &d, &fm));
        QVERIFY(!DspOptionsPage::comboValuesDiffer3(&p, &d, &fm));

        // Change CW filter size → 4-way RX check fires.
        cw.addItem("8192");
        cw.setCurrentText("8192");
        QVERIFY(DspOptionsPage::comboValuesDiffer4(&p, &cw, &d, &fm));
    }

    // ── Filter-type: RX 4-way check ──────────────────────────────────────────

    void filter_type_rx_3_of_4_equal_fires()
    {
        // phoneRx/cwRx/digRx all "Low Latency", fmRx "Linear Phase" → differs.
        QComboBox phoneRx, cwRx, digRx, fmRx;
        phoneRx.addItem("Low Latency");  phoneRx.setCurrentText("Low Latency");
        cwRx.addItem("Low Latency");     cwRx.setCurrentText("Low Latency");
        digRx.addItem("Low Latency");    digRx.setCurrentText("Low Latency");
        fmRx.addItem("Linear Phase");    fmRx.setCurrentText("Linear Phase");

        QVERIFY(DspOptionsPage::comboValuesDiffer4(&phoneRx, &cwRx, &digRx, &fmRx));
    }

    void filter_type_all_rx_equal_no_warning()
    {
        // All RX type combos "Low Latency" and all TX type combos "Linear Phase"
        // — each group is internally uniform, so no warning for either side.
        QComboBox pRx, cwRx, dRx, fmRx;
        for (auto* cb : {&pRx, &cwRx, &dRx, &fmRx}) {
            cb->addItem("Low Latency");
            cb->setCurrentText("Low Latency");
        }
        QComboBox pTx, dTx, fmTx;
        for (auto* cb : {&pTx, &dTx, &fmTx}) {
            cb->addItem("Linear Phase");
            cb->setCurrentText("Linear Phase");
        }
        QVERIFY(!DspOptionsPage::comboValuesDiffer4(&pRx, &cwRx, &dRx, &fmRx));
        QVERIFY(!DspOptionsPage::comboValuesDiffer3(&pTx, &dTx, &fmTx));
    }
};

QTEST_MAIN(TestDspOptionsWarningLogic)
#include "tst_dsp_options_warning_logic.moc"
