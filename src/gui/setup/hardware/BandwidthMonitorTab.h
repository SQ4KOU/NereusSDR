#pragma once
// BandwidthMonitorTab.h
//
// "Bandwidth Monitor" sub-tab of HardwarePage.
// Shows live LAN PHY rate and throttle state; provides a user-adjustable
// throttle threshold and an auto-pause toggle.
//
// Source: ChannelMaster/bandwidth_monitor.h —
//   bandwidth_monitor_reset() (line 47), bandwidth_monitor_in/out(bytes)
//   (lines 48-49). The .h exposes a reset + byte-accounting API; NereusSDR
//   Phase 3I wires the static controls only. Live feed from
//   P1RadioConnection's bandwidth monitor deferred to Phase 3L / Task 21.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QGroupBox;
class QLabel;
class QSpinBox;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class BandwidthMonitorTab : public QWidget {
    Q_OBJECT
public:
    explicit BandwidthMonitorTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

public slots:
    // Called by a future RadioModel / P1RadioConnection signal when live data
    // arrives. Phase 3I leaves this unconnected; Task 21 / Phase 3L wires it.
    void updateLiveStats(int currentMbps, int throttleEventCount);

private:
    RadioModel*  m_model{nullptr};

    // Live read-only labels
    QLabel*      m_currentRateLabel{nullptr};
    QLabel*      m_throttleEventLabel{nullptr};

    // User-adjustable controls
    QSpinBox*    m_throttleThresholdSpin{nullptr};
    QCheckBox*   m_autoPauseCheck{nullptr};
};

} // namespace NereusSDR
