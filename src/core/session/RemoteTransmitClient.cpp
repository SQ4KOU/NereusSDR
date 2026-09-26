// =================================================================
// src/core/session/RemoteTransmitClient.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See the header.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25 - Created for the desktop remote window's transmit
//                (R-IOS-13, R-R3-42). J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
//   2026-09-25 - iPhone app plan Task 37 (R-IOS-13): the keepalive for the
//                Core's transmit watchdog. J.J. Boyd (KG4VCF), AI-assisted
//                via Anthropic Claude Code.
//   2026-09-26: Transmit group fix wave: M7 coreStopped. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/RemoteTransmitClient.h"

#include <QLoggingCategory>
#include <QPointer>

#include <utility>

Q_LOGGING_CATEGORY(lcRemoteTransmit, "nereus.remotetransmit")

namespace NereusSDR {

namespace {

MirrorUpdate utf8Argument(const char* name, const QString& value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Utf8, QVariant(value)};
}

MirrorUpdate int64Argument(const char* name, qint64 value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Int64, QVariant(value)};
}

MirrorUpdate boolArgument(const char* name, bool value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Bool, QVariant(value)};
}

QString utf8Value(const QList<MirrorUpdate>& values, const char* name)
{
    for (const MirrorUpdate& value : values) {
        if (value.name == name && value.value.typeId() == QMetaType::QString) {
            return value.value.toString();
        }
    }
    return {};
}

} // namespace

RemoteTransmitClient::RemoteTransmitClient(Sender sender, QObject* parent)
    : QObject(parent)
    , m_sender(std::move(sender))
{
    // Task 37: the keepalive's timer.
    m_keepaliveTimer.setInterval(kKeepaliveIntervalMs);
    m_keepaliveTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_keepaliveTimer, &QTimer::timeout, this, &RemoteTransmitClient::keepaliveTick);
}

void RemoteTransmitClient::setVoxArmed(bool armed)
{
    if (m_voxArmed == armed) {
        return;
    }
    m_voxArmed = armed;
    refreshKeepalive();
}

quint32 RemoteTransmitClient::keepaliveEpoch() const
{
    if (m_screen.phase == Phase::On && m_screen.epoch != 0) {
        return m_screen.epoch;
    }
    if (m_program.phase == Phase::On && m_program.epoch != 0) {
        return m_program.epoch;
    }
    // Before a key's answer, or for a key that has no epoch of this
    // window's (VOX, TUNE, two-tone): never older than the Core's.
    return kReleaseAnyEpoch;
}

void RemoteTransmitClient::refreshKeepalive()
{
    const bool wanted = m_available
        && (micKeyDown() || holdsTransmit() || m_tuneAsked || m_twoToneAsked || m_voxArmed);
    if (wanted == m_keepaliveTimer.isActive()) {
        return;
    }
    if (wanted) {
        qCInfo(lcRemoteTransmit) << "Keepalives start";
        // The first goes at once: the Core's watch may already be running.
        m_keepaliveTimer.start();
        keepaliveTick();
    } else {
        m_keepaliveTimer.stop();
        qCInfo(lcRemoteTransmit) << "Keepalives stop after" << m_keepaliveSequence;
    }
}

void RemoteTransmitClient::keepaliveTick()
{
    if (!m_available) {
        return;
    }
    const quint64 sequence = ++m_keepaliveSequence;
    const quint32 epoch = keepaliveEpoch();
    if (m_channelKeepalive && m_channelKeepalive(sequence, epoch)) {
        ++m_channelKeepalives;
        return;
    }
    if (m_sessionKeepalive && m_sessionKeepalive(sequence, epoch)) {
        ++m_sessionKeepalives;
    }
}

void RemoteTransmitClient::setAvailable(bool available)
{
    if (m_available == available) {
        return;
    }
    m_available = available;
    if (!available) {
        // The Core unkeys a device whose link drops and never keys it
        // again by itself; nothing here survives.
        reset();
        // Task 37: a new link starts its keepalives from 1.
        m_keepaliveSequence = 0;
        m_channelKeepalives = 0;
        m_sessionKeepalives = 0;
    }
    refreshKeepalive();
}

quint32 RemoteTransmitClient::send(const QByteArray& verb, const QList<MirrorUpdate>& arguments,
                                   Kind kind)
{
    if (!m_available || !m_sender) {
        return 0;
    }
    const quint32 id = m_sender(verb, arguments);
    if (id != 0) {
        m_pending.insert(id, kind);
    }
    return id;
}

void RemoteTransmitClient::setScreenKey(bool down)
{
    if (down) {
        if (m_screen.phase != Phase::Idle) {
            return;  // already pressed
        }
        const quint32 id = send(QByteArrayLiteral("tx.key"),
                                {utf8Argument("trigger", QString::fromLatin1(kScreenTrigger))},
                                Kind::ScreenKey);
        if (id == 0) {
            emit refused(QString::fromLatin1(kNoLinkReason), QString(), QString());
            return;
        }
        m_screen = Key{};
        m_screen.phase = Phase::Waiting;
        m_screen.commandId = id;
        qCInfo(lcRemoteTransmit) << "Key sent, command" << id;
        publish();
        return;
    }
    if (m_screen.phase == Phase::Idle) {
        // MOX off while a program keys through this window: the MOX
        // button's off unkeys it, as it does locally.
        if (m_program.phase == Phase::On) {
            const quint32 epoch = m_program.epoch;
            m_program = Key{};
            release(epoch != 0 ? epoch : kReleaseAnyEpoch);
            publish();
        } else if (m_coreTransmitting) {
            // MOX off while the Core transmits for no key of this window's
            // own (its VOX key, say): the MOX button's off unkeys whatever
            // keys, locally. The Core ends this device's own key, and
            // refuses another device's in its words (the holder's rule).
            release(kReleaseAnyEpoch);
        }
        return;
    }
    // Released: the key's own epoch, or before its answer any epoch of
    // this device's (the key may already be on at the Core).
    const quint32 epoch = m_screen.phase == Phase::On ? m_screen.epoch : kReleaseAnyEpoch;
    m_screen = Key{};
    // The program's key is the same key at the Core; the operator's
    // release ends it too, as the MOX button's off does locally.
    if (m_program.phase == Phase::On) {
        m_program = Key{};
    }
    release(epoch);
    publish();
}

void RemoteTransmitClient::release(quint32 epoch)
{
    send(QByteArrayLiteral("tx.unkey"), {int64Argument("epoch", static_cast<qint64>(epoch))},
         Kind::Release);
    qCInfo(lcRemoteTransmit) << "Release sent, epoch" << epoch;
}

void RemoteTransmitClient::setTune(bool on)
{
    const quint32 id = send(QByteArrayLiteral("tx.tune"), {boolArgument("on", on)}, Kind::Tune);
    m_tuneAsked = on && id != 0;
    if (id == 0 && on) {
        emit refused(QString::fromLatin1(kNoLinkReason), QString(), QString());
    }
    refreshKeepalive();
}

void RemoteTransmitClient::setTwoTone(bool on)
{
    const quint32 id =
        send(QByteArrayLiteral("tx.twoTone"), {boolArgument("on", on)}, Kind::TwoTone);
    m_twoToneAsked = on && id != 0;
    if (id == 0 && on) {
        emit refused(QString::fromLatin1(kNoLinkReason), QString(), QString());
    }
    refreshKeepalive();
}

void RemoteTransmitClient::keyForProgram(std::function<void(const Answer&)> answer)
{
    if (m_program.phase == Phase::Waiting) {
        // One program key at a time (the TCI server asks one at a time).
        Answer refusedAnswer;
        refusedAnswer.reason = QStringLiteral("Another program is already asking to transmit.");
        if (answer) { answer(refusedAnswer); }
        return;
    }
    const quint32 id = send(QByteArrayLiteral("tx.key"),
                            {utf8Argument("trigger", QString::fromLatin1(kProgramTrigger))},
                            Kind::ProgramKey);
    if (id == 0) {
        Answer refusedAnswer;
        refusedAnswer.reason = QString::fromLatin1(kNoLinkReason);
        if (answer) { answer(refusedAnswer); }
        return;
    }
    m_program = Key{};
    m_program.phase = Phase::Waiting;
    m_program.commandId = id;
    m_programAnswer = std::move(answer);
    publish();
}

void RemoteTransmitClient::unkeyForProgram(quint32 epoch)
{
    const bool wasOn = m_program.phase != Phase::Idle;
    m_program = Key{};
    // While the operator's MOX holds the same key on, the program letting
    // go leaves it on: a manual key is the operator's until released
    // (MoxController's rule for a TCI release under a manual key).
    if (m_screen.phase == Phase::Idle && (wasOn || epoch != 0)) {
        release(epoch != 0 ? epoch : kReleaseAnyEpoch);
    }
    publish();
}

quint32 RemoteTransmitClient::epochOf(const QList<MirrorUpdate>& values)
{
    for (const MirrorUpdate& value : values) {
        if (value.name == "epoch" && value.value.typeId() == QMetaType::LongLong) {
            const qlonglong raw = value.value.toLongLong();
            if (raw > 0 && raw <= 0xFFFFFFFFLL) {
                return static_cast<quint32>(raw);
            }
        }
    }
    return 0;
}

void RemoteTransmitClient::commandFinished(quint32 commandId, const QByteArray& verb,
                                           bool accepted, const QString& reason,
                                           const QList<MirrorUpdate>& values)
{
    // Only the first answer per command counts; the copies' answers are
    // the same answer again.
    const auto pending = m_pending.constFind(commandId);
    if (pending == m_pending.cend()) {
        return;
    }
    const Kind kind = pending.value();
    m_pending.erase(pending);
    const QString code = utf8Value(values, "refusalCode");
    const QString fix = utf8Value(values, "refusalFix");
    const QString shown = reason.isEmpty()
        ? QStringLiteral("The Core refused the request without giving a reason.")
        : reason;
    qCInfo(lcRemoteTransmit).noquote() << "Core answered" << QString::fromUtf8(verb) << commandId
                                       << (accepted ? "accepted" : "refused:") << reason;

    switch (kind) {
    case Kind::ScreenKey:
        if (m_screen.phase != Phase::Waiting || m_screen.commandId != commandId) {
            // Released before the answer: the release already went.
            return;
        }
        if (accepted) {
            m_screen.phase = Phase::On;
            m_screen.epoch = epochOf(values);
            // The Core may already say it transmits (its answer and its
            // state travel separately).
            m_screen.sawTransmitting = m_coreTransmitting;
        } else {
            m_screen = Key{};
            emit refused(shown, code, fix);
        }
        publish();
        return;
    case Kind::ProgramKey: {
        if (m_program.phase != Phase::Waiting || m_program.commandId != commandId) {
            return;
        }
        Answer answer;
        answer.accepted = accepted;
        answer.reason = accepted ? QString() : shown;
        answer.code = code;
        answer.fix = fix;
        if (accepted) {
            m_program.phase = Phase::On;
            m_program.epoch = epochOf(values);
            m_program.sawTransmitting = m_coreTransmitting;
            answer.epoch = m_program.epoch;
        } else {
            m_program = Key{};
        }
        auto reply = std::exchange(m_programAnswer, {});
        const QPointer<RemoteTransmitClient> self(this);
        publish();
        if (reply && self) {
            reply(answer);
        }
        return;
    }
    case Kind::Tune:
        if (!accepted && m_tuneAsked) {
            m_tuneAsked = false;
            refreshKeepalive();
        }
        [[fallthrough]];
    case Kind::Release:
    case Kind::TwoTone:
        if (kind == Kind::TwoTone && !accepted && m_twoToneAsked) {
            m_twoToneAsked = false;
            refreshKeepalive();
        }
        if (!accepted) {
            emit refused(shown, code, fix);
        }
        return;
    }
}

void RemoteTransmitClient::setCoreTransmitting(bool on)
{
    m_coreTransmitting = on;
    if (!on) {
        // The Core stopped transmitting: a TUNE or two-tone this window
        // asked for is over too.
        m_tuneAsked = false;
        m_twoToneAsked = false;
    }
    bool ended = false;
    for (Key* key : {&m_screen, &m_program}) {
        if (key->phase != Phase::On) {
            continue;
        }
        if (on) {
            key->sawTransmitting = true;
        } else if (key->sawTransmitting) {
            // The Core ended the key on its own (a safety stop, a take):
            // the next press is a new command.
            *key = Key{};
            ended = true;
        }
    }
    if (ended) {
        qCInfo(lcRemoteTransmit) << "The Core ended this window's key";
        publish();
    }
    refreshKeepalive();
}

void RemoteTransmitClient::coreStopped(quint32 stopSerial, bool coreKeyed)
{
    if (stopSerial == m_coreStopSerial) {
        return;
    }
    m_coreStopSerial = stopSerial;
    if (coreKeyed) {
        // Another key is on by now (this window's next one, answered
        // already, or someone else's): nothing of this window's to end.
        return;
    }
    bool ended = false;
    for (Key* key : {&m_screen, &m_program}) {
        if (key->phase == Phase::On) {
            *key = Key{};
            ended = true;
        }
    }
    if (ended) {
        m_tuneAsked = false;
        m_twoToneAsked = false;
        qCInfo(lcRemoteTransmit) << "The Core stopped this window's key";
        publish();
        refreshKeepalive();
    }
}

bool RemoteTransmitClient::micKeyDown() const
{
    return m_screen.phase != Phase::Idle || m_program.phase != Phase::Idle;
}

bool RemoteTransmitClient::holdsTransmit() const
{
    return m_screen.phase == Phase::On || m_program.phase == Phase::On;
}

void RemoteTransmitClient::reset()
{
    m_screen = Key{};
    m_program = Key{};
    m_tuneAsked = false;
    m_twoToneAsked = false;
    m_pending.clear();
    auto reply = std::exchange(m_programAnswer, {});
    const QPointer<RemoteTransmitClient> self(this);
    publish();
    if (reply && self) {
        Answer answer;
        answer.reason = QString::fromLatin1(kNoLinkReason);
        reply(answer);
    }
}

void RemoteTransmitClient::publish()
{
    const QPointer<RemoteTransmitClient> self(this);
    const bool down = micKeyDown();
    if (down != m_publishedKeyDown) {
        m_publishedKeyDown = down;
        emit micKeyDownChanged(down);
        if (!self) { return; }
    }
    const bool holds = holdsTransmit();
    if (holds != m_publishedHolds) {
        m_publishedHolds = holds;
        emit holdsTransmitChanged(holds);
        if (!self) { return; }
    }
    refreshKeepalive();
}

} // namespace NereusSDR
