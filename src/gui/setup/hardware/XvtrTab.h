#pragma once
// =================================================================
// src/gui/setup/hardware/XvtrTab.h  (NereusSDR)
// =================================================================
//
// Ported from multiple Thetis sources:
//   Project Files/Source/Console/xvtr.cs
//
// Original Thetis copyright and license (preserved per GNU GPL,
// representing the union of contributors across all cited sources):
//
//   Thetis is a C# implementation of a Software Defined Radio.
//   Copyright (C) 2004-2009  FlexRadio Systems
//   Copyright (C) 2010-2013  Doug Wigley (W5WC)
//   Copyright (C) 2019-2026  Richard Samphire (MW0LGE) — heavily modified
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
//   2026-04-17 — Synthesized in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code. Combines logic from the Thetis sources
//                 listed above.
// =================================================================

// XvtrTab.h
//
// "XVTR" sub-tab of HardwarePage.
//
// Source: Thetis xvtr.cs XVTRForm class (lines 47-249).
// Columns per row: Enabled (chkEnable*), Name (txtButtonText*),
// RF Start (udFreqBegin*), RF End (udFreqEnd*), LO Offset (udLOOffset*),
// RX-only (chkRXOnly*), Power (udPower*), LO Error (udLOError*).
// Thetis has 16 rows (chkEnable0..15); we expose 5 rows by default
// (caps.xvtrJackCount rows), capped at 16.
//
// "Auto-select active band" checkbox — controls whether the active VFO
// band automatically selects the matching XVTR row.

#include <QVariant>
#include <QWidget>

class QCheckBox;
class QLabel;
class QTableWidget;
class QTableWidgetItem;

namespace NereusSDR {

class RadioModel;
struct RadioInfo;
struct BoardCapabilities;

class XvtrTab : public QWidget {
    Q_OBJECT
public:
    explicit XvtrTab(RadioModel* model, QWidget* parent = nullptr);
    void populate(const RadioInfo& info, const BoardCapabilities& caps);
    void restoreSettings(const QMap<QString, QVariant>& settings);

signals:
    void settingChanged(const QString& key, const QVariant& value);

private slots:
    void onTableItemChanged(QTableWidgetItem* item);

private:
    RadioModel*   m_model{nullptr};

    QCheckBox*    m_autoSelectBand{nullptr};
    QTableWidget* m_table{nullptr};
    QLabel*       m_noXvtrLabel{nullptr};

    // Number of rows currently shown
    int m_visibleRows{0};
};

} // namespace NereusSDR
