// no-port-check: NereusSDR-original. R-R3-22 station-owned TGXL identity.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via OpenAI Codex.
#pragma once

#include "core/TgxlConnection.h"
#include "models/TunerModel.h"
#include <QPointer>

namespace NereusSDR {
class LanDiscovery;

// The native protocol has no product field. Admission therefore requires
// same-peer LAN discovery plus the correlated native serial, on every dial.
// This policy is independent of receive-only/TX permissions.
class StationTgxlController final : public QObject {
    Q_OBJECT
public:
    StationTgxlController(TgxlConnection* connection, TunerModel* model,
                          QObject* parent = nullptr);
    ~StationTgxlController() override;
    void resetScope(const QString& host, quint16 port, bool enabled);
    void start(const QString& host, quint16 port);
    void cancel(bool disabled = false);

private:
    void identify(quint64 attempt, const QString& peer, quint16 port);
    void tryAdmit();
    void stopDiscovery();
    void publish();
    void clearIdentity();
    bool current(quint64 attempt) const;

    QPointer<TgxlConnection> m_connection;
    QPointer<TunerModel> m_model;
    QPointer<LanDiscovery> m_discovery;
    TunerModel::StationConnectionState m_state;
    TgxlIdentityInfo m_nativeInfo;
    QString m_discoveredModel;
    QString m_discoveredSerial;
    QString m_peer;
    quint16 m_peerPort{0};
    quint64 m_attempt{0};
    quint64 m_generation{0};
    bool m_running{false};
};
} // namespace NereusSDR
