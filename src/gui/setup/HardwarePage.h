#pragma once

#include "gui/SetupPage.h"

#include <QMap>
#include <QString>
#include <QVariant>
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
// populate the individual tab widgets; Task 21 adds per-MAC persistence.
class HardwarePage : public SetupPage {
    Q_OBJECT
public:
    explicit HardwarePage(RadioModel* model, QWidget* parent = nullptr);
    ~HardwarePage() override;

#ifdef NEREUS_BUILD_TESTS
    enum class Tab {
        RadioInfo, AntennaAlex, OcOutputs, Xvtr, PureSignal,
        Diversity, PaCalibration, Hl2IoBoard, BandwidthMonitor
    };
    bool isTabVisibleForTest(Tab t) const;
#endif

public slots:
    // Reconciles tab visibility from BoardCapabilities flags and restores
    // persisted values for the incoming radio's MAC.
    void onCurrentRadioChanged(const RadioInfo& info);

private slots:
    // Write-through slot: stores tab setting under hardware/<mac>/<tabKey>/<key>.
    void onTabSettingChanged(const QString& tabKey,
                             const QString& key,
                             const QVariant& value);

private:
    // Extract entries whose key starts with prefix and return them with the
    // prefix stripped.
    static QMap<QString, QVariant> filterPrefix(const QMap<QString, QVariant>& map,
                                                 const QString& prefix);

    RadioModel*  m_model{nullptr};
    QTabWidget*  m_tabs{nullptr};

    // MAC address of the currently displayed radio; empty if none.
    QString      m_currentMac;

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
