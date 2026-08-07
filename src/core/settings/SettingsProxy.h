#pragma once
// =================================================================
// src/core/settings/SettingsProxy.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 15.
//
// The client-side half of the settings mirror. A remote-mode GUI installs
// one instance via AppSettings::instance().setRemoteBackend(&proxy);
// from that point on every Station-classified key (SettingsScope.h, Task
// 14) reads and writes through this class's own in-memory cache instead
// of the local NereusSDR.settings file, while every OperatorLocal key
// keeps flowing through AppSettings's ordinary local path completely
// untouched -- see ISettingsBackend.h for the delegation seam this
// implements and why the split happens per-key rather than "whichever
// store is installed handles everything."
//
// ---- Why the cache is NOT AppSettings's own m_settings map ----
//
// A tempting shortcut is: proxy the ENTIRE key space, including
// OperatorLocal keys, and just reuse AppSettings's own m_settings QMap
// as the cache (applySnapshot() merging straight into it). Rejected: a
// remote-mode GUI still wants ITS OWN window geometry, trace colours and
// local sound-card selection to persist locally across launches,
// independent of which station it happens to be connected to this
// session, and AppSettings::save() serialises m_settings wholesale to
// that GUI's own NereusSDR.settings file. If a station's ~2,900 keys
// (Step 5's measured snapshot scope) were merged into that same map,
// the NEXT save() -- and every debounced settings-save timer already
// wired throughout the app, none of which know about Role::Remote --
// would write every one of them into the operator's own local file,
// which then out-of-syncs the moment they connect to a DIFFERENT
// station, or reappears as stale ghost values on a later LOCAL-mode
// launch. AppSettings gains a SEPARATE, symmetric extraction method
// instead (AppSettings::snapshot(prefixes), used server-side by
// SettingsProxyServer::buildSnapshot() -- see that class) so the
// station's real values are read out of the DAEMON's own store, and this
// class's m_cache is the only place they are ever merged INTO on the
// client side: purely in memory, never touched by AppSettings::save().
//
// ---- The three-state read ----
//
// A key this proxy handles (handlesKey() below) resolves through
// value()/contains() to one of three states, tracked by two members:
//
//   1. CACHE HIT       -- m_cache has a real value (from a snapshot, or
//                          from this client's own optimistic write).
//   2. PROVEN UNSET     -- no entry in m_cache, but at least one snapshot
//                          HAS been applied (m_snapshotEverApplied), so
//                          the absence is read as "the daemon's own
//                          AppSettings::value() would ALSO return the
//                          caller's default for this key" -- the same
//                          shape of fact hardwareValue()'s own bare-
//                          default fallback represents on the daemon
//                          side. Recorded into m_provenUnset lazily, on
//                          first read, purely for diagnostics (Step 9's
//                          log -- see below) and for tests
//                          (provenUnsetKeys()); it never changes what
//                          value() RETURNS, which is `defaultValue`
//                          either way.
//   3. NO SNAPSHOT YET  -- no entry in m_cache and NO snapshot has ever
//                          been applied. Still returns `defaultValue`
//                          (there is nothing else a synchronous,
//                          network-free read could do -- see
//                          ISettingsBackend.h's invariant), but is a
//                          DIFFERENT fact worth telling apart in the log:
//                          "we don't know yet" rather than "we asked and
//                          the answer was no."
//
// All three states satisfy "value() returns the caller's default for an
// unwritten key" (AppSettings.h's own contract, Task 13's
// unwrittenKeyStillReturnsCallerDefault, and the controller notes' own
// Region/"United States" example) -- the distinction exists purely for
// Step 9's log and does not change any return value.
//
// ---- Step 9: the proxied-read log ----
//
// Every call into value() that this backend actually answers (i.e. every
// call AppSettings delegated here via handlesKey()) logs one line via
// qCDebug(lcSettingsProxy), unconditionally, with the literal, greppable
// first token "proxied-read" so `grep proxied-read <log>` produces
// Step 9's scannable list. Exact shape (space-separated qCDebug streaming,
// one line per read):
//
//   proxied-read <key> outcome=CacheHit value="<value>"
//   proxied-read <key> outcome=ProvenUnset default="<defaultValue>"
//   proxied-read <key> outcome=NoSnapshotYet default="<defaultValue>"
//
// tests/tst_settings_proxy.cpp's proxiedReadLogHasScannableShape case
// installs a message handler and greps for exactly this.
//
// ---- Optimistic writes, origin tags, and rejection ----
//
// setValue() (ISettingsBackend) updates m_cache immediately -- BEFORE any
// daemon confirmation -- so a Setup control reads back what the operator
// just set on its very next value() call, then (while ready()) emits
// outboundWriteRequested() for a live session (Task 18) to relay over
// the wire tagged with localOriginTag(). AppSettings has no signals of
// its own (it is not a QObject -- AppSettings.h's own class comment), so
// every notification this class needs to give a caller lives here.
//
// The origin tag is a SESSION identifier, not a per-write sequence
// number: Task 18 assigns this client's own session/connection id once
// (setLocalOriginTag()) and tags every write this client sends with it.
// The daemon (SettingsProxyServer) echoes that SAME tag back on the
// resulting outbound broadcast to EVERY connected client, including the
// one that sent it. applyRemoteValue() below applies the incoming value
// to m_cache regardless of whose tag it carries (the daemon's report is,
// by definition, the current truth), but a caller with a live
// slider-drag in progress can compare `originTag == localOriginTag()`
// itself to decide "this is my own echo, I already have this" versus "a
// third party (or the daemon itself) changed this" without this class
// needing to guess at UI intent it has no visibility into. This class
// does not attempt to solve out-of-order redelivery across MULTIPLE
// in-flight writes from the SAME client (e.g. a rapid slider drag
// producing several writes before any confirmation returns); the
// transport is a single ordered reliable channel (R2 design addendum
// section 7.3's "Control" envelope), so writes and their confirmations
// arrive in the order they were sent, which is what keeps "same tag,
// last write" sufficient without a sequence number.
//
// Rejection (Step 6) reverts m_cache to whatever the daemon reports as
// the restored value (an invalid QVariant means "the daemon has nothing
// for this key either" -- proven-unset, not merely reset to empty
// string) and emits valueRejected() unconditionally, so a Setup widget
// can revert its own displayed value.
//
// ---- Offline behaviour (Step 8) ----
//
// ready() gates the OUTBOUND side only: while false, setValue()/remove()
// still update m_cache (so the UI stays interactive and consistent
// during an outage) but do NOT emit outboundWriteRequested()/
// outboundRemoveRequested() -- there is no live session to send them
// over, and Step 8 is explicit that a dropped write must not be queued
// for later replay (the daemon's own store can move underneath a queued
// write between now and reconnect; blind replay would silently revert
// someone else's change). Reconnect is Task 19's job: it re-establishes
// ready() and calls applySnapshot() again with a fresh snapshot, which
// -- being a MERGE, not a replace -- naturally supersedes whatever this
// client held locally for every key the fresh snapshot covers, and Task
// 19 owns comparing before/after to report what a caller's offline edits
// did not survive.
//
// ---- The Setup-dialog gate (Step 7) ----
//
// setupDialogAllowed() is the single predicate a caller (Task 20) checks
// before constructing SetupDialog. ready() alone is NOT sufficient: Task
// 1's daemon profile starts genuinely empty, so a freshly-reserved
// `nereusd --profile daemon` reports ready() (the handshake completed)
// with zero station settings for a Setup page to show. Without the
// second condition, 187 widget constructors would each read their
// AppSettings default, and the FIRST interaction with any one of them
// would write that ship default into the station store as if the
// operator had chosen it. AppSettings::kDaemonProfileSeededKey (Task 1)
// is what tells "empty because fresh" apart from "empty because
// something is broken": Task 1's seedDaemonProfileMarker() writes it
// unconditionally, idempotently, on every daemon startup, and
// SettingsProxyServer::buildSnapshot() includes it explicitly whenever
// present (see that class's comment) specifically so it survives even a
// snapshot that is otherwise completely empty. handlesKey() below
// carries the matching special case on the read side, because the key
// itself does not classify Station under SettingsScope.h's rules (it is
// not a "setting" in that sense at all -- it is this protocol's own
// bookkeeping) and would otherwise never reach this cache.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06  J.J. Boyd / KG4VCF  Remote daemon R2 Task 15: client-side
//                                    settings proxy. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "core/settings/ISettingsBackend.h"

namespace NereusSDR {

class SettingsProxy : public QObject, public ISettingsBackend {
    Q_OBJECT

public:
    explicit SettingsProxy(QObject* parent = nullptr);
    ~SettingsProxy() override = default;

    SettingsProxy(const SettingsProxy&) = delete;
    SettingsProxy& operator=(const SettingsProxy&) = delete;

    // ---- ISettingsBackend ----
    bool handlesKey(const QString& key) const override;
    QVariant value(const QString& key, const QVariant& defaultValue) const override;
    void setValue(const QString& key, const QVariant& val) override;
    bool contains(const QString& key) const override;
    void remove(const QString& key) override;
    QStringList handledKeys() const override;

    // ---- Session identity (Task 18 sets this once it knows) ----
    void setLocalOriginTag(const QString& tag);
    QString localOriginTag() const { return m_localOriginTag; }

    // ---- Handshake / link state ----
    // False until Task 18's session completes its handshake (or after a
    // link drop, until reconnect completes again). Gates the OUTBOUND
    // side of writes/removes only -- see the class comment's "Offline
    // behaviour" paragraph. Reads always keep serving the cache
    // regardless of this flag, which is what "reads never touch the
    // network" (ISettingsBackend.h) means in practice: there is no
    // "ready" branch in value() at all.
    void setReady(bool ready);
    bool ready() const { return m_ready; }

    // ---- Connect-time (and reconnect-time) snapshot ingestion ----
    // Merges `data` into m_cache -- NEVER replaces it (Step 3: a second,
    // narrower-scoped snapshot must not blow away keys outside its own
    // scope). Marks m_snapshotEverApplied even when `data` is empty (an
    // empty snapshot is real information: see setupDialogAllowed()).
    // Clears any m_provenUnset entry a newly-arrived real value
    // contradicts.
    void applySnapshot(const QMap<QString, QString>& data);

    bool hasReceivedSnapshot() const { return m_snapshotEverApplied; }

    // True once at least one snapshot has been applied AND it carried at
    // least one key besides AppSettings::kDaemonProfileSeededKey. See
    // the class comment: the seed marker alone does not count as "real"
    // station content, which is exactly what lets the OR-fallback in
    // setupDialogAllowed() below do anything.
    bool hasNonEmptySnapshot() const;

    // Step 7's Setup-dialog gate: ready() AND (hasNonEmptySnapshot() OR
    // the seed marker is present). See the class comment.
    bool setupDialogAllowed() const;

    // ---- Inbound from the daemon (Task 18 calls these per decoded wire
    // message) ----

    // A settings value the daemon reports as current for `key`, whether
    // a genuine third-party/daemon-local change or the echo of this
    // client's own write (see the class comment's origin-tag paragraph).
    // Applied to m_cache unconditionally.
    void applyRemoteValue(const QString& key, const QVariant& value, const QString& originTag);

    // The daemon rejected a write this client sent for `key`.
    // `restoredValue`, if valid, becomes the new cached value; an
    // invalid QVariant means the daemon has nothing for this key either
    // (reverts to proven-unset, not to an empty string). Always emits
    // valueRejected().
    void applyRejection(const QString& key, const QVariant& restoredValue);

    // ---- Test / diagnostic introspection ----
    QSet<QString> provenUnsetKeys() const { return m_provenUnset; }
    int cacheSize() const { return m_cache.size(); }

signals:
    /// setValue() produced a NEW outbound write to relay, while ready().
    /// Never fired while !ready() (Step 8: dropped, not queued).
    void outboundWriteRequested(const QString& key, const QVariant& value);

    /// remove() produced a new outbound removal to relay, while ready().
    void outboundRemoveRequested(const QString& key);

    /// The daemon rejected one of this client's writes. See
    /// applyRejection().
    void valueRejected(const QString& key, const QVariant& restored);

    /// A snapshot (connect-time or reconnect) was applied. `keyCount` is
    /// data.size() from THIS call, not the cache total.
    void snapshotApplied(int keyCount);

private:
    void logProxiedRead(const QString& key, const QString& outcome,
                        const QVariant& valueOrDefault) const;

    QMap<QString, QString> m_cache;

    /// See the class comment's "three-state read". Mutable: populated
    /// lazily from value(), a const method, exactly like AppSettings's
    /// own const accessors reading a mutable cache is not needed for
    /// (AppSettings has no such laziness) but this class does because the
    /// set is diagnostic-only and must not change what any const call
    /// returns.
    mutable QSet<QString> m_provenUnset;

    bool m_ready = false;
    bool m_snapshotEverApplied = false;
    QString m_localOriginTag;
};

} // namespace NereusSDR
