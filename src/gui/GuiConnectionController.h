// no-port-check: NereusSDR-original. R-R3-38 operator target selection.
#pragma once

#include "core/RadioDiscovery.h"
#include "core/session/StationLanDiscovery.h"
#include "gui/CoreTargetStore.h"
#include "gui/GuiSessionCoordinator.h"

#include <QMap>
#include <QPointer>

namespace NereusSDR {
class ConnectionSelector;
class RemoteConnectionController;

// Application-scoped connection UI. Selecting/editing a row never replaces
// the live session; an explicit Connect queues retirement after the originating
// widget's event handler has returned.
class GuiConnectionController final : public QObject {
    Q_OBJECT
public:
    explicit GuiConnectionController(QObject* parent = nullptr);
    ~GuiConnectionController() override;
    void start(const StationStartupRequest&);
    void shutdown();
    GuiSessionCoordinator* sessions() { return &m_sessions; }
    ConnectionSelector* selector() const { return m_selector.get(); }

public slots:
    void showConnections();

private:
    void attachWindow(MainWindow*);
    void refresh();
    void scan();
    void queueConnect(const QString& key);
    void connectTarget(const QString& key);
    void disconnectCurrent();
    void editCore(const QString& id = {});
    void editRadio(const QString& mac = {});
    void forgetTarget(const QString& key);
    void showDetails(const QString& key);
    void rememberAuthenticatedRadio();
    bool choose(const StationStartupSelection&, bool startConnection);

    CoreTargetStore m_store;
    GuiSessionCoordinator m_sessions;
    StationLanDiscovery m_lan;
    std::unique_ptr<ConnectionSelector> m_selector;
    QPointer<RadioDiscovery> m_discovery;
    QPointer<RemoteConnectionController> m_remoteControls;
    QMap<QString, RadioInfo> m_radios;
    QMap<QString, qint64> m_seenAt;
    QList<QMetaObject::Connection> m_windowConnections;
    quint64 m_request = 0;
    bool m_storeLoaded = false;
    bool m_shuttingDown = false;
};
} // namespace NereusSDR
