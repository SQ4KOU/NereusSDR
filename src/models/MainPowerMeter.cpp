// no-port-check: NereusSDR-original. See MainPowerMeter.h.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
#include "models/MainPowerMeter.h"
#include "models/AmplifierModel.h"
#include "models/RfKitModel.h"
#include "core/RadioStatus.h"
#include "core/ControlRanges.h"

namespace NereusSDR {
MainPowerMeter::MainPowerMeter(AmplifierModel* amplifier, RfKitModel* rfKit,
                             RadioStatus* radio, QObject* parent)
    : QObject(parent), m_amplifier(amplifier), m_rfKit(rfKit), m_radio(radio)
{
    connect(m_amplifier, &AmplifierModel::statusChanged, this, &MainPowerMeter::refresh);
    connect(m_rfKit, &RfKitModel::statusChanged, this, &MainPowerMeter::refresh);
    connect(m_radio, &RadioStatus::powerChanged, this, &MainPowerMeter::refresh);
    refresh();
}

void MainPowerMeter::setHardwareModel(HPSDRModel model)
{
    if (m_hardwareModel == model) {
        return;
    }
    m_hardwareModel = model;
    refresh();
}

void MainPowerMeter::refresh()
{
    Source source = Source::Radio;
    double forward = m_radio->forwardPowerWatts();
    double swr = m_radio->swrRatio();
    const double drive = forward;
    // A disconnected device retains its last operate flag. Presence is
    // required; Power Genius takes precedence when both are operating.
    if (m_amplifier->present() && m_amplifier->operate()) {
        source = Source::PowerGenius;
        forward = m_amplifier->forwardPowerW();
        swr = m_amplifier->swr();
    } else if (m_rfKit->present() && m_rfKit->operate()) {
        source = Source::RfKit;
        forward = m_rfKit->forwardPowerW();
        swr = m_rfKit->swr();
    }
    const int limit = source == Source::Radio
        ? paMaxWattsFor(m_hardwareModel) : kAmplifierLegalLimitW;
    // Radio headroom is the existing TxApplet/container rule, from
    // ControlRanges::kRfPowerGaugeHeadroom (20 percent above the PA rating).
    const int top = source == Source::Radio
        ? qRound(limit * ControlRanges::kRfPowerGaugeHeadroom) : kAmplifierScaleW;
    const bool sourceDiffers = source != m_source;
    const bool readingDiffers = forward != m_forwardW || swr != m_swr;
    const bool driveDiffers = drive != m_driveW;
    const bool scaleDiffers = limit != m_visualLimitW || top != m_scaleTopW;
    // Commit the whole derived state before any signal, so every consumer
    // sees consistent values even when mirrored inputs arrive separately.
    m_source = source;
    m_forwardW = forward;
    m_swr = swr;
    m_driveW = drive;
    m_visualLimitW = limit;
    m_scaleTopW = top;
    if (sourceDiffers) {
        emit sourceChanged();
    }
    if (scaleDiffers) {
        emit scaleChanged();
    }
    if (readingDiffers) {
        emit readingChanged(forward, swr);
    }
    if (driveDiffers) {
        emit driveChanged(drive);
    }
}
} // namespace NereusSDR
