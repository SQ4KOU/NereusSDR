// =================================================================
// src/core/session/StationCapabilities.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
// See StationCapabilities.h for the design rationale.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: capability
//                                    descriptor codec. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include "core/session/StationCapabilities.h"

#include "core/BoardCapabilities.h"

namespace NereusSDR {

namespace {

MirrorUpdate stringEntry(const char* name, const QString& value)
{
    return MirrorUpdate{ 0, QByteArray(name), MirrorWireKind::Utf8, QVariant(value) };
}

MirrorUpdate intEntry(const char* name, qint64 value)
{
    return MirrorUpdate{ 0, QByteArray(name), MirrorWireKind::Int64,
                         QVariant(static_cast<qlonglong>(value)) };
}

MirrorUpdate boolEntry(const char* name, bool value)
{
    return MirrorUpdate{ 0, QByteArray(name), MirrorWireKind::Bool, QVariant(value) };
}

} // namespace

QList<MirrorUpdate> StationCapabilities::toUpdates() const
{
    return {
        stringEntry("stationName", stationName),
        stringEntry("radioModel", radioModelName),
        stringEntry("firmwareVersion", firmwareVersion),
        stringEntry("macAddress", macAddress),
        intEntry("board", static_cast<qint64>(board)),
        boolEntry("radioConnected", radioConnected),
        intEntry("effectiveMaxSlices", effectiveMaxSlices),
        intEntry("boardMaxSlices", boardMaxSlices),
        intEntry("userDdcCount", userDdcCount),
        boolEntry("pureSignalPresent", pureSignalPresent),
        boolEntry("txPermitted", txPermitted),
        intEntry("remoteMediaVersion", remoteMediaVersion),
        intEntry("remoteCtunVersion", remoteCtunVersion),
        intEntry("stationTelemetryVersion", stationTelemetryVersion),
        intEntry("remoteTgxlConfigVersion", remoteTgxlConfigVersion),
        intEntry("remoteFourO3AControlVersion", remoteFourO3AControlVersion),
        intEntry("wdspVersion", wdspVersion),
        intEntry("wdspCompatibilityVersion", wdspCompatibilityVersion),
        intEntry("nnrVersion", nnrVersion),
        intEntry("psAlgorithmVersion", psAlgorithmVersion),
        intEntry("propertyResultVersion", propertyResultVersion),
        intEntry("dspAssetVersion", dspAssetVersion),
        intEntry("psDisplayVersion", psDisplayVersion),
        intEntry("settingsSchemaVersion", settingsSchemaVersion),
    };
}

StationCapabilities StationCapabilities::fromUpdates(const QList<MirrorUpdate>& updates)
{
    StationCapabilities caps;
    for (const MirrorUpdate& u : updates) {
        if (u.name == "stationName") {
            caps.stationName = u.value.toString();
        } else if (u.name == "radioModel") {
            caps.radioModelName = u.value.toString();
        } else if (u.name == "firmwareVersion") {
            caps.firmwareVersion = u.value.toString();
        } else if (u.name == "macAddress") {
            caps.macAddress = u.value.toString();
        } else if (u.name == "board") {
            // A board integer this build has never heard of resolves to
            // Unknown rather than being kept as an enum value no switch
            // here handles. HPSDRHW's values are a sparse, deliberately-
            // reserved range (HpsdrModel.h reserves 7..9 and 13..19), so a
            // newer daemon's SKU number can land in a hole this build has
            // no row for. The cast itself is well defined (HPSDRHW has a
            // fixed underlying type), so the check is semantic, not a
            // UB guard: BoardCapsTable::forBoard() falls back to the
            // Unknown row for anything it does not carry, and comparing
            // the row it hands back against what was asked for is the one
            // available way to tell "recognised" from "fell back" without
            // hand-copying the enum list into a second place that can
            // drift. Every consumer already has a fallback for Unknown.
            const auto candidate = static_cast<HPSDRHW>(u.value.toLongLong());
            caps.board = BoardCapsTable::forBoard(candidate).board == candidate
                             ? candidate
                             : HPSDRHW::Unknown;
        } else if (u.name == "radioConnected") {
            caps.radioConnected = u.value.toBool();
        } else if (u.name == "effectiveMaxSlices") {
            caps.effectiveMaxSlices = static_cast<int>(u.value.toLongLong());
        } else if (u.name == "boardMaxSlices") {
            caps.boardMaxSlices = static_cast<int>(u.value.toLongLong());
        } else if (u.name == "userDdcCount") {
            caps.userDdcCount = static_cast<int>(u.value.toLongLong());
        } else if (u.name == "pureSignalPresent") {
            caps.pureSignalPresent = u.value.toBool();
        } else if (u.name == "txPermitted") {
            caps.txPermitted = u.value.toBool();
        } else if (u.name == "remoteMediaVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteMediaVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "remoteCtunVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteCtunVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "stationTelemetryVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.stationTelemetryVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "remoteTgxlConfigVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteTgxlConfigVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "remoteFourO3AControlVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteFourO3AControlVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "wdspVersion" || u.name == "wdspCompatibilityVersion"
                   || u.name == "nnrVersion" || u.name == "psAlgorithmVersion"
                   || u.name == "propertyResultVersion" || u.name == "dspAssetVersion"
                   || u.name == "psDisplayVersion") {
            const qlonglong raw = u.value.toLongLong();
            const int version = raw >= 0 && raw <= 65535 ? static_cast<int>(raw) : 0;
            if (u.name == "wdspVersion") caps.wdspVersion = version;
            else if (u.name == "wdspCompatibilityVersion") caps.wdspCompatibilityVersion = version;
            else if (u.name == "nnrVersion") caps.nnrVersion = version;
            else if (u.name == "psAlgorithmVersion") caps.psAlgorithmVersion = version;
            else if (u.name == "propertyResultVersion") caps.propertyResultVersion = version;
            else if (u.name == "dspAssetVersion") caps.dspAssetVersion = version;
            else caps.psDisplayVersion = version;
        } else if (u.name == "settingsSchemaVersion") {
            caps.settingsSchemaVersion = static_cast<qint32>(u.value.toLongLong());
        }
        // Anything else: ignored on purpose. See fromUpdates()'s doc
        // comment -- a newer daemon advertising more is the expected
        // forward-compatible case, not an error.
    }
    return caps;
}

} // namespace NereusSDR
