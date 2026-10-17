// src/core/GeniusPollPacer.h (NereusSDR)
//
// How often a Power Genius XL or Tuner Genius XL link asks for `status`:
// a slow interval in receive and a fast one while keyed, so the meters
// follow the signal while transmitting. Fast while any of these holds:
//   - the Core is on the air (setKeyed);
//   - the device's own status says it is keyed or tuning (setDeviceBusy);
//   - the hold is running: kHoldMs after the last of those two went false.
// The fast interval applies at once: the poll timer is restarted, so the
// first fast poll does not wait out the rest of a slow tick. GeniusStatusGate
// is what makes the fast rates safe: a poll whose predecessor is unanswered
// sends nothing.
//
// no-port-check: NereusSDR-original file. What it rests on:
//   - AetherSDR src/core/PgxlConnection.h:66-67 [@a9a83e66]: kPollTxMs = 100
//     (10 Hz), kPollRxMs = 250 (4 Hz). src/core/PgxlConnection.cpp:531-549
//     [@a9a83e66]: applyPollRateFor (state TRANSMIT_A / TRANSMIT_B) and
//     setTransmitting (restart the timer so the rate applies now).
//   - AetherSDR src/core/TgxlConnection.h:77-78 [@a9a83e66]: kPollTxMs = 16
//     (about 60 Hz), kPollRxMs = 250. src/core/TgxlConnection.cpp:512-530
//     [@a9a83e66]: applyPollRateFor (pttA or pttB keyed) and setTransmitting.
//   - captures/flex-pgxl-tgxl-capture_00001_20260519173452.pcapng: the
//     vendor's PowerGeniusDesktop asks the amplifier every 124 ms and
//     TunerGeniusDesktop asks the tuner every 62 ms, receive and transmit
//     alike.
//   This is a NereusSDR design on that basis, not a port. What differs:
//   - the amplifier's receive rate stays 200 ms, as it was here before;
//   - the tuner's keyed rate is the vendor's own 62 ms, not AetherSDR's
//     16 ms;
//   - "keyed" also comes from the Core, so the rate rises before the first
//     keyed status answer;
//   - the tuner's `tuning` flag counts as busy;
//   - the 2 s hold before the slow rate returns is ours.
//
// Modification history (NereusSDR):
//   2026-10-12 - Created; shared by PgxlConnection and TgxlConnection.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#pragma once

#include <QObject>
#include <QTimer>
#include <QtGlobal>

namespace NereusSDR {

class GeniusPollPacer {
public:
    // How long the fast interval outlasts the key. NereusSDR's own value:
    // long enough that the pauses of CW and of speech between overs do not
    // drop the rate, and that the meters show the fall after the key.
    static constexpr int kHoldMs = 2000;

    // `pollTimer` is the link's own poll timer and outlives the pacer's
    // use of it (both are members of the link). Sets it to `receiveMs`.
    GeniusPollPacer(QTimer* pollTimer, int receiveMs, int keyedMs)
        : m_pollTimer(pollTimer), m_receiveMs(receiveMs), m_keyedMs(keyedMs)
    {
        m_holdTimer.setSingleShot(true);
        QObject::connect(&m_holdTimer, &QTimer::timeout, &m_holdTimer, [this] {
            m_holding = false;
            apply();
        });
        m_pollTimer->setInterval(m_receiveMs);
    }

    // The Core's own state: transmitting, TUNE or the two-tone test.
    void setKeyed(bool keyed)
    {
        const bool was = active();
        m_keyed = keyed;
        changed(was);
    }

    // The device's own status: keyed, or (the tuner) tuning.
    void setDeviceBusy(bool busy)
    {
        const bool was = active();
        m_deviceBusy = busy;
        changed(was);
    }

    // A new connection: what the last one's device said no longer counts
    // and no hold runs. The Core's state still applies.
    void reset()
    {
        m_deviceBusy = false;
        m_holding = false;
        m_holdTimer.stop();
        apply();
    }

    // True while the poll runs at the keyed interval.
    bool fast() const { return active() || m_holding; }

    // Test-only.
    void setHoldMsForTesting(int ms) { m_holdMs = ms > 0 ? ms : 1; }

private:
    Q_DISABLE_COPY_MOVE(GeniusPollPacer)

    bool active() const { return m_keyed || m_deviceBusy; }

    // The hold starts only when the Core's and the device's state are both
    // false after either was true; either one true again ends it.
    void changed(bool wasActive)
    {
        if (active()) {
            m_holding = false;
            m_holdTimer.stop();
        } else if (wasActive) {
            m_holding = true;
            m_holdTimer.start(m_holdMs);
        }
        apply();
    }

    // An unchanged interval leaves the timer alone, so a repeated status
    // never pushes the next poll back.
    void apply()
    {
        const int interval = fast() ? m_keyedMs : m_receiveMs;
        if (m_pollTimer->interval() == interval) {
            return;
        }
        if (m_pollTimer->isActive()) {
            // Restart: the new interval applies now, not after the rest of
            // the tick under way.
            m_pollTimer->start(interval);
        } else {
            m_pollTimer->setInterval(interval);
        }
    }

    QTimer* m_pollTimer;
    int     m_receiveMs;
    int     m_keyedMs;
    int     m_holdMs{kHoldMs};
    bool    m_keyed{false};
    bool    m_deviceBusy{false};
    bool    m_holding{false};
    QTimer  m_holdTimer;
};

}  // namespace NereusSDR
