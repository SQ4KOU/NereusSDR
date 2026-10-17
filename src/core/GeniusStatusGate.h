// src/core/GeniusStatusGate.h (NereusSDR)
//
// One `status` request in flight on a Power Genius XL or Tuner Genius XL
// link. A `status` is not sent while the one before it is unanswered; after
// kReplyWaitMs the answer counts as lost and the next one goes, so a
// dropped reply never silences the meters. Nothing is queued: the next poll
// asks.
//
// no-port-check: NereusSDR-original file. What it rests on:
//   - Bench, Power Genius XL firmware 3.9.9, 2026-10-12: four closes of the
//     link in twelve minutes, each 30 to 175 ms after a `status` sent while
//     the one before it was unanswered.
//   - captures/flex-pgxl-tgxl-capture_00001_20260519173452.pcapng: the
//     vendor's own programs never do that. PowerGeniusDesktop sent 4,604
//     `status` on TCP 9008 (one every 124 ms) and TunerGeniusDesktop 9,598
//     on TCP 9010 (one every 62 ms), none while an answer was outstanding.
//   - The amplifier answers `status` with `R<seq>|0|...`, the tuner with
//     `S<seq>|status ...` (captures/flex-tgxl-direct-NOTES.md:73); both echo
//     the sequence number, which is what answered() matches.
//   - AetherSDR has the same rule for both devices (pollStatus,
//     m_pollInFlight, kPollStaleMs = 1000, src/core/PgxlConnection.cpp and
//     src/core/TgxlConnection.cpp [@a9a83e66], added in c2dfa9e1). This is
//     not a port of it: here the answer is matched by sequence number and
//     every sender of `status` waits, not only the poll.
//
// Modification history (NereusSDR):
//   2026-10-12 - Created; shared by PgxlConnection and TgxlConnection.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#pragma once

#include <QElapsedTimer>
#include <QtGlobal>

namespace NereusSDR {

class GeniusStatusGate {
public:
    // How long an unanswered `status` holds back the next one. The slowest
    // answers seen: 0.26 s (amplifier, bench 2026-10-12) and 0.65 s (tuner,
    // captures/flex-tgxl-direct-CONTROL.pcapng).
    static constexpr int kReplyWaitMs = 2000;

    // True while the `status` last sent has neither been answered nor
    // waited out.
    bool answerPending() const
    {
        return m_inFlightSeq != 0 && m_sinceSent.isValid()
            && m_sinceSent.elapsed() < m_replyWaitMs;
    }

    // A `status` with this sequence number was written to the link.
    void sent(quint32 seq)
    {
        m_inFlightSeq = seq;
        m_sinceSent.start();
    }

    // The device answered this sequence number, whatever the answer says.
    void answered(quint32 seq)
    {
        if (seq == m_inFlightSeq) {
            m_inFlightSeq = 0;
        }
    }

    // A new connection: a `status` the last one never answered holds
    // nothing back.
    void reset() { m_inFlightSeq = 0; }

    // Test-only.
    void setReplyWaitMsForTesting(int ms) { m_replyWaitMs = ms > 0 ? ms : 1; }

private:
    int           m_replyWaitMs{kReplyWaitMs};
    quint32       m_inFlightSeq{0};
    QElapsedTimer m_sinceSent;
};

}  // namespace NereusSDR
