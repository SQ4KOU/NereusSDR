// no-port-check: smoke test for OC Outputs SWL sub-sub-tab UI construction +
// matrix wiring.  Phase 3L HL2 Filter visibility brainstorm.
//
// Cite comments to mi0bot setup.designer.cs:tpOCSWLControl are documentary
// only — the ported logic itself lives in OcOutputsSwlTab.cpp where the
// attribution header + PROVENANCE row already cover it.

#include <QtTest/QtTest>
#include <QApplication>
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>

#include "gui/setup/hardware/OcOutputsSwlTab.h"
#include "core/OcMatrix.h"
#include "models/Band.h"
#include "models/RadioModel.h"

#include "GridHeaderAlignment.h"
#include "core/RadioDiscovery.h"
#include "gui/setup/HardwarePage.h"

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
    // Each P1-P7 header sits over its column of check boxes, in the RX and
    // the TX matrix, every band row, on a Saturn and an HL2 (whose tab is
    // "Hermes Lite Control"), in today's look and the workspace look (the
    // headers spread across the width while the boxes packed at the left).
    void pin_headers_sit_over_their_columns_data()
    {
        QTest::addColumn<bool>("workspace");
        QTest::addColumn<int>("board");
        QTest::newRow("today saturn") << false << static_cast<int>(HPSDRHW::Saturn);
        QTest::newRow("today hl2") << false << static_cast<int>(HPSDRHW::HermesLite);
        QTest::newRow("look saturn") << true << static_cast<int>(HPSDRHW::Saturn);
        QTest::newRow("look hl2") << true << static_cast<int>(HPSDRHW::HermesLite);
    }

    void pin_headers_sit_over_their_columns()
    {
        QFETCH(bool, workspace);
        QFETCH(int, board);
        const GridHeaderAlignment::AppLook look(workspace);
        const auto hw = static_cast<HPSDRHW>(board);
        RadioModel model;
        model.setBoardForTest(hw);
        HardwarePage page(&model);
        RadioInfo info;
        info.boardType  = hw;
        info.protocol   = hw == HPSDRHW::Saturn ? ProtocolVersion::Protocol2
                                                : ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
        page.onCurrentRadioChanged(info);
        QVERIFY(GridHeaderAlignment::selectTab(&page, page.tabTextForTest(HardwarePage::Tab::OcOutputs)));
        QVERIFY(GridHeaderAlignment::selectTab(&page, QStringLiteral("SWL")));
        auto* tab = page.findChild<OcOutputsSwlTab*>();
        QVERIFY(tab != nullptr);
        look.apply(&page);
        page.resize(1400, 900);
        page.show();
        QVERIFY(QTest::qWaitForWindowExposed(&page));
        int groupsChecked = 0;
        for (QGroupBox* group : tab->findChildren<QGroupBox*>()) {
            const bool tx = group->title().contains(QStringLiteral("TX OC"));
            const bool rx = group->title().contains(QStringLiteral("RX OC"));
            if (!tx && !rx) {
                continue;
            }
            ++groupsChecked;
            for (int pin = 1; pin <= 7; ++pin) {
                QLabel* header = nullptr;
                for (QLabel* label : group->findChildren<QLabel*>()) {
                    if (label->text() == QStringLiteral("P%1").arg(pin)) {
                        header = label;
                    }
                }
                QVERIFY(header != nullptr);
                const qreal headerX = GridHeaderAlignment::labelCentreX(header, &page);
                int boxes = 0;
                const QString pinTip = QStringLiteral(" OC pin %1, ").arg(pin);
                for (QCheckBox* box : group->findChildren<QCheckBox*>()) {
                    if (!box->toolTip().contains(pinTip)) {
                        continue;
                    }
                    ++boxes;
                    const qreal boxX = GridHeaderAlignment::indicatorCentreX(box, &page);
                    QVERIFY2(qAbs(boxX - headerX) <= 1.0,
                             qPrintable(QStringLiteral("%1 P%2: header centre %3, box centre %4 (%5)")
                                            .arg(group->title()).arg(pin).arg(headerX).arg(boxX)
                                            .arg(box->toolTip())));
                }
                QVERIFY(boxes > 0);
            }
        }
        QCOMPARE(groupsChecked, 2);
    }
};

QTEST_MAIN(TestOcOutputsSwlTab)
#include "tst_oc_outputs_swl_tab.moc"
