// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/RemoteKeying.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 35 (R-IOS-13). See RemoteKeying.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 35 (R-IOS-13), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/RemoteKeying.h"

#include "core/LogCategories.h"
#include "core/MoxController.h"
#include "core/TwoToneController.h"
#include "core/safety/TransmitHolder.h"
#include "models/RadioModel.h"

#include <limits>

namespace NereusSDR {

namespace {

// The refusal a key call reports, captured while it runs: the transmit
// refusal MoxController sends (moxRefused) and TUNE's own plain reason
// (tuneRefused), whichever came last.
class RefusalCapture {
public:
    RefusalCapture(MoxController* mox, RadioModel* model)
    {
        if (mox != nullptr) {
            m_mox = QObject::connect(mox, &MoxController::moxRefused,
                                     [this](const TxRefusal& refusal) { m_refusal = refusal; });
        }
        if (model != nullptr) {
            m_tune = QObject::connect(model, &RadioModel::tuneRefused,
                                      [this](const QString& reason) { m_reason = reason; });
        }
    }
    ~RefusalCapture()
    {
        QObject::disconnect(m_mox);
        QObject::disconnect(m_tune);
    }
    RefusalCapture(const RefusalCapture&) = delete;
    RefusalCapture& operator=(const RefusalCapture&) = delete;

    const TxRefusal& refusal() const { return m_refusal; }
    const QString& reason() const { return m_reason; }

private:
    QMetaObject::Connection m_mox;
    QMetaObject::Connection m_tune;
    TxRefusal m_refusal;
    QString m_reason;
};

RemoteKeying::Result accepted(quint32 epoch)
{
    RemoteKeying::Result result;
    result.accepted = true;
    result.epoch = epoch;
    return result;
}

RemoteKeying::Result refused(const TxRefusal& refusal)
{
    RemoteKeying::Result result;
    result.refusal = refusal;
    result.reason = refusal.text;
    return result;
}

RemoteKeying::Result refusedPlain(const QString& reason)
{
    RemoteKeying::Result result;
    result.reason = reason;
    return result;
}

} // namespace

bool RemoteKeying::isTrigger(const QByteArray& trigger)
{
    return trigger == "screen" || trigger == "headset" || trigger == "bluetooth"
        || trigger == "actionButton" || trigger == kProgramTrigger;
}

RemoteKeying::RemoteKeying(RadioModel* model, TransmitHolder* holder, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_holder(holder)
{
    if (m_model != nullptr && m_model->moxController() != nullptr) {
        connect(m_model->moxController(), &MoxController::stateChanged, this,
                [this](MoxState) { publishKeyedBy(); });
    }
    if (m_model != nullptr && m_model->twoToneController() != nullptr) {
        // A two-tone that ended (or never keyed) leaves no key to start.
        connect(m_model->twoToneController(), &TwoToneController::twoToneActiveChanged, this,
                [this](bool active) {
                    const TwoToneController* tt = m_model ? m_model->twoToneController() : nullptr;
                    if (!active && tt != nullptr && !tt->isActivationInFlight()
                        && m_pending.has_value() && m_pending->trigger == "twoTone") {
                        m_pending.reset();
                    }
                });
    }
}

RemoteKeying::Result RemoteKeying::handle(const Command& command)
{
    if (m_model.isNull() || m_model->moxController() == nullptr || m_holder.isNull()) {
        return refusedPlain(QStringLiteral("The Core has no radio ready."));
    }
    if (const std::optional<Result> copy = copyOf(command)) {
        return *copy;
    }
    Result result;
    switch (command.verb) {
    case Verb::Key:
        result = key(command);
        break;
    case Verb::Unkey:
        result = unkey(command);
        break;
    case Verb::Tune:
        result = tune(command);
        break;
    case Verb::TwoTone:
        result = twoTone(command);
        break;
    }
    remember(command, result);
    return result;
}

void RemoteKeying::forgetSession(const QString& session)
{
    m_sessions.remove(session);
}

// ---- Copies (the pairing design, section 9.6) ------------------------------

std::optional<RemoteKeying::Result> RemoteKeying::copyOf(const Command& command) const
{
    const auto it = m_sessions.constFind(command.session);
    if (it == m_sessions.cend()) {
        return std::nullopt;
    }
    for (const Remembered& earlier : *it) {
        if (earlier.verb != command.verb || earlier.commandId != command.commandId) {
            continue;
        }
        // A key the Core acted on: the same answer while that key is on;
        // once it has ended (a safety stop, a release, a take), the copy is
        // refused and never keys again.
        const bool startsAKey = command.verb == Verb::Key
            || ((command.verb == Verb::Tune || command.verb == Verb::TwoTone) && command.on);
        if (startsAKey && earlier.result.accepted
            && !keyLive(command.deviceId, earlier.result.epoch)) {
            return refused(TxRefusals::keyEnded());
        }
        return earlier.result;
    }
    return std::nullopt;
}

void RemoteKeying::remember(const Command& command, const Result& result)
{
    QList<Remembered>& list = m_sessions[command.session];
    list.append(Remembered{command.verb, command.commandId, result});
    while (list.size() > kRememberedCommands) {
        list.removeFirst();
    }
}

// ---- What is on ---------------------------------------------------------------

bool RemoteKeying::moxKeyedFor(const QByteArray& deviceId) const
{
    const MoxController* mox = m_model ? m_model->moxController() : nullptr;
    return mox != nullptr && mox->isMox() && mox->currentKeyer().deviceId == deviceId;
}

bool RemoteKeying::twoToneRunningFor(const QByteArray& deviceId) const
{
    const TwoToneController* tt = m_model ? m_model->twoToneController() : nullptr;
    return tt != nullptr && (tt->isActive() || tt->isActivationInFlight())
        && tt->keyer().deviceId == deviceId;
}

bool RemoteKeying::keyLive(const QByteArray& deviceId, quint32 epoch) const
{
    if (moxKeyedFor(deviceId)) {
        return epoch == 0 || m_model->keyedBy().epoch == epoch;
    }
    // A two-tone start admitted and on its way to keying (its settle).
    if (twoToneRunningFor(deviceId) && m_pending.has_value()
        && m_pending->deviceId == deviceId) {
        return epoch == 0 || m_pending->epoch == epoch;
    }
    return false;
}

// ---- The verbs ------------------------------------------------------------------

RemoteKeying::Result RemoteKeying::key(const Command& command)
{
    MoxController* mox = m_model->moxController();
    // A key with a new id while this device's own key is on changes
    // nothing: the key already on answers.
    if (moxKeyedFor(command.deviceId)) {
        return accepted(m_model->keyedBy().epoch);
    }
    KeyerIdentity keyer;
    keyer.deviceId = command.deviceId;
    keyer.source = PttMode::None;
    // D58: a program's key never takes transmit (TransmitHolder::askKey).
    keyer.program = command.trigger == kProgramTrigger;

    // The key's epoch is the next one; it is spent only if the key keys.
    m_pending = Pending{command.deviceId, command.trigger, nextEpoch()};
    RefusalCapture capture(mox, nullptr);
    mox->setMox(true, keyer);
    if (moxKeyedFor(command.deviceId)) {
        m_pending.reset();
        qCInfo(lcDsp) << "Keyed for" << command.deviceId << "by" << command.trigger
                      << "epoch" << m_model->keyedBy().epoch;
        return accepted(m_model->keyedBy().epoch);
    }
    m_pending.reset();
    TxRefusal refusal = capture.refusal();
    if (refusal.isEmpty()) {
        refusal = mox->lastRefusal().isEmpty() ? TxRefusals::changingHands() : mox->lastRefusal();
    }
    return refused(refusal);
}

RemoteKeying::Result RemoteKeying::unkey(const Command& command)
{
    // The device's own key, or its two-tone on its way to keying.
    if (keyLive(command.deviceId)) {
        const quint32 live = moxKeyedFor(command.deviceId) ? m_model->keyedBy().epoch
                                                           : m_pending->epoch;
        if (command.epoch < live) {
            // An unkey for an earlier key of this device: ignored.
            return accepted(0);
        }
        return stopFrom(command.deviceId, true);
    }
    // A VOX key while this device holds transmit is this device's (ruling
    // 8.4), so its release stops it too.
    const MoxController* mox = m_model->moxController();
    if (m_holder->isHeldBy(command.deviceId) && mox->isMox() && mox->currentKeyer().isStation()
        && mox->currentKeyer().source == PttMode::Vox) {
        if (command.epoch < m_model->keyedBy().epoch) {
            return accepted(0);
        }
        m_model->moxController()->setMox(false);
        qCInfo(lcDsp) << "Unkeyed the VOX key of" << command.deviceId;
        return accepted(0);
    }
    return stopFrom(command.deviceId, false);
}

RemoteKeying::Result RemoteKeying::stopFrom(const QByteArray& deviceId, bool deviceKeyOn)
{
    if (deviceKeyOn) {
        // Ruling 8.5: a release unkeys only this device's own key. TUNE and
        // two-tone end their own way, restoring what they changed.
        if (m_model->isTune() && moxKeyedFor(deviceId)) {
            m_model->setTune(false);
        } else if (twoToneRunningFor(deviceId)) {
            m_model->twoToneController()->setActive(false);
        } else {
            KeyerIdentity keyer;
            keyer.deviceId = deviceId;
            m_model->moxController()->setMox(false, keyer);
        }
        qCInfo(lcDsp) << "Unkeyed for" << deviceId;
        return accepted(0);
    }
    // Nothing of this device's is on. From a device that does not hold
    // transmit, the holder's release is refused and the transmission
    // continues (ruling 8.5).
    if (const std::optional<TransmitHolder::Holder> holder = m_holder->holder()) {
        if (holder->deviceId != deviceId) {
            return refused(TxRefusals::otherDeviceHoldsStop(holder->name));
        }
    }
    return accepted(0);
}

RemoteKeying::Result RemoteKeying::tune(const Command& command)
{
    if (!command.on) {
        return stopFrom(command.deviceId, m_model->isTune() && moxKeyedFor(command.deviceId));
    }
    if (m_model->isTune() && moxKeyedFor(command.deviceId)) {
        return accepted(m_model->keyedBy().epoch);
    }
    MoxController* mox = m_model->moxController();
    KeyerIdentity keyer;
    keyer.deviceId = command.deviceId;
    keyer.source = PttMode::Manual;
    m_pending = Pending{command.deviceId, QByteArrayLiteral("tune"), nextEpoch()};
    RefusalCapture capture(mox, m_model.data());
    m_model->setTune(true, keyer);
    const bool keyed = m_model->isTune() && moxKeyedFor(command.deviceId);
    m_pending.reset();
    if (keyed) {
        return accepted(m_model->keyedBy().epoch);
    }
    if (!capture.refusal().isEmpty()) {
        return refused(capture.refusal());
    }
    if (!capture.reason().isEmpty()) {
        return refusedPlain(capture.reason());
    }
    return refusedPlain(QStringLiteral("The Core could not start TUNE."));
}

RemoteKeying::Result RemoteKeying::twoTone(const Command& command)
{
    TwoToneController* tt = m_model->twoToneController();
    if (!command.on) {
        return stopFrom(command.deviceId, tt != nullptr && twoToneRunningFor(command.deviceId));
    }
    if (tt == nullptr) {
        return refusedPlain(QStringLiteral("The two-tone test is not available on this Core."));
    }
    // The holder's refusal first (a question only; nothing changes), so a
    // device that cannot key hears why before two-tone's own checks.
    if (const TxRefusal refusal = m_holder->keyRefusalFor(command.deviceId);
        !refusal.isEmpty()) {
        return refused(refusal);
    }
    if (twoToneRunningFor(command.deviceId)) {
        return accepted(moxKeyedFor(command.deviceId) ? m_model->keyedBy().epoch
                        : m_pending.has_value()      ? m_pending->epoch
                                                     : 0);
    }
    MoxController* mox = m_model->moxController();
    KeyerIdentity keyer;
    keyer.deviceId = command.deviceId;
    keyer.source = PttMode::None;
    const quint32 epoch = nextEpoch();
    m_pending = Pending{command.deviceId, QByteArrayLiteral("twoTone"), epoch};
    RefusalCapture capture(mox, nullptr);
    tt->setActive(true, keyer);
    if (twoToneRunningFor(command.deviceId)) {
        // Two-tone keys after its own settle: its epoch is spent now, so
        // no other key takes it meanwhile.
        if (!moxKeyedFor(command.deviceId) && m_model->keyingEpoch() != epoch) {
            m_model->advanceKeyingEpoch();
        }
        // Two-tone keys after its own settle; its key takes this epoch.
        if (moxKeyedFor(command.deviceId)) {
            m_pending.reset();
            return accepted(m_model->keyedBy().epoch);
        }
        return accepted(epoch);
    }
    m_pending.reset();
    if (!capture.refusal().isEmpty()) {
        return refused(capture.refusal());
    }
    return refusedPlain(QStringLiteral("The two-tone test could not start on the Core."));
}

// ---- Who is keyed ---------------------------------------------------------------

quint32 RemoteKeying::nextEpoch() const
{
    const quint32 now = m_model->keyingEpoch();
    return now == std::numeric_limits<quint32>::max() ? 1 : now + 1;
}

QByteArray RemoteKeying::stationTrigger(const KeyerIdentity& keyer) const
{
    switch (keyer.source) {
    case PttMode::Mic:
        return QByteArrayLiteral("radioPtt");
    case PttMode::Vox:
        return QByteArrayLiteral("vox");
    case PttMode::Tci:
        return QByteArrayLiteral("tci");
    case PttMode::Cat:
        return QByteArrayLiteral("cat");
    default:
        break;
    }
    if (m_model && m_model->isTune()) {
        return QByteArrayLiteral("tune");
    }
    return QByteArrayLiteral("station");
}

void RemoteKeying::publishKeyedBy()
{
    const MoxController* mox = m_model ? m_model->moxController() : nullptr;
    if (mox == nullptr) {
        return;
    }
    if (!mox->isMox()) {
        m_liveKeyer.clear();
        m_model->setKeyedBy({});
        return;
    }
    const KeyerIdentity& keyer = mox->currentKeyer();
    if (!m_liveKeyer.isEmpty() && m_liveKeyer == keyer.deviceId) {
        return;   // the same key, further along its walk
    }
    m_liveKeyer = keyer.deviceId;
    RadioModel::KeyedBy keyedBy;
    if (m_pending.has_value() && m_pending->deviceId == keyer.deviceId) {
        keyedBy.trigger = m_pending->trigger;
        keyedBy.epoch = m_pending->epoch;
        // Spent now (a two-tone's was spent when it was admitted).
        if (m_model->keyingEpoch() != keyedBy.epoch) {
            m_model->advanceKeyingEpoch();
        }
        // A two-tone's key has now started.
        if (m_pending->trigger == "twoTone") {
            m_pending.reset();
        }
    } else {
        keyedBy.trigger = keyer.isStation() ? stationTrigger(keyer) : QByteArrayLiteral("station");
        keyedBy.epoch = m_model->advanceKeyingEpoch();
    }
    // Section 2.2: keyedBy names the holder (VOX and a program's key name
    // the device that holds transmit).
    const std::optional<TransmitHolder::Holder> holder =
        m_holder ? m_holder->holder() : std::nullopt;
    if (holder.has_value()) {
        keyedBy.deviceId = holder->deviceId;
        keyedBy.deviceName = holder->name;
        keyedBy.deviceKind = holder->kind;
    } else if (keyer.isStation()) {
        keyedBy.deviceId = QByteArray(KeyerIdentity::kStationDeviceId);
        keyedBy.deviceName = QStringLiteral("Radio");
        keyedBy.deviceKind = QStringLiteral("station");
    } else {
        keyedBy.deviceId = keyer.deviceId;
    }
    m_model->setKeyedBy(keyedBy);
}

} // namespace NereusSDR
