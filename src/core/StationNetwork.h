// no-port-check: NereusSDR-original. R-R3-48 station network address choice.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#pragma once

#include <QHostAddress>
#include <QList>
#include <QNetworkAddressEntry>

namespace NereusSDR::StationNetwork {

/// This computer's address on the network that holds `peer` (the radio, or
/// an amplifier): the first IPv4 address entry whose subnet contains it.
/// Null when no entry does, or `peer` is not an IPv4 address.
QHostAddress addressFacing(const QHostAddress& peer,
                           const QList<QNetworkAddressEntry>& entries);

/// The address entries of this computer's running interfaces (loopback
/// left out), as QNetworkInterface reports them.
QList<QNetworkAddressEntry> localEntries();

/// `address` as IPv4 when it is an IPv4-mapped IPv6 address, else itself.
QHostAddress plainIpv4(const QHostAddress& address);

} // namespace NereusSDR::StationNetwork
