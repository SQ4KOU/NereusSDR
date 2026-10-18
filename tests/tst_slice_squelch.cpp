// =================================================================
// tests/tst_slice_squelch.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/radio.cs, original licence from Thetis source is included below
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//   2026-10-14 - FM squelch threshold default is Thetis's 0, from Thetis
//                 console.cs:171 [v2.10.3.15], by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-10-15 - Squelch persistence tests (Thetis console.cs:2830-2852
//                 save, 4164-4193 load [v2.10.3.15]). J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-16 - The persistence tests expect the FM threshold default of 0
//                 (Thetis console.cs:171 [v2.10.3.15]) where nothing usable
//                 is saved. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// radio.cs
//=================================================================
// PowerSDR is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
// Copyright (C) 2019-2026  Richard Samphire
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
// ApacheLabs G2E support added throughout Thetis in various files, all changes marked  //N1GP G2E added
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
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Final modifictions by MW0LGE Richard Samphire - 19th April 2026
// Nothing further added by him after this date, and his repo is now in archive https://github.com/ramdor/Thetis
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#include <QtTest/QtTest>
#include "core/AppSettings.h"
#include "models/Band.h"
#include "models/SliceModel.h"
#include "core/RxChannel.h"

using namespace NereusSDR;

class TestSliceSquelch : public QObject {
    Q_OBJECT

private:
    // The six saved squelch keys, as SliceModel::saveToSettings writes them.
    static QStringList squelchKeys() {
        return {QStringLiteral("SsqlEnabled"), QStringLiteral("SsqlThresh"),
                QStringLiteral("AmsqEnabled"), QStringLiteral("AmsqThresh"),
                QStringLiteral("FmsqEnabled"), QStringLiteral("FmsqThresh")};
    }

    static void resetSquelchKeys() {
        auto& s = AppSettings::instance();
        for (int slice = 0; slice < 4; ++slice) {
            for (const QString& field : squelchKeys()) {
                s.remove(QStringLiteral("Slice%1/").arg(slice) + field);
            }
        }
    }

private slots:
    void init()    { resetSquelchKeys(); }
    void cleanup() { resetSquelchKeys(); }

    // ── Persistence (Thetis console.cs:2830-2852 save, 4164-4193 load) ───────

    void squelchRoundTripsThroughSettings() {
        {
            SliceModel a(0);
            a.setSsqlEnabled(true);
            a.setSsqlThresh(42.0);
            a.setAmsqEnabled(true);
            a.setAmsqThresh(-77.0);
            a.setFmsqEnabled(true);
            a.setFmsqThresh(-33.0);
            a.saveToSettings(Band::Band40m);
        }
        auto& s = AppSettings::instance();
        QCOMPARE(s.value(QStringLiteral("Slice0/SsqlEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice0/AmsqEnabled")).toString(),
                 QStringLiteral("True"));
        QCOMPARE(s.value(QStringLiteral("Slice0/FmsqEnabled")).toString(),
                 QStringLiteral("True"));

        SliceModel b(0);
        b.restoreFromSettings(Band::Band40m);
        QCOMPARE(b.ssqlEnabled(), true);
        QCOMPARE(b.ssqlThresh(), 42.0);
        QCOMPARE(b.amsqEnabled(), true);
        QCOMPARE(b.amsqThresh(), -77.0);
        QCOMPARE(b.fmsqEnabled(), true);
        QCOMPARE(b.fmsqThresh(), -33.0);
    }

    void squelchIsPerSliceNotPerBand() {
        // Thetis saves squelch per receiver, not per band: a value saved on
        // one band comes back on another.
        {
            SliceModel a(0);
            a.setFmsqEnabled(true);
            a.setFmsqThresh(-20.0);
            a.saveToSettings(Band::Band40m);
        }
        SliceModel b(0);
        b.restoreFromSettings(Band::Band20m);
        QCOMPARE(b.fmsqEnabled(), true);
        QCOMPARE(b.fmsqThresh(), -20.0);
    }

    void squelchLoadSetsThresholdBeforeEnable() {
        // A saved enable must not reach WDSP ahead of its saved threshold.
        {
            SliceModel a(0);
            a.setSsqlEnabled(true);
            a.setSsqlThresh(50.0);
            a.setAmsqEnabled(true);
            a.setAmsqThresh(-90.0);
            a.setFmsqEnabled(true);
            a.setFmsqThresh(-60.0);
            a.saveToSettings(Band::Band40m);
        }
        SliceModel b(0);
        QStringList order;
        connect(&b, &SliceModel::ssqlThreshChanged, this, [&] { order << QStringLiteral("ssqlThresh"); });
        connect(&b, &SliceModel::ssqlEnabledChanged, this, [&] { order << QStringLiteral("ssqlEnabled"); });
        connect(&b, &SliceModel::amsqThreshChanged, this, [&] { order << QStringLiteral("amsqThresh"); });
        connect(&b, &SliceModel::amsqEnabledChanged, this, [&] { order << QStringLiteral("amsqEnabled"); });
        connect(&b, &SliceModel::fmsqThreshChanged, this, [&] { order << QStringLiteral("fmsqThresh"); });
        connect(&b, &SliceModel::fmsqEnabledChanged, this, [&] { order << QStringLiteral("fmsqEnabled"); });
        b.restoreFromSettings(Band::Band40m);
        for (const char* name : {"ssql", "amsq", "fmsq"}) {
            const int thresh = order.indexOf(QLatin1String(name) + QStringLiteral("Thresh"));
            const int enabled = order.indexOf(QLatin1String(name) + QStringLiteral("Enabled"));
            QVERIFY2(thresh >= 0 && enabled >= 0, qPrintable(order.join(QLatin1Char(','))));
            QVERIFY2(thresh < enabled, qPrintable(order.join(QLatin1Char(','))));
        }
    }

    void squelchLoadClampsThresholdsToTheirRanges_data() {
        QTest::addColumn<QString>("field");
        QTest::addColumn<QString>("stored");
        QTest::addColumn<double>("expected");
        // ControlRanges.h: SSQL 0..100 slider units, AM and FM -160..0.
        QTest::newRow("ssql above") << "SsqlThresh" << "250" << 100.0;
        QTest::newRow("ssql below") << "SsqlThresh" << "-5" << 0.0;
        QTest::newRow("am below") << "AmsqThresh" << "-999" << -160.0;
        QTest::newRow("am above") << "AmsqThresh" << "12" << 0.0;
        QTest::newRow("fm above") << "FmsqThresh" << "500" << 0.0;
        QTest::newRow("fm below") << "FmsqThresh" << "-400" << -160.0;
        // Not a number: the value is skipped and the default stays. For FM
        // that is Thetis's 0 (console.cs:171 [v2.10.3.15]), the tightest
        // threshold.
        QTest::newRow("fm garbage") << "FmsqThresh" << "garbage" << 0.0;
        QTest::newRow("fm nan") << "FmsqThresh" << "nan" << 0.0;
        QTest::newRow("ssql garbage") << "SsqlThresh" << "x" << 16.0;
    }

    void squelchLoadClampsThresholdsToTheirRanges() {
        QFETCH(QString, field);
        QFETCH(QString, stored);
        QFETCH(double, expected);
        AppSettings::instance().setValue(QStringLiteral("Slice0/") + field, stored);
        SliceModel b(0);
        b.restoreFromSettings(Band::Band40m);
        const double actual = field == QLatin1String("SsqlThresh") ? b.ssqlThresh()
            : field == QLatin1String("AmsqThresh") ? b.amsqThresh() : b.fmsqThresh();
        QCOMPARE(actual, expected);
    }

    void squelchLoadWithNoSavedKeysKeepsDefaults() {
        SliceModel b(0);
        b.restoreFromSettings(Band::Band40m);
        QCOMPARE(b.ssqlEnabled(), false);
        QCOMPARE(b.ssqlThresh(), 16.0);
        QCOMPARE(b.amsqEnabled(), false);
        QCOMPARE(b.amsqThresh(), -150.0);
        QCOMPARE(b.fmsqEnabled(), false);
        QCOMPARE(b.fmsqThresh(), 0.0);
    }

    // ── ssqlEnabled ──────────────────────────────────────────────────────────

    void ssqlEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1185 — _bSSqlOn default = false
        // Upstream inline attribution preserved verbatim (radio.cs:1183):
        //   // MW0LGE [2.9.0.8]
        //   // Voice Squeltch - SSQL from 1.21 WDSP
        SliceModel s;
        QCOMPARE(s.ssqlEnabled(), false);
    }

    void setSsqlEnabledStoresValue() {
        SliceModel s;
        s.setSsqlEnabled(true);
        QCOMPARE(s.ssqlEnabled(), true);
    }

    void setSsqlEnabledEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::ssqlEnabledChanged);
        s.setSsqlEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setSsqlEnabledNoSignalOnSameValue() {
        SliceModel s;
        s.setSsqlEnabled(true);
        QSignalSpy spy(&s, &SliceModel::ssqlEnabledChanged);
        s.setSsqlEnabled(true);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    void setSsqlEnabledToggleRoundTrip() {
        SliceModel s;
        s.setSsqlEnabled(true);
        s.setSsqlEnabled(false);
        QCOMPARE(s.ssqlEnabled(), false);
    }

    // ── ssqlThresh ───────────────────────────────────────────────────────────

    void ssqlThreshDefaultValue() {
        // From Thetis radio.cs:1187 — _fSSqlThreshold = 0.16f (0..1 linear)
        // Model stores slider units 0–100; 16 maps to 0.16 at WDSP boundary.
        // Upstream inline attribution preserved verbatim (radio.cs:1183):
        //   // MW0LGE [2.9.0.8]
        //   // Voice Squeltch - SSQL from 1.21 WDSP
        SliceModel s;
        QCOMPARE(s.ssqlThresh(), 16.0);
    }

    void setSsqlThreshStoresValue() {
        SliceModel s;
        s.setSsqlThresh(-80.0);
        QCOMPARE(s.ssqlThresh(), -80.0);
    }

    void setSsqlThreshEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::ssqlThreshChanged);
        s.setSsqlThresh(-90.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), -90.0);
    }

    // ── amsqEnabled ──────────────────────────────────────────────────────────

    void amsqEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1293 — rx_am_squelch_on default = false
        SliceModel s;
        QCOMPARE(s.amsqEnabled(), false);
    }

    void setAmsqEnabledStoresValue() {
        SliceModel s;
        s.setAmsqEnabled(true);
        QCOMPARE(s.amsqEnabled(), true);
    }

    void setAmsqEnabledEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::amsqEnabledChanged);
        s.setAmsqEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setAmsqEnabledNoSignalOnSameValue() {
        SliceModel s;
        s.setAmsqEnabled(true);
        QSignalSpy spy(&s, &SliceModel::amsqEnabledChanged);
        s.setAmsqEnabled(true);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    // ── amsqThresh ───────────────────────────────────────────────────────────

    void amsqThreshDefaultValue() {
        // From Thetis radio.cs:1164 — rx_squelch_threshold = -150.0f dB
        SliceModel s;
        QCOMPARE(s.amsqThresh(), -150.0);
    }

    void setAmsqThreshStoresValue() {
        SliceModel s;
        s.setAmsqThresh(-60.0);
        QCOMPARE(s.amsqThresh(), -60.0);
    }

    void setAmsqThreshEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::amsqThreshChanged);
        s.setAmsqThresh(-70.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), -70.0);
    }

    // ── fmsqEnabled ──────────────────────────────────────────────────────────

    void fmsqEnabledDefaultIsFalse() {
        // From Thetis radio.cs:1312 — rx_fm_squelch_on default = false
        SliceModel s;
        QCOMPARE(s.fmsqEnabled(), false);
    }

    void setFmsqEnabledStoresValue() {
        SliceModel s;
        s.setFmsqEnabled(true);
        QCOMPARE(s.fmsqEnabled(), true);
    }

    void setFmsqEnabledEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::fmsqEnabledChanged);
        s.setFmsqEnabled(true);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toBool(), true);
    }

    void setFmsqEnabledNoSignalOnSameValue() {
        SliceModel s;
        s.setFmsqEnabled(true);
        QSignalSpy spy(&s, &SliceModel::fmsqEnabledChanged);
        s.setFmsqEnabled(true);  // same value — no signal
        QCOMPARE(spy.count(), 0);
    }

    void setFmsqEnabledToggleRoundTrip() {
        SliceModel s;
        s.setFmsqEnabled(true);
        s.setFmsqEnabled(false);
        QCOMPARE(s.fmsqEnabled(), false);
    }

    // ── fmsqThresh ───────────────────────────────────────────────────────────

    void fmsqThreshDefaultValue() {
        // From Thetis console.cs:171 [v2.10.3.15]
        //   private int rx1_fm_squelch_threshold_scroll = 0;
        // and console.Designer.cs:7578 (ptbSquelch.Value = 0): a -160..0
        // squelch slider position.
        SliceModel s;
        QCOMPARE(s.fmsqThresh(), 0.0);
    }

    void fmsqThreshDefaultSendsThetisThreshold() {
        // A slice with no FM threshold set sends what Thetis sends for its
        // slider at 0: n = 100, (float)Math.Pow(10.0, -2.0)
        // (console.cs:47295-47304).
        SliceModel s;
        QCOMPARE(RxChannel::fmsqThresholdForWdsp(s.fmsqThresh()),
                 static_cast<double>(0.01f));
    }

    void fmsqThreshOldDefaultIsKept() {
        // No migration: a slice given the old NereusSDR default, -150, keeps
        // it and sends its threshold (n = 6), not the new default's.
        SliceModel s;
        s.setFmsqThresh(-150.0);
        QCOMPARE(s.fmsqThresh(), -150.0);
        QCOMPARE(RxChannel::fmsqThresholdForWdsp(s.fmsqThresh()),
                 static_cast<double>(static_cast<float>(0.7585775750291838)));
    }

    void setFmsqThreshStoresValue() {
        SliceModel s;
        s.setFmsqThresh(-40.0);
        QCOMPARE(s.fmsqThresh(), -40.0);
    }

    void setFmsqThreshEmitsSignal() {
        SliceModel s;
        QSignalSpy spy(&s, &SliceModel::fmsqThreshChanged);
        s.setFmsqThresh(-50.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toDouble(), -50.0);
    }
};

QTEST_MAIN(TestSliceSquelch)
#include "tst_slice_squelch.moc"
