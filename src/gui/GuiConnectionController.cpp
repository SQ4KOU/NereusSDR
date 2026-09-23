// no-port-check: NereusSDR-original. R-R3-38 operator target selection.
#include "gui/GuiConnectionController.h"

#include "core/AppSettings.h"
#include "core/session/StationClient.h"
#include "gui/AddCustomRadioDialog.h"
#include "gui/ConnectionPanel.h"
#include "gui/ConnectionSelector.h"
#include "gui/CoreTargetEditor.h"
#include "gui/MainWindow.h"
#include "gui/OperatorReasonText.h"
#include "gui/StationLanSelection.h"
#include "gui/RemoteConnectionController.h"
#include "models/RadioModel.h"

#include <QDateTime>
#include <QTimer>
#include <QUrl>

namespace NereusSDR {
namespace {
QString endpointText(const RemoteStationOptions& connection)
{
    const QUrl url(connection.url);
    QString host = url.host();
    if (host.contains(QLatin1Char(':'))) { host = QLatin1Char('[') + host + QLatin1Char(']'); }
    return url.port() >= 0 ? host + QLatin1Char(':') + QString::number(url.port()) : host;
}

bool sameConnection(const RemoteStationOptions& a, const RemoteStationOptions& b)
{
    return QUrl(a.url) == QUrl(b.url) && a.token == b.token
        && a.fingerprint == b.fingerprint && a.allowUnpinned == b.allowUnpinned;
}

bool selectionMatchesSaved(const StationStartupSelection& selection, const SavedCoreTarget& target)
{
    RemoteStationOptions original = selection.connection;
    if (!selection.savedAddressBeforeDiscovery.isEmpty()) {
        original.url = selection.savedAddressBeforeDiscovery;
        // Discovered connections always enforce the saved pin, even if a
        // record also carried the legacy bench flag.
        original.allowUnpinned = target.connection.allowUnpinned;
    }
    return sameConnection(original, target.connection);
}
}

GuiConnectionController::GuiConnectionController(QObject* parent)
    : QObject(parent), m_store(AppSettings::instance()), m_lan(this), m_selector(std::make_unique<ConnectionSelector>())
{
    connect(&m_lan, &StationLanDiscovery::changed, this, &GuiConnectionController::refresh);
    connect(&m_sessions, &GuiSessionCoordinator::windowChanged,
            this, &GuiConnectionController::attachWindow);
    connect(&m_sessions, &GuiSessionCoordinator::connectionsRequested,
            this, &GuiConnectionController::showConnections);
    connect(m_selector.get(), &ConnectionSelector::connectRequested,
            this, &GuiConnectionController::queueConnect);
    connect(m_selector.get(), &ConnectionSelector::disconnectRequested,
            this, &GuiConnectionController::disconnectCurrent);
    connect(m_selector.get(), &ConnectionSelector::addCoreRequested,
            this, [this] { editCore(); });
    connect(m_selector.get(), &ConnectionSelector::addRadioRequested,
            this, [this] { editRadio(); });
    connect(m_selector.get(), &ConnectionSelector::editRequested, this, [this](const QString& key) {
        if (key.startsWith(QLatin1String("saved:"))) { editCore(key.mid(6)); }
        else if (key.startsWith(QLatin1String("radio:"))) { editRadio(key.mid(6)); }
        else if (key == QLatin1String("current")) { editCore(); }
    });
    connect(m_selector.get(), &ConnectionSelector::forgetRequested,
            this, &GuiConnectionController::forgetTarget);
    connect(m_selector.get(), &ConnectionSelector::detailsRequested,
            this, &GuiConnectionController::showDetails);
    connect(m_selector.get(), &ConnectionSelector::scanRequested,
            this, &GuiConnectionController::scan);
}

GuiConnectionController::~GuiConnectionController() { shutdown(); }

void GuiConnectionController::start(const StationStartupRequest& request)
{
    QString error;
    m_storeLoaded = m_store.load(&error);
    const auto selected = m_storeLoaded ? resolveStationStartup(request, m_store, &error)
                                        : std::nullopt;
    // A corrupt address book or bad CLI must not start the old local radio
    // implicitly. Open an idle local-capable window with a visible error.
    m_sessions.replace(selected.value_or(StationStartupSelection{}), selected.has_value());
    if (!error.isEmpty()) {
        m_selector->setNotice(error);
        showConnections();
    }
}

void GuiConnectionController::shutdown()
{
    if (m_shuttingDown) { return; }
    m_shuttingDown = true;
    ++m_request;
    m_selector->hide();
    m_lan.stop();
    m_sessions.shutdown();
}

void GuiConnectionController::attachWindow(MainWindow* window)
{
    for (const QMetaObject::Connection& connection : m_windowConnections) {
        QObject::disconnect(connection);
    }
    m_windowConnections.clear();
    m_remoteControls = nullptr;
    m_discovery = nullptr;
    m_radios.clear();
    m_seenAt.clear();
    if (!window || m_shuttingDown) { return; }
    const quint64 generation = m_sessions.generation();
    RadioModel* model = window->radioModel();
    m_discovery = model->discovery();
    m_remoteControls = window->findChild<RemoteConnectionController*>();
    const auto update = [this, generation] {
        if (generation == m_sessions.generation() && !m_shuttingDown) { refresh(); }
    };
    m_windowConnections.append(connect(model, &RadioModel::connectionStateChanged, this, update));
    m_windowConnections.append(connect(model, &RadioModel::infoChanged, this, update));
    const auto found = [this, generation](const RadioInfo& radio) {
        if (generation != m_sessions.generation() || m_shuttingDown) { return; }
        m_radios.insert(radio.macAddress, radio);
        m_seenAt.insert(radio.macAddress, QDateTime::currentMSecsSinceEpoch());
        refresh();
    };
    m_windowConnections.append(connect(m_discovery, &RadioDiscovery::radioDiscovered, this, found));
    m_windowConnections.append(connect(m_discovery, &RadioDiscovery::radioUpdated, this, found));
    m_windowConnections.append(connect(m_discovery, &RadioDiscovery::radioLost, this,
        [this, generation](const QString& mac) {
            if (generation != m_sessions.generation()) { return; }
            m_seenAt.remove(mac);
            refresh();
        }));
    if (m_remoteControls) {
        m_windowConnections.append(connect(m_remoteControls, &RemoteConnectionController::changed,
                                            this, update));
    }
    if (auto* client = window->findChild<StationClient*>()) {
        const auto remember = [this, generation] {
            if (generation == m_sessions.generation() && !m_shuttingDown) {
                rememberAuthenticatedRadio();
                refresh();
            }
        };
        m_windowConnections.append(connect(client, &StationClient::stateSnapshotApplied, this, remember));
        m_windowConnections.append(connect(client, &StationClient::handshakeComplete, this, remember));
    }
    refresh();
    if (m_selector->isVisible()) { scan(); }
}

void GuiConnectionController::refresh()
{
    if (m_shuttingDown) { return; }
    MainWindow* window = m_sessions.window();
    if (!window) { return; }
    RadioModel* model = window->radioModel();
    const auto current = m_sessions.selection();
    QMap<QString, RadioInfo> radios = m_radios;
    for (const SavedRadio& saved : AppSettings::instance().savedRadios()) {
        if (!radios.contains(saved.info.macAddress)) { radios.insert(saved.info.macAddress, saved.info); }
    }
    QList<ConnectionTargetRow> rows;
    rows.append({QStringLiteral("local"), ConnectionTargetKind::LocalRadio,
        tr("This computer's Core"), tr("Choose a local radio"), tr("This computer"),
        model->ownsLocalDsp() ? tr("Selected") : tr("Available"), !model->ownsLocalDsp(), false, false});
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (auto it = radios.cbegin(); it != radios.cend(); ++it) {
        const RadioInfo& radio = it.value();
        const bool connected = model->ownsLocalDsp() && model->isConnected()
            && model->currentRadioMac().compare(radio.macAddress, Qt::CaseInsensitive) == 0;
        QString state = tr("Saved / not seen");
        if (connected) { state = tr("Connected locally"); }
        else if (ConnectionPanel::statePillForLastSeen(m_seenAt.value(it.key()), now)
                 == ConnectionPanel::StatePill::Online) {
            state = radio.inUse ? tr("In use") : tr("Available");
        }
        rows.append({QStringLiteral("radio:") + it.key(), ConnectionTargetKind::LocalRadio,
            radio.name.isEmpty() ? radio.displayName() : radio.name, tr("This computer's Core"),
            radio.address.toString(), state, !connected, true,
            AppSettings::instance().savedRadio(it.key()).has_value()});
    }
    for (const SavedCoreTarget& target : m_store.targets()) {
        const bool selected = current.savedId == target.id;
        const bool exact = selected && selectionMatchesSaved(current, target);
        const bool needsSetup = target.connection.token.isEmpty()
            || (target.connection.fingerprint.isEmpty() && !target.connection.allowUnpinned);
        QString state = needsSetup ? tr("Needs setup") : tr("Disconnected");
        QString radio = target.lastRadioName.isEmpty() ? tr("Radio unknown")
            : tr("%1 (last known)").arg(target.lastRadioName);
        if (selected && m_remoteControls) {
            state = m_remoteControls->statusText();
            radio = m_remoteControls->radioText();
            if (!exact) { state += tr(" — saved changes pending"); }
            else if (!current.savedAddressBeforeDiscovery.isEmpty()) { state += tr(" — LAN address"); }
        }
        rows.append({QStringLiteral("saved:") + target.id, ConnectionTargetKind::SavedCore,
            target.label.isEmpty() ? endpointText(target.connection) : target.label,
            radio, endpointText(target.connection), state,
            !exact || !m_remoteControls || m_remoteControls->canConnect(), m_storeLoaded, m_storeLoaded});
    }
    for (const StationLanEndpoint& endpoint : m_lan.endpoints()) {
        const auto matches = matchingSavedCores(endpoint, m_store.targets());
        const bool exact = matches.size() == 1 && current.savedId == matches.first().id
            && selectionMatchesSaved(current, matches.first())
            && QUrl(current.connection.url) == endpoint.url();
        const auto& advertised = endpoint.announcement;
        const QString state = exact && m_remoteControls ? m_remoteControls->statusText()
            : matches.size() > 1 ? tr("Choose a saved entry")
            : matches.isEmpty() ? tr("Needs setup") : tr("Saved, ready to connect");
        rows.append({QStringLiteral("lan:") + endpoint.key(), ConnectionTargetKind::LanCore,
            tr("%1 (advertised)").arg(advertised.coreName),
            advertised.radioConnected ? tr("%1 (advertised online)").arg(advertised.radioName)
                : tr("%1 (advertised offline)").arg(advertised.radioName.isEmpty() ? tr("Radio") : advertised.radioName),
            endpointText({endpoint.url().toString(), {}, {}, false}), state,
            matches.size() < 2 && (!exact || !m_remoteControls || m_remoteControls->canConnect()),
            false, false});
    }
    m_selector->setDiscoveryStatus(m_lan.port() == 0
        ? tr("LAN discovery is not running. Saved and manual addresses remain available.")
        : m_lan.lastError().isEmpty() ? tr("LAN discovery is active. Verify new Cores in Core setup.")
        : OperatorReasonText::lanDiscoveryForDisplay(m_lan.lastError()));
    if (current.connection.isRemote() && !m_store.target(current.savedId)) {
        rows.append({QStringLiteral("current"), ConnectionTargetKind::SavedCore,
            tr("Current Core (not saved)"), m_remoteControls ? m_remoteControls->radioText() : QString(),
            endpointText(current.connection), m_remoteControls ? m_remoteControls->statusText() : QString(),
            m_remoteControls && m_remoteControls->canConnect(), m_storeLoaded, false});
    }
    m_selector->setTargets(rows);
    if (m_remoteControls) {
        m_selector->setCurrentConnection(m_remoteControls->statusText(), m_remoteControls->detailText(),
            m_remoteControls->canDisconnect(), m_remoteControls->state() == ConnectionState::LinkLost);
    } else {
        const bool active = model->connectionState() != ConnectionState::Disconnected;
        m_selector->setCurrentConnection(model->isConnected() ? tr("Connected using this computer's Core")
            : tr("This computer's Core — no radio connected"), model->connectionIpText(), active,
            model->connectionState() == ConnectionState::LinkLost);
    }
}

void GuiConnectionController::showConnections()
{
    if (m_shuttingDown) { return; }
    refresh();
    m_selector->show();
    m_selector->raise();
    m_selector->activateWindow();
    scan();
}

void GuiConnectionController::scan()
{
    if (m_shuttingDown) { return; }
    m_lan.start();
    if (m_discovery) { m_discovery->startDiscovery(); }
    refresh();
}

void GuiConnectionController::queueConnect(const QString& key)
{
    const quint64 request = ++m_request;
    QTimer::singleShot(0, this, [this, key, request] {
        if (!m_shuttingDown && request == m_request) { connectTarget(key); }
    });
}

bool GuiConnectionController::choose(const StationStartupSelection& choice, bool startConnection)
{
    QString error;
    if (!m_sessions.canReplace(choice, &error)) {
        m_selector->setNotice(error);
        return false;
    }
    const QString previous = m_store.selectedId();
    const bool persist = m_storeLoaded && !choice.savedId.isEmpty();
    if (!m_storeLoaded && choice.connection.isRemote()) {
        m_selector->setNotice(tr("The saved Core list must be recovered before selecting a Core."));
        return false;
    }
    if (persist && !m_store.select(choice.savedId, &error)) {
        m_selector->setNotice(error);
        return false;
    }
    if (!m_sessions.replace(choice, startConnection, &error)) {
        if (persist) { m_store.select(previous); }
        m_selector->setNotice(error);
        return false;
    }
    m_selector->setNotice(m_storeLoaded ? QString()
        : tr("Using this computer's Core. The damaged saved Core list has been preserved."));
    return true;
}

void GuiConnectionController::connectTarget(const QString& key)
{
    if (key == QLatin1String("local")) {
        if (choose({}, false)) { scan(); }
    } else if (key == QLatin1String("current")) {
        if (m_remoteControls) { m_remoteControls->connectToStation(); }
    } else if (key.startsWith(QLatin1String("saved:"))) {
        const auto target = m_store.target(key.mid(6));
        if (!target) { m_selector->setNotice(tr("That saved Core is no longer available.")); return; }
        if (target->connection.token.isEmpty()
            || (target->connection.fingerprint.isEmpty() && !target->connection.allowUnpinned)) {
            editCore(target->id);
            return;
        }
        const auto current = m_sessions.selection();
        if (current.savedId == target->id && sameConnection(current.connection, target->connection)) {
            if (m_remoteControls) { m_remoteControls->connectToStation(); }
        } else {
            choose({target->connection, target->id, {}}, true);
        }
    } else if (key.startsWith(QLatin1String("lan:"))) {
        for (const StationLanEndpoint& endpoint : m_lan.endpoints()) {
            if (key != QStringLiteral("lan:") + endpoint.key()) { continue; }
            const auto matches = matchingSavedCores(endpoint, m_store.targets());
            if (matches.size() == 1) {
                StationStartupSelection selection{matches.first().connection, matches.first().id, {}};
                selection.savedAddressBeforeDiscovery = selection.connection.url;
                selection.connection.url = endpoint.url().toString();
                selection.connection.allowUnpinned = false;
                const auto current = m_sessions.selection();
                if (current.savedId == selection.savedId
                    && sameConnection(current.connection, selection.connection)) {
                    if (m_remoteControls) { m_remoteControls->connectToStation(); }
                } else {
                    choose(selection, true);
                }
            } else if (matches.isEmpty()) {
                editCore(); // Address/name only. Never adopt an advertised pin.
            } else {
                m_selector->setNotice(tr("Several saved entries use this Core identity. Choose the intended entry under Your stations."));
            }
            refresh();
            return;
        }
        m_selector->setNotice(tr("That Core announcement has expired. Scan again or use its saved address."));
    } else if (key.startsWith(QLatin1String("radio:"))) {
        const QString mac = key.mid(6);
        const auto saved = AppSettings::instance().savedRadio(mac);
        RadioInfo radio = m_radios.contains(mac) ? m_radios.value(mac)
            : saved ? saved->info : RadioInfo{};
        if (radio.macAddress.isEmpty() || radio.address.isNull() || radio.port == 0) {
            m_selector->setNotice(tr("That radio has no usable address. Edit it or scan again."));
            return;
        }
        if (!choose({}, false)) { return; }
        // Reuse the existing local-radio persistence and connection APIs.
        AppSettings& settings = AppSettings::instance();
        const HPSDRModel modelOverride = settings.modelOverride(mac);
        if (modelOverride != HPSDRModel::FIRST) { radio.modelOverride = modelOverride; }
        settings.saveRadio(radio, saved ? saved->pinToMac : false, true);
        settings.setLastConnected(radio.macAddress);
        if (!settings.save()) { m_selector->setNotice(tr("The local radio preference could not be saved.")); }
        if (radio.inUse) { m_selector->setNotice(tr("This radio reports in use; attempting the explicitly selected connection.")); }
        m_sessions.window()->radioModel()->connectToRadio(radio);
    }
    refresh();
}

void GuiConnectionController::disconnectCurrent()
{
    ++m_request;
    if (m_remoteControls) { m_remoteControls->disconnectFromStation(); }
    else if (m_sessions.window()) { m_sessions.window()->radioModel()->disconnectFromRadio(); }
    refresh();
}

void GuiConnectionController::editCore(const QString& id)
{
    if (!m_storeLoaded) {
        m_selector->setNotice(tr("The saved Core list could not be loaded. Correct the settings file before changing it."));
        return;
    }
    SavedCoreTarget initial;
    if (!id.isEmpty()) {
        const auto target = m_store.target(id);
        if (!target) { return; }
        initial = *target;
    } else {
        initial.id = CoreTargetStore::createId();
        if (m_selector->selectedKey() == QLatin1String("current")) {
            initial.connection = m_sessions.selection().connection;
            initial.label = endpointText(initial.connection);
        } else {
            for (const StationLanEndpoint& endpoint : m_lan.endpoints()) {
                if (m_selector->selectedKey() == QStringLiteral("lan:") + endpoint.key()) {
                    initial.label = endpoint.announcement.coreName;
                    initial.connection.url = endpoint.url().toString();
                    break;
                }
            }
        }
    }
    CoreTargetEditor editor(initial, m_selector.get());
    if (editor.exec() != QDialog::Accepted) { return; }
    QString error;
    if (!m_store.upsert(editor.target(), &error)) { m_selector->setNotice(error); return; }
    refresh();
    m_selector->setSelectedKey(QStringLiteral("saved:") + initial.id);
    m_selector->setNotice(tr("Core saved. Select Connect to use it."));
}

void GuiConnectionController::editRadio(const QString& mac)
{
    AddCustomRadioDialog editor(m_selector.get());
    const auto saved = AppSettings::instance().savedRadio(mac);
    if (!mac.isEmpty()) {
        const RadioInfo info = m_radios.contains(mac) ? m_radios.value(mac)
            : saved ? saved->info : RadioInfo{};
        if (info.macAddress.isEmpty()) { return; }
        editor.setEditTarget(info, saved ? saved->pinToMac : false, saved ? saved->autoConnect : false);
    }
    if (editor.exec() != QDialog::Accepted) { return; }
    const RadioInfo radio = editor.result();
    AppSettings& settings = AppSettings::instance();
    if (!mac.isEmpty() && radio.macAddress != mac) { settings.forgetRadio(mac); m_radios.remove(mac); }
    settings.saveRadio(radio, editor.pinToMac(), editor.autoConnect());
    if (!settings.save()) { m_selector->setNotice(tr("The local radio could not be saved.")); }
    m_radios.insert(radio.macAddress, radio);
    refresh();
    m_selector->setSelectedKey(QStringLiteral("radio:") + radio.macAddress);
    if (!editor.savedOffline()) { queueConnect(QStringLiteral("radio:") + radio.macAddress); }
}

void GuiConnectionController::forgetTarget(const QString& key)
{
    QString error;
    if (key.startsWith(QLatin1String("saved:"))) {
        if (!m_store.remove(key.mid(6), &error)) { m_selector->setNotice(error); return; }
    } else if (key.startsWith(QLatin1String("radio:"))) {
        const QString mac = key.mid(6);
        AppSettings::instance().forgetRadio(mac);
        if (!AppSettings::instance().save()) { m_selector->setNotice(tr("The updated radio list could not be saved.")); return; }
        m_radios.remove(mac);
        m_seenAt.remove(mac);
    }
    m_selector->setNotice(tr("Saved entry forgotten. The current connection is unchanged."));
    refresh();
}

void GuiConnectionController::showDetails(const QString& key)
{
    if (key == QLatin1String("local")) {
        m_selector->setNotice(tr("This computer runs its own Core and does its own signal processing. Choose a radio under Radios on this network to operate it directly from this computer."));
        return;
    }
    if (key == QLatin1String("current") || key == QStringLiteral("saved:") + m_sessions.selection().savedId) {
        if (m_remoteControls) { m_selector->setNotice(m_remoteControls->detailText()); return; }
    }
    if (key.startsWith(QLatin1String("saved:"))) {
        const auto target = m_store.target(key.mid(6));
        if (target) {
            m_selector->setNotice(tr("Core: %1\nRadio: %2 (as of the last connection to this Core)")
                .arg(endpointText(target->connection), target->lastRadioName.isEmpty() ? tr("unknown") : target->lastRadioName));
        }
    } else if (key.startsWith(QLatin1String("lan:"))) {
        for (const StationLanEndpoint& endpoint : m_lan.endpoints()) {
            if (key == QStringLiteral("lan:") + endpoint.key()) {
                m_selector->setNotice(tr("Core seen on this network, not yet verified: %1\nAddress: %2\nRadio MAC: %3\nUse a saved entry for it, or get its pairing token and certificate fingerprint from Core setup.")
                    .arg(endpoint.announcement.coreName, endpointText({endpoint.url().toString(), {}, {}, false}),
                         endpoint.announcement.radioMac));
                return;
            }
        }
    } else if (key.startsWith(QLatin1String("radio:"))) {
        const QString mac = key.mid(6);
        m_selector->setNotice(tr("Local radio: %1\nThis computer runs the Core and does the signal processing. Edit to see its saved address and model.").arg(mac));
    }
}

void GuiConnectionController::rememberAuthenticatedRadio()
{
    if (!m_storeLoaded || !m_sessions.window()) { return; }
    auto* client = m_sessions.window()->findChild<StationClient*>();
    if (!client || !client->isHandshakeComplete()) { return; }
    auto target = m_store.target(m_sessions.selection().savedId);
    if (!target || !selectionMatchesSaved(m_sessions.selection(), *target)) { return; }
    const auto& caps = client->capabilities();
    if (!caps.radioConnected || caps.macAddress.isEmpty()) { return; }
    if (target->lastRadioName == caps.stationName && target->lastRadioMac == caps.macAddress) { return; }
    target->lastRadioName = caps.stationName;
    target->lastRadioMac = caps.macAddress;
    QString error;
    if (!m_store.upsert(*target, &error)) { m_selector->setNotice(error); }
}
} // namespace NereusSDR
