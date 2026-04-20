#pragma once

#include "gui/SetupPage.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Audio > TCI
//
// Placeholder page for the TCI server configuration UI — the documented
// terminal state for Sub-Phase 12. Full implementation lands in Phase 3J
// (see design spec §11.1).
// ---------------------------------------------------------------------------
class AudioTciPage : public SetupPage {
    Q_OBJECT
public:
    explicit AudioTciPage(RadioModel* model, QWidget* parent = nullptr);
};

} // namespace NereusSDR
