#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/security/PairingWindow.h  (NereusSDR)
// =================================================================
//
// When the Core accepts a new device, and the code it accepts it with
// (iPhone app plan Task 14, R-IOS-08; the pairing design,
// docs/architecture/2026-08-02-remote-station-identity-and-pairing-design.md
// section 4.5).
//
// Four states:
//
//   OpenUnclaimed  the Core has no paired device and no pairing token
//                  (DeviceStore::isClaimed() false). Open with NO timer
//                  (but with the attempt ceiling below):
//                  an unclaimed Core holds nothing worth taking, and a
//                  timer would expire while the operator fetches a phone.
//                  One tap on the Core's own network pairs, and so does
//                  the code from anywhere.
//   ClosedClaimed  the Core is claimed and the window is shut: no pairing
//                  of any kind.
//   OpenReopened   the operator reopened it, from the Core's console or
//                  from a paired device (`pairing.open`): the code pairs
//                  (not one tap); it closes after one successful pairing,
//                  close() or `pairing.close`, or kReopenedLifetimeMs
//                  (10 minutes) after it opened.
//   ClosedUnclaimed an unclaimed window the attempt ceiling closed. Only
//                  the console's reopen() opens it again.
//
// The attempt ceiling (fix wave R1-I2): kMaxConsecutiveFailures (5)
// burned codes in a row close any open window, reopened or unclaimed, and
// reopening starts afresh (no failures, no wait).
//
// The state follows the device store: the first pairing claims the Core
// and closes an unclaimed window; a Core reset to unclaimed from its
// console opens again (devices.revoke never removes the last device while
// no token is active, so physical access is the only way back). The
// window's state is the Core's, independent of any connection.
//
// The code is single use. The exchange takes it (takeCode()) at the step
// where the Core first commits to it, so exactly one guess is made per
// code, even with several connections trying at once. A success claims
// (or closes); anything else burns it: the next code appears after 5 s,
// the wait doubling after each consecutive failure (up to 300 s, though
// the ceiling closes the window at the fifth), and a success resets it.
// retryAfterMs() says when.
//
// Time comes from an injected clock (milliseconds, monotonic); a
// single-shot timer calls poll() when a wait ends, and a test advances its
// clock and calls poll() itself.
//
// The code is a secret while it is live. Nothing here logs it; the Core's
// console prints it (StationServer), and the link carries it only to a
// connection signed in with a paired device's key.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-24: Part C fix wave (R1-I1): the last device is not
//               revoked while no token is active. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-24: Part C fix wave (R1-I2): a reopened pairing window
//               lasts 10 minutes, five burned codes in a row close any window,
//               and reopening starts afresh. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace NereusSDR {

class DeviceStore;

class PairingWindow : public QObject {
    Q_OBJECT

public:
    enum class State { ClosedClaimed, OpenUnclaimed, OpenReopened, ClosedUnclaimed };
    Q_ENUM(State)

    using Clock = std::function<qint64()>;

    /// The wait before the next code after one failure, and its ceiling;
    /// it doubles after each consecutive failure between the two.
    static constexpr qint64 kFirstRetryMs = 5000;
    static constexpr qint64 kMaxRetryMs = 300000;
    /// The nameplate until the rendezvous supplies one: 1 to this.
    static constexpr int kLocalNameplateMax = 99;
    /// Fix wave R1-I2 (the controller's ruling, 2026-09-24): consecutive
    /// burned codes that close any open window, and how long a reopened
    /// window stays open.
    static constexpr int kMaxConsecutiveFailures = 5;
    static constexpr qint64 kReopenedLifetimeMs = 10 * 60 * 1000;

    /// `devices` is not owned and must outlive this object.
    explicit PairingWindow(DeviceStore& devices, QObject* parent = nullptr);
    ~PairingWindow() override;

    State state() const { return m_state; }
    bool isOpen() const { return isOpenState(m_state); }

    /// Opens a closed window afresh (no failures, no wait) with a code: on
    /// a claimed Core OpenReopened, for kReopenedLifetimeMs, from the
    /// console or a paired device; on an unclaimed Core the attempt ceiling
    /// closed (ClosedUnclaimed) OpenUnclaimed again, from the console.
    /// Nothing to do when it is open.
    void reopen();
    /// Closes a reopened window. An unclaimed Core's window stays open.
    void close();

    /// The code to pair with; empty while closed, while the last one is
    /// being tried, and while the wait after a failure runs.
    QString currentCode() const { return m_code; }

    /// Task 27 supplies the rendezvous nameplate; until then a random
    /// number from 1 to kLocalNameplateMax. A change makes a new code.
    void setNameplate(int nameplate);
    int nameplate() const { return m_nameplate; }

    // ── The exchange's accounting (StationServer) ─────────────────────

    /// Changes with every new code, so an exchange that started on one
    /// code cannot spend the next.
    quint64 codeSerial() const { return m_serial; }
    /// Spends the code of `serial`: true once, while it is still the
    /// current code and no other exchange holds it. From here it is
    /// either paired with or burned.
    bool takeCode(quint64 serial);
    /// A device was paired (by the code or by one tap): resets the wait
    /// and closes a reopened window. The device store's change closes an
    /// unclaimed one.
    void pairingSucceeded();
    /// The code taken was wrong, or its exchange ended without pairing:
    /// burned, and the next code waits.
    void pairingFailed();
    /// True between takeCode() and the exchange's outcome.
    bool codeInUse() const { return m_codeInUse; }
    /// Milliseconds until the next code appears: 0 when one is shown or
    /// the window is closed; kFirstRetryMs while another exchange holds
    /// the code.
    qint64 retryAfterMs() const;
    int consecutiveFailures() const { return m_failures; }

    void setClock(Clock clock);
    /// Re-checks the wait against the clock: a code whose wait has ended
    /// appears. The wait's timer calls it.
    void poll();

signals:
    void stateChanged(NereusSDR::PairingWindow::State state);
    /// The code changed; empty when there is none to show.
    void codeChanged(const QString& code);

private:
    static bool isOpenState(State state);
    void followDevices();
    /// No failures counted and no wait (reopen() and a reset).
    void startAfresh();
    /// The attempt ceiling: closes the open window.
    void closeForCeiling();
    /// Sets the state and the code, then signals each that moved.
    void commit(State state, const QString& code);
    /// The code to show in `state`: the current one, a new one, or none
    /// (closed, in use, or waiting; the wait's timer is then armed).
    QString codeFor(State state);
    qint64 now() const;

    DeviceStore& m_devices;
    Clock m_clock;
    QTimer* m_wait = nullptr;
    QTimer* m_expiry = nullptr;
    State m_state = State::ClosedClaimed;
    QString m_code;
    quint64 m_serial = 0;
    int m_nameplate = 1;
    bool m_codeInUse = false;
    int m_failures = 0;
    qint64 m_nextCodeAt = 0;
    /// When a reopened window closes by itself; 0 for none.
    qint64 m_openUntil = 0;
};

} // namespace NereusSDR
