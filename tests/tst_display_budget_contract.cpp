// =================================================================
// tests/tst_display_budget_contract.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original protocol and lifecycle contract tests.
//
// Session display budget Task 2: pins the configured descriptor at the
// actual SessionMessages wire codec and at StationServer/StationClient's
// negotiated-session boundary. No radio, audio device, or network socket is
// opened; the production consumers communicate through LoopbackTransport.
// =================================================================

#include <QtTest/QtTest>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <limits>
#include <memory>
#include <optional>

#include "core/AppSettings.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "models/RadioModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

constexpr quint64 kBytes = 2400000;
constexpr quint64 kSamples = 1800000;

class ImmediatePs3ResultTransport final : public LoopbackTransport {
public:
    explicit ImmediatePs3ResultTransport(QObject* parent)
        : LoopbackTransport(QStringLiteral("immediate-gui"), parent) {}
    void sendText(const QByteArray& wire) override
    {
        SessionMessage request;
        if (reply && SessionMessages::decode(wire, &request)
            && request.kind == SessionMessageKind::CommandInvoke
            && request.commandVerb == "ps3.subscribeDisplay") {
            ++requests;
            lastReply = SessionMessages::encode(SessionMessages::commandResult(
                request.commandVerb, request.commandId, true, {}, {}));
            emit textReceived(lastReply);
            return;
        }
        LoopbackTransport::sendText(wire);
    }
    bool reply = false;
    int requests = 0;
    QByteArray lastReply;
};

DisplayBudgetLimits limits(quint32 generation, quint64 bytes = kBytes,
                           quint64 samples = kSamples)
{
    return DisplayBudgetLimits{bytes, samples, generation};
}

StationCapabilities budgetCapabilities(quint32 generation = 1, bool ps3 = false,
                                       quint64 bytes = kBytes,
                                       quint64 samples = kSamples)
{
    StationCapabilities capabilities;
    capabilities.stationName = QStringLiteral("display-budget-test");
    capabilities.remoteMediaVersion = 1;
    capabilities.remoteDisplayBudgetVersion = 1;
    capabilities.displayBudget = limits(generation, bytes, samples);
    capabilities.remotePs3DisplaySubscribed = ps3;
    return capabilities;
}

std::optional<SessionMessage> wireRoundTrip(const QList<MirrorUpdate>& updates)
{
    const QByteArray wire =
        SessionMessages::encode(SessionMessages::capabilities(updates));
    SessionMessage decoded;
    if (wire.isEmpty() || !SessionMessages::decode(wire, &decoded)) {
        return std::nullopt;
    }
    return decoded;
}

int updateIndex(const QList<MirrorUpdate>& updates, const QByteArray& name)
{
    for (int i = 0; i < updates.size(); ++i) {
        if (updates.at(i).name == name) {
            return i;
        }
    }
    return -1;
}

void sendCapabilities(LoopbackTransport* station,
                      const StationCapabilities& capabilities)
{
    station->sendText(SessionMessages::encode(
        SessionMessages::capabilities(capabilities.toUpdates())));
}

void completeFakeStationHandshake(LoopbackTransport* station, StationClient* client,
                                  quint16 minor,
                                  const StationCapabilities& capabilities)
{
    station->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, minor, 0, QStringLiteral("fake-station"))));
    QTRY_VERIFY(station->receivedKinds().contains(QByteArrayLiteral("auth.request")));
    station->sendText(SessionMessages::encode(
        SessionMessages::authResult(true, QString(), false)));
    sendCapabilities(station, capabilities);
    station->sendText(SessionMessages::encode(SessionMessages::settingsSnapshot({})));
    station->sendText(SessionMessages::encode(SessionMessages::snapshotComplete()));
    QTRY_VERIFY(client->isHandshakeComplete());
}

void compareBudget(const std::optional<DisplayBudgetLimits>& actual,
                   const DisplayBudgetLimits& expected)
{
    QVERIFY(actual.has_value());
    QCOMPARE(actual->applicationBytesPerSecond, expected.applicationBytesPerSecond);
    QCOMPARE(actual->spectrumSampleUnitsPerSecond,
             expected.spectrumSampleUnitsPerSecond);
    QCOMPARE(actual->generation, expected.generation);
}

} // namespace

class TstDisplayBudgetContract : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_securityDir.isValid());
        const QString profile = QStringLiteral("display-budget-contract-%1")
                                    .arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void protocolMinorAndCapabilityVersionAreAllocated()
    {
        QCOMPARE(kSessionProtocolMinor, quint16(10));
        QCOMPARE(kRemoteDisplayBudgetSessionProtocolMinor, quint16(7));
        QCOMPARE(kRemoteAudioStatusSessionProtocolMinor, quint16(8));
        QCOMPARE(kRemoteSpectrumGrantSessionProtocolMinor, quint16(9));

        const StationCapabilities capabilities = budgetCapabilities();
        QCOMPARE(capabilities.remoteDisplayBudgetVersion, 1);
    }

    void completeDescriptorRoundTripsThroughTheActualWireCodec()
    {
        const quint64 jsonMaximum = kDisplayBudgetJsonSafePositiveLimit;
        const StationCapabilities sent =
            budgetCapabilities(27, true, jsonMaximum, kSamples);
        const std::optional<SessionMessage> message = wireRoundTrip(sent.toUpdates());
        QVERIFY(message.has_value());
        QCOMPARE(message->kind, SessionMessageKind::Capabilities);

        const StationCapabilities received =
            StationCapabilities::fromUpdates(message->updates);
        QCOMPARE(received.remoteDisplayBudgetVersion, 1);
        compareBudget(received.displayBudget, *sent.displayBudget);
        QVERIFY(received.remotePs3DisplaySubscribed);

        const QList<QByteArray> integerFields{
            QByteArrayLiteral("remoteDisplayBudgetVersion"),
            QByteArrayLiteral("displayApplicationBytesPerSecond"),
            QByteArrayLiteral("spectrumSampleUnitsPerSecond"),
            QByteArrayLiteral("displayBudgetGeneration"),
        };
        for (const QByteArray& name : integerFields) {
            const int index = updateIndex(message->updates, name);
            QVERIFY2(index >= 0, name.constData());
            QCOMPARE(message->updates.at(index).kind, MirrorWireKind::Int64);
            QCOMPARE(message->updates.at(index).value.typeId(), QMetaType::LongLong);
        }
        const int ps3 = updateIndex(message->updates,
                                    QByteArrayLiteral("remotePs3DisplaySubscribed"));
        QVERIFY(ps3 >= 0);
        QCOMPARE(message->updates.at(ps3).kind, MirrorWireKind::Bool);
        QCOMPARE(message->updates.at(ps3).value.typeId(), QMetaType::Bool);
    }

    void incompleteDuplicateOrWrongKindDescriptorIsNotUsable_data()
    {
        QTest::addColumn<QByteArray>("field");
        QTest::addColumn<int>("mutation");

        const QList<QByteArray> fields{
            QByteArrayLiteral("remoteDisplayBudgetVersion"),
            QByteArrayLiteral("displayApplicationBytesPerSecond"),
            QByteArrayLiteral("spectrumSampleUnitsPerSecond"),
            QByteArrayLiteral("displayBudgetGeneration"),
            QByteArrayLiteral("remotePs3DisplaySubscribed"),
        };
        for (const QByteArray& field : fields) {
            QTest::newRow(qPrintable(QStringLiteral("missing-%1")
                                         .arg(QString::fromLatin1(field))))
                << field << 0;
            QTest::newRow(qPrintable(QStringLiteral("duplicate-%1")
                                         .arg(QString::fromLatin1(field))))
                << field << 1;
            QTest::newRow(qPrintable(QStringLiteral("wrong-kind-%1")
                                         .arg(QString::fromLatin1(field))))
                << field << 2;
        }
    }

    void incompleteDuplicateOrWrongKindDescriptorIsNotUsable()
    {
        QFETCH(QByteArray, field);
        QFETCH(int, mutation);

        QList<MirrorUpdate> updates = budgetCapabilities(9, true).toUpdates();
        const int index = updateIndex(updates, field);
        QVERIFY(index >= 0);
        if (mutation == 0) {
            updates.removeAt(index);
        } else if (mutation == 1) {
            updates.append(updates.at(index));
        } else if (updates.at(index).kind == MirrorWireKind::Bool) {
            updates[index].kind = MirrorWireKind::Int64;
            updates[index].value = QVariant::fromValue<qlonglong>(1);
        } else {
            updates[index].kind = MirrorWireKind::Bool;
            updates[index].value = true;
        }

        const std::optional<SessionMessage> message = wireRoundTrip(updates);
        QVERIFY(message.has_value());
        const StationCapabilities received =
            StationCapabilities::fromUpdates(message->updates);
        QCOMPARE(received.remoteDisplayBudgetVersion, 0);
        QVERIFY(!received.displayBudget.has_value());
        QVERIFY(!received.remotePs3DisplaySubscribed);
    }

    void descriptorRejectsInvalidIntegerValues_data()
    {
        QTest::addColumn<QByteArray>("field");
        QTest::addColumn<qlonglong>("value");

        QTest::newRow("zero-bytes")
            << QByteArray("displayApplicationBytesPerSecond") << qlonglong(0);
        QTest::newRow("negative-bytes")
            << QByteArray("displayApplicationBytesPerSecond") << qlonglong(-1);
        QTest::newRow("above-json-safe-bytes")
            << QByteArray("displayApplicationBytesPerSecond")
            << qlonglong(kDisplayBudgetJsonSafePositiveLimit + 1);
        QTest::newRow("zero-samples")
            << QByteArray("spectrumSampleUnitsPerSecond") << qlonglong(0);
        QTest::newRow("negative-samples")
            << QByteArray("spectrumSampleUnitsPerSecond") << qlonglong(-1);
        QTest::newRow("above-json-safe-samples")
            << QByteArray("spectrumSampleUnitsPerSecond")
            << qlonglong(kDisplayBudgetJsonSafePositiveLimit + 1);
        QTest::newRow("zero-generation")
            << QByteArray("displayBudgetGeneration") << qlonglong(0);
        QTest::newRow("generation-overflow")
            << QByteArray("displayBudgetGeneration")
            << qlonglong(std::numeric_limits<quint32>::max()) + 1;
        QTest::newRow("negative-version")
            << QByteArray("remoteDisplayBudgetVersion") << qlonglong(-1);
        QTest::newRow("version-overflow")
            << QByteArray("remoteDisplayBudgetVersion") << qlonglong(65536);
    }

    void descriptorRejectsInvalidIntegerValues()
    {
        QFETCH(QByteArray, field);
        QFETCH(qlonglong, value);
        QList<MirrorUpdate> updates = budgetCapabilities().toUpdates();
        const int index = updateIndex(updates, field);
        QVERIFY(index >= 0);
        updates[index].value = QVariant::fromValue(value);

        const std::optional<SessionMessage> message = wireRoundTrip(updates);
        QVERIFY(message.has_value());
        const StationCapabilities received =
            StationCapabilities::fromUpdates(message->updates);
        QCOMPARE(received.remoteDisplayBudgetVersion, 0);
        QVERIFY(!received.displayBudget.has_value());
    }

    void fractionalAndOverflowingJsonIntegersFailWireDecode_data()
    {
        QTest::addColumn<double>("value");
        QTest::newRow("fractional") << 1.5;
        QTest::newRow("one-past-qint64")
            << -static_cast<double>(std::numeric_limits<qint64>::min());
    }

    void fractionalAndOverflowingJsonIntegersFailWireDecode()
    {
        QFETCH(double, value);
        const QByteArray encoded = SessionMessages::encode(
            SessionMessages::capabilities(budgetCapabilities().toUpdates()));
        QJsonObject root = QJsonDocument::fromJson(encoded).object();
        QJsonArray properties = root.value(QStringLiteral("properties")).toArray();
        bool replaced = false;
        for (int i = 0; i < properties.size(); ++i) {
            QJsonObject property = properties.at(i).toObject();
            if (property.value(QStringLiteral("name")).toString()
                == QLatin1String("displayApplicationBytesPerSecond")) {
                property.insert(QStringLiteral("value"), value);
                properties[i] = property;
                replaced = true;
                break;
            }
        }
        QVERIFY(replaced);
        root.insert(QStringLiteral("properties"), properties);

        SessionMessage decoded;
        QVERIFY(!SessionMessages::decode(
            QJsonDocument(root).toJson(QJsonDocument::Compact), &decoded));
    }

    void serverClaimsOnlyAnEnforcedDescriptorAndUsesSerialGenerationOrder()
    {
        RadioModel station;
        AppSettings settings(m_securityDir.filePath(QStringLiteral("server.settings")));
        StationServer server(&station, settings, m_securityDir.path());

        server.setMediaEnabled(true);
        QVERIFY(!server.buildCapabilities().displayBudget.has_value());
        QCOMPARE(server.buildCapabilities().remoteDisplayBudgetVersion, 0);

        QVERIFY(server.setDisplayBudgetLimits(limits(1)));
        // A configured descriptor is not an advertised promise until the
        // production controller has installed its enforcement boundary.
        QVERIFY(!server.buildCapabilities().displayBudget.has_value());
        server.setDisplayBudgetEnforcementEnabled(true);
        compareBudget(server.buildCapabilities().displayBudget, limits(1));
        QCOMPARE(server.buildCapabilities().remoteDisplayBudgetVersion, 1);

        QVERIFY(!server.setDisplayBudgetLimits(DisplayBudgetLimits{0, kSamples, 2}));
        QVERIFY(!server.setDisplayBudgetLimits(limits(1, kBytes + 1, kSamples)));
        QVERIFY(!server.setDisplayBudgetLimits(limits(0)));
        QVERIFY(!server.setDisplayBudgetLimits(limits(0x80000001u)));
        compareBudget(server.displayBudgetLimits(), limits(1));

        QVERIFY(server.setDisplayBudgetLimits(limits(2, kBytes + 1, kSamples + 1)));
        compareBudget(server.displayBudgetLimits(), limits(2, kBytes + 1, kSamples + 1));
        QVERIFY(server.setDisplayBudgetLimits(limits(2, kBytes + 1, kSamples + 1)));

        RadioModel wrappingStation;
        AppSettings wrappingSettings(
            m_securityDir.filePath(QStringLiteral("wrapping-server.settings")));
        StationServer wrapping(&wrappingStation, wrappingSettings, m_securityDir.path());
        QVERIFY(wrapping.setDisplayBudgetLimits(limits(
            std::numeric_limits<quint32>::max(), kBytes, kSamples)));
        QVERIFY(wrapping.setDisplayBudgetLimits(limits(1, kBytes + 1, kSamples + 1)));
        compareBudget(wrapping.displayBudgetLimits(), limits(1, kBytes + 1, kSamples + 1));
    }

    void runtimeLimitChangePublishesToAnEstablishedMediaSession()
    {
        RadioModel station;
        AppSettings settings(m_securityDir.filePath(QStringLiteral("runtime.settings")));
        StationServer server(&station, settings, m_securityDir.path());
        server.setMediaEnabled(true);
        QVERIFY(server.setDisplayBudgetLimits(limits(1)));
        server.setDisplayBudgetEnforcementEnabled(true);

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto* core = new LoopbackTransport(QStringLiteral("core"), this);
        auto* gui = new LoopbackTransport(QStringLiteral("gui"), this);
        core->linkTo(gui);
        QSignalSpy established(&client, &StationClient::handshakeComplete);
        client.startSession(gui, server.token());
        server.acceptTransport(core);
        QTRY_COMPARE(established.count(), 1);
        compareBudget(client.remoteDisplayBudgetLimits(), limits(1));

        QSignalSpy changes(&client, &StationClient::displayBudgetChanged);
        QVERIFY(server.setDisplayBudgetLimits(limits(2, kBytes + 100, kSamples + 100)));
        QTRY_COMPARE(changes.count(), 1);
        compareBudget(client.remoteDisplayBudgetLimits(),
                      limits(2, kBytes + 100, kSamples + 100));

        QVERIFY(!server.setDisplayBudgetLimits(limits(1, kBytes + 200, kSamples + 200)));
        QCoreApplication::processEvents();
        QCOMPARE(changes.count(), 1);
    }

    void clientGetterRequiresHandshakeMinorSevenAndMediaCapability()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QSignalSpy changes(&client, &StationClient::displayBudgetChanged);
        auto* station = new LoopbackTransport(QStringLiteral("gated-station"), this);
        auto* clientWire = new LoopbackTransport(QStringLiteral("gated-client"), this);
        station->linkTo(clientWire);

        client.startSession(clientWire, QStringLiteral("token"));
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        StationCapabilities controlOnly = budgetCapabilities(3);
        controlOnly.remoteMediaVersion = 0;
        completeFakeStationHandshake(station, &client,
                                     kRemoteDisplayBudgetSessionProtocolMinor,
                                     controlOnly);
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        QCOMPARE(changes.count(), 0);

        sendCapabilities(station, budgetCapabilities(3));
        QTRY_COMPARE(changes.count(), 1);
        compareBudget(client.remoteDisplayBudgetLimits(), limits(3));
    }

    void ps3TransitionIsArmedBeforeSynchronousReplyAndRetiresWithTheSession()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        auto* station = new LoopbackTransport(QStringLiteral("ps3-station"), this);
        auto* gui = new ImmediatePs3ResultTransport(this);
        station->linkTo(gui);
        client.startSession(gui, QStringLiteral("token"));
        StationCapabilities caps = budgetCapabilities();
        caps.psDisplayVersion = 1;
        completeFakeStationHandshake(station, &client, kSessionProtocolMinor, caps);
        QSignalSpy started(&client, &StationClient::ps3DisplaySubscriptionStarted);
        QSignalSpy finished(&client, &StationClient::ps3DisplaySubscriptionFinished);
        bool armedBeforeReply = false;
        connect(&client, &StationClient::ps3DisplaySubscriptionFinished, this,
                [&](quint32 id, bool, bool, const QString&) {
            armedBeforeReply = !started.isEmpty() && started.last().first().toUInt() == id;
        });
        gui->reply = true;
        const quint32 enabled = client.requestPs3DisplaySubscription(true);
        QVERIFY(enabled != 0);
        QVERIFY(armedBeforeReply);
        QCOMPARE(finished.size(), 1);
        QCOMPARE(finished.first().at(0).toUInt(), enabled);
        QCOMPARE(finished.first().at(1).toBool(), true);
        QCOMPARE(finished.first().at(2).toBool(), true);
        emit gui->textReceived(gui->lastReply);
        QCOMPARE(finished.size(), 1); // A duplicate cannot complete a later intent.

        const quint32 disabled = client.requestPs3DisplaySubscription(false);
        QVERIFY(disabled != 0 && disabled != enabled);
        QCOMPARE(finished.size(), 2);
        QCOMPARE(finished.last().at(1).toBool(), false);
        connect(&client, &StationClient::ps3DisplaySubscriptionStarted, this,
                [&client](quint32, bool) {
            client.disconnectFromStation(QStringLiteral("retired during start notification"));
        });
        QCOMPARE(client.requestPs3DisplaySubscription(true), quint32{0});
        QCOMPARE(gui->requests, 2); // Retirement happened before another wire send.
        QCOMPARE(finished.size(), 2);
    }

    void clientRequiresNewNegotiationAndPreservesTheLastValidEpochDescriptor()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        QSignalSpy changes(&client, &StationClient::displayBudgetChanged);

        auto* oldStation = new LoopbackTransport(QStringLiteral("old-station"), this);
        auto* oldClient = new LoopbackTransport(QStringLiteral("old-client"), this);
        oldStation->linkTo(oldClient);
        client.startSession(oldClient, QStringLiteral("token"));
        completeFakeStationHandshake(oldStation, &client,
                                     kRemoteWidebandSessionProtocolMinor,
                                     budgetCapabilities(1));
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        QVERIFY(!client.remotePs3DisplaySubscribed());
        QCOMPARE(changes.count(), 0);

        auto* station = new LoopbackTransport(QStringLiteral("current-station"), this);
        auto* clientWire = new LoopbackTransport(QStringLiteral("current-client"), this);
        station->linkTo(clientWire);
        client.startSession(clientWire, QStringLiteral("token"));
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        completeFakeStationHandshake(station, &client,
                                     kRemoteDisplayBudgetSessionProtocolMinor,
                                     budgetCapabilities(10));
        compareBudget(client.remoteDisplayBudgetLimits(), limits(10));
        QCOMPARE(changes.count(), 0);

        QList<MirrorUpdate> malformed = budgetCapabilities(11).toUpdates();
        malformed.removeAt(updateIndex(
            malformed, QByteArrayLiteral("spectrumSampleUnitsPerSecond")));
        station->sendText(SessionMessages::encode(
            SessionMessages::capabilities(malformed)));
        QCoreApplication::processEvents();
        compareBudget(client.remoteDisplayBudgetLimits(), limits(10));
        QCOMPARE(changes.count(), 0);

        sendCapabilities(station, budgetCapabilities(9, true, kBytes + 9, kSamples + 9));
        QCoreApplication::processEvents();
        compareBudget(client.remoteDisplayBudgetLimits(), limits(10));
        QVERIFY(!client.remotePs3DisplaySubscribed());
        QCOMPARE(changes.count(), 0);

        sendCapabilities(station, budgetCapabilities(10, true, kBytes + 1, kSamples));
        QCoreApplication::processEvents();
        compareBudget(client.remoteDisplayBudgetLimits(), limits(10));
        QVERIFY(!client.remotePs3DisplaySubscribed());
        QCOMPARE(changes.count(), 0);

        sendCapabilities(station, budgetCapabilities(10, true));
        QTRY_COMPARE(changes.count(), 1);
        compareBudget(client.remoteDisplayBudgetLimits(), limits(10));
        QVERIFY(client.remotePs3DisplaySubscribed());

        sendCapabilities(station, budgetCapabilities(11, true, kBytes + 11, kSamples + 11));
        QTRY_COMPARE(changes.count(), 2);
        compareBudget(client.remoteDisplayBudgetLimits(),
                      limits(11, kBytes + 11, kSamples + 11));
        QVERIFY(client.remotePs3DisplaySubscribed());

        sendCapabilities(station, budgetCapabilities(11, true, kBytes + 11, kSamples + 11));
        QCoreApplication::processEvents();
        QCOMPARE(changes.count(), 2);

        auto* replacementStation =
            new LoopbackTransport(QStringLiteral("replacement-station"), this);
        auto* replacementClient =
            new LoopbackTransport(QStringLiteral("replacement-client"), this);
        replacementStation->linkTo(replacementClient);
        client.startSession(replacementClient, QStringLiteral("token"));
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        QVERIFY(!client.remotePs3DisplaySubscribed());

        StationCapabilities legacy;
        legacy.remoteMediaVersion = 1;
        completeFakeStationHandshake(replacementStation, &client,
                                     kRemoteDisplayBudgetSessionProtocolMinor, legacy);
        QVERIFY(!client.remoteDisplayBudgetLimits().has_value());
        QVERIFY(!client.remotePs3DisplaySubscribed());
        QCOMPARE(changes.count(), 2);
    }

private:
    QTemporaryDir m_securityDir;
};

QTEST_MAIN(TstDisplayBudgetContract)
#include "tst_display_budget_contract.moc"
