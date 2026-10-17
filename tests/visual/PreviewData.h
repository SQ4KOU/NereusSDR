// no-port-check: NereusSDR-original visual preview tool; fixed illustrative data.
// =================================================================
// tests/visual/PreviewData.h  (NereusSDR)
// =================================================================
//
// Fixed illustrative data for the workspace visual preview tool: a seeded
// band model (noise, speech, Morse, FT8) for the spectrum and waterfall,
// and the freeze step that holds every live readout still so two captures
// of one scene match pixel for pixel.
// Nothing here reads the wall clock, the network or an unseeded RNG.
//
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - waitForQuietLayout: a scene waits for its layout to stop
//                 moving, not for a fixed time, before it freezes, feeds
//                 and grabs. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

#pragma once

#include <QJsonArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QtGlobal>

#include <memory>

class QWidget;

namespace NereusSDR {

class MainWindow;
class SpectrumWidget;

namespace Preview {

// Shape of one illustrative signal on a pan.
enum class SignalShape {
    Usb,  // 2.4 kHz voice passband above the dial frequency
    Lsb,  // 2.4 kHz voice passband below the dial frequency
    Cw,   // 60 Hz carrier
    Ft8,  // one 50 Hz FT8 tone
};

struct SignalSpec {
    double hz{0.0};    // dial (SSB) or tone/carrier (CW, FT8) frequency
    double dbm{0.0};   // peak level the trace should read
    SignalShape shape{SignalShape::Usb};
};

struct PanSpectrum {
    QString panId;
    double noiseDbm{-126.0};
    QVector<SignalSpec> signalList;
    quint32 seed{1};
};

// Result of feeding one pan: what the trace reads at the strongest signal,
// and where the noise floor sits.
struct SpectrumCheck {
    QString panId;
    double targetHz{0.0};
    double targetDbm{0.0};
    double displayedDbm{0.0};
    double centreHz{0.0};
    double spanHz{0.0};
    double ddcCentreHz{0.0};
    double sampleRateHz{0.0};
    int waterfallRows{0};
    double rowPeriodMs{0.0};
    double noiseTargetDbm{0.0};     // the stated displayed noise floor
    double noiseDisplayedDbm{0.0};  // median of the settled trace
    double noiseOffsetDb{0.0};      // added to the bin mean to land there
};

// Deterministic xorshift32, so the data is identical on every platform.
class SeededRng {
public:
    explicit SeededRng(quint32 seed) : m_state(seed == 0 ? 0x9E3779B9u : seed) {}
    quint32 next();
    // Uniform in [lo, hi).
    double uniform(double lo, double hi);
    // Standard normal (Box-Muller).
    double gaussian();
    // Unit-mean exponential: the power of one bin of Gaussian noise, so
    // 10*log10 of it is the Rayleigh texture of a waterfall row (~5.6 dB std).
    double exponential();

private:
    quint32 m_state;
};

// The band a pan shows, built once per pan: the noise floor and, for every
// signal, a seeded activity track over the waterfall's time span (speech
// syllables and pauses for SSB, Morse keying at kCwWpm for CW, 15 s FT8
// cycles with 8-FSK symbols). Frames are in the linear power form
// SpectrumWidget::updateSpectrumLinear expects.
class BandModel {
public:
    BandModel(const SpectrumWidget& sw, const PanSpectrum& spec, int rows);
    ~BandModel();
    BandModel(const BandModel&) = delete;
    BandModel& operator=(const BandModel&) = delete;

    // The settled spectrum frame: every signal at its stated peak level over
    // noise with about kTraceJitterDb of per-bin jitter. noiseOffsetDb moves
    // the noise mean.
    QVector<float> traceFrame(double noiseOffsetDb) const;
    // Waterfall row `row` (0 newest, rows - 1 oldest), one row period apart
    // in time, ending at kSceneTimeSec. Never repeats a row.
    QVector<float> rowFrame(int row, double noiseOffsetDb) const;
    double rowPeriodSec() const { return m_rowSec; }

    struct Impl;

private:
    std::unique_ptr<Impl> m_impl;
    double m_rowSec{0.03};
};

// Feeds one pan: pauses its waterfall ticker, settles the trace once to
// measure where the noise floor displays, pushes one generated row per
// waterfall row (rendering a frame every few rows so every row reaches the
// texture), then settles the trace frame. Returns the check value.
SpectrumCheck feedPan(SpectrumWidget* sw, const PanSpectrum& spec);

// Feeds a keyed pan the way LocalTxDisplaySource does on a local window:
// the transmit window set to the pan's span around the carrier, then
// transmit trace pixels (updateSpectrumFromTxPixels) and transmit waterfall
// rows (pushTxWaterfallRow) of one voice signal (LSB, or USB when `usb`),
// filter `lowHz`..`highHz` from the carrier, peak `peakDbm` over a floor of
// `floorDbm`. Display calls only; nothing reaches a connection.
void feedTxPan(SpectrumWidget* sw, double carrierHz, bool usb, int lowHz, int highHz,
               double peakDbm, double floorDbm, quint32 seed);

// The dBm offset and window ENB passed with every frame (the values the
// existing spectrum tests use).
inline constexpr double kDbmOffset = -60.0;
inline constexpr double kWindowEnb = 1.5;
inline constexpr int kBinCount = 4096;          // bins per kReferenceRateHz
inline constexpr double kReferenceRateHz = 192000.0;
inline constexpr int kTraceFrames = 240;
inline constexpr double kTraceJitterDb = 3.0;   // per-bin std of the settled trace
inline constexpr double kCwWpm = 22.0;
// The scene's clock: 14:32:10 UTC, in seconds of the day. FT8 cycles start
// on :00, :15, :30 and :45, so the newest row is 10 s into a cycle and the
// visible rows (about 6.5 s at 30 ms a row) start some 3 s after the
// cycle's tones begin: the waterfall AGC has settled on them by then, and
// the cycle's quiet gap falls below the visible rows.
inline constexpr double kSceneTimeSec = 14 * 3600 + 32 * 60 + 10;

// Fixed text for the frozen readouts.
inline constexpr const char* kFrozenUtc = "14:32:10 UTC";
inline constexpr double kFrozenCpuPercent = 7.0;
inline constexpr double kFrozenRxMbps = 18.4;
inline constexpr double kFrozenTxMbps = 0.6;
inline constexpr int kFrozenRttMs = 2;

// Stops every active QTimer under `root` and returns a description of each
// one stopped (class of its parent, object name, interval).
QJsonArray stopTimersUnder(QObject* root);

// The layout is quiet when no visible widget under the roots has moved or
// resized for kQuietLayoutMs and kQuietLayoutPolls polls of the event loop,
// and no single-shot QTimer under a root with an interval up to
// kPendingTimerMaxMs is still to fire (a debounced resize or rebuild, such
// as SpectrumWidget's 250 ms waterfall history timer).
inline constexpr int kQuietLayoutMs = 300;
inline constexpr int kQuietLayoutPolls = 5;
inline constexpr int kPendingTimerMaxMs = 1000;
inline constexpr int kQuietLayoutMaxMs = 20000;

struct LayoutQuiet {
    bool quiet{false};
    qint64 waitedMs{0};
    int moves{0};           // geometry changes seen while waiting
    QStringList pending;    // single-shot timers still to fire when it gave up
};

// Pumps the event loop until the layout under `roots` is quiet, or for at
// most `maxMs`. A fixed wait captures a layout still in motion when the
// machine is loaded (a debounce or a deferred step lands later); this waits
// on what the layout does instead, so load only makes it take longer.
LayoutQuiet waitForQuietLayout(const QList<QWidget*>& roots, int maxMs = kQuietLayoutMaxMs);

// Freezes the main window's live readouts: UTC clock, connection segment
// (pulse, rates, RTT, audio pip), CPU tile. Returns what it froze.
QJsonArray freezeReadouts(MainWindow* window);

} // namespace Preview
} // namespace NereusSDR
