// =================================================================
// src/core/ReceiveLayoutStore.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original Core receive-layout persistence adapter.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-22: Original implementation for R-R3-34 by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Codex.
// =================================================================

#pragma once

#include "core/WdspTypes.h"

#include <QList>
#include <QString>

#include <optional>

namespace NereusSDR {

class AppSettings;

/// Stable, data-only receive state owned by the Core restart manifest.
struct ReceiveSliceState {
    int id {0};
    QString panKey;
    double frequencyHz {0.0};
    DSPMode dspMode {DSPMode::USB};
};

/// Per-radio persistence adapter for the bounded receive-layout manifest.
///
/// This class only validates and stages AppSettings values. It deliberately
/// does not construct a SliceModel, allocate a DDC, start DSP, or write disk.
class ReceiveLayoutStore final {
public:
    enum class LoadState {
        Missing,
        Loaded,
        InvalidIdentity,
        InvalidData,
    };

    struct LoadResult {
        LoadState state {LoadState::Missing};
        QList<ReceiveSliceState> slices;
        QString error;
        // Decoded receive-audio owner only; never a TX or active-slice choice.
        std::optional<int> radeRxOwnerId{std::nullopt};
    };

    /// Decode a per-MAC receive layout. Invalid stored bytes are retained.
    static LoadResult load(const AppSettings& settings, const QString& mac);

    /// Validate then replace the in-memory manifest. Does not call save().
    /// Empty pan keys are normalized to pan-0 before validation/serialization.
    /// radeRxOwnerId is the decoded receive-audio owner, never TX/active state.
    static bool stage(AppSettings& settings, const QString& mac,
                      const QList<ReceiveSliceState>& slices,
                      QString* error = nullptr,
                      std::optional<int> radeRxOwnerId = std::nullopt);

    /// Validate already-decoded descriptor state and receive-audio ownership.
    static bool validate(const QList<ReceiveSliceState>& slices,
                         QString* error = nullptr,
                         std::optional<int> radeRxOwnerId = std::nullopt);
};

} // namespace NereusSDR
