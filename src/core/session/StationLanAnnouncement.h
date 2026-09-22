// NereusSDR-original bounded Core LAN discovery announcement contract.
#pragma once

#include <QByteArray>
#include <QHostAddress>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <optional>

namespace NereusSDR {

inline constexpr quint16 kStationLanDiscoveryPort = 47910;
inline constexpr char kStationLanIpv4MulticastGroup[] = "239.255.42.99";
inline constexpr char kStationLanIpv6MulticastGroup[] = "ff12::4e52:5344";
inline constexpr int kStationLanMulticastHopLimit = 1;
inline constexpr qint64 kStationLanAnnouncementIntervalMs = 5'000;
inline constexpr qint64 kStationLanCacheTtlMs = 15'000;
inline constexpr int kStationLanMaxDatagramBytes = 512;
inline constexpr int kStationLanMaxCoreNameBytes = 128;
inline constexpr int kStationLanMaxRadioNameBytes = 128;
inline constexpr quint8 kStationLanAnnouncementSchema = 1;
inline constexpr quint8 kStationLanWssControlService = 1;

struct StationLanAnnouncement {
    quint16 controlPort = 0;
    QString fingerprint;
    QString coreName;
    QString radioName;
    QString radioMac;
    bool radioConnected = false;
    bool operator==(const StationLanAnnouncement&) const = default;
};

QByteArray encodeStationLanAnnouncement(const StationLanAnnouncement& value,
                                        QString* error = nullptr);
std::optional<StationLanAnnouncement> decodeStationLanAnnouncement(
    const QByteArray& bytes, QString* error = nullptr);

struct StationLanEndpoint {
    StationLanAnnouncement announcement;
    QHostAddress address;
    uint interfaceIndex = 0;
    qint64 lastSeenMs = 0;

    QString key() const;
    QUrl url() const;
};

} // namespace NereusSDR
