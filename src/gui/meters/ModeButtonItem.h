#pragma once

#include "ButtonBoxItem.h"

namespace NereusSDR {

// Mode selection buttons: LSB, USB, DSB, CWL, CWU, FM, AM, SAM, DIGL, DIGU.
// Ported from Thetis clsModeButtonBox (MeterManager.cs:9951+).
class ModeButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    explicit ModeButtonItem(QObject* parent = nullptr);

    void setActiveMode(int index);
    int activeMode() const { return m_activeMode; }

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void modeClicked(int modeIndex);

private:
    void onButtonClicked(int index, Qt::MouseButton button);
    int m_activeMode{-1};
    static constexpr int kModeCount = 10;
};

} // namespace NereusSDR
