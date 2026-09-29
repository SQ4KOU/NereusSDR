// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/SliceAccessController.cpp  (NereusSDR)
// =================================================================
//
// Slice control and shared listening plan Task 4: listen in, stop
// listening, take control and release, checked and applied in one place.
// See SliceAccessController.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-28: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 4,
//               with AI-assisted implementation via Anthropic Claude Code.
//   2026-09-28: fix wave for the Tasks 1-4 review by J.J. Boyd (KG4VCF):
//               a take clears the transmit selection for a slice with no
//               controller too, and a release of the Core's last slice is
//               refused while it transmits. AI-assisted via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/SliceAccessController.h"

#include "core/session/ObjectRegistry.h"
#include "core/session/SliceAccessPolicy.h"
#include "core/session/SliceAccessSet.h"
#include "models/RadioModel.h"

namespace NereusSDR {

namespace {

// The words a device is refused with. Plain operator words (link section
// 18.3's style); the letter is the only thing inserted.
QString closedReason()
{
    return QStringLiteral("That slice has closed. Choose it again from the list.");
}

QString changedReason(const QString& letter)
{
    return QStringLiteral("Someone else changed who controls slice %1. Look again and try "
                          "once more.")
        .arg(letter);
}

QString takeWhileTransmittingReason(const QString& letter)
{
    return QStringLiteral("Slice %1 is transmitting. Take control once it stops.").arg(letter);
}

QString releaseWhileTransmittingReason(const QString& letter)
{
    return QStringLiteral("Slice %1 is transmitting. Release it once it stops.").arg(letter);
}

QString controllerStopsReason(const QString& letter)
{
    return QStringLiteral("You control slice %1. Use Release to leave it.").arg(letter);
}

QString notControllerReason(const QString& letter)
{
    return QStringLiteral("Only the device that controls slice %1 can release it.").arg(letter);
}

QString notListeningReason(const QString& letter)
{
    return QStringLiteral("You are not listening to slice %1. Listen in first.").arg(letter);
}

QString goneReason()
{
    return QStringLiteral("That receiver is no longer on the Core.");
}

QString notReadyReason()
{
    return QStringLiteral("The Core has no radio ready.");
}

} // namespace

SliceAccessController::SliceAccessController(RadioModel* radio, Hooks hooks, QObject* parent)
    : QObject(parent)
    , m_radio(radio)
    , m_hooks(std::move(hooks))
{
}

QString SliceAccessController::letterOf(int sliceId)
{
    return QString(QChar(QLatin1Char('A').unicode() + sliceId));
}

SliceOwnership* SliceAccessController::ownership() const
{
    return m_radio ? m_radio->sliceOwnership() : nullptr;
}

SliceAccessController::Result SliceAccessController::refused(const QString& reason)
{
    Result result;
    result.reason = reason;
    return result;
}

QList<QByteArray> SliceAccessController::keysOf(int sliceId)
{
    return {ObjectRegistry::keyForSlice(sliceId), SliceAccessSet::keyFor(sliceId)};
}

bool SliceAccessController::closeIsRelease(const QByteArray& device, int sliceId) const
{
    const SliceOwnership* own = ownership();
    if (own == nullptr || device.isEmpty() || !own->isLive(sliceId)
        || !SliceAccessPolicy::mayChange(*own, device, sliceId)) {
        return false;
    }
    for (const QByteArray& other : own->listenersOf(sliceId)) {
        if (other != device) {
            return true;
        }
    }
    return false;
}

void SliceAccessController::closeIfNobodyIsOn(int sliceId)
{
    SliceOwnership* own = ownership();
    if (own == nullptr || !own->isLive(sliceId) || !own->mark(sliceId).owner.isEmpty()
        || !own->listenersOf(sliceId).isEmpty()) {
        return;
    }
    // The Core's last slice stays until the zero-slice work lifts that
    // guard (plan Task 7); it is then a slice nobody is on.
    if (m_hooks.close) {
        m_hooks.close(sliceId);
    }
}

SliceAccessController::Result SliceAccessController::listen(const QByteArray& device,
                                                            SliceOwnership::SliceRef ref)
{
    SliceOwnership* own = ownership();
    if (own == nullptr) {
        return refused(notReadyReason());
    }
    if (device.isEmpty() || !own->matches(ref)) {
        return refused(closedReason());
    }
    // Nothing is allocated: joining changes only who hears and sees it.
    // Already joined is the same answer with no change.
    if (!own->join(device, ref.sliceId)) {
        return refused(closedReason());
    }
    Result result;
    result.accepted = true;
    result.controlRevision = own->controlRevision(ref.sliceId);
    result.affected = keysOf(ref.sliceId);
    return result;
}

SliceAccessController::Result SliceAccessController::stopListening(const QByteArray& device,
                                                                   SliceOwnership::SliceRef ref)
{
    SliceOwnership* own = ownership();
    if (own == nullptr) {
        return refused(notReadyReason());
    }
    if (device.isEmpty() || !own->matches(ref)) {
        return refused(closedReason());
    }
    const int sliceId = ref.sliceId;
    // Ruling Q5: the controller releases instead.
    if (SliceAccessPolicy::mayChange(*own, device, sliceId)) {
        return refused(controllerStopsReason(letterOf(sliceId)));
    }
    Result result;
    result.accepted = true;
    result.affected = keysOf(sliceId);
    if (!own->isListening(device, sliceId)) {
        // Not joined (left already): nothing to leave.
        return result;
    }
    // The device's receive choice moves to its next joined slice, in
    // creation order after this one (wrapping), when this was it.
    int next = -1;
    if (own->activeRxFor(device) == sliceId) {
        const QList<int> joined = own->joinedBy(device);
        const qsizetype at = joined.indexOf(sliceId);
        if (at >= 0 && joined.size() > 1) {
            next = joined.at((at + 1) % joined.size());
        }
    }
    own->leave(device, sliceId);
    if (next >= 0) {
        own->setActiveRx(device, next);
    }
    closeIfNobodyIsOn(sliceId);
    return result;
}

SliceAccessController::Result SliceAccessController::takeControl(const QByteArray& device,
                                                                 SliceOwnership::SliceRef ref,
                                                                 quint64 expectedRevision)
{
    SliceOwnership* own = ownership();
    if (own == nullptr) {
        return refused(notReadyReason());
    }
    if (device.isEmpty() || !own->matches(ref)) {
        return refused(closedReason());
    }
    const int sliceId = ref.sliceId;
    const QString letter = letterOf(sliceId);
    if (own->controlRevision(sliceId) != expectedRevision) {
        return refused(changedReason(letter));
    }
    const QByteArray former = own->mark(sliceId).owner;
    if (former == device) {
        Result result;
        result.accepted = true;
        result.controlRevision = own->controlRevision(sliceId);
        result.affected = keysOf(sliceId);
        return result;
    }
    // Checked at the change itself: a key that started since the device
    // looked stops the take.
    if (m_hooks.transmitting && m_hooks.transmitting(sliceId)) {
        return refused(takeWhileTransmittingReason(letter));
    }
    // Ruling Q7: the former controller must stay on as a listener.
    if (!former.isEmpty() && m_hooks.cannotHandOff) {
        const QString refusal = m_hooks.cannotHandOff(former, sliceId);
        if (!refusal.isEmpty()) {
            return refused(refusal);
        }
    }
    // Ruling Q8: the former controller's transmit selection of the slice is
    // cleared first, so no moment has the new controller's slice flagged
    // for the old one's transmit. Slice control fix wave: for a slice with
    // no controller too, since whoever holds transmit with the flag parked
    // on it loses that selection (the hook's third-holder case).
    if (m_hooks.clearTransmitSelection) {
        m_hooks.clearTransmitSelection(former, sliceId);
    }
    // One mark change: the taker joins, the former controller stays joined
    // (SliceOwnership), nothing is removed or made.
    own->setOwner(sliceId, device);
    if (m_hooks.tookControl) {
        m_hooks.tookControl(device, sliceId);
    }
    Result result;
    result.accepted = true;
    result.controlRevision = own->controlRevision(sliceId);
    result.affected = keysOf(sliceId);
    if (!former.isEmpty()) {
        emit controlTaken(sliceId, former, device);
    }
    return result;
}

SliceAccessController::Result SliceAccessController::release(const QByteArray& device,
                                                             SliceOwnership::SliceRef ref,
                                                             quint64 expectedRevision)
{
    SliceOwnership* own = ownership();
    if (own == nullptr) {
        return refused(notReadyReason());
    }
    if (device.isEmpty() || !own->matches(ref)) {
        return refused(closedReason());
    }
    const int sliceId = ref.sliceId;
    const QString letter = letterOf(sliceId);
    if (own->controlRevision(sliceId) != expectedRevision) {
        return refused(changedReason(letter));
    }
    if (!SliceAccessPolicy::mayChange(*own, device, sliceId)) {
        return refused(notControllerReason(letter));
    }
    Result result;
    result.accepted = true;
    result.affected = keysOf(sliceId);
    if (!closeIsRelease(device, sliceId)) {
        // Nobody else is on it: it closes, by the close path every slice
        // takes (the transmit flag moves as ruling 8.12 says).
        if (m_hooks.close && m_hooks.close(sliceId)) {
            return result;
        }
        // The Core's last slice stays, with nobody on it: a hand-off to
        // nobody as below, so not while it transmits (slice control fix
        // wave, Important 1). The close above changed nothing.
        if (m_hooks.transmitting && m_hooks.transmitting(sliceId)) {
            return refused(releaseWhileTransmittingReason(letter));
        }
        if (m_hooks.clearTransmitSelection) {
            m_hooks.clearTransmitSelection(device, sliceId);
        }
        own->setOwner(sliceId, QByteArray());
        own->leave(device, sliceId);
        return result;
    }
    // Kept for its listeners: a hand-off to nobody, so not while it
    // transmits.
    if (m_hooks.transmitting && m_hooks.transmitting(sliceId)) {
        return refused(releaseWhileTransmittingReason(letter));
    }
    if (m_hooks.clearTransmitSelection) {
        m_hooks.clearTransmitSelection(device, sliceId);
    }
    own->setOwner(sliceId, QByteArray());
    own->leave(device, sliceId);
    result.controlRevision = own->controlRevision(sliceId);
    return result;
}

SliceAccessController::Result SliceAccessController::selectRx(const QByteArray& device,
                                                              int sliceId)
{
    SliceOwnership* own = ownership();
    if (own == nullptr || m_radio.isNull()) {
        return refused(notReadyReason());
    }
    if (m_radio->sliceById(sliceId) == nullptr || !own->isLive(sliceId)) {
        return refused(goneReason());
    }
    if (!own->isListening(device, sliceId)) {
        return refused(notListeningReason(letterOf(sliceId)));
    }
    if (!m_radio->setActiveRxFor(device, sliceId)) {
        return refused(goneReason());
    }
    Result result;
    result.accepted = true;
    result.affected = {ObjectRegistry::keyForSlice(sliceId)};
    return result;
}

} // namespace NereusSDR
