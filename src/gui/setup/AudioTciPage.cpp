#include "AudioTciPage.h"

namespace NereusSDR {

AudioTciPage::AudioTciPage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("TCI"), model, parent)
{
    addSection(QStringLiteral("Coming in Phase 3J (see design spec §11.1)"));
    addLabeledLabel(QStringLiteral("Status"),
                    QStringLiteral("Coming in Phase 3J (see design spec §11.1)"));
}

} // namespace NereusSDR
