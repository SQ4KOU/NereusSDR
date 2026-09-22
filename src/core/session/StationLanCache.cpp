// NereusSDR-original bounded cache for untrusted Core LAN announcements.
#include "StationLanCache.h"

#include <algorithm>
#include <QSet>

namespace NereusSDR {
namespace {

void setError(QString* error, const char* text)
{
    if (error) {
        *error = QString::fromLatin1(text);
    }
}

bool usableSource(const QHostAddress& source)
{
    if (source.isNull() || source == QHostAddress::Any || source == QHostAddress::AnyIPv4
        || source == QHostAddress::AnyIPv6 || source == QHostAddress::Broadcast
        || source.isMulticast()) {
        return false;
    }
    return source.protocol() == QAbstractSocket::IPv4Protocol
        || source.protocol() == QAbstractSocket::IPv6Protocol;
}

QHostAddress normalizedSource(QHostAddress source, uint interfaceIndex, QString* error)
{
    if (!usableSource(source)) {
        setError(error, "Station LAN announcement has an invalid source address.");
        return {};
    }
    if (source.protocol() == QAbstractSocket::IPv6Protocol && source.isLinkLocal()
        && source.scopeId().isEmpty()) {
        if (interfaceIndex == 0) {
            setError(error, "Station LAN announcement has an unscoped link-local source.");
            return {};
        }
        source.setScopeId(QString::number(interfaceIndex));
    }
    return source;
}

} // namespace

bool StationLanCache::ingest(const QByteArray& bytes, const QHostAddress& source,
                             uint interfaceIndex, qint64 nowMs, QString* error)
{
    const auto decoded = decodeStationLanAnnouncement(bytes, error);
    if (!decoded) {
        return false;
    }
    const QHostAddress normalized = normalizedSource(source, interfaceIndex, error);
    if (normalized.isNull()) {
        return false;
    }
    const bool expired = expire(nowMs);
    StationLanEndpoint candidate{*decoded, normalized, interfaceIndex, nowMs};
    const QString candidateKey = candidate.key();
    for (StationLanEndpoint& existing : m_endpoints) {
        if (existing.key() != candidateKey) {
            continue;
        }
        const bool changed = existing.announcement != candidate.announcement;
        existing.announcement = candidate.announcement;
        existing.lastSeenMs = nowMs;
        if (error) {
            error->clear();
        }
        return expired || changed;
    }
    QSet<QString> fingerprints;
    int endpointCount = 0;
    for (const StationLanEndpoint& endpoint : m_endpoints) {
        fingerprints.insert(endpoint.announcement.fingerprint);
        if (endpoint.announcement.fingerprint == candidate.announcement.fingerprint) {
            ++endpointCount;
        }
    }
    if (!fingerprints.contains(candidate.announcement.fingerprint)
        && fingerprints.size() >= kMaxFingerprints) {
        setError(error, "Station LAN announcement cache is full.");
        return expired;
    }
    if (endpointCount >= kMaxEndpointsPerFingerprint) {
        setError(error, "Station LAN announcement endpoint limit reached.");
        return expired;
    }
    m_endpoints.append(std::move(candidate));
    if (error) {
        error->clear();
    }
    return true;
}

bool StationLanCache::expire(qint64 nowMs)
{
    const auto firstExpired = std::remove_if(m_endpoints.begin(), m_endpoints.end(),
        [nowMs](const StationLanEndpoint& endpoint) {
            return nowMs >= endpoint.lastSeenMs
                && nowMs - endpoint.lastSeenMs >= kStationLanCacheTtlMs;
        });
    if (firstExpired == m_endpoints.end()) {
        return false;
    }
    m_endpoints.erase(firstExpired, m_endpoints.end());
    return true;
}

} // namespace NereusSDR
