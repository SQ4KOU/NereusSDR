// src/core/antenna/AmateurBands.cpp

// --- From src/core/antenna/AmateurBands.h ---
// =================================================================
// src/core/antenna/AmateurBands.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// Where the bands begin and end, so a sweep can be drawn against
// something real.
//
// ── Why this is not the table already in DxccColorProvider ───────────
//
// That one exists to guess a mode from a spot frequency, and it is
// written to Region 2 edges: 40 m running to 7.300, 80 m to 4.000,
// 160 m from 1.800.
//
// For an operator in Region 1 those numbers are wrong in the direction
// that matters. Drawing a 40 m band edge at 7.300 puts a hundred
// kilohertz of "your antenna is fine here" over spectrum they may not
// transmit in. An antenna tool has to know which set it is using and
// say so.
//
// ── This is a band PLAN, not a licence ───────────────────────────────
//
// The IARU plan is what the three regions agreed among themselves.
// National allocations differ — a licence class may be narrower, and a
// country may have more or less than the plan says. So the region is a
// setting rather than a constant, and anything drawn from this is
// labelled with the region it came from. Nobody should read a green
// bar here as permission.
//
// Region 1 edges checked against the IARU R1 HF band plan, August 2026.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-10 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// --- From src/core/antenna/AmateurBands.cpp ---
// =================================================================
// src/core/antenna/AmateurBands.cpp  (Longpath)
// =================================================================
//
// Longpath-original. See AmateurBands.h — this is a band plan, not a
// licence, and the region is a setting for that reason.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-10 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/core/antenna/AmateurBands.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/core/antenna/AmateurBands.h
//   src/core/antenna/AmateurBands.cpp
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/antenna/AmateurBands.h"

#include <algorithm>

namespace NereusSDR::AmateurBands {
namespace {

// From Longpath src/core/antenna/AmateurBands.cpp:21 [@551576e3]
Band mk(double lowMHz, double highMHz, const char* name)
{
    Band b;
    b.lowHz  = lowMHz  * 1e6;
    b.highHz = highMHz * 1e6;
    b.name   = QString::fromLatin1(name);
    return b;
}

// From Longpath src/core/antenna/AmateurBands.cpp:36 [@551576e3]
// ── Region 1 ─────────────────────────────────────────────────────────
// Europe, Africa, the Middle East and northern Asia. OE5 is here.
//
// The three edges that differ most from Region 2 and are the ones an
// operator gets bitten by: 160 m starts at 1.810, 80 m stops at 3.800,
// 40 m stops at 7.200.
const QVector<Band>& region1()
{
    static const QVector<Band> b = {
        mk(1.810,   2.000,   "160 m"),
        mk(3.500,   3.800,   "80 m"),
        mk(5.3515,  5.3665,  "60 m"),
        mk(7.000,   7.200,   "40 m"),
        mk(10.100,  10.150,  "30 m"),
        mk(14.000,  14.350,  "20 m"),
        mk(18.068,  18.168,  "17 m"),
        mk(21.000,  21.450,  "15 m"),
        mk(24.890,  24.990,  "12 m"),
        mk(28.000,  29.700,  "10 m"),
        mk(50.000,  52.000,  "6 m"),
        mk(144.000, 146.000, "2 m"),
        mk(430.000, 440.000, "70 cm"),
    };
    return b;
}

// From Longpath src/core/antenna/AmateurBands.cpp:56 [@551576e3]
const QVector<Band>& region2()
{
    static const QVector<Band> b = {
        mk(1.800,   2.000,   "160 m"),
        mk(3.500,   4.000,   "80 m"),
        mk(5.3320,  5.4050,  "60 m"),
        mk(7.000,   7.300,   "40 m"),
        mk(10.100,  10.150,  "30 m"),
        mk(14.000,  14.350,  "20 m"),
        mk(18.068,  18.168,  "17 m"),
        mk(21.000,  21.450,  "15 m"),
        mk(24.890,  24.990,  "12 m"),
        mk(28.000,  29.700,  "10 m"),
        mk(50.000,  54.000,  "6 m"),
        mk(144.000, 148.000, "2 m"),
        mk(430.000, 450.000, "70 cm"),
    };
    return b;
}

// From Longpath src/core/antenna/AmateurBands.cpp:79 [@551576e3]
// Region 3 varies more between countries than the other two — several
// administrations allocate less than the plan. Taken as the IARU R3
// plan, and the header's warning about licences applies here hardest.
const QVector<Band>& region3()
{
    static const QVector<Band> b = {
        mk(1.800,   2.000,   "160 m"),
        mk(3.500,   3.900,   "80 m"),
        mk(5.3515,  5.3665,  "60 m"),
        mk(7.000,   7.200,   "40 m"),
        mk(10.100,  10.150,  "30 m"),
        mk(14.000,  14.350,  "20 m"),
        mk(18.068,  18.168,  "17 m"),
        mk(21.000,  21.450,  "15 m"),
        mk(24.890,  24.990,  "12 m"),
        mk(28.000,  29.700,  "10 m"),
        mk(50.000,  54.000,  "6 m"),
        mk(144.000, 148.000, "2 m"),
        mk(430.000, 450.000, "70 cm"),
    };
    return b;
}

} // namespace

// From Longpath src/core/antenna/AmateurBands.cpp:101 [@551576e3]
const QVector<Band>& forRegion(Region r)
{
    switch (r) {
    case Region::One:   return region1();
    case Region::Two:   return region2();
    case Region::Three: return region3();
    }
    return region1();
}

// From Longpath src/core/antenna/AmateurBands.cpp:111 [@551576e3]
QString regionName(Region r)
{
    switch (r) {
    case Region::One:   return QStringLiteral("IARU Region 1");
    case Region::Two:   return QStringLiteral("IARU Region 2");
    case Region::Three: return QStringLiteral("IARU Region 3");
    }
    return QString{};
}

// From Longpath src/core/antenna/AmateurBands.cpp:121 [@551576e3]
Band containing(double hz, Region r)
{
    for (const Band& b : forRegion(r)) {
        if (b.contains(hz)) { return b; }
    }
    return Band{};
}

// From Longpath src/core/antenna/AmateurBands.cpp:129 [@551576e3]
QVector<Band> allOverlapping(double startHz, double stopHz, Region r)
{
    if (stopHz < startHz) { std::swap(startHz, stopHz); }
    QVector<Band> out;
    for (const Band& b : forRegion(r)) {
        // Same strictness as bestOverlap: touching at exactly one point
        // is not being about a band.
        if (std::min(stopHz, b.highHz) - std::max(startHz, b.lowHz) > 0.0) {
            out.append(b);
        }
    }
    return out;
}

// From Longpath src/core/antenna/AmateurBands.cpp:143 [@551576e3]
Band bestOverlap(double startHz, double stopHz, Region r)
{
    if (stopHz < startHz) { std::swap(startHz, stopHz); }

    Band  best;
    double bestSpan = 0.0;
    for (const Band& b : forRegion(r)) {
        const double lo = std::max(startHz, b.lowHz);
        const double hi = std::min(stopHz,  b.highHz);
        const double span = hi - lo;
        // Strictly greater than zero: a sweep that stops exactly on a
        // band edge touches that band at one point and is not about it.
        if (span > 0.0 && span > bestSpan) {
            best = b;
            bestSpan = span;
        }
    }
    return best;
}

} // namespace NereusSDR::AmateurBands
