#include "AudioAdvancedPage.h"

namespace NereusSDR {

AudioAdvancedPage::AudioAdvancedPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Advanced"), model, parent)
{
    addSection(QStringLiteral("Coming in Task 12.4"));
    addLabeledLabel(QStringLiteral("Status"),
                    QStringLiteral("Scaffolding only — populated in Task 12.4"));
}

} // namespace NereusSDR
