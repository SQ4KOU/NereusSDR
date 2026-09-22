// Focused unit tests for untrusted Core LAN announcement codec/cache.
#include <QtTest>

#include "core/session/StationLanAnnouncement.h"
#include "core/session/StationLanCache.h"

using namespace NereusSDR;

namespace {

QString pin(int seed = 0)
{
    QString out;
    for (int i = 0; i < 32; ++i) {
        if (i) {
            out += QLatin1Char(':');
        }
        out += QString::number((seed + i) & 0xff, 16).rightJustified(2, QLatin1Char('0')).toUpper();
    }
    return out;
}

StationLanAnnouncement value(int seed = 0)
{
    return {47910, pin(seed), QStringLiteral("Rock 5C"), QStringLiteral("Saturn G2"),
            QStringLiteral("AA:BB:CC:DD:EE:FF"), true};
}

QByteArray wire(const StationLanAnnouncement& announcement)
{
    QString error;
    const QByteArray result = encodeStationLanAnnouncement(announcement, &error);
    Q_ASSERT(!result.isEmpty());
    Q_ASSERT(error.isEmpty());
    return result;
}

int connectionOffset(const QByteArray& bytes)
{
    return 8 + 95 + 1 + static_cast<quint8>(bytes.at(8 + 95));
}

} // namespace

class TstStationLanCache : public QObject {
    Q_OBJECT

private slots:
    void codecRoundTripAndStrictRejections()
    {
        const StationLanAnnouncement source = value();
        QString error;
        const QByteArray encoded = wire(source);
        const auto decoded = decodeStationLanAnnouncement(encoded, &error);
        QVERIFY2(decoded.has_value(), qPrintable(error));
        QCOMPARE(*decoded, source);

        StationLanAnnouncement nonBmp = source;
        nonBmp.coreName = QString::fromUtf8("Core \xF0\x9F\x9A\x80");
        const QByteArray nonBmpWire = encodeStationLanAnnouncement(nonBmp, &error);
        QVERIFY2(!nonBmpWire.isEmpty(), qPrintable(error));
        const auto nonBmpDecoded = decodeStationLanAnnouncement(nonBmpWire, &error);
        QVERIFY2(nonBmpDecoded.has_value(), qPrintable(error));
        QCOMPARE(*nonBmpDecoded, nonBmp);

        StationLanAnnouncement malformedSurrogate = source;
        malformedSurrogate.coreName = QString(QChar(0xd800));
        QVERIFY(encodeStationLanAnnouncement(malformedSurrogate, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid Core name."));
        malformedSurrogate.coreName = QString(QChar(0xdc00));
        QVERIFY(encodeStationLanAnnouncement(malformedSurrogate, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid Core name."));

        StationLanAnnouncement maximum = source;
        maximum.coreName = QString(kStationLanMaxCoreNameBytes, QLatin1Char('C'));
        maximum.radioName = QString(kStationLanMaxRadioNameBytes, QLatin1Char('R'));
        QVERIFY(!encodeStationLanAnnouncement(maximum, &error).isEmpty());
        maximum.coreName.append(QLatin1Char('C'));
        QVERIFY(encodeStationLanAnnouncement(maximum, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid Core name."));

        StationLanAnnouncement offline = source;
        offline.radioConnected = false;
        offline.radioName.clear();
        offline.radioMac = QStringLiteral("00:00:00:00:00:00");
        QVERIFY(!encodeStationLanAnnouncement(offline, &error).isEmpty());
        offline.radioConnected = true;
        QVERIFY(encodeStationLanAnnouncement(offline, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid radio name."));
        offline.radioName = QStringLiteral("Radio");
        QVERIFY(encodeStationLanAnnouncement(offline, &error).isEmpty());
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid radio MAC."));

        QByteArray invalid = encoded;
        invalid[4] = '\x02';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an unsupported schema."));
        invalid = encoded;
        invalid[5] = '\x02';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an unsupported service."));
        invalid = encoded;
        invalid[6] = invalid[7] = '\0';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid control port."));
        invalid = encoded;
        invalid[8] = 'a';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid fingerprint."));
        invalid = encoded;
        invalid[8 + 95 + 1] = '\n';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid Core name."));
        invalid = encoded;
        invalid[8 + 95 + 1] = static_cast<char>(0xff);
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid Core name."));
        invalid = encoded;
        invalid[connectionOffset(invalid)] = '\x02';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid radio connection state."));
        invalid = encoded;
        const int macOffset = connectionOffset(invalid) + 2
            + static_cast<quint8>(invalid.at(connectionOffset(invalid) + 1));
        invalid[macOffset] = 'a';
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid radio MAC."));
        invalid = encoded;
        invalid.append('x');
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement is malformed."));
        invalid = encoded;
        invalid.append(QByteArray(kStationLanMaxDatagramBytes, 'x'));
        QVERIFY(!decodeStationLanAnnouncement(invalid, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement is too large."));
    }

    void sourceScopeAndUrlRoundTrip()
    {
        StationLanCache cache;
        QHostAddress source(QStringLiteral("fe80::1234"));
        source.setScopeId(QStringLiteral("en7"));
        QString error;
        QVERIFY2(cache.ingest(wire(value()), source, 7, 100, &error), qPrintable(error));
        const StationLanEndpoint endpoint = cache.endpoints().first();
        QCOMPARE(endpoint.address.scopeId(), QStringLiteral("en7"));
        QCOMPARE(endpoint.interfaceIndex, 7u);
        const QUrl url = endpoint.url();
        QCOMPARE(url.scheme(), QStringLiteral("wss"));
        QCOMPARE(url.port(), 47910);
        const QHostAddress recovered(url.host(QUrl::FullyDecoded));
        QCOMPARE(recovered.protocol(), QAbstractSocket::IPv6Protocol);
        QCOMPARE(recovered.scopeId(), QStringLiteral("en7"));

        StationLanCache indexed;
        QHostAddress unscoped(QStringLiteral("fe80::5678"));
        QVERIFY(indexed.ingest(wire(value(1)), unscoped, 42, 100, &error));
        QCOMPARE(indexed.endpoints().first().address.scopeId(), QStringLiteral("42"));
        StationLanCache missingScope;
        QVERIFY(!missingScope.ingest(wire(value(2)), unscoped, 0, 100, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an unscoped link-local source."));
    }

    void cacheRefreshBoundsExpiryAndReadmission()
    {
        StationLanCache cache;
        const QHostAddress source(QStringLiteral("192.0.2.10"));
        QString error;
        QVERIFY(cache.ingest(wire(value()), source, 1, 100, &error));
        QVERIFY(!cache.ingest(wire(value()), source, 1, 200, &error));
        QCOMPARE(cache.endpoints().first().lastSeenMs, 200);
        StationLanAnnouncement changed = value();
        changed.radioName = QStringLiteral("Saturn G2E");
        QVERIFY(cache.ingest(wire(changed), source, 1, 300, &error));
        QCOMPARE(cache.endpoints().size(), 1);
        QCOMPARE(cache.endpoints().first().announcement.radioName, QStringLiteral("Saturn G2E"));

        for (int i = 1; i < StationLanCache::kMaxFingerprints; ++i) {
            QVERIFY2(cache.ingest(wire(value(i)), source, 1, 300, &error), qPrintable(error));
        }
        QCOMPARE(cache.endpoints().size(), StationLanCache::kMaxFingerprints);
        QVERIFY(!cache.ingest(wire(value(200)), source, 1, 300, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement cache is full."));
        QVERIFY(cache.ingest(wire(value(200)), source, 1, 300 + kStationLanCacheTtlMs, &error));
        QCOMPARE(cache.endpoints().size(), 1);
        QCOMPARE(cache.endpoints().first().announcement.fingerprint, pin(200));
    }

    void endpointBoundAndSourceValidation()
    {
        StationLanCache cache;
        const QByteArray encoded = wire(value());
        QString error;
        for (int i = 1; i <= StationLanCache::kMaxEndpointsPerFingerprint; ++i) {
            QVERIFY2(cache.ingest(encoded, QHostAddress(QStringLiteral("192.0.2.%1").arg(i)),
                                  static_cast<uint>(i), 100, &error), qPrintable(error));
        }
        QVERIFY(!cache.ingest(encoded, QHostAddress(QStringLiteral("192.0.2.99")), 99, 100,
                              &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement endpoint limit reached."));

        StationLanCache invalidSource;
        QVERIFY(!invalidSource.ingest(encoded, QHostAddress::AnyIPv4, 0, 100, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid source address."));
        QVERIFY(!invalidSource.ingest(encoded, QHostAddress(QStringLiteral("239.1.1.1")), 0,
                                      100, &error));
        QCOMPARE(error, QStringLiteral("Station LAN announcement has an invalid source address."));
        QVERIFY(invalidSource.ingest(encoded, QHostAddress::LocalHost, 0, 100, &error));
    }
};

QTEST_GUILESS_MAIN(TstStationLanCache)
#include "tst_station_lan_cache.moc"
