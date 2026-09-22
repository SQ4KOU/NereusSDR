// no-port-check: NereusSDR-original configuration value and validation contract.
// Defaults/domains are source-backed by TAPR WDSP 2.10 RXA.c/create_nnr,
// nnet.c/create_dfhead, NNET_TAU_DEFAULT and NNET_GMAX_DB, with the operator
// ranges approved in 2026-09-21-wdsp210-nnr-ps3-design.md section 6.
// Modification history (NereusSDR):
//   2026-09-21 — J.J. Boyd (KG4VCF), with OpenAI Codex assistance.
#pragma once

#include "core/WdspTypes.h"

#include <array>
#include <cmath>
#include <QString>

namespace NereusSDR {

struct NnrSettings {
    int modelSlot{0};
    double maskFloorDb{-25.0};
    NrPosition position{NrPosition::PostAgc};
    double alpha{1.0};
    double alphaKneeDb{10.0};
    double tauSeconds{2.0};
    double maxGainDb{12.0};
    double attackMs{0.0};
    double releaseMs{0.0};

    bool operator==(const NnrSettings&) const = default;

    [[nodiscard]] bool isValid() const noexcept
    {
        const auto within = [](double value, double low, double high) {
            return std::isfinite(value) && value >= low && value <= high;
        };
        return (modelSlot == 0 || modelSlot == 1)
            && (position == NrPosition::PreAgc || position == NrPosition::PostAgc)
            && within(maskFloorDb, -50.0, -10.0)
            && within(alpha, 0.0, 4.0)
            && within(alphaKneeDb, 0.0, 40.0)
            && within(tauSeconds, 0.05, 30.0)
            && within(maxGainDb, 0.0, 24.0)
            && within(attackMs, 0.0, 500.0)
            && within(releaseMs, 0.0, 500.0);
    }
};

enum class NnrModelSource : int { Unavailable = 0, Bundled = 1, File = 2 };

// Readback only. Diagnostic processing modes intentionally have no normal
// save/load representation; they reset when the station session is replaced.
struct NnrDiagnostics {
    bool operator==(const NnrDiagnostics&) const = default;
    bool available{false};
    bool ready{false};
    bool running{false};
    bool rateSupported{false};
    int actualModelSlot{-1};
    std::array<bool, 2> modelAvailable{};
    std::array<NnrModelSource, 2> modelSources{};
    int dspRateHz{0};
    int networkRateHz{0};
    int delaySamples{0};
    double latencyMs{0.0};
    int testMode{0};
    int outputMode{1};
    bool profilingAvailable{false};
    QString explanation;
};

} // namespace NereusSDR
