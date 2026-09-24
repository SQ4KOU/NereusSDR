// =================================================================
// tests/tst_slice_cap_every_path.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original test infrastructure.
//
// Receiver and transmit gaps plan, Task 1 (Phase 3F design section 3, the
// slice cap and its message; R-R3-21, a remote window's request is held to
// the Core's rules). Before this task only RadioModel::addSliceOnPan checked
// the slice cap. RadioModel::addSlice(), and with it the session verb
// `addSlice` a remote window sends, created a slice past the board's limit
// whenever the new slice fitted an existing receiver window. The slice then
// sat on a slice id the Core never opened a demodulator channel for.
//
// Covers: a local addSlice() at the cap returns -1, creates nothing and
// emits the cap message; the session verb at the cap is refused with the
// same plain reason and the Core creates nothing, both against a sized
// stream pool and against a real (fake) Hermes Lite 2 connection; below the
// cap nothing changes; addSliceWithStationId on a remote window is not
// refused by the window's own count.
// =================================================================

#include <QtTest/QtTest>
#include <QSignalSpy>

#include <memory>

#include "core/session/ObjectRegistry.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionMessages.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "OperatorWording.h"
#include "fakes/ConnectableRadioModel.h"
#include "fakes/LoopbackStationLink.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;
using NereusSDR::Test::LoopbackStationLink;

namespace {

MirrorUpdate strArg(const QByteArray& name, const QString& value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Utf8, QVariant(value) };
}

// Same shape as tst_session_verbs.cpp's DispatchHarness: decode on the
// Core's side, dispatch, send the result back, decode on the window's side.
class DispatchHarness : public QObject {
public:
    explicit DispatchHarness(RadioModel* radioModel)
        : dispatcher(radioModel)
    {
        connect(&link, &LoopbackStationLink::receivedByDaemon, this,
                [this](const QByteArray& wire) {
                    SessionMessage msg;
                    if (SessionMessages::decode(wire, &msg)) {
                        dispatcher.dispatch(msg);
                    }
                });
        connect(&dispatcher, &SessionCommandDispatcher::commandResultReady, this,
                [this](const SessionMessage& result) {
                    link.sendFromDaemon(SessionMessages::encode(result));
                });
        connect(&link, &LoopbackStationLink::receivedByClient, this,
                [this](const QByteArray& wire) {
                    SessionMessage msg;
                    if (SessionMessages::decode(wire, &msg)) {
                        results.append(msg);
                    }
                });
    }

    void invokeAddSlice(quint32 commandId)
    {
        link.sendFromClient(SessionMessages::encode(SessionMessages::commandInvoke(
            "addSlice", commandId, { strArg("initialPanId", QStringLiteral("pan-0")) })));
    }

    LoopbackStationLink link;
    SessionCommandDispatcher dispatcher;
    QList<SessionMessage> results;
};

} // namespace

class TestSliceCapEveryPath : public QObject {
    Q_OBJECT

private slots:

    void localAddSliceAtCapReturnsMinusOneAndEmitsCapMessage()
    {
        RadioModel model;
        // Five receivers but room for only two slices: every add below
        // lands in the first slice's window, so the allocator alone would
        // accept all of them. Only the slice cap may refuse.
        model.configureStreamPool(/*userDdcCount*/ 5, /*maxSlices*/ 2, 192000);

        QSignalSpy rejected(&model, &RadioModel::sliceAddRejected);
        QCOMPARE(model.addSlice(), 0);
        QCOMPARE(model.addSlice(), 1);
        QCOMPARE(rejected.count(), 0); // below the cap nothing changes

        QCOMPARE(model.addSlice(), -1);
        QCOMPARE(model.slices().size(), 2);
        QVERIFY(model.sliceById(2) == nullptr);
        QCOMPARE(rejected.count(), 1);
        const QString reason = rejected.at(0).at(0).toString();
        QCOMPARE(reason, QStringLiteral("This radio supports a maximum of 2 slices"));
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
    }

    void sessionVerbAddSliceAtCapIsRefusedWithTheCapReason()
    {
        RadioModel model;
        model.configureStreamPool(/*userDdcCount*/ 5, /*maxSlices*/ 2, 192000);
        DispatchHarness harness(&model);

        harness.invokeAddSlice(1);
        harness.invokeAddSlice(2);
        QCOMPARE(harness.results.size(), 2);
        QVERIFY2(harness.results.at(0).accepted, qPrintable(harness.results.at(0).reason));
        QVERIFY2(harness.results.at(1).accepted, qPrintable(harness.results.at(1).reason));

        harness.invokeAddSlice(3);
        QCOMPARE(harness.results.size(), 3);
        const SessionMessage& refused = harness.results.at(2);
        QVERIFY(!refused.accepted);
        QCOMPARE(refused.commandId, quint32(3));
        QCOMPARE(refused.reason, QStringLiteral("This radio supports a maximum of 2 slices"));
        QVERIFY2(OperatorWording::isPlain(refused.reason), qPrintable(refused.reason));
        QVERIFY(refused.affectedKeys.isEmpty());
        QCOMPARE(model.slices().size(), 2);
        QVERIFY(model.sliceById(2) == nullptr);
    }

    void sessionVerbAddSliceAtCapOnAConnectedCoreIsRefused()
    {
        // The Core in production: a real (fake) Hermes Lite 2 connection.
        // HL2 allows five slices on two receivers, so slices share a window
        // and the allocator never refuses the sixth by itself.
        std::unique_ptr<ConnectableRadioModel> connected = ConnectableRadioModel::create();
        QVERIFY(connected);
        RadioModel& model = connected->model();
        QVERIFY(model.isConnected());
        const int cap = model.maxSlices();
        QCOMPARE(cap, 5);
        QCOMPARE(model.slices().size(), 1); // connectToRadio creates Slice A

        DispatchHarness harness(&model);
        for (quint32 id = 1; model.slices().size() < cap; ++id) {
            harness.invokeAddSlice(id);
            QVERIFY(!harness.results.isEmpty());
            QVERIFY2(harness.results.last().accepted,
                     qPrintable(harness.results.last().reason));
        }

        const qsizetype before = harness.results.size();
        harness.invokeAddSlice(99);
        QCOMPARE(harness.results.size(), before + 1);
        const SessionMessage& refused = harness.results.last();
        QVERIFY(!refused.accepted);
        QVERIFY2(refused.reason.endsWith(QStringLiteral(" supports a maximum of 5 slices")),
                 qPrintable(refused.reason));
        QVERIFY2(OperatorWording::isPlain(refused.reason), qPrintable(refused.reason));
        QVERIFY(refused.affectedKeys.isEmpty());
        QCOMPARE(model.slices().size(), cap);
        QVERIFY(model.sliceById(cap) == nullptr);
    }

    void addSliceOnPanAndAddSliceShareTheCapWording()
    {
        // Disconnected, addSliceOnPan's cap is maxSlices() == 1.
        RadioModel model;
        QSignalSpy rejected(&model, &RadioModel::sliceAddRejected);
        model.addSliceOnPan(QStringLiteral("pan-0"));
        model.addSliceOnPan(QStringLiteral("pan-1"));
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(),
                 QStringLiteral("This radio supports a maximum of 1 slices"));
    }

    void remoteWindowReproducesStationSlicesPastItsOwnCount()
    {
        // A remote window reproduces slices the Core already made. Its own
        // maxSlices() is 1 until the Core advertises a limit, and that must
        // never refuse a slice the Core has already created.
        RadioModel remote(RadioModel::Role::Remote);
        QCOMPARE(remote.maxSlices(), 1);
        QSignalSpy rejected(&remote, &RadioModel::sliceAddRejected);

        QCOMPARE(remote.addSliceWithStationId(0, QStringLiteral("pan-0")), 0);
        QCOMPARE(remote.addSliceWithStationId(1, QStringLiteral("pan-0")), 1);
        QCOMPARE(remote.addSliceWithStationId(2, QStringLiteral("pan-1")), 2);
        QCOMPARE(remote.slices().size(), 3);
        QCOMPARE(rejected.count(), 0);
    }
};

QTEST_MAIN(TestSliceCapEveryPath)
#include "tst_slice_cap_every_path.moc"
