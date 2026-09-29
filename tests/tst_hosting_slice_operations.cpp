// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_hosting_slice_operations.cpp  (NereusSDR)
// =================================================================
//
// Slice control and shared listening plan Task 10: the hosting desktop's
// slice requests run as the station device through
// StationServer::invokeAsStationDevice() (HostingSliceActions), over one
// Core and the in-process loopback, with remote devices signed in by keys
// made at run time:
//   - an add at full capacity gets the question a remote device gets, field
//     by field, and nothing closes before the proceed;
//   - a take of a remote device's slice on the air is refused in the words a
//     remote take gets;
//   - a close with a remote listener keeps the slice (ruling Q6); with
//     nobody it closes, to none when it was the last;
//   - a stale take (an old incarnation) and a select of a slice no longer on
//     the Core are refused;
//   - controlTaken reaches the host as a refusal; a verb the host does not
//     run is refused as unknown; without a server, nothing runs.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 10,
//               with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "gui/HostingSliceActions.h"

#include <QSignalSpy>

namespace {

const QHash<QByteArray, int> kShares{{"deviceAuth", 1}, {"sessionHolder", 1}, {"sliceAccess", 1}};
const QHash<QByteArray, int> kSharesTx{
    {"deviceAuth", 1}, {"sessionHolder", 1}, {"sliceAccess", 1}, {"remoteTx", 1}};

const QString kHostName = QStringLiteral("Mac");

// The desktop begins hosting, as StationHost does: the station device is a
// connected device named `kHostName`, and adopts every slice nobody owns.
void startHosting(Core& core)
{
    const QByteArray& station = SliceOwnership::stationDevice();
    core.server->deviceSessions()->registerHostingDevice(station, kHostName, kHostName);
    core.server->setStationDeviceWords(kHostName, kHostName);
    SliceOwnership* ownership = core.model->sliceOwnership();
    if (!ownership->adoptUnowned(station).isEmpty()) {
        core.model->setActiveSliceByIdFor(station, ownership->activeFor(station));
    }
}

QList<int> ownedBy(const Core& core, const QByteArray& device)
{
    return core.model->sliceOwnership()->ownedBy(device);
}

QString reasonOf(const QJsonObject& result)
{
    return result.value(QStringLiteral("reason")).toString();
}

bool accepted(const QJsonObject& result)
{
    return result.value(QStringLiteral("accepted")).toBool(false);
}

QList<MirrorUpdate> refOf(const Core& core, int sliceId, qint64 incarnationShift = 0)
{
    return {int64("sliceId", sliceId),
            int64("incarnation",
                  static_cast<qint64>(core.model->sliceOwnership()->incarnation(sliceId))
                      + incarnationShift)};
}

QJsonObject lastOfType(const LoopbackTransport* app, const QString& type)
{
    const QList<QJsonObject> all = ofType(app->received(), type);
    return all.isEmpty() ? QJsonObject{} : all.last();
}

// A question with what differs by who asked taken out: its ids, and the
// asker's device id wherever it appears.
QJsonObject comparable(QJsonObject question, const QString& askerWireId)
{
    question.remove(QStringLiteral("id"));
    question.remove(QStringLiteral("forCommandId"));
    QString text = QString::fromUtf8(QJsonDocument(question).toJson(QJsonDocument::Compact));
    if (!askerWireId.isEmpty()) {
        text.replace(askerWireId, QStringLiteral("ASKER"));
    }
    return QJsonDocument::fromJson(text.toUtf8()).object();
}

// Both receivers held: the asker's slice 0 on 7.074 MHz, the phone's slice
// on 14.074 MHz.
void holdBothReceivers(Core& core, const Device& phone)
{
    for (int id : ownedBy(core, phone.key.fingerprint())) {
        core.model->sliceById(id)->setFrequency(14074000.0);
    }
}

// The takeReceiver choice for `stream`, or -1.
qint64 choiceFor(const QJsonObject& ask, int stream)
{
    for (const QJsonValue& v : ask.value(QStringLiteral("choices")).toArray()) {
        if (v.toObject().value(QStringLiteral("streamIndex")).toInt(-1) == stream) {
            return v.toObject().value(QStringLiteral("choice")).toInteger(-1);
        }
    }
    return -1;
}

} // namespace

class TstHostingSliceOperations : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppSettings::setProfileOverride(
            QStringLiteral("hosting-slice-ops-%1").arg(QCoreApplication::applicationPid()));
        AppSettings::instance().clear();
    }

    void anAddAtFullCapacityAsksWhatARemoteDeviceIsAsked()
    {
        Device phone;

        // The host asks.
        QJsonObject hostQuestion;
        QString hostWireId;
        QString hostRefusal;
        QString remoteRefusal;
        {
            Core core;
            core.model->configureStreamPool(2, 5, 192000);
            core.model->sliceById(0)->setFrequency(7074000.0);
            startHosting(core);
            const QByteArray station = SliceOwnership::stationDevice();
            QCOMPARE(ownedBy(core, station), QList<int>{0});
            core.pair(phone);
            LoopbackTransport* appP = core.signIn(phone, kShares);
            QVERIFY(admitted(appP));
            holdBothReceivers(core, phone);
            QCOMPARE(receiversInUse(*core.model), 2);
            const int slicesBefore = static_cast<int>(core.model->slices().size());

            HostingSliceActions host(core.server.get(), core.model.get());
            QSignalSpy asked(&host, &HostingSliceActions::question);
            QSignalSpy refused(&host, &HostingSliceActions::refused);
            QSignalSpy finished(&host, &HostingSliceActions::finished);
            host.addOnPan(QStringLiteral("new-pan"));
            QTRY_COMPARE(asked.count(), 1);
            // As a remote device's: the Add is answered with who holds the
            // receivers, and the question follows.
            QCOMPARE(finished.count(), 1);
            QCOMPARE(finished.first().at(0).toByteArray(), QByteArrayLiteral("addSliceOnPan"));
            QCOMPARE(finished.first().at(2).toBool(), false);
            hostRefusal = finished.first().at(3).toString();
            QCOMPARE(refused.count(), 1);
            QCOMPARE(refused.first().at(0).toString(), hostRefusal);
            // Nothing closed before the proceed.
            QCOMPARE(static_cast<int>(core.model->slices().size()), slicesBefore);
            QVERIFY(!ownedBy(core, phone.key.fingerprint()).isEmpty());

            const SessionMessage question = asked.first().at(0).value<SessionMessage>();
            hostQuestion = QJsonDocument::fromJson(SessionMessages::encode(question)).object();
            hostWireId = StationIdentity::toBase64Url(station);

            // Proceed on the phone's receiver: the host's new slice opens on it.
            const int phoneStream =
                core.model->sliceById(ownedBy(core, phone.key.fingerprint()).first())->streamIndex();
            const qint64 choice = choiceFor(hostQuestion, phoneStream);
            QVERIFY(choice >= 0);
            host.proceed(hostQuestion.value(QStringLiteral("id")).toInteger(), choice);
            QTRY_COMPARE(finished.count(), 2);
            QCOMPARE(finished.last().at(0).toByteArray(), QByteArrayLiteral("confirm.proceed"));
            QVERIFY2(finished.last().at(2).toBool(), qPrintable(finished.last().at(3).toString()));
            QTRY_COMPARE(ownedBy(core, station).size(), 2);
            bool onNewPan = false;
            for (int id : ownedBy(core, station)) {
                if (core.model->sliceById(id)->panKey() == QStringLiteral("new-pan")) {
                    onNewPan = true;
                }
            }
            QVERIFY(onNewPan);
        }

        // A remote device in the same place asks.
        QJsonObject remoteQuestion;
        Device mac(kHostName, QStringLiteral("computer"), kHostName);
        {
            Core core;
            core.model->configureStreamPool(2, 5, 192000);
            core.model->sliceById(0)->setFrequency(7074000.0);
            core.pair(mac);
            core.pair(phone);
            LoopbackTransport* appM = core.signIn(mac, kShares);
            QVERIFY(admitted(appM));
            QCOMPARE(ownedBy(core, mac.key.fingerprint()), QList<int>{0});
            LoopbackTransport* appP = core.signIn(phone, kShares);
            QVERIFY(admitted(appP));
            holdBothReceivers(core, phone);
            const QJsonObject result =
                core.invoke(appM, "addSliceOnPan", {utf8("panId", QStringLiteral("new-pan"))});
            QVERIFY(!accepted(result));
            remoteRefusal = reasonOf(result);
            QTRY_VERIFY(!lastOfType(appM, QStringLiteral("confirm.request")).isEmpty());
            remoteQuestion = lastOfType(appM, QStringLiteral("confirm.request"));
        }

        QVERIFY(!hostRefusal.isEmpty());
        QCOMPARE(hostRefusal, remoteRefusal);
        QVERIFY(OperatorWording::isPlain(hostRefusal));
        const QJsonObject host = comparable(hostQuestion, hostWireId);
        const QJsonObject remote = comparable(remoteQuestion, mac.id());
        QVERIFY2(host == remote,
                 qPrintable(QStringLiteral("host %1\nremote %2")
                                .arg(QString::fromUtf8(QJsonDocument(host).toJson()),
                                     QString::fromUtf8(QJsonDocument(remote).toJson()))));
    }

    void aTakeOfATransmittingSliceIsRefusedInTheRemoteWords()
    {
        Core core;
        allowTransmit(core);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kSharesTx);
        QVERIFY(admitted(appA));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        startHosting(core);
        LoopbackTransport* appB = core.signIn(b, kSharesTx);
        QVERIFY(admitted(appB));

        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy refused(&host, &HostingSliceActions::refused);
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.listen(0);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.first().at(2).toBool(), qPrintable(finished.first().at(3).toString()));
        QVERIFY(core.model->sliceOwnership()->listenersOf(0).contains(SliceOwnership::stationDevice()));
        QVERIFY(accepted(core.invoke(appB, "slice.listen", refOf(core, 0))));

        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Tx);

        const QString words = QStringLiteral("Slice A is transmitting. Take control once it stops.");
        const QJsonObject remote = core.invoke(
            appB, "slice.takeControl",
            refOf(core, 0)
                << int64("controlRevision",
                         static_cast<qint64>(core.model->sliceOwnership()->controlRevision(0))));
        QVERIFY(!accepted(remote));
        QCOMPARE(reasonOf(remote), words);

        host.takeControl(0);
        QTRY_COMPARE(finished.count(), 2);
        QCOMPARE(finished.last().at(2).toBool(), false);
        QCOMPARE(finished.last().at(3).toString(), words);
        QCOMPARE(refused.count(), 1);
        QCOMPARE(refused.first().at(0).toString(), words);
        QVERIFY(OperatorWording::isPlain(words));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        QVERIFY(mox->isMox());

        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
    }

    void aCloseWithARemoteListenerKeepsTheSlice()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        startHosting(core);
        const QByteArray station = SliceOwnership::stationDevice();
        QCOMPARE(ownedBy(core, station), QList<int>{0});
        Device l;
        core.pair(l);
        LoopbackTransport* appL = core.signIn(l, kShares);
        QVERIFY(admitted(appL));
        QVERIFY(accepted(core.invoke(appL, "slice.listen", refOf(core, 0))));

        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.close(0);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.first().at(2).toBool(), qPrintable(finished.first().at(3).toString()));
        QVERIFY(core.model->sliceById(0) != nullptr);
        QVERIFY(!ownedBy(core, station).contains(0));
        QVERIFY(core.model->sliceOwnership()->listenersOf(0).contains(l.key.fingerprint()));
    }

    void aCloseWithNobodyListeningClosesTheLastSlice()
    {
        Core core;
        startHosting(core);
        const QByteArray station = SliceOwnership::stationDevice();
        QCOMPARE(ownedBy(core, station), QList<int>{0});
        QCOMPARE(static_cast<int>(core.model->slices().size()), 1);

        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.close(0);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.first().at(2).toBool(), qPrintable(finished.first().at(3).toString()));
        QTRY_COMPARE(static_cast<int>(core.model->slices().size()), 0);
    }

    void aStaleTakeAndAStaleSelectAreRefused()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        startHosting(core);

        // A take that names an incarnation the slice no longer has.
        QList<MirrorUpdate> stale = refOf(core, 0, -1);
        stale << int64("controlRevision",
                       static_cast<qint64>(core.model->sliceOwnership()->controlRevision(0)));
        SessionMessage answer;
        bool answered = false;
        core.server->invokeAsStationDevice(
            SessionMessages::commandInvoke("slice.takeControl", 7, stale),
            [&](const SessionMessage& result) {
                answer = result;
                answered = true;
            },
            {});
        QTRY_VERIFY(answered);
        QVERIFY(!answer.accepted);
        QVERIFY(!answer.reason.isEmpty());
        QVERIFY(OperatorWording::isPlain(answer.reason));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());

        // A select of a slice no longer on the Core.
        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy refused(&host, &HostingSliceActions::refused);
        host.select(42);
        // Answered before select() returns: the window reads it at once.
        QCOMPARE(refused.count(), 1);
        QVERIFY(OperatorWording::isPlain(refused.first().at(0).toString()));
    }

    // The window's Select waits for no event loop: the answer is in by the
    // time select() returns, and the station device's active slice moved.
    void aSelectIsAnsweredAtOnce()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        startHosting(core);
        const QByteArray station = SliceOwnership::stationDevice();
        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.addOnPan(QStringLiteral("pan-0"));
        QCOMPARE(finished.count(), 1);
        QVERIFY2(finished.first().at(2).toBool(), qPrintable(finished.first().at(3).toString()));
        QCOMPARE(ownedBy(core, station).size(), 2);
        const int second = ownedBy(core, station).last();
        core.model->setActiveSliceByIdFor(station, ownedBy(core, station).first());

        host.select(second);
        QCOMPARE(finished.count(), 2);
        QVERIFY2(finished.last().at(2).toBool(), qPrintable(finished.last().at(3).toString()));
        QCOMPARE(core.model->sliceOwnership()->activeFor(station), second);
        QCOMPARE(host.invoking(), false);
    }

    void theHostTakesALiveSliceAndItsControllerIsTold()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        startHosting(core);

        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.listen(0);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.last().at(2).toBool(), qPrintable(finished.last().at(3).toString()));
        const int noticesBefore = static_cast<int>(ofType(appA->received(), QStringLiteral("notice")).size());
        host.takeControl(0);
        QTRY_COMPARE(finished.count(), 2);
        QVERIFY2(finished.last().at(2).toBool(), qPrintable(finished.last().at(3).toString()));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, SliceOwnership::stationDevice());
        // A stays on as a listener and is told who took it.
        QVERIFY(core.model->sliceOwnership()->listenersOf(0).contains(a.key.fingerprint()));
        QTRY_VERIFY(ofType(appA->received(), QStringLiteral("notice")).size() > noticesBefore);
        const QJsonObject told = lastOfType(appA, QStringLiteral("notice"));
        QCOMPARE(told.value(QStringLiteral("kind")).toString(), QStringLiteral("controlTaken"));
        QVERIFY(told.value(QStringLiteral("reason")).toString().contains(kHostName));
    }

    void theHostListeningIsToldWhenATakeClosesTheSlice()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kShares);
        QVERIFY(admitted(appA));
        startHosting(core);
        LoopbackTransport* appB = core.signIn(b, kShares);
        QVERIFY(admitted(appB));
        holdBothReceivers(core, b);
        QCOMPARE(receiversInUse(*core.model), 2);

        HostingSliceActions host(core.server.get(), core.model.get());
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        QSignalSpy notices(&host, &HostingSliceActions::notice);
        host.listen(0);
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY2(finished.last().at(2).toBool(), qPrintable(finished.last().at(3).toString()));

        const quint64 incarnation = core.model->sliceOwnership()->incarnation(0);
        // B takes A's receiver for a new pan: slice 0 closes.
        const int before = static_cast<int>(ofType(appB->received(), QStringLiteral("confirm.request")).size());
        const QJsonObject refused =
            core.invoke(appB, "addSliceOnPan", {utf8("panId", QStringLiteral("b-new-pan"))});
        QVERIFY(!accepted(refused));
        QTRY_VERIFY(ofType(appB->received(), QStringLiteral("confirm.request")).size() > before);
        const QJsonObject ask = lastOfType(appB, QStringLiteral("confirm.request"));
        const qint64 choice = choiceFor(ask, core.model->sliceById(0)->streamIndex());
        QVERIFY(choice >= 0);
        const QJsonObject proceeded = core.invoke(
            appB, "confirm.proceed",
            {int64("id", ask.value(QStringLiteral("id")).toInteger()), int64("choice", choice)});
        QVERIFY2(accepted(proceeded), qPrintable(reasonOf(proceeded)));
        QTRY_VERIFY(!core.model->sliceOwnership()->matches(SliceOwnership::SliceRef{0, incarnation}));

        // The host, a listener, is told as every listener is (Task 9).
        QTRY_COMPARE(notices.count(), 1);
        const SessionMessage told = notices.first().at(0).value<SessionMessage>();
        QVERIFY(!told.reason.isEmpty());
        QVERIFY(OperatorWording::isPlain(told.reason));
        QCOMPARE(told.prompt.kind, QStringLiteral("sliceClosed"));
        QCOMPARE(told.reason, QStringLiteral("iPad took the receiver slice A was on. You were "
                                             "listening to it."));
    }

    void aVerbTheHostDoesNotRunIsRefusedAsUnknown()
    {
        Core core;
        startHosting(core);
        SessionMessage answer;
        bool answered = false;
        core.server->invokeAsStationDevice(
            SessionMessages::commandInvoke("tx.key", 3, {}),
            [&](const SessionMessage& result) {
                answer = result;
                answered = true;
            },
            {});
        QVERIFY(answered);
        QVERIFY(!answer.accepted);
        QCOMPARE(answer.reason, kUnknownVerb);
    }

    void withoutAServerNothingRuns()
    {
        HostingSliceActions host(nullptr, nullptr);
        QSignalSpy refused(&host, &HostingSliceActions::refused);
        QSignalSpy finished(&host, &HostingSliceActions::finished);
        host.select(0);
        QCOMPARE(refused.count(), 1);
        QVERIFY(OperatorWording::isPlain(refused.first().at(0).toString()));
        QCOMPARE(finished.count(), 1);
        QCOMPARE(finished.first().at(2).toBool(), false);
    }
};

QTEST_MAIN(TstHostingSliceOperations)
#include "tst_hosting_slice_operations.moc"
