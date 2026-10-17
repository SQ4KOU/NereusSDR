// =================================================================
// tests/tst_panadapter_applet_slice_assoc.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Phase 3F Sub-Epic D Task 1: PanadapterApplet skeleton + slice assoc.
// =================================================================
#include <QtTest/QtTest>
#include "core/AppSettings.h"
#include "core/BoardCapabilities.h"
#include "core/DdcAssignment.h"
#include "core/P2RadioConnection.h"
#include "gui/PanadapterApplet.h"
#include "gui/PanadapterStack.h"
#include "gui/MainWindow.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

class TestPanadapterAppletSliceAssoc : public QObject {
    Q_OBJECT
private slots:
    void init() { AppSettings::instance().clear(); }

    void applet_constructs_with_pan_id()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        QCOMPARE(applet.panId(), QStringLiteral("pan-0"));
    }

    void add_slice_makes_it_active_when_no_active_yet()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        QCOMPARE(applet.activeSliceIndex(), -1);
        applet.addSlice(2);
        QCOMPARE(applet.activeSliceIndex(), 2);
    }

    void add_second_slice_does_not_change_active()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        applet.addSlice(0);
        applet.addSlice(1);
        QCOMPARE(applet.activeSliceIndex(), 0);
        QCOMPARE(applet.associatedSlices().size(), 2);
    }

    void remove_active_slice_promotes_another()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        applet.addSlice(0);
        applet.addSlice(1);
        applet.removeSlice(0);
        QCOMPARE(applet.activeSliceIndex(), 1);
    }

    void remove_last_slice_sets_active_to_minus_1()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        applet.addSlice(0);
        applet.removeSlice(0);
        QCOMPARE(applet.activeSliceIndex(), -1);
    }

    void persisted_extended_false_is_applied_to_the_spectrum_widget()
    {
        AppSettings::instance().setValue(
            QStringLiteral("Pan_pan-1_ExtendedView"),
            QStringLiteral("False"));

        PanadapterApplet applet(QStringLiteral("pan-1"));
        QVERIFY(!applet.extendedViewEnabled());
        QVERIFY(!applet.spectrumWidget()->extendedMode());
    }

    void forced_off_stays_off_after_wide_zoom()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        SpectrumWidget* const spectrum = applet.spectrumWidget();
        spectrum->setSampleRate(192000.0);
        spectrum->setFrequencyRange(14.2e6, 5000000.0);
        QVERIFY(spectrum->extendedMode());

        applet.setExtendedViewEnabled(false);
        QVERIFY(!spectrum->extendedMode());

        spectrum->setFrequencyRange(14.2e6, 4000000.0);
        QVERIFY(!spectrum->extendedMode());
    }

    void allowed_at_normal_zoom_stays_in_normal_mode()
    {
        PanadapterApplet applet(QStringLiteral("pan-0"));
        SpectrumWidget* const spectrum = applet.spectrumWidget();
        applet.setExtendedViewEnabled(false);
        spectrum->setSampleRate(192000.0);
        spectrum->setFrequencyRange(14.2e6, 96000.0);

        applet.setExtendedViewEnabled(true);
        QVERIFY(!spectrum->extendedMode());
    }

    void spectrum_settings_round_trip_in_separate_pan_namespaces()
    {
        auto& settings = AppSettings::instance();
        settings.setValue(QStringLiteral("DisplayBandwidth"), QStringLiteral("110000"));
        settings.setValue(QStringLiteral("DisplayBandwidth_1"), QStringLiteral("220000"));

        {
            PanadapterApplet pan0(QStringLiteral("pan-0"));
            PanadapterApplet pan1(QStringLiteral("pan-1"));
            MainWindow::configureSpectrumForPanForTest(pan0.spectrumWidget(),
                                                       pan0.panId());
            MainWindow::configureSpectrumForPanForTest(pan1.spectrumWidget(),
                                                       pan1.panId());
            QCOMPARE(pan0.spectrumWidget()->bandwidth(), 110000.0);
            QCOMPARE(pan1.spectrumWidget()->bandwidth(), 220000.0);

            pan0.spectrumWidget()->setFrequencyRange(14.2e6, 130000.0);
            pan1.spectrumWidget()->setFrequencyRange(7.1e6, 260000.0);
            pan0.spectrumWidget()->saveSettings();
            pan1.spectrumWidget()->saveSettings();
        }

        PanadapterApplet restored0(QStringLiteral("pan-0"));
        PanadapterApplet restored1(QStringLiteral("pan-1"));
        MainWindow::configureSpectrumForPanForTest(restored0.spectrumWidget(),
                                                   restored0.panId());
        MainWindow::configureSpectrumForPanForTest(restored1.spectrumWidget(),
                                                   restored1.panId());
        QCOMPARE(restored0.spectrumWidget()->bandwidth(), 130000.0);
        QCOMPARE(restored1.spectrumWidget()->bandwidth(), 260000.0);
    }

    void pan_one_wideband_toggle_changes_only_pan_ones_slice()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(5, 5, 192000);
        SliceModel* first = model.sliceById(model.addSlice(QStringLiteral("pan-0")));
        SliceModel* second = model.sliceById(model.addSlice(QStringLiteral("pan-1")));
        QVERIFY(first);
        QVERIFY(second);

        PanadapterStack stack;
        PanadapterApplet* pan0 = stack.panadapter(QStringLiteral("pan-0"));
        PanadapterApplet* pan1 = stack.addPanadapter(QStringLiteral("pan-1"));
        QVERIFY(pan0);
        QVERIFY(pan1);
        pan0->addSlice(first->sliceIndex());
        pan1->addSlice(second->sliceIndex());

        MainWindow::wireWidebandExtensionForTest(
            pan1->spectrumWidget(), &model, &stack, pan1->panId());
        pan1->spectrumWidget()->setSampleRate(192000.0);
        pan1->spectrumWidget()->setFrequencyRange(14.2e6, 5000000.0);

        QVERIFY(second->widebandExtensionRequested());
        QVERIFY(!first->widebandExtensionRequested());
    }

    void local_wide_zoom_respects_radio_capability_data()
    {
        QTest::addColumn<int>("modelValue");
        QTest::addColumn<int>("protocolValue");
        QTest::addColumn<bool>("available");
        QTest::newRow("hl2-p1") << int(HPSDRModel::HERMESLITE)
            << int(ProtocolVersion::Protocol1) << false;
        QTest::newRow("g2-row-p1") << int(HPSDRModel::ANAN_G2)
            << int(ProtocolVersion::Protocol1) << false;
        QTest::newRow("g2-p2") << int(HPSDRModel::ANAN_G2)
            << int(ProtocolVersion::Protocol2) << true;
    }

    void local_wide_zoom_respects_radio_capability()
    {
        QFETCH(int, modelValue);
        QFETCH(int, protocolValue);
        QFETCH(bool, available);
        RadioModel model;
        model.setHpsdrModelForTest(static_cast<HPSDRModel>(modelValue));
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = static_cast<ProtocolVersion>(protocolValue);
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(slice->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        spectrum->setDisplayWindowClamped(14.2e6, 1920000.0);
        QCOMPARE(spectrum->bandwidth(), available ? 1920000.0 : 192000.0);
        QCOMPARE(spectrum->extendedMode(), available);
        QCOMPARE(slice->widebandExtensionRequested(), available);
        QVERIFY(pan->extendedViewEnabled()); // Permission is still the operator's.
    }

    void extended_request_follows_the_pans_active_slice()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* first = model.sliceById(model.addSlice());
        SliceModel* second = model.sliceById(model.addSlice());
        QVERIFY(first && second);
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(first->sliceIndex());
        pan->addSlice(second->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        spectrum->setFrequencyRange(14.2e6, 1920000.0);
        QVERIFY(first->widebandExtensionRequested());
        QVERIFY(!second->widebandExtensionRequested());

        pan->setActiveSliceIndex(second->sliceIndex());
        QVERIFY(!first->widebandExtensionRequested());
        QVERIFY(second->widebandExtensionRequested());
        // Removal promotes the remaining slice without another zoom edge.
        pan->removeSlice(second->sliceIndex());
        QVERIFY(first->widebandExtensionRequested());
        QVERIFY(!second->widebandExtensionRequested());
        pan->removeSlice(first->sliceIndex());
        QVERIFY(!first->widebandExtensionRequested());
        QVERIFY(!spectrum->extendedMode());
    }

    void request_handoff_moves_capture_without_retiring_an_endpoint_owner()
    {
        P2RadioConnection connection;
        connection.setBoardForTest(HPSDRHW::Saturn);
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        model.injectConnectionForTest(&connection);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* first = model.sliceById(model.addSlice());
        SliceModel* second = model.sliceById(model.addSlice());
        QVERIFY(first && second);
        second->setFrequency(7100000.0);
        QVERIFY(first->streamIndex() != second->streamIndex());
        // Explicit test routing, not a new radio capability or DDC policy.
        DdcAssignment assignment{};
        assignment.streamDdc[first->streamIndex()] = 2;
        assignment.streamDdc[second->streamIndex()] = 3;
        assignment.rate[2] = assignment.rate[3] = 192000;
        assignment.ddcEnable = 0x0c;
        assignment.adcCtrl1 = (1 << 6); // DDC2 -> ADC0, DDC3 -> ADC1.
        model.publishDdcAssignmentForTest(assignment);
        QCOMPARE(model.sliceAdcIndex(first->sliceIndex()), 0);
        QCOMPARE(model.sliceAdcIndex(second->sliceIndex()), 1);
        // From Thetis ChannelMaster/network.c:879 [v2.10.3.15]:
        // packetbuf[23] = (char)_InterlockedAnd(&prn->wb_enable, 0xff);
        constexpr int kWidebandEnableByte = 23;
        const auto captureMask = [&connection]() {
            quint8 packet[60] = {};
            connection.composeCmdGeneralForTest(packet);
            return packet[kWidebandEnableByte];
        };
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(first->sliceIndex());
        pan->addSlice(second->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        spectrum->setFrequencyRange(14.2e6, 1920000.0);
        QCOMPARE(captureMask(), quint8(0x01));
        pan->setActiveSliceIndex(second->sliceIndex());
        QCOMPARE(captureMask(), quint8(0x02));

        const auto endpoint = model.acquireWidebandDemand(first->sliceIndex());
        QVERIFY(endpoint != 0);
        QVERIFY(model.setWidebandDemandActive(endpoint, true));
        QCOMPARE(captureMask(), quint8(0x03));
        pan->setExtendedViewEnabled(false);
        QCOMPARE(captureMask(), quint8(0x01));
        model.releaseWidebandDemand(endpoint);
        QCOMPARE(captureMask(), quint8(0));
    }

    void routing_to_an_unavailable_adc_releases_the_local_request()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        // Synthetic capability subset to exercise per-ADC eligibility, not
        // a hardware-table change: two inputs, only ADC0 supplies wideband.
        model.setWidebandTopologyForTest(2, 1, 1);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        DdcAssignment assignment{};
        assignment.streamDdc[slice->streamIndex()] = 0;
        assignment.rate[0] = 192000;
        assignment.ddcEnable = 0x01;
        model.publishDdcAssignmentForTest(assignment);
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(slice->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        spectrum->setFrequencyRange(14.2e6, 1920000.0);
        QVERIFY(slice->widebandExtensionRequested());
        assignment.adcCtrl1 = 1; // DDC0 -> ADC1.
        model.publishDdcAssignmentForTest(assignment);
        QCOMPARE(model.sliceAdcIndex(slice->sliceIndex()), 1);
        QCOMPARE(spectrum->bandwidth(), 192000.0);
        QVERIFY(!spectrum->extendedMode());
        QVERIFY(!slice->widebandExtensionRequested());
    }

    void retiring_a_pan_releases_only_its_slice_request()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* owned = model.sliceById(model.addSlice());
        SliceModel* unrelated = model.sliceById(model.addSlice());
        QVERIFY(owned && unrelated);
        unrelated->setWidebandExtensionRequested(true);
        {
            PanadapterStack stack;
            PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
            pan->addSlice(owned->sliceIndex());
            SpectrumWidget* spectrum = pan->spectrumWidget();
            spectrum->setSampleRate(192000.0);
            MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
            spectrum->setFrequencyRange(14.2e6, 1920000.0);
            QVERIFY(owned->widebandExtensionRequested());
        }
        QVERIFY(!owned->widebandExtensionRequested());
        QVERIFY(unrelated->widebandExtensionRequested());
    }

    void capability_loss_clamps_an_existing_wide_view()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(slice->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        spectrum->setFrequencyRange(14.2e6, 1920000.0);
        QVERIFY(slice->widebandExtensionRequested());
        info.protocol = ProtocolVersion::Protocol1;
        model.setLastRadioInfoForTest(info);
        model.emitCurrentRadioChangedForTest();
        QCOMPARE(spectrum->bandwidth(), 192000.0);
        QVERIFY(!spectrum->extendedMode());
        QVERIFY(!slice->widebandExtensionRequested());
        QVERIFY(pan->extendedViewEnabled());
    }

    void empty_startup_bridge_preserves_restored_span_until_discovery_data()
    {
        QTest::addColumn<int>("modelValue");
        QTest::addColumn<int>("protocolValue");
        QTest::addColumn<bool>("eligible");
        QTest::addColumn<bool>("poolFirst");
        QTest::newRow("g2-p2-deferred-binding") << int(HPSDRModel::ANAN_G2)
            << int(ProtocolVersion::Protocol2) << true << false;
        QTest::newRow("g2-p2-binding-before-association") << int(HPSDRModel::ANAN_G2)
            << int(ProtocolVersion::Protocol2) << true << true;
        QTest::newRow("hl2-p1-known-unsupported") << int(HPSDRModel::HERMESLITE)
            << int(ProtocolVersion::Protocol1) << false << false;
    }

    void empty_startup_bridge_preserves_restored_span_until_discovery()
    {
        QFETCH(int, modelValue);
        QFETCH(int, protocolValue);
        QFETCH(bool, eligible);
        QFETCH(bool, poolFirst);
        AppSettings::instance().setValue(QStringLiteral("DisplayBandwidth"),
                                         QStringLiteral("5000000"));
        RadioModel model;
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        QVERIFY(pan);
        SpectrumWidget* spectrum = pan->spectrumWidget();
        // Production order: restore the window before installing its bridge,
        // with no radio descriptor or slices; only then does discovery bind.
        MainWindow::configureSpectrumForPanForTest(spectrum, pan->panId());
        QCOMPARE(spectrum->bandwidth(), 5000000.0);
        QVERIFY(pan->associatedSlices().isEmpty());
        QCOMPARE(model.currentRadioInfo().boardType, HPSDRHW::Unknown);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        QCOMPARE(spectrum->bandwidth(), 5000000.0);

        model.setHpsdrModelForTest(static_cast<HPSDRModel>(modelValue));
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = static_cast<ProtocolVersion>(protocolValue);
        model.setLastRadioInfoForTest(info);
        if (poolFirst) { model.configureStreamPool(4, 4, 192000); }
        // connectToRadioImpl publishes Slice A before it configures the pool.
        // MainWindow associates that still-unbound object with the pan.
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        QCOMPARE(slice->streamIndex() < 0, !poolFirst);
        pan->addSlice(slice->sliceIndex());
        const double expectedSpan = eligible ? 5000000.0 : spectrum->sampleRate();
        QCOMPARE(spectrum->bandwidth(), expectedSpan);
        QCOMPARE(slice->widebandExtensionRequested(), eligible && poolFirst);
        if (!poolFirst) {
            model.configureStreamPool(4, 4, 192000);
            model.bindUnboundSlices();
        }
        QVERIFY(slice->streamIndex() >= 0);
        QVERIFY(model.sliceAdcIndex(slice->sliceIndex()) >= 0);
        model.emitCurrentRadioChangedForTest();
        QCOMPARE(spectrum->bandwidth(), expectedSpan);
        QCOMPARE(spectrum->extendedMode(), eligible);
        QCOMPARE(slice->widebandExtensionRequested(), eligible);
        // Once it has a descriptor, an emptied pan still retires its intent.
        pan->removeSlice(slice->sliceIndex());
        QVERIFY(!slice->widebandExtensionRequested());
        QVERIFY(!spectrum->extendedMode());
    }

    void discovery_keeps_a_supported_restored_span()
    {
        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        model.configureStreamPool(4, 4, 192000);
        SliceModel* slice = model.sliceById(model.addSlice());
        QVERIFY(slice);
        PanadapterStack stack;
        PanadapterApplet* pan = stack.panadapter(QStringLiteral("pan-0"));
        pan->addSlice(slice->sliceIndex());
        SpectrumWidget* spectrum = pan->spectrumWidget();
        spectrum->setSampleRate(192000.0);
        spectrum->setFrequencyRange(14.2e6, 1920000.0);
        MainWindow::wireWidebandExtensionForTest(spectrum, &model, &stack, pan->panId());
        QCOMPARE(spectrum->bandwidth(), 1920000.0);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.emitCurrentRadioChangedForTest();
        QCOMPARE(spectrum->bandwidth(), 1920000.0);
        QVERIFY(spectrum->extendedMode());
        QVERIFY(slice->widebandExtensionRequested());
    }

    void restored_wide_zoom_seeds_slice_when_bridge_is_installed()
    {
        AppSettings::instance().setValue(
            QStringLiteral("DisplayBandwidth"),
            QStringLiteral("5000000"));

        RadioModel model;
        model.setHpsdrModelForTest(HPSDRModel::ANAN_G2);
        RadioInfo info;
        info.boardType = model.boardCapabilities().board;
        info.protocol = ProtocolVersion::Protocol2;
        model.setLastRadioInfoForTest(info);
        model.configureStreamPool(5, 5, 192000);
        SliceModel* const slice =
            model.sliceById(model.addSlice(QStringLiteral("pan-0")));
        QVERIFY(slice);

        PanadapterStack stack;
        PanadapterApplet* const pan = stack.panadapter(QStringLiteral("pan-0"));
        QVERIFY(pan);
        pan->addSlice(slice->sliceIndex());
        MainWindow::configureSpectrumForPanForTest(
            pan->spectrumWidget(), pan->panId());
        pan->spectrumWidget()->setSampleRate(192000.0);
        QVERIFY(pan->spectrumWidget()->extendedMode());
        QVERIFY(!slice->widebandExtensionRequested());

        MainWindow::wireWidebandExtensionForTest(
            pan->spectrumWidget(), &model, &stack, pan->panId());

        QVERIFY(slice->widebandExtensionRequested());
    }
};

QTEST_MAIN(TestPanadapterAppletSliceAssoc)
#include "tst_panadapter_applet_slice_assoc.moc"
