#pragma once
// =================================================================
// src/gui/setup/hardware/Hl2IoBoardTab.h  (NereusSDR)
// =================================================================
//
// Ported from mi0bot/Thetis-HL2 source:
//   HPSDR/IoBoardHl2.cs
//
// Original copyright and license (preserved per GNU GPL):
//
//   Thetis / Thetis-HL2 is a C# implementation of a Software Defined Radio.
//   Copyright (C) 2004-2009  FlexRadio Systems
//   Copyright (C) 2010-2020  Doug Wigley (W5WC)
//   Copyright (C) 2019-2026  Richard Samphire (MW0LGE) — Thetis heavy modifications
//   Copyright (C) 2020-2026  MI0BOT — Hermes-Lite fork contributions
//
//   This program is free software; you can redistribute it and/or
//   modify it under the terms of the GNU General Public License
//   as published by the Free Software Foundation; either version 2
//   of the License, or (at your option) any later version.
//
//   This program is distributed in the hope that it will be useful,
//   but WITHOUT ANY WARRANTY; without even the implied warranty of
//   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//   GNU General Public License for more details.
//
// Dual-Licensing Statement (applies ONLY to Richard Samphire MW0LGE's
// contributions in the upstream Thetis source — preserved verbatim from
// Thetis LICENSE-DUAL-LICENSING):
//
//   For any code originally written by Richard Samphire MW0LGE, or for
//   any modifications made by him, the copyright holder for those
//   portions (Richard Samphire) reserves the right to use, license, and
//   distribute such code under different terms, including closed-source
//   and proprietary licences, in addition to the GNU General Public
//   License granted in LICENCE. Nothing in this statement restricts any
//   rights granted to recipients under the GNU GPL.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Structural template follows AetherSDR
//                 (ten9876/AetherSDR) Qt6 conventions.
// =================================================================

// Hl2IoBoardTab.h
//
// "HL2 I/O Board" sub-tab of HardwarePage.
// Surfaces the Hermes Lite 2 I/O board GPIO/PTT/aux-output configuration.
//
// Source: mi0bot Thetis-HL2 HPSDR/IoBoardHl2.cs —
//   GPIO_DIRECT_BASE = 170 (line 79), I2C address 0x1d (line 139),
//   register REG_TX_FREQ_BYTE* (lines 194-198).
//
// NOTE: IoBoardHl2.cs wraps closed-source I2C register code. The pin names
// and GPIO counts here (0-3) are reasonable defaults based on the HL2 open
// hardware spec (Hermes Lite 2 BOM/schematic). Pending real HL2 smoke test.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QGroupBox;
class QLabel;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class Hl2IoBoardTab : public QWidget {
    Q_OBJECT
public:
    explicit Hl2IoBoardTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private:
    RadioModel*  m_model{nullptr};

    // I/O board present indicator (read-only once connected)
    QLabel*      m_ioBoardPresentLabel{nullptr};

    // GPIO pin combo boxes
    QComboBox*   m_extPttInputCombo{nullptr};
    QComboBox*   m_cwKeyInputCombo{nullptr};

    // Aux output assignment combo boxes
    QComboBox*   m_auxOut1Combo{nullptr};
    QComboBox*   m_auxOut2Combo{nullptr};
};

} // namespace NereusSDR
