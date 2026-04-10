#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "FloatingContainer.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QSplitter>

namespace NereusSDR {

ContainerManager::ContainerManager(QWidget* dockParent, QSplitter* splitter,
                                   QObject* parent)
    : QObject(parent)
    , m_dockParent(dockParent)
    , m_splitter(splitter)
{
    qCDebug(lcContainer) << "ContainerManager created";
}

ContainerManager::~ContainerManager()
{
    qCDebug(lcContainer) << "ContainerManager destroyed";
}

} // namespace NereusSDR
