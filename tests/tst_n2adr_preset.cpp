// no-port-check: unit test for the N2ADR Filter preset helper.  Cite
// comments to mi0bot setup.cs are documentary only — the ported logic
// itself lives in N2adrPreset.cpp where the attribution header +
// PROVENANCE row already cover it.
//
// Phase 3L HL2 Filter visibility brainstorm.  Locks the per-band write
// table that backs both Hl2IoBoardTab::onN2adrToggled (UI toggle) and
// RadioModel::handleConnect (app-launch reconcile) so future upstream
// resyncs touch one helper, not two duplicates.

#include <QtTest/QtTest>

#include "core/OcMatrix.h"
#include "core/accessories/N2adrPreset.h"
#include "models/Band.h"

using namespace NereusSDR;

class TestN2adrPreset : public QObject {
    Q_OBJECT

private slots:
    // enabled=true populates 160m pin 1 (RX + TX) — the simplest ham-band
    // entry from setup.cs:14324-14325 [v2.10.3.13-beta2].
    void enable_writes_160m_rx_and_tx_pin0()
    {
        OcMatrix oc;
        applyN2adrPreset(oc, /*enabled=*/true);
        QVERIFY(oc.pinEnabled(Band::Band160m, /*pin=*/0, /*tx=*/false));
        QVERIFY(oc.pinEnabled(Band::Band160m, /*pin=*/0, /*tx=*/true));
    }

    // 80m gets pin 2 + pin 7 RX, pin 2 TX (decoded chkPenOCrcv802 +
    // chkPenOCrcv807 + chkPenOCxmit802).
    void enable_writes_80m_rx_pins_1_and_6_tx_pin_1()
    {
        OcMatrix oc;
        applyN2adrPreset(oc, /*enabled=*/true);
        QVERIFY(oc.pinEnabled(Band::Band80m, /*pin=*/1, /*tx=*/false));
        QVERIFY(oc.pinEnabled(Band::Band80m, /*pin=*/6, /*tx=*/false));
        QVERIFY(oc.pinEnabled(Band::Band80m, /*pin=*/1, /*tx=*/true));
        // Pin 7 must NOT be set on TX side.
        QVERIFY(!oc.pinEnabled(Band::Band80m, /*pin=*/6, /*tx=*/true));
    }

    // 13 SWL bands × pin-7 RX entries — the missing 30% of N2ADR
    // visibility that motivated this Phase 3L commit.  Source:
    // mi0bot setup.cs:14346-14358 chkOCrcv1207..chkOCrcv117 [v2.10.3.13-beta2].
    void enable_writes_all_13_swl_pin7_rx_entries()
    {
        OcMatrix oc;
        applyN2adrPreset(oc, /*enabled=*/true);

        for (int idx = static_cast<int>(Band::SwlFirst);
             idx <= static_cast<int>(Band::SwlLast); ++idx) {
            const Band b = static_cast<Band>(idx);
            QVERIFY2(oc.pinEnabled(b, /*pin=*/6, /*tx=*/false),
                     qPrintable(QStringLiteral("SWL pin-7 RX missing for band %1")
                                    .arg(static_cast<int>(b))));
        }
    }

    // SWL bands must have NO TX entries — N2ADR Filter board is RX-only
    // for SWL (pin 7 is the "RX active" relay; mi0bot has no chkOCxmit*
    // entries for SWL bands at all).
    void enable_does_not_set_any_swl_tx_pins()
    {
        OcMatrix oc;
        applyN2adrPreset(oc, /*enabled=*/true);

        for (int idx = static_cast<int>(Band::SwlFirst);
             idx <= static_cast<int>(Band::SwlLast); ++idx) {
            const Band b = static_cast<Band>(idx);
            for (int pin = 0; pin < 7; ++pin) {
                QVERIFY2(!oc.pinEnabled(b, pin, /*tx=*/true),
                         qPrintable(QStringLiteral("Unexpected SWL TX pin %1 set on band %2")
                                        .arg(pin).arg(static_cast<int>(b))));
            }
        }
    }

    // SWL bands also must have NO RX pins set OTHER than pin 7.
    void enable_does_not_set_swl_rx_pins_other_than_7()
    {
        OcMatrix oc;
        applyN2adrPreset(oc, /*enabled=*/true);

        for (int idx = static_cast<int>(Band::SwlFirst);
             idx <= static_cast<int>(Band::SwlLast); ++idx) {
            const Band b = static_cast<Band>(idx);
            for (int pin = 0; pin < 6; ++pin) {  // pins 0..5 only
                QVERIFY2(!oc.pinEnabled(b, pin, /*tx=*/false),
                         qPrintable(QStringLiteral("Unexpected SWL RX pin %1 set on band %2")
                                        .arg(pin).arg(static_cast<int>(b))));
            }
        }
    }

    // enabled=false wipes every cell.  Pre-seed both an HF entry and an
    // SWL entry, then apply false — both must clear.
    void disable_wipes_all_bands_including_swl()
    {
        OcMatrix oc;

        // Pre-seed: 20m pin 3 RX + 49m pin 6 RX.
        oc.setPin(Band::Band20m, /*pin=*/3, /*tx=*/false, true);
        oc.setPin(Band::Band49m, /*pin=*/6, /*tx=*/false, true);
        QVERIFY(oc.pinEnabled(Band::Band20m, 3, false));
        QVERIFY(oc.pinEnabled(Band::Band49m, 6, false));

        applyN2adrPreset(oc, /*enabled=*/false);

        // All cells clear.
        for (int b = 0; b < int(Band::Count); ++b) {
            for (int pin = 0; pin < 7; ++pin) {
                QVERIFY(!oc.pinEnabled(static_cast<Band>(b), pin, /*tx=*/false));
                QVERIFY(!oc.pinEnabled(static_cast<Band>(b), pin, /*tx=*/true));
            }
        }
    }

    // enabled=true also wipes pre-existing manual entries before populating
    // (mi0bot's "N2ADR owns the OC matrix while enabled" model).  Pre-seed
    // a 40m pin 5 entry that is NOT part of the N2ADR pattern; it must
    // disappear after the preset applies.
    void enable_wipes_pre_existing_manual_entries()
    {
        OcMatrix oc;
        oc.setPin(Band::Band40m, /*pin=*/5, /*tx=*/false, true);
        QVERIFY(oc.pinEnabled(Band::Band40m, 5, false));

        applyN2adrPreset(oc, /*enabled=*/true);

        QVERIFY(!oc.pinEnabled(Band::Band40m, 5, false));
        // But the N2ADR-owned 40m pin 2 + pin 6 are now set.
        QVERIFY(oc.pinEnabled(Band::Band40m, 2, false));
        QVERIFY(oc.pinEnabled(Band::Band40m, 6, false));
    }

    // Idempotency check: applying enabled=true twice yields the same state.
    void enable_is_idempotent()
    {
        OcMatrix once;
        applyN2adrPreset(once, /*enabled=*/true);

        OcMatrix twice;
        applyN2adrPreset(twice, /*enabled=*/true);
        applyN2adrPreset(twice, /*enabled=*/true);

        for (int b = 0; b < int(Band::Count); ++b) {
            for (int pin = 0; pin < 7; ++pin) {
                QCOMPARE(once.pinEnabled(static_cast<Band>(b), pin, false),
                         twice.pinEnabled(static_cast<Band>(b), pin, false));
                QCOMPARE(once.pinEnabled(static_cast<Band>(b), pin, true),
                         twice.pinEnabled(static_cast<Band>(b), pin, true));
            }
        }
    }
};

QTEST_MAIN(TestN2adrPreset)
#include "tst_n2adr_preset.moc"
