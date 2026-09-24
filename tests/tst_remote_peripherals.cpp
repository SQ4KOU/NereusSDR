// no-port-check: NereusSDR-original. Remote TGXL Peripherals UI coverage.
// 2026-09-23: R-R3-47 / R-R3-22: the Power Genius and RF-Kit applets read
// the Core's `amplifier` and `rfkit` objects in a remote window (filled on
// attach, updated, false and zero shown, stale on Core loss, no accessory
// socket opened) and the same objects in-process in a local window. J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-09-24: R-R3-47 / R-R3-22 / R-R3-25: a remote window connects,
// disconnects and configures the Core's Power Genius through the Core (the
// Peripherals row and the Power Genius tab), opening no socket of its own;
// a receive-only Core refuses the amp's operate and the tuner's operate,
// bypass and antenna writes, changing nothing and sending nothing. J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-09-24: R-R3-47 / R-R3-48 / R-R3-22 / R-R3-25: a remote window
// switches, connects and disconnects the Core's RF-Kit through the Core (the
// RF-Kit page and the applet), sees its tuner, antenna and band-follow rows,
// and a raw write of the RF-Kit switch is refused in plain words; the Power
// Genius's band-follow line, local and remote; the one TCI switch turns the
// Core's station TCI server on and off, which keeps running when the window
// goes and another connects. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
// Claude Code.
// 2026-09-24: R-R3-47 / R-R3-22: a remote window's Advanced pages are views
// of the Core's records (they open no accessory connection): faults raised
// on the Core appear without a reconnect and are cleared through the Core,
// the counters are the Core's and not the window's, the output limit is
// set through the Core and its alert reaches the window, the antenna names
// and tune memory are the Core's; the interlock policy is changed from a
// remote window, applied and enforced on the Core, and shown by every
// window; an older Core leaves the interlock section saying why. J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-09-24: R-R3-47 / R-R3-22: every control on a remote window's Power
// Genius and Tuner Genius Advanced pages works through the Core: a fake
// amp and tuner on the Core record the bytes, which are the ones a local
// window's page sends; network changes and Save & Reboot ask first; the
// device's answers, its values and the Core's refusals (on their own route)
// show on the page; an older Core leaves the controls saying why. J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code.

#include <QtTest>

#include <QDateTime>
#include <QTemporaryDir>
#include <cmath>
#include <memory>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTabWidget>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QMenu>
#include <QRadioButton>
#include <QSlider>
#include <QTimer>
#include <QApplication>
#include <QDialog>

#include "OperatorWording.h"
#include "core/PgxlConnection.h"
#include "core/ConnectionDiagnostics.h"
#include "core/FaultLog.h"
#include "core/TuneMemoryStore.h"
#include "core/TxInterlockPolicy.h"
#include "core/TgxlConnection.h"
#include "core/SmartSdrApiListener.h"
#include "core/StationPgxlController.h"
#include "core/StationTgxlController.h"
#include "core/LanDiscovery.h"
#include "core/AppSettings.h"
#include "core/Rf2ksConnection.h"
#include "core/session/IStationLink.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/applets/AmpApplet.h"
#include "gui/applets/Rf2ksApplet.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "gui/setup/FourO3APage.h"
#include "gui/setup/RfKitPage.h"
#include "gui/setup/PgxlAdvancedPage.h"
#include "gui/setup/PgxlInterlockPage.h"
#include "gui/setup/TgxlAdvancedPage.h"
#include "gui/SetupDialog.h"
#include "gui/PgxlSaveRebootDialog.h"
#include "models/AccessorySettingsModel.h"
#include "models/AccessoryDataModel.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"
#include "models/RfKitModel.h"
#include "models/StationTciModel.h"
#include "core/StationTciController.h"
#include "core/TciServer.h"
#include "core/TciSwitch.h"
#include "gui/OperatorReasonText.h"
#include "models/TunerModel.h"

#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

class RecordingTgxlLink final : public IStationLink {
public:
    bool available{true};
    bool fourO3AAvailable{false};
    int configureCalls{0};
    int disconnectCalls{0};
    int fourO3ACalls{0};
    bool requestedFourO3AEnabled{false};
    CommandOutcome fourO3AOutcome{true, {}};
    QString configuredHost;
    quint16 configuredPort{0};

    CommandOutcome requestAddSlice(const QString&) override { return {}; }
    CommandOutcome requestAddSliceOnPan(const QString&) override { return {}; }
    CommandOutcome requestRemoveSlice(int) override { return {}; }
    CommandOutcome requestActiveSlice(int) override { return {}; }
    CommandOutcome requestSliceSampleRate(int, int) override { return {}; }

    CommandOutcome requestConfigureTgxl(const QString& host, quint16 port) override
    {
        ++configureCalls;
        configuredHost = host;
        configuredPort = port;
        return {true, {}};
    }

    CommandOutcome requestDisconnectTgxl() override
    {
        ++disconnectCalls;
        return {true, {}};
    }

    bool remoteTgxlConfigAvailable() const override { return available; }
    CommandOutcome requestFourO3AEnabled(bool enabled) override
    {
        ++fourO3ACalls;
        requestedFourO3AEnabled = enabled;
        return fourO3AOutcome;
    }
    bool remoteFourO3AControlAvailable() const override { return fourO3AAvailable; }

    // R-R3-47: the link's state for the amplifier and RF-Kit readings.
    bool linkReady{false};
    bool amplifierStatus{false};
    bool rfKitStatus{false};
    bool stationLinkReady() const override { return linkReady; }
    bool remoteAmplifierStatusAvailable() const override { return amplifierStatus; }
    bool remoteRfKitStatusAvailable() const override { return rfKitStatus; }

    // R-R3-47: the Power Genius commands.
    bool pgxlAvailable{false};
    int pgxlConfigureCalls{0};
    int pgxlDisconnectCalls{0};
    int pgxlSettingsCalls{0};
    QString pgxlHost;
    quint16 pgxlPort{0};
    bool pgxlAutoReconnect{true};
    int pgxlKeepaliveSec{0};
    int pgxlPingSec{-1};
    bool remotePgxlControlAvailable() const override { return pgxlAvailable; }
    CommandOutcome requestConfigurePgxl(const QString& host, quint16 port) override
    {
        ++pgxlConfigureCalls;
        pgxlHost = host;
        pgxlPort = port;
        return {true, {}};
    }
    CommandOutcome requestDisconnectPgxl() override
    {
        ++pgxlDisconnectCalls;
        return {true, {}};
    }
    CommandOutcome requestPgxlConnectionSettings(bool autoReconnect, int keepaliveSec,
                                                 int pingSec) override
    {
        ++pgxlSettingsCalls;
        pgxlAutoReconnect = autoReconnect;
        pgxlKeepaliveSec = keepaliveSec;
        pgxlPingSec = pingSec;
        return {true, {}};
    }
};

// Status lines and REST replies as the repository's parser tests carry
// them (tst_pgxl_connection_parse, tst_rf2ks_connection_parse); the
// transmit frame uses the same keys, 60 dBm (1000 W) and -24.5 dB (1.13).
QMap<QString, QString> pgxlFrame(const char* line)
{
    QMap<QString, QString> kvs;
    const QString body = QString::fromLatin1(line);
    for (const QString& part : body.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        const int eq = part.indexOf(QLatin1Char('='));
        if (eq > 0) {
            kvs.insert(part.left(eq), part.mid(eq + 1));
        }
    }
    return kvs;
}
constexpr const char* kOperate = "state=OPERATE temp=42.5 vac=240 fwd=1480.0 swr=2.1";
constexpr const char* kTransmit = "state=TRANSMIT_A peakfwd=60.0 swr=-24.5 id=22.5";
constexpr const char* kStandby = "state=STANDBY peakfwd=60.0 swr=-24.5 id=0.0";
constexpr const char* kRfKitPower =
    R"({"temperature":{"value":27.0,"unit":"°C"},"voltage":{"value":52.7,"unit":"V"},"current":{"value":0.0,"unit":"A"},"forward":{"value":850,"max_value":1200,"unit":"W"},"reflected":{"value":3,"max_value":20,"unit":"W"},"swr":{"value":1.4,"max_value":2.1,"unit":""}})";
constexpr const char* kRfKitIdle =
    R"({"temperature":{"value":0.0,"unit":"°C"},"voltage":{"value":0.0,"unit":"V"},"current":{"value":0.0,"unit":"A"},"forward":{"value":0,"max_value":0,"unit":"W"},"reflected":{"value":0,"max_value":0,"unit":"W"},"swr":{"value":1.0,"max_value":1.0,"unit":""}})";

RfKitPowerSnapshot rfKitPower(int forwardW, float swr, float tempC, float volts, float amps)
{
    RfKitPowerSnapshot snap;
    snap.forwardW = forwardW;
    snap.swr = swr;
    snap.temperatureC = tempC;
    snap.voltageV = volts;
    snap.currentA = amps;
    return snap;
}

// R-R3-47: the RF-Kit's REST interface on the loopback (the /info body is
// tst_rf2ks_connection_parse's).
class FakeRfKit : public QTcpServer {
public:
    FakeRfKit()
    {
        listen(QHostAddress::LocalHost, 0);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket* sock = nextPendingConnection()) {
                connect(sock, &QTcpSocket::readyRead, this, [this, sock] {
                    const QByteArray req = sock->readAll();
                    const int sp = req.indexOf(' ') + 1;
                    const QByteArray path = req.mid(sp, req.indexOf(' ', sp) - sp);
                    ++requests;
                    lines.append(QString::fromLatin1(req.left(sp) + path));
                    QByteArray body = "{}";
                    if (path == "/info") {
                        body = R"({"device":"RF2K-S","software_version":{"GUI":200,"controller":267},"custom_device_name":"KG4VCF"})";
                    } else if (path == "/operate-mode") {
                        body = R"({"operate_mode":"STANDBY"})";
                    }
                    sock->write("HTTP/1.0 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
                                + QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    sock->flush();
                    sock->disconnectFromHost();
                });
            }
        });
    }
    int requests{0};
    QStringList lines;   // "GET /info", "POST /error/reset": what reached the amp
};

// A loopback stand-in for the Power Genius or Tuner Genius: records every
// command line it receives (the bytes on the wire) and answers on request.
class FakeGenius : public QObject {
public:
    QTcpServer server;
    QPointer<QTcpSocket> peer;
    QStringList commands;          // the command part of each C<seq>|<command> line
    QList<quint32> sequences;
    QByteArray pending;

    bool listen() { return server.listen(QHostAddress::LocalHost, 0); }
    quint16 port() const { return server.serverPort(); }
    bool accept()
    {
        if (!QTest::qWaitFor([this] { return server.hasPendingConnections(); }, 3000)) {
            return false;
        }
        peer = server.nextPendingConnection();
        connect(peer.data(), &QTcpSocket::readyRead, this, [this] { read(); });
        return true;
    }
    void read()
    {
        pending += peer->readAll();
        qsizetype nl = 0;
        while ((nl = pending.indexOf('\n')) >= 0) {
            const QString line = QString::fromUtf8(pending.left(nl)).trimmed();
            pending.remove(0, nl + 1);
            const qsizetype bar = line.indexOf(QLatin1Char('|'));
            if (line.startsWith(QLatin1Char('C')) && bar > 1) {
                sequences.append(line.mid(1, bar - 1).toUInt());
                commands.append(line.mid(bar + 1));
            }
        }
    }
    void send(const QString& line)
    {
        peer->write(line.toUtf8() + '\n');
        peer->flush();
    }
    /// Index of the first `command` received at or after `from`; -1 if none
    /// arrives in time.
    int waitFor(const QString& command, int from = 0)
    {
        int found = -1;
        (void)QTest::qWaitFor([&] {   // -1 when it never arrives
            for (int i = from; i < commands.size(); ++i) {
                if (commands.at(i) == command) {
                    found = i;
                    return true;
                }
            }
            return false;
        }, 3000);
        return found;
    }
    void reply(int index, const QString& codeAndBody)
    {
        send(QStringLiteral("R%1|%2").arg(sequences.at(index)).arg(codeAndBody));
    }
    /// The commands that change or read the device's own settings, from
    /// `from` on (status polls, keepalive and pairing left out).
    QStringList settingsCommands(int from) const
    {
        QStringList out;
        for (int i = from; i < commands.size(); ++i) {
            const QString& c = commands.at(i);
            if (c.startsWith(QLatin1String("setup ")) || c.startsWith(QLatin1String("ifconf "))
                || c == QLatin1String("save")) {
                if (out.isEmpty() || out.last() != c) {   // one per change
                    out.append(c);
                }
            }
        }
        return out;
    }
};

quint16 freeLoopbackPort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { return 0; }
    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

TunerModel::StationConnectionState state(TunerModel::ConnectionPhase phase,
                                         const QString& host = {},
                                         quint16 port = 0,
                                         const QString& error = {})
{
    TunerModel::StationConnectionState result;
    result.phase = phase;
    result.configuredHost = host;
    result.configuredPort = port;
    result.error = error;
    return result;
}

} // namespace

class RemotePeripheralsTest : public QObject {
    Q_OBJECT

private slots:
    void remoteTgxlDraftUsesStationLinkAndSurvivesUnrelatedSnapshots();
    void unknownCapabilityKeepsRemoteTgxlInert();
    void remoteParentPageExposesOnlyStationBackedControls();
    void remoteMasterShowsPendingAndRefusalWithoutLocalActivation();
    void coreTunerErrorsAreShownInUserWords();
    void remoteAmpAndRfKitAppletsFollowTheCore();
    void remoteAppletsSayWhyReadingsAreNotLive();
    void localAppletsShowTheSameValuesAsBefore();
    void remotePgxlRowAndTabUseTheStationLink();
    void remoteWindowSetsUpThePgxlThroughTheCore();
    void receiveOnlyCoreRefusesTunerAndAmpOperation();
    void remoteWindowSetsUpTheRfKitThroughTheCore();
    void rawRfKitSwitchWriteIsRefused();
    void pgxlBandFollowLineLocalAndRemote();
    void oneTciSwitchDrivesTheCoresStationServer();
    // R-R3-47 / R-R3-22
    void remoteWindowShowsTheCoresRecords();
    void remoteWindowChangesTheInterlockOnTheCore();
    void olderCoreLeavesTheInterlockSayingWhy();
    void remoteWindowChangesTheAmpsOwnSettingsThroughTheCore();
    void remoteWindowChangesTheTunersOwnSettingsThroughTheCore();
    void olderCoreLeavesTheDeviceSettingsSayingWhy();
    void accessoryRefusalsNeverReachTheSliceToast();
    void remoteRfKitPageWorksEveryControl();
    void olderCoreLeavesTheRfKitSettingsSayingWhy();
};

void RemotePeripheralsTest::remoteParentPageExposesOnlyStationBackedControls()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    model.attachStation(&link);
    FourO3APage page(&model);
    auto* peripherals = page.findChild<PeripheralsPage*>();
    QVERIFY(peripherals);
    QVERIFY(peripherals->isEnabled());
    // R-R3-47: the Advanced pages are views of the Core's records; they
    // open no connection to an accessory and send it nothing.
    QVERIFY(page.findChild<PgxlAdvancedPage*>());
    QVERIFY(page.findChild<TgxlAdvancedPage*>());
    QVERIFY(page.findChild<PgxlInterlockPage*>());
    QVERIFY(!model.pgxlConnection()->isConnected());
    QVERIFY(!model.tgxlConnection()->isConnected());
    QVERIFY(model.pgxlConnection()->peerAddress().isEmpty());
    // R-R3-47: the Power Genius tab has check boxes of its own now.
    auto* master = page.findChild<QCheckBox*>(QStringLiteral("fourO3AMasterToggle"));
    QVERIFY(master);
    QVERIFY(!master->isEnabled());
    QVERIFY(master->toolTip().contains(QStringLiteral("Core")));
    QVERIFY(QMetaObject::invokeMethod(&page, "onMasterToggled", Qt::DirectConnection,
                                      Q_ARG(bool, true)));
    model.setFourO3AEnabled(true);
    QVERIFY(!model.smartSdrListener()->isListening());

    SetupDialog dialog(&model);
    dialog.selectPage(QStringLiteral("4O3A"));
    auto* actualPage = dialog.findChild<FourO3APage*>();
    QVERIFY(actualPage);
    QVERIFY(actualPage->isEnabled());
    QVERIFY(actualPage->findChild<PeripheralsPage*>()->isEnabled());
}

void RemotePeripheralsTest::remoteMasterShowsPendingAndRefusalWithoutLocalActivation()
{
    RadioModel model(RadioModel::Role::Remote);
    RadioInfo info;
    info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:99");
    model.setLastRadioInfoForTest(info);
    model.setConnectionStateForTest(ConnectionState::Connected);
    RecordingTgxlLink link;
    link.fourO3AAvailable = true;
    model.attachStation(&link);

    FourO3APage page(&model);
    auto* master = page.findChild<QCheckBox*>(QStringLiteral("fourO3AMasterToggle"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("fourO3AListenerStatus"));
    QVERIFY(master && status);
    QVERIFY(master->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(&page, "onMasterToggled", Qt::DirectConnection,
                                      Q_ARG(bool, true)));
    QCOMPARE(link.fourO3ACalls, 1);
    QVERIFY(link.requestedFourO3AEnabled);
    QVERIFY(!master->isChecked());
    QVERIFY(!master->isEnabled());
    QVERIFY(master->text().contains(QStringLiteral("pending")));
    QVERIFY(!model.smartSdrListener()->isListening());

    model.reportStationFourO3ACommandFinished(false, QStringLiteral("Core refused test request"));
    QVERIFY(master->isEnabled());
    QVERIFY(!master->text().contains(QStringLiteral("pending")));
    QVERIFY(status->text().contains(QStringLiteral("refused test request")));
    // R-R3-21: a refusal in the Core's own terms is shown in user words.
    model.reportStationFourO3ACommandFinished(
        false, QStringLiteral("Remote 4O3A control requires a newer station protocol."));
    QVERIFY2(status->text().contains(QStringLiteral("Update this app to control 4O3A on this Core.")),
             qPrintable(status->text()));
    QVERIFY(OperatorWording::isPlain(status->text()));
    QVERIFY(OperatorWording::isPlain(master->toolTip()));
    model.reportStationFourO3ACommandFinished(false, QStringLiteral("Core refused test request"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onMasterToggled", Qt::DirectConnection,
                                     Q_ARG(bool, true)));
    QVERIFY(!master->isEnabled());
    link.fourO3AAvailable = false;
    model.reportStationLinkStateChanged();
    QVERIFY(!master->text().contains(QStringLiteral("pending")));
    link.fourO3AAvailable = true;
    model.reportStationLinkStateChanged();
    QVERIFY(master->isEnabled());
    QTRY_VERIFY(!status->text().contains(QStringLiteral("refused test request")));
}

void RemotePeripheralsTest::remoteTgxlDraftUsesStationLinkAndSurvivesUnrelatedSnapshots()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    model.attachStation(&link);

    PeripheralsPage page(&model);
    auto* host = page.findChild<QLineEdit*>(QStringLiteral("tgxlHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("tgxlPortSpin"));
    auto* connect = page.findChild<QPushButton*>(QStringLiteral("tgxlConnectButton"));
    auto* scan = page.findChild<QPushButton*>(QStringLiteral("tgxlScanButton"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("tgxlStatusLabel"));
    QVERIFY(host && port && connect && scan && status);
    QVERIFY(!scan->isEnabled());

    QSignalSpy tgxlFrames(model.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
    QSignalSpy pgxlFrames(model.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);
    host->setText(QStringLiteral("draft.station.example"));
    port->setValue(9021);

    // A phase/error snapshot changes neither Core's configured endpoint nor
    // the operator's unsent draft. It does make the actionable error visible.
    model.tunerModel()->setStationConnectionState(
        state(TunerModel::ConnectionPhase::Error, {}, 0,
              QStringLiteral("wrong device")));
    QCOMPARE(host->text(), QStringLiteral("draft.station.example"));
    QCOMPARE(port->value(), 9021);
    QVERIFY(status->text().contains(QStringLiteral("wrong device")));
    QCOMPARE(connect->text(), QStringLiteral("Connect"));

    // The draft goes to the typed station-link seam exactly once; no Mac-local
    // socket or LAN path is used.
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 0)));
    QCOMPARE(link.disconnectCalls, 0);
    QCOMPARE(link.configureCalls, 1);
    QCOMPARE(link.configuredHost, QStringLiteral("draft.station.example"));
    QCOMPARE(link.configuredPort, quint16{9021});
    QCOMPARE(tgxlFrames.count(), 0);
    QCOMPARE(pgxlFrames.count(), 0);

    // An active snapshot changes the same action to Core-side cancellation
    // without replacing the still-unacknowledged draft.
    model.tunerModel()->setStationConnectionState(
        state(TunerModel::ConnectionPhase::Retrying, {}, 0,
              QStringLiteral("wrong device")));
    QCOMPARE(host->text(), QStringLiteral("draft.station.example"));
    QCOMPARE(port->value(), 9021);
    QCOMPARE(connect->text(), QStringLiteral("Cancel"));
    QVERIFY(status->text().contains(QStringLiteral("wrong device")));
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 0)));
    QCOMPARE(link.disconnectCalls, 1);

    // Once Core reports a different configured endpoint, it owns the visible
    // fields. A later snapshot remains the sole source of that confirmation.
    model.tunerModel()->setStationConnectionState(
        state(TunerModel::ConnectionPhase::Disconnected,
              QStringLiteral("core.station.example"), 9010));
    QCOMPARE(host->text(), QStringLiteral("core.station.example"));
    QCOMPARE(port->value(), 9010);
    QCOMPARE(connect->text(), QStringLiteral("Connect"));
    // A connected snapshot turns the same UI action into a Core-side
    // disconnect, without changing the configured endpoint locally.
    model.tunerModel()->setStationConnectionState(
        state(TunerModel::ConnectionPhase::Connected,
              QStringLiteral("core.station.example"), 9010));
    QCOMPARE(connect->text(), QStringLiteral("Disconnect"));
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 0)));
    QCOMPARE(link.disconnectCalls, 2);
    QCOMPARE(tgxlFrames.count(), 0);
    QCOMPARE(pgxlFrames.count(), 0);

    // A remote model has no connected radio MAC, so typing did not create a
    // local per-radio peripheral setting either.
    QCOMPARE(model.peripheralValue(QStringLiteral("TGXL_ManualIp"),
                                   QStringLiteral("not-written")),
             QStringLiteral("not-written"));
}

void RemotePeripheralsTest::unknownCapabilityKeepsRemoteTgxlInert()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    link.available = false;
    model.attachStation(&link);

    PeripheralsPage page(&model);
    auto* connect = page.findChild<QPushButton*>(QStringLiteral("tgxlConnectButton"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("tgxlStatusLabel"));
    QVERIFY(connect && status);
    QVERIFY(!connect->isEnabled());
    QCOMPARE(status->text(),
             QStringLiteral("This Core does not offer Tuner Genius XL control to this app."));
    QVERIFY2(OperatorWording::isPlain(status->text()), qPrintable(status->text()));

    // Capability negotiation can complete without a radio connection-state
    // change; the dedicated station-link signal must refresh this row.
    link.available = true;
    model.reportStationLinkStateChanged();
    QVERIFY(connect->isEnabled());

    link.available = false;
    model.reportStationLinkStateChanged();
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 0)));
    QCOMPARE(link.configureCalls, 0);
    QCOMPARE(link.disconnectCalls, 0);
}

// R-R3-17, R-R3-21: the Core's Tuner Genius XL errors reach the row in user
// words; the Core's own text (here as StationTgxlController and
// TgxlConnection word it) is unchanged on the wire and kept in the log.
void RemotePeripheralsTest::coreTunerErrorsAreShownInUserWords()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    model.attachStation(&link);

    PeripheralsPage page(&model);
    auto* status = page.findChild<QLabel*>(QStringLiteral("tgxlStatusLabel"));
    QVERIFY(status);

    const QString identity = QStringLiteral(
        "Expected TunerGenius/TunerGeniusXL at the connected endpoint; observed PowerGeniusXL "
        "(serial 1234-5678).");
    for (const auto phase : {TunerModel::ConnectionPhase::Error,
                             TunerModel::ConnectionPhase::Retrying}) {
        model.tunerModel()->setStationConnectionState(state(phase, {}, 0, identity));
        QCOMPARE(model.tunerModel()->connectionError(), identity);
        QVERIFY2(status->text().contains(QStringLiteral(
                     "The device at this address is not a Tuner Genius. Check the tuner's "
                     "address and port.")),
                 qPrintable(status->text()));
        QVERIFY2(OperatorWording::isPlain(status->text()), qPrintable(status->text()));
    }

    for (const QString& raw :
         {QStringLiteral("No matching TGXL discovery announcement for 192.0.2.40:9010. Check the "
                         "tuner address, port and station LAN discovery."),
          QStringLiteral("TGXL identity serial mismatch: expected 1234-5678, observed 8765-4321"),
          QStringLiteral("TGXL identity was rejected by station discovery"),
          QStringLiteral("TGXL native info failed with code 3"),
          QStringLiteral("TGXL native info omitted a nonempty serial"),
          QStringLiteral("TGXL native identity timed out"),
          QStringLiteral("TGXL discovery approval timed out for serial 1234-5678")}) {
        model.tunerModel()->setStationConnectionState(
            state(TunerModel::ConnectionPhase::Error, {}, 0, raw));
        QVERIFY2(!status->text().contains(raw), qPrintable(status->text()));
        QVERIFY2(!status->text().contains(QStringLiteral("TGXL")), qPrintable(status->text()));
        QVERIFY2(OperatorWording::isPlain(status->text()), qPrintable(status->text()));
    }

    // An error with no words says where the reason is, not "Error: ".
    model.tunerModel()->setStationConnectionState(
        state(TunerModel::ConnectionPhase::Error, {}, 0, {}));
    QCOMPARE(status->text(), QStringLiteral("Error: The reason is in the log."));
}

// R-R3-47 / R-R3-22: a remote window's Power Genius and RF-Kit applets read
// the Core's objects over the in-process loopback: filled on attach,
// updated, false and zero shown as they are, stale when the Core is lost,
// and no accessory socket or request from this window.
void RemotePeripheralsTest::remoteAmpAndRfKitAppletsFollowTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RadioModel station;
    station.enableStationAccessoryIdentity();
    station.amplifierModel()->applyStatusFrame(pgxlFrame(kOperate));
    station.rfKitModel()->applyInfo(QStringLiteral("RF2K-S"), QStringLiteral("G200C267"),
                                    QStringLiteral("KG4VCF"));
    station.rfKitModel()->applyPower(rfKitPower(850, 1.4f, 27.0f, 52.7f, 0.0f));
    station.rfKitModel()->applyOperateMode(QStringLiteral("OPERATE"));
    TunerModel::StationConnectionState up;
    up.configuredHost = QStringLiteral("192.0.2.41");
    up.configuredPort = 8080;
    up.phase = TunerModel::ConnectionPhase::Connected;
    up.deviceModel = QStringLiteral("RF2K-S");
    up.deviceVersion = QStringLiteral("G200C267");
    up.deviceNickname = QStringLiteral("KG4VCF");
    station.rfKitModel()->setStationConnectionState(up);
    station.rfKitModel()->applyPower(rfKitPower(850, 1.4f, 27.0f, 52.7f, 0.0f));
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());

    RadioModel window(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&window, &proxy);
    AmpApplet amp(&window);
    Rf2ksApplet rfKit(&window);
    QSignalSpy pgxlFrames(window.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);

    // Not attached yet: nothing live to show.
    QVERIFY(amp.staleIndicatorVisibleForTesting());
    QVERIFY(rfKit.staleIndicatorVisibleForTesting());
    QVERIFY(!amp.operateButtonShownForTesting());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QVERIFY(completed.wait(5000) || !completed.isEmpty());
    QVERIFY(client.remoteAmplifierStatusAvailable());
    QVERIFY(client.remoteRfKitStatusAvailable());

    // Filled on attach.
    QTRY_COMPARE(amp.tempGaugeValueForTesting(), 42.5);
    QVERIFY(amp.operateButtonShownForTesting());
    QCOMPARE(amp.operateButtonTextForTesting(), QStringLiteral("OPERATE"));
    QCOMPARE(amp.powerLabelTextForTesting(), QStringLiteral("Volts: 240V\u00A0\u00A0Amps: 0.0A"));
    QCOMPARE(amp.fwdGaugeValueForTesting(), 0.0);
    QVERIFY(!amp.staleIndicatorVisibleForTesting());
    QTRY_COMPARE(rfKit.fwdGaugeValueForTesting(), 850);
    QVERIFY(rfKit.connectedStateForTesting());
    QCOMPARE(rfKit.operateButtonTextForTesting(), QStringLiteral("OPERATE"));
    QCOMPARE(rfKit.nicknameLabelTextForTesting(), QStringLiteral("KG4VCF  G200C267"));
    QCOMPARE(rfKit.telemetryStripTextForTesting(),
             QStringLiteral("Fwd 850 W  SWR 1.40  53 V  0.0 A"));
    QVERIFY(!rfKit.staleIndicatorVisibleForTesting());

    // Updated as the Core's amps report.
    station.amplifierModel()->applyStatusFrame(pgxlFrame(kTransmit));
    QTRY_VERIFY(std::abs(amp.fwdGaugeValueForTesting() - 1000.0) < 1e-3);
    QVERIFY(std::abs(amp.swrGaugeValueForTesting() - 1.12668) < 1e-4);
    QCOMPARE(amp.powerLabelTextForTesting(), QStringLiteral("Volts: 240V\u00A0\u00A0Amps: 22.5A"));

    // False and zero, shown as they are.
    station.amplifierModel()->applyStatusFrame(pgxlFrame(kStandby));
    station.rfKitModel()->applyOperateMode(QStringLiteral("STANDBY"));
    station.rfKitModel()->applyPower(rfKitPower(0, 1.0f, 0.0f, 0.0f, 0.0f));
    QTRY_COMPARE(amp.operateButtonTextForTesting(), QStringLiteral("STANDBY"));
    QTRY_COMPARE(amp.fwdGaugeValueForTesting(), 0.0);
    QCOMPARE(amp.swrGaugeValueForTesting(), 1.0);
    QCOMPARE(amp.powerLabelTextForTesting(), QStringLiteral("Volts: 240V\u00A0\u00A0Amps: 0.0A"));
    QTRY_COMPARE(rfKit.operateButtonTextForTesting(), QStringLiteral("STANDBY"));
    QTRY_COMPARE(rfKit.fwdGaugeValueForTesting(), 0);
    QCOMPARE(rfKit.telemetryStripTextForTesting(),
             QStringLiteral("Fwd 0 W  SWR 1.00  0 V  0.0 A"));

    // The Core is lost: the last readings stay, marked stale.
    stationEnd->closeLink(QStringLiteral("test: Core lost"));
    QTRY_VERIFY(amp.staleIndicatorVisibleForTesting());
    QTRY_VERIFY(rfKit.staleIndicatorVisibleForTesting());
    QCOMPARE(amp.staleIndicatorTextForTesting(),
             QStringLiteral("Core disconnected. Power Genius readings are stale."));
    QCOMPARE(rfKit.staleIndicatorTextForTesting(),
             QStringLiteral("Core disconnected. RF-Kit readings are stale."));
    QVERIFY(OperatorWording::isPlain(amp.staleIndicatorTextForTesting()));
    QVERIFY(OperatorWording::isPlain(rfKit.staleIndicatorTextForTesting()));
    QCOMPARE(amp.operateButtonTextForTesting(), QStringLiteral("STANDBY"));

    // The window opened no accessory connection of its own.
    QCOMPARE(pgxlFrames.count(), 0);
    QVERIFY(!window.pgxlConnection()->isConnected());
    QVERIFY(!window.rfKitConnection()->isConnected());
    QCOMPARE(window.rfKitConnection()->testInFlightReplyCount(), 0);
    QVERIFY(!window.rfKitConnection()->testPollActive());
    QCOMPARE(window.rfKitConnection()->pollsSucceeded() + window.rfKitConnection()->pollsFailed(), 0);
}

// R-R3-47: with the link up but an older Core, the applets say the Core
// does not report the amp; every line is in user words.
void RemotePeripheralsTest::remoteAppletsSayWhyReadingsAreNotLive()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    model.attachStation(&link);
    AmpApplet amp(&model);
    Rf2ksApplet rfKit(&model);
    QVERIFY(amp.staleIndicatorVisibleForTesting());
    QVERIFY(amp.staleIndicatorTextForTesting().contains(QStringLiteral("stale")));

    link.linkReady = true;
    model.reportStationLinkStateChanged();
    QCOMPARE(amp.staleIndicatorTextForTesting(),
             QStringLiteral("This Core does not report its Power Genius to this app. "
                            "Updating the Core may help."));
    QCOMPARE(rfKit.staleIndicatorTextForTesting(),
             QStringLiteral("This Core does not report its RF-Kit amplifier to this app. "
                            "Updating the Core may help."));
    for (const QString& text : {amp.staleIndicatorTextForTesting(),
                                rfKit.staleIndicatorTextForTesting(),
                                AmplifierModel::readOnlyReason(),
                                RfKitModel::readOnlyReason()}) {
        QVERIFY2(OperatorWording::isPlain(text), qPrintable(text));
    }

    link.amplifierStatus = true;
    link.rfKitStatus = true;
    model.reportStationLinkStateChanged();
    QVERIFY(!amp.staleIndicatorVisibleForTesting());
    QVERIFY(!rfKit.staleIndicatorVisibleForTesting());
}

// R-R3-47: a local window shows what it showed before, now through the
// same AmplifierModel and RfKitModel the Core mirrors, fed by this
// computer's own connections. No stale line locally.
void RemotePeripheralsTest::localAppletsShowTheSameValuesAsBefore()
{
    RadioModel model;
    AmpApplet amp(&model);
    Rf2ksApplet rfKit(&model);
    QVERIFY(!amp.staleIndicatorVisibleForTesting());
    QVERIFY(!rfKit.staleIndicatorVisibleForTesting());
    QVERIFY(!amp.operateButtonShownForTesting());

    PgxlConnection* pgxl = model.pgxlConnection();
    pgxl->injectLineForTesting(QStringLiteral("R1|0|") + QString::fromLatin1(kOperate));
    QCOMPARE(amp.tempGaugeValueForTesting(), 42.5);
    QCOMPARE(amp.operateButtonTextForTesting(), QStringLiteral("OPERATE"));
    QCOMPARE(amp.fwdGaugeValueForTesting(), 0.0);  // latched peak not shown
    pgxl->injectLineForTesting(QStringLiteral("S0|status ") + QString::fromLatin1(kTransmit));
    QVERIFY(std::abs(amp.fwdGaugeValueForTesting() - 1000.0) < 1e-3);
    QVERIFY(std::abs(amp.swrGaugeValueForTesting() - 1.12668) < 1e-4);
    QCOMPARE(amp.powerLabelTextForTesting(), QStringLiteral("Volts: 240V\u00A0\u00A0Amps: 22.5A"));
    pgxl->injectLineForTesting(QStringLiteral("S0|status ") + QString::fromLatin1(kStandby));
    QCOMPARE(amp.fwdGaugeValueForTesting(), 0.0);
    QCOMPARE(amp.swrGaugeValueForTesting(), 1.0);
    QCOMPARE(amp.operateButtonTextForTesting(), QStringLiteral("STANDBY"));
    pgxl->injectLineForTesting(QStringLiteral("R2|0|nickname=ShackAmp fan=auto meffa=off led=65"));
    QCOMPARE(amp.meffLabelTextForTesting(), QStringLiteral("MEffA:\u00A0\u00A0\u00A0off"));

    Rf2ksConnection* conn = model.rfKitConnection();
    conn->injectJsonForTesting(QStringLiteral("/power"), kRfKitPower);
    conn->injectJsonForTesting(QStringLiteral("/operate-mode"), R"({"operate_mode":"OPERATE"})");
    QCOMPARE(rfKit.fwdGaugeValueForTesting(), 850);
    QVERIFY(std::abs(rfKit.swrGaugeValueForTesting() - 1.4f) < 1e-4f);
    QCOMPARE(rfKit.tempGaugeValueForTesting(), 27.0f);
    QCOMPARE(rfKit.telemetryStripTextForTesting(),
             QStringLiteral("Fwd 850 W  SWR 1.40  53 V  0.0 A"));
    QCOMPARE(rfKit.operateButtonTextForTesting(), QStringLiteral("OPERATE"));
    conn->injectJsonForTesting(QStringLiteral("/power"), kRfKitIdle);
    QCOMPARE(rfKit.fwdGaugeValueForTesting(), 0);
    QVERIFY(!rfKit.staleIndicatorVisibleForTesting());
}

// R-R3-47: the remote Power Genius row and tab ask the Core (typed link
// requests) and show the Core's state; no socket of this window's own.
void RemotePeripheralsTest::remotePgxlRowAndTabUseTheStationLink()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    model.attachStation(&link);
    FourO3APage page(&model);
    auto* host = page.findChild<QLineEdit*>(QStringLiteral("pgxlHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("pgxlPortSpin"));
    auto* rowConnect = page.findChild<QPushButton*>(QStringLiteral("pgxlConnectButton"));
    auto* scan = page.findChild<QPushButton*>(QStringLiteral("pgxlScanButton"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("pgxlStatusLabel"));
    auto* peripherals = page.findChild<PeripheralsPage*>();
    auto* tabStatus = page.findChild<QLabel*>(QStringLiteral("remotePgxlStatus"));
    auto* tabConnect = page.findChild<QPushButton*>(QStringLiteral("remotePgxlConnectButton"));
    auto* operate = page.findChild<QPushButton*>(QStringLiteral("remotePgxlOperateButton"));
    auto* apply = page.findChild<QPushButton*>(QStringLiteral("remotePgxlApplySettings"));
    auto* keepalive = page.findChild<QSpinBox*>(QStringLiteral("remotePgxlKeepalive"));
    auto* ping = page.findChild<QSpinBox*>(QStringLiteral("remotePgxlPing"));
    auto* autoReconnect = page.findChild<QCheckBox*>(QStringLiteral("remotePgxlAutoReconnect"));
    auto* tabs = page.findChild<QTabWidget*>();
    QVERIFY(host && port && rowConnect && scan && status && peripherals && tabStatus
            && tabConnect && operate && apply && keepalive && ping && autoReconnect && tabs);
    QSignalSpy pgxlFrames(model.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);

    // An older Core: nothing to press, and it says why in user words.
    QVERIFY(!rowConnect->isEnabled());
    QCOMPARE(status->text(),
             QStringLiteral("This Core does not offer Power Genius XL control to this app."));
    QVERIFY(!tabs->isTabEnabled(1));
    QVERIFY(OperatorWording::isPlain(status->text()));

    link.pgxlAvailable = true;
    model.reportStationLinkStateChanged();
    QVERIFY(rowConnect->isEnabled());
    QVERIFY(!scan->isEnabled());
    QVERIFY(tabs->isTabEnabled(1));
    QVERIFY(!operate->isEnabled());
    QCOMPARE(operate->toolTip(), OperatorReasonText::forDisplay(
                                     AmplifierModel::receiveOnlyOperateReason()));
    QVERIFY(OperatorWording::isPlain(operate->toolTip()));

    // Connect sends the draft to the Core; nothing local.
    host->setText(QStringLiteral("amp.station.example"));
    port->setValue(9018);
    QVERIFY(QMetaObject::invokeMethod(peripherals, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 1)));
    QCOMPARE(link.pgxlConfigureCalls, 1);
    QCOMPARE(link.pgxlHost, QStringLiteral("amp.station.example"));
    QCOMPARE(link.pgxlPort, quint16{9018});
    QCOMPARE(link.configureCalls, 0);  // not the tuner's command

    // The Core's state arrives; an identifying amp can be cancelled.
    TunerModel::StationConnectionState identifying =
        state(TunerModel::ConnectionPhase::Identifying, QStringLiteral("amp.station.example"), 9018);
    model.amplifierModel()->setStationConnectionState(identifying);
    QCOMPARE(rowConnect->text(), QStringLiteral("Cancel"));
    QCOMPARE(status->text(), QStringLiteral("Identifying device"));
    QCOMPARE(tabConnect->text(), QStringLiteral("Cancel"));
    QVERIFY(QMetaObject::invokeMethod(peripherals, "onConnect", Qt::DirectConnection,
                                      Q_ARG(int, 1)));
    QCOMPARE(link.pgxlDisconnectCalls, 1);

    // Connected, with the Core's identity.
    TunerModel::StationConnectionState up =
        state(TunerModel::ConnectionPhase::Connected, QStringLiteral("amp.station.example"), 9018);
    up.deviceModel = QStringLiteral("PowerGeniusXL");
    up.deviceSerial = QStringLiteral("10-200/24-0046");
    up.deviceVersion = QStringLiteral("3.8.9");
    model.amplifierModel()->setStationConnectionState(up);
    QCOMPARE(rowConnect->text(), QStringLiteral("Disconnect"));
    QCOMPARE(status->text(), QStringLiteral("Connected: PowerGeniusXL 10-200/24-0046"));
    auto* identity = page.findChild<QLabel*>(QStringLiteral("remotePgxlIdentity"));
    QVERIFY(identity->text().contains(QStringLiteral("10-200/24-0046")));
    QVERIFY(QMetaObject::invokeMethod(&page, "onRemotePgxlConnectClicked", Qt::DirectConnection));
    QCOMPARE(link.pgxlDisconnectCalls, 2);

    // A refusal in the Core's words reaches the row in user words.
    TunerModel::StationConnectionState wrong =
        state(TunerModel::ConnectionPhase::Error, QStringLiteral("amp.station.example"), 9018,
              QStringLiteral("Expected PowerGeniusXL at the connected endpoint; observed "
                             "TunerGenius (serial 241288-1)."));
    model.amplifierModel()->setStationConnectionState(wrong);
    QVERIFY2(status->text().contains(QStringLiteral(
                 "The device at this address is not a Power Genius.")), qPrintable(status->text()));
    QVERIFY2(OperatorWording::isPlain(status->text()), qPrintable(status->text()));
    QVERIFY2(OperatorWording::isPlain(tabStatus->text()), qPrintable(tabStatus->text()));
    for (const QString& raw :
         {QStringLiteral("No matching PGXL discovery announcement for 192.0.2.40:9008. Check the "
                         "amplifier address, port and station LAN discovery."),
          QStringLiteral("PGXL identity serial mismatch: expected 1, observed 2"),
          QStringLiteral("PGXL native identity timed out"),
          QStringLiteral("PGXL native info omitted a nonempty serial"),
          QStringLiteral("PGXL native info failed with code 3"),
          QStringLiteral("PGXL discovery approval timed out for serial 1"),
          QStringLiteral("The station does not support remote PGXL configuration.")}) {
        const QString shown = OperatorReasonText::forDisplay(raw);
        QVERIFY2(shown != raw || !raw.contains(QStringLiteral("PGXL")), qPrintable(raw));
        QVERIFY2(OperatorWording::isPlain(shown), qPrintable(shown));
    }

    // Connection settings go to the Core as one request.
    autoReconnect->setChecked(false);
    keepalive->setValue(45);
    ping->setValue(0);
    QVERIFY(QMetaObject::invokeMethod(&page, "onRemotePgxlApplySettingsClicked",
                                      Qt::DirectConnection));
    QCOMPARE(link.pgxlSettingsCalls, 1);
    QVERIFY(!link.pgxlAutoReconnect);
    QCOMPARE(link.pgxlKeepaliveSec, 45);
    QCOMPARE(link.pgxlPingSec, 0);

    QCOMPARE(pgxlFrames.count(), 0);
    QVERIFY(!model.pgxlConnection()->isConnected());
    QCOMPARE(model.pgxlConnection()->socketAttemptToken(), quint64(0));
}

// R-R3-47 / R-R3-22: end to end over the in-process loopback. The window
// asks; the Core dials the amp (a loopback stand-in), identifies it, pairs
// it; the window follows the Core's `amplifier` object and opens nothing.
void RemotePeripheralsTest::remoteWindowSetsUpThePgxlThroughTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings::instance().setValue(QStringLiteral("PeripheralsMigrationDone"),
                                     QStringLiteral("True"));
    QTcpServer amp;
    QVERIFY(amp.listen(QHostAddress::LocalHost, 0));
    RadioModel station;
    station.enableStationAccessoryIdentity();
    RadioInfo radio;
    radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:73");
    station.setLastRadioInfoForTest(radio);
    station.setConnectionStateForTest(ConnectionState::Connected);
    station.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    station.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());
    QSignalSpy stationFrames(station.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);

    RadioModel window(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&window, &proxy);
    window.attachStation(&client);
    PeripheralsPage page(&window);
    auto* host = page.findChild<QLineEdit*>(QStringLiteral("pgxlHostEdit"));
    auto* port = page.findChild<QSpinBox*>(QStringLiteral("pgxlPortSpin"));
    auto* connectButton = page.findChild<QPushButton*>(QStringLiteral("pgxlConnectButton"));
    auto* status = page.findChild<QLabel*>(QStringLiteral("pgxlStatusLabel"));
    QVERIFY(host && port && connectButton && status);
    QSignalSpy windowFrames(window.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QVERIFY(completed.wait(5000) || !completed.isEmpty());
    QTRY_VERIFY(client.remotePgxlControlAvailable());
    window.reportStationLinkStateChanged();
    QTRY_VERIFY(connectButton->isEnabled());

    // Connect: the Core dials, and the window follows its phase.
    host->setText(QStringLiteral("127.0.0.1"));
    port->setValue(amp.serverPort());
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection, Q_ARG(int, 1)));
    QTRY_VERIFY(amp.hasPendingConnections());
    QTcpSocket* peer = amp.nextPendingConnection();
    peer->write("V3.8.9\n");
    peer->flush();
    QTRY_COMPARE(window.amplifierModel()->connectionPhase(),
                 AmplifierModel::ConnectionPhase::Identifying);
    QCOMPARE(status->text(), QStringLiteral("Identifying device"));
    quint32 infoSeq = 0;
    QTRY_VERIFY([&] {
        for (const auto& row : stationFrames) {
            const QString frame = row.first().toString();
            if (frame.endsWith(QStringLiteral("|info"))) {
                infoSeq = frame.mid(1, frame.indexOf(QLatin1Char('|')) - 1).toUInt();
            }
        }
        return infoSeq != 0;
    }());
    peer->write(QStringLiteral("R%1|0|serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240\n")
                    .arg(infoSeq).toUtf8());
    peer->flush();
    auto* controller = station.findChild<StationPgxlController*>();
    QVERIFY(controller);
    QTRY_VERIFY(controller->findChild<LanDiscovery*>());
    controller->findChild<LanDiscovery*>()->injectDatagramForTesting(
        QStringLiteral("PowerGeniusXL ip=127.0.0.1 v=3.8.9 serial=10-200/24-0046 nickname=PowerGeniusXL"),
        amp.serverPort());
    QTRY_COMPARE(window.amplifierModel()->connectionPhase(),
                 AmplifierModel::ConnectionPhase::Connected);
    QTRY_COMPARE(status->text(), QStringLiteral("Connected: PowerGeniusXL 10-200/24-0046"));
    QCOMPARE(window.amplifierModel()->configuredPort(), int(amp.serverPort()));
    QCOMPARE(connectButton->text(), QStringLiteral("Disconnect"));

    // Configure: the settings command reaches the Core and applies there.
    const auto settingsOutcome = client.requestPgxlConnectionSettings(true, 40, 0);
    QVERIFY(settingsOutcome.sent);
    QTRY_COMPARE(AppSettings::instance().value(QStringLiteral("PGXL_KeepaliveSec")).toString(),
                 QStringLiteral("40"));

    // Disconnect: the Core closes it; the window shows it.
    QVERIFY(QMetaObject::invokeMethod(&page, "onConnect", Qt::DirectConnection, Q_ARG(int, 1)));
    QTRY_COMPARE(window.amplifierModel()->connectionPhase(),
                 AmplifierModel::ConnectionPhase::Disconnected);
    QVERIFY(!station.pgxlConnection()->isConnected());
    QCOMPARE(status->text(), QStringLiteral("Disconnected"));

    // The window opened no connection of its own.
    QCOMPARE(windowFrames.count(), 0);
    QCOMPARE(window.pgxlConnection()->socketAttemptToken(), quint64(0));
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-25: a receive-only Core refuses a window's write to the tuner's
// operate, bypass or antenna and to the amp's operate, with one plain
// reason; nothing changes on the Core and nothing reaches the tuner.
void RemotePeripheralsTest::receiveOnlyCoreRefusesTunerAndAmpOperation()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RadioModel station;
    station.setReceiveOnlyStationPolicy(true);
    // The tuner is connected on the Core (offline parser seam): a write that
    // got through would send `operate=`, `bypass=` or `activate ant=`.
    station.tgxlConnection()->injectLineForTesting(QStringLiteral("V1.2.17"));
    QVERIFY(station.tgxlConnection()->isConnected());
    station.tgxlConnection()->injectLineForTesting(
        QStringLiteral("S0|state operate=0 bypass=0 antA=1 one_by_three=1"));
    QSignalSpy tunerFrames(station.tgxlConnection(), &TgxlConnection::testFrameWrittenForTesting);
    QSignalSpy ampFrames(station.pgxlConnection(), &PgxlConnection::testFrameWrittenForTesting);
    const bool operateBefore = station.tunerModel()->isOperate();
    const bool bypassBefore = station.tunerModel()->isBypass();
    const int antennaBefore = station.tunerModel()->antennaA();
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());

    auto* core = new LoopbackTransport(QStringLiteral("core"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("raw-gui"), this);
    core->linkTo(peer);
    server.acceptTransport(core);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("guard-test"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    const auto messages = [peer] {
        QList<SessionMessage> list;
        for (const QByteArray& wire : peer->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)) { list.append(message); }
        }
        return list;
    };
    QTRY_VERIFY([&] {
        for (const SessionMessage& m : messages()) {
            if (m.kind == SessionMessageKind::SnapshotComplete) { return true; }
        }
        return false;
    }());

    peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        "tuner", {MirrorUpdate{0, "isOperate", MirrorWireKind::Bool, QVariant(!operateBefore)}}, 11)));
    peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        "tuner", {MirrorUpdate{0, "isBypass", MirrorWireKind::Bool, QVariant(!bypassBefore)}}, 12)));
    peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        "tuner", {MirrorUpdate{0, "antennaA", MirrorWireKind::Int64, QVariant(qint64(3))}}, 13)));
    peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        "amplifier", {MirrorUpdate{0, "operate", MirrorWireKind::Bool, QVariant(true)}}, 14)));
    QList<SessionPropertyResult> results;
    QTRY_VERIFY([&] {
        results.clear();
        for (const SessionMessage& m : messages()) {
            if (m.kind == SessionMessageKind::PropertyResult) {
                results.append(m.propertyResults);
            }
        }
        return results.size() >= 3;
    }());
    // The amp object is offered only on a Core that owns its accessories;
    // this Core does not, so only the three tuner results are certain.
    QTest::qWait(50);
    int tunerRefusals = 0;
    for (const SessionPropertyResult& result : results) {
        QVERIFY(!result.accepted);
        if (result.property != "operate") {
            QCOMPARE(result.reason, AmplifierModel::receiveOnlyOperateReason());
            ++tunerRefusals;
        }
    }
    QCOMPARE(tunerRefusals, 3);
    QVERIFY(OperatorWording::isPlain(AmplifierModel::receiveOnlyOperateReason()));
    QCOMPARE(tunerFrames.count(), 0);
    QCOMPARE(ampFrames.count(), 0);
    QCOMPARE(station.tunerModel()->isOperate(), operateBefore);
    QCOMPARE(station.tunerModel()->isBypass(), bypassBefore);
    QCOMPARE(station.tunerModel()->antennaA(), antennaBefore);
    core->closeLink(QStringLiteral("test done"));
}

// R-R3-47 / R-R3-48: the RF-Kit page and applet in a remote window ask the
// Core, which switches, identifies, connects and disconnects its amp; the
// window sees the rows and the band-follow line, and dials nothing.
void RemotePeripheralsTest::remoteWindowSetsUpTheRfKitThroughTheCore()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings::instance().setValue(QStringLiteral("PeripheralsMigrationDone"),
                                     QStringLiteral("True"));
    AppSettings::instance().setValue(QStringLiteral("RfKit_PollIntervalMs"), QStringLiteral("5000"));
    FakeRfKit amp;
    RadioModel station;
    station.enableStationAccessoryIdentity();
    station.setReceiveOnlyStationPolicy(true);
    RadioInfo radio;
    radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:83");
    station.setLastRadioInfoForTest(radio);
    station.setConnectionStateForTest(ConnectionState::Connected);
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());

    RadioModel window(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&window, &proxy);
    window.attachStation(&client);
    RfKitPage page(&window);
    Rf2ksApplet applet(&window);
    QCheckBox* master = page.masterCheckboxForTesting();
    QVERIFY(master);
    QVERIFY(!master->isEnabled());   // no Core yet

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QVERIFY(completed.wait(5000) || !completed.isEmpty());
    QTRY_VERIFY(client.remoteRfKitControlAvailable());
    window.reportStationLinkStateChanged();
    QTRY_VERIFY(master->isEnabled());
    QVERIFY(!window.rfKitEnabled());
    QVERIFY(!page.detailTabIsEnabledForTesting());

    // Switch it on at the Core.
    master->setChecked(true);
    QTRY_VERIFY(station.rfKitEnabled());
    QTRY_VERIFY(window.rfKitEnabled());
    QVERIFY(master->isChecked());
    QTRY_VERIFY(page.detailTabIsEnabledForTesting());

    // Connect: the Core identifies the amp and admits it.
    page.setHostForTesting(QStringLiteral("127.0.0.1"));
    page.setPortForTesting(amp.serverPort());
    page.testConnectionButtonForTesting()->click();
    QTRY_COMPARE_WITH_TIMEOUT(window.rfKitModel()->connectionPhase(),
                              RfKitModel::ConnectionPhase::Connected, 5000);
    QCOMPARE(window.rfKitModel()->deviceModel(), QStringLiteral("RF2K-S"));
    QCOMPARE(window.rfKitModel()->configuredPort(), int(amp.serverPort()));
    QCOMPARE(station.peripheralValue(QStringLiteral("RfKit_ManualIp")), QStringLiteral("127.0.0.1"));
    QTRY_VERIFY(page.liveStatusTextForTesting().contains(QStringLiteral("Connected")));
    QVERIFY(OperatorWording::isPlain(page.liveStatusTextForTesting()));
    QTRY_VERIFY(applet.connectedStateForTesting());

    // The rows the Core's amp reports reach the window's applet.
    station.rfKitConnection()->injectJsonForTesting(QStringLiteral("/power"), kRfKitPower);
    station.rfKitConnection()->injectJsonForTesting(QStringLiteral("/tuner"),
        R"({"mode":"AUTO","setup":"LC","L":{"value":1200,"unit":"nH"},"C":{"value":345,"unit":"pF"},"tuned_frequency":{"value":3891,"unit":"kHz"},"segment_size":{"value":9,"unit":"kHz"}})");
    station.rfKitConnection()->injectJsonForTesting(QStringLiteral("/antennas"),
        R"({"antennas":[{"type":"INTERNAL","number":1,"state":"AVAILABLE"},{"type":"INTERNAL","number":2,"state":"ACTIVE"},{"type":"INTERNAL","number":3,"state":"AVAILABLE"},{"type":"INTERNAL","number":4,"state":"AVAILABLE"}]})");
    station.rfKitConnection()->injectJsonForTesting(QStringLiteral("/antennas/active"),
                                                   R"({"type":"INTERNAL","number":2})");
    QTRY_COMPARE(applet.tunerStatusTextForTesting(), QStringLiteral("TUNED 3.891 MHz (LC)"));
    QTRY_VERIFY(applet.antennaButtonIsActiveForTesting(2));
    QVERIFY(!applet.antennaButtonIsActiveForTesting(1));
    QVERIFY(!applet.antennaButtonIsEnabledForTesting(2));   // antennas wait for remote transmit

    // Band follow: the Core runs no station TCI server here, so it is off.
    QCOMPARE(applet.bandFollowTextForTesting(), window.rfKitModel()->bandFollowText());
    QCOMPARE(page.bandFollowTextForTesting(), window.rfKitModel()->bandFollowText());
    QVERIFY(page.bandFollowTextForTesting().startsWith(QStringLiteral("Band follow: off")));

    // The applet's Disconnect asks the Core (MainWindow sends it).
    std::unique_ptr<QMenu> menu(applet.buildContextMenuForTesting());
    bool toggleEnabled = false;
    for (QAction* a : menu->actions()) {
        if (a->text() == QStringLiteral("Disconnect")) { toggleEnabled = a->isEnabled(); }
    }
    QVERIFY(toggleEnabled);

    // Disconnect from the page.
    page.disconnectButtonForTesting()->click();
    QTRY_COMPARE(window.rfKitModel()->connectionPhase(),
                 RfKitModel::ConnectionPhase::Disconnected);
    QVERIFY(!station.rfKitConnection()->isConnected());

    // Switch it off.
    master->setChecked(false);
    QTRY_VERIFY(!station.rfKitEnabled());
    QTRY_VERIFY(!window.rfKitEnabled());
    QTRY_COMPARE(window.rfKitModel()->connectionPhase(), RfKitModel::ConnectionPhase::Disabled);

    // The window opened no connection of its own.
    QVERIFY(!window.rfKitConnection()->isConnected());
    QCOMPARE(window.rfKitConnection()->pollsSucceeded() + window.rfKitConnection()->pollsFailed(), 0);
    QVERIFY(window.rfKitConnection()->peerAddress().isEmpty());
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-47: an app that writes the RF-Kit switch as a raw value (every app
// before this one) is refused in plain words; the Core's switch stays.
void RemotePeripheralsTest::rawRfKitSwitchWriteIsRefused()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    AppSettings::instance().setValue(QStringLiteral("PeripheralsMigrationDone"),
                                     QStringLiteral("True"));
    RadioModel station;
    station.enableStationAccessoryIdentity();
    RadioInfo radio;
    radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:84");
    station.setLastRadioInfoForTest(radio);
    station.setConnectionStateForTest(ConnectionState::Connected);
    QVERIFY(!station.rfKitEnabled());
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());

    auto* core = new LoopbackTransport(QStringLiteral("core"), this);
    auto* peer = new LoopbackTransport(QStringLiteral("raw-gui"), this);
    core->linkTo(peer);
    server.acceptTransport(core);
    peer->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("switch-test"))));
    peer->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    const auto messages = [peer] {
        QList<SessionMessage> list;
        for (const QByteArray& wire : peer->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)) { list.append(message); }
        }
        return list;
    };
    QTRY_VERIFY([&] {
        for (const SessionMessage& m : messages()) {
            if (m.kind == SessionMessageKind::SnapshotComplete) { return true; }
        }
        return false;
    }());
    peer->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
        "radio", {MirrorUpdate{0, "rfKitEnabled", MirrorWireKind::Bool, QVariant(true)}}, 21)));
    SessionPropertyResult result;
    QTRY_VERIFY([&] {
        for (const SessionMessage& m : messages()) {
            if (m.kind == SessionMessageKind::PropertyResult && !m.propertyResults.isEmpty()) {
                result = m.propertyResults.first();
                return true;
            }
        }
        return false;
    }());
    QVERIFY(!result.accepted);
    QCOMPARE(result.reason,
             QStringLiteral("Update this app to turn the RF-Kit amplifier on or off on this Core."));
    QVERIFY(OperatorWording::isPlain(result.reason));
    QVERIFY(!station.rfKitEnabled());

    // The command is what changes it.
    peer->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
        "setRfKitEnabled", 31, {MirrorUpdate{0, "enabled", MirrorWireKind::Bool, QVariant(true)}})));
    QTRY_VERIFY(station.rfKitEnabled());
    core->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-48: the Power Genius's band-follow line on the applet and the 4O3A
// page follows its pairing, in a local window and from the Core.
void RemotePeripheralsTest::pgxlBandFollowLineLocalAndRemote()
{
    // The amp's own reports drive the state: connected, paired, dropped.
    // (A bare connection and model, so no pairing request is sent.)
    {
        PgxlConnection conn;
        AmplifierModel amp;
        amp.bindConnection(&conn);
        QCOMPARE(amp.bandFollow(), TunerModel::BandFollow::Off);
        emit conn.connected();
        QCOMPARE(amp.bandFollow(), TunerModel::BandFollow::Waiting);
        emit conn.pairingResult(false, QStringLiteral("R12|1|"));
        QCOMPARE(amp.bandFollow(), TunerModel::BandFollow::Waiting);
        emit conn.pairingResult(true, QString());
        QCOMPARE(amp.bandFollow(), TunerModel::BandFollow::Following);
        emit conn.disconnected();
        QCOMPARE(amp.bandFollow(), TunerModel::BandFollow::Off);
    }

    // A local window's applet and 4O3A page show the line.
    RadioModel local;
    AmpApplet localApplet(&local);
    FourO3APage localPage(&local);
    auto* localLine = localPage.findChild<QLabel*>(QStringLiteral("pgxlBandFollowLabel"));
    QVERIFY(localLine);
    QCOMPARE(localApplet.bandFollowTextForTesting(),
             QStringLiteral("Band follow: off while the Power Genius is not connected."));
    local.amplifierModel()->setBandFollow(TunerModel::BandFollow::Waiting);
    QCOMPARE(localApplet.bandFollowTextForTesting(),
             QStringLiteral("Band follow: waiting for the Power Genius to pair with the radio."));
    local.amplifierModel()->setBandFollow(TunerModel::BandFollow::Following);
    QCOMPARE(localApplet.bandFollowTextForTesting(),
             QStringLiteral("Band follow: following the radio"));
    QCOMPARE(localLine->text(), QStringLiteral("Band follow: following the radio"));
    for (const QString& text : {localApplet.bandFollowTextForTesting(), localLine->text()}) {
        QVERIFY(OperatorWording::isPlain(text));
    }

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RadioModel station;
    station.enableStationAccessoryIdentity();
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());
    RadioModel window(RadioModel::Role::Remote);
    SettingsProxy proxy;
    StationClient client(&window, &proxy);
    window.attachStation(&client);
    AmpApplet remoteApplet(&window);
    FourO3APage remotePage(&window);
    auto* remoteLine = remotePage.findChild<QLabel*>(QStringLiteral("pgxlBandFollowLabel"));
    QVERIFY(remoteLine);
    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(&client, &StationClient::handshakeComplete);
    client.startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QVERIFY(completed.wait(5000) || !completed.isEmpty());
    station.amplifierModel()->setBandFollow(TunerModel::BandFollow::Following);
    QTRY_COMPARE(window.amplifierModel()->bandFollow(), TunerModel::BandFollow::Following);
    QCOMPARE(remoteApplet.bandFollowTextForTesting(),
             QStringLiteral("Band follow: following the radio"));
    QCOMPARE(remoteLine->text(), QStringLiteral("Band follow: following the radio"));
    station.amplifierModel()->setBandFollow(TunerModel::BandFollow::Waiting);
    QTRY_COMPARE(remoteApplet.bandFollowTextForTesting(),
                 QStringLiteral("Band follow: waiting for the Power Genius to pair with the radio."));
    stationEnd->closeLink(QStringLiteral("test done"));
}

// R-R3-48: the app's one TCI switch turns the Core's station server on and
// off. The Core keeps it when the window goes and when another connects.
// On the same computer as the Core, the window runs no server of its own
// and apps here reach the Core's.
void RemotePeripheralsTest::oneTciSwitchDrivesTheCoresStationServer()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const quint16 port = freeLoopbackPort();
    RadioModel station;
    station.enableStationTci(QStringLiteral("127.0.0.1"));
    AppSettings stationSettings(dir.filePath(QStringLiteral("station.settings")));
    StationServer server(&station, stationSettings, dir.path());
    QCOMPARE(server.stationTciVersion(), 1);

    auto window = std::make_unique<RadioModel>(RadioModel::Role::Remote);
    SettingsProxy proxy;
    auto client = std::make_unique<StationClient>(window.get(), &proxy);
    window->attachStation(client.get());
    client->setCoreOnThisComputerForTest(true);
    auto local = std::make_unique<TciServer>(window.get());
    auto tci = std::make_unique<TciSwitch>(local.get(), window.get());
    CatTciServerPage page;
    page.setRadioModel(window.get());
    QVERIFY(page.stationLineForTesting().isEmpty());

    auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
    auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
    stationEnd->linkTo(clientEnd);
    QSignalSpy completed(client.get(), &StationClient::handshakeComplete);
    client->startSession(clientEnd, server.token());
    server.acceptTransport(stationEnd);
    QVERIFY(completed.wait(5000) || !completed.isEmpty());
    QTRY_VERIFY(client->stationTciAvailable());
    window->reportStationLinkStateChanged();

    // On: the Core listens on this port; this window runs none of its own.
    tci->setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
    QTRY_VERIFY(station.stationTciModel()->listening());
    QCOMPARE(station.stationTciModel()->port(), int(port));
    QVERIFY(!local->isRunning());
    QTRY_VERIFY(window->stationTciModel()->listening());
    QCOMPARE(page.stationLineForTesting(),
             QStringLiteral("The Core on this computer serves TCI apps here, port %1.").arg(port));
    QVERIFY(OperatorWording::isPlain(page.stationLineForTesting()));
    {
        QWebSocket app;
        QStringList frames;
        connect(&app, &QWebSocket::textMessageReceived, &app,
                [&frames](const QString& text) { frames.append(text); });
        app.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
        QTRY_VERIFY(frames.join(QString()).contains(QStringLiteral("receive_only:true;")));
        app.close();
    }

    // The window goes: the Core keeps its switch.
    stationEnd->closeLink(QStringLiteral("window closed"));
    tci.reset();
    local.reset();
    client.reset();
    window.reset();
    QTest::qWait(50);
    QVERIFY(station.stationTciModel()->enabled());
    QVERIFY(station.stationTciModel()->listening());

    // Another app connects: the Core's switch is unchanged, and that app
    // sees it; its own switch turns both off.
    RadioModel second(RadioModel::Role::Remote);
    SettingsProxy secondProxy;
    StationClient secondClient(&second, &secondProxy);
    second.attachStation(&secondClient);
    TciServer secondLocal(&second);
    TciSwitch secondSwitch(&secondLocal, &second);
    auto* stationEnd2 = new LoopbackTransport(QStringLiteral("station-end-2"), this);
    auto* clientEnd2 = new LoopbackTransport(QStringLiteral("client-end-2"), this);
    stationEnd2->linkTo(clientEnd2);
    QSignalSpy completed2(&secondClient, &StationClient::handshakeComplete);
    secondClient.startSession(clientEnd2, server.token());
    server.acceptTransport(stationEnd2);
    QVERIFY(completed2.wait(5000) || !completed2.isEmpty());
    QTRY_VERIFY(second.stationTciModel()->listening());
    QVERIFY(station.stationTciModel()->listening());
    QTRY_VERIFY(secondClient.stationTciAvailable());

    secondSwitch.setSwitch(false, port, QHostAddress(QHostAddress::LocalHost));
    QTRY_VERIFY(!station.stationTciModel()->enabled());
    QVERIFY(!station.stationTciModel()->listening());
    QVERIFY(!secondLocal.isRunning());
    QTRY_VERIFY(!second.stationTciModel()->listening());
    stationEnd2->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

namespace {

// R-R3-47: a Core that owns its accessories and a remote window on it, over
// the in-process loopback.
struct CoreAndWindow {
    QTemporaryDir dir;
    RadioModel station;
    AppSettings stationSettings;
    StationServer server;
    RadioModel window{RadioModel::Role::Remote};
    SettingsProxy proxy;
    StationClient client{&window, &proxy};
    CoreAndWindow()
        : stationSettings(dir.filePath(QStringLiteral("station.settings")))
        , server((prepareStation(station), &station), stationSettings, dir.path())
    {
        window.attachStation(&client);
    }
    static void prepareStation(RadioModel& core)
    {
        AppSettings::instance().setValue(QStringLiteral("PeripheralsMigrationDone"),
                                         QStringLiteral("True"));
        core.enableStationAccessoryIdentity();
        core.setReceiveOnlyStationPolicy(true);
        RadioInfo radio;
        radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:47");
        core.setLastRadioInfoForTest(radio);
        core.setConnectionStateForTest(ConnectionState::Connected);
    }
    LoopbackTransport* connect(QObject* owner)
    {
        auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), owner);
        auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), owner);
        stationEnd->linkTo(clientEnd);
        client.startSession(clientEnd, server.token());
        server.acceptTransport(stationEnd);
        return stationEnd;
    }
};

} // namespace

// R-R3-47 / R-R3-22: a remote window's Power Genius and Tuner Genius pages
// show what the Core keeps, live, and change it only through the Core.
void RemotePeripheralsTest::remoteWindowShowsTheCoresRecords()
{
    AppSettings::instance().clear();
    CoreAndWindow cw;
    RadioModel& station = cw.station;
    RadioModel& window = cw.window;
    PgxlAdvancedPage pgxlPage(&window);
    TgxlAdvancedPage tgxlPage(&window);
    QVERIFY(OperatorWording::isPlain(pgxlPage.remoteNoteForTesting()));
    QVERIFY(!pgxlPage.powerCapCheckForTesting()->isEnabled());   // no Core yet

    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.accessoryDataAvailable());
    QCOMPARE(cw.server.accessoryDataVersion(), 1);
    window.reportStationLinkStateChanged();
    QTRY_VERIFY(pgxlPage.powerCapCheckForTesting()->isEnabled());
    QVERIFY(OperatorWording::isPlain(pgxlPage.remoteNoteForTesting()));

    // A Power Genius fault on the Core appears without a reconnect, with
    // its time, device and plain words. (The Core's amp connection takes
    // status lines only from an admitted amp, so the fault is recorded as
    // RadioModel::onPgxlStatus records it, with the captured readings.)
    station.pgxlFaultLog()->capture(FaultEvent{QDateTime::currentMSecsSinceEpoch(),
                                               QStringLiteral("FAULT"), 1820.0f, 2.85f, 78.0f,
                                               FaultLog::likelyCauseFor(1820.0f, 2.85f, 78.0f)});
    QCOMPARE(station.pgxlFaultLog()->events().size(), 1);
    QTRY_COMPARE(window.pgxlFaultLog()->events().size(), 1);
    const FaultEvent pgxlFault = window.pgxlFaultLog()->events().first();
    QCOMPARE(pgxlFault.device, QStringLiteral("pgxl"));
    QCOMPARE(pgxlFault.whenMs, station.pgxlFaultLog()->events().first().whenMs);
    QVERIFY(pgxlFault.text.startsWith(QStringLiteral("The Power Genius reported a fault.")));
    QCOMPARE(pgxlPage.faultRowCountForTesting(), 1);
    QCOMPARE(pgxlPage.faultTextForTesting(0), pgxlFault.text);
    QVERIFY(OperatorWording::isPlain(pgxlPage.faultTextForTesting(0)));

    // An RF-Kit fault (the amp's interface error) and a Tuner Genius one.
    station.rfKitConnection()->injectJsonForTesting(
        QStringLiteral("/operational-interface"),
        R"({"operational_interface":"UDP","error":"CAT timeout"})");
    QTRY_COMPARE(window.rfkitFaultLog()->events().size(), 1);
    QCOMPARE(window.rfkitFaultLog()->events().first().detail, QStringLiteral("CAT timeout"));
    QVERIFY(OperatorWording::isPlain(window.rfkitFaultLog()->events().first().text));
    station.tgxlFaultLog()->captureNotice(QStringLiteral("link"),
                                          QStringLiteral("The Tuner Genius stopped answering."),
                                          QString());
    QTRY_COMPARE(tgxlPage.faultRowCountForTesting(), 1);
    QCOMPARE(tgxlPage.faultTextForTesting(0), QStringLiteral("The Tuner Genius stopped answering."));
    QVERIFY(window.accessoryDataModel()->faultRevision() >= 3);

    // Cleared from the remote page: the Core's history goes, and the window's.
    pgxlPage.clearFaultsButtonForTesting()->click();
    QTRY_VERIFY(station.pgxlFaultLog()->events().isEmpty());
    QTRY_COMPARE(pgxlPage.faultRowCountForTesting(), 0);
    tgxlPage.clearFaultsButtonForTesting()->click();
    QTRY_VERIFY(station.tgxlFaultLog()->events().isEmpty());
    QTRY_COMPARE(tgxlPage.faultRowCountForTesting(), 0);
    QCOMPARE(station.rfkitFaultLog()->events().size(), 1);   // only the one asked for

    // The counters are the Core's: a retry the Core's amp connection counts
    // shows here; one on the window's own (idle) connection does not count.
    emit window.pgxlConnection()->reconnectAttempt(1, 1000);
    window.pgxlDiagnostics()->testFlushCoalesceTimer();
    QCOMPARE(pgxlPage.reconnectCountTextForTesting(), QStringLiteral("0"));
    emit station.pgxlConnection()->reconnectAttempt(1, 1000);
    emit station.pgxlConnection()->reconnectAttempt(2, 2000);
    station.pgxlDiagnostics()->testFlushCoalesceTimer();
    QTRY_COMPARE(pgxlPage.reconnectCountTextForTesting(), QStringLiteral("2"));
    QCOMPARE(window.accessoryDataModel()->pgxlReconnectCount(), 2);
    emit station.tgxlConnection()->reconnectAttempt(1, 1000);
    station.tgxlDiagnostics()->testFlushCoalesceTimer();
    QTRY_COMPARE(tgxlPage.reconnectCountTextForTesting(), QStringLiteral("1"));

    // The output limit is set through the Core; the Core raises the alert
    // and the window sees it.
    pgxlPage.powerCapSpinForTesting()->setValue(800);   // limit off: not sent yet
    pgxlPage.powerCapCheckForTesting()->setChecked(true);
    QTRY_VERIFY(station.accessoryDataModel()->powerCapEnabled());
    QCOMPARE(station.accessoryDataModel()->powerCapW(), 800);
    QTRY_VERIFY(window.accessoryDataModel()->powerCapEnabled());
    // The amp's peak forward power as the Core reads it (onPgxlStatus).
    emit station.ampMetersChanged(1000.0f, 1.13f);
    QTRY_COMPARE(window.accessoryDataModel()->powerCapAlertCount(), 1);
    QVERIFY(window.accessoryDataModel()->powerCapExceeded());
    QCOMPARE(window.accessoryDataModel()->powerCapAlertText(),
             QStringLiteral("Power Genius output 1000 W is above the 800 W limit."));

    // Antenna names and tune memory are the Core's. (A window's own edit
    // reaches the Core as a station setting; the Core then publishes it.)
    AppSettings::instance().setValue(QStringLiteral("TGXL_Ant1_Label"), QStringLiteral("Dipole"));
    station.applyRemoteAccessorySetting(QStringLiteral("TGXL_Ant1_Label"));
    QTRY_COMPARE(window.accessoryDataModel()->tgxlAntenna1Label(), QStringLiteral("Dipole"));
    QTRY_COMPARE(tgxlPage.antennaLabelForTesting(1), QStringLiteral("Dipole"));
    station.tuneMemoryStore()->store(TuneMemory{2, Band::Band40m, 10, 20, 30, 1790000000000});
    QTRY_COMPARE(window.tuneMemoryStore()->listAll().size(), 1);
    QCOMPARE(window.tuneMemoryStore()->listAll().first().band, Band::Band40m);
    QCOMPARE(tgxlPage.tuneMemoryRowCountForTesting(), 1);

    // The window opened no accessory connection of its own.
    QVERIFY(!window.pgxlConnection()->isConnected());
    QVERIFY(!window.tgxlConnection()->isConnected());
    QVERIFY(!window.rfKitConnection()->isConnected());
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-47 / R-R3-22: the interlock policy changed from a remote window takes
// effect on the Core at once, where the refusal to transmit happens; the
// window that changed it and a window that connects later show it; a
// request the Core cannot take changes nothing and says why.
void RemotePeripheralsTest::remoteWindowChangesTheInterlockOnTheCore()
{
    AppSettings::instance().clear();
    CoreAndWindow cw;
    RadioModel& station = cw.station;
    PgxlInterlockPage page(&cw.window);
    QVERIFY(!page.modeComboForTesting()->isEnabled());
    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.accessoryDataAvailable());
    cw.window.reportStationLinkStateChanged();
    QTRY_VERIFY(page.modeComboForTesting()->isEnabled());
    QVERIFY(OperatorWording::isPlain(page.remoteNoteForTesting()));
    QVERIFY(station.txInterlockPolicy()->evaluateTxRequest(true, false, 1.0f));

    page.modeComboForTesting()->setCurrentIndex(2);   // Block
    QTRY_COMPARE(station.txInterlockPolicy()->mode(), TxInterlockPolicy::Block);
    QCOMPARE(AppSettings::instance().value(QStringLiteral("PGXL_TxInterlockMode")).toString(),
             QStringLiteral("Block"));
    // The Core refuses to transmit now; the window's copy is the Core's.
    QVERIFY(!station.txInterlockPolicy()->evaluateTxRequest(true, false, 1.0f));
    QTRY_COMPARE(cw.window.txInterlockPolicy()->mode(), TxInterlockPolicy::Block);
    QCOMPARE(cw.window.accessoryDataModel()->interlockMode(),
             AccessoryDataModel::InterlockMode::Block);
    QCOMPARE(page.modeComboForTesting()->currentIndex(), 2);

    page.swrGateCheckboxForTesting()->setChecked(true);
    page.graceSpinboxForTesting()->setValue(1250);
    page.swrGateMaxSpinboxForTesting()->setValue(2.4);
    QTRY_COMPARE(station.txInterlockPolicy()->graceMs(), 1250);
    QTRY_VERIFY(qFuzzyCompare(station.txInterlockPolicy()->swrGateMax(), 2.4f));
    QVERIFY(station.txInterlockPolicy()->swrGateEnabled());
    QTRY_COMPARE(cw.window.txInterlockPolicy()->graceMs(), 1250);

    // A request the Core cannot take: nothing changes; the window says why,
    // as an accessory refusal (L1), never as a slice one.
    QSignalSpy sliceToast(&cw.window, &RadioModel::sliceAddRejected);
    QSignalSpy refused(&cw.window, &RadioModel::accessoryRequestRefused);
    QVERIFY(cw.client.requestTxInterlockPolicy(1, 99999, false, 2.0).sent);
    QTRY_COMPARE(refused.count(), 1);
    QCOMPARE(refused.first().at(0).toString(), QStringLiteral("interlock"));
    QVERIFY(OperatorWording::isPlain(refused.first().at(1).toString()));
    QCOMPARE(sliceToast.count(), 0);
    QCOMPARE(station.txInterlockPolicy()->mode(), TxInterlockPolicy::Block);
    QCOMPARE(page.modeComboForTesting()->currentIndex(), 2);

    // A window that connects later shows the Core's policy.
    stationEnd->closeLink(QStringLiteral("first window gone"));
    RadioModel second(RadioModel::Role::Remote);
    SettingsProxy secondProxy;
    StationClient secondClient(&second, &secondProxy);
    second.attachStation(&secondClient);
    PgxlInterlockPage secondPage(&second);
    auto* stationEnd2 = new LoopbackTransport(QStringLiteral("station-end-2"), this);
    auto* clientEnd2 = new LoopbackTransport(QStringLiteral("client-end-2"), this);
    stationEnd2->linkTo(clientEnd2);
    secondClient.startSession(clientEnd2, cw.server.token());
    cw.server.acceptTransport(stationEnd2);
    QTRY_COMPARE(second.txInterlockPolicy()->mode(), TxInterlockPolicy::Block);
    QCOMPARE(second.txInterlockPolicy()->graceMs(), 1250);
    QTRY_COMPARE(secondPage.modeComboForTesting()->currentIndex(), 2);
    QCOMPARE(secondPage.graceSpinboxForTesting()->value(), 1250);
    stationEnd2->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-47: a Core that does not share its interlock (an older Core) leaves
// the section showing, unchangeable, with the reason in user words.
void RemotePeripheralsTest::olderCoreLeavesTheInterlockSayingWhy()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    link.linkReady = true;
    model.attachStation(&link);
    PgxlInterlockPage page(&model);
    QVERIFY(!page.modeComboForTesting()->isEnabled());
    QVERIFY(!page.graceSpinboxForTesting()->isEnabled());
    QVERIFY(page.remoteNoteForTesting().contains(QStringLiteral("does not share")));
    QVERIFY(OperatorWording::isPlain(page.remoteNoteForTesting()));
    PgxlAdvancedPage advanced(&model);
    QVERIFY(!advanced.powerCapCheckForTesting()->isEnabled());
    QVERIFY(!advanced.clearFaultsButtonForTesting()->isEnabled());
    QVERIFY(OperatorWording::isPlain(advanced.remoteNoteForTesting()));
}

namespace {

// Accept the modal dialog a local page opens (Save & Reboot).
void acceptNextModal()
{
    QTimer::singleShot(0, [] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
            dialog->accept();
        }
    });
}

void editName(QLineEdit* edit, const QString& name)
{
    edit->setText(name);
    edit->setModified(true);
    emit edit->editingFinished();
}

// The Core's Power Genius, admitted: the V banner, the captured `info`
// reply and the captured discovery announcement.
bool admitCoreAmp(RadioModel& station, FakeGenius& amp)
{
    QString reason;
    if (!station.configurePgxlForStation(QStringLiteral("127.0.0.1"), amp.port(), &reason)
        || !amp.accept()) {
        return false;
    }
    amp.send(QStringLiteral("V3.8.9"));
    const int info = amp.waitFor(QStringLiteral("info"));
    if (info < 0) {
        return false;
    }
    amp.reply(info, QStringLiteral("0|serial=10-200/24-0046  version=3.8.9 protocol=1.0 mains=240"));
    auto* controller = station.findChild<StationPgxlController*>();
    if (!controller
        || !QTest::qWaitFor([&] { return controller->findChild<LanDiscovery*>() != nullptr; },
                            3000)) {
        return false;
    }
    controller->findChild<LanDiscovery*>()->injectDatagramForTesting(
        QStringLiteral("PowerGeniusXL ip=127.0.0.1 v=3.8.9 serial=10-200/24-0046 "
                       "nickname=PowerGeniusXL"),
        amp.port());
    return QTest::qWaitFor([&] { return station.pgxlConnection()->isConnected(); }, 3000);
}

// The Core's Tuner Genius, admitted likewise.
bool admitCoreTuner(RadioModel& station, FakeGenius& tuner)
{
    QString reason;
    if (!station.configureTgxlForStation(QStringLiteral("127.0.0.1"), tuner.port(), &reason)
        || !tuner.accept()) {
        return false;
    }
    tuner.send(QStringLiteral("V1.2.17"));
    const int info = tuner.waitFor(QStringLiteral("info"));
    if (info < 0) {
        return false;
    }
    tuner.reply(info, QStringLiteral("0|info serial=241288-1 version=1.2.17 "
                                     "nickname=Tuner_Genius_XL 3way=1"));
    auto* controller = station.findChild<StationTgxlController*>();
    if (!controller
        || !QTest::qWaitFor([&] { return controller->findChild<LanDiscovery*>() != nullptr; },
                            3000)) {
        return false;
    }
    controller->findChild<LanDiscovery*>()->injectDatagramForTesting(
        QStringLiteral("TunerGeniusXL ip=127.0.0.1 v=1.2.17 serial=241288-1 "
                       "nickname=Tuner_Genius_XL"),
        tuner.port());
    return QTest::qWaitFor([&] { return station.tgxlConnection()->isConnected(); }, 3000);
}

} // namespace

// R-R3-47 / R-R3-22: every control on a remote window's Power Genius
// Advanced page works through the Core. The same clicks on a remote page
// and on a local window's page reach their amps as the same commands; the
// remote page asks before network changes and Save & Reboot, and sends
// nothing on a no; the amp's answers and values, and the Core's refusals,
// show on the page in plain words; the window opens no connection to the amp
// and nothing operates it.
void RemotePeripheralsTest::remoteWindowChangesTheAmpsOwnSettingsThroughTheCore()
{
    AppSettings::instance().clear();
    CoreAndWindow cw;
    RadioModel& station = cw.station;
    RadioModel& window = cw.window;
    station.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    station.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    FakeGenius amp;
    QVERIFY(amp.listen());
    PgxlAdvancedPage page(&window);
    bool yes = true;
    QStringList asked;
    page.setConfirmationForTesting([&](const QString& title, const QString& text) {
        asked.append(title + QLatin1Char('|') + text);
        return yes;
    });
    // A local window's page, on its own amp, for the same clicks.
    FakeGenius localAmp;
    QVERIFY(localAmp.listen());
    RadioModel local;
    PgxlAdvancedPage localPage(&local);

    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.pgxlDeviceSettingsAvailable());
    window.reportStationLinkStateChanged();
    QVERIFY(page.nicknameEditForTesting()->isEnabled());
    QVERIFY(page.pairAttemptCheckForTesting()->isEnabled());
    // The Core has no amp yet: Apply, Revert and Save & Reboot wait for it,
    // and a request says why in the Core's words.
    QVERIFY(!page.applyNetworkButtonForTesting()->isEnabled());
    QVERIFY(!page.revertButtonForTesting()->isEnabled());
    editName(page.nicknameEditForTesting(), QStringLiteral("Offline"));
    QTRY_COMPARE(page.deviceAnswerForTesting(),
                 QStringLiteral("The Core is not connected to the Power Genius."));
    QVERIFY(OperatorWording::isPlain(page.deviceAnswerForTesting()));

    QVERIFY(admitCoreAmp(station, amp));
    QTRY_VERIFY(page.applyNetworkButtonForTesting()->isEnabled());
    QTRY_COMPARE(page.firmwareTextForTesting(), QStringLiteral("3.8.9"));
    local.pgxlConnection()->connectToPgxl(QStringLiteral("127.0.0.1"), localAmp.port());
    QVERIFY(localAmp.accept());
    localAmp.send(QStringLiteral("V3.8.9"));
    // The local page reads the amp's settings when it connects.
    QVERIFY(localAmp.waitFor(QStringLiteral("ifconf read")) >= 0);

    // I5: DHCP off with no address is not sent from either page, and the
    // remote page asks nothing; both say why in the same words.
    {
        const int remoteBefore = amp.commands.size();
        const int localBefore = localAmp.commands.size();
        const qsizetype askedBefore = asked.size();
        for (PgxlAdvancedPage* p : {&page, &localPage}) {
            p->dhcpCheckForTesting()->setChecked(false);
            p->ipEditForTesting()->clear();
            p->netmaskEditForTesting()->clear();
            p->gatewayEditForTesting()->clear();
            p->applyNetworkButtonForTesting()->click();
            QCOMPARE(p->networkProblemForTesting(),
                     QStringLiteral("Without DHCP, enter an address and a netmask."));
            QVERIFY(OperatorWording::isPlain(p->networkProblemForTesting()));
        }
        QCOMPARE(asked.size(), askedBefore);
        QTest::qWait(100);
        for (int i = remoteBefore; i < amp.commands.size(); ++i) {
            QVERIFY2(!amp.commands.at(i).startsWith(QStringLiteral("ifconf address")),
                     qPrintable(amp.commands.at(i)));
        }
        for (int i = localBefore; i < localAmp.commands.size(); ++i) {
            QVERIFY2(!localAmp.commands.at(i).startsWith(QStringLiteral("ifconf address")),
                     qPrintable(localAmp.commands.at(i)));
        }
    }

    // ---- The same clicks on both pages reach the amps as the same commands.
    const int remoteMark = amp.commands.size();
    const int localMark = localAmp.commands.size();
    const auto drive = [](PgxlAdvancedPage& p) {
        editName(p.nicknameEditForTesting(), QStringLiteral("Shack PGXL"));
        p.biasClassAForTesting()->click();
        p.fanModeComboForTesting()->setCurrentText(QStringLiteral("Quiet"));
        p.ledSliderForTesting()->setValue(40);
        p.dhcpCheckForTesting()->setChecked(false);
        p.ipEditForTesting()->setText(QStringLiteral("192.168.1.50"));
        p.netmaskEditForTesting()->setText(QStringLiteral("255.255.255.0"));
        p.gatewayEditForTesting()->setText(QStringLiteral("192.168.1.1"));
        p.applyNetworkButtonForTesting()->click();
        p.revertButtonForTesting()->click();
        p.ledSliderForTesting()->setValue(50);
        QVERIFY(p.saveAndRebootButtonForTesting()->isEnabled());
        acceptNextModal();
        p.saveAndRebootButtonForTesting()->click();
    };
    drive(page);
    drive(localPage);
    QVERIFY(amp.waitFor(QStringLiteral("save"), remoteMark) >= 0);
    QVERIFY(localAmp.waitFor(QStringLiteral("save"), localMark) >= 0);
    const QStringList expected{
        QStringLiteral("setup nickname=Shack PGXL"),
        QStringLiteral("setup bias=a"),
        QStringLiteral("setup fan=quiet"),
        QStringLiteral("setup led=40"),
        QStringLiteral("ifconf address=192.168.1.50 netmask=255.255.255.0 "
                       "gateway=192.168.1.1 dhcp=false"),
        QStringLiteral("setup read"),
        QStringLiteral("ifconf read"),
        QStringLiteral("setup led=50"),
        QStringLiteral("save"),
    };
    QCOMPARE(localAmp.settingsCommands(localMark), expected);
    QCOMPARE(amp.settingsCommands(remoteMark), expected);
    // One request per click on the remote page (the local page sends the
    // bias twice, once per radio button).
    QCOMPARE(amp.commands.mid(remoteMark).count(QStringLiteral("setup bias=a")), 1);
    // The remote page asked first, in the local page's words.
    QCOMPARE(asked, (QStringList{
        QStringLiteral("Apply Network Settings|") + PgxlAdvancedPage::networkWarningText(),
        QStringLiteral("Save & Reboot PGXL|") + PgxlSaveRebootDialog::message()}));

    // ---- A no sends nothing.
    yes = false;
    const int declineMark = amp.commands.size();
    page.applyNetworkButtonForTesting()->click();
    page.ledSliderForTesting()->setValue(60);
    QVERIFY(amp.waitFor(QStringLiteral("setup led=60"), declineMark) >= 0);
    page.saveAndRebootButtonForTesting()->click();
    QCOMPARE(asked.size(), 4);
    QTest::qWait(150);
    QCOMPARE(amp.settingsCommands(declineMark), QStringList{QStringLiteral("setup led=60")});
    yes = true;

    // ---- The amp's answers and values show on the page.
    int at = -1;
    const int answerMark = amp.commands.size();
    editName(page.nicknameEditForTesting(), QStringLiteral("Remote Amp"));
    at = amp.waitFor(QStringLiteral("setup nickname=Remote Amp"), answerMark);
    QVERIFY(at >= 0);
    QTRY_COMPARE(page.deviceAnswerForTesting(),
                 QStringLiteral("Sent to the Power Genius. Waiting for its answer."));
    amp.reply(at, QStringLiteral("0|"));
    QTRY_COMPARE(page.deviceAnswerForTesting(),
                 QStringLiteral("The Power Genius took the new name."));
    QVERIFY(OperatorWording::isPlain(page.deviceAnswerForTesting()));
    QCOMPARE(window.accessorySettingsModel()->pgxlNickname(), QStringLiteral("Remote Amp"));
    editName(page.nicknameEditForTesting(), QStringLiteral("Refused"));
    at = amp.waitFor(QStringLiteral("setup nickname=Refused"), answerMark);
    QVERIFY(at >= 0);
    amp.reply(at, QStringLiteral("50000015|"));
    QTRY_COMPARE(page.deviceAnswerForTesting(),
                 QStringLiteral("The Power Genius did not take the new name."));
    page.revertButtonForTesting()->click();
    const int setupAt = amp.waitFor(QStringLiteral("setup read"), answerMark);
    const int ifconfAt = amp.waitFor(QStringLiteral("ifconf read"), answerMark);
    QVERIFY(setupAt >= 0 && ifconfAt >= 0);
    amp.reply(setupAt, QStringLiteral("0|nickname=Amp2 bias=classab fan=continuous led=90"));
    amp.reply(ifconfAt, QStringLiteral("0|dhcp=0 ip=10.0.0.5 netmask=255.0.0.0 gateway=10.0.0.1"));
    QTRY_COMPARE(page.ipEditForTesting()->text(), QStringLiteral("10.0.0.5"));
    QCOMPARE(page.nicknameEditForTesting()->text(), QStringLiteral("Amp2"));
    QVERIFY(!page.biasClassAForTesting()->isChecked());
    QCOMPARE(page.fanModeComboForTesting()->currentText(), QStringLiteral("Continuous"));
    QCOMPARE(page.ledSliderForTesting()->value(), 90);
    QCOMPARE(page.netmaskEditForTesting()->text(), QStringLiteral("255.0.0.0"));
    QCOMPARE(page.gatewayEditForTesting()->text(), QStringLiteral("10.0.0.1"));
    QVERIFY(!page.dhcpCheckForTesting()->isChecked());
    QCOMPARE(page.deviceAnswerForTesting(),
             QStringLiteral("The Power Genius sent its network settings."));

    // ---- A Core refusal reaches the page on its own route, not the slice
    // toast, and changes nothing.
    QSignalSpy sliceToast(&window, &RadioModel::sliceAddRejected);
    QSignalSpy refused(&window, &RadioModel::accessoryRequestRefused);
    const int refuseMark = amp.commands.size();
    QVERIFY(cw.client.requestPgxlNetwork(false, QStringLiteral("999.1.1.1"), QString(),
                                         QString()).sent);
    QTRY_COMPARE(refused.count(), 1);
    QCOMPARE(refused.first().first().toString(), QStringLiteral("pgxl"));
    QCOMPARE(sliceToast.count(), 0);
    QCOMPARE(page.deviceAnswerForTesting(),
             QStringLiteral("Enter each address as four numbers from 0 to 255 separated by dots."));
    QVERIFY(OperatorWording::isPlain(page.deviceAnswerForTesting()));
    QCOMPARE(page.ipEditForTesting()->text(), QStringLiteral("10.0.0.5"));
    QCOMPARE(amp.settingsCommands(refuseMark), QStringList{});

    // ---- Pairing settings are the station's: the window writes them.
    page.pairAttemptCheckForTesting()->setChecked(false);
    QCOMPARE(AppSettings::instance().value(QStringLiteral("PGXL_PairAttempt")).toString(),
             QStringLiteral("False"));

    // Nothing operated the amp; the window opened no connection to it.
    for (const QString& command : amp.commands) {
        QVERIFY2(!command.contains(QStringLiteral("operate")), qPrintable(command));
    }
    QVERIFY(!station.amplifierModel()->operate());
    QCOMPARE(window.pgxlConnection()->socketAttemptToken(), quint64(0));
    local.pgxlConnection()->disconnect();
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-47 / R-R3-22: every control on a remote window's Tuner Genius
// Advanced page works through the Core, as for the Power Genius.
void RemotePeripheralsTest::remoteWindowChangesTheTunersOwnSettingsThroughTheCore()
{
    AppSettings::instance().clear();
    CoreAndWindow cw;
    RadioModel& station = cw.station;
    RadioModel& window = cw.window;
    station.smartSdrListener()->setListenEndpointForTesting(QHostAddress::LocalHost, 0);
    station.setPeripheralValue(QStringLiteral("FourO3A_Enabled"), QStringLiteral("True"));
    FakeGenius tuner;
    QVERIFY(tuner.listen());
    TgxlAdvancedPage page(&window);
    bool yes = true;
    QStringList asked;
    page.setConfirmationForTesting([&](const QString& title, const QString& text) {
        asked.append(title + QLatin1Char('|') + text);
        return yes;
    });
    FakeGenius localTuner;
    QVERIFY(localTuner.listen());
    RadioModel local;
    TgxlAdvancedPage localPage(&local);

    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.tgxlDeviceSettingsAvailable());
    window.reportStationLinkStateChanged();
    QVERIFY(page.nicknameEditForTesting()->isEnabled());
    QVERIFY(!page.applyNetworkButtonForTesting()->isEnabled());

    QVERIFY(admitCoreTuner(station, tuner));
    QTRY_VERIFY(page.applyNetworkButtonForTesting()->isEnabled());
    QTRY_COMPARE(page.firmwareTextForTesting(), QStringLiteral("1.2.17"));
    QCOMPARE(page.variantTextForTesting(), QStringLiteral("3x1"));
    local.tgxlConnection()->connectToTgxl(QStringLiteral("127.0.0.1"), localTuner.port());
    QVERIFY(localTuner.accept());
    localTuner.send(QStringLiteral("V1.2.17"));
    QVERIFY(localTuner.waitFor(QStringLiteral("ifconf read")) >= 0);

    // I5: DHCP off with no address is not sent from either page, and the
    // remote page asks nothing; both say why in the same words.
    {
        const int remoteBefore = tuner.commands.size();
        const int localBefore = localTuner.commands.size();
        const qsizetype askedBefore = asked.size();
        for (TgxlAdvancedPage* p : {&page, &localPage}) {
            p->dhcpCheckForTesting()->setChecked(false);
            p->ipEditForTesting()->clear();
            p->netmaskEditForTesting()->clear();
            p->gatewayEditForTesting()->clear();
            p->applyNetworkButtonForTesting()->click();
            QCOMPARE(p->networkProblemForTesting(),
                     QStringLiteral("Without DHCP, enter an address and a netmask."));
            QVERIFY(OperatorWording::isPlain(p->networkProblemForTesting()));
        }
        QCOMPARE(asked.size(), askedBefore);
        QTest::qWait(100);
        for (int i = remoteBefore; i < tuner.commands.size(); ++i) {
            QVERIFY2(!tuner.commands.at(i).startsWith(QStringLiteral("ifconf address")),
                     qPrintable(tuner.commands.at(i)));
        }
        for (int i = localBefore; i < localTuner.commands.size(); ++i) {
            QVERIFY2(!localTuner.commands.at(i).startsWith(QStringLiteral("ifconf address")),
                     qPrintable(localTuner.commands.at(i)));
        }
    }

    const int remoteMark = tuner.commands.size();
    const int localMark = localTuner.commands.size();
    const auto drive = [](TgxlAdvancedPage& p) {
        editName(p.nicknameEditForTesting(), QStringLiteral("Shack Tuner"));
        p.dhcpCheckForTesting()->setChecked(true);
        p.applyNetworkButtonForTesting()->click();
        QVERIFY(p.saveAndRebootButtonForTesting()->isEnabled());
        acceptNextModal();
        p.saveAndRebootButtonForTesting()->click();
        p.revertButtonForTesting()->click();
    };
    drive(page);
    drive(localPage);
    QVERIFY(tuner.waitFor(QStringLiteral("ifconf read"), remoteMark) >= 0);
    QVERIFY(localTuner.waitFor(QStringLiteral("ifconf read"), localMark) >= 0);
    const QStringList expected{
        QStringLiteral("setup nickname=Shack Tuner"),
        QStringLiteral("ifconf address= netmask= gateway= dhcp=true"),
        QStringLiteral("save"),
        QStringLiteral("setup read"),
        QStringLiteral("ifconf read"),
    };
    QCOMPARE(localTuner.settingsCommands(localMark), expected);
    QCOMPARE(tuner.settingsCommands(remoteMark), expected);
    QCOMPARE(asked, (QStringList{
        QStringLiteral("Apply Network Settings|") + TgxlAdvancedPage::networkWarningText(),
        QStringLiteral("Save & Reboot TGXL|") + PgxlSaveRebootDialog::message()}));

    // A no sends nothing.
    yes = false;
    const int declineMark = tuner.commands.size();
    page.applyNetworkButtonForTesting()->click();
    QTest::qWait(150);
    QCOMPARE(tuner.settingsCommands(declineMark), QStringList{});
    yes = true;

    // The tuner's values and answers.
    const int readAt = tuner.waitFor(QStringLiteral("setup read"), remoteMark);
    const int ifconfAt = tuner.waitFor(QStringLiteral("ifconf read"), remoteMark);
    tuner.reply(readAt, QStringLiteral("0|nickname=Tuner_Genius_XL"));
    tuner.reply(ifconfAt, QStringLiteral("0|dhcp=1 ip=192.168.1.60 netmask=255.255.255.0 "
                                         "gateway=192.168.1.1"));
    QTRY_COMPARE(page.ipEditForTesting()->text(), QStringLiteral("192.168.1.60"));
    QCOMPARE(page.nicknameEditForTesting()->text(), QStringLiteral("Tuner_Genius_XL"));
    QVERIFY(page.dhcpCheckForTesting()->isChecked());
    QVERIFY(!page.ipEditForTesting()->isEnabled());   // DHCP gates the fields
    const int saveAt = tuner.waitFor(QStringLiteral("save"), remoteMark);
    tuner.reply(saveAt, QStringLiteral("0|saving"));
    QTRY_COMPARE(page.deviceAnswerForTesting(),
                 QStringLiteral("The Tuner Genius is saving its settings and restarting."));
    QVERIFY(OperatorWording::isPlain(page.deviceAnswerForTesting()));

    // A Core refusal on its own route.
    QSignalSpy refused(&window, &RadioModel::accessoryRequestRefused);
    QVERIFY(cw.client.requestTgxlName(QStringLiteral("Bad\tname")).sent);
    QTRY_COMPARE(refused.count(), 1);
    QCOMPARE(refused.first().first().toString(), QStringLiteral("tgxl"));
    QCOMPARE(page.deviceAnswerForTesting(),
             QStringLiteral("Enter a name without line breaks or tabs."));

    // The Core loses the tuner: Apply and Revert wait for it again.
    QString reason;
    QVERIFY(station.disconnectTgxlForStation(&reason));
    QTRY_VERIFY(!page.applyNetworkButtonForTesting()->isEnabled());
    QVERIFY(!page.revertButtonForTesting()->isEnabled());

    for (const QString& command : tuner.commands) {
        QVERIFY2(!command.contains(QStringLiteral("operate"))
                     && !command.contains(QStringLiteral("bypass")), qPrintable(command));
    }
    QCOMPARE(window.tgxlConnection()->socketAttemptToken(), quint64(0));
    local.tgxlConnection()->disconnect();
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// R-R3-47 / R-R3-22: a Core that does not offer the devices' own settings
// (an older Core) leaves those controls unchangeable, saying why in plain
// words; the station settings (pairing) still reach it.
void RemotePeripheralsTest::olderCoreLeavesTheDeviceSettingsSayingWhy()
{
    RadioModel model(RadioModel::Role::Remote);
    RecordingTgxlLink link;
    link.linkReady = true;
    model.attachStation(&link);
    PgxlAdvancedPage pgxl(&model);
    QVERIFY(!pgxl.nicknameEditForTesting()->isEnabled());
    QVERIFY(!pgxl.biasClassAForTesting()->isEnabled());
    QVERIFY(!pgxl.fanModeComboForTesting()->isEnabled());
    QVERIFY(!pgxl.ledSliderForTesting()->isEnabled());
    QVERIFY(!pgxl.dhcpCheckForTesting()->isEnabled());
    QVERIFY(!pgxl.ipEditForTesting()->isEnabled());
    QVERIFY(!pgxl.applyNetworkButtonForTesting()->isEnabled());
    QVERIFY(!pgxl.revertButtonForTesting()->isEnabled());
    QVERIFY(!pgxl.saveAndRebootButtonForTesting()->isEnabled());
    QVERIFY(pgxl.pairAttemptCheckForTesting()->isEnabled());
    QCOMPARE(pgxl.deviceAnswerForTesting(), IStationLink::pgxlDeviceSettingsUnavailableReason());
    QVERIFY(OperatorWording::isPlain(pgxl.deviceAnswerForTesting()));
    TgxlAdvancedPage tgxl(&model);
    QVERIFY(!tgxl.nicknameEditForTesting()->isEnabled());
    QVERIFY(!tgxl.dhcpCheckForTesting()->isEnabled());
    QVERIFY(!tgxl.applyNetworkButtonForTesting()->isEnabled());
    QVERIFY(!tgxl.revertButtonForTesting()->isEnabled());
    QCOMPARE(tgxl.deviceAnswerForTesting(), IStationLink::tgxlDeviceSettingsUnavailableReason());
    QVERIFY(OperatorWording::isPlain(tgxl.deviceAnswerForTesting()));
    // A request is not sent: the link says why.
    const IStationLink::CommandOutcome outcome = link.requestPgxlSaveAndRestart();
    QVERIFY(!outcome.sent);
    QVERIFY(OperatorWording::isPlain(outcome.reason));
}

// L1 (R-R3-47, R-R3-22, R-R3-48): the Core's refusals of the accessory
// requests reach the window as accessory refusals, keyed by what they were
// about, and never as a slice refusal.
void RemotePeripheralsTest::accessoryRefusalsNeverReachTheSliceToast()
{
    AppSettings::instance().clear();
    CoreAndWindow cw;
    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.accessoryDataAvailable());
    QTRY_VERIFY(cw.client.pgxlDeviceSettingsAvailable());
    QSignalSpy sliceToast(&cw.window, &RadioModel::sliceAddRejected);
    QSignalSpy refused(&cw.window, &RadioModel::accessoryRequestRefused);

    struct Case { const char* device; std::function<IStationLink::CommandOutcome()> send; };
    StationClient& c = cw.client;
    const QList<Case> cases{
        {"pgxl", [&] { return c.requestPgxlPowerCap(true, 5); }},
        {"pgxl", [&] { return c.requestPgxlName(QStringLiteral("Amp")); }},   // no amp on the Core
        {"tgxl", [&] { return c.requestTgxlSaveAndRestart(); }},                 // no tuner
        {"interlock", [&] { return c.requestTxInterlockPolicy(9, 0, false, 2.0); }},
        {"faults", [&] { return c.requestClearAccessoryFaults(QStringLiteral("amp")); }},
    };
    for (const Case& one : cases) {
        refused.clear();
        QVERIFY(one.send().sent);
        QTRY_COMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(0).toString(), QString::fromLatin1(one.device));
        QVERIFY(OperatorWording::isPlain(
            OperatorReasonText::forDisplay(refused.first().at(1).toString())));
    }
    QTest::qWait(50);
    QCOMPARE(sliceToast.count(), 0);
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// I4 (R-R3-47): every control on a remote window's RF-Kit page works
// through the Core. Auto-reconnect, poll interval and the antenna names
// are the station's settings (the Core applies them at once); Reset amp
// error reaches the Core's amp as the request the local page's button
// sends to its own amp (the fakes record both).
void RemotePeripheralsTest::remoteRfKitPageWorksEveryControl()
{
    AppSettings::instance().clear();
    AppSettings::instance().setValue(QStringLiteral("RfKit_PollIntervalMs"),
                                     QStringLiteral("5000"));
    CoreAndWindow cw;
    RadioModel& station = cw.station;
    RadioModel& window = cw.window;
    FakeRfKit amp;
    RfKitPage page(&window);
    LoopbackTransport* stationEnd = cw.connect(this);
    QTRY_VERIFY(cw.client.rfKitSettingsAvailable());
    window.reportStationLinkStateChanged();
    QString reason;
    QVERIFY(station.setRfKitEnabledForStation(true, &reason));
    QTRY_VERIFY(page.detailTabIsEnabledForTesting());
    for (QWidget* w : std::initializer_list<QWidget*>{
             page.autoReconnectForTesting(), page.pollIntervalForTesting(),
             page.saveButtonForTesting(), page.resetErrorButtonForTesting(),
             page.antennaLabelEditForTesting(1), page.antennaLabelEditForTesting(4)}) {
        QVERIFY(w->isEnabled());
    }
    QCOMPARE(page.pollIntervalForTesting()->value(), 5000);

    // Reset amp error with no amp at the Core: the Core's words, nothing sent.
    QSignalSpy sliceToast(&window, &RadioModel::sliceAddRejected);
    page.resetErrorButtonForTesting()->click();
    QTRY_VERIFY(page.liveStatusTextForTesting().contains(
        QStringLiteral("The Core is not connected to the RF-Kit amplifier.")));
    QVERIFY(OperatorWording::isPlain(page.liveStatusTextForTesting()));
    QCOMPARE(sliceToast.count(), 0);

    // The Core admits its amp; the window's Reset reaches it.
    QVERIFY(station.configureRfKitForStation(QStringLiteral("127.0.0.1"), amp.serverPort(),
                                             &reason));
    QTRY_COMPARE_WITH_TIMEOUT(window.rfKitModel()->connectionPhase(),
                              RfKitModel::ConnectionPhase::Connected, 5000);
    const int remoteMark = amp.lines.size();
    page.resetErrorButtonForTesting()->click();
    QTRY_VERIFY(amp.lines.mid(remoteMark).contains(QStringLiteral("POST /error/reset")));

    // A local window's page on its own amp: the same request.
    FakeRfKit localAmp;
    RadioModel local;
    RadioInfo radio;
    radio.macAddress = QStringLiteral("aa:bb:cc:dd:ee:48");
    local.setLastRadioInfoForTest(radio);
    local.setConnectionStateForTest(ConnectionState::Connected);
    local.setRfKitEnabled(true);
    RfKitPage localPage(&local);
    QVERIFY(localPage.detailTabIsEnabledForTesting());
    local.rfKitConnection()->connectToAmp(QStringLiteral("127.0.0.1"), localAmp.serverPort());
    QTRY_VERIFY_WITH_TIMEOUT(local.rfKitConnection()->isConnected(), 5000);
    const int localMark = localAmp.lines.size();
    localPage.resetErrorButtonForTesting()->click();
    QTRY_VERIFY(localAmp.lines.mid(localMark).contains(QStringLiteral("POST /error/reset")));
    local.rfKitConnection()->disconnect();

    // Save: the station's settings, then applied by the Core at once.
    page.autoReconnectForTesting()->setChecked(false);
    page.pollIntervalForTesting()->setValue(2500);
    page.antennaLabelEditForTesting(1)->setText(QStringLiteral("Beam"));
    page.antennaLabelEditForTesting(4)->setText(QStringLiteral("Loop"));
    page.saveButtonForTesting()->click();
    auto& s = AppSettings::instance();
    QCOMPARE(s.value(QStringLiteral("RfKit_AutoReconnect")).toString(), QStringLiteral("False"));
    QCOMPARE(s.value(QStringLiteral("RfKit_PollIntervalMs")).toString(), QStringLiteral("2500"));
    QCOMPARE(s.value(QStringLiteral("RfKit_Ant1_Label")).toString(), QStringLiteral("Beam"));
    QCOMPARE(s.value(QStringLiteral("RfKit_Ant4_Label")).toString(), QStringLiteral("Loop"));
    // The station settings write reaches the Core's live objects (as
    // StationServer does after storing it).
    for (const char* key : {"RfKit_AutoReconnect", "RfKit_PollIntervalMs", "RfKit_Ant1_Label",
                            "RfKit_Ant4_Label"}) {
        station.applyRemoteAccessorySetting(QString::fromLatin1(key));
    }
    QVERIFY(!station.rfKitConnection()->autoReconnect());
    QCOMPARE(station.rfKitConnection()->pollIntervalMs(), 2500);
    QTRY_COMPARE(window.accessoryDataModel()->rfkitAntennaLabels().value(0),
                 QStringLiteral("Beam"));
    QCOMPARE(window.accessoryDataModel()->rfkitAntennaLabels().value(3), QStringLiteral("Loop"));

    // Nothing but reads and the reset reached the Core's amp; the window
    // opened no connection of its own.
    for (const QString& line : amp.lines) {
        QVERIFY2(line.startsWith(QStringLiteral("GET "))
                     || line == QStringLiteral("POST /error/reset"), qPrintable(line));
    }
    QVERIFY(!window.rfKitConnection()->isConnected());
    QVERIFY(window.rfKitConnection()->peerAddress().isEmpty());
    stationEnd->closeLink(QStringLiteral("test done"));
    AppSettings::instance().clear();
}

// I4: a Core without remoteRfKitControlVersion 3 leaves the page's settings,
// names and Reset amp error unchangeable, and says why in plain words.
void RemotePeripheralsTest::olderCoreLeavesTheRfKitSettingsSayingWhy()
{
    AppSettings::instance().clear();
    struct OlderCore final : IStationLink {
        CommandOutcome requestAddSlice(const QString&) override { return {}; }
        CommandOutcome requestAddSliceOnPan(const QString&) override { return {}; }
        CommandOutcome requestRemoveSlice(int) override { return {}; }
        CommandOutcome requestActiveSlice(int) override { return {}; }
        CommandOutcome requestSliceSampleRate(int, int) override { return {}; }
        bool stationLinkReady() const override { return true; }
        bool remoteRfKitControlAvailable() const override { return true; }
    } link;
    RadioModel model(RadioModel::Role::Remote);
    model.attachStation(&link);
    RfKitPage page(&model);
    model.reportStationLinkStateChanged();
    for (QWidget* w : std::initializer_list<QWidget*>{
             page.autoReconnectForTesting(), page.pollIntervalForTesting(),
             page.saveButtonForTesting(), page.resetErrorButtonForTesting(),
             page.antennaLabelEditForTesting(2)}) {
        QVERIFY(!w->isEnabled());
        QVERIFY(OperatorWording::isPlain(w->toolTip()));
    }
    const IStationLink::CommandOutcome outcome = link.requestResetRfKitError();
    QVERIFY(!outcome.sent);
    QVERIFY(OperatorWording::isPlain(outcome.reason));
    AppSettings::instance().clear();
}

QTEST_MAIN(RemotePeripheralsTest)
#include "tst_remote_peripherals.moc"
