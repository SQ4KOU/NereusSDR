#pragma once

#include "gui/SetupPage.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Audio > VAX
//
// Hosts the four VAX channel cards + TX row + Auto-detect picker. Scaffolding
// shell in Sub-Phase 12 Task 12.1; populated in Task 12.3.
// ---------------------------------------------------------------------------
class AudioVaxPage : public SetupPage {
    Q_OBJECT
public:
    explicit AudioVaxPage(RadioModel* model, QWidget* parent = nullptr);
};

} // namespace NereusSDR
