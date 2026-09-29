// =================================================================
// src/core/PttSource.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. Tracks which source asserted the most recent
// PTT/MOX state change. Phase 3P-H Diagnostics → Radio Status page
// surfaces this for users.
//
// No Thetis port at this layer.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-20 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-25 - iPhone app plan Task 35 (R-IOS-13): Remote, a paired
//                 device's key through the Core (RadioModel::keyedBy names
//                 which device). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include <QString>

namespace NereusSDR {

enum class PttSource {
    None      = 0,
    Mox       = 1,   // MOX button on the front console
    Vox       = 2,   // Voice-operated
    Cat       = 3,   // CAT command (rigctld / hamlib / etc.)
    MicPtt    = 4,   // Mic-element PTT ring
    Cw        = 5,   // Firmware keyer
    Tune      = 6,   // Tune button (reduced power carrier)
    TwoTone   = 7,   // Two-tone test generator
    Remote    = 8,   // A paired device's key through the Core (Task 35)
    Count
};

inline QString pttSourceLabel(PttSource s) {
    switch (s) {
        case PttSource::None:    return QStringLiteral("none");
        case PttSource::Mox:     return QStringLiteral("MOX");
        case PttSource::Vox:     return QStringLiteral("VOX");
        case PttSource::Cat:     return QStringLiteral("CAT");
        case PttSource::MicPtt:  return QStringLiteral("Mic PTT");
        case PttSource::Cw:      return QStringLiteral("CW");
        case PttSource::Tune:    return QStringLiteral("Tune");
        case PttSource::TwoTone: return QStringLiteral("2-Tone");
        case PttSource::Remote:  return QStringLiteral("Remote");
        default:                 return QStringLiteral("?");
    }
}

// Which source the Radio Status page shows for the radio's key, from what
// both kinds of window know about it: whether the radio is keyed, whether a
// paired device (not the station itself) holds the key, TUNE, the two-tone
// test, and the key's trigger (RadioModel::KeyedBy::trigger, which the Core
// sends to its windows as `txState`'s keyedTrigger). A local window and a
// remote window give the same inputs for the same key, so both pages agree.
inline PttSource pttSourceForKey(bool keyed, bool deviceKey, bool tuning,
                                 bool twoTone, const QString& trigger)
{
    if (!keyed) {
        return PttSource::None;
    }
    // A paired device's key is Remote; its VOX key shows as VOX.
    if (deviceKey) {
        return trigger == QLatin1String("vox") ? PttSource::Vox : PttSource::Remote;
    }
    if (twoTone || trigger == QLatin1String("twoTone")) {
        return PttSource::TwoTone;
    }
    if (tuning || trigger == QLatin1String("tune")) {
        return PttSource::Tune;
    }
    // The station's own keys (RemoteKeying::stationTrigger). A TCI key has
    // no pill of its own and shows with the other program keys as CAT.
    if (trigger == QLatin1String("radioPtt")) {
        return PttSource::MicPtt;
    }
    if (trigger == QLatin1String("vox")) {
        return PttSource::Vox;
    }
    if (trigger == QLatin1String("cat") || trigger == QLatin1String("tci")) {
        return PttSource::Cat;
    }
    return PttSource::Mox;
}

} // namespace NereusSDR
