#pragma once


// =================================================================
// src/gui/meters/OtherButtonItem.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/MeterManager.cs
//
// Original Thetis copyright and license (preserved per GNU GPL):
//
//   Thetis is a C# implementation of a Software Defined Radio.
//   Copyright (C) 2020-2026  Richard Samphire (MW0LGE)
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
//   You should have received a copy of the GNU General Public License
//   along with this program; if not, write to the Free Software
//   Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
//
// Dual-Licensing Statement (applies ONLY to Richard Samphire MW0LGE's
// contributions — preserved verbatim from Thetis LICENSE-DUAL-LICENSING):
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
//   2026-04-16 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Structural template follows AetherSDR
//                 (ten9876/AetherSDR) Qt6 conventions.
// =================================================================

#include "ButtonBoxItem.h"

#include <QVector>

namespace NereusSDR {

// Miscellaneous control buttons + 31 macro slots.
// Ported from Thetis clsOtherButtons (MeterManager.cs:8225+).
class OtherButtonItem : public ButtonBoxItem {
    Q_OBJECT

public:
    enum class ButtonId {
        Power, Rx2, Mon, Tun, Mox, TwoTon, Dup, PsA,
        Play, Rec, Anf, Snb, Mnf, Avg, PeakHold, Ctun,
        Vac1, Vac2, Mute, Bin, SubRx, PanSwap, Xpa,
        Spectrum, Panadapter, Scope, Scope2, Phase,
        Waterfall, Histogram, Panafall, Panascope, Spectrascope,
        DisplayOff,
        Macro0 = 64
    };

    enum class MacroType { Off, On, Toggle, Led, ContainerVis, Cat };

    struct MacroSettings {
        MacroType type{MacroType::Toggle};
        QString onText;
        QString offText;
        QString catCommand;
        int containerVisibleId{-1};
    };

    explicit OtherButtonItem(QObject* parent = nullptr);

    void setButtonState(ButtonId id, bool on);
    MacroSettings& macroSettings(int macroIndex);

    Layer renderLayer() const override { return Layer::OverlayDynamic; }
    QString serialize() const override;
    bool deserialize(const QString& data) override;

signals:
    void otherButtonClicked(int buttonId);
    void macroTriggered(int macroIndex);

private:
    void onButtonClicked(int index, Qt::MouseButton button);

    static constexpr int kCoreButtonCount = 34;
    static constexpr int kMacroCount = 31;

    QVector<int> m_buttonMap;
    QVector<MacroSettings> m_macroSettings;
};

} // namespace NereusSDR
