// tst_hardware_page_capability_gating.cpp
//
// Phase 3I Task 21 — data-driven test asserting that each HPSDRHW board type
// shows the correct set of tabs in HardwarePage.
//
// no-port-check: test fixture exercises NereusSDR HardwarePage; cite
// comments to mi0bot setup.cs:20232 are documentary only — the ported
// logic itself lives in HardwarePage.cpp where the attribution header
// + PROVENANCE row already cover it.

#include <QtTest/QtTest>
#include "gui/setup/HardwarePage.h"
#include "gui/UnbuiltFeatures.h"
#include "core/HpsdrModel.h"
#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include "models/RadioModel.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/accessories/AlexController.h"
#include "core/session/StationCapabilities.h"
#include "gui/setup/hardware/AntennaAlexAntennaControlTab.h"
#include "models/Band.h"

#include <QCheckBox>
#include <QRadioButton>
#include <QScopeGuard>

using namespace NereusSDR;

class TestHardwarePageGating : public QObject {
    Q_OBJECT
private slots:
    // HardwarePage::onCurrentRadioChanged derives capability gating from
    // m_model->hardwareProfile().caps (Phase 3I-RP). In production
    // RadioModel::connectToRadio seeds the hardware profile via
    // profileForModel(info.modelOverride | defaultModelForBoard(info))
    // before emitting currentRadioChanged, so caps is live by the time
    // HardwarePage responds. Tests that drive onCurrentRadioChanged
    // directly must prime the profile via setBoardForTest, or the
    // dereference at HardwarePage.cpp:198 (*caps) segfaults on the
    // default-constructed HardwareProfile whose caps pointer is null.

    void hl2_hides_alex_pa_diversity()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType       = HPSDRHW::HermesLite;
        info.protocol        = ProtocolVersion::Protocol1;
        info.macAddress      = QStringLiteral("aa:bb:cc:11:22:33");
        info.firmwareVersion = 72;
        page.onCurrentRadioChanged(info);

        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::RadioInfo));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::AntennaAlex));
        // OC Outputs tab is visible for HL2 (relabeled "Hermes Lite Control")
        // because the I/O board pattern reuses OcMatrix for N2ADR Filter
        // pin assignments — mi0bot setup.cs:20232 [v2.10.3.13-beta2].
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::OcOutputs));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Xvtr));
        // Setup → Hardware → PureSignal tab retired in Phase 3M-4 Task 14;
        // no Tab::PureSignal enum value to assert against any more.
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Diversity));
        // Calibration tab is always visible after IA reshape Phase 5 —
        // PA-specific groups moved to PA → Watt Meter, so the per-board
        // hasPaProfile gate at the parent-tab level was dropped.
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Calibration));
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Hl2IoBoard));
        // R-R3-49: the Bandwidth Monitor tab is hidden until it is built,
        // even on a board that has the monitor (next case).
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::BandwidthMonitor));
        // HL2 Options tab is a new Phase 3L surface — also gated on
        // caps.hasIoBoardHl2 like the existing HL2 I/O Board tab.
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Hl2Options));
    }

    // R-R3-49: once the Bandwidth Monitor is built, HL2 shows its tab again
    // (the board gate still applies; Angelia below does not show it).
    void hl2_shows_bandwidth_monitor_once_built()
    {
        UnbuiltFeatures::setBuiltForTest(UnbuiltFeature::BandwidthMonitor, true);
        const auto reset = qScopeGuard([] { UnbuiltFeatures::resetForTest(); });
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType  = HPSDRHW::HermesLite;
        info.protocol   = ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
        page.onCurrentRadioChanged(info);

        QVERIFY(page.isTabVisibleForTest(HardwarePage::Tab::BandwidthMonitor));
    }

    // Hermes Lite Control relabel — mi0bot setup.cs:20232 [v2.10.3.13-beta2]
    //   tpPennyCtrl.Text = "Hermes Lite Control";
    void hl2_oc_outputs_tab_relabeled_to_hermes_lite_control()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::HermesLite);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType  = HPSDRHW::HermesLite;
        info.protocol   = ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("aa:bb:cc:11:22:33");
        page.onCurrentRadioChanged(info);

        QCOMPARE(page.tabTextForTest(HardwarePage::Tab::OcOutputs),
                 QStringLiteral("Hermes Lite Control"));
    }

    // Non-HL2 boards retain the standard "OC Outputs" title.
    void angelia_oc_outputs_tab_keeps_standard_title()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Angelia);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType  = HPSDRHW::Angelia;
        info.protocol   = ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("aa:bb:cc:44:55:66");
        page.onCurrentRadioChanged(info);

        QCOMPARE(page.tabTextForTest(HardwarePage::Tab::OcOutputs),
                 QStringLiteral("OC Outputs"));
    }

    void angelia_shows_alex_pa_diversity()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Angelia);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType  = HPSDRHW::Angelia;
        info.protocol   = ProtocolVersion::Protocol1;
        info.macAddress = QStringLiteral("aa:bb:cc:44:55:66");
        page.onCurrentRadioChanged(info);

        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::AntennaAlex));
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::OcOutputs));
        // Setup → Hardware → PureSignal tab retired in Phase 3M-4 Task 14;
        // PsForm at Tools > PureSignal is the entire PS control surface.
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Diversity));
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Calibration));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Hl2IoBoard));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::BandwidthMonitor));
        // Non-HL2 boards must NOT see the HL2 Options tab.
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Hl2Options));
    }

    void atlas_shows_only_radio_info()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Atlas);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType = HPSDRHW::Atlas;
        info.protocol  = ProtocolVersion::Protocol1;
        page.onCurrentRadioChanged(info);

        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::RadioInfo));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::AntennaAlex));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::OcOutputs));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Xvtr));
        // Setup → Hardware → PureSignal tab retired in Phase 3M-4 Task 14.
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Diversity));
        // Calibration tab is always visible after IA reshape Phase 5 —
        // PA-specific groups moved to PA → Watt Meter, so the per-board
        // hasPaProfile gate at the parent-tab level was dropped. Atlas
        // shows the freq/level/HPSDR/TX-display/Volts-Amps Cal groups now.
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Calibration));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Hl2Options));
    }

    void saturn_shows_nearly_all_tabs()
    {
        RadioModel model;
        model.setBoardForTest(HPSDRHW::Saturn);
        HardwarePage page(&model);

        RadioInfo info;
        info.boardType = HPSDRHW::Saturn;
        info.protocol  = ProtocolVersion::Protocol2;
        page.onCurrentRadioChanged(info);

        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::AntennaAlex));
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::OcOutputs));
        // Setup → Hardware → PureSignal tab retired in Phase 3M-4 Task 14.
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Diversity));
        QVERIFY( page.isTabVisibleForTest(HardwarePage::Tab::Calibration));
        QVERIFY(!page.isTabVisibleForTest(HardwarePage::Tab::Hl2IoBoard));
    }

    // R-R3-46: in a remote window the Antenna Control grid shows the
    // Core's antennas (its `alexAntennas` object) and a receive edit goes
    // to that object, never to the window's own AlexController. A refused
    // edit shows the Core's value again.
    void remote_antenna_control_shows_and_writes_the_cores_antennas()
    {
        RadioModel remote(RadioModel::Role::Remote);
        StationCapabilities caps;
        caps.macAddress = QStringLiteral("AA:BB:CC:DD:EE:51");
        caps.board = HPSDRHW::Angelia;
        caps.radioConnected = true;
        caps.radioIdentityEntries = true;
        caps.hpsdrModel = HPSDRModel::ANAN100D;
        caps.radioProtocol = 1;
        remote.applyStationCapabilities(caps);

        AlexAntennaFacade* core = remote.alexAntennaFacade();
        QVERIFY(!core->isBound());
        core->setRxAntennas(QStringLiteral("1,1,1,2,1,1,1,1,1,1,1,1,1,1"));  // 40m on 2
        QVERIFY(core->applyRemoteProperty("txAntennas",
                                          QStringLiteral("1,1,1,1,1,3,1,1,1,1,1,1,1,1")));
        QVERIFY(core->applyRemoteProperty("blockTxAnt2", true));
        core->setWindowAvailability(true, {});  // the Core offers Hardware Config

        HardwarePage page(&remote);
        auto* ctl = page.findChild<AntennaAlexAntennaControlTab*>();
        QVERIFY(ctl != nullptr);
        QVERIFY(ctl->rxButtonForTest(Band::Band40m, 2)->isChecked());
        QVERIFY(ctl->txButtonForTest(Band::Band20m, 3)->isChecked());
        QVERIFY(ctl->blockTxAnt2ForTest()->isChecked());

        ctl->rxButtonForTest(Band::Band40m, 3)->click();
        QCOMPARE(core->rxAnt(Band::Band40m), 3);
        QCOMPARE(remote.alexController().rxAnt(Band::Band40m), 1);  // not the window's own

        core->setRxAntennas(QStringLiteral("1,2,1,3,1,1,1,1,1,1,1,1,1,1"));  // from the Core
        QVERIFY(ctl->rxButtonForTest(Band::Band80m, 2)->isChecked());

        core->setEditGate([](QString* reason) {
            if (reason) { *reason = QStringLiteral("Connect to the Core to change the radio's hardware settings."); }
            return false;
        });
        ctl->rxButtonForTest(Band::Band40m, 1)->click();
        QCOMPARE(core->rxAnt(Band::Band40m), 3);
        QVERIFY(ctl->rxButtonForTest(Band::Band40m, 3)->isChecked());
        ctl->useTxAntForRxForTest()->click();
        QVERIFY(!core->useTxAntennaForRx());
        QVERIFY(!ctl->useTxAntForRxForTest()->isChecked());
    }
};

QTEST_MAIN(TestHardwarePageGating)
#include "tst_hardware_page_capability_gating.moc"
