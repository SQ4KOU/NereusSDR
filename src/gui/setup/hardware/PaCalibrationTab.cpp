#include "PaCalibrationTab.h"
#include "models/RadioModel.h"
#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include <QLabel>
#include <QVBoxLayout>

namespace NereusSDR {

PaCalibrationTab::PaCalibrationTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* l = new QVBoxLayout(this);
    l->addWidget(new QLabel(tr("PA Calibration — populated in Phase 3I Task 20"), this));
    l->addStretch();
}

void PaCalibrationTab::populate(const RadioInfo&, const BoardCapabilities&)
{
    // Task 20 implements this.
}

} // namespace NereusSDR
