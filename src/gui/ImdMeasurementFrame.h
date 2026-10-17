#pragma once
// no-port-check: NereusSDR-original GUI measurement contract; no DSP authority.
// Modification history (NereusSDR):
//   2026-10-13: J.J. Boyd (KG4VCF), OpenAI Codex; cached IMD presentation.
#include "gui/ImdOverlay.h"
#include <QMetaType>
#include <array>
#include <optional>
namespace NereusSDR {
enum class ImdStatus { Disabled, WaitingForTx, DuplexRequired,
    WaitingForSpectrum, FundamentalsUnresolved, ProductsOutsideView,
    ProductsMissing, Valid };
enum class ImdProduct { FundamentalLower, FundamentalUpper,
    ThirdLower, ThirdUpper, FifthLower, FifthUpper };
struct ImdInputContext {
    quint64 frameSequence{0};
    quint64 contextRevision{0};
    qint64 acceptedAtMs{0};
    double firstSampleHz{0.0};
    double sampleStepHz{0.0};
    bool mox{false};
    bool testing{false};
    bool measurementsEnabled{false};
    bool duplex{false};
    bool sourceEligible{false};
};
struct ImdPeakReading {
    int sampleIndex{0};
    double frequencyHz{0.0};
    float absoluteDbm{0.0f};
    std::optional<float> relativeDbc;
};
struct ImdMeasurementFrame {
    ImdStatus status{ImdStatus::Disabled};
    ImdInputContext context;
    std::array<std::optional<ImdPeakReading>, 6> peaks;
    std::array<std::optional<double>, 6> expectedFrequencyHz;
    std::optional<ImdReadout> summary;
};
} // namespace NereusSDR
Q_DECLARE_METATYPE(NereusSDR::ImdMeasurementFrame)
