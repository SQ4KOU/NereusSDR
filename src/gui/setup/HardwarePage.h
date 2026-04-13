#pragma once

#include "gui/SetupPage.h"

#include <QWidget>

class QTabWidget;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class RadioInfoTab;
class AntennaAlexTab;
class OcOutputsTab;
class XvtrTab;
class PureSignalTab;
class DiversityTab;
class PaCalibrationTab;
class Hl2IoBoardTab;
class BandwidthMonitorTab;

// HardwarePage — top-level "Hardware Config" entry in SetupDialog.
//
// Contains a nested QTabWidget that mirrors Thetis's Setup.cs Hardware Config
// sub-tabs. Tab visibility is capability-gated: call onCurrentRadioChanged()
// whenever the connected radio (or its BoardCapabilities) change. Tasks 19/20
// populate the individual tab widgets; for now they are empty stubs.
class HardwarePage : public SetupPage {
    Q_OBJECT
public:
    explicit HardwarePage(RadioModel* model, QWidget* parent = nullptr);
    ~HardwarePage() override;

public slots:
    // Reconciles tab visibility from BoardCapabilities flags.
    void onCurrentRadioChanged(const RadioInfo& info);

#ifdef NEREUS_BUILD_TESTS
    enum class Tab {
        RadioInfo, AntennaAlex, OcOutputs, Xvtr, PureSignal,
        Diversity, PaCalibration, Hl2IoBoard, BandwidthMonitor
    };
    bool isTabVisibleForTest(Tab t) const;
#endif

private:
    RadioModel*  m_model{nullptr};
    QTabWidget*  m_tabs{nullptr};

    RadioInfoTab*        m_radioInfoTab{nullptr};
    AntennaAlexTab*      m_antennaAlexTab{nullptr};
    OcOutputsTab*        m_ocOutputsTab{nullptr};
    XvtrTab*             m_xvtrTab{nullptr};
    PureSignalTab*       m_pureSignalTab{nullptr};
    DiversityTab*        m_diversityTab{nullptr};
    PaCalibrationTab*    m_paCalTab{nullptr};
    Hl2IoBoardTab*       m_hl2IoTab{nullptr};
    BandwidthMonitorTab* m_bwMonitorTab{nullptr};

    int m_radioInfoIdx{-1};
    int m_antennaAlexIdx{-1};
    int m_ocOutputsIdx{-1};
    int m_xvtrIdx{-1};
    int m_pureSignalIdx{-1};
    int m_diversityIdx{-1};
    int m_paCalIdx{-1};
    int m_hl2IoIdx{-1};
    int m_bwMonitorIdx{-1};
};

} // namespace NereusSDR
