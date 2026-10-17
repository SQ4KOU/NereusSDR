#pragma once

// no-port-check: NereusSDR-original helper, moved unchanged from
// P2RadioConnection.cpp so the Protocol 1 EP2 sender shares it with the
// Protocol 2 transmit I/Q sender. No upstream code is reproduced here.
//
// Modification history (NereusSDR):
//   2026-10-14 - Moved here unchanged from P2RadioConnection.cpp (where it
//                sat in an anonymous namespace), for the Protocol 1 EP2
//                sender. J.J. Boyd (KG4VCF), with AI-assisted implementation
//                via Anthropic Claude Code.

#include <QtGlobal>

#ifdef Q_OS_WIN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace NereusSDR {

// The native send on the connection socket's descriptor (the same source
// port as every other packet to the radio, as Thetis's single listenSock).
// The kernel serialises datagrams on one socket, so this thread and the
// connection thread may send on it at once. QUdpSocket binds
// QHostAddress::Any as a dual-stack IPv6 socket, so the destination is
// written in the socket's own family (an IPv4-mapped address on IPv6).
struct TxIqNativeDest {
    qintptr fd{-1};
    sockaddr_storage addr{};
    int addrLen{0};
    int lastError{0};
};

inline bool fillTxIqNativeDest(TxIqNativeDest* d, qintptr fd, quint32 ipv4, quint16 port)
{
    d->fd = fd;
    sockaddr_storage local{};
#ifdef Q_OS_WIN
    int localLen = static_cast<int>(sizeof(local));
    if (::getsockname(static_cast<SOCKET>(fd), reinterpret_cast<sockaddr*>(&local), &localLen) != 0) {
        return false;
    }
#else
    socklen_t localLen = sizeof(local);
    if (::getsockname(static_cast<int>(fd), reinterpret_cast<sockaddr*>(&local), &localLen) != 0) {
        return false;
    }
#endif
    if (local.ss_family == AF_INET6) {
        auto* a6 = reinterpret_cast<sockaddr_in6*>(&d->addr);
        a6->sin6_family = AF_INET6;
        a6->sin6_port = htons(port);
        // ::ffff:a.b.c.d
        unsigned char* b = reinterpret_cast<unsigned char*>(&a6->sin6_addr);
        b[10] = 0xff;
        b[11] = 0xff;
        b[12] = static_cast<unsigned char>((ipv4 >> 24) & 0xff);
        b[13] = static_cast<unsigned char>((ipv4 >> 16) & 0xff);
        b[14] = static_cast<unsigned char>((ipv4 >> 8) & 0xff);
        b[15] = static_cast<unsigned char>(ipv4 & 0xff);
        d->addrLen = static_cast<int>(sizeof(sockaddr_in6));
        return true;
    }
    if (local.ss_family == AF_INET) {
        auto* a4 = reinterpret_cast<sockaddr_in*>(&d->addr);
        a4->sin_family = AF_INET;
        a4->sin_port = htons(port);
        a4->sin_addr.s_addr = htonl(ipv4);
        d->addrLen = static_cast<int>(sizeof(sockaddr_in));
        return true;
    }
    return false;
}

} // namespace NereusSDR
