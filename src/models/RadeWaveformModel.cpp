// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
// no-port-check: NereusSDR-original queued observation and remote clock owner.
#include "RadeWaveformModel.h"
#include <algorithm>
#include <atomic>
#include <limits>
namespace NereusSDR {
namespace {
int nativeRate(WaveformKind kind)
{
    switch (kind) {
    case WaveformKind::RawMicrophone:
        return 48000;
    case WaveformKind::ConditionedMicrophone:
        return 16000;
    case WaveformKind::DecodedSpeech:
        return 24000;
    }
    return 0;
}
bool validRequest(int sliceId, WaveformKind kind)
{
    return sliceId >= 0 && sliceId <= (std::numeric_limits<int>::max() - 2) / 3 && nativeRate(kind) != 0;
}
QList<WaveformRequest> demandUnion(const QHash<quintptr, QList<WaveformRequest>>& demands)
{
    QList<WaveformRequest> result;
    for (const auto& requests : demands) {
        for (const auto& request : requests) {
            if (validRequest(request.sliceId, request.kind) && !result.contains(request)) {
                result.append(request);
            }
        }
    }
    std::sort(result.begin(), result.end(), [](const WaveformRequest& a, const WaveformRequest& b) {
        return a.sliceId < b.sliceId || (a.sliceId == b.sliceId && int(a.kind) < int(b.kind));
    });
    return result;
}
bool safeAdd(qint64 a, qint64 b, qint64& out)
{
    if ((b > 0 && a > std::numeric_limits<qint64>::max() - b) ||
        (b < 0 && a < std::numeric_limits<qint64>::min() - b)) {
        return false;
    }
    out = a + b;
    return true;
}
quint64 nextToken()
{
    // Tokens remain unique when a model/receiver is replaced around a live publisher.
    static std::atomic<quint64> next{0};
    quint64 token = next.fetch_add(1, std::memory_order_relaxed) + 1;
    if (token == 0) {
        token = next.fetch_add(1, std::memory_order_relaxed) + 1;
    }
    return token;
}
} // namespace
RadeWaveformModel::RadeWaveformModel(QObject* parent) : QObject(parent)
{
    m_refreshTimer.setInterval(33);
    m_refreshTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_refreshTimer, &QTimer::timeout, this, &RadeWaveformModel::localObservationRefreshRequested);
}
RadeWaveformModel::~RadeWaveformModel()
{
    for (auto& endpoint : m_endpoints) {
        if (endpoint.publisher) {
            endpoint.publisher->setBindingToken(0);
        }
    }
}
void RadeWaveformModel::setRefreshDemand(quintptr owner, bool active)
{
    if (active) {
        m_refreshOwners.insert(owner);
    } else {
        m_refreshOwners.remove(owner);
    }
    reconcile();
}
void RadeWaveformModel::clearRefreshDemand(quintptr owner)
{
    setRefreshDemand(owner, false);
}
void RadeWaveformModel::setDemand(quintptr owner, const QList<WaveformRequest>& requests)
{
    m_demands[owner] = requests;
    reconcile();
}
void RadeWaveformModel::clearDemand(quintptr owner)
{
    m_demands.remove(owner);
    reconcile();
}
WaveformEndpointState RadeWaveformModel::endpointState(int sliceId, WaveformKind kind) const
{
    if (!validRequest(sliceId, kind)) {
        return {};
    }
    const int id = key(sliceId, kind);
    if (m_remoteMode) {
        const auto it = m_remoteEndpoints.constFind(id);
        if (!m_remoteSupported || it == m_remoteEndpoints.constEnd()) {
            return {0, 0, false, m_remoteReason};
        }
        return {it->sourceGeneration, it->sampleRate, it->availability == WaveformAvailability::Available,
                it->reason};
    }
    const auto it = m_endpoints.constFind(id);
    if (it == m_endpoints.constEnd()) {
        return {};
    }
    return {it->generation, it->publisher ? it->publisher->sampleRate() : 0,
            it->available && !it->publisher.isNull(), it->reason};
}
void RadeWaveformModel::setEndpoint(int sliceId, WaveformKind kind, WaveformPublisher* publisher,
                                    quint64 generation, bool available, const QString& reason)
{
    if (!validRequest(sliceId, kind) || m_remoteMode) {
        return;
    }
    const WaveformEndpointState before = endpointState(sliceId, kind);
    const int id = key(sliceId, kind);
    Endpoint& endpoint = m_endpoints[id];
    const bool replaced = endpoint.publisher != publisher || endpoint.generation != generation;
    if (replaced) {
        if (endpoint.publisher) {
            endpoint.publisher->setBindingToken(0);
        }
        endpoint.history.clear();
        endpoint.token = 0;
        endpoint.publisher = publisher;
        endpoint.generation = generation;
        if (publisher && !m_observedPublishers.contains(publisher)) {
            m_observedPublishers.insert(publisher);
            const QPointer<WaveformPublisher> alive(publisher);
            // One stable queued receiver acknowledges retired packets too.
            connect(
                publisher, &WaveformPublisher::batchReady, this,
                [this, alive](WaveformBatchPtr batch) {
                    if (!batch) {
                        return;
                    }
                    const QPointer<RadeWaveformModel> self(this);
                    if (!m_remoteMode) {
                        // Project the latest physical microphone/receiver identity
                        // before accepting queued data, including A->B->A transitions.
                        emit localObservationRefreshRequested();
                        if (!self) {
                            if (alive) {
                                alive->acknowledge(batch->serial);
                            }
                            return;
                        }
                    }
                    if (alive && !m_remoteMode) {
                        for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
                            if (it->publisher == alive && it->token != 0 && it->token == batch->token &&
                                int(batch->kind) == it.key() % 3 && it->available &&
                                batch->sampleRate == nativeRate(batch->kind)) {
                                const int sliceId = it.key() / 3;
                                const quint64 sourceGeneration = it->generation;
                                it->history.append(*batch);
                                // Fanout follows sole-consumer history acceptance; no second producer ack.
                                emit acceptedBatch(sliceId, batch->kind, sourceGeneration, batch);
                                break;
                            }
                        }
                    }
                    if (alive) {
                        alive->acknowledge(batch->serial);
                    }
                },
                Qt::QueuedConnection);
            connect(publisher, &QObject::destroyed, this,
                    [this, publisher] { m_observedPublishers.remove(publisher); });
        }
    }
    endpoint.available = available && publisher && generation != 0 && publisher->kind() == kind &&
                         publisher->sampleRate() == nativeRate(kind);
    endpoint.reason = reason;
    const WaveformEndpointState after = endpointState(sliceId, kind);
    if (before.sourceGeneration != after.sourceGeneration || before.available != after.available ||
        before.reason != after.reason) {
        emit endpointStateChanged(sliceId, kind, after.sourceGeneration, after.available, after.reason);
    }
    reconcile();
}
void RadeWaveformModel::retireEndpoint(int sliceId, WaveformKind kind)
{
    if (!validRequest(sliceId, kind)) {
        return;
    }
    const int id = key(sliceId, kind);
    auto it = m_endpoints.find(id);
    if (it == m_endpoints.end()) {
        return;
    }
    if (it->publisher) {
        it->publisher->setBindingToken(0);
    }
    m_endpoints.erase(it);
    emit endpointStateChanged(sliceId, kind, 0, false, QStringLiteral("Source retired"));
}
void RadeWaveformModel::reconcile()
{
    const QList<WaveformRequest> requests = demandUnion(m_demands);
    // Watching a visible but unavailable/refused source refreshes its identity
    // without enabling producer capture. The sole timer stays on the model thread.
    const bool refresh = !m_remoteMode && (!requests.isEmpty() || !m_refreshOwners.isEmpty());
    if (refresh && !m_refreshTimer.isActive()) {
        m_refreshTimer.start();
    } else if (!refresh) {
        m_refreshTimer.stop();
    }
    QSet<int> wanted;
    for (const auto& request : requests) {
        wanted.insert(key(request.sliceId, request.kind));
    }
    for (auto it = m_endpoints.begin(); it != m_endpoints.end(); ++it) {
        const bool enabled = !m_remoteMode && it->available && it->publisher && wanted.contains(it.key());
        if (enabled && it->token == 0) {
            it->token = nextToken();
            it->publisher->setBindingToken(it->token);
        }
        if (!enabled && it->token != 0) {
            if (it->publisher) {
                it->publisher->setBindingToken(0);
            }
            it->token = 0;
            it->history.clear();
        }
    }
    if (m_remoteMode) {
        const QList<WaveformRequest> effective = m_remoteSupported ? requests : QList<WaveformRequest>();
        QSet<int> remoteWanted;
        for (const auto& request : effective) {
            remoteWanted.insert(key(request.sliceId, request.kind));
        }
        for (auto it = m_remoteEndpoints.begin(); it != m_remoteEndpoints.end();) {
            if (!remoteWanted.contains(it.key())) {
                it = m_remoteEndpoints.erase(it);
            } else {
                ++it;
            }
        }
        if (effective != m_remoteDemandUnion) {
            m_remoteDemandUnion = effective;
            emit remoteDemandChanged(m_remoteDemandUnion);
        }
    }
}
void RadeWaveformModel::setRemoteSupported(bool supported, const QString& reason)
{
    const bool changed = !m_remoteMode || m_remoteSupported != supported;
    m_remoteMode = true;
    m_remoteSupported = supported;
    m_remoteReason = reason;
    if (changed || !supported) {
        m_remoteEndpoints.clear();
        if (!supported) {
            m_clockValid = false;
        }
    }
    reconcile();
}
void RadeWaveformModel::applyRemoteContext(int sliceId, WaveformKind kind, quint64 sourceGeneration,
                                           quint64 deliveryGeneration, int sampleRate,
                                           WaveformAvailability availability, const QString& reason)
{
    if (!m_remoteMode || !m_remoteSupported || !validRequest(sliceId, kind)) {
        return;
    }
    if (!m_remoteDemandUnion.contains({sliceId, kind})) {
        return;
    }
    if (availability == WaveformAvailability::Available &&
        (sourceGeneration == 0 || deliveryGeneration == 0 || sampleRate != nativeRate(kind))) {
        return;
    }
    const WaveformEndpointState before = endpointState(sliceId, kind);
    RemoteEndpoint& endpoint = m_remoteEndpoints[key(sliceId, kind)];
    if (endpoint.sourceGeneration != sourceGeneration || endpoint.deliveryGeneration != deliveryGeneration ||
        endpoint.sampleRate != sampleRate || availability != WaveformAvailability::Available) {
        endpoint.history.clear();
    }
    endpoint.sourceGeneration = sourceGeneration;
    endpoint.deliveryGeneration = deliveryGeneration;
    endpoint.sampleRate = sampleRate;
    endpoint.availability = availability;
    endpoint.reason = reason;
    const WaveformEndpointState after = endpointState(sliceId, kind);
    if (before.sourceGeneration != after.sourceGeneration || before.available != after.available ||
        before.reason != after.reason) {
        emit endpointStateChanged(sliceId, kind, after.sourceGeneration, after.available, after.reason);
    }
}
void RadeWaveformModel::appendRemoteBatch(int sliceId, quint64 sourceGeneration, WaveformBatchPtr batch)
{
    // Before a trustworthy clock map, drop observations rather than invent arrival-time data.
    if (!m_remoteMode || !m_remoteSupported || !m_clockValid || !batch ||
        !validRequest(sliceId, batch->kind) || !m_remoteDemandUnion.contains({sliceId, batch->kind})) {
        return;
    }
    auto it = m_remoteEndpoints.find(key(sliceId, batch->kind));
    if (it == m_remoteEndpoints.end() || it->availability != WaveformAvailability::Available ||
        sourceGeneration == 0 || it->sourceGeneration != sourceGeneration || batch->token == 0 ||
        it->deliveryGeneration != batch->token || batch->sampleRate != it->sampleRate ||
        batch->sampleRate != nativeRate(batch->kind) || batch->samples.empty() ||
        batch->samples.size() > size_t(batch->sampleRate / 10) || batch->segments.empty() ||
        batch->segments.size() > 4096) {
        return;
    }
    int covered = 0;
    for (const WaveformSegment& segment : batch->segments) {
        if (segment.offset != covered || segment.count <= 0 ||
            segment.count > int(batch->samples.size()) - covered) {
            return;
        }
        covered += segment.count;
    }
    if (covered != int(batch->samples.size())) {
        return;
    }
    it->history.append(*batch);
    emit acceptedBatch(sliceId, batch->kind, sourceGeneration, batch);
}
void RadeWaveformModel::setRemoteClockMap(qint64 coreMinusLocalNs, bool valid)
{
    m_coreMinusLocalNs = coreMinusLocalNs;
    m_clockValid = valid && coreMinusLocalNs != std::numeric_limits<qint64>::min();
}
WaveformSnapshotPtr RadeWaveformModel::snapshot(int sliceId, WaveformKind kind, qint64 now)
{
    auto unavailable = std::make_shared<WaveformSnapshot>();
    unavailable->kind = kind;
    unavailable->asOfNs = now;
    unavailable->availability = WaveformAvailability::Unavailable;
    if (m_remoteMode && !m_remoteSupported) {
        unavailable->availability = WaveformAvailability::Unsupported;
        unavailable->reason = m_remoteReason.isEmpty()
                                  ? QStringLiteral("Core does not advertise waveform telemetry")
                                  : m_remoteReason;
        return unavailable;
    }
    if (!validRequest(sliceId, kind)) {
        unavailable->reason = "Unknown waveform source";
        return unavailable;
    }
    const int id = key(sliceId, kind);
    if (m_remoteMode) {
        if (!m_remoteSupported) {
            unavailable->availability = WaveformAvailability::Unsupported;
            unavailable->reason = m_remoteReason.isEmpty()
                                      ? QStringLiteral("Core does not advertise waveform telemetry")
                                      : m_remoteReason;
            return unavailable;
        }
        qint64 coreNow = 0;
        if (!m_clockValid || !safeAdd(now, m_coreMinusLocalNs, coreNow)) {
            unavailable->reason = "Waiting for a usable Core observation clock";
            return unavailable;
        }
        auto it = m_remoteEndpoints.find(id);
        if (it == m_remoteEndpoints.end()) {
            unavailable->reason = "Waiting for Core waveform source";
            return unavailable;
        }
        if (it->availability != WaveformAvailability::Available) {
            unavailable->availability = it->availability;
            unavailable->reason = it->reason;
            return unavailable;
        }
        const WaveformSnapshotPtr core = it->history.snapshot(coreNow);
        auto translated = std::make_shared<WaveformSnapshot>(*core);
        translated->asOfNs = now;
        translated->kind = kind;
        for (WaveformSegment& segment : translated->segments) {
            qint64 local = 0;
            if (!safeAdd(segment.startNs, -m_coreMinusLocalNs, local)) {
                unavailable->reason = "Core observation timestamp is outside the clock range";
                return unavailable;
            }
            segment.startNs = local;
        }
        if (translated->samples.isEmpty()) {
            translated->reason = "Waiting for observed Core PCM";
        }
        return translated;
    }
    auto it = m_endpoints.find(id);
    if (it != m_endpoints.end() && it->available && it->publisher && it->token != 0) {
        const WaveformSnapshotPtr frame = it->history.snapshot(now);
        if (!frame->samples.isEmpty()) {
            return frame;
        }
    }
    unavailable->reason = it != m_endpoints.end() && !it->reason.isEmpty()
                              ? it->reason
                              : QStringLiteral("Waiting for observed PCM");
    return unavailable;
}
} // namespace NereusSDR
