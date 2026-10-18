// tests/tst_p1_loopback_connection.cpp
// no-port-check: NereusSDR-original test; IoBoardHl2.h is NereusSDR's own header.
//
// Phase 3I Task 9 — end-to-end integration test for P1RadioConnection.
// Uses P1FakeRadio (loopback UDP) to verify:
//   1. State transitions: Disconnected → Connecting → Connected
//   2. iqDataReceived emits correctly-shaped interleaved I/Q QVector<float>
//   3. disconnect() stops the fake's "running" state
//   4. the radio's frames reach this connection's socket, and no other
//      socket on the machine bound to the address the radio answers can
//      take them (tst_slice_cap_every_path's lost reconnect)
//   5. onReadyRead reads a bounded batch per call, so queued control calls
//      (an unkey) get a turn while EP6 arrives faster than it is read
//      (2026-10-14 HL2 TUNE stall)
//   6. EP2 goes out from its own sender thread: a blocked connection thread
//      or an EP6 flood while keyed leaves no EP2 gap over 40 ms; the thread
//      sends the bytes the synchronous seam builds; fifty connect, restart,
//      reconnect and disconnect rounds never run two senders
//
// Uses QTEST_MAIN (not APPLESS_MAIN) — requires QCoreApplication for socket I/O.

#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QUdpSocket>
#include <QElapsedTimer>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <pthread.h>
#ifdef Q_OS_MAC
#include <pthread/qos.h>
#endif
#include "core/P1RadioConnection.h"
#include "core/RadioConnection.h"
#include "core/RadioDiscovery.h"
#include "core/HpsdrModel.h"
#include "core/HermesLiteBandwidthMonitor.h"
#include "core/IoBoardHl2.h"
#include "core/audio/RealtimeAudioPriority.h"
#include "fakes/P1FakeRadio.h"
#include "RealtimeTestLoad.h"

using namespace NereusSDR;
using NereusSDR::Test::P1FakeRadio;

namespace {
// Issue #258 regression — message handler that counts "ep6 stream established"
// log lines emitted during the test.  Installed once per test method (init())
// and uninstalled (cleanup()) so the count is fresh between cases.
static std::atomic<int> g_ep6EstablishedLogCount{0};
static QtMessageHandler g_previousMsgHandler = nullptr;

static void countEp6EstablishedLogs(QtMsgType type,
                                    const QMessageLogContext& ctx,
                                    const QString& msg)
{
    if (msg.contains(QStringLiteral("Connected, ep6 stream established"))) {
        g_ep6EstablishedLogCount.fetch_add(1);
    }
    if (g_previousMsgHandler) {
        g_previousMsgHandler(type, ctx, msg);
    }
}

qint64 steadyNowNs()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// EP6 frames the connection parsed: one iqDataReceived for receiver 0 per
// frame. Since 2026-10-14 frameReceived is emitted once per onReadyRead
// call (P1 status at P2's rate), so it no longer counts frames.
class Ep6FrameCounter {
public:
    explicit Ep6FrameCounter(P1RadioConnection& conn)
    {
        QObject::connect(&conn, &RadioConnection::iqDataReceived, &m_ctx,
                         [this](int rx, const QVector<float>&) {
                             if (rx == 0) { ++m_count; }
                         });
    }
    int count() const { return m_count; }
    void clear() { m_count = 0; }
private:
    QObject m_ctx;
    int m_count{0};
};

// The EP2 frames that reached the fake in [fromNs, toNs], and the largest
// gap between them. The window's edges count as arrivals, so a stall at
// the start or the end of the window shows as a gap.
struct Ep2Span {
    int frames{0};
    qint64 maxGapNs{0};
};

Ep2Span ep2Span(const QList<qint64>& arrivals, qint64 fromNs, qint64 toNs)
{
    Ep2Span span;
    qint64 prev = fromNs;
    for (qint64 t : arrivals) {
        if (t < fromNs || t > toNs) { continue; }
        ++span.frames;
        span.maxGapNs = std::max(span.maxGapNs, t - prev);
        prev = t;
    }
    span.maxGapNs = std::max(span.maxGapNs, toNs - prev);
    return span;
}

// The frames the sender sent in [fromNs, toNs] by its own send stamps
// (recordEp2SendsForTest), the largest gap between them, and the frame that
// ended that gap. The window's edges count as sends, as in ep2Span.
struct SendSpan {
    int frames{0};
    qint64 maxGapNs{0};
    bool gapEndsWindow{false};
    P1RadioConnection::Ep2SendStamp after{};
};

SendSpan sendSpan(const QList<P1RadioConnection::Ep2SendStamp>& stamps, qint64 fromNs,
                  qint64 toNs)
{
    SendSpan span;
    qint64 prev = fromNs;
    for (const auto& st : stamps) {
        if (st.sentNs < fromNs || st.sentNs > toNs) { continue; }
        ++span.frames;
        if (st.sentNs - prev > span.maxGapNs) {
            span.maxGapNs = st.sentNs - prev;
            span.after = st;
            span.gapEndsWindow = false;
        }
        prev = st.sentNs;
    }
    if (toNs - prev > span.maxGapNs) {
        span.maxGapNs = toNs - prev;
        span.gapEndsWindow = true;
    }
    return span;
}

// The calling thread's scheduling, for the soak's report: on macOS its QoS
// class (0x21 user-interactive, 0x19 user-initiated, 0x15 default; Mach's
// thread_info reports base priority 31 for every class, so it says
// nothing here), elsewhere its policy and priority.
QString threadSchedulingNote()
{
#ifdef Q_OS_MAC
    qos_class_t qos = QOS_CLASS_UNSPECIFIED;
    int rel = 0;
    pthread_get_qos_class_np(pthread_self(), &qos, &rel);
    return QString("QoS class 0x%1").arg(int(qos), 0, 16);
#else
    int policy = 0;
    sched_param sp{};
    pthread_getschedparam(pthread_self(), &policy, &sp);
    return QString("policy %1, priority %2").arg(policy).arg(sp.sched_priority);
#endif
}

// Where the time of the gap before a sent frame went: how late its pass
// woke, how long it waited for the frame lock, how long sendto took.
QString describeSend(const P1RadioConnection::Ep2SendStamp& st)
{
    return QString("pass woke %1 ms late, lock wait %2 ms (%3 timeouts), sendto %4 ms")
        .arg(st.wakeLateNs / 1e6, 0, 'f', 2).arg(st.lockWaitNs / 1e6, 0, 'f', 2)
        .arg(st.lockTimeouts).arg(st.sendNs / 1e6, 0, 'f', 2);
}

// A transmit pump: feeds the connection 48 kHz TX I/Q from a thread of its
// own, as TxWorkerThread does, in 126-sample blocks at the wire rate.
class TxIqPump {
public:
    explicit TxIqPump(P1RadioConnection& conn) : m_conn(conn) {}
    ~TxIqPump() { stop(); }

    void start()
    {
        m_run.store(true);
        m_thread = std::thread([this] {
            constexpr int kBlock = 126;
            std::array<float, kBlock * 2> iq{};
            for (int i = 0; i < kBlock; ++i) {
                iq[size_t(i) * 2] = 0.25f;  // a quarter-scale carrier
            }
            const qint64 startNs = steadyNowNs();
            qint64 pushed = 0;
            while (m_run.load()) {
                const qint64 due = (steadyNowNs() - startNs) * 48 / 1000000;
                while (pushed + kBlock <= due) {
                    m_conn.sendTxIq(iq.data(), kBlock);
                    pushed += kBlock;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
    }

    void stop()
    {
        m_run.store(false);
        if (m_thread.joinable()) { m_thread.join(); }
    }

private:
    P1RadioConnection& m_conn;
    std::atomic<bool> m_run{false};
    std::thread m_thread;
};

// Posts work to the connection's thread from another thread, as RadioModel
// does (QMetaObject::invokeMethod onto the connection).
template <typename F>
void postFromAnotherThread(P1RadioConnection& conn, F work)
{
    std::thread poster([&conn, work] {
        QMetaObject::invokeMethod(&conn, work);
    });
    poster.join();
}

// C0 bit 0 is MOX in every bank's C0, in both subframes.
bool ep2CcKeyed(const QByteArray& cc)
{
    return cc.size() >= 6 && (quint8(cc[0]) & 0x01) != 0 && (quint8(cc[5]) & 0x01) != 0;
}
} // namespace

// ---------------------------------------------------------------------------
// EP6 flood check, reusable by any change to the P1 receive path.
//
// Ep6Flood builds `count` ep6 datagrams numbered in sequence from
// `firstSeq`, each sample of each receiver carrying its own 24-bit I and Q
// (frame, sample and receiver are all encoded in the value, Q negative), and
// records every frame and every iqDataReceived the connection emits. send()
// writes them straight to the connection's port in waves of `waveSize`; a
// wave equal to `count` is one burst. verify() then checks the receive path
// end to end:
//   - every frame is processed, with none dropped (frameReceived count, and
//     no new EP6 sequence gap on the bandwidth monitor);
//   - they arrive in sequence order (emission k matches frame k);
//   - the I/Q delivered on iqDataReceived is sample for sample what was sent.
// ---------------------------------------------------------------------------
class Ep6Flood {
public:
    Ep6Flood(P1RadioConnection& conn, int count, quint32 firstSeq, int numRx)
        : m_conn(conn), m_count(count), m_firstSeq(firstSeq), m_numRx(numRx)
    {
        // 2026-10-14: frames are counted on iqDataReceived for receiver 0;
        // frameReceived is now once per onReadyRead call.
        QObject::connect(&conn, &RadioConnection::iqDataReceived, &m_ctx,
                         [this](int rx, const QVector<float>& samples) {
                             if (rx == 0) { ++m_framesSeen; }
                             m_iq.append(qMakePair(rx, samples));
                         });
    }

    static int samplesPerSubframe(int numRx) { return 504 / (6 * numRx + 2); }

    // The 24-bit value a sample carries: frame, sample and receiver in its
    // bits, so a dropped, repeated or swapped frame or sample shows.
    static qint32 iValue(int frame, int sample, int rx) {
        return (qint32(frame) << 11) | (qint32(sample) << 3) | qint32(rx);
    }
    static qint32 qValue(int frame, int sample, int rx) {
        return -(iValue(frame, sample, rx) + 1);
    }

    QByteArray frame(int f) const {
        QByteArray d(1032, '\0');
        auto* b = reinterpret_cast<quint8*>(d.data());
        const quint32 seq = m_firstSeq + quint32(f);
        b[0] = 0xEF; b[1] = 0xFE; b[2] = 0x01; b[3] = 0x06;
        b[4] = quint8(seq >> 24); b[5] = quint8(seq >> 16);
        b[6] = quint8(seq >> 8);  b[7] = quint8(seq);
        b[8] = b[9] = b[10] = 0x7F;
        b[520] = b[521] = b[522] = 0x7F;
        const int slot = 6 * m_numRx + 2;
        const int spr = samplesPerSubframe(m_numRx);
        auto put24 = [b](int off, qint32 v) {
            const quint32 u = quint32(v) & 0xFFFFFFu;
            b[off] = quint8(u >> 16); b[off + 1] = quint8(u >> 8); b[off + 2] = quint8(u);
        };
        for (int sub = 0; sub < 2; ++sub) {
            const int base = sub == 0 ? 16 : 528;
            for (int s = 0; s < spr; ++s) {
                for (int r = 0; r < m_numRx; ++r) {
                    const int off = base + s * slot + r * 6;
                    const int sample = sub * spr + s;
                    put24(off, iValue(f, sample, r));
                    put24(off + 3, qValue(f, sample, r));
                }
            }
        }
        return d;
    }

    // Sends every frame to `port` on the loopback, `waveSize` at a time,
    // waiting after each wave until the connection has processed it.
    bool send(quint16 port, int waveSize) {
        QUdpSocket out;
        if (!out.bind(QHostAddress::LocalHost, 0)) { return false; }
        int sent = 0;
        while (sent < m_count) {
            const int n = std::min(waveSize, m_count - sent);
            for (int i = 0; i < n; ++i) {
                if (out.writeDatagram(frame(sent + i), QHostAddress::LocalHost, port) != 1032) {
                    return false;
                }
            }
            sent += n;
            if (sent < m_count && !waitProcessed(sent, 5000)) { return false; }
        }
        return true;
    }

    bool waitProcessed(int frames, int timeoutMs) {
        QElapsedTimer t;
        t.start();
        while (m_framesSeen < frames && t.elapsed() < timeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        return m_framesSeen >= frames;
    }

    int framesSeen() const { return m_framesSeen; }

    // Empty when the flood came through whole; otherwise the first fault.
    QString verify() const {
        if (m_framesSeen != m_count) {
            return QString("%1 of %2 frames processed").arg(m_framesSeen).arg(m_count);
        }
        if (m_iq.size() != m_count * m_numRx) {
            return QString("%1 I/Q emissions for %2 frames of %3 receivers")
                .arg(m_iq.size()).arg(m_count).arg(m_numRx);
        }
        const int perFrame = 2 * samplesPerSubframe(m_numRx);
        for (int f = 0; f < m_count; ++f) {
            for (int r = 0; r < m_numRx; ++r) {
                const auto& [rx, v] = m_iq.at(f * m_numRx + r);
                if (rx != r) {
                    return QString("frame %1: emission for rx %2, expected rx %3").arg(f).arg(rx).arg(r);
                }
                if (v.size() != 2 * perFrame) {
                    return QString("frame %1 rx %2: %3 floats, expected %4")
                        .arg(f).arg(r).arg(v.size()).arg(2 * perFrame);
                }
                for (int s = 0; s < perFrame; ++s) {
                    const float ei = float(iValue(f, s, r)) / 8388608.0f;
                    const float eq = float(qValue(f, s, r)) / 8388608.0f;
                    if (v[2 * s] != ei || v[2 * s + 1] != eq) {
                        return QString("frame %1 rx %2 sample %3: got (%4, %5), sent (%6, %7)")
                            .arg(f).arg(r).arg(s).arg(v[2 * s]).arg(v[2 * s + 1]).arg(ei).arg(eq);
                    }
                }
            }
        }
        return {};
    }

private:
    P1RadioConnection& m_conn;
    int m_count;
    quint32 m_firstSeq;
    int m_numRx;
    QObject m_ctx;
    int m_framesSeen{0};
    QList<QPair<int, QVector<float>>> m_iq;
};

class TestP1LoopbackConnection : public QObject {
    Q_OBJECT

private:
    // Every address: dual-stack Any, or IPv4 Any when Qt keeps the IPv4
    // socket a failed bind to an IPv4 address left behind.
    static bool listensEverywhere(const QHostAddress& a) {
        return a == QHostAddress(QHostAddress::Any)
            || a == QHostAddress(QHostAddress::AnyIPv4);
    }

    // The receive buffer the kernel grants a socket asking what
    // P1RadioConnection asks (0x400000); the kernel may cap it.
    static int expectedReceiveBuffer() {
        QUdpSocket probe;
        if (!probe.bind(QHostAddress::Any, 0)) { return -2; }
        probe.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                              QVariant(0x400000));
        return probe.socketOption(QAbstractSocket::ReceiveBufferSizeSocketOption).toInt();
    }

    RadioInfo makeInfo(P1FakeRadio& fake) const {
        RadioInfo info;
        info.address         = fake.localAddress();
        info.port            = fake.localPort();
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
        info.firmwareVersion = 72;
        info.name            = QStringLiteral("FakeHL2");
        return info;
    }

    // Connects to the fake and keys: MOX posted from another thread, and the
    // fake seeing C0 MOX on the wire. Returns false on a timeout.
    bool connectAndKey(P1FakeRadio& fake, P1RadioConnection& conn) {
        conn.connectToRadio(makeInfo(fake));
        if (!QTest::qWaitFor([&] { return fake.isRunning(); }, 3000)) { return false; }
        fake.sendEp6Frames(1);
        if (!QTest::qWaitFor([&] { return conn.state() == ConnectionState::Connected; }, 3000)) {
            return false;
        }
        postFromAnotherThread(conn, [&conn] {
            conn.setMox(true);
            conn.setTrxRelay(true);
        });
        return QTest::qWaitFor([&] {
            const QList<QByteArray> cc = fake.ep2CcReceived();
            return !cc.isEmpty() && ep2CcKeyed(cc.last());
        }, 3000);
    }

private slots:
    // The flood, unkey and soak cases measure the EP2 sender against the
    // wall clock (REALTIME in tests/CMakeLists.txt): print the load
    // average when a case fails.
    void cleanup() { RealtimeTestLoad::printLoadAverageIfFailed(); }

    // Test 1: Full RX path end-to-end
    void rxPathEndToEnd() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();

        QSignalSpy stateSpy(&conn, &RadioConnection::connectionStateChanged);
        QSignalSpy dataSpy(&conn,  &RadioConnection::iqDataReceived);

        conn.connectToRadio(makeInfo(fake));

        // Wait for Connected state (timeout 3s)
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);

        // Wait for the fake to process the metis-start datagram.
        // Both sockets are in the same thread — event loop must tick for the
        // fake's readyRead to fire after connectToRadio() sends the start packet.
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        // Fake sends 10 ep6 frames
        fake.sendEp6Frames(10);

        // Wait for first iqDataReceived emission
        QTRY_VERIFY_WITH_TIMEOUT(dataSpy.count() >= 1, 3000);

        // Verify sample format — hwReceiverIndex=0, interleaved I/Q floats.
        // Use first() rather than last(): the fake streams 10 ep6 frames and
        // dataSpy may have captured additional emissions between QTRY_VERIFY
        // returning and this line running (e.g. rx=1 from a subsequent frame
        // if the connection ever fans out to multiple DDCs). The invariant
        // the test checks is "the first iqDataReceived emission is rx=0 with
        // the I=0.5 / Q=0 fake payload," which first() encodes unambiguously.
        auto args    = dataSpy.first();
        int  rxIdx   = args.at(0).toInt();
        QCOMPARE(rxIdx, 0);

        auto samples = args.at(1).value<QVector<float>>();
        QVERIFY(samples.size() > 0);
        QVERIFY((samples.size() % 2) == 0);  // interleaved I/Q pairs

        // The fake emits I=0.5, Q=0.0. Verify at least one I sample is ~0.5.
        // samples[0] = I0, samples[1] = Q0
        QVERIFY(qAbs(samples[0] - 0.5f) < 0.001f);
        QVERIFY(qAbs(samples[1] - 0.0f) < 0.001f);

        conn.disconnect();
        fake.stop();
    }

    // R-R3-21: Diagnostics > Connection Quality's "EP6 sequence gaps".
    // Each ep6 frame whose sequence number is not one past the last counts
    // one error, as P1RadioConnection::onReadyRead documents.
    void ep6SequenceGapsAreCounted() {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        HermesLiteBandwidthMonitor bw;
        P1RadioConnection conn;
        conn.init();
        conn.setBandwidthMonitor(&bw);
        Ep6FrameCounter frames(conn);

        conn.connectToRadio(makeInfo(fake));
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        fake.sendEp6Frames(5);
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 5, 3000);
        const int before = bw.ep6SequenceErrorCount();

        // In-order frames add nothing.
        fake.sendEp6Frames(3);
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 8, 3000);
        QCOMPARE(bw.ep6SequenceErrorCount(), before);

        // A jump in the sequence counts one gap.
        fake.skipEp6Sequence(4);
        fake.sendEp6Frames(2);
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 10, 3000);
        QCOMPARE(bw.ep6SequenceErrorCount(), before + 1);

        conn.disconnect();
        fake.stop();
    }

    // (Removed 2026-04-13) firmwareBelowMinimumRefusesConnect — the firmware
    // refusal path was removed from P1RadioConnection because Thetis enforces
    // no equivalent floor for the boards the previous test covered. See
    // BoardCapabilities.cpp file-header comment for the audit details.

    // Test 3: disconnect() stops the radio stream
    void disconnectStopsData() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();

        conn.connectToRadio(makeInfo(fake));
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        // Wait for the fake to register the metis-start before issuing
        // metis-stop — otherwise the stop can race ahead of the start in
        // the fake's event loop and m_running ends up true.
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        conn.disconnect();
        QCOMPARE(conn.state(), ConnectionState::Disconnected);

        // The fake should have received a metis-stop and cleared m_running.
        // Use QTRY_VERIFY because the stop is processed asynchronously by
        // the fake's readyRead slot one event-loop turn after disconnect().
        QTRY_VERIFY_WITH_TIMEOUT(!fake.isRunning(), 1000);

        fake.stop();
    }

    // tst_slice_cap_every_path once lost a reconnect: the fake streamed, and
    // P1RadioConnection's 2 s connect watchdog still saw no ep6. The
    // connection bound its socket to every address (Any) on a port the OS
    // chose. On macOS the OS can hand such a socket a port that another
    // socket already holds on one address (127.0.0.1, which every test fake
    // and some of the app's own helpers bind), and a datagram to that
    // address and port goes to the socket bound to the address, not to the
    // connection. Another program took the radio's frames. The connection
    // now binds the address itself, as Thetis does (the cites are at
    // P1RadioConnection::bindToRadioFacingAddress), where no other socket
    // can share the port.
    //
    // The OS will not hand the connection a chosen port, so this test binds
    // the other socket second, the same sharing in the other order: asked
    // with ShareAddress (SO_REUSEADDR), macOS lets it share a port bound to
    // Any, and refuses it a port already bound to the same address.
    // The collision reproduces only on macOS: a pass on Linux or Windows
    // does not cover the fix.
    void anotherSocketOnTheRadiosAddressCannotTakeItsFrames() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        Ep6FrameCounter frames(conn);

        conn.connectToRadio(makeInfo(fake));
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(fake.clientPort() != 0, 3000);
        const quint16 hostPort = fake.clientPort();

        QUdpSocket other;
        const bool otherBound =
            other.bind(fake.localAddress(), hostPort, QAbstractSocket::ShareAddress);

        frames.clear();
        QTRY_VERIFY_WITH_TIMEOUT(frames.count() >= 5, 2000);
        QVERIFY2(!otherBound || !other.hasPendingDatagrams(),
                 "the radio's frames went to another socket");

        conn.disconnect();
        fake.stop();
    }

    // With no route to the radio, connectToRadio warns and listens on every
    // address. disconnect() closes the socket, so on a reconnect the
    // fallback must bind it again, with the buffer sizes: before, it left
    // the socket closed and Qt bound it on the first send without them.
    void noRouteOnAReconnectBindsEveryAddressWithTheBuffers() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(makeInfo(fake));
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        conn.disconnect();
        QTRY_VERIFY_WITH_TIMEOUT(!fake.isRunning(), 1000);

        conn.setRadioFacingAddressForTest(QHostAddress());
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("^P1: no local address reaches")));
        conn.connectToRadio(makeInfo(fake));

        QCOMPARE(conn.socketStateForTest(), QAbstractSocket::BoundState);
        QVERIFY(listensEverywhere(conn.socketAddressForTest()));
        QCOMPARE(conn.socketReceiveBufferForTest(), expectedReceiveBuffer());
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);

        conn.disconnect();
        fake.stop();
    }

    // An address this host cannot bind (TEST-NET-1, RFC 5737) takes the
    // same fallback, with its own warning.
    void anUnbindableAddressBindsEveryAddressWithTheBuffers() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.setRadioFacingAddressForTest(QHostAddress(QStringLiteral("192.0.2.1")));
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("^P1: could not listen on")));
        conn.connectToRadio(makeInfo(fake));

        QCOMPARE(conn.socketStateForTest(), QAbstractSocket::BoundState);
        QVERIFY(listensEverywhere(conn.socketAddressForTest()));
        QCOMPARE(conn.socketReceiveBufferForTest(), expectedReceiveBuffer());
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);

        conn.disconnect();
        fake.stop();
    }

    // The log line gives the buffer in the terms it was asked and says
    // whether the system gave all of it, as P2's does (tst_p2_loopback_bind).
    void theReceiveBufferIsLoggedInTheTermsItWasAsked() {
        constexpr int asked = RadioConnection::kRadioSocketReceiveBufferBytes;
        const int granted =
            RadioConnection::grantedReceiveBufferBytes(expectedReceiveBuffer());
        QVERIFY2(granted > 0 && granted <= asked, qPrintable(QString::number(granted)));
        const QString line = granted >= asked
            ? QStringLiteral("^P1: radio socket receive buffer %1 bytes, all that was asked for$")
                  .arg(granted)
            : QStringLiteral("^P1: radio socket receive buffer %1 bytes of the %2 asked for")
                  .arg(granted).arg(asked);
        QTest::ignoreMessage(QtInfoMsg, QRegularExpression(line));
        P1RadioConnection conn;
        conn.init();
        QCOMPARE(RadioConnection::grantedReceiveBufferBytes(
                     conn.socketReceiveBufferForTest()), granted);
    }

    // Issue #258 regression — when a burst of N 1032-byte ep6 datagrams
    // lands in a single readyRead batch while state is still Connecting,
    // the "P1: Connected, ep6 stream established" log line must fire
    // EXACTLY ONCE, not once per datagram.  Field repro: first connect
    // emitted 181 spurious lines in <10 ms because cs was a const local
    // snapshot read outside the drain loop.
    void firstConnectLogFiresExactlyOncePerBatch() {
        P1FakeRadio fake;
        // Disable the 10 ms auto-stream timer so we have exclusive control
        // over when ep6 frames arrive.  Otherwise the first auto-stream
        // tick would promote state to Connected before our burst lands,
        // masking the bug (state==Connected -> log branch not taken).
        fake.setAutoStreamEnabled(false);
        fake.start();

        // Install the counting message handler AFTER fake.start() so the
        // fake's own diagnostics aren't counted (they don't match the
        // substring, but belt-and-braces).
        g_ep6EstablishedLogCount.store(0);
        g_previousMsgHandler = qInstallMessageHandler(countEp6EstablishedLogs);

        P1RadioConnection conn;
        conn.init();

        RadioInfo info;
        info.address         = fake.localAddress();
        info.port            = fake.localPort();
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
        info.firmwareVersion = 72;
        info.name            = QStringLiteral("FakeHL2");
        conn.connectToRadio(info);

        // Wait for the fake to receive metis-start (m_running flips true)
        // — that means conn is now in Connecting waiting for first ep6.
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);
        QCOMPARE(conn.state(), ConnectionState::Connecting);

        // Burst-send 100 ep6 frames in a tight loop.  All 100 land in
        // the OS UDP receive queue before conn's event loop ticks; when
        // onReadyRead fires it drains all 100 in one while-loop pass.
        fake.sendEp6Frames(100);

        // Pump the event loop until the conn transitions to Connected
        // (i.e. at least the first of the 100 frames has been drained
        // through onReadyRead).
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);

        // Bug A: pre-fix this was 100 (once per datagram in the batch).
        // Post-fix: 1.
        QCOMPARE(g_ep6EstablishedLogCount.load(), 1);

        qInstallMessageHandler(g_previousMsgHandler);
        g_previousMsgHandler = nullptr;

        conn.disconnect();
        fake.stop();
    }

    // Verifies EP2 (host→radio) cadence matches the spec's 48 kHz / 126
    // samples-per-packet rate of ~381 pps. Before the pacer fix the rate
    // was ~40 pps (once per 25 ms watchdog tick), starving the radio's
    // audio DAC. See issue #38 pcap analysis.
    void ep2PaceRateMatchesAudioClock() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(makeInfo(fake));
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);

        // Baseline count after connection is up — discard discovery/start framing.
        const int baseline = fake.ep2FramesReceived();

        // Sample for 500 ms.
        QTest::qWait(500);

        const int delta = fake.ep2FramesReceived() - baseline;
        // At 380.95 pps ideal we expect ~190 packets in 500 ms. Windows QTimer
        // jitter + catch-up loop settle the observed rate around 300-400 pps,
        // so assert a floor of 100 packets (200 pps) which is still 5x the
        // broken 40 pps watchdog cadence.
        QVERIFY2(delta >= 100,
            qPrintable(QString("EP2 rate too slow: %1 packets in 500 ms (=%2 pps)")
                .arg(delta).arg(delta * 2)));

        conn.disconnect();
        fake.stop();
    }

    // 2026-10-14, Pi 4 Core, HL2 keyed: the connection thread read EP6
    // slower than the HL2 sent it, onReadyRead's loop never emptied the
    // socket, and the EP2 pacer on the same thread sent nothing for 12.9 s.
    // One call now reads at most kMaxEp6DatagramsPerRead datagrams and
    // leaves the rest, which are read without any new datagram arriving.
    void readLoopIsBoundedPerCall() {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        P1RadioConnection conn;
        conn.init();
        Ep6FrameCounter frames(conn);
        QSignalSpy reads(&conn, &RadioConnection::frameReceived);

        conn.connectToRadio(makeInfo(fake));
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);
        fake.sendEp6Frames(1);
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), 1, 3000);
        frames.clear();
        reads.clear();

        // Four batches' worth, queued on the socket before this thread's
        // event loop runs again (sendEp6Frames returns once they are
        // written; loopback delivers them at once).
        const int cap = P1RadioConnection::maxEp6DatagramsPerReadForTest();
        const int sent = cap * 4;
        fake.sendEp6Frames(sent);

        conn.onReadyReadForTest();
        QCOMPARE(frames.count(), cap);
        // One activity tick for the whole read (P1 status at P2's rate).
        QCOMPARE(reads.count(), 1);
        QVERIFY2(conn.socketHasPendingDatagramsForTest(),
                 "one onReadyRead call read every pending datagram");

        // No new arrivals: the read notifier brings the rest in.
        QTRY_COMPARE_WITH_TIMEOUT(frames.count(), sent, 3000);
        QVERIFY(!conn.socketHasPendingDatagramsForTest());

        conn.disconnect();
        fake.stop();
    }

    // A flood well past the bound, in one burst the receive buffer holds
    // without the kernel dropping any: 640 datagrams (40 batches) where the
    // socket gets the 4 MB it asks for (the Mac, the Pi 4 Core), fewer where
    // the system caps it (Linux at its default limit grants 425984 bytes).
    // One onReadyRead call leaves datagrams pending, and the receive path is
    // otherwise unchanged: every frame processed, in sequence order, with the
    // I/Q delivered sample for sample as sent.
    void ep6FloodIsReceivedWhole() {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        HermesLiteBandwidthMonitor bw;
        P1RadioConnection conn;
        conn.init();
        conn.setBandwidthMonitor(&bw);

        conn.connectToRadio(makeInfo(fake));
        QTRY_VERIFY_WITH_TIMEOUT(fake.isRunning(), 3000);
        // The fake's one frame carries sequence 0 and connects.
        QSignalSpy first(&conn, &RadioConnection::frameReceived);
        fake.sendEp6Frames(1);
        QTRY_COMPARE_WITH_TIMEOUT(conn.state(), ConnectionState::Connected, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(first.count(), 1, 3000);
        const int gapsBefore = bw.ep6SequenceErrorCount();

        constexpr int kMaxFlood = 640;
        // Buffer charged per queued datagram: its 1032 bytes plus the
        // kernel's own overhead, with room to spare.
        constexpr int kBufferBytesPerDatagram = 4096;
        const int receiveBuffer = conn.socketReceiveBufferForTest();
        const int floodSize = std::min(kMaxFlood, receiveBuffer / kBufferBytesPerDatagram);
        // Still well past the bound: four batches at the least.
        const int minFlood = 4 * P1RadioConnection::maxEp6DatagramsPerReadForTest();
        qInfo("receive buffer %d bytes, flood of %d datagrams", receiveBuffer, floodSize);
        if (floodSize < minFlood) {
            conn.disconnect();
            fake.stop();
            QSKIP(qPrintable(QString("receive buffer %1 too small for a burst of %2 datagrams")
                                 .arg(receiveBuffer).arg(minFlood)));
        }

        Ep6Flood flood(conn, floodSize, 1, conn.activeRxCountForTest());
        QVERIFY(flood.send(fake.clientPort(), floodSize));

        // Bound: one call reads one batch and leaves the rest pending.
        conn.onReadyReadForTest();
        QCOMPARE(flood.framesSeen(), P1RadioConnection::maxEp6DatagramsPerReadForTest());
        QVERIFY(conn.socketHasPendingDatagramsForTest());

        // Receive unchanged.
        QVERIFY(flood.waitProcessed(floodSize, 5000));
        const QString fault = flood.verify();
        QVERIFY2(fault.isEmpty(), qPrintable(fault));
        QCOMPARE(bw.ep6SequenceErrorCount(), gapsBefore);
        QVERIFY(!conn.socketHasPendingDatagramsForTest());

        conn.disconnect();
        fake.stop();
    }

    // 2026-10-14, Pi 4 Core, HL2 keyed: EP2 stopped for 12.9 s while the
    // connection thread was busy, and the HL2 dropped PTT each time its TX
    // FIFO ran dry. EP2 now goes out from its own thread, so the connection
    // thread blocked for 500 ms, keyed with TX I/Q flowing, leaves no gap
    // over 40 ms and the count stays within 10% of the 2625 us cadence.
    // The block runs on another object on the connection's thread (a busy
    // thread, as the read loop was), not as a call on the connection.
    void ep2KeepsFlowingWhileTheConnectionThreadIsBlocked() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        QVERIFY(connectAndKey(fake, conn));
        TxIqPump pump(conn);
        pump.start();
        QTest::qWait(200);

        QObject busy;
        std::atomic<qint64> blockStart{0};
        std::atomic<qint64> blockEnd{0};
        QMetaObject::invokeMethod(&busy, [&blockStart, &blockEnd] {
            blockStart.store(steadyNowNs());
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            blockEnd.store(steadyNowNs());
        }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(blockEnd.load() != 0, 3000);
        QTest::qWait(50);
        pump.stop();

        const Ep2Span span = ep2Span(fake.ep2ArrivalNs(), blockStart.load(), blockEnd.load());
        const double blockMs = (blockEnd.load() - blockStart.load()) / 1e6;
        const double expected = blockMs * 1000.0 / 2625.0;
        qInfo().noquote() << QString("MEASURE blocked-thread: block %1 ms, %2 EP2 frames "
                                     "(expected %3), max gap %4 ms")
                                 .arg(blockMs, 0, 'f', 1).arg(span.frames)
                                 .arg(expected, 0, 'f', 1).arg(span.maxGapNs / 1e6, 0, 'f', 2);
        QVERIFY2(span.frames > 0, "no EP2 frame reached the radio while the thread was blocked");
        QVERIFY2(span.maxGapNs <= 40'000'000,
                 qPrintable(QString("EP2 gap of %1 ms").arg(span.maxGapNs / 1e6, 0, 'f', 2)));
        QVERIFY2(std::abs(span.frames - expected) <= 0.10 * expected,
                 qPrintable(QString("%1 EP2 frames in %2 ms, expected %3 within 10%")
                                .arg(span.frames).arg(blockMs, 0, 'f', 1)
                                .arg(expected, 0, 'f', 1)));

        conn.disconnect();
        fake.stop();
    }

    // An EP6 flood well past the read-loop cap while keyed, read by a
    // slow thread (15 ms a frame, so one capped batch of 16 is 240 ms,
    // six times the bound): no EP2 gap over 40 ms, and receive unchanged
    // (every frame, in order, with the I/Q as sent). With EP2 sent from the
    // connection thread between batches (the read-loop cap, 238351694) the
    // gap is a whole batch.
    void ep6FloodWhileKeyedLeavesNoEp2Gap() {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        HermesLiteBandwidthMonitor bw;
        P1RadioConnection conn;
        conn.init();
        conn.setBandwidthMonitor(&bw);
        QVERIFY(connectAndKey(fake, conn));
        conn.recordEp2SendsForTest(1 << 14);
        TxIqPump pump(conn);
        pump.start();
        QTest::qWait(100);
        const int gapsBefore = bw.ep6SequenceErrorCount();

        QObject slowReader;
        // Per frame: iqDataReceived for receiver 0 (frameReceived is once
        // per read since 2026-10-14).
        connect(&conn, &RadioConnection::iqDataReceived, &slowReader,
                [](int rx, const QVector<float>&) {
            if (rx != 0) { return; }
            // A sleep, not a spin, so the reader takes no core from the
            // sender it is testing.
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        });

        // The fake's one frame carried sequence 0; the flood goes on from 1.
        constexpr int kFlood = 64;
        Ep6Flood flood(conn, kFlood, 1, conn.activeRxCountForTest());
        const qint64 floodStart = steadyNowNs();
        QVERIFY(flood.send(fake.clientPort(), kFlood));
        QVERIFY(flood.waitProcessed(kFlood, 10000));
        const qint64 floodEnd = steadyNowNs();
        QObject::disconnect(&conn, &RadioConnection::iqDataReceived, &slowReader, nullptr);
        pump.stop();

        const Ep2Span span = ep2Span(fake.ep2ArrivalNs(), floodStart, floodEnd);
        const SendSpan sent = sendSpan(conn.ep2SendStampsForTest(), floodStart, floodEnd);
        qInfo().noquote() << QString("MEASURE ep6-flood: %1 ms, %2 EP2 frames, max gap %3 ms;"
                                     " sent %4, max send gap %5 ms (%6)")
                                 .arg((floodEnd - floodStart) / 1e6, 0, 'f', 1).arg(span.frames)
                                 .arg(span.maxGapNs / 1e6, 0, 'f', 2).arg(sent.frames)
                                 .arg(sent.maxGapNs / 1e6, 0, 'f', 2)
                                 .arg(sent.gapEndsWindow ? QStringLiteral("at the window's end")
                                                         : describeSend(sent.after));
        // Judged by the sender's own send stamps: the fake's thread reads
        // and stamps at normal priority, and on a loaded machine it was
        // late by up to 136 ms when the sender was not (2026-10-15). The
        // arrival gap stays as a weaker bound, under the 240 ms a capped
        // batch costs this thread, so EP2 sent between batches still fails.
        QVERIFY2(sent.maxGapNs <= 40'000'000,
                 qPrintable(QString("EP2 send gap of %1 ms (%2)")
                                .arg(sent.maxGapNs / 1e6, 0, 'f', 2)
                                .arg(sent.gapEndsWindow ? QStringLiteral("at the window's end")
                                                        : describeSend(sent.after))));
        QVERIFY2(span.maxGapNs <= 200'000'000,
                 qPrintable(QString("EP2 arrival gap of %1 ms").arg(span.maxGapNs / 1e6, 0, 'f', 2)));
        const QString fault = flood.verify();
        QVERIFY2(fault.isEmpty(), qPrintable(fault));
        QCOMPARE(bw.ep6SequenceErrorCount(), gapsBefore);

        conn.disconnect();
        fake.stop();
    }

    // Control latency: EP6 arrives faster than this thread reads it (each
    // frame costs 200 us here, about 5,000 a second, while a feeder sends
    // about twice that). An unkey posted from another thread, as RadioModel
    // posts it, must run on the connection thread within two capped read
    // batches, and then clear C0 MOX on the wire. The setter runs on the
    // connection thread, so this is what the read-loop cap is for: without
    // it onReadyRead never returned and the unkey waited for the feeder to
    // stop.
    //
    // Judged in read batches, not milliseconds (2026-10-15): the wall-clock
    // bound of 50 ms came within 4 ms of failing at load 33 to 40, because
    // a loaded machine stretches every 200 us frame, while the bug it
    // guards keeps the setter waiting for the whole feed, thousands of
    // frames. Two batches: the one being read when the unkey is posted,
    // and one more whose read notification may already be queued ahead of
    // it. The milliseconds are still printed.
    void unkeyReachesTheWireWhileEp6OutrunsTheReader() {
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        P1RadioConnection conn;
        conn.init();
        QSignalSpy frames(&conn, &RadioConnection::frameReceived);
        QVERIFY(connectAndKey(fake, conn));

        // A slow reader: every frame read takes at least 200 us on this
        // thread.
        QObject slowReader;
        std::atomic<int> framesRead{0};
        // Per frame: iqDataReceived for receiver 0 (frameReceived is once
        // per read since 2026-10-14).
        connect(&conn, &RadioConnection::iqDataReceived, &slowReader,
                [&framesRead](int rx, const QVector<float>&) {
            if (rx != 0) { return; }
            framesRead.fetch_add(1);
            // A sleep, not a spin, so the reader takes no core from the
            // sender it is testing.
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        });

        constexpr int kFeedMs = 500;
        std::atomic<qint64> unkeyPostedNs{0};
        std::atomic<int> framesReadAtPost{-1};
        std::atomic<int> framesReadAtUnkey{-1};
        std::atomic<qint64> unkeyRanNs{0};
        std::thread feeder([&fake, &conn, &unkeyPostedNs, &framesRead, &framesReadAtPost,
                            &framesReadAtUnkey, &unkeyRanNs] {
            QElapsedTimer t;
            t.start();
            // Let the reader fall behind before the unkey.
            while (t.elapsed() < 100) {
                fake.sendEp6Frames(20);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            unkeyPostedNs.store(steadyNowNs());
            framesReadAtPost.store(framesRead.load());
            QMetaObject::invokeMethod(&conn, [&conn, &framesRead, &framesReadAtUnkey,
                                              &unkeyRanNs] {
                framesReadAtUnkey.store(framesRead.load());
                unkeyRanNs.store(steadyNowNs());
                conn.setMox(false);
                conn.setTrxRelay(false);
            });
            while (t.elapsed() < 100 + kFeedMs) {
                fake.sendEp6Frames(20);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });

        // Run this thread's event loop while the feeder runs.
        QTest::qWait(100 + kFeedMs + 50);
        feeder.join();
        QObject::disconnect(&conn, &RadioConnection::iqDataReceived, &slowReader, nullptr);

        // The first frame after the post with C0 MOX clear, and every frame
        // after it unkeyed.
        const QList<QByteArray> cc = fake.ep2CcReceived();
        const QList<qint64> at = fake.ep2ArrivalNs();
        QCOMPARE(cc.size(), at.size());
        qint64 clearedAt = -1;
        for (int i = 0; i < cc.size(); ++i) {
            if (at[i] < unkeyPostedNs.load()) { continue; }
            const bool keyed = (quint8(cc[i][0]) & 0x01) != 0 || (quint8(cc[i][5]) & 0x01) != 0;
            if (clearedAt < 0 && !keyed) {
                clearedAt = at[i];
            }
            QVERIFY2(clearedAt < 0 || !keyed, "a frame after the unkey carried MOX again");
        }
        constexpr int kCap = P1RadioConnection::maxEp6DatagramsPerReadForTest();
        const int framesBetween = framesReadAtUnkey.load() - framesReadAtPost.load();
        const double toSetterMs = (unkeyRanNs.load() - unkeyPostedNs.load()) / 1e6;
        const double toWireMs = clearedAt >= 0 ? (clearedAt - unkeyPostedNs.load()) / 1e6 : -1.0;
        QVERIFY2(framesReadAtUnkey.load() >= 0, "the unkey never ran while the feeder ran");
        qInfo().noquote() << QString("MEASURE unkey-latency: %1 ms to the wire; the setter ran"
                                     " %2 ms after the post, %3 EP6 frames read in between"
                                     " (bound %4)")
                                 .arg(toWireMs, 0, 'f', 2).arg(toSetterMs, 0, 'f', 2)
                                 .arg(framesBetween).arg(2 * kCap);
        QVERIFY2(framesBetween <= 2 * kCap,
                 qPrintable(QString("the unkey waited for %1 EP6 frames to be read (bound %2,"
                                    " two capped batches)")
                                .arg(framesBetween).arg(2 * kCap)));
        QVERIFY2(clearedAt >= 0, "C0 MOX never cleared on the wire");

        // The backlog left when the feeder stops is read with no new arrivals.
        QTRY_VERIFY_WITH_TIMEOUT(!conn.socketHasPendingDatagramsForTest(), 5000);

        conn.disconnect();
        fake.stop();
    }

    // A soak, skipped unless NEREUS_EP2_SOAK_S gives its length in seconds:
    // keyed with TX I/Q flowing while this (the connection's) thread reads
    // an EP6 flood it cannot keep up with, as a Pi 4 Core's connection
    // thread is while keyed. Reports the worst EP2 gap by the sender's own
    // send stamps, where each gap's time went, and the wakeup lateness of a
    // control thread raised as the sender is (ep2SenderMain's elevation)
    // and sleeping 2 ms a pass, so a gap the machine imposes on every
    // raised thread shows in both. CPU load on the other cores comes from
    // outside the test. The 32 ms bound is the HL2's: it drops PTT after
    // about that long without EP2.
    void ep2SendGapSoak() {
        const int soakS = qEnvironmentVariableIntValue("NEREUS_EP2_SOAK_S");
        if (soakS <= 0) {
            QSKIP("set NEREUS_EP2_SOAK_S to run the EP2 soak");
        }
        P1FakeRadio fake;
        fake.setAutoStreamEnabled(false);
        fake.start();

        P1RadioConnection conn;
        conn.init();
        QVERIFY(connectAndKey(fake, conn));
        conn.recordEp2SendsForTest(soakS * 400 + 4000);
        TxIqPump pump(conn);
        pump.start();

        // Each EP6 frame costs this thread 200 us; the feeder sends about
        // 10,000 a second, twice what it can read. A spin here, not the
        // sleep the timing cases use: the soak means to keep the
        // connection thread's core busy, as a Pi 4 Core's is while keyed.
        QObject slowReader;
        connect(&conn, &RadioConnection::iqDataReceived, &slowReader,
                [](int rx, const QVector<float>&) {
            if (rx != 0) { return; }
            QElapsedTimer t;
            t.start();
            while (t.nsecsElapsed() < 200000) {
            }
        });

        std::atomic<bool> run{true};
        std::thread feeder([&fake, &run] {
            while (run.load()) {
                fake.sendEp6Frames(20);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        });
        struct Wake { qint64 atNs; qint64 lateNs; };
        std::vector<Wake> wakes;
        wakes.reserve(size_t(soakS) * 600 + 1000);
        QString controlScheduling;
        std::thread control([&run, &wakes, &controlScheduling] {
            // Raised as ep2SenderMain raises the sender: QoS alone on the
            // Mac, the audio thread priority elsewhere.
            AudioPriorityToken* prio = nullptr;
#ifdef Q_OS_MAC
            elevateLatencyCriticalThreadPriority(/*logSuccess=*/false);
#else
            prio = elevateAudioThreadPriority();
#endif
            controlScheduling = threadSchedulingNote();
            while (run.load()) {
                const qint64 from = steadyNowNs();
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                const qint64 now = steadyNowNs();
                wakes.push_back({now, now - from - 2'000'000});
            }
            leaveAudioThreadPriority(prio);
        });

        QTest::qWait(500);  // the control thread's priority settles
        const qint64 fromNs = steadyNowNs();
        QTest::qWait(soakS * 1000);
        const qint64 toNs = steadyNowNs();
        run.store(false);
        feeder.join();
        control.join();
        QObject::disconnect(&conn, &RadioConnection::iqDataReceived, &slowReader, nullptr);
        pump.stop();

        // Every gap over 10 ms by send time, and the worst.
        const QList<P1RadioConnection::Ep2SendStamp> stamps = conn.ep2SendStampsForTest();
        const SendSpan sent = sendSpan(stamps, fromNs, toNs);
        int over10 = 0;
        int over20 = 0;
        int over32 = 0;
        qint64 prev = fromNs;
        QList<QPair<qint64, P1RadioConnection::Ep2SendStamp>> big;
        for (const auto& st : stamps) {
            if (st.sentNs < fromNs || st.sentNs > toNs) { continue; }
            const qint64 gap = st.sentNs - prev;
            over10 += gap > 10'000'000 ? 1 : 0;
            over20 += gap > 20'000'000 ? 1 : 0;
            over32 += gap > 32'000'000 ? 1 : 0;
            if (gap > 10'000'000) { big.append({gap, st}); }
            prev = st.sentNs;
        }
        std::sort(big.begin(), big.end(), [](const auto& a, const auto& b) {
            return a.first > b.first;
        });
        qint64 controlMax = 0;
        qint64 controlMaxAt = 0;
        int controlOver10 = 0;
        int controlWakes = 0;
        for (const Wake& w : wakes) {
            if (w.atNs < fromNs || w.atNs > toNs) { continue; }
            ++controlWakes;
            controlOver10 += w.lateNs > 10'000'000 ? 1 : 0;
            if (w.lateNs > controlMax) {
                controlMax = w.lateNs;
                controlMaxAt = w.atNs;
            }
        }
        const double seconds = (toNs - fromNs) / 1e9;
        qInfo().noquote() << QString("MEASURE soak: %1 s, %2 EP2 frames sent (%3 a second),"
                                     " max send gap %4 ms; gaps over 10/20/32 ms: %5/%6/%7")
                                 .arg(seconds, 0, 'f', 1).arg(sent.frames)
                                 .arg(sent.frames / seconds, 0, 'f', 1)
                                 .arg(sent.maxGapNs / 1e6, 0, 'f', 2)
                                 .arg(over10).arg(over20).arg(over32);
        for (int i = 0; i < std::min<qsizetype>(big.size(), 8); ++i) {
            // The control thread's worst wake in the 50 ms before this send.
            qint64 controlNear = 0;
            for (const Wake& w : wakes) {
                if (w.atNs >= big[i].second.sentNs - 50'000'000
                    && w.atNs <= big[i].second.sentNs + 5'000'000) {
                    controlNear = std::max(controlNear, w.lateNs);
                }
            }
            qInfo().noquote() << QString("MEASURE soak gap %1: %2 ms at +%3 s; %4;"
                                         " control's worst wake nearby %5 ms late")
                                     .arg(i + 1).arg(big[i].first / 1e6, 0, 'f', 2)
                                     .arg((big[i].second.sentNs - fromNs) / 1e9, 0, 'f', 3)
                                     .arg(describeSend(big[i].second))
                                     .arg(controlNear / 1e6, 0, 'f', 2);
        }
        qInfo().noquote() << QString("MEASURE soak control: %1 wakes, worst %2 ms late at +%3 s,"
                                     " %4 over 10 ms late; raised as the sender is: %5")
                                 .arg(controlWakes).arg(controlMax / 1e6, 0, 'f', 2)
                                 .arg((controlMaxAt - fromNs) / 1e9, 0, 'f', 3)
                                 .arg(controlOver10).arg(controlScheduling);
        QVERIFY2(sent.maxGapNs <= 32'000'000,
                 qPrintable(QString("EP2 send gap of %1 ms").arg(sent.maxGapNs / 1e6, 0, 'f', 2)));

        conn.disconnect();
        fake.stop();
    }

    // Test 4: the sender thread builds the same frames as the synchronous
    // seam. Two connections in the same state, one sending from its thread
    // to a local socket, one driven through serviceEp2SendForTest: every
    // frame, all 1032 bytes, alike, then again after the force-bank setters
    // (setMox, setTrxRelay, setMicTipRing), on an HL2 and on a Hermes. The
    // HL2 I2C row queues the same I2C reads and a write on both I/O boards
    // before each round: the I2C subframes go out alike, and the reads
    // wait in the pending-read FIFO alike. Then, with the sender running,
    // reads the sender sends are answered on this thread in FIFO order.
    void senderThreadSendsTheSeamsBytes_data() {
        QTest::addColumn<int>("board");
        QTest::addColumn<bool>("i2c");
        QTest::newRow("HermesLite") << int(HPSDRHW::HermesLite) << false;
        QTest::newRow("Hermes") << int(HPSDRHW::Hermes) << false;
        QTest::newRow("HermesLite I2C") << int(HPSDRHW::HermesLite) << true;
    }
    void senderThreadSendsTheSeamsBytes() {
        QFETCH(int, board);
        QFETCH(bool, i2c);
        QUdpSocket wire;
        QVERIFY(wire.bind(QHostAddress::LocalHost, 0));

        P1RadioConnection threaded;
        P1RadioConnection seam;
        // A clock that never moves, so no read is given up unanswered.
        IoBoardHl2 threadedIo;
        IoBoardHl2 seamIo;
        threadedIo.setClockForTest([] { return qint64(0); });
        seamIo.setClockForTest([] { return qint64(0); });
        for (P1RadioConnection* c : {&threaded, &seam}) {
            c->init();
            c->setBoardForTest(HPSDRHW(board));
            if (i2c) {
                c->setIoBoard(c == &threaded ? &threadedIo : &seamIo);
            }
            c->armEp2ForTest(QHostAddress::LocalHost, wire.localPort());
        }

        // Two reads that ask for an answer and a write, on both boards.
        constexpr int kReadsPerRound = 2;
        const auto queueI2c = [&](quint8 reg) -> bool {
            for (IoBoardHl2* io : {&threadedIo, &seamIo}) {
                IoBoardHl2::I2cTxn read;
                read.bus = 1;
                read.address = 0x41;
                read.control = reg;
                read.isRead = true;
                read.needsResponse = true;
                IoBoardHl2::I2cTxn write;
                write.bus = 1;
                write.address = 0x1d;
                write.control = quint8(reg + 1);
                write.writeData = 0x5a;
                IoBoardHl2::I2cTxn read2 = read;
                read2.control = quint8(reg + 2);
                if (!io->enqueueI2c(read) || !io->enqueueI2c(write) || !io->enqueueI2c(read2)) {
                    return false;
                }
            }
            return true;
        };
        // The I2C subframes in a frame (C0 bits 1-7 name the I2C chip).
        const auto i2cSubframes = [](const QByteArray& f) {
            int n = 0;
            for (int off : {11, 523}) {
                const int chip = (quint8(f[off]) >> 1) & 0x3f;
                if (chip == 0x3c || chip == 0x3d) { ++n; }
            }
            return n;
        };

        constexpr int kFrames = 40;
        const auto compareRound = [&](const char* round) -> bool {
            if (threaded.roundRobinIdxForTest() != seam.roundRobinIdxForTest()
                || threaded.ep2NextSequenceForTest() != seam.ep2NextSequenceForTest()) {
                qWarning() << round << "the two do not start from the same round-robin index";
                return false;
            }

            // The thread: send until kFrames have arrived, then stop.
            QList<QByteArray> fromThread;
            threaded.resetEp2PacerForTest();
            threaded.startEp2SenderForTest();
            const bool arrived = QTest::qWaitFor([&] {
                while (wire.hasPendingDatagrams()) {
                    QByteArray d(int(wire.pendingDatagramSize()), Qt::Uninitialized);
                    wire.readDatagram(d.data(), d.size());
                    fromThread.append(d);
                }
                return fromThread.size() >= kFrames;
            }, 5000);
            threaded.stopEp2SenderForTest();
            QTest::qWait(20);
            while (wire.hasPendingDatagrams()) {
                QByteArray d(int(wire.pendingDatagramSize()), Qt::Uninitialized);
                wire.readDatagram(d.data(), d.size());
                fromThread.append(d);
            }
            if (!arrived) {
                qWarning() << round << "only" << fromThread.size() << "frames arrived";
                return false;
            }

            // The seam: one frame a pass, as many as the thread sent.
            QList<QByteArray> fromSeam;
            seam.resetEp2PacerForTest();
            seam.serviceEp2SendForTest(0, &fromSeam);  // starts the clock
            for (qint64 t = 0; fromSeam.size() < fromThread.size();) {
                t += 2625000;
                if (seam.serviceEp2SendForTest(t, &fromSeam) != 1) {
                    qWarning() << round << "a seam pass did not send one frame";
                    return false;
                }
            }

            int i2cFrames = 0;
            for (int i = 0; i < fromThread.size(); ++i) {
                if (fromThread[i] != fromSeam[i]) {
                    qWarning() << round << "frame" << i << "differs:\n thread"
                               << fromThread[i].left(16).toHex(' ') << fromThread[i].mid(520, 8).toHex(' ')
                               << "\n seam  " << fromSeam[i].left(16).toHex(' ')
                               << fromSeam[i].mid(520, 8).toHex(' ');
                    return false;
                }
                i2cFrames += i2cSubframes(fromThread[i]);
            }
            if (i2c && i2cFrames != 3) {
                qWarning() << round << i2cFrames << "I2C subframes on the wire, expected 3";
                return false;
            }
            if (!i2c && i2cFrames != 0) {
                qWarning() << round << i2cFrames << "I2C subframes with nothing queued";
                return false;
            }
            qInfo().noquote() << QString("%1: %2 frames alike, %3 I2C subframes")
                                     .arg(round).arg(fromThread.size()).arg(i2cFrames);
            return true;
        };

        int readsSent = 0;
        if (i2c) {
            QVERIFY(queueI2c(0x00));
            readsSent += kReadsPerRound;
        }
        QVERIFY(compareRound("plain"));
        if (i2c) {
            // Both sent every read, and both wait for the same answers.
            QCOMPARE(threadedIo.i2cDequeuedCount(), threadedIo.i2cEnqueuedCount());
            QCOMPARE(seamIo.i2cDequeuedCount(), seamIo.i2cEnqueuedCount());
            QCOMPARE(threadedIo.pendingReadDepth(), readsSent);
            QCOMPARE(seamIo.pendingReadDepth(), readsSent);
            QVERIFY(queueI2c(0x10));
            readsSent += kReadsPerRound;
        }

        // The force-bank setters, on both, with no sender running.
        for (P1RadioConnection* c : {&threaded, &seam}) {
            c->setMox(true);
            c->setTrxRelay(true);
            c->setMicTipRing(true);
            QVERIFY(c->forceBank0NextForTest());
        }
        QVERIFY(compareRound("forced"));
        QVERIFY(!threaded.forceBank0NextForTest());
        QVERIFY(!seam.forceBank0NextForTest());
        if (!i2c) {
            return;
        }
        QCOMPARE(threadedIo.pendingReadDepth(), readsSent);
        QCOMPARE(seamIo.pendingReadDepth(), readsSent);

        // The sender pushes each read it sends; the answers, parsed on this
        // thread while it runs, pop them oldest first.
        threadedIo.clearPendingReads();
        QSignalSpy answered(&threadedIo, &IoBoardHl2::i2cReadAnswered);
        threaded.resetEp2PacerForTest();
        threaded.startEp2SenderForTest();
        constexpr int kLiveReads = 6;
        for (int i = 0; i < kLiveReads; ++i) {
            IoBoardHl2::I2cTxn read;
            read.bus = 1;
            read.address = 0x41;
            read.control = quint8(0x20 + i);
            read.isRead = true;
            read.needsResponse = true;
            QVERIFY(threadedIo.enqueueI2c(read));
        }
        QVERIFY(QTest::qWaitFor([&] {
            return threadedIo.i2cDequeuedCount() == threadedIo.i2cEnqueuedCount();
        }, 3000));
        for (int i = 0; i < kLiveReads; ++i) {
            // C0 bit 7 marks an I2C answer; C1-C4 carry the read's bytes.
            threaded.parseI2cResponseForTest(quint8(0x80 | 0x3d), quint8(i), 0x11, 0x22, 0x33);
        }
        threaded.stopEp2SenderForTest();
        QCOMPARE(answered.size(), kLiveReads);
        for (int i = 0; i < kLiveReads; ++i) {
            QCOMPARE(answered.at(i).at(0).value<quint8>(), quint8(0x41));
            QCOMPARE(answered.at(i).at(1).value<quint8>(), quint8(0x20 + i));
            QCOMPARE(answered.at(i).at(2).value<quint8>(), quint8(i));
        }
        QCOMPARE(threadedIo.pendingReadDepth(), 0);
    }

    // The reconnect attempt stops the EP2 sender before it rebinds the
    // socket the sender sends on (the bind warns when a sender runs), and
    // the attempt brings the link back with the sender running.
    void reconnectStopsTheSenderBeforeTheRebind() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.connectToRadio(makeInfo(fake));
        QVERIFY(QTest::qWaitFor([&] { return conn.state() == ConnectionState::Connected; }, 3000));
        QVERIFY(conn.ep2SenderRunningForTest());

        QTest::failOnWarning(QRegularExpression(QStringLiteral("socket rebound while the EP2 sender runs")));
        const quint64 starts = conn.ep2SenderStartsForTest();
        conn.onReconnectTimeoutForTest();
        QVERIFY(conn.ep2SenderRunningForTest());
        QVERIFY(conn.ep2SenderStartsForTest() > starts);
        QVERIFY(QTest::qWaitFor([&] { return conn.state() == ConnectionState::Connected; }, 3000));

        conn.disconnect();
        fake.stop();
    }

    // The sender's last frames reach the upload readout when the link is
    // lost and when a connect times out: nothing is left undrained.
    void senderBytesAreDrainedWhenItStops() {
        {
            P1FakeRadio fake;
            fake.start();
            P1RadioConnection conn;
            conn.init();
            // A long reconnect interval: no attempt restarts the sender here.
            conn.setReconnectTimingForTest(/*watchdogSilenceMs=*/150, /*reconnectIntervalMs=*/10000);
            conn.connectToRadio(makeInfo(fake));
            QVERIFY(QTest::qWaitFor([&] { return conn.state() == ConnectionState::Connected; }, 3000));
            QTest::qWait(100);
            fake.goSilent();
            QVERIFY(QTest::qWaitFor([&] { return conn.state() == ConnectionState::LinkLost; }, 3000));
            QVERIFY(!conn.ep2SenderRunningForTest());
            QCOMPARE(conn.ep2BytesUnrecordedForTest(), qint64(0));
            QVERIFY(conn.txByteRate(5000) > 0.0);
            conn.disconnect();
            fake.stop();
        }
        {
            // A radio that never answers: the connect times out (2 s).
            P1FakeRadio fake;
            fake.start();
            fake.goSilent();
            P1RadioConnection conn;
            conn.init();
            QSignalSpy failed(&conn, &P1RadioConnection::connectFailed);
            conn.connectToRadio(makeInfo(fake));
            QVERIFY(conn.ep2SenderRunningForTest());
            QVERIFY(failed.wait(5000));
            QCOMPARE(conn.state(), ConnectionState::Disconnected);
            QVERIFY(!conn.ep2SenderRunningForTest());
            QCOMPARE(conn.ep2BytesUnrecordedForTest(), qint64(0));
            fake.stop();
        }
    }

    // Test 5: the sender's lifecycle. Fifty rounds of connect, a
    // sample-rate change, a receiver-count change, a lost link and its
    // reconnect, and a disconnect, with the sender running throughout: no
    // deadlock, the sender stopped and started at each step, never two.
    void senderLifecycleNeverRunsTwo() {
        P1FakeRadio fake;
        fake.start();

        P1RadioConnection conn;
        conn.init();
        conn.setReconnectTimingForTest(/*watchdogSilenceMs=*/150, /*reconnectIntervalMs=*/50);
        P1RadioConnection::resetMaxLiveEp2SendersForTest();
        QCOMPARE(P1RadioConnection::liveEp2SendersForTest(), 0);

        constexpr int kCycles = 50;
        for (int i = 0; i < kCycles; ++i) {
            const QByteArray where = QByteArray("cycle ") + QByteArray::number(i);
            const auto at = [&](const char* step) {
                return where + ": " + step;
            };
            quint64 starts = conn.ep2SenderStartsForTest();

            conn.connectToRadio(makeInfo(fake));
            QVERIFY2(conn.ep2SenderRunningForTest(), at("no sender after connect").constData());
            QVERIFY2(conn.ep2SenderStartsForTest() > starts, at("connect did not start the sender").constData());
            QVERIFY2(QTest::qWaitFor([&] { return conn.state() == ConnectionState::Connected; }, 3000),
                     at("never connected").constData());

            // A sample-rate change (posted, as RadioModel does).
            starts = conn.ep2SenderStartsForTest();
            const int rate = (i % 2 == 0) ? 96000 : 48000;
            postFromAnotherThread(conn, [&conn, rate] { conn.restartStreamWithRate(rate); });
            QVERIFY2(QTest::qWaitFor([&] { return conn.ep2SenderStartsForTest() > starts; }, 3000),
                     at("the rate change did not restart the sender").constData());
            QVERIFY2(conn.ep2SenderRunningForTest(), at("no sender after the rate change").constData());

            // A receiver-count change (the HL2 announces at least two).
            starts = conn.ep2SenderStartsForTest();
            const int count = (i % 2 == 0) ? 3 : 2;
            postFromAnotherThread(conn, [&conn, count] { conn.setActiveReceiverCount(count); });
            QVERIFY2(QTest::qWaitFor([&] { return conn.ep2SenderStartsForTest() > starts; }, 3000),
                     at("the receiver-count change did not restart the sender").constData());
            QVERIFY2(conn.ep2SenderRunningForTest(), at("no sender after the count change").constData());

            // A lost link: the watchdog stops the sender, the reconnect
            // starts it again.
            starts = conn.ep2SenderStartsForTest();
            fake.goSilent();
            QVERIFY2(QTest::qWaitFor([&] { return conn.state() == ConnectionState::LinkLost; }, 3000),
                     at("the silence was not seen").constData());
            fake.resume();
            QVERIFY2(QTest::qWaitFor([&] {
                         return conn.state() == ConnectionState::Connected
                             && conn.ep2SenderStartsForTest() > starts;
                     }, 5000),
                     at("never reconnected").constData());
            QVERIFY2(conn.ep2SenderRunningForTest(), at("no sender after the reconnect").constData());

            conn.disconnect();
            QVERIFY2(!conn.ep2SenderRunningForTest(), at("a sender after disconnect").constData());
            QCOMPARE(P1RadioConnection::liveEp2SendersForTest(), 0);
            QVERIFY2(QTest::qWaitFor([&] { return !fake.isRunning(); }, 1000),
                     at("the fake never saw the stop").constData());
        }

        qInfo().noquote() << QString("MEASURE lifecycle: %1 cycles, %2 sender starts, at most %3 alive")
                                 .arg(kCycles)
                                 .arg(conn.ep2SenderStartsForTest())
                                 .arg(P1RadioConnection::maxLiveEp2SendersForTest());
        QCOMPARE(P1RadioConnection::maxLiveEp2SendersForTest(), 1);
        QVERIFY(conn.ep2SenderStartsForTest() >= quint64(4 * kCycles));
        fake.stop();
    }
};

QTEST_MAIN(TestP1LoopbackConnection)
#include "tst_p1_loopback_connection.moc"
