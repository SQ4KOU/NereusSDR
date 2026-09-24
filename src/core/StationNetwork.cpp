// no-port-check: NereusSDR-original. R-R3-48 station network address choice.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#include "core/StationNetwork.h"

#include <QNetworkInterface>

namespace NereusSDR::StationNetwork {

QHostAddress plainIpv4(const QHostAddress& address)
{
    bool ok = false;
    const quint32 v4 = address.toIPv4Address(&ok);
    return ok ? QHostAddress(v4) : address;
}

QHostAddress addressFacing(const QHostAddress& peer,
                           const QList<QNetworkAddressEntry>& entries)
{
    const QHostAddress target = plainIpv4(peer);
    if (target.protocol() != QAbstractSocket::IPv4Protocol || target.isLoopback()) {
        return {};
    }
    for (const QNetworkAddressEntry& entry : entries) {
        const QHostAddress ip = entry.ip();
        if (ip.protocol() != QAbstractSocket::IPv4Protocol || ip.isLoopback()) {
            continue;
        }
        const int prefix = entry.prefixLength();
        if (prefix <= 0 || prefix > 32) {
            continue;
        }
        if (target.isInSubnet(ip, prefix)) {
            return ip;
        }
    }
    return {};
}

QList<QNetworkAddressEntry> localEntries()
{
    QList<QNetworkAddressEntry> entries;
    const auto interfaces = QNetworkInterface::allInterfaces();
    for (const QNetworkInterface& iface : interfaces) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }
        entries.append(iface.addressEntries());
    }
    return entries;
}

} // namespace NereusSDR::StationNetwork
