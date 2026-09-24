#pragma once
// =================================================================
// src/core/session/SessionCommandDispatcher.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 11.
//
// Turns a decoded CommandInvoke SessionMessage into the corresponding
// RadioModel call, and reports back what ACTUALLY happened as a
// CommandResult (SessionMessages.h). This is the production half of "in-
// process dispatch" tst_session_verbs.cpp exercises over
// tests/fakes/LoopbackStationLink.h; Task 18 is what feeds a real wss
// session's inbound bytes into dispatch() and relays commandResultReady()
// back out.
//
// Five verbs. Four matching the R2 plan's Task 11 step 2 exactly
// (RadioModel.h, this tree's HEAD at commit 998e7854), plus a fifth added
// in fix round 1 after review found the plan's own justification for
// leaving it out ("active rides the ordinary delta path") was true
// OUTBOUND only -- SliceModel::active has no WRITE at all, so a remote
// peer had no path to move it (see SliceModel.cpp's applyMirroredValue,
// which now names this verb instead of the unreachable
// RadioModel::setActiveSlice()):
//
//   addSlice               -- RadioModel.h:775   int addSlice(QString)
//   removeSlice             -- RadioModel.h:779   void removeSlice(int)
//   requestSliceSampleRate  -- RadioModel.h:720   void requestSliceSampleRate(int, int)
//   addSliceOnPan           -- RadioModel.h:813   Q_INVOKABLE void addSliceOnPan(QString)
//   setActiveSliceById      -- RadioModel.h:804   bool setActiveSliceById(int)
//
// Nothing here writes a mirrored PROPERTY -- that is StateMirror::
// applyInbound()'s job (Task 8). This class exists for the five RadioModel
// entry points MirrorPolicy has no property to gate at all: creating,
// destroying or activating a slice is not a value change on an existing
// object's WRITE accessor, and requestSliceSampleRate is the one case
// (StateMirror.cpp's kVerbHints table, :69) where a mirrored property
// (SliceModel::sampleRateHz) is deliberately Outbound-only specifically
// BECAUSE this verb is how a client changes it.
//
// ── THREADING ────────────────────────────────────────────────────────────
//
// dispatch() must be called on RadioModel's own thread. Task 18's session
// read loop -- whatever decodes bytes off the wss socket into a
// SessionMessage and calls dispatch() -- therefore also runs on that same
// thread, not a dedicated I/O thread. This is not a new constraint this
// class invents: it is the same single-thread precondition StateMirror.h
// already documents for attachSession() and for the m_applying inbound-
// echo guard (StateMirror.cpp) -- both rely on Qt::AutoConnection
// resolving to a direct call, which is only true while sender and
// receiver share a thread. Splitting the session onto its own thread
// later is Task 18's call to make, but if it does, every watched model,
// StateMirror, ObjectRegistry AND this dispatcher all have to move
// together, or all three invariants break at once.
//
// ── requestSliceSampleRate IS THE ONE ASYNCHRONOUS VERB ─────────────────
//
// RadioModel::requestSliceSampleRate can reach RadioModel::setSampleRateLive
// (RadioModel.cpp), whose own doc comment measures at least 40 ms of
// QThread::msleep across three calls. Since dispatch() runs on the model
// thread (above), calling that synchronously from inside dispatch() would
// stall the session read loop -- and therefore every OTHER inbound
// message, command or otherwise -- for the duration. handleRequestSliceSampleRate
// defers the actual RadioModel call to a LATER turn of that same thread's
// event loop via QMetaObject::invokeMethod(..., Qt::QueuedConnection), so
// dispatch() itself always returns immediately. Its CommandResult is only
// emitted once that queued call has actually run -- see
// commandResultReady()'s doc comment. The other three verbs have no such
// hazard and both run AND report synchronously, within the dispatch() call
// that requested them.
//
// ── SCOPE, NOT JUST SUCCESS ──────────────────────────────────────────────
//
// requestSliceSampleRate names one slice, but RadioModel can retune more
// than that slice in response to it: co-hosted slices share one DDC
// stream, so any rate change to that stream moves every slice on it, and
// on a Protocol 1 board requestSliceSampleRate can escalate all the way to
// RadioModel::setSampleRateLive's own 12-step, radio-wide sequence
// (RadioModel.cpp, sampleRateIsRadioWide()/setStreamSampleRate) -- every
// slice's sampleRateHz moves, not only the one named in the request.
// handleRequestSliceSampleRate does not special-case either mechanism: it
// snapshots every known slice's sampleRateHz before calling RadioModel,
// calls it, and reports whichever slices' values actually differ
// afterward as the CommandResult's affectedKeys. This is deliberately
// protocol- and topology-agnostic -- it reports what happened, not why.
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
//                                    Important 1 -- a remote operator's
//                                    active-slice click had no path to the
//                                    daemon). AI-assisted transformation
//                                    via Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 1: an
//                                    integer argument outside int's range
//                                    is now refused rather than truncated
//                                    (see dispatch()). AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QHash>
#include <functional>
#include <utility>

#include "core/session/SessionMessages.h"

namespace NereusSDR {

class RadioModel;

class SessionCommandDispatcher : public QObject {
    Q_OBJECT

public:
    using Ps3DisplayAdmissionHandler = std::function<bool(bool, QString*)>;
    void setPs3DisplayAdmissionHandler(Ps3DisplayAdmissionHandler handler)
    { m_ps3DisplayAdmission = std::move(handler); }
    /// `radioModel` is watched via QPointer, not owned: this class outlives
    /// or is outlived by it depending on which a future session's lifetime
    /// (Task 18) ties to which, and neither ordering should crash.
    explicit SessionCommandDispatcher(RadioModel* radioModel, QObject* parent = nullptr);

    /// Decode `invoke`'s verb and act on it.
    ///
    /// A malformed request (`invoke.kind` is not CommandInvoke, no
    /// RadioModel attached, an unrecognised commandVerb, a missing
    /// argument, or an integer argument outside the range RadioModel's
    /// own entry point can hold) produces an immediate, synchronous
    /// rejection via commandResultReady() before this call returns. The
    /// range check applies to requestSliceSampleRate too, so an
    /// out-of-range argument there is answered synchronously and its
    /// deferral (see the class comment) is never entered. A well-formed command's result may also arrive
    /// synchronously (addSlice, removeSlice, addSliceOnPan) or on a LATER
    /// turn of RadioModel's event loop (requestSliceSampleRate -- see the
    /// class comment). Callers must not assume commandResultReady() has
    /// fired by the time dispatch() itself returns.
    void dispatch(const NereusSDR::SessionMessage& invoke);
    void setSessionOwner(const QString& owner);

signals:
    /// Every CommandResult this dispatcher produces, in answer to some
    /// earlier dispatch() call. Task 18 encodes and relays each one back
    /// out over the wire, the same relationship StateMirror::
    /// sessionMessageReady() already has to its own outbound stream.
    void commandResultReady(const NereusSDR::SessionMessage& result);

private:
    Ps3DisplayAdmissionHandler m_ps3DisplayAdmission;
    void handleAddSlice(const NereusSDR::SessionMessage& invoke);
    void handleRemoveSlice(const NereusSDR::SessionMessage& invoke);
    void handleRequestSliceSampleRate(const NereusSDR::SessionMessage& invoke);
    void handleAddSliceOnPan(const NereusSDR::SessionMessage& invoke);
    void handleSetActiveSliceById(const NereusSDR::SessionMessage& invoke);
    void handleRequestStreamCtunPinned(const NereusSDR::SessionMessage& invoke);
    void handleRequestStreamCentre(const NereusSDR::SessionMessage& invoke);
    void handleConfigureTgxl(const NereusSDR::SessionMessage& invoke);
    void handleDisconnectTgxl(const NereusSDR::SessionMessage& invoke);
    void handleSetFourO3AEnabled(const NereusSDR::SessionMessage& invoke);
    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 2): the Core's Power
    // Genius XL, as configureTgxl / disconnectTgxl, plus its connection
    // settings.
    void handleConfigurePgxl(const NereusSDR::SessionMessage& invoke);
    void handleDisconnectPgxl(const NereusSDR::SessionMessage& invoke);
    void handleSetPgxlConnectionSettings(const NereusSDR::SessionMessage& invoke);
    // R-R3-47 / R-R3-22 (remoteRfKitControlVersion 2): the Core's RF-Kit
    // RF2K-S, and its switch.
    void handleConfigureRfKit(const NereusSDR::SessionMessage& invoke);
    void handleDisconnectRfKit(const NereusSDR::SessionMessage& invoke);
    void handleSetRfKitEnabled(const NereusSDR::SessionMessage& invoke);
    // I4 (R-R3-47, remoteRfKitControlVersion 3): Reset amp error.
    void handleResetRfKitError(const NereusSDR::SessionMessage& invoke);
    // R-R3-48 (stationTciVersion 1): the station's TCI switch and port.
    void handleSetStationTci(const NereusSDR::SessionMessage& invoke);
    // R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core's accessory
    // records and settings.
    void handleSetTxInterlockPolicy(const NereusSDR::SessionMessage& invoke);
    void handleSetPgxlPowerCap(const NereusSDR::SessionMessage& invoke);
    void handleClearAccessoryFaults(const NereusSDR::SessionMessage& invoke);
    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
    // 1): the amp's and tuner's own settings, sent by the Core to the device
    // as the local Advanced page's own commands.
    void handleAccessoryDeviceSettings(const NereusSDR::SessionMessage& invoke);
    void handleRequestIoBoardProbe(const NereusSDR::SessionMessage& invoke);
    // R-R3-46 fix wave (radioHardwareVersion 3): one band's RX or RX-only
    // antenna, applied through the Core's AlexAntennaFacade.
    void handleSetAlexRxAntenna(const NereusSDR::SessionMessage& invoke);
    // nnr.setDiagnostics, nnr.resetTuning and (R-R3-40, minor 11)
    // nnr.tryAgain, each addressed to one slice ID.
    void handleNnrAction(const NereusSDR::SessionMessage& invoke);
    // R-R3-21 / R-R3-09: notch.add / notch.move / notch.setActive /
    // notch.delete against the Core's NotchModel.
    void handleNotchAction(const NereusSDR::SessionMessage& invoke);
    void handlePureSignalAction(const NereusSDR::SessionMessage& invoke);

    void emitResult(const QByteArray& verb, quint32 commandId, bool accepted,
                    const QString& reason, const QList<QByteArray>& affectedKeys);

    QPointer<RadioModel> m_radioModel;
    QString m_sessionOwner{QStringLiteral("local")};
    struct PendingPureSignalCommand {
        quint32 commandId;
        QByteArray verb;
    };
    QHash<quint32, PendingPureSignalCommand> m_pureSignalCommands;
};

} // namespace NereusSDR
