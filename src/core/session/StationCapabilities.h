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
    int remoteDisplayBudgetVersion = 0;
    std::optional<DisplayBudgetLimits> displayBudget;
    bool remotePs3DisplaySubscribed = false;
    int remoteCtunVersion = 0;
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
