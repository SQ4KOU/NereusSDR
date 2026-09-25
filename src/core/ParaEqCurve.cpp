// =================================================================
// src/core/ParaEqCurve.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/ucParametricEq.cs (the response curve,
//   JSON load, point ordering and default points) and
//   Project Files/Source/Console/eqform.cs (the TX EQ panel's widget
//   limits), original licences from Thetis source are included below.
//   Sole author of ucParametricEq.cs: Richard Samphire (MW0LGE).
//
// Implementation; declarations in ParaEqCurve.h.
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

#include "core/ParaEqCurve.h"

#include "core/ParaEqEnvelope.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <algorithm>
#include <limits>
#include <optional>

namespace NereusSDR {

namespace ParaEqCurve {

namespace {

// The widget's ordering pass without its selection bookkeeping (a curve
// here has no selected or dragged point). From Thetis
// ucParametricEq.cs:3223-3312 [v2.10.3.15], allowPointReorder true (as
// eqform.cs:930 sets it for the TX EQ panel).
void enforceOrdering(Curve& c, double minPointSpacingHz, bool enforceSpacingAll)
{
    QVector<Point>& pts = c.points;
    if (pts.isEmpty()) { return; }

    if (pts.size() > 1) {
        std::stable_sort(pts.begin(), pts.end(), [](const Point& a, const Point& b) {
            if (a.frequencyHz != b.frequencyHz) { return a.frequencyHz < b.frequencyHz; }
            return a.bandId < b.bandId;
        });
    }

    for (Point& p : pts) {
        p.frequencyHz = clamp(p.frequencyHz, c.frequencyMinHz, c.frequencyMaxHz);
    }
    if (pts.size() > 0) { pts.front().frequencyHz = c.frequencyMinHz; }
    if (pts.size() > 1) { pts.back().frequencyHz  = c.frequencyMaxHz; }

    if (!enforceSpacingAll) { return; }
    if (pts.size() < 3) { return; }

    double spacing = minPointSpacingHz;
    const double maxSpacing = (c.frequencyMaxHz - c.frequencyMinHz) / double(pts.size() - 1);
    if (spacing > maxSpacing) { spacing = maxSpacing; }
    if (spacing < 0.0)        { spacing = 0.0; }

    for (int i = 1; i < pts.size() - 1; ++i) {
        const double minF = c.frequencyMinHz + (spacing * i);
        double maxF = c.frequencyMaxHz - (spacing * (pts.size() - 1 - i));
        if (maxF < minF) { maxF = minF; }
        pts[i].frequencyHz = clamp(pts[i].frequencyHz, minF, maxF);
    }
    for (int i = 1; i < pts.size() - 1; ++i) {
        const double wantMin = pts[i - 1].frequencyHz + spacing;
        if (pts[i].frequencyHz < wantMin) { pts[i].frequencyHz = wantMin; }
    }
    for (int i = pts.size() - 2; i >= 1; --i) {
        const double wantMax = pts[i + 1].frequencyHz - spacing;
        if (pts[i].frequencyHz > wantMax) { pts[i].frequencyHz = wantMax; }
    }
    pts.front().frequencyHz = c.frequencyMinHz;
    pts.back().frequencyHz  = c.frequencyMaxHz;
}

// Flat default points across the curve's range. From Thetis
// ucParametricEq.cs:3163-3197 [v2.10.3.15] (ResetPointsDefault), with the
// TX EQ panel's gain and Q limits.
void resetPointsDefault(Curve& c, int bandCount)
{
    c.points.clear();
    int count = bandCount;
    if (count < 2) { count = 2; }

    double span = c.frequencyMaxHz - c.frequencyMinHz;
    if (span <= 0.0) { span = 1.0; }

    for (int i = 0; i < count; ++i) {
        const double t = double(i) / double(count - 1);
        c.points.append(Point{i + 1, c.frequencyMinHz + t * span, 0.0, 4.0});
    }

    enforceOrdering(c, kTxEqMinPointSpacingHz, true);
    for (Point& p : c.points) {
        p.gainDb = clamp(p.gainDb, kTxEqDbMin, kTxEqDbMax);
        p.q      = clamp(p.q, kTxEqQMin, kTxEqQMax);
    }
    c.globalGainDb = clamp(c.globalGainDb, kTxEqDbMin, kTxEqDbMax);
}

} // namespace

double txEqResponseDb(const Curve& curve, double frequencyHz)
{
    return responseDb(curve.points, curve.parametricEq, curve.frequencyMinHz,
                      curve.frequencyMaxHz, kTxEqQMin, kTxEqQMax, frequencyHz);
}

// From Thetis ucParametricEq.cs:1488-1573 [v2.10.3.15] (LoadFromJson),
// loading into the TX EQ panel's widget as TxEqDialog builds it: ten flat
// bands from 0 to 2700 Hz (eqform.cs:928-967), parametric on.
bool loadTxEqCurve(const QString& json, Curve& out)
{
    if (json.trimmed().isEmpty()) { return false; }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError) { return false; }
    if (!doc.isObject()) { return false; }

    const QJsonObject root = doc.object();
    if (!root.contains(QStringLiteral("points"))) { return false; }
    const QJsonValue ptsVal = root.value(QStringLiteral("points"));
    if (!ptsVal.isArray()) { return false; }
    const QJsonArray pts = ptsVal.toArray();
    if (pts.size() < 2) { return false; }

    int bandCount = root.value(QStringLiteral("band_count")).toInt(0);
    if (bandCount < 2) { bandCount = pts.size(); }

    if (bandCount < 2)            { return false; }
    if (bandCount > 256)          { return false; }
    if (bandCount != pts.size())  { return false; }

    const double newFreqMin = root.value(QStringLiteral("frequency_min_hz")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    const double newFreqMax = root.value(QStringLiteral("frequency_max_hz")).toDouble(
        std::numeric_limits<double>::quiet_NaN());
    if (std::isnan(newFreqMin) || std::isinf(newFreqMin)) { return false; }
    if (std::isnan(newFreqMax) || std::isinf(newFreqMax)) { return false; }
    if (newFreqMax <= newFreqMin) { return false; }

    // The panel's widget before the load.
    Curve c;
    c.parametricEq   = true;
    c.globalGainDb   = kTxEqGlobalGainDb;
    c.frequencyMinHz = kTxEqMinHz;
    c.frequencyMaxHz = kTxEqMaxHz;
    resetPointsDefault(c, kTxEqBandCount);

    if (bandCount != c.points.size()) {
        // As the widget: the defaults for a new band count are laid out
        // over the OLD range, then each point below is overwritten.
        resetPointsDefault(c, bandCount);
    }

    c.parametricEq   = root.value(QStringLiteral("parametric_eq")).toBool(false);
    c.globalGainDb   = clamp(root.value(QStringLiteral("global_gain_db")).toDouble(0.0),
                             kTxEqDbMin, kTxEqDbMax);
    c.frequencyMinHz = newFreqMin;
    c.frequencyMaxHz = newFreqMax;

    for (int i = 0; i < c.points.size(); ++i) {
        Point& p = c.points[i];
        if (i >= pts.size()) { break; }
        if (!pts.at(i).isObject()) { continue; }
        const QJsonObject jp = pts.at(i).toObject();

        const double jpFreq = jp.value(QStringLiteral("frequency_hz")).toDouble(p.frequencyHz);
        const double jpGain = jp.value(QStringLiteral("gain_db")).toDouble(p.gainDb);
        const double jpQ    = jp.value(QStringLiteral("q")).toDouble(p.q);

        // The first and last points are locked to the range's ends
        // (ucParametricEq.cs:3384-3394 [v2.10.3.15]).
        if (i == 0) {
            p.frequencyHz = c.frequencyMinHz;
        } else if (i == c.points.size() - 1) {
            p.frequencyHz = c.frequencyMaxHz;
        } else {
            p.frequencyHz = clamp(jpFreq, c.frequencyMinHz, c.frequencyMaxHz);
        }
        p.gainDb = clamp(jpGain, kTxEqDbMin, kTxEqDbMax);
        p.q      = clamp(jpQ, kTxEqQMin, kTxEqQMax);
    }

    enforceOrdering(c, kTxEqMinPointSpacingHz, true);
    out = c;
    return true;
}

bool txEqCurveFromParaEqData(const QString& paraEqData, Curve& out)
{
    if (paraEqData.isEmpty()) { return false; }
    // As TxEqDialog::syncParametricFromModel: the Thetis envelope
    // (eqform.cs:3269-3271, Common.cs:1764-1790 [v2.10.3.15]), or raw JSON
    // saved by an early build before the envelope.
    const std::optional<QString> decoded = ParaEqEnvelope::decode(paraEqData);
    if (decoded.has_value()) {
        return loadTxEqCurve(*decoded, out);
    }
    if (paraEqData.trimmed().startsWith(QLatin1Char('{'))) {
        return loadTxEqCurve(paraEqData, out);
    }
    return false;
}

// Moved from TxEqDialog::pushParametricCurveToWdsp (NereusSDR, 3M-3a-ii
// follow-up). WDSP's TX EQ takes ten bands; the panel has 5, 10 or 18.
// Ten go one to one; any other count samples the curve at ten evenly
// spaced frequencies (the Q effect survives at that resolution). Thetis
// likewise feeds SetTXAEQProfile sampled F and G arrays.
void sampleTxEqProfile(const Curve& curve, std::vector<double>& freqs,
                       std::vector<double>& gains)
{
    freqs.assign(10, 0.0);
    gains.assign(11, 0.0);
    gains[0] = curve.globalGainDb;  // preamp slot

    const int n = curve.points.size();
    if (n == 10) {
        for (int i = 0; i < 10; ++i) {
            freqs[static_cast<std::size_t>(i)]     = curve.points.at(i).frequencyHz;
            gains[static_cast<std::size_t>(i + 1)] = curve.points.at(i).gainDb;
        }
        return;
    }
    const double minHz = curve.frequencyMinHz;
    const double maxHz = curve.frequencyMaxHz;
    const double step  = (maxHz > minHz) ? (maxHz - minHz) / 9.0 : 0.0;
    for (int i = 0; i < 10; ++i) {
        const double f = minHz + step * i;
        freqs[static_cast<std::size_t>(i)]     = f;
        gains[static_cast<std::size_t>(i + 1)] = txEqResponseDb(curve, f);
    }
}

} // namespace ParaEqCurve

} // namespace NereusSDR
