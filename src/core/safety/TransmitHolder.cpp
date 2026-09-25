// no-port-check: NereusSDR-original.
// =================================================================
// src/core/safety/TransmitHolder.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 34 (R-IOS-02). See TransmitHolder.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 34 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/safety/TransmitHolder.h"

#include "core/LogCategories.h"

namespace NereusSDR {

namespace {

const QByteArray kStation = QByteArrayLiteral("station");

} // namespace

TransmitHolder::TransmitHolder(QObject* parent)
    : QObject(parent)
{
}

void TransmitHolder::setHooks(Hooks hooks)
{
    m_hooks = std::move(hooks);
}

qint64 TransmitHolder::now() const
{
    return m_hooks.clock ? m_hooks.clock() : 0;
}

bool TransmitHolder::moxOn() const
{
    return m_hooks.moxOn && m_hooks.moxOn();
}

void TransmitHolder::disarmVox()
{
    if (m_hooks.disarmVox) {
        m_hooks.disarmVox();
    }
}

std::optional<TransmitHolder::Holder> TransmitHolder::holder() const
{
    if (!m_holder.has_value()) {
        return std::nullopt;
    }
    Holder h = *m_holder;
    // Ruling 8.1: after a take by the radio's own PTT the holder is
    // "Radio", kind station, until the next change of holder. Otherwise
    // the words are the device's as the Core names it now (Task 71's
    // numbering), so a rename or a new numbering shows at once.
    if (h.source == Source::RadioPtt) {
        h.name = QStringLiteral("Radio");
        h.shortName = QStringLiteral("Radio");
        h.kind = QStringLiteral("station");
    } else if (h.deviceId == kStation) {
        if (h.name.isEmpty()) {
            h.name = QStringLiteral("Radio");
            h.shortName = QStringLiteral("Radio");
        }
        h.kind = QStringLiteral("station");
    } else if (m_hooks.describe) {
        if (const std::optional<Words> words = m_hooks.describe(h.deviceId)) {
            h.name = words->name;
            h.shortName = words->shortName;
            h.kind = words->kind;
        }
    }
    return h;
}

bool TransmitHolder::isHeldBy(const QByteArray& deviceId) const
{
    return m_state == State::Held && m_holder.has_value() && m_holder->deviceId == deviceId;
}

TxRefusal TransmitHolder::keyRefusalFor(const QByteArray& deviceId, bool program) const
{
    // Step 2's end with MOX still on (a transfer's, or a dropped holder's
    // fence): until it reads off.
    if (m_stopUnconfirmed) {
        return TxRefusals::stopNotConfirmed();
    }
    // Ruling 8.2 step 1, and the dropped holder's fence (ruling 8.15).
    if (m_state == State::Transferring || m_fenced) {
        return TxRefusals::changingHands();
    }
    if (m_state == State::Held && m_holder.has_value()) {
        if (m_holder->deviceId == deviceId) {
            return {};
        }
        // Ruling 8.3: another device's key names the holder, away or not.
        return TxRefusals::otherDeviceHolds(holder()->name);
    }
    // Unheld. A program never takes transmit (D58, D63).
    if (program) {
        return TxRefusals::programNeedsTransmit();
    }
    return {};
}

KeyingAnswer TransmitHolder::askKey(const KeyRequest& request)
{
    const TxRefusal refusal = keyRefusalFor(request.deviceId, request.program);
    if (!refusal.isEmpty()) {
        return {KeyingVerdict::Refuse, refusal};
    }
    if (m_state == State::Held) {
        return {KeyingVerdict::Admit, {}};
    }
    // Unheld: a person's key takes transmit and keys (D63); the station
    // device's own keys likewise until Task 77. Nobody is keyed and MOX
    // reads off, so the transfer has nothing to unkey and ends at once.
    Holder next;
    next.deviceId = request.deviceId;
    next.source = request.source;
    ++m_epoch;
    next.sinceMs = now();
    m_holder = next;
    m_state = State::Held;
    // Ruling 8.4: VOX is disarmed at every change of holder. The station
    // device's own VOX key is the one exception: disarming VOX would end
    // the very key being admitted. (Refusing to arm VOX without holding
    // transmit is Task 77's.)
    if (!request.vox) {
        disarmVox();
    }
    qCInfo(lcDsp) << "Transmit taken by" << next.deviceId << "(nobody held it)";
    emit changed();
    return {KeyingVerdict::Admit, {}};
}

void TransmitHolder::transferTo(std::optional<Holder> next, const QString& reason,
                                std::function<void(bool)> done)
{
    if (m_state == State::Transferring) {
        if (done) {
            done(false);
        }
        return;
    }
    // Step 1: transferring; every key refused from here to the end.
    m_state = State::Transferring;
    m_next = std::move(next);
    m_transferDone = std::move(done);
    m_transferReason = reason;
    const quint64 generation = ++m_generation;
    emit changed();

    // Step 2: a keyed holder is unkeyed through the unkey gate first.
    const bool keyed = (m_holder.has_value() && m_holder->keyed) || moxOn();
    if (keyed && m_hooks.unkey) {
        m_hooks.unkey(reason, [this, generation](UnkeyOutcome) { afterUnkey(generation); });
        return;
    }
    afterUnkey(generation);
}

void TransmitHolder::afterUnkey(quint64 generation)
{
    if (generation != m_generation || m_state != State::Transferring) {
        return;
    }
    // Keyed or not, MOX is read: no holder is assigned while it reads on.
    if (!moxOn()) {
        assign(generation);
        return;
    }
    m_waitingMoxOff = true;
    if (m_hooks.stopAllTx) {
        m_hooks.stopAllTx(m_transferReason);
    }
    if (m_hooks.schedule) {
        m_hooks.schedule(kMoxOffWaitMs, [this, generation]() {
            if (generation != m_generation || !m_waitingMoxOff) {
                return;
            }
            if (moxOn()) {
                failTransfer(generation);
            } else {
                m_waitingMoxOff = false;
                assign(generation);
            }
        });
    }
}

void TransmitHolder::assign(quint64 generation)
{
    if (generation != m_generation) {
        return;
    }
    m_waitingMoxOff = false;
    // Step 3: the new holder, unkeyed, or nobody; VOX disarmed; the epoch
    // advanced; published.
    const bool hadHolder = m_holder.has_value();
    if (m_next.has_value()) {
        Holder next = *m_next;
        next.keyed = false;
        next.away = false;
        next.sinceMs = now();
        m_holder = next;
        m_state = State::Held;
    } else {
        m_holder.reset();
        m_state = State::Unheld;
    }
    m_next.reset();
    if (hadHolder || m_holder.has_value()) {
        ++m_epoch;
    }
    disarmVox();
    std::function<void(bool)> done = std::move(m_transferDone);
    m_transferDone = {};
    qCInfo(lcDsp) << "Transmit now held by"
                  << (m_holder.has_value() ? m_holder->deviceId : QByteArray("nobody"));
    emit changed();
    if (done) {
        done(true);
    }
}

void TransmitHolder::failTransfer(quint64 generation)
{
    if (generation != m_generation) {
        return;
    }
    // Step 2's end with MOX still on: transmit unheld, every key refused
    // until MOX reads off.
    m_waitingMoxOff = false;
    const bool hadHolder = m_holder.has_value();
    m_holder.reset();
    m_next.reset();
    m_state = State::Unheld;
    m_stopUnconfirmed = true;
    if (hadHolder) {
        ++m_epoch;
    }
    disarmVox();
    qCWarning(lcDsp) << "The radio did not confirm it stopped transmitting; transmit is unheld"
                        " and every key is refused until it does.";
    std::function<void(bool)> done = std::move(m_transferDone);
    m_transferDone = {};
    emit changed();
    if (done) {
        done(false);
    }
}

void TransmitHolder::release(const QByteArray& deviceId, const QString& reason)
{
    if (!m_holder.has_value() || m_holder->deviceId != deviceId
        || m_state == State::Transferring) {
        return;
    }
    transferTo(std::nullopt, reason);
}

void TransmitHolder::holderDropped(const QByteArray& deviceId, const QString& reason)
{
    if (m_state != State::Held || !m_holder.has_value() || m_holder->deviceId != deviceId) {
        return;
    }
    // Ruling 8.15: held for it, away, VOX disarmed; not a change of holder.
    m_holder->away = true;
    disarmVox();
    if (m_holder->keyed || moxOn()) {
        startFence(reason);
    }
    emit changed();
}

void TransmitHolder::startFence(const QString& reason)
{
    // Step 2's fence without a change of holder: keys refused until MOX
    // reads off.
    m_fenced = true;
    const quint64 generation = ++m_generation;
    const auto afterStop = [this, generation]() {
        if (generation != m_generation || !m_fenced) {
            return;
        }
        if (!moxOn()) {
            m_fenced = false;
            if (m_holder.has_value()) {
                m_holder->keyed = false;
            }
            emit changed();
            return;
        }
        m_fenceWaitingMoxOff = true;
        if (m_hooks.stopAllTx) {
            m_hooks.stopAllTx(m_transferReason);
        }
        if (m_hooks.schedule) {
            m_hooks.schedule(kMoxOffWaitMs, [this, generation]() {
                if (generation != m_generation || !m_fenceWaitingMoxOff) {
                    return;
                }
                m_fenceWaitingMoxOff = false;
                if (moxOn()) {
                    // Still on: keys stay refused (now "did not confirm")
                    // until MOX reads off; the holder is kept.
                    m_stopUnconfirmed = true;
                } else {
                    m_fenced = false;
                }
                emit changed();
            });
        }
    };
    m_transferReason = reason;
    if (m_hooks.unkey) {
        m_hooks.unkey(reason, [afterStop](UnkeyOutcome) { afterStop(); });
    } else {
        afterStop();
    }
}

void TransmitHolder::holderReturned(const QByteArray& deviceId)
{
    if (!m_holder.has_value() || m_holder->deviceId != deviceId || !m_holder->away) {
        return;
    }
    m_holder->away = false;
    emit changed();
}

void TransmitHolder::setKeyed(bool keyed)
{
    if (!m_holder.has_value() || m_holder->keyed == keyed) {
        return;
    }
    m_holder->keyed = keyed;
    m_holder->keyedSinceMs = keyed ? now() : 0;
    emit changed();
}

void TransmitHolder::onMoxReading(bool on)
{
    if (on) {
        return;
    }
    bool changedNow = false;
    if (m_holder.has_value() && m_holder->keyed) {
        m_holder->keyed = false;
        m_holder->keyedSinceMs = 0;
        changedNow = true;
    }
    if (m_waitingMoxOff && m_state == State::Transferring) {
        m_waitingMoxOff = false;
        assign(m_generation);
        return;
    }
    if (m_fenced && (m_fenceWaitingMoxOff || m_stopUnconfirmed)) {
        m_fenceWaitingMoxOff = false;
        m_fenced = false;
        m_stopUnconfirmed = false;
        changedNow = true;
    }
    if (m_stopUnconfirmed) {
        m_stopUnconfirmed = false;
        changedNow = true;
    }
    if (changedNow) {
        emit changed();
    }
}

} // namespace NereusSDR
