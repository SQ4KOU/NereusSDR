// =================================================================
// tests/tst_slice_audio_panel.cpp  (NereusSDR)
// =================================================================
//
// Ported from multiple Thetis sources:
//   Project Files/Source/Console/dsp.cs
//   Project Files/Source/Console/radio.cs
//
// Original Thetis copyright and license (preserved per GNU GPL,
// representing the union of contributors across all cited sources):
//
//   Thetis is a C# implementation of a Software Defined Radio.
//   Copyright (C) 2004-2009  FlexRadio Systems
//   Copyright (C) 2010-2020  Doug Wigley (W5WC)
//   Copyright (C) 2013-2017  Warren Pratt (NR0V) [dsp.cs]
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

// tst_slice_audio_panel.cpp
//
// Verifies that SliceModel mute / audio pan / binaural setters store + emit
// signals correctly. SliceModel is the single source of truth for VFO/DSP state.
// Setters must:
//   1. Guard against no-op updates (unchanged value → no signal)
//   2. Store the new value
//   3. Emit the corresponding changed signal
//
// Source citations:
//   From Thetis Project Files/Source/Console/dsp.cs:393-394 — PanelRun
//   From Thetis Project Files/Source/Console/radio.cs:1386-1403 — Pan
//   From Thetis Project Files/Source/Console/radio.cs:1145-1162 — BinOn
//   WDSP: third_party/wdsp/src/patchpanel.c:126,159,187

#include <QtTest/QtTest>
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceAudioPanel : public QObject {
    Q_OBJECT

private slots:
    // ── muted ────────────────────────────────────────────────────────────────

    void mutedDefaultIsFalse() {
        SliceModel s;
        QCOMPARE(s.muted(), false);
    }

    void setMutedStoresValue() {
        SliceModel s;
        s.setMuted(true);
        QCOMPARE(s.muted(), true);
    }

    void setMutedEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::mutedChanged);
        s.setMuted(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setMutedNoSignalOnSameValue() {
        SliceModel s;
        s.setMuted(true);
        QSignalSpy spy(&s, &SliceModel::mutedChanged);
        s.setMuted(true);   // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    void setMutedToggleRoundTrip() {
        SliceModel s;
        s.setMuted(true);
        s.setMuted(false);
        QCOMPARE(s.muted(), false);
    }

    // ── audioPan ─────────────────────────────────────────────────────────────

    void audioPanDefaultIsZero() {
        // Neutral default: 0.0 = center. Thetis default pan=0.5 (WDSP scale).
        SliceModel s;
        QCOMPARE(s.audioPan(), 0.0);
    }

    void setAudioPanStoresValue() {
        SliceModel s;
        s.setAudioPan(-0.5);
        QCOMPARE(s.audioPan(), -0.5);
    }

    void setAudioPanEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::audioPanChanged);
        s.setAudioPan(0.5);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 0.5);
    }

    void setAudioPanNoSignalOnSameValue() {
        SliceModel s;
        s.setAudioPan(0.0);   // same as default → no signal
        QSignalSpy spy(&s, &SliceModel::audioPanChanged);
        s.setAudioPan(0.0);
        QCOMPARE(spy.count(), 0);
    }

    void setAudioPanFullLeft() {
        SliceModel s;
        s.setAudioPan(-1.0);
        QCOMPARE(s.audioPan(), -1.0);
    }

    void setAudioPanFullRight() {
        SliceModel s;
        s.setAudioPan(1.0);
        QCOMPARE(s.audioPan(), 1.0);
    }

    // ── binauralEnabled ──────────────────────────────────────────────────────

    void binauralEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1145-1162 — bin_on = false (dual-mono default)
        SliceModel s;
        QCOMPARE(s.binauralEnabled(), false);
    }

    void setBinauralEnabledStoresValue() {
        SliceModel s;
        s.setBinauralEnabled(true);
        QCOMPARE(s.binauralEnabled(), true);
    }

    void setBinauralEnabledEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::binauralEnabledChanged);
        s.setBinauralEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setBinauralEnabledNoSignalOnSameValue() {
        SliceModel s;
        s.setBinauralEnabled(true);
        QSignalSpy spy(&s, &SliceModel::binauralEnabledChanged);
        s.setBinauralEnabled(true);   // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    void setBinauralEnabledToggleRoundTrip() {
        SliceModel s;
        s.setBinauralEnabled(true);
        s.setBinauralEnabled(false);
        QCOMPARE(s.binauralEnabled(), false);
    }
};

QTEST_MAIN(TestSliceAudioPanel)
#include "tst_slice_audio_panel.moc"
