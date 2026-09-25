// =================================================================
// src/core/ParaEqCurve.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/ucParametricEq.cs (the response curve,
//   JSON load, point ordering and default points) and
//   Project Files/Source/Console/eqform.cs (the TX EQ panel's widget
//   limits), original licences from Thetis source are included below.
//   Sole author of ucParametricEq.cs: Richard Samphire (MW0LGE).
//
// Declarations; the implementation is in ParaEqCurve.cpp.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25 - R-R3-49 (parity Task 4): the TX EQ parametric curve
//                 moved out of the GUI (ParametricEqWidget's response
//                 curve, TxEqDialog's sampling onto the TX channel's ten
//                 bands) into src/core, so the Core applies the curve
//                 saved in txEqParaEqData to its own TX channel. Same
//                 numbers as the dialog (tst_para_eq_curve).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
// =================================================================

// --- From ucParametricEq.cs ---
/*  ucParametricEq.cs

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

// --- From eqform.cs ---
//=================================================================
// eqform.cs
//=================================================================
// PowerSDR is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems
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

#pragma once

#include <QString>
#include <QVector>

#include <cmath>
#include <vector>

namespace NereusSDR {

namespace ParaEqCurve {

// One band of a parametric curve (ucParametricEq EqPoint without its
// display colour).
struct Point {
    int    bandId      = 0;
    double frequencyHz = 0.0;
    double gainDb      = 0.0;
    double q           = 4.0;
};

// A parametric curve as the TX EQ panel holds it.
struct Curve {
    QVector<Point> points;
    bool   parametricEq   = true;
    double globalGainDb   = 0.0;
    double frequencyMinHz = 0.0;
    double frequencyMaxHz = 2700.0;
};

// The TX EQ panel's widget limits. From Thetis eqform.cs:946-970
// [v2.10.3.15] (ucParametricEq1's property block); TxEqDialog applies
// the same values to its widget.
inline constexpr double kTxEqDbMin               = -24.0;   // cs:960
inline constexpr double kTxEqDbMax               =  24.0;   // cs:959
inline constexpr double kTxEqMinHz               =   0.0;   // cs:963
inline constexpr double kTxEqMaxHz               = 2700.0;  // cs:962
inline constexpr double kTxEqQMin                =   0.2;   // cs:970
inline constexpr double kTxEqQMax                =  20.0;   // cs:969
inline constexpr double kTxEqGlobalGainDb        =   0.0;   // cs:964
inline constexpr double kTxEqMinPointSpacingHz   =   5.0;   // cs:966
inline constexpr int    kTxEqBandCount           = 10;      // TxEqDialog default

// From Thetis ucParametricEq.cs:2983-2988 [v2.10.3.15].
inline double clamp(double v, double lo, double hi)
{
    if (v < lo) { return lo; }
    if (v > hi) { return hi; }
    return v;
}

// The response curve in dB at `frequencyHz`. From Thetis
// ucParametricEq.cs:2694-2748 [v2.10.3.15]. Two branches:
//   - graphic EQ (!parametricEq): straight lines between adjacent points,
//     clamped to the first and last gain at the edges;
//   - parametric: a Gaussian per point, FWHM = span / (q*3) (at least
//     span/6000), sigma = FWHM / 2.3548200450309493, summed unweighted.
// A template so ParametricEqWidget's own points (EqPoint) and Point both
// use it without a copy per call.
template <typename PointList>
double responseDb(const PointList& points, bool parametricEq,
                  double frequencyMinHz, double frequencyMaxHz,
                  double qMin, double qMax, double frequencyHz)
{
    if (!parametricEq) {
        if (points.isEmpty()) { return 0.0; }
        const double f = frequencyHz;
        if (f <= points.first().frequencyHz) { return points.first().gainDb; }
        if (f >= points.last().frequencyHz)  { return points.last().gainDb; }

        for (int i = 1; i < points.size(); ++i) {
            const auto& left  = points.at(i - 1);
            const auto& right = points.at(i);
            if (f <= right.frequencyHz) {
                const double denom = right.frequencyHz - left.frequencyHz;
                if (denom <= 0.0000001) { return right.gainDb; }
                double t = (f - left.frequencyHz) / denom;
                if (t < 0.0) { t = 0.0; }
                if (t > 1.0) { t = 1.0; }
                return left.gainDb + ((right.gainDb - left.gainDb) * t);
            }
        }
        return points.last().gainDb;
    }

    double span = frequencyMaxHz - frequencyMinHz;
    if (span <= 0.0) { span = 1.0; }

    double sum = 0.0;
    for (const auto& p : points) {
        const double q = clamp(p.q, qMin, qMax);
        double fwhm = span / (q * 3.0);
        const double minFwhm = span / 6000.0;
        if (fwhm < minFwhm) { fwhm = minFwhm; }
        const double sigma = fwhm / 2.3548200450309493;
        const double d = (frequencyHz - p.frequencyHz) / sigma;
        const double w = std::exp(-0.5 * d * d);
        sum += p.gainDb * w;
    }
    return sum;
}

/// The TX EQ panel's curve before anything is loaded: ten flat bands
/// from 0 to 2700 Hz, parametric (what TxEqDialog's widget starts with).
Curve defaultTxEqCurve();

/// A curve's response with the TX EQ panel's Q limits.
double txEqResponseDb(const Curve& curve, double frequencyHz);

/// The TX EQ panel's curve after loading `json` (ucParametricEq
/// LoadFromJson into the panel's widget as TxEqDialog builds it: ten
/// flat bands from 0 to 2700 Hz). False, leaving `out` alone, when the
/// JSON is not a curve the widget would load.
bool loadTxEqCurve(const QString& json, Curve& out);

/// The TX EQ panel's curve from a saved txEqParaEqData value (the
/// ParaEqEnvelope blob, or raw JSON from an early build). False when the
/// value holds no curve.
bool txEqCurveFromParaEqData(const QString& paraEqData, Curve& out);

/// The curve on the TX channel's ten EQ bands, as SetTXAEQProfile takes
/// it: `freqs` ten centres in Hz, `gains` the preamp then ten band gains
/// in dB. Ten bands go one to one; any other count is sampled at ten
/// evenly spaced frequencies across the curve's range.
void sampleTxEqProfile(const Curve& curve, std::vector<double>& freqs,
                       std::vector<double>& gains);

} // namespace ParaEqCurve

} // namespace NereusSDR
