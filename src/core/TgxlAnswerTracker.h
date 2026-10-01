// src/core/TgxlAnswerTracker.h (NereusSDR)
//
// What this computer has sent the Tuner Genius XL that the tuner may answer
// with its own `transmit tune on` / `transmit tune off` on the SmartSDR API
// port, so that an answer is never taken for the tuner's front-panel TUNE
// (ruling 8.9b, design doc 2026-09-24-several-devices-on-one-core-design.md).
//
// no-port-check: NereusSDR-original file. The tuner behaviour it models is
// read from captures, not ported from any upstream source:
//   - captures/flex-tgxl-direct-CONTROL.pcapng: `C27|autotune` at T+172.199,
//     `S0|state ... tuning=1` at T+172.201, then `C7|transmit tune on` and
//     `C6|interlock ready 3` together at T+172.702 (503 ms after the send).
//   - commit 01ca5b824 item (8), bench 2026-05-20: the tuner answers our
//     `transmit tune=1` broadcast with its own `transmit tune on`.
//
// Modification history (NereusSDR):
//   2026-10-01 - Created for the TGXL tune lane (Job B round 2, I-A and
//                m-A). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                Claude Code.
//
// One entry per thing sent, counted, never a single flag:
//   - an `autotune` (by its sequence number) expects a tune on;
//   - a `transmit tune=1` broadcast expects a tune on (the echo);
//   - a `transmit tune=0` broadcast expects a tune off (the echo).
// A tune on from the tuner first answers the oldest entry that expects one;
// only a tune on that answers nothing is a press. An entry leaves when:
//   - its answer arrives;
//   - the tuner rejects that `autotune` (its own sequence only);
//   - the tuner's sweep for it has started (tuning 0 to 1 after the send)
//     and then ends (tuning 1 to 0, or a tune off that is not an echo);
//   - it is older than kAnswerWindowMs.
// Nothing else clears it: a link drop and reconnect does not, since the
// answer comes on the SmartSDR API port, not the dropped :9010 link.
// Every way this can be wrong blocks a press for at most the window; none
// lets an answer count as a press.

#pragma once

#include <QtGlobal>

#include <vector>

namespace NereusSDR {

class TgxlAnswerTracker
{
public:
    /// How long an entry waits for its answer. The capture shows the
    /// tuner answering an `autotune` in 503 ms; the Core already gives the
    /// tuner 3 s to start a sweep once the carrier is up
    /// (RadioModel::kTgxlDeviceCycleStartMs, TunerApplet's short watchdog),
    /// so an answer later than that is not waited for.
    static constexpr qint64 kAnswerWindowMs = 3000;

    void autotuneSent(quint32 seq, qint64 nowMs)
    {
        m_entries.push_back({Kind::Autotune, seq, nowMs, false});
    }

    void autotuneRejected(quint32 seq)
    {
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->kind == Kind::Autotune && it->seq == seq) {
                m_entries.erase(it);
                return;
            }
        }
    }

    /// The Core broadcast `transmit tune=<on>` to the SmartSDR API clients
    /// while the tuner was connected.
    void tuneBroadcast(bool on, qint64 nowMs)
    {
        m_entries.push_back({on ? Kind::TuneOnEcho : Kind::TuneOffEcho, 0, nowMs, false});
    }

    void tuningChanged(bool tuning, qint64 nowMs)
    {
        expire(nowMs);
        if (tuning) {
            for (Entry& e : m_entries) {
                if (e.kind == Kind::Autotune) {
                    e.sweepStarted = true;
                }
            }
            return;
        }
        endStartedSweeps();
    }

    /// The tuner sent `transmit tune on`. True when it answers something
    /// this computer sent (the entry is used up); false when it answers
    /// nothing, which is the tuner's own TUNE.
    bool tuneOnAnswers(qint64 nowMs)
    {
        expire(nowMs);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->kind == Kind::Autotune || it->kind == Kind::TuneOnEcho) {
                m_entries.erase(it);
                return true;
            }
        }
        return false;
    }

    /// The tuner sent `transmit tune off`: the echo of a tune=0 broadcast
    /// if one is waiting, else the tuner letting go of a sweep it started.
    void tuneOff(qint64 nowMs)
    {
        expire(nowMs);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            if (it->kind == Kind::TuneOffEcho) {
                m_entries.erase(it);
                return;
            }
        }
        endStartedSweeps();
    }

    /// Entries still waiting for a tune on.
    int awaitingTuneOn(qint64 nowMs)
    {
        expire(nowMs);
        int n = 0;
        for (const Entry& e : m_entries) {
            if (e.kind != Kind::TuneOffEcho) {
                ++n;
            }
        }
        return n;
    }

private:
    enum class Kind { Autotune, TuneOnEcho, TuneOffEcho };
    struct Entry {
        Kind kind;
        quint32 seq;
        qint64 sentMs;
        bool sweepStarted;
    };

    void expire(qint64 nowMs)
    {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (nowMs - it->sentMs > kAnswerWindowMs) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    void endStartedSweeps()
    {
        for (auto it = m_entries.begin(); it != m_entries.end();) {
            if (it->kind == Kind::Autotune && it->sweepStarted) {
                it = m_entries.erase(it);
            } else {
                ++it;
            }
        }
    }

    std::vector<Entry> m_entries;
};

} // namespace NereusSDR
