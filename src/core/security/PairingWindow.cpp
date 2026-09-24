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
    m_wait->setSingleShot(true);
    connect(m_wait, &QTimer::timeout, this, &PairingWindow::poll);
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

void PairingWindow::followDevices()
{
    const bool claimed = m_devices.isClaimed();
    if (!claimed && m_state != State::OpenUnclaimed) {
        // A new Core, or one that lost its last device: open, no timer.
        commit(State::OpenUnclaimed, codeFor(State::OpenUnclaimed));
    } else if (claimed && m_state == State::OpenUnclaimed) {
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
    if (state == State::ClosedClaimed || m_codeInUse) {
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
    if (m_state != State::ClosedClaimed) {
        return;
    }
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
    // 5 s, 10 s, 20 s, ... 300 s.
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
    if (!isOpen() || m_codeInUse || !m_code.isEmpty()) {
        return;
    }
    commit(m_state, codeFor(m_state));
}

} // namespace NereusSDR
