// SPDX-License-Identifier: GPL-3.0-or-later
//
// no-port-check: NereusSDR-native model, no Thetis or AetherSDR
// equivalent. See DisplaySettingsModel.h for the full design note (3D
// Stacked-Trace Spectrum Plan Task 17).
//
// NereusSDR - DisplaySettingsModel implementation.
//
// Modification history (NereusSDR)
//   2026-10-12: Query closed-pan defaults without constructing a model.
//     J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-12: Read current per-pan values through the persistence visitor.
//     J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-10: Display pill plan Task 3: waterfall Black Level is 0 to
//     100 and the Black Level and Color Gain defaults are 57 and 60 (the
//     Clarity level model). J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//     Claude Code.
//   2026-10-10: Per-pan keys read and written through PanDisplayKeys
//     (bare key is the Setup default, <base>_<pan> a sparse override).
//     J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-02: Lossless completed-grid reflection and selected RX snapshot
//     persistence. J.J. Boyd / KG4VCF, AI-assisted via OpenAI Codex.
//   Created 2026-08-09 by J.J. Boyd / KG4VCF, 3D Stacked-Trace Spectrum
//     Plan Task 17. AI tooling: Claude Code.

#include "DisplaySettingsModel.h"

#include "core/settings/PanDisplayKeys.h"
#include "core/spectrum/ClarityLevels.h"

#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

namespace {

// Ship defaults for the fourteen per-pan keys. load() falls back to them
// and save() passes the same ones to PanDisplayKeys::writeSparse(), which
// is how a pan that still shows the ship default writes no key of its own.
// They match SpectrumWidget::loadSettings()'s ship defaults for the same
// keys exactly, so a fresh install looks identical through either object.
constexpr int   kDefaultWfColorScheme      = 0;
// Display pill plan Task 3: the positions that reproduce the look before
// the Clarity level model (core/spectrum/ClarityLevels.h); they were 45
// and 104 under the model before it.
constexpr int   kDefaultWfColorGain        = ClarityLevels::kColorGainDefault;
constexpr int   kDefaultWfBlackLevel       = ClarityLevels::kBlackLevelDefault;
constexpr float kDefaultFillAlpha          = 0.70f;
constexpr bool  kDefaultPanFill            = true;
constexpr float kDefaultSpectrumFrac       = 0.40f;
constexpr int   kDefaultSpectrumRenderMode = 0;
constexpr int   kDefaultDssGain            = 70;
constexpr int   kDefaultDssRowSpan         = 100;
constexpr int   kDefaultDssAngle           = 50;
constexpr int   kDefaultDssRowDivider      = 0;
constexpr bool  kDefaultThreeDSliceDepth   = false;

QString floatText(float value)
{
    return QString::number(static_cast<double>(value));
}

QString boolText(bool value)
{
    return value ? QStringLiteral("True") : QStringLiteral("False");
}

} // namespace

DisplaySettingsModel::DisplaySettingsModel(QObject* parent)
    : QObject(parent)
{
}

// ---- Waterfall ----

void DisplaySettingsModel::setWfColorScheme(int scheme)
{
    const int clamped = std::clamp(scheme, 0, 7); // WfColorScheme::Count == 8
    if (m_wfColorScheme == clamped) { return; }
    m_wfColorScheme = clamped;
    emit wfColorSchemeChanged(m_wfColorScheme);
}

void DisplaySettingsModel::setWfColorGain(int gain)
{
    const int clamped = std::clamp(gain, ClarityLevels::kColorGainMin,
                                   ClarityLevels::kColorGainMax);
    if (m_wfColorGain == clamped) { return; }
    m_wfColorGain = clamped;
    emit wfColorGainChanged(m_wfColorGain);
}

void DisplaySettingsModel::setWfBlackLevel(int level)
{
    // 0 to 100, 50 = black at the noise floor, up = blacker (was 0 to 125).
    const int clamped = std::clamp(level, ClarityLevels::kBlackLevelMin,
                                   ClarityLevels::kBlackLevelMax);
    if (m_wfBlackLevel == clamped) { return; }
    m_wfBlackLevel = clamped;
    emit wfBlackLevelChanged(m_wfBlackLevel);
}

// ---- Spectrum ----

void DisplaySettingsModel::setRefLevel(float dBm)
{
    // Native Thetis strip editors now accept -200..200. The historical
    // Task 18 rationale below remains; runtime reflection bypasses this
    // editor clamp and assigns a completed pair atomically.
    // -180.0f..80.0f, not the popup slider's -160..20: SpectrumWidget's
    // Task 19 Ctrl-drag gesture (mouseMoveEvent's m_draggingDbmRange
    // branch) can legitimately drive refLevel up to upstream's own
    // kMaxDisplayDbm = 80.0f via clampDbmRangeForBottom -- found only
    // once this class was actually bound to SpectrumWidget (Task 18):
    // tst_dbm_range_drag.cpp's ctrlDragRange_clampsAtMaximum drags to
    // refLevel() == 60.0f, which a -160..20 model clamp would silently
    // narrow to 20.0f on the round trip back into the widget. Same
    // never-narrower-than-any-widget-write-path rule as Dyn Range above.
    const float clamped = std::clamp(dBm, -200.0f, 200.0f);
    if (qFuzzyCompare(m_refLevel + 1.0f, clamped + 1.0f)) { return; }
    m_refLevel = clamped;
    emit refLevelChanged(m_refLevel);
}

void DisplaySettingsModel::setDynamicRange(float dB)
{
    // 10.0f..200.0f, not the popup slider's 20..160: this is the widest
    // bound any widget write path accepts. From SpectrumWidget::wheelEvent
    // (wheel-over-dBm-strip block), qBound(10.0f, ..., 200.0f) -- the
    // model must never clamp narrower than that or a round trip through
    // the model would narrow a value the widget itself allows.
    const float clamped = std::clamp(dB, 10.0f, 200.0f);
    if (qFuzzyCompare(m_dynamicRange, clamped)) { return; }
    m_dynamicRange = clamped;
    emit dynamicRangeChanged(m_dynamicRange);
}

void DisplaySettingsModel::reflectDbmRange(float refLevel, float dynamicRange)
{
    if (!std::isfinite(refLevel) || !std::isfinite(dynamicRange)) { return; }
    const bool refChanged = m_refLevel != refLevel;
    const bool rangeChanged = m_dynamicRange != dynamicRange;
    m_refLevel = refLevel;
    m_dynamicRange = dynamicRange;
    if (refChanged) { emit refLevelChanged(m_refLevel); }
    if (rangeChanged) { emit dynamicRangeChanged(m_dynamicRange); }
}

void DisplaySettingsModel::setFillAlpha(float alpha)
{
    const float clamped = std::clamp(alpha, 0.0f, 1.0f);
    // +1.0f offset: qFuzzyCompare's relative tolerance breaks down when
    // either operand is exactly (or very near) 0.0, which fillAlpha's
    // valid range legitimately includes -- same +1.0f idiom used by this
    // codebase's own tst_pan_display_settings_inherit.cpp for the same
    // reason.
    if (qFuzzyCompare(m_fillAlpha + 1.0f, clamped + 1.0f)) { return; }
    m_fillAlpha = clamped;
    emit fillAlphaChanged(m_fillAlpha);
}

void DisplaySettingsModel::setPanFill(bool on)
{
    if (m_panFill == on) { return; }
    m_panFill = on;
    emit panFillChanged(m_panFill);
}

void DisplaySettingsModel::setSpectrumFrac(float frac)
{
    const float clamped = std::clamp(frac, 0.10f, 0.90f);
    if (qFuzzyCompare(m_spectrumFrac, clamped)) { return; }
    m_spectrumFrac = clamped;
    emit spectrumFracChanged(m_spectrumFrac);
}

// ---- 3D ----

void DisplaySettingsModel::setSpectrumRenderMode(int mode)
{
    const int clamped = std::clamp(mode, 0, 1); // SpectrumRenderMode::Count == 2
    if (m_spectrumRenderMode == clamped) { return; }
    m_spectrumRenderMode = clamped;
    emit spectrumRenderModeChanged(m_spectrumRenderMode);
}

void DisplaySettingsModel::setDssFloorDepth(int depthDb)
{
    const int clamped = std::clamp(depthDb, 0, 24);
    if (m_dssFloorDepth == clamped) { return; }
    m_dssFloorDepth = clamped;
    emit dssFloorDepthChanged(m_dssFloorDepth);
}

void DisplaySettingsModel::setDssGain(int pct)
{
    const int clamped = std::clamp(pct, 0, 100);
    if (m_dssGain == clamped) { return; }
    m_dssGain = clamped;
    emit dssGainChanged(m_dssGain);
}

void DisplaySettingsModel::setDssRowSpan(int pct)
{
    const int clamped = std::clamp(pct, 0, 100);
    if (m_dssRowSpan == clamped) { return; }
    m_dssRowSpan = clamped;
    emit dssRowSpanChanged(m_dssRowSpan);
}

void DisplaySettingsModel::setDssAngle(int pct)
{
    const int clamped = std::clamp(pct, 0, 100);
    if (m_dssAngle == clamped) { return; }
    m_dssAngle = clamped;
    emit dssAngleChanged(m_dssAngle);
}

void DisplaySettingsModel::setDssRowDivider(int n)
{
    const int clamped = std::clamp(n, 0, 10);
    if (m_dssRowDivider == clamped) { return; }
    m_dssRowDivider = clamped;
    emit dssRowDividerChanged(m_dssRowDivider);
}

void DisplaySettingsModel::setThreeDSliceDepth(bool on)
{
    if (m_threeDSliceDepth == on) { return; }
    m_threeDSliceDepth = on;
    emit threeDSliceDepthChanged(m_threeDSliceDepth);
}

// ---- Persistence ----

void DisplaySettingsModel::load()
{
    // A pan reads its own key (<base>_<panIndex>) when it has one, else
    // the Setup default (the bare key), else the ship default: an
    // untouched pan follows the Setup default until it is given a value
    // of its own. SpectrumWidget::loadSettings() reads its keys through
    // the same PanDisplayKeys helper, so the two cannot disagree about
    // where a value comes from.
    auto rawValue = [&](const QString& key) -> QString {
        return PanDisplayKeys::read(key, m_panIndex, QString()).toString();
    };
    auto readFloat = [&](const QString& key, float def) -> float {
        const QString val = rawValue(key);
        if (val.isEmpty()) { return def; }
        bool ok = false;
        const float v = val.toFloat(&ok);
        return ok ? v : def;
    };
    auto readInt = [&](const QString& key, int def) -> int {
        const QString val = rawValue(key);
        if (val.isEmpty()) { return def; }
        bool ok = false;
        const int v = val.toInt(&ok);
        return ok ? v : def;
    };
    auto readBool = [&](const QString& key, bool def) -> bool {
        const QString val = rawValue(key);
        if (val.isEmpty()) { return def; }
        return val == QStringLiteral("True");
    };

    // Defaults match SpectrumWidget::loadSettings()'s ship defaults for
    // the same keys exactly, so a fresh install looks identical through
    // either object.
    setWfColorScheme(readInt(QStringLiteral("DisplayWfColorScheme"), kDefaultWfColorScheme));
    setWfColorGain(readInt(QStringLiteral("DisplayWfColorGain"), kDefaultWfColorGain));
    setWfBlackLevel(readInt(QStringLiteral("DisplayWfBlackLevel"), kDefaultWfBlackLevel));

    // Display pill plan Task 9 (S9): ref level and dynamic range are not
    // loaded. The receive range is the band store's
    // (DisplayGridMax_<band> / DisplayGridMin_<band>, PanadapterModel);
    // these two follow the widget's live range (reflectDbmRange()). The
    // old per-pan DisplayGridMax / DisplayGridMin keys, bare or with a pan
    // suffix, are never read, rewritten or deleted.

    setFillAlpha(readFloat(QStringLiteral("DisplayFftFillAlpha"), kDefaultFillAlpha));
    setPanFill(readBool(QStringLiteral("DisplayPanFill"), kDefaultPanFill));
    setSpectrumFrac(readFloat(QStringLiteral("DisplaySpectrumFrac"), kDefaultSpectrumFrac));

    setSpectrumRenderMode(readInt(QStringLiteral("DisplaySpectrumRenderMode"),
                                  kDefaultSpectrumRenderMode));
    setDssGain(readInt(QStringLiteral("Display3DGain"), kDefaultDssGain));
    setDssRowSpan(readInt(QStringLiteral("Display3DSpan"), kDefaultDssRowSpan));
    setDssAngle(readInt(QStringLiteral("Display3DAngle"), kDefaultDssAngle));
    setDssRowDivider(readInt(QStringLiteral("Display3DSpeed"), kDefaultDssRowDivider));
    setThreeDSliceDepth(readBool(QStringLiteral("Display3DSliceShadow"),
                                 kDefaultThreeDSliceDepth));
    // dssFloorDepth is deliberately not read here: it is not scoped by
    // panIndex() at all, and its persistence lives entirely on
    // PanadapterModel, per band (see the file header).
}

void DisplaySettingsModel::save()
{
    // Sparse: a key of this pan's own is written only when the pan already
    // has it or the value differs from what the pan would inherit (see
    // PanDisplayKeys.h). The bare key is the Setup default and is never
    // written from here, by any pan.
    visitPerPanKeys([this](const QString& base, const QString& value,
                           const QString& fallback) {
        PanDisplayKeys::writeSparse(base, m_panIndex, value, fallback);
    });
}

QStringList DisplaySettingsModel::perPanKeyBases() const
{
    QStringList bases;
    visitPerPanKeys([&bases](const QString& base, const QString&, const QString&) {
        bases.append(base);
    });
    return bases;
}

QString DisplaySettingsModel::perPanShipDefault(const QString& wanted) const
{
    return perPanShipDefaultForClosedPan(wanted);
}

QString DisplaySettingsModel::perPanShipDefaultForClosedPan(const QString& wanted)
{
    QString found;
    visitPerPanKeys(nullptr, [&found, &wanted](const QString& base, const QString&,
                                              const QString& fallback) {
        if (base == wanted) { found = fallback; }
    });
    return found;
}

QString DisplaySettingsModel::perPanValue(const QString& wanted) const
{
    QString found;
    visitPerPanKeys([&found, &wanted](const QString& base, const QString& value,
                                     const QString&) {
        if (base == wanted) { found = value; }
    });
    return found;
}

// The one list of this model's per-pan keys: each key's base name, the
// value save() stores for it and the ship default load() falls back to,
// both formatted the way the key is stored. save() and perPanKeyBases()
// both walk it, so what a reset clears is exactly what a save can write.
void DisplaySettingsModel::visitPerPanKeys(const PerPanKeyVisitor& visit) const
{
    visitPerPanKeys(this, visit);
}

void DisplaySettingsModel::visitPerPanKeys(const DisplaySettingsModel* source,
                                        const PerPanKeyVisitor& visit)
{
    // Lazy values keep a closed-pan default lookup independent of model
    // construction, current state, settings and signal emission.
    auto visitFloat = [&](const QString& key, const auto& value, float def) {
        visit(key, source ? floatText(value()) : floatText(def), floatText(def));
    };
    auto visitInt = [&](const QString& key, const auto& value, int def) {
        visit(key, source ? QString::number(value()) : QString::number(def), QString::number(def));
    };
    auto visitBool = [&](const QString& key, const auto& value, bool def) {
        visit(key, source ? boolText(value()) : boolText(def), boolText(def));
    };

    visitInt(QStringLiteral("DisplayWfColorScheme"), [&] { return source->m_wfColorScheme; }, kDefaultWfColorScheme);
    visitInt(QStringLiteral("DisplayWfColorGain"), [&] { return source->m_wfColorGain; }, kDefaultWfColorGain);
    visitInt(QStringLiteral("DisplayWfBlackLevel"), [&] { return source->m_wfBlackLevel; }, kDefaultWfBlackLevel);

    // Display pill plan Task 9 (S9): no DisplayGridMax / DisplayGridMin
    // here. Ref level and dynamic range are live values, not persisted.

    visitFloat(QStringLiteral("DisplayFftFillAlpha"), [&] { return source->m_fillAlpha; }, kDefaultFillAlpha);
    visitBool(QStringLiteral("DisplayPanFill"), [&] { return source->m_panFill; }, kDefaultPanFill);
    visitFloat(QStringLiteral("DisplaySpectrumFrac"), [&] { return source->m_spectrumFrac; }, kDefaultSpectrumFrac);

    visitInt(QStringLiteral("DisplaySpectrumRenderMode"), [&] { return source->m_spectrumRenderMode; },
             kDefaultSpectrumRenderMode);
    visitInt(QStringLiteral("Display3DGain"), [&] { return source->m_dssGain; }, kDefaultDssGain);
    visitInt(QStringLiteral("Display3DSpan"), [&] { return source->m_dssRowSpan; }, kDefaultDssRowSpan);
    visitInt(QStringLiteral("Display3DAngle"), [&] { return source->m_dssAngle; }, kDefaultDssAngle);
    visitInt(QStringLiteral("Display3DSpeed"), [&] { return source->m_dssRowDivider; }, kDefaultDssRowDivider);
    visitBool(QStringLiteral("Display3DSliceShadow"), [&] { return source->m_threeDSliceDepth; },
              kDefaultThreeDSliceDepth);
    // dssFloorDepth is deliberately not written here: same reason as the
    // matching comment in load() above.
}

} // namespace NereusSDR
