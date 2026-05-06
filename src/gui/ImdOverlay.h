#pragma once

// no-port-check: NereusSDR-original IMD-overlay class implementing the
// Thetis two-tone IMD measurement algorithm. The algorithm itself is
// ported verbatim from Thetis display.cs:5008, 5210-5316, 5453-5475,
// 5512-5685, 5725-5760 [v2.10.3.13] (see inline cites below); but
// because Thetis' display.cs is a 24,000+ LOC monolith that mixes
// rendering with measurement, NereusSDR factors the IMD subset into a
// dedicated NereusSDR-only class. SpectrumWidget remains the host;
// ImdOverlay is the analytical core.
//
// =================================================================
// src/gui/ImdOverlay.h  (NereusSDR)
// =================================================================
//
// Two-tone IMD measurement overlay — peak detection, IMD3/IMD5
// labeling, EMA smoothing, and readout-text formatting.  Renders
// nothing itself; SpectrumWidget owns the QPainter draw call and
// calls into ImdOverlay for the values + the formatted strings.
//
// Show condition (from Thetis display.cs:5008 [v2.10.3.13]):
//   show_imd_measurements = local_mox && _testing_imd
//                           && _show_imd_measurements && displayduplex;
//
// In NereusSDR these four flags are sourced from:
//   local_mox            -> MoxController::isMox()
//   _testing_imd         -> TwoToneController::isActive() (drives
//                            Display.TestingIMD via PureSignal::
//                            setTwoToneOn -> TwoToneController::
//                            setActive; PSForm.cs:508-522 [v2.10.3.13])
//   _show_imd_measurements -> PureSignal::show2ToneMeasurements()
//                              (PSForm.cs:968-971 chkShow2ToneMeasurements
//                              _CheckedChanged: Display.ShowIMDMeasurments
//                              = checked. [v2.10.3.13])
//   displayduplex         -> existing SpectrumWidget duplex mode flag
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-06 — Created by J.J. Boyd (KG4VCF) for Phase 3M-4
//                 PureSignal Task 12, with AI-assisted source-first
//                 protocol via Anthropic Claude Code.
// =================================================================

#include <QObject>
#include <QString>
#include <vector>

namespace NereusSDR {

// Per-peak record used for IMD measurement.  Direct port of Thetis
// `private struct Maximums` at display.cs:4421-4428 [v2.10.3.13]:
//   public float max_dBm;
//   public int X;
//   public int MaxY_pixel;
//   public bool Enabled;
//   public double Time;
// NereusSDR drops the Time field — peak-blob aging is a carson-branch
// concern; IMD measurement is single-frame and doesn't track age.
struct Maximum {
    float dBm    = 0.0f;
    int   x      = 0;       // bin index in the spectrum buffer
    int   yPixel = 0;       // pixel Y for ellipse marker (0 = unused)
    bool  enabled = true;
};

// EMA-smoothed measurement readout.  Field semantics match Thetis
// display.cs:4946-4966 [v2.10.3.13] _ema_* fields, plus derived
// imd3dBc / imd5dBc / oip3 / oip5.
//
// All dBm values are absolute (raw from the spectrum bin).  dbc is the
// "carrier reference" = max(f0L, f0U) per display.cs:5575 [v2.10.3.13]:
//   float dbc = Math.Max(f[0], f[1]);
// worstImd3DBc / worstImd5DBc are stored signed so display can apply
// the Thetis `-` flip at format time (display.cs:5648-5649: worst_imd3
// = -_ema_imd3dBc, worst_imd5 = -_ema_imd5dBc).
struct ImdReadout {
    float f0L = 0.0f;
    float f0U = 0.0f;
    float imd3L = 0.0f;
    float imd3U = 0.0f;
    float imd5L = 0.0f;
    float imd5U = 0.0f;
    float dbc = 0.0f;            // = max(f0L, f0U)
    float dbcMin = 0.0f;          // = min(f0L, f0U)
    float worstImd3DBc = 0.0f;   // = dbcMin - max(imd3L, imd3U)  (signed)
    float worstImd5DBc = 0.0f;   // = dbcMin - max(imd5L, imd5U)  (signed)
    float oip3 = 0.0f;            // = dbcMin + worstImd3DBc/2
    float oip5 = 0.0f;            // = dbcMin + worstImd5DBc/2
    bool  valid = false;
};

class ImdOverlay : public QObject {
    Q_OBJECT
public:
    explicit ImdOverlay(QObject* parent = nullptr);

    // Walk the spectrum buffer (bin index = sample index) and detect
    // local maxima.  Direct port of Thetis display.cs:5283-5298
    // [v2.10.3.13]: state machine alternates between "look_for_max"
    // and "look_for_min".  A peak is confirmed when the running max
    // drops by triggerDelta dB; a trough is confirmed when the running
    // min rises by triggerDelta dB.  The yPixel field of returned
    // Maxima is left zero (caller is responsible for mapping bin -> Y
    // for the ellipse marker, since pixel mapping depends on
    // grid_max / dbmToPixel which are SpectrumWidget concerns).
    //
    // triggerDelta default in Thetis is 10 dB (display.cs:5217).
    static std::vector<Maximum> detectPeaks(const std::vector<float>& spectrumDbm,
                                            float triggerDelta);

    // Identify f0L, f0U, IMD3L, IMD3U, IMD5L, IMD5U from the detected
    // peak set.  Direct port of Thetis display.cs:5512-5560
    // [v2.10.3.13]:
    //   sorted = peaks.OrderByDescending(max_dBm); top 2 = fundamentals.
    //   pixel_diff = |sorted[0].X - sorted[1].X|; if <= 10, fail.
    //   sortedlow  = peaks.Where(X < mid_x).OrderByDescending(X)
    //   sortedhigh = peaks.Where(X > mid_x).OrderBy(X)
    //   findImd(sortedlow,  N, pixel_diff, low_x,  true)
    //   findImd(sortedhigh, N, pixel_diff, high_x, false)
    // Returns false if fewer than 2 peaks, fundamentals too close, or
    // any of fL / fH / IMD3L / IMD3U / IMD5L / IMD5U cannot be found.
    static bool labelImdProducts(const std::vector<Maximum>& peaks,
                                 Maximum& outF0L, Maximum& outF0U,
                                 Maximum& outImd3L, Maximum& outImd3U,
                                 Maximum& outImd5L, Maximum& outImd5U);

    // EMA-smooth the labeled values into m_readout.  Direct port of
    // Thetis display.cs:5589-5641 [v2.10.3.13]:
    //   first call (when uninitialised): copy raw values verbatim
    //   subsequent calls: previous = alpha * newValue + (1 - alpha) * previous;
    //   alpha = 0.1f (display.cs:5618)
    void updateReadout(const Maximum& f0L, const Maximum& f0U,
                       const Maximum& imd3L, const Maximum& imd3U,
                       const Maximum& imd5L, const Maximum& imd5U);

    // Format the 3 readout columns (label / val1 / val2) verbatim per
    // Thetis display.cs:5650-5685 [v2.10.3.13].
    struct ReadoutText {
        QString readings;  // labels column
        QString val1;      // absolute dBm column + dBc / dB summary block
        QString val2;      // relative dB column (6 lines)
    };
    ReadoutText formatReadout() const;

    // Read-only access to the smoothed readout.  Returned by const-ref
    // for tests + external diagnostics.
    const ImdReadout& readout() const { return m_readout; }

    // Clear EMA state.  Mirrors the Thetis display.cs:5680
    // [v2.10.3.13]:
    //   else if (_ema_dbc != -999) _ema_dbc = -999;
    // i.e. when the show condition flips off (or peaks lost), the next
    // show-on transition seeds EMA from raw values rather than blending
    // with stale values.
    void reset();

private:
    ImdReadout m_readout;
    bool m_emaInitialized = false;
};

} // namespace NereusSDR
