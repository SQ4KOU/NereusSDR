#pragma once
// no-port-check: NereusSDR-original native display contract, GPLv3.
// Modification history (NereusSDR):
// 2026-10-12 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
// Accepted display-pill-live.html:1610-1613 and call 56 define a retained
// folded row's quiet level; no simulated texture or new DSP is involved.

#include <QVector>
#include <optional>
#include <algorithm>
#include <cmath>

namespace NereusSDR {

struct DssHistorySample {
    float dbm;
    bool covered;
};

// From accepted display-pill-live.html:1610-1613 (2026-10-12), call 56:
// sample folded calibrated row x=0,8,... and select floor(count * 0.3).
inline constexpr int kDssQuietSampleStride = 8;
inline constexpr double kDssQuietFraction = 0.3;

inline std::optional<float> dssQuietLevelFromFoldedRow(const QVector<float>& row)
{
    QVector<float> samples;
    samples.reserve((row.size() + kDssQuietSampleStride - 1) / kDssQuietSampleStride);
    for (qsizetype i = 0; i < row.size(); i += kDssQuietSampleStride) {
        if (std::isfinite(row[i])) { samples.append(row[i]); }
    }
    if (samples.isEmpty()) { return std::nullopt; }
    const qsizetype index = static_cast<qsizetype>(std::floor(samples.size() * kDssQuietFraction));
    std::nth_element(samples.begin(), samples.begin() + index, samples.end());
    return samples[index];
}

inline DssHistorySample dssSampleQuietOverhang(float frequencyUnit,
    std::optional<float> exact, std::optional<float> wide,
    std::optional<float> quiet, float currentFloorDbm)
{
    // Real captured data always wins. Only beyond captured coverage in
    // extended overhang does call 56 supply this retained row's quiet level.
    if (exact) { return {*exact, true}; }
    if (wide) { return {*wide, true}; }
    if (std::isfinite(frequencyUnit) && (frequencyUnit < 0.0f || frequencyUnit > 1.0f)
            && quiet && std::isfinite(*quiet)) {
        return {*quiet, true};
    }
    return {currentFloorDbm, false};
}

} // namespace NereusSDR
