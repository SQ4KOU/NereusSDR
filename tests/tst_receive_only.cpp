// no-port-check: NereusSDR test fixture; the Thetis behaviour it checks is
// cited inline and ported in MoxController / RadioModel / GeneralOptionsPage.
//
// =================================================================
// tests/tst_receive_only.cpp  (NereusSDR)
// =================================================================
//
// Receiver and transmit gaps plan, Task 16: receive only stops every key,
// as Thetis's RXOnly does.
//
// From Thetis console.cs:15312-15334 [v2.10.3.15] (RXOnly setter): MOX
// disabled unless SPEC or DRM, TUN, 2TONE (// MW0LGE_21a) and VOX disabled,
// a keyed MOX dropped, Setup kept in step. PollPTT skips every source while
// _rx_only is set (console.cs:25470) and chkMOX_CheckedChanged2 refuses any
// key (console.cs:29378). Setup's chkGeneralRXOnly_CheckedChanged
// (setup.cs:6479) asks before transmit is turned back on.
//
// Covered:
//   - every keying source is refused through the one gate (MoxController),
//     with a plain reason for the ones that report a refusal, and a keyed
//     transmission drops when receive only turns on;
//   - the HL2 receive-only kit (board byte 12, isRxOnlySku) always runs
//     receive only, and the Setup box shows checked and disabled with the
//     reason (NereusSDR's own rule; mi0bot-Thetis has no kit model);
//   - the Setup box is never hidden, asks before turning transmit on, and
//     follows the model;
//   - the TX applet's MOX, TUNE, 2-Tone and VOX and the container buttons
//     show disabled with the reason (MOX follows the SPEC / DRM exception);
//   - a remote window's box reaches the Core's gate over a session, follows
//     the Core's value, and an older Core's refusal puts it back.
//
// No hardware; nothing keys a radio; no audio device is opened.
//
// Modification history (NereusSDR):
//   2026-09-25: created (Task 16), by J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/HpsdrModel.h"
#include "core/MoxController.h"
#include "core/RadioConnection.h"
#include "core/TwoToneController.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/applets/TxApplet.h"
#include "gui/containers/ContainerButtonDispatcher.h"
#include "gui/setup/GeneralOptionsPage.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "OperatorWording.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

class RxOnlyMockConnection : public RadioConnection {
    Q_OBJECT
public:
    explicit RxOnlyMockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        setState(ConnectionState::Connected);
    }

    void init() override {}
    void connectToRadio(const NereusSDR::RadioInfo&) override {}
    void disconnect() override {}
    void setReceiverFrequency(int, quint64) override {}
    void setTxFrequency(quint64) override {}
    void setActiveReceiverCount(int) override {}
    void setSampleRate(int) override {}
    void setAttenuator(int) override {}
    void setPreamp(bool) override {}
    void setTxDrive(int) override {}
    void sendTxIq(const float*, int) override {}
    void setWatchdogEnabled(bool) override {}
    void setAntennaRouting(AntennaRouting) override {}
    void setMox(bool) override {}
    void setTrxRelay(bool) override {}
    void setMicBoost(bool) override {}
    void setLineIn(bool) override {}
    void setMicTipRing(bool) override {}
    void setMicBias(bool) override {}
    void setLineInGain(int) override {}
    void setUserDigOut(quint8) override {}
    void setPuresignalRun(bool) override {}
    void setMicPTTDisabled(bool) override {}
    void setMicXlr(bool) override {}
};

namespace {

const QString kKey = QStringLiteral("RxOnly");

void pump()
{
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
    }
}

// A local radio: a connected mock, synchronous MOX walk, one USB slice.
struct Rig {
    std::unique_ptr<RxOnlyMockConnection> conn;
    std::unique_ptr<RadioModel> model;

    // capsOverride false: the board's own capability row, so
    // setBoardForTest reaches boardCapabilities() (the kit test).
    explicit Rig(bool capsOverride = true)
        : conn(std::make_unique<RxOnlyMockConnection>())
        , model(std::make_unique<RadioModel>())
    {
        if (capsOverride) {
            model->setCapsForTest(/*hasAlex=*/false);
        }
        model->injectConnectionForTest(conn.get());
        model->moxController()->setTimerIntervals(0, 0, 0, 0, 0, 0);
        model->setTuneOffSettleMsForTest(0);
        model->addSlice();
        model->activeSlice()->setDspMode(DSPMode::USB);
    }

    ~Rig()
    {
        if (model->isTune()) {
            model->setTune(false);
            pump();
        }
        model->injectConnectionForTest(nullptr);
        model.reset();
    }

    MoxController* mox() const { return model->moxController(); }
};

// Keys one source the way it keys in production.
void keySource(Rig& rig, const QString& source)
{
    MoxController* mox = rig.mox();
    if (source == QLatin1String("mic")) {
        mox->onMicPttFromRadio(true);
    } else if (source == QLatin1String("vox")) {
        mox->onVoxActive(true);
    } else if (source == QLatin1String("cat")) {
        mox->onCatPtt(true);
    } else if (source == QLatin1String("tci")) {
        rig.model->setMox(true);            // TciProtocol's trx shim
    } else if (source == QLatin1String("mox button")) {
        rig.model->setMoxFromButton(true);  // TxApplet and the container MOX
    } else if (source == QLatin1String("tun")) {
        rig.model->setTune(true);
    } else if (source == QLatin1String("two-tone")) {
        // Two-tone keys with the manual key and setMox(true).
        mox->setManualKey(true);
        mox->setMox(true);
    } else if (source == QLatin1String("space")) {
        mox->onSpacePtt(true);
    }
}

class ScopedRemoteBackend {
public:
    explicit ScopedRemoteBackend(ISettingsBackend* backend)
    {
        AppSettings::instance().setRemoteBackend(backend);
    }
    ~ScopedRemoteBackend() { AppSettings::instance().setRemoteBackend(nullptr); }
};

QCheckBox* rxOnlyBox(GeneralOptionsPage& page)
{
    return page.findChild<QCheckBox*>(QStringLiteral("chkGeneralRXOnly"));
}

} // namespace

class TestReceiveOnly : public QObject {
    Q_OBJECT

private:
    QTemporaryDir m_securityDir;

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_securityDir.isValid());
    }
    void init() { AppSettings::instance().clear(); }
    void cleanup() { AppSettings::instance().clear(); }

    // ---- The gate: every source refused -----------------------------------

    void everySourceIsRefused_data()
    {
        QTest::addColumn<QString>("source");
        QTest::addColumn<bool>("reported");
        // The PollPTT sources are skipped silently, as Thetis's poll is
        // (console.cs:25470); every other key reports why.
        QTest::newRow("mic") << QStringLiteral("mic") << false;
        QTest::newRow("vox") << QStringLiteral("vox") << false;
        QTest::newRow("cat") << QStringLiteral("cat") << false;
        QTest::newRow("tci") << QStringLiteral("tci") << false;
        QTest::newRow("mox button") << QStringLiteral("mox button") << true;
        QTest::newRow("tun") << QStringLiteral("tun") << true;
        QTest::newRow("two-tone") << QStringLiteral("two-tone") << true;
        QTest::newRow("space") << QStringLiteral("space") << true;
    }

    void everySourceIsRefused()
    {
        QFETCH(QString, source);
        QFETCH(bool, reported);
        Rig rig;
        rig.model->setRxOnly(true);
        QVERIFY(rig.model->isRxOnly());
        QVERIFY(rig.mox()->isRxOnly());

        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        keySource(rig, source);
        pump();
        QVERIFY2(!rig.mox()->isMox(), qPrintable(source + QStringLiteral(" keyed in receive only")));
        QVERIFY(!rig.model->isTune());
        if (reported) {
            QVERIFY2(!rejected.isEmpty(), qPrintable(source + QStringLiteral(" was not told why")));
            const QString reason = rejected.last().at(0).toString();
            QCOMPARE(reason, rig.model->rxOnlyReason());
            QVERIFY(OperatorWording::isPlain(reason));
        } else {
            QVERIFY(rejected.isEmpty());
        }
    }

    // CAT and TCI requests are dropped, not held: they do not key when
    // receive only is turned off (as under TX inhibit, Task 7 follow-up N3).
    void appRequestsAreDroppedNotHeld_data()
    {
        QTest::addColumn<QString>("source");
        for (const char* source : {"cat", "tci", "mox button", "tun"}) {
            QTest::newRow(source) << QString::fromLatin1(source);
        }
    }

    void appRequestsAreDroppedNotHeld()
    {
        QFETCH(QString, source);
        Rig rig;
        rig.model->setRxOnly(true);
        keySource(rig, source);
        pump();
        rig.model->setRxOnly(false);
        pump();
        QVERIFY(!rig.mox()->isRxOnly());
        QVERIFY2(!rig.mox()->isMox(),
                 qPrintable(source + QStringLiteral(" keyed when receive only went off")));
    }

    // From Thetis console.cs:15325-15326 [v2.10.3.15]:
    //   if (_rx_only && chkMOX.Checked)
    //       chkMOX.Checked = false;
    void turningOnDropsAKeyedTransmission_data()
    {
        QTest::addColumn<QString>("source");
        for (const char* source : {"mox button", "mic", "tun", "tci"}) {
            QTest::newRow(source) << QString::fromLatin1(source);
        }
    }

    void turningOnDropsAKeyedTransmission()
    {
        QFETCH(QString, source);
        Rig rig;
        keySource(rig, source);
        pump();
        QVERIFY2(rig.mox()->isMox(), qPrintable(source + QStringLiteral(" did not key")));

        rig.model->setRxOnly(true);
        pump();
        QVERIFY2(!rig.mox()->isMox(), qPrintable(source + QStringLiteral(" stayed keyed")));
        QVERIFY(!rig.model->isTune());
    }

    // Turned off, keying works again: a new press keys.
    void turningOffLetsANewPressKey()
    {
        Rig rig;
        rig.model->setRxOnly(true);
        rig.model->setRxOnly(false);
        QVERIFY(!rig.model->isRxOnly());
        rig.model->setMoxFromButton(true);
        pump();
        QVERIFY(rig.mox()->isMox());
        rig.model->setMoxFromButton(false);
        pump();
        QVERIFY(!rig.mox()->isMox());
    }

    void settingIsSavedAndReadAtStart()
    {
        {
            Rig rig;
            QSignalSpy changed(rig.model.get(), &RadioModel::rxOnlyChanged);
            rig.model->setRxOnly(true);
            QCOMPARE(AppSettings::instance().value(kKey).toString(), QStringLiteral("True"));
            QCOMPARE(changed.count(), 1);
        }
        // Thetis restores it at start (setup.cs:740 [v2.10.3.15]).
        RadioModel fresh;
        QVERIFY(fresh.isRxOnly());
        QVERIFY(fresh.moxController()->isRxOnly());
    }

    // ---- The HL2 receive-only kit -----------------------------------------

    void receiveOnlyKitAlwaysRunsReceiveOnly()
    {
        Rig rig(/*capsOverride=*/false);
        rig.model->setBoardForTest(HPSDRHW::HermesLiteRxOnly);
        QVERIFY(rig.model->boardCapabilities().isRxOnlySku);
        QVERIFY(rig.model->isRxOnly());
        QVERIFY(rig.model->isRxOnlyForced());
        QVERIFY(rig.mox()->isRxOnly());
        QCOMPARE(rig.model->rxOnlyReason(), RadioModel::rxOnlyForcedReason());
        QVERIFY(OperatorWording::isPlain(rig.model->rxOnlyReason()));
        QVERIFY(rig.model->rxOnlyReason().contains(QStringLiteral("no transmitter")));

        // The setting cannot turn it off.
        rig.model->setRxOnly(false);
        QVERIFY(rig.model->isRxOnly());
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        rig.model->setMoxFromButton(true);
        pump();
        QVERIFY(!rig.mox()->isMox());
        QCOMPARE(rejected.last().at(0).toString(), RadioModel::rxOnlyForcedReason());

        // A standard HL2 follows the setting again.
        rig.model->setBoardForTest(HPSDRHW::HermesLite);
        QVERIFY(!rig.model->isRxOnly());
        QVERIFY(!rig.model->isRxOnlyForced());
        QVERIFY(!rig.mox()->isRxOnly());
    }

    // ---- Setup > General > Options: Receive Only ---------------------------

    void checkboxIsNeverHidden_data()
    {
        QTest::addColumn<int>("board");
        QTest::newRow("no radio") << -1;
        QTest::newRow("HL2") << int(HPSDRHW::HermesLite);
        QTest::newRow("kit") << int(HPSDRHW::HermesLiteRxOnly);
        QTest::newRow("G2") << int(HPSDRHW::OrionMKII);
        QTest::newRow("Hermes") << int(HPSDRHW::Hermes);
    }

    void checkboxIsNeverHidden()
    {
        QFETCH(int, board);
        RadioModel model;
        if (board >= 0) {
            model.setBoardForTest(static_cast<HPSDRHW>(board));
        }
        GeneralOptionsPage page(board >= 0 ? &model : nullptr);
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        QVERIFY2(!box->isHidden(), "Receive Only must be shown on every radio");
        QCOMPARE(box->text(), QStringLiteral("Receive Only"));
    }

    void kitShowsCheckedAndDisabledWithTheReason()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLiteRxOnly);
        GeneralOptionsPage page(&model);
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        QVERIFY(box->isChecked());
        QVERIFY(!box->isEnabled());
        QCOMPARE(box->toolTip(), RadioModel::rxOnlyForcedReason());
        QVERIFY(OperatorWording::isPlain(box->toolTip()));

        // Back on a radio with a transmitter: the setting, enabled.
        model.setBoardForTest(HPSDRHW::HermesLite);
        model.emitCurrentRadioChangedForTest();
        QVERIFY(!box->isChecked());
        QVERIFY(box->isEnabled());
        QCOMPARE(box->toolTip(), QStringLiteral("Check to disable transmit functionality."));
    }

    // From Thetis setup.cs:6481-6498 [v2.10.3.15]: unchecking asks first, and
    // No puts the check back.
    void turningOffAsksFirst()
    {
        Rig rig;
        GeneralOptionsPage page(rig.model.get());
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        int asked = 0;
        bool answer = false;
        page.setEnableTransmitConfirmForTest([&] { ++asked; return answer; });

        box->click();                  // on: no question
        QCOMPARE(asked, 0);
        QVERIFY(rig.model->isRxOnly());
        QCOMPARE(AppSettings::instance().value(kKey).toString(), QStringLiteral("True"));

        box->click();                  // off, answered No
        QCOMPARE(asked, 1);
        QVERIFY(box->isChecked());
        QVERIFY(rig.model->isRxOnly());

        answer = true;
        box->click();                  // off, answered Yes
        QCOMPARE(asked, 2);
        QVERIFY(!box->isChecked());
        QVERIFY(!rig.model->isRxOnly());
        QCOMPARE(AppSettings::instance().value(kKey).toString(), QStringLiteral("False"));
    }

    // Thetis keeps Setup in step with console.RXOnly (console.cs:15328-15332).
    void checkboxFollowsTheModel()
    {
        Rig rig;
        GeneralOptionsPage page(rig.model.get());
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        QVERIFY(!box->isChecked());
        rig.model->setRxOnly(true);
        QVERIFY(box->isChecked());
        rig.model->applyRxOnlySetting(false);
        QVERIFY(!box->isChecked());
    }

    // ---- The transmit buttons ----------------------------------------------

    void txAppletButtonsAreDisabledWithTheReason()
    {
        Rig rig;
        TxApplet applet(rig.model.get());
        QPushButton* mox = applet.moxButton();
        QPushButton* tune = applet.tuneButton();
        QPushButton* twoTone = applet.twoToneButton();
        QPushButton* vox = applet.voxButton();
        QVERIFY(mox && tune && twoTone && vox);
        const QString moxTip = mox->toolTip();
        const QString tuneTip = tune->toolTip();
        QVERIFY(mox->isEnabled() && tune->isEnabled() && twoTone->isEnabled()
                && vox->isEnabled());

        rig.model->setRxOnly(true);
        for (QPushButton* b : {mox, tune, twoTone, vox}) {
            QVERIFY2(!b->isEnabled(), qPrintable(b->text() + QStringLiteral(" left enabled")));
            QCOMPARE(b->toolTip(), rig.model->rxOnlyReason());
            QVERIFY(OperatorWording::isPlain(b->toolTip()));
        }

        rig.model->setRxOnly(false);
        for (QPushButton* b : {mox, tune, twoTone, vox}) {
            QVERIFY(b->isEnabled());
        }
        QCOMPARE(mox->toolTip(), moxTip);
        QCOMPARE(tune->toolTip(), tuneTip);
    }

    // From Thetis console.cs:15318-15321 [v2.10.3.15]: in SPEC and DRM the
    // RXOnly setter leaves chkMOX.Enabled alone. The gate still refuses.
    void moxButtonFollowsTheSpecAndDrmException_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("SPEC") << int(DSPMode::SPEC);
        QTest::newRow("DRM") << int(DSPMode::DRM);
    }

    void moxButtonFollowsTheSpecAndDrmException()
    {
        QFETCH(int, mode);
        Rig rig;
        TxApplet applet(rig.model.get());
        rig.model->activeSlice()->setDspMode(static_cast<DSPMode>(mode));
        rig.model->setRxOnly(true);
        QVERIFY(!rig.model->receiveOnlyDisablesMoxButton());
        QVERIFY(applet.moxButton()->isEnabled());
        QVERIFY(!applet.tuneButton()->isEnabled());
        QVERIFY(!applet.twoToneButton()->isEnabled());
        QVERIFY(!applet.voxButton()->isEnabled());

        // Back to USB: MOX is disabled too.
        rig.model->activeSlice()->setDspMode(DSPMode::USB);
        QVERIFY(rig.model->receiveOnlyDisablesMoxButton());
        QVERIFY(!applet.moxButton()->isEnabled());

        rig.model->activeSlice()->setDspMode(static_cast<DSPMode>(mode));
        QSignalSpy rejected(rig.mox(), &MoxController::moxRejected);
        rig.model->setMoxFromButton(true);
        pump();
        QVERIFY(!rig.mox()->isMox());
    }

    // The lock sits on top of a remote window's transmit-permission gate.
    void txAppletLockStacksOnTheTransmitPermission()
    {
        Rig rig;
        TxApplet applet(rig.model.get());
        QPushButton* tune = applet.tuneButton();
        const QString tuneTip = tune->toolTip();
        rig.model->setRxOnly(true);
        applet.setTransmitPermitted(false, QStringLiteral("Waiting for the Core."));
        QVERIFY(!tune->isEnabled());
        QCOMPARE(tune->toolTip(), rig.model->rxOnlyReason());
        rig.model->setRxOnly(false);
        QVERIFY(!tune->isEnabled());
        QCOMPARE(tune->toolTip(), QStringLiteral("Waiting for the Core."));
        applet.setTransmitPermitted(true);
        QVERIFY(tune->isEnabled());
        QCOMPARE(tune->toolTip(), tuneTip);
    }

    void containerButtonsAreUnavailableWithTheReason()
    {
        Rig rig;
        ContainerButtonDispatcher::Hooks hooks;
        hooks.transmitPermitted = [] { return true; };
        ContainerButtonDispatcher dispatcher(rig.model.get(), std::move(hooks));
        using Id = ContainerButtonDispatcher::Id;
        for (Id id : {Id::Tun, Id::Mox, Id::TwoTon}) {
            QVERIFY(dispatcher.stateOf(id, 0).available);
        }

        rig.model->setRxOnly(true);
        for (Id id : {Id::Tun, Id::Mox, Id::TwoTon}) {
            const auto st = dispatcher.stateOf(id, 0);
            QVERIFY(!st.available);
            QCOMPARE(st.reason, rig.model->rxOnlyReason());
        }
        // A click says why and keys nothing.
        QCOMPARE(dispatcher.click(Id::Mox, 0), rig.model->rxOnlyReason());
        pump();
        QVERIFY(!rig.mox()->isMox());

        // SPEC: MOX keeps its state (the gate still refuses).
        rig.model->activeSlice()->setDspMode(DSPMode::SPEC);
        QVERIFY(dispatcher.stateOf(Id::Mox, 0).available);
        QVERIFY(!dispatcher.stateOf(Id::Tun, 0).available);
    }

    // ---- A remote window ---------------------------------------------------

    void remoteCheckboxReachesTheCoresGate()
    {
        QTemporaryDir dir;
        AppSettings coreSettings(dir.filePath(QStringLiteral("station.settings")));
        Rig core;
        {
            StationServer server(core.model.get(), coreSettings, m_securityDir.path());
            RadioModel window(RadioModel::Role::Remote);
            SettingsProxy proxy;
            StationClient client(&window, &proxy);
            auto* stationEnd = new LoopbackTransport(QStringLiteral("station-end"), this);
            auto* clientEnd = new LoopbackTransport(QStringLiteral("client-end"), this);
            stationEnd->linkTo(clientEnd);
            QSignalSpy completed(&client, &StationClient::handshakeComplete);
            client.startSession(clientEnd, server.token());
            server.acceptTransport(stationEnd);
            QVERIFY(completed.wait(5000) || !completed.isEmpty());

            ScopedRemoteBackend backend(&proxy);
            GeneralOptionsPage page(&window);
            page.setEnableTransmitConfirmForTest([] { return true; });
            QCheckBox* box = rxOnlyBox(page);
            QVERIFY(box != nullptr);
            QVERIFY(!box->isHidden());
            QVERIFY(!box->isChecked());

            box->click();
            QTRY_COMPARE(coreSettings.value(kKey).toString(), QStringLiteral("True"));
            QTRY_VERIFY(core.model->isRxOnly());
            QVERIFY(core.mox()->isRxOnly());
            QVERIFY(window.isRxOnly());

            // The Core's gate refuses its own sources.
            core.mox()->onCatPtt(true);
            core.model->setMoxFromButton(true);
            pump();
            QVERIFY(!core.mox()->isMox());

            box->click();   // off, answered Yes
            QTRY_COMPARE(coreSettings.value(kKey).toString(), QStringLiteral("False"));
            QTRY_VERIFY(!core.model->isRxOnly());

            // A change made on the Core reaches the window and its box.
            coreSettings.setValue(kKey, QStringLiteral("True"));
            QTRY_VERIFY(core.model->isRxOnly());
            QTRY_VERIFY(window.isRxOnly());
            QTRY_VERIFY(box->isChecked());

            // A settings reset on the Core reads the default (off).
            coreSettings.remove(kKey);
            QTRY_VERIFY(!core.model->isRxOnly());
            QTRY_VERIFY(!window.isRxOnly());
        }
    }

    void remoteWindowOnAKitCoreShowsReceiveOnly()
    {
        RadioModel window(RadioModel::Role::Remote);
        StationCapabilities caps;
        caps.macAddress = QStringLiteral("AA:BB:CC:DD:EE:16");
        caps.board = HPSDRHW::HermesLiteRxOnly;
        caps.radioConnected = true;
        caps.radioIdentityEntries = true;
        caps.hpsdrModel = HPSDRModel::HERMESLITE;
        caps.radioProtocol = 1;
        window.applyStationCapabilities(caps);
        QVERIFY(window.isRxOnly());
        QVERIFY(window.isRxOnlyForced());

        GeneralOptionsPage page(&window);
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        QVERIFY(box->isChecked());
        QVERIFY(!box->isEnabled());
        QCOMPARE(box->toolTip(), RadioModel::rxOnlyForcedReason());

        // The Core's gate on a remote window: still locked while the
        // window has no Core settings, and unlocked to the gate's state.
        page.setStationSettingsAvailable(false, QStringLiteral("Connect to the Core to change these."));
        QVERIFY(!box->isEnabled());
        page.setStationSettingsAvailable(true, QString());
        QVERIFY(!box->isEnabled());
        QCOMPARE(box->toolTip(), RadioModel::rxOnlyForcedReason());
    }

    void remoteCheckboxIsDisabledWithoutTheCoresSettings()
    {
        RadioModel window(RadioModel::Role::Remote);
        GeneralOptionsPage page(&window);
        QCheckBox* box = rxOnlyBox(page);
        QVERIFY(box != nullptr);
        const QString reason = QStringLiteral("Connect to the Core to change these.");
        page.setStationSettingsAvailable(false, reason);
        QVERIFY(!box->isEnabled());
        QCOMPARE(box->toolTip(), reason);
        page.setStationSettingsAvailable(true, QString());
        QVERIFY(box->isEnabled());
    }

    void olderCoreRefusalPutsTheBoxBackAndSaysSo()
    {
        RadioModel window(RadioModel::Role::Remote);
        SettingsProxy proxy;
        ScopedRemoteBackend backend(&proxy);
        GeneralOptionsPage page(&window);
        QCheckBox* box = rxOnlyBox(page);
        auto* note = page.findChild<QLabel*>(QStringLiteral("lblRxOnlyCore"));
        QVERIFY(box != nullptr && note != nullptr);
        QVERIFY(note->isHidden());

        box->click();
        QVERIFY(window.isRxOnly());
        // An older Core keeps the key to itself and has no value for it.
        proxy.applyRejection(kKey, QVariant());
        QVERIFY(!box->isChecked());
        QVERIFY(!window.isRxOnly());
        QVERIFY(!note->isHidden());
        QVERIFY(OperatorWording::isPlain(note->text()));
        QVERIFY(note->text().contains(QStringLiteral("Core needs updating")));
    }

    // ---- Wording -----------------------------------------------------------

    void everyReasonIsPlain()
    {
        QVERIFY(OperatorWording::isPlain(MoxController::defaultRxOnlyReason()));
        QVERIFY(OperatorWording::isPlain(RadioModel::rxOnlyForcedReason()));
        QVERIFY(OperatorWording::coreCalledStationIn(MoxController::defaultRxOnlyReason()).isEmpty());
        QVERIFY(OperatorWording::coreCalledStationIn(RadioModel::rxOnlyForcedReason()).isEmpty());
    }
};

QTEST_MAIN(TestReceiveOnly)
#include "tst_receive_only.moc"
