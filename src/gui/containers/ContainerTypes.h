#pragma once
// =================================================================
// src/gui/containers/ContainerTypes.h (NereusSDR)
// Ported from Thetis source:
//   Project Files/Source/Console/ucMeter.cs (AxisLock, extracted from ContainerWidget).
// Modification history (NereusSDR):
//   2026-10-02 — J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//                 Share saved enum values without a QWidget dependency.
//                 Header/layout modes and DockMode are NereusSDR-original.
//   2026-10-07 - DockEdge for the workspace preview's four-edge strips
//                 (NereusSDR-original) by J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
//   2026-10-11 - HeaderMode::Always is named Shown; Reveal exists only with
//                 the workspace look off. J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
// =================================================================

/*  ucMeter.cs

This file is part of a program that implements a Software-Defined Radio.

This code/file can be found on GitHub : https://github.com/ramdor/Thetis

Copyright (C) 2020-2026 Richard Samphire MW0LGE

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

namespace NereusSDR {

// From Thetis ucMeter.cs:49-59 [v2.10.3.15] — axis lock positions for docked containers.
enum class AxisLock {
    Left = 0, TopLeft, Top, TopRight,
    Right, BottomRight, Bottom, BottomLeft
};

// Preserve the numeric values already saved by ContainerWidget.
enum class DockMode {
    PanelDocked = 0, // In QSplitter (Container #0 default)
    OverlayDocked,  // Absolute position over central widget (Thetis style)
    Floating        // Separate window
};

// The saved integers. Reveal (shown on hover or focus) exists only with the
// workspace look off. With the look on a header is Shown or Hidden: the
// manager saves a Reveal it loads as Hidden, and anything that still holds
// Reveal there treats it as Hidden.
enum class HeaderMode { Shown = 0, Reveal = 1, Hidden = 2 };
enum class ContentLayout { LegacyCanvas = 0, VerticalStack = 1, FreeCanvas = 2 };
// Workspace preview: the strip a PanelDocked container sits in. Saved by
// name ("Left", "Top", "Bottom"); Right is the default and is not written.
enum class DockEdge { Left = 0, Right = 1, Top = 2, Bottom = 3 };

} // namespace NereusSDR
