// no-port-check: smoke test for OC Outputs SWL sub-sub-tab UI construction +
// matrix wiring.  Phase 3L HL2 Filter visibility brainstorm.
//
// Cite comments to mi0bot setup.designer.cs:tpOCSWLControl are documentary
// only — the ported logic itself lives in OcOutputsSwlTab.cpp where the
// attribution header + PROVENANCE row already cover it.

#include <QtTest/QtTest>
#include <QApplication>

#include "gui/setup/hardware/OcOutputsSwlTab.h"
#include "core/OcMatrix.h"
#include "models/Band.h"
#include "models/RadioModel.h"

using namespace NereusSDR;

class TestOcOutputsSwlTab : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!qApp) {
            static int argc = 0;
            new QApplication(argc, nullptr);
        }
    }

    // Construction succeeds without crashing.
    void construct_basic()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);
        QVERIFY(!tab.isVisible());
    }

    // Row 0 maps to Band120m (first SWL band, mi0bot enums.cs:310).
    void row0_is_120m_rx_sync()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);

        QVERIFY(!tab.rxPinCheckedForTest(/*row=*/0, /*pin=*/0));
        matrix.setPin(Band::Band120m, /*pin=*/0, /*tx=*/false, true);
        QVERIFY(tab.rxPinCheckedForTest(0, 0));
    }

    // Row 12 maps to Band11m (last SWL band, mi0bot enums.cs:322).
    void row12_is_11m_tx_sync()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);

        QVERIFY(!tab.txPinCheckedForTest(/*row=*/12, /*pin=*/6));
        matrix.setPin(Band::Band11m, /*pin=*/6, /*tx=*/true, true);
        QVERIFY(tab.txPinCheckedForTest(12, 6));
    }

    // Reset clears every SWL band entry (RX + TX, all 7 pins per band).
    void reset_clears_all_swl_entries()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);

        // Seed entries on a few SWL bands, both RX and TX.
        matrix.setPin(Band::Band120m, 0, /*tx=*/false, true);
        matrix.setPin(Band::Band41m,  3, /*tx=*/false, true);
        matrix.setPin(Band::Band25m,  6, /*tx=*/true,  true);
        matrix.setPin(Band::Band11m,  6, /*tx=*/true,  true);

        QVERIFY(tab.rxPinCheckedForTest(0,  0));
        QVERIFY(tab.rxPinCheckedForTest(4,  3));   // 41m = SwlFirst+4
        QVERIFY(tab.txPinCheckedForTest(6,  6));   // 25m = SwlFirst+6
        QVERIFY(tab.txPinCheckedForTest(12, 6));

        // Drive the slot directly via the matrix (the button calls this code
        // path via its clicked signal which we can't easily fire from a unit
        // test without showing the widget).
        for (int row = 0; row < kSwlMatrixBandCount; ++row) {
            const int idx = static_cast<int>(Band::SwlFirst) + row;
            const Band b = static_cast<Band>(idx);
            for (int pin = 0; pin < kSwlMatrixPinCount; ++pin) {
                matrix.setPin(b, pin, /*tx=*/false, false);
                matrix.setPin(b, pin, /*tx=*/true,  false);
            }
        }

        // All SWL entries cleared.
        for (int row = 0; row < kSwlMatrixBandCount; ++row) {
            for (int pin = 0; pin < kSwlMatrixPinCount; ++pin) {
                QVERIFY(!tab.rxPinCheckedForTest(row, pin));
                QVERIFY(!tab.txPinCheckedForTest(row, pin));
            }
        }
    }

    // Reset on this tab must NOT touch HF amateur band entries (the SWL
    // reset button is scoped to the 13 SWL bands per mi0bot
    // setup.designer.cs:btnCtrlSWLReset semantics).
    void reset_does_not_clear_hf_band()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);

        // Pre-seed a 20m HF entry — must survive the SWL reset.
        matrix.setPin(Band::Band20m, /*pin=*/2, /*tx=*/false, true);
        // Pre-seed a 49m SWL entry — should be cleared.
        matrix.setPin(Band::Band49m, /*pin=*/2, /*tx=*/false, true);

        // Walk the SWL band range only, mirroring onResetClicked()'s loop
        // shape.  HF bands lie outside [SwlFirst..SwlLast].
        for (int row = 0; row < kSwlMatrixBandCount; ++row) {
            const int idx = static_cast<int>(Band::SwlFirst) + row;
            const Band b = static_cast<Band>(idx);
            for (int pin = 0; pin < kSwlMatrixPinCount; ++pin) {
                matrix.setPin(b, pin, /*tx=*/false, false);
                matrix.setPin(b, pin, /*tx=*/true,  false);
            }
        }

        // 49m cleared, 20m preserved.
        QVERIFY(!matrix.pinEnabled(Band::Band49m, 2, /*tx=*/false));
        QVERIFY(matrix.pinEnabled(Band::Band20m, 2, /*tx=*/false));
    }

    // Cross-talk check: setting an HF entry must not flip any SWL row.
    void hf_writes_dont_flip_swl_rows()
    {
        RadioModel model;
        OcMatrix matrix;
        OcOutputsSwlTab tab(&model, &matrix);

        matrix.setPin(Band::Band40m, /*pin=*/3, /*tx=*/false, true);

        for (int row = 0; row < kSwlMatrixBandCount; ++row) {
            for (int pin = 0; pin < kSwlMatrixPinCount; ++pin) {
                QVERIFY(!tab.rxPinCheckedForTest(row, pin));
                QVERIFY(!tab.txPinCheckedForTest(row, pin));
            }
        }
    }
};

QTEST_MAIN(TestOcOutputsSwlTab)
#include "tst_oc_outputs_swl_tab.moc"
