#pragma once

#include "gui/SetupPage.h"

namespace NereusSDR {

// ---------------------------------------------------------------------------
// Audio > Devices
//
// Hosts the Speakers / Headphones / TX Input device cards. Scaffolding shell
// in Sub-Phase 12 Task 12.1; populated in Task 12.2.
// ---------------------------------------------------------------------------
class AudioDevicesPage : public SetupPage {
    Q_OBJECT
public:
    explicit AudioDevicesPage(RadioModel* model, QWidget* parent = nullptr);
};

} // namespace NereusSDR
