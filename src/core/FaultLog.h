// =================================================================
// src/core/FaultLog.h  (NereusSDR)
// =================================================================
//
// NereusSDR-native ring buffer of the last 10 fault events per device,
// persisted to AppSettings as a JSON array. Consumed by the PGXL
// advanced setup page and any future fault-history surface.
//
// Design reference: docs/architecture/2026-05-18-pgxl-tgxl-and-analog-smeter-design.md §4.7
//
// AI tooling: Anthropic Claude Code.

#pragma once

#include <QObject>
#include <QString>
#include <QVector>

namespace NereusSDR {

/// Single fault event captured when a PGXL (or TGXL) state transitions
/// to a value beginning with "FAULT".
struct FaultEvent {
    qint64  whenMs;          // epoch ms at capture time
    QString state;           // e.g. "FAULT" or "FAULT_PROTECT"
    float   fwdAtFaultW;     // forward power (watts) at fault transition
    float   swrAtFault;      // SWR at fault transition
    float   tempAtFaultC;    // temperature (Celsius) at fault transition
    QString likelyCause;     // heuristic: "SWR trip", "Overtemp", "Drive too high", "Unknown"
};

/// Ring buffer of the last 10 fault events for a single device, keyed by an
/// AppSettings device key (e.g. "PGXL_FaultHistory" or "TGXL_FaultHistory").
///
/// The buffer is newest-first: `events().first()` is the most recent fault.
/// Persistence uses AppSettings as a compact JSON string so no separate file
/// is needed.
class FaultLog : public QObject {
    Q_OBJECT
public:
    explicit FaultLog(const QString& deviceKey, QObject* parent = nullptr);

    /// Returns all events, newest first. Size is at most 10.
    QVector<FaultEvent> events() const;

    /// Prepend ev to the ring buffer. If size exceeds 10 after the prepend,
    /// the oldest entry (tail) is evicted. Persists and emits changed().
    void capture(const FaultEvent& ev);

    /// Remove all events and persist the empty state. Emits changed().
    void clear();

    // ------------------------------------------------------------------
    // Remote Daemon R2, Task 15 -- re-read after a settings snapshot.
    //
    // The constructor's private load() (below) runs exactly once, at
    // construction, and reads whatever AppSettings::instance().value()
    // returns for m_deviceKey RIGHT NOW. On a remote-mode GUI that is a
    // real problem: RadioModel (and everything it owns, including
    // whichever TunerModel/AmpApplet path owns a FaultLog instance) is
    // constructed SYNCHRONOUSLY at GUI launch -- the R2 design addendum
    // section 2's own worked example is explicit that "the GUI launches
    // with --station wss://localhost:PORT and constructs a RadioModel
    // that never calls connectToRadio" -- while the connect-time
    // settings snapshot (SettingsProxy::applySnapshot(),
    // src/core/settings/SettingsProxy.h) arrives ASYNCHRONOUSLY, later,
    // once Task 18's wss handshake completes. A FaultLog constructed
    // before that snapshot lands calls the private load() against an
    // empty client-side cache and finds nothing -- forever, since
    // nothing re-reads it afterward. Task 15's own delegation seam
    // (AppSettings::setRemoteBackend()) already makes a FRESH read of
    // "PGXL_FaultHistory"/"TGXL_FaultHistory" (both classify Station --
    // SettingsScope.cpp's "PGXL_"/"TGXL_" prefix rules) return the real
    // data once a snapshot has landed; what was missing was a way to
    // TRIGGER that fresh read on an object that already exists.
    //
    // reload() is that trigger: re-runs load() against whatever
    // AppSettings::instance().value() returns AT THE TIME reload() IS
    // CALLED (which, through the same delegation seam, is the
    // SettingsProxy cache on a remote-mode GUI), then emits changed() --
    // unlike the constructor's own load() call, which never emits, since
    // nothing could legitimately be observing an object mid-construction.
    //
    // STATED ORDERING (Task 15 brief, Step 10): a FaultLog instance is
    // ALWAYS constructed before the first connect-time snapshot can
    // possibly have landed, for the RadioModel-construction-timing
    // reason above. reload() must therefore be called, on each
    // already-existing FaultLog instance, STRICTLY AFTER
    // SettingsProxy::snapshotApplied() first fires for that session.
    // Task 15 does not call reload() from anywhere in this task's own
    // files: doing so requires knowing where a live session's FaultLog
    // instances actually live in the RadioModel/TunerModel object graph,
    // which src/core/settings/ has no visibility into, and requires an
    // actual session object to hang the "first snapshot has landed" call
    // off of, which does not exist until Task 18 builds one. This
    // matches the pattern this same plan already uses elsewhere for
    // enabling-but-not-yet-wired capability (StateMirror.h's Task 7:
    // "nothing constructs a StateMirror in production yet";
    // SessionCommandDispatcher.cpp's Task 11: "No production caller yet
    // -- Task 18 feeds a real wss session's inbound bytes into
    // dispatch()"). Making load() reachable from outside the constructor
    // is the whole of what Task 15 owns here; wiring the call is Task
    // 18's (or a later task's) job once the session it depends on
    // exists.
    // ------------------------------------------------------------------
    void reload();

    /// Heuristic: derive a human-readable cause from the telemetry values
    /// recorded at fault time. Priority order: SWR -> temp -> fwd -> unknown.
    /// No instance state is needed, so this is a static helper.
    static QString likelyCauseFor(float fwd, float swr, float temp);

signals:
    void changed();

private:
    void load();
    void save() const;

    QString             m_deviceKey;
    QVector<FaultEvent> m_events;    // newest-first; capped at 10
};

}  // namespace NereusSDR
