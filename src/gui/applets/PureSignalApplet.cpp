// =================================================================
// src/gui/applets/PureSignalApplet.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/PSForm.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 Layout ports Thetis PSForm.cs (PureSignal feedback/correction controls). All controls NYI — wired in later phase (3M-4).
// =================================================================

/*  PSForm.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2000-2025 Original authors
Copyright (C) 2020-2025 Richard Samphire MW0LGE

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

The author can be reached by email at

mw0lge@grange-lane.co.uk
*/
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

#include "PureSignalApplet.h"
#include "NyiOverlay.h"
#include "gui/HGauge.h"
#include "gui/StyleConstants.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>

namespace NereusSDR {

PureSignalApplet::PureSignalApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent)
{
    buildUI();
}

void PureSignalApplet::buildUI()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(appletTitleBar(QStringLiteral("PureSignal")));

    auto* body = new QWidget(this);
    auto* vbox = new QVBoxLayout(body);
    vbox->setContentsMargins(4, 2, 4, 4);
    vbox->setSpacing(2);

    // --- Control 1+2: Calibrate + Auto-cal row ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // Control 1: calibrate (non-toggle push button)
        m_calibrateBtn = styledButton(QStringLiteral("Calibrate"));
        row->addWidget(m_calibrateBtn);

        // Control 2: auto-cal (green toggle)
        m_autoCalBtn = greenToggle(QStringLiteral("Auto"));
        m_autoCalBtn->setCheckable(true);
        row->addWidget(m_autoCalBtn);
        row->addStretch();

        vbox->addLayout(row);

        NyiOverlay::markNyi(m_calibrateBtn, QStringLiteral("3I-4"));
        NyiOverlay::markNyi(m_autoCalBtn,   QStringLiteral("3I-4"));
    }

    // --- Control 3: Feedback level gauge (0-100, yellow@70, red@90) ---
    m_feedbackGauge = new HGauge(this);
    m_feedbackGauge->setRange(0.0, 100.0);
    m_feedbackGauge->setYellowStart(70.0);
    m_feedbackGauge->setRedStart(90.0);
    m_feedbackGauge->setTitle(QStringLiteral("FB Level"));
    vbox->addWidget(m_feedbackGauge);
    NyiOverlay::markNyi(m_feedbackGauge, QStringLiteral("3I-4"));

    // --- Control 4: Correction magnitude gauge (0-100, yellow@80, red@95) ---
    m_correctionGauge = new HGauge(this);
    m_correctionGauge->setRange(0.0, 100.0);
    m_correctionGauge->setYellowStart(80.0);
    m_correctionGauge->setRedStart(95.0);
    m_correctionGauge->setTitle(QStringLiteral("Correction"));
    vbox->addWidget(m_correctionGauge);
    NyiOverlay::markNyi(m_correctionGauge, QStringLiteral("3I-4"));

    // --- Control 5+6+7: Save / Restore / Two-tone row ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(4);

        // Control 5: save coefficients
        m_saveBtn = styledButton(QStringLiteral("Save"));
        row->addWidget(m_saveBtn);

        // Control 6: restore coefficients
        m_restoreBtn = styledButton(QStringLiteral("Restore"));
        row->addWidget(m_restoreBtn);

        // Control 7: two-tone test (green toggle)
        m_twoToneBtn = greenToggle(QStringLiteral("2-Tone"));
        m_twoToneBtn->setCheckable(true);
        row->addWidget(m_twoToneBtn);
        row->addStretch();

        vbox->addLayout(row);

        NyiOverlay::markNyi(m_saveBtn,     QStringLiteral("3I-4"));
        NyiOverlay::markNyi(m_restoreBtn,  QStringLiteral("3I-4"));
        NyiOverlay::markNyi(m_twoToneBtn,  QStringLiteral("3I-4"));
    }

    // --- Control 8: Status LEDs row — "Cal", "Run", "Fbk" ---
    {
        auto* row = new QHBoxLayout;
        row->setSpacing(6);

        const QString ledNames[3] = {
            QStringLiteral("Cal"),
            QStringLiteral("Run"),
            QStringLiteral("Fbk")
        };

        const QString ledInactive = QStringLiteral(
            "QLabel {"
            "  background: #405060; border-radius: 4px;"
            "  color: #6080a0; font-size: 8px; font-weight: bold;"
            "  padding: 0px 2px;"
            "}");

        for (int i = 0; i < 3; ++i) {
            m_led[i] = new QLabel(ledNames[i], this);
            m_led[i]->setFixedSize(24, 14);
            m_led[i]->setAlignment(Qt::AlignCenter);
            m_led[i]->setStyleSheet(ledInactive);
            row->addWidget(m_led[i]);
        }
        row->addStretch();
        vbox->addLayout(row);
    }

    // --- Info readout labels ---
    m_iterations   = new QLabel(QStringLiteral("Iterations: 0"), this);
    m_feedbackDb   = new QLabel(QStringLiteral("Feedback: \u2014 dB"), this);
    m_correctionDb = new QLabel(QStringLiteral("Correction: \u2014 dB"), this);

    const QString infoStyle = QStringLiteral(
        "QLabel { font-size: 10px; color: %1; }").arg(Style::kTextSecondary);
    for (QLabel* lbl : {m_iterations, m_feedbackDb, m_correctionDb}) {
        lbl->setStyleSheet(infoStyle);
        vbox->addWidget(lbl);
    }

    vbox->addStretch();
    root->addWidget(body);
}

void PureSignalApplet::syncFromModel()
{
    // NYI — Phase 3I-4
}

} // namespace NereusSDR
