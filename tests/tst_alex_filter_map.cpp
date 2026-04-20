#include <QtTest/QtTest>
#include "core/codec/AlexFilterMap.h"

using namespace NereusSDR::codec::alex;

class TestAlexFilterMap : public QObject {
    Q_OBJECT
private slots:
    // From Thetis console.cs:6830-6942 [@501e3f5]
    void hpfBypass_under_1_5MHz()       { QCOMPARE(computeHpf(1.0),  quint8(0x20)); }
    void hpf_1_5_to_6_5_MHz()           { QCOMPARE(computeHpf(3.5),  quint8(0x10)); }
    void hpf_6_5_to_9_5_MHz()           { QCOMPARE(computeHpf(7.0),  quint8(0x08)); }
    void hpf_9_5_to_13_MHz()            { QCOMPARE(computeHpf(10.0), quint8(0x04)); }
    void hpf_13_to_20_MHz()             { QCOMPARE(computeHpf(14.1), quint8(0x01)); }
    void hpf_20_to_50_MHz()             { QCOMPARE(computeHpf(28.0), quint8(0x02)); }
    void hpf_6m_preamp_50_MHz_and_up()  { QCOMPARE(computeHpf(50.0), quint8(0x40)); }

    // From Thetis console.cs:7168-7234 [@501e3f5]
    void lpf_160m_under_2MHz()  { QCOMPARE(computeLpf(1.9),   quint8(0x08)); }
    void lpf_80m_2_to_4_MHz()   { QCOMPARE(computeLpf(3.8),   quint8(0x04)); }
    void lpf_60_40m()           { QCOMPARE(computeLpf(7.1),   quint8(0x02)); }
    void lpf_30_20m()           { QCOMPARE(computeLpf(14.1),  quint8(0x01)); }
    void lpf_17_15m()           { QCOMPARE(computeLpf(21.0),  quint8(0x40)); }
    void lpf_12_10m()           { QCOMPARE(computeLpf(28.0),  quint8(0x20)); }
    void lpf_6m_29_7_and_up()   { QCOMPARE(computeLpf(50.0),  quint8(0x10)); }

    // Boundary edges — values exactly on the breakpoint go to the upper band
    void hpf_edge_1_5_MHz_exact()  { QCOMPARE(computeHpf(1.5),  quint8(0x10)); }
    void hpf_edge_50_MHz_exact()   { QCOMPARE(computeHpf(50.0), quint8(0x40)); }
    void lpf_edge_2_0_MHz_exact()  { QCOMPARE(computeLpf(2.0),  quint8(0x04)); }
    void lpf_edge_29_7_MHz_exact() { QCOMPARE(computeLpf(29.7), quint8(0x10)); }
};

QTEST_APPLESS_MAIN(TestAlexFilterMap)
#include "tst_alex_filter_map.moc"
