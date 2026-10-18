// no-port-check: NereusSDR-original read-only source routing; no DSP reads.
// Modification history (NereusSDR):
//   2026-10-08 - writeContainerSource: the container source write shared by
//                 Container Settings and the workspace header's source menu.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-04 — Selected RX source identity and RX-only presentation reset by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-02 — Mixed container ownership, persistence and source routing by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include "ContainerSourceAdapter.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "gui/meters/MeterPoller.h"
#include <QJsonArray>
#include <QJsonDocument>
namespace NereusSDR {
bool ContainerSourceAdapter::followsSelectedRx(const QJsonObject& context)
{
    return context.value("rxSourceMode").toString() == QStringLiteral("followSelectedRx");
}
void ContainerSourceAdapter::writeContainerSource(QJsonObject& config, int rxSource)
{
    if (config.contains("sliceId")) { config["sliceId"] = rxSource - 1; }
    else { config["rxSource"] = rxSource; }
}
SliceModel* ContainerSourceAdapter::slice(RadioModel* model, const QJsonObject& context, SliceModel* inherited, const QString& currentSessionId)
{
    if (!model) { return nullptr; }
    // A foreign session remains unavailable rather than reading this station.
    if (context.contains("sessionId") && !context.value("sessionId").toString().isEmpty()
        && context.value("sessionId").toString() != currentSessionId) { return nullptr; }
    if (followsSelectedRx(context)) { return inherited; }
    if (context.contains("sliceId")) { return model->sliceById(context.value("sliceId").toInt(-1)); }
    if (context.contains("rxSource")) { return model->sliceById(context.value("rxSource").toInt() - 1); }
    return inherited;
}
QByteArray ContainerSourceAdapter::sourceIdentity(RadioModel* model, const QJsonObject& context, SliceModel* inherited,
                                                 const QString& currentSessionId, bool ready)
{
    const SliceModel* source = slice(model, context, inherited, currentSessionId);
    // Epochs use strings so the full 64-bit identity survives JSON numbers.
    const QJsonArray identity{currentSessionId, source ? source->sliceIndex() : -1,
        source ? source->streamIndex() : -1,
        source ? QString::number(source->streamEpoch()) : QString(),
        ready && source && source->streamIndex() >= 0};
    return QJsonDocument(identity).toJson(QJsonDocument::Compact);
}
double ContainerSourceAdapter::reading(RadioModel* model, const QJsonObject& context, SliceModel* inherited,
                                      int binding, bool ready, bool extendedReadings,
                                      const std::function<double(const SliceModel*)>& maxBin, const QString& currentSessionId)
{
    SliceModel* source = slice(model, context, inherited, currentSessionId);
    if (!ready || !source || source->streamIndex() < 0) { return kNoMeterReadingDbm; }
    switch (binding) {
    case MeterBinding::SignalPeak: return source->signalPeakDbm();
    case MeterBinding::SignalAvg: return source->signalAverageDbm();
    case MeterBinding::SignalMaxBin: return maxBin ? maxBin(source) : kNoMeterReadingDbm;
    // No sanctioned cache supplies passband SNR. Never manufacture a zero.
    case MeterBinding::PbSnr: return kNoMeterReadingDbm;
    default: break;
    }
    if (!extendedReadings) { return kNoMeterReadingDbm; }
    switch (binding) {
    case MeterBinding::AdcPeak: return source->adcPeakDbfs();
    case MeterBinding::AdcAvg: return source->adcAverageDbfs();
    case MeterBinding::AgcGain: return source->agcGainDb();
    case MeterBinding::AgcPeak: return source->agcPeakDb();
    case MeterBinding::AgcAvg: return source->agcAverageDb();
    default: return kNoMeterReadingDbm;
    }
}
}

namespace NereusSDR {
std::optional<WaveformRequest>
ContainerSourceAdapter::waveformRequest(RadioModel* model, const QJsonObject& context, SliceModel* inherited,
                                        WaveformKind kind, const QString& session)
{
    if (!model || !model->isConnected()) {
        return {};
    }
    SliceModel* resolved = slice(model, context, inherited, session);
    if (!resolved || resolved->streamIndex() < 0 || !isRadeDspMode(resolved->dspMode())) {
        return {};
    }
    if (kind != WaveformKind::DecodedSpeech && resolved != model->txBoundSlice()) {
        return {};
    }
    return WaveformRequest{resolved->sliceIndex(), kind};
}
WaveformSnapshotPtr ContainerSourceAdapter::waveform(RadioModel* model, const QJsonObject& context,
                                                     SliceModel* inherited, WaveformKind kind,
                                                     const QString& session)
{
    auto unavailable = std::make_shared<WaveformSnapshot>();
    unavailable->kind = kind;
    unavailable->asOfNs = waveformNowNs();
    unavailable->availability = WaveformAvailability::Unavailable;
    if (context.contains("sessionId") && !context["sessionId"].toString().isEmpty() &&
        context["sessionId"].toString() != session) {
        unavailable->reason = "Foreign radio session";
        return unavailable;
    }
    if (model && model->role() == RadioModel::Role::Remote) {
        const WaveformSnapshotPtr support =
            model->radeWaveformModel()->snapshot(-1, kind, unavailable->asOfNs);
        if (support->availability == WaveformAvailability::Unsupported) {
            return support;
        }
    }
    if (!model || !model->isConnected()) {
        unavailable->reason = "Radio source disconnected";
        return unavailable;
    }
    SliceModel* resolved = slice(model, context, inherited, session);
    if (!resolved || resolved->streamIndex() < 0) {
        unavailable->reason = "Receive source unavailable";
        return unavailable;
    }
    if (!isRadeDspMode(resolved->dspMode())) {
        unavailable->reason = "Select a RADE slice for this waveform";
        return unavailable;
    }
    if (kind != WaveformKind::DecodedSpeech && resolved != model->txBoundSlice()) {
        unavailable->reason = "Microphone belongs to the current TX-bound slice";
        return unavailable;
    }
    return model->radeWaveformModel()->snapshot(resolved->sliceIndex(), kind, unavailable->asOfNs);
}
}
