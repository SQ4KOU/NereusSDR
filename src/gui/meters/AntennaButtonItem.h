#pragma once

#include "ButtonBoxItem.h"

namespace NereusSDR {

// Antenna selection: Rx Ant 1-3, Rx Aux 1-2, Tx Ant 1-3, Rx/Tx toggle.
// Ported from Thetis clsAntennaButtonBox (MeterManager.cs:9502+).
class AntennaButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    explicit AntennaButtonItem(QObject* parent = nullptr);

    void setActiveRxAntenna(int index);
    void setActiveTxAntenna(int index);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void antennaSelected(int index);

private:
    void onButtonClicked(int index, Qt::MouseButton button);
    int m_activeRxAnt{0};
    int m_activeTxAnt{0};
    static constexpr int kAntennaCount = 10;
};

} // namespace NereusSDR
