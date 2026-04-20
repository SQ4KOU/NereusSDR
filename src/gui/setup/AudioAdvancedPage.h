#pragma once

#include "gui/SetupPage.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Audio > Advanced
//
// Hosts DSP rate/block, per-VAX VAC feedback tuning, feature flags,
// detected-cables readout, and reset-to-defaults. Scaffolding shell in
// Sub-Phase 12 Task 12.1; populated in Task 12.4.
// ---------------------------------------------------------------------------
class AudioAdvancedPage : public SetupPage {
    Q_OBJECT
public:
    explicit AudioAdvancedPage(RadioModel* model, QWidget* parent = nullptr);
};

} // namespace NereusSDR
