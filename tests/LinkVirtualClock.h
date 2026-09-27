#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// tests/LinkVirtualClock.h  (NereusSDR)
// =================================================================
//
// The session fixture player's virtual clock (LinkFixtures::runSession),
// moved out of LinkFixtures.cpp so its own tests can reach it.
//
// A virtual clock over every QTimer under a root object (the station
// server's heartbeat and delta flush timers, its grace timer, and each
// peer's handshake deadline, parented to that peer's transport).
// advance() fires them in virtual time order by emitting their timeout
// directly, exactly as a real expiry would: a single-shot timer stops
// first, a repeating one runs on.
//
// Those timers still run in real time too, and the fixtures count on it
// for one of them: a delta the 50 ms flush sends is awaited, in real time,
// without an advanceMs. What the player does between two awaited messages
// takes no virtual time, though, so while it holds the clock (Hold: a
// drain, a client's connect, advance() itself) their real expiries are
// swallowed. Over a data channel a client's connect takes real time (a
// DTLS and SCTP handshake), and the 50 ms flush fired in the middle of the
// step and split one coalesced delta into two.
//
// A timer's virtual due time is taken from its real remaining time when
// the clock first sees it, kept to its interval (a coarse timer, Qt's
// default, may report up to 5% more). A timer started again is seen by its
// real remaining time growing. That is judged on the real remaining time
// as reported, never the value kept to the interval: the kept value stays
// at the interval for as long as the report is above it (up to 9 s for a
// 180 s timer), so it read as a fresh start every time the clock looked,
// and each look pushed the due time forward again. A device's 180 s grace
// then never ended inside its advanceMs.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-27: moved from LinkFixtures.cpp; a restart is judged on the
//               real remaining time; real expiries are swallowed while the
//               clock is held (iPhone app plan Task 28 tail, R-IOS-16). J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>

#include <algorithm>
#include <functional>
#include <vector>

namespace NereusSDR::Test {

class LinkVirtualClock : public QObject {
public:
    /// A timer whose real remaining time grew by more than this since it
    /// was last seen was started again.
    static constexpr qint64 kRestartSlackMs = 20;

    /// `drain` runs the queued work between timers (the player's drain()).
    LinkVirtualClock(QObject* root, std::function<void()> drain)
        : m_root(root)
        , m_drain(std::move(drain))
    {
        if (QCoreApplication* app = QCoreApplication::instance()) {
            app->installEventFilter(this);
        }
    }

    ~LinkVirtualClock() override
    {
        if (QCoreApplication* app = QCoreApplication::instance()) {
            app->removeEventFilter(this);
        }
    }

    qint64 now() const { return m_now; }

    /// While one is alive, the real expiries of the timers under the root
    /// are swallowed: what runs in between takes no virtual time.
    class Hold {
    public:
        explicit Hold(LinkVirtualClock& clock)
            : m_clock(clock)
        {
            ++m_clock.m_holding;
        }
        ~Hold() { --m_clock.m_holding; }
        Hold(const Hold&) = delete;
        Hold& operator=(const Hold&) = delete;

    private:
        LinkVirtualClock& m_clock;
    };

    void scan()
    {
        m_tracked.erase(std::remove_if(m_tracked.begin(), m_tracked.end(),
                                       [](const Tracked& t) {
                                           return t.timer.isNull() || !t.timer->isActive();
                                       }),
                        m_tracked.end());
        if (m_root.isNull()) {
            return;
        }
        const QList<QTimer*> timers = m_root->findChildren<QTimer*>();
        for (QTimer* timer : timers) {
            if (!timer->isActive()) {
                continue;
            }
            const qint64 reported = std::max(0, timer->remainingTime());
            // The clock keeps to the interval (a coarse timer may report
            // up to 5% more).
            const qint64 kept = std::min<qint64>(reported, std::max(0, timer->interval()));
            auto it = std::find_if(m_tracked.begin(), m_tracked.end(),
                                   [timer](const Tracked& t) { return t.timer == timer; });
            if (it == m_tracked.end()) {
                Tracked t;
                t.timer = timer;
                t.due = m_now + kept;
                t.realRemaining = reported;
                t.since.start();
                m_tracked.push_back(t);
            } else if (reported > it->realRemaining - it->since.elapsed() + kRestartSlackMs) {
                it->due = m_now + kept;
                it->realRemaining = reported;
                it->since.restart();
            }
        }
    }

    /// Empty on success; a description when a timeout could not be fired.
    QString advance(qint64 ms)
    {
        const Hold hold(*this);
        const qint64 target = m_now + ms;
        for (int guard = 0; guard < 100000; ++guard) {
            runQueued();
            scan();
            auto next = std::min_element(m_tracked.begin(), m_tracked.end(),
                                         [](const Tracked& a, const Tracked& b) {
                                             return a.due < b.due;
                                         });
            if (next == m_tracked.end() || next->due > target) {
                break;
            }
            m_now = std::max(m_now, next->due);
            QPointer<QTimer> timer = next->timer;
            if (timer->isSingleShot()) {
                timer->stop();
                m_tracked.erase(next);
            } else {
                timer->start();
                next->due = m_now + std::max(1, timer->interval());
                next->realRemaining = std::max(0, timer->remainingTime());
                next->since.restart();
            }
            if (!QMetaObject::invokeMethod(timer, "timeout", Qt::DirectConnection)) {
                return QStringLiteral("could not fire a timer's timeout");
            }
        }
        m_now = target;
        runQueued();
        scan();
        return QString();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (m_holding == 0 || event->type() != QEvent::Timer) {
            return false;
        }
        auto* timer = qobject_cast<QTimer*>(watched);
        if (timer == nullptr || !isUnderRoot(timer)) {
            return false;
        }
        // A real expiry of a timer the clock fires. The underlying timer
        // runs on, so its next real expiry is one interval later: a
        // repeating period, not a start the scan should read.
        auto it = std::find_if(m_tracked.begin(), m_tracked.end(),
                               [timer](const Tracked& t) { return t.timer == timer; });
        if (it != m_tracked.end()) {
            it->realRemaining += std::max(0, timer->interval());
        }
        ++m_swallowed;
        return true;
    }

public:
    /// How many real expiries were swallowed (for the clock's own tests).
    int swallowedForTest() const { return m_swallowed; }

private:
    struct Tracked {
        QPointer<QTimer> timer;
        qint64 due = 0;
        qint64 realRemaining = 0;
        QElapsedTimer since;
    };

    bool isUnderRoot(const QObject* object) const
    {
        for (const QObject* o = object; o != nullptr; o = o->parent()) {
            if (o == m_root.data()) {
                return true;
            }
        }
        return false;
    }

    void runQueued()
    {
        if (m_drain) {
            m_drain();
        }
    }

    QPointer<QObject> m_root;
    std::function<void()> m_drain;
    qint64 m_now = 0;
    std::vector<Tracked> m_tracked;
    int m_swallowed = 0;
    int m_holding = 0;
};

} // namespace NereusSDR::Test
