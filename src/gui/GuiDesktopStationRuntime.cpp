// no-port-check: NereusSDR-original desktop Remote Access runtime.
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/GuiDesktopStationRuntime.h"

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/session/StationDevicesFacade.h"
#include "core/session/StationServer.h"
#include "core/station/StationHost.h"
#include "core/station/StationRadios.h"
#include "gui/SetupDialog.h"
#include "models/RadioModel.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>

namespace NereusSDR {
namespace {
constexpr auto kRun = "DesktopCore/Run";
constexpr auto kKeep = "DesktopCore/KeepRunning";
QString cleanPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}
QString when(const QString& iso)
{
    const QDateTime parsed = QDateTime::fromString(iso, Qt::ISODate);
    return parsed.isValid() ? QLocale().toString(parsed.toLocalTime(), QLocale::ShortFormat)
                            : QObject::tr("Never");
}
}

GuiDesktopStationRuntime::GuiDesktopStationRuntime(RadioModel* model, AppSettings* settings,
                                                   const QString& profile, bool profileOwned,
                                                   StationServiceOptions serviceOptions,
                                                   RuntimeStationBindings bindings,
                                                   QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_settings(settings)
    , m_profileOwned(profileOwned)
{
    if (settings) {
        m_profileDirectory = cleanPath(QFileInfo(settings->filePath()).absolutePath());
    }
    if (!profile.isEmpty() && !AppSettings::isValidProfileName(profile)) {
        m_configurationError = tr("The selected Core profile name is invalid.");
    } else if (settings && cleanPath(AppSettings::resolveConfigDir(profile))
                           != m_profileDirectory) {
        m_configurationError = tr("The selected Core profile does not match this window's settings.");
    }
    m_serviceOptions = std::move(serviceOptions);
    if (!m_serviceOptions.profileDirectory.isEmpty()
        && cleanPath(m_serviceOptions.profileDirectory) != m_profileDirectory) {
        m_configurationError = tr("The background station profile does not match this window's settings.");
    }
    if (!m_serviceOptions.profile.isEmpty() && m_serviceOptions.profile != profile) {
        m_configurationError = tr("The background station profile name does not match this window.");
    }
    m_serviceOptions.profile = profile;
    m_serviceOptions.profileDirectory = m_profileDirectory;
    m_serviceOptions.inheritActiveProfile = false;
    if (m_serviceOptions.homeDirectory.isEmpty()) {
        m_serviceOptions.homeDirectory = QDir::homePath();
    }
    m_service = std::make_unique<StationServiceManager>(m_serviceOptions);
    QString error;
    if (m_configurationError.isEmpty() && !loadConfig(&error)) {
        m_configurationError = error;
    }
    StationHostOptions hosting;
    hosting.settings = settings;
    hosting.securityDirectory = m_profileDirectory;
    hosting.stationRadios = bindings.stationRadios;
    hosting.selectedRadioMac = std::move(bindings.selectedRadioMac);
    hosting.linkMajors = std::move(bindings.linkMajors);
    const QString computer = QHostInfo::localHostName();
    hosting.hostingDevice = StationHostOptions::HostingDevice{
        computer.isEmpty() ? QStringLiteral("This computer") : computer,
        computer.section(QLatin1Char('.'), 0, 0)};
    hosting.coreName = m_config.coreName;
    hosting.remoteBind = m_config.remoteBind;
    hosting.remotePort = m_config.remotePort;
    hosting.statusPage = m_config.statusPage;
    hosting.statusPort = m_config.statusPort;
    hosting.pairingLanClickAllowed = m_config.pairingLanClickAllowed;
    hosting.remoteTransmitAllowed = m_config.remoteTransmitAllowed;
    hosting.supportConfigPath = m_config.sourcePath;
    hosting.audioBitrate = m_config.audioBitrate;
    hosting.audioLosslessAllowed = m_config.audioLosslessAllowed;
    hosting.displayAdaptive = m_config.displayAdaptive;
    hosting.displayBudgetLimits = m_config.displayBudgetLimits();
    hosting.rendezvousServers = m_config.rendezvousServers;
    hosting.relayAllowed = m_config.relayAllowed;
    m_controller = std::make_unique<DesktopStationController>(model, hosting);
    if (model) {
        connect(model, &RadioModel::coreOnAirChanged, this, [this] { updateState(); });
        connect(model, &RadioModel::connectionStateChanged, this, [this] { updateState(); });
        connect(model, &QObject::destroyed, this, [this] { m_model = nullptr; updateState(); });
    }
    m_keepRunning = settings && settings->value(QLatin1String(kKeep), false).toBool();
    m_startWithComputer = m_service->startsWithComputer();
    m_refreshTimer.setInterval(900);
    connect(&m_refreshTimer, &QTimer::timeout, this, [this] {
        if (m_controller && m_controller->host()) { updateState(); }
    });
    updateState();
}

GuiDesktopStationRuntime::GuiDesktopStationRuntime(RadioModel* model, AppSettings* settings,
                                                   const QString& profile, bool profileOwned,
                                                   StationServiceOptions serviceOptions,
                                                   QObject* parent)
    : GuiDesktopStationRuntime(model, settings, profile, profileOwned,
                               std::move(serviceOptions), {}, parent)
{
}

GuiDesktopStationRuntime::~GuiDesktopStationRuntime()
{
    stop();
}

bool GuiDesktopStationRuntime::available(QString* reason) const
{
    QString why;
    if (m_closed) {
        why = tr("This Core is closing.");
    } else if (!m_model || !m_settings) {
        why = tr("Connect this window to a local radio to manage its Core.");
    } else if (m_settings != &AppSettings::instance()) {
        why = tr("The Core requires this window's active settings store.");
    } else if (m_model->role() != RadioModel::Role::Local
               || m_settings->remoteBackend() != nullptr) {
        why = tr("Connect this window to a local radio to manage its Core.");
    } else if (!m_profileOwned) {
        why = tr("This window does not own the selected Core profile.");
    } else if (!m_configurationError.isEmpty()) {
        why = m_configurationError;
    }
    if (reason) { *reason = why; }
    return why.isEmpty();
}

bool GuiDesktopStationRuntime::actionAllowed(QString* reason) const
{
    if (!available(reason)) { return false; }
    QString why;
    if (m_actionActive || m_retiring || m_lifecycleBusy || m_model->connectionState() == ConnectionState::Probing
        || m_model->connectionState() == ConnectionState::Connecting) {
        why = tr("A Core change is in progress.");
    } else if (m_model->isCoreOnAir()) {
        why = tr("Setup controls are locked while transmitting.");
    }
    if (reason) { *reason = why; }
    return why.isEmpty();
}

bool GuiDesktopStationRuntime::loadConfig(QString* reason)
{
    if (reason) { reason->clear(); }
    const QString path = m_service->configPath();
    const QFileInfo info(path);
    if (info.isSymLink() && !info.exists()) {
        if (reason) { *reason = tr("The station configuration link is broken."); }
        return false;
    }
    if (info.exists()) {
        if (!info.isFile()) {
            if (reason) { *reason = tr("The station configuration is not a regular file."); }
            return false;
        }
        m_config = DaemonConfig::fromFile(path, reason);
        if (reason && !reason->isEmpty()) { return false; }
    } else {
        m_config = DaemonConfig::defaults();
    }
    if (!m_config.validate(reason)) { return false; }
    if (!m_config.stateDirectory.isEmpty()
        && cleanPath(m_config.stateDirectory) != m_profileDirectory) {
        if (reason) {
            *reason = tr("The station control socket must stay inside this profile.");
        }
        return false;
    }
    return true;
}

bool GuiDesktopStationRuntime::ensureBackgroundConfig(QString* reason)
{
    if (!loadConfig(reason)) { return false; }
    const QString path = m_service->configPath();
    if (QFileInfo(path).exists()) { return true; }
    if (!QDir().mkpath(m_profileDirectory)) {
        if (reason) { *reason = tr("Could not create the station profile folder."); }
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || file.write("# Desktop Core settings. Default listener, media and rendezvous options apply.\n") < 0
        || !file.commit()) {
        if (reason) { *reason = tr("Could not save the station configuration."); }
        return false;
    }
    return loadConfig(reason);
}

void GuiDesktopStationRuntime::fail(const QString& reason)
{
    emit operationFailed(reason.isEmpty() ? tr("The Core could not complete that change.") : reason);
    updateState();
}

bool GuiDesktopStationRuntime::restore()
{
    QString reason;
    if (!available(&reason)) { fail(reason); return false; }
    m_keepRunning = m_settings->value(QLatin1String(kKeep), false).toBool();
    if (!m_settings->value(QLatin1String(kRun), false).toBool()) {
        updateState();
        return true;
    }
    if (!m_controller->start(true)) {
        // start() may keep a Host alive for listener retry. A rejected Run
        // preference must never become a listener after the port frees up.
        m_controller->stop();
        fail(tr("The Core listener could not open. Check its station configuration and port."));
        return false;
    }
    attachHostSignals();
    m_refreshTimer.start();
    updateState();
    return true;
}

void GuiDesktopStationRuntime::setLifecycleBusy(bool busy)
{
    m_lifecycleBusy = busy;
    updateState();
}

bool GuiDesktopStationRuntime::setRunCore(bool enabled)
{
    QString reason;
    if (enabled) {
        if (!actionAllowed(&reason)) { fail(reason); return false; }
    } else {
        // Turning the Core off is the TX-first teardown, including a stale
        // page callback delivered while the radio was on the air.
        if (!available(&reason) || m_actionActive || m_retiring || m_lifecycleBusy
            || m_model->connectionState() == ConnectionState::Probing
            || m_model->connectionState() == ConnectionState::Connecting) {
            if (reason.isEmpty()) { reason = tr("A Core change is in progress."); }
            fail(reason);
            return false;
        }
    }
    if (enabled && m_controller->enabled()) {
        updateState();
        return true;
    }
    m_actionActive = true;
    if (enabled) {
        if (!m_controller->start(true)) {
            m_controller->stop();
            m_actionActive = false;
            fail(tr("The Core listener could not open. Check its station configuration and port."));
            return false;
        }
        attachHostSignals();
        m_settings->setValue(QLatin1String(kRun), true);
        if (!m_settings->save(&reason)) {
            m_settings->setValue(QLatin1String(kRun), false);
            m_controller->stop();
            m_actionActive = false;
            fail(reason);
            return false;
        }
        m_refreshTimer.start();
    } else {
        m_controller->stop(); // stopAllTx precedes listener shutdown in the controller.
        m_refreshTimer.stop();
        m_backgroundStartWanted = false;
        m_pendingBackgroundRequest = false;
        // Recheck the service entry even if a stale page snapshot said off:
        // an entry created since construction must not survive Run off.
        if (!m_service->setStartWithComputer(false)) {
            m_actionActive = false;
            fail(m_service->lastError());
            return false;
        }
        m_startWithComputer = false;
        m_keepRunning = false;
        m_settings->setValue(QLatin1String(kKeep), false);
        m_settings->setValue(QLatin1String(kRun), false);
        if (!m_settings->save(&reason)) {
            m_actionActive = false;
            fail(reason);
            return false;
        }
    }
    m_actionActive = false;
    updateState();
    return true;
}

bool GuiDesktopStationRuntime::setKeepRunning(bool enabled)
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    if (enabled && !m_controller->enabled()) {
        fail(tr("Run a Core on this computer first."));
        return false;
    }
    if (enabled && !ensureBackgroundConfig(&reason)) { fail(reason); return false; }
    const bool old = m_keepRunning;
    m_keepRunning = enabled;
    m_settings->setValue(QLatin1String(kKeep), enabled);
    if (!m_settings->save(&reason)) {
        m_keepRunning = old;
        m_settings->setValue(QLatin1String(kKeep), old);
        fail(reason);
        return false;
    }
    if (!enabled) { m_pendingBackgroundRequest = false; }
    updateState();
    return true;
}

bool GuiDesktopStationRuntime::setStartWithComputer(bool enabled)
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    if (enabled && !m_controller->enabled()) {
        fail(tr("Run a Core on this computer first."));
        return false;
    }
    if (enabled && !ensureBackgroundConfig(&reason)) { fail(reason); return false; }
    if (!m_service->setStartWithComputer(enabled)) {
        fail(m_service->lastError());
        return false;
    }
    m_startWithComputer = m_service->startsWithComputer();
    if (enabled && !m_startWithComputer) {
        fail(tr("The station startup entry could not be verified."));
        return false;
    }
    updateState();
    return true;
}

void GuiDesktopStationRuntime::attachHostSignals()
{
    StationServer* server = m_controller->server();
    if (!server) { return; }
    connect(server, &StationServer::listeningChanged, this, [this] { updateState(); });
    if (StationDevicesFacade* facade = server->devicesFacade()) {
        connect(facade, &StationDevicesFacade::devicesStateChanged,
                this, [this] { updateState(); });
    }
}

bool GuiDesktopStationRuntime::renameStation(const QString& name)
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    StationDevicesFacade* facade = m_controller->server()
        ? m_controller->server()->devicesFacade() : nullptr;
    if (!facade) { fail(tr("Run a Core on this computer first.")); return false; }
    const DeviceAdminResult result = facade->rename(name);
    if (!result.accepted) { fail(result.reason); return false; }
    updateState();
    return true;
}

bool GuiDesktopStationRuntime::revokeDevice(const QByteArray& id)
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    StationDevicesFacade* facade = m_controller->server()
        ? m_controller->server()->devicesFacade() : nullptr;
    if (!facade) { fail(tr("Run a Core on this computer first.")); return false; }
    const DeviceAdminResult result = facade->revoke(QString::fromLatin1(id));
    if (!result.accepted) { fail(result.reason); return false; }
    updateState();
    return true;
}

bool GuiDesktopStationRuntime::addDevice()
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    StationDevicesFacade* facade = m_controller->server()
        ? m_controller->server()->devicesFacade() : nullptr;
    if (!facade) { fail(tr("Run a Core on this computer first.")); return false; }
    const DeviceAdminResult result = facade->openPairing();
    if (!result.accepted) { fail(result.reason); return false; }
    updateState();
    return true;
}

bool GuiDesktopStationRuntime::acknowledgeKeyBackup()
{
    QString reason;
    if (!actionAllowed(&reason)) { fail(reason); return false; }
    StationDevicesFacade* facade = m_controller->server()
        ? m_controller->server()->devicesFacade() : nullptr;
    if (!facade) { fail(tr("Run a Core on this computer first.")); return false; }
    const DeviceAdminResult result = facade->acknowledgeKeyBackup();
    if (!result.accepted) { fail(result.reason); return false; }
    updateState();
    return true;
}

void GuiDesktopStationRuntime::bindSetupDialog(SetupDialog* dialog)
{
    if (!dialog) { return; }
    dialog->setRemoteStationPageBinder([self = QPointer<GuiDesktopStationRuntime>(this)](
                                           RemoteStationPage* page) {
        if (self) { self->bindPage(page); }
    });
}

void GuiDesktopStationRuntime::bindPage(RemoteStationPage* page)
{
    if (!page || m_pages.contains(page)) { return; }
    m_pages.append(page);
    connect(page, &RemoteStationPage::runCoreRequested, this,
            [this](bool enabled) { setRunCore(enabled); });
    connect(page, &RemoteStationPage::keepRunningRequested, this,
            [this](bool enabled) { setKeepRunning(enabled); });
    connect(page, &RemoteStationPage::startWithComputerRequested, this,
            [this](bool enabled) { setStartWithComputer(enabled); });
    connect(page, &RemoteStationPage::renameRequested, this,
            [this](const QString& name) { renameStation(name); });
    connect(page, &RemoteStationPage::revokeRequested, this,
            [this](const QByteArray& id) { revokeDevice(id); });
    connect(page, &RemoteStationPage::addDeviceRequested, this,
            [this] { addDevice(); });
    connect(page, &RemoteStationPage::keyBackupAcknowledgedRequested, this,
            [this] { acknowledgeKeyBackup(); });
    page->setState(m_state);
}

void GuiDesktopStationRuntime::refresh()
{
    m_startWithComputer = m_service->startsWithComputer();
    updateState();
}

void GuiDesktopStationRuntime::updateState()
{
    RemoteStationPage::State next;
    QString reason;
    next.available = available(&reason);
    next.unavailableReason = reason;
    next.busy = m_actionActive || m_retiring || m_lifecycleBusy
        || (m_model && (m_model->connectionState() == ConnectionState::Probing
                        || m_model->connectionState() == ConnectionState::Connecting));
    next.transmitting = m_model && m_model->isCoreOnAir();
    next.runCore = m_controller && m_controller->enabled();
    next.keepRunning = m_keepRunning && next.runCore;
    next.startWithComputer = m_startWithComputer;
    next.stationName = m_controller && m_controller->host()
        ? m_controller->host()->coreLabel() : QString();
    if (!next.runCore) {
        next.reachabilityText = tr("No Core listener is open from this window.");
    } else {
        const QString bind = m_config.remoteBind.isEmpty()
            ? tr("all configured interfaces") : m_config.remoteBind;
        next.reachabilityText = tr("Listener open on %1, port %2. LAN discovery and remote "
                                   "registration are not verified here.")
                                    .arg(bind).arg(m_config.remotePort);
    }
    if (next.startWithComputer) {
        switch (m_service->startupMode()) {
        case StationServiceManager::StartupMode::Boot:
            next.reachabilityText += tr(" Background Core starts at boot.");
            break;
        case StationServiceManager::StartupMode::Login:
            next.reachabilityText += tr(" Background Core starts at login.");
            break;
        case StationServiceManager::StartupMode::Failed:
            next.reachabilityText += tr(" Background startup could not be verified.");
            break;
        case StationServiceManager::StartupMode::Disabled:
            next.reachabilityText += tr(" Background startup entry is enabled.");
            break;
        }
    }
    StationServer* server = m_controller ? m_controller->server() : nullptr;
    StationDevicesFacade* facade = server ? server->devicesFacade() : nullptr;
    if (facade) {
        next.stationName = facade->stationLabel().isEmpty()
            ? next.stationName : facade->stationLabel();
        next.pairingOpen = facade->pairingWindowOpen();
        next.pairingCode = facade->pairingCode();
        next.keyBackupPath = facade->keyPath();
        next.keyBackupAcknowledged = facade->keyBackupAcknowledged();
        const QJsonArray list = QJsonDocument::fromJson(facade->listJson().toUtf8()).array();
        const bool lastWithNoToken = !facade->tokenActive() && list.size() <= 1;
        DeviceStore* store = server->deviceStore();
        for (const QJsonValue& value : list) {
            const QJsonObject device = value.toObject();
            RemoteStationPage::Device row;
            row.id = device.value(QStringLiteral("id")).toString().toLatin1();
            row.name = device.value(QStringLiteral("name")).toString();
            row.pairedText = when(device.value(QStringLiteral("pairedAt")).toString());
            row.lastSeenText = when(device.value(QStringLiteral("lastSeen")).toString());
            bool ok = false;
            const QByteArray raw = StationIdentity::fromBase64Url(QString::fromLatin1(row.id), &ok);
            const std::optional<PairedDevice> paired = ok && store
                ? store->find(raw) : std::nullopt;
            row.revocable = paired && !lastWithNoToken
                && !(paired->enrolledThroughToken && facade->tokenActive());
            next.devices.append(row);
        }
    }
    if (sameState(next, m_state)) { return; }
    m_state = next;
    for (auto it = m_pages.begin(); it != m_pages.end();) {
        if (!*it) { it = m_pages.erase(it); }
        else { (*it)->setState(m_state); ++it; }
    }
    emit stateChanged();
}

bool GuiDesktopStationRuntime::sameState(const RemoteStationPage::State& a,
                                          const RemoteStationPage::State& b)
{
    if (a.available != b.available || a.unavailableReason != b.unavailableReason
        || a.runCore != b.runCore || a.keepRunning != b.keepRunning
        || a.startWithComputer != b.startWithComputer || a.busy != b.busy
        || a.transmitting != b.transmitting || a.stationName != b.stationName
        || a.reachabilityText != b.reachabilityText || a.pairingCode != b.pairingCode
        || a.keyBackupPath != b.keyBackupPath || a.pairingOpen != b.pairingOpen
        || a.keyBackupAcknowledged != b.keyBackupAcknowledged
        || a.devices.size() != b.devices.size()) { return false; }
    for (qsizetype i = 0; i < a.devices.size(); ++i) {
        const auto& x = a.devices.at(i);
        const auto& y = b.devices.at(i);
        if (x.id != y.id || x.name != y.name || x.pairedText != y.pairedText
            || x.lastSeenText != y.lastSeenText || x.revocable != y.revocable) { return false; }
    }
    return true;
}

void GuiDesktopStationRuntime::stop()
{
    m_closed = true;
    if (!m_retirementPrepared) { m_backgroundStartWanted = false; }
    m_pendingBackgroundRequest = false;
    m_refreshTimer.stop();
    if (m_controller) { m_controller->stop(); }
    updateState();
}

bool GuiDesktopStationRuntime::prepareForRetirement(bool requestBackground, QString* error)
{
    if (error) { error->clear(); }
    QString reason;
    if (!available(&reason) || m_lifecycleBusy || m_actionActive || m_retiring) {
        if (reason.isEmpty()) { reason = tr("A Core change is in progress."); }
        if (error) { *error = reason; }
        fail(reason);
        return false;
    }
    m_backgroundStartWanted = false;
    if (!requestBackground) { m_pendingBackgroundRequest = false; }
    const bool wantsBackground = requestBackground
        && (m_controller->enabled() || m_pendingBackgroundRequest)
        && m_keepRunning && m_settings->value(QLatin1String(kRun), false).toBool();
    m_pendingBackgroundRequest = wantsBackground;
    m_retiring = true;
    updateState();
    // A checked retirement ends RF first. The caller disconnects and unlocks
    // only after this method returns true.
    m_model->stopAllTx(tr("The desktop Core is closing."));
    m_controller->stop();
    m_refreshTimer.stop();
    if (wantsBackground && !ensureBackgroundConfig(&reason)) {
        m_retiring = false;
        if (error) { *error = reason; }
        fail(reason);
        return false;
    }
    if (m_model->isConnected()) {
        const QString mac = m_model->currentRadioInfo().macAddress.trimmed();
        if (!mac.isEmpty()) {
            m_settings->setValue(QLatin1String(StationRadios::kChoiceKey), mac);
        }
    }
    if (!m_model->saveForStationHandover(&reason) || !m_settings->save(&reason)) {
        m_retiring = false;
        if (error) { *error = reason; }
        fail(reason);
        return false;
    }
    m_backgroundStartWanted = wantsBackground;
    m_pendingBackgroundRequest = false;
    m_retirementPrepared = true;
    m_closed = true;
    updateState();
    return true;
}

} // namespace NereusSDR
