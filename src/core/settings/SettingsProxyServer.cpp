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
// =================================================================

#include "core/settings/SettingsProxyServer.h"

#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/settings/SettingsScope.h"

#include <QScopeGuard>

#include <algorithm>

namespace NereusSDR {

namespace {

// Fix round 2 (review, Important 3a). Fix round 1's union was derived by
// grepping the literal text ".attenuator =" in BoardCapabilities.cpp,
// which finds every board's STATIC {minDb, maxDb, ...} row but completely
// misses BoardCapsTable::stepAttMaxDb(hw, alexPresent)
// (BoardCapabilities.cpp:1394-1428) -- the function whose entire purpose
// is to WIDEN that static maxDb: Atlas, Hermes, HermesII, Angelia and
// Orion reach 61 dB when an Alex filter board is present (Thetis parity,
// GeneralOptionsPage.cpp:538-541 against setup.cs:15773-15786). The
// fix-round-1 union of [-28, 31] therefore REJECTED a legitimate 32-61 dB
// setting on five real board types -- the wrong direction to err in: a
// bounds check exists to stop hostile/garbage values, not real operator
// settings RxApplet.cpp:1573-1578 itself puts on those same boards' own
// spinboxes.
//
// This computes the union PROPERLY: for every named board, ask
// stepAttMaxDb(hw, /*alexPresent=*/true) -- the widest case this class
// can offer, since (as fix round 1 already established) it has no
// MAC-to-board-type resolution to ask for less -- for the maximum, and
// read BoardCapabilities::attenuator.minDb directly for the minimum
// (stepAttMaxDb() only ever answers the maximum; the minimum is never
// Alex-widened, only HL2's signed range differs from the 0 dB floor
// every other board uses). This is the SAME pair of calls
// RxApplet.cpp:1573-1578 already makes for the live spinbox range, so
// this mirrors an established call shape rather than inventing a new
// one. Calling the real function (rather than re-reading its output by
// hand into a second, static copy, which is exactly the mistake fix
// round 1 made) means this union tracks BoardCapabilities.cpp
// automatically if it ever changes again.
//
// Fix round 3 (review, Minor): fix round 2's claim that "BoardCapabilities
// .h/.cpp exposes no 'every board' enumeration to iterate instead" was
// false -- BoardCapsTable::all() (BoardCapabilities.h:586) returns a
// std::span over the whole table (13 entries, including
// HPSDRHW::Unknown), and six existing test files already use
// `for (const auto& caps : BoardCapsTable::all())` as the house idiom
// (e.g. tests/tst_board_capabilities.cpp:52). Iterating it here instead
// of a hand-maintained board array removes the exact drift vector fix
// round 2 introduced while claiming to remove drift: a board added to
// HPSDRHW without a matching hand-added entry would have silently
// dropped out of the union. all() needs no Unknown special-case: its row
// has attenuator.present == false, so the existing !present guard below
// already skips it -- the same guard that already skips Atlas for the
// same reason (see "Things I noticed but did not fix", fix round 2's
// report section).
struct StepAttUnionRange {
    int minDb;
    int maxDb;
};

const StepAttUnionRange& stepAttenuatorUnionRange()
{
    static const StepAttUnionRange range = [] {
        int minDb = 0;
        int maxDb = 0;
        bool first = true;
        for (const BoardCapabilities& caps : BoardCapsTable::all()) {
            if (!caps.attenuator.present) {
                continue;
            }
            const int hwMaxDb = BoardCapsTable::stepAttMaxDb(caps.board, /*alexPresent=*/true);
            const int hwMinDb = caps.attenuator.minDb;
            if (first) {
                minDb = hwMinDb;
                maxDb = hwMaxDb;
                first = false;
            } else {
                minDb = std::min(minDb, hwMinDb);
                maxDb = std::max(maxDb, hwMaxDb);
            }
        }
        return StepAttUnionRange{minDb, maxDb};
    }();
    return range;
}

// Which step-attenuator key family (if any) `key` belongs to. Fix round 2
// (review, Important 3b): fix round 1's check matched ONLY
// options/stepAtt/rx1Value, the line StepAttenuatorController::
// loadSettings() (StepAttenuatorController.cpp:1052-1148) reads FIRST
// (:1067) -- but :1093-1106 then reads options/stepAtt/rx1Band/<band>
// UNCLAMPED into m_bandState[b].attDb, and :1109-1114 does
// `m_attDb = it->second.attDb` whenever the CURRENT band has a stored
// entry, silently overwriting the one value the fix-round-1 check
// actually gated. options/stepAtt/txBand/<band> (:1126-1135, feeding
// m_txAttByBand[]) is the identical shape on the TX side, bypassing
// setTxAttenuationForBand()'s own clamp (StepAttenuatorController.cpp:
// 313-314). Both are covered here.
//
// Fix round 3 (review, Important 3 TX half): ALL THREE families share
// ONE floor, range.minDb -- see applyInboundWrite() below. There is no
// per-family split any more. This is not a simplification made for its
// own sake; it is the exhaustively-traced conclusion. Every writer of
// every field saveSettings() persists under these three keys was
// enumerated (task report, "Exhaustive writer audit"):
//   m_attDb / m_bandState[].attDb (rx1Value, rx1Band/<band>) trace, via
//   setBand()/onMoxHardwareFlipped()/applyAttToHardware()'s callers, to
//   ONE of setAttenuation() ([m_minAttDb, m_maxAttDb]) or whatever is
//   already in m_txAttByBand[] (below) -- never a wider value than
//   those two produce.
//   m_txAttByBand[] (txBand/<band>) has TWO independent primary writers:
//   setTxAttenuationForBand() ([0, m_maxAttDb], StepAttenuatorController
//   .cpp:305-316) and setAttOnTxValue() ([m_minAttDb, 31], :349-374 --
//   called from PureSignal's AutoAtt RestoreOperation state,
//   PureSignal.cpp:1600-1616, computing `max(oldAtten + deltaDb,
//   minAttenuation())` with NO upper bound of its own and relying
//   entirely on setAttOnTxValue()'s internal clamp; its own comment
//   states the bypass of setTxAttenuationForBand()'s hardcoded-0 floor
//   is deliberate, "which would strip the HL2 signed range").
// For every board, m_minAttDb <= 0 and m_maxAttDb >= 31 (confirmed
// against every row in BoardCapabilities.cpp, not assumed), so
// [0, m_maxAttDb] union [m_minAttDb, 31] is CONTIGUOUS and collapses to
// exactly [m_minAttDb, m_maxAttDb] -- identical to the RX bound, for
// every board, not a coincidence of the union operation. A fix-round-2
// TX-specific 0 floor therefore rejected legitimate negative HL2 TX
// values (PureSignal AutoAtt is the live path that produces them, not a
// theoretical one) for no reason: the correct TX floor was always the
// same as RX's.

// Whole-branch review, Minor 7. Both taken from the operator's own
// control rather than invented here: TransmitSetupPages.cpp's
// udSwrProtectionLimit is a QDoubleSpinBox with setRange(1.0, 5.0), and
// carries its own upstream cite at that call site. Keeping the wire bound
// identical to the UI bound is the whole point: a remote client must not
// be able to express a limit the operator sitting at the station cannot.
// If that spinbox's range ever moves, this pair moves with it.
constexpr double kSwrProtectionLimitMin = 1.0;
constexpr double kSwrProtectionLimitMax = 5.0;

enum class StepAttKeyFamily {
    None,
    RxValue, // options/stepAtt/rx1Value -- exact, no band suffix
    RxBand,  // options/stepAtt/rx1Band/<band>
    TxBand,  // options/stepAtt/txBand/<band>
};

StepAttKeyFamily stepAttenuatorKeyFamily(const QString& key)
{
    if (!key.startsWith(QStringLiteral("hardware/"))) {
        return StepAttKeyFamily::None;
    }
    if (key.endsWith(QStringLiteral("/options/stepAtt/rx1Value"))) {
        return StepAttKeyFamily::RxValue;
    }
    if (key.contains(QStringLiteral("/options/stepAtt/rx1Band/"))) {
        return StepAttKeyFamily::RxBand;
    }
    if (key.contains(QStringLiteral("/options/stepAtt/txBand/"))) {
        return StepAttKeyFamily::TxBand;
    }
    return StepAttKeyFamily::None;
}

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
        result.reason = QStringLiteral("Use the station DSP controls; raw settings writes cannot bypass model validation.");
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

    // Fix round 1 (review, Important 3b), corrected in fix round 2
    // (review, Important 3a+3b) and fix round 3 (review, Important 3 TX
    // half). See the class comment's "Inbound-write validation" section
    // and stepAttenuatorUnionRange()'s / stepAttenuatorKeyFamily()'s own
    // comments above for the full reasoning: three key shapes gated, ONE
    // union range applied UNIFORMLY to all three (fix round 3 removed
    // the fix-round-2 TX-specific 0 dB floor -- setAttOnTxValue(), the
    // PureSignal AutoAtt write path, deliberately bypasses
    // setTxAttenuationForBand()'s hardcoded-0 floor specifically to let
    // HL2's negative range through, so a 0 dB TX floor rejected values
    // that path legitimately produces). Erring permissive is correct
    // here: a live per-board lookup is not available to this class, so
    // the union must never reject a value legitimate on ANY board.
    const StepAttKeyFamily stepAttFamily = stepAttenuatorKeyFamily(key);
    if (stepAttFamily != StepAttKeyFamily::None) {
        const StepAttUnionRange& range = stepAttenuatorUnionRange();
        bool ok = false;
        const int dB = value.toInt(&ok);
        if (!ok || dB < range.minDb || dB > range.maxDb) {
            SettingsApplyResult result;
            result.accepted = false;
            result.reason = QStringLiteral("step attenuator value out of range [%1, %2]")
                                .arg(range.minDb)
                                .arg(range.maxDb);
            result.restoredValue = m_appSettings.value(key);
            return result;
        }
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
