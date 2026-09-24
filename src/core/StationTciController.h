// no-port-check: NereusSDR-original. R-R3-48 / R-R3-25 the Core's station TCI server.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#pragma once

#include "core/StationNetwork.h"
#include "models/StationTciModel.h"

#include <QHostAddress>
#include <QList>
#include <QNetworkAddressEntry>
#include <QObject>
#include <QPointer>

#include <memory>
#include <optional>

namespace NereusSDR {

class RadioModel;
class TciServer;

// The Core runs the app's existing TciServer on its own radio model, so
// devices on the station network (the RF-Kit RF2K-S first) follow the
// Core's radio. One switch and port: the window's TCI switch sends them
// with the setStationTci command, and the Core keeps them in its own
// settings (StationTci_Enabled, StationTci_Port) across window sessions,
// other apps connecting, and restarts.
//
// Where it listens: the station network only, plus this computer (so apps
// on the Core's own computer reach it, and a window on the same computer
// needs no server of its own), by the one rule every station listener
// follows (StationNetwork::StationBind): nereusd.conf's station_bind
// (older name station_tci_bind); else this computer's address on the
// radio's subnet once the radio is known. Before either is known the
// server listens on this computer only. A bind to every address
// (0.0.0.0) is used as given and covers this computer too.
//
// The server transmits for no app until remote transmit (R-R3-25): see
// TciServer::setStationReceiveOnly.
class StationTciController : public QObject {
    Q_OBJECT
public:
    /// The station switch's default port: the app's TCI default
    /// (CatTciServerPage's TciServerPort, 50001).
    static constexpr quint16 kDefaultPort = 50001;
    /// The settings keys the Core keeps its switch in.
    static QString enabledKey() { return QStringLiteral("StationTci_Enabled"); }
    static QString portKey() { return QStringLiteral("StationTci_Port"); }

    StationTciController(RadioModel* radio, StationTciModel* model, QObject* parent = nullptr);
    ~StationTciController() override;

    /// nereusd.conf station_bind (or station_tci_bind): empty chooses the
    /// station network from the radio's address.
    void setBindOverride(const QString& address);
    /// The radio's address: the station network is the subnet holding it.
    void setRadioAddress(const QHostAddress& radio);
    /// Test seam: this computer's address entries (default: the live ones).
    void setInterfaceEntriesForTest(const QList<QNetworkAddressEntry>& entries);

    /// Read the saved switch and port and apply them (the Core's start).
    void applySaved();
    /// The station's TCI switch and port, saved and applied. Refused (with
    /// a plain reason) for a port outside 1024 to 65535.
    bool setEnabled(bool enabled, int port, QString* reason);

    /// The addresses the server listens on when on.
    QList<QHostAddress> wantedAddresses() const;

    TciServer* server() const;

private:
    void apply();
    void publish();

    QPointer<RadioModel> m_radio;
    QPointer<StationTciModel> m_model;
#ifdef HAVE_WEBSOCKETS
    std::unique_ptr<TciServer> m_server;
#endif
    StationNetwork::StationBind m_bind;
    bool m_enabled{false};
    quint16 m_port{kDefaultPort};
    QList<QHostAddress> m_listening;
    QString m_error;
};

} // namespace NereusSDR
