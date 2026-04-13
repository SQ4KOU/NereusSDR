#include "Hl2IoBoardTab.h"
#include "models/RadioModel.h"
#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

Hl2IoBoardTab::Hl2IoBoardTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("HL2 I/O Board — populated in Phase 3I Task 20"), this));
    l->addStretch();
}

void Hl2IoBoardTab::populate(const RadioInfo&, const BoardCapabilities&)
{
    // Task 20 implements this.
}

} // namespace NereusSDR
