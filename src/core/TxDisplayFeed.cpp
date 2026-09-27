// =================================================================
// src/core/TxDisplayFeed.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See TxDisplayFeed.h.
//
// Modification history (NereusSDR):
//   2026-09-26 : Created for remote-window parity Task 28 (R-R3-49, A11,
//                 R-IOS-13) by J.J. Boyd (KG4VCF). AI-assisted
//                 implementation via Anthropic Claude Code.
//   2026-09-26 : Tasks 27-29 fix wave (R-R3-49): remote viewer changes
//                 coalesced while keyed; one SetAnalyzer per view. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/TxDisplayFeed.h"

#include "core/MoxController.h"
#include "core/TxChannel.h"
#include "core/TxSliceArbiter.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <cmath>

namespace NereusSDR {

namespace {

// The siphon's baseband: the TX DSP rate, 96 kHz (WdspEngine::
// kTxDspSampleRate), either side of the carrier.
constexpr int kHalfBasebandHz = 48000;

} // namespace

TxDisplayFeed::TxDisplayFeed(RadioModel* model, TxAnalyzer* analyzer, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_analyzer(analyzer)
{
    m_remoteViewTimer.setSingleShot(true);
    connect(&m_remoteViewTimer, &QTimer::timeout, this, [this]() {
        m_remoteApplied.restart();
        recompute();
    });
    if (m_model.isNull() || m_analyzer.isNull()) {
        return;
    }
    m_view.carrierHz = carrierHz();
    m_view.lowHz = kDefaultLowHz;
    m_view.highHz = kDefaultHighHz;
    m_view.pixels = m_analyzer->numPixels();

    // Every key, whatever keyed it (a MOX click, a hardware PTT, TUNE, VOX,
    // a remote device): MoxController::moxStateChanged is the one edge.
    if (MoxController* mox = m_model->moxController()) {
        connect(mox, &MoxController::moxStateChanged, this,
                &TxDisplayFeed::onMoxStateChanged);
    }
    if (TxSliceArbiter* arbiter = m_model->txSliceArbiter()) {
        connect(arbiter, &TxSliceArbiter::txBoundSliceChanged, this, [this](int, int) {
            if (m_keyed) {
                watchTransmitSlice();
                recompute();
            }
        });
    }
    // The analyzer runs only while keyed; a poll that lands after the fall
    // is not the transmit display any more.
    connect(m_analyzer, &TxAnalyzer::txFftReady, this,
            [this](int, const QVector<float>& dbm) {
        if (m_keyed) {
            emit traceReady(dbm);
        }
    });
    connect(m_analyzer, &TxAnalyzer::txWaterfallReady, this,
            [this](int, const QVector<float>& dbm) {
        if (m_keyed) {
            emit waterfallReady(dbm);
        }
    });
}

TxDisplayFeed::~TxDisplayFeed() = default;

int TxDisplayFeed::addViewer(double centreHz, double spanHz, int pixels, bool local)
{
    const int id = m_nextViewerId++;
    Viewer viewer;
    // Held relative to the carrier, so the view moves with it (XIT, a
    // retune of the transmit slice) as Thetis's does (getLowHighForRXn).
    viewer.centreHz = centreHz - carrierHz();
    viewer.spanHz = spanHz;
    viewer.pixels = pixels;
    viewer.local = local;
    m_viewers.emplace(id, viewer);
    recompute();
    return id;
}

void TxDisplayFeed::updateViewer(int id, double centreHz, double spanHz, int pixels)
{
    auto it = m_viewers.find(id);
    if (it == m_viewers.end()) {
        return;
    }
    it->second.centreHz = centreHz - carrierHz();
    it->second.spanHz = spanHz;
    it->second.pixels = pixels;
    if (!m_keyed || it->second.local) {
        recompute();
        return;
    }
    // A remote viewer while keyed: at most one change per coalescing
    // period reaches the analyzer; a held one follows when it ends, with
    // the latest values (the viewer's own, stored above).
    if (m_remoteViewTimer.isActive()) {
        return;
    }
    const qint64 sinceMs = m_remoteApplied.isValid() ? m_remoteApplied.elapsed()
                                                     : qint64(kRemoteViewCoalesceMs);
    if (sinceMs >= kRemoteViewCoalesceMs) {
        m_remoteApplied.restart();
        recompute();
        return;
    }
    m_remoteViewTimer.start(int(kRemoteViewCoalesceMs - sinceMs));
}

void TxDisplayFeed::removeViewer(int id)
{
    if (m_viewers.erase(id) == 0) {
        return;
    }
    recompute();
}

bool TxDisplayFeed::isGoverning(int id) const
{
    return id != 0 && id == m_governor;
}

int TxDisplayFeed::governingViewer() const
{
    return m_governor;
}

int TxDisplayFeed::fftSize() const
{
    return m_analyzer ? m_analyzer->fftSize() : 0;
}

int TxDisplayFeed::outputFps() const
{
    return m_analyzer ? m_analyzer->outputFps() : 0;
}

double TxDisplayFeed::carrierHz() const
{
    if (m_model.isNull()) {
        return m_view.carrierHz;
    }
    SliceModel* slice = m_model->txBoundSlice();
    if (slice == nullptr) {
        return m_view.carrierHz;
    }
    // The transmitter's own number (dial plus XIT when XIT is on), so the
    // display and the transmitter cannot disagree about the carrier.
    return static_cast<double>(m_model->txFrequencyForSlice(slice));
}

void TxDisplayFeed::onMoxStateChanged(bool keyed)
{
    if (m_analyzer.isNull() || keyed == m_keyed) {
        return;
    }
    if (keyed) {
        m_keyed = true;
        // SetAnalyzer's bf_sz is the TX channel's DSP block (TxAnalyzer::
        // setBlockSize), as DaemonApp and MainWindow set it on every key.
        if (m_model) {
            if (TxChannel* txc = m_model->txChannel()) {
                m_analyzer->setBlockSize(txc->dspBlockFrames());
            }
        }
        watchTransmitSlice();
        recompute();
        m_analyzer->start();
        emit keyedChanged(true);
        return;
    }
    m_analyzer->stop();
    m_remoteViewTimer.stop();
    m_remoteApplied.invalidate();
    // Clear the clip so the next key starts from the full baseband, as the
    // local window's fall does.
    m_analyzer->setSpectrumWindow(0, 0);
    m_keyed = false;
    for (const QMetaObject::Connection& connection : std::as_const(m_sliceConnections)) {
        disconnect(connection);
    }
    m_sliceConnections.clear();
    m_watchedSlice.clear();
    emit keyedChanged(false);
}

void TxDisplayFeed::watchTransmitSlice()
{
    for (const QMetaObject::Connection& connection : std::as_const(m_sliceConnections)) {
        disconnect(connection);
    }
    m_sliceConnections.clear();
    m_watchedSlice = m_model ? m_model->txBoundSlice() : nullptr;
    if (m_watchedSlice.isNull()) {
        return;
    }
    // Row 16: the carrier follows the transmit slice's frequency and XIT
    // while keyed (Thetis console.cs:22138-22150 [v2.10.3.15], "xit, only
    // when txing").
    SliceModel* slice = m_watchedSlice.data();
    m_sliceConnections.append(connect(slice, &SliceModel::frequencyChanged, this,
                                      [this](double) { recompute(); }));
    m_sliceConnections.append(connect(slice, &SliceModel::xitEnabledChanged, this,
                                      [this](bool) { recompute(); }));
    m_sliceConnections.append(connect(slice, &SliceModel::xitHzChanged, this,
                                      [this](int) { recompute(); }));
}

void TxDisplayFeed::recompute()
{
    if (m_analyzer.isNull()) {
        return;
    }
    // The local viewer governs whenever there is one; otherwise the lowest
    // id. std::map orders by id.
    int governor = 0;
    for (const auto& [id, viewer] : m_viewers) {
        if (viewer.local) {
            governor = id;
            break;
        }
    }
    if (governor == 0 && !m_viewers.empty()) {
        governor = m_viewers.begin()->first;
    }

    const double carrier = carrierHz();
    TxDisplayView view;
    view.carrierHz = carrier;
    if (governor != 0) {
        Viewer& viewer = m_viewers.at(governor);
        view = TxAnalyzer::clampViewToBaseband(carrier, carrier + viewer.centreHz,
                                               viewer.spanHz, viewer.pixels);
        if (view.empty()) {
            // A span under 1000 Hz keeps the last good view.
            view = viewer.lastGood;
            if (view.empty()) {
                view.lowHz = kDefaultLowHz;
                view.highHz = kDefaultHighHz;
            }
            view.carrierHz = carrier;
            view.pixels = viewer.pixels;
        }
        viewer.lastGood = view;
    } else {
        // Nobody asked: the analyzer's own window (a local window that has
        // not moved onto the feed yet still sets it itself).
        view.lowHz = m_analyzer->spectrumWindowLowHz();
        view.highHz = m_analyzer->spectrumWindowHighHz();
        if (view.empty()) {
            view.lowHz = -kHalfBasebandHz;
            view.highHz = kHalfBasebandHz;
        }
        view.pixels = m_analyzer->numPixels();
    }

    if (m_keyed && governor != 0) {
        m_analyzer->setView(view.lowHz, view.highHz, view.pixels);
    }

    const bool governorMoved = governor != m_governor;
    m_governor = governor;
    const bool viewMoved = !(view == m_view);
    m_view = view;
    if (governorMoved) {
        emit governorChanged(governor);
    }
    if (viewMoved && m_keyed) {
        emit viewChanged(view);
    }
}

} // namespace NereusSDR
