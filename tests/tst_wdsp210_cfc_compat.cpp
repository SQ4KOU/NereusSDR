// no-port-check: NereusSDR-original CFC compatibility characterization.
// Exercises the public profile API and reads its actual frequency response;
// no upstream algorithm is duplicated in this test.
#include <QtTest>
#include <QTemporaryDir>
#include "WdspCfcReference.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "core/TxChannel.h"
#include "core/WdspEngine.h"

extern "C" int nereus_copy_cfc_profile(int channel, double* compression,
                                      double* postEq, int capacity);

using namespace NereusSDR;

class TestWdsp210CfcCompat : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_engine = std::make_unique<WdspEngine>();
        m_engine->setSynchronousInitForTest(true);
        QVERIFY(m_engine->initialize(m_directory.path() + QLatin1Char('/')));
        m_tx = m_engine->createTxChannel(1);
        QVERIFY(m_tx);
    }

    void defaultProfileResponse()
    {
        verifyResponse(-1);
    }

    void profileResponse_data()
    {
        QTest::addColumn<int>("qMode");
        QTest::newRow("linear") << 0;
        QTest::newRow("compression-q") << 1;
        QTest::newRow("post-eq-q") << 2;
        QTest::newRow("both-q") << 3;
    }

    void profileResponse()
    {
        QFETCH(int, qMode);
        // Deliberately unsorted and asymmetric: a lost Q field or sorting the
        // frequency independently of its gain/Q tuple changes this response.
        const std::vector<double> frequencies{3000.0, 200.0, 1000.0, 5000.0};
        const std::vector<double> gains{2.0, 6.0, 0.0, 4.0};
        const std::vector<double> postEq{-3.0, 1.0, 5.0, 0.0};
        const std::vector<double> compressionQ{0.25, 2.0, 0.8, 1.3};
        const std::vector<double> postEqQ{0.7, 1.7, 0.35, 2.5};
        m_tx->setTxCfcPrecompDb(3.5);
        m_tx->setTxCfcPrePeqDb(-2.0);
        m_tx->setTxCfcProfile(frequencies, gains, postEq,
                            (qMode & 1) ? compressionQ : std::vector<double>{},
                            (qMode & 2) ? postEqQ : std::vector<double>{});
        verifyResponse(qMode);
    }

    void cleanupTestCase()
    {
        m_tx = nullptr;
        m_engine.reset();
    }

private:
    void verifyResponse(int qMode)
    {
        std::array<double, 1025> compression{};
        std::array<double, 1025> equalization{};
        QCOMPARE(nereus_copy_cfc_profile(1, compression.data(), equalization.data(),
                                        static_cast<int>(compression.size())), 1025);
        // A relative 1e-11 allowance covers libm/compiler rounding while
        // remaining far below the measured difference between Q modes.
        // These fixed values were observed in the pre-import binary; tests
        // never regenerate or silently accept a changed reference.
        int compared = 0;
        for (const auto& reference : kCfcReference) {
            if (reference.qMode != qMode)
                continue;
            const auto close = [](double actual, double expected) {
                return std::isfinite(actual)
                    && std::abs(actual - expected) <= 1e-11 * std::max(1.0, std::abs(expected));
            };
            const QByteArray context = QByteArray("mode=") + QByteArray::number(qMode)
                + " bin=" + QByteArray::number(reference.bin);
            QVERIFY2(close(compression[reference.bin], reference.compression), context.constData());
            QVERIFY2(close(equalization[reference.bin], reference.equalization), context.constData());
            ++compared;
        }
        QCOMPARE(compared, 13);
    }

    QTemporaryDir m_directory;
    std::unique_ptr<WdspEngine> m_engine;
    TxChannel* m_tx{nullptr};
};

QTEST_APPLESS_MAIN(TestWdsp210CfcCompat)
#include "tst_wdsp210_cfc_compat.moc"
