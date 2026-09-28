// no-port-check: NereusSDR-original.
// =================================================================
// src/core/SliceOwnership.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 73 (R-IOS-02): whose each slice is, and each owner's
// active slice. See SliceOwnership.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 73 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-25: iPhone app plan Task 74 (R-IOS-02, R-IOS-30): each
//               receiver's anchor. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-28: slice control and shared listening plan Task 1: each
//               slice's incarnation and control revision. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/SliceOwnership.h"

#include <QRandomGenerator>

namespace NereusSDR {

const QByteArray& SliceOwnership::stationDevice()
{
    static const QByteArray id = QByteArrayLiteral("station");
    return id;
}

SliceOwnership::SliceOwnership(QObject* parent)
    : SliceOwnership(QRandomGenerator::system()->bounded(quint32{1} << kNonceBits), parent)
{
}

SliceOwnership::SliceOwnership(quint32 bootNonce, QObject* parent)
    : QObject(parent)
    , m_incarnationBase(quint64{bootNonce & ((quint32{1} << kNonceBits) - 1)} << 32)
{
}

// ── Lifecycle ───────────────────────────────────────────────────────────

void SliceOwnership::noteSliceAdded(int sliceId)
{
    m_removing.remove(sliceId);
    m_order.removeAll(sliceId);
    m_order.append(sliceId);
    Mark mark;
    mark.owner = m_creator;
    m_marks.insert(sliceId, mark);
    // Task 1 (slice control plan): a new incarnation, never 0 (the counter
    // skips 0 should it ever wrap), and control revision 1.
    if (++m_incarnationCounter == 0) {
        m_incarnationCounter = 1;
    }
    m_incarnations.insert(sliceId, m_incarnationBase | m_incarnationCounter);
    m_revisions.insert(sliceId, 1);
    // Task 74: bound before it was noted, it claimed its receiver for
    // nobody; it is the first there, so the receiver is its owner's.
    const auto stream = m_streamOf.constFind(sliceId);
    if (stream != m_streamOf.cend() && m_anchor.value(*stream).isEmpty()
        && m_joinOrder.value(*stream).value(0, -1) == sliceId) {
        m_anchor.insert(*stream, mark.subject());
    }
    emit activeChanged();
}

void SliceOwnership::beginRemove(int sliceId)
{
    if (!isLive(sliceId)) {
        return;
    }
    const QByteArray owner = m_marks.value(sliceId).owner;
    m_removing.insert(sliceId);
    if (m_mostRecent == sliceId) {
        // The station-level slice was this one: its owner's next, if any.
        m_mostRecent = activeFor(owner);
    }
    emit activeChanged();
}

void SliceOwnership::endRemove(int sliceId)
{
    leaveStream(sliceId);
    m_removing.remove(sliceId);
    m_order.removeAll(sliceId);
    m_marks.remove(sliceId);
    m_incarnations.remove(sliceId);
    m_revisions.remove(sliceId);
    if (m_mostRecent == sliceId) {
        m_mostRecent = -1;
    }
}

void SliceOwnership::setOrder(const QList<int>& sliceIds)
{
    QList<int> order;
    for (int id : sliceIds) {
        if (isLive(id) && !order.contains(id)) {
            order.append(id);
        }
    }
    for (int id : std::as_const(m_order)) {
        if (!order.contains(id)) {
            order.append(id);
        }
    }
    if (order != m_order) {
        m_order = order;
        emit activeChanged();
    }
}

bool SliceOwnership::isLive(int sliceId) const
{
    return m_marks.contains(sliceId) && !m_removing.contains(sliceId);
}

QList<int> SliceOwnership::liveSlices() const
{
    return matching([](const Mark&) { return true; });
}

// ── Incarnation and control revision (slice control plan Task 1) ────────

quint64 SliceOwnership::incarnation(int sliceId) const
{
    return isLive(sliceId) ? m_incarnations.value(sliceId, 0) : 0;
}

quint64 SliceOwnership::controlRevision(int sliceId) const
{
    return isLive(sliceId) ? m_revisions.value(sliceId, 0) : 0;
}

bool SliceOwnership::matches(const SliceRef& ref) const
{
    return ref.incarnation != 0 && incarnation(ref.sliceId) == ref.incarnation;
}

SliceOwnership::SliceRef SliceOwnership::refOf(int sliceId) const
{
    return SliceRef{sliceId, incarnation(sliceId)};
}

// ── Marks ───────────────────────────────────────────────────────────────

SliceOwnership::Mark SliceOwnership::mark(int sliceId) const
{
    return m_marks.value(sliceId);
}

void SliceOwnership::setMark(int sliceId, const Mark& requested)
{
    if (!isLive(sliceId)) {
        return;
    }
    Mark next = requested;
    if (next.isHeld()) {
        next.owner = stationDevice();
    }
    const Mark before = m_marks.value(sliceId);
    if (before == next) {
        return;
    }
    m_marks.insert(sliceId, next);
    // Task 74: the anchor goes with the slice to its new owner when it was
    // the old owner's only slice on the receiver.
    const auto stream = m_streamOf.constFind(sliceId);
    if (stream != m_streamOf.cend() && before.subject() != next.subject()
        && m_anchor.value(*stream) == before.subject()) {
        bool otherOfOld = false;
        for (int other : m_joinOrder.value(*stream)) {
            if (other != sliceId && subjectOf(other) == before.subject()) {
                otherOfOld = true;
                break;
            }
        }
        if (!otherOfOld) {
            m_anchor.insert(*stream, next.subject());
        }
    }
    // Task 1 (slice control plan): each change of owner is one control
    // revision; a change of heldFor alone is not.
    quint64 revision = 0;
    if (before.owner != next.owner) {
        revision = ++m_revisions[sliceId];
    }
    emit markChanged(sliceId, before.owner, before.heldFor);
    if (revision != 0) {
        emit controlRevisionChanged(sliceId, revision);
    }
    emit activeChanged();
}

void SliceOwnership::setOwner(int sliceId, const QByteArray& owner)
{
    setMark(sliceId, Mark{owner, QByteArray()});
}

void SliceOwnership::hold(int sliceId, const QByteArray& device)
{
    setMark(sliceId, Mark{stationDevice(), device});
}

QList<int> SliceOwnership::matching(const std::function<bool(const Mark&)>& test) const
{
    QList<int> ids;
    for (int id : m_order) {
        if (isLive(id) && test(m_marks.value(id))) {
            ids.append(id);
        }
    }
    return ids;
}

QList<int> SliceOwnership::ownedBy(const QByteArray& owner) const
{
    return matching([&owner](const Mark& mark) { return mark.owner == owner; });
}

QList<int> SliceOwnership::heldFor(const QByteArray& device) const
{
    if (device.isEmpty()) {
        return {};
    }
    return matching([&device](const Mark& mark) { return mark.heldFor == device; });
}

QList<int> SliceOwnership::unowned() const
{
    return matching([](const Mark& mark) { return mark.owner.isEmpty(); });
}

QList<int> SliceOwnership::returnHeld(const QByteArray& device)
{
    const QList<int> ids = heldFor(device);
    for (int id : ids) {
        setOwner(id, device);
    }
    return ids;
}

QList<int> SliceOwnership::adoptUnowned(const QByteArray& device)
{
    if (device.isEmpty()) {
        return {};
    }
    const QList<int> ids = unowned();
    const int wasActive = activeFor(QByteArray());
    for (int id : ids) {
        setOwner(id, device);
    }
    if (wasActive >= 0 && !m_chosen.contains(device)) {
        m_chosen.insert(device, wasActive);
        emit activeChanged();
    }
    return ids;
}

// ── The active slice ────────────────────────────────────────────────────

int SliceOwnership::activeFor(const QByteArray& owner) const
{
    const auto chosen = m_chosen.constFind(owner);
    if (chosen != m_chosen.cend() && isLive(*chosen) && m_marks.value(*chosen).owner == owner) {
        return *chosen;
    }
    const QList<int> own = ownedBy(owner);
    return own.isEmpty() ? -1 : own.first();
}

bool SliceOwnership::isActive(int sliceId) const
{
    return isLive(sliceId) && activeFor(m_marks.value(sliceId).owner) == sliceId;
}

void SliceOwnership::setActive(const QByteArray& owner, int sliceId)
{
    if (!isLive(sliceId) || m_marks.value(sliceId).owner != owner) {
        return;
    }
    m_chosen.insert(owner, sliceId);
    m_mostRecent = sliceId;
    emit activeChanged();
}

int SliceOwnership::stationActiveSlice() const
{
    if (!m_transmitHolder.isEmpty()) {
        const int holders = activeFor(m_transmitHolder);
        if (holders >= 0) {
            return holders;
        }
    }
    return isLive(m_mostRecent) ? m_mostRecent : -1;
}

void SliceOwnership::setTransmitHolder(const QByteArray& holder)
{
    if (m_transmitHolder == holder) {
        return;
    }
    m_transmitHolder = holder;
    emit activeChanged();
}

// ── Anchors (Task 74, rulings 6.2 and 6.3) ──────────────────────────────

void SliceOwnership::leaveStream(int sliceId)
{
    const auto found = m_streamOf.constFind(sliceId);
    if (found == m_streamOf.cend()) {
        return;
    }
    const int stream = *found;
    m_streamOf.remove(sliceId);
    QList<int>& order = m_joinOrder[stream];
    order.removeAll(sliceId);
    if (order.isEmpty()) {
        // The last slice left: the receiver is free.
        m_joinOrder.remove(stream);
        m_anchor.remove(stream);
        return;
    }
    const QByteArray anchor = m_anchor.value(stream);
    for (int other : std::as_const(order)) {
        if (subjectOf(other) == anchor) {
            return;  // the anchor still has a slice here
        }
    }
    // Ruling 6.2: the device whose slice has been here longest. Nobody is
    // asked or told; nothing on anyone's band moves.
    m_anchor.insert(stream, subjectOf(order.first()));
}

void SliceOwnership::noteStream(int sliceId, int stream)
{
    if (m_streamOf.value(sliceId, -1) == stream) {
        return;
    }
    leaveStream(sliceId);
    if (stream < 0) {
        return;
    }
    QList<int>& order = m_joinOrder[stream];
    if (order.isEmpty()) {
        // Ruling 6.2: this slice claimed the receiver (NewStream).
        m_anchor.insert(stream, subjectOf(sliceId));
    }
    order.append(sliceId);
    m_streamOf.insert(sliceId, stream);
}

QByteArray SliceOwnership::anchorOf(int stream) const
{
    return m_anchor.value(stream);
}

QList<int> SliceOwnership::slicesOnStreamInJoinOrder(int stream) const
{
    return m_joinOrder.value(stream);
}

// ── Creator scope ───────────────────────────────────────────────────────

SliceOwnership::CreatorScope::CreatorScope(SliceOwnership* ownership, const QByteArray& owner)
    : m_ownership(ownership)
{
    if (m_ownership != nullptr) {
        m_previous = m_ownership->m_creator;
        m_ownership->m_creator = owner;
    }
}

SliceOwnership::CreatorScope::~CreatorScope()
{
    if (m_ownership != nullptr) {
        m_ownership->m_creator = m_previous;
    }
}

} // namespace NereusSDR
