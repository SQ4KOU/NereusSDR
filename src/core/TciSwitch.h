// no-port-check: NereusSDR-original. R-R3-48 the app's one TCI switch.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#pragma once

#include <QHostAddress>
#include <QObject>
#include <QPointer>

namespace NereusSDR {

class RadioModel;
class TciServer;

// The app's one TCI switch and port (Setup > CAT & Network > TCI Server).
//
// On, it starts this window's own TciServer (as it always has) and, in a
// window on a Core that runs a station TCI server (stationTciVersion 1),
// asks the Core to run its server on the same port (setStationTci). Off
// stops both. The Core keeps its switch when this window closes and when
// another app connects: the command is sent only when the operator
// changes the switch or the port here.
//
// A Core on this same computer already serves TCI here (it listens on this
// computer too), so the window then runs no server of its own and apps on
// this computer use the Core's: one server, one port.
class TciSwitch : public QObject {
    Q_OBJECT
public:
    TciSwitch(TciServer* local, RadioModel* model, QObject* parent = nullptr);

    /// The switch changed (or the window started): apply it here and, when
    /// `tellCore`, ask the Core for the same.
    void setSwitch(bool on, quint16 port, const QHostAddress& bindAddress, bool tellCore = true);
    /// The port or bind address changed: restart this window's server if
    /// it runs, and give the Core the new port while the switch is on.
    void setPortOrBind(quint16 port, const QHostAddress& bindAddress);
    /// The link to the Core changed: start or stop this window's server as
    /// the placement rule says.
    void reevaluate();

    bool switchOn() const { return m_on; }
    /// The Core on this computer serves TCI here, so this window runs none.
    bool coreServesThisComputer() const;
    /// The Core runs a station TCI server this switch also controls.
    bool coreHasStationServer() const;

    /// R-R3-48: the TCI page's line about the Core's station server, in a
    /// remote window on a Core that runs one: "Also at the station:
    /// <address>, port <port>". Empty when there is nothing to say.
    static QString stationLine(const RadioModel* model);

signals:
    /// A request to the Core could not be sent; `reason` in plain words.
    void stationRequestFailed(const QString& reason);

private:
    void applyLocal();
    void tellCore();

    QPointer<TciServer> m_local;
    QPointer<RadioModel> m_model;
    bool m_on{false};
    quint16 m_port{50001};
    QHostAddress m_bind{QHostAddress::LocalHost};
};

} // namespace NereusSDR
