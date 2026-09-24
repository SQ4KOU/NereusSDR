// =================================================================
// src/core/session/MirrorPolicy.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 7.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-05  J.J. Boyd / KG4VCF  Remote daemon R2 Task 7: mirror
//                                    direction table. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-40: SliceModel nnrLimit is
//                                    Outbound. AI-assisted implementation
//                                    via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-45: SliceModel outputRoute is
//                                    Bidirectional. AI-assisted
//                                    implementation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-11: the Core's step
//                                    attenuator and preamp (`stepAtt`).
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23 - R-R3-46: AlexAntennaFacade directions. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-23 - R-R3-46 fix wave: IoBoardHl2Facade, all Outbound. J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-23 - R-R3-47 / R-R3-22: AmplifierModel and RfKitModel, all
//                 Outbound. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-09-24 - R-R3-45: SliceModel outputRoute is Outbound until the
//                 headphones plan's remote window task makes it two-way.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-48: RfKitModel rows, bandFollow,
//                StationTciModel, all Outbound; rfKitEnabled Outbound. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-09-24 - Merge: lane B's headphones Task 2 makes outputRoute
//                 two-way; the Outbound entry is removed. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: AccessoryDataModel, all Outbound. J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: AccessorySettingsModel, all Outbound.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 13 (R-IOS-08): StationDevicesFacade, all
//                 Outbound. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
// =================================================================

#include "core/session/MirrorPolicy.h"

// For MirrorSchema::shortClassName only: QMetaObject::className()
// reports "NereusSDR::SliceModel" while this table (and hand-written
// call sites) use the bare name, and both must resolve to one entry.
// MirrorPolicy.h stays free of the dependency.
#include "core/session/MirrorSchema.h"

#include <QHash>

#include <iterator>

namespace NereusSDR {

namespace {

// Total over the mirrored surface, class by class, in declaration order.
// SliceModel::sliceLetter is absent because MirrorSchema excludes it from
// the surface outright (CONSTANT, derived from sliceIndex, and the only
// QChar anywhere in the mirror); MeterModel is absent because the whole
// class is excluded.
//
// Three groups are worth reading twice, because nothing in the
// meta-object system would produce them on its own:
//
//   1. Some SliceModel properties are Outbound DESPITE carrying WRITE.
//      The R2 plan names seven -- chainIndex, ddcIndex, streamIndex,
//      shiftOffsetHz, sampleRateHz, widebandExtensionRequested and
//      psPaused -- and every one is codec- or coordinator-owned state
//      that the daemon computes and the GUI only displays. sampleRateHz
//      is the clearest case: it is a property of the DDC stream, not of
//      the slice, and the way to change it is
//      RadioModel::requestSliceSampleRate. Writing the property directly
//      moves the display and nothing else, and the next bind overwrites
//      it.
//
//      Whole-branch review, Minor 1: that seven is the PLAN's list, not
//      the current total. snrDb and lastRadeRxCallsign joined the same
//      category later and are argued at their own entries below, which
//      already say the plan's seven is "a floor, not a cap". Reworded
//      here because reading this paragraph alone gave the wrong answer,
//      and because a StateMirror.cpp comment quoting the same stale seven
//      had drifted out of agreement with this file.
//
//   2. panKey is Bidirectional. The R2 plan's step 5 deliberately
//      corrects the design addendum's section 6.1 here, which counts
//      panKey among the outbound-only names; the addendum's own prose in
//      the same paragraph agrees with the plan, so that was a counting
//      slip rather than a design dispute. It is mirrored outbound so a
//      reconnecting client restores its layout, and applied inbound under
//      the mirror's guard, with pan-affecting CREATION routed through the
//      addSliceOnPan verb instead of through a property write.
//
//   3. sliceIndex is ConstantSnapshot. It is the mirror's object
//      identity, it carries no NOTIFY, and without an explicit snapshot
//      read every object.create on the wire would be anonymous.
//
// Everything read-only in the metaobject is Outbound here too, which is
// belt and braces: MirrorSchema::write already refuses a property with no
// WRITE. Listing them keeps the table total, so the guard can name a
// newly added property instead of silently accepting it.
const MirrorPolicy::Entry kEntries[] = {
    // ---- SliceModel (144 entries) ----
    { "SliceModel", "frequency", MirrorDirection::Bidirectional },
    { "SliceModel", "dspMode", MirrorDirection::Bidirectional },
    { "SliceModel", "filterLow", MirrorDirection::Bidirectional },
    { "SliceModel", "filterHigh", MirrorDirection::Bidirectional },
    { "SliceModel", "agcMode", MirrorDirection::Bidirectional },
    { "SliceModel", "stepHz", MirrorDirection::Bidirectional },
    { "SliceModel", "afGain", MirrorDirection::Bidirectional },
    { "SliceModel", "rfGain", MirrorDirection::Bidirectional },
    { "SliceModel", "rxAntenna", MirrorDirection::Bidirectional },
    { "SliceModel", "txAntenna", MirrorDirection::Bidirectional },
    { "SliceModel", "active", MirrorDirection::Outbound },
    { "SliceModel", "txSlice", MirrorDirection::Outbound },
    { "SliceModel", "sliceIndex", MirrorDirection::ConstantSnapshot },
    { "SliceModel", "band", MirrorDirection::Outbound },
    // Task 12: per-slice S-meter reading. Outbound -- the daemon's
    // SliceMeterPump produces the value; a remote client never writes it
    // back. No WRITE accessor at all (unlike snrDb/lastRadeRxCallsign
    // below, which carry WRITE and are refused via the writable-but-
    // Outbound path instead), so an inbound value for THIS property can
    // only ever reach the object through SliceModel::applyMirroredValue's
    // hook -- the design doc's "Inbound-only telemetry" phrasing describes
    // this same direction from the client's point of view; this table is
    // the daemon's point of view, and Outbound is the correct entry here.
    { "SliceModel", "signalStrengthDbm", MirrorDirection::Outbound },
    { "SliceModel", "signalPeakDbm", MirrorDirection::Outbound },
    { "SliceModel", "signalAverageDbm", MirrorDirection::Outbound },
    { "SliceModel", "stationAutoAgcNoiseFloorDbm", MirrorDirection::Outbound },
    { "SliceModel", "stationAutoAgcNoiseFloorValid", MirrorDirection::Outbound },
    { "SliceModel", "stationAutoAgcNoiseFloorGeneration", MirrorDirection::Outbound },
    { "SliceModel", "chainIndex", MirrorDirection::Outbound },
    { "SliceModel", "ddcIndex", MirrorDirection::Outbound },
    { "SliceModel", "streamIndex", MirrorDirection::Outbound },
    { "SliceModel", "streamCtunPinned", MirrorDirection::Outbound },
    { "SliceModel", "streamEpoch", MirrorDirection::Outbound },
    { "SliceModel", "shiftOffsetHz", MirrorDirection::Outbound },
    { "SliceModel", "panKey", MirrorDirection::Bidirectional },
    { "SliceModel", "sampleRateHz", MirrorDirection::Outbound },
    { "SliceModel", "diversityEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "diversityPhaseDeg", MirrorDirection::Bidirectional },
    { "SliceModel", "diversityGainDb", MirrorDirection::Bidirectional },
    { "SliceModel", "diversityFineNullEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "widebandExtensionRequested", MirrorDirection::Outbound },
    { "SliceModel", "psPaused", MirrorDirection::Outbound },
    { "SliceModel", "locked", MirrorDirection::Bidirectional },
    { "SliceModel", "muted", MirrorDirection::Bidirectional },
    { "SliceModel", "audioPan", MirrorDirection::Bidirectional },
    // R-R3-45: speakers or headphones (VAX design 6.2). The Core owns it:
    // the Core's mixer splits the two mixes and the Core saves the choice;
    // a remote window writes it from the flag and plays the headphones mix
    // the Core sends while any receiver is on the headphones.
    { "SliceModel", "outputRoute", MirrorDirection::Bidirectional },
    { "SliceModel", "ssqlEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "ssqlThresh", MirrorDirection::Bidirectional },
    { "SliceModel", "amsqEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "amsqThresh", MirrorDirection::Bidirectional },
    { "SliceModel", "fmsqEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "fmsqThresh", MirrorDirection::Bidirectional },
    { "SliceModel", "agcThreshold", MirrorDirection::Bidirectional },
    { "SliceModel", "agcHang", MirrorDirection::Bidirectional },
    { "SliceModel", "agcSlope", MirrorDirection::Bidirectional },
    { "SliceModel", "agcAttack", MirrorDirection::Bidirectional },
    { "SliceModel", "agcDecay", MirrorDirection::Bidirectional },
    { "SliceModel", "autoAgcEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "autoAgcOffset", MirrorDirection::Bidirectional },
    { "SliceModel", "agcFixedGain", MirrorDirection::Bidirectional },
    { "SliceModel", "agcHangThreshold", MirrorDirection::Bidirectional },
    { "SliceModel", "agcMaxGain", MirrorDirection::Bidirectional },
    { "SliceModel", "ritEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "ritHz", MirrorDirection::Bidirectional },
    { "SliceModel", "xitEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "xitHz", MirrorDirection::Bidirectional },
    { "SliceModel", "nbMode", MirrorDirection::Bidirectional },
    { "SliceModel", "activeNr", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrModelSlot", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrMaskFloorDb", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrPosition", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrAlpha", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrAlphaKneeDb", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrTauSeconds", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrMaxGainDb", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrAttackMs", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrReleaseMs", MirrorDirection::Bidirectional },
    { "SliceModel", "nnrAvailable", MirrorDirection::Outbound },
    { "SliceModel", "nnrReady", MirrorDirection::Outbound },
    { "SliceModel", "nnrRunning", MirrorDirection::Outbound },
    { "SliceModel", "nnrStandardAvailable", MirrorDirection::Outbound },
    { "SliceModel", "nnrPremiumAvailable", MirrorDirection::Outbound },
    { "SliceModel", "nnrRateSupported", MirrorDirection::Outbound },
    { "SliceModel", "nnrActualModelSlot", MirrorDirection::Outbound },
    { "SliceModel", "nnrDspRateHz", MirrorDirection::Outbound },
    { "SliceModel", "nnrNetworkRateHz", MirrorDirection::Outbound },
    { "SliceModel", "nnrDelaySamples", MirrorDirection::Outbound },
    { "SliceModel", "nnrProfilingAvailable", MirrorDirection::Outbound },
    { "SliceModel", "nnrLatencyMs", MirrorDirection::Outbound },
    { "SliceModel", "nnrTestMode", MirrorDirection::Outbound },
    { "SliceModel", "nnrOutputMode", MirrorDirection::Outbound },
    { "SliceModel", "nnrModelSource", MirrorDirection::Outbound },
    { "SliceModel", "nnrStatus", MirrorDirection::Outbound },
    { "SliceModel", "nnrLastError", MirrorDirection::Outbound },
    // R-R3-40: the Core's runtime step-back. Outbound only; the operator
    // clears it with the nnr.tryAgain command or by choosing a model, and
    // StationServer leaves it out for peers below minor 11.
    { "SliceModel", "nnrLimit", MirrorDirection::Outbound },
    { "SliceModel", "nr1Taps", MirrorDirection::Bidirectional },
    { "SliceModel", "nr1Delay", MirrorDirection::Bidirectional },
    { "SliceModel", "nr1Gain", MirrorDirection::Bidirectional },
    { "SliceModel", "nr1Leakage", MirrorDirection::Bidirectional },
    { "SliceModel", "nr1Position", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2GainMethod", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2NpeMethod", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2TrainT1", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2TrainT2", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2AeFilter", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Position", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Post2Run", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Post2Level", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Post2Factor", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Post2Rate", MirrorDirection::Bidirectional },
    { "SliceModel", "nr2Post2Taper", MirrorDirection::Bidirectional },
    { "SliceModel", "nr3Position", MirrorDirection::Bidirectional },
    { "SliceModel", "nr3UseDefaultGain", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4Reduction", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4Smoothing", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4Whitening", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4Rescale", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4PostThresh", MirrorDirection::Bidirectional },
    { "SliceModel", "nr4Algo", MirrorDirection::Bidirectional },
    { "SliceModel", "dfnrAttenLimit", MirrorDirection::Bidirectional },
    { "SliceModel", "dfnrPostFilterBeta", MirrorDirection::Bidirectional },
    { "SliceModel", "bnrStrength", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrStrength", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrOversub", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrFloor", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrAlpha", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrBias", MirrorDirection::Bidirectional },
    { "SliceModel", "mnrGsmooth", MirrorDirection::Bidirectional },
    { "SliceModel", "snbEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "anfEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "nb1Threshold", MirrorDirection::Bidirectional },
    { "SliceModel", "nb1TransitionMs", MirrorDirection::Bidirectional },
    { "SliceModel", "nb1LeadMs", MirrorDirection::Bidirectional },
    { "SliceModel", "nb1LagMs", MirrorDirection::Bidirectional },
    { "SliceModel", "nb2Mode", MirrorDirection::Bidirectional },
    { "SliceModel", "snbK1", MirrorDirection::Bidirectional },
    { "SliceModel", "snbK2", MirrorDirection::Bidirectional },
    { "SliceModel", "snbOutputBandwidthHz", MirrorDirection::Bidirectional },
    { "SliceModel", "apfEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "apfTuneHz", MirrorDirection::Bidirectional },
    { "SliceModel", "binauralEnabled", MirrorDirection::Bidirectional },
    { "SliceModel", "fmCtcssMode", MirrorDirection::Bidirectional },
    { "SliceModel", "fmCtcssValueHz", MirrorDirection::Bidirectional },
    { "SliceModel", "fmOffsetHz", MirrorDirection::Bidirectional },
    { "SliceModel", "fmTxMode", MirrorDirection::Bidirectional },
    { "SliceModel", "fmReverse", MirrorDirection::Bidirectional },
    { "SliceModel", "diglOffsetHz", MirrorDirection::Bidirectional },
    { "SliceModel", "diguOffsetHz", MirrorDirection::Bidirectional },
    { "SliceModel", "rttyMarkHz", MirrorDirection::Bidirectional },
    { "SliceModel", "rttyShiftHz", MirrorDirection::Bidirectional },
    // snrDb and lastRadeRxCallsign are daemon-produced RADE telemetry that
    // carries WRITE only so the decoder can set it. Outbound, and NOT
    // because they are read-only in spirit: writing either has a real
    // effect on station behaviour.
    //
    // Both setters restart the RADE idle-clear timer, not merely store:
    // SliceModel::setSnrDb on any non-NaN write (SliceModel.cpp:2308) and
    // setLastRadeRxCallsign on any non-empty write (:2332). A client
    // writing either more often than the idle window would suppress the
    // operator's idle clear indefinitely, pinning a stale callsign and SNR
    // on the local VFO flag.
    //
    // And nothing would overwrite a fabricated value. lastRadeRxCallsign's
    // only genuine writers are EOO decodes (RadioModel.cpp:1882, :5492),
    // which arrive seconds after a remote station FINISHES transmitting,
    // plus a clearing path (:5595). With no station on air there is no
    // next decode, so a fabricated callsign simply stays on the flag --
    // and that is the field an operator reads when logging a QSO.
    //
    // The R2 plan names seven writable properties that MUST be Outbound;
    // that is a floor, not a cap. Twenty further read-only properties are
    // Outbound here too.
    { "SliceModel", "snrDb", MirrorDirection::Outbound },
    { "SliceModel", "lastRadeRxCallsign", MirrorDirection::Outbound },

    // ---- TransmitModel (15 entries) ----
    { "TransmitModel", "mox", MirrorDirection::Bidirectional },
    { "TransmitModel", "tune", MirrorDirection::Bidirectional },
    { "TransmitModel", "power", MirrorDirection::Bidirectional },
    { "TransmitModel", "micGain", MirrorDirection::Bidirectional },
    { "TransmitModel", "pureSig", MirrorDirection::Bidirectional },
    { "TransmitModel", "filterLow", MirrorDirection::Bidirectional },
    { "TransmitModel", "filterHigh", MirrorDirection::Bidirectional },
    { "TransmitModel", "lineInGain", MirrorDirection::Bidirectional },
    { "TransmitModel", "userDigOut", MirrorDirection::Bidirectional },
    { "TransmitModel", "forceAttwhenPSAoff", MirrorDirection::Bidirectional },
    { "TransmitModel", "forceAttwhenPowerChangesWhenPSAon", MirrorDirection::Bidirectional },
    { "TransmitModel", "forceAttwhenPowerChangesWhenPSAonAndDecreased", MirrorDirection::Bidirectional },
    { "TransmitModel", "antiVoxTauMs", MirrorDirection::Bidirectional },
    { "TransmitModel", "antiVoxRun", MirrorDirection::Bidirectional },
    { "TransmitModel", "paSettingsBypass", MirrorDirection::Bidirectional },

    // ---- TunerModel (21 entries) ----
    { "TunerModel", "relayC1", MirrorDirection::Outbound },
    { "TunerModel", "relayL", MirrorDirection::Outbound },
    { "TunerModel", "relayC2", MirrorDirection::Outbound },
    { "TunerModel", "isOperate", MirrorDirection::Outbound },
    { "TunerModel", "isBypass", MirrorDirection::Outbound },
    { "TunerModel", "isTuning", MirrorDirection::Outbound },
    { "TunerModel", "antennaA", MirrorDirection::Outbound },
    { "TunerModel", "hasAntennaSwitch", MirrorDirection::Outbound },
    { "TunerModel", "isPresent", MirrorDirection::Outbound },
    { "TunerModel", "hasDirectConnection", MirrorDirection::Outbound },
    { "TunerModel", "tgxlIp", MirrorDirection::Outbound },
    { "TunerModel", "fwdPower", MirrorDirection::Outbound },
    { "TunerModel", "swr", MirrorDirection::Outbound },
    { "TunerModel", "configuredHost", MirrorDirection::Outbound },
    { "TunerModel", "configuredPort", MirrorDirection::Outbound },
    { "TunerModel", "connectionPhase", MirrorDirection::Outbound },
    { "TunerModel", "connectionError", MirrorDirection::Outbound },
    { "TunerModel", "deviceModel", MirrorDirection::Outbound },
    { "TunerModel", "deviceSerial", MirrorDirection::Outbound },
    { "TunerModel", "deviceVersion", MirrorDirection::Outbound },
    { "TunerModel", "deviceNickname", MirrorDirection::Outbound },

    { "PureSignalSessionFacade", "available", MirrorDirection::Outbound },
    { "PureSignalSessionFacade", "canActuate", MirrorDirection::Outbound },
    { "PureSignalSessionFacade", "twoToneOn", MirrorDirection::Outbound },
    { "PureSignalSessionFacade", "statusJson", MirrorDirection::Outbound },
    { "PureSignalSessionFacade", "lastActionError", MirrorDirection::Outbound },
    { "PureSignalSessionFacade", "displayGeneration", MirrorDirection::Outbound },

    { "DspAssetService", "nnrStandardAsset", MirrorDirection::Outbound },
    { "DspAssetService", "nnrPremiumAsset", MirrorDirection::Outbound },
    { "DspAssetService", "nnrModelSelectionPending", MirrorDirection::Outbound },
    { "DspAssetService", "nnrModelStatus", MirrorDirection::Outbound },
    { "DspAssetService", "selectionRevision", MirrorDirection::Outbound },
    // R-R3-21 (dspAssetVersion 2): the Core-wide NR3 model. Outbound only;
    // a window changes it with the dspAssets.selectNr3Model command.
    { "DspAssetService", "nr3ModelAsset", MirrorDirection::Outbound },
    { "DspAssetService", "nr3ModelStatus", MirrorDirection::Outbound },
    // Fix wave I3: false when the Core has no usable NR3 model, so a window
    // refuses turning NR3 on with nr3ModelStatus. An older Core never sends
    // it and the window keeps its default, true.
    { "DspAssetService", "nr3Runnable", MirrorDirection::Outbound },

    // R-R3-21 / R-R3-09 (notchControlVersion 1): the Core's notch list. The
    // list and its revision are Outbound; a window changes the list only
    // with the notch.* commands. The master enable and auto-increase are
    // plain two-way switches.
    { "NotchModel", "listJson", MirrorDirection::Outbound },
    { "NotchModel", "revision", MirrorDirection::Outbound },
    { "NotchModel", "globalEnabled", MirrorDirection::Bidirectional },
    { "NotchModel", "autoIncrease", MirrorDirection::Bidirectional },

    // R-R3-46 / R-R3-11 (radioHardwareVersion 1): the Core's step
    // attenuator and preamp. The operator settings are two-way; the Core
    // applies each through its own controller and answers with the value it
    // kept. The range, auto-attenuate's own state, the overload readings and
    // ADC sharing are the Core's to report.
    { "StepAttenuatorFacade", "enabled", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "attenuationDb", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "preampMode", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "rx1Preamp", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "autoAttEnabled", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "autoAttMode", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "autoAttUndo", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "autoAttUndoDelayMs", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "autoAttHoldMs", MirrorDirection::Bidirectional },
    { "StepAttenuatorFacade", "minDb", MirrorDirection::Outbound },
    { "StepAttenuatorFacade", "maxDb", MirrorDirection::Outbound },
    { "StepAttenuatorFacade", "autoAttApplied", MirrorDirection::Outbound },
    { "StepAttenuatorFacade", "overloadAdc0", MirrorDirection::Outbound },
    { "StepAttenuatorFacade", "overloadAdc1", MirrorDirection::Outbound },
    { "StepAttenuatorFacade", "adcLinked", MirrorDirection::Outbound },

    // R-R3-46 (radioHardwareVersion 2): the Core's Alex antenna settings.
    // The receive settings are two-way; the Core applies each through its
    // own AlexController and answers with the value it kept. The transmit
    // antennas and relays are the Core's to report until remote transmit.
    { "AlexAntennaFacade", "rxAntennas", MirrorDirection::Bidirectional },
    { "AlexAntennaFacade", "rxOnlyAntennas", MirrorDirection::Bidirectional },
    { "AlexAntennaFacade", "useTxAntennaForRx", MirrorDirection::Bidirectional },
    { "AlexAntennaFacade", "txAntennas", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "blockTxAnt2", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "blockTxAnt3", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "rxOutOnTx", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "ext1OutOnTx", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "ext2OutOnTx", MirrorDirection::Outbound },
    { "AlexAntennaFacade", "rxOutOverride", MirrorDirection::Outbound },

    // R-R3-46 (radioHardwareVersion 3): the Core's HL2 I/O board, as the
    // Core reads it; a window asks for a probe with a command.
    { "IoBoardHl2Facade", "detected", MirrorDirection::Outbound },
    { "IoBoardHl2Facade", "hardwareVersion", MirrorDirection::Outbound },
    { "IoBoardHl2Facade", "registers", MirrorDirection::Outbound },

    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 1): the Core's Power
    // Genius XL status. Every property is the Core's to report; a window
    // connects, configures and operates the amp only through commands.
    { "AmplifierModel", "connectionPhase", MirrorDirection::Outbound },
    { "AmplifierModel", "configuredHost", MirrorDirection::Outbound },
    { "AmplifierModel", "configuredPort", MirrorDirection::Outbound },
    { "AmplifierModel", "connectionError", MirrorDirection::Outbound },
    { "AmplifierModel", "deviceModel", MirrorDirection::Outbound },
    { "AmplifierModel", "deviceSerial", MirrorDirection::Outbound },
    { "AmplifierModel", "deviceVersion", MirrorDirection::Outbound },
    { "AmplifierModel", "deviceNickname", MirrorDirection::Outbound },
    { "AmplifierModel", "present", MirrorDirection::Outbound },
    { "AmplifierModel", "state", MirrorDirection::Outbound },
    { "AmplifierModel", "deviceState", MirrorDirection::Outbound },
    { "AmplifierModel", "operate", MirrorDirection::Outbound },
    { "AmplifierModel", "transmitting", MirrorDirection::Outbound },
    { "AmplifierModel", "forwardPowerW", MirrorDirection::Outbound },
    { "AmplifierModel", "swr", MirrorDirection::Outbound },
    { "AmplifierModel", "temperatureC", MirrorDirection::Outbound },
    { "AmplifierModel", "mainsVoltageV", MirrorDirection::Outbound },
    { "AmplifierModel", "drainCurrentA", MirrorDirection::Outbound },
    { "AmplifierModel", "efficiencyText", MirrorDirection::Outbound },
    // R-R3-48 (remotePgxlControlVersion 2): band follow, read-only.
    { "AmplifierModel", "bandFollow", MirrorDirection::Outbound },

    // R-R3-47 / R-R3-22 (remoteRfKitControlVersion 1): the Core's RF-Kit
    // RF2K-S status, read-only for the same reason.
    { "RfKitModel", "connectionPhase", MirrorDirection::Outbound },
    { "RfKitModel", "configuredHost", MirrorDirection::Outbound },
    { "RfKitModel", "configuredPort", MirrorDirection::Outbound },
    { "RfKitModel", "connectionError", MirrorDirection::Outbound },
    { "RfKitModel", "deviceModel", MirrorDirection::Outbound },
    { "RfKitModel", "deviceSerial", MirrorDirection::Outbound },
    { "RfKitModel", "deviceVersion", MirrorDirection::Outbound },
    { "RfKitModel", "deviceNickname", MirrorDirection::Outbound },
    { "RfKitModel", "present", MirrorDirection::Outbound },
    { "RfKitModel", "operate", MirrorDirection::Outbound },
    { "RfKitModel", "forwardPowerW", MirrorDirection::Outbound },
    { "RfKitModel", "reflectedPowerW", MirrorDirection::Outbound },
    { "RfKitModel", "swr", MirrorDirection::Outbound },
    { "RfKitModel", "temperatureC", MirrorDirection::Outbound },
    { "RfKitModel", "voltageV", MirrorDirection::Outbound },
    { "RfKitModel", "currentA", MirrorDirection::Outbound },
    // R-R3-47 / R-R3-48 (remoteRfKitControlVersion 2): the interface,
    // antenna and tuner rows and band follow, read-only.
    { "RfKitModel", "operationalInterface", MirrorDirection::Outbound },
    { "RfKitModel", "antennaPresentMask", MirrorDirection::Outbound },
    { "RfKitModel", "antennaDisabledMask", MirrorDirection::Outbound },
    { "RfKitModel", "activeAntennaNumber", MirrorDirection::Outbound },
    { "RfKitModel", "activeAntennaExternal", MirrorDirection::Outbound },
    { "RfKitModel", "tunerMode", MirrorDirection::Outbound },
    { "RfKitModel", "tunerSetup", MirrorDirection::Outbound },
    { "RfKitModel", "tunerInductanceNh", MirrorDirection::Outbound },
    { "RfKitModel", "tunerCapacitancePf", MirrorDirection::Outbound },
    { "RfKitModel", "tunerFrequencyKhz", MirrorDirection::Outbound },
    { "RfKitModel", "tunerSegmentKhz", MirrorDirection::Outbound },
    { "RfKitModel", "bandFollow", MirrorDirection::Outbound },
    { "RfKitModel", "bandFollowAddress", MirrorDirection::Outbound },
    { "RfKitModel", "bandFollowPort", MirrorDirection::Outbound },

    // R-R3-48 (stationTciVersion 1): the Core's station TCI server,
    // read-only. Its switch changes only through setStationTci.
    { "StationTciModel", "enabled", MirrorDirection::Outbound },
    { "StationTciModel", "port", MirrorDirection::Outbound },
    { "StationTciModel", "listening", MirrorDirection::Outbound },
    { "StationTciModel", "stationAddress", MirrorDirection::Outbound },
    { "StationTciModel", "error", MirrorDirection::Outbound },

    // R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core's accessory
    // records and settings, read-only. A window changes the interlock
    // policy, the output limit and a fault history only through
    // setTxInterlockPolicy, setPgxlPowerCap and clearAccessoryFaults.
    { "AccessoryDataModel", "faultRevision", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlFaults", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlFaults", MirrorDirection::Outbound },
    { "AccessoryDataModel", "rfkitFaults", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlConnectedSinceMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlLastRttMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlKeepaliveMissed", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlReconnectCount", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlFramesIn", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlFramesOut", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlBytesIn", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlBytesOut", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlLastFrameMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "pgxlFaultsSession", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlConnectedSinceMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlLastRttMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlKeepaliveMissed", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlReconnectCount", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlFramesIn", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlFramesOut", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlBytesIn", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlBytesOut", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlLastFrameMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlFaultsSession", MirrorDirection::Outbound },
    { "AccessoryDataModel", "interlockMode", MirrorDirection::Outbound },
    { "AccessoryDataModel", "interlockGraceMs", MirrorDirection::Outbound },
    { "AccessoryDataModel", "interlockSwrGateEnabled", MirrorDirection::Outbound },
    { "AccessoryDataModel", "interlockSwrGateMax", MirrorDirection::Outbound },
    { "AccessoryDataModel", "powerCapEnabled", MirrorDirection::Outbound },
    { "AccessoryDataModel", "powerCapW", MirrorDirection::Outbound },
    { "AccessoryDataModel", "powerCapExceeded", MirrorDirection::Outbound },
    { "AccessoryDataModel", "powerCapAlertText", MirrorDirection::Outbound },
    { "AccessoryDataModel", "powerCapAlertCount", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tuneMemory", MirrorDirection::Outbound },
    { "AccessoryDataModel", "autoTuneMemoryRecall", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlAntenna1Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlAntenna2Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "tgxlAntenna3Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "rfkitAntenna1Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "rfkitAntenna2Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "rfkitAntenna3Label", MirrorDirection::Outbound },
    { "AccessoryDataModel", "rfkitAntenna4Label", MirrorDirection::Outbound },

    // R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
    // 1): the amp's and tuner's own settings as the Core last heard them,
    // read-only. A window changes them only through the setPgxl* / setTgxl*,
    // save and read commands.
    { "AccessorySettingsModel", "pgxlNickname", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlBiasMode", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlFanMode", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlLedIntensity", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlNetworkKnown", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlDhcp", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlAddress", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlNetmask", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlGateway", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlAnswer", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlAnswerAccepted", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "pgxlAnswerCount", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlNickname", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlNetworkKnown", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlDhcp", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlAddress", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlNetmask", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlGateway", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlAnswer", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlAnswerAccepted", MirrorDirection::Outbound },
    { "AccessorySettingsModel", "tgxlAnswerCount", MirrorDirection::Outbound },

    // iPhone app Task 13 (R-IOS-08, deviceAdminVersion 1): the Core's paired
    // devices, label, claim, token and key backup, read-only. A device
    // changes them only through devices.revoke, station.rename,
    // station.acknowledgeKeyBackup and station.retireToken.
    { "StationDevicesFacade", "listJson", MirrorDirection::Outbound },
    { "StationDevicesFacade", "revision", MirrorDirection::Outbound },
    { "StationDevicesFacade", "stationLabel", MirrorDirection::Outbound },
    { "StationDevicesFacade", "claimed", MirrorDirection::Outbound },
    { "StationDevicesFacade", "tokenActive", MirrorDirection::Outbound },
    { "StationDevicesFacade", "keyBackupAcknowledged", MirrorDirection::Outbound },
    { "StationDevicesFacade", "keyPath", MirrorDirection::Outbound },

    // Normal PS3 configuration is distinct from operational arming/actions.
    { "PureSignalSettings", "autoCalEnabled", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "runCalibrationProcessing", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "autoAttenuate", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "quickAttenuate", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "moxDelaySeconds", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "loopDelaySeconds", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "requestedTxDelayNs", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "hardwarePeakOverrideEnabled", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "hardwarePeakOverride", MirrorDirection::Bidirectional },
    { "PureSignalSettings", "lastLoadError", MirrorDirection::Outbound },

    // ---- RadioModel (19 entries) ----
    { "RadioModel", "settingsSaveError", MirrorDirection::Outbound },
    { "RadioModel", "receiveLayoutRestoreState", MirrorDirection::Outbound },
    { "RadioModel", "receiveLayoutRestoreMessage", MirrorDirection::Outbound },
    { "RadioModel", "name", MirrorDirection::Outbound },
    { "RadioModel", "model", MirrorDirection::Outbound },
    { "RadioModel", "version", MirrorDirection::Outbound },
    { "RadioModel", "connected", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter0Mode", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter0Effective", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter0Band", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter0Reason", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter1Mode", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter1Effective", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter1Band", MirrorDirection::Outbound },
    { "RadioModel", "rxFilter1Reason", MirrorDirection::Outbound },
    // R-R3-47: the Core's RF-Kit switch. A window changes it with the
    // setRfKitEnabled command; a raw write is refused.
    { "RadioModel", "rfKitEnabled", MirrorDirection::Outbound },
    // The 4O3A listener and its bind error exist only at Core. A remote
    // client renders these observational values and must never write one
    // back into a listener, socket, or per-MAC settings scope.
    { "RadioModel", "fourO3AEnabled", MirrorDirection::Outbound },
    { "RadioModel", "fourO3AListening", MirrorDirection::Outbound },
    { "RadioModel", "fourO3AListenerError", MirrorDirection::Outbound },

    // ---- PanadapterModel (4 entries) ----
    { "PanadapterModel", "centerFrequency", MirrorDirection::Bidirectional },
    { "PanadapterModel", "bandwidth", MirrorDirection::Bidirectional },
    { "PanadapterModel", "dBmFloor", MirrorDirection::Bidirectional },
    { "PanadapterModel", "dBmCeiling", MirrorDirection::Bidirectional },

};

QByteArray makeKey(const QByteArray& className, const QByteArray& property)
{
    // '\0' rather than "::" so a property name containing a colon could
    // never collide with a class-qualified key.
    return MirrorSchema::shortClassName(className) + '\0' + property;
}

const QHash<QByteArray, MirrorDirection>& lookupTable()
{
    static const QHash<QByteArray, MirrorDirection> table = [] {
        QHash<QByteArray, MirrorDirection> t;
        t.reserve(static_cast<int>(std::size(kEntries)));
        for (const MirrorPolicy::Entry& e : kEntries) {
            t.insert(makeKey(QByteArray(e.className), QByteArray(e.property)),
                     e.direction);
        }
        return t;
    }();
    return table;
}

} // namespace

MirrorDirection MirrorPolicy::directionFor(const QByteArray& className,
                                           const QByteArray& property)
{
    // Default deny. An unclassified property is mirrored out and never
    // written back.
    return lookupTable().value(makeKey(className, property),
                               MirrorDirection::Outbound);
}

bool MirrorPolicy::inboundAllowed(const QByteArray& className,
                                  const QByteArray& property)
{
    return directionFor(className, property) == MirrorDirection::Bidirectional;
}

bool MirrorPolicy::hasExplicitEntry(const QByteArray& className,
                                    const QByteArray& property)
{
    return lookupTable().contains(makeKey(className, property));
}

const QList<MirrorPolicy::Entry>& MirrorPolicy::entries()
{
    static const QList<Entry> all(std::begin(kEntries), std::end(kEntries));
    return all;
}

} // namespace NereusSDR
