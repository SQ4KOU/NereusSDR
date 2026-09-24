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
//   2026-08-06  J.J. Boyd / KG4VCF  Fix round 1 (review): Important 3
//                                    (step-attenuator bounds check on
//                                    applyInboundWrite()), Minor 5
//                                    (QScopeGuard around
//                                    m_applyingInboundWrite), Minor 6
//                                    (both broadcast paths emit
//                                    m_appSettings.value(key), not the
//                                    caller's raw QVariant). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-06  J.J. Boyd / KG4VCF  Fix round 2 (review): Important 3a
//                                    (union range corrected to include
//                                    stepAttMaxDb()'s Alex-widened 61 dB
//                                    ceiling, not just the static
//                                    unwidened .attenuator table), 3b
//                                    (key matcher extended to the
//                                    rx1Band/<band> and txBand/<band>
//                                    siblings that actually win over
//                                    rx1Value on the load path). AI-
//                                    assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-06  J.J. Boyd / KG4VCF  Fix round 3 (review): Important 3 TX
//                                    half (the hardcoded 0 dB TX floor
//                                    rejected legitimate negative HL2
//                                    values from setAttOnTxValue()'s
//                                    PureSignal AutoAtt write path; all
//                                    three key families now share one
//                                    union floor) and a Minor
//                                    (BoardCapsTable::all() replaces the
//                                    hand-maintained kAllBoards list).
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    onLocalAppSettingsChange() emits
//                                    outboundValueRemoved() for a key that
//                                    is now absent. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Minor 7:
//                                    SwrProtectionLimit range-checked on
//                                    the inbound write path. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-11: the step
//                                    attenuator range check removed: its
//                                    keys are refused before it as the
//                                    Core's own (isModelOwnedDspSettingsKey).
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include "core/settings/SettingsProxyServer.h"

#include "core/AppSettings.h"
#include "core/settings/SettingsScope.h"

#include <QScopeGuard>

namespace NereusSDR {

namespace {

// Whole-branch review, Minor 7. Both taken from the operator's own
// control rather than invented here: TransmitSetupPages.cpp's
// udSwrProtectionLimit is a QDoubleSpinBox with setRange(1.0, 5.0), and
// carries its own upstream cite at that call site. Keeping the wire bound
// identical to the UI bound is the whole point: a remote client must not
// be able to express a limit the operator sitting at the station cannot.
// If that spinbox's range ever moves, this pair moves with it.
constexpr double kSwrProtectionLimitMin = 1.0;
constexpr double kSwrProtectionLimitMax = 5.0;

} // namespace

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
    if (isModelOwnedDspSettingsKey(key)) {
        SettingsApplyResult result;
        result.reason = modelOwnedSettingsRefusal(key);
        result.restoredValue = m_appSettings.value(key);
        return result;
    }
    if (classifySettingsKey(key) != SettingsScope::Station) {
        SettingsApplyResult result;
        result.accepted = false;
        result.reason = QStringLiteral("key is not Station-scoped");
        result.restoredValue = m_appSettings.value(key);
        return result;
    }

    // Whole-branch review, Minor 7. SwrProtectionLimit is Station-scoped
    // and reaches a PA-protection gate: RadioModel's construction reads it
    // and hands it to SwrProtectionController::setLimit(), which stores
    // without clamping, while the only UI that writes it is a
    // QDoubleSpinBox pinned to 1.0..5.0 (TransmitSetupPages.cpp's
    // udSwrProtectionLimit). Over the wire there was nothing between the
    // socket and the applied limit, so an authenticated client could park
    // a limit at 99 (protection effectively disabled at the next daemon
    // start) or below 1.0 (unreachable, so the gate trips permanently).
    //
    // Hardening, not a live defect: it needs an authenticated client AND a
    // daemon restart, which is the same "ungated inbound" class the class
    // comment above describes. Bounded to the operator's own control's
    // range exactly, so the wire cannot express a limit the UI cannot --
    // deliberately NOT a new general validation framework, which that
    // section explains does not belong here.
    if (key == QLatin1String("SwrProtectionLimit")) {
        bool ok = false;
        const double limit = value.toDouble(&ok);
        if (!ok || limit < kSwrProtectionLimitMin || limit > kSwrProtectionLimitMax) {
            SettingsApplyResult result;
            result.accepted = false;
            result.reason = QStringLiteral("SWR protection limit out of range [%1, %2]")
                                .arg(kSwrProtectionLimitMin)
                                .arg(kSwrProtectionLimitMax);
            result.restoredValue = m_appSettings.value(key);
            return result;
        }
    }

    // The anti-echo suppression this whole task exists to build. See the
    // class comment's "The hazard this class exists to not build"
    // section for the full mechanism: m_applyingInboundWrite is checked
    // FIRST in onLocalAppSettingsChange(), so the generic change-hook
    // path emits nothing for THIS write -- this method emits the one,
    // correctly-tagged broadcast for it explicitly, below.
    //
    // Fix round 1 (review, Minor 5): QScopeGuard, not a bare
    // `= true; ...; = false;` pair -- see m_applyingInboundWrite's own
    // doc comment (SettingsProxyServer.h) for why this is about
    // exception safety, not re-entrancy. If AppSettings::setValue()
    // (or anything the Task 13 hook chain calls) ever threw, a bare
    // pair would leave the flag stuck true for this object's entire
    // remaining lifetime. Scoped to a nested block so the guard fires
    // (flag back to false) immediately after setValue() returns or
    // unwinds, strictly before the emit below -- matching the original
    // ordering, though nothing here actually depends on it.
    {
        m_applyingInboundWrite = true;
        const auto clearApplyingGuard = qScopeGuard([this]() { m_applyingInboundWrite = false; });
        m_appSettings.setValue(key, value);
    }

    // Fix round 1 (review, Minor 6): m_appSettings.value(key), not the
    // caller's raw `value` parameter -- setValue()'s own contract
    // collapses everything to QString on the way in
    // (AppSettings.cpp:val.toString()), so the two broadcast paths must
    // agree on what comes back out. Emitting the caller's original
    // QVariant here (an int, in a typical caller) while
    // onLocalAppSettingsChange() below emits value(key) (always
    // QVariant(QString)) would serialise the SAME logical change as two
    // different JSON shapes -- 192000 versus "192000" -- depending on
    // which of the two paths happened to produce it.
    emit outboundValueChanged(key, m_appSettings.value(key), originTag);

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
    // Whole-branch review, Important 4: a REMOVAL is not a value change,
    // and this hook fires for both. AppSettings::remove() calls this
    // exactly as setValue() does, and value() on the now-absent key
    // returns an invalid QVariant that the relay used to flatten into ""
    // -- so a removal arrived at every client as "set to empty string",
    // leaving contains() true there and false here. contains() is the
    // question that separates the two cases, and it is asked here rather
    // than left for a downstream reader to infer from QVariant validity.
    if (!m_appSettings.contains(key)) {
        emit outboundValueRemoved(key);
        return;
    }
    // A genuine local/daemon-side change: empty origin tag, since it is
    // nobody's echo (see SettingsProxy.h's origin-tag paragraph: an
    // empty tag can never equal a real session's own localOriginTag()).
    const QVariant value = m_appSettings.value(key);
    emit outboundValueChanged(key, value, QString());
}

} // namespace NereusSDR
