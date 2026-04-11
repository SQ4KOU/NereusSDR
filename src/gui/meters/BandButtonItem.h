#pragma once

#include "ButtonBoxItem.h"

namespace NereusSDR {

// Band selection grid: 160m-6m + GEN.
// Ported from Thetis clsBandButtonBox (MeterManager.cs:11482+).
class BandButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    explicit BandButtonItem(QObject* parent = nullptr);

    void setActiveBand(int index);
    int activeBand() const { return m_activeBand; }

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void bandClicked(int bandIndex);
    // From Thetis PopupBandstack (MeterManager.cs:11896)
    void bandStackRequested(int bandIndex);

private:
    void onButtonClicked(int index, Qt::MouseButton button);
    int m_activeBand{-1};
    static constexpr int kBandCount = 12;
};

} // namespace NereusSDR
