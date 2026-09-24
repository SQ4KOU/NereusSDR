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
    CommandOutcome requestStationTci(bool on, quint16 port) override
    {
        ++requests;
        requestedOn = on;
        requestedPort = port;
        return {true, {}};
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

    // I1 (R-R3-48): a Core on this computer whose station switch is off (or
    // not listening, or on another port) serves no TCI here, so this
    // window's own server keeps running. Turning the Core's switch off from
    // another window brings this window's server back.
    void coreHereWithItsSwitchOffKeepsThisWindowsServer()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        const QHostAddress loopback(QHostAddress::LocalHost);

        // Startup: the window applies its switch without telling the Core,
        // and the Core's station switch is off (its default).
        tci.setSwitch(true, port, loopback, /*tellCore=*/false);
        QVERIFY(local.isRunning());
        QCOMPARE(link.requests, 0);
        window.reportStationLinkStateChanged();
        QVERIFY(local.isRunning());

        // The Core's switch on, on this port, but not listening: the window
        // offers it the port once (follow-up 1a); the Core never listens,
        // so after the wait this window serves again.
        StationTciModel::State state;
        state.enabled = true;
        state.port = port;
        window.stationTciModel()->setState(state);
        QVERIFY(!local.isRunning());
        QCOMPARE(link.requests, 1);
        QTRY_VERIFY_WITH_TIMEOUT(local.isRunning(), TciSwitch::kCoreAnswerWaitMs + 2000);
        QCOMPARE(link.requests, 1);

        // Listening on another port: still this window's.
        state.listening = true;
        state.port = port + 1;
        window.stationTciModel()->setState(state);
        QVERIFY(local.isRunning());

        // Listening on this port: the Core serves it; one server.
        state.port = port;
        window.stationTciModel()->setState(state);
        QVERIFY(!local.isRunning());

        // Another window turns the Core's switch off: this one serves again.
        state.enabled = false;
        state.listening = false;
        window.stationTciModel()->setState(state);
        QVERIFY(local.isRunning());
        QCOMPARE(local.port(), port);
    }

    // I1: this window turns the switch on with the Core here. It waits for
    // the Core rather than taking the port first; if the Core cannot listen,
    // this window serves.
    void switchOnWithCoreHereWaitsForTheCore()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        const QHostAddress loopback(QHostAddress::LocalHost);

        tci.setSwitch(true, port, loopback);
        QCOMPARE(link.requests, 1);
        QVERIFY(!local.isRunning());   // the Core was asked; its answer decides

        StationTciModel::State state;
        state.enabled = true;
        state.port = port;
        state.error = QStringLiteral("The port is in use.");
        window.stationTciModel()->setState(state);   // answered: not listening
        // The Core retries (follow-up 1b); this window serves once the wait
        // runs out, and does not ask again for the same state.
        QVERIFY(!local.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(local.isRunning(), TciSwitch::kCoreAnswerWaitMs + 2000);
        QCOMPARE(link.requests, 1);

        // No answer at all: this window serves after the wait.
        tci.setSwitch(false, port, loopback);
        QVERIFY(!local.isRunning());
        StationTciModel::State off;
        window.stationTciModel()->setState(off);
        tci.setSwitch(true, port, loopback);
        QVERIFY(!local.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(local.isRunning(), TciSwitch::kCoreAnswerWaitMs + 2000);
    }

    // Follow-up 1d (R-R3-48): the Core's answer arrives one property at a
    // time, in the object's order (enabled, port, listening,
    // stationAddress, error). The wait ends on the whole answer (on,
    // listening, this port), so the first property never makes this
    // window try to bind the port the Core is taking.
    void waitEndsOnTheCoresWholeAnswer()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        QSignalSpy starts(&local, &TciServer::serverStarted);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost));
        QVERIFY(!local.isRunning());
        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", true);
        QVERIFY(!local.isRunning());
        station->applyStationValue("port", int(port));
        QVERIFY(!local.isRunning());
        station->applyStationValue("listening", true);
        station->applyStationValue("error", QString());
        QVERIFY(!local.isRunning());
        QCOMPARE(starts.count(), 0);   // no bind was ever tried
    }

    // Follow-up 1c: with the link to the Core on this computer down, the
    // Core may be gone; its last state must not keep this window from
    // serving. The link back with the Core serving: one server again.
    void linkDropLetsThisWindowServe()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        StationTciModel::State serving;
        serving.enabled = true;
        serving.listening = true;
        serving.port = port;
        window.stationTciModel()->setState(serving);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost), /*tellCore=*/false);
        QVERIFY(!local.isRunning());

        link.ready = false;
        window.reportStationLinkStateChanged();
        QVERIFY(local.isRunning());

        link.ready = true;
        window.reportStationLinkStateChanged();
        QVERIFY(!local.isRunning());
    }

    // Follow-up 1a: this window serves the port on the Core's computer and
    // the Core's station switch comes on (from another window or the
    // phone), but the Core cannot listen: it reports on, not listening, on
    // this port. The window releases the port and asks the Core again,
    // once; the Core's listening answer completes the handover. With no
    // such answer inside the wait the window serves again, and does not
    // hand over again for the same state.
    void handsThePortToTheCoreWhenItCannotListen()
    {
        const quint16 port = freePort();
        RadioModel window(RadioModel::Role::Remote);
        FakeStationLink link;
        link.coreHere = true;
        window.attachStation(&link);
        TciServer local(&window);
        TciSwitch tci(&local, &window);
        tci.setSwitch(true, port, QHostAddress(QHostAddress::LocalHost), /*tellCore=*/false);
        QVERIFY(local.isRunning());
        QCOMPARE(link.requests, 0);

        StationTciModel* station = window.stationTciModel();
        station->applyStationValue("enabled", true);
        station->applyStationValue("port", int(port));
        station->applyStationValue("error", QStringLiteral("The port is in use."));
        QVERIFY(!local.isRunning());   // released
        QCOMPARE(link.requests, 1);
        QVERIFY(link.requestedOn);
        QCOMPARE(link.requestedPort, port);
        station->applyStationValue("listening", true);
        station->applyStationValue("error", QString());
        QVERIFY(!local.isRunning());   // the Core serves it now
        QVERIFY(tci.coreCoversThisComputer());

        // The Core loses it again and never answers: the window serves
        // after the wait, and asks only once for this state.
        station->applyStationValue("listening", false);
        QCOMPARE(link.requests, 2);
        QVERIFY(!local.isRunning());
        QTRY_VERIFY_WITH_TIMEOUT(local.isRunning(), TciSwitch::kCoreAnswerWaitMs + 2000);
        station->applyStationValue("error", QStringLiteral("Still in use."));
        QTest::qWait(50);
        QVERIFY(local.isRunning());
        QCOMPARE(link.requests, 2);
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
        QString reason;
        QVERIFY(controller.setEnabled(true, port, &reason));
        QVERIFY(state.enabled());
        QVERIFY(!state.listening());
        QCOMPARE(state.error(), StationTciController::cannotListenReason(port));
        QVERIFY(OperatorWording::isPlain(state.error()));
        blocker.close();
        QTRY_VERIFY_WITH_TIMEOUT(state.listening(), 5000);
        QVERIFY(state.error().isEmpty());
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
