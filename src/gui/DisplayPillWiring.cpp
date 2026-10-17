// =================================================================
// src/gui/DisplayPillWiring.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. See DisplayPillWiring.h.
//
// Modification history (NereusSDR):
//   2026-10-10 - Created. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-10-11 - The "Sizing:" row's request reaches applyFftSizingMode
//                 (design D38). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#include "gui/DisplayPillWiring.h"

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QWidget>

#include <memory>
#include <utility>

#include "gui/SpectrumWidget.h"

namespace NereusSDR {

namespace {

// Runs `shown` each time the watched widget is shown.
class ShowWatcher : public QObject {
public:
    ShowWatcher(QObject* parent, std::function<void()> shown)
        : QObject(parent), m_shown(std::move(shown)) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Show && m_shown) {
            m_shown();
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_shown;
};

} // namespace

void wireDisplayPill(SpectrumOverlayPanel* panel, SpectrumWidget* widget,
                     const DisplayPillHooks& hooks)
{
    if (!panel || !widget) { return; }

    // The per-pan rows read and write this pan themselves.
    panel->setDisplayTarget(widget);

    // One copy of the hooks, kept for as long as the strip's connections.
    const auto kept = std::make_shared<const DisplayPillHooks>(hooks);
    const QPointer<SpectrumOverlayPanel> strip(panel);

    // The FFT tab shows the station, not what was asked: after a request
    // that was taken it reads the same, after one that was refused it goes
    // back.
    const auto showFftState = [kept, strip]() {
        if (strip && kept->fftState) {
            strip->setFftState(kept->fftState());
        }
    };

    QObject::connect(panel, &SpectrumOverlayPanel::fftBaselineRequested, panel,
                     [kept, showFftState](int size) {
        if (kept->applyFftBaseline) { kept->applyFftBaseline(size); }
        showFftState();
    });
    QObject::connect(panel, &SpectrumOverlayPanel::fftSizingModeRequested, panel,
                     [kept, showFftState](ControlRanges::DisplayFftSizingMode mode,
                                          double hzPerBinIfNoneRemembered) {
        if (kept->applyFftSizingMode) {
            kept->applyFftSizingMode(mode, hzPerBinIfNoneRemembered);
        }
        showFftState();
    });
    QObject::connect(panel, &SpectrumOverlayPanel::fftFpsRequested, panel,
                     [kept, showFftState](int fps) {
        if (kept->applyFps) { kept->applyFps(fps); }
        showFftState();
    });
    QObject::connect(panel, &SpectrumOverlayPanel::fftWindowRequested, panel,
                     [kept, showFftState](int windowType) {
        if (kept->applyWindowType) { kept->applyWindowType(windowType); }
        showFftState();
    });
    QObject::connect(panel, &SpectrumOverlayPanel::hzPerBinTargetRequested, panel,
                     [kept, showFftState](double hz) {
        if (kept->applyHzPerBinTarget) { kept->applyHzPerBinTarget(hz); }
        showFftState();
    });
    QObject::connect(panel, &SpectrumOverlayPanel::decimationRequested, panel,
                     [kept, showFftState](int decimation) {
        if (kept->applyDecimation) { kept->applyDecimation(decimation); }
        showFftState();
    });

    // "Reset this pan": the pan's own appearance keys go; the station's
    // settings are not the pan's and stay (R-DSP-09).
    const QPointer<SpectrumWidget> pan(widget);
    QObject::connect(panel, &SpectrumOverlayPanel::resetPanRequested, panel, [pan]() {
        if (pan) { pan->resetPanDisplayOverrides(); }
    });

    QObject::connect(panel, &SpectrumOverlayPanel::openSetupRequested, panel,
                     [kept](const QString& page) {
        if (kept->openSetupPage) { kept->openSetupPage(page); }
    });
    QObject::connect(panel, &SpectrumOverlayPanel::clarityEnabledRequested, panel,
                     [kept](bool on) {
        if (kept->setClarityEnabled) { kept->setClarityEnabled(on); }
    });
    QObject::connect(panel, &SpectrumOverlayPanel::clarityRetuneRequested, panel, [kept]() {
        if (kept->retuneClarity) { kept->retuneClarity(); }
    });
    QObject::connect(panel, &SpectrumOverlayPanel::perBandDbMaxRequested, panel,
                     [kept](int db) {
        if (kept->setPerBandDbMax) { kept->setPerBandDbMax(db); }
    });
    QObject::connect(panel, &SpectrumOverlayPanel::perBandDbMinRequested, panel,
                     [kept](int db) {
        if (kept->setPerBandDbMin) { kept->setPerBandDbMin(db); }
    });

    // The strip builds the pill on its own parent (the pan). Opening it
    // reads the station again, so a pill opened after Setup or another
    // window changed an FFT setting shows the value in force.
    if (QWidget* host = panel->parentWidget()) {
        if (QWidget* pill = host->findChild<QWidget*>(QStringLiteral("displayPill"),
                                                      Qt::FindDirectChildrenOnly)) {
            pill->installEventFilter(new ShowWatcher(panel, showFftState));
        }
    }

    showFftState();
}

} // namespace NereusSDR
