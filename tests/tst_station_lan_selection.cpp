// no-port-check: NereusSDR-original. R-R3-38 discovery is never trust.
#include <QtTest>
#include <QCheckBox>
#include <QFile>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSslSocket>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QUdpSocket>
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "gui/ConnectionSelector.h"
#include "gui/CoreTargetEditor.h"
#include "gui/GuiConnectionController.h"
#include "gui/MainWindow.h"
#include "gui/StationLanSelection.h"
#include "models/RadioModel.h"
#include "fakes/MainWindowTestSettings.h"
#include "fakes/UpgradedCoreToken.h"
using namespace NereusSDR;

namespace {
SavedCoreTarget savedCore(const QString& fingerprint, const QString& token)
{
    return {QStringLiteral("shack"), QStringLiteral("Saved Shack"),
        {QStringLiteral("wss://old-address.invalid:4711"), token, fingerprint, false}, {}, {}};
}
StationLanAnnouncement advertisement(StationServer& server)
{
    return {server.serverPort(), server.certificateFingerprint(), QStringLiteral("Test Core"),
        QStringLiteral("Saturn"), QStringLiteral("AA:BB:CC:DD:EE:01"), false};
}
QPushButton* connectButton(GuiConnectionController& controller)
{
    return controller.selector()->findChild<QPushButton*>(QStringLiteral("connectionSelectorConnect"));
}
}

class TestStationLanSelection : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("lan-selection-%1").arg(QCoreApplication::applicationPid()));
        QVERIFY(QSslSocket::supportsSsl());
    }
    void init()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        AppSettings::instance().clear();
        Test::markAudioFirstRunDone();
        QVERIFY(AppSettings::instance().save());
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery guard;
        guard.holdOffScans(std::chrono::minutes{5});
    }
    void cleanup()
    {
        QVERIFY(!AppSettings::instance().remoteBackend());
        RadioDiscovery::clearHoldOffForTest();
    }
    void cleanupTestCase()
    {
        QFile::remove(AppSettings::instance().filePath());
        QFile::remove(AppSettings::instance().filePath() + QStringLiteral(".bak"));
    }
    void matchingRequiresCompleteSavedPinAndDoesNotPickAmongDuplicates()
    {
        StationLanEndpoint endpoint;
        endpoint.announcement = {4711, QStringLiteral("AB:").repeated(31) + QStringLiteral("AB"),
            QStringLiteral("Core"), {}, QStringLiteral("00:00:00:00:00:00"), false};
        const auto correct = savedCore(endpoint.announcement.fingerprint.toLower(), QStringLiteral("token"));
        auto unpinned = correct;
        unpinned.connection.fingerprint.clear();
        unpinned.connection.allowUnpinned = true;
        auto noToken = correct;
        noToken.connection.token.clear();
        auto wrong = correct;
        wrong.connection.fingerprint[0] = QLatin1Char('C');
        const auto matches = matchingSavedCores(endpoint, {unpinned, noToken, wrong, correct});
        QCOMPARE(matches.size(), 1);
        QCOMPARE(matches.first().id, correct.id);
        auto duplicate = correct;
        duplicate.id = QStringLiteral("another-account");
        QCOMPARE(matchingSavedCores(endpoint, {correct, duplicate}).size(), 2);
        endpoint.announcement.fingerprint.clear();
        QVERIFY(matchingSavedCores(endpoint, {unpinned}).isEmpty());
    }
    void discoveredAddressUsesSavedPinWithoutRewritingAddress()
    {
        QTemporaryDir directory;
        AppSettings stationSettings(directory.filePath(QStringLiteral("station.settings")));
        RadioModel radio;
        StationServer server(&radio, stationSettings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()));
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const auto saved = savedCore(server.certificateFingerprint(), server.token());
        CoreTargetStore store(AppSettings::instance());
        QVERIFY(store.load());
        QVERIFY(store.upsert(saved));
        QVERIFY(store.select(QStringLiteral("local")));
        GuiConnectionController controller;
        controller.start({});
        auto* discovery = controller.findChild<StationLanDiscovery*>();
        QVERIFY(discovery && discovery->start(0));
        controller.showConnections();
        QUdpSocket sender;
        const auto bytes = encodeStationLanAnnouncement(advertisement(server));
        QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, discovery->port()), bytes.size());
        QTRY_COMPARE(discovery->endpoints().size(), 1);
        controller.selector()->setSelectedKey(QStringLiteral("lan:") + discovery->endpoints().first().key());
        QVERIFY(connectButton(controller)->isEnabled());
        connectButton(controller)->click();
        QTRY_VERIFY(server.hasAuthenticatedSession());
        StationClient* client = controller.sessions()->window()->findChild<StationClient*>();
        QVERIFY(client);
        QTRY_VERIFY(client->isHandshakeComplete());
        const quint64 generation = controller.sessions()->generation();
        QTRY_VERIFY(!connectButton(controller)->isEnabled());
        connectButton(controller)->click();
        QCoreApplication::processEvents();
        QCOMPARE(controller.sessions()->generation(), generation);
        auto* disconnectButton = controller.selector()->findChild<QPushButton*>(QStringLiteral("connectionSelectorDisconnect"));
        QVERIFY(disconnectButton && disconnectButton->isEnabled());
        disconnectButton->click();
        QTRY_VERIFY(!client->isHandshakeComplete());
        QTRY_VERIFY(connectButton(controller)->isEnabled());
        connectButton(controller)->click();
        QTRY_VERIFY(client->isHandshakeComplete());
        QCOMPARE(controller.sessions()->generation(), generation);
        QVERIFY(!controller.sessions()->window()->radioModel()->isConnected());
        QCOMPARE(controller.sessions()->selection().savedAddressBeforeDiscovery, saved.connection.url);
        QVERIFY(store.load());
        QCOMPARE(store.selectedId(), saved.id);
        QCOMPARE(store.target(saved.id)->connection.url, saved.connection.url);
        QCOMPARE(store.target(saved.id)->connection.fingerprint, saved.connection.fingerprint);
        controller.shutdown();
    }
    void unknownAnnouncementNeverSeedsCredentialsOrDialsOnSelection()
    {
        QTemporaryDir directory;
        AppSettings stationSettings(directory.filePath(QStringLiteral("station.settings")));
        RadioModel radio;
        StationServer server(&radio, stationSettings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()));
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        GuiConnectionController controller;
        controller.start({});
        QPointer<MainWindow> original = controller.sessions()->window();
        auto* discovery = controller.findChild<StationLanDiscovery*>();
        QVERIFY(discovery && discovery->start(0));
        controller.showConnections();
        QUdpSocket sender;
        const auto bytes = encodeStationLanAnnouncement(advertisement(server));
        QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, discovery->port()), bytes.size());
        QTRY_COMPARE(discovery->endpoints().size(), 1);
        controller.selector()->setSelectedKey(QStringLiteral("lan:") + discovery->endpoints().first().key());
        bool inspected = false;
        QTimer::singleShot(0, &controller, [&] {
            // queueConnect runs first; its modal editor then delivers this.
            QTimer::singleShot(0, &controller, [&] {
                auto* editor = controller.selector()->findChild<CoreTargetEditor*>();
                if (!editor) { return; }
                inspected = true;
                QVERIFY(editor->target().connection.token.isEmpty());
                QVERIFY(editor->target().connection.fingerprint.isEmpty());
                QVERIFY(!editor->target().connection.allowUnpinned);
                QCOMPARE(editor->target().connection.url, discovery->endpoints().first().url().toString());
                editor->reject();
            });
        });
        connectButton(controller)->click();
        QTRY_VERIFY(inspected);
        QVERIFY(original && controller.sessions()->window() == original);
        QVERIFY(!server.hasAuthenticatedSession());
        CoreTargetStore store(AppSettings::instance());
        QVERIFY(store.load());
        QVERIFY(store.targets().isEmpty());
        controller.shutdown();
    }
    void coresAreListedByLabelOrElseByName()
    {
        // iPhone app Task 16: a schema-2 Core is listed by its label; a
        // Core from before Task 16 (schema 1) by its name.
        GuiConnectionController controller;
        controller.start({});
        auto* discovery = controller.findChild<StationLanDiscovery*>();
        QVERIFY(discovery && discovery->start(0));
        controller.showConnections();
        StationLanAnnouncement labelled{4711, QStringLiteral("AB:").repeated(31) + QStringLiteral("AB"),
            QStringLiteral("Test Core"), {}, QStringLiteral("00:00:00:00:00:00"), false};
        labelled.schema = kStationLanAnnouncementSchema;
        labelled.identity = QByteArray(kStationLanIdentityBytes, '\x11');
        labelled.label = QStringLiteral("KG4VCF/shack");
        labelled.pairing = StationLanPairing::Click;
        StationLanAnnouncement older{4712, QStringLiteral("CD:").repeated(31) + QStringLiteral("CD"),
            QStringLiteral("Older Core"), {}, QStringLiteral("00:00:00:00:00:00"), false};
        QUdpSocket sender;
        for (const StationLanAnnouncement& packet : {labelled, older}) {
            const auto bytes = encodeStationLanAnnouncement(packet);
            QVERIFY(!bytes.isEmpty());
            QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, discovery->port()), bytes.size());
        }
        QTRY_COMPARE(discovery->endpoints().size(), 2);
        auto* tree = controller.selector()->findChild<QTreeWidget*>(QStringLiteral("connectionSelectorTargets"));
        QVERIFY(tree);
        const auto listed = [tree](const QString& text) {
            for (int group = 0; group < tree->topLevelItemCount(); ++group) {
                for (int row = 0; row < tree->topLevelItem(group)->childCount(); ++row) {
                    if (tree->topLevelItem(group)->child(row)->text(0) == text) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY(listed(QStringLiteral("KG4VCF/shack (advertised)")));
        QVERIFY(listed(QStringLiteral("Older Core (advertised)")));
        QVERIFY(!listed(QStringLiteral("Test Core (advertised)")));
        controller.shutdown();
    }
    void spoofedKnownAnnouncementStillFailsTlsPin()
    {
        QTemporaryDir directory;
        AppSettings stationSettings(directory.filePath(QStringLiteral("station.settings")));
        RadioModel radio;
        StationServer server(&radio, stationSettings, NereusSDR::Test::seedUpgradedCoreToken(directory.path()));
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        auto packet = advertisement(server);
        packet.fingerprint[0] = packet.fingerprint[0] == QLatin1Char('A') ? QLatin1Char('B') : QLatin1Char('A');
        const auto saved = savedCore(packet.fingerprint, QStringLiteral("must-not-be-sent"));
        CoreTargetStore store(AppSettings::instance());
        QVERIFY(store.load());
        QVERIFY(store.upsert(saved));
        GuiConnectionController controller;
        controller.start({});
        auto* discovery = controller.findChild<StationLanDiscovery*>();
        QVERIFY(discovery && discovery->start(0));
        controller.showConnections();
        QUdpSocket sender;
        const auto bytes = encodeStationLanAnnouncement(packet);
        QCOMPARE(sender.writeDatagram(bytes, QHostAddress::LocalHost, discovery->port()), bytes.size());
        QTRY_COMPARE(discovery->endpoints().size(), 1);
        controller.selector()->setSelectedKey(QStringLiteral("lan:") + discovery->endpoints().first().key());
        connectButton(controller)->click();
        QTRY_VERIFY(controller.sessions()->window()->findChild<StationClient*>());
        StationClient* client = controller.sessions()->window()->findChild<StationClient*>();
        QTRY_VERIFY(client->lastError().contains(QStringLiteral("does not match")));
        QVERIFY(!client->lastError().contains(saved.connection.fingerprint));
        QVERIFY(!client->lastError().contains(server.certificateFingerprint()));
        QVERIFY(!client->isHandshakeComplete());
        QVERIFY(!client->isReconnectPending());
        QVERIFY(!server.hasAuthenticatedSession());
        QVERIFY(store.load());
        QCOMPARE(store.target(saved.id)->connection.fingerprint, saved.connection.fingerprint);
        controller.shutdown();
    }
};
QTEST_MAIN(TestStationLanSelection)
#include "tst_station_lan_selection.moc"
