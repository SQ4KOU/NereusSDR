// =================================================================
// tests/tst_juice_dual_stack_port.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test. On macOS libjuice's dual-stack
// ICE socket (IPv6 wildcard, IPV6_V6ONLY off) could bind a port another
// program already held as an IPv4-only socket, and IPv4 datagrams to it
// then went to that program: on 2026-10-16 an Android emulator held
// udp4 *.52282 while a data channel test's agent bound udp46 *.52282, and
// the channel never opened (cmake/patches/libjuice-0004). These cases hold
// IPv4 ports the way that program did and check that every socket libjuice
// makes still gets its own IPv4 datagrams, and that a data channel whose
// port range is mostly held still opens. Only this computer's own
// addresses are used; no radio, Core or audio device is opened.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-16 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-16 - Review fixes: the callbacks' state outlives an early
//                 return, and a host without IPv6 finds a free range.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QScopeGuard>
#include <QtTest/QtTest>

#include <rtc/rtc.hpp>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

extern "C" {
#include "udp.h"
}
#endif

namespace {

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)

// With one in eight of the system's ephemeral ports held for IPv4, 7 of
// 100 unprotected system-picked dual-stack binds landed on a held port on
// 2026-10-16; at that rate 200 binds all miss in under 1 run in a million.
constexpr int kHeldIpv4Ports = 2048;
constexpr int kPickedSockets = 200;
constexpr int kDatagramWaitMs = 1000;
// A data channel's port range: all but kFreeInRange of its ports are held
// for IPv4 by another socket, so each unprotected agent lands on a held
// port 22 times in 24.
constexpr int kRangeSize = 24;
constexpr int kFreeInRange = 2;
constexpr int kOpenTimeoutMs = 10000;

// IPv4-only wildcard sockets, as the other program held them.
class Ipv4Holds {
public:
    ~Ipv4Holds() { release(); }

    // Binds 0.0.0.0:`port` (0: a port the system picks). Returns the bound
    // port, or 0 when the port is taken.
    quint16 hold(quint16 port)
    {
        const int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd < 0) {
            return 0;
        }
        sockaddr_in sin {};
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = htonl(INADDR_ANY);
        sin.sin_port = htons(port);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&sin), sizeof(sin)) != 0) {
            ::close(fd);
            return 0;
        }
        socklen_t len = sizeof(sin);
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&sin), &len);
        m_fds.push_back(fd);
        m_ports.insert(ntohs(sin.sin_port));
        return ntohs(sin.sin_port);
    }

    bool holds(quint16 port) const { return m_ports.count(port) != 0; }
    int count() const { return int(m_fds.size()); }

    void release()
    {
        for (const int fd : m_fds) {
            ::close(fd);
        }
        m_fds.clear();
        m_ports.clear();
    }

private:
    std::vector<int> m_fds;
    std::set<quint16> m_ports;
};

// True when nothing holds `port` for IPv4 or IPv6 right now.
bool portIsFree(quint16 port)
{
    Ipv4Holds probe;
    const int fd = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        // No IPv6 on this host: libjuice binds IPv4 only, and only IPv4
        // can hold the port.
        return errno == EAFNOSUPPORT && probe.hold(port) == port;
    }
    const int off = 0;
    ::setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &off, sizeof(off));
    sockaddr_in6 sin6 {};
    sin6.sin6_family = AF_INET6;
    sin6.sin6_addr = in6addr_any;
    sin6.sin6_port = htons(port);
    const bool v6Free = ::bind(fd, reinterpret_cast<sockaddr*>(&sin6), sizeof(sin6)) == 0;
    ::close(fd);
    return v6Free && probe.hold(port) == port;
}

// True when a datagram sent to 127.0.0.1:`port` arrives at `sock`.
bool receivesIpv4(int sock, quint16 port)
{
    const int sender = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sender < 0) {
        return false;
    }
    sockaddr_in to {};
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    to.sin_port = htons(port);
    const char payload[] = "nereus";
    ::sendto(sender, payload, sizeof(payload), 0, reinterpret_cast<sockaddr*>(&to), sizeof(to));
    ::close(sender);

    pollfd pfd {};
    pfd.fd = sock;
    pfd.events = POLLIN;
    if (::poll(&pfd, 1, kDatagramWaitMs) != 1) {
        return false;
    }
    char buffer[64];
    return ::recv(sock, buffer, sizeof(buffer), 0) == ssize_t(sizeof(payload));
}

void raiseOpenFileLimit(rlim_t wanted)
{
    rlimit limit {};
    if (::getrlimit(RLIMIT_NOFILE, &limit) != 0 || limit.rlim_cur >= wanted) {
        return;
    }
    limit.rlim_cur = limit.rlim_max == RLIM_INFINITY ? wanted : std::min(wanted, limit.rlim_max);
    ::setrlimit(RLIMIT_NOFILE, &limit);
}

#endif

} // namespace

class TstJuiceDualStackPort : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
#if !defined(Q_OS_MAC) && !defined(Q_OS_LINUX)
        QSKIP("POSIX sockets only; the libjuice change is compiled on macOS.");
#endif
    }

#if defined(Q_OS_MAC) || defined(Q_OS_LINUX)
    // Every socket libjuice binds to a port the system picks gets the IPv4
    // datagrams sent to that port, with many ports held for IPv4 elsewhere.
    void systemPickedPortGetsItsIpv4Datagrams()
    {
        raiseOpenFileLimit(kHeldIpv4Ports + 256);
        Ipv4Holds holds;
        for (int i = 0; i < kHeldIpv4Ports; ++i) {
            QVERIFY2(holds.hold(0) != 0, "could not hold an IPv4 port");
        }

        int landedOnHeld = 0;
        int missed = 0;
        for (int i = 0; i < kPickedSockets; ++i) {
            udp_socket_config_t config {};
            const socket_t sock = udp_create_socket(&config);
            QVERIFY(sock != INVALID_SOCKET);
            const quint16 port = udp_get_port(sock);
            if (holds.holds(port)) {
                ++landedOnHeld;
            }
            if (!receivesIpv4(sock, port)) {
                ++missed;
            }
            closesocket(sock);
        }
        QCOMPARE(landedOnHeld, 0);
        QCOMPARE(missed, 0);
    }

    // A data channel opens over IPv4 when all but two ports of its range
    // are held for IPv4 by another socket.
    void dataChannelOpensBesideIpv4Holders()
    {
        Ipv4Holds holds;
        std::optional<quint16> begin;
        const quint16 start = quint16(20000 + (QCoreApplication::applicationPid() % 400) * 50);
        for (quint16 base = start; base < 45000 && !begin; base += kRangeSize) {
            holds.release();
            bool ok = true;
            for (int i = kFreeInRange; i < kRangeSize && ok; ++i) {
                ok = holds.hold(quint16(base + i)) == quint16(base + i);
            }
            for (int i = 0; i < kFreeInRange && ok; ++i) {
                ok = portIsFree(quint16(base + i));
            }
            if (ok) {
                begin = base;
            }
        }
        QVERIFY2(begin.has_value(), "no port range with two free ports found");
        QCOMPARE(holds.count(), kRangeSize - kFreeInRange);

        rtc::Configuration config;
        config.portRangeBegin = *begin;
        config.portRangeEnd = quint16(*begin + kRangeSize - 1);

        // What the callbacks write lives as long as the last callback that
        // holds it, so a check that returns early can't leave a late callback
        // writing to a destroyed local.
        struct Shared {
            std::mutex mutex;
            std::optional<rtc::Description> offer;
            std::vector<rtc::Candidate> offerCandidates;
            std::atomic<bool> offererGathered {false};
            std::atomic<bool> open {false};
        };
        const auto shared = std::make_shared<Shared>();

        auto offerer = std::make_shared<rtc::PeerConnection>(config);
        auto answerer = std::make_shared<rtc::PeerConnection>(config);
        const std::weak_ptr<rtc::PeerConnection> weakOfferer = offerer;
        const auto closeBoth = qScopeGuard([&offerer, &answerer]() {
            offerer->close();
            answerer->close();
        });

        // The offer goes over only once the offerer has its port, so the
        // two agents never pick from the range at the same time.
        offerer->onLocalDescription([shared](rtc::Description d) {
            std::lock_guard lock(shared->mutex);
            shared->offer = d;
        });
        offerer->onLocalCandidate([shared](rtc::Candidate c) {
            std::lock_guard lock(shared->mutex);
            shared->offerCandidates.push_back(c);
        });
        offerer->onGatheringStateChange([shared](rtc::PeerConnection::GatheringState s) {
            if (s == rtc::PeerConnection::GatheringState::Complete) {
                shared->offererGathered = true;
            }
        });
        // IPv4 candidates only: the case is IPv4 to a dual-stack socket.
        answerer->onLocalDescription([weakOfferer](rtc::Description d) {
            d.extractCandidates();
            if (auto p = weakOfferer.lock()) {
                p->setRemoteDescription(d);
            }
        });
        answerer->onLocalCandidate([weakOfferer](rtc::Candidate c) {
            if (c.family() != rtc::Candidate::Family::Ipv4) {
                return;
            }
            if (auto p = weakOfferer.lock()) {
                p->addRemoteCandidate(c);
            }
        });
        answerer->onDataChannel([shared](std::shared_ptr<rtc::DataChannel>) { shared->open = true; });

        auto channel = offerer->createDataChannel("dual-stack");
        QTRY_VERIFY_WITH_TIMEOUT(shared->offererGathered.load(), kOpenTimeoutMs);
        {
            std::lock_guard lock(shared->mutex);
            QVERIFY(shared->offer.has_value());
            shared->offer->extractCandidates();
            answerer->setRemoteDescription(*shared->offer);
            int ipv4 = 0;
            for (const rtc::Candidate& c : shared->offerCandidates) {
                if (c.family() == rtc::Candidate::Family::Ipv4) {
                    answerer->addRemoteCandidate(c);
                    ++ipv4;
                }
            }
            QVERIFY2(ipv4 > 0, "the offerer gathered no IPv4 candidate");
        }
        QTRY_VERIFY_WITH_TIMEOUT(shared->open.load(), kOpenTimeoutMs);

        channel->close();
    }
#endif
};

QTEST_GUILESS_MAIN(TstJuiceDualStackPort)
#include "tst_juice_dual_stack_port.moc"
