// no-port-check: NereusSDR-original. R-R3-48 / R-R3-25 the Core's station
// TCI server and the app's one TCI switch.
//
// The Core's server: where it listens (the station network facing the
// radio, nereusd.conf's override, and this computer), the switch and port
// it keeps in its own settings, and what a TCI app on the station network
// (the stand-in for the RF-Kit amplifier) receives from the Core's radio:
// the init burst saying receive-only, split_enable, and vfo: as the Core's
// slice moves. Band follow for the RF-Kit over that server. The window
// side: the one switch starts this window's server and asks the Core for
// the same, runs none of its own when the Core is on this computer, and
// the TCI page's "Also at the station" line.
//
// Loopback sockets only; synthetic interface entries stand in for the
// station network. No radio, amplifier or real Core is contacted.
// J.J. Boyd (KG4VCF), September 2026; AI-assisted via Anthropic Claude Code.
#include <QtTest/QtTest>
#include <QNetworkInterface>
#include <QTcpServer>
#include <QWebSocket>

#include "OperatorWording.h"
#include "core/AppSettings.h"
#include "core/RfKitBandFollow.h"
#include "core/StationNetwork.h"
#include "core/StationTciController.h"
#include "core/TciServer.h"
#include "core/TciSwitch.h"
#include "core/session/IStationLink.h"
#include "models/RadioModel.h"
#include "models/RfKitModel.h"
#include "models/SliceModel.h"
#include "models/StationTciModel.h"

using namespace NereusSDR;
using BandFollow = TunerModel::BandFollow;

namespace {

quint16 freePort()
{
    QTcpServer reservation;
    if (!reservation.listen(QHostAddress::LocalHost, 0)) { return 0; }
    const quint16 port = reservation.serverPort();
    reservation.close();
    return port;
}

QNetworkAddressEntry entry(const char* ip, int prefix)
{
    QNetworkAddressEntry e;
    e.setIp(QHostAddress(QString::fromLatin1(ip)));
    e.setPrefixLength(prefix);
    return e;
}

// An app on the station network: every text frame the server sends it.
struct TciApp {
    QWebSocket socket;
    QStringList frames;
    explicit TciApp(quint16 port)
    {
        QObject::connect(&socket, &QWebSocket::textMessageReceived, &socket,
                         [this](const QString& text) {
            for (const QString& part : text.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
                frames.append(part.trimmed() + QLatin1Char(';'));
            }
        });
        socket.open(QUrl(QStringLiteral("ws://127.0.0.1:%1").arg(port)));
    }
    bool has(const QString& frame) const { return frames.contains(frame); }
};

class FakeStationLink final : public IStationLink {
public:
    bool tciAvailable{true};
    bool coreHere{false};
    bool ready{true};
    int stored{1};   // the Core has a stored station switch (-1: not known yet)
    int requests{0};
    bool requestedOn{false};
    quint16 requestedPort{0};
    CommandOutcome requestAddSlice(const QString&) override { return {}; }
    CommandOutcome requestAddSliceOnPan(const QString&) override { return {}; }
    CommandOutcome requestRemoveSlice(int) override { return {}; }
    CommandOutcome requestActiveSlice(int) override { return {}; }
    CommandOutcome requestSliceSampleRate(int, int) override { return {}; }
    bool stationLinkReady() const override { return ready; }
    bool stationTciAvailable() const override { return ready && tciAvailable; }
    bool coreServesTciOnThisComputer() const override { return coreHere; }
    int coreStationTciStored() const override { return stored; }
    CommandOutcome requestStationTci(bool on, quint16 port) override
    {
        ++requests;
        requestedOn = on;
        requestedPort = port;
        return {true, {}, quint32(100 + requests)};   // command ids 101, 102, ...
    }
};

} // namespace

class StationTciServerTest : public QObject {
    Q_OBJECT

private slots:
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // The station network is this computer's address on the radio's subnet.
    void stationAddressFacesTheRadio()
    {
        const QList<QNetworkAddressEntry> entries{
            entry("10.8.0.4", 24), entry("192.168.1.20", 24), entry("fe80::1", 64)};
        QCOMPARE(StationNetwork::addressFacing(QHostAddress(QStringLiteral("192.168.1.50")), entries),
                 QHostAddress(QStringLiteral("192.168.1.20")));
        QCOMPARE(StationNetwork::addressFacing(QHostAddress(QStringLiteral("10.8.0.99")), entries),
                 QHostAddress(QStringLiteral("10.8.0.4")));
        QVERIFY(StationNetwork::addressFacing(QHostAddress(QStringLiteral("172.16.0.9")), entries)
                    .isNull());
        QCOMPARE(StationNetwork::addressFacing(QHostAddress(QStringLiteral("::ffff:192.168.1.7")),
                                               entries),
                 QHostAddress(QStringLiteral("192.168.1.20")));
    }

    // Where the Core listens: the station network plus this computer; the
    // nereusd.conf override replaces the station choice; every address
    // covers this computer on its own.
    void listensOnTheStationNetworkAndThisComputer()
    {
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        const QHostAddress loopback(QHostAddress::LocalHost);
        controller.setInterfaceEntriesForTest({entry("192.168.1.20", 24)});
        QCOMPARE(controller.wantedAddresses(), QList<QHostAddress>{loopback});   // no radio yet
        controller.setRadioAddress(QHostAddress(QStringLiteral("192.168.1.50")));
        QCOMPARE(controller.wantedAddresses(),
                 (QList<QHostAddress>{QHostAddress(QStringLiteral("192.168.1.20")), loopback}));
        controller.setRadioAddress(QHostAddress(QStringLiteral("172.16.0.9")));
        QCOMPARE(controller.wantedAddresses(), QList<QHostAddress>{loopback});
        controller.setBindOverride(QStringLiteral("10.0.0.7"));
        QCOMPARE(controller.wantedAddresses(),
                 (QList<QHostAddress>{QHostAddress(QStringLiteral("10.0.0.7")), loopback}));
        controller.setBindOverride(QStringLiteral("0.0.0.0"));
        QCOMPARE(controller.wantedAddresses(),
                 QList<QHostAddress>{QHostAddress(QHostAddress::AnyIPv4)});
        controller.setBindOverride(QStringLiteral("127.0.0.1"));
        QCOMPARE(controller.wantedAddresses(), QList<QHostAddress>{loopback});
    }

    // The station's switch and port are saved on the Core and applied; a
    // restarted Core comes back as it was; off stops the server.
    void switchIsKeptOnTheCore()
    {
        const quint16 port = freePort();
        QVERIFY(port >= 1024);
        RadioModel model;
        StationTciModel state;
        {
            StationTciController controller(&model, &state);
            controller.setBindOverride(QStringLiteral("127.0.0.1"));
            controller.applySaved();
            QVERIFY(!state.enabled());
            QVERIFY(!state.listening());

            QString reason;
            QVERIFY(!controller.setEnabled(true, 80, &reason));
            QCOMPARE(reason, QStringLiteral("Choose a TCI port from 1024 to 65535."));
            QVERIFY(OperatorWording::isPlain(reason));
            QVERIFY(!state.enabled());

            QVERIFY(controller.setEnabled(true, port, &reason));
            QVERIFY(reason.isEmpty());
            QVERIFY(state.enabled());
            QVERIFY(state.listening());
            QCOMPARE(state.port(), int(port));
            QVERIFY(state.stationAddress().isEmpty());   // this computer only
            QCOMPARE(AppSettings::instance().value(StationTciController::enabledKey()).toString(),
                     QStringLiteral("True"));
            QCOMPARE(AppSettings::instance().value(StationTciController::portKey()).toString(),
                     QString::number(port));
            QVERIFY(controller.server()->stationReceiveOnly());
        }
        {
            StationTciController restarted(&model, &state);
            restarted.setBindOverride(QStringLiteral("127.0.0.1"));
            restarted.applySaved();
            QVERIFY(state.enabled());
            QVERIFY(state.listening());
            QCOMPARE(restarted.server()->port(), port);

            QString reason;
            QVERIFY(restarted.setEnabled(false, port, &reason));
            QVERIFY(!state.enabled());
            QVERIFY(!state.listening());
            QVERIFY(!restarted.server()->isRunning());
        }
    }

    // A TCI app at the station (the RF-Kit amplifier's stand-in) hears the
    // Core's radio: receive-only, split_enable and vfo as the slice moves.
    // Transmit over the Core's TCI is refused with a plain reason.
    void ampStandInFollowsTheCoresSlice()
    {
        const quint16 port = freePort();
        RadioModel station;
        const int sliceId = station.addSlice(QStringLiteral("pan-0"));
        SliceModel* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        slice->setFrequency(7074000.0);
        station.enableStationTci(QStringLiteral("127.0.0.1"));
        QVERIFY(station.stationTciController());
        QString reason;
        QVERIFY(station.setStationTciForStation(true, port, &reason));
        QVERIFY(station.stationTciModel()->listening());

        TciApp amp(port);
        QTRY_VERIFY_WITH_TIMEOUT(amp.has(QStringLiteral("ready;")), 3000);
        QVERIFY(amp.has(QStringLiteral("receive_only:true;")));
        QVERIFY(amp.has(QStringLiteral("split_enable:0,false;")));
        QVERIFY(amp.has(QStringLiteral("tx_enable:0,false;")));
        QVERIFY(amp.has(QStringLiteral("vfo:0,0,7074000;")));

        slice->setFrequency(14074000.0);
        QTRY_VERIFY_WITH_TIMEOUT(amp.has(QStringLiteral("vfo:0,0,14074000;")), 3000);
        slice->setFrequency(21074000.0);
        QTRY_VERIFY_WITH_TIMEOUT(amp.has(QStringLiteral("vfo:0,0,21074000;")), 3000);
        // Another app's split set reaches the amp as split_enable too.
        amp.socket.sendTextMessage(QStringLiteral("split_enable:0,false;"));
        QTRY_VERIFY_WITH_TIMEOUT(amp.frames.count(QStringLiteral("split_enable:0,false;")) >= 2,
                                 3000);

        // Transmit waits for remote transmit: no MOX, the app hears
        // trx:0,false, and the reason is plain and off the wire.
        amp.frames.clear();
        amp.socket.sendTextMessage(QStringLiteral("trx:0,true;"));
        QTRY_VERIFY_WITH_TIMEOUT(amp.has(QStringLiteral("trx:0,false;")), 3000);
        QVERIFY(!station.mox());
        TciServer* server = station.stationTciController()->server();
        QCOMPARE(server->operatorNoticeReason(),
                 QString::fromLatin1(TciServer::kStationTransmitRefusedReason));
        QVERIFY(OperatorWording::isPlain(server->operatorNoticeReason()));
        for (const QString& frame : amp.frames) {
            QVERIFY2(!frame.contains(QStringLiteral("remote transmit")), qPrintable(frame));
        }
        QCOMPARE(server->activeTxClientCount(), 0);
        amp.socket.close();
    }

    // R-R3-48: band follow over the Core's server. The amp's address
    // connected as an app: following. Before that: the address to enter
    // on the amp, or "this computer only". Switched off: off.
    void rfKitBandFollowOverTheCoresServer()
    {
        const quint16 port = freePort();
        RadioModel station;
        station.enableStationTci(QStringLiteral("127.0.0.1"));
        RfKitModel* rfKit = station.rfKitModel();
        QCOMPARE(rfKit->bandFollow(), BandFollow::Off);
        QVERIFY(OperatorWording::isPlain(rfKit->bandFollowText()));

        RfKitModel::StationConnectionState amp;
        amp.configuredHost = QStringLiteral("127.0.0.1");
        amp.configuredPort = 8080;
        rfKit->setStationConnectionState(amp);
        QString reason;
        QVERIFY(station.setStationTciForStation(true, port, &reason));
        QTRY_COMPARE(rfKit->bandFollow(), BandFollow::ThisComputerOnly);
        QVERIFY(OperatorWording::isPlain(rfKit->bandFollowText()));

        {
            TciApp app(port);
            QTRY_COMPARE_WITH_TIMEOUT(rfKit->bandFollow(), BandFollow::Following, 3000);
            QCOMPARE(rfKit->bandFollowText(), QStringLiteral("Band follow: following the radio"));
            QCOMPARE(rfKit->bandFollowPort(), int(port));
            app.socket.close();
            QTRY_COMPARE_WITH_TIMEOUT(rfKit->bandFollow(), BandFollow::ThisComputerOnly, 3000);
        }

        QVERIFY(station.setStationTciForStation(false, port, &reason));
        QTRY_COMPARE(rfKit->bandFollow(), BandFollow::Off);
    }

    // The address to enter on the amp, given where a server listens.
    void addressToEnterOnTheAmp()
    {
        const QHostAddress amp(QStringLiteral("192.168.1.60"));
        const QList<QNetworkAddressEntry> entries{entry("10.8.0.4", 24), entry("192.168.1.20", 24)};
        const QHostAddress loopback(QHostAddress::LocalHost);
        QCOMPARE(RfKitBandFollow::addressForAmp({QHostAddress(QStringLiteral("192.168.1.20")),
                                                 loopback}, amp, entries),
                 QHostAddress(QStringLiteral("192.168.1.20")));
        QCOMPARE(RfKitBandFollow::addressForAmp({QHostAddress(QHostAddress::AnyIPv4)}, amp, entries),
                 QHostAddress(QStringLiteral("192.168.1.20")));
        QVERIFY(RfKitBandFollow::addressForAmp({loopback}, amp, entries).isNull());

        RfKitModel model;
        model.setBandFollow(BandFollow::Waiting, QStringLiteral("192.168.1.20"), 50001);
        QCOMPARE(model.bandFollowText(),
                 QStringLiteral("Band follow: enter 192.168.1.20, port 50001 as the TCI server on "
                                "the amplifier."));
        QVERIFY(OperatorWording::isPlain(model.bandFollowText()));
    }

    // The window's one switch: its own server and the Core's; none of its
    // own when the Core is on this computer; off stops both; a port change
    // goes to the Core; an older Core leaves this window's server only.
    void oneSwitchDrivesBothServers()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        const QHostAddress loopback(QHostAddress::LocalHost);

        tci.setSwitch(true, port, loopback);
        QVERIFY(local.isRunning());
        QCOMPARE(local.port(), port);
        QCOMPARE(link.requests, 1);
        QVERIFY(link.requestedOn);
        QCOMPARE(link.requestedPort, port);

        const quint16 other = freePort();
        tci.setPortOrBind(other, loopback);
        QVERIFY(local.isRunning());
        QCOMPARE(local.port(), other);
        QCOMPARE(link.requests, 2);
        QCOMPARE(link.requestedPort, other);

        // The Core turns out to be on this computer and serves TCI on this
        // port: one server, the Core's.
        link.coreHere = true;
        StationTciModel::State serving;
        serving.enabled = true;
        serving.listening = true;
        serving.port = other;
        window.stationTciModel()->setState(serving);
        window.reportStationLinkStateChanged();
        QVERIFY(!local.isRunning());
        QVERIFY(tci.coreServesThisComputer());

        tci.setSwitch(false, other, loopback);
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 3);
        QVERIFY(!link.requestedOn);

        tci.setSwitch(true, other, loopback);
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 4);
        QVERIFY(link.requestedOn);

        // Startup applies the switch here without telling the Core.
        link.coreHere = false;
        link.tciAvailable = false;   // an older Core
        tci.setSwitch(true, port, loopback, /*tellCore=*/false);
        QVERIFY(local.isRunning());
        QCOMPARE(link.requests, 4);
        tci.setSwitch(false, port, loopback);
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 4);
    }

    // Rework part 1 (R-R3-48, operator decision 2026-09-23: one TCI switch
    // and one port). A window connected to a Core on another computer shows
    // the Core's station switch and port: another window (or the phone)
    // changing them changes this window's switch, and this window's own
    // server follows it. The whole state is taken at once (the object's
    // properties arrive one at a time), so a first property never makes the
    // window restart on a stale port.
    void windowFollowsTheCoresSwitchOnAnotherComputer()
    {
        const quint16 port = freePort();
        const quint16 other = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        QSignalSpy starts(&local, &TciServer::serverStarted);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
        QVERIFY(local.isRunning());
        QCOMPARE(link.requests, 1);

        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", true);
        station->applyStationValue("port", int(port));
        station->applyStationValue("listening", true);
        QCoreApplication::processEvents();
        QVERIFY(local.isRunning());
        QCOMPARE(starts.count(), 1);

        // Another window moves the Core to another port: this window's
        // switch and server follow, once.
        station->applyStationValue("port", int(other));
        QCoreApplication::processEvents();
        QTRY_COMPARE(local.port(), other);
        QCOMPARE(tci.port(), other);
        QCOMPARE(AppSettings::instance().value(QStringLiteral("TciServerPort")).toInt(), int(other));
        QCOMPARE(link.requests, 1);   // following never asks the Core

        // And turns the Core's switch off: this window's switch goes off.
        station->applyStationValue("enabled", false);
        station->applyStationValue("listening", false);
        QTRY_VERIFY(!local.isRunning());
        QVERIFY(!tci.switchOn());
        QCOMPARE(AppSettings::instance().value(QStringLiteral("TciServerEnabled")).toString(),
                 QStringLiteral("False"));
        QCOMPARE(link.requests, 1);
    }

    // Rework part 1: on the Core's own computer the window runs no server
    // while connected (the Core's loopback listener serves apps here); its
    // switch still sets the Core's.
    void coreHereWindowRunsNoServer()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 1);
        QVERIFY(link.requestedOn);
        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", true);
        station->applyStationValue("port", int(port));
        QCoreApplication::processEvents();
        QVERIFY(!local.isRunning());   // not listening yet: still none here
        tci.setSwitch(false, port, QHostAddress(QHostAddress::LocalHost));
        QCOMPARE(link.requests, 2);
        QVERIFY(!link.requestedOn);
        QVERIFY(!local.isRunning());
    }

    // Rework part 2 (R-R3-48): at connect the Core's stored switch wins;
    // this window's switch follows it and asks the Core for nothing.
    void coresStoredSwitchWinsAtConnect()
    {
        const quint16 port = freePort();
        const quint16 corePort = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.ready = false;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost), /*tellCore=*/false);
        QVERIFY(local.isRunning());

        link.ready = true;
        window.reportStationLinkStateChanged();
        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", false);
        station->applyStationValue("port", int(corePort));
        QTRY_VERIFY(!tci.switchOn());
        QCOMPARE(tci.port(), corePort);
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 0);
    }

    // Rework part 2: a Core with no stored station switch yet (the upgrade:
    // nereusd and this window on one computer with TCI on before) takes
    // this window's switch and port at connect. Its first state (its
    // defaults) does not turn this window's switch off meanwhile. A Core
    // whose settings have not arrived yet is not decided until they do.
    void windowSeedsACoreWithNoStoredSwitch()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.ready = false;
        link.stored = -1;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost), /*tellCore=*/false);

        link.ready = true;
        window.reportStationLinkStateChanged();
        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", false);
        station->applyStationValue("port", int(StationTciController::kDefaultPort));
        QCoreApplication::processEvents();
        QCOMPARE(link.requests, 0);        // not known yet: nothing decided
        QVERIFY(tci.switchOn());

        link.stored = 0;                   // the Core's settings arrive
        window.reportStationSettingChanged(QString());
        QTRY_COMPARE(link.requests, 1);
        QVERIFY(link.requestedOn);
        QCOMPARE(link.requestedPort, port);
        QCoreApplication::processEvents();
        QVERIFY(tci.switchOn());           // the Core's defaults did not win
        station->applyStationValue("enabled", true);
        station->applyStationValue("port", int(port));
        station->applyStationValue("listening", true);
        QCoreApplication::processEvents();
        QVERIFY(tci.switchOn());
        QCOMPARE(tci.port(), port);
        QCOMPARE(link.requests, 1);
    }

    // Rework follow-up 1 (R-R3-48): a window that asked the Core for a
    // switch and port follows the Core again once that request is over,
    // whatever became of it: the link dropped before the Core's echo, the
    // Core refused it, or another window's change landed in the same turn
    // so the echo never matched.
    void windowFollowsAgainAfterItsRequestEnds()
    {
        const quint16 port = freePort();
        const quint16 other = freePort();
        const auto coreSays = [](RadioModel& window, bool on, quint16 p) {
            StationTciModel::State state;
            state.enabled = on;
            state.port = p;
            state.listening = on;
            window.stationTciModel()->setState(state);
            QCoreApplication::processEvents();
        };
        // The link drops before the echo.
        {
            RadioModel window(RadioModel::Role::Remote);
            FakeStationLink link;
            window.attachStation(&link);
            TciServer local(&window);
            TciSwitch tci(&local, &window);
            coreSays(window, false, port);
            tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
            link.ready = false;
            window.reportStationLinkStateChanged();
            link.ready = true;
            window.reportStationLinkStateChanged();
            coreSays(window, false, other);
            QVERIFY(!tci.switchOn());
            QCOMPARE(tci.port(), other);
        }
        // The Core refuses the request (a hand-edited port below 1024).
        {
            RadioModel window(RadioModel::Role::Remote);
            FakeStationLink link;
            window.attachStation(&link);
            TciServer local(&window);
            TciSwitch tci(&local, &window);
            coreSays(window, false, port);
            tci.setSwitch(true, 900, QHostAddress(QHostAddress::LocalHost));
            window.reportStationAccessoryRefusal(QStringLiteral("tci"),
                QStringLiteral("Choose a TCI port from 1024 to 65535."), 101);
            QCoreApplication::processEvents();
            QVERIFY(!tci.switchOn());   // the Core's switch again
            QCOMPARE(tci.port(), port);
            coreSays(window, true, other);
            QVERIFY(tci.switchOn());
            QCOMPARE(tci.port(), other);
            local.stop();
        }
        // Another window's change lands in the same turn: the echo never
        // matches; the request's acceptance ends the wait.
        {
            RadioModel window(RadioModel::Role::Remote);
            FakeStationLink link;
            window.attachStation(&link);
            TciServer local(&window);
            TciSwitch tci(&local, &window);
            coreSays(window, false, port);
            tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
            coreSays(window, true, other);
            QCOMPARE(tci.port(), port);   // still waiting for its own echo
            window.forgetAccessoryRequest(101);   // the Core accepted it
            QCoreApplication::processEvents();
            QVERIFY(tci.switchOn());
            QCOMPARE(tci.port(), other);
            local.stop();
        }
    }

    // Rework part 4 (R-R3-48): with the link to the Core down the switch
    // shows the last known state. On the Core's computer the window starts
    // no server (there is no radio here to serve); on another computer its
    // server follows the switch as before. The TCI page's line says
    // nothing about a Core it cannot reach.
    void linkDownKeepsTheLastSwitch()
    {
        const quint16 port = freePort();
        for (const bool coreHere : {true, false}) {
            RadioModel window(RadioModel::Role::Remote);
            FakeStationLink link;
            link.coreHere = coreHere;
            window.attachStation(&link);
            TciServer local(&window);
            TciSwitch tci(&local, &window);
            StationTciModel::State serving;
            serving.enabled = true;
            serving.listening = true;
            serving.port = port;
            window.stationTciModel()->setState(serving);
            tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost), /*tellCore=*/false);
            QCoreApplication::processEvents();
            QCOMPARE(local.isRunning(), !coreHere);
            QVERIFY(!TciSwitch::stationLine(&window).isEmpty());

            link.ready = false;
            window.reportStationLinkStateChanged();
            QCoreApplication::processEvents();
            QVERIFY(tci.switchOn());
            QCOMPARE(tci.port(), port);
            QCOMPARE(local.isRunning(), !coreHere);
            QVERIFY(TciSwitch::stationLine(&window).isEmpty());
            local.stop();
        }
    }

    // Follow-up 1b: the Core retries a station listener that could not
    // start (another program had its port), with a plain reason while it
    // cannot listen, and listens once the port is free.
    void coreRetriesAFailedStationListener()
    {
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        const quint16 port = blocker.serverPort();
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        controller.setBindOverride(QStringLiteral("127.0.0.1"));
        // Rework follow-up 2: the first failure is logged once, and the
        // retries not at all (warnings counted while it fails).
        static int s_listenWarnings = 0;
        s_listenWarnings = 0;
        static QtMessageHandler s_previous = nullptr;
        s_previous = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& context, const QString& text) {
                if (type == QtWarningMsg && text.contains(QStringLiteral("listen"))) {
                    ++s_listenWarnings;
                }
                if (s_previous) {
                    s_previous(type, context, text);
                }
            });
        QString reason;
        QVERIFY(controller.setEnabled(true, port, &reason));
        QTest::qWait(2500);   // at least one retry
        qInstallMessageHandler(s_previous);
        QCOMPARE(s_listenWarnings, 1);
        QVERIFY(state.enabled());
        QVERIFY(!state.listening());
        QCOMPARE(state.error(), StationTciController::blockedReason(
                                    port, {QHostAddress(QHostAddress::LocalHost)}));
        QVERIFY(OperatorWording::isPlain(state.error()));
        blocker.close();
        QTRY_VERIFY_WITH_TIMEOUT(state.listening(), 5000);
        QVERIFY(state.error().isEmpty());
        QVERIFY(controller.setEnabled(false, port, &reason));
    }

    // Rework part 3 (R-R3-48): the Core binds the station address and this
    // computer separately. A third program holds the port on this computer:
    // the station network is still served (the RF-Kit's band follow keeps
    // working), the object says which address is blocked, in plain words;
    // when the program lets go, the Core takes this computer too on its
    // next retry, with no stop and start of the server.
    void stationNetworkServedWhileThisComputerIsBlocked()
    {
        QString stationAddress;
        for (const QHostAddress& address : QNetworkInterface::allAddresses()) {
            if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
                stationAddress = address.toString();
                break;
            }
        }
        if (stationAddress.isEmpty()) {
            QSKIP("No non-loopback IPv4 address on this computer to stand in for the station.");
        }
        QTcpServer blocker;
        QVERIFY(blocker.listen(QHostAddress::LocalHost, 0));
        const quint16 port = blocker.serverPort();
        RadioModel model;
        StationTciModel state;
        StationTciController controller(&model, &state);
        controller.setBindOverride(stationAddress);
        QSignalSpy starts(controller.server(), &TciServer::serverStarted);
        QSignalSpy stops(controller.server(), &TciServer::serverStopped);
        QString reason;
        QVERIFY(controller.setEnabled(true, port, &reason));
        QVERIFY(state.listening());
        QCOMPARE(state.stationAddress(), stationAddress);
        QCOMPARE(state.error(), StationTciController::blockedReason(
                                    port, {QHostAddress(QHostAddress::LocalHost)}));
        QVERIFY(OperatorWording::isPlain(state.error()));
        // The RF-Kit's band follow is up on the station address.
        RfKitModel rfKit;
        RfKitModel::StationConnectionState ampState;
        ampState.configuredHost = stationAddress;
        ampState.configuredPort = 8080;
        rfKit.setStationConnectionState(ampState);
        RfKitBandFollow follow(&rfKit);
        follow.setServer(controller.server());
        QTRY_VERIFY(rfKit.bandFollow() != BandFollow::Off);
        QCOMPARE(rfKit.bandFollowAddress(), stationAddress);
        QWebSocket device;
        QStringList frames;
        connect(&device, &QWebSocket::textMessageReceived, &device,
                [&frames](const QString& text) { frames.append(text); });
        device.open(QUrl(QStringLiteral("ws://%1:%2").arg(stationAddress).arg(port)));
        QTRY_VERIFY(frames.join(QString()).contains(QStringLiteral("receive_only:true;")));
        device.close();

        blocker.close();
        QTRY_VERIFY_WITH_TIMEOUT(controller.server()->listenAddresses().contains(
                                     QHostAddress(QHostAddress::LocalHost)), 5000);
        QTRY_VERIFY(state.error().isEmpty());
        QCOMPARE(starts.count(), 1);
        QCOMPARE(stops.count(), 0);
        QVERIFY(controller.setEnabled(false, port, &reason));
    }

    // The TCI page's line, in user words.
    void stationLineReadsInUserWords()
    {
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        window.attachStation(&link);
        StationTciModel::State state;
        QVERIFY(TciSwitch::stationLine(&window).isEmpty());   // switch off at the Core
        state.enabled = true;
        state.port = 50001;
        window.stationTciModel()->setState(state);
        QCOMPARE(TciSwitch::stationLine(&window),
                 QStringLiteral("The station's TCI server is not running."));
        state.listening = true;
        state.stationAddress = QStringLiteral("192.168.1.20");
        window.stationTciModel()->setState(state);
        QCOMPARE(TciSwitch::stationLine(&window),
                 QStringLiteral("Also at the station: 192.168.1.20, port 50001"));
        link.coreHere = true;
        QCOMPARE(TciSwitch::stationLine(&window),
                 QStringLiteral("The Core on this computer serves TCI apps here, port 50001."));
        link.coreHere = false;
        link.tciAvailable = false;
        QVERIFY(TciSwitch::stationLine(&window).isEmpty());
        link.tciAvailable = true;
        for (const QString& line : {TciSwitch::stationLine(&window),
                                    StationTciModel::readOnlyReason()}) {
            QVERIFY2(OperatorWording::isPlain(line), qPrintable(line));
        }
        RadioModel local;
        QVERIFY(TciSwitch::stationLine(&local).isEmpty());
    }
};

QTEST_MAIN(StationTciServerTest)
#include "tst_station_tci_server.moc"
