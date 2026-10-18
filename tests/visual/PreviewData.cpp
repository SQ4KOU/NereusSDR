// no-port-check: NereusSDR-original visual preview tool; fixed illustrative data.
// =================================================================
// tests/visual/PreviewData.cpp  (NereusSDR)
// =================================================================
//
// See PreviewData.h.
//
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09 - waitForQuietLayout: waits until no geometry moves and no
//                 short single-shot timer is pending under the roots.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "PreviewData.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLabel>
#include <QPair>
#include <QRegularExpression>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

#include "core/AudioEngine.h"
#include "gui/MainWindow.h"
#include "gui/SpectrumWidget.h"
#include "gui/TitleBar.h"
#include "gui/widgets/SystemTile.h"

namespace NereusSDR::Preview {

namespace {

// ---- constants -------------------------------------------------------------

// SSB voice passband (audio Hz) and its long-term contour: a rise from
// 300 Hz, flat 500 to 1000 Hz, then a slope to -12 dB at 2.7 kHz.
constexpr double kSsbLowHz = 300.0;
constexpr double kSsbHighHz = 2700.0;
// Formants: three bumps whose centres move syllable to syllable.
constexpr double kFormantGainDb[3] = {6.0, 4.0, 3.0};
constexpr double kFormantWidthHz[3] = {140.0, 200.0, 240.0};
constexpr double kFormantBaseDb = -5.0;   // contour under the formants
constexpr double kTraceFormantHz[3] = {600.0, 1500.0, 2450.0};
// Speech timing: talk spans and pauses, syllables of 3 to 6 per second.
constexpr double kTalkMinSec = 1.8;
constexpr double kTalkMaxSec = 4.8;
constexpr double kPauseMinSec = 0.25;
constexpr double kPauseMaxSec = 1.6;
constexpr double kSyllableMinSec = 0.13;
constexpr double kSyllableMaxSec = 0.26;
constexpr double kSyllableGapMaxSec = 0.08;
constexpr double kSyllableMinDb = -11.0;
constexpr double kSpeechFloor = 1e-3;     // envelope below this: silent
// Waterfall rows: Rayleigh (exponential) power per bin. The deepest nulls are
// held at -4 dB under the mean: the waterfall AGC sets its low threshold
// from the row minimum, and an unbounded tail drags it far enough down to
// paint the noise green instead of the dark blue of a live band.
constexpr double kRowNoiseMinFraction = 0.4;
// The settled trace is an average; a waterfall row is one instant. A
// stated level is the trace's, so instantaneous rows run hotter: loud
// syllables peak above the averaged voice, and a keyed carrier above its
// average over the keying (about half on, so 3 dB).
constexpr double kSpeechRowPeakOverTraceDb = 6.0;
constexpr double kCwKeyDownOverTraceDb = 3.0;
// CW: a carrier about 60 Hz wide, keyed in Morse.
constexpr double kCwHalfWidthHz = 30.0;
// FT8: 15 s cycles, 79 symbols of 0.16 s from 0.5 s, 8 tones 6.25 Hz apart.
constexpr double kFt8CycleSec = 15.0;
constexpr double kFt8StartSec = 0.5;
constexpr double kFt8SymbolSec = 0.16;
constexpr int kFt8Symbols = 79;
constexpr double kFt8ToneStepHz = 6.25;
constexpr double kFt8HalfWidthHz = 25.0;  // trace frame: the 8 tones' 50 Hz
constexpr int kFt8Costas[7] = {3, 1, 4, 0, 6, 5, 2};
constexpr double kFt8ActiveFraction = 0.8;
// How far back the activity tracks start before the oldest row.
constexpr double kTrackLeadSec = 20.0;
// Render a frame after this many rows so every row reaches the GPU texture.
constexpr int kRowsPerRender = 8;
// Rows pushed ahead of the visible ones. The waterfall AGC is a slow
// one-pole follower on each row's min and max; these rows scroll off the
// bottom and let it settle, so the visible rows share one colour scale.
constexpr int kAgcSettleRows = 120;
// Noise floor calibration: settle the trace, measure its median, correct.
constexpr double kNoiseToleranceDb = 0.25;

const char* const kMorse[36] = {
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
    "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--..",
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----.",
};

// Mixes three values into one well-spread seed (a 32-bit finaliser), so
// neighbouring signals and rows do not draw correlated values.
quint32 mixSeed(quint32 a, quint32 b, quint32 c)
{
    quint32 h = a * 0x9E3779B1u;
    h ^= b + 0x7F4A7C15u + (h << 6) + (h >> 2);
    h ^= c + 0x165667B1u + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}

double dbmToMw(double dbm)
{
    return std::pow(10.0, dbm / 10.0);
}

double baseContourDb(double audioHz)
{
    if (audioHz < 500.0) {
        return -6.0 + 6.0 * (audioHz - kSsbLowHz) / (500.0 - kSsbLowHz);
    }
    if (audioHz <= 1000.0) {
        return 0.0;
    }
    return -12.0 * (audioHz - 1000.0) / (kSsbHighHz - 1000.0);
}

// The voice contour with formants at `f`, before normalising its peak.
double formantContourDb(double audioHz, const double f[3])
{
    double bumps = 0.0;
    for (int i = 0; i < 3; ++i) {
        const double x = (audioHz - f[i]) / kFormantWidthHz[i];
        bumps += kFormantGainDb[i] * std::exp(-x * x);
    }
    return baseContourDb(audioHz) + kFormantBaseDb + bumps;
}

// The contour's maximum over the passband, so a shape can be normalised to
// peak at 0 dB.
double formantContourMaxDb(const double f[3])
{
    double best = -1000.0;
    for (double a = kSsbLowHz; a <= kSsbHighHz; a += 10.0) {
        best = std::max(best, formantContourDb(a, f));
    }
    return best;
}

// ---- activity tracks -------------------------------------------------------

struct Syllable {
    double start{0.0};
    double dur{0.0};
    double amp{1.0};       // linear power at the syllable's centre
    double formants[3]{};
};

// Seeded speech: talk spans of syllables separated by pauses.
QVector<Syllable> speechTrack(quint32 seed, double from, double to)
{
    QVector<Syllable> out;
    SeededRng rng(seed);
    double t = from - rng.uniform(0.0, kTalkMaxSec);
    while (t < to) {
        const double talkEnd = t + rng.uniform(kTalkMinSec, kTalkMaxSec);
        while (t < talkEnd) {
            Syllable s;
            s.start = t;
            s.dur = rng.uniform(kSyllableMinSec, kSyllableMaxSec);
            s.amp = dbmToMw(rng.uniform(0.0, 1.0) < 0.25 ? 0.0 : rng.uniform(kSyllableMinDb, -1.0));
            s.formants[0] = rng.uniform(350.0, 850.0);
            s.formants[1] = rng.uniform(1000.0, 2200.0);
            s.formants[2] = rng.uniform(2300.0, 2650.0);
            out.append(s);
            t += s.dur + rng.uniform(0.0, kSyllableGapMaxSec);
        }
        t += rng.uniform(kPauseMinSec, kPauseMaxSec);
    }
    return out;
}

// Speech power envelope at time t (0..1) and the formants then.
double speechAt(const QVector<Syllable>& track, double t, const double** formants)
{
    // Syllables do not overlap; find the one covering t.
    auto it = std::upper_bound(track.cbegin(), track.cend(), t,
                               [](double v, const Syllable& s) { return v < s.start; });
    if (it == track.cbegin()) {
        return 0.0;
    }
    --it;
    const double x = (t - it->start) / it->dur;
    if (x < 0.0 || x > 1.0) {
        return 0.0;
    }
    *formants = it->formants;
    const double shape = std::sin(M_PI * x);
    return it->amp * shape * shape;
}

// Seeded Morse keying at kCwWpm: on intervals [start, end).
QVector<QPair<double, double>> cwTrack(quint32 seed, double from, double to)
{
    QVector<QPair<double, double>> on;
    SeededRng rng(seed);
    const double dit = 1.2 / kCwWpm;
    double t = from - rng.uniform(0.0, 4.0);
    auto sendChar = [&](int index) {
        for (const char* p = kMorse[index]; *p != '\0'; ++p) {
            const double len = *p == '.' ? dit : 3.0 * dit;
            on.append({t, t + len});
            t += len + dit;
        }
        t += 2.0 * dit;  // letter gap: 3 dits in all
    };
    auto sendWord = [&](const QVector<int>& word) {
        for (int c : word) {
            sendChar(c);
        }
        t += 4.0 * dit;  // word gap: 7 dits in all
    };
    // A seeded callsign: letter, letter or nothing, digit, two or three letters.
    QVector<int> call;
    call.append(static_cast<int>(rng.uniform(0.0, 26.0)));
    if (rng.uniform(0.0, 1.0) < 0.5) {
        call.append(static_cast<int>(rng.uniform(0.0, 26.0)));
    }
    call.append(26 + static_cast<int>(rng.uniform(0.0, 10.0)));
    const int suffix = rng.uniform(0.0, 1.0) < 0.5 ? 2 : 3;
    for (int i = 0; i < suffix; ++i) {
        call.append(static_cast<int>(rng.uniform(0.0, 26.0)));
    }
    const QVector<int> cq = {2, 16};          // C Q
    const QVector<int> de = {3, 4};           // D E
    const QVector<int> k = {10};              // K
    const QVector<int> rst = {31, 13, 13};    // 5 N N
    const QVector<int> tu = {19, 20};         // T U
    while (t < to) {
        if (rng.uniform(0.0, 1.0) < 0.5) {
            sendWord(cq);
            sendWord(cq);
            sendWord(de);
            sendWord(call);
            sendWord(call);
            sendWord(k);
        } else {
            sendWord(tu);
            sendWord(rst);
            sendWord(rst);
            sendWord(call);
        }
        t += rng.uniform(1.0, 4.0);  // the other station's turn
    }
    return on;
}

// The keyed fraction of [a, b).
double keyedFraction(const QVector<QPair<double, double>>& on, double a, double b)
{
    double keyed = 0.0;
    auto it = std::upper_bound(on.cbegin(), on.cend(), a,
                               [](double v, const QPair<double, double>& e) { return v < e.second; });
    for (; it != on.cend() && it->first < b; ++it) {
        keyed += std::min(b, it->second) - std::max(a, it->first);
    }
    return std::clamp(keyed / (b - a), 0.0, 1.0);
}

struct Ft8Cycle {
    bool active{false};
    double levelDb{0.0};
    int symbols[kFt8Symbols]{};
};

Ft8Cycle ft8Cycle(quint32 seed, long long cycleIndex)
{
    SeededRng rng(mixSeed(seed, 0xF8F8u, static_cast<quint32>(cycleIndex)));
    Ft8Cycle c;
    c.active = rng.uniform(0.0, 1.0) < kFt8ActiveFraction;
    c.levelDb = rng.uniform(-3.0, 0.0);
    for (int i = 0; i < kFt8Symbols; ++i) {
        c.symbols[i] = static_cast<int>(rng.uniform(0.0, 8.0));
    }
    // The three Costas sync arrays at symbols 0, 36 and 72.
    for (int base : {0, 36, 72}) {
        for (int i = 0; i < 7; ++i) {
            c.symbols[base + i] = kFt8Costas[i];
        }
    }
    return c;
}

// Adds `mw` at frequency f to the bins, split between the two nearest bins.
void addTone(QVector<double>& mwBins, double fftLow, double binWidth, double f, double mw)
{
    const double pos = (f - fftLow) / binWidth;
    const int i0 = static_cast<int>(std::floor(pos));
    const double frac = pos - i0;
    if (i0 >= 0 && i0 < mwBins.size()) {
        mwBins[i0] += mw * (1.0 - frac);
    }
    if (i0 + 1 >= 0 && i0 + 1 < mwBins.size()) {
        mwBins[i0 + 1] += mw * frac;
    }
}

QVector<float> toLinear(const QVector<double>& mwBins)
{
    // updateSpectrumLinear folds dbmOffset into the avenger's scale:
    // displayed dBm = 10*log10(linear) + dbmOffset.
    QVector<float> bins(mwBins.size());
    const double scale = std::pow(10.0, -kDbmOffset / 10.0);
    for (int i = 0; i < mwBins.size(); ++i) {
        bins[i] = static_cast<float>(mwBins[i] * scale);
    }
    return bins;
}

} // namespace

// ---- SeededRng -------------------------------------------------------------

quint32 SeededRng::next()
{
    quint32 x = m_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    m_state = x;
    return x;
}

double SeededRng::uniform(double lo, double hi)
{
    return lo + (hi - lo) * (static_cast<double>(next()) / 4294967296.0);
}

double SeededRng::gaussian()
{
    const double u1 = uniform(1e-12, 1.0);
    const double u2 = uniform(0.0, 1.0);
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
}

double SeededRng::exponential()
{
    return -std::log(uniform(1e-12, 1.0));
}

// ---- BandModel -------------------------------------------------------------

struct BandModel::Impl {
    PanSpectrum spec;
    int binCount{kBinCount};
    double binWidth{0.0};
    double fftLow{0.0};
    double newestSec{kSceneTimeSec};
    QVector<QVector<Syllable>> speech;                    // per signal (SSB)
    QVector<QVector<QPair<double, double>>> keying;       // per signal (CW)

    void addSignalsTrace(QVector<double>& mw) const;
    void addSignalsRow(QVector<double>& mw, double t, double rowSec) const;
};

BandModel::BandModel(const SpectrumWidget& sw, const PanSpectrum& spec, int rows)
    : m_impl(std::make_unique<Impl>())
{
    Impl& d = *m_impl;
    d.spec = spec;
    const double sampleRate = sw.sampleRate();
    // A fixed bin width (kBinCount bins per 192 kHz), so a wider stream
    // gets proportionally more bins, as a fixed-resolution FFT would.
    d.binCount = std::max(kBinCount, static_cast<int>(std::lround(
        kBinCount * sampleRate / kReferenceRateHz)));
    d.binWidth = sampleRate / d.binCount;
    d.fftLow = sw.ddcCenterFrequency() - sampleRate / 2.0;
    // One waterfall row per update period, as the strip's time labels read.
    m_rowSec = std::max(1, sw.wfUpdatePeriodMs()) / 1000.0;
    const double oldest = d.newestSec - rows * m_rowSec - kTrackLeadSec;
    d.speech.resize(spec.signalList.size());
    d.keying.resize(spec.signalList.size());
    for (int s = 0; s < spec.signalList.size(); ++s) {
        const quint32 seed = mixSeed(spec.seed, 0x5160u, static_cast<quint32>(s + 1));
        switch (spec.signalList[s].shape) {
        case SignalShape::Usb:
        case SignalShape::Lsb:
            d.speech[s] = speechTrack(seed, oldest, d.newestSec + 1.0);
            break;
        case SignalShape::Cw:
            d.keying[s] = cwTrack(seed, oldest, d.newestSec + 1.0);
            break;
        case SignalShape::Ft8:
            break;
        }
    }
}

BandModel::~BandModel() = default;

void BandModel::Impl::addSignalsTrace(QVector<double>& mw) const
{
    const double traceMax = formantContourMaxDb(kTraceFormantHz);
    for (const SignalSpec& sig : spec.signalList) {
        for (int i = 0; i < binCount; ++i) {
            const double f = fftLow + i * binWidth;
            switch (sig.shape) {
            case SignalShape::Usb:
            case SignalShape::Lsb: {
                const double audio = sig.shape == SignalShape::Usb ? f - sig.hz : sig.hz - f;
                if (audio >= kSsbLowHz && audio <= kSsbHighHz) {
                    mw[i] += dbmToMw(sig.dbm + formantContourDb(audio, kTraceFormantHz) - traceMax);
                }
                break;
            }
            case SignalShape::Cw:
            case SignalShape::Ft8: {
                const double half = sig.shape == SignalShape::Cw ? kCwHalfWidthHz : kFt8HalfWidthHz;
                const double centre = sig.shape == SignalShape::Ft8
                    ? sig.hz + 3.5 * kFt8ToneStepHz : sig.hz;
                if (std::abs(f - centre) <= std::max(half, binWidth / 2.0)) {
                    mw[i] += dbmToMw(sig.dbm);
                }
                break;
            }
            }
        }
    }
}

void BandModel::Impl::addSignalsRow(QVector<double>& mw, double t, double rowSec) const
{
    for (int s = 0; s < spec.signalList.size(); ++s) {
        const SignalSpec& sig = spec.signalList[s];
        switch (sig.shape) {
        case SignalShape::Usb:
        case SignalShape::Lsb: {
            const double* formants = nullptr;
            const double env = speechAt(speech[s], t, &formants);
            if (env < kSpeechFloor || formants == nullptr) {
                break;
            }
            const double peak = formantContourMaxDb(formants);
            const double levelDb = sig.dbm + kSpeechRowPeakOverTraceDb + 10.0 * std::log10(env) - peak;
            const double sign = sig.shape == SignalShape::Usb ? 1.0 : -1.0;
            const double fLow = sign > 0 ? sig.hz + kSsbLowHz : sig.hz - kSsbHighHz;
            const double fHigh = sign > 0 ? sig.hz + kSsbHighHz : sig.hz - kSsbLowHz;
            const int lo = static_cast<int>(std::floor((fLow - fftLow) / binWidth));
            const int hi = static_cast<int>(std::ceil((fHigh - fftLow) / binWidth));
            for (int i = std::max(0, lo); i <= std::min(binCount - 1, hi); ++i) {
                const double audio = sign * (fftLow + i * binWidth - sig.hz);
                if (audio >= kSsbLowHz && audio <= kSsbHighHz) {
                    mw[i] += dbmToMw(levelDb + formantContourDb(audio, formants));
                }
            }
            break;
        }
        case SignalShape::Cw: {
            const double frac = keyedFraction(keying[s], t - rowSec / 2.0, t + rowSec / 2.0);
            if (frac > 0.0) {
                addTone(mw, fftLow, binWidth, sig.hz,
                        dbmToMw(sig.dbm + kCwKeyDownOverTraceDb) * frac);
            }
            break;
        }
        case SignalShape::Ft8: {
            const long long cycle = static_cast<long long>(std::floor(t / kFt8CycleSec));
            const double inCycle = t - cycle * kFt8CycleSec - kFt8StartSec;
            const int symbol = static_cast<int>(std::floor(inCycle / kFt8SymbolSec));
            if (inCycle < 0.0 || symbol >= kFt8Symbols) {
                break;
            }
            const Ft8Cycle c = ft8Cycle(mixSeed(spec.seed, 0xF7u, static_cast<quint32>(s + 1)), cycle);
            if (!c.active) {
                break;
            }
            addTone(mw, fftLow, binWidth, sig.hz + c.symbols[symbol] * kFt8ToneStepHz,
                    dbmToMw(sig.dbm + c.levelDb));
            break;
        }
        }
    }
}

QVector<float> BandModel::traceFrame(double noiseOffsetDb) const
{
    const Impl& d = *m_impl;
    SeededRng noise(mixSeed(d.spec.seed, 0x7EACu, 1u));
    QVector<double> mw(d.binCount);
    for (int i = 0; i < d.binCount; ++i) {
        mw[i] = dbmToMw(d.spec.noiseDbm + noiseOffsetDb + kTraceJitterDb * noise.gaussian());
    }
    d.addSignalsTrace(mw);
    return toLinear(mw);
}

QVector<float> BandModel::rowFrame(int row, double noiseOffsetDb) const
{
    const Impl& d = *m_impl;
    const double t = d.newestSec - row * m_rowSec;
    SeededRng noise(mixSeed(d.spec.seed, 0x4E01u, static_cast<quint32>(row + 2)));
    const double floorMw = dbmToMw(d.spec.noiseDbm + noiseOffsetDb);
    QVector<double> mw(d.binCount);
    for (int i = 0; i < d.binCount; ++i) {
        mw[i] = floorMw * std::max(noise.exponential(), kRowNoiseMinFraction);
    }
    d.addSignalsRow(mw, t, m_rowSec);
    return toLinear(mw);
}

// ---- feeding ---------------------------------------------------------------

namespace {

double medianOf(QVector<float> v)
{
    if (v.isEmpty()) {
        return 0.0;
    }
    const auto mid = v.begin() + v.size() / 2;
    std::nth_element(v.begin(), mid, v.end());
    return *mid;
}

void settleTrace(SpectrumWidget* sw, const QVector<float>& frame)
{
    for (int i = 0; i < kTraceFrames; ++i) {
        sw->updateSpectrumLinear(0, frame, kWindowEnb, kDbmOffset);
    }
}

} // namespace

SpectrumCheck feedPan(SpectrumWidget* sw, const PanSpectrum& spec)
{
    SpectrumCheck check;
    check.panId = spec.panId;
    if (sw == nullptr) {
        return check;
    }
    sw->setWaterfallTickerPausedForTest(true);

    const int height = sw->waterfallHeightForTest();
    const int rows = height + 8 + kAgcSettleRows;
    const BandModel model(*sw, spec, rows);

    // The detector shows each pixel's strongest bin, so the displayed floor
    // sits above the bin mean. Settle once, measure, and move the mean so
    // the median of the settled trace lands on the stated floor.
    settleTrace(sw, model.traceFrame(0.0));
    double offset = spec.noiseDbm - medianOf(sw->renderedPixels());
    if (std::abs(offset) < kNoiseToleranceDb) {
        offset = 0.0;
    }

    // Oldest row first, one generated row per waterfall row. The GPU path
    // uploads only the rows written since its last frame, and no more than
    // one texture's height of them, so render a frame every few rows (the
    // live app pushes about one row per frame, so it never meets this).
    for (int r = rows - 1; r >= 0; --r) {
        sw->updateSpectrumLinear(0, model.rowFrame(r, offset), kWindowEnb, kDbmOffset);
        sw->tickWaterfallForTest();
#ifdef NEREUS_GPU_SPECTRUM
        if (r % kRowsPerRender == 0) {
            (void)sw->grabFramebuffer();
        }
#endif
    }
    settleTrace(sw, model.traceFrame(offset));

    check.centreHz = sw->centerFrequency();
    check.spanHz = sw->bandwidth();
    check.ddcCentreHz = sw->ddcCenterFrequency();
    check.sampleRateHz = sw->sampleRate();
    check.waterfallRows = rows;
    check.rowPeriodMs = model.rowPeriodSec() * 1000.0;
    check.noiseTargetDbm = spec.noiseDbm;
    check.noiseOffsetDb = offset;
    const QVector<float>& px = sw->renderedPixels();
    check.noiseDisplayedDbm = medianOf(px);

    // The check value: the trace's maximum over the strongest signal.
    const SignalSpec* strongest = nullptr;
    for (const SignalSpec& sig : spec.signalList) {
        if (strongest == nullptr || sig.dbm > strongest->dbm) {
            strongest = &sig;
        }
    }
    if (strongest == nullptr || px.isEmpty() || check.spanHz <= 0.0) {
        return check;
    }
    double lo = strongest->hz - 100.0;
    double hi = strongest->hz + 100.0;
    if (strongest->shape == SignalShape::Usb) {
        lo = strongest->hz + kSsbLowHz;
        hi = strongest->hz + kSsbHighHz;
    } else if (strongest->shape == SignalShape::Lsb) {
        lo = strongest->hz - kSsbHighHz;
        hi = strongest->hz - kSsbLowHz;
    } else if (strongest->shape == SignalShape::Ft8) {
        hi = strongest->hz + 8.0 * kFt8ToneStepHz + 100.0;
    }
    const double left = check.centreHz - check.spanHz / 2.0;
    const int n = px.size();
    const int x0 = std::clamp(static_cast<int>(std::floor((lo - left) / check.spanHz * n)), 0, n - 1);
    const int x1 = std::clamp(static_cast<int>(std::ceil((hi - left) / check.spanHz * n)), 0, n - 1);
    float best = -1000.0f;
    for (int x = x0; x <= x1; ++x) {
        best = std::max(best, px[x]);
    }
    check.targetHz = strongest->hz;
    check.targetDbm = strongest->dbm;
    check.displayedDbm = best;
    return check;
}

void feedTxPan(SpectrumWidget* sw, double carrierHz, bool usb, int lowHz, int highHz,
               double peakDbm, double floorDbm, quint32 seed)
{
    if (sw == nullptr) {
        return;
    }
    sw->setWaterfallTickerPausedForTest(true);
    const double span = sw->bandwidth();
    sw->setTxCenterFrequency(carrierHz);
    sw->setTxSampleRate(span);
    // One point per pixel, as the transmit analyzer is set up.
    const int pixels = std::max(800, sw->width());
    const double left = carrierHz - span / 2.0;
    const int height = sw->waterfallHeightForTest();
    const int rows = height + 8;
    const double rowSec = std::max(1, sw->wfUpdatePeriodMs()) / 1000.0;
    const QVector<Syllable> speech = speechTrack(mixSeed(seed, 0x7A02u, 1u),
                                                 kSceneTimeSec - rows * rowSec - kTrackLeadSec,
                                                 kSceneTimeSec + 1.0);
    const double traceMax = formantContourMaxDb(kTraceFormantHz);

    // row < 0: the settled trace; otherwise waterfall row `row`.
    auto frame = [&](int row) {
        SeededRng noise(mixSeed(seed, 0x7A01u, static_cast<quint32>(row + 2)));
        double levelDb = peakDbm - traceMax;
        const double* formants = kTraceFormantHz;
        bool silent = false;
        if (row >= 0) {
            const double* f = nullptr;
            const double env = speechAt(speech, kSceneTimeSec - row * rowSec, &f);
            silent = env < kSpeechFloor || f == nullptr;
            if (!silent) {
                formants = f;
                levelDb = peakDbm + 10.0 * std::log10(env) - formantContourMaxDb(f);
            }
        }
        QVector<float> px(pixels);
        for (int x = 0; x < pixels; ++x) {
            const double f = left + (x + 0.5) * span / pixels;
            const double audio = usb ? f - carrierHz : carrierHz - f;
            const double noiseDb = row >= 0 ? 10.0 * std::log10(std::max(noise.exponential(), kRowNoiseMinFraction))
                                            : kTraceJitterDb * noise.gaussian();
            double mw = dbmToMw(floorDbm + noiseDb);
            if (!silent && audio >= lowHz && audio <= highHz) {
                mw += dbmToMw(levelDb + formantContourDb(
                    std::clamp(audio, kSsbLowHz, kSsbHighHz), formants));
            }
            px[x] = static_cast<float>(10.0 * std::log10(mw));
        }
        return px;
    };

    for (int r = rows - 1; r >= 0; --r) {
        sw->pushTxWaterfallRow(-1, frame(r));
        sw->tickWaterfallForTest();
#ifdef NEREUS_GPU_SPECTRUM
        if (r % kRowsPerRender == 0) {
            (void)sw->grabFramebuffer();
        }
#endif
    }
    sw->updateSpectrumFromTxPixels(-1, frame(-1));
}

QJsonArray stopTimersUnder(QObject* root)
{
    QJsonArray stopped;
    if (root == nullptr) {
        return stopped;
    }
    const QList<QTimer*> timers = root->findChildren<QTimer*>();
    for (QTimer* timer : timers) {
        if (!timer->isActive()) {
            continue;
        }
        QJsonObject entry;
        entry[QStringLiteral("parent")] = timer->parent() != nullptr
            ? QString::fromLatin1(timer->parent()->metaObject()->className())
            : QString();
        entry[QStringLiteral("name")] = timer->objectName();
        entry[QStringLiteral("interval_ms")] = timer->interval();
        timer->stop();
        stopped.append(entry);
    }
    return stopped;
}

QJsonArray freezeReadouts(MainWindow* window)
{
    QJsonArray frozen;
    if (window == nullptr) {
        return frozen;
    }
    if (auto* title = window->findChild<TitleBar*>()) {
        const QString live = title->utcText();
        for (QLabel* label : title->findChildren<QLabel*>()) {
            if (!live.isEmpty() && label->text() == live) {
                label->setText(QString::fromLatin1(kFrozenUtc));
                frozen.append(QStringLiteral("utc-clock: %1").arg(QString::fromLatin1(kFrozenUtc)));
                break;
            }
        }
        if (ConnectionSegment* seg = title->connectionSegment()) {
            seg->setPulsePausedForTest(true);
            seg->setRates(kFrozenRxMbps, kFrozenTxMbps);
            for (int i = 0; i < 3; ++i) {
                seg->setRttMs(kFrozenRttMs);
            }
            seg->setAudioFlowState(AudioEngine::FlowState::Healthy);
            frozen.append(QStringLiteral("connection-segment: pulse held, rates %1/%2 Mbps, RTT %3 ms, audio healthy")
                              .arg(kFrozenRxMbps).arg(kFrozenTxMbps).arg(kFrozenRttMs));
        }
    }
    if (auto* tile = window->findChild<SystemTile*>()) {
        tile->setCpuPercent(kFrozenCpuPercent);
        frozen.append(QStringLiteral("cpu: %1%").arg(kFrozenCpuPercent));
    }
    return frozen;
}

namespace {

// Every visible widget under the roots, with where it sits, as one string:
// two equal strings are an unmoved layout.
QString layoutSignature(const QList<QWidget*>& roots)
{
    QString sig;
    for (QWidget* root : roots) {
        if (root == nullptr || !root->isVisible()) {
            continue;
        }
        const QRect g = root->geometry();
        sig += QStringLiteral("%1:%2,%3 %4x%5;").arg(QString::fromLatin1(root->metaObject()->className()))
                   .arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
        for (QWidget* w : root->findChildren<QWidget*>()) {
            if (w->isWindow() || !w->isVisible()) {
                continue;
            }
            const QPoint at = w->mapTo(root, QPoint(0, 0));
            sig += QStringLiteral("%1,%2 %3x%4;").arg(at.x()).arg(at.y()).arg(w->width()).arg(w->height());
        }
    }
    return sig;
}

// The single-shot timers under the roots still to fire within
// kPendingTimerMaxMs: work a debounce or a deferred step has yet to do.
QStringList pendingTimers(const QList<QWidget*>& roots)
{
    QStringList pending;
    for (QWidget* root : roots) {
        if (root == nullptr) {
            continue;
        }
        for (QTimer* timer : root->findChildren<QTimer*>()) {
            if (timer->isActive() && timer->isSingleShot() && timer->interval() <= kPendingTimerMaxMs) {
                pending.append(QStringLiteral("%1 %2 %3 ms").arg(
                    timer->parent() != nullptr ? QString::fromLatin1(timer->parent()->metaObject()->className())
                                               : QString(),
                    timer->objectName()).arg(timer->interval()));
            }
        }
    }
    return pending;
}

} // namespace

LayoutQuiet waitForQuietLayout(const QList<QWidget*>& roots, int maxMs)
{
    LayoutQuiet q;
    QElapsedTimer total;
    total.start();
    QElapsedTimer sinceChange;
    sinceChange.start();
    QString last = layoutSignature(roots);
    int quietPolls = 0;
    while (true) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        const QString now = layoutSignature(roots);
        if (now != last) {
            last = now;
            ++q.moves;
            quietPolls = 0;
            sinceChange.restart();
        } else {
            ++quietPolls;
        }
        const QStringList pending = pendingTimers(roots);
        if (pending.isEmpty() && quietPolls >= kQuietLayoutPolls && sinceChange.elapsed() >= kQuietLayoutMs) {
            q.quiet = true;
            break;
        }
        if (total.elapsed() >= maxMs) {
            q.pending = pending;
            if (quietPolls < kQuietLayoutPolls || sinceChange.elapsed() < kQuietLayoutMs) {
                q.pending.append(QStringLiteral("geometry moved %1 ms ago").arg(sinceChange.elapsed()));
            }
            break;
        }
    }
    q.waitedMs = total.elapsed();
    return q;
}

} // namespace NereusSDR::Preview
