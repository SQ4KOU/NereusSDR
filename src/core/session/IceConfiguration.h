#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/IceConfiguration.h  (NereusSDR)
// =================================================================
//
// The ICE settings of a peer connection that came through the rendezvous
// (iPhone app plan Task 27, R-IOS-16; the rendezvous document,
// docs/architecture/2026-09-23-rendezvous-v1.md, sections 6.1, 6.3 and 8).
// A peer that did not come through the rendezvous has none, and gathers
// host candidates only, as before.
//
// Built for the pinned libdatachannel v0.24.5 over libjuice (@3c40a354),
// whose limits decide its shape:
//
//   - One STUN server. libdatachannel picks one STUN server from its
//     configuration at random (src/impl/icetransport.cpp:101-113), so this
//     carries exactly one: the first the service's hello lists. On
//     rv.nereussdr.com that is the IPv4-only name (rv4), because a peer
//     that has only IPv4, behind NAT, needs a server-reflexive candidate
//     (the service's operators, 2026-09-26).
//   - Relay servers: libdatachannel takes at most two (icetransport.cpp:39,
//     MAX_TURN_SERVERS_COUNT; libjuice agent.h:65, MAX_RELAY_ENTRIES_COUNT),
//     and libjuice resolves one address per TURN host, preferring IPv4
//     (agent.c:386-395), so each address family needs a name of its own:
//     the service lists an IPv6-only and an IPv4-only relay name (section
//     8). The relay is sized by allocations, four for each Core's id with
//     both ends of a session sharing them, so one end relays on one family
//     unless it asks for both: the first host of the service's list, the
//     first URL for it (setRelay()'s `families`). Only UDP relays: libjuice
//     speaks TURN over UDP alone (icetransport.cpp:159-162).
//   - A full relay (TURN 486, Allocation Quota Reached) is an ordinary
//     outcome, not an error: libjuice marks that relay failed and finishes
//     gathering without it (agent.c:1941-1949), and the connection goes on
//     with the paths it has.
//   - Credentials are fixed when gathering starts: the peer is built with
//     automatic gathering off, and gathering starts once the credentials
//     are known (or known to be absent), with the TURN servers passed to
//     gatherLocalCandidates() (peerconnection.cpp:170-180).
//   - An MTU of 996 bytes: TURN's ChannelData header (4 bytes) then keeps
//     every relayed datagram at the 1000 bytes the pairing design caps
//     media at (section 9.3).
//   - Deadlines: libjuice gives up on a STUN or TURN server after 23.5 s
//     (agent.h:29, MAX_STUN_SERVER_RETRANSMISSION_COUNT) and on the
//     connectivity checks after 39.5 s (agent.h:43, ICE_PAC_TIMEOUT), so a
//     deadline for the whole connection covers both, one after the other.
//
// `relay = deny` in nereusd.conf keeps the Core off the relay: it asks for
// no credentials and refuses the far end's relay candidates, so its
// connections are direct or nothing (the pairing design, section 5.4).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/RendezvousWire.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace NereusSDR {

/// A STUN server, or a TURN server's address.
struct IceServerAddress {
    QString host;
    quint16 port = 0;

    bool operator==(const IceServerAddress&) const = default;
};

/// A TURN server with the credentials the rendezvous minted (UDP only).
struct IceRelayServer {
    QString host;
    quint16 port = 0;
    QString username;
    QString password;

    bool operator==(const IceRelayServer&) const = default;
};

class IceConfiguration {
public:
    /// libdatachannel v0.24.5 src/impl/icetransport.cpp:39 and libjuice
    /// @3c40a354 src/agent.h:65.
    static constexpr int kMaxRelayServers = 2;
    /// 1000 bytes on the wire less TURN's 4-byte ChannelData header.
    static constexpr int kMtuBytes = 996;
    /// libjuice @3c40a354 src/agent.h:29 (a STUN or TURN server's last
    /// retransmission, 23.5 s in all) and src/agent.h:43 (ICE_PAC_TIMEOUT).
    static constexpr int kGatheringDeadlineMs = 23500;
    static constexpr int kConnectivityTimeoutMs = 39500;
    /// Gathering, then the connectivity checks: how long an ICE connection
    /// through the rendezvous may take before its failure is certain.
    static constexpr int kConnectDeadlineMs = kGatheringDeadlineMs + kConnectivityTimeoutMs;
    /// The port a STUN or TURN URL means when it names none (RFC 7064, RFC
    /// 7065).
    static constexpr quint16 kDefaultPort = 3478;

    /// A connection through the rendezvous: `stunUrls` as the service's
    /// hello listed them; `relayAllowed` the station's `relay` setting (a
    /// client allows it). No relay servers until setRelay().
    static IceConfiguration throughRendezvous(const QStringList& stunUrls, bool relayAllowed);

    /// The relay credentials the introduction brought (the service's
    /// `turn`; nullopt when it sent null). Ignored when the relay is not
    /// allowed. `families` is how many relay hosts to allocate on, 1 (the
    /// first the service lists) or 2 (the first two, one for each address
    /// family, only where an end needs both); each allocation takes one of
    /// the relay's slots for this Core. Returns how many relay servers the
    /// configuration now holds.
    int setRelay(const std::optional<RendezvousWire::Turn>& turn, int families = 1);

    std::optional<IceServerAddress> stunServer() const { return m_stun; }
    QList<IceRelayServer> relayServers() const { return m_relays; }
    bool relayAllowed() const { return m_relayAllowed; }
    /// When the relay credentials arrived: gathering may start.
    bool relayKnown() const { return m_relayKnown; }

    /// Whether a candidate the far end sent may be used: a relay candidate
    /// (`typ relay`) only when the relay is allowed.
    bool acceptsRemoteCandidate(const QString& candidate) const;

    /// `stun:host[:port]` (RFC 7064), an IPv6 literal in brackets.
    static std::optional<IceServerAddress> parseStunUrl(const QString& url);
    /// `turn:host[:port][?transport=udp]` (RFC 7065). nullopt for `turns:`
    /// and for any transport but UDP, which the pinned libjuice cannot use.
    static std::optional<IceServerAddress> parseTurnUrl(const QString& url);
    /// The candidate's type (`host`, `srflx`, `prflx`, `relay`), empty when
    /// it has none.
    static QString candidateType(const QString& candidate);

private:
    std::optional<IceServerAddress> m_stun;
    QList<IceRelayServer> m_relays;
    bool m_relayAllowed = true;
    bool m_relayKnown = false;
};

} // namespace NereusSDR
