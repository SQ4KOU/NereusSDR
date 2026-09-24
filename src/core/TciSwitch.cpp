// no-port-check: NereusSDR-original. R-R3-48 the app's one TCI switch.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
// 2026-09-24: R-R3-48 follow-up: the port handover to a Core on this
// computer, the wait on the Core's whole answer, the link-down rule. J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include "core/TciSwitch.h"

#include "core/LogCategories.h"
#include "core/TciServer.h"
#include "core/session/IStationLink.h"
#include "models/RadioModel.h"
#include "models/StationTciModel.h"

namespace NereusSDR {

TciSwitch::TciSwitch(TciServer* local, RadioModel* model, QObject* parent)
    : QObject(parent), m_local(local), m_model(model)
{
    m_awaitTimer.setSingleShot(true);
    m_awaitTimer.setInterval(kCoreAnswerWaitMs);
    connect(&m_awaitTimer, &QTimer::timeout, this, [this]() {
        m_awaitingCore = false;
        applyLocal();
    });
    if (model) {
        connect(model, &RadioModel::stationLinkStateChanged, this, &TciSwitch::reevaluate);
        if (StationTciModel* station = model->stationTciModel()) {
            connect(station, &StationTciModel::stateChanged,
                    this, &TciSwitch::onStationTciChanged);
        }
    }
}

bool TciSwitch::linkUp() const
{
    const IStationLink* link = m_model ? m_model->stationLink() : nullptr;
    return link && link->stationLinkReady();
}

bool TciSwitch::coreCoversThisComputer() const
{
    // Follow-up 1c: with the link down the Core may be gone; its last state
    // does not keep this window from serving.
    if (!coreServesThisComputer() || !linkUp()) {
        return false;
    }
    const StationTciModel* station = m_model->stationTciModel();
    return station && station->enabled() && station->listening()
        && station->port() == int(m_port);
}

void TciSwitch::onStationTciChanged()
{
    const StationTciModel* station = m_model ? m_model->stationTciModel() : nullptr;
    if (station) {
        // The Core's switch or port moved: a new state, which may be
        // handed over again (not while this window waits: that is the
        // answer to its own request).
        if (station->enabled() != m_lastCoreEnabled || station->port() != m_lastCorePort) {
            if (!m_awaitingCore) {
                m_handoverTried = false;
            }
            m_lastCoreEnabled = station->enabled();
            m_lastCorePort = station->port();
        }
    }
    // Follow-up 1d: the wait ends on the Core's whole answer (on, listening,
    // this port), not on its first property; a Core that cannot listen is
    // still retrying, so the wait runs out instead.
    if (m_awaitingCore && coreCoversThisComputer()) {
        m_awaitingCore = false;
        m_awaitTimer.stop();
    }
    applyLocal();
}

bool TciSwitch::coreHasStationServer() const
{
    const IStationLink* link = m_model ? m_model->stationLink() : nullptr;
    return m_model && m_model->role() == RadioModel::Role::Remote && link
        && link->stationTciAvailable();
}

bool TciSwitch::coreServesThisComputer() const
{
    const IStationLink* link = m_model ? m_model->stationLink() : nullptr;
    return m_model && m_model->role() == RadioModel::Role::Remote && link
        && link->coreServesTciOnThisComputer();
}

QString TciSwitch::stationLine(const RadioModel* model)
{
    if (!model || model->role() != RadioModel::Role::Remote) {
        return {};
    }
    const IStationLink* link = model->stationLink();
    const StationTciModel* station = model->stationTciModel();
    if (!link || !station
        || (!link->coreServesTciOnThisComputer() && !link->stationTciAvailable())) {
        return {};
    }
    if (!station->enabled()) {
        return {};
    }
    if (!station->listening()) {
        return QStringLiteral("The station's TCI server is not running.");
    }
    if (link->coreServesTciOnThisComputer()) {
        return QStringLiteral("The Core on this computer serves TCI apps here, port %1.")
            .arg(station->port());
    }
    if (station->stationAddress().isEmpty()) {
        return QStringLiteral("Also at the station, port %1, for apps on the Core's computer.")
            .arg(station->port());
    }
    return QStringLiteral("Also at the station: %1, port %2")
        .arg(station->stationAddress()).arg(station->port());
}

void TciSwitch::setSwitch(bool on, quint16 port, const QHostAddress& bindAddress, bool tell)
{
    m_on = on;
    m_port = port;
    m_bind = bindAddress;
    m_handoverTried = false;
    if (tell) {
        tellCore();
    }
    applyLocal();
}

void TciSwitch::setPortOrBind(quint16 port, const QHostAddress& bindAddress)
{
    const bool portChanged = port != m_port;
    m_port = port;
    if (portChanged) {
        m_handoverTried = false;
    }
    m_bind = bindAddress;
#ifdef HAVE_WEBSOCKETS
    if (m_local && m_local->isRunning()) {
        m_local->stop();
    }
#endif
    if (m_on && portChanged) {
        tellCore();
    }
    applyLocal();
}

void TciSwitch::reevaluate()
{
    // The link changed: the Core's state is fresh again.
    m_handoverTried = false;
    applyLocal();
}

void TciSwitch::applyLocal()
{
#ifdef HAVE_WEBSOCKETS
    if (!m_local) {
        return;
    }
    const bool coreHere = coreServesThisComputer() && linkUp();
    if (!m_on || !coreHere) {
        m_awaitingCore = false;
        m_awaitTimer.stop();
    }
    const StationTciModel* station = m_model ? m_model->stationTciModel() : nullptr;
    if (coreCoversThisComputer()) {
        m_handoverTried = false;   // a later loss may be handed over again
    }
    // Follow-up 1a: the Core here is on, on this port, but not listening
    // (it could not take the port, perhaps from this window): release it
    // and ask the Core again, once for this state, under the wait.
    if (m_on && coreHere && station && station->enabled() && !station->listening()
        && station->port() == int(m_port) && !m_awaitingCore && !m_handoverTried) {
        m_handoverTried = true;
        if (m_local->isRunning()) {
            qCInfo(lcTci) << "Handing TCI port" << m_port
                          << "to the Core on this computer, which could not listen on it";
            m_local->stop();
        }
        tellCore();
    }
    const bool wanted = m_on && !coreCoversThisComputer() && !(m_awaitingCore && coreHere);
    if (wanted && !m_local->isRunning()) {
        m_local->start(m_bind, m_port);
    } else if (!wanted && m_local->isRunning()) {
        if (m_on) {
            qCInfo(lcTci) << "The Core on this computer serves TCI here;"
                          << "this window runs no TCI server of its own";
        }
        m_local->stop();
    }
#endif
}

void TciSwitch::tellCore()
{
    if (!m_model || m_model->role() != RadioModel::Role::Remote) {
        return;
    }
    IStationLink* link = m_model->stationLink();
    if (!link || !link->stationTciAvailable()) {
        return; // An older Core (or no link): this window's server only.
    }
    const auto outcome = link->requestStationTci(m_on, m_port);
    if (!outcome.sent) {
        emit stationRequestFailed(outcome.reason);
        return;
    }
    if (m_on && coreServesThisComputer() && linkUp() && !coreCoversThisComputer()) {
        // The Core here was asked to serve this port: wait for its answer
        // before this window takes the port itself.
        m_handoverTried = true;
        m_awaitingCore = true;
        m_awaitTimer.start();
    }
}

} // namespace NereusSDR
