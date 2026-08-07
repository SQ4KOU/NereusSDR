// =================================================================
// src/core/settings/SettingsProxy.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 15.
//
// See SettingsProxy.h for the full design.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06  J.J. Boyd / KG4VCF  Remote daemon R2 Task 15: client-side
//                                    settings proxy. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/settings/SettingsProxy.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsScope.h"

#include <QLoggingCategory>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcSettingsProxy, "nereus.settingsproxy")
} // namespace

SettingsProxy::SettingsProxy(QObject* parent)
    : QObject(parent)
{
}

bool SettingsProxy::handlesKey(const QString& key) const
{
    // R2's own connect-time bookkeeping marker (AppSettings.h's doc
    // comment on kDaemonProfileSeededKey) is not a "setting" in
    // classifySettingsKey()'s sense at all -- no rule there names it, on
    // purpose, since it is protocol state rather than a station fact.
    // Special-cased here so it still reaches this cache (via
    // applySnapshot()/SettingsProxyServer::buildSnapshot(), which
    // includes it unconditionally) rather than falling through to
    // AppSettings's own local m_settings map, where a freshly-launched
    // remote-mode GUI would never find it. See SettingsProxy.h's
    // "Setup-dialog gate" paragraph.
    if (key == QLatin1String(AppSettings::kDaemonProfileSeededKey)) {
        return true;
    }
    return classifySettingsKey(key) == SettingsScope::Station;
}

QVariant SettingsProxy::value(const QString& key, const QVariant& defaultValue) const
{
    auto it = m_cache.constFind(key);
    if (it != m_cache.constEnd()) {
        logProxiedRead(key, QStringLiteral("CacheHit"), QVariant(it.value()));
        return QVariant(it.value());
    }
    if (m_snapshotEverApplied) {
        // See the class comment's "three-state read": absence after at
        // least one snapshot has landed reads as "the daemon's own
        // AppSettings::value() would also return the caller's default
        // for this key". Does not change the return value -- only what
        // gets logged/recorded -- but m_provenUnset is real state a test
        // (and Step 9's log) can inspect.
        m_provenUnset.insert(key);
        logProxiedRead(key, QStringLiteral("ProvenUnset"), defaultValue);
    } else {
        logProxiedRead(key, QStringLiteral("NoSnapshotYet"), defaultValue);
    }
    return defaultValue;
}

void SettingsProxy::setValue(const QString& key, const QVariant& val)
{
    m_cache.insert(key, val.toString());
    m_provenUnset.remove(key);
    if (m_ready) {
        emit outboundWriteRequested(key, val);
    }
    // See the class comment's "Offline behaviour" paragraph: while
    // !m_ready, the cache above is still updated (the UI stays
    // consistent) but nothing is emitted -- a dropped write, not a
    // queued one.
}

bool SettingsProxy::contains(const QString& key) const
{
    return m_cache.contains(key);
}

void SettingsProxy::remove(const QString& key)
{
    m_cache.remove(key);
    m_provenUnset.insert(key);
    if (m_ready) {
        emit outboundRemoveRequested(key);
    }
}

QStringList SettingsProxy::handledKeys() const
{
    return m_cache.keys();
}

void SettingsProxy::setLocalOriginTag(const QString& tag)
{
    m_localOriginTag = tag;
}

void SettingsProxy::setReady(bool ready)
{
    m_ready = ready;
}

void SettingsProxy::applySnapshot(const QMap<QString, QString>& data)
{
    // Merge, never replace -- see the class comment's "Why the cache is
    // NOT AppSettings's own m_settings map" and the R2 Task 15 brief's
    // own Step 3. A key this snapshot reports real content for
    // supersedes anything m_provenUnset previously recorded for it (the
    // daemon evidently has it now, whatever this cache believed before).
    for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
        m_cache.insert(it.key(), it.value());
        m_provenUnset.remove(it.key());
    }
    m_snapshotEverApplied = true;
    emit snapshotApplied(data.size());
}

bool SettingsProxy::hasNonEmptySnapshot() const
{
    if (m_cache.isEmpty()) {
        return false;
    }
    // The seed marker alone does not count as "real" station content --
    // see the class comment and setupDialogAllowed()'s own doc comment.
    // Without this carve-out, EVERY snapshot (which always carries the
    // marker once the daemon has seeded it) would trivially satisfy
    // "non-empty" and the OR-fallback below would never do anything.
    if (m_cache.size() == 1 &&
        m_cache.contains(QLatin1String(AppSettings::kDaemonProfileSeededKey))) {
        return false;
    }
    return true;
}

bool SettingsProxy::setupDialogAllowed() const
{
    if (!m_ready) {
        return false;
    }
    if (hasNonEmptySnapshot()) {
        return true;
    }
    return m_cache.contains(QLatin1String(AppSettings::kDaemonProfileSeededKey));
}

void SettingsProxy::applyRemoteValue(const QString& key, const QVariant& value, const QString& originTag)
{
    Q_UNUSED(originTag); // see the class comment's origin-tag paragraph: applied unconditionally here
    m_cache.insert(key, value.toString());
    m_provenUnset.remove(key);
}

void SettingsProxy::applyRejection(const QString& key, const QVariant& restoredValue)
{
    if (restoredValue.isValid()) {
        m_cache.insert(key, restoredValue.toString());
        m_provenUnset.remove(key);
    } else {
        // The daemon has nothing for this key either -- revert to
        // proven-unset, not to an empty string (see the class comment).
        m_cache.remove(key);
        m_provenUnset.insert(key);
    }
    emit valueRejected(key, restoredValue);
}

void SettingsProxy::logProxiedRead(const QString& key, const QString& outcome,
                                   const QVariant& valueOrDefault) const
{
    // Step 9: unconditional, one line per proxied read, literal
    // "proxied-read" first token so `grep proxied-read <log>` produces
    // Task 20's scannable list. Built as a single QString and logged via
    // .noquote() rather than streamed token-by-token through QDebug's
    // default operator<<, which would wrap each QString/QVariant in
    // its own quotes and print a QVariant via its verbose debug
    // representation (QVariant(QString, "...")), not the bare value --
    // neither is what a scannable, greppable line needs. See the class
    // comment for the exact shape and
    // tests/tst_settings_proxy.cpp's proxiedReadLogHasScannableShape for
    // the pinned format.
    const QString label = (outcome == QStringLiteral("CacheHit"))
        ? QStringLiteral("value")
        : QStringLiteral("default");
    qCDebug(lcSettingsProxy).noquote()
        << QStringLiteral("proxied-read %1 outcome=%2 %3=\"%4\"")
               .arg(key, outcome, label, valueOrDefault.toString());
}

} // namespace NereusSDR
