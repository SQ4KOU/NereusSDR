// no-port-check: NereusSDR-original. R-R3-47 / R-R3-48 / R-R3-22 station-owned RF2K-S.
// The Power Genius controller's shape (StationPgxlController.h).
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#pragma once

#include "core/Rf2ksConnection.h"
#include "models/RfKitModel.h"
#include <QPointer>

namespace NereusSDR {

// The Core's RF-Kit RF2K-S: identity before admission, then band follow.
//
// The amp speaks REST on TCP 8080. Rf2ksConnection counts it as connected
// only once its /info reply names the product `RF2K-S`
// (Rf2ksConnection::expectedDevice(); design doc
// 2026-05-24-rfkit-rf2ks-applet-design.md section 6.1, firmware G200C267).
// Anything else at the address is refused and never retried. This
// controller owns the `rfkit` object's connection phase on the Core:
// connecting, retrying, connected, error, disconnected, disabled. (A REST
// amp has no separate identifying step: the /info reply that proves the
// address answers is the one that identifies it.)
//
// Band follow (R-R3-48): the amp learns the radio's frequency as a TCI
// client (it parses only `vfo:` and `split_enable:`). When the station's
// TCI server is on, the Core switches the amp into TCI mode through its web
// interface (PUT /operational-interface, the page's "Set amp to TCI mode"),
// once per admitted connection, so an operator who switches it back on the
// amp's front panel is not fought. The TCI server's address itself is set
// on the amp's touchscreen; the band-follow line says which one.
class StationRfKitController final : public QObject {
    Q_OBJECT
public:
    StationRfKitController(Rf2ksConnection* connection, RfKitModel* model,
                           QObject* parent = nullptr);
    ~StationRfKitController() override;

    /// A radio's saved address becomes the current scope: the previous
    /// scope's requests and retry are retired first.
    void resetScope(const QString& host, quint16 port, bool enabled);
    /// Dial `host`:`port` and identify what answers.
    void start(const QString& host, quint16 port);
    /// Stop in any phase; nothing is redialled. `disabled` reports the
    /// station's RF-Kit switch as off.
    void cancel(bool disabled = false);

    /// R-R3-48: the station's TCI server is on (so the amp should be in
    /// TCI mode) or off.
    void setBandFollowWanted(bool wanted);
    bool bandFollowWanted() const { return m_bandFollowWanted; }
    /// Whether this connection already asked the amp for TCI mode.
    bool tciModeRequestedForTesting() const { return m_tciModeRequested; }

private:
    void publish(RfKitModel::ConnectionPhase phase, const QString& error = {});
    void maybeRequestTciMode();

    QPointer<Rf2ksConnection> m_connection;
    QPointer<RfKitModel> m_model;
    QString m_host;
    quint16 m_port{0};
    quint64 m_generation{0};
    bool m_running{false};
    bool m_bandFollowWanted{false};
    bool m_tciModeRequested{false};
};

} // namespace NereusSDR
