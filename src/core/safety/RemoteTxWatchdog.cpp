// no-port-check: NereusSDR-original.
// =================================================================
// src/core/safety/RemoteTxWatchdog.cpp  (NereusSDR)
// =================================================================
//
// See RemoteTxWatchdog.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 37 (R-IOS-13), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/safety/RemoteTxWatchdog.h"

#include "core/LogCategories.h"

#include <QList>
#include <QPointer>

#include <algorithm>
#include <limits>

namespace NereusSDR {

RemoteTxWatchdog::RemoteTxWatchdog(QObject* parent)
    : QObject(parent)
{
}

void RemoteTxWatchdog::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

QString RemoteTxWatchdog::stopMessage(const QString& deviceName)
{
    return QStringLiteral("The link to %1 went quiet, so the Core stopped transmitting.")
        .arg(deviceName);
}

qint64 RemoteTxWatchdog::now() const
{
    return m_hooks.clock ? m_hooks.clock() : 0;
}

bool RemoteTxWatchdog::isWatching(const QByteArray& deviceId) const
{
    return m_devices.contains(deviceId);
}

void RemoteTxWatchdog::setKeyed(const QByteArray& deviceId, bool keyed, quint32 epoch)
{
    update(deviceId, keyed, true, epoch, false, false);
}

void RemoteTxWatchdog::setVoxArmed(const QByteArray& deviceId, bool armed)
{
    update(deviceId, false, false, 0, armed, true);
}

void RemoteTxWatchdog::update(const QByteArray& deviceId, bool keyed, bool keyedChanged,
                              quint32 epoch, bool voxArmed, bool voxChanged)
{
    if (deviceId.isEmpty()) {
        return;
    }
    auto it = m_devices.find(deviceId);
    const bool wasWatched = it != m_devices.end();
    Watch watch = wasWatched ? it.value() : Watch{};
    if (keyedChanged) {
        watch.keyed = keyed;
        watch.epoch = keyed ? epoch : 0;
    }
    if (voxChanged) {
        watch.voxArmed = voxArmed;
    }
    if (!watch.keyed && !watch.voxArmed) {
        if (wasWatched) {
            m_devices.erase(it);
            qCDebug(lcDsp) << "Transmit watchdog: no longer watching" << deviceId;
            reschedule();
        }
        return;
    }
    if (!wasWatched) {
        // A fresh 400 ms, and sequences start again.
        watch.lastMs = now();
        watch.lastSequence = 0;
        qCDebug(lcDsp) << "Transmit watchdog: watching" << deviceId
                      << (watch.keyed ? "(keyed)" : "(VOX armed)");
    }
    m_devices.insert(deviceId, watch);
    reschedule();
}

bool RemoteTxWatchdog::keepalive(const QByteArray& deviceId, quint64 sequence, quint32 epoch,
                                 Path path)
{
    Q_UNUSED(path);
    auto it = m_devices.find(deviceId);
    if (it == m_devices.end()) {
        // Nothing watched: a keepalive before the key, or after its end.
        return false;
    }
    Watch& watch = it.value();
    const qint64 heard = now();
    // Socket input can run before an overdue timer on a busy event loop.
    // Once the watch expires, no arriving packet may renew it.
    if (heard - watch.lastMs > kLinkLossDeadlineMs) {
        trip(deviceId, false);
        return false; // The stop callback may have destroyed this object.
    }
    if (sequence <= watch.lastSequence) {
        // A copy (the same keepalive by another path) or one overtaken.
        return false;
    }
    if (watch.epoch != 0 && epoch < watch.epoch) {
        // From a key that has ended: it says nothing about this one.
        return false;
    }
    watch.lastSequence = sequence;
    // Task 29 step 2b: how long since the one before, for the measurement
    // of keyed-event tails on the web relay (nothing acts on it).
    const qint64 gap = watch.lastMs > 0 ? heard - watch.lastMs : -1;
    watch.lastMs = heard;
    reschedule();
    emit keepaliveHeard(deviceId, gap);
    return true;
}

void RemoteTxWatchdog::linkClosed(const QByteArray& deviceId)
{
    if (!m_devices.contains(deviceId)) {
        return;
    }
    trip(deviceId, true);
}

void RemoteTxWatchdog::onTimer()
{
    const qint64 at = now();
    QList<QByteArray> quiet;
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        if (at - it.value().lastMs > kLinkLossDeadlineMs) {
            quiet.append(it.key());
        }
    }
    const QPointer<RemoteTxWatchdog> self(this);
    for (const QByteArray& deviceId : std::as_const(quiet)) {
        if (!self) {
            return;
        }
        // A stop for one device may already have ended another's watch.
        if (m_devices.contains(deviceId)) {
            trip(deviceId, false);
        }
    }
    if (self) {
        reschedule();
    }
}

void RemoteTxWatchdog::trip(const QByteArray& deviceId, bool linkClosed)
{
    const Watch watch = m_devices.take(deviceId);
    const qint64 silentMs = now() - watch.lastMs;
    const QString name = m_hooks.deviceName ? m_hooks.deviceName(deviceId) : QString();
    const QString message = stopMessage(name.isEmpty() ? QStringLiteral("a device") : name);
    qCWarning(lcDsp).noquote() << "Transmit watchdog:"
                               << (linkClosed ? "the link closed" : "no keepalive for")
                               << (linkClosed ? QString() : QStringLiteral("%1 ms").arg(silentMs))
                               << "from" << QString::fromLatin1(deviceId)
                               << "-" << message;
    const QPointer<RemoteTxWatchdog> self(this);
    if (m_hooks.stop) {
        m_hooks.stop(deviceId, message);
    }
    if (!self) {
        return;
    }
    emit tripped(deviceId, linkClosed, silentMs);
    if (self) {
        reschedule();
    }
}

void RemoteTxWatchdog::reschedule()
{
    if (m_devices.isEmpty()) {
        if (m_hooks.stopTimer) {
            m_hooks.stopTimer();
        }
        return;
    }
    qint64 earliest = std::numeric_limits<qint64>::max();
    for (auto it = m_devices.cbegin(); it != m_devices.cend(); ++it) {
        // "More than 400 ms": the first millisecond past the deadline.
        earliest = std::min(earliest, it.value().lastMs + kLinkLossDeadlineMs + 1);
    }
    const qint64 wait = std::max<qint64>(0, earliest - now());
    if (m_hooks.startTimer) {
        m_hooks.startTimer(static_cast<int>(std::min<qint64>(wait, kLinkLossDeadlineMs + 1)));
    }
}

QByteArray RemoteTxWatchdog::channelKeepalive(quint64 sequence, quint32 epoch)
{
    QByteArray message(kChannelKeepaliveBytes, '\0');
    message[0] = static_cast<char>(kChannelKeepaliveKind);
    for (int i = 0; i < 8; ++i) {
        message[1 + i] = static_cast<char>((sequence >> (56 - 8 * i)) & 0xFFu);
    }
    for (int i = 0; i < 4; ++i) {
        message[9 + i] = static_cast<char>((epoch >> (24 - 8 * i)) & 0xFFu);
    }
    return message;
}

bool RemoteTxWatchdog::readChannelKeepalive(const QByteArray& message, quint64* sequence,
                                            quint32* epoch)
{
    if (message.size() != kChannelKeepaliveBytes
        || static_cast<quint8>(message.at(0)) != kChannelKeepaliveKind) {
        return false;
    }
    quint64 s = 0;
    for (int i = 0; i < 8; ++i) {
        s = (s << 8) | static_cast<quint8>(message.at(1 + i));
    }
    quint32 e = 0;
    for (int i = 0; i < 4; ++i) {
        e = (e << 8) | static_cast<quint8>(message.at(9 + i));
    }
    if (s == 0) {
        return false;
    }
    if (sequence) {
        *sequence = s;
    }
    if (epoch) {
        *epoch = e;
    }
    return true;
}

} // namespace NereusSDR
