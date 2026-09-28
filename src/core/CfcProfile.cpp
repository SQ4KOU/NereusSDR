// CFC paired-profile codec (NereusSDR).
// Ported from Thetis frmCFCConfig.cs:333-392,492-557 and
// ucParametricEq.cs:1353-1452 [v2.10.3.15].
// Modification history (NereusSDR): 2026-09-27 J.J. Boyd (KG4VCF),
// AI-assisted via OpenAI Codex: bounded Core codec for the existing
// CFCParaEQData format.
/*  frmCFCConfig.cs

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

#include "core/CfcProfile.h"
#include "core/ParaEqEnvelope.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace NereusSDR::CfcProfile {
namespace {
constexpr qsizetype kMaxEncodedChars = 16 * 1024;
constexpr qsizetype kMaxDecodedBytes = 64 * 1024;

bool number(const QJsonObject& o, const char* key, double lo, double hi, double& out)
{
    const QJsonValue v = o.value(QLatin1String(key));
    if (!v.isDouble()) { return false; }
    const double n = v.toDouble();
    if (!std::isfinite(n) || n < lo || n > hi) { return false; }
    out = n;
    return true;
}

bool curve(const QString& json, double gainMin, double gainMax,
           std::vector<double>& f, std::vector<double>& g,
           std::vector<double>& q, double& globalDb,
           double& minHz, double& maxHz, bool& parametric)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) { return false; }
    const QJsonObject root = document.object();
    const QJsonValue countValue = root.value(QStringLiteral("band_count"));
    const QJsonValue flagValue = root.value(QStringLiteral("parametric_eq"));
    const QJsonValue pointsValue = root.value(QStringLiteral("points"));
    if (!countValue.isDouble() || !flagValue.isBool() || !pointsValue.isArray()) { return false; }
    const int count = countValue.toInt(-1);
    const QJsonArray points = pointsValue.toArray();
    if ((count != 5 && count != 10 && count != 18) || points.size() != count) { return false; }
    if (!number(root, "frequency_min_hz", 0.0, 20000.0, minHz)
        || !number(root, "frequency_max_hz", 0.0, 20000.0, maxHz)
        || maxHz <= minHz
        || !number(root, "global_gain_db", gainMin, gainMax, globalDb)) { return false; }
    parametric = flagValue.toBool();
    f.reserve(count); g.reserve(count); q.reserve(count);
    double previous = -1.0;
    for (const QJsonValue& value : points) {
        if (!value.isObject()) { return false; }
        const QJsonObject point = value.toObject();
        double hz = 0.0, gain = 0.0, factor = 0.0;
        if (!number(point, "frequency_hz", minHz, maxHz, hz)
            || !number(point, "gain_db", gainMin, gainMax, gain)
            || !number(point, "q", 0.2, 20.0, factor)
            || hz <= previous) { return false; }
        previous = hz;
        f.push_back(hz); g.push_back(gain); q.push_back(factor);
    }
    return f.front() == minHz && f.back() == maxHz;
}

QString json(const Profile& p, bool compression)
{
    const std::vector<double>& frequencies = compression ? p.f : p.postF;
    const std::vector<double>& gains = compression ? p.g : p.e;
    const std::vector<double>& factors = compression ? p.qg : p.qe;
    QJsonArray points;
    for (std::size_t i = 0; i < frequencies.size(); ++i) {
        points.append(QJsonObject{{QStringLiteral("frequency_hz"), frequencies[i]},
                                  {QStringLiteral("gain_db"), gains[i]},
                                  {QStringLiteral("q"), factors[i]}});
    }
    return QString::fromUtf8(QJsonDocument(QJsonObject{
        {QStringLiteral("band_count"), static_cast<int>(p.f.size())},
        {QStringLiteral("parametric_eq"), compression ? p.compParametric : p.eqParametric},
        {QStringLiteral("global_gain_db"), compression ? p.precompDb : p.postEqGainDb},
        {QStringLiteral("frequency_min_hz"), compression ? p.minHz : p.postMinHz},
        {QStringLiteral("frequency_max_hz"), compression ? p.maxHz : p.postMaxHz},
        {QStringLiteral("points"), points}}).toJson(QJsonDocument::Indented));
}
} // namespace

// From Thetis frmCFCConfig.cs:492-557 [v2.10.3.15], with bounds before
// the Core accepts a new remote write. Runtime uses both parametric flags
// (frmCFCConfig.cs:378), so mismatched flags remain valid and disable Q.
bool decode(const QString& blob, Profile& out)
{
    if (blob.isEmpty() || blob.size() > kMaxEncodedChars) { return false; }
    const std::optional<QString> decoded = ParaEqEnvelope::decode(blob, kMaxDecodedBytes);
    if (!decoded) { return false; }
    const qsizetype separator = decoded->indexOf(QStringLiteral("<SEP>"));
    if (separator < 0 || decoded->indexOf(QStringLiteral("<SEP>"), separator + 5) >= 0) {
        return false;
    }
    Profile candidate;
    if (!curve(decoded->left(separator), 0.0, 16.0,
               candidate.f, candidate.g, candidate.qg,
               candidate.precompDb, candidate.minHz, candidate.maxHz,
               candidate.compParametric)
        || !curve(decoded->mid(separator + 5), -24.0, 24.0,
                  candidate.postF, candidate.e, candidate.qe,
                  candidate.postEqGainDb, candidate.postMinHz, candidate.postMaxHz,
                  candidate.eqParametric)
        || candidate.postF.size() != candidate.f.size()) { return false; }
    out = std::move(candidate);
    return true;
}

// From Thetis frmCFCConfig.cs:492-504 [v2.10.3.15].
QString encode(const Profile& p)
{
    const std::size_t count = p.f.size();
    if ((count != 5 && count != 10 && count != 18)
        || p.postF.size() != count || p.g.size() != count || p.e.size() != count
        || p.qg.size() != count || p.qe.size() != count) { return {}; }
    const QString blob = ParaEqEnvelope::encode(json(p, true) + QStringLiteral("<SEP>")
                                                  + json(p, false));
    if (blob.size() > kMaxEncodedChars) { return {}; }
    Profile check;
    return decode(blob, check) ? blob : QString();
}
} // namespace NereusSDR::CfcProfile
