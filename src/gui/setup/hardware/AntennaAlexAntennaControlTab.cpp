// =================================================================
// src/gui/setup/hardware/AntennaAlexAntennaControlTab.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/setup.designer.cs (~lines 5981-7000+,
//     grpAlexAntCtrl + panelAlexTXAntControl + panelAlexRXAntControl)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-20 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                (KG4VCF), with AI-assisted transformation via Anthropic
//                Claude Code. Sub-sub-tab under Hardware → Antenna/ALEX.
//                Per-band antenna assignment + Block-TX safety; backed
//                by AlexController model (Phase 3P-F Task 1).
// =================================================================

//=================================================================
// setup.designer.cs
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
//
// === Verbatim Thetis Console/setup.designer.cs header (lines 1-50) ===
// namespace Thetis { using System.Windows.Forms; partial class Setup {
//   #region Windows Form Designer generated code
//   private void InitializeComponent() {
//     this.components = new System.ComponentModel.Container();
//     System.Windows.Forms.TabPage tpAlexAntCtrl;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS3;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS4;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS6;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS9;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS10;
//     System.Windows.Forms.NumericUpDownTS numericUpDownTS12;
//     System.ComponentModel.ComponentResourceManager resources = ...;
//     this.chkForceATTwhenOutPowerChanges_decreased = new CheckBoxTS();
//     this.chkUndoAutoATTTx = new CheckBoxTS();
//     this.chkAutoATTTXPsOff = new CheckBoxTS();
//     this.lblTXattBand = new LabelTS();
//     this.chkForceATTwhenOutPowerChanges = new CheckBoxTS();
//     this.chkForceATTwhenPSAoff = new CheckBoxTS();
//     this.chkEnableXVTRHF = new CheckBoxTS();
//     this.chkBPF2Gnd = new CheckBoxTS();
//     this.chkDisableRXOut = new CheckBoxTS();
//     this.chkEXT2OutOnTx = new CheckBoxTS();
//     this.chkEXT1OutOnTx = new CheckBoxTS();
//     this.labelATTOnTX = new LabelTS();
// =================================================================

#include "AntennaAlexAntennaControlTab.h"

#include "core/accessories/AlexController.h"
#include "models/RadioModel.h"
#include "models/Band.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QRadioButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace NereusSDR {

// ── Constructor ───────────────────────────────────────────────────────────────

AntennaAlexAntennaControlTab::AntennaAlexAntennaControlTab(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
    , m_alex(&model->alexControllerMutable())
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    outerLayout->setSpacing(6);

    // Row 1: Block-TX safety strip
    buildBlockTxStrip(outerLayout);

    // Row 2: TX + RX grids side-by-side, inside a scroll area so they don't
    // overflow when the window is short.
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto* scrollContents = new QWidget(scrollArea);
    auto* gridRow = new QHBoxLayout(scrollContents);
    gridRow->setContentsMargins(0, 0, 0, 0);
    gridRow->setSpacing(12);

    buildTxGrid(gridRow);   // pass QHBoxLayout* — overloaded via QBoxLayout*
    buildRxGrid(gridRow);

    scrollArea->setWidget(scrollContents);
    outerLayout->addWidget(scrollArea, 1);

    // ── Connect model → UI ────────────────────────────────────────────────────
    connect(m_alex, &AlexController::antennaChanged,
            this, &AntennaAlexAntennaControlTab::onAntennaChanged);
    connect(m_alex, &AlexController::blockTxChanged,
            this, &AntennaAlexAntennaControlTab::onBlockTxChanged);
}

// ── buildBlockTxStrip ─────────────────────────────────────────────────────────
//
// Source: Thetis grpAlexAntCtrl — chkAlexBlockTxAnt2 / chkAlexBlockTxAnt3
// (setup.designer.cs:5981-6001) [@501e3f5]
//
// NereusSDR addition: these Block-TX flags are not in Thetis's setup designer;
// they map to AlexController::blockTxAnt2/3 (Phase 3P-F Task 1 NereusSDR spin).

void AntennaAlexAntennaControlTab::buildBlockTxStrip(QVBoxLayout* outerLayout)
{
    auto* frame = new QFrame(this);
    frame->setFrameShape(QFrame::StyledPanel);
    frame->setStyleSheet(QStringLiteral(
        "QFrame { background-color: rgba(200,50,50,0.08); border: 1px solid rgba(200,50,50,0.4); border-radius: 4px; }"));

    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(8, 4, 8, 4);
    row->setSpacing(16);

    m_blockTxAnt2 = new QCheckBox(tr("Block TX on Ant 2"), frame);
    m_blockTxAnt2->setChecked(m_alex->blockTxAnt2());
    m_blockTxAnt2->setToolTip(tr("Prevents transmit assignments to Antenna Port 2. "
                                  "Use when Ant 2 is wired for receive only."));
    row->addWidget(m_blockTxAnt2);

    m_blockTxAnt3 = new QCheckBox(tr("Block TX on Ant 3"), frame);
    m_blockTxAnt3->setChecked(m_alex->blockTxAnt3());
    m_blockTxAnt3->setToolTip(tr("Prevents transmit assignments to Antenna Port 3. "
                                  "Use when Ant 3 is wired for receive only."));
    row->addWidget(m_blockTxAnt3);

    auto* note = new QLabel(tr("<i>Safety: prevent transmit into RX-only / receive-loop antennas.</i>"), frame);
    note->setWordWrap(false);
    row->addWidget(note);
    row->addStretch();

    outerLayout->addWidget(frame);

    // ── Wire Block-TX checkboxes → controller ─────────────────────────────────
    connect(m_blockTxAnt2, &QCheckBox::toggled, this, [this](bool checked) {
        m_alex->setBlockTxAnt2(checked);
    });
    connect(m_blockTxAnt3, &QCheckBox::toggled, this, [this](bool checked) {
        m_alex->setBlockTxAnt3(checked);
    });
}

// ── buildTxGrid ───────────────────────────────────────────────────────────────
//
// Source: Thetis panelAlexTXAntControl (setup.designer.cs:~6002-6400) [@501e3f5]
// 14 rows (one per Band) × 3 radio buttons (Ant 1 / Ant 2 / Ant 3).

void AntennaAlexAntennaControlTab::buildTxGrid(QBoxLayout* outerLayout)
{
    auto* grp = new QGroupBox(tr("TX Antenna per Band"), this);
    auto* layout = new QVBoxLayout(grp);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    // Header row
    auto* hdrRow = new QHBoxLayout();
    hdrRow->setContentsMargins(0, 0, 0, 0);
    auto* hdrBand = new QLabel(tr("Band"), grp);
    hdrBand->setFixedWidth(48);
    hdrRow->addWidget(hdrBand);
    for (const char* lbl : {"Ant 1", "Ant 2", "Ant 3"}) {
        auto* h = new QLabel(tr(lbl), grp);
        h->setAlignment(Qt::AlignCenter);
        hdrRow->addWidget(h, 1);
    }
    layout->addLayout(hdrRow);

    // Band rows
    for (int b = 0; b < kBandCount; ++b) {
        auto band = static_cast<Band>(b);
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* bandLbl = new QLabel(bandLabel(band), grp);
        bandLbl->setFixedWidth(48);
        rowLayout->addWidget(bandLbl);

        auto* grpBtn = new QButtonGroup(this);
        m_txGroups[b] = grpBtn;

        const int currentAnt = m_alex->txAnt(band);  // 1-based

        for (int a = 0; a < 3; ++a) {
            auto* rb = new QRadioButton(grp);
            rb->setChecked((a + 1) == currentAnt);
            rb->setToolTip(tr("TX Ant %1 for %2").arg(a + 1).arg(bandLabel(band)));
            grpBtn->addButton(rb, a + 1);  // button id = 1-based ant number
            m_txButtons[b][a] = rb;
            rowLayout->addWidget(rb, 1, Qt::AlignCenter);

            // Wire to controller
            connect(rb, &QRadioButton::toggled, this, [this, band, antNum = a + 1](bool checked) {
                if (!checked) { return; }
                m_alex->setTxAnt(band, antNum);
            });
        }
        layout->addLayout(rowLayout);
    }

    layout->addStretch();
    outerLayout->addWidget(grp, 1);

    // Apply initial blocked state
    updateTxBlockedStates();
}

// ── buildRxGrid ───────────────────────────────────────────────────────────────
//
// Source: Thetis panelAlexRXAntControl (setup.designer.cs:~6401-7000) [@501e3f5]
// 14 rows × 6 radio buttons: RX1 (1/2/3) + visual separator + RX-only (1/2/3).

void AntennaAlexAntennaControlTab::buildRxGrid(QBoxLayout* outerLayout)
{
    auto* grp = new QGroupBox(tr("RX1 / RX2 Antenna per Band"), this);
    auto* layout = new QVBoxLayout(grp);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(2);

    // Header row
    auto* hdrRow = new QHBoxLayout();
    hdrRow->setContentsMargins(0, 0, 0, 0);
    auto* hdrBand = new QLabel(tr("Band"), grp);
    hdrBand->setFixedWidth(48);
    hdrRow->addWidget(hdrBand);
    // RX1 sub-header
    auto* rx1Hdr = new QLabel(tr("RX1"), grp);
    rx1Hdr->setAlignment(Qt::AlignCenter);
    hdrRow->addWidget(rx1Hdr, 3);
    // separator
    auto* sep = new QFrame(grp);
    sep->setFrameShape(QFrame::VLine);
    sep->setFrameShadow(QFrame::Sunken);
    hdrRow->addWidget(sep);
    // RX-only sub-header
    auto* rxOnlyHdr = new QLabel(tr("RX-only"), grp);
    rxOnlyHdr->setAlignment(Qt::AlignCenter);
    hdrRow->addWidget(rxOnlyHdr, 3);
    layout->addLayout(hdrRow);

    // Second header row with individual ant labels
    auto* hdrRow2 = new QHBoxLayout();
    hdrRow2->setContentsMargins(0, 0, 0, 0);
    hdrRow2->addSpacing(48);  // band label space
    for (int i = 0; i < 2; ++i) {
        for (const char* lbl : {"1", "2", "3"}) {
            auto* h = new QLabel(tr(lbl), grp);
            h->setAlignment(Qt::AlignCenter);
            hdrRow2->addWidget(h, 1);
        }
        if (i == 0) {
            auto* sep2 = new QFrame(grp);
            sep2->setFrameShape(QFrame::VLine);
            sep2->setFrameShadow(QFrame::Sunken);
            hdrRow2->addWidget(sep2);
        }
    }
    layout->addLayout(hdrRow2);

    // Band rows
    for (int b = 0; b < kBandCount; ++b) {
        auto band = static_cast<Band>(b);
        auto* rowLayout = new QHBoxLayout();
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(4);

        auto* bandLbl = new QLabel(bandLabel(band), grp);
        bandLbl->setFixedWidth(48);
        rowLayout->addWidget(bandLbl);

        // RX1 group
        auto* rx1Grp = new QButtonGroup(this);
        m_rx1Groups[b] = rx1Grp;
        const int currentRx1 = m_alex->rxAnt(band);  // 1-based
        for (int a = 0; a < 3; ++a) {
            auto* rb = new QRadioButton(grp);
            rb->setChecked((a + 1) == currentRx1);
            rb->setToolTip(tr("RX1 Ant %1 for %2").arg(a + 1).arg(bandLabel(band)));
            rx1Grp->addButton(rb, a + 1);
            m_rx1Buttons[b][a] = rb;
            rowLayout->addWidget(rb, 1, Qt::AlignCenter);

            connect(rb, &QRadioButton::toggled, this, [this, band, antNum = a + 1](bool checked) {
                if (!checked) { return; }
                m_alex->setRxAnt(band, antNum);
            });
        }

        // Visual separator
        auto* rowSep = new QFrame(grp);
        rowSep->setFrameShape(QFrame::VLine);
        rowSep->setFrameShadow(QFrame::Sunken);
        rowLayout->addWidget(rowSep);

        // RX-only group
        auto* rxOnlyGrp = new QButtonGroup(this);
        m_rxOnlyGroups[b] = rxOnlyGrp;
        const int currentRxOnly = m_alex->rxOnlyAnt(band);  // 1-based
        for (int a = 0; a < 3; ++a) {
            auto* rb = new QRadioButton(grp);
            rb->setChecked((a + 1) == currentRxOnly);
            rb->setToolTip(tr("RX-only Ant %1 for %2").arg(a + 1).arg(bandLabel(band)));
            rxOnlyGrp->addButton(rb, a + 1);
            m_rxOnlyButtons[b][a] = rb;
            rowLayout->addWidget(rb, 1, Qt::AlignCenter);

            connect(rb, &QRadioButton::toggled, this, [this, band, antNum = a + 1](bool checked) {
                if (!checked) { return; }
                m_alex->setRxOnlyAnt(band, antNum);
            });
        }

        layout->addLayout(rowLayout);
    }

    layout->addStretch();
    outerLayout->addWidget(grp, 2);
}

// ── controller accessor ───────────────────────────────────────────────────────

AlexController& AntennaAlexAntennaControlTab::controller()
{
    return *m_alex;
}

// ── slots ─────────────────────────────────────────────────────────────────────

void AntennaAlexAntennaControlTab::onAntennaChanged(Band band)
{
    const int row = static_cast<int>(band);
    if (row < 0 || row >= kBandCount) { return; }
    syncTxRow(row);
    syncRxRow(row);
}

void AntennaAlexAntennaControlTab::onBlockTxChanged()
{
    // Sync Block-TX checkbox states from controller
    {
        QSignalBlocker b2(m_blockTxAnt2);
        m_blockTxAnt2->setChecked(m_alex->blockTxAnt2());
    }
    {
        QSignalBlocker b3(m_blockTxAnt3);
        m_blockTxAnt3->setChecked(m_alex->blockTxAnt3());
    }
    updateTxBlockedStates();
}

// ── private helpers ───────────────────────────────────────────────────────────

void AntennaAlexAntennaControlTab::syncTxRow(int row)
{
    auto band = static_cast<Band>(row);
    const int currentAnt = m_alex->txAnt(band);  // 1-based
    for (int a = 0; a < 3; ++a) {
        if (auto* rb = m_txButtons[row][a]) {
            QSignalBlocker sb(rb);
            rb->setChecked((a + 1) == currentAnt);
        }
    }
}

void AntennaAlexAntennaControlTab::syncRxRow(int row)
{
    auto band = static_cast<Band>(row);
    const int currentRx1     = m_alex->rxAnt(band);
    const int currentRxOnly  = m_alex->rxOnlyAnt(band);
    for (int a = 0; a < 3; ++a) {
        if (auto* rb = m_rx1Buttons[row][a]) {
            QSignalBlocker sb(rb);
            rb->setChecked((a + 1) == currentRx1);
        }
        if (auto* rb = m_rxOnlyButtons[row][a]) {
            QSignalBlocker sb(rb);
            rb->setChecked((a + 1) == currentRxOnly);
        }
    }
}

void AntennaAlexAntennaControlTab::updateTxBlockedStates()
{
    // When a TX port is blocked, grey out (disable) that column's radio buttons
    // for all bands so the user cannot select a blocked port.
    // Port columns: a == 0 → Ant 1, a == 1 → Ant 2, a == 2 → Ant 3.
    const bool blk2 = m_alex->blockTxAnt2();
    const bool blk3 = m_alex->blockTxAnt3();

    for (int b = 0; b < kBandCount; ++b) {
        if (auto* rb = m_txButtons[b][1]) { rb->setEnabled(!blk2); }  // Ant 2
        if (auto* rb = m_txButtons[b][2]) { rb->setEnabled(!blk3); }  // Ant 3
    }
}

} // namespace NereusSDR
