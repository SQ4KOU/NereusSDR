// =================================================================
// tests/tst_spectrum_endpoint.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote daemon R3 Task 2.
//
// =================================================================

#include <QtTest>

#include <algorithm>
#include <limits>
#include <optional>

#include "core/session/media/SpectrumEndpoint.h"

using namespace NereusSDR;

namespace {

SpectrumEndpointSourceContext sourceContext(quint64 generation = 11,
                                            quint32 contextGeneration = 4)
{
    SpectrumEndpointSourceContext source;
    source.source = {2, FftTier::Wide};
    source.sourceGeneration = generation;
    source.fftBins = 1024;
    source.centreHz = 14'200'000.0;
    source.sampleRateHz = 192'000.0;
    source.contextGeneration = contextGeneration;
    return source;
}

SpectrumEndpointRequest request()
{
    SpectrumEndpointRequest endpoint;
    endpoint.endpointId = 91;
    endpoint.source = {2, FftTier::Wide};
    endpoint.centreHz = 14'200'000.0;
    endpoint.spanHz = 96'000.0;
    endpoint.pixels = 64;
    endpoint.targetFps = 30;
    endpoint.framesPerLine = 2;
    endpoint.trace.detector = SpectrumDetectorMode::Peak;
    endpoint.trace.averageMode = 0;
    endpoint.waterfall.detector = SpectrumDetectorMode::Average;
    endpoint.waterfall.averageMode = 0;
    endpoint.requestedWideSpanFactor = 2.0;
    return endpoint;
}

DaemonSpectrumFrame frame(const SpectrumEndpointSourceContext& source,
                          qint64 producedAtNs)
{
    DaemonSpectrumFrame frame;
    frame.source = source.source;
    frame.generation = source.sourceGeneration;
    frame.centreHz = source.centreHz;
    frame.sampleRateHz = source.sampleRateHz;
    frame.producedAtNs = producedAtNs;
    frame.windowEnb = 1.0;
    frame.dbmOffset = 0.0;
    frame.binsLinear.fill(1.0e-12f, source.fftBins);
    frame.binsLinear[300] = 1.0e-5f;
    frame.binsLinear[800] = 1.0e-4f;
    return frame;
}

} // namespace

class TstSpectrumEndpoint : public QObject
{
    Q_OBJECT

private slots:
    void logAveragingDoesNotRestartAtZeroDbAfterRetune()
    {
        SpectrumEndpoint endpoint;
        auto req = request();
        req.trace.averageMode = 3;
        req.trace.averageAlpha = 0.5;
        auto source = sourceContext();
        QVERIFY(endpoint.configure(req, source));
        auto input = frame(source, 1'000'000'000);
        input.binsLinear.fill(1.0e-12f);
        auto output = endpoint.consume(input);
        QVERIFY(output);
        QVERIFY(std::abs(output->traceDbm[32] - (-140.0f)) < 0.001f);
        input.producedAtNs += 40'000'000;
        output = endpoint.consume(input);
        QVERIFY(output);
        QVERIFY(std::abs(output->traceDbm[32] - (-130.0f)) < 0.001f);

        ++source.sourceGeneration;
        ++source.contextGeneration;
        source.centreHz += 100;
        QVERIFY(endpoint.configure(req, source));
        QVERIFY(!endpoint.consume(input)); // Old source frames remain inadmissible.
        input = frame(source, 1'100'000'000);
        input.binsLinear.fill(1.0e-12f);
        output = endpoint.consume(input);
        QVERIFY(output);
        QVERIFY(std::abs(output->traceDbm[32] - (-140.0f)) < 0.001f);
    }

    void independentPlanesClampAndWideCoverage()
    {
        SpectrumEndpoint endpoint;
        const SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));
        const SpectrumEndpointContext context = endpoint.context();
        QCOMPARE(context.codec.traceSamples, quint16(64));
        QCOMPARE(context.codec.waterfallSamples, quint16(64));
        QCOMPARE(context.codec.wideSamples, quint16(768));
        QVERIFY(context.exactSpanHz > 0.0);
        QVERIFY(context.wideSpanHz > context.exactSpanHz);

        const std::optional<DisplayCodecFrame> output = endpoint.consume(frame(source, 1'000'000'000));
        QVERIFY(output.has_value());
        QCOMPARE(output->traceDbm.size(), 64);
        QCOMPARE(output->waterfallDbm.size(), 64);
        QCOMPARE(output->wideDbm.size(), 768);
        QVERIFY(output->waterfallAdvance);
        QCOMPARE(output->encoderSequence, quint32(0));

        // Peak and average reducers cover the same crop but are independent:
        // the single-bin carrier is preserved by Peak and diluted by Average.
        const int exactPeak = std::distance(output->traceDbm.cbegin(),
                                            std::max_element(output->traceDbm.cbegin(),
                                                             output->traceDbm.cend()));
        QVERIFY(output->traceDbm.at(exactPeak) > output->waterfallDbm.at(exactPeak));
        const int widePeak = std::distance(output->wideDbm.cbegin(),
                                           std::max_element(output->wideDbm.cbegin(), output->wideDbm.cend()));
        QVERIFY(widePeak >= 0 && widePeak < output->wideDbm.size());
        QVERIFY(output->wideDbm.at(widePeak) > -80.0f);

        SpectrumEndpoint clamped;
        SpectrumEndpointRequest large = request();
        large.pixels = 8'000; // source bins then protocol cap
        QVERIFY(clamped.configure(large, source));
        // A half-DDC crop includes both inclusive boundary bins under the
        // existing floor/ceil reducer contract, therefore 513, not 1024.
        QCOMPARE(clamped.context().codec.traceSamples, quint16(513));
    }

    void cadenceAndGenerationReset()
    {
        SpectrumEndpoint endpoint;
        SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));
        QVERIFY(endpoint.consume(frame(source, 1'000'000'000)).has_value());
        QVERIFY(!endpoint.consume(frame(source, 1'010'000'000)).has_value());
        const std::optional<DisplayCodecFrame> second = endpoint.consume(frame(source, 1'040'000'000));
        QVERIFY(second.has_value());
        QVERIFY(!second->waterfallAdvance);
        QCOMPARE(second->encoderSequence, quint32(1));

        source.sourceGeneration = 12;
        source.contextGeneration = 5;
        QVERIFY(endpoint.configure(request(), source));
        const std::optional<DisplayCodecFrame> reset = endpoint.consume(frame(source, 2'000'000'000));
        QVERIFY(reset.has_value());
        QCOMPARE(reset->context.contextGeneration, quint32(5));
        QCOMPARE(reset->encoderSequence, quint32(0));
        QVERIFY(reset->waterfallAdvance);
    }

    void cadenceScheduleToleratesNominalJitterAndPreservesWaterfallCadence()
    {
        SpectrumEndpoint endpoint;
        const SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));

        qint64 producedAtNs = 1'000'000'000;
        int waterfallAdvances = 0;
        constexpr int kFrames = 120;
        for (int index = 0; index < kFrames; ++index) {
            const auto output = endpoint.consume(frame(source, producedAtNs));
            QVERIFY2(output.has_value(),
                     "32 ms / 34.666667 ms source jitter must retain the 30 FPS schedule");
            QCOMPARE(output->encoderSequence, static_cast<quint32>(index));
            if (output->waterfallAdvance) {
                ++waterfallAdvances;
            }
            producedAtNs += (index % 2 == 0) ? 32'000'000 : 34'666'667;
        }
        QCOMPARE(waterfallAdvances, kFrames / request().framesPerLine);
    }

    void fasterProducerStaysCappedAndStallDoesNotCatchUp()
    {
        SpectrumEndpoint endpoint;
        const SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));

        int emitted = 0;
        for (qint64 offsetNs = 0; offsetNs < 1'000'000'000; offsetNs += 10'000'000) {
            if (endpoint.consume(frame(source, 1'000'000'000 + offsetNs)).has_value()) {
                ++emitted;
            }
        }
        // A 100 FPS shared FFT producer must not make this 30 FPS endpoint
        // exceed its own budget. Inclusive time endpoints allow 30 frames.
        QVERIFY(emitted <= 30);
        QVERIFY(emitted >= 29);

        SpectrumEndpoint afterStall;
        QVERIFY(afterStall.configure(request(), source));
        QVERIFY(afterStall.consume(frame(source, 1'000'000'000)).has_value());
        QVERIFY(afterStall.consume(frame(source, 2'000'000'000)).has_value());
        for (qint64 offsetNs = 1'000'000; offsetNs < 20'000'000;
             offsetNs += 1'000'000) {
            QVERIFY(!afterStall.consume(frame(source, 2'000'000'000 + offsetNs)).has_value());
        }
    }

    void backwardAndDuplicateProducerTimestampsCannotEmitTwice()
    {
        SpectrumEndpoint endpoint;
        const SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));

        const auto first = endpoint.consume(frame(source, 1'000'000'000));
        QVERIFY(first.has_value());
        QCOMPARE(first->encoderSequence, quint32(0));
        QVERIFY(!endpoint.consume(frame(source, 1'000'000'000)).has_value());
        QVERIFY(!endpoint.consume(frame(source, 999'999'999)).has_value());

        const auto next = endpoint.consume(frame(source, 1'040'000'000));
        QVERIFY(next.has_value());
        QCOMPARE(next->encoderSequence, quint32(1));
    }

    void deepCropNeverClaimsMorePixelsThanItsBins()
    {
        SpectrumEndpoint endpoint;
        const SpectrumEndpointSourceContext source = sourceContext();
        SpectrumEndpointRequest deep = request();
        deep.spanHz = source.sampleRateHz / source.fftBins;
        deep.pixels = 512;
        QVERIFY(endpoint.configure(deep, source));
        const SpectrumEndpointContext context = endpoint.context();
        const int coveredBins = qRound(context.exactSpanHz
                                       / (source.sampleRateHz / source.fftBins));
        QVERIFY(coveredBins > 0);
        QCOMPARE(context.codec.traceSamples, static_cast<quint16>(coveredBins));
        QCOMPARE(context.codec.waterfallSamples, static_cast<quint16>(coveredBins));
        const std::optional<DisplayCodecFrame> output = endpoint.consume(frame(source, 1'000'000'000));
        QVERIFY(output.has_value());
        QCOMPARE(output->traceDbm.size(), coveredBins);
    }

    void outOfRangeAndStaleSourceNeverYieldPriorFrame()
    {
        SpectrumEndpoint endpoint;
        SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(endpoint.configure(request(), source));
        QVERIFY(endpoint.consume(frame(source, 1'000'000'000)).has_value());

        SpectrumEndpointRequest outside = request();
        outside.centreHz = source.centreHz + source.sampleRateHz * 4.0;
        QVERIFY(endpoint.configure(outside, source));
        QCOMPARE(endpoint.context().exactSpanHz, 0.0);
        QVERIFY(!endpoint.consume(frame(source, 2'000'000'000)).has_value());

        QVERIFY(endpoint.configure(request(), source));
        DaemonSpectrumFrame stale = frame(source, 3'000'000'000);
        stale.generation = source.sourceGeneration - 1;
        QVERIFY(!endpoint.consume(stale).has_value());
    }

    void validatesOwnershipAndInput()
    {
        SpectrumEndpoint endpoint;
        SpectrumEndpointRequest bad = request();
        bad.endpointId = 0;
        QVERIFY(!endpoint.configure(bad, sourceContext()));
        bad = request();
        bad.source.streamIndex = 3;
        QVERIFY(!endpoint.configure(bad, sourceContext()));
        bad = request();
        bad.trace.averageMode = 4;
        QVERIFY(!endpoint.configure(bad, sourceContext()));
        bad = request();
        bad.targetFps = 61;
        QVERIFY(!endpoint.configure(bad, sourceContext()));
        bad = request();
        bad.centreHz = std::numeric_limits<double>::max();
        bad.spanHz = std::numeric_limits<double>::max();
        QVERIFY(!endpoint.configure(bad, sourceContext()));
        SpectrumEndpointSourceContext oversized = sourceContext();
        oversized.fftBins = std::numeric_limits<int>::max();
        QVERIFY(!endpoint.configure(request(), oversized));

        SpectrumEndpoint configured;
        const SpectrumEndpointSourceContext source = sourceContext();
        QVERIFY(configured.configure(request(), source));
        DaemonSpectrumFrame nonfinite = frame(source, 1'000'000'000);
        nonfinite.binsLinear[0] = std::numeric_limits<float>::infinity();
        QVERIFY(!configured.consume(nonfinite).has_value());
    }
};

QTEST_MAIN(TstSpectrumEndpoint)
#include "tst_spectrum_endpoint.moc"
