// no-port-check: NereusSDR-original IMD-overlay class implementing the
// Thetis algorithm verbatim.  See header file for full provenance.
//
// =================================================================
// src/gui/ImdOverlay.cpp  (NereusSDR)
// =================================================================
//
// Two-tone IMD measurement overlay implementation.
//
// Modification history (NereusSDR):
//   2026-05-06 — Created by J.J. Boyd (KG4VCF) for Phase 3M-4
//                 PureSignal Task 12, with AI-assisted source-first
//                 protocol via Anthropic Claude Code.
// =================================================================

#include "gui/ImdOverlay.h"

#include <QString>
#include <QStringLiteral>

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace NereusSDR {

ImdOverlay::ImdOverlay(QObject* parent)
    : QObject(parent)
{
}

// ── Peak detection ──────────────────────────────────────────────────────────
//
// Direct port of Thetis display.cs:5210-5316 [v2.10.3.13].  The Thetis
// host code interleaves peak detection with several other concerns
// (noise floor accumulation, spectral peak hold, panadapter fill,
// visual notch).  ImdOverlay extracts the IMD-only state machine.
//
// State variables map 1:1 from Thetis:
//   dbm_min            -> dbmMin
//   dbm_max            -> dbmMax
//   dbm_max_xpos       -> dbmMaxXpos
//   dbm_min_xpos       -> dbmMinXpos
//   dbm_max_ypos       -> dbmMaxYpos (NereusSDR drops Y plumbing here;
//                         caller maps bin -> pixel via grid_max /
//                         dbmToPixel; we leave yPixel = 0)
//   look_for_max       -> lookForMax
//   trigger_delta      -> triggerDelta
//
// Behaviour: walk each bin.  Maintain rolling min/max.  When in
// "look-for-max" state and the new sample drops triggerDelta dB below
// the rolling max, emit a peak at (dbmMax, dbmMaxXpos).  When in
// "look-for-min" state and the new sample rises triggerDelta dB above
// the rolling min, switch back to look-for-max.
std::vector<Maximum> ImdOverlay::detectPeaks(
    const std::vector<float>& spectrumDbm, float triggerDelta)
{
    std::vector<Maximum> peaks;
    if (spectrumDbm.empty()) {
        return peaks;
    }

    // Source-first port of Thetis display.cs:5210-5316 [v2.10.3.13].
    // Thetis tracks dbm_min_xpos (display.cs:5215) but doesn't read it
    // for IMD measurement — only dbm_min itself enters the state-machine
    // condition (display.cs:5310 max > dbm_min + trigger_delta).  Drop
    // the unused xpos in NereusSDR but keep the comment to record parity.
    float dbmMin = std::numeric_limits<float>::infinity();
    float dbmMax = -std::numeric_limits<float>::infinity();
    int   dbmMaxXpos = 0;
    bool  lookForMax = true;

    const int n = static_cast<int>(spectrumDbm.size());
    for (int i = 0; i < n; ++i) {
        const float v = spectrumDbm[i];

        // From Thetis display.cs:5274-5284 [v2.10.3.13]: maintain
        // rolling max/min unconditionally each iteration.
        if (v > dbmMax) {
            dbmMax = v;
            dbmMaxXpos = i;
        }
        if (v < dbmMin) {
            dbmMin = v;
        }

        if (lookForMax) {
            // From Thetis display.cs:5286-5305 [v2.10.3.13]: emit a peak
            // when the current sample drops trigger_delta below the
            // running max, and switch to look-for-min.
            if (v < dbmMax - triggerDelta) {
                Maximum mm;
                mm.dBm     = dbmMax;
                mm.x       = dbmMaxXpos;
                mm.yPixel  = 0;       // SpectrumWidget supplies pixel Y
                mm.enabled = true;
                peaks.push_back(mm);
                dbmMin = v;
                lookForMax = false;
            }
        } else {
            // From Thetis display.cs:5306-5314 [v2.10.3.13]: when
            // climbing trigger_delta above the running min, prime for a
            // new peak and switch back to look-for-max.
            if (v > dbmMin + triggerDelta) {
                dbmMax = v;
                dbmMaxXpos = i;
                lookForMax = true;
            }
        }
    }

    return peaks;
}

// ── IMD3 / IMD5 labeling ────────────────────────────────────────────────────
//
// Direct port of Thetis display.cs:5512-5560 + display.cs:5725-5760
// findImd helper [v2.10.3.13].
//
// findImd algorithm:
//   jump = (imd - 1) / 2;          // 0,1,2 for orders 1,3,5
//   estimate_pixel_pos = offset -/+ jump * pixel_jump;  (low/high)
//   search_range = pixel_jump / 4;
//   walk `sorted`, return the entry whose X is within search_range of
//     estimate_pixel_pos and whose dBm is highest (tiebreak: closest).
//
// `sortedlow`  is the peaks with X < mid_x, ordered by descending X
// `sortedhigh` is the peaks with X > mid_x, ordered by ascending X
// (display.cs:5538/5542 [v2.10.3.13]).  findImd is order-agnostic — it
// scans the whole list for the best match — so the ordering mostly
// matters for output X reporting (which in C++ we don't care about
// since we return the matched Maximum directly).
namespace {

// Find the IMD product at order `imd` (1, 3, or 5) on one side
// (low or high) of the fundamental.  Direct port of display.cs:5725-5760
// [v2.10.3.13].  Returns the matched Maximum's index in `sorted`, or -1
// if no peak in range.
int findImd(const std::vector<Maximum>& sorted,
            int imd, int pixelJump, int offset, bool low)
{
    const int jump = (imd - 1) / 2;
    int estimatePixelPos;
    if (low) {
        estimatePixelPos = offset - (jump * pixelJump);
    } else {
        estimatePixelPos = offset + (jump * pixelJump);
    }
    const int searchRange = pixelJump / 4;

    int bestIndex = -1;
    float bestDbm = -std::numeric_limits<float>::max();
    int bestDistance = std::numeric_limits<int>::max();

    for (int i = 0; i < static_cast<int>(sorted.size()); ++i) {
        const int distance = std::abs(sorted[i].x - estimatePixelPos);
        if (distance <= searchRange) {
            // From Thetis display.cs:5749-5754 [v2.10.3.13]:
            //   if (sorted[i].max_dBm > best_dBm
            //       || (sorted[i].max_dBm == best_dBm && distance < best_distance))
            if (sorted[i].dBm > bestDbm
                || (sorted[i].dBm == bestDbm && distance < bestDistance)) {
                bestDbm = sorted[i].dBm;
                bestDistance = distance;
                bestIndex = i;
            }
        }
    }
    return bestIndex;
}

} // namespace

bool ImdOverlay::labelImdProducts(
    const std::vector<Maximum>& peaks,
    Maximum& outF0L, Maximum& outF0U,
    Maximum& outImd3L, Maximum& outImd3U,
    Maximum& outImd5L, Maximum& outImd5U)
{
    // From Thetis display.cs:5523-5526 [v2.10.3.13]:
    //   Maximums[] sorted = imd_measurements.OrderByDescending(...).ToArray();
    //   if (sorted.Length >= 2) { ...box rendering... }
    if (peaks.size() < 2) {
        return false;
    }

    std::vector<Maximum> sorted = peaks;
    std::sort(sorted.begin(), sorted.end(),
              [](const Maximum& a, const Maximum& b) {
                  return a.dBm > b.dBm;
              });

    // From Thetis display.cs:5532-5537 [v2.10.3.13]:
    //   int pixel_diff = Math.Abs(sorted[0].X - sorted[1].X);
    //   if (pixel_diff > 10) { ... }
    const int pixelDiff = std::abs(sorted[0].x - sorted[1].x);
    if (pixelDiff <= 10) {
        return false;
    }

    // From Thetis display.cs:5535-5537 [v2.10.3.13]:
    //   int low_x  = sorted[0].X < sorted[1].X ? sorted[0].X : sorted[1].X;
    //   int high_x = sorted[0].X > sorted[1].X ? sorted[0].X : sorted[1].X;
    //   int mid_x  = low_x + (pixel_diff / 2);
    const int lowX = std::min(sorted[0].x, sorted[1].x);
    const int highX = std::max(sorted[0].x, sorted[1].x);
    const int midX = lowX + (pixelDiff / 2);

    // From Thetis display.cs:5538-5544 [v2.10.3.13]:
    //   sortedlow  = imd_measurements.OrderByDescending(m => m.X)
    //                                .Where(m => m.X < mid_x).ToArray();
    //   sortedhigh = sorted.OrderBy(m => m.X)
    //                      .Where(m => m.X > mid_x).ToArray();
    std::vector<Maximum> sortedLow;
    sortedLow.reserve(peaks.size());
    for (const auto& p : peaks) {
        if (p.x < midX) {
            sortedLow.push_back(p);
        }
    }
    std::sort(sortedLow.begin(), sortedLow.end(),
              [](const Maximum& a, const Maximum& b) { return a.x > b.x; });

    std::vector<Maximum> sortedHigh;
    sortedHigh.reserve(sorted.size());
    for (const auto& p : sorted) {
        if (p.x > midX) {
            sortedHigh.push_back(p);
        }
    }
    std::sort(sortedHigh.begin(), sortedHigh.end(),
              [](const Maximum& a, const Maximum& b) { return a.x < b.x; });

    // From Thetis display.cs:5546-5556 [v2.10.3.13]:
    //   int fL = findImd(sortedlow, 1, pixel_diff, low_x, true, ...);
    //   int fH = findImd(sortedhigh, 1, pixel_diff, high_x, false, ...);
    //   ...
    const int fL = findImd(sortedLow, 1, pixelDiff, lowX, true);
    const int fH = findImd(sortedHigh, 1, pixelDiff, highX, false);
    const int imd3L = findImd(sortedLow, 3, pixelDiff, lowX, true);
    const int imd3H = findImd(sortedHigh, 3, pixelDiff, highX, false);
    const int imd5L = findImd(sortedLow, 5, pixelDiff, lowX, true);
    const int imd5H = findImd(sortedHigh, 5, pixelDiff, highX, false);

    // From Thetis display.cs:5557 [v2.10.3.13]:
    //   bool ok = fL != -1 && fH != -1 && imd3indexL != -1
    //             && imd3indexH != -1 && imd5indexL != -1 && imd5indexH != -1;
    if (fL == -1 || fH == -1 || imd3L == -1 || imd3H == -1
            || imd5L == -1 || imd5H == -1) {
        return false;
    }

    outF0L  = sortedLow[fL];
    outF0U  = sortedHigh[fH];
    outImd3L = sortedLow[imd3L];
    outImd3U = sortedHigh[imd3H];
    outImd5L = sortedLow[imd5L];
    outImd5U = sortedHigh[imd5H];
    return true;
}

// ── EMA smoothing ──────────────────────────────────────────────────────────
//
// Direct port of Thetis display.cs:5589-5641 [v2.10.3.13]:
//   if (_ema_dbc == -999) { /* init: copy raw to ema */ }
//   else { float alpha = 0.1f; _ema = alpha * new + (1 - alpha) * _ema; }
//
// dbc / dbcMin / imd3dBc / imd5dBc / oip3 / oip5 derivations come from
// display.cs:5575-5582 [v2.10.3.13]:
//   float dbc      = Math.Max(f[0], f[1]);
//   float dbc_min  = Math.Min(f[0], f[1]);
//   float imd3max  = Math.Max(imd3[0], imd3[1]);
//   float imd5max  = Math.Max(imd5[0], imd5[1]);
//   float imd3dBc  = dbc_min - imd3max;
//   float imd5dBc  = dbc_min - imd5max;
//   float oip3     = dbc_min + (imd3dBc / 2f);
//   float oip5     = dbc_min + (imd5dBc / 2f);
void ImdOverlay::updateReadout(const Maximum& f0L, const Maximum& f0U,
                               const Maximum& imd3L, const Maximum& imd3U,
                               const Maximum& imd5L, const Maximum& imd5U)
{
    // From Thetis display.cs:5575-5582 [v2.10.3.13].
    const float rawF0L  = f0L.dBm;
    const float rawF0U  = f0U.dBm;
    const float rawImd3L = imd3L.dBm;
    const float rawImd3U = imd3U.dBm;
    const float rawImd5L = imd5L.dBm;
    const float rawImd5U = imd5U.dBm;

    const float rawDbc   = std::max(rawF0L, rawF0U);
    const float rawDbcMin = std::min(rawF0L, rawF0U);
    const float rawImd3Max = std::max(rawImd3L, rawImd3U);
    const float rawImd5Max = std::max(rawImd5L, rawImd5U);
    const float rawImd3dBc = rawDbcMin - rawImd3Max;
    const float rawImd5dBc = rawDbcMin - rawImd5Max;
    const float rawOip3 = rawDbcMin + (rawImd3dBc / 2.0f);
    const float rawOip5 = rawDbcMin + (rawImd5dBc / 2.0f);

    if (!m_emaInitialized) {
        // From Thetis display.cs:5591-5614 [v2.10.3.13] init branch:
        m_readout.f0L = rawF0L;
        m_readout.f0U = rawF0U;
        m_readout.imd3L = rawImd3L;
        m_readout.imd3U = rawImd3U;
        m_readout.imd5L = rawImd5L;
        m_readout.imd5U = rawImd5U;
        m_readout.dbc = rawDbc;
        m_readout.dbcMin = rawDbcMin;
        m_readout.worstImd3DBc = rawImd3dBc;
        m_readout.worstImd5DBc = rawImd5dBc;
        m_readout.oip3 = rawOip3;
        m_readout.oip5 = rawOip5;
        m_emaInitialized = true;
    } else {
        // From Thetis display.cs:5618-5641 [v2.10.3.13]:
        //   float alpha = 0.1f;
        //   _ema_dbc = alpha * dbc + (1 - alpha) * _ema_dbc;
        //   ... (per-field EMA blend)
        constexpr float alpha = 0.1f;
        const float oneMinusAlpha = 1.0f - alpha;
        auto blend = [=](float oldVal, float newVal) {
            return alpha * newVal + oneMinusAlpha * oldVal;
        };
        m_readout.f0L = blend(m_readout.f0L, rawF0L);
        m_readout.f0U = blend(m_readout.f0U, rawF0U);
        m_readout.imd3L = blend(m_readout.imd3L, rawImd3L);
        m_readout.imd3U = blend(m_readout.imd3U, rawImd3U);
        m_readout.imd5L = blend(m_readout.imd5L, rawImd5L);
        m_readout.imd5U = blend(m_readout.imd5U, rawImd5U);
        m_readout.dbc = blend(m_readout.dbc, rawDbc);
        m_readout.dbcMin = blend(m_readout.dbcMin, rawDbcMin);
        m_readout.worstImd3DBc = blend(m_readout.worstImd3DBc, rawImd3dBc);
        m_readout.worstImd5DBc = blend(m_readout.worstImd5DBc, rawImd5dBc);
        m_readout.oip3 = blend(m_readout.oip3, rawOip3);
        m_readout.oip5 = blend(m_readout.oip5, rawOip5);
    }
    m_readout.valid = true;
}

void ImdOverlay::reset()
{
    // From Thetis display.cs:5680 [v2.10.3.13]:
    //   else if (_ema_dbc != -999) _ema_dbc = -999;
    m_readout = ImdReadout{};
    m_emaInitialized = false;
}

// ── Readout text formatting ────────────────────────────────────────────────
//
// Direct port of Thetis display.cs:5642-5685 [v2.10.3.13].  Three
// strings are emitted; SpectrumWidget paints each in its own column
// inside the rounded readout box.
ImdOverlay::ReadoutText ImdOverlay::formatReadout() const
{
    auto f2 = [](float v) { return QString::number(v, 'f', 2); };

    ReadoutText t;

    // From Thetis display.cs:5651-5660 [v2.10.3.13]:
    //   string readings =
    //       "    f0 L\n" +
    //       "    f0 U\n" +
    //       "IMD3 L\n" +
    //       "IMD3 U\n" +
    //       "IMD5 L\n" +
    //       "IMD5 U\n\n" +
    //       "        IMD3\n" +
    //       "        IMD5\n" +
    //       "        OIP3\n" +
    //       "        OIP5";
    t.readings = QStringLiteral(
        "    f0 L\n"
        "    f0 U\n"
        "IMD3 L\n"
        "IMD3 U\n"
        "IMD5 L\n"
        "IMD5 U\n\n"
        "        IMD3\n"
        "        IMD5\n"
        "        OIP3\n"
        "        OIP5");

    // From Thetis display.cs:5648-5649 [v2.10.3.13]:
    //   float worst_imd3 = -_ema_imd3dBc;
    //   float worst_imd5 = -_ema_imd5dBc;
    const float worstImd3 = -m_readout.worstImd3DBc;
    const float worstImd5 = -m_readout.worstImd5DBc;

    // From Thetis display.cs:5662-5673 [v2.10.3.13]:
    //   string val1 =
    //       _ema_f0l.ToString("f2") + "\n" +
    //       _ema_f0u.ToString("f2") + "\n" +
    //       _ema_imd3l.ToString("f2") + "\n" +
    //       _ema_imd3u.ToString("f2") + "\n" +
    //       _ema_imd5l.ToString("f2") + "\n" +
    //       _ema_imd5u.ToString("f2") + "\n\n" +
    //       "    " + worst_imd3.ToString("f2") + " dBc\n" +
    //       "    " + worst_imd5.ToString("f2") + " dBc\n" +
    //       "    " + _ema_oip3.ToString("f2") + " dB\n" +
    //       "    " + _ema_oip5.ToString("f2") + " dB";
    t.val1 = f2(m_readout.f0L) + QStringLiteral("\n")
           + f2(m_readout.f0U) + QStringLiteral("\n")
           + f2(m_readout.imd3L) + QStringLiteral("\n")
           + f2(m_readout.imd3U) + QStringLiteral("\n")
           + f2(m_readout.imd5L) + QStringLiteral("\n")
           + f2(m_readout.imd5U) + QStringLiteral("\n\n")
           + QStringLiteral("    ") + f2(worstImd3) + QStringLiteral(" dBc\n")
           + QStringLiteral("    ") + f2(worstImd5) + QStringLiteral(" dBc\n")
           + QStringLiteral("    ") + f2(m_readout.oip3) + QStringLiteral(" dB\n")
           + QStringLiteral("    ") + f2(m_readout.oip5) + QStringLiteral(" dB");

    // From Thetis display.cs:5642-5648 [v2.10.3.13]:
    //   float f0l   = -(_ema_dbc - _ema_f0l);
    //   float f0u   = -(_ema_dbc - _ema_f0u);
    //   float imd3l = -(_ema_dbc - _ema_imd3l);
    //   float imd3u = -(_ema_dbc - _ema_imd3u);
    //   float imd5l = -(_ema_dbc - _ema_imd5l);
    //   float imd5u = -(_ema_dbc - _ema_imd5u);
    const float f0lRel  = -(m_readout.dbc - m_readout.f0L);
    const float f0uRel  = -(m_readout.dbc - m_readout.f0U);
    const float imd3lRel = -(m_readout.dbc - m_readout.imd3L);
    const float imd3uRel = -(m_readout.dbc - m_readout.imd3U);
    const float imd5lRel = -(m_readout.dbc - m_readout.imd5L);
    const float imd5uRel = -(m_readout.dbc - m_readout.imd5U);

    // From Thetis display.cs:5675-5681 [v2.10.3.13]:
    //   string val2 =
    //       f0l.ToString("f2") + "\n" +
    //       f0u.ToString("f2") + "\n" +
    //       imd3l.ToString("f2") + "\n" +
    //       imd3u.ToString("f2") + "\n" +
    //       imd5l.ToString("f2") + "\n" +
    //       imd5u.ToString("f2");
    t.val2 = f2(f0lRel) + QStringLiteral("\n")
           + f2(f0uRel) + QStringLiteral("\n")
           + f2(imd3lRel) + QStringLiteral("\n")
           + f2(imd3uRel) + QStringLiteral("\n")
           + f2(imd5lRel) + QStringLiteral("\n")
           + f2(imd5uRel);

    return t;
}

} // namespace NereusSDR
