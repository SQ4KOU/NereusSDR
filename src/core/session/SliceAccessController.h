#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/SliceAccessController.h  (NereusSDR)
// =================================================================
//
// Listen in, stop listening, take control and release (slice control and
// shared listening plan Task 4; the design, docs/architecture/2026-09-28-
// slice-control-and-listening-design.md, "Approved behavior"; G-118).
//
// One place holds the checks and the change for each of these, so a
// remote device's command (slice.listen, slice.stopListening,
// slice.takeControl, slice.release, and setActiveSliceById on a slice it
// listens to) and the hosting desktop's own window (Task 10) run the same
// code. Each call names the slice by its SliceRef (id and incarnation), so
// a command never reaches a letter made again after a close, and take and
// release also carry the control revision the device saw, so exactly one
// of two devices that saw the same controller acts.
//
//   listen        joins the slice as a listener. Nothing is allocated (no
//                 slice, receiver stream or DDC); it works at full
//                 capacity. Already joined is accepted and changes
//                 nothing.
//   stopListening leaves it. Refused from its controller (ruling Q5: the
//                 controller uses Release). The device's receive choice
//                 moves to its next joined slice. A slice left with nobody
//                 on it closes, except the last slice on the Core.
//   takeControl   makes the device its controller, in one mark change: no
//                 slice is removed or made, its DSP channel and audio
//                 sources stay. The former controller stays a listener and
//                 is told (controlTaken). Refused while the slice
//                 transmits (checked here, at the change), and when its
//                 controller cannot stay on as a listener (ruling Q7). The
//                 former controller's transmit selection of the slice is
//                 cleared first (ruling Q8).
//   release       the controller only: control is cleared and it leaves.
//                 The slice stays for its other listeners, with no
//                 controller (ruling Q9: nobody adopts it; Take control
//                 does), or closes when nobody else is on it (the last
//                 slice on the Core stays). Refused while the slice
//                 transmits whenever it stays. The Core's other close paths
//                 act as this from a controller others listen with
//                 (ruling Q6).
//   selectRx      the device's active receive slice, any joined slice
//                 (RadioModel::setActiveRxFor).
//
// What only the Core's session server knows (who is transmitting, which
// device can stay on as a listener, the transmit selection, closing a
// slice) comes in through Hooks.
//
// Single thread: RadioModel's.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-28: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 4,
//               with AI-assisted implementation via Anthropic Claude Code.
//   2026-09-28: fix wave for the Tasks 1-4 review by J.J. Boyd (KG4VCF):
//               a take clears the transmit selection of the slice for any
//               holder, and a release that keeps the Core's last slice is
//               refused while it transmits. AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include "core/SliceOwnership.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>

namespace NereusSDR {

class RadioModel;

class SliceAccessController : public QObject {
    Q_OBJECT

public:
    struct Result {
        bool accepted = false;
        /// Plain words when refused.
        QString reason;
        /// The slice's control revision afterwards (listen, takeControl).
        quint64 controlRevision = 0;
        /// The object keys the change reached.
        QList<QByteArray> affected;
    };

    struct Hooks {
        /// Whether the slice is transmitting now: the transmit slice of a
        /// holder on the air, or the one the station freeze holds.
        std::function<bool(int sliceId)> transmitting;
        /// Why control of the slice cannot pass from `controller` (it
        /// cannot stay on as a listener: an older window, a device that is
        /// away, the Core's own position), or empty when it can.
        std::function<QString(const QByteArray& controller, int sliceId)> cannotHandOff;
        /// Ruling Q8: clears `former`'s transmit selection of the slice
        /// before control passes from it (`former` is empty for a slice
        /// with no controller), and that of any device holding transmit
        /// with the flag on the slice.
        std::function<void(const QByteArray& former, int sliceId)> clearTransmitSelection;
        /// Ruling Q8: `taker` took the slice; its transmit binding does not
        /// pick the slice up by itself.
        std::function<void(const QByteArray& taker, int sliceId)> tookControl;
        /// Closes a slice nobody is on; false when it stays (the Core's
        /// last slice).
        std::function<bool(int sliceId)> close;
    };

    SliceAccessController(RadioModel* radio, Hooks hooks, QObject* parent = nullptr);

    Result listen(const QByteArray& device, SliceOwnership::SliceRef ref);
    Result stopListening(const QByteArray& device, SliceOwnership::SliceRef ref);
    Result takeControl(const QByteArray& device, SliceOwnership::SliceRef ref,
                       quint64 expectedRevision);
    Result release(const QByteArray& device, SliceOwnership::SliceRef ref,
                   quint64 expectedRevision);
    Result selectRx(const QByteArray& device, int sliceId);

    /// Whether a close of `sliceId` from `device` is a release (ruling Q6):
    /// it controls the slice and another device is joined to it.
    bool closeIsRelease(const QByteArray& device, int sliceId) const;

    /// The letter of `sliceId`, as the words name it.
    static QString letterOf(int sliceId);

signals:
    /// `byDevice` took control of the slice from `fromDevice`, which is
    /// still listening.
    void controlTaken(int sliceId, const QByteArray& fromDevice, const QByteArray& byDevice);

private:
    SliceOwnership* ownership() const;
    static Result refused(const QString& reason);
    static QList<QByteArray> keysOf(int sliceId);
    /// Closes the slice when nobody is on it any more.
    void closeIfNobodyIsOn(int sliceId);

    QPointer<RadioModel> m_radio;
    Hooks m_hooks;
};

} // namespace NereusSDR
