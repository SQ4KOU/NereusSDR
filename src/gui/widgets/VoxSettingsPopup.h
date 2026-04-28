// =================================================================
// src/gui/widgets/VoxSettingsPopup.h  (NereusSDR)
// =================================================================
//
// NereusSDR-native widget — VOX quick-settings popup.
// Mirrors the DspParamPopup pattern (AetherSDR-derived) but is
// specific to the VOX parameters on TransmitModel.
//
// No Thetis source equivalent: Thetis embeds VOX controls in the
// Setup form rather than a right-click popup.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-28 — Phase 3M-1b J.2: Created by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

#pragma once

#include <QWidget>
#include <functional>

class QVBoxLayout;
class QSlider;
class QLabel;

namespace NereusSDR {

// VoxSettingsPopup — floating right-click popup for VOX quick-parameter access.
//
// Contains 3 sliders bound to TransmitModel VOX properties:
//   - Threshold (dB):  range [-80, 0]   from Thetis console.Designer.cs:6018-6019 [v2.10.3.13]
//   - Gain (scalar):   range [0.0, 2.0] (display: 0–200 integer mapped from x100 scalar)
//   - Hang Time (ms):  range [1, 2000]  from Thetis setup.designer.cs:45005-45013 [v2.10.3.13]
//
// Auto-dismisses on click outside (Qt::Popup window flag).
// ShowAt() positions the popup so it stays on-screen.
class VoxSettingsPopup : public QWidget {
    Q_OBJECT

public:
    explicit VoxSettingsPopup(
        int   currentThresholdDb,
        float currentGainScalar,
        int   currentHangTimeMs,
        QWidget* parent = nullptr);

    // Show anchored to a position (typically the button's global pos).
    void showAt(const QPoint& globalPos);

    // Accessors so tests can verify slider counts.
    int sliderCount() const { return m_sliderCount; }

signals:
    void thresholdDbChanged(int dB);
    void gainScalarChanged(float scalar);
    void hangTimeMsChanged(int ms);

private:
    QVBoxLayout* m_layout{nullptr};
    int          m_sliderCount{0};
};

} // namespace NereusSDR
