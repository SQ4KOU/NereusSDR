#include "FloatingContainer.h"
#include "ContainerWidget.h"
#include "core/LogCategories.h"

namespace NereusSDR {

FloatingContainer::FloatingContainer(int rxSource, QWidget* parent)
    : QWidget(parent, Qt::Window | Qt::Tool)
    , m_rxSource(rxSource)
{
    setMinimumSize(ContainerWidget::kMinContainerWidth,
                   ContainerWidget::kMinContainerHeight);
}

FloatingContainer::~FloatingContainer() = default;

} // namespace NereusSDR
