// no-port-check: NereusSDR-original. R-R3-38 complete station-session ownership.
#pragma once

#include "gui/StationStartupSelection.h"

#include <QObject>
#include <memory>

namespace NereusSDR {
class MainWindow;
class SettingsProxy;

// Roles are immutable. Switching stations replaces the whole operating
// window/model/session; ordinary reconnect stays inside the existing window.
class GuiSessionCoordinator final : public QObject {
    Q_OBJECT
public:
    explicit GuiSessionCoordinator(QObject* parent = nullptr);
    ~GuiSessionCoordinator() override;

    MainWindow* window() const { return m_window.get(); }
    StationStartupSelection selection() const { return m_selection; }
    quint64 generation() const { return m_generation; }
    bool canReplace(const StationStartupSelection&, QString* error = nullptr) const;

    // Synchronous retirement: UI callers must queue this after the outgoing
    // widget's event handler returns. Reentrant and active-TX switches fail
    // before touching the active session. Does not persist the selection.
    bool replace(const StationStartupSelection&, bool startConnection,
                 QString* error = nullptr);
    void shutdown();

    /// iPhone app Task 18 (R-IOS-08): the live window's client enrolled
    /// this computer's key with its Core. The current selection is trusted
    /// by that identity from now on, as the saved Core is, so the two still
    /// match; the window itself is kept.
    void noteStationIdentity(const QByteArray& identityFingerprint)
    {
        m_selection.connection.identityFingerprint = identityFingerprint;
    }

signals:
    void windowChanged(NereusSDR::MainWindow* window);
    void connectionsRequested();

private:
    void retireWindow();

    std::unique_ptr<SettingsProxy> m_proxy;
    std::unique_ptr<MainWindow> m_window;
    StationStartupSelection m_selection;
    quint64 m_generation = 0;
    bool m_replacing = false;
};
} // namespace NereusSDR
