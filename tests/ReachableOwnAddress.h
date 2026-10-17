#pragma once
// tests/ReachableOwnAddress.h
//
// no-port-check: NereusSDR-original test helper.
//
// The address a test uses when a real address of this computer stands in
// for the station network (tst_remote_peripherals, tst_station_tci_server).
//
// 2026-10-11: moved here from tst_remote_peripherals.cpp so both tests pick
// the address the same way. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
// Claude Code.

#include <QAbstractSocket>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

namespace NereusSDR::TestNetwork {

// An IPv4 address of this computer, other than loopback, that this computer
// can itself open a TCP connection to; empty when it has none. The first
// non-loopback address is not always one: an overlay network's interface
// (ZeroTier bridging the LAN, measured 2026-10-11) can hold an address in
// the LAN's own range that times out when dialled from this machine, and a
// test that dialled it then failed for a reason that was not the Core's.
inline QString reachableOwnAddress()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::AnyIPv4, 0)) { return {}; }
    for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol || address.isLoopback()) {
            continue;
        }
        QTcpSocket socket;
        socket.connectToHost(address, probe.serverPort());
        const bool dialled = socket.waitForConnected(500);
        const bool arrived = probe.waitForNewConnection(500);
        // Both ends: the dial completes and this listener sees it arrive.
        // The dial alone is not proof; to the overlay address it completed
        // from the LAN interface and never arrived here.
        if (dialled && arrived) {
            return address.toString();
        }
    }
    return {};
}

} // namespace NereusSDR::TestNetwork
