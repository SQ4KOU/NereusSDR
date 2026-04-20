#include "AudioVaxPage.h"

namespace NereusSDR {

AudioVaxPage::AudioVaxPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("VAX"), model, parent)
{
    addSection(QStringLiteral("Coming in Task 12.3"));
    addLabeledLabel(QStringLiteral("Status"),
                    QStringLiteral("Scaffolding only — populated in Task 12.3"));
}

} // namespace NereusSDR
