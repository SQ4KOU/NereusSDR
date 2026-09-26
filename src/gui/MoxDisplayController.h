// =================================================================
// src/gui/MoxDisplayController.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original. The one owner of what a window's pan
// does while its radio is keyed, for a window running its own DSP and for a
// remote window alike. It cites Thetis's display for the behaviour it
// follows; the widget work it orders already lives in SpectrumWidget and
// the transmit sources, and it translates no Thetis code.
//
// What it follows, Thetis v2.10.3.15:
//   - display.cs:1575-1597 (Display.MOX): the MOX edge swaps the cached
//     waterfall minimum and purges the buffers (here: setMoxOverlay's grid
//     and span swap, the waterfall AGC re-prime and the averaging cleared);
//   - display.cs:1782-1790 (SpectrumGridMaxMoxModified): the transmit grid
//     while keyed (setMoxOverlay loads it);
//   - display.cs:6420-6427: the waterfall's own transmit levels
//     (TXWFAmpMin / TXWFAmpMax, -70 and 30 at display.cs:1917-1937), the
//     transmit colour scheme and the transmit low colour while keyed (the
//     widget's m_moxOverlay branch);
//   - console.cs:24281-24338 (DisplayThread): the transmitting receiver's
//     display takes the transmit analyzer, never the receiver;
//   - console.cs:22069-22150 (getLowHighForRXn): the display follows XIT
//     while transmitting (carrierChanged).
//
// Modification history (NereusSDR):
//   2026-09-26 : Created for remote-window parity Task 29 (A11, R-R3-49,
//                 R-R3-12, verification row 16) by J.J. Boyd (KG4VCF): the
//                 rise and fall MainWindow's MOX lambda made, moved here so
//                 a remote window makes the same ones. AI-assisted
//                 implementation via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QMetaObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

namespace NereusSDR {

class ITxDisplaySource;
class PanadapterStack;
class RadioModel;
class SliceModel;
class SpectrumWidget;
class StationClient;

class MoxDisplayController : public QObject {
    Q_OBJECT

public:
    MoxDisplayController(PanadapterStack* pans, RadioModel* model, QObject* parent);
    ~MoxDisplayController() override;

    /// Not owned. Set before the first key; a source set while keyed takes
    /// over at the next rise.
    void setSource(ITxDisplaySource* source);
    ITxDisplaySource* source() const { return m_source; }

    /// The rise and fall. On the rise, on the pan hosting slice
    /// `txSliceId` (never the active pan, never a fallback): the MOX
    /// overlay (red border, transmit grid, palette and waterfall levels),
    /// the receive rate and DDC centre saved, the view moved to the
    /// carrier, Clarity paused, the external transmit waterfall on, the
    /// waterfall AGC re-primed and the averaging cleared, then the source's
    /// transmit view. The fall undoes each on the pan recorded at the rise.
    void setKeyed(bool keyed, int txSliceId);
    bool isKeyed() const { return m_keyed; }

    /// The pan showing the transmit display; empty unless keyed.
    QString transmitPanId() const { return m_panId; }

    /// Row 16: the transmit carrier moved while keyed (XIT, a retune). The
    /// view, the transmit filter overlay and the trace move with it.
    void carrierChanged(double carrierHz);
    double carrierHz() const { return m_carrierHz; }

    /// The high-SWR border (with fold-back when latched) on the
    /// transmitting pan, as RadioModel drives it on a local window.
    void setHighSwr(bool highSwr, bool windBackLatched);

    /// A window running its own DSP: follows MoxController::moxStateChanged
    /// with RadioModel::txBoundSlice().
    void followLocalRadio();
    /// A remote window: follows the Core's `txState` (keyed, txSliceId,
    /// highSwr, swrWindBackLatched) or, on a Core that sends none, the
    /// mirrored radio.transmitting and the slice whose txSlice is true.
    void followStation(StationClient* client);

    /// Test seam: record each widget call of the rise and the fall, in
    /// order (the source's too), into callLog().
    void setCallRecording(bool on);
    QStringList callLog() const { return m_callLog; }
    void clearCallLog() { m_callLog.clear(); }

private:
    void rise(int txSliceId);
    void fall();
    void onViewWindowChanged(double centreHz, double bandwidthHz);
    void applyHighSwr();
    void note(const QString& call);

    QPointer<PanadapterStack> m_pans;
    QPointer<RadioModel> m_model;
    ITxDisplaySource* m_source{nullptr};

    bool m_keyed{false};
    QString m_panId;
    QPointer<SpectrumWidget> m_pan;
    QPointer<SliceModel> m_slice;
    double m_carrierHz{0.0};
    // The receive-side bin mapping, saved on the rise: the receive rate is
    // the wire DDC rate while the transmit display's is its own window, and
    // the DDC may sit off the VFO under CTUN.
    double m_savedSampleRate{0.0};
    double m_savedDdcHz{0.0};
    QMetaObject::Connection m_viewConnection;

    bool m_highSwr{false};
    bool m_windBackLatched{false};
    QPointer<SpectrumWidget> m_swrPan;

    bool m_recording{false};
    QStringList m_callLog;
};

} // namespace NereusSDR
