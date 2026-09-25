#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/ConnectedDevicesFacade.h  (NereusSDR)
// =================================================================
//
// The mirrored `connectedDevices` object (iPhone app plan Task 71,
// R-IOS-02; the several-devices design, section 10.3, rulings 4.3 and
// 10.3): who is on the Core, the list a device's Devices page reads for
// "Connected now". Every property is outbound. StationServer sends it only
// at agreed minor 11 to a view whose hello declared `sessionHolder` 1 with
// `deviceAuth` 1 (sessionHolderVersion 1), so an older view never receives
// it.
//
//   listJson     a JSON array, one entry per device holding a place (live
//                or away), in admission order:
//                {deviceId, name, shortName, kind, paired, hostsCore,
//                 revocable, state, holdsTransmit, lastActivitySeconds,
//                 connectedForSeconds, awayForSeconds,
//                 transmittingForSeconds, listeningOn}
//                (transmittingOn is absent until Task 77; listeningOn is
//                [] until Task 73; holdsTransmit false and
//                transmittingForSeconds 0 until Task 34).
//   revision     moves by one with every change (serial-number arithmetic,
//                as `devices`' revision).
//   deviceLimit  4 (DeviceSessionRegistry::kMaxDeviceSessions).
//
// Names and short names are numbered by DeviceSessionRegistry::numberNames
// over the paired devices in pairing order, then a hosting desktop the
// store does not hold, then token windows in the order they connected: the
// same numbering the `devices` object uses, so one device reads the same on
// both lists.
//
// Durations are measured on the Core's monotonic clock (the registry's)
// each time listJson is read, which is when the mirror sends it (its
// object.create at attach, a delta on a change); never from the wall clock.
// The list changes, and is re-sent, only when something other than time
// passing changes in it; lastActivitySeconds moves at most once a minute
// per device (DeviceSessionRegistry::noteActivity).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 71 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QObject>
#include <QString>

namespace NereusSDR {

class DeviceSessionRegistry;
class DeviceStore;

class ConnectedDevicesFacade final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString listJson READ listJson NOTIFY connectedDevicesChanged)
    Q_PROPERTY(quint32 revision READ revision NOTIFY connectedDevicesChanged)
    Q_PROPERTY(int deviceLimit READ deviceLimit NOTIFY connectedDevicesChanged)

public:
    /// Neither is owned; both must outlive this object.
    ConnectedDevicesFacade(const DeviceSessionRegistry& registry, const DeviceStore& devices,
                           QObject* parent = nullptr);

    /// Measured now; see the header comment.
    QString listJson() const;
    quint32 revision() const { return m_revision; }
    int deviceLimit() const;

    /// Re-reads the registry and the paired devices; a change other than
    /// time passing moves revision once and notifies.
    void refresh();

    /// Holds refresh() back until the matching resumeRefresh(), which
    /// refreshes once: a sign-in (a new short name, then the admission) is
    /// one change.
    void holdRefresh() { ++m_hold; }
    void resumeRefresh();

signals:
    void connectedDevicesChanged();

private:
    /// The list without the durations measured from now: what decides
    /// whether it changed.
    QString stableForm() const;
    QString render(bool withDurations) const;

    const DeviceSessionRegistry& m_registry;
    const DeviceStore& m_devices;
    QString m_stable;
    quint32 m_revision = 0;
    int m_hold = 0;
    bool m_refreshWanted = false;
};

} // namespace NereusSDR
