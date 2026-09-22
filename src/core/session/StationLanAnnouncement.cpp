// NereusSDR-original strict Core LAN discovery codec.
#include "StationLanAnnouncement.h"

namespace NereusSDR {
namespace {

constexpr char kUnknownMac[] = "00:00:00:00:00:00";
constexpr int kFingerprintBytes = 95;
constexpr int kMacBytes = 17;

void setError(QString* error, const char* text)
{
    if (error) {
        *error = QString::fromLatin1(text);
    }
}

bool upperHex(char value)
{
    return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'F');
}

bool canonicalHexPairs(const QString& value, int expectedLength)
{
    if (value.size() != expectedLength) {
        return false;
    }
    for (int i = 0; i < value.size(); ++i) {
        const ushort unicode = value.at(i).unicode();
        if (unicode > 0x7f) {
            return false;
        }
        const char ascii = static_cast<char>(unicode);
        if ((i % 3 == 2 && ascii != ':') || (i % 3 != 2 && !upperHex(ascii))) {
            return false;
        }
    }
    return true;
}

bool validText(const QString& value, int minimumBytes, int maximumBytes)
{
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar character = value.at(i);
        if (character.isHighSurrogate()) {
            if (i + 1 >= value.size() || !value.at(i + 1).isLowSurrogate()) {
                return false;
            }
            const char32_t scalar = QChar::surrogateToUcs4(character, value.at(i + 1));
            if (QChar::category(scalar) == QChar::Other_Control) {
                return false;
            }
            ++i;
            continue;
        }
        if (character.isLowSurrogate() || character.category() == QChar::Other_Control) {
            return false;
        }
    }
    const QByteArray encoded = value.toUtf8();
    return encoded.size() >= minimumBytes && encoded.size() <= maximumBytes
        && encoded.isValidUtf8() && QString::fromUtf8(encoded) == value;
}

bool validate(const StationLanAnnouncement& value, QString* error)
{
    if (value.controlPort == 0) {
        setError(error, "Station LAN announcement has an invalid control port.");
        return false;
    }
    if (!canonicalHexPairs(value.fingerprint, kFingerprintBytes)) {
        setError(error, "Station LAN announcement has an invalid fingerprint.");
        return false;
    }
    if (!validText(value.coreName, 1, kStationLanMaxCoreNameBytes)) {
        setError(error, "Station LAN announcement has an invalid Core name.");
        return false;
    }
    if (!validText(value.radioName, value.radioConnected ? 1 : 0,
                   kStationLanMaxRadioNameBytes)) {
        setError(error, "Station LAN announcement has an invalid radio name.");
        return false;
    }
    if (!canonicalHexPairs(value.radioMac, kMacBytes)
        || (value.radioConnected && value.radioMac == QLatin1String(kUnknownMac))) {
        setError(error, "Station LAN announcement has an invalid radio MAC.");
        return false;
    }
    return true;
}

bool take(const QByteArray& bytes, int* offset, int count, QByteArray* result)
{
    if (count < 0 || *offset > bytes.size() - count) {
        return false;
    }
    *result = bytes.mid(*offset, count);
    *offset += count;
    return true;
}

bool takeByte(const QByteArray& bytes, int* offset, quint8* result)
{
    if (*offset >= bytes.size()) {
        return false;
    }
    *result = static_cast<quint8>(bytes.at((*offset)++));
    return true;
}

} // namespace

QByteArray encodeStationLanAnnouncement(const StationLanAnnouncement& value, QString* error)
{
    if (!validate(value, error)) {
        return {};
    }
    const QByteArray coreName = value.coreName.toUtf8();
    const QByteArray radioName = value.radioName.toUtf8();
    QByteArray out;
    out.reserve(4 + 1 + 1 + 2 + kFingerprintBytes + 1 + coreName.size() + 1 + 1
                + radioName.size() + kMacBytes);
    out.append("NRSC", 4);
    out.append(static_cast<char>(kStationLanAnnouncementSchema));
    out.append(static_cast<char>(kStationLanWssControlService));
    out.append(static_cast<char>(value.controlPort >> 8));
    out.append(static_cast<char>(value.controlPort & 0xff));
    out.append(value.fingerprint.toLatin1());
    out.append(static_cast<char>(coreName.size()));
    out.append(coreName);
    out.append(value.radioConnected ? '\x01' : '\x00');
    out.append(static_cast<char>(radioName.size()));
    out.append(radioName);
    out.append(value.radioMac.toLatin1());
    if (out.size() > kStationLanMaxDatagramBytes) {
        setError(error, "Station LAN announcement is too large.");
        return {};
    }
    if (error) {
        error->clear();
    }
    return out;
}

std::optional<StationLanAnnouncement> decodeStationLanAnnouncement(const QByteArray& bytes,
                                                                     QString* error)
{
    if (bytes.size() > kStationLanMaxDatagramBytes) {
        setError(error, "Station LAN announcement is too large.");
        return std::nullopt;
    }
    int offset = 0;
    QByteArray field;
    if (!take(bytes, &offset, 4, &field) || field != QByteArrayLiteral("NRSC")) {
        setError(error, "Station LAN announcement is malformed.");
        return std::nullopt;
    }
    quint8 schema = 0;
    if (!takeByte(bytes, &offset, &schema)) {
        setError(error, "Station LAN announcement is malformed.");
        return std::nullopt;
    }
    if (schema != kStationLanAnnouncementSchema) {
        setError(error, "Station LAN announcement has an unsupported schema.");
        return std::nullopt;
    }
    quint8 service = 0;
    if (!takeByte(bytes, &offset, &service)) {
        setError(error, "Station LAN announcement is malformed.");
        return std::nullopt;
    }
    if (service != kStationLanWssControlService) {
        setError(error, "Station LAN announcement has an unsupported service.");
        return std::nullopt;
    }
    quint8 high = 0, low = 0, coreLength = 0, connected = 0, radioLength = 0;
    QByteArray fingerprint, coreName, radioName, radioMac;
    if (!takeByte(bytes, &offset, &high) || !takeByte(bytes, &offset, &low)
        || !take(bytes, &offset, kFingerprintBytes, &fingerprint)
        || !takeByte(bytes, &offset, &coreLength)
        || !take(bytes, &offset, coreLength, &coreName)
        || !takeByte(bytes, &offset, &connected)
        || !takeByte(bytes, &offset, &radioLength)
        || !take(bytes, &offset, radioLength, &radioName)
        || !take(bytes, &offset, kMacBytes, &radioMac) || offset != bytes.size()) {
        setError(error, "Station LAN announcement is malformed.");
        return std::nullopt;
    }
    if (connected > 1) {
        setError(error, "Station LAN announcement has an invalid radio connection state.");
        return std::nullopt;
    }
    if (coreLength == 0 || coreLength > kStationLanMaxCoreNameBytes || !coreName.isValidUtf8()) {
        setError(error, "Station LAN announcement has an invalid Core name.");
        return std::nullopt;
    }
    if (radioLength > kStationLanMaxRadioNameBytes || !radioName.isValidUtf8()) {
        setError(error, "Station LAN announcement has an invalid radio name.");
        return std::nullopt;
    }
    StationLanAnnouncement value;
    value.controlPort = static_cast<quint16>((static_cast<quint16>(high) << 8) | low);
    value.fingerprint = QString::fromLatin1(fingerprint);
    value.coreName = QString::fromUtf8(coreName);
    value.radioName = QString::fromUtf8(radioName);
    value.radioMac = QString::fromLatin1(radioMac);
    value.radioConnected = connected == 1;
    if (!validate(value, error)) {
        return std::nullopt;
    }
    if (error) {
        error->clear();
    }
    return value;
}

QString StationLanEndpoint::key() const
{
    const QChar separator(0x1f);
    return announcement.fingerprint + separator + address.toString() + separator
        + address.scopeId() + separator + QString::number(interfaceIndex) + separator
        + QString::number(announcement.controlPort);
}

QUrl StationLanEndpoint::url() const
{
    QString host = address.toString();
    if (address.protocol() == QAbstractSocket::IPv6Protocol) {
        if (!address.scopeId().isEmpty() && !host.contains(QLatin1Char('%'))) {
            host.append(QLatin1Char('%'));
            host.append(address.scopeId());
        }
        host.replace(QLatin1Char('%'), QStringLiteral("%25"));
        return QUrl(QStringLiteral("wss://[%1]:%2").arg(host).arg(announcement.controlPort),
                    QUrl::StrictMode);
    }
    return QUrl(QStringLiteral("wss://%1:%2").arg(host).arg(announcement.controlPort),
                QUrl::StrictMode);
}

} // namespace NereusSDR
