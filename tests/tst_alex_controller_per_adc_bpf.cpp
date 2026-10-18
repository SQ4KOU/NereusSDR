// =================================================================
// tests/tst_alex_controller_per_adc_bpf.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Phase 3F Sub-Epic B Task 11+: AlexController per-ADC BPF state machine
// per docs/architecture/2026-05-26-phase3f-multi-pan-multi-slice-design.md §4.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>
#include "core/accessories/AlexController.h"

using namespace NereusSDR;

class TestAlexControllerPerAdcBpf : public QObject {
    Q_OBJECT
private slots:
    void alex_adc_state_struct_exists()
    {
        AlexController::AlexAdcState s{};
        s.mode = AlexController::BpfMode::Auto;
        s.effective = AlexController::BpfEffective::Filtered;
        s.currentBpfBand = Band::Band20m;
        QCOMPARE(s.mode, AlexController::BpfMode::Auto);
    }

    void default_bpf_mode_is_auto_per_adc()
    {
        AlexController alex;
        QCOMPARE(alex.bpfMode(0), AlexController::BpfMode::Auto);
        QCOMPARE(alex.bpfMode(1), AlexController::BpfMode::Auto);
    }

    void set_bpf_mode_round_trips_per_adc()
    {
        AlexController alex;
        alex.setBpfMode(0, AlexController::BpfMode::ForceBypass);
        QCOMPARE(alex.bpfMode(0), AlexController::BpfMode::ForceBypass);
        QCOMPARE(alex.bpfMode(1), AlexController::BpfMode::Auto);  // ADC1 unaffected
    }

    void set_bpf_mode_emits_state_changed_signal()
    {
        AlexController alex;
        QSignalSpy spy(&alex, &AlexController::bpfStateChanged);
        alex.setBpfMode(0, AlexController::BpfMode::ForceBypass);
        QVERIFY(spy.count() >= 1);
    }

    void wideband_active_overrides_force_band()
    {
        AlexController alex;
        alex.setBpfMode(0, AlexController::BpfMode::ForceBand);
        alex.setWidebandActive(0, true);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::WidebandLocked);
    }

    void auto_mode_single_band_filters_to_that_band()
    {
        AlexController alex;
        std::array<Band, 5> slicesOnAdc {Band::Band20m, Band::Count, Band::Count, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, slicesOnAdc);
        alex.recomputeBpf(0);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(alex.adcState(0).currentBpfBand, Band::Band20m);
    }

    void auto_mode_multi_band_bypasses()
    {
        AlexController alex;
        std::array<Band, 5> slicesOnAdc {Band::Band20m, Band::Band40m, Band::Count, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, slicesOnAdc);
        alex.recomputeBpf(0);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Bypass);
        QVERIFY(alex.adcState(0).reasonText.contains("multi-band"));
    }

    // The entries are filter selections, so two are a difference even when
    // their bands share a name: WWV on 5 and on 15 MHz sit in different
    // filters. Each band is named once.
    void auto_mode_two_selections_under_one_band_name_bypass()
    {
        AlexController alex;
        std::array<Band, 5> slicesOnAdc {Band::WWV, Band::WWV, Band::Count, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, slicesOnAdc);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("BYPASS (multi-band: WWV)"));

        // Where the filters combine, both are selected and the chain filters.
        alex.setBandFiltersCombine(0, true);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("WWV"));
    }

    // A radio whose band filters combine (codec::alex::combinesRxBandFilters):
    // slices on more than one band leave the chain filtered, every band
    // named.
    void auto_mode_multi_band_stays_filtered_where_the_filters_combine()
    {
        AlexController alex;
        alex.setBandFiltersCombine(0, true);
        QVERIFY(alex.bandFiltersCombine(0));
        QVERIFY(!alex.bandFiltersCombine(1));
        std::array<Band, 5> slicesOnAdc {Band::Band20m, Band::Band40m, Band::Band10m, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, slicesOnAdc);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("40m + 20m + 10m"));

        // The other chain was not told so, and bypasses.
        alex.notifySlicesOnAdc(1, slicesOnAdc);
        QCOMPARE(alex.adcState(1).effective, AlexController::BpfEffective::Bypass);

        // The operator's bypass and a wideband view bypass as on any radio.
        alex.setBpfMode(0, AlexController::BpfMode::ForceBypass);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Bypass);
        alex.setBpfMode(0, AlexController::BpfMode::Auto);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);
        alex.setWidebandActive(0, true);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::WidebandLocked);
        alex.setWidebandActive(0, false);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);

        // Switched off, the same slices bypass, and the change is announced.
        QSignalSpy changed(&alex, &AlexController::bpfStateChanged);
        alex.setBandFiltersCombine(0, false);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Bypass);
        QCOMPARE(changed.count(), 1);
    }

    // What the combined selection costs the chain's slices follows the bands
    // (setCombineNote), and only while Auto holds more than one selection
    // where the filters combine.
    void combine_note_follows_the_bands()
    {
        AlexController alex;
        alex.setBandFiltersCombine(0, true);
        const std::array<Band, 5> two {Band::Band40m, Band::Band6m, Band::Count, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, two);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("40m + 6m"));

        QSignalSpy changed(&alex, &AlexController::bpfStateChanged);
        const QString note = QStringLiteral("6m LNA in: HF reads low");
        alex.setCombineNote(0, note);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(alex.adcState(0).effective, AlexController::BpfEffective::Filtered);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("40m + 6m (6m LNA in: HF reads low)"));
        alex.setCombineNote(0, note);
        QCOMPARE(changed.count(), 1);   // the same note again says nothing
        QCOMPARE(alex.adcState(1).reasonText.contains(QStringLiteral("LNA")), false);

        // Not over the operator's bypass, a wideband view or one selection.
        alex.setBpfMode(0, AlexController::BpfMode::ForceBypass);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("BYPASS (operator override)"));
        alex.setBpfMode(0, AlexController::BpfMode::Auto);
        alex.setWidebandActive(0, true);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("BYPASS (wideband active)"));
        alex.setWidebandActive(0, false);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("40m + 6m (6m LNA in: HF reads low)"));
        const std::array<Band, 5> one {Band::Band6m, Band::Count, Band::Count, Band::Count, Band::Count};
        alex.notifySlicesOnAdc(0, one);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("6m"));

        // Nor where the filters do not combine.
        alex.notifySlicesOnAdc(0, two);
        alex.setBandFiltersCombine(0, false);
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("BYPASS (multi-band: 40m + 6m)"));

        alex.setBandFiltersCombine(0, true);
        alex.setCombineNote(0, QString());
        QCOMPARE(alex.adcState(0).reasonText, QStringLiteral("40m + 6m"));
    }

    void bpf_mode_persists_per_adc_per_mac()
    {
        const QString testMac = QStringLiteral("00:11:22:33:44:55");
        {
            AlexController alex;
            alex.setMacAddress(testMac);
            alex.setBpfMode(0, AlexController::BpfMode::ForceBypass);
            alex.setBpfMode(1, AlexController::BpfMode::ForceBand);
            alex.save();
        }
        {
            AlexController alex2;
            alex2.setMacAddress(testMac);
            alex2.load();
            QCOMPARE(alex2.bpfMode(0), AlexController::BpfMode::ForceBypass);
            QCOMPARE(alex2.bpfMode(1), AlexController::BpfMode::ForceBand);
        }
    }
};

QTEST_MAIN(TestAlexControllerPerAdcBpf)
#include "tst_alex_controller_per_adc_bpf.moc"
