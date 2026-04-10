#pragma once

#include <QObject>
#include <QMap>

class QSplitter;

namespace NereusSDR {

class ContainerWidget;
class FloatingContainer;
enum class DockMode;

class ContainerManager : public QObject {
    Q_OBJECT
public:
    explicit ContainerManager(QWidget* dockParent, QSplitter* splitter,
                              QObject* parent = nullptr);
    ~ContainerManager() override;

private:
    QWidget* m_dockParent{nullptr};
    QSplitter* m_splitter{nullptr};
};

} // namespace NereusSDR
