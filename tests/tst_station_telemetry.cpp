// no-port-check: NereusSDR-original telemetry wire contract tests (R-R3-32).
#include <QJsonDocument>
#include <QTest>
#include <limits>

#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"

using namespace NereusSDR;

namespace {
StationTelemetrySnapshot measured()
{
    StationTelemetrySnapshot sample;
    sample.sequence = 17;
    sample.sampledElapsedMs = 3400;
    sample.radio.connected = true;
    sample.radio.rxMbps = 7.25;
    sample.radio.txMbps = 0.0; // A measured zero must survive.
    sample.radio.rttMs = 3;
    sample.radio.rttAgeMs = 900;
    sample.audio.active = true;
    sample.audio.contextGeneration = 11;
    sample.audio.sourceFramesPerSecond = 48000;
    sample.audio.sourceDropsPerSecond = 0.5;
    sample.audio.encodedPacketsPerSecond = 25;
    sample.audio.encodeFailuresPerSecond = 0;
    sample.audio.sendAcceptedPerSecond = 24;
    sample.audio.sendRejectedPerSecond = 1;
    return sample;
}
}

class TestStationTelemetry : public QObject {
    Q_OBJECT
private slots:
    void measuredValuesRetainUnitsAndMeaning()
    {
        SessionMessage message;
        message.kind = SessionMessageKind::StationTelemetry;
        message.telemetry = measured();
        const QByteArray wire = SessionMessages::encode(message);
        QVERIFY(!wire.isEmpty());
        QVERIFY(wire.size() < kMaxStationTelemetryBytes);
        QCOMPARE(QJsonDocument::fromJson(wire).object().value("type").toString(),
                 QStringLiteral("station.metrics.v1"));
        SessionMessage decoded;
        QVERIFY(SessionMessages::decode(wire, &decoded));
        QCOMPARE(decoded.kind, SessionMessageKind::StationTelemetry);
        QCOMPARE(decoded.telemetry.sequence, 17u);
        QCOMPARE(decoded.telemetry.sampledElapsedMs, 3400);
        QCOMPARE(decoded.telemetry.radio.rxMbps, std::optional<double>(7.25));
        QCOMPARE(decoded.telemetry.radio.txMbps, std::optional<double>(0));
        QCOMPARE(decoded.telemetry.radio.rttMs, std::optional<qint64>(3));
        QCOMPARE(decoded.telemetry.radio.rttAgeMs, std::optional<qint64>(900));
        const auto& audio = decoded.telemetry.audio;
        QCOMPARE(audio.contextGeneration, 11u);
        QCOMPARE(audio.sourceFramesPerSecond, std::optional<double>(48000));
        QCOMPARE(audio.sourceDropsPerSecond, std::optional<double>(0.5));
        QCOMPARE(audio.encodedPacketsPerSecond, std::optional<double>(25));
        QCOMPARE(audio.encodeFailuresPerSecond, std::optional<double>(0));
        QCOMPARE(audio.sendAcceptedPerSecond, std::optional<double>(24));
        QCOMPARE(audio.sendRejectedPerSecond, std::optional<double>(1));
    }

    void unavailableStaysAbsentAndFutureFieldsAreIgnored()
    {
        StationTelemetrySnapshot sample;
        sample.sequence = 1;
        auto payload = StationTelemetryCodec::encode(sample);
        QVERIFY(payload);
        QVERIFY(!payload->value("radio").toObject().contains("rxMbps"));
        QVERIFY(!payload->value("radio").toObject().contains("rttMs"));
        payload->insert("futureObservation", QJsonObject{{"value", 7}});
        StationTelemetrySnapshot decoded = measured();
        QVERIFY(StationTelemetryCodec::decode(*payload, &decoded));
        QVERIFY(!decoded.radio.connected);
        QVERIFY(!decoded.radio.rxMbps);
        QVERIFY(!decoded.radio.rttMs);
        QVERIFY(!decoded.audio.active);
        QVERIFY(!decoded.audio.sourceFramesPerSecond);
        QVERIFY(!decoded.audio.sendAcceptedPerSecond);
    }

    void malformedSampleIsTransactional_data()
    {
        QTest::addColumn<QJsonObject>("payload");
        const QJsonObject valid = *StationTelemetryCodec::encode(measured());
        auto field = [&](const char* name, const char* key, const QJsonValue& value) {
            QJsonObject broken = valid;
            broken.insert(QString::fromLatin1(key), value);
            QTest::newRow(name) << broken;
        };
        field("missing-sequence", "sequence", QJsonValue(QJsonValue::Undefined));
        field("zero-sequence", "sequence", 0);
        field("fractional-sequence", "sequence", 1.5);
        field("oversized-sequence", "sequence", 4294967296.0);
        field("negative-time", "sampledElapsedMs", -1);
        field("inexact-time", "sampledElapsedMs", 9007199254740992.0);
        field("not-radio-object", "radio", true);
        auto radioField = [&](const char* name, const char* key, const QJsonValue& value) {
            QJsonObject radio = valid.value("radio").toObject();
            radio.insert(QString::fromLatin1(key), value);
            field(name, "radio", radio);
        };
        radioField("not-connected-bool", "connected", "true");
        radioField("negative-rate", "rxMbps", -0.1);
        radioField("null-is-not-absence", "rxMbps", QJsonValue(QJsonValue::Null));
        radioField("string-rate", "rxMbps", "7.25");
        radioField("fractional-rtt", "rttMs", 1.5);
        radioField("rtt-without-age", "rttAgeMs", QJsonValue(QJsonValue::Undefined));
        radioField("disconnected-with-current-rates", "connected", false);
        auto audioField = [&](const char* name, const char* key, const QJsonValue& value) {
            QJsonObject audio = valid.value("audio").toObject();
            audio.insert(QString::fromLatin1(key), value);
            field(name, "audio", audio);
        };
        audioField("active-without-context", "contextGeneration", 0);
        audioField("negative-audio-rate", "sendAcceptedPerSecond", -1);
        audioField("retired-with-current-rates", "active", false);
        audioField("not-active-bool", "active", 1);
    }

    void malformedSampleIsTransactional()
    {
        QFETCH(QJsonObject, payload);
        StationTelemetrySnapshot previous = measured();
        previous.sequence = 88;
        QVERIFY(!StationTelemetryCodec::decode(payload, &previous));
        QCOMPARE(previous.sequence, 88u);
        QCOMPARE(previous.radio.rxMbps, std::optional<double>(7.25));
    }

    void encoderRefusesNonFiniteAndInconsistentValues()
    {
        auto sample = measured();
        sample.radio.rxMbps = std::numeric_limits<double>::infinity();
        QVERIFY(!StationTelemetryCodec::encode(sample));
        sample = measured();
        sample.audio.sourceFramesPerSecond = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!StationTelemetryCodec::encode(sample));
        sample = measured();
        sample.radio.rttAgeMs.reset();
        QVERIFY(!StationTelemetryCodec::encode(sample));
        sample = measured();
        sample.sampledElapsedMs = std::numeric_limits<qint64>::max();
        QVERIFY(!StationTelemetryCodec::encode(sample));
    }

    void wholeMessageIsBoundedIncludingUnknownFields()
    {
        QJsonObject envelope{{"type", "station.metrics.v1"},
            {"payload", *StationTelemetryCodec::encode(measured())},
            {"future", QString(kMaxStationTelemetryBytes, QChar('x'))}};
        SessionMessage previous;
        previous.kind = SessionMessageKind::SnapshotComplete;
        QVERIFY(!SessionMessages::decode(QJsonDocument(envelope).toJson(), &previous));
        QCOMPARE(previous.kind, SessionMessageKind::SnapshotComplete);
        envelope.remove("future");
        envelope.insert("payload", QJsonValue(QJsonValue::Null));
        QVERIFY(!SessionMessages::decode(QJsonDocument(envelope).toJson(), &previous));
    }

    void olderCapabilitiesDefaultToUnsupported()
    {
        QCOMPARE(StationCapabilities::fromUpdates({}).stationTelemetryVersion, 0);
        StationCapabilities caps;
        caps.stationTelemetryVersion = 1;
        QCOMPARE(StationCapabilities::fromUpdates(caps.toUpdates()).stationTelemetryVersion, 1);
        caps.stationTelemetryVersion = -1;
        QCOMPARE(StationCapabilities::fromUpdates(caps.toUpdates()).stationTelemetryVersion, 0);
    }
};

QTEST_GUILESS_MAIN(TestStationTelemetry)
#include "tst_station_telemetry.moc"
