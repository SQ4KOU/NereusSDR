// =================================================================
// tests/tst_fault_log.cpp  (NereusSDR)
// =================================================================
// NereusSDR-native test. No AetherSDR equivalent; FaultLog is a
// NereusSDR-native class per design doc §4.7.
// =================================================================
// Modification history (NereusSDR):
//   2026-05-19  Created by J.J. Boyd (KG4VCF), with AI-assisted
//                 transformation via Anthropic Claude Code.
//                 Tests: ringBufferKeepsNewestTen, likelyCauseSwrTrip,
//                 persistsAcrossInstances.
//   2026-08-06  J.J. Boyd (KG4VCF), Remote daemon R2 Task 15: FaultLog::
//                 reload() (FaultLog.h) plus
//                 reloadPicksUpValueDeliveredThroughRemoteBackend, proving
//                 a FaultLog constructed before a remote-mode snapshot
//                 lands finds nothing at construction and the real data
//                 after reload(). AI-assisted transformation via
//                 Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include "core/AppSettings.h"
#include "core/FaultLog.h"
#include "core/settings/SettingsProxy.h"

namespace {
// RAII guard so a QVERIFY/QCOMPARE failure partway through
// reloadPicksUpValueDeliveredThroughRemoteBackend() (an early `return`,
// not a throw -- still runs local destructors normally) cannot leave
// AppSettings::instance() pointed at a since-destroyed local
// SettingsProxy for whatever test slot in this SAME PROCESS runs next.
// tests/CMakeLists.txt's own comment on TestSandboxInit.cpp documents
// that every test file in this suite shares one sandboxed
// AppSettings::instance() singleton per process, not per slot.
class ScopedRemoteBackend {
public:
    explicit ScopedRemoteBackend(NereusSDR::ISettingsBackend* backend)
    {
        NereusSDR::AppSettings::instance().setRemoteBackend(backend);
    }
    ~ScopedRemoteBackend()
    {
        NereusSDR::AppSettings::instance().setRemoteBackend(nullptr);
    }
    ScopedRemoteBackend(const ScopedRemoteBackend&) = delete;
    ScopedRemoteBackend& operator=(const ScopedRemoteBackend&) = delete;
};
} // namespace

class FaultLogTest : public QObject {
    Q_OBJECT
private slots:
    void ringBufferKeepsNewestTen();
    void likelyCauseSwrTrip();
    void persistsAcrossInstances();
    void reloadPicksUpValueDeliveredThroughRemoteBackend();
};

// Capture 12 events and verify only the 10 newest are retained, newest first.
void FaultLogTest::ringBufferKeepsNewestTen()
{
    NereusSDR::FaultLog log("PGXL_FaultHistory");
    log.clear();
    for (int i = 0; i < 12; ++i) {
        NereusSDR::FaultEvent ev;
        ev.whenMs       = static_cast<qint64>(i) * 1000;
        ev.state        = "FAULT";
        ev.fwdAtFaultW  = 1500.0f;
        ev.swrAtFault   = 1.5f;
        ev.tempAtFaultC = 70.0f;
        ev.likelyCause  = "X";
        log.capture(ev);
    }
    QCOMPARE(log.events().size(), 10);
    // Events are prepended so the last captured (i=11, whenMs=11000) is first.
    QCOMPARE(log.events().first().whenMs, qint64(11000));
    // The oldest surviving event should be i=2 (whenMs=2000); i=0 and i=1 were evicted.
    QCOMPARE(log.events().last().whenMs, qint64(2000));
}

// Verify the SWR-trip heuristic fires when SWR exceeds 2.5.
void FaultLogTest::likelyCauseSwrTrip()
{
    const QString cause = NereusSDR::FaultLog::likelyCauseFor(1500.0f, 3.0f, 60.0f);
    QCOMPARE(cause, QString("SWR trip"));
}

// Store events via instance A, construct instance B with the same key, and
// verify the events round-trip through AppSettings JSON correctly.
void FaultLogTest::persistsAcrossInstances()
{
    const QString key = "PGXL_FaultHistory_PersistTest";

    // Write via instance A.
    {
        NereusSDR::FaultLog a(key);
        a.clear();
        NereusSDR::FaultEvent ev;
        ev.whenMs       = qint64(99000);
        ev.state        = "FAULT_PROTECT";
        ev.fwdAtFaultW  = 1800.0f;
        ev.swrAtFault   = 2.9f;
        ev.tempAtFaultC = 72.0f;
        ev.likelyCause  = NereusSDR::FaultLog::likelyCauseFor(
                              ev.fwdAtFaultW, ev.swrAtFault, ev.tempAtFaultC);
        a.capture(ev);
        QCOMPARE(a.events().size(), 1);
    }

    // Read via instance B with the same key -- should see the persisted event.
    {
        NereusSDR::FaultLog b(key);
        const QVector<NereusSDR::FaultEvent> evs = b.events();
        QCOMPARE(evs.size(), 1);
        const NereusSDR::FaultEvent& ev = evs.first();
        QCOMPARE(ev.whenMs,       qint64(99000));
        QCOMPARE(ev.state,        QString("FAULT_PROTECT"));
        QCOMPARE(ev.likelyCause,  QString("SWR trip"));
        QVERIFY(qFuzzyCompare(ev.fwdAtFaultW, 1800.0f));

        // Clean up the test key so repeated runs start clean.
        b.clear();
    }
}

// Remote Daemon R2, Task 15: proves the FaultLog.h "STATED ORDERING"
// comment's claim is actually true, not just asserted. A FaultLog
// constructed BEFORE a remote-mode GUI's connect-time snapshot lands
// finds nothing (mirrors RadioModel always constructing before Task 18's
// handshake can possibly complete); reload(), called after the snapshot
// arrives, picks up the real data because it re-reads through the SAME
// AppSettings::instance().value() the constructor used, which by then
// resolves through the installed SettingsProxy instead of the (empty)
// local store.
void FaultLogTest::reloadPicksUpValueDeliveredThroughRemoteBackend()
{
    // "PGXL_" is a Task 14 (SettingsScope.cpp) Station prefix, so this
    // key delegates to a remote backend once one is installed --
    // exactly like the real "PGXL_FaultHistory"/"TGXL_FaultHistory" keys
    // FaultLog.h documents.
    const QString key = QStringLiteral("PGXL_FaultHistory_RemoteBackendTest");

    NereusSDR::SettingsProxy proxy;
    ScopedRemoteBackend guard(&proxy); // see its class comment for why this is RAII, not a bare call

    // Constructed before any snapshot -- finds nothing, exactly like a
    // real RadioModel-owned FaultLog at GUI launch, before Task 18's
    // handshake completes.
    NereusSDR::FaultLog log(key);
    QVERIFY(log.events().isEmpty());

    // One JSON-encoded FaultEvent, matching FaultLog::save()'s own wire
    // shape -- as it would arrive via SettingsProxyServer::buildSnapshot()
    // on the daemon side once Task 18 relays it.
    const QString json = QStringLiteral(
        "[{\"whenMs\":99000,\"state\":\"FAULT_PROTECT\",\"fwdAtFaultW\":1800,"
        "\"swrAtFault\":2.9,\"tempAtFaultC\":72,\"likelyCause\":\"SWR trip\"}]");
    proxy.applySnapshot(QMap<QString, QString>{{key, json}});

    // Still nothing -- the constructor already ran; nothing has told
    // THIS instance to look again yet. This is the exact gap FaultLog.h
    // describes: the snapshot landed, but no re-read happened.
    QVERIFY(log.events().isEmpty());

    log.reload();

    QCOMPARE(log.events().size(), 1);
    QCOMPARE(log.events().first().whenMs, qint64(99000));
    QCOMPARE(log.events().first().state, QStringLiteral("FAULT_PROTECT"));
    QCOMPARE(log.events().first().likelyCause, QStringLiteral("SWR trip"));
    QVERIFY(qFuzzyCompare(log.events().first().fwdAtFaultW, 1800.0f));
}

QTEST_GUILESS_MAIN(FaultLogTest)
#include "tst_fault_log.moc"
