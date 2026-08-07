#pragma once
// =================================================================
// src/core/settings/SettingsProxyServer.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 15.
//
// The daemon-side half of the settings mirror. Unlike SettingsProxy
// (SettingsProxyServer's client-side counterpart), this class does NOT
// implement ISettingsBackend and is never installed via
// AppSettings::setRemoteBackend() -- the daemon's own AppSettings IS the
// station's real settings store, reading and writing its own local file
// exactly as it always has. This class WRAPS that AppSettings instance
// from the outside, through its existing public API plus the Task 13
// change hook, to do two jobs: hand a connecting client a bulk snapshot
// of everything Station-scoped, and apply writes a connected client
// sends back onto the real store.
//
// ---- The hazard this class exists to not build ----
//
// Task 13's own review named this task's most likely failure by shape,
// before it was written: an inbound remote write lands via
// AppSettings::setValue(), which fires the Task 13 change hook, which
// ships the SAME change straight back out to every connected client --
// including, redundantly, the one that just sent it -- as though it were
// a brand-new local change. AppSettings.h:187-198's contract is explicit
// that the fix is NOT a suppression flag inside AppSettings itself (that
// would silently swallow a hook body's own legitimate derived writes,
// trading a loud stack overflow for a quiet data-loss bug): the CONSUMER
// of the hook must suppress its own forwarding around an inbound apply.
// This class is that consumer, and applyInboundWrite() is where the
// suppression lives -- see its doc comment and m_applyingInboundWrite
// below.
//
// The two-path design that makes this work:
//   - applyInboundWrite(key, value, originTag) is the ONLY entry point
//     for a write that originated on a connected client (Task 18 calls
//     it once per decoded inbound write). It sets m_applyingInboundWrite,
//     calls AppSettings::setValue() (which still fires the Task 13
//     hook), clears the guard, and THEN emits outboundValueChanged()
//     itself, explicitly, WITH the real originTag it was given.
//   - onLocalAppSettingsChange(key) is installed once, in the
//     constructor, as the Task 13 change hook. It is the GENERIC path:
//     it fires for EVERY AppSettings mutation on this instance,
//     including the ones applyInboundWrite() itself just caused. Its
//     FIRST action is to check m_applyingInboundWrite and return
//     immediately if true -- suppressing exactly, and only, the
//     redundant SECOND (untagged) broadcast that path would otherwise
//     produce for the identical change applyInboundWrite() is already
//     broadcasting explicitly, correctly tagged, itself.
// A change that happens for any OTHER reason (a Setup page open directly
// on the daemon's own console, if this build ever grows one; a migration
// running at startup; nereusd.conf-derived seeding) reaches
// onLocalAppSettingsChange() with m_applyingInboundWrite false, and IS
// forwarded, with an EMPTY origin tag -- there is no client to attribute
// it to, and an empty tag can never equal a real session's
// SettingsProxy::localOriginTag(), so no client will ever mistake a
// genuine daemon-local change for its own echo.
//
// tests/tst_settings_proxy.cpp's serverSuppressesEchoOnInboundApply is
// the test that proves exactly one broadcast happens per
// applyInboundWrite() call, and this task's sabotage-and-revert pass
// (see the task report) temporarily removed the m_applyingInboundWrite
// guard specifically to confirm that test fails with spy.count() == 2,
// not some unrelated assertion, before restoring it.
//
// ---- Snapshot scope (Step 5) ----
//
// buildSnapshot(connectedMac) is NOT a blanket classifySettingsKey scan
// with no MAC awareness -- hardware/ is 92% of a real settings file (the
// R2 design addendum section 8) and is inherently PER-MAC, so a scan
// with no MAC filter would hand a client every OTHER saved radio's
// hardware state too. The method instead:
//   1. Uses AppSettings::snapshot() for exactly two prefixes:
//      "hardware/<connectedMac>/" and the literal "hardware/oc/" segment
//      (Step 5's own call-out: "oc" is a fixed literal some hardware/*
//      call sites use in the MAC position -- OcOutputsHfTab.cpp's
//      pennyExtCtrl is Task 14's own canonical proof -- not a MAC, and
//      must be included explicitly because it will never equal
//      `connectedMac`).
//   2. Scans AppSettings::allKeys() once for every OTHER key (skipping
//      anything starting with "hardware/", already fully handled by
//      step 1) and includes it iff classifySettingsKey() says Station.
//      This is the "use classifySettingsKey rather than re-deriving the
//      rule" requirement: Task 14's own prefix table is a private,
//      unexported implementation detail of SettingsScope.cpp, so the
//      only way to stay in sync with it without hand-copying it here is
//      to ask the function per key rather than guess its family list.
//   3. Includes AppSettings::kDaemonProfileSeededKey explicitly,
//      unconditionally, whenever AppSettings::instance().contains() says
//      it is present -- see SettingsProxy.h's own "Setup-dialog gate"
//      paragraph for why: it is not a Station "setting" in
//      classifySettingsKey's sense at all (no rule there names it), it
//      is this protocol's own connect-time bookkeeping, so step 2 would
//      never pick it up on its own.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06  J.J. Boyd / KG4VCF  Remote daemon R2 Task 15: daemon-side
//                                    settings snapshot + inbound-apply
//                                    server. AI-assisted transformation
//                                    via Anthropic Claude Code.
// =================================================================

#include <QMap>
#include <QObject>
#include <QString>
#include <QVariant>

namespace NereusSDR {

class AppSettings;

/// Outcome of one applyInboundWrite() call. Shape matches
/// StateMirror.h's MirrorApplyResult (accepted / reason) for the same
/// class of "was this write allowed" answer, plus a settings-specific
/// restoredValue for the rejection case.
struct SettingsApplyResult {
    /// True iff the write landed. False leaves the daemon's real
    /// AppSettings store exactly as it was.
    bool accepted = false;

    /// Empty iff accepted.
    QString reason;

    /// Meaningful only when !accepted: the daemon's OWN current value for
    /// `key` (invalid QVariant if the daemon has nothing for it either --
    /// proven-unset, not an empty string), for the caller to relay back
    /// to the offending client as SettingsProxy::applyRejection()'s
    /// `restoredValue`.
    QVariant restoredValue;
};

class SettingsProxyServer : public QObject {
    Q_OBJECT

public:
    /// `appSettings` is the daemon's OWN settings store -- normally
    /// AppSettings::instance() on a real nereusd, or an isolated
    /// AppSettings(tempPath) in a test. Non-owning; caller keeps it
    /// alive for at least this object's lifetime. Installs this
    /// instance's change hook on `appSettings` (see the class comment);
    /// the destructor clears it again so a SettingsProxyServer that
    /// outlives its own usefulness never leaves a dangling `this`
    /// captured in a std::function on an AppSettings instance that
    /// outlives it (AppSettings::instance() is a function-local static
    /// with effectively unbounded lifetime).
    ///
    /// PRECONDITION, matching StateMirror's own (StateMirror.h): this
    /// object, `appSettings`, and whatever calls applyInboundWrite() must
    /// all run on the SAME thread. AppSettings has no internal locking
    /// (AppSettings.h:200-203) and neither does this class.
    explicit SettingsProxyServer(AppSettings& appSettings, QObject* parent = nullptr);
    ~SettingsProxyServer() override;

    SettingsProxyServer(const SettingsProxyServer&) = delete;
    SettingsProxyServer& operator=(const SettingsProxyServer&) = delete;

    /// See the class comment's "Snapshot scope" section. `connectedMac`
    /// may be empty (no per-MAC hardware/ subtree is then included at
    /// all -- every other Station-scoped key, plus the seed marker, still
    /// is); a real caller always has a connected MAC by the time it asks
    /// for a snapshot, but an empty-MAC test fixture should not crash.
    QMap<QString, QString> buildSnapshot(const QString& connectedMac) const;

    /// Applies one inbound write a connected client sent for `key`. See
    /// the class comment for the anti-echo suppression this performs
    /// around the underlying AppSettings::setValue() call.
    ///
    /// Rejects (accepted=false, nothing changes) when `key` does not
    /// classify Station -- a well-behaved client's own SettingsProxy
    /// never offers a non-Station key over the wire in the first place
    /// (ISettingsBackend::handlesKey()), so reaching this path at all
    /// means a stale, buggy, or hostile client sent something outside
    /// the protocol; this is defense in depth, not the primary gate.
    /// Value-bounds / range validation for a key that IS Station-scoped
    /// is explicitly NOT this method's job -- that is SettingsHygiene's
    /// existing, separate responsibility (the R2 design addendum section
    /// 6.4 names it "the daemon's station-value validator"); duplicating
    /// it here would give the tree two places that disagree eventually.
    SettingsApplyResult applyInboundWrite(const QString& key, const QVariant& value,
                                          const QString& originTag);

signals:
    /// One Station-key change worth telling every connected client
    /// about: either a genuine local/daemon-side change (originTag
    /// empty), or the deliberate, explicitly-tagged echo of an
    /// applyInboundWrite() call (originTag is exactly what that call was
    /// given). Never fired twice for the same underlying setValue() call
    /// -- see the class comment.
    void outboundValueChanged(const QString& key, const QVariant& value, const QString& originTag);

private:
    /// Installed as m_appSettings's Task 13 change hook. See the class
    /// comment for the full anti-echo mechanism.
    void onLocalAppSettingsChange(const QString& key);

    AppSettings& m_appSettings;

    /// True for the duration of one applyInboundWrite() call. Checked
    /// first in onLocalAppSettingsChange(), before that method does
    /// anything else, so it suppresses the generic broadcast path for
    /// exactly the write currently in flight through applyInboundWrite()
    /// -- see the class comment's two-path explanation. Save/restore
    /// rather than hardcoded false on exit would only matter if
    /// applyInboundWrite() could re-enter itself (it cannot: it makes
    /// exactly one AppSettings::setValue() call and that call cannot
    /// synchronously re-invoke applyInboundWrite()), so a plain bool set
    /// true then false around the one call it guards is sufficient here,
    /// unlike StateMirror's m_applying (StateMirror.h), which DOES need
    /// save/restore because a command handler can nest a second
    /// applyInbound() inside the first.
    bool m_applyingInboundWrite = false;
};

} // namespace NereusSDR
