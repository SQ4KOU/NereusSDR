// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#pragma once
// no-port-check: NereusSDR-original local observation subscription owner.
#include "core/NereusCoreExport.h"
#include "core/audio/WaveformHistory.h"
#include "core/audio/WaveformPublisher.h"
#include <QHash>
#include <QList>
#include <QPointer>
#include <QSet>
#include <QTimer>
namespace NereusSDR {
struct WaveformRequest {
    int sliceId = -1;
    WaveformKind kind = WaveformKind::ConditionedMicrophone;
    bool operator==(const WaveformRequest&) const = default;
};
class NEREUS_CORE_EXPORT RadeWaveformModel : public QObject
{
    Q_OBJECT
  public:
    explicit RadeWaveformModel(QObject* parent = nullptr);
    ~RadeWaveformModel() override;
    WaveformEndpointState endpointState(int sliceId, WaveformKind kind) const;
    void setRemoteSupported(bool supported, const QString& reason = {});
    void applyRemoteContext(int sliceId, WaveformKind kind, quint64 sourceGeneration,
                            quint64 deliveryGeneration, int sampleRate, WaveformAvailability availability,
                            const QString& reason);
    void appendRemoteBatch(int sliceId, quint64 sourceGeneration, WaveformBatchPtr batch);
    void setRemoteClockMap(qint64 coreMinusLocalNs, bool valid);
    void setRefreshDemand(quintptr owner, bool active);
    void clearRefreshDemand(quintptr owner);
    void setDemand(quintptr owner, const QList<WaveformRequest>& requests);
    void clearDemand(quintptr owner);
    void setEndpoint(int sliceId, WaveformKind kind, WaveformPublisher* publisher, quint64 generation,
                     bool available, const QString& reason = {});
    void retireEndpoint(int sliceId, WaveformKind kind);
    WaveformSnapshotPtr snapshot(int sliceId, WaveformKind kind, qint64 asOfNs);

  signals:
    void localObservationRefreshRequested();
    void acceptedBatch(int sliceId, NereusSDR::WaveformKind kind, quint64 sourceGeneration,
                       NereusSDR::WaveformBatchPtr batch);
    void endpointStateChanged(int sliceId, NereusSDR::WaveformKind kind, quint64 sourceGeneration,
                              bool available, const QString& reason);
    void remoteDemandChanged(const QList<NereusSDR::WaveformRequest>& requests);

  private:
    struct Endpoint {
        QPointer<WaveformPublisher> publisher;
        quint64 generation = 0;
        quint64 token = 0;
        bool available = false;
        QString reason;
        WaveformHistory history;
    };
    static int key(int sliceId, WaveformKind kind)
    {
        return sliceId * 3 + int(kind);
    }
    void reconcile();
    QTimer m_refreshTimer;
    QSet<quintptr> m_refreshOwners;
    QHash<quintptr, QList<WaveformRequest>> m_demands;
    QHash<int, Endpoint> m_endpoints;
    QSet<WaveformPublisher*> m_observedPublishers;
    struct RemoteEndpoint {
        quint64 sourceGeneration = 0;
        quint64 deliveryGeneration = 0;
        int sampleRate = 0;
        WaveformAvailability availability = WaveformAvailability::Unavailable;
        QString reason;
        WaveformHistory history;
    };
    QHash<int, RemoteEndpoint> m_remoteEndpoints;
    QList<WaveformRequest> m_remoteDemandUnion;
    bool m_remoteMode = false;
    bool m_remoteSupported = false;
    QString m_remoteReason;
    qint64 m_coreMinusLocalNs = 0;
    bool m_clockValid = false;
};
} // namespace NereusSDR
