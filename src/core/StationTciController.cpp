// no-port-check: NereusSDR-original. R-R3-48 / R-R3-25 the Core's station TCI server.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
// 2026-09-24: R-R3-48 follow-up: a listener that cannot start is retried
// with a bounded backoff and a plain reason. J.J. Boyd (KG4VCF), AI-assisted
// via Anthropic Claude Code.
#include "core/StationTciController.h"

#include "core/AppSettings.h"
#include "core/LogCategories.h"
#include "core/StationNetwork.h"
#include "core/TciServer.h"
#include "models/RadioModel.h"

#include <iterator>

namespace NereusSDR {

StationTciController::StationTciController(RadioModel* radio, StationTciModel* model,
                                           QObject* parent)
    : QObject(parent), m_radio(radio), m_model(model)
{
    m_retryTimer.setSingleShot(true);
    connect(&m_retryTimer, &QTimer::timeout, this, &StationTciController::apply);
#ifdef HAVE_WEBSOCKETS
    // The Core's own server on the Core's radio model (a Local model, so
    // vfo:, split_enable:, audio and I/Q come from the Core's receivers).
    //
    // TCI compatibility keys (the carry note of the remote window Setup
    // plan): the Core seeds none of TciEmulateExpertSDR3Protocol,
    // TciEmulateSunSDR2Pro, TciCwluBecomesCw, TciCwBecomesCwuAbove10mhz or
    // the other Tci* keys, and relies on their readers' defaults on
    // purpose (the two emulation keys read True, as in a fresh window).
    // The Core has no page to change them; a Core whose settings file
    // still holds Tci values from before R-R3-42 honours those, as the
    // operator's own earlier choices.
    m_server = std::make_unique<TciServer>(radio);
    m_server->setStationReceiveOnly(true);
    connect(m_server.get(), &TciServer::errorOccurred, this, [this](const QString& error) {
        // The raw socket error is for the log; the object carries plain
        // words (apply()).
        m_error = error;
    });
    connect(m_server.get(), &TciServer::operatorNotice, this,
            [](const QString& peer, const QString& reason, bool) {
        qCInfo(lcTci) << "Station TCI server:" << reason << "(app" << peer << ")";
    });
#endif
}

StationTciController::~StationTciController()
{
#ifdef HAVE_WEBSOCKETS
    if (m_server) {
        m_server->stop();
    }
#endif
}

// static
QString StationTciController::cannotListenReason(quint16 port)
{
    return QStringLiteral("The station's TCI server cannot use port %1 right now; another "
                          "program may be using it. The Core keeps trying.").arg(port);
}

void StationTciController::resetRetry()
{
    m_retryTimer.stop();
    m_retryStep = 0;
}

TciServer* StationTciController::server() const
{
#ifdef HAVE_WEBSOCKETS
    return m_server.get();
#else
    return nullptr;
#endif
}

void StationTciController::setBindOverride(const QString& address)
{
    const QString trimmed = address.trimmed();
    if (trimmed == m_bind.bindOverride) {
        return;
    }
    m_bind.bindOverride = trimmed;
    apply();
}

void StationTciController::setRadioAddress(const QHostAddress& radio)
{
    if (radio == m_bind.radio) {
        return;
    }
    m_bind.radio = radio;
    apply();
}

void StationTciController::setInterfaceEntriesForTest(const QList<QNetworkAddressEntry>& entries)
{
    m_bind.entriesForTest = entries;
    apply();
}

void StationTciController::applySaved()
{
    auto& settings = AppSettings::instance();
    m_enabled = settings.value(enabledKey(), QStringLiteral("False")).toString()
        == QStringLiteral("True");
    bool ok = false;
    const int port = settings.value(portKey(), QString::number(kDefaultPort)).toString().toInt(&ok);
    m_port = ok && port >= 1024 && port <= 65535 ? static_cast<quint16>(port) : kDefaultPort;
    apply();
}

bool StationTciController::setEnabled(bool enabled, int port, QString* reason)
{
    if (port < 1024 || port > 65535) {
        if (reason) {
            *reason = QStringLiteral("Choose a TCI port from 1024 to 65535.");
        }
        return false;
    }
    m_enabled = enabled;
    m_port = static_cast<quint16>(port);
    auto& settings = AppSettings::instance();
    settings.setValue(enabledKey(), enabled ? QStringLiteral("True") : QStringLiteral("False"));
    settings.setValue(portKey(), QString::number(port));
    settings.save();
    resetRetry();   // a request tries at once, from the first delay again
    apply();
    if (reason) {
        reason->clear();
    }
    return true; // Saved; whether it listens is the `stationTci` object.
}

QList<QHostAddress> StationTciController::wantedAddresses() const
{
    // The one station listener rule (StationNetwork::StationBind).
    return m_bind.listenAddresses();
}

void StationTciController::apply()
{
#ifdef HAVE_WEBSOCKETS
    if (!m_server) {
        return;
    }
    if (!m_enabled) {
        resetRetry();
        m_failing = false;
        m_server->setQuietListenAttempts(false);
        if (m_server->isRunning()) {
            m_server->stop();
        }
        m_listening.clear();
        m_error.clear();
        publish();
        return;
    }
    const QList<QHostAddress> wanted = wantedAddresses();
    if (m_server->isRunning() && m_server->port() == m_port && wanted == m_listening) {
        publish();
        return;
    }
    if (m_server->isRunning()) {
        m_server->stop();
    }
    m_error.clear();
    if (m_server->start(wanted, m_port)) {
        m_listening = wanted;
        resetRetry();
        qCInfo(lcTci) << (m_failing ? "Station TCI server listening again on"
                                    : "Station TCI server listening on")
                      << wanted << "port" << m_port;
        m_failing = false;
        m_server->setQuietListenAttempts(false);
    } else {
        m_listening.clear();
        const int delay = kRetryDelaysMs[qMin(m_retryStep, int(std::size(kRetryDelaysMs)) - 1)];
        ++m_retryStep;
        if (!m_failing) {
            // One line when it starts failing; the tries stay quiet.
            qCWarning(lcTci) << "Station TCI server could not listen on" << wanted
                             << "port" << m_port << m_error << "; retrying";
            m_failing = true;
            m_server->setQuietListenAttempts(true);
        }
        m_retryTimer.start(delay);
    }
#endif
    publish();
}

void StationTciController::publish()
{
    if (!m_model) {
        return;
    }
    StationTciModel::State state;
    state.enabled = m_enabled;
    state.port = m_port;
#ifdef HAVE_WEBSOCKETS
    state.listening = m_server && m_server->isRunning();
#endif
    for (const QHostAddress& address : m_listening) {
        if (!address.isLoopback()) {
            state.stationAddress = address.toString();
            break;
        }
    }
    state.error = state.listening || !m_failing ? QString() : cannotListenReason(m_port);
    m_model->setState(state);
}

} // namespace NereusSDR
