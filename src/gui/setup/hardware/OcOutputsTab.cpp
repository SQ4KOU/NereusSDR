#include "OcOutputsTab.h"
#include "models/RadioModel.h"
#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

OcOutputsTab::OcOutputsTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("OC Outputs — populated in Phase 3I Task 19"), this));
    l->addStretch();
}

void OcOutputsTab::populate(const RadioInfo&, const BoardCapabilities&)
{
    // Task 19 implements this.
}

} // namespace NereusSDR
