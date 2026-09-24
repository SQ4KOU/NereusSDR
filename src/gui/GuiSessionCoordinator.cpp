// no-port-check: NereusSDR-original. R-R3-38 complete station-session ownership.
#include "gui/GuiSessionCoordinator.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsProxy.h"
#include "gui/MainWindow.h"
#include "models/RadioModel.h"

#include <QApplication>
#include <QScopedValueRollback>
#include <QScopeGuard>

namespace NereusSDR {

GuiSessionCoordinator::GuiSessionCoordinator(QObject* parent) : QObject(parent) {}

GuiSessionCoordinator::~GuiSessionCoordinator()
{
    shutdown();
}

bool GuiSessionCoordinator::canReplace(const StationStartupSelection& selection, QString* error) const
{
    if (error) { error->clear(); }
    const auto fail = [error](const QString& message) {
        if (error) { *error = message; }
        return false;
    };
    if (m_replacing) {
        return fail(tr("A Core switch is already in progress."));
    }
    if (selection.connection.isRemote()
        && !RemoteStationOptions::isValidStationUrl(selection.connection.url)) {
        return fail(tr("The selected Core has an invalid address."));
    }
    if (m_window) {
        RadioModel* model = m_window->radioModel();
        if (model->localConnectionSetupActive()) {
            return fail(tr("Local radio initialization is still finishing. Disconnect can cancel the connection; switch when initialization has finished."));
        }
        // Remote transmit state is mirrored in TransmitModel; its local
        // MoxController intentionally never keys this computer's hardware.
        if (model->mox() || model->isTune() || model->transmitModel().isMox()
            || model->transmitModel().isTune()) {
            return fail(tr("End transmit or Tune before switching Cores."));
        }
    }

    return true;
}

bool GuiSessionCoordinator::replace(const StationStartupSelection& selection,
                                    bool startConnection, QString* error)
{
    if (!canReplace(selection, error)) { return false; }
    const QScopedValueRollback<bool> replacing(m_replacing, true);
    const bool quitOnClose = QApplication::quitOnLastWindowClosed();
    QApplication::setQuitOnLastWindowClosed(false);
    const auto restoreQuit = qScopeGuard([quitOnClose] {
        QApplication::setQuitOnLastWindowClosed(quitOnClose);
    });

    ++m_generation;
    retireWindow();
    m_selection = selection;
    if (selection.connection.isRemote()) {
        m_proxy = std::make_unique<SettingsProxy>();
        // This must precede every model constructor: an unready proxy keeps
        // local defaults from being written into another Core's settings.
        AppSettings::instance().setRemoteBackend(m_proxy.get());
    }
    m_window = std::make_unique<MainWindow>(selection.connection, nullptr,
                                           MainWindow::ConnectionStartup::Deferred);
    m_window->setConnectionPickerManaged(true);
    const quint64 generation = m_generation;
    connect(m_window.get(), &MainWindow::connectionsRequested, this, [this, generation] {
        // Queued events survive disconnect. An old window must not open a
        // picker over the replacement station or trigger a second switch.
        if (generation == m_generation && m_window) { emit connectionsRequested(); }
    }, Qt::QueuedConnection);
    m_window->show();
    emit windowChanged(m_window.get());
    if (startConnection) { m_window->startInitialConnection(); }
    return true;
}

void GuiSessionCoordinator::retireWindow()
{
    if (m_window) {
        m_window->retireForSessionSwitch();
        m_window.reset();
    }
    // AppSettings' backend is non-owning. It stays available throughout the
    // outgoing window/model destruction and is detached before proxy release.
    AppSettings::instance().setRemoteBackend(nullptr);
    m_proxy.reset();
}

void GuiSessionCoordinator::shutdown()
{
    if (m_replacing || (!m_window && !m_proxy)) { return; }
    const QScopedValueRollback<bool> replacing(m_replacing, true);
    ++m_generation;
    const bool quitOnClose = QApplication::quitOnLastWindowClosed();
    QApplication::setQuitOnLastWindowClosed(false);
    retireWindow();
    QApplication::setQuitOnLastWindowClosed(quitOnClose);
    emit windowChanged(nullptr);
}

} // namespace NereusSDR
