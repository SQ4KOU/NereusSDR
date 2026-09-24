// no-port-check: NereusSDR-original. Remote TGXL Peripherals UI coverage.
// 2026-09-23: R-R3-47 / R-R3-22: the Power Genius and RF-Kit applets read
// the Core's `amplifier` and `rfkit` objects in a remote window (filled on
// attach, updated, false and zero shown, stale on Core loss, no accessory
// socket opened) and the same objects in-process in a local window. J.J.
// Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.

#include <QtTest>

#include <QTemporaryDir>
#include <cmath>
#include <memory>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>

#include "OperatorWording.h"
#include "core/PgxlConnection.h"
#include "core/TgxlConnection.h"
#include "core/SmartSdrApiListener.h"
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
#include "gui/setup/PgxlAdvancedPage.h"
#include "gui/setup/TgxlAdvancedPage.h"
#include "gui/SetupDialog.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"
#include "models/RfKitModel.h"
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
    QVERIFY(!page.findChild<PgxlAdvancedPage*>());
    QVERIFY(!page.findChild<TgxlAdvancedPage*>());
    auto* master = page.findChild<QCheckBox*>();
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

QTEST_MAIN(RemotePeripheralsTest)
#include "tst_remote_peripherals.moc"
