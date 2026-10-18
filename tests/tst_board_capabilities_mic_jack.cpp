// no-port-check: NereusSDR-original test. hasMicJack is a NereusSDR-original
// capability flag with no Thetis port; no attribution is required.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-27 — Original test for NereusSDR by J.J. Boyd (KG4VCF),
//                 with AI-assisted implementation via Anthropic Claude Code.
// =================================================================
//
// Tests for BoardCapabilities::hasMicJack (Phase 3M-1b Task B.1).
//
// Design source: docs/architecture/phase3m-1b-thetis-pre-code-review.md §11.
// HermesLite 2 has no radio-side mic jack; all other boards do.

#include <QtTest/QtTest>
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestBoardCapabilitiesMicJack : public QObject {
    Q_OBJECT
private slots:

    // -----------------------------------------------------------------------
    // Phase 3M-1b Task B.1: hasMicJack per-board flag
    //
    // Source: pre-code review §11.3 per-board table.
    //   Atlas, Hermes / Hermes-II, Angelia, Orion / Orion-MkII,
    //   ANAN-7000DLE / 8000DLE / Anvelina, Saturn G2 / G2-1K  → true
    //   HermesLite 2 (all variants)                             → false
    //
    // §11 cite: NereusSDR-original; derived from Thetis Setup->Audio->Primary
    // per-board panel visibility:
    //   panelSaturnMicInput  (setup.designer.cs:8613 [v2.10.3.13])
    //   panelOrionMic        (setup.designer.cs:8661 [v2.10.3.13])
    //   panelOrionPTT        (setup.designer.cs:8709 [v2.10.3.13])
    //   panelOrionBias       (setup.designer.cs:8755 [v2.10.3.13])
    //   grpBoxMic            (setup.designer.cs:5154 [v2.10.3.13])
    // All hidden when CurrentModel == HermesLite2 per setup.cs:19834-20310
    // RadioModelChanged() per-model if-ladder [@501e3f5].
    // -----------------------------------------------------------------------

    void hasMicJack_data() {
        QTest::addColumn<int>("hw");
        QTest::addColumn<bool>("expectedHasMicJack");

        // All boards except HL2 variants have a radio-side mic jack.
        QTest::newRow("Atlas")            << int(HPSDRHW::Atlas)            << true;
        QTest::newRow("Hermes")           << int(HPSDRHW::Hermes)           << true;
        QTest::newRow("HermesII")         << int(HPSDRHW::HermesII)         << true;
        QTest::newRow("Angelia")          << int(HPSDRHW::Angelia)          << true;
        QTest::newRow("Orion")            << int(HPSDRHW::Orion)            << true;
        QTest::newRow("OrionMKII")        << int(HPSDRHW::OrionMKII)        << true;
        QTest::newRow("Saturn")           << int(HPSDRHW::Saturn)           << true;
        QTest::newRow("SaturnMKII")       << int(HPSDRHW::SaturnMKII)       << true;
        QTest::newRow("Andromeda")        << int(HPSDRHW::Andromeda)        << true;

        // HermesLite 2 has no radio-side mic jack hardware.
        QTest::newRow("HermesLite")       << int(HPSDRHW::HermesLite)       << false;
        QTest::newRow("HermesLiteRxOnly") << int(HPSDRHW::HermesLiteRxOnly) << false;
    }

    void hasMicJack() {
        QFETCH(int, hw);
        QFETCH(bool, expectedHasMicJack);
        const auto& caps = BoardCapsTable::forBoard(static_cast<HPSDRHW>(hw));
        QCOMPARE(caps.hasMicJack, expectedHasMicJack);
    }

    // Convenience: iterate BoardCapsTable::all() to confirm no board other than
    // HL2 variants returns false.  Also checks Unknown (the forBoard fallback)
    // is covered by the parametrized test or defaults true (safe for UI).
    void hasMicJack_allTableEntries_onlyHl2IsFalse() {
        for (const auto& caps : BoardCapsTable::all()) {
            const bool isHl2Variant =
                (caps.board == HPSDRHW::HermesLite) ||
                (caps.board == HPSDRHW::HermesLiteRxOnly);

            if (isHl2Variant) {
                QVERIFY2(!caps.hasMicJack,
                         qPrintable(QStringLiteral("HL2 variant must have hasMicJack=false: ")
                                    + caps.displayName));
            } else {
                QVERIFY2(caps.hasMicJack,
                         qPrintable(QStringLiteral("non-HL2 board must have hasMicJack=true: ")
                                    + caps.displayName));
            }
        }
    }
};

QTEST_APPLESS_MAIN(TestBoardCapabilitiesMicJack)
#include "tst_board_capabilities_mic_jack.moc"
