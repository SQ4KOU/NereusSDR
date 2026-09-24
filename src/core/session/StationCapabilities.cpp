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
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: hpsdrModel, radioProtocol and
//                                    radioAddress entries. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46: radioHardwareVersion, last
//                                    in the same minor-11 block.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22:
//                                    remotePgxlControlVersion and
//                                    remoteRfKitControlVersion after it.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-48: stationTciVersion, last in the
//                minor-11 block. J.J. Boyd (KG4VCF),
//                AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: accessoryDataVersion, last in the
//                minor-11 block. J.J. Boyd (KG4VCF), AI-assisted via
//                Anthropic Claude Code.
// =================================================================

#include "core/session/StationCapabilities.h"

#include "core/BoardCapabilities.h"
#include <QHostAddress>
#include <QSet>
#include <limits>

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
    const bool hasBudget = remoteDisplayBudgetVersion > 0 && displayBudget
        && displayBudget->isValid();
    QList<MirrorUpdate> updates{
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
        intEntry("remoteWidebandDisplayVersion", remoteWidebandDisplayVersion),
        intEntry("remoteAudioStatusVersion", remoteAudioStatusVersion),
        intEntry("spectrumGrantVersion", spectrumGrantVersion),
        intEntry("remoteDisplayBudgetVersion", hasBudget ? remoteDisplayBudgetVersion : 0),
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
        intEntry("notchControlVersion", notchControlVersion),
        intEntry("audioProfileVersion", audioProfileVersion),
        intEntry("audioClockVersion", audioClockVersion),
        intEntry("receiverAudioVersion", receiverAudioVersion),
        intEntry("headphonesMixVersion", headphonesMixVersion),
        intEntry("settingsSchemaVersion", settingsSchemaVersion),
    };
    if (hasBudget) {
        updates.append(intEntry("displayApplicationBytesPerSecond",
                                static_cast<qint64>(displayBudget->applicationBytesPerSecond)));
        updates.append(intEntry("spectrumSampleUnitsPerSecond",
                                static_cast<qint64>(displayBudget->spectrumSampleUnitsPerSecond)));
        updates.append(intEntry("displayBudgetGeneration", displayBudget->generation));
        updates.append(boolEntry("remotePs3DisplaySubscribed", remotePs3DisplaySubscribed));
        if (displayBudgetReason) {
            updates.append(stringEntry("displayBudgetReason",
                                       displayBudgetReasonWireName(*displayBudgetReason)));
        }
    }
    // R-R3-46: last, so an app that negotiated them sees today's descriptor
    // followed by the three, and one that did not sees today's descriptor.
    if (radioIdentityEntries) {
        updates.append(intEntry("hpsdrModel", static_cast<qint64>(hpsdrModel)));
        updates.append(intEntry("radioProtocol", radioProtocol));
        updates.append(stringEntry("radioAddress", radioAddress));
        updates.append(intEntry("radioHardwareVersion", radioHardwareVersion));
        // R-R3-47 / R-R3-22: the Core's amplifier and RF-Kit status objects.
        updates.append(intEntry("remotePgxlControlVersion", remotePgxlControlVersion));
        updates.append(intEntry("remoteRfKitControlVersion", remoteRfKitControlVersion));
        // R-R3-48: the Core's station TCI server.
        updates.append(intEntry("stationTciVersion", stationTciVersion));
        // R-R3-47 / R-R3-22: the Core's accessory records and settings, last.
        updates.append(intEntry("accessoryDataVersion", accessoryDataVersion));
    }
    return updates;
}

StationCapabilities StationCapabilities::fromUpdates(const QList<MirrorUpdate>& updates)
{
    StationCapabilities caps;
    DisplayBudgetLimits budget;
    QSet<QByteArray> budgetFields;
    bool invalidBudget = false;
    int reasonEntries = 0;
    std::optional<DisplayBudgetReason> reason;
    for (const MirrorUpdate& u : updates) {
        if (u.name == "displayBudgetReason") {
            // Not one of the five budget fields: an older app ignores it,
            // and a bad reason never costs this app its budget.
            ++reasonEntries;
            if (u.kind == MirrorWireKind::Utf8 && u.value.typeId() == QMetaType::QString) {
                reason = displayBudgetReasonFromWireName(u.value.toString());
            }
        } else if (u.name == "remoteDisplayBudgetVersion"
            || u.name == "displayApplicationBytesPerSecond"
            || u.name == "spectrumSampleUnitsPerSecond"
            || u.name == "displayBudgetGeneration"
            || u.name == "remotePs3DisplaySubscribed") {
            if (budgetFields.contains(u.name)) { invalidBudget = true; }
            budgetFields.insert(u.name);
            if (u.name == "remotePs3DisplaySubscribed") {
                if (u.kind != MirrorWireKind::Bool || u.value.typeId() != QMetaType::Bool) {
                    invalidBudget = true;
                } else {
                    caps.remotePs3DisplaySubscribed = u.value.toBool();
                }
                continue;
            }
            if (u.kind != MirrorWireKind::Int64 || u.value.typeId() != QMetaType::LongLong) {
                invalidBudget = true;
                continue;
            }
            const qint64 value = u.value.toLongLong();
            if (u.name == "remoteDisplayBudgetVersion") {
                if (value < 0 || value > 65535) { invalidBudget = true; }
                else { caps.remoteDisplayBudgetVersion = static_cast<int>(value); }
            } else if (u.name == "displayBudgetGeneration") {
                if (value <= 0 || quint64(value) > std::numeric_limits<quint32>::max()) {
                    invalidBudget = true;
                } else { budget.generation = static_cast<quint32>(value); }
            } else if (value <= 0) {
                invalidBudget = true;
            } else if (u.name == "displayApplicationBytesPerSecond") {
                budget.applicationBytesPerSecond = static_cast<quint64>(value);
            } else {
                budget.spectrumSampleUnitsPerSecond = static_cast<quint64>(value);
            }
        } else if (u.name == "stationName") {
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
        } else if (u.name == "remoteWidebandDisplayVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteWidebandDisplayVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "remoteAudioStatusVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.remoteAudioStatusVersion = version >= 0 && version <= 65535
                ? static_cast<int>(version) : 0;
        } else if (u.name == "spectrumGrantVersion") {
            const qlonglong version = u.value.toLongLong();
            caps.spectrumGrantVersion = version >= 0 && version <= 65535
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
                   || u.name == "psDisplayVersion" || u.name == "notchControlVersion"
                   || u.name == "audioProfileVersion" || u.name == "audioClockVersion"
                   || u.name == "receiverAudioVersion"
                   || u.name == "headphonesMixVersion") {
            const qlonglong raw = u.value.toLongLong();
            const int version = raw >= 0 && raw <= 65535 ? static_cast<int>(raw) : 0;
            if (u.name == "wdspVersion") caps.wdspVersion = version;
            else if (u.name == "wdspCompatibilityVersion") caps.wdspCompatibilityVersion = version;
            else if (u.name == "nnrVersion") caps.nnrVersion = version;
            else if (u.name == "psAlgorithmVersion") caps.psAlgorithmVersion = version;
            else if (u.name == "propertyResultVersion") caps.propertyResultVersion = version;
            else if (u.name == "dspAssetVersion") caps.dspAssetVersion = version;
            else if (u.name == "notchControlVersion") caps.notchControlVersion = version;
            else if (u.name == "audioProfileVersion") caps.audioProfileVersion = version;
            else if (u.name == "audioClockVersion") caps.audioClockVersion = version;
            else if (u.name == "receiverAudioVersion") caps.receiverAudioVersion = version;
            else if (u.name == "headphonesMixVersion") caps.headphonesMixVersion = version;
            else caps.psDisplayVersion = version;
        } else if (u.name == "hpsdrModel") {
            // R-R3-46. A model this build has never heard of (a newer Core's
            // SKU) reads as not reported, so the window falls back to the
            // board alone rather than holding an enum value no switch here
            // handles.
            caps.radioIdentityEntries = true;
            if (u.kind == MirrorWireKind::Int64 && u.value.typeId() == QMetaType::LongLong) {
                const qlonglong raw = u.value.toLongLong();
                caps.hpsdrModel = raw > static_cast<qlonglong>(HPSDRModel::FIRST)
                        && raw < static_cast<qlonglong>(HPSDRModel::LAST)
                    ? static_cast<HPSDRModel>(raw) : HPSDRModel::FIRST;
            }
        } else if (u.name == "radioProtocol") {
            caps.radioIdentityEntries = true;
            if (u.kind == MirrorWireKind::Int64 && u.value.typeId() == QMetaType::LongLong) {
                const qlonglong raw = u.value.toLongLong();
                caps.radioProtocol = raw == 1 || raw == 2 ? static_cast<int>(raw) : 0;
            }
        } else if (u.name == "radioAddress") {
            caps.radioIdentityEntries = true;
            if (u.kind == MirrorWireKind::Utf8 && u.value.typeId() == QMetaType::QString) {
                // Only an address: anything else is not shown as one.
                const QString text = u.value.toString().trimmed();
                caps.radioAddress = QHostAddress(text).isNull() ? QString() : text;
            }
        } else if (u.name == "radioHardwareVersion") {
            // R-R3-46: sent in the same block as the three above.
            caps.radioIdentityEntries = true;
            if (u.kind == MirrorWireKind::Int64 && u.value.typeId() == QMetaType::LongLong) {
                const qlonglong raw = u.value.toLongLong();
                caps.radioHardwareVersion = raw >= 0 && raw <= 65535 ? static_cast<int>(raw) : 0;
            }
        } else if (u.name == "remotePgxlControlVersion"
                   || u.name == "remoteRfKitControlVersion"
                   || u.name == "stationTciVersion"
                   || u.name == "accessoryDataVersion") {
            // R-R3-47 / R-R3-22 / R-R3-48: sent in the same block as the
            // four above.
            caps.radioIdentityEntries = true;
            if (u.kind == MirrorWireKind::Int64 && u.value.typeId() == QMetaType::LongLong) {
                const qlonglong raw = u.value.toLongLong();
                const int version = raw >= 0 && raw <= 65535 ? static_cast<int>(raw) : 0;
                if (u.name == "remotePgxlControlVersion") {
                    caps.remotePgxlControlVersion = version;
                } else if (u.name == "remoteRfKitControlVersion") {
                    caps.remoteRfKitControlVersion = version;
                } else if (u.name == "stationTciVersion") {
                    caps.stationTciVersion = version;
                } else {
                    caps.accessoryDataVersion = version;
                }
            }
        } else if (u.name == "settingsSchemaVersion") {
            caps.settingsSchemaVersion = static_cast<qint32>(u.value.toLongLong());
        }
        // Anything else: ignored on purpose. See fromUpdates()'s doc
        // comment -- a newer daemon advertising more is the expected
        // forward-compatible case, not an error.
    }
    if (!invalidBudget && budgetFields.size() == 5
        && caps.remoteDisplayBudgetVersion > 0 && budget.isValid()) {
        caps.displayBudget = budget;
        if (reasonEntries == 1) {
            caps.displayBudgetReason = reason;
        }
    } else {
        caps.remoteDisplayBudgetVersion = 0;
        caps.remotePs3DisplaySubscribed = false;
    }
    return caps;
}

} // namespace NereusSDR
