// =================================================================
// src/core/session/media/SpectrumEndpoint.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote daemon R3 Task 2.
//
// =================================================================

#include "core/session/media/SpectrumEndpoint.h"
#include "core/FFTEngine.h"

#include "core/session/media/DssWideRow.h"
#include "core/spectrum/SpectrumReducer.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace NereusSDR {
namespace {

bool finite(double value)
{
    return std::isfinite(value);
}

bool validPlane(const SpectrumPlaneRequest& plane)
{
    return static_cast<int>(plane.detector) >= static_cast<int>(SpectrumDetectorMode::Peak)
        && static_cast<int>(plane.detector) < static_cast<int>(SpectrumDetectorMode::Count)
        && plane.averageMode >= -1 && plane.averageMode <= 3
        && finite(plane.averageAlpha) && plane.averageAlpha >= 0.0 && plane.averageAlpha <= 1.0;
}

ReducerConfig reducerConfig(const SpectrumEndpointRequest& request,
                            const SpectrumEndpointSourceContext& source,
                            const SpectrumPlaneRequest& plane,
                            int pixels)
{
    ReducerConfig config;
    config.pixels = pixels;
    config.centreHz = request.centreHz;
    config.spanHz = request.spanHz;
    config.streamCentreHz = source.centreHz;
    config.sampleRateHz = source.sampleRateHz;
    config.detector = plane.detector;
    config.averageMode = plane.averageMode;
    config.averageAlpha = plane.averageAlpha;
    return config;
}

bool overlapsSource(const SpectrumEndpointRequest& request,
                    const SpectrumEndpointSourceContext& source)
{
    const double sourceLow = source.centreHz - source.sampleRateHz * 0.5;
    const double sourceHigh = source.centreHz + source.sampleRateHz * 0.5;
    const double requestLow = request.centreHz - request.spanHz * 0.5;
    const double requestHigh = request.centreHz + request.spanHz * 0.5;
    return requestHigh > sourceLow && requestLow < sourceHigh;
}

bool finiteNonnegativeBins(const QVector<float>& bins)
{
    return std::all_of(bins.cbegin(), bins.cend(), [](float value) {
        return std::isfinite(value) && value >= 0.0f;
    });
}

bool finiteRow(const QVector<float>& row)
{
    return std::all_of(row.cbegin(), row.cend(), [](float value) {
        return std::isfinite(value);
    });
}

QVector<float> calibratedDbm(const DaemonSpectrumFrame& frame)
{
    const double scale = std::pow(10.0, frame.dbmOffset / 10.0);
    if (!finite(scale) || scale <= 0.0) {
        return {};
    }
    QVector<float> output;
    output.resize(frame.binsLinear.size());
    for (int index = 0; index < frame.binsLinear.size(); ++index) {
        const float linear = frame.binsLinear.at(index);
        if (!std::isfinite(linear) || linear < 0.0f) {
            return {};
        }
        output[index] = static_cast<float>(10.0 * std::log10(scale * linear + 1.0e-60));
        if (!std::isfinite(output[index])) {
            return {};
        }
    }
    return output;
}

qint64 saturatingAdd(qint64 value, qint64 increment)
{
    if (value > std::numeric_limits<qint64>::max() - increment) {
        return std::numeric_limits<qint64>::max();
    }
    return value + increment;
}

} // namespace

bool SpectrumEndpoint::validRequest(const SpectrumEndpointRequest& request)
{
    return request.endpointId != 0 && request.source.streamIndex >= 0
        && finite(request.centreHz) && finite(request.spanHz) && request.spanHz > 0.0
        && finite(request.centreHz - request.spanHz * 0.5)
        && finite(request.centreHz + request.spanHz * 0.5)
        && request.pixels > 0 && request.targetFps >= 1 && request.targetFps <= 60
        && request.framesPerLine > 0 && validPlane(request.trace) && validPlane(request.waterfall)
        && std::isfinite(request.minDbm) && std::isfinite(request.maxDbm)
        && request.maxDbm > request.minDbm
        && finite(request.requestedWideSpanFactor)
        && finite(request.requestedWideSpanFactor * request.spanHz)
        && (request.requestedWideSpanFactor == 0.0 || request.requestedWideSpanFactor > 1.0);
}

bool SpectrumEndpoint::validSourceContext(const SpectrumEndpointSourceContext& sourceContext)
{
    return sourceContext.source.streamIndex >= 0 && sourceContext.sourceGeneration != 0
        && sourceContext.fftBins > 0 && sourceContext.fftBins <= FFTEngine::maximumFftSize()
        && finite(sourceContext.centreHz)
        && finite(sourceContext.sampleRateHz) && sourceContext.sampleRateHz > 0.0
        && sourceContext.contextGeneration != 0
        && finite(sourceContext.centreHz - sourceContext.sampleRateHz * 0.5)
        && finite(sourceContext.centreHz + sourceContext.sampleRateHz * 0.5);
}

bool SpectrumEndpoint::configure(const SpectrumEndpointRequest& request,
                                 const SpectrumEndpointSourceContext& sourceContext)
{
    if (!validRequest(request) || !validSourceContext(sourceContext)
        || !(request.source == sourceContext.source)) {
        return false;
    }

    // visibleBinRange is geometry-only; resolve it before accepting pixels so
    // a deep crop cannot claim more independent output samples than bins.
    const ReducerConfig geometry = reducerConfig(request, sourceContext, request.trace, 1);
    const std::pair<int, int> bins = SpectrumReducer::visibleBinRange(sourceContext.fftBins, geometry);
    const int count = bins.second - bins.first + 1;
    if (count <= 0) {
        return false;
    }
    const int pixels = std::min({request.pixels, count, kMaxPixels});
    if (pixels <= 0) {
        return false;
    }
    const ReducerConfig traceConfig = reducerConfig(request, sourceContext, request.trace, pixels);
    const double binHz = sourceContext.sampleRateHz / sourceContext.fftBins;
    SpectrumEndpointContext accepted;
    accepted.codec.endpointId = request.endpointId;
    accepted.codec.contextGeneration = sourceContext.contextGeneration;
    accepted.codec.minDbm = request.minDbm;
    accepted.codec.maxDbm = request.maxDbm;
    accepted.codec.traceSamples = static_cast<quint16>(pixels);
    accepted.codec.waterfallSamples = static_cast<quint16>(pixels);
    accepted.source = request.source;
    accepted.sourceGeneration = sourceContext.sourceGeneration;
    const bool hasOverlap = overlapsSource(request, sourceContext);
    if (hasOverlap) {
        accepted.exactCentreHz = sourceContext.centreHz - sourceContext.sampleRateHz * 0.5
            + (bins.first + bins.second + 1) * binHz * 0.5;
        accepted.exactSpanHz = count * binHz;
    }
    accepted.targetFps = request.targetFps;
    accepted.framesPerLine = request.framesPerLine;

    if (hasOverlap && request.requestedWideSpanFactor > 1.0) {
        const QVector<float> emptyBins(sourceContext.fftBins, -200.0f);
        const DssWideRow wide = cropDssWideRow(emptyBins, {
            request.centreHz, request.spanHz, sourceContext.centreHz,
            sourceContext.sampleRateHz, request.requestedWideSpanFactor});
        if (!wide.binsDbm.isEmpty()) {
            accepted.codec.wideSamples = static_cast<quint16>(
                std::min(static_cast<int>(wide.binsDbm.size()), kMaxWideSamples));
            accepted.wideCentreHz = wide.centreHz;
            accepted.wideSpanHz = wide.spanHz;
        }
    }

    auto trace = std::make_unique<SpectrumReducer>();
    auto waterfall = std::make_unique<SpectrumReducer>();
    trace->setConfig(traceConfig);
    waterfall->setConfig(reducerConfig(request, sourceContext, request.waterfall, pixels));
    trace->clearAveraging();
    waterfall->clearAveraging();

    m_request = request;
    m_sourceContext = sourceContext;
    m_context = accepted;
    m_traceReducer = std::move(trace);
    m_waterfallReducer = std::move(waterfall);
    m_hasProducerTimestamp = false;
    m_lastProducerTimestampNs = 0;
    m_nextOutputDueNs = 0;
    m_emittedFrames = 0;
    m_nextEncoderSequence = 0;
    m_hasOverlap = hasOverlap;
    m_configured = true;
    return true;
}

std::optional<DisplayCodecFrame> SpectrumEndpoint::consume(const DaemonSpectrumFrame& frame)
{
    if (!m_configured || !(frame.source == m_sourceContext.source)
        || frame.generation != m_sourceContext.sourceGeneration
        || frame.binsLinear.size() != m_sourceContext.fftBins
        || frame.centreHz != m_sourceContext.centreHz
        || frame.sampleRateHz != m_sourceContext.sampleRateHz
        || !finite(frame.windowEnb) || frame.windowEnb <= 0.0 || !finite(frame.dbmOffset)
        || frame.producedAtNs < 0 || !m_hasOverlap || !finiteNonnegativeBins(frame.binsLinear)) {
        return std::nullopt;
    }
    if (m_hasProducerTimestamp
        && frame.producedAtNs <= m_lastProducerTimestampNs) {
        return std::nullopt;
    }
    m_hasProducerTimestamp = true;
    m_lastProducerTimestampNs = frame.producedAtNs;

    const qint64 periodNs = 1'000'000'000LL / m_context.targetFps;
    // Permit up to 5% early arrival against an advancing schedule. The
    // schedule itself advances by a full period, so a persistently fast
    // producer eventually spends its allowance and cannot exceed target FPS.
    constexpr qint64 kEarlyToleranceDivisor = 20;
    const qint64 earlyToleranceNs = periodNs / kEarlyToleranceDivisor;
    if (m_nextOutputDueNs != 0
        && frame.producedAtNs < m_nextOutputDueNs - earlyToleranceNs) {
        return std::nullopt;
    }

    QVector<float> trace;
    QVector<float> waterfall;
    m_traceReducer->reduce(frame.binsLinear, frame.windowEnb, frame.dbmOffset, trace);
    m_waterfallReducer->reduce(frame.binsLinear, frame.windowEnb, frame.dbmOffset, waterfall);
    if (trace.size() != m_context.codec.traceSamples
        || waterfall.size() != m_context.codec.waterfallSamples
        || !finiteRow(trace) || !finiteRow(waterfall)) {
        return std::nullopt;
    }

    DisplayCodecFrame output;
    output.context = m_context.codec;
    output.encoderSequence = m_nextEncoderSequence++;
    output.producerTimestamp = static_cast<quint64>(frame.producedAtNs);
    output.waterfallAdvance = (m_emittedFrames % static_cast<quint64>(m_context.framesPerLine)) == 0;
    output.traceDbm = std::move(trace);
    output.waterfallDbm = std::move(waterfall);
    if (m_context.codec.wideSamples != 0) {
        const QVector<float> fullDbm = calibratedDbm(frame);
        const DssWideRow wide = cropDssWideRow(fullDbm, {
            m_request.centreHz, m_request.spanHz, m_sourceContext.centreHz,
            m_sourceContext.sampleRateHz, m_request.requestedWideSpanFactor});
        output.wideDbm = peakReduceDssWideRow(wide.binsDbm, kMaxWideSamples);
        if (output.wideDbm.size() != m_context.codec.wideSamples) {
            return std::nullopt;
        }
    }
    // Keep the advancing calendar for a frame that is merely late within its
    // slot; shared sources then retain the requested average FPS. Only a
    // frame that missed at least one entire slot resynchronizes to itself,
    // preventing a post-stall catch-up burst.
    const qint64 followingScheduledDue =
        saturatingAdd(m_nextOutputDueNs, periodNs);
    m_nextOutputDueNs = frame.producedAtNs >= followingScheduledDue
        ? saturatingAdd(frame.producedAtNs, periodNs)
        : followingScheduledDue;
    ++m_emittedFrames;
    return output;
}

void SpectrumEndpoint::reset()
{
    m_configured = false;
    m_hasOverlap = false;
    m_traceReducer.reset();
    m_waterfallReducer.reset();
    m_hasProducerTimestamp = false;
    m_lastProducerTimestampNs = 0;
    m_nextOutputDueNs = 0;
    m_emittedFrames = 0;
    m_nextEncoderSequence = 0;
}

} // namespace NereusSDR
