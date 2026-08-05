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
//   1. Seven SliceModel properties are Outbound DESPITE carrying WRITE:
//      chainIndex, ddcIndex, streamIndex, shiftOffsetHz, sampleRateHz,
//      widebandExtensionRequested and psPaused. Every one is codec- or
//      coordinator-owned state that the daemon computes and the GUI only
//      displays. sampleRateHz is the clearest case: it is a property of
//      the DDC stream, not of the slice, and the way to change it is
//      RadioModel::requestSliceSampleRate. Writing the property directly
//      moves the display and nothing else, and the next bind overwrites
//      it.
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
    // ---- SliceModel (108 entries) ----
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
    { "SliceModel", "chainIndex", MirrorDirection::Outbound },
    { "SliceModel", "ddcIndex", MirrorDirection::Outbound },
    { "SliceModel", "streamIndex", MirrorDirection::Outbound },
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

    // ---- TunerModel (13 entries) ----
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

    // ---- RadioModel (5 entries) ----
    { "RadioModel", "name", MirrorDirection::Outbound },
    { "RadioModel", "model", MirrorDirection::Outbound },
    { "RadioModel", "version", MirrorDirection::Outbound },
    { "RadioModel", "connected", MirrorDirection::Outbound },
    { "RadioModel", "rfKitEnabled", MirrorDirection::Bidirectional },

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
