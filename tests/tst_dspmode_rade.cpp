// no-port-check: NereusSDR-original unit-test file. No Thetis logic
// ported in this test file; DSPMode::RADE is a NereusSDR-native
// extension to the WDSP-derived DSPMode enum (RADE is not a WDSP
// mode).
// =================================================================
// tests/tst_dspmode_rade.cpp  (NereusSDR)
// =================================================================
//
// Phase 3R Task J1 unit tests: DSPMode::RADE enum entry round-trips
// through the SliceModel::modeName / modeFromName serialization
// helpers.
//
// Test cases (2):
//   1. modeName(RADE) returns the literal "RADE"
//   2. modeFromName("RADE") returns DSPMode::RADE
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-11 - New test file for Phase 3R Task J1.  J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include "core/WdspTypes.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestDspModeRade : public QObject {
    Q_OBJECT
private slots:
    void serializesRadeToString();
    void deserializesRadeFromString();
};

void TestDspModeRade::serializesRadeToString() {
    QCOMPARE(SliceModel::modeName(DSPMode::RADE), QStringLiteral("RADE"));
}

void TestDspModeRade::deserializesRadeFromString() {
    QCOMPARE(SliceModel::modeFromName(QStringLiteral("RADE")),
             DSPMode::RADE);
}

QTEST_GUILESS_MAIN(TestDspModeRade)
#include "tst_dspmode_rade.moc"
