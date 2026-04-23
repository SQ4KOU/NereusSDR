// =================================================================
// src/gui/setup/DspSetupPages.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/setup.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// setup.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Continual modifications Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//=================================================================
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

#include "DspSetupPages.h"

#include "core/AppSettings.h"
#include "core/wdsp_api.h"
#include "models/RadioModel.h"
#include "core/WdspEngine.h"
#include "models/SliceModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace NereusSDR {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: disable every child widget inside a group box (NYI guard).
// ─────────────────────────────────────────────────────────────────────────────
static void disableGroup(QGroupBox* grp)
{
    grp->setEnabled(false);
}

// ══════════════════════════════════════════════════════════════════════════════
// AgcAlcSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageAGC controls:
//   comboAGCMode, udAGCAttack, udAGCDecay, udAGCHang, tbAGCSlope,
//   udAGCMaxGain, tbAGCHangThreshold, udALCDecay, udALCMaxGain,
//   chkLevelerEnable, udLevelerThreshold, udLevelerDecay
//
AgcAlcSetupPage::AgcAlcSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("AGC/ALC", model, parent)
{
    SliceModel* slice = model->activeSlice();
    if (!slice) {
        // No active slice (disconnected) — show disabled placeholder
        QGroupBox* grp = addSection("RX1 AGC");
        disableGroup(grp);
        return;
    }

    // ── RX1 AGC ──────────────────────────────────────────────────────────────
    QGroupBox* agcGrp = addSection("RX1 AGC");
    QVBoxLayout* agcLay = qobject_cast<QVBoxLayout*>(agcGrp->layout());

    m_agcModeCombo = new QComboBox;
    m_agcModeCombo->addItems({"Off", "Long", "Slow", "Med", "Fast", "Custom"});
    m_agcModeCombo->setCurrentIndex(static_cast<int>(slice->agcMode()));
    // From Thetis v2.10.3.13 console.resx:4555 — comboAGC.ToolTip
    m_agcModeCombo->setToolTip(QStringLiteral("Automatic Gain Control Mode Setting"));
    addLabeledCombo(agcLay, "Mode", m_agcModeCombo);

    m_agcAttack = new QSpinBox;
    m_agcAttack->setRange(1, 1000);
    m_agcAttack->setSuffix(" ms");
    m_agcAttack->setValue(slice->agcAttack());
    addLabeledSpinner(agcLay, "Attack", m_agcAttack);

    m_agcDecay = new QSpinBox;
    m_agcDecay->setRange(1, 5000);
    m_agcDecay->setSuffix(" ms");
    m_agcDecay->setValue(slice->agcDecay());
    // From Thetis v2.10.3.13 setup.designer.cs:39390 — udDSPAGCDecay.ToolTip
    m_agcDecay->setToolTip(QStringLiteral("Time-constant to increase signal amplitude after strong signal"));
    addLabeledSpinner(agcLay, "Decay", m_agcDecay);

    m_agcHang = new QSpinBox;
    m_agcHang->setRange(10, 5000);
    m_agcHang->setSuffix(" ms");
    m_agcHang->setValue(slice->agcHang());
    // From Thetis v2.10.3.13 setup.designer.cs:39294 — udDSPAGCHangTime.ToolTip
    m_agcHang->setToolTip(QStringLiteral("Time to hold constant gain after strong signal"));
    addLabeledSpinner(agcLay, "Hang", m_agcHang);

    m_agcSlope = new QSlider(Qt::Horizontal);
    m_agcSlope->setRange(0, 20);
    m_agcSlope->setValue(slice->agcSlope() / 10);
    // From Thetis v2.10.3.13 setup.designer.cs:39358 — udDSPAGCSlope.ToolTip
    m_agcSlope->setToolTip(QStringLiteral("Gain difference for weak and strong signals"));
    addLabeledSlider(agcLay, "Slope", m_agcSlope);

    m_agcMaxGain = new QSpinBox;
    m_agcMaxGain->setRange(-20, 120);
    m_agcMaxGain->setSuffix(" dB");
    m_agcMaxGain->setValue(slice->agcMaxGain());
    // From Thetis v2.10.3.13 setup.designer.cs:39325 — udDSPAGCMaxGaindB.ToolTip
    m_agcMaxGain->setToolTip(QStringLiteral("Threshold AGC: no gain over this Max Gain is applied, irrespective of signal weakness"));
    addLabeledSpinner(agcLay, "Max Gain", m_agcMaxGain);

    m_agcFixedGain = new QSpinBox;
    m_agcFixedGain->setRange(-20, 120);
    m_agcFixedGain->setSuffix(" dB");
    m_agcFixedGain->setValue(slice->agcFixedGain());
    // From Thetis v2.10.3.13 setup.designer.cs:39448 — udDSPAGCFixedGaindB.ToolTip
    m_agcFixedGain->setToolTip(QStringLiteral("Gain when AGC is disabled"));
    addLabeledSpinner(agcLay, "Fixed Gain", m_agcFixedGain);

    m_agcHangThresh = new QSlider(Qt::Horizontal);
    m_agcHangThresh->setRange(0, 100);
    m_agcHangThresh->setValue(slice->agcHangThreshold());
    // From Thetis v2.10.3.13 setup.designer.cs:39250 — tbDSPAGCHangThreshold.ToolTip
    m_agcHangThresh->setToolTip(QStringLiteral("Level at which the 'hang' function is engaged"));
    addLabeledSlider(agcLay, "Hang Threshold", m_agcHangThresh);

    // ── Wire AGC controls to SliceModel ──────────────────────────────────────

    connect(m_agcModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [slice](int idx) {
        slice->setAgcMode(static_cast<AGCMode>(idx));
    });
    connect(m_agcModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        updateCustomGating(static_cast<AGCMode>(idx));
    });

    connect(m_agcAttack, QOverload<int>::of(&QSpinBox::valueChanged),
            slice, &SliceModel::setAgcAttack);

    connect(m_agcDecay, QOverload<int>::of(&QSpinBox::valueChanged),
            slice, &SliceModel::setAgcDecay);

    connect(m_agcHang, QOverload<int>::of(&QSpinBox::valueChanged),
            slice, &SliceModel::setAgcHang);

    connect(m_agcSlope, &QSlider::valueChanged,
            this, [slice](int val) {
        slice->setAgcSlope(val * 10);  // UI 0-20, WDSP gets ×10
    });

    connect(m_agcMaxGain, QOverload<int>::of(&QSpinBox::valueChanged),
            slice, &SliceModel::setAgcMaxGain);

    connect(m_agcFixedGain, QOverload<int>::of(&QSpinBox::valueChanged),
            slice, &SliceModel::setAgcFixedGain);

    connect(m_agcHangThresh, &QSlider::valueChanged,
            slice, &SliceModel::setAgcHangThreshold);

    // ── Auto AGC ─────────────────────────────────────────────────────────────
    QGroupBox* autoAgcGrp = addSection("Auto AGC");
    QVBoxLayout* autoAgcLay = qobject_cast<QVBoxLayout*>(autoAgcGrp->layout());

    m_autoAgcChk = new QCheckBox("Auto AGC RX1");
    m_autoAgcChk->setChecked(slice->autoAgcEnabled());
    // From Thetis v2.10.3.13 setup.designer.cs:38679 — chkAutoAGCRX1.ToolTip
    m_autoAgcChk->setToolTip(QStringLiteral("Automatically adjust AGC based on Noise Floor"));
    autoAgcLay->addWidget(m_autoAgcChk);

    m_autoAgcOffset = new QSpinBox;
    m_autoAgcOffset->setRange(-60, 60);
    m_autoAgcOffset->setSuffix(" dB");
    m_autoAgcOffset->setValue(static_cast<int>(slice->autoAgcOffset()));
    // From Thetis v2.10.3.13 setup.designer.cs:38649 — udRX1AutoAGCOffset.ToolTip
    m_autoAgcOffset->setToolTip(QStringLiteral("dB shift from noise floor"));
    addLabeledSpinner(autoAgcLay, "± Offset", m_autoAgcOffset);

    connect(m_autoAgcChk, &QCheckBox::toggled,
            slice, &SliceModel::setAutoAgcEnabled);

    connect(m_autoAgcOffset, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [slice](int val) {
        slice->setAutoAgcOffset(static_cast<double>(val));
    });

    // ── Custom-mode gating ───────────────────────────────────────────────────
    // From Thetis v2.10.3.13 setup.cs:5046-5076 — CustomRXAGCEnabled
    updateCustomGating(slice->agcMode());

    // ── ALC ──────────────────────────────────────────────────────────────────
    QGroupBox* alcGrp = addSection("ALC");
    QVBoxLayout* alcLay = qobject_cast<QVBoxLayout*>(alcGrp->layout());

    auto* alcDecay = new QSpinBox;
    alcDecay->setRange(1, 5000);
    alcDecay->setSuffix(" ms");
    addLabeledSpinner(alcLay, "Decay", alcDecay);

    auto* alcMaxGain = new QSpinBox;
    alcMaxGain->setRange(0, 120);
    alcMaxGain->setSuffix(" dB");
    addLabeledSpinner(alcLay, "Max Gain", alcMaxGain);

    disableGroup(alcGrp);

    // ── Leveler ───────────────────────────────────────────────────────────────
    QGroupBox* levGrp = addSection("Leveler");
    QVBoxLayout* levLay = qobject_cast<QVBoxLayout*>(levGrp->layout());

    auto* levEnable = new QPushButton("Enable");
    addLabeledToggle(levLay, "Enable", levEnable);

    auto* levThresh = new QSpinBox;
    levThresh->setRange(-20, 20);
    levThresh->setSuffix(" dB");
    addLabeledSpinner(levLay, "Threshold", levThresh);

    auto* levDecay = new QSpinBox;
    levDecay->setRange(1, 5000);
    levDecay->setSuffix(" ms");
    addLabeledSpinner(levLay, "Decay", levDecay);

    disableGroup(levGrp);
}

// From Thetis v2.10.3.13 setup.cs:5046-5076 — CustomRXAGCEnabled
void AgcAlcSetupPage::updateCustomGating(AGCMode mode)
{
    const bool custom = (mode == AGCMode::Custom);
    m_agcDecay->setEnabled(custom);
    m_agcHang->setEnabled(custom);
}

// ══════════════════════════════════════════════════════════════════════════════
// NrAnfSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// Porting from Thetis setup.designer.cs NR/ANF controls [v2.10.3.13]:
//
//   NR1 tab: udDSPNRTaps (16-1024), udDSPNRDelay (1-256),
//            tbDSPNRGain (0-999), tbDSPNRLeak (0-999),
//            rdoDSPNRPreAGC / rdoDSPNRPostAGC
//   NR2 tab: rdoEMNRGainMethod 0-3 (Linear/Log/Gamma/Trained),
//            rdoEMNRNPEMethod 0-2 (OSMS/MMSE/NSTAT),
//            udDSPEMNRTrainT1 (−5.0..5.0 step 0.1),
//            udDSPEMNRTrainT2 (0.0..2.0 step 0.05),
//            chkDSPEMNRAEFilter, rdoDSPEMNRPreAGC / rdoDSPEMNRPostAGC,
//            Noise Post Proc group: chkDSPEMNRPost2Run,
//              udDSPEMNRPost2Level/Factor/Rate, udDSPEMNRPost2Taper
//   NR3 tab: rdoDSPNR3PreAGC / rdoDSPNR3PostAGC,
//            chkRXANR3FixedGain, model selector (global)
//   NR4 tab: udDSPSBNRreduction (0-20 step 0.1), udDSPSBNRsmooth (0-100),
//            udDSPSBNRwhiten (0-100), udDSPSBNRrescale (0-12 step 0.1),
//            udDSPSBNRsnrthresh (-10..10 step 0.5), rdoSBNR1/2/3
//   DFNR tab: (AetherSDR native, not Thetis)
//   MNR tab:  (AetherSDR native, macOS only, not Thetis)
//   ANF tab:  chkANFEnable (Thetis chkDSPANFEnable)
//
// Slice-tracking policy: binds to model->activeSlice() at construction
// (simple pattern matching AgcAlcSetupPage). Slice-switch requires
// close+reopen of Setup dialog. Full dynamic rebind deferred to Task 21.
//
NrAnfSetupPage::NrAnfSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("NR/ANF", model, parent)
{
    // Embed QTabWidget directly in the inherited contentLayout().
    // This is identical to HardwarePage's pattern (HardwarePage.cpp:90-95).
    auto* tabs = new QTabWidget(this);
    tabs->setTabPosition(QTabWidget::North);
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #304050; background: #0f0f1a; }"
        "QTabBar::tab { background: #1a2a3a; color: #8aa8c0; padding: 4px 10px; "
        "               border: 1px solid #304050; border-bottom: none; border-radius: 3px 3px 0 0; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #c8d8e8; }"
        "QTabBar::tab:hover { background: #203040; }");
    contentLayout()->setContentsMargins(0, 0, 0, 0);
    // Remove the trailing stretch that SetupPage adds in its ctor
    // (SetupPage.cpp:90 — m_contentLayout->addStretch(1)). That stretch
    // would otherwise compete with our tabs widget for vertical space
    // (50/50 split when both have stretch=1), leaving dead space below
    // the tab pane. With the stretch removed and our tabs getting
    // stretch=1 + Expanding policy, the tab pane fills all available
    // vertical space in the Setup dialog.
    {
        const int last = contentLayout()->count() - 1;
        if (auto* s = contentLayout()->itemAt(last); s && s->spacerItem()) {
            delete contentLayout()->takeAt(last);
        }
    }
    tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    contentLayout()->addWidget(tabs, /*stretch=*/1);

    SliceModel* slice = model ? model->activeSlice() : nullptr;

    // Helper: build a tab-page widget with a QVBoxLayout + scroll.
    // Returns {outer QWidget (used as tab), inner QVBoxLayout (for sections)}.
    auto makeTab = [](QTabWidget* tw, const QString& name)
        -> std::pair<QWidget*, QVBoxLayout*>
    {
        auto* page = new QWidget;
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setStyleSheet("QScrollArea { background: #0f0f1a; border: none; }");
        page->setStyleSheet("QWidget { background: #0f0f1a; }");
        auto* vlay = new QVBoxLayout(page);
        vlay->setContentsMargins(8, 8, 8, 8);
        vlay->setSpacing(6);
        scroll->setWidget(page);
        tw->addTab(scroll, name);
        return {page, vlay};
    };

    // Helper: add a titled group box to a tab VBoxLayout.
    // Returns the group's inner QVBoxLayout.
    auto makeGroup = [](QVBoxLayout* parent, const QString& title) -> QVBoxLayout*
    {
        static const QString kGrpStyle =
            "QGroupBox { border: 1px solid #304050; border-radius: 4px; "
            "margin-top: 8px; padding-top: 12px; font-weight: bold; color: #8aa8c0; }"
            "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }";
        auto* grp = new QGroupBox(title);
        grp->setStyleSheet(kGrpStyle);
        auto* lay = new QVBoxLayout(grp);
        lay->setContentsMargins(8, 4, 8, 8);
        lay->setSpacing(4);
        parent->addWidget(grp);
        return lay;
    };

    // Shared label/control style constants (mirror SetupPage::makeLabeledRow).
    static const QString kLbl = "QLabel { color: #c8d8e8; font-size: 12px; }";
    static const QString kCombo =
        "QComboBox { background: #1a2a3a; border: 1px solid #304050; "
        "border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 2px 4px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background: #1a2a3a; color: #c8d8e8; "
        "selection-background-color: #00b4d8; }";
    static const QString kSlider =
        "QSlider::groove:horizontal { background: #1a2a3a; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #00b4d8; width: 12px; height: 12px; "
        "border-radius: 6px; margin: -4px 0; }";
    static const QString kInfoLbl =
        "QLabel { color: #5a8aaa; font-size: 12px; font-style: italic; }";

    // Label style used inside lambdas (non-static copy so it can be captured).
    const QString lblStyle = kLbl;

    // Helper: add a labeled row (150px label + stretch control) into a QVBoxLayout.
    auto addRow = [lblStyle](QVBoxLayout* vl, const QString& labelText, QWidget* ctrl)
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* lbl = new QLabel(labelText);
        lbl->setStyleSheet(lblStyle);
        lbl->setFixedWidth(150);
        row->addWidget(lbl);
        row->addWidget(ctrl, 1);
        vl->addLayout(row);
    };

    // Helper: add a pair of radio buttons as a Position row.
    // Returns {preRadio, postRadio}.
    auto addPositionRow = [lblStyle](QVBoxLayout* vl) -> std::pair<QRadioButton*, QRadioButton*>
    {
        auto* preRdo  = new QRadioButton("Pre-AGC");
        auto* postRdo = new QRadioButton("Post-AGC");
        preRdo->setStyleSheet(
            "QRadioButton { color: #c8d8e8; font-size: 12px; }"
            "QRadioButton::indicator { width: 14px; height: 14px; }"
            "QRadioButton::indicator:unchecked { border: 2px solid #304050; "
            "border-radius: 7px; background: #1a2a3a; }"
            "QRadioButton::indicator:checked { border: 2px solid #00b4d8; "
            "border-radius: 7px; background: #00b4d8; }");
        postRdo->setStyleSheet(preRdo->styleSheet());
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        auto* lbl = new QLabel("Position");
        lbl->setStyleSheet(lblStyle);
        lbl->setFixedWidth(150);
        row->addWidget(lbl);
        row->addWidget(preRdo);
        row->addWidget(postRdo);
        row->addStretch(1);
        vl->addLayout(row);
        return {preRdo, postRdo};
    };

    // ── NR1 tab ───────────────────────────────────────────────────────────────
    // From Thetis setup.designer.cs NR1 (ANR) group [v2.10.3.13].
    {
        auto [tabPage, tabLay] = makeTab(tabs, "NR1");
        Q_UNUSED(tabPage)
        QVBoxLayout* grpLay = makeGroup(tabLay, "NR1 (LMS)");

        // Taps — udDSPNRTaps: 16-1024, default 64
        auto* taps = new QSpinBox;
    
        taps->setRange(16, 1024);
        taps->setValue(slice ? slice->nr1Taps() : 64);
        // From Thetis setup.designer.cs — udDSPNRTaps.ToolTip [v2.10.3.13]
        taps->setToolTip(tr("LMS filter length (number of taps). Longer = more suppression, "
                            "more latency. Range 16-1024."));
        addRow(grpLay, "Taps", taps);

        // Delay — udDSPNRDelay: 1-256, default 16
        auto* delay = new QSpinBox;
    
        delay->setRange(1, 256);
        delay->setValue(slice ? slice->nr1Delay() : 16);
        delay->setToolTip(tr("LMS adaptive delay in samples. Separates desired signal "
                             "from correlated noise."));
        addRow(grpLay, "Delay", delay);

        // Gain — tbDSPNRGain: UI range 0-999 → WDSP domain = UI × 1e-6
        // From Thetis setup.cs NR1 gain rescaling [v2.10.3.13].
        auto* gain = new QSpinBox;
    
        gain->setRange(0, 999);
        // Reverse-scale from WDSP domain: WDSP_val = UI / 1e6 → UI = WDSP_val × 1e6
        gain->setValue(slice ? static_cast<int>(slice->nr1Gain() * 1e6) : 1600);
        gain->setToolTip(tr("LMS adaptation rate (gain). UI units × 1e-6 = WDSP domain value. "
                            "Default 1600 (= 16e-4 WDSP)."));
        addRow(grpLay, "Gain", gain);

        // Leakage — tbDSPNRLeak: UI range 0-999 → WDSP domain = UI × 1e-3
        // From Thetis setup.cs NR1 leakage rescaling [v2.10.3.13].
        auto* leak = new QSpinBox;
    
        leak->setRange(0, 999);
        // Reverse-scale: WDSP_val = UI / 1e3 → UI = WDSP_val × 1e3
        // Default slice value is 10e-7; scaled UI = 10e-7 × 1e3 ≈ 0
        leak->setValue(slice ? static_cast<int>(slice->nr1Leakage() * 1e3) : 0);
        leak->setToolTip(tr("LMS leakage factor. UI units × 1e-3 = WDSP domain value. "
                            "Default 0 (= 10e-7 WDSP)."));
        addRow(grpLay, "Leak", leak);

        // Position radio
        auto [preRdo, postRdo] = addPositionRow(grpLay);
        const bool isPost = !slice || (slice->nr1Position() == NrPosition::PostAgc);
        preRdo->setChecked(!isPost);
        postRdo->setChecked(isPost);

        tabLay->addStretch(1);

        // ── Wire NR1 controls → SliceModel ──────────────────────────────────
        if (slice) {
            connect(taps, QOverload<int>::of(&QSpinBox::valueChanged),
                    slice, &SliceModel::setNr1Taps);

            connect(delay, QOverload<int>::of(&QSpinBox::valueChanged),
                    slice, &SliceModel::setNr1Delay);

            connect(gain, QOverload<int>::of(&QSpinBox::valueChanged),
                    slice, [slice](int v) {
                // UI × 1e-6 → WDSP domain (matching VfoWidget Task 8 scaling).
                slice->setNr1Gain(static_cast<double>(v) * 1e-6);
            });

            connect(leak, QOverload<int>::of(&QSpinBox::valueChanged),
                    slice, [slice](int v) {
                // UI × 1e-3 → WDSP domain (matching VfoWidget Task 8 scaling).
                slice->setNr1Leakage(static_cast<double>(v) * 1e-3);
            });

            connect(preRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr1Position(NrPosition::PreAgc); }
            });
            connect(postRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr1Position(NrPosition::PostAgc); }
            });

            // ── Model → UI (bi-directional sync) ────────────────────────────
            connect(slice, &SliceModel::nr1TapsChanged, taps, [taps](int v) {
                QSignalBlocker b(taps); taps->setValue(v);
            });
            connect(slice, &SliceModel::nr1DelayChanged, delay, [delay](int v) {
                QSignalBlocker b(delay); delay->setValue(v);
            });
            connect(slice, &SliceModel::nr1GainChanged, gain, [gain](double v) {
                QSignalBlocker b(gain); gain->setValue(static_cast<int>(v * 1e6));
            });
            connect(slice, &SliceModel::nr1LeakageChanged, leak, [leak](double v) {
                QSignalBlocker b(leak); leak->setValue(static_cast<int>(v * 1e3));
            });
            connect(slice, &SliceModel::nr1PositionChanged, preRdo,
                    [preRdo, postRdo](NrPosition p) {
                QSignalBlocker b1(preRdo), b2(postRdo);
                preRdo->setChecked(p == NrPosition::PreAgc);
                postRdo->setChecked(p == NrPosition::PostAgc);
            });
        }
    }

    // ── NR2 tab ───────────────────────────────────────────────────────────────
    // From Thetis setup.designer.cs NR2 (EMNR) group [v2.10.3.13].
    {
        auto [tabPage, tabLay] = makeTab(tabs, "NR2");
        Q_UNUSED(tabPage)

        // ── Gain Method ─────────────────────────────────────────────────────
        QVBoxLayout* gmGrp = makeGroup(tabLay, "Gain Method");
        const QStringList gmLabels = {"Linear", "Log", "Gamma", "Trained"};
        QVector<QRadioButton*> gmRdos;
        {
            for (int i = 0; i < gmLabels.size(); ++i) {
                auto* rdo = new QRadioButton(gmLabels[i]);
                rdo->setStyleSheet(
                    "QRadioButton { color: #c8d8e8; font-size: 12px; }"
                    "QRadioButton::indicator { width: 14px; height: 14px; }"
                    "QRadioButton::indicator:unchecked { border: 2px solid #304050; "
                    "border-radius: 7px; background: #1a2a3a; }"
                    "QRadioButton::indicator:checked { border: 2px solid #00b4d8; "
                    "border-radius: 7px; background: #00b4d8; }");
                gmGrp->addWidget(rdo);
                gmRdos.append(rdo);
            }
            const int gmIdx = slice ? static_cast<int>(slice->nr2GainMethod()) : 2; // Gamma default
            if (gmIdx >= 0 && gmIdx < gmRdos.size()) { gmRdos[gmIdx]->setChecked(true); }
        }

        // ── NPE Method ──────────────────────────────────────────────────────
        QVBoxLayout* npeGrp = makeGroup(tabLay, "NPE Method");
        const QStringList npeLabels = {"OSMS", "MMSE", "NSTAT"};
        QVector<QRadioButton*> npeRdos;
        {
            for (const QString& lbl : npeLabels) {
                auto* rdo = new QRadioButton(lbl);
                rdo->setStyleSheet(gmRdos[0]->styleSheet());
                npeGrp->addWidget(rdo);
                npeRdos.append(rdo);
            }
            const int npeIdx = slice ? static_cast<int>(slice->nr2NpeMethod()) : 0; // OSMS default
            if (npeIdx >= 0 && npeIdx < npeRdos.size()) { npeRdos[npeIdx]->setChecked(true); }
        }

        // ── Training / Filter ────────────────────────────────────────────────
        QVBoxLayout* tfGrp = makeGroup(tabLay, "Training / Filter");

        // T1 — udDSPEMNRTrainT1: −5.0 .. 5.0 step 0.1, default −0.5
        auto* t1 = new QDoubleSpinBox;
        t1->setRange(-5.0, 5.0);
        t1->setSingleStep(0.1);
        t1->setDecimals(1);
        t1->setValue(slice ? slice->nr2TrainT1() : -0.5);
        t1->setToolTip(tr("EMNR Zeta threshold (T1). Controls the asymmetry of the "
                          "noise estimator. Range -5.0 to +5.0."));
        addRow(tfGrp, "T1", t1);

        // T2 — udDSPEMNRTrainT2: 0.0 .. 2.0 step 0.05, default 0.20
        auto* t2 = new QDoubleSpinBox;
        t2->setRange(0.0, 2.0);
        t2->setSingleStep(0.05);
        t2->setDecimals(2);
        t2->setValue(slice ? slice->nr2TrainT2() : 0.20);
        t2->setToolTip(tr("EMNR T2 training parameter. Range 0.0 to 2.0."));
        addRow(tfGrp, "T2", t2);

        // AE Filter checkbox
        auto* aeChk = new QCheckBox("AE Filter");
        aeChk->setChecked(slice ? slice->nr2AeFilter() : true);
        aeChk->setToolTip(tr("Enable EMNR Acoustic Echo (AE) filter stage."));
        tfGrp->addWidget(aeChk);

        // Position radio
        auto [preRdo, postRdo] = addPositionRow(tfGrp);
        {
            const bool isPost = !slice || (slice->nr2Position() == NrPosition::PostAgc);
            preRdo->setChecked(!isPost);
            postRdo->setChecked(isPost);
        }

        // ── Noise Post-Proc ─────────────────────────────────────────────────
        QVBoxLayout* ppGrp = makeGroup(tabLay, "Noise Post-Proc");

        auto* ppRun = new QCheckBox("Enable");
        ppRun->setChecked(slice ? slice->nr2Post2Run() : false);
        ppRun->setToolTip(tr("Enable EMNR Noise Post-Processing cascade."));
        ppGrp->addWidget(ppRun);

        // Level — udDSPEMNRPost2Level
        auto* ppLevel = new QDoubleSpinBox;
    
        ppLevel->setRange(0.0, 100.0);
        ppLevel->setSingleStep(0.1);
        ppLevel->setDecimals(1);
        ppLevel->setValue(slice ? slice->nr2Post2Level() : 15.0);
        addRow(ppGrp, "Level", ppLevel);

        // Factor — udDSPEMNRPost2Factor
        auto* ppFactor = new QDoubleSpinBox;
    
        ppFactor->setRange(0.0, 100.0);
        ppFactor->setSingleStep(0.1);
        ppFactor->setDecimals(1);
        ppFactor->setValue(slice ? slice->nr2Post2Factor() : 15.0);
        addRow(ppGrp, "Factor", ppFactor);

        // Rate — udDSPEMNRPost2Rate
        auto* ppRate = new QDoubleSpinBox;
    
        ppRate->setRange(0.0, 100.0);
        ppRate->setSingleStep(0.1);
        ppRate->setDecimals(1);
        ppRate->setValue(slice ? slice->nr2Post2Rate() : 5.0);
        addRow(ppGrp, "Rate", ppRate);

        // Taper — udDSPEMNRPost2Taper (integer)
        auto* ppTaper = new QSpinBox;
    
        ppTaper->setRange(0, 100);
        ppTaper->setValue(slice ? slice->nr2Post2Taper() : 12);
        addRow(ppGrp, "Taper", ppTaper);

        tabLay->addStretch(1);

        // ── Wire NR2 controls → SliceModel ──────────────────────────────────
        if (slice) {
            // Gain Method radios → slice
            for (int i = 0; i < gmRdos.size(); ++i) {
                connect(gmRdos[i], &QRadioButton::toggled, slice,
                        [slice, i](bool checked) {
                    if (checked) {
                        slice->setNr2GainMethod(static_cast<EmnrGainMethod>(i));
                    }
                });
            }
            // NPE Method radios → slice
            for (int i = 0; i < npeRdos.size(); ++i) {
                connect(npeRdos[i], &QRadioButton::toggled, slice,
                        [slice, i](bool checked) {
                    if (checked) {
                        slice->setNr2NpeMethod(static_cast<EmnrNpeMethod>(i));
                    }
                });
            }

            connect(t1, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr2TrainT1);
            connect(t2, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr2TrainT2);
            connect(aeChk, &QCheckBox::toggled, slice, &SliceModel::setNr2AeFilter);

            connect(preRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr2Position(NrPosition::PreAgc); }
            });
            connect(postRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr2Position(NrPosition::PostAgc); }
            });

            connect(ppRun, &QCheckBox::toggled, slice, &SliceModel::setNr2Post2Run);
            connect(ppLevel, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr2Post2Level);
            connect(ppFactor, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr2Post2Factor);
            connect(ppRate, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr2Post2Rate);
            connect(ppTaper, QOverload<int>::of(&QSpinBox::valueChanged),
                    slice, &SliceModel::setNr2Post2Taper);

            // ── Model → UI (bi-directional sync) ────────────────────────────
            connect(slice, &SliceModel::nr2GainMethodChanged,
                    [gmRdos](EmnrGainMethod v) {
                const int idx = static_cast<int>(v);
                if (idx >= 0 && idx < gmRdos.size()) {
                    QSignalBlocker b(gmRdos[idx]);
                    gmRdos[idx]->setChecked(true);
                }
            });
            connect(slice, &SliceModel::nr2NpeMethodChanged,
                    [npeRdos](EmnrNpeMethod v) {
                const int idx = static_cast<int>(v);
                if (idx >= 0 && idx < npeRdos.size()) {
                    QSignalBlocker b(npeRdos[idx]);
                    npeRdos[idx]->setChecked(true);
                }
            });
            connect(slice, &SliceModel::nr2TrainT1Changed, t1, [t1](double v) {
                QSignalBlocker b(t1); t1->setValue(v);
            });
            connect(slice, &SliceModel::nr2TrainT2Changed, t2, [t2](double v) {
                QSignalBlocker b(t2); t2->setValue(v);
            });
            connect(slice, &SliceModel::nr2AeFilterChanged, aeChk, [aeChk](bool v) {
                QSignalBlocker b(aeChk); aeChk->setChecked(v);
            });
            connect(slice, &SliceModel::nr2PositionChanged, preRdo,
                    [preRdo, postRdo](NrPosition p) {
                QSignalBlocker b1(preRdo), b2(postRdo);
                preRdo->setChecked(p == NrPosition::PreAgc);
                postRdo->setChecked(p == NrPosition::PostAgc);
            });
            connect(slice, &SliceModel::nr2Post2RunChanged, ppRun, [ppRun](bool v) {
                QSignalBlocker b(ppRun); ppRun->setChecked(v);
            });
            connect(slice, &SliceModel::nr2Post2LevelChanged, ppLevel, [ppLevel](double v) {
                QSignalBlocker b(ppLevel); ppLevel->setValue(v);
            });
            connect(slice, &SliceModel::nr2Post2FactorChanged, ppFactor, [ppFactor](double v) {
                QSignalBlocker b(ppFactor); ppFactor->setValue(v);
            });
            connect(slice, &SliceModel::nr2Post2RateChanged, ppRate, [ppRate](double v) {
                QSignalBlocker b(ppRate); ppRate->setValue(v);
            });
            connect(slice, &SliceModel::nr2Post2TaperChanged, ppTaper, [ppTaper](int v) {
                QSignalBlocker b(ppTaper); ppTaper->setValue(v);
            });
        }
    }

    // ── NR3 tab ───────────────────────────────────────────────────────────────
    // From Thetis setup.cs NR3 (RNNR) controls [v2.10.3.13].
    {
        auto [tabPage, tabLay] = makeTab(tabs, "NR3");
        Q_UNUSED(tabPage)

        QVBoxLayout* grpLay = makeGroup(tabLay, "NR3 (RNNoise)");

        // Position radio
        auto [preRdo, postRdo] = addPositionRow(grpLay);
        {
            const bool isPost = !slice || (slice->nr3Position() == NrPosition::PostAgc);
            preRdo->setChecked(!isPost);
            postRdo->setChecked(isPost);
        }

        // "Use fixed gain for input samples" — chkRXANR3FixedGain [v2.10.3.13]
        auto* fixedGainChk = new QCheckBox("Use fixed gain for input samples");
        fixedGainChk->setChecked(slice ? slice->nr3UseDefaultGain() : true);
        // Tooltip source: Thetis setup.cs:35460 chkRXANR3FixedGain [v2.10.3.13]
        fixedGainChk->setToolTip(tr("Use a fixed (rather than adaptive) input sample gain."));
        grpLay->addWidget(fixedGainChk);

        // Model selector group — GLOBAL (not per-slice), stored in AppSettings
        QVBoxLayout* mdlGrp = makeGroup(tabLay, "RNNoise Model (Global)");

        auto* modelLabel = new QLabel;
        modelLabel->setStyleSheet("QLabel { color: #00c8ff; font-size: 12px; }");
        {
            const QString saved = AppSettings::instance().value("Nr3ModelPath", "").toString();
            modelLabel->setText(saved.isEmpty() ? "Default (large)" : QFileInfo(saved).baseName());
        }
        mdlGrp->addWidget(modelLabel);

        auto* btnRow = new QHBoxLayout;
        auto* useModelBtn = new QPushButton("Use Model...");
        useModelBtn->setStyleSheet(
            "QPushButton { background: #1a2a3a; border: 1px solid #304050; "
            "border-radius: 3px; color: #c8d8e8; font-size: 12px; padding: 3px 10px; }"
            "QPushButton:hover { background: #203040; }"
            "QPushButton:pressed { background: #00b4d8; color: #0f0f1a; }");
        auto* defBtn = new QPushButton("Default");
        defBtn->setStyleSheet(useModelBtn->styleSheet());
        btnRow->addWidget(useModelBtn);
        btnRow->addWidget(defBtn);
        btnRow->addStretch(1);
        mdlGrp->addLayout(btnRow);

        tabLay->addStretch(1);

        // ── Wire NR3 controls → SliceModel / global ──────────────────────────
        connect(useModelBtn, &QPushButton::clicked, this, [this, modelLabel]() {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Choose RNNoise Model"), QString(), tr("Model Files (*.bin *.rnnn);;All files (*)"));
            if (path.isEmpty()) { return; }
            AppSettings::instance().setValue("Nr3ModelPath", path);
            RNNRloadModel(path.toStdString().c_str());
            modelLabel->setText(QFileInfo(path).baseName());
        });

        connect(defBtn, &QPushButton::clicked, this, [modelLabel]() {
            AppSettings::instance().setValue("Nr3ModelPath", "");
            RNNRloadModel("");
            modelLabel->setText("Default (large)");
        });

        if (slice) {
            connect(preRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr3Position(NrPosition::PreAgc); }
            });
            connect(postRdo, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr3Position(NrPosition::PostAgc); }
            });
            connect(fixedGainChk, &QCheckBox::toggled, slice, &SliceModel::setNr3UseDefaultGain);

            // ── Model → UI ────────────────────────────────────────────────
            connect(slice, &SliceModel::nr3PositionChanged, preRdo,
                    [preRdo, postRdo](NrPosition p) {
                QSignalBlocker b1(preRdo), b2(postRdo);
                preRdo->setChecked(p == NrPosition::PreAgc);
                postRdo->setChecked(p == NrPosition::PostAgc);
            });
            connect(slice, &SliceModel::nr3UseDefaultGainChanged, fixedGainChk,
                    [fixedGainChk](bool v) {
                QSignalBlocker b(fixedGainChk); fixedGainChk->setChecked(v);
            });
        }
    }

    // ── NR4 tab ───────────────────────────────────────────────────────────────
    // From Thetis setup.cs NR4 (SBNR / SpecBleach) controls [v2.10.3.13].
    {
        auto [tabPage, tabLay] = makeTab(tabs, "NR4");
        Q_UNUSED(tabPage)

        QVBoxLayout* grpLay = makeGroup(tabLay, "NR4 (SpecBleach)");

        // Reduction — udDSPSBNRreduction: 0-20 step 0.1, default 10.0
        auto* reduction = new QDoubleSpinBox;
    
        reduction->setRange(0.0, 20.0);
        reduction->setSingleStep(0.1);
        reduction->setDecimals(1);
        reduction->setValue(slice ? slice->nr4Reduction() : 10.0);
        // Tooltip source: Thetis setup.cs udDSPSBNRreduction [v2.10.3.13]
        reduction->setToolTip(tr("Spectral reduction amount in dB. Range 0-20, step 0.1."));
        addRow(grpLay, "Reduction", reduction);

        // Smoothing — udDSPSBNRsmooth: 0-100 step 1, default 65.0
        auto* smoothing = new QDoubleSpinBox;
    
        smoothing->setRange(0.0, 100.0);
        smoothing->setSingleStep(1.0);
        smoothing->setDecimals(1);
        smoothing->setValue(slice ? slice->nr4Smoothing() : 65.0);
        // Tooltip source: Thetis setup.cs udDSPSBNRsmooth [v2.10.3.13]
        smoothing->setToolTip(tr("Spectral smoothing factor. Range 0-100."));
        addRow(grpLay, "Smoothing", smoothing);

        // Whitening — udDSPSBNRwhiten: 0-100 step 1, default 2.0
        auto* whitening = new QDoubleSpinBox;
    
        whitening->setRange(0.0, 100.0);
        whitening->setSingleStep(1.0);
        whitening->setDecimals(1);
        whitening->setValue(slice ? slice->nr4Whitening() : 2.0);
        // Tooltip source: Thetis setup.cs udDSPSBNRwhiten [v2.10.3.13]
        whitening->setToolTip(tr("Spectral whitening factor. Range 0-100."));
        addRow(grpLay, "Whitening", whitening);

        // Rescale — udDSPSBNRrescale: 0-12 step 0.1, default 2.0
        auto* rescale = new QDoubleSpinBox;
    
        rescale->setRange(0.0, 12.0);
        rescale->setSingleStep(0.1);
        rescale->setDecimals(1);
        rescale->setValue(slice ? slice->nr4Rescale() : 2.0);
        // Tooltip source: Thetis setup.cs udDSPSBNRrescale [v2.10.3.13]
        rescale->setToolTip(tr("Output rescale factor. Range 0-12, step 0.1."));
        addRow(grpLay, "Rescale", rescale);

        // SNR Threshold — udDSPSBNRsnrthresh: -10..10 step 0.5, default -10.0
        auto* snrThresh = new QDoubleSpinBox;
    
        snrThresh->setRange(-10.0, 10.0);
        snrThresh->setSingleStep(0.5);
        snrThresh->setDecimals(1);
        snrThresh->setValue(slice ? slice->nr4PostThresh() : -10.0);
        // Tooltip source: Thetis setup.cs udDSPSBNRsnrthresh [v2.10.3.13]
        snrThresh->setToolTip(tr("Post-processing SNR threshold. Range -10 to +10 dB, step 0.5."));
        addRow(grpLay, "SNR Thresh", snrThresh);

        // Algorithm radio — rdoSBNR1/2/3 [v2.10.3.13]
        const QString rdoStyle =
            "QRadioButton { color: #c8d8e8; font-size: 12px; }"
            "QRadioButton::indicator { width: 14px; height: 14px; }"
            "QRadioButton::indicator:unchecked { border: 2px solid #304050; "
            "border-radius: 7px; background: #1a2a3a; }"
            "QRadioButton::indicator:checked { border: 2px solid #00b4d8; "
            "border-radius: 7px; background: #00b4d8; }";

        auto* algo1 = new QRadioButton("Algo 1");
        auto* algo2 = new QRadioButton("Algo 2");
        auto* algo3 = new QRadioButton("Algo 3");
        algo1->setStyleSheet(rdoStyle);
        algo2->setStyleSheet(rdoStyle);
        algo3->setStyleSheet(rdoStyle);

        {
            const int algoIdx = slice ? static_cast<int>(slice->nr4Algo()) : 1; // Algo2 default
            if (algoIdx == 0) { algo1->setChecked(true); }
            else if (algoIdx == 2) { algo3->setChecked(true); }
            else { algo2->setChecked(true); }
        }

        auto* algoRow = new QHBoxLayout;
        auto* algoLbl = new QLabel("Algorithm");
        algoLbl->setStyleSheet(kLbl);
        algoLbl->setFixedWidth(150);
        algoRow->addWidget(algoLbl);
        algoRow->addWidget(algo1);
        algoRow->addWidget(algo2);
        algoRow->addWidget(algo3);
        algoRow->addStretch(1);
        grpLay->addLayout(algoRow);

        tabLay->addStretch(1);

        // ── Wire NR4 controls → SliceModel ──────────────────────────────────
        if (slice) {
            connect(reduction, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr4Reduction);
            connect(smoothing, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr4Smoothing);
            connect(whitening, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr4Whitening);
            connect(rescale, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr4Rescale);
            connect(snrThresh, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                    slice, &SliceModel::setNr4PostThresh);

            connect(algo1, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr4Algo(SbnrAlgo::Algo1); }
            });
            connect(algo2, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr4Algo(SbnrAlgo::Algo2); }
            });
            connect(algo3, &QRadioButton::toggled, slice, [slice](bool checked) {
                if (checked) { slice->setNr4Algo(SbnrAlgo::Algo3); }
            });

            // ── Model → UI ────────────────────────────────────────────────
            connect(slice, &SliceModel::nr4ReductionChanged, reduction, [reduction](double v) {
                QSignalBlocker b(reduction); reduction->setValue(v);
            });
            connect(slice, &SliceModel::nr4SmoothingChanged, smoothing, [smoothing](double v) {
                QSignalBlocker b(smoothing); smoothing->setValue(v);
            });
            connect(slice, &SliceModel::nr4WhiteningChanged, whitening, [whitening](double v) {
                QSignalBlocker b(whitening); whitening->setValue(v);
            });
            connect(slice, &SliceModel::nr4RescaleChanged, rescale, [rescale](double v) {
                QSignalBlocker b(rescale); rescale->setValue(v);
            });
            connect(slice, &SliceModel::nr4PostThreshChanged, snrThresh, [snrThresh](double v) {
                QSignalBlocker b(snrThresh); snrThresh->setValue(v);
            });
            connect(slice, &SliceModel::nr4AlgoChanged, algo1,
                    [algo1, algo2, algo3](SbnrAlgo v) {
                QSignalBlocker b1(algo1), b2(algo2), b3(algo3);
                algo1->setChecked(v == SbnrAlgo::Algo1);
                algo2->setChecked(v == SbnrAlgo::Algo2);
                algo3->setChecked(v == SbnrAlgo::Algo3);
            });
        }
    }

    // ── DFNR tab ──────────────────────────────────────────────────────────────
    // AetherSDR-native DeepFilter NR (not in Thetis). HAVE_DFNR guards WDSP
    // integration; UI is always shown so users can understand the feature gate.
    {
        auto [tabPage, tabLay] = makeTab(tabs, "DFNR");
        Q_UNUSED(tabPage)

        QVBoxLayout* grpLay = makeGroup(tabLay, "DeepFilter NR");

#ifndef HAVE_DFNR
        // Feature not compiled in — show a helpful note and gray out the group.
        auto* note = new QLabel(
            "DFNR is not enabled in this build.\n"
            "Run ./setup-deepfilter.sh and rebuild to enable DeepFilter NR.");
        note->setStyleSheet(kInfoLbl);
        note->setWordWrap(true);
        grpLay->addWidget(note);
        grpLay->parentWidget()->setEnabled(false);
#endif

        // Attenuation Limit slider (0-100, default 100)
        auto* attenRow = new QHBoxLayout;
        auto* attenLbl = new QLabel("Attenuation Limit");
        attenLbl->setStyleSheet(kLbl);
        attenLbl->setFixedWidth(150);
        attenRow->addWidget(attenLbl);
        auto* attenSl = new QSlider(Qt::Horizontal);
        attenSl->setStyleSheet(kSlider);
        attenSl->setRange(0, 100);
        attenSl->setValue(slice ? static_cast<int>(slice->dfnrAttenLimit()) : 100);
        attenSl->setToolTip(tr("Maximum noise attenuation in dB (0 = bypass, 100 = maximum). "
                               "Default 100. Higher values suppress more noise but may clip speech peaks."));
        attenRow->addWidget(attenSl, 1);
        auto* attenVal = new QLabel(QString::number(attenSl->value()));
        attenVal->setStyleSheet("QLabel { color: #00c8ff; font-size: 12px; min-width: 28px; }");
        attenRow->addWidget(attenVal);
        grpLay->addLayout(attenRow);
        connect(attenSl, &QSlider::valueChanged, attenVal, [attenVal](int v) {
            attenVal->setText(QString::number(v));
        });

        // Post-Filter Beta slider (0-30 displayed as /100 → 0.00–0.30)
        auto* betaRow = new QHBoxLayout;
        auto* betaLbl = new QLabel("Post-Filter Beta");
        betaLbl->setStyleSheet(kLbl);
        betaLbl->setFixedWidth(150);
        betaRow->addWidget(betaLbl);
        auto* betaSl = new QSlider(Qt::Horizontal);
        betaSl->setStyleSheet(kSlider);
        betaSl->setRange(0, 30);
        betaSl->setValue(slice ? static_cast<int>(slice->dfnrPostFilterBeta() * 100.0) : 0);
        betaSl->setToolTip(tr("Post-filter aggressiveness (0 = disabled, 0.30 = maximum). Default 0. "
                              "Higher values reduce residual musical-noise artifacts but may "
                              "over-attenuate consonants."));
        betaRow->addWidget(betaSl, 1);
        auto* betaVal = new QLabel(QStringLiteral("0.%1").arg(betaSl->value(), 2, 10, QChar('0')));
        betaVal->setStyleSheet("QLabel { color: #00c8ff; font-size: 12px; min-width: 40px; }");
        betaRow->addWidget(betaVal);
        grpLay->addLayout(betaRow);
        connect(betaSl, &QSlider::valueChanged, betaVal, [betaVal](int v) {
            betaVal->setText(QStringLiteral("%1").arg(v / 100.0, 0, 'f', 2));
        });

        tabLay->addStretch(1);

        // ── Wire DFNR controls → SliceModel ─────────────────────────────────
        if (slice) {
            connect(attenSl, &QSlider::valueChanged, slice, [slice](int v) {
                slice->setDfnrAttenLimit(static_cast<double>(v));
            });
            connect(betaSl, &QSlider::valueChanged, slice, [slice](int v) {
                slice->setDfnrPostFilterBeta(static_cast<double>(v) / 100.0);
            });

            // ── Model → UI ────────────────────────────────────────────────
            connect(slice, &SliceModel::dfnrAttenLimitChanged, attenSl, [attenSl](double v) {
                QSignalBlocker b(attenSl); attenSl->setValue(static_cast<int>(v));
            });
            connect(slice, &SliceModel::dfnrPostFilterBetaChanged, betaSl, [betaSl](double v) {
                QSignalBlocker b(betaSl); betaSl->setValue(static_cast<int>(v * 100.0));
            });
        }
    }

    // ── MNR tab ───────────────────────────────────────────────────────────────
    // AetherSDR-native macOS noise reduction (not in Thetis). Always shown
    // so Windows/Linux users see the feature exists; controls grayed out on
    // non-Apple builds.
    {
        auto [tabPage, tabLay] = makeTab(tabs, "MNR");
        Q_UNUSED(tabPage)

        QVBoxLayout* grpLay = makeGroup(tabLay, "macOS NR (MNR)");

#ifndef Q_OS_APPLE
        auto* note = new QLabel(
            "MNR uses Apple Accelerate and is only available on macOS.\n"
            "On this platform the MNR controls are inactive.");
        note->setStyleSheet(kInfoLbl);
        note->setWordWrap(true);
        grpLay->addWidget(note);
#endif

        // Strength slider (0-100, stored as 0.0-1.0 in SliceModel)
        auto* strRow = new QHBoxLayout;
        auto* strLbl = new QLabel("Strength");
        strLbl->setStyleSheet(kLbl);
        strLbl->setFixedWidth(150);
        strRow->addWidget(strLbl);
        auto* strSl = new QSlider(Qt::Horizontal);
        strSl->setStyleSheet(kSlider);
        strSl->setRange(0, 100);
        // SliceModel stores 0.0-1.0; scale to 0-100 for the slider
        strSl->setValue(slice ? static_cast<int>(slice->mnrStrength() * 100.0) : 100);
        strSl->setToolTip(tr("Noise reduction strength 0-100. Default 100 = full NR; lower values "
                             "blend the processed and clean signal. "
                             "MMSE-Wiener spectral filter using Apple Accelerate vDSP."));
        strRow->addWidget(strSl, 1);
        auto* strVal = new QLabel(QString::number(strSl->value()));
        strVal->setStyleSheet("QLabel { color: #00c8ff; font-size: 12px; min-width: 28px; }");
        strRow->addWidget(strVal);
        grpLay->addLayout(strRow);
        connect(strSl, &QSlider::valueChanged, strVal, [strVal](int v) {
            strVal->setText(QString::number(v));
        });

#ifndef Q_OS_APPLE
        strSl->setEnabled(false);
#endif

        tabLay->addStretch(1);

        // ── Wire MNR controls → SliceModel ──────────────────────────────────
#ifdef Q_OS_APPLE
        if (slice) {
            connect(strSl, &QSlider::valueChanged, slice, [slice](int v) {
                slice->setMnrStrength(static_cast<double>(v) / 100.0);
            });
            connect(slice, &SliceModel::mnrStrengthChanged, strSl, [strSl](double v) {
                QSignalBlocker b(strSl); strSl->setValue(static_cast<int>(v * 100.0));
            });
        }
#endif
    }

    // ── ANF tab ───────────────────────────────────────────────────────────────
    // From Thetis setup.designer.cs — chkDSPANFEnable [v2.10.3.13].
    // Advanced ANF tuning (Taps/Delay/Gain/Leakage) is not yet in SliceModel;
    // deferred to a future phase. This tab wires the Enable toggle only.
    {
        auto [tabPage, tabLay] = makeTab(tabs, "ANF");
        Q_UNUSED(tabPage)

        QVBoxLayout* grpLay = makeGroup(tabLay, "Adaptive Noise Filter");

        // Note: NrSlot::Off means ANF is included as part of the NR selector
        // but ANF itself doesn't have a dedicated NrSlot — in Thetis ANF is a
        // parallel stage enabled independently of the NR slot selection.
        // For now expose an informational note and a placeholder Enable toggle
        // that will be wired once SliceModel gains anfEnabled.
        auto* note = new QLabel(
            "ANF is available via the VFO popup ANF button.\n"
            "Advanced tuning (Taps, Delay, Gain, Leakage) will be added in a future phase.\n"
            "Enable toggle below mirrors the VFO ANF button state (not yet wired).");
        note->setStyleSheet(kInfoLbl);
        note->setWordWrap(true);
        grpLay->addWidget(note);

        // Placeholder Enable toggle — from Thetis chkDSPANFEnable [v2.10.3.13].
        // NYI: SliceModel does not yet expose anfEnabled / setAnfEnabled.
        // Deferred to the same phase that adds Taps/Delay/Gain/Leakage to SliceModel.
        auto* anfEnableChk = new QCheckBox("Enable ANF");
        anfEnableChk->setChecked(false);
        // Tooltip source: Thetis setup.designer.cs chkDSPANFEnable [v2.10.3.13]
        anfEnableChk->setToolTip(tr("Enable Adaptive Notch Filter. Full ANF tuning (Taps/Delay/"
                                    "Gain/Leakage) coming in a future phase."));
        anfEnableChk->setEnabled(false);  // NYI until SliceModel has anfEnabled
        grpLay->addWidget(anfEnableChk);

        tabLay->addStretch(1);
        // No wiring: SliceModel::anfEnabled does not yet exist (Task 17 scope).
    }
}

// ══════════════════════════════════════════════════════════════════════════════
// NbSnbSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageNoiseBlanker controls:
//   tbNB1Threshold, comboNB1Mode, tbNB2Threshold,
//   tbSNBK1, tbSNBK2, udSNBOutputBW
//
NbSnbSetupPage::NbSnbSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("NB/SNB", model, parent)
{
    // Sliders with inline live-value labels. Tooltips use Thetis's own user-
    // facing text (from setup.designer.cs ToolTip attributes [v2.10.3.13]).
    // Ranges / defaults mirror Thetis NumericUpDown widgets byte-for-byte.
    auto& s = AppSettings::instance();

    // Gate: the WDSP SetEXT* / SetRXASNBA* setters dereference
    // panb[0]/pnob[0]/channels[0] — if no RX channel has been created
    // (user opened Setup dialog before connecting to a radio), calling
    // them will null-deref inside WDSP. The slider value is still
    // persisted via AppSettings and seeded into NbFamily at channel
    // create time, so "Setup → change before connect" still takes effect
    // on next connect. This just stops the crash.
    // (Codex review #120, P1 — 2026-04-23.)
    auto channelReady = [this]() -> bool {
        auto* rm = this->model();
        if (!rm || !rm->wdspEngine()) { return false; }
        return rm->wdspEngine()->rxChannel(0) != nullptr;
    };

    // Helper: integer slider, live value label showing "value / max" with unit.
    // Returns the slider so caller can wire valueChanged.
    auto addIntSlider = [this](QVBoxLayout* parent, const QString& label,
                               int minVal, int maxVal, int value,
                               const QString& unitSuffix,
                               const QString& tooltip) -> QSlider*
    {
        auto* sl = new QSlider(Qt::Horizontal);
        sl->setRange(minVal, maxVal);
        sl->setValue(value);
        sl->setToolTip(tooltip);
        auto* valLbl = new QLabel;
        valLbl->setMinimumWidth(80);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        auto renderInt = [valLbl, maxVal, unitSuffix](int v) {
            valLbl->setText(QStringLiteral("%1 / %2%3")
                .arg(v).arg(maxVal).arg(unitSuffix));
        };
        renderInt(value);
        QObject::connect(sl, &QSlider::valueChanged, valLbl, renderInt);
        this->addLabeledSlider(parent, label, sl, valLbl);
        return sl;
    };

    // Helper: "decimal" slider. Slider uses integer internally at the chosen
    // scale (e.g. ×100 for 0.01 step); value label renders at the real scale
    // with the given unit. Returns {slider, scaleDivisor}.
    auto addScaledSlider = [this](QVBoxLayout* parent, const QString& label,
                                  int sliderMin, int sliderMax, int sliderValue,
                                  double divisor, int decimals,
                                  const QString& unitSuffix,
                                  const QString& tooltip) -> QSlider*
    {
        auto* sl = new QSlider(Qt::Horizontal);
        sl->setRange(sliderMin, sliderMax);
        sl->setValue(sliderValue);
        sl->setToolTip(tooltip);
        auto* valLbl = new QLabel;
        valLbl->setMinimumWidth(110);
        valLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        const double maxReal = sliderMax / divisor;
        auto render = [valLbl, decimals, divisor, maxReal, unitSuffix](int v) {
            valLbl->setText(QStringLiteral("%1 / %2%3")
                .arg(v / divisor, 0, 'f', decimals)
                .arg(maxReal, 0, 'f', decimals)
                .arg(unitSuffix));
        };
        render(sliderValue);
        QObject::connect(sl, &QSlider::valueChanged, valLbl, render);
        this->addLabeledSlider(parent, label, sl, valLbl);
        return sl;
    };

    // ── NB1 ───────────────────────────────────────────────────────────────────
    // Thetis grpDSPNB (setup.designer.cs:44399-44604 [v2.10.3.13]).
    QGroupBox* nb1Grp = addSection("NB1");
    QVBoxLayout* nb1Lay = qobject_cast<QVBoxLayout*>(nb1Grp->layout());

    // Threshold — udDSPNB: 1-1000, default 30. WDSP threshold = 0.165 × value.
    QSlider* nb1Thresh = addIntSlider(nb1Lay, tr("Threshold"),
        1, 1000,
        s.value(QStringLiteral("NbDefaultThresholdSlider"), 30).toInt(),
        QString{},
        tr("Controls the detection threshold for impulse noise.\n"
           "Lower = more aggressive (blanks weaker impulses too).\n"
           "Higher = more conservative (only strong clicks get blanked)."));
    connect(nb1Thresh, &QSlider::valueChanged, [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("NbDefaultThresholdSlider"), v);
        if (channelReady()) { SetEXTANBThreshold(0, 0.165 * static_cast<double>(v)); }
    });

    // Transition — udDSPNBTransition: 0.01-2.00 ms, step 0.01, default 0.01.
    // Slider internal: 1-200 (×100 scale). Label shows "X.XX / 2.00 ms".
    QSlider* nb1Trans = addScaledSlider(nb1Lay, tr("Transition"),
        1, 200,
        s.value(QStringLiteral("NbDefaultTransition"), 1).toInt(),
        100.0, 2, tr(" ms"),
        tr("Time to decrease/increase to/from zero amplitude around an\n"
           "impulse. Controls how gradually the blanker fades in and out\n"
           "— very short = crisp click; longer = gentler but audible."));
    connect(nb1Trans, &QSlider::valueChanged, [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("NbDefaultTransition"), v);
        // v/100 ms → × 0.001 s = v * 1e-5 s.
        if (channelReady()) { SetEXTANBTau(0, static_cast<double>(v) * 1e-5); }
    });

    // Lead — udDSPNBLead: 0.01-2.00 ms, default 0.01.
    QSlider* nb1Lead = addScaledSlider(nb1Lay, tr("Lead"),
        1, 200,
        s.value(QStringLiteral("NbDefaultLead"), 1).toInt(),
        100.0, 2, tr(" ms"),
        tr("Time at zero amplitude BEFORE the detected impulse. Blanks\n"
           "the leading edge of the click that the detector would\n"
           "otherwise miss. Raise slightly if clicks still get through."));
    connect(nb1Lead, &QSlider::valueChanged, [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("NbDefaultLead"), v);
        if (channelReady()) { SetEXTANBAdvtime(0, static_cast<double>(v) * 1e-5); }
    });

    // Lag — udDSPNBLag: 0.01-2.00 ms, default 0.01.
    QSlider* nb1Lag = addScaledSlider(nb1Lay, tr("Lag"),
        1, 200,
        s.value(QStringLiteral("NbDefaultLag"), 1).toInt(),
        100.0, 2, tr(" ms"),
        tr("Time to remain at zero amplitude AFTER the impulse. Blanks\n"
           "the decay tail of the click. Raise this if pops still have\n"
           "an audible ringing after the initial transient."));
    connect(nb1Lag, &QSlider::valueChanged, [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("NbDefaultLag"), v);
        if (channelReady()) { SetEXTANBHangtime(0, static_cast<double>(v) * 1e-5); }
    });

    // NB2 Mode — Thetis comboDSPNOBmode.
    auto* nb1Mode = new QComboBox;
    // Item text matches Thetis comboDSPNOBmode verbatim (setup.designer.cs:44434 [v2.10.3.13]).
    nb1Mode->addItems({tr("Zero"), tr("Sample && Hold"), tr("Mean-Hold"),
                       tr("Hold && Sample"), tr("Linear Interpolate")});
    nb1Mode->setCurrentIndex(s.value(QStringLiteral("Nb2DefaultMode"), 0).toInt());
    nb1Mode->setToolTip(tr(
        "Method used to fill in the blanked samples when NB2 triggers.\n"
        "Zero silences the impulse entirely; the other modes synthesise\n"
        "a replacement waveform from surrounding samples to reduce\n"
        "audible artifacts on voice peaks."));
    addLabeledCombo(nb1Lay, "NB2 Mode", nb1Mode);
    connect(nb1Mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("Nb2DefaultMode"), v);
        if (channelReady()) { SetEXTNOBMode(0, v); }
    });

    // ── NB2 Threshold — intentionally absent (Thetis parity) ─────────────────
    // Thetis has no NB2 threshold UI. NB2 runs at cmaster.c:68 [v2.10.3.13]
    // hardcoded default of 30.0.

    // ── SNB ───────────────────────────────────────────────────────────────────
    // Thetis grpDSPSNB (setup.designer.cs:44280-44398 [v2.10.3.13]).
    QGroupBox* snbGrp = addSection("SNB");
    QVBoxLayout* snbLay = qobject_cast<QVBoxLayout*>(snbGrp->layout());

    // Threshold 1 — udDSPSNBThresh1: 2.0-20.0, step 0.1, default 8.0.
    // Slider internal: 20-200 (×10 scale).
    QSlider* snbK1 = addScaledSlider(snbLay, tr("Threshold 1"),
        20, 200,
        qRound(s.value(QStringLiteral("SnbDefaultK1"), 8.0).toDouble() * 10.0),
        10.0, 1, QString{},
        tr("Multiple of the running noise power at which a sample is\n"
           "flagged as a candidate outlier. Lower = more aggressive\n"
           "first-pass detection; higher = miss weaker noise."));
    connect(snbK1, &QSlider::valueChanged, [channelReady](int v) {
        const double real = static_cast<double>(v) / 10.0;
        AppSettings::instance().setValue(QStringLiteral("SnbDefaultK1"), real);
        if (channelReady()) { SetRXASNBAk1(0, real); }
    });

    // Threshold 2 — udDSPSNBThresh2: 4.0-60.0, step 0.1, default 20.0.
    // Slider internal: 40-600 (×10 scale).
    QSlider* snbK2 = addScaledSlider(snbLay, tr("Threshold 2"),
        40, 600,
        qRound(s.value(QStringLiteral("SnbDefaultK2"), 20.0).toDouble() * 10.0),
        10.0, 1, QString{},
        tr("Multiplier applied to the final detection threshold — confirms\n"
           "candidates from Threshold 1 as real noise outliers. Lower =\n"
           "more aggressive overall blanking; higher = fewer false triggers\n"
           "on genuine voice peaks."));
    connect(snbK2, &QSlider::valueChanged, [channelReady](int v) {
        const double real = static_cast<double>(v) / 10.0;
        AppSettings::instance().setValue(QStringLiteral("SnbDefaultK2"), real);
        if (channelReady()) { SetRXASNBAk2(0, real); }
    });

    // SNB Output Bandwidth — NOT in Thetis Setup page. Thetis sets it
    // automatically per mode in rxa.cs:112-124. Kept as a NereusSDR-native
    // global override.
    QSlider* snbOutBw = addIntSlider(snbLay, tr("Output Bandwidth"),
        100, 96000,
        s.value(QStringLiteral("SnbDefaultOutputBW"), 6000).toInt(),
        tr(" Hz"),
        tr("Width of the audio band SNB operates on, centred on zero.\n"
           "Smaller = focuses the blanker on the active passband;\n"
           "larger = covers wider modes (FM, DRM). Default 6000 Hz\n"
           "covers SSB + AM comfortably."));
    connect(snbOutBw, &QSlider::valueChanged, [channelReady](int v) {
        AppSettings::instance().setValue(QStringLiteral("SnbDefaultOutputBW"), v);
        const double half = static_cast<double>(v) / 2.0;
        if (channelReady()) { SetRXASNBAOutputBandwidth(0, -half, half); }
    });
}

// ══════════════════════════════════════════════════════════════════════════════
// CwSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageCW controls:
//   comboCWKeyerMode, udCWKeyerWeight, tbCWLetterSpacing, tbCWDotDashRatio,
//   udCWSemiBreakInDelay, tbCWSidetoneVolume,
//   chkAPFEnable, udAPFFreq, udAPFBandwidth, tbAPFGain
//
CwSetupPage::CwSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("CW", model, parent)
{
    // ── Keyer ─────────────────────────────────────────────────────────────────
    QGroupBox* keyerGrp = addSection("Keyer");
    QVBoxLayout* keyerLay = qobject_cast<QVBoxLayout*>(keyerGrp->layout());

    auto* keyerMode = new QComboBox;
    keyerMode->addItems({"Iambic A", "Iambic B", "Bug", "Straight"});
    addLabeledCombo(keyerLay, "Mode", keyerMode);

    auto* keyerWeight = new QSpinBox;
    keyerWeight->setRange(10, 90);
    addLabeledSpinner(keyerLay, "Weight", keyerWeight);

    auto* letterSpacing = new QSlider(Qt::Horizontal);
    letterSpacing->setRange(0, 100);
    addLabeledSlider(keyerLay, "Letter Spacing", letterSpacing);

    auto* dotDashRatio = new QSlider(Qt::Horizontal);
    dotDashRatio->setRange(100, 500);
    addLabeledSlider(keyerLay, "Dot-Dash Ratio", dotDashRatio);

    disableGroup(keyerGrp);

    // ── Timing ────────────────────────────────────────────────────────────────
    QGroupBox* timingGrp = addSection("Timing");
    QVBoxLayout* timingLay = qobject_cast<QVBoxLayout*>(timingGrp->layout());

    auto* semiDelay = new QSlider(Qt::Horizontal);
    semiDelay->setRange(0, 2000);
    addLabeledSlider(timingLay, "Semi-Break-In Delay (ms)", semiDelay);

    auto* sidetoneVol = new QSlider(Qt::Horizontal);
    sidetoneVol->setRange(0, 100);
    addLabeledSlider(timingLay, "Sidetone Volume", sidetoneVol);

    disableGroup(timingGrp);

    // ── APF ───────────────────────────────────────────────────────────────────
    QGroupBox* apfGrp = addSection("APF");
    QVBoxLayout* apfLay = qobject_cast<QVBoxLayout*>(apfGrp->layout());

    auto* apfEnable = new QPushButton("Enable");
    addLabeledToggle(apfLay, "Enable", apfEnable);

    auto* apfCenter = new QSlider(Qt::Horizontal);
    apfCenter->setRange(200, 3000);
    addLabeledSlider(apfLay, "Center Freq", apfCenter);

    auto* apfBw = new QSlider(Qt::Horizontal);
    apfBw->setRange(10, 500);
    addLabeledSlider(apfLay, "Bandwidth", apfBw);

    auto* apfGain = new QSlider(Qt::Horizontal);
    apfGain->setRange(0, 100);
    addLabeledSlider(apfLay, "Gain", apfGain);

    disableGroup(apfGrp);
}

// ══════════════════════════════════════════════════════════════════════════════
// AmSamSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageAMSAM controls:
//   tbAMTXCarrierLevel, comboSAMFadeLevel, comboSAMDSBMode,
//   tbAMSquelchThreshold, udAMSquelchMaxTail
//
AmSamSetupPage::AmSamSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("AM/SAM", model, parent)
{
    // ── AM TX ─────────────────────────────────────────────────────────────────
    QGroupBox* amGrp = addSection("AM TX");
    QVBoxLayout* amLay = qobject_cast<QVBoxLayout*>(amGrp->layout());

    auto* carrierLevel = new QSlider(Qt::Horizontal);
    carrierLevel->setRange(0, 100);
    addLabeledSlider(amLay, "Carrier Level", carrierLevel);

    disableGroup(amGrp);

    // ── SAM ───────────────────────────────────────────────────────────────────
    QGroupBox* samGrp = addSection("SAM");
    QVBoxLayout* samLay = qobject_cast<QVBoxLayout*>(samGrp->layout());

    auto* fadeLevel = new QComboBox;
    fadeLevel->addItems({"0", "1", "2", "3", "4", "5"});
    addLabeledCombo(samLay, "Fade Level", fadeLevel);

    auto* dsbMode = new QComboBox;
    dsbMode->addItems({"LSB", "USB", "Both"});
    addLabeledCombo(samLay, "DSB Mode", dsbMode);

    disableGroup(samGrp);

    // ── Squelch ───────────────────────────────────────────────────────────────
    QGroupBox* sqGrp = addSection("Squelch");
    QVBoxLayout* sqLay = qobject_cast<QVBoxLayout*>(sqGrp->layout());

    auto* sqThresh = new QSlider(Qt::Horizontal);
    sqThresh->setRange(-160, 0);
    addLabeledSlider(sqLay, "AM Squelch Threshold", sqThresh);

    auto* sqMaxTail = new QSpinBox;
    sqMaxTail->setRange(1, 1000);
    sqMaxTail->setSuffix(" ms");
    addLabeledSpinner(sqLay, "Max Tail", sqMaxTail);

    disableGroup(sqGrp);
}

// ══════════════════════════════════════════════════════════════════════════════
// FmSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageFM controls:
//   comboFMDeviation, tbFMSquelchThreshold, chkFMDeEmphasis,
//   comboFMTXDeviation, tbFMMicGain, comboFMTXEmphasisPosition
//
FmSetupPage::FmSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("FM", model, parent)
{
    // ── RX ────────────────────────────────────────────────────────────────────
    QGroupBox* rxGrp = addSection("RX");
    QVBoxLayout* rxLay = qobject_cast<QVBoxLayout*>(rxGrp->layout());

    auto* rxDeviation = new QComboBox;
    rxDeviation->addItems({"5k", "2.5k"});
    addLabeledCombo(rxLay, "Deviation", rxDeviation);

    auto* squelchThresh = new QSlider(Qt::Horizontal);
    squelchThresh->setRange(-160, 0);
    addLabeledSlider(rxLay, "Squelch Threshold", squelchThresh);

    auto* deEmphasis = new QPushButton("Enable");
    addLabeledToggle(rxLay, "De-Emphasis", deEmphasis);

    disableGroup(rxGrp);

    // ── TX ────────────────────────────────────────────────────────────────────
    QGroupBox* txGrp = addSection("TX");
    QVBoxLayout* txLay = qobject_cast<QVBoxLayout*>(txGrp->layout());

    auto* txDeviation = new QComboBox;
    txDeviation->addItems({"5k", "2.5k"});
    addLabeledCombo(txLay, "Deviation", txDeviation);

    auto* micGain = new QSlider(Qt::Horizontal);
    micGain->setRange(0, 100);
    addLabeledSlider(txLay, "Mic Gain", micGain);

    auto* emphasisPos = new QComboBox;
    emphasisPos->addItems({"Pre-EQ", "Post-EQ"});
    addLabeledCombo(txLay, "Emphasis Position", emphasisPos);

    disableGroup(txGrp);
}

// ══════════════════════════════════════════════════════════════════════════════
// VoxDexpSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageVOX controls:
//   tbVOXThreshold, tbVOXGain, tbVOXDelay, tbAntiVOXGain,
//   tbDEXPThreshold, tbDEXPAttack, tbDEXPRelease
//
VoxDexpSetupPage::VoxDexpSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("VOX/DEXP", model, parent)
{
    // ── VOX ───────────────────────────────────────────────────────────────────
    QGroupBox* voxGrp = addSection("VOX");
    QVBoxLayout* voxLay = qobject_cast<QVBoxLayout*>(voxGrp->layout());

    auto* voxThresh = new QSlider(Qt::Horizontal);
    voxThresh->setRange(0, 100);
    addLabeledSlider(voxLay, "Threshold", voxThresh);

    auto* voxGain = new QSlider(Qt::Horizontal);
    voxGain->setRange(0, 100);
    addLabeledSlider(voxLay, "Gain", voxGain);

    auto* voxDelay = new QSlider(Qt::Horizontal);
    voxDelay->setRange(0, 2000);
    addLabeledSlider(voxLay, "Delay (ms)", voxDelay);

    auto* antiVoxGain = new QSlider(Qt::Horizontal);
    antiVoxGain->setRange(0, 100);
    addLabeledSlider(voxLay, "Anti-VOX Gain", antiVoxGain);

    disableGroup(voxGrp);

    // ── DEXP ──────────────────────────────────────────────────────────────────
    QGroupBox* dexpGrp = addSection("DEXP");
    QVBoxLayout* dexpLay = qobject_cast<QVBoxLayout*>(dexpGrp->layout());

    auto* dexpThresh = new QSlider(Qt::Horizontal);
    dexpThresh->setRange(0, 100);
    addLabeledSlider(dexpLay, "Threshold", dexpThresh);

    auto* dexpAttack = new QSlider(Qt::Horizontal);
    dexpAttack->setRange(0, 2000);
    addLabeledSlider(dexpLay, "Attack (ms)", dexpAttack);

    auto* dexpRelease = new QSlider(Qt::Horizontal);
    dexpRelease->setRange(0, 2000);
    addLabeledSlider(dexpLay, "Release (ms)", dexpRelease);

    disableGroup(dexpGrp);
}

// ══════════════════════════════════════════════════════════════════════════════
// CfcSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageCFC controls:
//   chkCFCEnable, comboCFCPosition, chkCFCPreCompEQ, lblCFCProfile (display)
//
CfcSetupPage::CfcSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("CFC", model, parent)
{
    // ── CFC ───────────────────────────────────────────────────────────────────
    QGroupBox* cfcGrp = addSection("CFC");
    QVBoxLayout* cfcLay = qobject_cast<QVBoxLayout*>(cfcGrp->layout());

    auto* cfcEnable = new QPushButton("Enable");
    addLabeledToggle(cfcLay, "Enable", cfcEnable);

    auto* cfcPos = new QComboBox;
    cfcPos->addItems({"Pre", "Post"});
    addLabeledCombo(cfcLay, "Position", cfcPos);

    auto* cfcPreComp = new QPushButton("Enable");
    addLabeledToggle(cfcLay, "Pre-Comp EQ", cfcPreComp);

    disableGroup(cfcGrp);

    // ── Profile ───────────────────────────────────────────────────────────────
    QGroupBox* profGrp = addSection("Profile");
    QVBoxLayout* profLay = qobject_cast<QVBoxLayout*>(profGrp->layout());

    auto* profileLabel = new QLabel("(no profile loaded)");
    addLabeledLabel(profLay, "Profile", profileLabel);

    disableGroup(profGrp);
}

// ══════════════════════════════════════════════════════════════════════════════
// MnfSetupPage
// ══════════════════════════════════════════════════════════════════════════════
//
// From Thetis setup.cs — tabDSP / tabPageMNF controls:
//   chkMNFAutoIncrease, comboMNFWindow, lstNotches (display)
//
MnfSetupPage::MnfSetupPage(RadioModel* model, QWidget* parent)
    : SetupPage("MNF", model, parent)
{
    // ── Manual Notch ──────────────────────────────────────────────────────────
    QGroupBox* mnfGrp = addSection("Manual Notch");
    QVBoxLayout* mnfLay = qobject_cast<QVBoxLayout*>(mnfGrp->layout());

    auto* autoIncrease = new QPushButton("Enable");
    addLabeledToggle(mnfLay, "Auto-Increase", autoIncrease);

    auto* windowCombo = new QComboBox;
    windowCombo->addItems({"Blackman-Harris", "Hann", "Flat-Top"});
    addLabeledCombo(mnfLay, "Window", windowCombo);

    auto* notchList = new QLabel("(no notches)");
    addLabeledLabel(mnfLay, "Notch List", notchList);

    disableGroup(mnfGrp);
}

} // namespace NereusSDR
