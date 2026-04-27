#pragma once

// =================================================================
// src/gui/applets/TxApplet.h  (NereusSDR)
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
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Layout pattern from AetherSDR `src/gui/TxApplet.{h,cpp}`.
//                 Wiring deferred to Phase 3M.
//   2026-04-26 — Phase 3M-1a H.3: TUNE/MOX/Tune-Power/RF-Power deep-wired.
//                 Out-of-phase controls hidden with TODO comments.
//                 syncFromModel() activates. setCurrentBand(Band) added for
//                 tune-power slider sync on band change.
// =================================================================

#include "AppletWidget.h"
#include "models/Band.h"

class QPushButton;
class QSlider;
class QComboBox;
class QLabel;

namespace NereusSDR {

// TxApplet — transmit controls panel.
//
// Layout (AetherSDR TxApplet.cpp pattern):
//  1.  Forward Power gauge  — HGauge 0–120 W, red > 100 W
//  2.  SWR gauge            — HGauge 1.0–3.0, red > 2.5
//  3.  RF Power slider row  — label(62) + slider + value(22)
//  4.  Tune Power slider row
//  5.  MOX button           — checkable, red when active
//  6.  TUNE button          — checkable, red + "TUNING..." when active
//  7.  ATU button           — checkable
//  8.  MEM button           — checkable
//  9.  TX Profile combo     — "Default" item
// 10.  Tune mode combo
// 11.  2-Tone test button   — hidden until Phase 3M-3 (out-of-phase)
// 12.  PS-A toggle          — hidden until Phase 3M-4 (out-of-phase)
// 13.  DUP (full duplex)    — checkable
// 14.  xPA indicator button — checkable
// 15.  SWR protection LED   — QLabel indicator
//
// Phase 3M-1a H.3: TUNE/MOX/Tune-Power/RF-Power are deep-wired.
// Out-of-phase controls (2-Tone, PS-A) are hidden.
class TxApplet : public AppletWidget {
    Q_OBJECT
public:
    explicit TxApplet(RadioModel* model, QWidget* parent = nullptr);

    QString appletId() const override { return QStringLiteral("TX"); }
    QString appletTitle() const override { return QStringLiteral("TX"); }
    void syncFromModel() override;

    // Called by MainWindow when band changes so the Tune Power slider
    // can reflect the stored per-band tune power.
    // Phase 3M-1a H.3.
    void setCurrentBand(Band band);

private:
    void buildUI();
    void wireControls();  // called after buildUI() — attaches signals/slots

    // 1. Forward Power gauge
    QWidget* m_fwdPowerGauge  = nullptr;
    // 2. SWR gauge
    QWidget* m_swrGauge       = nullptr;
    // 3. RF Power
    QSlider* m_rfPowerSlider  = nullptr;
    QLabel*  m_rfPowerValue   = nullptr;
    // 4. Tune Power
    QSlider* m_tunePwrSlider  = nullptr;
    QLabel*  m_tunePwrValue   = nullptr;
    // 5. MOX
    QPushButton* m_moxBtn     = nullptr;
    // 6. TUNE
    QPushButton* m_tuneBtn    = nullptr;
    // 7. ATU
    QPushButton* m_atuBtn     = nullptr;
    // 8. MEM
    QPushButton* m_memBtn     = nullptr;
    // 9. TX Profile combo
    QComboBox*   m_profileCombo = nullptr;
    // 10. Tune mode combo
    QComboBox*   m_tuneModeCombo = nullptr;
    // 11. 2-Tone test
    QPushButton* m_twoToneBtn = nullptr;
    // 12. PS-A
    QPushButton* m_psaBtn     = nullptr;
    // 13. DUP (full duplex)
    QPushButton* m_dupBtn     = nullptr;
    // 14. xPA indicator
    QPushButton* m_xpaBtn     = nullptr;
    // 15. SWR protection LED
    QLabel*      m_swrProtLed = nullptr;

    // Current band — used to resolve per-band tune power.
    // Updated by setCurrentBand() when PanadapterModel::bandChanged fires.
    Band m_currentBand{Band::Band20m};

    // Flag preventing echo loops between the model and the UI.
    bool m_updatingFromModel{false};
};

} // namespace NereusSDR
