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
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: whether the Core
//                                    reports its Power Genius and RF-Kit
//                                    to this app, and whether the link to
//                                    the Core is up. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-48: the RF-Kit's
//                                    configure, disconnect and switch
//                                    requests, and the station TCI
//                                    switch. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: the accessory
//                                    records and settings
//                                    (accessoryDataVersion 1): interlock
//                                    policy, output limit, fault history.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: the amp's and
//                                    tuner's own settings
//                                    (remotePgxlControlVersion 3,
//                                    remoteTgxlControlVersion 1).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-21: the filter policy
//                                    request (radioHardwareVersion 4).
//                                    AI-assisted via Anthropic Claude Code.
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
        /// The id the command went out under, when sent and the link
        /// numbers its commands (0 otherwise). Its result arrives as
        /// RadioModel::stationCommandFinished with the same id, so a sender
        /// can tell its own command's result apart (the amp applets, the
        /// TCI switch), and a page that sent it can claim the Core's
        /// refusal (RadioModel::noteAccessoryRequestShownOnPage).
        quint32 commandId = 0;
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
    /// R-R3-47 / R-R3-22: the link to the Core is up and its first state has
    /// arrived. False while connecting and after the Core is lost, when a
    /// window's copy of the Core's readings is stale.
    virtual bool stationLinkReady() const { return false; }
    /// R-R3-47 / R-R3-22: the Core reports its Power Genius XL (the
    /// `amplifier` object) and its RF-Kit RF2K-S (the `rfkit` object) to
    /// this app. Both false on an older Core or link.
    virtual bool remoteAmplifierStatusAvailable() const { return false; }
    virtual bool remoteRfKitStatusAvailable() const { return false; }

    /// R-R3-47 / R-R3-22 (remotePgxlControlVersion 2): the Core's Power
    /// Genius XL is set up through the Core. Acceptance means the Core
    /// took the request; `amplifier`.connectionPhase says what happened.
    virtual bool remotePgxlControlAvailable() const { return false; }
    virtual CommandOutcome requestConfigurePgxl(const QString&, quint16)
    { return { false, QStringLiteral("The station does not support remote PGXL configuration.") }; }
    virtual CommandOutcome requestDisconnectPgxl()
    { return { false, QStringLiteral("The station does not support remote PGXL configuration.") }; }
    virtual CommandOutcome requestPgxlConnectionSettings(bool, int, int)
    { return { false, QStringLiteral("The station does not support remote PGXL configuration.") }; }

    /// R-R3-47 / R-R3-22 (remoteRfKitControlVersion 2): the Core's RF-Kit
    /// RF2K-S is set up and switched through the Core. Acceptance means the
    /// Core took the request; `rfkit`.connectionPhase and the `radio`
    /// object's rfKitEnabled say what happened.
    virtual bool remoteRfKitControlAvailable() const { return false; }
    virtual CommandOutcome requestConfigureRfKit(const QString&, quint16)
    { return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") }; }
    virtual CommandOutcome requestDisconnectRfKit()
    { return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") }; }
    virtual CommandOutcome requestRfKitEnabled(bool)
    { return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") }; }
    /// I4 (R-R3-47, remoteRfKitControlVersion 3): the RF-Kit page's
    /// connection settings, antenna names and Reset amp error work from a
    /// remote window. The settings and names travel as station settings,
    /// which the Core applies at once; Reset amp error is its own request.
    virtual bool rfKitSettingsAvailable() const { return false; }
    virtual CommandOutcome requestResetRfKitError()
    { return { false, QStringLiteral("This Core does not let this app reset the RF-Kit amplifier's error. Updating the Core may help.") }; }

    /// R-R3-48 (stationTciVersion 1): the Core runs its own TCI server on
    /// the station network, switched by this app's one TCI switch and port.
    virtual bool stationTciAvailable() const { return false; }
    virtual CommandOutcome requestStationTci(bool, quint16)
    { return { false, QStringLiteral("This Core has no TCI server for the station.") }; }
    /// R-R3-48: the Core this window uses runs on this computer and serves
    /// TCI here, so the window runs no TCI server of its own. Kept while the
    /// link is down (the Core keeps its server), false for another Core.
    virtual bool coreServesTciOnThisComputer() const { return false; }
    /// Rework part 2 (R-R3-48): whether the Core keeps a station TCI switch
    /// of its own yet (1), not yet (0: this window's switch seeds it), or
    /// its settings have not arrived on this link (-1).
    virtual int coreStationTciStored() const { return -1; }

    /// R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core shares its
    /// accessory records and settings (`accessoryData`) and takes these
    /// three changes. Acceptance means the Core took the request; the
    /// `accessoryData` object says what it now holds.
    virtual bool accessoryDataAvailable() const { return false; }
    virtual CommandOutcome requestTxInterlockPolicy(int, int, bool, double)
    { return { false, QStringLiteral("This Core does not share its amplifier and tuner settings with this app.") }; }
    virtual CommandOutcome requestPgxlPowerCap(bool, int)
    { return { false, QStringLiteral("This Core does not share its amplifier and tuner settings with this app.") }; }
    virtual CommandOutcome requestClearAccessoryFaults(const QString&)
    { return { false, QStringLiteral("This Core does not share its amplifier and tuner settings with this app.") }; }

    /// R-R3-47 / R-R3-22 (remotePgxlControlVersion 3): the Core sends the
    /// Power Genius's own settings (name, hardware, network, Save & Reboot,
    /// Revert) to the amp as the local Advanced page does. Acceptance means
    /// the request left for the amp; the amp's answer and values come back
    /// on `accessorySettings`.
    virtual bool pgxlDeviceSettingsAvailable() const { return false; }
    virtual CommandOutcome requestPgxlName(const QString&)
    { return { false, pgxlDeviceSettingsUnavailableReason() }; }
    /// `setting` is "biasMode" (ClassA or ClassAB), "fanMode" (Auto, Quiet
    /// or Continuous) or "ledIntensity" (0 to 100, as a number).
    virtual CommandOutcome requestPgxlHardware(const QString&, const QString&)
    { return { false, pgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestPgxlNetwork(bool, const QString&, const QString&, const QString&)
    { return { false, pgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestPgxlSaveAndRestart()
    { return { false, pgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestPgxlReadSettings()
    { return { false, pgxlDeviceSettingsUnavailableReason() }; }
    /// R-R3-47 / R-R3-22 (remoteTgxlControlVersion 1): the same for the
    /// Tuner Genius's own settings (name, network, Save & Reboot, Revert).
    virtual bool tgxlDeviceSettingsAvailable() const { return false; }
    virtual CommandOutcome requestTgxlName(const QString&)
    { return { false, tgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestTgxlNetwork(bool, const QString&, const QString&, const QString&)
    { return { false, tgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestTgxlSaveAndRestart()
    { return { false, tgxlDeviceSettingsUnavailableReason() }; }
    virtual CommandOutcome requestTgxlReadSettings()
    { return { false, tgxlDeviceSettingsUnavailableReason() }; }
    static QString pgxlDeviceSettingsUnavailableReason()
    { return QStringLiteral("This Core does not let this app change the Power Genius's own settings. Updating the Core may help."); }
    static QString tgxlDeviceSettingsUnavailableReason()
    { return QStringLiteral("This Core does not let this app change the Tuner Genius's own settings. Updating the Core may help."); }

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

    // R-R3-46 / R-R3-21 (radioHardwareVersion 4): the filter policy dialog
    // in a remote window. Whether the Core takes a filter policy change from
    // this app now, why not in plain words, and the request itself (chain
    // 0 or 1; mode 0 Auto, 1 Force filter, 2 Force bypass). The defaults
    // refuse, for links that did not negotiate it.
    virtual bool filterPolicyEditAvailable() const { return false; }
    virtual QString filterPolicyUnavailableReason() const
    { return QStringLiteral("This Core cannot change its filter policy for this app. Updating the Core may help."); }
    virtual CommandOutcome requestFilterPolicy(int /*chain*/, int /*mode*/)
    { return { false, filterPolicyUnavailableReason() }; }
};

} // namespace NereusSDR
