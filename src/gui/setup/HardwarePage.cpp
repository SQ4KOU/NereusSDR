// HardwarePage.cpp
//
// Top-level Hardware Config page for SetupDialog. Wraps a QTabWidget with
// 9 sub-tabs mirroring Thetis Setup.cs Hardware Config sub-tabs. Tab
// visibility is reconciled from BoardCapabilities flags whenever the
// connected radio changes.

#include "HardwarePage.h"

#include "hardware/RadioInfoTab.h"
#include "hardware/AntennaAlexTab.h"
#include "hardware/OcOutputsTab.h"
#include "hardware/XvtrTab.h"
#include "hardware/PureSignalTab.h"
#include "hardware/DiversityTab.h"
#include "hardware/PaCalibrationTab.h"
#include "hardware/Hl2IoBoardTab.h"
#include "hardware/BandwidthMonitorTab.h"

#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include "models/RadioModel.h"

#include <QTabWidget>
#include <QVBoxLayout>

namespace NereusSDR {

// ── Construction ──────────────────────────────────────────────────────────────

HardwarePage::HardwarePage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("Hardware Config"), model, parent)
    , m_model(model)
{
    // Replace the default SetupPage content area with a plain tab widget so
    // the sub-tabs fill the whole right pane. We insert the QTabWidget into
    // the inherited contentLayout() so SetupPage's title header is preserved.
    m_tabs = new QTabWidget(this);
    m_tabs->setTabPosition(QTabWidget::North);
    contentLayout()->setContentsMargins(0, 0, 0, 0);
    contentLayout()->addWidget(m_tabs);

    // ── Create stub tab widgets ───────────────────────────────────────────────
    m_radioInfoTab    = new RadioInfoTab(model, this);
    m_antennaAlexTab  = new AntennaAlexTab(model, this);
    m_ocOutputsTab    = new OcOutputsTab(model, this);
    m_xvtrTab         = new XvtrTab(model, this);
    m_pureSignalTab   = new PureSignalTab(model, this);
    m_diversityTab    = new DiversityTab(model, this);
    m_paCalTab        = new PaCalibrationTab(model, this);
    m_hl2IoTab        = new Hl2IoBoardTab(model, this);
    m_bwMonitorTab    = new BandwidthMonitorTab(model, this);

    // ── Add tabs — order mirrors Thetis Setup.cs Hardware Config tab strip ────
    m_radioInfoIdx   = m_tabs->addTab(m_radioInfoTab,   tr("Radio Info"));
    m_antennaAlexIdx = m_tabs->addTab(m_antennaAlexTab, tr("Antenna / ALEX"));
    m_ocOutputsIdx   = m_tabs->addTab(m_ocOutputsTab,   tr("OC Outputs"));
    m_xvtrIdx        = m_tabs->addTab(m_xvtrTab,        tr("XVTR"));
    m_pureSignalIdx  = m_tabs->addTab(m_pureSignalTab,  tr("PureSignal"));
    m_diversityIdx   = m_tabs->addTab(m_diversityTab,   tr("Diversity"));
    m_paCalIdx       = m_tabs->addTab(m_paCalTab,       tr("PA Calibration"));
    m_hl2IoIdx       = m_tabs->addTab(m_hl2IoTab,       tr("HL2 I/O"));
    m_bwMonitorIdx   = m_tabs->addTab(m_bwMonitorTab,   tr("Bandwidth Monitor"));

    // All tabs start visible; onCurrentRadioChanged() hides the gated ones
    // once a radio connects. Task 21 will wire the RadioModel signal.
}

HardwarePage::~HardwarePage() = default;

// ── onCurrentRadioChanged ─────────────────────────────────────────────────────

void HardwarePage::onCurrentRadioChanged(const RadioInfo& info)
{
    const BoardCapabilities& caps = BoardCapsTable::forBoard(info.boardType);

    // Radio Info is always visible.
    // Remaining tabs are shown only when the connected board supports them.
    m_tabs->setTabVisible(m_antennaAlexIdx, caps.hasAlexFilters);
    m_tabs->setTabVisible(m_ocOutputsIdx,   caps.ocOutputCount > 0);
    m_tabs->setTabVisible(m_xvtrIdx,        caps.xvtrJackCount > 0);
    m_tabs->setTabVisible(m_pureSignalIdx,  caps.hasPureSignal);
    m_tabs->setTabVisible(m_diversityIdx,   caps.hasDiversityReceiver);
    m_tabs->setTabVisible(m_paCalIdx,       caps.hasPaProfile);
    m_tabs->setTabVisible(m_hl2IoIdx,       caps.hasIoBoardHl2);
    m_tabs->setTabVisible(m_bwMonitorIdx,   caps.hasBandwidthMonitor);

    // Notify each tab so it can cache the info for Task 19/20 populate() calls.
    m_radioInfoTab->populate(info, caps);
    m_antennaAlexTab->populate(info, caps);
    m_ocOutputsTab->populate(info, caps);
    m_xvtrTab->populate(info, caps);
    m_pureSignalTab->populate(info, caps);
    m_diversityTab->populate(info, caps);
    m_paCalTab->populate(info, caps);
    m_hl2IoTab->populate(info, caps);
    m_bwMonitorTab->populate(info, caps);
}

// ── Test helper ───────────────────────────────────────────────────────────────

#ifdef NEREUS_BUILD_TESTS
bool HardwarePage::isTabVisibleForTest(Tab t) const
{
    switch (t) {
        case Tab::RadioInfo:        return m_tabs->isTabVisible(m_radioInfoIdx);
        case Tab::AntennaAlex:      return m_tabs->isTabVisible(m_antennaAlexIdx);
        case Tab::OcOutputs:        return m_tabs->isTabVisible(m_ocOutputsIdx);
        case Tab::Xvtr:             return m_tabs->isTabVisible(m_xvtrIdx);
        case Tab::PureSignal:       return m_tabs->isTabVisible(m_pureSignalIdx);
        case Tab::Diversity:        return m_tabs->isTabVisible(m_diversityIdx);
        case Tab::PaCalibration:    return m_tabs->isTabVisible(m_paCalIdx);
        case Tab::Hl2IoBoard:       return m_tabs->isTabVisible(m_hl2IoIdx);
        case Tab::BandwidthMonitor: return m_tabs->isTabVisible(m_bwMonitorIdx);
    }
    return false;
}
#endif

} // namespace NereusSDR
