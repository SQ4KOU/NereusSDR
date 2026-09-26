// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_on_air_refusals.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 34 (the several-devices design, ruling 7.4; D60):
// while the holder is on the air, a change from another device to the
// transmit path (the amplifier and its settings, the tuner, an antenna,
// PureSignal, the interlock, the power cap), a Protocol 1 sample-rate
// change, and a C-Tune move of the holder's transmit slice are refused,
// never asked, with "<holder's short name> is on the air. Try again when
// they stop." The holder's own change is not refused by this rule, and with
// the holder unkeyed none is.
//
// Two layers: the dispatcher's verbs (with the Core's access rule stood in,
// so each verb's own availability checks do not hide the rule), and the
// Core itself over the loopback with two devices (property writes and the
// verbs that need no accessory).
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 34 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "core/session/SessionCommandDispatcher.h"

namespace {

const QString kOnAir = QStringLiteral("iPhone is on the air. Try again when they stop.");

struct Dispatch {
    RadioModel model;
    SessionCommandDispatcher dispatcher{&model};
    bool keyed{true};
    QList<SessionMessage> results;
    quint32 nextId{1};

    Dispatch()
    {
        model.setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:02");
        info.boardType = HPSDRHW::HermesLite;
        info.protocol = ProtocolVersion::Protocol1;
        model.setLastRadioInfoForTest(info);
        model.setConnectionStateForTest(ConnectionState::Connected);
        model.addSlice(QStringLiteral("pan-0"));
        SessionCommandDispatcher::TransmitAccess access;
        access.onAir = [this](const QByteArray& requester) -> TxRefusal {
            if (!keyed || requester == "phone") {
                return {};
            }
            return TxRefusals::holderOnAir(QStringLiteral("iPhone"), false);
        };
        dispatcher.setTransmitAccess(access);
        QObject::connect(&dispatcher, &SessionCommandDispatcher::commandResultReady,
                         [this](const SessionMessage& m) { results.append(m); });
    }

    SessionMessage invoke(const QByteArray& requester, const QByteArray& verb,
                          const QList<MirrorUpdate>& arguments = {})
    {
        results.clear();
        dispatcher.setRequester(requester);
        dispatcher.dispatch(SessionMessages::commandInvoke(verb, nextId++, arguments));
        dispatcher.setRequester({});
        return results.isEmpty() ? SessionMessage{} : results.first();
    }
};

bool refusedOnAir(const SessionMessage& result)
{
    if (result.accepted || result.reason != kOnAir) {
        return false;
    }
    bool code = false;
    for (const MirrorUpdate& value : result.updates) {
        code = code || (value.name == "refusalCode" && value.value.toString() == QStringLiteral("holderOnAir"));
    }
    return code;
}

struct Verb {
    QByteArray name;
    QList<MirrorUpdate> arguments;
};

QList<Verb> transmitPathVerbs()
{
    return {
        {"configurePgxl", {utf8("host", QStringLiteral("192.0.2.9")), int64("port", 9008)}},
        {"setPgxlName", {utf8("name", QStringLiteral("Amp"))}},
        {"configureTgxl", {utf8("host", QStringLiteral("192.0.2.9")), int64("port", 9010)}},
        {"setTgxlAntenna", {int64("port", 2)}},
        {"setAlexRxAntenna", {int64("band", 5), int64("antenna", 2),
                              MirrorUpdate{0, "rxOnly", MirrorWireKind::Bool, QVariant(false)}}},
        {"ps3.off", {}},
        {"setTxInterlockPolicy", {int64("mode", 2), int64("graceMs", 1000),
                                  MirrorUpdate{0, "swrGateEnabled", MirrorWireKind::Bool, QVariant(true)},
                                  f64("swrGateMax", 2.5)}},
        {"setPgxlPowerCap", {MirrorUpdate{0, "enabled", MirrorWireKind::Bool, QVariant(true)},
                             int64("watts", 800)}},
        {"requestSliceSampleRate", {int64("sliceId", 0), int64("rateHz", 96000)}},
        {"requestStreamCentre", {int64("sliceId", 0), f64("centreHz", 14150000.0)}},
    };
}

} // namespace

class TstOnAirRefusals : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qRegisterMetaType<NereusSDR::TxRefusal>(); }

    // ---- The dispatcher's verbs ---------------------------------------------

    void anotherDevicesTransmitPathChangeIsRefusedWhileTheHolderIsOnTheAir()
    {
        Dispatch d;
        for (const Verb& verb : transmitPathVerbs()) {
            const SessionMessage result = d.invoke("pad", verb.name, verb.arguments);
            QVERIFY2(refusedOnAir(result),
                     qPrintable(QString::fromLatin1(verb.name) + QStringLiteral(": ") + result.reason));
        }
    }

    void theHoldersOwnChangeIsNotRefusedByTheRule()
    {
        Dispatch d;
        for (const Verb& verb : transmitPathVerbs()) {
            const SessionMessage result = d.invoke("phone", verb.name, verb.arguments);
            QVERIFY2(result.reason != kOnAir, verb.name.constData());
        }
    }

    void withTheHolderUnkeyedNoneIsRefusedByTheRule()
    {
        Dispatch d;
        d.keyed = false;
        for (const Verb& verb : transmitPathVerbs()) {
            const SessionMessage result = d.invoke("pad", verb.name, verb.arguments);
            QVERIFY2(result.reason != kOnAir, verb.name.constData());
        }
    }

    void twoToneAndAProtocol2RateChangeAreNotThisRules()
    {
        Dispatch d;
        QVERIFY(d.invoke("pad", "ps3.twoTone",
                         {MirrorUpdate{0, "enabled", MirrorWireKind::Bool, QVariant(true)}})
                    .reason != kOnAir);
        RadioInfo info = d.model.currentRadioInfo();
        info.protocol = ProtocolVersion::Protocol2;
        d.model.setLastRadioInfoForTest(info);
        QVERIFY(d.invoke("pad", "requestSliceSampleRate",
                         {int64("sliceId", 0), int64("rateHz", 96000)}).reason != kOnAir);
    }

    void aCTuneMoveOffTheTransmitSlicesReceiverIsNotThisRules()
    {
        Dispatch d;
        d.model.configureStreamPool(5, 5, 192000);
        const int far = d.model.addSlice(QStringLiteral("pan-0"));
        d.model.sliceById(0)->setFrequency(14200000.0);
        d.model.sliceById(far)->setFrequency(7100000.0);
        QVERIFY(d.model.txBoundSlice() == d.model.sliceById(0));
        QVERIFY(d.model.sliceById(far)->streamIndex() != d.model.sliceById(0)->streamIndex());
        QVERIFY(d.invoke("pad", "requestStreamCentre",
                         {int64("sliceId", far), f64("centreHz", 7150000.0)}).reason != kOnAir);
        QVERIFY(refusedOnAir(d.invoke("pad", "requestStreamCentre",
                                      {int64("sliceId", 0), f64("centreHz", 14150000.0)})));
    }

    // ---- The Core, two devices over the loopback -------------------------------

    void propertyWritesToTheTransmitPathWaitWhileTheHolderIsOnTheAir()
    {
        Core core;
        allowTransmit(core);
        Device a(QStringLiteral("Grant's iPhone"), QStringLiteral("phone"), QStringLiteral("iPhone"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a, kTransmitter);
        LoopbackTransport* appB = core.signIn(b, kTransmitter);
        QVERIFY(admitted(appA) && admitted(appB));
        const QStringList bSlices = heldKeys(appB, QStringLiteral("slice:"));
        QCOMPARE(bSlices.size(), 1);
        const QByteArray bSlice = bSlices.first().toUtf8();

        MoxController* mox = core.model->moxController();
        mox->setMox(true, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Tx);

        qint64 writeId = 100;
        const auto write = [&](LoopbackTransport* app, const QByteArray& key,
                               const MirrorUpdate& update) -> QJsonObject {
            const qint64 id = ++writeId;
            app->sendText(SessionMessages::encode(
                SessionMessages::propertyWrite(key, {update}, static_cast<quint32>(id))));
            const bool answered =
                QTest::qWaitFor([app, id]() { return !propertyResult(app, id).isEmpty(); }, 5000);
            Q_UNUSED(answered);
            const QJsonArray results = propertyResult(app, id).value(QStringLiteral("results")).toArray();
            return results.isEmpty() ? QJsonObject{} : results.first().toObject();
        };

        // B: its own slice's antennas, PureSignal, the transmitter's
        // PureSignal switch.
        for (const auto& [key, update] :
             QList<QPair<QByteArray, MirrorUpdate>>{
                 {bSlice, utf8("rxAntenna", QStringLiteral("ANT2"))},
                 {bSlice, utf8("txAntenna", QStringLiteral("ANT2"))},
                 {"pureSignalSettings", MirrorUpdate{0, "autoAttenuate", MirrorWireKind::Bool, QVariant(false)}},
                 {"transmit", MirrorUpdate{0, "pureSig", MirrorWireKind::Bool, QVariant(true)}}}) {
            const QJsonObject result = write(appB, key, update);
            QVERIFY2(!result.value(QStringLiteral("accepted")).toBool(true), key.constData());
            QCOMPARE(result.value(QStringLiteral("reason")).toString(), kOnAir);
        }
        // A's own change is not refused by this rule.
        const QStringList aSlices = heldKeys(appA, QStringLiteral("slice:"));
        QVERIFY(!aSlices.isEmpty());
        const QJsonObject own = write(appA, aSlices.first().toUtf8(),
                                      utf8("rxAntenna", QStringLiteral("ANT2")));
        QVERIFY(own.value(QStringLiteral("reason")).toString() != kOnAir);

        // The Protocol 1 rate change and a C-Tune move of the holder's
        // transmit slice's receiver, through the Core.
        QJsonObject r = core.invoke(appB, "requestSliceSampleRate",
                                    {int64("sliceId", bSlice.mid(6).toInt()), int64("rateHz", 96000)});
        QCOMPARE(r.value(QStringLiteral("reason")).toString(), kOnAir);
        // A C-Tune move of the holder's transmit slice itself is refused;
        // since the several-devices ownership rule (Task 73, merged from the
        // trunk) the slice's owner answers first: only A changes its slice.
        const int txSlice = core.model->txSliceArbiter()->txBoundSliceId();
        const double centreBefore =
            core.model->streamCentreHz(core.model->sliceById(txSlice)->streamIndex());
        r = core.invoke(appB, "requestStreamCentre",
                        {int64("sliceId", txSlice), f64("centreHz", 14150000.0)});
        QVERIFY(!r.value(QStringLiteral("accepted")).toBool(true));
        QCOMPARE(r.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That slice belongs to Grant's iPhone. It can be changed only there."));
        QCOMPARE(core.model->streamCentreHz(core.model->sliceById(txSlice)->streamIndex()),
                 centreBefore);

        // Unkeyed: none of them is refused by this rule.
        mox->setMox(false, keyerFor(a));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        const QJsonObject after = write(appB, bSlice, utf8("rxAntenna", QStringLiteral("ANT2")));
        QVERIFY(after.value(QStringLiteral("reason")).toString() != kOnAir);
        r = core.invoke(appB, "requestSliceSampleRate",
                        {int64("sliceId", bSlice.mid(6).toInt()), int64("rateHz", 96000)});
        QVERIFY(r.value(QStringLiteral("reason")).toString() != kOnAir);
    }

    void theRadiosOwnPttSaysTheRadioIsOnTheAir()
    {
        Core core;
        allowTransmit(core);
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(b);
        LoopbackTransport* appB = core.signIn(b, kTransmitter);
        QVERIFY(admitted(appB));
        core.model->moxController()->onMicPttFromRadio(true);
        QTRY_COMPARE(core.model->moxController()->state(), MoxState::Tx);
        const int txSlice = core.model->txSliceArbiter()->txBoundSliceId();
        const QJsonObject r = core.invoke(appB, "requestStreamCentre",
                                          {int64("sliceId", txSlice), f64("centreHz", 14150000.0)});
        QCOMPARE(r.value(QStringLiteral("reason")).toString(), QStringLiteral("The radio is on the air. Try again when it stops."));
        core.model->moxController()->onMicPttFromRadio(false);
        QTRY_COMPARE(core.model->moxController()->state(), MoxState::Rx);
    }
};

QTEST_MAIN(TstOnAirRefusals)
#include "tst_on_air_refusals.moc"
