// =================================================================
// tests/tst_slice_auto_agc.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//   Project Files/Source/Console/setup.designer.cs (upstream has no top-of-file header — project-level LICENSE applies)
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

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

//
// Upstream source 'Project Files/Source/Console/setup.designer.cs' has no top-of-file GPL header —
// project-level Thetis LICENSE applies.

#include <QtTest/QtTest>
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestSliceAutoAgc : public QObject {
    Q_OBJECT

private slots:
    // ── autoAgcEnabled ──────────────────────────────────────────────────────

    void autoAgcEnabledDefaultIsFalse() {
        // From Thetis console.cs:45926 — m_bAutoAGCRX1 = false
        SliceModel s;
        QCOMPARE(s.autoAgcEnabled(), false);
    }

    void setAutoAgcEnabledStoresValue() {
        SliceModel s;
        s.setAutoAgcEnabled(true);
        QCOMPARE(s.autoAgcEnabled(), true);
    }

    void setAutoAgcEnabledEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::autoAgcEnabledChanged);
        s.setAutoAgcEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setAutoAgcEnabledNoSignalOnSameValue() {
        SliceModel s;
        s.setAutoAgcEnabled(true);
        QSignalSpy spy(&s, &SliceModel::autoAgcEnabledChanged);
        s.setAutoAgcEnabled(true);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    // ── autoAgcOffset ───────────────────────────────────────────────────────

    void autoAgcOffsetDefaultIsTwenty() {
        // From Thetis setup.designer.cs:38630 — udRX1AutoAGCOffset default 20.0
        SliceModel s;
        QCOMPARE(s.autoAgcOffset(), 20.0);
    }

    void setAutoAgcOffsetStoresValue() {
        SliceModel s;
        s.setAutoAgcOffset(15.5);
        QCOMPARE(s.autoAgcOffset(), 15.5);
    }

    void setAutoAgcOffsetEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::autoAgcOffsetChanged);
        s.setAutoAgcOffset(30.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), 30.0);
    }

    void setAutoAgcOffsetNoSignalOnSameValue() {
        SliceModel s;
        s.setAutoAgcOffset(10.0);
        QSignalSpy spy(&s, &SliceModel::autoAgcOffsetChanged);
        s.setAutoAgcOffset(10.0);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    // ── agcFixedGain ────────────────────────────────────────────────────────

    void agcFixedGainDefaultIsTwenty() {
        // From Thetis setup.designer.cs:39320 — udDSPAGCFixedGaindB default 20
        SliceModel s;
        QCOMPARE(s.agcFixedGain(), 20);
    }

    void setAgcFixedGainStoresValue() {
        SliceModel s;
        s.setAgcFixedGain(50);
        QCOMPARE(s.agcFixedGain(), 50);
    }

    void setAgcFixedGainEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::agcFixedGainChanged);
        s.setAgcFixedGain(75);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 75);
    }

    void setAgcFixedGainNoSignalOnSameValue() {
        SliceModel s;
        s.setAgcFixedGain(30);
        QSignalSpy spy(&s, &SliceModel::agcFixedGainChanged);
        s.setAgcFixedGain(30);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    // ── agcHangThreshold ────────────────────────────────────────────────────

    void agcHangThresholdDefaultIsZero() {
        // From Thetis setup.designer.cs:39418 — tbDSPAGCHangThreshold default 0
        SliceModel s;
        QCOMPARE(s.agcHangThreshold(), 0);
    }

    void setAgcHangThresholdStoresValue() {
        SliceModel s;
        s.setAgcHangThreshold(50);
        QCOMPARE(s.agcHangThreshold(), 50);
    }

    void setAgcHangThresholdEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::agcHangThresholdChanged);
        s.setAgcHangThreshold(80);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 80);
    }

    void setAgcHangThresholdNoSignalOnSameValue() {
        SliceModel s;
        s.setAgcHangThreshold(25);
        QSignalSpy spy(&s, &SliceModel::agcHangThresholdChanged);
        s.setAgcHangThreshold(25);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    // ── agcMaxGain ──────────────────────────────────────────────────────────

    void agcMaxGainDefaultIsNinety() {
        // From Thetis setup.designer.cs:39245 — udDSPAGCMaxGaindB default 90
        SliceModel s;
        QCOMPARE(s.agcMaxGain(), 90);
    }

    void setAgcMaxGainStoresValue() {
        SliceModel s;
        s.setAgcMaxGain(120);
        QCOMPARE(s.agcMaxGain(), 120);
    }

    void setAgcMaxGainEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::agcMaxGainChanged);
        s.setAgcMaxGain(100);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 100);
    }

    void setAgcMaxGainNoSignalOnSameValue() {
        SliceModel s;
        s.setAgcMaxGain(60);
        QSignalSpy spy(&s, &SliceModel::agcMaxGainChanged);
        s.setAgcMaxGain(60);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }
};

QTEST_MAIN(TestSliceAutoAgc)
#include "tst_slice_auto_agc.moc"
