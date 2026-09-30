// no-port-check: NereusSDR-original.
// =================================================================
// src/gui/HostingSliceActions.cpp  (NereusSDR)
// =================================================================
//
// See HostingSliceActions.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 10,
//               with AI-assisted implementation via Anthropic Claude Code.
//   2026-09-30: take-over parity: controlTaken is a notice with Take it
//               back, as in a remote window. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "gui/HostingSliceActions.h"

#include "core/SliceOwnership.h"
#include "core/session/MirrorSchema.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

namespace NereusSDR {

namespace {

MirrorUpdate intArg(const char* name, qint64 value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Int64, QVariant(value)};
}

MirrorUpdate utf8Arg(const char* name, const QString& value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Utf8, QVariant(value)};
}

MirrorUpdate doubleArg(const char* name, double value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Float64, QVariant(value)};
}

MirrorUpdate boolArg(const char* name, bool value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Bool, QVariant(value)};
}

// A change held for a question is answered "Waiting for you to confirm."
// with `phase` `needsConfirmation` (the several-devices design, section
// 7.3); the question follows, and the final answer after it.
bool awaitingConfirmation(const SessionMessage& result)
{
    if (result.accepted) {
        return false;
    }
    for (const MirrorUpdate& value : result.updates) {
        if (value.name == "phase"
            && value.value.toString() == QStringLiteral("needsConfirmation")) {
            return true;
        }
    }
    return false;
}

} // namespace

HostingSliceActions::HostingSliceActions(StationServer* server, RadioModel* model,
                                         QObject* parent)
    : QObject(parent)
    , m_server(server)
    , m_model(model)
{
    if (!m_server) {
        return;
    }
    const QPointer<HostingSliceActions> self(this);
    m_server->setStationNoticeHandler([self](const SessionMessage& message) {
        if (!self) {
            return;
        }
        // Take-over parity: controlTaken too, with Take it back, as in a
        // remote window.
        emit self->notice(message);
    });
}

HostingSliceActions::~HostingSliceActions()
{
    if (m_server) {
        m_server->setStationNoticeHandler({});
    }
}

QList<MirrorUpdate> HostingSliceActions::sliceArguments(int sliceId, bool withRevision) const
{
    const SliceOwnership* ownership = m_model ? m_model->sliceOwnership() : nullptr;
    const quint64 incarnation = ownership ? ownership->incarnation(sliceId) : 0;
    QList<MirrorUpdate> arguments{intArg("sliceId", sliceId),
                                  intArg("incarnation", static_cast<qint64>(incarnation))};
    if (withRevision) {
        const quint64 revision = ownership ? ownership->controlRevision(sliceId) : 0;
        arguments.append(intArg("controlRevision", static_cast<qint64>(revision)));
    }
    return arguments;
}

void HostingSliceActions::listen(int sliceId)
{
    run(QByteArrayLiteral("slice.listen"), sliceId, sliceArguments(sliceId, false));
}

void HostingSliceActions::stopListening(int sliceId)
{
    run(QByteArrayLiteral("slice.stopListening"), sliceId, sliceArguments(sliceId, false));
}

void HostingSliceActions::takeControl(int sliceId)
{
    run(QByteArrayLiteral("slice.takeControl"), sliceId, sliceArguments(sliceId, true));
}

void HostingSliceActions::release(int sliceId)
{
    run(QByteArrayLiteral("slice.release"), sliceId, sliceArguments(sliceId, true));
}

void HostingSliceActions::setListenLevel(int sliceId, double level, bool muted)
{
    QList<MirrorUpdate> arguments = sliceArguments(sliceId, false);
    arguments.append(doubleArg("level", level));
    arguments.append(boolArg("muted", muted));
    run(QByteArrayLiteral("slice.setListenLevel"), sliceId, arguments);
}

void HostingSliceActions::select(int sliceId)
{
    run(QByteArrayLiteral("setActiveSliceById"), sliceId, {intArg("sliceId", sliceId)});
}

void HostingSliceActions::addOnPan(const QString& panId)
{
    run(QByteArrayLiteral("addSliceOnPan"), -1, {utf8Arg("panId", panId)});
}

void HostingSliceActions::close(int sliceId)
{
    run(QByteArrayLiteral("removeSlice"), sliceId, {intArg("sliceId", sliceId)});
}

void HostingSliceActions::proceed(qint64 questionId, qint64 choice)
{
    run(QByteArrayLiteral("confirm.proceed"), -1,
        {intArg("id", questionId), intArg("choice", choice)});
}

void HostingSliceActions::cancel(qint64 questionId)
{
    run(QByteArrayLiteral("confirm.cancel"), -1, {intArg("id", questionId)});
}

void HostingSliceActions::takeBack(qint64 noticeId)
{
    run(QByteArrayLiteral("notice.takeBack"), -1, {intArg("id", noticeId)});
}

void HostingSliceActions::run(const QByteArray& verb, int sliceId,
                              const QList<MirrorUpdate>& arguments)
{
    if (!m_server) {
        const QString reason = QStringLiteral("This computer is not sharing the radio now.");
        emit refused(reason);
        emit finished(verb, sliceId, false, reason);
        return;
    }
    const quint32 commandId = m_nextCommandId++;
    if (m_nextCommandId == 0) {
        m_nextCommandId = 1;
    }
    const QPointer<HostingSliceActions> self(this);
    StationAnswer answer = [self, verb, sliceId](const SessionMessage& result) {
        if (self) {
            self->onAnswer(verb, sliceId, result);
        }
    };
    StationAnswer asked = [self](const SessionMessage& prompt) {
        if (self) {
            emit self->question(prompt);
        }
    };
    ++m_invokeDepth;
    m_server->invokeAsStationDevice(SessionMessages::commandInvoke(verb, commandId, arguments),
                                    std::move(answer), std::move(asked));
    if (self) {
        --m_invokeDepth;
    }
}

void HostingSliceActions::onAnswer(const QByteArray& verb, int sliceId,
                                   const SessionMessage& result)
{
    if (awaitingConfirmation(result)) {
        // Not a refusal: the question follows.
        m_waiting.insert(result.commandId);
        emit pending(sliceId, true);
        return;
    }
    if (m_waiting.remove(result.commandId)) {
        emit pending(sliceId, false);
    }
    if (!result.accepted && !result.reason.isEmpty()) {
        emit refused(result.reason);
    }
    emit finished(verb, sliceId, result.accepted, result.reason);
}

} // namespace NereusSDR
