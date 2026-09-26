// no-port-check: NereusSDR-original.
// =================================================================
// src/core/security/PairingWindow.cpp  (NereusSDR)
// =================================================================
//
// See PairingWindow.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-24: Part C fix wave (R1-I2): a reopened pairing window
//               lasts 10 minutes, five burned codes in a row close any window,
//               and reopening starts afresh. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-24: Part C fix wave (security Minors R1-M1, M2, M4,
//               M5): the confirm-step recheck, the step 1 point check, the
//               per-address handshake cap and 0600 on load. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
// =================================================================

#include "core/security/PairingWindow.h"

#include "core/security/DeviceStore.h"
#include "core/security/PairingCode.h"

#include <QElapsedTimer>
#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QTimer>

#include <algorithm>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcPairing, "nereussdr.pairing")

// A monotonic millisecond clock shared by every window that is not given
// one.
qint64 monotonicMs()
{
    static QElapsedTimer timer = [] {
        QElapsedTimer t;
        t.start();
        return t;
    }();
    return timer.elapsed();
}
} // namespace

PairingWindow::PairingWindow(DeviceStore& devices, QObject* parent)
    : QObject(parent)
    , m_devices(devices)
    , m_clock(&monotonicMs)
    , m_nameplate(static_cast<int>(QRandomGenerator::system()->bounded(1, kLocalNameplateMax + 1)))
{
    m_wait = new QTimer(this);
    m_wait->setObjectName(QStringLiteral("pairingCodeWait"));
    m_wait->setSingleShot(true);
    connect(m_wait, &QTimer::timeout, this, &PairingWindow::poll);
    m_expiry = new QTimer(this);
    m_expiry->setObjectName(QStringLiteral("pairingWindowExpiry"));
    m_expiry->setSingleShot(true);
    connect(m_expiry, &QTimer::timeout, this, &PairingWindow::poll);
    connect(&m_devices, &DeviceStore::devicesChanged, this, &PairingWindow::followDevices);
    followDevices();
}

PairingWindow::~PairingWindow() = default;

qint64 PairingWindow::now() const
{
    return m_clock();
}

void PairingWindow::setClock(Clock clock)
{
    m_clock = clock ? std::move(clock) : Clock(&monotonicMs);
}

bool PairingWindow::isOpenState(State state)
{
    return state == State::OpenUnclaimed || state == State::OpenReopened;
}

void PairingWindow::startAfresh()
{
    m_failures = 0;
    m_nextCodeAt = 0;
    m_wait->stop();
}

void PairingWindow::followDevices()
{
    const bool claimed = m_devices.isClaimed();
    if (!claimed && (m_state == State::ClosedClaimed || m_state == State::OpenReopened)) {
        // A new Core, or one reset to unclaimed from its console (the only
        // way a claimed Core gets here): open, no timer, a fresh count. A
        // window the attempt ceiling closed (ClosedUnclaimed) stays shut
        // until the console reopens it.
        m_expiry->stop();
        startAfresh();
        commit(State::OpenUnclaimed, codeFor(State::OpenUnclaimed));
    } else if (claimed
               && (m_state == State::OpenUnclaimed || m_state == State::ClosedUnclaimed)) {
        // The first device claimed it.
        m_wait->stop();
        commit(State::ClosedClaimed, QString());
    }
}

void PairingWindow::commit(State state, const QString& code)
{
    // Both fields first, then the signals, so a listener reading the window
    // on the first signal sees the whole change: one change, not two.
    const bool stateMoved = m_state != state;
    const bool codeMoved = m_code != code;
    if (isOpenState(m_state) && !isOpenState(state)) {
        m_expiry->stop();
        m_openUntil = 0;
        // Part C fix wave (R1-M1): an exchange that took a code of this
        // window cannot pair with it once the window has closed, even if it
        // is reopened before that exchange's confirm step (holdsCode()).
        ++m_serial;
    }
    m_state = state;
    m_code = code;
    if (stateMoved) {
        qCInfo(lcPairing) << "Pairing window:" << state;
        emit stateChanged(state);
    }
    if (codeMoved) {
        // Never logged: the code is a secret while it is live.
        emit codeChanged(m_code);
    }
}

QString PairingWindow::codeFor(State state)
{
    if (!isOpenState(state) || m_codeInUse) {
        return {};
    }
    if (!m_code.isEmpty()) {
        return m_code;
    }
    const qint64 remaining = m_nextCodeAt - now();
    if (remaining > 0) {
        m_wait->start(static_cast<int>(std::min<qint64>(remaining, kMaxRetryMs)));
        return {};
    }
    m_wait->stop();
    const QString code = PairingCode::generate(m_nameplate);
    if (code.isEmpty()) {
        qCWarning(lcPairing) << "No pairing code could be made: the word list is missing";
        return {};
    }
    ++m_serial;
    return code;
}

void PairingWindow::reopen()
{
    if (isOpen()) {
        return;
    }
    // Whoever reopens it (the console, or a paired device on a claimed
    // Core) starts it afresh: no failures counted and no wait.
    startAfresh();
    if (m_state == State::ClosedUnclaimed) {
        // The attempt ceiling shut an unclaimed Core's window; only the
        // console reaches this (no device is paired). No timer, as before.
        commit(State::OpenUnclaimed, codeFor(State::OpenUnclaimed));
        return;
    }
    m_openUntil = now() + kReopenedLifetimeMs;
    m_expiry->start(static_cast<int>(kReopenedLifetimeMs));
    commit(State::OpenReopened, codeFor(State::OpenReopened));
}

void PairingWindow::close()
{
    if (m_state != State::OpenReopened) {
        return;
    }
    m_wait->stop();
    commit(State::ClosedClaimed, QString());
}

void PairingWindow::closeForCeiling()
{
    m_wait->stop();
    const State closed =
        m_state == State::OpenUnclaimed ? State::ClosedUnclaimed : State::ClosedClaimed;
    qCInfo(lcPairing) << "Pairing closed after" << m_failures
                      << "wrong pairing codes in a row; it opens again only from the Core's "
                         "console or a paired device";
    commit(closed, QString());
}

void PairingWindow::setNameplate(int nameplate)
{
    if (nameplate < 1 || nameplate > PairingCode::kMaxNameplate || nameplate == m_nameplate) {
        return;
    }
    m_nameplate = nameplate;
    if (!m_code.isEmpty() && !m_codeInUse) {
        // The shown code carries the old number: make a new one.
        const QString old = m_code;
        m_code.clear();
        const QString next = codeFor(m_state);
        m_code = old;
        commit(m_state, next);
    }
}

bool PairingWindow::takeCode(quint64 serial)
{
    if (!isOpen() || m_codeInUse || m_code.isEmpty() || serial != m_serial) {
        return false;
    }
    m_codeInUse = true;
    commit(m_state, QString());
    return true;
}

void PairingWindow::pairingSucceeded()
{
    m_codeInUse = false;
    m_failures = 0;
    m_nextCodeAt = 0;
    m_wait->stop();
    if (m_state == State::OpenReopened) {
        // One device per reopening.
        commit(State::ClosedClaimed, QString());
        return;
    }
    followDevices();
    if (isOpen()) {
        commit(m_state, codeFor(m_state));
    }
}

void PairingWindow::pairingFailed()
{
    m_codeInUse = false;
    ++m_failures;
    if (isOpen() && m_failures >= kMaxConsecutiveFailures) {
        // The attempt ceiling (fix wave R1-I2): the fifth burn in a row
        // closes the window, reopened or unclaimed.
        closeForCeiling();
        return;
    }
    // 5 s, 10 s, 20 s, 40 s (the ceiling closes the window before 80 s).
    const int doublings = std::min(m_failures - 1, 16);
    const qint64 wait = std::min<qint64>(kFirstRetryMs << doublings, kMaxRetryMs);
    m_nextCodeAt = now() + wait;
    qCInfo(lcPairing) << "Pairing failed; the next code follows in" << wait << "ms";
    commit(m_state, codeFor(m_state));
}

qint64 PairingWindow::retryAfterMs() const
{
    if (!isOpen()) {
        return 0;
    }
    if (m_codeInUse) {
        return kFirstRetryMs;
    }
    return std::max<qint64>(0, m_nextCodeAt - now());
}

void PairingWindow::poll()
{
    if (m_state == State::OpenReopened && m_openUntil > 0 && now() >= m_openUntil) {
        // A reopened window's lifetime is over (fix wave R1-I2).
        qCInfo(lcPairing) << "Pairing closed: it was open for"
                          << kReopenedLifetimeMs / 60000 << "minutes";
        m_wait->stop();
        commit(State::ClosedClaimed, QString());
        return;
    }
    if (m_state == State::OpenReopened && m_openUntil > 0 && !m_expiry->isActive()) {
        m_expiry->start(static_cast<int>(std::max<qint64>(0, m_openUntil - now())));
    }
    if (!isOpen() || m_codeInUse || !m_code.isEmpty()) {
        return;
    }
    commit(m_state, codeFor(m_state));
}

} // namespace NereusSDR
