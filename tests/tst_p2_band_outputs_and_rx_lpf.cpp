// no-port-check: test-only. Thetis file names appear only in source-cite
// comments that document which upstream line each assertion verifies.
// No Thetis logic is ported here; this file is NereusSDR-original.
//
// =================================================================
// Protocol 2: the band outputs reach the wire, and the receive low-pass
// follows RX1 as Thetis chooses it.
// =================================================================
//
// Plan Task 14 (3M-1 transmit, Phase 3F section 16.3.2, R-R3-49).
//
// 1. Byte 1401 of the high-priority packet carries the OC outputs:
//      From Thetis ChannelMaster/network.c:1030-1031 [v2.10.3.15]
//        // Open Collector Outputs
//        packetbuf[1401] = (prn->oc_output << 1) & 0xfe;
//    No NereusSDR Protocol 2 codec wrote it, so an ANAN-G2 sent no band
//    data to an amplifier or band decoder at all. The band rule is the one
//    Penny.cs applies on every protocol: the transmitting VFO's band while
//    keyed, VFO A's while not (Penny.cs:174-177 [v2.10.3.15]), by VFO
//    frequency (console.cs:29101-29102 [v2.10.3.15]).
//
// 2. The receive low-pass (Alex0 while unkeyed) is RX1's, or the higher of
//    RX1 and RX2 when RX2 shares the front end:
//      From Thetis console.cs:15487-15498 [v2.10.3.15]
//        private void UpdateAlexTXFilter()
//        { if (!_mox) {
//            if (!_rx2_preamp_present && chkRX2.Checked)
//            { if (rx1_dds_freq_mhz > rx2_dds_freq_mhz) setAlexLPF(rx1_dds_freq_mhz, false);
//              else setAlexLPF(rx2_dds_freq_mhz, false); }
//            else setAlexLPF(rx1_dds_freq_mhz, false); } }
//    NereusSDR used whichever DDC was retuned last, so adding slice B on a
//    lower band put slice A behind B's low-pass.
// =================================================================

#include <QtTest/QtTest>

#include "core/AppSettings.h"
#include "core/OcMatrix.h"
#include "core/P2RadioConnection.h"
#include "core/ReceiverManager.h"
#include "core/TxSliceArbiter.h"
#include "core/codec/P2CodecSaturn.h"
#include "models/Band.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;

namespace {

constexpr double k20mHz = 14200000.0;
constexpr double k40mHz =  7100000.0;

constexpr int kOcByte = 1401;

class ConnectedP2 final : public P2RadioConnection {
public:
    ConnectedP2() { setState(ConnectionState::Connected); }
};

struct DetachConnection {
    RadioModel* model{nullptr};
    ~DetachConnection() { if (model) { model->injectConnectionForTest(nullptr); } }
};

quint8 highPriorityByte(P2RadioConnection& conn, int offset)
{
    quint8 buf[1444] = {};
    conn.composeCmdHighPriorityForTest(buf);
    return buf[offset];
}

// The wire form of an OC mask, network.c:1031.
quint8 wireOc(quint8 mask)
{
    return quint8((mask << 1) & 0xfe);
}

// An ANAN-G2 (Saturn board, P2CodecSaturn: stream 0 -> DDC2, stream 1 ->
// DDC3, ...) with an injected, Connected connection and the production
// ReceiverManager -> connection wiring.
struct G2Session {
    G2Session()
    {
        AppSettings::instance().clear();
        oc.setPin(Band::Band20m, 0, /*tx=*/false, true);
        oc.setPin(Band::Band40m, 1, /*tx=*/false, true);
        oc.setPin(Band::Band40m, 2, /*tx=*/true,  true);
        oc.setPin(Band::Band20m, 3, /*tx=*/true,  true);

        model.setBoardForTest(HPSDRHW::Saturn);
        conn.setBoardForTest(HPSDRHW::Saturn);
        conn.setOcMatrix(&oc);
        model.configureStreamPool(5, 5, 192000);
        model.receiverManager()->setP2Codec(&codec);
        model.injectConnectionForTest(&conn);
        detach.model = &model;
        model.wireReceiverManagerHardwarePushesForTest();
        for (int st = 0; st < 5; ++st) {
            model.receiverManager()->createReceiver();
        }
    }
    ~G2Session() { AppSettings::instance().clear(); }

    int add(double hz)
    {
        const int id = model.addSlice();
        model.sliceById(id)->setFrequency(hz);
        return id;
    }

    OcMatrix         oc;
    P2CodecSaturn    codec;
    RadioModel       model;
    ConnectedP2      conn;
    DetachConnection detach;
};

} // namespace

class TestP2BandOutputsAndRxLpf : public QObject {
    Q_OBJECT

private slots:

    // ── Byte 1401 on the G2, unkeyed and keyed ───────────────────────────
    void g2_byte1401_carriesTheBandOutputs()
    {
        G2Session s;
        const int a = s.add(k20mHz);
        const int b = s.add(k40mHz);
        s.model.setActiveSlice(a);

        // Unkeyed: the receive mask of RX1 (slice A).
        QCOMPARE(highPriorityByte(s.conn, kOcByte),
                 wireOc(s.oc.maskFor(Band::Band20m, /*tx=*/false)));

        // Keyed on B: the transmit mask of B's band.
        QVERIFY(s.model.txSliceArbiter()->requestHandoff(b));
        s.conn.setMox(true);
        QCOMPARE(highPriorityByte(s.conn, kOcByte),
                 wireOc(s.oc.maskFor(Band::Band40m, /*tx=*/true)));
        s.conn.setMox(false);
        QCOMPARE(highPriorityByte(s.conn, kOcByte),
                 wireOc(s.oc.maskFor(Band::Band20m, /*tx=*/false)));

        // Slice A closed: B stands in for RX1.
        s.model.removeSlice(a);
        QCOMPARE(highPriorityByte(s.conn, kOcByte),
                 wireOc(s.oc.maskFor(Band::Band40m, /*tx=*/false)));
    }

    // ── The band comes from the VFO, not the DDC centre ──────────────────
    void g2_bandIsTheVfosNotTheCentres()
    {
        G2Session s;
        constexpr double kVfoHz    = 14010000.0;
        constexpr double kCentreHz = 13950000.0;
        QVERIFY(bandFromFrequency(kCentreHz) != Band::Band20m);

        const int a = s.add(kVfoHz);
        QVERIFY(s.model.requestStreamCentre(a, kCentreHz));
        QCOMPARE(highPriorityByte(s.conn, kOcByte),
                 wireOc(s.oc.maskFor(Band::Band20m, /*tx=*/false)));
    }
};

QTEST_MAIN(TestP2BandOutputsAndRxLpf)
#include "tst_p2_band_outputs_and_rx_lpf.moc"
