#include <QtTest/QtTest>
#include "core/BoardCapabilities.h"
#include "core/HpsdrModel.h"

using namespace NereusSDR;

class TestBoardCapabilities : public QObject {
    Q_OBJECT
private slots:

    void forBoardReturnsMatchingEntry_data() {
        QTest::addColumn<int>("hw");
        QTest::newRow("Atlas")      << int(HPSDRHW::Atlas);
        QTest::newRow("Hermes")     << int(HPSDRHW::Hermes);
        QTest::newRow("HermesII")   << int(HPSDRHW::HermesII);
        QTest::newRow("Angelia")    << int(HPSDRHW::Angelia);
        QTest::newRow("Orion")      << int(HPSDRHW::Orion);
        QTest::newRow("OrionMKII")  << int(HPSDRHW::OrionMKII);
        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite);
        QTest::newRow("Saturn")     << int(HPSDRHW::Saturn);
        QTest::newRow("SaturnMKII") << int(HPSDRHW::SaturnMKII);
    }
    void forBoardReturnsMatchingEntry() {
        QFETCH(int, hw);
        const auto& caps = BoardCapsTable::forBoard(static_cast<HPSDRHW>(hw));
        QCOMPARE(static_cast<int>(caps.board), hw);
    }

    void forModelMatchesBoardForModel() {
        for (int i = 0; i < int(HPSDRModel::LAST); ++i) {
            auto m = static_cast<HPSDRModel>(i);
            const auto& caps = BoardCapsTable::forModel(m);
            QCOMPARE(caps.board, boardForModel(m));
        }
    }

    void sampleRatesAreSaneForEveryBoard() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.sampleRates[0] == 48000);
            int prev = 0;
            for (int sr : caps.sampleRates) {
                if (sr == 0) { break; }
                QVERIFY(sr > prev);
                QVERIFY(sr <= caps.maxSampleRate);
                prev = sr;
            }
        }
    }

    void attenuatorAbsentImpliesZeroRange() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (!caps.attenuator.present) {
                QCOMPARE(caps.attenuator.minDb, 0);
                QCOMPARE(caps.attenuator.maxDb, 0);
            } else {
                QVERIFY(caps.attenuator.minDb <= caps.attenuator.maxDb);
                QVERIFY(caps.attenuator.stepDb > 0);
            }
        }
    }

    void diversityRequiresTwoAdcs() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (caps.hasDiversityReceiver) {
                QCOMPARE(caps.adcCount, 2);
            }
        }
    }

    void alexTxRoutingImpliesAlexFilters() {
        for (const auto& caps : BoardCapsTable::all()) {
            if (caps.hasAlexTxRouting) {
                QVERIFY(caps.hasAlexFilters);
            }
        }
    }

    void displayNameAndCitationNonNull() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.displayName != nullptr);
            QVERIFY(caps.sourceCitation != nullptr);
            QVERIFY(std::string(caps.displayName).size() > 0);
            QVERIFY(std::string(caps.sourceCitation).size() > 0);
        }
    }

    void firmwareMinIsLessOrEqualKnownGood() {
        for (const auto& caps : BoardCapsTable::all()) {
            QVERIFY(caps.minFirmwareVersion <= caps.knownGoodFirmware);
        }
    }

    void hermesLiteHasHl2Extras() {
        const auto& caps = BoardCapsTable::forBoard(HPSDRHW::HermesLite);
        QVERIFY(caps.hasBandwidthMonitor);
        QVERIFY(caps.hasIoBoardHl2);
        QVERIFY(!caps.hasAlexFilters);
        QCOMPARE(caps.ocOutputCount, 0);
        QCOMPARE(caps.attenuator.maxDb, 60);
    }

    void angeliaHasDiversityAndPureSignal() {
        const auto& caps = BoardCapsTable::forBoard(HPSDRHW::Angelia);
        QVERIFY(caps.hasDiversityReceiver);
        QVERIFY(caps.hasPureSignal);
        QCOMPARE(caps.adcCount, 2);
    }
};

QTEST_APPLESS_MAIN(TestBoardCapabilities)
#include "tst_board_capabilities.moc"
