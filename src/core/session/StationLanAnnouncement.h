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
inline constexpr quint8 kStationLanWssControlService = 1;

// iPhone app Task 16 (D36, R-IOS-16): schema 2 adds the Core's identity,
// its label, whether it is claimed and how it pairs. A station sends
// schema 2 only (kStationLanAnnouncementSchema); a listener reads schema 1
// and schema 2, so a Core from before Task 16 is still found.
inline constexpr quint8 kStationLanAnnouncementSchema1 = 1;
inline constexpr quint8 kStationLanAnnouncementSchema2 = 2;
inline constexpr quint8 kStationLanAnnouncementSchema = kStationLanAnnouncementSchema2;
/// The identity fingerprint: SHA-256 of the identity key (StationIdentity).
inline constexpr int kStationLanIdentityBytes = 32;
/// StationLabel: a callsign of up to 32 characters, '/', and a suffix of up
/// to 32, so a label is never cut short.
inline constexpr int kStationLanMaxLabelBytes = 65;
/// The largest schema-2 datagram: both names and the label at their limits.
inline constexpr int kStationLanMaxSchema2DatagramBytes = 479;

/// How the Core accepts a new device right now (the pairing window, link
/// document section 3.6). On the wire: 0 closed, 1 click, 2 code.
enum class StationLanPairing : quint8 {
    Closed = 0, ///< The window is shut: nothing pairs.
    Click = 1,  ///< Unclaimed, and one tap on this network pairs (the code does too).
    Code = 2,   ///< Only the code pairs.
};

/// "closed", "click" or "code": the words the Bonjour TXT record and the
/// conformance expectations use.
QString stationLanPairingName(StationLanPairing pairing);
std::optional<StationLanPairing> stationLanPairingFromName(const QString& name);

struct StationLanAnnouncement {
    quint16 controlPort = 0;
    QString fingerprint;
    QString coreName;
    QString radioName;
    QString radioMac;
    bool radioConnected = false;
    /// kStationLanAnnouncementSchema1 or kStationLanAnnouncementSchema2. A
    /// schema-1 announcement carries none of the fields below.
    quint8 schema = kStationLanAnnouncementSchema1;
    bool claimed = false;
    /// kStationLanIdentityBytes raw bytes in schema 2.
    QByteArray identity;
    /// The StationLabel display, 0 to kStationLanMaxLabelBytes ASCII
    /// letters, digits, '/', '_' and '-'. Empty when the Core has none.
    QString label;
    StationLanPairing pairing = StationLanPairing::Closed;

    /// What a list shows: the label, or the Core name when there is none.
    QString displayName() const { return label.isEmpty() ? coreName : label; }

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
