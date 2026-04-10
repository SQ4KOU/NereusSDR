#pragma once

#include <QObject>
#include <QList>
#include <QMap>
#include <QSize>

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

    // --- Container lifecycle ---
    ContainerWidget* createContainer(int rxSource, DockMode mode);
    void destroyContainer(const QString& id);

    // --- Dock mode transitions ---
    void floatContainer(const QString& id);
    void dockContainer(const QString& id);
    void panelDockContainer(const QString& id);
    void overlayDockContainer(const QString& id);
    void recoverContainer(const QString& id);

    // --- Axis-lock repositioning (overlay-docked only) ---
    void updateDockedPositions(int hDelta, int vDelta);

    // --- Panel width (QSplitter state) ---
    void saveSplitterState();
    void restoreSplitterState();

    // --- Persistence ---
    void saveState();
    void restoreState();

    // --- Queries ---
    QList<ContainerWidget*> allContainers() const;
    ContainerWidget* container(const QString& id) const;
    ContainerWidget* panelContainer() const;
    int containerCount() const;

    // --- Visibility ---
    void setContainerVisible(const QString& id, bool visible);

signals:
    void containerAdded(const QString& id);
    void containerRemoved(const QString& id);

private:
    void setMeterFloating(ContainerWidget* container, FloatingContainer* form);
    void returnMeterFromFloating(ContainerWidget* container, FloatingContainer* form);
    void wireContainer(ContainerWidget* container);

    QWidget* m_dockParent{nullptr};
    QSplitter* m_splitter{nullptr};
    QMap<QString, ContainerWidget*> m_containers;
    QMap<QString, FloatingContainer*> m_floatingForms;
    QString m_panelContainerId;
};

} // namespace NereusSDR
