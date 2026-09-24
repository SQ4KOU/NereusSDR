// no-port-check: NereusSDR-original. R-R3-47 / R-R3-22 station-owned PGXL identity.
// The Tuner Genius controller's shape (StationTgxlController.h).
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#pragma once

#include "core/PgxlConnection.h"
#include "models/AmplifierModel.h"
#include <QPointer>

namespace NereusSDR {
class LanDiscovery;

// The Core's Power Genius XL: identity before admission, then pairing.
//
// A V banner says only that a peer speaks the Genius protocol (a Tuner
// Genius sends one too), and the amp's `info` reply carries no model. So,
// as for the Tuner Genius, admission needs both a LAN discovery
// announcement from the same address and port naming the product
// `PowerGeniusXL`, and the same serial in the amp's own `info` reply, on
// every dial. Until then PgxlConnection sends nothing but `info`; pairing
// (RadioModel::onPgxlConnected) runs on PgxlConnection::connected, which
// only admission emits.
//
// Captured evidence (both from the real amp at 192.168.109.235):
//   discovery, UDP 9008 (captures/flex-pgxl-tgxl-capture_00001_20260519173452.pcapng):
//     "PowerGeniusXL ip=192.168.109.235 v=3.8.9 serial=10-200/24-0046 nickname=PowerGeniusXL"
//   info reply, TCP 9008 (captures/flex-tgxl-direct-CONTROL.pcapng, C30698):
//     "R30698|0|serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240"
class StationPgxlController final : public QObject {
    Q_OBJECT
public:
    /// The one product name a Power Genius XL announces (captured above).
    static QString expectedProduct() { return QStringLiteral("PowerGeniusXL"); }

    StationPgxlController(PgxlConnection* connection, AmplifierModel* model,
                          QObject* parent = nullptr);
    ~StationPgxlController() override;

    /// A radio's saved address becomes the current scope: every socket and
    /// timer of the previous scope is retired first.
    void resetScope(const QString& host, quint16 port, bool enabled);
    /// Dial `host`:`port` and identify what answers.
    void start(const QString& host, quint16 port);
    /// Stop in any phase; nothing is redialled. `disabled` reports the
    /// station's switch as off.
    void cancel(bool disabled = false);
    /// Connection settings changed on the Core: apply them to a running
    /// connection (automatic retry, keepalive interval, ping interval).
    void applyConnectionSettings();

private:
    void identify(quint64 attempt, const QString& peer, quint16 port);
    void tryAdmit();
    void stopDiscovery();
    void publish();
    void clearIdentity();
    bool current(quint64 attempt) const;

    QPointer<PgxlConnection> m_connection;
    QPointer<AmplifierModel> m_model;
    QPointer<LanDiscovery> m_discovery;
    AmplifierModel::StationConnectionState m_state;
    PgxlIdentityInfo m_nativeInfo;
    QString m_discoveredModel;
    QString m_discoveredSerial;
    QString m_discoveredNickname;
    QString m_peer;
    quint16 m_peerPort{0};
    quint64 m_attempt{0};
    quint64 m_generation{0};
    bool m_running{false};
};
} // namespace NereusSDR
