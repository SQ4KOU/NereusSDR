// =================================================================
// src/core/settings/SettingsProxyServer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 15.
//
// See SettingsProxyServer.h for the full design.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06  J.J. Boyd / KG4VCF  Remote daemon R2 Task 15: daemon-side
//                                    settings snapshot + inbound-apply
//                                    server. AI-assisted transformation
//                                    via Anthropic Claude Code.
// =================================================================

#include "core/settings/SettingsProxyServer.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsScope.h"

namespace NereusSDR {

SettingsProxyServer::SettingsProxyServer(AppSettings& appSettings, QObject* parent)
    : QObject(parent)
    , m_appSettings(appSettings)
{
    // See the class comment: this is the ONLY place this instance's
    // change-hook observation is installed. AppSettings::setChangeHook()
    // REPLACES whatever hook was there, so constructing a second
    // SettingsProxyServer on the same AppSettings instance silently
    // steals observation from the first -- acceptable for R2 (one
    // daemon, one settings store, one server), flagged here so a future
    // reader does not assume multiple observers compose.
    m_appSettings.setChangeHook([this](const QString& key) { onLocalAppSettingsChange(key); });
}

SettingsProxyServer::~SettingsProxyServer()
{
    // Clear the hook so a destroyed SettingsProxyServer never leaves a
    // dangling `this` captured in a std::function on an AppSettings
    // instance that outlives it (AppSettings::instance() is a
    // function-local static with effectively unbounded lifetime -- see
    // the class comment).
    m_appSettings.setChangeHook(nullptr);
}

QMap<QString, QString> SettingsProxyServer::buildSnapshot(const QString& connectedMac) const
{
    // Part 1: the hardware/ subtree, MAC-scoped. hardware/ is ~92% of a
    // real settings file (R2 design addendum section 8) and is
    // inherently per-MAC, so this is handled as a dedicated prefix
    // extraction via AppSettings::snapshot() rather than folded into the
    // classifySettingsKey() scan below, which has no notion of "which
    // MAC is connected" and must not be given one (see
    // classifySettingsKey()'s own header comment).
    QStringList hwPrefixes;
    if (!connectedMac.isEmpty()) {
        hwPrefixes << QStringLiteral("hardware/%1/").arg(connectedMac);
    }
    hwPrefixes << QStringLiteral("hardware/oc/"); // a literal segment, not a MAC -- Task 14's own proof
    QMap<QString, QString> out = m_appSettings.snapshot(hwPrefixes);

    // Part 2: every OTHER Station-classified key, found by asking
    // classifySettingsKey() directly rather than re-deriving its private
    // prefix table here as a second, driftable copy.
    static const QString kHwPrefix = QStringLiteral("hardware/");
    const QStringList keys = m_appSettings.allKeys();
    for (const QString& key : keys) {
        if (key.startsWith(kHwPrefix)) {
            continue; // fully handled by part 1 above -- see that comment
        }
        if (classifySettingsKey(key) == SettingsScope::Station) {
            out.insert(key, m_appSettings.value(key).toString());
        }
    }

    // Part 3: the seed marker, unconditionally, whenever present. Not a
    // "setting" classifySettingsKey() has any rule for (by design -- see
    // SettingsProxy.h's "Setup-dialog gate" paragraph), so part 2 would
    // never pick it up on its own.
    static const QString kSeedKey = QLatin1String(AppSettings::kDaemonProfileSeededKey);
    if (m_appSettings.contains(kSeedKey)) {
        out.insert(kSeedKey, m_appSettings.value(kSeedKey).toString());
    }

    return out;
}

SettingsApplyResult SettingsProxyServer::applyInboundWrite(const QString& key, const QVariant& value,
                                                           const QString& originTag)
{
    if (classifySettingsKey(key) != SettingsScope::Station) {
        SettingsApplyResult result;
        result.accepted = false;
        result.reason = QStringLiteral("key is not Station-scoped");
        result.restoredValue = m_appSettings.value(key);
        return result;
    }

    // The anti-echo suppression this whole task exists to build. See the
    // class comment's "The hazard this class exists to not build"
    // section for the full mechanism: m_applyingInboundWrite is checked
    // FIRST in onLocalAppSettingsChange(), so the generic change-hook
    // path emits nothing for THIS write -- this method emits the one,
    // correctly-tagged broadcast for it explicitly, below.
    m_applyingInboundWrite = true;
    m_appSettings.setValue(key, value);
    m_applyingInboundWrite = false;

    emit outboundValueChanged(key, value, originTag);

    SettingsApplyResult result;
    result.accepted = true;
    return result;
}

void SettingsProxyServer::onLocalAppSettingsChange(const QString& key)
{
    if (m_applyingInboundWrite) {
        // Suppressed: applyInboundWrite() is already broadcasting this
        // EXACT change itself, explicitly, with the real origin tag.
        // Letting this generic path also fire would emit a second,
        // untagged, redundant broadcast for the identical change -- the
        // hazard Task 13's review named by shape before this task was
        // written. See the class comment.
        return;
    }
    if (classifySettingsKey(key) != SettingsScope::Station) {
        // An OperatorLocal key changed on the daemon (e.g. nereusd.conf-
        // derived local audio device selection) -- not a remote GUI's
        // concern, nothing to broadcast.
        return;
    }
    // A genuine local/daemon-side change: empty origin tag, since it is
    // nobody's echo (see SettingsProxy.h's origin-tag paragraph: an
    // empty tag can never equal a real session's own localOriginTag()).
    const QVariant value = m_appSettings.value(key);
    emit outboundValueChanged(key, value, QString());
}

} // namespace NereusSDR
