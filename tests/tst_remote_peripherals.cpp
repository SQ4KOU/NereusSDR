// no-port-check: NereusSDR-original. Remote TGXL Peripherals UI coverage.

#include <QtTest>

#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>

#include "OperatorWording.h"
#include "core/PgxlConnection.h"
#include "core/TgxlConnection.h"
#include "core/SmartSdrApiListener.h"
#include "core/session/IStationLink.h"
#include "gui/setup/CatNetworkSetupPages.h"
#include "gui/setup/FourO3APage.h"
#include "gui/setup/PgxlAdvancedPage.h"
#include "gui/setup/TgxlAdvancedPage.h"
#include "gui/SetupDialog.h"
#include "models/RadioModel.h"
#include "models/TunerModel.h"

using namespace NereusSDR;

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
};

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

QTEST_MAIN(RemotePeripheralsTest)
#include "tst_remote_peripherals.moc"
