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
// =================================================================

#include "core/SliceOwnership.h"

namespace NereusSDR {

const QByteArray& SliceOwnership::stationDevice()
{
    static const QByteArray id = QByteArrayLiteral("station");
    return id;
}

SliceOwnership::SliceOwnership(QObject* parent)
    : QObject(parent)
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
    m_removing.remove(sliceId);
    m_order.removeAll(sliceId);
    m_marks.remove(sliceId);
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
    emit markChanged(sliceId, before.owner, before.heldFor);
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
