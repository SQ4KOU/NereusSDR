// no-port-check: NereusSDR-original. Negotiated GUI availability, no RF/DSP logic.
// 2026-09-24: R-R3-49 (parity Task 2): the TX applet's EQ toggle is a
// transmit setting, live in a remote window off the air. J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
// 2026-09-25: R-R3-49 (parity Task 3): Audio > TX Profile follows the
// transmit settings gate, not remote transmit. J.J. Boyd (KG4VCF),
// AI-assisted via Anthropic Claude Code.
// 2026-09-25: R-R3-49 (parity Task 5): Transmit > Power and DEXP/VOX open
// without remote transmit and gate their settings on version 5. J.J. Boyd
// (KG4VCF), AI-assisted via Anthropic Claude Code.
// 2026-09-25: R-R3-49, R-R3-21 (parity Task 11): the flag's XIT writes the
// Core's slice in a remote window, as RIT does, whatever txPermitted says.
// J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include <QtTest>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <chrono>
#include "core/AppSettings.h"
#include "core/RadioDiscovery.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/MainWindow.h"
#include "gui/SetupDialog.h"
#include "gui/applets/TxApplet.h"
#include "gui/applets/TxEqDialog.h"
#include "gui/widgets/ScrollableLabel.h"
#include "gui/widgets/VfoWidget.h"
#include "core/MoxController.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/MainWindowTestSettings.h"

using namespace NereusSDR;

namespace {
class BackendScope final {
public:
    explicit BackendScope(SettingsProxy* proxy) { AppSettings::instance().setRemoteBackend(proxy); }
    ~BackendScope() { AppSettings::instance().setRemoteBackend(nullptr); }
};
}

class TestRemoteTxPresentation : public QObject {
    Q_OBJECT
private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(QStringLiteral("remote-tx-presentation-%1")
                                           .arg(QCoreApplication::applicationPid()));
    }
    void init()
    {
        AppSettings::instance().clear();
        Test::markAudioFirstRunDone();
        RadioDiscovery::clearHoldOffForTest();
        RadioDiscovery discovery;
        discovery.holdOffScans(std::chrono::minutes{5});
    }
    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
        RadioDiscovery::clearHoldOffForTest();
    }
    void setupPagesFollowPermissionWithoutRemovingResourceGates_data()
    {
        QTest::addColumn<QString>("label");
        // Audio > TX Input is not a whole-page transmit leaf since R-R3-36:
        // its PC microphone controls are this computer's and stay live, and
        // only its controls held for the radio follow the permission
        // (tst_remote_gui_gating, remoteTxInputKeepsThisComputersMicrophoneUsable).
        // Transmit > TX Profiles (a page that only says it moved) is not
        // registered while it is on the unbuilt list (R-R3-49). Audio > TX
        // Profile follows the transmit settings gate since parity Task 3
        // (tst_remote_tx_profiles, setupOpensTxProfileWithoutRemoteTransmit).
        // Transmit > Speech Processor opens since parity Task 4: it shows the
        // Core's TX chain and opens the editors, each on the Core's gate
        // (tst_remote_tx_eq_cfc).
        // R-R3-49 (parity Task 5): Transmit > Power and DEXP/VOX open too.
        // The Core mirrors their settings and each page gates its own
        // controls on transmitSettingsVersion 5 (Enable VOX keeps the
        // transmit permission); the case now proves that gate
        // (tst_remote_transmit_setup_pages covers the open gate).
        for (const char* label : {"Power", "DEXP/VOX"}) {
            QTest::newRow(label) << QString::fromLatin1(label);
        }
    }
    void setupPagesFollowPermissionWithoutRemovingResourceGates()
    {
        QFETCH(QString, label);
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        const int before = remote.localDspHandOutCount();
        dialog.selectPage(label);
        QWidget* page = dialog.realizedPageForTest(label);
        QVERIFY(page);
        const bool resourceUnavailable = remote.localDspHandOutCount() > before;
        QVERIFY(!resourceUnavailable);
        QVERIFY(page->isEnabled());
        auto* notice = dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"));
        QVERIFY(notice && notice->isHidden());

        const int power = remote.transmitModel().power();
        const int micGain = remote.transmitModel().micGainDb();
        const bool dexp = remote.transmitModel().dexpEnabled();
        for (QPushButton* button : page->findChildren<QPushButton*>()) {
            QVERIFY(!button->isEnabled());
            button->click();
        }
        for (QSlider* slider : page->findChildren<QSlider*>()) {
            QVERIFY(!slider->isEnabled());
            QTest::keyClick(slider, Qt::Key_Right);
        }
        QCOMPARE(remote.transmitModel().power(), power);
        QCOMPARE(remote.transmitModel().micGainDb(), micGain);
        QCOMPARE(remote.transmitModel().dexpEnabled(), dexp);
        QVERIFY(dialog.findChildren<TxEqDialog*>().isEmpty());

        if (label == QStringLiteral("Power")) {
            const QString captures = qEnvironmentVariable("NEREUS_TX_GATE_CAPTURES");
            if (!captures.isEmpty()) {
                QVERIFY(QDir().mkpath(captures));
                dialog.show();
                QCoreApplication::processEvents();
                QVERIFY(dialog.grab().save(captures + QStringLiteral("/remote-tx-setup.png")));
            }
        }
        // The version 5 gate opens the settings without remote transmit, and
        // closing it puts them back with its reason.
        dialog.setTransmitSettingsPermitted(true, QString(), 5);
        for (QSlider* slider : page->findChildren<QSlider*>()) {
            QVERIFY(slider->isEnabled());
        }
        dialog.setTransmitSettingsPermitted(false, QStringLiteral("Permission withdrawn"), 5);
        for (QSlider* slider : page->findChildren<QSlider*>()) {
            QVERIFY(!slider->isEnabled());
            QCOMPARE(slider->toolTip(), QStringLiteral("Permission withdrawn"));
        }
        QVERIFY(page->isEnabled());
        QVERIFY(notice->isHidden());

        RadioModel local;
        SetupDialog localDialog(&local);
        QWidget* localPage = localDialog.realizePageForTest(label);
        QVERIFY(localPage && localPage->isEnabled());
    }
    void receiveSetupRemainsAvailable()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SetupDialog dialog(&remote);
        dialog.selectPage(QStringLiteral("NR/ANF"));
        QWidget* page = dialog.realizedPageForTest(QStringLiteral("NR/ANF"));
        QVERIFY(page && page->isEnabled());
        QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("setupTransmitUnavailable"))->isHidden());
        // VAX is receive export. It is this computer's page and works in a
        // remote window (R-R3-44), and the transmit permission must not
        // reclassify it: granting or withdrawing it leaves the page usable.
        QWidget* vax = dialog.realizePageForTest(QStringLiteral("VAX"));
        QVERIFY(vax && vax->isEnabled());
        dialog.setTransmitPermitted(true);
        QVERIFY(vax->isEnabled());
        dialog.setTransmitPermitted(false, QStringLiteral("Permission withdrawn"));
        QVERIFY(vax->isEnabled());
    }
    void toolsActionKeepsLocalTxEditorAvailable()
    {
        MainWindow window({}, nullptr, MainWindow::ConnectionStartup::Deferred);
        QAction* action = window.findChild<QAction*>(QStringLiteral("toolsTxEqualizer"));
        QVERIFY(action && action->isEnabled());
        action->trigger();
        QVERIFY(window.findChild<TxEqDialog*>());
    }
    void authenticatedRemoteControlsWriteXitAndRit()
    {
        QTemporaryDir directory;
        AppSettings stationSettings(directory.filePath(QStringLiteral("station.settings")));
        stationSettings.setValue(QLatin1String(AppSettings::kDaemonProfileSeededKey),
                                 QStringLiteral("True"));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int id = station.addSlice(QStringLiteral("pan-0"));
        SliceModel* stationSlice = station.sliceById(id);
        QVERIFY(stationSlice);
        stationSlice->setXitHz(230);
        StationServer server(&station, stationSettings, directory.path());
        SettingsProxy proxy;
        BackendScope backend(&proxy);
        MainWindow window({QStringLiteral("ws://127.0.0.1:1"), {}, {}, true}, nullptr,
                          MainWindow::ConnectionStartup::Deferred);
        auto* client = window.findChild<StationClient*>();
        QVERIFY(client);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client->startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client->isHandshakeComplete());
        QTRY_VERIFY(window.radioModel()->sliceById(id));
        QTRY_VERIFY(window.findChild<QPushButton*>(QStringLiteral("VfoXitButton")));
        auto* xit = window.findChild<QPushButton*>(QStringLiteral("VfoXitButton"));
        auto* zero = window.findChild<QPushButton*>(QStringLiteral("VfoXitZeroButton"));
        auto* offset = window.findChild<ScrollableLabel*>(QStringLiteral("VfoXitOffset"));
        auto* rit = window.findChild<QPushButton*>(QStringLiteral("VfoRitButton"));
        QVERIFY(xit && zero && offset && rit);
        // R-R3-49 (parity Task 11): XIT is a slice setting like RIT. The
        // Core does not offer transmit here, and XIT still writes.
        QVERIFY(!client->capabilities().txPermitted);
        QVERIFY(xit->isEnabled() && zero->isEnabled() && offset->isEnabled());
        QVERIFY(rit->isEnabled());
        QTRY_COMPARE(offset->value(), 230);
        // R-R3-49 (parity Task 4): Tools > TX Equalizer opens in a remote
        // window; opening it writes nothing.
        auto* action = window.findChild<QAction*>(QStringLiteral("toolsTxEqualizer"));
        QVERIFY(action && action->isEnabled());
        QVERIFY(!action->toolTip().isEmpty());
        // The dialog it opens takes the Core's gate: open off the air.
        QTRY_VERIFY(TxEqDialog::settingsPermitted());
        stationLink->clearReceived();
        xit->click();
        QTRY_VERIFY(stationSlice->xitEnabled());
        offset->setValue(400);
        QTRY_COMPARE(stationSlice->xitHz(), 400);
        QTRY_COMPARE(window.radioModel()->sliceById(id)->xitHz(), 400);
        zero->click();
        QTRY_COMPARE(stationSlice->xitHz(), 0);
        // The Core's change comes back to the flag.
        stationSlice->setXitHz(-150);
        QTRY_COMPARE(offset->value(), -150);
        stationSlice->setXitEnabled(false);
        QTRY_VERIFY(!xit->isChecked());
        // Group B fix wave: on the air, as in Thetis, XIT and the TX antenna
        // still change (console.cs udXIT_ValueChanged retunes with
        // updateVFOFreqs(_mox); an Alex TX antenna applies at once with
        // tx = _mox), from a remote window as from a local one.
        MoxController* const mox = station.moxController();
        QVERIFY(mox);
        mox->setMoxCheck({});
        mox->setMox(true);
        QTRY_VERIFY(window.radioModel()->isCoreOnAir());
        QVERIFY(xit->isEnabled() && offset->isEnabled() && zero->isEnabled());
        xit->click();
        QTRY_VERIFY(stationSlice->xitEnabled());
        offset->setValue(120);
        QTRY_COMPARE(stationSlice->xitHz(), 120);
        const QString txAntenna = stationSlice->txAntenna() == QStringLiteral("ANT2")
            ? QStringLiteral("ANT3") : QStringLiteral("ANT2");
        window.radioModel()->sliceById(id)->setTxAntenna(txAntenna);
        QTRY_COMPARE(stationSlice->txAntenna(), txAntenna);
        mox->setMox(false);
        QTRY_VERIFY(!window.radioModel()->isCoreOnAir());
        xit->click();
        QTRY_VERIFY(!stationSlice->xitEnabled());
        stationSlice->setXitHz(-150);
        QTRY_COMPARE(offset->value(), -150);
        action->trigger();
        auto* eq = window.findChild<QPushButton*>(QStringLiteral("TxEqButton"));
        // R-R3-49 (parity Task 2): the EQ toggle is a transmit setting, live
        // off the air. Parity Task 4: its right-click opens the TX equalizer
        // dialog (the one Tools opened above).
        QVERIFY(eq);
        QTRY_VERIFY(eq->isEnabled());
        TxEqDialog* const eqDialog = window.findChild<TxEqDialog*>();
        QVERIFY(eqDialog);
        eqDialog->hide();
        QMetaObject::invokeMethod(eq, "customContextMenuRequested", Q_ARG(QPoint, QPoint()));
        QVERIFY(eqDialog->isVisible());

        // RIT writes too.
        const bool desiredRit = !stationSlice->ritEnabled();
        rit->click();
        QTRY_COMPARE(stationSlice->ritEnabled(), desiredRit);
        QCOMPARE(stationSlice->xitHz(), -150);
        QVERIFY(!stationSlice->xitEnabled());
        bool wroteXitEnabled = false;
        bool wroteXitHz = false;
        for (const QByteArray& wire : stationLink->received()) {
            SessionMessage message;
            QVERIFY(SessionMessages::decode(wire, &message));
            if (message.kind != SessionMessageKind::PropertyWrite) { continue; }
            for (const MirrorUpdate& update : message.updates) {
                wroteXitEnabled = wroteXitEnabled || update.name == QByteArrayLiteral("xitEnabled");
                wroteXitHz = wroteXitHz || update.name == QByteArrayLiteral("xitHz");
            }
        }
        QVERIFY(wroteXitEnabled && wroteXitHz);

        QAction* settingsAction = nullptr;
        for (QAction* candidate : window.findChildren<QAction*>()) {
            if (candidate->text() == QStringLiteral("&Settings...")) {
                settingsAction = candidate;
                break;
            }
        }
        QVERIFY(settingsAction);
        settingsAction->trigger();
        auto* setup = window.findChild<SetupDialog*>();
        QVERIFY(setup);
        setup->selectPage(QStringLiteral("Power"));
        QWidget* powerPage = setup->realizedPageForTest(QStringLiteral("Power"));
        // R-R3-49 (parity Task 5): Transmit > Power opens without remote
        // transmit; its settings follow transmitSettingsVersion 5.
        QVERIFY(powerPage && powerPage->isEnabled());
        auto* proc = window.findChild<QPushButton*>(QStringLiteral("PhoneCwProcButton"));
        // R-R3-49 (parity Task 2): PROC and EQ are transmit settings: live
        // off the air whatever txPermitted says.
        QVERIFY(proc && proc->isEnabled());
        const quint32 epoch = client->sessionEpoch();
        const ConnectionState radioState = window.radioModel()->connectionState();
        StationCapabilities capabilities = client->capabilities();
        capabilities.txPermitted = true;
        stationLink->sendText(SessionMessages::encode(
            SessionMessages::capabilities(capabilities.toUpdates())));
        QTRY_VERIFY(client->capabilities().txPermitted);
        QTRY_VERIFY(xit->isEnabled());
        QVERIFY(action->isEnabled());
        QVERIFY(proc->isEnabled() && eq->isEnabled());
        QVERIFY(powerPage->isEnabled());
        capabilities.txPermitted = false;
        stationLink->sendText(SessionMessages::encode(
            SessionMessages::capabilities(capabilities.toUpdates())));
        QTRY_VERIFY(!client->capabilities().txPermitted);
        QVERIFY(xit->isEnabled() && zero->isEnabled() && offset->isEnabled());
        // R-R3-49 (parity Task 4): TX Equalizer stays available.
        QVERIFY(action->isEnabled());
        QVERIFY(proc->isEnabled() && eq->isEnabled());
        QVERIFY(powerPage->isEnabled());
        QCOMPARE(client->sessionEpoch(), epoch);
        QCOMPARE(window.radioModel()->connectionState(), radioState);
        client->disconnectFromStation(QStringLiteral("test complete"));
        // R-R3-49 (parity Task 4): the entry stays; the dialog it opens is
        // greyed with the reason while no Core takes a change.
        QVERIFY(action->isEnabled());
        QVERIFY(!TxEqDialog::settingsPermitted());
    }
};

QTEST_MAIN(TestRemoteTxPresentation)
#include "tst_remote_tx_presentation.moc"
