// =================================================================
// src/gui/setup/AudioOutputPage.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original Setup → Audio → Output page.
// No Thetis port, no attribution headers required (per memory:
// feedback_source_first_ui_vs_dsp — Qt widgets in Setup pages are
// NereusSDR-native).
//
// Phase 3O Task 20 (2026-04-24): Three output cards (Primary,
// Sidetone, Monitor) each with target-sink combo, quantum preset
// buttons (128/256/512/1024/custom), read-only node-info labels,
// and a placeholder telemetry block. Primary card adds volume slider
// + mute-on-TX checkbox. Per-slice routing section at the bottom.
//
// Design spec: docs/architecture/2026-04-23-linux-audio-pipewire-plan.md
// §§9.3 + 10.
// =================================================================
// Modification history (NereusSDR):
//   2026-04-24 — Written by J.J. Boyd (KG4VCF), with AI-assisted
//                transformation via Anthropic Claude Code.
// =================================================================

// SPDX-License-Identifier: GPL-2.0-or-later
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License as
// published by the Free Software Foundation; version 2.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.

#pragma once

#include "gui/SetupPage.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVector>
#include <QWidget>

namespace NereusSDR {

class AudioEngine;
class RadioModel;
class SliceModel;

// ---------------------------------------------------------------------------
// OutputCard — one output-stream card (Primary / Sidetone / Monitor).
//
// Each card contains:
//   • target-sink combo (follow default + named sinks placeholder)
//   • read-only node info labels (node.name, node.id, media.role,
//     rate, format)
//   • quantum preset buttons (128 / 256 / 512 / 1024 / custom)
//   • placeholder telemetry block (measuredLatency, xruns, CPU, state)
//   • optional volume slider + mute-on-TX checkbox (Primary only)
//   • optional RT-warning gear label (Sidetone @ quantum 128)
//
// Persistence keys are passed in via settingsPrefix, e.g.
// "Audio/Output/Primary".
// ---------------------------------------------------------------------------
class OutputCard : public QGroupBox {
    Q_OBJECT
public:
    enum class Role { Primary, Sidetone, Monitor };

    explicit OutputCard(const QString& title,
                        Role            role,
                        const QString&  settingsPrefix,
                        QWidget*        parent = nullptr);

    // Reload all controls from AppSettings (called during construction).
    void loadFromSettings();

    // Persist current control state to AppSettings.
    void saveToSettings();

signals:
    void sinkNodeNameChanged(const QString& nodeName);
    void quantumChanged(int quantum);

private slots:
    void onSinkComboChanged(int index);
    void onQuantumPreset(int quantum);
    void onCustomQuantum();
    void onVolumeChanged(int value);
    void onMuteOnTxChanged(bool checked);

private:
    void buildLayout();

    Role            m_role;
    QString         m_settingsPrefix;

    // Target sink
    QComboBox*      m_sinkCombo{nullptr};

    // Node info (read-only)
    QLabel*         m_nodeNameLabel{nullptr};
    QLabel*         m_nodeIdLabel{nullptr};
    QLabel*         m_mediaRoleLabel{nullptr};
    QLabel*         m_rateLabel{nullptr};
    QLabel*         m_formatLabel{nullptr};

    // Quantum preset buttons
    QPushButton*    m_q128Btn{nullptr};
    QPushButton*    m_q256Btn{nullptr};
    QPushButton*    m_q512Btn{nullptr};
    QPushButton*    m_q1024Btn{nullptr};
    QPushButton*    m_qCustomBtn{nullptr};
    QSpinBox*       m_customQuantumSpin{nullptr};
    int             m_currentQuantum{512};

    // Telemetry (placeholder — see TODO comment in .cpp)
    QLabel*         m_measuredLatencyLabel{nullptr};
    QLabel*         m_ringBreakdownLabel{nullptr};
    QLabel*         m_xrunLabel{nullptr};
    QLabel*         m_cpuLabel{nullptr};
    QLabel*         m_stateLabel{nullptr};

    // Primary-only controls
    QSlider*        m_volumeSlider{nullptr};
    QLabel*         m_volumeValueLabel{nullptr};
    QCheckBox*      m_muteOnTxCheck{nullptr};

    // Sidetone-only RT warning
    QLabel*         m_rtWarningLabel{nullptr};
};

// ---------------------------------------------------------------------------
// AudioOutputPage
// ---------------------------------------------------------------------------
class AudioOutputPage : public SetupPage {
    Q_OBJECT
public:
    explicit AudioOutputPage(AudioEngine* eng,
                             RadioModel*  radio,
                             QWidget*     parent = nullptr);

private:
    void buildPage();
    void buildPerSliceSection();

    AudioEngine*          m_eng{nullptr};
    RadioModel*           m_radio{nullptr};

    OutputCard*           m_primaryCard{nullptr};
    OutputCard*           m_sidetoneCard{nullptr};
    OutputCard*           m_monitorCard{nullptr};

    // Per-slice combos — one per active SliceModel, built at ctor time.
    // Each combo is wired to slice->setSinkNodeName() directly.
    QVector<QComboBox*>   m_sliceSinkCombos;

};

} // namespace NereusSDR
