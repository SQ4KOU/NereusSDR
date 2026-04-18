// =================================================================
// src/gui/applets/FmApplet.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/setup.cs, original licence from Thetis source is included below
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 Layout ports Thetis setup.cs FM tab (38-tone CTCSS list, 5.0k/2.5k deviation presets, Simplex button) + console.cs FM mode wiring. All controls NYI — wired in later phase.
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

//
//=================================================================
// console.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
// Credit is given to Sizenko Alexander of Style-7 (http://www.styleseven.com/) for the Digital-7 font.
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
// Modifications to support the Behringer Midi controllers
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines.
// Modifications for using the new database import function.  W2PA, 29 May 2017
// Support QSK, possible with Protocol-2 firmware v1.7 (Orion-MkI and Orion-MkII), and later.  W2PA, 5 April 2019
// Modfied heavily - Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
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

// Migrated to VS2026 - 18/12/25 MW0LGE v2.10.3.12

#pragma once

#include "AppletWidget.h"

class QComboBox;
class QSlider;
class QPushButton;
class QLabel;

namespace NereusSDR {

// FM mode controls applet — NYI shell (Phase 3I-3).
//
// Controls:
//   1. FM Mic slider       — label + QSlider + value label
//   2. Deviation buttons   — "5.0k" (blue) + "2.5k" (blue), mutually exclusive
//   3. CTCSS enable        — QPushButton("CTCSS", green, checkable)
//   4. CTCSS tone combo    — QComboBox with standard CTCSS tones
//   5. Simplex toggle      — QPushButton("Simplex", green, checkable)
//   6. Repeater offset     — Label + TriBtn(◀) + inset value + TriBtn(▶)
//   7. Offset direction    — QPushButton("-") + QPushButton("+") + QPushButton("Rev"),
//                            mutually exclusive
//   8. FM TX Profile combo — QComboBox with "Default"
//
// All controls are disabled (NYI). Body margins: (4,2,4,2), spacing 2.
class FmApplet : public AppletWidget {
    Q_OBJECT

public:
    explicit FmApplet(RadioModel* model, QWidget* parent = nullptr);

    QString appletId()    const override { return QStringLiteral("FM"); }
    QString appletTitle() const override { return QStringLiteral("FM"); }
    void syncFromModel() override;

private:
    void buildUI();

    // Control 1 — FM Mic
    QSlider*     m_micSlider    = nullptr;
    QLabel*      m_micValueLbl  = nullptr;

    // Control 2 — Deviation
    QPushButton* m_dev5kBtn     = nullptr;
    QPushButton* m_dev25kBtn    = nullptr;

    // Control 3 — CTCSS enable
    QPushButton* m_ctcssBtn     = nullptr;

    // Control 4 — CTCSS tone
    QComboBox*   m_ctcssCombo   = nullptr;

    // Control 5 — Simplex
    QPushButton* m_simplexBtn   = nullptr;

    // Control 6 — Repeater offset stepper
    QLabel*      m_offsetLbl    = nullptr;

    // Control 7 — Offset direction
    QPushButton* m_offsetNegBtn = nullptr;
    QPushButton* m_offsetPosBtn = nullptr;
    QPushButton* m_offsetRevBtn = nullptr;

    // Control 8 — TX profile
    QComboBox*   m_txProfileCombo = nullptr;
};

} // namespace NereusSDR
