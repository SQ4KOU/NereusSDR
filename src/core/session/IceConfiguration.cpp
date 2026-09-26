// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/IceConfiguration.cpp  (NereusSDR)
// =================================================================
//
// See IceConfiguration.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/IceConfiguration.h"

#include <QHostAddress>
#include <QUrl>

namespace NereusSDR {

namespace {

// "host[:port]" or "[v6][:port]", the part of a STUN or TURN URL after its
// scheme (RFC 7064 section 3.1, RFC 7065 section 3.1).
std::optional<IceServerAddress> parseHostPort(const QString& text)
{
    if (text.isEmpty()) {
        return std::nullopt;
    }
    QString host;
    QString portText;
    if (text.startsWith(QLatin1Char('['))) {
        const qsizetype close = text.indexOf(QLatin1Char(']'));
        if (close < 0) {
            return std::nullopt;
        }
        host = text.mid(1, close - 1);
        const QString rest = text.mid(close + 1);
        if (!rest.isEmpty()) {
            if (!rest.startsWith(QLatin1Char(':'))) {
                return std::nullopt;
            }
            portText = rest.mid(1);
        }
        if (QHostAddress(host).protocol() != QAbstractSocket::IPv6Protocol) {
            return std::nullopt;
        }
    } else {
        const qsizetype colon = text.indexOf(QLatin1Char(':'));
        if (text.count(QLatin1Char(':')) > 1) {
            // A bare IPv6 literal is not allowed: RFC 7064 writes it in
            // brackets.
            return std::nullopt;
        }
        host = colon < 0 ? text : text.left(colon);
        if (colon >= 0) {
            portText = text.mid(colon + 1);
        }
        QUrl probe;
        probe.setHost(host, QUrl::StrictMode);
        if (!probe.isValid() || probe.host().isEmpty()) {
            return std::nullopt;
        }
    }
    if (host.isEmpty()) {
        return std::nullopt;
    }
    IceServerAddress address;
    address.host = host;
    address.port = IceConfiguration::kDefaultPort;
    if (!portText.isEmpty()) {
        bool ok = false;
        const int port = portText.toInt(&ok);
        if (!ok || port < 1 || port > 65535) {
            return std::nullopt;
        }
        address.port = static_cast<quint16>(port);
    }
    return address;
}

} // namespace

IceConfiguration IceConfiguration::throughRendezvous(const QStringList& stunUrls,
                                                     bool relayAllowed)
{
    IceConfiguration configuration;
    configuration.m_relayAllowed = relayAllowed;
    // The first STUN URL this build can use, in the order the service lists
    // them (section 6.1: a client uses what it needs of the list).
    for (const QString& url : stunUrls) {
        if (const auto stun = parseStunUrl(url)) {
            configuration.m_stun = stun;
            break;
        }
    }
    return configuration;
}

int IceConfiguration::setRelay(const std::optional<RendezvousWire::Turn>& turn)
{
    m_relayKnown = true;
    m_relays.clear();
    if (!m_relayAllowed || !turn) {
        return 0;
    }
    // One URL for each of the first two hosts, in the service's order: an
    // IPv6-only and an IPv4-only relay name on the NereusSDR server.
    for (const QString& url : turn->urls) {
        const auto address = parseTurnUrl(url);
        if (!address) {
            continue;
        }
        bool hostSeen = false;
        for (const IceRelayServer& relay : std::as_const(m_relays)) {
            if (relay.host.compare(address->host, Qt::CaseInsensitive) == 0) {
                hostSeen = true;
                break;
            }
        }
        if (hostSeen) {
            continue;
        }
        m_relays.append(IceRelayServer{address->host, address->port, turn->username,
                                       turn->password});
        if (m_relays.size() >= kMaxRelayServers) {
            break;
        }
    }
    return static_cast<int>(m_relays.size());
}

bool IceConfiguration::acceptsRemoteCandidate(const QString& candidate) const
{
    if (!RendezvousWire::isCandidate(candidate) || candidate.isEmpty()) {
        return false;
    }
    return m_relayAllowed || candidateType(candidate) != QLatin1String("relay");
}

std::optional<IceServerAddress> IceConfiguration::parseStunUrl(const QString& url)
{
    if (!url.startsWith(QLatin1String("stun:"))) {
        return std::nullopt;
    }
    QString rest = url.mid(5);
    if (rest.contains(QLatin1Char('?'))) {
        return std::nullopt;
    }
    return parseHostPort(rest);
}

std::optional<IceServerAddress> IceConfiguration::parseTurnUrl(const QString& url)
{
    if (!url.startsWith(QLatin1String("turn:"))) {
        // turns: is TURN over TLS, which the pinned libjuice cannot speak.
        return std::nullopt;
    }
    QString rest = url.mid(5);
    const qsizetype query = rest.indexOf(QLatin1Char('?'));
    if (query >= 0) {
        const QString parameters = rest.mid(query + 1);
        rest = rest.left(query);
        if (parameters.compare(QLatin1String("transport=udp"), Qt::CaseInsensitive) != 0) {
            return std::nullopt;
        }
    }
    return parseHostPort(rest);
}

QString IceConfiguration::candidateType(const QString& candidate)
{
    // RFC 8839 section 5.1: "... typ <type> ...".
    const QStringList fields = candidate.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (qsizetype index = 0; index + 1 < fields.size(); ++index) {
        if (fields.at(index) == QLatin1String("typ")) {
            return fields.at(index + 1);
        }
    }
    return {};
}

} // namespace NereusSDR
