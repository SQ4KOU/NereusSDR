// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/RemoteDevicesState.cpp  (NereusSDR)
// =================================================================
//
// See RemoteDevicesState.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 78 (R-IOS-02, R-IOS-07,
//               R-IOS-30), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/RemoteDevicesState.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace NereusSDR {

namespace {

constexpr const char* kMarkerPrefix = "marker:";
constexpr const char* kConnectedKey = "connectedDevices";
constexpr const char* kDevicesKey = "devices";

RemoteDeviceSlice sliceFrom(const QJsonObject& o)
{
    RemoteDeviceSlice slice;
    slice.sliceId = o.value(QStringLiteral("sliceId")).toInt(-1);
    slice.letter = o.value(QStringLiteral("letter")).toString();
    slice.band = o.value(QStringLiteral("band")).toInt(-1);
    slice.mode = o.value(QStringLiteral("mode")).toInt(-1);
    return slice;
}

QJsonArray arrayOf(const QString& listJson)
{
    const QJsonDocument doc = QJsonDocument::fromJson(listJson.toUtf8());
    return doc.isArray() ? doc.array() : QJsonArray{};
}

} // namespace

QString RemoteSliceMarker::letter() const
{
    return sliceId >= 0 ? QString(QChar(u'A' + sliceId)) : QString();
}

RemoteDevicesState::RemoteDevicesState(QObject* parent)
    : QObject(parent)
{
}

bool RemoteDevicesState::holdsKey(const QByteArray& objectKey)
{
    return objectKey == kConnectedKey || objectKey == kDevicesKey
        || markerSliceId(objectKey) >= 0;
}

int RemoteDevicesState::markerSliceId(const QByteArray& objectKey)
{
    if (!objectKey.startsWith(kMarkerPrefix)) {
        return -1;
    }
    bool ok = false;
    const int id = objectKey.mid(int(qstrlen(kMarkerPrefix))).toInt(&ok);
    return ok && id >= 0 ? id : -1;
}

void RemoteDevicesState::applyObject(const QByteArray& objectKey,
                                     const QList<MirrorUpdate>& updates)
{
    if (objectKey == kConnectedKey) {
        bool changed = false;
        for (const MirrorUpdate& u : updates) {
            if (u.name == "listJson") {
                m_connected = parseConnectedList(u.value.toString());
                changed = true;
            } else if (u.name == "deviceLimit") {
                m_deviceLimit = int(u.value.toLongLong());
                changed = true;
            }
        }
        if (changed) {
            emit connectedDevicesChanged();
        }
        return;
    }
    if (objectKey == kDevicesKey) {
        // Only the list. The object also carries the pairing code, which
        // this window neither keeps nor shows here.
        for (const MirrorUpdate& u : updates) {
            if (u.name == "listJson") {
                m_paired = parsePairedList(u.value.toString());
                emit pairedDevicesChanged();
            }
        }
        return;
    }
    const int sliceId = markerSliceId(objectKey);
    if (sliceId < 0) {
        return;
    }
    RemoteSliceMarker& m = m_markers[sliceId];
    m.sliceId = sliceId;
    for (const MirrorUpdate& u : updates) {
        const QByteArray& n = u.name;
        if (n == "ownerDeviceId") {
            m.ownerDeviceId = u.value.toString();
        } else if (n == "ownerName") {
            m.ownerName = u.value.toString();
        } else if (n == "ownerShortName") {
            m.ownerShortName = u.value.toString();
        } else if (n == "ownerKind") {
            m.ownerKind = u.value.toString();
        } else if (n == "ownerAway") {
            m.ownerAway = u.value.toBool();
        } else if (n == "frequency") {
            m.frequencyHz = u.value.toDouble();
        } else if (n == "dspMode") {
            m.dspMode = int(u.value.toLongLong());
        } else if (n == "filterLow") {
            m.filterLowHz = int(u.value.toLongLong());
        } else if (n == "filterHigh") {
            m.filterHighHz = int(u.value.toLongLong());
        } else if (n == "txSlice") {
            m.txSlice = u.value.toBool();
        } else if (n == "band") {
            m.band = int(u.value.toLongLong());
        } else if (n == "streamIndex") {
            m.streamIndex = int(u.value.toLongLong());
        } else if (n == "psPaused") {
            m.psPaused = u.value.toBool();
        }
    }
    emit markersChanged();
}

void RemoteDevicesState::destroyObject(const QByteArray& objectKey)
{
    if (objectKey == kConnectedKey) {
        m_connected.clear();
        m_deviceLimit = 0;
        emit connectedDevicesChanged();
        return;
    }
    if (objectKey == kDevicesKey) {
        m_paired.clear();
        emit pairedDevicesChanged();
        return;
    }
    const int sliceId = markerSliceId(objectKey);
    if (sliceId >= 0 && m_markers.remove(sliceId) > 0) {
        emit markersChanged();
    }
}

void RemoteDevicesState::setQuestion(const SessionPrompt& prompt, const QString& reason)
{
    m_question = RemotePrompt{prompt, reason, QDateTime::currentDateTime()};
    emit questionChanged();
}

void RemoteDevicesState::closeQuestion(qint64 id)
{
    if (!m_question || m_question->prompt.id != id) {
        return;
    }
    m_question.reset();
    emit questionChanged();
}

void RemoteDevicesState::addNotice(const SessionPrompt& prompt, const QString& reason)
{
    const RemotePrompt entry{prompt, reason, QDateTime::currentDateTime()};
    bool replaced = false;
    for (RemotePrompt& existing : m_notices) {
        if (existing.prompt.id == prompt.id) {
            existing = entry;
            replaced = true;
        }
    }
    if (!replaced) {
        m_notices.append(entry);
    }
    emit noticesChanged();
    emit noticeArrived(prompt.id);
}

void RemoteDevicesState::dismissNotice(qint64 id)
{
    for (int i = 0; i < m_notices.size(); ++i) {
        if (m_notices.at(i).prompt.id == id) {
            m_notices.removeAt(i);
            emit noticesChanged();
            return;
        }
    }
}

std::optional<RemotePrompt> RemoteDevicesState::notice(qint64 id) const
{
    for (const RemotePrompt& n : m_notices) {
        if (n.prompt.id == id) {
            return n;
        }
    }
    return std::nullopt;
}

std::optional<RemoteSliceMarker> RemoteDevicesState::marker(int sliceId) const
{
    const auto it = m_markers.constFind(sliceId);
    if (it == m_markers.cend()) {
        return std::nullopt;
    }
    return it.value();
}

std::optional<RemoteConnectedDevice>
RemoteDevicesState::connectedDevice(const QString& deviceId) const
{
    for (const RemoteConnectedDevice& d : m_connected) {
        if (d.deviceId == deviceId) {
            return d;
        }
    }
    return std::nullopt;
}

void RemoteDevicesState::clear()
{
    const bool hadMarkers = !m_markers.isEmpty();
    const bool hadConnected = !m_connected.isEmpty() || m_deviceLimit != 0;
    const bool hadPaired = !m_paired.isEmpty();
    const bool hadQuestion = m_question.has_value();
    // Notices stay: what another device did is still worth reading after
    // the link drops, and each card goes when the operator puts it away.
    m_markers.clear();
    m_connected.clear();
    m_paired.clear();
    m_deviceLimit = 0;
    m_question.reset();
    if (hadMarkers) { emit markersChanged(); }
    if (hadConnected) { emit connectedDevicesChanged(); }
    if (hadPaired) { emit pairedDevicesChanged(); }
    if (hadQuestion) { emit questionChanged(); }
}

QList<RemoteConnectedDevice> RemoteDevicesState::parseConnectedList(const QString& listJson)
{
    QList<RemoteConnectedDevice> out;
    for (const QJsonValue& v : arrayOf(listJson)) {
        const QJsonObject o = v.toObject();
        RemoteConnectedDevice d;
        d.deviceId = o.value(QStringLiteral("deviceId")).toString();
        d.name = o.value(QStringLiteral("name")).toString();
        d.shortName = o.value(QStringLiteral("shortName")).toString();
        d.kind = o.value(QStringLiteral("kind")).toString();
        d.paired = o.value(QStringLiteral("paired")).toBool();
        d.hostsCore = o.value(QStringLiteral("hostsCore")).toBool();
        d.revocable = o.value(QStringLiteral("revocable")).toBool();
        d.state = o.value(QStringLiteral("state")).toString();
        d.holdsTransmit = o.value(QStringLiteral("holdsTransmit")).toBool();
        d.lastActivitySeconds = o.value(QStringLiteral("lastActivitySeconds")).toInteger();
        d.connectedForSeconds = o.value(QStringLiteral("connectedForSeconds")).toInteger();
        d.awayForSeconds = o.value(QStringLiteral("awayForSeconds")).toInteger();
        d.transmittingForSeconds = o.value(QStringLiteral("transmittingForSeconds")).toInteger();
        for (const QJsonValue& s : o.value(QStringLiteral("listeningOn")).toArray()) {
            d.listeningOn.append(sliceFrom(s.toObject()));
        }
        if (o.value(QStringLiteral("transmittingOn")).isObject()) {
            d.transmittingOn = sliceFrom(o.value(QStringLiteral("transmittingOn")).toObject());
        }
        out.append(d);
    }
    return out;
}

QList<RemotePairedDevice> RemoteDevicesState::parsePairedList(const QString& listJson)
{
    QList<RemotePairedDevice> out;
    for (const QJsonValue& v : arrayOf(listJson)) {
        const QJsonObject o = v.toObject();
        RemotePairedDevice d;
        d.id = o.value(QStringLiteral("id")).toString();
        d.name = o.value(QStringLiteral("name")).toString();
        d.shortName = o.value(QStringLiteral("shortName")).toString();
        d.kind = o.value(QStringLiteral("kind")).toString();
        d.lastSeen = o.value(QStringLiteral("lastSeen")).toString();
        d.connected = o.value(QStringLiteral("connected")).toBool();
        out.append(d);
    }
    return out;
}

} // namespace NereusSDR
