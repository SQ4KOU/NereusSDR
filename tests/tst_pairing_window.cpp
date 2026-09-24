// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_pairing_window.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 14 (R-IOS-08): the pairing window.
//
// Refusal, burn and backoff first:
//
//   - a claimed Core's window is closed and offers no code; close() does
//     not close an unclaimed Core's window;
//   - the code is single use: takeCode() succeeds once per code, never for
//     a code that has changed, and never while another exchange holds it;
//   - a failure burns the code; the next appears after 5 s, the wait
//     doubling after each consecutive failure up to 300 s, reset by a
//     success; retryAfterMs() says when;
//
// then the admit paths: a new Core is OpenUnclaimed with no timer; the
// first pairing claims it (ClosedClaimed); reopen() opens OpenReopened,
// which closes after one successful pairing or close(); a Core that loses
// its last device opens again; every change of the code is signalled.
//
// Time is an injected clock; nothing sleeps. Codes are made at run time
// and never printed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QtTest>

#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/security/DeviceStore.h"
#include "core/security/PairingCode.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"

using namespace NereusSDR;

namespace {

// A device record with a key made at run time.
PairedDevice makeDevice()
{
    QTemporaryDir dir;
    const StationIdentity key = StationIdentity::loadOrCreate(dir.path());
    PairedDevice device;
    device.id = key.fingerprint();
    device.publicKeySpki = key.publicKeySpki();
    device.name = QStringLiteral("Shack iPhone");
    device.kind = QStringLiteral("phone");
    return device;
}

struct Fixture {
    QTemporaryDir dir;
    std::unique_ptr<DeviceStore> store = std::make_unique<DeviceStore>(dir.path());
    qint64 clock = 1000000;
    std::unique_ptr<PairingWindow> window;

    Fixture()
    {
        window = std::make_unique<PairingWindow>(*store);
        window->setClock([this] { return clock; });
    }

    void advance(qint64 ms)
    {
        clock += ms;
        window->poll();
    }

    // One burned code: taken, then failed.
    void burn()
    {
        QVERIFY(window->takeCode(window->codeSerial()));
        window->pairingFailed();
    }
};

bool isWellFormedCode(const QString& code, int nameplate)
{
    return PairingCode::normalise(code) == code
        && code.startsWith(QString::number(nameplate) + QLatin1Char('-'));
}

} // namespace

class TstPairingWindow : public QObject {
    Q_OBJECT

private slots:
    // ── Refusal, burn and backoff ───────────────────────────────────────

    void aClaimedCoreIsClosedWithNoCode()
    {
        Fixture f;
        QVERIFY(f.store->add(makeDevice()));
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(!f.window->isOpen());
        QVERIFY(f.window->currentCode().isEmpty());
        QVERIFY(!f.window->takeCode(f.window->codeSerial()));
        QCOMPARE(f.window->retryAfterMs(), qint64(0));
        // A window constructed on an already claimed Core starts closed.
        PairingWindow second(*f.store);
        QCOMPARE(second.state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(second.currentCode().isEmpty());
    }

    void closeDoesNotCloseAnUnclaimedCore()
    {
        Fixture f;
        const QString code = f.window->currentCode();
        f.window->close();
        QCOMPARE(f.window->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(f.window->currentCode() == code);
        // Nor does reopen() change it.
        f.window->reopen();
        QCOMPARE(f.window->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(f.window->currentCode() == code);
    }

    void aCodeIsTakenOnceAndOnlyWhileCurrent()
    {
        Fixture f;
        const quint64 serial = f.window->codeSerial();
        QVERIFY(!f.window->takeCode(serial + 1));
        QVERIFY(!f.window->takeCode(serial - 1));
        QVERIFY(f.window->takeCode(serial));
        QVERIFY(f.window->codeInUse());
        // Spent: shown nowhere, and not taken again by another exchange.
        QVERIFY(f.window->currentCode().isEmpty());
        QVERIFY(!f.window->takeCode(serial));
        QCOMPARE(f.window->retryAfterMs(), PairingWindow::kFirstRetryMs);
        // No new code turns up while it is being tried, whatever the time.
        f.advance(PairingWindow::kMaxRetryMs * 2);
        QVERIFY(f.window->currentCode().isEmpty());
    }

    void aFailureBurnsTheCodeAndTheNextWaitsFiveSeconds()
    {
        Fixture f;
        QSignalSpy codes(f.window.get(), &PairingWindow::codeChanged);
        f.burn();
        QVERIFY(f.window->currentCode().isEmpty());
        QCOMPARE(f.window->consecutiveFailures(), 1);
        QCOMPARE(f.window->retryAfterMs(), qint64(5000));
        f.advance(4999);
        QVERIFY(f.window->currentCode().isEmpty());
        QCOMPARE(f.window->retryAfterMs(), qint64(1));
        f.advance(1);
        QVERIFY(!f.window->currentCode().isEmpty());
        QCOMPARE(f.window->retryAfterMs(), qint64(0));
        QVERIFY(isWellFormedCode(f.window->currentCode(), f.window->nameplate()));
        // Taken (""), then the new one.
        QCOMPARE(codes.size(), 2);
        QVERIFY(codes.at(0).at(0).toString().isEmpty());
        QVERIFY(codes.at(1).at(0).toString() == f.window->currentCode());
    }

    void theWaitDoublesUpTo300Seconds()
    {
        Fixture f;
        const QList<qint64> expected{5000, 10000, 20000, 40000, 80000, 160000, 300000, 300000};
        for (const qint64 wait : expected) {
            f.burn();
            QCOMPARE(f.window->retryAfterMs(), wait);
            f.advance(wait - 1);
            QVERIFY(f.window->currentCode().isEmpty());
            f.advance(1);
            QVERIFY(!f.window->currentCode().isEmpty());
        }
        QCOMPARE(f.window->consecutiveFailures(), expected.size());
    }

    void aSuccessResetsTheWait()
    {
        Fixture f;
        QVERIFY(f.store->add(makeDevice()));
        f.window->reopen();
        f.burn();
        f.advance(5000);
        f.burn();
        QCOMPARE(f.window->retryAfterMs(), qint64(10000));
        f.advance(10000);
        QVERIFY(f.window->takeCode(f.window->codeSerial()));
        QVERIFY(f.store->add(makeDevice()));
        f.window->pairingSucceeded();
        QCOMPARE(f.window->consecutiveFailures(), 0);
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        // Reopened, the code is there at once, and the next failure waits
        // 5 s again.
        f.window->reopen();
        QVERIFY(!f.window->currentCode().isEmpty());
        f.burn();
        QCOMPARE(f.window->retryAfterMs(), qint64(5000));
    }

    void aWaitSurvivesClosingAndReopening()
    {
        Fixture f;
        QVERIFY(f.store->add(makeDevice()));
        f.window->reopen();
        f.burn();
        f.window->close();
        f.window->reopen();
        // Reopening does not skip the wait.
        QVERIFY(f.window->currentCode().isEmpty());
        QCOMPARE(f.window->retryAfterMs(), qint64(5000));
        f.advance(5000);
        QVERIFY(!f.window->currentCode().isEmpty());
    }

    void theWaitEndsByItsOwnTimer()
    {
        // A single-shot timer, armed for the wait's length, calls poll()
        // when the wait ends; the test moves its clock and polls instead
        // of waiting for it.
        Fixture f;
        f.burn();
        QTimer* timer = f.window->findChild<QTimer*>();
        QVERIFY(timer != nullptr);
        QVERIFY(timer->isActive());
        QVERIFY(timer->isSingleShot());
        QCOMPARE(timer->interval(), 5000);
        f.advance(5000);
        QVERIFY(!f.window->currentCode().isEmpty());
        QVERIFY(!timer->isActive());
    }

    // ── Admit paths ─────────────────────────────────────────────────────

    void aNewCoreIsOpenWithACodeAndNoTimer()
    {
        Fixture f;
        QCOMPARE(f.window->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(f.window->isOpen());
        QVERIFY(f.window->nameplate() >= 1);
        QVERIFY(f.window->nameplate() <= PairingWindow::kLocalNameplateMax);
        QVERIFY(isWellFormedCode(f.window->currentCode(), f.window->nameplate()));
        // No timer: a day later it is the same code.
        const QString code = f.window->currentCode();
        f.advance(24LL * 60 * 60 * 1000);
        QCOMPARE(f.window->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(f.window->currentCode() == code);
        QTimer* timer = f.window->findChild<QTimer*>();
        QVERIFY(timer == nullptr || !timer->isActive());
    }

    void theFirstPairingClaimsTheCore()
    {
        Fixture f;
        QSignalSpy states(f.window.get(), &PairingWindow::stateChanged);
        QVERIFY(f.window->takeCode(f.window->codeSerial()));
        QVERIFY(f.store->add(makeDevice()));
        f.window->pairingSucceeded();
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(f.window->currentCode().isEmpty());
        QVERIFY(!f.window->codeInUse());
        QCOMPARE(states.size(), 1);
        // One tap claims it too: the store's change alone closes it.
        Fixture tap;
        QVERIFY(tap.store->add(makeDevice()));
        QCOMPARE(tap.window->state(), PairingWindow::State::ClosedClaimed);
    }

    void reopenedClosesAfterOnePairing()
    {
        Fixture f;
        QVERIFY(f.store->add(makeDevice()));
        QSignalSpy codes(f.window.get(), &PairingWindow::codeChanged);
        f.window->reopen();
        QCOMPARE(f.window->state(), PairingWindow::State::OpenReopened);
        QVERIFY(isWellFormedCode(f.window->currentCode(), f.window->nameplate()));
        QCOMPARE(codes.size(), 1);
        QVERIFY(f.window->takeCode(f.window->codeSerial()));
        QVERIFY(f.store->add(makeDevice()));
        f.window->pairingSucceeded();
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(f.window->currentCode().isEmpty());
    }

    void reopenedClosesOnClose()
    {
        Fixture f;
        QVERIFY(f.store->add(makeDevice()));
        f.window->reopen();
        const quint64 serial = f.window->codeSerial();
        f.window->close();
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(f.window->currentCode().isEmpty());
        QVERIFY(!f.window->takeCode(serial));
        // Each reopening makes a new code.
        f.window->reopen();
        QVERIFY(f.window->codeSerial() != serial);
    }

    void losingTheLastDeviceOpensTheWindowAgain()
    {
        Fixture f;
        const PairedDevice device = makeDevice();
        QVERIFY(f.store->add(device));
        QCOMPARE(f.window->state(), PairingWindow::State::ClosedClaimed);
        QVERIFY(f.store->remove(device.id));
        QCOMPARE(f.window->state(), PairingWindow::State::OpenUnclaimed);
        QVERIFY(!f.window->currentCode().isEmpty());
    }

    void aNewNameplateMakesANewCode()
    {
        Fixture f;
        const quint64 serial = f.window->codeSerial();
        const int other = f.window->nameplate() == 42 ? 43 : 42;
        f.window->setNameplate(other);
        QCOMPARE(f.window->nameplate(), other);
        QVERIFY(f.window->codeSerial() != serial);
        QVERIFY(isWellFormedCode(f.window->currentCode(), other));
        // Out of range is ignored.
        f.window->setNameplate(0);
        QCOMPARE(f.window->nameplate(), other);
    }
};

QTEST_GUILESS_MAIN(TstPairingWindow)
#include "tst_pairing_window.moc"
