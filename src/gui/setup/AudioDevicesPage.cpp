#include "AudioDevicesPage.h"

namespace NereusSDR {

AudioDevicesPage::AudioDevicesPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Devices"), model, parent)
{
    addSection(QStringLiteral("Coming in Task 12.2"));
    addLabeledLabel(QStringLiteral("Status"),
                    QStringLiteral("Scaffolding only — populated in Task 12.2"));
}

} // namespace NereusSDR
