#pragma once

#include <QWidget>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>

namespace NereusSDR {

// Right-click overlay menu for SpectrumWidget display settings.
// Tier 2 settings: occasional adjustment (per-band or per-mode).
//
// Self-contained QWidget popup — communicates via signals only.
// No awareness of containers or docking. Designed so it can later
// be wrapped in a ContainerWidget (Phase 3F) without changes.
//
// From plan Step 9 / AetherSDR SpectrumOverlayMenu pattern.
class SpectrumOverlayMenu : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumOverlayMenu(QWidget* parent = nullptr);

    // Set current values (called before showing)
    void setValues(int wfColorGain, int wfBlackLevel, bool autoBlack,
                   int wfScheme, float fillAlpha, bool panFill,
                   bool heatMap, float refLevel, float dynRange,
                   bool ctunEnabled = true);

signals:
    void wfColorGainChanged(int gain);
    void wfBlackLevelChanged(int level);
    void wfColorSchemeChanged(int scheme);
    void fillAlphaChanged(float alpha);
    void panFillChanged(bool on);
    void refLevelChanged(float dBm);
    void dynRangeChanged(float dB);
    void ctunChanged(bool enabled);

private:
    void buildUI();

    QSlider*   m_wfGainSlider{nullptr};
    QSlider*   m_wfBlackSlider{nullptr};
    QComboBox* m_schemeCombo{nullptr};
    QSlider*   m_fillAlphaSlider{nullptr};
    QCheckBox* m_panFillCheck{nullptr};
    QSlider*   m_refLevelSlider{nullptr};
    QSlider*   m_dynRangeSlider{nullptr};
    QLabel*    m_wfGainLabel{nullptr};
    QLabel*    m_wfBlackLabel{nullptr};
    QLabel*    m_fillAlphaLabel{nullptr};
    QLabel*    m_refLevelLabel{nullptr};
    QLabel*    m_dynRangeLabel{nullptr};
    QCheckBox* m_ctunCheck{nullptr};
};

} // namespace NereusSDR
