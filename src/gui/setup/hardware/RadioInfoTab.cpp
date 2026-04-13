#include "RadioInfoTab.h"
#include "models/RadioModel.h"
#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

RadioInfoTab::RadioInfoTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("Radio Info — populated in Phase 3I Task 19"), this));
    l->addStretch();
}

void RadioInfoTab::populate(const RadioInfo&, const BoardCapabilities&)
{
    // Task 19 implements this.
}

} // namespace NereusSDR
