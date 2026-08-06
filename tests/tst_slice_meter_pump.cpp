// no-port-check: NereusSDR-original unit-test file. No Thetis logic is
// ported here; this exercises NereusSDR's own SliceMeterPump wiring
// (remote-daemon R2 Task 12). The WDSP meter reads it drives ARE Thetis
// ports (see SliceMeterPump.cpp's own citations), but nothing in this file
// re-derives that logic independently -- it calls the same production
// accessors (RxChannel::getMeter, WdspEngine::getMaxBinDbm) SliceMeterPump
// itself calls, so a test failure means the wiring is wrong, not that this
// file disagrees with Thetis about a meter formula.
// =================================================================
// tests/tst_slice_meter_pump.cpp  (NereusSDR)
// =================================================================
//
// Remote-daemon R2 Task 12 -- the per-slice S-meter gets a model home.
//
// SliceMeterPump replaces MeterPoller::pollSliceSMeters() (GUI-only, so a
// headless nereusd could never produce a per-slice S-meter reading for the
// mirror to carry) with a core-side QTimer that writes directly into each
// live slice's SliceModel::signalStrengthDbm. See SliceMeterPump.h's class
// comment for the full design.
//
// Test groups:
//   1. SliceModel::signalStrengthDbm's shape: no WRITE, has a NOTIFY,
//      defaults to -140.0, emits once per distinct value (bare SliceModel,
//      no RadioModel needed).
//   2. poll() against an UNCONNECTED RadioModel: no WDSP channel means the
//      value stays at its constructed default; TX (RadioStatus::
//      isTransmitting) means poll() touches nothing at all, even a slice
//      that already holds a real reading.
//   3. poll() against a REAL connected RxChannel (fakes/ConnectableRadioModel),
//      proving the source selector actually reaches WDSP: SignalAverage
//      (the default with no selector wired) and SignalPeak both match an
//      independently-taken read of the SAME live meter; MaxBin, with no
//      detector ever configured, passes the WdspEngine::getMaxBinDbm
//      -400.0 "not active" sentinel straight through -- deterministic
//      without needing a live FFT displayed at all.
//   4. Construction is gated on RadioModel::Role -- the obligation Task 5's
//      brief could not discharge (SliceMeterPump did not exist yet) and
//      reassigned here. See tst_remote_role_inert.cpp's own header comment
//      for that history.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-06 -- New test file for remote-daemon R2 Task 12. J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via Anthropic
//                 Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include <memory>

#include "core/RadioStatus.h"
#include "core/RxChannel.h"
#include "core/WdspEngine.h"
#include "core/WdspTypes.h"
#include "core/meters/SliceMeterPump.h"
#include "fakes/ConnectableRadioModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;

class TestSliceMeterPump : public QObject {
    Q_OBJECT

private slots:

    // ── Group 1: SliceModel::signalStrengthDbm's shape ─────────────────────

    // Step 1: "not writable, has a notify, starts at -140.0". Pure
    // meta-object introspection plus a freshly-constructed default -- no
    // RadioModel, no WdspEngine, nothing that could touch a real radio.
    void signalStrengthDbmIsReadOnlyWithNotifyAndDefaultsToMinus140()
    {
        SliceModel slice(0);

        const QMetaObject* mo = slice.metaObject();
        const int idx = mo->indexOfProperty("signalStrengthDbm");
        QVERIFY2(idx >= 0, "signalStrengthDbm must be a declared Q_PROPERTY");
        const QMetaProperty prop = mo->property(idx);
        QVERIFY2(!prop.isWritable(),
                 "signalStrengthDbm must carry no WRITE -- SliceMeterPump is "
                 "the only local writer, via setSignalStrengthDbm(), which is "
                 "not a QMetaProperty accessor");
        QVERIFY2(prop.hasNotifySignal(),
                 "signalStrengthDbm must have a NOTIFY or it can never reach "
                 "the mirror as a delta");
        QCOMPARE(prop.notifySignal().name(), QByteArray("signalStrengthDbmChanged"));

        QCOMPARE(slice.signalStrengthDbm(), -140.0);
    }

    // "Emits once per distinct value." A property of setSignalStrengthDbm's
    // own equality guard, tested directly against the setter rather than
    // through a live poll -- a live WDSP meter reading two ticks apart is
    // not guaranteed identical even under silence, which would make this
    // assertion flaky for a reason that has nothing to do with the emit
    // guard under test.
    void setSignalStrengthDbmEmitsOncePerDistinctValue()
    {
        SliceModel slice(0);
        QSignalSpy spy(&slice, &SliceModel::signalStrengthDbmChanged);

        slice.setSignalStrengthDbm(-73.0);
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.constLast().at(0).toDouble(), -73.0);

        // Same value again: no second emit.
        slice.setSignalStrengthDbm(-73.0);
        QCOMPARE(spy.count(), 1);

        // A distinct value: exactly one more emit.
        slice.setSignalStrengthDbm(-91.5);
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.constLast().at(0).toDouble(), -91.5);
    }

    // ── Group 2: poll() against an unconnected RadioModel ───────────────────

    // Step 1: "a slice with no WDSP channel reads -140.0." A bare, never-
    // connected Role::Local RadioModel has a real SliceMeterPump (role
    // gate, group 4 below) but wdspEngine()->rxChannel(id) is null for
    // every id -- poll() must skip such a slice and leave it at its
    // constructed default rather than writing a sentinel of its own.
    void pollLeavesSliceAtDefaultWhenNoWdspChannelExists()
    {
        RadioModel model{RadioModel::Role::Local};
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        const int sliceId = model.addSlice();
        QVERIFY(sliceId >= 0);
        SliceModel* slice = model.sliceById(sliceId);
        QVERIFY(slice != nullptr);
        QVERIFY(model.wdspEngine()->rxChannel(slice->sliceIndex()) == nullptr);

        pump->poll();

        QCOMPARE(slice->signalStrengthDbm(), -140.0);
    }

    // Step 4: "the pump stops while transmitting." RadioStatus::
    // isTransmitting() (not MeterPoller's m_inTx, which only ever sees MOX
    // asserted through MoxController) gates poll() before it looks at a
    // single slice. Seeded with a real reading first so a wrongly-ungated
    // poll() would visibly clobber it back toward -140.0 rather than the
    // test vacuously re-confirming an unmoved default.
    void pollDoesNothingWhileTransmitting()
    {
        RadioModel model{RadioModel::Role::Local};
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        const int sliceId = model.addSlice();
        SliceModel* slice = model.sliceById(sliceId);
        QVERIFY(slice != nullptr);

        // setSignalStrengthDbm is a plain public method (like setActive /
        // setTxSlice), not a QMetaProperty WRITE -- direct calls are the
        // normal way to seed it in a test.
        slice->setSignalStrengthDbm(-42.0);

        model.radioStatus().setTransmitting(true);
        pump->poll();

        QCOMPARE(slice->signalStrengthDbm(), -42.0);
    }

    // ── Group 3: poll() against a real connected RxChannel ──────────────────

    // The default source (no selector ever wired) is SignalAverage, the
    // same fixed choice the pre-Task-12 pollSliceSMeters() made. Compares
    // against a read of the SAME live meter taken immediately after
    // poll() returns -- no intervening QTest::qWait or event-loop turn --
    // so the two reads observe WDSP's accumulator at, for all practical
    // purposes, the same instant.
    void pollWritesSignalAverageByDefaultForAConnectedSlice()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);
        RadioModel& model = harness->model();
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        SliceModel* slice = model.sliceById(0);
        QVERIFY(slice != nullptr);
        RxChannel* ch = model.wdspEngine()->rxChannel(slice->sliceIndex());
        QVERIFY(ch != nullptr);

        pump->poll();

        const double expected =
            ch->getMeter(RxMeterType::SignalAvg) + model.rxMeterOffsetDb();
        QCOMPARE(slice->signalStrengthDbm(), expected);

        harness.reset();
    }

    // Step 7: the rxMode()-driven source selector. Wiring the selector to
    // report SignalPeak must change which WDSP meter type gets read,
    // exactly matching MeterPoller::pollSMeter()'s SMeter/SMeterPeak
    // branch (RxMeterType::SignalPeak), not the fixed SignalAvg the old
    // pollSliceSMeters() always used.
    void pollWritesSignalPeakWhenSelectorSaysSo()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);
        RadioModel& model = harness->model();
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        SliceModel* slice = model.sliceById(0);
        QVERIFY(slice != nullptr);
        RxChannel* ch = model.wdspEngine()->rxChannel(slice->sliceIndex());
        QVERIFY(ch != nullptr);

        pump->setSourceSelector([]() { return SliceMeterPump::MeterSource::SignalPeak; });
        pump->poll();

        const double expected =
            ch->getMeter(RxMeterType::SignalPeak) + model.rxMeterOffsetDb();
        QCOMPARE(slice->signalStrengthDbm(), expected);

        harness.reset();
    }

    // MaxBin, with setupMaxBinDetector() never called anywhere in this
    // test, is WdspEngine::getMaxBinDbm's own documented "not yet active"
    // case: an exact, deterministic -400.0 sentinel, passed straight
    // through per WdspEngine::getMaxBinDbm's doc comment and
    // MeterPoller::pollSMeter()'s own "> -400 else pass through unchanged"
    // handling. No live FFT or display channel needed, which is what
    // keeps this branch testable without a real waterfall running.
    void pollPassesThroughMaxBinNotActiveSentinel()
    {
        std::unique_ptr<ConnectableRadioModel> harness = ConnectableRadioModel::create();
        QVERIFY(harness != nullptr);
        RadioModel& model = harness->model();
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        SliceModel* slice = model.sliceById(0);
        QVERIFY(slice != nullptr);
        QVERIFY(model.wdspEngine()->rxChannel(slice->sliceIndex()) != nullptr);
        QCOMPARE(model.wdspEngine()->getMaxBinDbm(/*disp=*/0), -400.0);

        pump->setSourceSelector([]() { return SliceMeterPump::MeterSource::MaxBin; });
        pump->poll();

        QCOMPARE(slice->signalStrengthDbm(), -400.0);

        harness.reset();
    }

    // intervalMs()/setIntervalMs() clamp the same way MeterPoller's do
    // (Task 3.1 precedent, MeterPoller.cpp), so a persisted or live
    // MultimeterDelayMs of 0 (a stale/corrupt settings value) can never
    // stall the timer.
    void intervalMsClampsToTenTwoThousand()
    {
        RadioModel model{RadioModel::Role::Local};
        SliceMeterPump* pump = model.sliceMeterPump();
        QVERIFY(pump != nullptr);

        pump->setIntervalMs(0);
        QCOMPARE(pump->intervalMs(), 10);

        pump->setIntervalMs(50000);
        QCOMPARE(pump->intervalMs(), 2000);

        pump->setIntervalMs(250);
        QCOMPARE(pump->intervalMs(), 250);
    }

    // ── Group 4: construction is gated on Role ──────────────────────────────
    //
    // The obligation Task 5's brief step 1(e) could not discharge because
    // SliceMeterPump did not exist yet (see tst_remote_role_inert.cpp's own
    // header comment for the full history) -- reassigned to this task's
    // step 4b. RadioModel constructs WdspEngine unconditionally regardless
    // of role, so an unguarded pump would run a 10 Hz timer against a
    // channel-less engine on a Role::Remote model and clobber every
    // mirrored needle with the -140.0 fallback the moment a future task
    // wires the mirror's inbound apply into signalStrengthDbm.

    void localRoleConstructsSliceMeterPump()
    {
        RadioModel model{RadioModel::Role::Local};
        QVERIFY(model.sliceMeterPump() != nullptr);
    }

    void remoteRoleDoesNotConstructSliceMeterPump()
    {
        RadioModel model{RadioModel::Role::Remote};
        QVERIFY(model.sliceMeterPump() == nullptr);
    }
};

QTEST_MAIN(TestSliceMeterPump)
#include "tst_slice_meter_pump.moc"
