// no-port-check: NereusSDR-original linked-WDSP integration tests. Synthetic
// samples prove processing/lifecycle contracts, not listening or RF quality.
#include <QtTest>
#include <QFile>
#include <QTemporaryDir>
#include <QtEndian>

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numbers>

#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "core/dsp/NnrAdapter.h"
#include "core/wdsp_api.h"

extern "C" {
extern const unsigned char nnr_model_1_data[];
extern const unsigned int nnr_model_1_size;
}

using namespace NereusSDR;

class TestWdspNnr : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_directory.isValid());
        m_engine = std::make_unique<WdspEngine>();
        QString reason;
        QVERIFY(!m_engine->setNnrModelPaths({QString(512, 'a'), QString{}}, &reason));
        m_engine->setSynchronousInitForTest(true);
        QVERIFY(m_engine->initialize(m_directory.path() + '/'));
        m_a = m_engine->createRxChannel(0, 256, 1024, 48000, 48000, 48000);
        m_b = m_engine->createRxChannel(2, 256, 1024, 48000, 48000, 48000);
        QVERIFY(m_a);
        QVERIFY(m_b);
    }

    void completeTuningReachesBothRealModelsWhileDisabled()
    {
        const auto state = m_a->nnrDiagnostics();
        QVERIFY(state.available);
        QVERIFY(state.ready);
        QVERIFY(state.modelAvailable[0]);
        QVERIFY(state.modelAvailable[1]);
        QCOMPARE(state.modelSources[0], NnrModelSource::Bundled);
        QCOMPARE(state.modelSources[1], NnrModelSource::Bundled);
        QVERIFY(!state.running);
        QVERIFY(state.delaySamples > 0);

        NnrSettings wanted{1, -31.25, NrPosition::PreAgc, 1.75, 12.5, 2.75, 13.5, 17.25, 83.5};
        QVERIFY(m_a->setNnrTuning(wanted));
        QVERIFY(m_a->nnrTuning() == wanted);
        QCOMPARE(m_a->nnrDiagnostics().actualModelSlot, 1);
        QVERIFY(!m_a->nnrDiagnostics().running);
        QVERIFY(m_b->nnrTuning() == NnrSettings{});

        wanted.modelSlot = 0;
        QVERIFY(m_a->setNnrTuning(wanted));
        QVERIFY(m_a->nnrTuning() == wanted);
        QVERIFY(m_a->setActiveNr(NrSlot::NNR));
        QVERIFY(m_a->nnrDiagnostics().running);
        QVERIFY(m_a->setActiveNr(NrSlot::NR2));
        QVERIFY(!m_a->nnrDiagnostics().running);
        QVERIFY(m_a->emnrEnabled());
    }

    void invalidModelAndTuningLeavePreviousStateIntact()
    {
        const auto before = m_a->nnrTuning();
        auto invalid = before;
        invalid.alpha = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!m_a->setNnrTuning(invalid));
        invalid = before;
        invalid.modelSlot = 7;
        QVERIFY(!m_a->setNnrTuning(invalid));
        QVERIFY(m_a->nnrTuning() == before);
        QCOMPARE(m_a->activeNr(), NrSlot::NR2);
    }

    void internalSizeAndRateRebuildRetainTuningAndDiagnosticSession()
    {
        QVERIFY(m_a->setActiveNr(NrSlot::NNR));
        QVERIFY(m_a->setNnrDiagnostics(1, 0));
        const auto before = m_a->nnrTuning();
        SetDSPBuffsize(0, 2048);
        QVERIFY(m_a->nnrTuning() == before);
        QCOMPARE(m_a->nnrDiagnostics().testMode, 1);
        QCOMPARE(m_a->nnrDiagnostics().outputMode, 0);
        QVERIFY(m_a->nnrDiagnostics().running);

        // 24 kHz is a legal 2:1 WDSP rate relative to 48 kHz I/O, but is
        // not an integer multiple of this NNR network's 16 kHz rate.
        SetDSPSamplerate(0, 24000);
        QVERIFY(!m_a->nnrDiagnostics().rateSupported);
        QVERIFY(!m_a->nnrDiagnostics().running);
        QVERIFY(m_a->nnrTuning() == before);
        SetDSPSamplerate(0, 48000);
        QVERIFY(m_a->nnrDiagnostics().rateSupported);
        QVERIFY(m_a->nnrDiagnostics().running);
        QVERIFY(m_a->nnrTuning() == before);
        QVERIFY(m_a->setNnrDiagnostics(0, 1));
    }

    void realNetworkProducesFiniteAudioForBothModels()
    {
        std::array<float, 256> inI{}, inQ{}, outI{}, outQ{};
        for (int model = 0; model < 2; ++model) {
            auto settings = m_b->nnrTuning();
            settings.modelSlot = model;
            QVERIFY(m_b->setNnrTuning(settings));
            QVERIFY(m_b->setActiveNr(NrSlot::NNR));
            m_b->setActive(true);
            double maximum = 0.0;
            for (int block = 0; block < 64; ++block) {
                for (int i = 0; i < 256; ++i) {
                    const double phase = 2.0 * std::numbers::pi * 1000.0 * (block * 256 + i) / 48000.0;
                    inI[i] = static_cast<float>(0.01 * std::cos(phase));
                    inQ[i] = static_cast<float>(-0.01 * std::sin(phase));
                }
                m_b->processIq(inI.data(), inQ.data(), outI.data(), outQ.data(), 256, 256);
                for (int i = 0; i < 256; ++i) {
                    QVERIFY(std::isfinite(outI[i]));
                    QVERIFY(std::isfinite(outQ[i]));
                    maximum = std::max(maximum, std::abs(static_cast<double>(outI[i])));
                }
            }
            QVERIFY(maximum > 1e-8);
            m_b->setActive(false);
        }
    }

    void recreatedChannelRetainsNormalSettingsAndResetsDiagnostics()
    {
        QVERIFY(m_a->setNnrDiagnostics(2, 0));
        const auto saved = m_a->nnrTuning();
        ChannelConfig config;
        config.sampleRate = 96000;
        config.bufferSize = 256;
        config.filterSize = 2048;
        QVERIFY(m_engine->rebuildRxChannel(0, config) >= 0);
        m_a = m_engine->rxChannel(0);
        QVERIFY(m_a);
        QVERIFY(m_a->nnrTuning() == saved);
        QCOMPARE(m_a->activeNr(), NrSlot::NNR);
        QVERIFY(m_a->nnrDiagnostics().running);
        QCOMPARE(m_a->nnrDiagnostics().testMode, 0);
        QCOMPARE(m_a->nnrDiagnostics().outputMode, 1);
    }

    void partiallyLoadedModelCannotBeSelectedOrDereferenced()
    {
        // Keep the real file grammar and tensors but remove one required
        // first-layer tensor name. The native loader reaches its partial
        // model path; user imports are rejected earlier by host preflight.
        QByteArray broken(reinterpret_cast<const char*>(nnr_model_1_data), nnr_model_1_size);
        const auto count = qFromLittleEndian<quint32>(broken.constData() + 12);
        bool changed = false;
        for (quint32 i = 0; i < count; ++i) {
            const int offset = 32 + static_cast<int>(i) * 72;
            if (broken.mid(offset, 6) == "enc1_w") {
                broken[offset] = 'x';
                changed = true;
                break;
            }
        }
        QVERIFY(changed);
        const QString path = m_directory.filePath("missing-required-tensor.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(broken), broken.size());
        file.close();
        const auto encoded = QFile::encodeName(path);
        SetNNRModelPathSlot(1, encoded.constData());
        auto* partial = m_engine->createRxChannel(4, 256, 1024, 48000, 48000, 48000);
        SetNNRModelPathSlot(1, "");
        QVERIFY(partial);
        QVERIFY(partial->nnrDiagnostics().modelAvailable[0]);
        QVERIFY(!partial->nnrDiagnostics().modelAvailable[1]);
        QVERIFY(partial->setActiveNr(NrSlot::NR2));
        const auto before = partial->nnrTuning();
        auto requested = before;
        requested.modelSlot = 1;
        requested.alpha = 2.5;
        QVERIFY(!partial->setNnrTuning(requested));
        QVERIFY(partial->nnrTuning() == before);
        QCOMPARE(partial->activeNr(), NrSlot::NR2);
        requested.modelSlot = 0;
        QVERIFY(partial->setNnrTuning(requested));
        QCOMPARE(partial->nnrTuning().alpha, 2.5);
        m_engine->destroyRxChannel(4);
    }

    void modelPathsStayFixedUntilReconnect()
    {
        QString reason;
        QVERIFY(!m_engine->setNnrModelPaths({}, &reason));
        QVERIFY(reason.contains("reconnect"));
    }

    void cleanupTestCase()
    {
        m_a = nullptr;
        m_b = nullptr;
        m_engine.reset();
    }

private:
    QTemporaryDir m_directory;
    std::unique_ptr<WdspEngine> m_engine;
    RxChannel* m_a{nullptr};
    RxChannel* m_b{nullptr};
};

QTEST_APPLESS_MAIN(TestWdspNnr)
#include "tst_wdsp_nnr.moc"
