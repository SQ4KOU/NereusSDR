// =================================================================
// src/core/ParaEqCurve.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/ucParametricEq.cs (the response curve,
//   PointsFromJson, GetDefaults and enforceOrdering) and
//   Project Files/Source/Console/eqform.cs (the TX EQ panel's widget
//   limits, ParaEQTXData's setter, sendTXDspUpdate and setTXEQProfile),
//   original licences from Thetis source are included below.
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
//   2026-09-25 - R-R3-49 (group A fix wave): the TX EQ reaches WDSP as
//                 Thetis sends it. The Core decodes a saved curve as
//                 Thetis's transmit path does (PointsFromJson, GetDefaults
//                 for a blank or broken value) and builds the arrays of
//                 sendTXDspUpdate (every point's F and G, Q when the panel
//                 uses Q factors) and of setTXEQProfile for the legacy EQ.
//                 The ten-point sampling, the widget-load port and the
//                 point ordering it needed are gone. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-09-28 - R-IOS-13 / R-R3-49: the panel's point ordering
//                 (ucParametricEq enforceOrdering with the TX panel's
//                 reorder and 5 Hz spacing) and txEqCurveJson, the
//                 NereusSDR-owned, read-only form of the saved curve the
//                 Core sends as transmit.txEqCurve (station link document,
//                 "The TX EQ curve"). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-09-28 - R-IOS-13 / R-R3-49 (follow-up): readCurveJson, the
//                 deserialise-and-check block PointsFromJson and
//                 LoadFromJson share, so the Core and ParametricEqWidget
//                 read a saved curve by one parser. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
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
#include <cmath>
#include <optional>

namespace NereusSDR {

namespace ParaEqCurve {

namespace {

// .NET Math.Round(value, digits): scale, round half to even, scale back.
double roundDigits(double value, int digits)
{
    const double power10 = std::pow(10.0, digits);
    return std::nearbyint(value * power10) / power10;
}

// A JSON number, or 0 where Json.NET would leave the field at its default.
double jsonDouble(const QJsonObject& o, const char* key)
{
    return o.value(QLatin1String(key)).toDouble(0.0);
}

} // namespace

// From Thetis ucParametricEq.cs:1403-1428 and 1490-1515 [v2.10.3.15]: the
// deserialise-and-check block PointsFromJson and LoadFromJson share, word
// for word. JsonConvert.DeserializeObject<EqJsonState> (cs:220-252) leaves
// a missing field at its C# default: band_count 0, parametric_eq false,
// global_gain_db, frequency_min_hz, frequency_max_hz, and each point's
// frequency_hz, gain_db and q 0. A document or point that is not a JSON
// object fails, as the deserialiser throws.
bool readCurveJson(const QString& json, CurveJson& out)
{
    //   if (string.IsNullOrWhiteSpace(json)) return false;
    if (json.trimmed().isEmpty()) { return false; }

    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) { return false; }
    const QJsonObject state = doc.object();

    //   if (state.Points == null) return false;
    //   if (state.Points.Count < 2) return false;
    const QJsonValue pointsValue = state.value(QStringLiteral("points"));
    if (!pointsValue.isArray()) { return false; }
    const QJsonArray points = pointsValue.toArray();
    if (points.size() < 2) { return false; }

    //   if (state.BandCount < 2) state.BandCount = state.Points.Count;
    int bandCount = state.value(QStringLiteral("band_count")).toInt(0);
    if (bandCount < 2) { bandCount = points.size(); }

    if (bandCount < 2) { return false; }
    if (bandCount > 256) { return false; }
    if (bandCount != points.size()) { return false; }
    const double stateMinHz = jsonDouble(state, "frequency_min_hz");
    const double stateMaxHz = jsonDouble(state, "frequency_max_hz");
    if (std::isnan(stateMinHz) || std::isinf(stateMinHz)) { return false; }
    if (std::isnan(stateMaxHz) || std::isinf(stateMaxHz)) { return false; }
    if (stateMaxHz <= stateMinHz) { return false; }

    CurveJson r;
    r.bandCount      = bandCount;
    r.parametricEq   = state.value(QStringLiteral("parametric_eq")).toBool(false);
    r.globalGainDb   = jsonDouble(state, "global_gain_db");
    r.frequencyMinHz = stateMinHz;
    r.frequencyMaxHz = stateMaxHz;
    r.f.reserve(static_cast<std::size_t>(points.size()));
    r.g.reserve(static_cast<std::size_t>(points.size()));
    r.q.reserve(static_cast<std::size_t>(points.size()));
    for (const QJsonValue& value : points) {
        if (!value.isObject()) { return false; }
        const QJsonObject jp = value.toObject();
        r.f.push_back(jsonDouble(jp, "frequency_hz"));
        r.g.push_back(jsonDouble(jp, "gain_db"));
        r.q.push_back(jsonDouble(jp, "q"));
    }
    out = std::move(r);
    return true;
}

// From Thetis ucParametricEq.cs:1392-1452 [v2.10.3.15] (PointsFromJson),
// with the TX panel's _db_min/_db_max/_q_min/_q_max (eqform.cs:959-970).
bool pointsFromJson(const QString& json, TxEqPoints& out)
{
    CurveJson state;
    if (!readCurveJson(json, state)) { return false; }

    const int pointCount = static_cast<int>(state.f.size());
    TxEqPoints r;
    r.f.resize(static_cast<std::size_t>(pointCount));
    r.g.resize(static_cast<std::size_t>(pointCount));
    r.q.resize(static_cast<std::size_t>(pointCount));

    r.parametricEq = state.parametricEq;
    r.preampDb     = roundDigits(clamp(state.globalGainDb, kTxEqDbMin, kTxEqDbMax), 1);
    r.minHz        = roundDigits(state.frequencyMinHz, 3);
    r.maxHz        = roundDigits(state.frequencyMaxHz, 3);
    r.bandCount    = state.bandCount;

    for (int i = 0; i < pointCount; ++i) {
        const auto k = static_cast<std::size_t>(i);
        double pointFrequencyHz = clamp(state.f[k], state.frequencyMinHz, state.frequencyMaxHz);
        const double gainDb = clamp(state.g[k], kTxEqDbMin, kTxEqDbMax);
        const double q = clamp(state.q[k], kTxEqQMin, kTxEqQMax);

        if (i == 0) { pointFrequencyHz = state.frequencyMinHz; }
        if (i == pointCount - 1) { pointFrequencyHz = state.frequencyMaxHz; }

        r.f[k] = roundDigits(pointFrequencyHz, 3);
        r.g[k] = roundDigits(gainDb, 1);
        r.q[k] = roundDigits(q, 2);
    }

    out = std::move(r);
    return true;
}

// From Thetis ucParametricEq.cs:1107-1131 [v2.10.3.15] (GetDefaults), with
// its default arguments (10 bands, 0 to 4000 Hz).
TxEqPoints defaultTxEqPoints()
{
    TxEqPoints r;
    r.bandCount    = kTxEqDefaultBandCount;
    r.preampDb     = 0.0;
    r.minHz        = kTxEqDefaultMinHz;
    r.maxHz        = kTxEqDefaultMaxHz;
    r.parametricEq = true;

    int count = r.bandCount;
    if (count < 2) { count = 2; }

    r.f.resize(static_cast<std::size_t>(count));
    r.g.resize(static_cast<std::size_t>(count));
    r.q.resize(static_cast<std::size_t>(count));

    double span = r.maxHz - r.minHz;
    if (span <= 0.0) { span = 1.0; }

    for (int i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(count - 1);
        const auto k = static_cast<std::size_t>(i);
        r.f[k] = r.minHz + (t * span);
        r.g[k] = 0.0;
        r.q[k] = kTxEqDefaultQ;
    }
    return r;
}

bool loadTxEqPoints(const QString& paraEqData, TxEqPoints& out)
{
    if (paraEqData.isEmpty()) { return false; }
    // eqform.cs:3287 [v2.10.3.15]: string json = Common.Decompress_gzip(value);
    // (Common.cs:1764-1790). NereusSDR also reads raw JSON saved by an early
    // build before the envelope.
    const std::optional<QString> decoded = ParaEqEnvelope::decode(paraEqData);
    if (decoded.has_value()) {
        return pointsFromJson(*decoded, out);
    }
    if (paraEqData.trimmed().startsWith(QLatin1Char('{'))) {
        return pointsFromJson(paraEqData, out);
    }
    return false;
}

// From Thetis eqform.cs:3276-3320 [v2.10.3.15] (ParaEQTXData's setter):
// PointsFromJson, or GetDefaults when it fails.
TxEqPoints txEqPointsFromParaEqData(const QString& paraEqData)
{
    TxEqPoints points;
    if (!loadTxEqPoints(paraEqData, points)) {
        points = defaultTxEqPoints();
    }
    return points;
}

// From Thetis ucParametricEq.cs:3223-3312 [v2.10.3.15] (enforceOrdering,
// enforce_spacing_all true), with the TX panel's _allow_point_reorder and
// _min_point_spacing_hz (eqform.cs:946, 966). The panel's BandId is the
// point's saved position here, so a tie in frequency keeps the saved order.
TxEqPoints txEqDisplayPoints(const TxEqPoints& points)
{
    TxEqPoints r = points;
    const std::size_t count = std::min({r.f.size(), r.g.size(), r.q.size()});
    //   if (_points.Count == 0) return;
    if (count == 0) { return r; }

    struct Point { double f; double g; double q; std::size_t bandId; };
    std::vector<Point> p;
    p.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        p.push_back(Point{r.f[i], r.g[i], r.q[i], i});
    }

    //   if (_allow_point_reorder && _points.Count > 1) _points.Sort(...)
    //     by FrequencyHz, then BandId
    if (p.size() > 1) {
        std::sort(p.begin(), p.end(), [](const Point& a, const Point& b) {
            if (a.f != b.f) { return a.f < b.f; }
            return a.bandId < b.bandId;
        });
    }

    const double minHz = r.minHz;
    const double maxHz = r.maxHz;
    for (Point& pt : p) {
        pt.f = clamp(pt.f, minHz, maxHz);
    }
    if (!p.empty()) { p.front().f = minHz; }
    if (p.size() > 1) { p.back().f = maxHz; }

    //   if (!enforce_spacing_all) return;   (the panel always passes true)
    if (p.size() >= 3) {
        const int n = static_cast<int>(p.size());
        double spacing = kTxEqMinPointSpacingHz;
        const double maxSpacing = (maxHz - minHz) / static_cast<double>(n - 1);
        if (spacing > maxSpacing) { spacing = maxSpacing; }
        if (spacing < 0.0) { spacing = 0.0; }

        for (int i = 1; i < n - 1; ++i) {
            const double minF = minHz + (spacing * i);
            double maxF = maxHz - (spacing * (n - 1 - i));
            if (maxF < minF) { maxF = minF; }
            p[static_cast<std::size_t>(i)].f = clamp(p[static_cast<std::size_t>(i)].f, minF, maxF);
        }
        for (int i = 1; i < n - 1; ++i) {
            const double wantMin = p[static_cast<std::size_t>(i - 1)].f + spacing;
            if (p[static_cast<std::size_t>(i)].f < wantMin) { p[static_cast<std::size_t>(i)].f = wantMin; }
        }
        for (int i = n - 2; i >= 1; --i) {
            const double wantMax = p[static_cast<std::size_t>(i + 1)].f - spacing;
            if (p[static_cast<std::size_t>(i)].f > wantMax) { p[static_cast<std::size_t>(i)].f = wantMax; }
        }
        p.front().f = minHz;
        p.back().f = maxHz;
    }

    r.f.resize(p.size());
    r.g.resize(p.size());
    r.q.resize(p.size());
    for (std::size_t i = 0; i < p.size(); ++i) {
        r.f[i] = p[i].f;
        r.g[i] = p[i].g;
        r.q[i] = p[i].q;
    }
    return r;
}

// NereusSDR-original: the read-only curve on the link (R-IOS-13, R-R3-49).
// The points are what the TX EQ panel draws for this value: the saved
// curve read as Thetis's transmit path reads it (loadTxEqPoints), or
// GetDefaults' flat curve for an empty value, then ordered as the panel
// orders them. A value that is not empty and holds no curve Thetis would
// load says so rather than showing the flat curve the Core falls back to.
QString txEqCurveJson(const QString& paraEqData)
{
    QJsonObject root;
    TxEqPoints points;
    if (paraEqData.isEmpty()) {
        points = defaultTxEqPoints();
        root.insert(QStringLiteral("state"), QStringLiteral("default"));
    } else if (loadTxEqPoints(paraEqData, points)) {
        root.insert(QStringLiteral("state"), QStringLiteral("saved"));
    } else {
        root.insert(QStringLiteral("state"), QStringLiteral("unavailable"));
        return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }

    const TxEqPoints shown = txEqDisplayPoints(points);
    root.insert(QStringLiteral("parametric"), shown.parametricEq);
    root.insert(QStringLiteral("preampDb"), shown.preampDb);
    root.insert(QStringLiteral("minHz"), shown.minHz);
    root.insert(QStringLiteral("maxHz"), shown.maxHz);
    QJsonArray list;
    for (std::size_t i = 0; i < shown.f.size(); ++i) {
        list.append(QJsonObject{{QStringLiteral("frequencyHz"), shown.f[i]},
                                {QStringLiteral("gainDb"), shown.g[i]},
                                {QStringLiteral("q"), shown.q[i]}});
    }
    root.insert(QStringLiteral("points"), list);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

// From Thetis eqform.cs:3041-3072 [v2.10.3.15] (sendTXDspUpdate):
//   int nfreqs = _tempTX_BandCount;
//   F[0] = 0.0; G[0] = _state.TX_Preamp; Q[0] = 0.0;
//   for (int n = 0; n < nfreqs; n++) { F[n + 1] = _tempTX_F[n]; ... }
//   WDSP.SetTXAEQProfile(WDSP.id(1, 0), nfreqs, Fptr, Gptr,
//                        _state.TX_ParametricEQ ? Qptr : null);
TxEqProfile txEqProfileFromPoints(const TxEqPoints& points)
{
    const int nfreqs = points.bandCount;
    TxEqProfile p;
    p.f.assign(static_cast<std::size_t>(nfreqs + 1), 0.0);
    p.g.assign(static_cast<std::size_t>(nfreqs + 1), 0.0);
    std::vector<double> q(static_cast<std::size_t>(nfreqs + 1), 0.0);

    p.f[0] = 0.0;
    p.g[0] = points.preampDb;
    q[0]   = 0.0;

    for (int n = 0; n < nfreqs; ++n) {
        const auto k = static_cast<std::size_t>(n);
        p.f[k + 1] = k < points.f.size() ? points.f[k] : 0.0;
        p.g[k + 1] = k < points.g.size() ? points.g[k] : 0.0;
        q[k + 1]   = k < points.q.size() ? points.q[k] : 0.0;
    }

    if (points.parametricEq) {
        p.q = std::move(q);
    }
    return p;
}

// From Thetis eqform.cs:2777-2816 [v2.10.3.15] (setTXEQProfile):
//   const int nfreqs = 10;
//   F[0] = 0.0; F[1] = (double)udTXEQ0.Value; ... F[10] = udTXEQ9
//   G[0] = (double)tbTXEQPre.Value; G[1] = tbTXEQ0 ... G[10] = tbTXEQ9
//   WDSP.SetTXAEQProfile(WDSP.id(1, 0), nfreqs, Fptr, Gptr, null);
TxEqProfile legacyTxEqProfile(int preampDb, const std::array<int, 10>& bandGainsDb,
                              const std::array<int, 10>& bandFreqsHz)
{
    constexpr int nfreqs = 10;
    TxEqProfile p;
    p.f.assign(nfreqs + 1, 0.0);
    p.g.assign(nfreqs + 1, 0.0);
    p.f[0] = 0.0;
    p.g[0] = static_cast<double>(preampDb);
    for (int i = 0; i < nfreqs; ++i) {
        const auto k = static_cast<std::size_t>(i);
        p.f[k + 1] = static_cast<double>(bandFreqsHz[k]);
        p.g[k + 1] = static_cast<double>(bandGainsDb[k]);
    }
    return p;
}

} // namespace ParaEqCurve

} // namespace NereusSDR
