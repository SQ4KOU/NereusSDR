// no-port-check: test fixture asserts RadioModel antenna pump (Phase 3P-I-a T14)
#include <QtTest/QtTest>
#include <QSignalSpy>
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/Band.h"
#include "core/RadioConnection.h"
#include "core/accessories/AlexController.h"
#include "core/BoardCapabilities.h"
#include "core/AppSettings.h"

using namespace NereusSDR;

// Minimal RadioConnection that captures setAntennaRouting calls.
// Uses setState(Connected) in the constructor so the base-class
// isConnected() (non-virtual, reads m_state) returns true, which
// allows applyAlexAntennaForBand to pass its guard check.
class MockConnection : public RadioConnection {
    Q_OBJECT
public:
    QList<AntennaRouting> calls;

    explicit MockConnection(QObject* parent = nullptr)
        : RadioConnection(parent)
    {
        // Mark as connected so RadioModel::applyAlexAntennaForBand()
        // passes the !isConnected() early-return guard.
        setState(ConnectionState::Connected);
    }

    // --- Pure virtuals from RadioConnection ---
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
    void setMox(bool) override {}
    void setAntennaRouting(AntennaRouting r) override {
        calls.append(r);
    }
};

class TestAntennaRoutingModel : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        AppSettings::instance().clear();
    }

    void cleanup() {
        AppSettings::instance().clear();
    }

    // T9/T12: writing AlexController for the current band reaches the wire.
    // User clicks RX Ant 2 while tuned to Band20m → setAntennaRouting called
    // with trxAnt == 2.
    void rxAntChange_for_current_band_reaches_wire() {
        RadioModel model;
        // Set hasAlex=true so applyAlexAntennaForBand takes the antenna-read
        // branch rather than the hasAlex=false zero-routing branch.
        model.setCapsForTest(/*hasAlex=*/true);
        auto* mock = new MockConnection();
        model.injectConnectionForTest(mock);

        // m_lastBand defaults to Band20m; set RxAnt on that same band.
        // The antennaChanged signal fires → lambda sees b == m_lastBand →
        // applyAlexAntennaForBand(Band20m) → mock->setAntennaRouting called.
        model.alexControllerMutable().setRxAnt(model.lastBand(), 2);

        QVERIFY(mock->calls.size() >= 1);
        QCOMPARE(mock->calls.last().trxAnt, 2);

        // Null out m_connection before deleting mock so RadioModel's
        // destructor does not dereference a deleted pointer.
        model.injectConnectionForTest(nullptr);
        delete mock;
    }

    // T10: band boundary crossing reapplies the new band's stored antenna.
    // User stored RxAnt=3 for 40m, then tunes from 20m → 40m.
    void band_crossing_reapplies_new_band_antenna() {
        RadioModel model;
        // Set hasAlex=true so the antenna value (not zero) is sent to the wire.
        model.setCapsForTest(/*hasAlex=*/true);
        auto* mock = new MockConnection();
        model.injectConnectionForTest(mock);

        // Store antenna 3 for 40m. Because m_lastBand == Band20m,
        // the antennaChanged(Band40m) is filtered out (b != m_lastBand),
        // so no call is recorded yet.
        model.alexControllerMutable().setRxAnt(Band::Band40m, 3);
        mock->calls.clear();

        // Simulate band crossing: sets m_lastBand = Band40m and calls
        // applyAlexAntennaForBand(Band40m) because the band changed.
        model.setLastBandForTest(Band::Band40m);

        QVERIFY(mock->calls.size() >= 1);
        QCOMPARE(mock->calls.last().trxAnt, 3);

        model.injectConnectionForTest(nullptr);
        delete mock;
    }

    // T11: connect applies persisted state.
    // Antenna stored for Band20m before connection; onConnectedForTest()
    // mirrors the Connected state handler that calls applyAlexAntennaForBand.
    void connect_applies_persisted_antenna() {
        RadioModel model;
        model.setCapsForTest(/*hasAlex=*/true);
        // Store the antenna before connection (no mock yet, no wire call).
        model.alexControllerMutable().setRxAnt(Band::Band20m, 2);

        auto* mock = new MockConnection();
        model.injectConnectionForTest(mock);
        mock->calls.clear();

        // Simulate the Connected state handler. setLastBandForTest with the
        // same band (Band20m == default m_lastBand) is a no-op for the
        // crossing path; onConnectedForTest() applies unconditionally.
        model.onConnectedForTest();

        QVERIFY(mock->calls.size() >= 1);
        QCOMPARE(mock->calls.last().trxAnt, 2);

        model.injectConnectionForTest(nullptr);
        delete mock;
    }

    // T10 follow-up: band crossing also refreshes the slice's cached
    // rxAntenna/txAntenna labels so the VFO Flag / RxApplet buttons
    // show the new band's value. Prior to the 2026-04-22 fix, T10
    // only reapplied to the wire — slice labels stayed on the old
    // band and the user reported "it did not switch automatically"
    // (which was really "the UI label didn't update"). KG4VCF caught
    // it on the bench against ANAN-G2.
    void band_crossing_refreshes_slice_labels() {
        RadioModel model;
        model.setCapsForTest(/*hasAlex=*/true);
        model.addSlice();
        SliceModel* slice = model.sliceAt(0);
        QVERIFY(slice);

        // Store antenna 3 for 40m in the controller but NOT on the slice
        // (the slice default is "ANT1"). Because m_lastBand starts at
        // Band20m, the intermediate antennaChanged(Band40m) emission is
        // filtered by T9's band-match check, so slice->rxAntenna stays
        // at the default.
        model.alexControllerMutable().setRxAnt(Band::Band40m, 3);
        QCOMPARE(slice->rxAntenna(), QStringLiteral("ANT1"));

        auto* mock = new MockConnection();
        model.injectConnectionForTest(mock);

        // Simulate band crossing. setLastBandForTest fires both the
        // wire reapply (applyAlexAntennaForBand) AND the slice label
        // refresh (refreshAntennasFromAlex) — mirrors production T10.
        model.setLastBandForTest(Band::Band40m);

        QCOMPARE(slice->rxAntenna(), QStringLiteral("ANT3"));

        model.injectConnectionForTest(nullptr);
        delete mock;
    }

    // !caps.hasAlex → zeros on the wire (Thetis Alex.cs:312-317 SetAntBits(0,0,0,0,false) parity).
    void hasAlex_false_writes_zero_routing() {
        RadioModel model;
        // setCapsForTest(false): the static override returns hasAlex=false,
        // which causes applyAlexAntennaForBand to zero trxAnt and txAnt.
        model.setCapsForTest(/*hasAlex=*/false);

        auto* mock = new MockConnection();
        model.injectConnectionForTest(mock);
        mock->calls.clear();

        model.onConnectedForTest();

        QVERIFY(mock->calls.size() >= 1);
        QCOMPARE(mock->calls.last().trxAnt, 0);
        QCOMPARE(mock->calls.last().txAnt,  0);

        model.injectConnectionForTest(nullptr);
        delete mock;
    }
};

QTEST_MAIN(TestAntennaRoutingModel)
#include "tst_antenna_routing_model.moc"
