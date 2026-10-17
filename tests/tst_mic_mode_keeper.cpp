// =================================================================
// tests/tst_mic_mode_keeper.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test of the rule that keeps the
// window's stored macOS Mic Mode at Standard (MicModeKeeper), which the
// standing capture helper runs at its warm-up.  The system's calls are
// stood in for; nothing here reads or changes a real setting.
//
// Modification history (NereusSDR):
//   2026-10-16: new. J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//               Code.
// =================================================================

#include <QtTest>

#include "core/audio/MacMicMode.h"
#include "core/audio/MicModeKeeper.h"

#include <vector>

using namespace NereusSDR;

namespace {

constexpr char kBundle[] = "com.example.window";

// A stored mode the calls read and write, with every call recorded.
struct FakeStore {
    long mode = kMicModeStandard;
    bool takesWrites = true;
    std::vector<QString> reads;
    std::vector<std::pair<long, QString>> writes;

    MicModeCalls calls()
    {
        MicModeCalls c;
        c.read = [this](const QString& bundleId) {
            reads.push_back(bundleId);
            return mode;
        };
        c.write = [this](long next, const QString& bundleId) {
            writes.emplace_back(next, bundleId);
            if (takesWrites) {
                mode = next;
            }
            return takesWrites;
        };
        return c;
    }
};

} // namespace

class TstMicModeKeeper : public QObject {
    Q_OBJECT

private slots:
    void voiceIsolationBecomesStandard()
    {
        FakeStore store;
        store.mode = kMicModeVoiceIsolation;
        const MicModeResult result = keepMicModeStandard(QString::fromLatin1(kBundle),
                                                         store.calls());
        QCOMPARE(result.outcome, MicModeOutcome::MadeStandard);
        QCOMPARE(result.before, kMicModeVoiceIsolation);
        QCOMPARE(store.writes.size(), std::size_t(1));
        QCOMPARE(store.writes.front().first, kMicModeStandard);
        QCOMPARE(store.writes.front().second, QString::fromLatin1(kBundle));
        QCOMPARE(store.mode, kMicModeStandard);
        // Read before, and read back after, for our bundle only.
        QCOMPARE(store.reads.size(), std::size_t(2));
        for (const QString& read : store.reads) {
            QCOMPARE(read, QString::fromLatin1(kBundle));
        }
    }

    void wideSpectrumBecomesStandard()
    {
        FakeStore store;
        store.mode = kMicModeWideSpectrum;
        const MicModeResult result = keepMicModeStandard(QString::fromLatin1(kBundle),
                                                         store.calls());
        QCOMPARE(result.outcome, MicModeOutcome::MadeStandard);
        QCOMPARE(result.before, kMicModeWideSpectrum);
        QCOMPARE(store.mode, kMicModeStandard);
    }

    void standardIsLeftAlone()
    {
        FakeStore store;
        const MicModeResult result = keepMicModeStandard(QString::fromLatin1(kBundle),
                                                         store.calls());
        QCOMPARE(result.outcome, MicModeOutcome::AlreadyStandard);
        QCOMPARE(result.before, kMicModeStandard);
        QVERIFY(store.writes.empty());
        QCOMPARE(store.reads.size(), std::size_t(1));
    }

    void aRefusedWriteIsReported()
    {
        FakeStore store;
        store.mode = kMicModeVoiceIsolation;
        store.takesWrites = false;
        const MicModeResult result = keepMicModeStandard(QString::fromLatin1(kBundle),
                                                         store.calls());
        QCOMPARE(result.outcome, MicModeOutcome::Refused);
        QCOMPARE(result.before, kMicModeVoiceIsolation);
        QCOMPARE(store.mode, kMicModeVoiceIsolation);
    }

    // The system said yes but still reads the old mode: not Standard.
    void aWriteThatDoesNotStickIsRefused()
    {
        FakeStore store;
        store.mode = kMicModeVoiceIsolation;
        MicModeCalls calls = store.calls();
        calls.write = [&store](long next, const QString& bundleId) {
            store.writes.emplace_back(next, bundleId);
            return true;
        };
        QCOMPARE(keepMicModeStandard(QString::fromLatin1(kBundle), calls).outcome,
                 MicModeOutcome::Refused);
    }

    // A read that fails (the system threw) leaves the mode alone.
    void aFailedReadChangesNothing()
    {
        FakeStore store;
        MicModeCalls calls = store.calls();
        calls.read = [&store](const QString& bundleId) {
            store.reads.push_back(bundleId);
            return kMicModeNotRead;
        };
        const MicModeResult result = keepMicModeStandard(QString::fromLatin1(kBundle), calls);
        QCOMPARE(result.outcome, MicModeOutcome::Refused);
        QCOMPARE(result.before, kMicModeNotRead);
        QVERIFY(store.writes.empty());
    }

    void missingCallsAreUnavailable()
    {
        const MicModeResult none = keepMicModeStandard(QString::fromLatin1(kBundle),
                                                       MicModeCalls{});
        QCOMPARE(none.outcome, MicModeOutcome::Unavailable);
        QCOMPARE(none.before, kMicModeNotRead);
        FakeStore store;
        MicModeCalls readOnly = store.calls();
        readOnly.write = nullptr;
        QCOMPARE(keepMicModeStandard(QString::fromLatin1(kBundle), readOnly).outcome,
                 MicModeOutcome::Unavailable);
        QVERIFY(store.reads.empty());
    }

    void noBundleTouchesNothing()
    {
        FakeStore store;
        store.mode = kMicModeVoiceIsolation;
        QCOMPARE(keepMicModeStandard(QString(), store.calls()).outcome, MicModeOutcome::NoBundle);
        QVERIFY(store.reads.empty());
        QVERIFY(store.writes.empty());
    }

    void namesAndTexts()
    {
        QCOMPARE(micModeName(kMicModeVoiceIsolation), QStringLiteral("Voice Isolation"));
        QCOMPARE(micModeName(kMicModeWideSpectrum), QStringLiteral("Wide Spectrum"));
        QCOMPARE(micModeName(kMicModeStandard), QStringLiteral("Standard"));
        QCOMPARE(micModeName(7), QStringLiteral("mode 7"));
        const MicModeOutcome all[] = {MicModeOutcome::AlreadyStandard, MicModeOutcome::MadeStandard,
                                      MicModeOutcome::Refused, MicModeOutcome::Unavailable,
                                      MicModeOutcome::NoBundle};
        for (MicModeOutcome outcome : all) {
            QVERIFY(!micModeOutcomeText(outcome).isEmpty());
        }
    }

    // A test binary has no bundle id, so nothing a test builds names one
    // for the helper's warm-up.
    void aTestRunHasNoBundleId()
    {
        QVERIFY(macMainBundleId().isEmpty());
    }

    // The window's one line for the helper's answer: the old mode when it
    // changed; a line when the system has no call or the change failed;
    // nothing when it was Standard already.
    void theWindowLogsOneLineNamingTheOldMode_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::addColumn<int>("before");
        QTest::addColumn<QString>("text");
        QTest::addColumn<bool>("warning");
        QTest::newRow("voice isolation set")
            << int(MicModeOutcome::MadeStandard) << int(kMicModeVoiceIsolation)
            << QStringLiteral("Mic Mode: set to Standard for this app; it was Voice Isolation")
            << false;
        QTest::newRow("wide spectrum set")
            << int(MicModeOutcome::MadeStandard) << int(kMicModeWideSpectrum)
            << QStringLiteral("Mic Mode: set to Standard for this app; it was Wide Spectrum")
            << false;
        QTest::newRow("standard already")
            << int(MicModeOutcome::AlreadyStandard) << int(kMicModeStandard) << QString() << false;
        QTest::newRow("no bundle")
            << int(MicModeOutcome::NoBundle) << int(kMicModeNotRead) << QString() << false;
        QTest::newRow("call missing")
            << int(MicModeOutcome::Unavailable) << int(kMicModeNotRead)
            << QStringLiteral("Mic Mode: this macOS has no Mic Mode call; Mic Mode left as it is")
            << false;
        QTest::newRow("call failed")
            << int(MicModeOutcome::Refused) << int(kMicModeVoiceIsolation)
            << QStringLiteral("Mic Mode: the system did not take Standard for this app; it was "
                              "Voice Isolation")
            << true;
        QTest::newRow("read failed")
            << int(MicModeOutcome::Refused) << int(kMicModeNotRead)
            << QStringLiteral("Mic Mode: the system could not read this app's Mic Mode; Mic "
                              "Mode left as it is")
            << true;
    }
    void theWindowLogsOneLineNamingTheOldMode()
    {
        QFETCH(int, outcome);
        QFETCH(int, before);
        QFETCH(QString, text);
        QFETCH(bool, warning);
        const MicModeLogLine line = micModeLogLine(MicModeResult{MicModeOutcome(outcome), before});
        QCOMPARE(line.text, text);
        QCOMPARE(line.warning, warning);
    }
};

QTEST_GUILESS_MAIN(TstMicModeKeeper)
#include "tst_mic_mode_keeper.moc"
