#include "SliceModel.h"

#include <algorithm>

namespace NereusSDR {

SliceModel::SliceModel(QObject* parent)
    : QObject(parent)
{
}

SliceModel::~SliceModel() = default;

// ---------------------------------------------------------------------------
// Frequency
// ---------------------------------------------------------------------------

void SliceModel::setFrequency(double freq)
{
    if (!qFuzzyCompare(m_frequency, freq)) {
        m_frequency = freq;
        emit frequencyChanged(freq);
    }
}

// ---------------------------------------------------------------------------
// Demodulation mode
// ---------------------------------------------------------------------------

void SliceModel::setDspMode(DSPMode mode)
{
    bool modeChanged = (m_dspMode != mode);
    m_dspMode = mode;

    // Apply default filter for the new mode
    // From Thetis console.cs:5180-5575 — InitFilterPresets, F5 per mode
    auto [low, high] = defaultFilterForMode(mode);
    bool filterChanged = (m_filterLow != low || m_filterHigh != high);
    m_filterLow = low;
    m_filterHigh = high;

    if (modeChanged) {
        emit dspModeChanged(mode);
    }
    if (filterChanged) {
        emit this->filterChanged(m_filterLow, m_filterHigh);
    }
}

// ---------------------------------------------------------------------------
// Bandpass filter
// ---------------------------------------------------------------------------

void SliceModel::setFilterLow(int low)
{
    if (m_filterLow != low) {
        m_filterLow = low;
        emit filterChanged(m_filterLow, m_filterHigh);
    }
}

void SliceModel::setFilterHigh(int high)
{
    if (m_filterHigh != high) {
        m_filterHigh = high;
        emit filterChanged(m_filterLow, m_filterHigh);
    }
}

void SliceModel::setFilter(int low, int high)
{
    if (m_filterLow != low || m_filterHigh != high) {
        m_filterLow = low;
        m_filterHigh = high;
        emit filterChanged(m_filterLow, m_filterHigh);
    }
}

// ---------------------------------------------------------------------------
// AGC
// ---------------------------------------------------------------------------

void SliceModel::setAgcMode(AGCMode mode)
{
    if (m_agcMode != mode) {
        m_agcMode = mode;
        emit agcModeChanged(mode);
    }
}

// ---------------------------------------------------------------------------
// Tuning step
// ---------------------------------------------------------------------------

void SliceModel::setStepHz(int hz)
{
    if (m_stepHz != hz && hz > 0) {
        m_stepHz = hz;
        emit stepHzChanged(hz);
    }
}

// ---------------------------------------------------------------------------
// Gains
// ---------------------------------------------------------------------------

void SliceModel::setAfGain(int gain)
{
    gain = std::clamp(gain, 0, 100);
    if (m_afGain != gain) {
        m_afGain = gain;
        emit afGainChanged(gain);
    }
}

void SliceModel::setRfGain(int gain)
{
    gain = std::clamp(gain, 0, 100);
    if (m_rfGain != gain) {
        m_rfGain = gain;
        emit rfGainChanged(gain);
    }
}

// ---------------------------------------------------------------------------
// Antenna selection
// ---------------------------------------------------------------------------

void SliceModel::setRxAntenna(const QString& ant)
{
    if (m_rxAntenna != ant) {
        m_rxAntenna = ant;
        emit rxAntennaChanged(ant);
    }
}

void SliceModel::setTxAntenna(const QString& ant)
{
    if (m_txAntenna != ant) {
        m_txAntenna = ant;
        emit txAntennaChanged(ant);
    }
}

// ---------------------------------------------------------------------------
// Slice state
// ---------------------------------------------------------------------------

void SliceModel::setActive(bool active)
{
    if (m_active != active) {
        m_active = active;
        emit activeChanged(active);
    }
}

void SliceModel::setTxSlice(bool tx)
{
    if (m_txSlice != tx) {
        m_txSlice = tx;
        emit txSliceChanged(tx);
    }
}

// ---------------------------------------------------------------------------
// Per-mode default filter presets
// ---------------------------------------------------------------------------

// Porting from Thetis console.cs:5180-5575 — InitFilterPresets, F5 per mode.
//
// Filter low/high are in Hz relative to the carrier frequency.
// LSB: negative offsets (passband below carrier)
// USB: positive offsets (passband above carrier)
// AM/SAM/DSB: symmetric around carrier
// CW: centered on cw_pitch (600 Hz from Thetis display.cs:1023)
// DIGU: centered on digu_click_tune_offset (1500 Hz from Thetis console.cs:14636)
// DIGL: centered on -digl_click_tune_offset (-2210 Hz from Thetis console.cs:14671)
std::pair<int, int> SliceModel::defaultFilterForMode(DSPMode mode)
{
    // From Thetis display.cs:1023
    static constexpr int kCwPitch = 600;
    // From Thetis console.cs:14636
    static constexpr int kDiguOffset = 1500;
    // From Thetis console.cs:14671
    static constexpr int kDiglOffset = 2210;

    switch (mode) {
    case DSPMode::LSB:
        // From Thetis console.cs:5207 — F5: -3000 to -100
        return {-3000, -100};
    case DSPMode::USB:
        // From Thetis console.cs:5249 — F5: 100 to 3000
        return {100, 3000};
    case DSPMode::DSB:
        // From Thetis console.cs:5543 — F5: -3300 to 3300
        return {-3300, 3300};
    case DSPMode::CWL:
        // From Thetis console.cs:5375 — F5: -(cw_pitch+200) to -(cw_pitch-200)
        return {-(kCwPitch + 200), -(kCwPitch - 200)};
    case DSPMode::CWU:
        // From Thetis console.cs:5417 — F5: (cw_pitch-200) to (cw_pitch+200)
        return {kCwPitch - 200, kCwPitch + 200};
    case DSPMode::FM:
        // FM filters are dynamic in Thetis (from deviation + high cut).
        // Default deviation=5000, so use ±8000 as reasonable default.
        // From Thetis console.cs:7559-7565
        return {-8000, 8000};
    case DSPMode::AM:
        // From Thetis console.cs:5459 — F5: -5000 to 5000
        return {-5000, 5000};
    case DSPMode::DIGU:
        // From Thetis console.cs:5333 — F5: (offset-500) to (offset+500)
        return {kDiguOffset - 500, kDiguOffset + 500};
    case DSPMode::SPEC:
        // SPEC mode: passthrough, wide filter
        return {-5000, 5000};
    case DSPMode::DIGL:
        // From Thetis console.cs:5291 — F5: -(offset+500) to -(offset-500)
        return {-(kDiglOffset + 500), -(kDiglOffset - 500)};
    case DSPMode::SAM:
        // From Thetis console.cs:5501 — F5: -5000 to 5000
        return {-5000, 5000};
    case DSPMode::DRM:
        // DRM: wide filter similar to AM
        return {-5000, 5000};
    }
    // Fallback
    return {100, 3000};
}

// ---------------------------------------------------------------------------
// Mode name utilities
// ---------------------------------------------------------------------------

QString SliceModel::modeName(DSPMode mode)
{
    switch (mode) {
    case DSPMode::LSB:  return QStringLiteral("LSB");
    case DSPMode::USB:  return QStringLiteral("USB");
    case DSPMode::DSB:  return QStringLiteral("DSB");
    case DSPMode::CWL:  return QStringLiteral("CWL");
    case DSPMode::CWU:  return QStringLiteral("CWU");
    case DSPMode::FM:   return QStringLiteral("FM");
    case DSPMode::AM:   return QStringLiteral("AM");
    case DSPMode::DIGU: return QStringLiteral("DIGU");
    case DSPMode::SPEC: return QStringLiteral("SPEC");
    case DSPMode::DIGL: return QStringLiteral("DIGL");
    case DSPMode::SAM:  return QStringLiteral("SAM");
    case DSPMode::DRM:  return QStringLiteral("DRM");
    }
    return QStringLiteral("USB");
}

DSPMode SliceModel::modeFromName(const QString& name)
{
    if (name == QLatin1String("LSB"))  return DSPMode::LSB;
    if (name == QLatin1String("USB"))  return DSPMode::USB;
    if (name == QLatin1String("DSB"))  return DSPMode::DSB;
    if (name == QLatin1String("CWL"))  return DSPMode::CWL;
    if (name == QLatin1String("CWU"))  return DSPMode::CWU;
    if (name == QLatin1String("FM"))   return DSPMode::FM;
    if (name == QLatin1String("AM"))   return DSPMode::AM;
    if (name == QLatin1String("DIGU")) return DSPMode::DIGU;
    if (name == QLatin1String("SPEC")) return DSPMode::SPEC;
    if (name == QLatin1String("DIGL")) return DSPMode::DIGL;
    if (name == QLatin1String("SAM"))  return DSPMode::SAM;
    if (name == QLatin1String("DRM"))  return DSPMode::DRM;
    return DSPMode::USB;
}

} // namespace NereusSDR
