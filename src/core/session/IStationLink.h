#pragma once
// =================================================================
// src/core/session/IStationLink.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 4.
//
// Non-owning seam RadioModel holds (attachStation / detachStation,
// RadioModel.h) for the control-plane link a remote GUI drives its
// station through. StationClient (core/session/StationClient.h) is the
// one production implementation; it turns each call below into a
// CommandInvoke on the wss session and lets SessionCommandDispatcher act
// on the DAEMON's RadioModel.
//
// ── WHY THE SEAM CARRIES COMMANDS AND NOT PROPERTIES ─────────────────────
//
// Property state already has a path in both directions: StateMirror
// pushes it out and StationClient applies it in. What had no path at all
// was the operator's CLICK. Five RadioModel entry points MUTATE the slice
// list or the active slice rather than writing a property, so no amount
// of property mirroring can carry them, and MirrorPolicy correctly
// refuses to send the daemon-authoritative properties they move
// (SliceModel::active is Outbound, so an optimistic local flip was sent
// nowhere, changed nothing on the daemon, and therefore drew no
// corrective delta back: a silent, PERMANENT divergence).
//
// ── WHY IT IS TYPED, AND NOT invoke(verb, arguments) ─────────────────────
//
// A generic verb-plus-bag signature would drag SessionMessages' wire
// types (MirrorUpdate, MirrorWireKind) into src/models, and would move
// the "did I spell the verb right" question from compile time to a
// bench. Five methods is not speculative surface: each one has exactly
// one call site, in the RadioModel entry point of the same name, and
// each maps to a verb SessionCommandDispatcher::dispatch() already
// accepts.
//
// ── ASYNCHRONOUS BY CONSTRUCTION ─────────────────────────────────────────
//
// CommandOutcome answers "did this leave the client", never "did the
// station do it". The station's answer arrives later: as a CommandResult
// (refusals reach the operator through RadioModel::
// reportStationSliceCommandRejected / reportStationRetuneRejected) and,
// for anything that worked, as ordinary mirror traffic. An implementation
// must not apply the change locally on the way out, or it rebuilds the
// divergence this seam exists to close.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-04  J.J. Boyd / KG4VCF  Remote daemon R2 Task 4: station-link
//                                    seam. AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Remote daemon R2: grow the seam into
//                                    the five slice command verbs, so the
//                                    GUI's slice controls reach the
//                                    daemon at all. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QString>
#include <QtGlobal>

namespace NereusSDR {

/// Non-owning control-plane seam. RadioModel holds a pointer to one via
/// attachStation()/detachStation() without owning or including anything
/// about the transport underneath it; the concrete implementation lives
/// in core/session/StationClient.h.
class IStationLink {
public:
    virtual ~IStationLink() = default;

    /// Whether the request left this process, and why not when it did
    /// not. Deliberately NOT the station's verdict: see the header
    /// comment. `reason` is plain English, ready for a status bar, and is
    /// meaningful only when `sent` is false.
    struct CommandOutcome {
        bool sent = false;
        QString reason;
    };

    /// SessionCommandDispatcher verb "addSlice", argument initialPanId.
    virtual CommandOutcome requestAddSlice(const QString& initialPanId) = 0;

    /// Verb "addSliceOnPan", argument panId. Distinct from requestAddSlice
    /// because the daemon derives its stream placement from whether the
    /// named pan already holds slices (R2 design addendum section 6.1:
    /// "routes pan-affecting creation through addSliceOnPan"), so the two
    /// produce different placements for the same end state.
    virtual CommandOutcome requestAddSliceOnPan(const QString& panId) = 0;

    /// Verb "removeSlice", argument sliceId.
    virtual CommandOutcome requestRemoveSlice(int sliceId) = 0;

    /// Verb "setActiveSliceById", argument sliceId.
    virtual CommandOutcome requestActiveSlice(int sliceId) = 0;

    /// Verb "requestSliceSampleRate", arguments sliceId and rateHz.
    virtual CommandOutcome requestSliceSampleRate(int sliceId, int rateHz) = 0;

    // R3 remote C-Tune. Default refusals retain source compatibility for
    // older test links and transports that do not negotiate this capability.
    virtual CommandOutcome requestStreamCtunPinned(int, bool)
    { return { false, QStringLiteral("Remote C-Tune is not supported by this station link.") }; }
    virtual CommandOutcome requestStreamCentre(int, double)
    { return { false, QStringLiteral("Remote C-Tune is not supported by this station link.") }; }

    // Task 4d remote TGXL configuration.  Defaults preserve existing test
    // links and transports which have not negotiated the accessory feature.
    virtual CommandOutcome requestConfigureTgxl(const QString&, quint16)
    { return { false, QStringLiteral("Remote TGXL configuration is not supported by this station link.") }; }
    virtual CommandOutcome requestDisconnectTgxl()
    { return { false, QStringLiteral("Remote TGXL configuration is not supported by this station link.") }; }

    // Task 4d remote 4O3A master control.  The station owns both the
    // persisted per-MAC intent and the listener; a remote GUI only asks it
    // to change that intent and waits for its mirrored state to return.
    virtual CommandOutcome requestFourO3AEnabled(bool)
    { return { false, QStringLiteral("Remote 4O3A control is not supported by this station link.") }; }

    /// Feature gate for accessory controls.  The default keeps every
    /// existing link inert until it explicitly implements the negotiated
    /// station capability.
    virtual bool remoteTgxlConfigAvailable() const { return false; }
    virtual bool remoteFourO3AControlAvailable() const { return false; }
    virtual CommandOutcome requestApplyNnrModels(quint32)
    { return { false, QStringLiteral("NNR model application is not supported by this station link.") }; }
    virtual bool nnrControlAvailable() const { return false; }
    virtual CommandOutcome requestNnrDiagnostics(int, int, int)
    { return { false, QStringLiteral("NNR diagnostics are not supported by this station link.") }; }

    // R-R3-46 (radioHardwareVersion 2): ask the Core to probe its radio's
    // HL2 I/O board. The default refuses, for links that did not negotiate
    // the Core's hardware settings.
    virtual CommandOutcome requestIoBoardProbe()
    { return { false, QStringLiteral("This Core cannot probe its radio's I/O board for this app.") }; }
};

} // namespace NereusSDR
