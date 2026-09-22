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
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 11 fix round 1:
//                                    added setActiveSliceById (review
//                                    Important 1) plus same-thread-
//                                    invariant notes on the by-reference
//                                    lambda captures (Minor 7). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 1:
//                                    findIntArgument() replaces four bare
//                                    QVariant::toInt() narrows that
//                                    silently truncated an out-of-range
//                                    id to 32 bits. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/SessionCommandDispatcher.h"

#include "core/session/ObjectRegistry.h"
#include "core/dsp/DspAssetService.h"
#include "DspCommandValues.h"
#include "PureSignalSessionFacade.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QHash>
#include <QMetaObject>
#include <QMetaType>
#include <QSet>
#include <QVariant>

#include <limits>
#include <initializer_list>
#include <cmath>

namespace NereusSDR {

namespace {

// Looks up one named argument out of a CommandInvoke's arguments list.
// `arguments` reuses MirrorUpdate as a generic {name, kind, value} triple
// (SessionMessage::arguments' own doc comment) -- `kind` and `ordinal` are
// not consulted here, only `name` and `value`. Every INTEGER argument goes
// through findIntArgument() below rather than narrowing the QVariant at
// the call site; only the string arguments (initialPanId, panId) still
// read this directly, and QVariant::toString() has no range to fall off.
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

bool findUtf8Argument(const QList<MirrorUpdate>& arguments, const QByteArray& name,
                      QString* out)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            if (arg.kind != MirrorWireKind::Utf8 || arg.value.typeId() != QMetaType::QString) {
                return false;
            }
            *out = arg.value.toString();
            return true;
        }
    }
    return false;
}

bool hasWireKind(const QList<MirrorUpdate>& arguments, const QByteArray& name,
                 MirrorWireKind kind)
{
    for (const MirrorUpdate& arg : arguments) {
        if (arg.name == name) {
            return arg.kind == kind;
        }
    }
    return false;
}

// What findIntArgument() found. Three states rather than a bool, because
// "you did not send sliceId" and "the sliceId you sent is not a number
// this station can act on" are different things to tell a peer, and the
// pre-existing "missing ..." reasons are worth keeping distinct.
enum class ArgumentStatus {
    Ok,
    Missing,
    NotRepresentable,
};

// Every id and rate argument in this file is an `int` on RadioModel's
// side, and every one of them arrives from the far side of a socket.
//
// Fix round 5 review finding (Important 1): each of these used to be a
// bare QVariant::toInt() with the `ok` flag discarded. Measured on this
// tree's Qt, QVariant(qlonglong 4294967296).toInt() returns 0 with
// ok == true and 4294967297 returns 1, so a peer asking to remove slice
// 4294967296 removed slice 0 and got back accepted with affected
// ["slice:0"] -- a request naming an object that does not exist
// destroying a DIFFERENT object that does. One helper rather than four
// checked narrows at four call sites: the four sites want identical
// semantics, the next verb added to this file gets the safe behaviour by
// construction, and the wording a peer sees stays in one place.
//
// The runtime type is checked, not the declared MirrorWireKind. The two
// carry the same information -- SessionMessages' decoder collapses each
// wire kind onto exactly one QVariant runtime type (fromJsonValue:
// bool / qlonglong for Int64 and Enum / double / QString) -- but the
// runtime type is the stronger statement, since it also holds for an
// in-process caller that built the MirrorUpdate directly. Refusing a
// double outright also keeps this code out of QVariant's own
// floating-to-integral conversion, which is not a narrowing this layer
// should be performing on untrusted input.
ArgumentStatus findIntArgument(const QList<MirrorUpdate>& arguments,
                               const QByteArray& name, int* out)
{
    QVariant raw;
    if (!findArgument(arguments, name, &raw)) {
        return ArgumentStatus::Missing;
    }
    switch (raw.typeId()) {
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::ULong:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        break;
    default:
        return ArgumentStatus::NotRepresentable;
    }
    bool ok = false;
    const qlonglong wide = raw.toLongLong(&ok);
    if (!ok || wide < static_cast<qlonglong>(std::numeric_limits<int>::min())
        || wide > static_cast<qlonglong>(std::numeric_limits<int>::max())) {
        return ArgumentStatus::NotRepresentable;
    }
    *out = static_cast<int>(wide);
    return ArgumentStatus::Ok;
}

bool hasExactlyArguments(const QList<MirrorUpdate>& arguments,
                         std::initializer_list<QByteArray> expected)
{
    if (arguments.size() != static_cast<qsizetype>(expected.size())) {
        return false;
    }
    QSet<QByteArray> expectedNames(expected.begin(), expected.end());
    QSet<QByteArray> seen;
    for (const MirrorUpdate& argument : arguments) {
        if (!expectedNames.contains(argument.name) || seen.contains(argument.name)) {
            return false;
        }
        seen.insert(argument.name);
    }
    return true;
}

bool findFiniteDoubleArgument(const QList<MirrorUpdate>& arguments,
                              const QByteArray& name, double* out)
{
    QVariant raw;
    if (!findArgument(arguments, name, &raw) || raw.typeId() != QMetaType::Double) {
        return false;
    }
    const double value = raw.toDouble();
    if (!std::isfinite(value)) {
        return false;
    }
    *out = value;
    return true;
}

QString notRepresentableReason(const QByteArray& name)
{
    return QStringLiteral("%1 argument is not a whole number this station can represent")
        .arg(QString::fromUtf8(name));
}

} // namespace

SessionCommandDispatcher::SessionCommandDispatcher(RadioModel* radioModel, QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
{
    if (radioModel) {
        connect(radioModel->pureSignalFacade(), &PureSignalSessionFacade::actionResult,
                this, [this](quint32 id, Ps3ActionPhase phase, const QString& reason,
                             QVariantMap values) {
            const auto found = m_pureSignalCommands.constFind(id);
            if (found == m_pureSignalCommands.cend()) {
                return;
            }
            const PendingPureSignalCommand command = *found;
            QString state;
            switch (phase) {
            case Ps3ActionPhase::Accepted: state = "accepted"; break;
            case Ps3ActionPhase::Pending: state = "pending"; break;
            case Ps3ActionPhase::Completed: state = "completed"; break;
            case Ps3ActionPhase::Failed: state = "failed"; break;
            }
            if (phase == Ps3ActionPhase::Completed || phase == Ps3ActionPhase::Failed) {
                m_pureSignalCommands.remove(id);
            }
            values.insert("phase", state);
            emit commandResultReady(SessionMessages::commandResult(command.verb, command.commandId,
                phase != Ps3ActionPhase::Failed, reason, {"pureSignal"},
                dspCommandValues(values).value_or(QList<MirrorUpdate>{})));
        });
    }
}

void SessionCommandDispatcher::setSessionOwner(const QString& owner)
{
    m_pureSignalCommands.clear();
    if (m_radioModel && !m_sessionOwner.isEmpty()) {
        m_radioModel->dspAssets()->cancelOwner(m_sessionOwner);
        for (SliceModel* slice : m_radioModel->slices()) {
            // Diagnostic modes are operator actions. A new session always
            // starts on normal audio and never replays a prior test signal.
            m_radioModel->setNnrDiagnosticMode(slice->sliceIndex(), 0, 1);
        }
    }
    m_sessionOwner = owner;
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

    if (invoke.commandVerb.startsWith("ps3.")) {
        handlePureSignalAction(invoke);
        return;
    }

    if (invoke.commandVerb.startsWith("dspAssets.")) {
        const auto arguments = dspCommandValues(invoke.arguments);
        const auto result = arguments && !m_sessionOwner.isEmpty()
            ? m_radioModel->dspAssets()->execute(invoke.commandVerb, *arguments, m_sessionOwner)
            : DspAssetServiceResult{false, QStringLiteral("Malformed DSP asset request."), {}};
        const auto values = dspCommandValues(result.values);
        emit commandResultReady(SessionMessages::commandResult(invoke.commandVerb,
            invoke.commandId, result.accepted, result.reason, {}, values.value_or(QList<MirrorUpdate>{})));
        return;
    }
    if (invoke.commandVerb == "nnr.applyModelSelection") {
        quint32 revision = 0;
        QString reason;
        const bool shape = hasExactlyArguments(invoke.arguments, {"revision"});
        const QVariant value = shape ? invoke.arguments.first().value : QVariant();
        const bool integer = value.typeId() == QMetaType::Int || value.typeId() == QMetaType::UInt
            || value.typeId() == QMetaType::LongLong || value.typeId() == QMetaType::ULongLong;
        bool converted = false;
        const qlonglong wide = value.toLongLong(&converted);
        const bool valid = shape && integer && converted
            && invoke.arguments.first().kind == MirrorWireKind::Int64
            && wide > 0 && wide <= std::numeric_limits<quint32>::max();
        if (valid) {
            revision = static_cast<quint32>(wide);
        }
        const bool accepted = valid && m_radioModel->applyNnrModelSelection(revision, &reason);
        if (!valid) {
            reason = QStringLiteral("Model application requires the current selection revision.");
        }
        emitResult(invoke.commandVerb, invoke.commandId, accepted, reason, {"dspAssets"});
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
    } else if (invoke.commandVerb == "setActiveSliceById") {
        handleSetActiveSliceById(invoke);
    } else if (invoke.commandVerb == "requestStreamCtunPinned") {
        handleRequestStreamCtunPinned(invoke);
    } else if (invoke.commandVerb == "requestStreamCentre") {
        handleRequestStreamCentre(invoke);
    } else if (invoke.commandVerb == "configureTgxl") {
        handleConfigureTgxl(invoke);
    } else if (invoke.commandVerb == "disconnectTgxl") {
        handleDisconnectTgxl(invoke);
    } else if (invoke.commandVerb == "setFourO3AEnabled") {
        handleSetFourO3AEnabled(invoke);
    } else if (invoke.commandVerb == "nnr.setDiagnostics" || invoke.commandVerb == "nnr.resetTuning") {
        handleNnrAction(invoke);
    } else {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("unrecognised command verb"), {});
    }
}

void SessionCommandDispatcher::handlePureSignalAction(const SessionMessage& invoke)
{
    PureSignalSessionFacade* facade = m_radioModel->pureSignalFacade();
    const auto arguments = dspCommandValues(invoke.arguments);
    if (m_sessionOwner.isEmpty() || invoke.commandId == 0 || !arguments) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("Malformed PureSignal request or retired session."), {});
        return;
    }
    if (invoke.commandVerb == "ps3.subscribeDisplay") {
        if (arguments->size() != 1 || !arguments->contains("enabled")
            || arguments->value("enabled").typeId() != QMetaType::Bool) {
            emitResult(invoke.commandVerb, invoke.commandId, false,
                       QStringLiteral("A display subscription requires one boolean enabled value."), {});
            return;
        }
        facade->setRemoteAmpViewSubscribed(arguments->value("enabled").toBool());
        emitResult(invoke.commandVerb, invoke.commandId, true, {}, {"pureSignal"});
        return;
    }
    const auto action = PureSignalSessionFacade::actionForVerb(invoke.commandVerb);
    if (!action) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("Unknown PureSignal action."), {});
        return;
    }
    const bool stop = *action == Ps3Action::OffReset
        || (*action == Ps3Action::SetTwoTone && arguments->size() == 1
            && arguments->value("enabled").typeId() == QMetaType::Bool
            && !arguments->value("enabled").toBool());
    // The session currently advertises txPermitted=false. Keep the same hard
    // gate here even when a client bypasses its disabled controls. R4 owns
    // replacing this gate with negotiated, station-authorized transmit.
    if (!stop && *action != Ps3Action::SaveCorrection) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("Remote PureSignal actuation requires R4 transmit support."), {});
        return;
    }
    for (const PendingPureSignalCommand& command : std::as_const(m_pureSignalCommands)) {
        if (command.commandId == invoke.commandId) {
            emitResult(invoke.commandVerb, invoke.commandId, false,
                       QStringLiteral("This PureSignal command identity is already pending."), {});
            return;
        }
    }
    const quint32 id = facade->requestAction(*action, *arguments);
    if (!id) {
        emitResult(invoke.commandVerb, invoke.commandId, false, facade->lastActionError(), {});
        return;
    }
    m_pureSignalCommands.insert(id, {invoke.commandId, invoke.commandVerb});
    emit commandResultReady(SessionMessages::commandResult(invoke.commandVerb, invoke.commandId,
        true, {}, {}, {{0, "phase", MirrorWireKind::Utf8, QStringLiteral("accepted")}}));
}

void SessionCommandDispatcher::handleNnrAction(const SessionMessage& invoke)
{
    const bool diagnostics = invoke.commandVerb == "nnr.setDiagnostics";
    const bool shapeValid = diagnostics
        ? hasExactlyArguments(invoke.arguments, {"sliceId", "testMode", "outputMode"})
        : hasExactlyArguments(invoke.arguments, {"sliceId"});
    int sliceId = -1;
    int testMode = 0;
    int outputMode = 1;
    bool typesValid = shapeValid && findIntArgument(invoke.arguments, "sliceId", &sliceId) == ArgumentStatus::Ok;
    for (const auto& argument : invoke.arguments) {
        typesValid = typesValid && argument.kind == MirrorWireKind::Int64;
    }
    if (diagnostics) {
        typesValid = typesValid && findIntArgument(invoke.arguments, "testMode", &testMode) == ArgumentStatus::Ok
            && findIntArgument(invoke.arguments, "outputMode", &outputMode) == ArgumentStatus::Ok;
    }
    SliceModel* slice = typesValid ? m_radioModel->sliceById(sliceId) : nullptr;
    if (!slice) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("Unknown receiver or invalid NNR action arguments."), {});
        return;
    }
    QString reason;
    bool accepted = false;
    if (diagnostics) {
        accepted = m_radioModel->setNnrDiagnosticMode(sliceId, testMode, outputMode, &reason);
    } else {
        slice->resetNnrTuning();
        reason = slice->nnrLastError();
        accepted = reason.isEmpty();
    }
    emitResult(invoke.commandVerb, invoke.commandId, accepted, reason,
               accepted ? QList<QByteArray>{ObjectRegistry::keyForSlice(sliceId)} : QList<QByteArray>{});
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
    // down. The by-reference capture below is correct only under the
    // class-level same-thread invariant: it relies on the connected signal
    // firing synchronously, inside this call, before `rejectionReason`
    // goes out of scope. A future thread split that made this connection
    // cross-thread would auto-queue it and turn this into a dangling read.
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
    int sliceId = 0;
    switch (findIntArgument(invoke.arguments, "sliceId", &sliceId)) {
    case ArgumentStatus::Missing:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing sliceId argument"), {});
        return;
    case ArgumentStatus::NotRepresentable:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason("sliceId"), {});
        return;
    case ArgumentStatus::Ok:
        break;
    }

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
    // immediately after the synchronous call returns. As in handleAddSlice
    // above, the by-reference captures below depend on the class-level
    // same-thread invariant -- a cross-thread connection would auto-queue
    // and read `newId`/`rejectionReason` after they are gone.
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
    int sliceId = 0;
    int rateHz = 0;
    const ArgumentStatus sliceIdStatus =
        findIntArgument(invoke.arguments, "sliceId", &sliceId);
    const ArgumentStatus rateHzStatus = findIntArgument(invoke.arguments, "rateHz", &rateHz);
    // One combined message for the missing case, as before -- naming both
    // is what tells a peer this verb needs the pair. The out-of-range
    // case names the offending argument specifically, because there the
    // peer sent something and needs to know WHICH one was refused.
    if (sliceIdStatus == ArgumentStatus::Missing || rateHzStatus == ArgumentStatus::Missing) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing sliceId or rateHz argument"), {});
        return;
    }
    if (sliceIdStatus == ArgumentStatus::NotRepresentable) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason("sliceId"), {});
        return;
    }
    if (rateHzStatus == ArgumentStatus::NotRepresentable) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason("rateHz"), {});
        return;
    }

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
            // returns and before this connection is torn down. This
            // by-reference capture is ALREADY running inside a queued
            // lambda on RadioModel's thread (see this method's own
            // deferral above), so it depends on the same same-thread
            // invariant as handleAddSlice's capture, one level further in.
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

// ── setActiveSliceById ───────────────────────────────────────────────────

// Fix round 1 review finding (Important 1): before this verb existed, a
// remote operator's active-slice click had no path to the daemon at all.
// SliceModel::active carries no WRITE (SliceModel.h), so StateMirror::
// applyInbound() always fell through to applyMirroredValue(), which
// refused it outright (SliceModel.cpp) -- both inbound doors were shut.
// This verb is the one that was missing.
//
// Mechanically identical to handleRemoveSlice above: resolve the id,
// check the RadioModel entry point's own success/failure signal (here a
// bool return rather than an existence probe plus a separate guard), and
// report the resulting scope. RadioModel::setActiveSliceById() already
// does its own "no such slice" check internally (sliceById(sliceId) ==
// nullptr) and returns false, so this handler does not duplicate it --
// unlike handleRemoveSlice, which has a SECOND rejection RadioModel
// signals nothing about (the last-slice guard) and therefore does have to
// duplicate.
void SessionCommandDispatcher::handleSetActiveSliceById(const SessionMessage& invoke)
{
    int sliceId = 0;
    switch (findIntArgument(invoke.arguments, "sliceId", &sliceId)) {
    case ArgumentStatus::Missing:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("missing sliceId argument"), {});
        return;
    case ArgumentStatus::NotRepresentable:
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   notRepresentableReason("sliceId"), {});
        return;
    case ArgumentStatus::Ok:
        break;
    }

    // Captured BEFORE the call: this is the slice that is ABOUT to stop
    // being active, and setActiveSliceById() (RadioModel.cpp) reassigns
    // m_activeSlice as its very first side effect on success, so reading
    // this afterward would already show the NEW slice.
    SliceModel* const previouslyActive = m_radioModel->activeSlice();
    const int previouslyActiveId =
        (previouslyActive != nullptr) ? previouslyActive->sliceIndex() : -1;

    if (!m_radioModel->setActiveSliceById(sliceId)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("no such slice"), {});
        return;
    }

    // The newly-active key, plus the previously-active one when it is a
    // DIFFERENT slice -- requesting the slice that was already active is a
    // legitimate no-op accept (RadioModel::setActiveSlice()'s own
    // change-guard makes it one), and reporting the same key twice would
    // not describe two objects moving, just one.
    QList<QByteArray> affected{ ObjectRegistry::keyForSlice(sliceId) };
    if (previouslyActiveId >= 0 && previouslyActiveId != sliceId) {
        affected.append(ObjectRegistry::keyForSlice(previouslyActiveId));
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleRequestStreamCtunPinned(const SessionMessage& invoke)
{
    int sliceId = 0;
    QVariant pinned;
    if (!hasExactlyArguments(invoke.arguments, { "sliceId", "pinned" })
        || findIntArgument(invoke.arguments, "sliceId", &sliceId) != ArgumentStatus::Ok
        || !findArgument(invoke.arguments, "pinned", &pinned)
        || pinned.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("invalid sliceId or pinned argument"), {});
        return;
    }
    if (!m_radioModel->requestStreamCtunPinned(sliceId, pinned.toBool())) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("slice is not bound to an active stream"), {});
        return;
    }
    QList<QByteArray> affected;
    if (SliceModel* slice = m_radioModel->sliceById(sliceId)) {
        for (int id : m_radioModel->slicesOnStream(slice->streamIndex())) {
            affected.append(ObjectRegistry::keyForSlice(id));
        }
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleRequestStreamCentre(const SessionMessage& invoke)
{
    int sliceId = 0;
    double centreHz = 0.0;
    if (!hasExactlyArguments(invoke.arguments, { "sliceId", "centreHz" })
        || findIntArgument(invoke.arguments, "sliceId", &sliceId) != ArgumentStatus::Ok
        || !findFiniteDoubleArgument(invoke.arguments, "centreHz", &centreHz)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("invalid sliceId or centreHz argument"), {});
        return;
    }
    SliceModel* const slice = m_radioModel->sliceById(sliceId);
    const int stream = slice ? slice->streamIndex() : -1;
    if (!m_radioModel->requestStreamCentre(sliceId, centreHz)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("C-Tune centre is invalid for this stream's cohosts"), {});
        return;
    }
    QList<QByteArray> affected;
    for (int id : m_radioModel->slicesOnStream(stream)) {
        affected.append(ObjectRegistry::keyForSlice(id));
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), affected);
}

void SessionCommandDispatcher::handleConfigureTgxl(const SessionMessage& invoke)
{
    QString host;
    int port = 0;
    if (!hasExactlyArguments(invoke.arguments, { "host", "port" })
        || !findUtf8Argument(invoke.arguments, "host", &host)
        || !hasWireKind(invoke.arguments, "port", MirrorWireKind::Int64)
        || findIntArgument(invoke.arguments, "port", &port) != ArgumentStatus::Ok
        || port < 1 || port > 65535) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("invalid host or port argument"), {});
        return;
    }

    QString reason;
    if (!m_radioModel->configureTgxlForStation(host, static_cast<quint16>(port), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("TGXL configuration was refused") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleDisconnectTgxl(const SessionMessage& invoke)
{
    if (!hasExactlyArguments(invoke.arguments, {})) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("disconnectTgxl takes no arguments"), {});
        return;
    }

    QString reason;
    if (!m_radioModel->disconnectTgxlForStation(&reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("TGXL disconnect was refused") : reason,
                   {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

void SessionCommandDispatcher::handleSetFourO3AEnabled(const SessionMessage& invoke)
{
    QVariant enabled;
    if (!hasExactlyArguments(invoke.arguments, { "enabled" })
        || !findArgument(invoke.arguments, "enabled", &enabled)
        || enabled.typeId() != QMetaType::Bool) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   QStringLiteral("setFourO3AEnabled requires exactly one enabled boolean argument"), {});
        return;
    }

    QString reason;
    if (!m_radioModel->setFourO3AEnabledForStation(enabled.toBool(), &reason)) {
        emitResult(invoke.commandVerb, invoke.commandId, false,
                   reason.isEmpty() ? QStringLiteral("4O3A master change was refused") : reason, {});
        return;
    }
    emitResult(invoke.commandVerb, invoke.commandId, true, QString(), {});
}

} // namespace NereusSDR
