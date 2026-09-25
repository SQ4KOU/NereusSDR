#pragma once
// =================================================================
// src/core/session/StationCapabilities.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
//
// The capability descriptor the daemon advertises immediately after
// authentication, and the client applies to its own RadioModel. Parent
// design section 7.0:
//
//   "Capability descriptor, advertised by the daemon: SKU, maxSlices,
//   userDdcCount, available modes, PureSignal present, wideband present,
//   TX permitted, supported audio codecs, supported display-codec
//   versions, max pixels, max frame rate, AppSettings schema version. The
//   client's UI gates on these."
//
// ---- EFFECTIVE, not board (parent section 4.5) ----
//
// "Capacity is a runtime property, not a SKU property. Section 7.0's
// capability descriptor currently advertises maxSlices and userDdcCount
// straight from BoardCapabilities, which describes what the RADIO
// supports. On the floor the DAEMON may not sustain that. The descriptor
// therefore advertises EFFECTIVE limits ... The client gates its UI on
// the effective values, never the board values."
//
// So `effectiveMaxSlices` is the number the client gates on, and
// `boardMaxSlices` travels alongside it for diagnostics ONLY -- so an
// operator looking at a station that will only give them two slices on a
// five-slice radio can see that it is a daemon decision and not a
// misidentified SKU. Nothing on the client may gate on boardMaxSlices;
// StationClient writes only the effective value into RadioModel.
//
// R2 has no PerfMonitor and no degradation ladder, so the effective value
// is whatever StationServer::setSustainableSliceLimit() was told, which
// defaults to the board value. Section 4.5's ladder (step 4, "Refuse
// additional slices beyond the sustainable count") is what eventually
// makes this number move at runtime; the field exists now so the client
// is already reading the right one when it does, rather than needing a
// protocol change on the day the ladder lands.
//
// ---- What R2 deliberately does NOT advertise ----
//
// Six of section 7.0's listed entries have no honest source in R2 and are
// therefore absent rather than present-and-fabricated: available modes,
// supported audio codecs, supported display-codec versions, max pixels,
// max frame rate, wideband present. R2's demo is explicitly "no spectrum
// trace, no waterfall, no sound" (design addendum section 2), so every one
// of those describes a subsystem this release does not carry over the
// link at all. A descriptor entry advertising a capability nobody
// implements is worse than a missing one: fromUpdates() below treats an
// absent entry as "the peer did not say", which is recoverable, whereas a
// zero or an empty list read as authoritative is not.
//
// Adding one later is a MINOR protocol bump plus a capability entry, which
// is exactly what section 7.0's version policy is for.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: capability
//                                    descriptor. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: the Core's radio model,
//                                    protocol and address. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22:
//                                    remotePgxlControlVersion and
//                                    remoteRfKitControlVersion, in the same
//                                    minor-11 block. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24 - R-R3-48: stationTciVersion, last in the
//                minor-11 block. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: accessoryDataVersion, last in the
//                minor-11 block. J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: remotePgxlControlVersion 3 and
//                remoteTgxlControlVersion 1 (the amp's and tuner's own
//                settings), the latter last in the minor-11 block. J.J.
//                Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 12 (R-IOS-08): stationIdentityVersion,
//                last in the minor-11 block. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 13 (R-IOS-08): deviceAdminVersion, last
//                in the minor-11 block. J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 14 (R-IOS-08): pairingVersion, last in
//                the minor-11 block. J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 19 (R-IOS-06): stationCatalogVersion,
//                last in the minor-11 block. J.J. Boyd (KG4VCF), AI-assisted
//                via Anthropic Claude Code.
//   2026-09-24 - iPhone app Task 20 (R-IOS-27): displayExtrasVersion,
//                last in the minor-11 block. J.J. Boyd (KG4VCF), AI-assisted
//                via Anthropic Claude Code.
//   2026-09-25: iPhone app Task 71 (R-IOS-02): sessionHolderVersion,
//               sent only to a peer that declared sessionHolder. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
// =================================================================

#include <QByteArray>
#include <QList>
#include <QString>

#include "core/HpsdrModel.h"
#include "core/session/MirrorSchema.h"
#include "core/session/media/DisplayBudget.h"

namespace NereusSDR {

struct StationCapabilities {
    // ---- Station identity ----
    // RadioModel only ever learns these from a connected radio, or from
    // this descriptor: name/model/version are mirrored Q_PROPERTYs with no
    // WRITE, and RadioModel::applyMirroredValue refuses all three
    // deliberately (see its own doc comment, which names this task).
    QString stationName;      ///< RadioModel::name(), e.g. "ANAN-G2E"
    QString radioModelName;   ///< RadioModel::model()
    QString firmwareVersion;  ///< RadioModel::version()
    QString macAddress;       ///< The connected radio's MAC, per-MAC settings scope
    HPSDRHW board = HPSDRHW::Unknown;

    // ---- The Core's radio (R-R3-46) ----
    // Sent only to a peer that negotiated kRadioIdentitySessionProtocolMinor
    // (radioIdentityEntries true); an older app receives exactly the
    // descriptor it was built for. Absent (an older Core) reads as the
    // defaults below, which a window treats as "the Core did not say".
    //
    // The three travel together: fromUpdates() sets radioIdentityEntries
    // when it sees any of them.
    bool radioIdentityEntries = false;
    /// The Core's own HardwareProfile model (an operator's model choice
    /// included), so a window resolves ANAN-8000DLE and ANAN-G2 1K rather
    /// than the first model on their board. FIRST: not reported, or a
    /// value this build does not know.
    HPSDRModel hpsdrModel = HPSDRModel::FIRST;
    /// ProtocolVersion as an integer: 1 or 2. 0: not reported.
    int radioProtocol = 0;
    /// The radio's LAN address as the Core sees it. Empty: not reported.
    QString radioAddress;
    /// R-R3-46 / R-R3-11: 1 means the Core mirrors its step attenuator and
    /// preamp as the `stepAtt` object and applies a window's edits to it
    /// through its own controller. 2 adds the Alex antenna settings
    /// (`alexAntennas`), the hardware apply step and the I/O board probe; 3
    /// the read-only `ioBoard` object and the setAlexRxAntenna command; 4
    /// the setAlexBpfMode command (a receive filter chain's filter policy,
    /// R-R3-46 / R-R3-21). Sent last in the same block as the three above,
    /// so only at minor 11. 0: a window keeps today's behaviour and does
    /// not write `stepAtt`.
    int radioHardwareVersion = 0;
    /// R-R3-47 / R-R3-22: 1 means the Core mirrors its Power Genius XL
    /// status as the read-only `amplifier` object. Sent after
    /// radioHardwareVersion in the same minor-11 block. 0: a window shows
    /// no Power Genius readings from this Core. 2 adds configurePgxl,
    /// disconnectPgxl and setPgxlConnectionSettings; 3 adds the amp's own
    /// settings (the pgxl* properties of the read-only `accessorySettings`
    /// object and the setPgxlName, setPgxlHardware, setPgxlNetwork,
    /// savePgxlSettings and readPgxlSettings commands).
    int remotePgxlControlVersion = 0;
    /// R-R3-47 / R-R3-22: 1 means the Core mirrors its RF-Kit RF2K-S status
    /// as the read-only `rfkit` object; 2 adds the interface, antenna and
    /// tuner rows, band follow, and the configureRfKit, disconnectRfKit and
    /// setRfKitEnabled commands. Sent in the same block.
    int remoteRfKitControlVersion = 0;
    /// R-R3-48: 1 means the Core runs its own TCI server on the station
    /// network, mirrored as the read-only `stationTci` object and switched
    /// by the setStationTci command. Sent in the same block. 0: a
    /// window's TCI switch changes only its own server.
    int stationTciVersion = 0;
    /// R-R3-47 / R-R3-22: 1 means the Core mirrors its accessory records
    /// and settings as the read-only `accessoryData` object (fault
    /// history, connection counters, interlock policy, output limit and
    /// its alert, tune memory, antenna names) and takes the
    /// setTxInterlockPolicy, setPgxlPowerCap and clearAccessoryFaults
    /// commands. Sent last in the same block.
    int accessoryDataVersion = 0;
    /// R-R3-47 / R-R3-22: 1 means the Core sends its Tuner Genius's own
    /// settings (the tgxl* properties of `accessorySettings`) and takes the
    /// setTgxlName, setTgxlNetwork, saveTgxlSettings and readTgxlSettings
    /// commands. Sent last in the same minor-11 block. 0: a window cannot
    /// change the tuner's own settings on this Core and says so.
    int remoteTgxlControlVersion = 0;
    /// iPhone app Task 12 (R-IOS-08): 1 means the Core has its own identity
    /// key and signs in paired devices by key (the hello's `identity` and
    /// `challenge`, auth.request's `device`). Sent last in the same
    /// minor-11 block. A client learns the same from the hello's
    /// `features.deviceAuth`, which it needs before capabilities arrive;
    /// this entry is what a signed-in window reads afterwards.
    int stationIdentityVersion = 0;
    /// iPhone app Task 13 (R-IOS-08): 1 means the Core sends the `devices`
    /// object (its paired devices, label, claim, token and key backup) to a
    /// device whose hello declares `deviceAuth` 1, and takes devices.revoke,
    /// station.rename, station.acknowledgeKeyBackup and station.retireToken.
    /// Sent in the same minor-11 block, after stationIdentityVersion.
    int deviceAdminVersion = 0;
    /// iPhone app Task 14 (R-IOS-08): 1 means the Core pairs devices (the
    /// `pair.*` messages, which a client learns before capabilities from the
    /// hello's `features.pairing`), keeps `pairingWindowOpen` and
    /// `pairingCode` on the `devices` object, and takes `pairing.open` and
    /// `pairing.close`. Sent in the same minor-11 block, after
    /// deviceAdminVersion.
    int pairingVersion = 0;
    /// iPhone app Task 19 (R-IOS-06): 1 means the Core sends the read-only
    /// `catalog` object (the modes, filter presets, tune steps, AGC and
    /// gauge ranges, board, band plans, palettes, slice colours and tools
    /// an app draws its controls from). Sent last in the same minor-11
    /// block.
    int stationCatalogVersion = 0;
    /// iPhone app Task 20 (R-IOS-27): 1 means a spectrum subscription may
    /// ask the Core for display extras (peak blobs, the active peak hold
    /// row, the noise floor, the waterfall's levels) and for normalise,
    /// calibration and averaging applied at the Core; the Core then sends
    /// an NSDX datagram beside each NSDC frame (display extras v1). Sent
    /// last in the same minor-11 block, after stationCatalogVersion.
    int displayExtrasVersion = 0;
    /// iPhone app Task 71 (R-IOS-02; the several-devices design, ruling
    /// 10.1): 1 means the Core admits up to four devices at once, sends the
    /// `connectedDevices` object and takes session.leave. Sent last in the
    /// same minor-11 block, and only to a peer whose hello declared the
    /// feature `sessionHolder` 1 with `deviceAuth` 1 (sessionHolderEntry);
    /// any other peer is sent no entry and reads 0, so its capabilities
    /// are exactly today's.
    bool sessionHolderEntry = false;
    int sessionHolderVersion = 0;

    /// Whether the DAEMON currently holds a live radio connection. A
    /// client that authenticated against a daemon whose radio is powered
    /// off must not present itself as Connected: every slice control it
    /// offers would reach a RadioModel that cannot act on it. See
    /// StationClient::applyCapabilities().
    bool radioConnected = false;

    // ---- Limits (see the header comment: EFFECTIVE, not board) ----
    int effectiveMaxSlices = 1;
    int boardMaxSlices = 1;   ///< diagnostics only; never gate on this
    int userDdcCount = 0;

    // ---- Feature bits ----
    bool pureSignalPresent = false;

    /// Always false in R2. TX is R4 in its entirety (design addendum
    /// section 2: "no MOX"), and section 12.2 requires TX stay disabled
    /// until the snapshot-complete marker has arrived regardless.
    bool txPermitted = false;

    /// Zero means control-only. Nonzero is advertised only when a daemon
    /// media controller is installed; negotiated session minor still gates it.
    int remoteMediaVersion = 0;
    int remoteWidebandDisplayVersion = 0;
    /// Audio contexts carry the accepted encoder profile or the reason audio
    /// is off. Nonzero only with media; negotiated minor still gates it.
    int remoteAudioStatusVersion = 0;
    /// Spectrum contexts report the grant Core made for the endpoint.
    /// Nonzero only with media; negotiated minor still gates it.
    int spectrumGrantVersion = 0;
    int remoteDisplayBudgetVersion = 0;
    std::optional<DisplayBudgetLimits> displayBudget;
    bool remotePs3DisplaySubscribed = false;
    /// Why the advertised display budget is below the Core's ceiling
    /// (R-R3-08, R-R3-37). A separate entry from the five budget fields,
    /// which older apps require to be exactly five: sent only with a budget
    /// and only to a peer that negotiated
    /// kDisplayBudgetReasonSessionProtocolMinor, parsed on its own, and
    /// dropped when the budget is not usable. nullopt: not sent, or a
    /// reason this build does not know.
    std::optional<DisplayBudgetReason> displayBudgetReason;
    int remoteCtunVersion = 0;
    /// 1: radio and audio telemetry. 2: adds the Core host section (CPU,
    /// memory, temperature). 3: adds the receivers section (each receiver's
    /// processing load and input wait). Negotiated minor still gates each
    /// version.
    int stationTelemetryVersion = 0;
    int remoteTgxlConfigVersion = 0;
    int remoteFourO3AControlVersion = 0;
    int wdspVersion = 0;
    int wdspCompatibilityVersion = 0;
    int nnrVersion = 0;
    int psAlgorithmVersion = 0;
    int propertyResultVersion = 0;
    int dspAssetVersion = 0;
    int psDisplayVersion = 0;
    /// R-R3-21 / R-R3-09: the Core owns the notch list, mirrors it as the
    /// `notches` object and takes notch.add / notch.move / notch.setActive
    /// / notch.delete. 0 means a window keeps today's settings-based notches.
    int notchControlVersion = 0;
    /// R-R3-23: 1 means the Core can send lossless audio (uncompressed
    /// 16-bit stereo, L16) beside Opus. A GUI that sees it may add
    /// audioProfileVersion to its media start (the offer then carries the
    /// L16 format) and `profile` to its audio control; the audio context
    /// then reports the profile running and any refusal. Nonzero only with
    /// media; the Core's audio_lossless setting may still refuse.
    int audioProfileVersion = 0;
    /// R-R3-35: 1 means the Core answers the media control
    /// {op:"clock-probe", id, t0} with {op:"clock-echo", id, t0, t1, t2,
    /// generation, rtpTimestamp, capturedNs}, so a GUI can measure how far
    /// behind real time its audio plays. Nonzero only with media. A GUI that
    /// does not see it sends no probe and shows no measured delay.
    int audioClockVersion = 0;
    /// R-R3-43: 1 means the Core can send a receiver's own audio on its own
    /// stream beside the speakers' mix. A GUI that sees it may add
    /// receiverAudioVersion to its media start (the offer then declares the
    /// receiver stream ids) and send {op:"receiver-audio", connectionId,
    /// sliceId, revision, enabled, profile}; the Core answers each with a
    /// receiver-audio-context. Nonzero only with media. A GUI that does not
    /// see it sends no receiver request and gets no receiver stream.
    int receiverAudioVersion = 0;
    /// R-R3-45: 1 means the Core can send the headphones mix (the receivers
    /// routed to the headphones) on its own stream beside the speakers'
    /// mix. A GUI that sees it may add headphonesMixVersion to its media
    /// start (the offer then declares the headphones stream id and the main
    /// stream carries the speakers' mix alone) and send {op:
    /// "headphones-audio", connectionId, revision, enabled, profile}; the
    /// Core answers with a headphones-audio-context. Nonzero only with
    /// media. A GUI that does not see it gets today's wire.
    int headphonesMixVersion = 0;

    /// The daemon's own AppSettings SettingsSchemaVersion, read by that
    /// key name from its own store. See StationClient's schema-skew check.
    qint32 settingsSchemaVersion = 0;

    /// The wire form: MirrorUpdate reused as a generic {name, kind, value}
    /// triple, exactly as CommandInvoke reuses it for arguments. `ordinal`
    /// is meaningless here and is always 0.
    QList<MirrorUpdate> toUpdates() const;

    /// Inverse of toUpdates(). Unknown entry names are IGNORED, not
    /// rejected: a newer daemon advertising a capability this client has
    /// never heard of is the expected forward-compatible case under
    /// section 7.0's "negotiate down on minor" policy, not a protocol
    /// error. Absent entries keep this struct's own defaults.
    static StationCapabilities fromUpdates(const QList<MirrorUpdate>& updates);
};

} // namespace NereusSDR
