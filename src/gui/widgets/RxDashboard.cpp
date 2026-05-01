// src/gui/widgets/RxDashboard.cpp
//
// Phase 3Q Sub-PR-5 (E.1) — RxDashboard widget
//
// 2026-04-30 update: removed the RX1 + frequency display (freq lives on
// the VFO Flag), composed badges into three BadgePair slots + one lone
// SQL badge, added per-badge tooltips, and extended the responsive
// behavior to a 3-stage ladder: horizontal → stacked-pairs → drop in
// priority order. Bumps StatusBadge sizing rely on the +2 px font /
// +1 px vertical padding change in StatusBadge.cpp.
//
// Signal mapping verified against SliceModel.h 2026-04-30:
//   agcModeChanged(AGCMode)  nbModeChanged(NbMode)
//   dspModeChanged(DSPMode)  activeNrChanged(NrSlot)  apfEnabledChanged(bool)
//   filterChanged(int,int)   ssqlEnabledChanged(bool)

#include "RxDashboard.h"

#include "models/SliceModel.h"
#include "BadgePair.h"
#include "StatusBadge.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>

namespace NereusSDR {

RxDashboard::RxDashboard(QWidget* parent) : QWidget(parent)
{
    buildUi();
    // Horizontal policy is Preferred (not Minimum) so the parent
    // QHBoxLayout can shrink the dashboard below its horizontal
    // sizeHint when the strip is tight. Minimum would lock the
    // dashboard at its preferred width and the
    // reapplyDropPriority() trigger (contentWidth > budget) would
    // never fire — see issue caught 2026-04-30.
    //
    // Floor is 60 px so even fully-dropped (just two stacked badges
    // visible) the dashboard remains visible enough to read. Drop
    // priority handles the in-between sizing.
    setMinimumWidth(60);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
}

namespace {

// Tooltips that don't depend on the active value — mode/filter/AGC/NR
// generate per-state tooltips inline (see the on*Changed handlers).
constexpr auto kNbTooltip  = "Noise blanker active";
constexpr auto kApfTooltip = "Auto-notch filter active";
constexpr auto kSqlTooltip = "Squelch open";

} // namespace

void RxDashboard::buildUi()
{
    auto* hbox = new QHBoxLayout(this);
    hbox->setContentsMargins(10, 0, 10, 0);
    hbox->setSpacing(6);

    // Build all badges first so BadgePair can take ownership.
    m_modeBadge   = new StatusBadge(this);
    m_filterBadge = new StatusBadge(this);
    m_agcBadge    = new StatusBadge(this);
    m_nrBadge     = new StatusBadge(this);
    m_nbBadge     = new StatusBadge(this);
    m_apfBadge    = new StatusBadge(this);
    m_sqlBadge    = new StatusBadge(this);

    // ── Slot 1: Mode + Filter (both always shown when connected) ─────────
    m_modeFilterPair = new BadgePair(m_modeBadge, m_filterBadge, this);
    hbox->addWidget(m_modeFilterPair);

    // ── Slot 2: AGC (always) + NR (active only) ──────────────────────────
    m_agcNrPair = new BadgePair(m_agcBadge, m_nrBadge, this);
    hbox->addWidget(m_agcNrPair);

    // ── Slot 3: NB + APF (both active only) ──────────────────────────────
    m_nbApfPair = new BadgePair(m_nbBadge, m_apfBadge, this);
    hbox->addWidget(m_nbApfPair);

    // ── Slot 4: SQL — alone (no pair partner) ────────────────────────────
    hbox->addWidget(m_sqlBadge);

    // Active-only badges hidden by default — "no NYI" rule.
    m_nrBadge->setVisible(false);
    m_nbBadge->setVisible(false);
    m_apfBadge->setVisible(false);
    m_sqlBadge->setVisible(false);

    // SVG icon prefixes — original cc55bde plan called for these glyphs
    // but they were dropped because the Unicode source (⨎ ⚡ ⌁ ∼ ⊘ ▼)
    // rendered inconsistently across font fallbacks. With the SVG icon
    // path now available on StatusBadge (see setSvgIcon docs), each
    // badge gets a font-independent shape that auto-tints to the
    // variant color. The mode badge gets a generic sine-wave glyph —
    // its text label (USB / LSB / CW / AM / FM / DIGU / DIGL) tells you
    // which mode; the icon just marks "this badge is the mode badge".
    m_modeBadge  ->setSvgIcon(QStringLiteral(":/icons/badge-mode.svg"));
    m_filterBadge->setSvgIcon(QStringLiteral(":/icons/badge-filter.svg"));
    m_agcBadge   ->setSvgIcon(QStringLiteral(":/icons/badge-agc.svg"));
    m_nrBadge    ->setSvgIcon(QStringLiteral(":/icons/badge-nr.svg"));
    m_nbBadge    ->setSvgIcon(QStringLiteral(":/icons/badge-nb.svg"));
    m_apfBadge   ->setSvgIcon(QStringLiteral(":/icons/badge-apf.svg"));
    m_sqlBadge   ->setSvgIcon(QStringLiteral(":/icons/badge-sql.svg"));

    // Always-shown badges: placeholder state until bound.
    m_modeBadge->setLabel(QStringLiteral("—"));
    m_modeBadge->setVariant(StatusBadge::Variant::Info);
    m_modeBadge->setToolTip(tr("Operating mode"));

    m_filterBadge->setLabel(QStringLiteral("—"));
    m_filterBadge->setVariant(StatusBadge::Variant::On);
    m_filterBadge->setToolTip(tr("Filter passband width"));

    m_agcBadge->setLabel(QStringLiteral("—"));
    m_agcBadge->setVariant(StatusBadge::Variant::Info);
    m_agcBadge->setToolTip(tr("AGC mode"));

    // Pre-set the active-only tooltips so they're already correct when
    // the badge first becomes visible.
    m_nbBadge->setToolTip(tr(kNbTooltip));
    m_apfBadge->setToolTip(tr(kApfTooltip));
    m_sqlBadge->setToolTip(tr(kSqlTooltip));
}

void RxDashboard::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    reapplyDropPriority();
}

void RxDashboard::reapplyDropPriority()
{
    // Re-entry guard: setVisible() / pair toggles trigger a layout
    // update which can re-fire resizeEvent before this method unwinds.
    if (m_inReapplyDropPriority) { return; }
    m_inReapplyDropPriority = true;

    const int budget = width();

    // Hysteresis (deadband): if the budget hasn't moved past a 30 px
    // window since our last decision, do not re-evaluate. The
    // setStacked() / setVisible() calls below trigger paints + layout
    // invalidation. Without a deadband, the parent QHBoxLayout
    // re-allocates space based on our just-changed sizeHint, which
    // re-fires resizeEvent at a slightly different width, which can
    // flip us back across the wide↔medium boundary. The visible result
    // is the two BadgePair orientations rendering on top of each other
    // rapidly. 30 px is wider than the typical layout-jitter signal
    // and small enough to not be felt as "sticky" during a manual drag.
    constexpr int kDeadbandPx = 30;
    if (m_settled && qAbs(budget - m_lastDecisionBudget) < kDeadbandPx) {
        m_inReapplyDropPriority = false;
        return;
    }

    // Phase 0: restore everything we previously force-dropped.
    const QSet<StatusBadge*> dropped = m_droppedBadges;
    m_droppedBadges.clear();
    for (auto* b : dropped) {
        if (b) { b->setVisible(true); }
    }

    // Phase 1: try the wide (horizontal) layout. Unstack every pair
    // and check fit.
    if (m_modeFilterPair) { m_modeFilterPair->setStacked(false); }
    if (m_agcNrPair)      { m_agcNrPair->setStacked(false); }
    if (m_nbApfPair)      { m_nbApfPair->setStacked(false); }

    int contentWidth = sizeHint().width();
    if (contentWidth <= budget) {
        m_lastDecisionBudget = budget;
        m_settled = true;
        m_inReapplyDropPriority = false;
        return;
    }

    // Phase 2: medium (stacked pairs). Trades horizontal for vertical
    // and typically saves ~50% width.
    if (m_modeFilterPair) { m_modeFilterPair->setStacked(true); }
    if (m_agcNrPair)      { m_agcNrPair->setStacked(true); }
    if (m_nbApfPair)      { m_nbApfPair->setStacked(true); }
    contentWidth = sizeHint().width();
    if (contentWidth <= budget) {
        m_lastDecisionBudget = budget;
        m_settled = true;
        m_inReapplyDropPriority = false;
        return;
    }

    // Phase 3: narrow — pairs stay stacked AND we drop in priority
    // order. Drop sequence: SQL → APF → NB → NR → AGC. Mode and
    // filter never drop — they're the always-critical RX state.
    StatusBadge* dropOrder[] = {
        m_sqlBadge, m_apfBadge, m_nbBadge, m_nrBadge, m_agcBadge
    };
    for (auto* b : dropOrder) {
        if (contentWidth <= budget) { break; }
        if (b && b->isVisible()) {
            contentWidth -= (b->sizeHint().width() + 6);
            b->setVisible(false);
            m_droppedBadges.insert(b);
        }
    }

    m_lastDecisionBudget = budget;
    m_settled = true;
    m_inReapplyDropPriority = false;
}

void RxDashboard::bindSlice(SliceModel* slice)
{
    if (m_slice == slice) { return; }
    if (m_slice) {
        disconnect(m_slice, nullptr, this, nullptr);
    }
    m_slice = slice;
    if (!m_slice) { return; }

    // Wire slice signals — exact names verified against SliceModel.h 2026-04-30.
    connect(slice, &SliceModel::dspModeChanged,    this,
            [this](DSPMode m) { onModeChanged(static_cast<int>(m)); });
    connect(slice, &SliceModel::filterChanged,     this,
            [this](int low, int high) { onFilterChanged(low, high); });
    connect(slice, &SliceModel::agcModeChanged,    this,
            [this](AGCMode m) { onAgcChanged(static_cast<int>(m)); });
    connect(slice, &SliceModel::activeNrChanged,   this,
            [this](NrSlot s) { onNrChanged(static_cast<int>(s)); });
    connect(slice, &SliceModel::nbModeChanged,     this,
            [this](NbMode m) { onNbChanged(static_cast<int>(m)); });
    connect(slice, &SliceModel::apfEnabledChanged, this,
            &RxDashboard::onApfChanged);
    connect(slice, &SliceModel::ssqlEnabledChanged, this,
            &RxDashboard::onSsqlChanged);

    // Prime with current values (slice may already have state before binding)
    onModeChanged(static_cast<int>(slice->dspMode()));
    onFilterChanged(slice->filterLow(), slice->filterHigh());
    onAgcChanged(static_cast<int>(slice->agcMode()));
    onNrChanged(static_cast<int>(slice->activeNr()));
    onNbChanged(static_cast<int>(slice->nbMode()));
    onApfChanged(slice->apfEnabled());
    onSsqlChanged(slice->ssqlEnabled());
}

void RxDashboard::onModeChanged(int mode)
{
    // Use SliceModel::modeName() static helper (verified present in SliceModel.h).
    const QString name = SliceModel::modeName(static_cast<DSPMode>(mode));
    m_modeBadge->setLabel(name.isEmpty() ? QStringLiteral("—") : name);
    m_modeBadge->setVariant(StatusBadge::Variant::Info);
    m_modeBadge->setToolTip(name.isEmpty()
        ? tr("Operating mode")
        : tr("Mode: %1").arg(name));
}

void RxDashboard::onFilterChanged(int low, int high)
{
    // Display the total passband width (high − low for SSB/CW, or just high
    // for AM/FM where low ≤ 0). Matches Thetis filter-width display convention.
    const int passband = (low < 0) ? (high - low) : high;
    QString text;
    QString tipDetail;
    if (passband >= 1000) {
        text = QString::asprintf("%.1fk", passband / 1000.0);
        tipDetail = tr("%1 kHz").arg(QString::number(passband / 1000.0, 'f', 1));
    } else if (passband > 0) {
        text = QString::number(passband);
        tipDetail = tr("%1 Hz").arg(passband);
    } else {
        text = QStringLiteral("—");
    }
    m_filterBadge->setLabel(text);
    m_filterBadge->setVariant(StatusBadge::Variant::On);
    m_filterBadge->setToolTip(tipDetail.isEmpty()
        ? tr("Filter passband width")
        : tr("Filter passband: %1").arg(tipDetail));
}

void RxDashboard::onAgcChanged(int agcMode)
{
    // AGCMode: Off=0, Long=1, Slow=2, Med=3, Fast=4, Custom=5
    // Display single-letter abbreviation: O / L / S / M / F / C
    static const char* kAgcLetters[] = { "O", "L", "S", "M", "F", "C" };
    static const char* kAgcNames[]   = {
        "Off", "Long", "Slow", "Medium", "Fast", "Custom"
    };
    constexpr int kAgcCount = static_cast<int>(
        sizeof(kAgcLetters) / sizeof(kAgcLetters[0]));
    const QString letter = (agcMode >= 0 && agcMode < kAgcCount)
        ? QString::fromLatin1(kAgcLetters[agcMode])
        : QStringLiteral("—");
    const QString full = (agcMode >= 0 && agcMode < kAgcCount)
        ? QString::fromLatin1(kAgcNames[agcMode])
        : QStringLiteral("—");
    m_agcBadge->setLabel(letter);
    m_agcBadge->setVariant(StatusBadge::Variant::Info);
    m_agcBadge->setToolTip(tr("AGC %1").arg(full));
}

void RxDashboard::onNrChanged(int nrSlot)
{
    // NrSlot: Off=0, NR1=1, NR2=2, NR3=3, NR4=4, DFNR=5, BNR=6, MNR=7
    if (nrSlot <= 0) {
        m_droppedBadges.remove(m_nrBadge);   // feature turned off — clear drop status
        m_nrBadge->setVisible(false);
        return;
    }
    static const char* kNrLabels[] = {
        "Off", "NR1", "NR2", "NR3", "NR4", "DFNR", "BNR", "MNR"
    };
    constexpr int kNrCount = static_cast<int>(
        sizeof(kNrLabels) / sizeof(kNrLabels[0]));
    const QString name = (nrSlot > 0 && nrSlot < kNrCount)
        ? QString::fromLatin1(kNrLabels[nrSlot])
        : QStringLiteral("NR");
    m_nrBadge->setLabel(name);
    m_nrBadge->setVariant(StatusBadge::Variant::On);
    m_nrBadge->setToolTip(tr("Noise reduction %1 active").arg(name));
    m_droppedBadges.remove(m_nrBadge);   // feature explicitly turned on — clear drop
    m_nrBadge->setVisible(true);
    reapplyDropPriority();   // budget may still be tight; drop something else if needed
}

void RxDashboard::onNbChanged(int nbMode)
{
    // NbMode: Off=0, NB=1, NB2=2
    if (nbMode <= 0) {
        m_droppedBadges.remove(m_nbBadge);   // feature turned off — clear drop status
        m_nbBadge->setVisible(false);
        return;
    }
    const QString label = (nbMode == 2)
        ? QStringLiteral("NB2")
        : QStringLiteral("NB");
    m_nbBadge->setLabel(label);
    m_nbBadge->setVariant(StatusBadge::Variant::On);
    m_nbBadge->setToolTip(tr("Noise blanker %1 active").arg(label));
    m_droppedBadges.remove(m_nbBadge);   // feature explicitly turned on — clear drop
    m_nbBadge->setVisible(true);
    reapplyDropPriority();   // budget may still be tight; drop something else if needed
}

void RxDashboard::onApfChanged(bool active)
{
    if (!active) {
        m_droppedBadges.remove(m_apfBadge);   // feature turned off — clear drop status
        m_apfBadge->setVisible(false);
        return;
    }
    m_apfBadge->setLabel(QStringLiteral("APF"));
    m_apfBadge->setVariant(StatusBadge::Variant::On);
    m_apfBadge->setToolTip(tr(kApfTooltip));
    m_droppedBadges.remove(m_apfBadge);   // feature explicitly turned on — clear drop
    m_apfBadge->setVisible(true);
    reapplyDropPriority();   // budget may still be tight; drop something else if needed
}

void RxDashboard::onSsqlChanged(bool active)
{
    if (!active) {
        m_droppedBadges.remove(m_sqlBadge);   // feature turned off — clear drop status
        m_sqlBadge->setVisible(false);
        return;
    }
    m_sqlBadge->setLabel(QStringLiteral("SQL"));
    m_sqlBadge->setVariant(StatusBadge::Variant::On);
    m_sqlBadge->setToolTip(tr(kSqlTooltip));
    m_droppedBadges.remove(m_sqlBadge);   // feature explicitly turned on — clear drop
    m_sqlBadge->setVisible(true);
    reapplyDropPriority();   // budget may still be tight; drop something else if needed
}

} // namespace NereusSDR
