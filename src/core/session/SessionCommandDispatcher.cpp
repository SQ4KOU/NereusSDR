// =================================================================
// src/core/session/SessionCommandDispatcher.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 11.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11: command
//                                    dispatch (addSlice / removeSlice /
//                                    requestSliceSampleRate /
//                                    addSliceOnPan). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/SessionCommandDispatcher.h"

#include "core/session/ObjectRegistry.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QHash>
#include <QMetaObject>
#include <QVariant>

namespace NereusSDR {

namespace {

// Looks up one named argument out of a CommandInvoke's arguments list.
// `arguments` reuses MirrorUpdate as a generic {name, kind, value} triple
// (SessionMessage::arguments' own doc comment) -- `kind` and `ordinal` are
// not consulted here, only `name` and `value`; the QVariant coercions in
// each handler below (toInt() / toString()) are what actually narrow the
// value to what RadioModel's entry point expects.
bool findArgument(const QList<MirrorUpdate>& arguments, const QByteArray& name, QVariant* out)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            *out = arg.value;
            return true;
        }
    }
    return false;
}

} // namespace

SessionCommandDispatcher::SessionCommandDispatcher(RadioModel* radioModel, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
{
}

void SessionCommandDispatcher::dispatch(const SessionMessage& invoke)
{
    if (invoke.kind != SessionMessageKind::CommandInvoke) {
        // Not this class's concern -- a caller routing error, not a
        // command failure worth reporting back.
        return;
    }
    if (m_radioModel.isNull()) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("no radio model attached"), {});
        return;
    }

    if (invoke.commandVerb == "addSlice") {
        handleAddSlice(invoke);
    } else if (invoke.commandVerb == "removeSlice") {
        handleRemoveSlice(invoke);
    } else if (invoke.commandVerb == "requestSliceSampleRate") {
        handleRequestSliceSampleRate(invoke);
    } else if (invoke.commandVerb == "addSliceOnPan") {
        handleAddSliceOnPan(invoke);
    } else {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("unrecognised command verb"), {});
    }
}

void SessionCommandDispatcher::emitResult(const QByteArray& verb, quint32 commandId,
                                          bool accepted, const QString& reason,
                                          const QList<QByteArray>& affectedKeys)
{
    emit commandResultReady(
        SessionMessages::commandResult(verb, commandId, accepted, reason, affectedKeys));
}

// ── addSlice ─────────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleAddSlice(const SessionMessage& invoke)
{
    QVariant panIdArg;
    if (!findArgument(invoke.arguments, "initialPanId", &panIdArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing initialPanId argument"), {});
        return;
    }

    // RadioModel::addSlice() (RadioModel.cpp) can reject a placement after
    // partial construction, rolling back and returning -1 -- but only
    // after already emitting sliceAddRejected with the real, human-
    // readable reason (design addendum: "Rejected creation is first-class
    // ... The command result carries the reason"). A temporary connection
    // captures it; the call is synchronous, so the emission (if any)
    // happens before addSlice() returns and before this connection is torn
    // down.
    QString rejectionReason;
    const QMetaObject::Connection conn = connect(
        m_radioModel, &RadioModel::sliceAddRejected, this,
        [&rejectionReason](const QString& reason) { rejectionReason = reason; });
    const int id = m_radioModel->addSlice(panIdArg.toString());
    QObject::disconnect(conn);

    if (id < 0) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   rejectionReason.isEmpty()
                       ? QStringLiteral("rejected by the slice allocator")
                       : rejectionReason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(),
               { ObjectRegistry::keyForSlice(id) });
}

// ── removeSlice ──────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleRemoveSlice(const SessionMessage& invoke)
{
    QVariant sliceIdArg;
    if (!findArgument(invoke.arguments, "sliceId", &sliceIdArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing sliceId argument"), {});
        return;
    }
    const int sliceId = sliceIdArg.toInt();

    if (m_radioModel->sliceById(sliceId) == nullptr) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("no such slice"), {});
        return;
    }
    if (m_radioModel->slices().size() <= 1) {
        // RadioModel::removeSlice() (RadioModel.cpp) silently no-ops rather
        // than remove the last remaining slice -- no signal marks this
        // rejection (there is nothing wrong with the request itself, only
        // with the station's state), so it has to be caught here, before
        // the call, or the result would wrongly claim success.
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("cannot remove the last remaining slice"), {});
        return;
    }

    const QByteArray key = ObjectRegistry::keyForSlice(sliceId);
    m_radioModel->removeSlice(sliceId);
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), { key });
}

// ── addSliceOnPan ────────────────────────────────────────────────────────

void SessionCommandDispatcher::handleAddSliceOnPan(const SessionMessage& invoke)
{
    QVariant panIdArg;
    if (!findArgument(invoke.arguments, "panId", &panIdArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing panId argument"), {});
        return;
    }

    // addSliceOnPan() returns void (RadioModel.h), unlike addSlice(), so
    // the outcome has to be read off the two signals its own cap check and
    // its addSlice() delegate can each produce: sliceAdded(id) on success,
    // sliceAddRejected(reason) either from the cap check itself or from
    // addSlice()'s own allocator rollback. Both connections are torn down
    // immediately after the synchronous call returns.
    int newId = -1;
    QString rejectionReason;
    const QMetaObject::Connection addedConn = connect(
        m_radioModel, &RadioModel::sliceAdded, this,
        [&newId](int id) { newId = id; });
    const QMetaObject::Connection rejectedConn = connect(
        m_radioModel, &RadioModel::sliceAddRejected, this,
        [&rejectionReason](const QString& reason) { rejectionReason = reason; });

    m_radioModel->addSliceOnPan(panIdArg.toString());

    QObject::disconnect(addedConn);
    QObject::disconnect(rejectedConn);

    if (newId < 0) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   rejectionReason.isEmpty()
                       ? QStringLiteral("rejected by the slice allocator")
                       : rejectionReason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(),
               { ObjectRegistry::keyForSlice(newId) });
}

// ── requestSliceSampleRate ───────────────────────────────────────────────

void SessionCommandDispatcher::handleRequestSliceSampleRate(const SessionMessage& invoke)
{
    QVariant sliceIdArg;
    QVariant rateHzArg;
    if (!findArgument(invoke.arguments, "sliceId", &sliceIdArg)
        || !findArgument(invoke.arguments, "rateHz", &rateHzArg)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing sliceId or rateHz argument"), {});
        return;
    }
    const int sliceId = sliceIdArg.toInt();
    const int rateHz = rateHzArg.toInt();

    if (m_radioModel->sliceById(sliceId) == nullptr) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("no such slice"), {});
        return;
    }

    // Deferred to a LATER turn of RadioModel's own event loop -- see the
    // class comment for why this is the one verb that cannot run inline.
    // self/radioModel are QPointer copies so the daemon tearing either one
    // down before this queued call runs leaves nothing dangling; the
    // by-value capture of verb/commandId/sliceId/rateHz keeps this
    // self-contained once dispatch() (and `invoke`, which is a reference to
    // a caller-owned temporary) has returned.
    const QByteArray verb = invoke.commandVerb;
    const quint32 commandId = invoke.commandId;
    const QPointer<SessionCommandDispatcher> self(this);
    const QPointer<RadioModel> radioModel(m_radioModel);

    QMetaObject::invokeMethod(
        m_radioModel,
        [self, radioModel, verb, commandId, sliceId, rateHz]() {
            if (self.isNull() || radioModel.isNull()) {
                return;
            }

            // Actual scope, not requested scope (see the class comment):
            // snapshot every slice's rate before, act, then report
            // whichever slices' rates actually differ afterward. Correct
            // regardless of WHY more than one moved -- co-hosted slices
            // sharing requestSliceSampleRate's target DDC stream, or (on a
            // Protocol 1 board) the request escalating all the way to
            // RadioModel::setSampleRateLive's radio-wide sequence.
            QHash<int, int> before;
            for (SliceModel* slice : radioModel->slices()) {
                if (slice != nullptr) {
                    before.insert(slice->sliceIndex(), slice->sampleRateHz());
                }
            }

            // setStreamSampleRate (RadioModel.cpp) can refuse the retune
            // outright -- every slice stays exactly where it was -- and
            // reports that through sliceRetuneRejected with a human-
            // readable reason, the same pattern handleAddSlice's
            // sliceAddRejected capture uses. requestSliceSampleRate() is
            // synchronous, so the emission (if any) lands before it
            // returns and before this connection is torn down.
            QString rejectionReason;
            const QMetaObject::Connection conn = connect(
                radioModel, &RadioModel::sliceRetuneRejected, self,
                [&rejectionReason](int, const QString& reason) { rejectionReason = reason; });
            radioModel->requestSliceSampleRate(sliceId, rateHz);
            QObject::disconnect(conn);

            if (!rejectionReason.isEmpty()) {
                self->emitResult(verb, commandId, false, rejectionReason, {});
                return;
            }

            QList<QByteArray> affected;
            for (SliceModel* slice : radioModel->slices()) {
                if (slice == nullptr) {
                    continue;
                }
                const int id = slice->sliceIndex();
                if (before.value(id, -1) != slice->sampleRateHz()) {
                    affected.append(ObjectRegistry::keyForSlice(id));
                }
            }
            // An empty `affected` here is a legitimate no-op (the slice was
            // not yet bound to a stream, or was already at this rate --
            // requestSliceSampleRate()'s own idempotent-check paths,
            // RadioModel.cpp), not a failure: RadioModel raised no
            // rejection, so nothing here second-guesses that.
            self->emitResult(verb, commandId, true, QString(), affected);
        },
        Qt::QueuedConnection);
}

} // namespace NereusSDR
