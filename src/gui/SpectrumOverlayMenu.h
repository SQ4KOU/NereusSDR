#pragma once

// =================================================================
// src/gui/SpectrumOverlayMenu.h  (NereusSDR)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-12 - Export signal addresses for Windows DLL consumers.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Overlay-menu pattern from AetherSDR
//                 `src/gui/SpectrumOverlayMenu.{h,cpp}`.
//   2026-08-08 - Adapted the 3D VIEW declarations from AetherSDR
//                 src/gui/SpectrumOverlayMenu.h:380-387 [@1872028c]
//                 for NereusSDR by J.J. Boyd (KG4VCF), with AI assistance
//                 via Anthropic Claude Code.
//   2026-10-01 - Completed 3D port history during PR review by
//                 J.J. Boyd (KG4VCF), with AI assistance via OpenAI Codex.
//   2026-10-10 - Display pill plan Task 19: trimmed to CTUN, Add notch
//                 here, Ref Level, Dyn Range and a "Display settings..."
//                 line. The waterfall, fill and 3D VIEW rows are in the
//                 pan's Display pill. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-12 - setCtunAvailable takes the reason the C-Tune switch is
//                 held. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

#include <QWidget>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include "gui/NereusGuiExport.h"

namespace NereusSDR {

// Right-click menu of a pan: the dB range (Ref Level and Dyn Range), CTUN,
// "Add notch here" and one line that opens the pan's Display pill, which
// holds every other display control (display pill design, section 7).
//
// Self-contained QWidget popup — communicates via signals only.
// No awareness of containers or docking. Designed so it can later
// be wrapped in a ContainerWidget (Phase 3F) without changes.
//
// From plan Step 9 / AetherSDR SpectrumOverlayMenu pattern.
class NEREUS_GUI_EXPORT SpectrumOverlayMenu : public QWidget {
    Q_OBJECT

public:
    explicit SpectrumOverlayMenu(QWidget* parent = nullptr);

    // Set current values (called before showing). Does not emit.
    void setValues(float refLevel, float dynRange, bool ctun = true);

    // `heldReason`, when C-Tune is available: the receiver's setting is not
    // this window's to change, and the switch is disabled with these words.
    void setCtunAvailable(bool available, const QString& heldReason = QString());
    void setCtunState(bool pinned);

    // Absolute RF Hz under the cursor at popup time.  The caller sets it
    // just before show(); the Notch section's button carries it back out
    // through notchAddRequested.  Kept separate from setValues because it
    // changes on every right-click while the display knobs above do not.
    void setNotchAddFrequency(double freqHz);

signals:
    void refLevelChanged(float dBm);
    void dynRangeChanged(float dB);
    void ctunChanged(bool enabled);

    // Tunable notch filter: "Add notch here" pressed.  freqHz is absolute
    // RF in Hz, the value last handed to setNotchAddFrequency.  Every
    // frequency crossing the TNF signal boundary is Hz; the only MHz
    // quantity in the stack is SpectrumWidget::NotchMarker::freqMhz.
    void notchAddRequested(double freqHz);

    // "Display settings..." pressed: open this pan's Display pill. The
    // menu has hidden itself by the time this is emitted.
    void displaySettingsRequested();

private:
    void buildUI();
    void updateNotchAddLabel();

    QSlider*   m_refLevelSlider{nullptr};
    QSlider*   m_dynRangeSlider{nullptr};
    QLabel*    m_refLevelLabel{nullptr};
    QLabel*    m_dynRangeLabel{nullptr};
    QCheckBox* m_ctunCheck{nullptr};

    // ---- Notch section ----
    QPushButton* m_notchAddButton{nullptr};
    QLabel*      m_notchFreqLabel{nullptr};
    double       m_notchAddFreqHz{0.0};

    // ---- Display pill line ----
    QPushButton* m_displaySettingsButton{nullptr};
};

} // namespace NereusSDR
