// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_phone_take_tx_slice.cpp  (NereusSDR)
// =================================================================
//
// Where transmit lands when a phone takes it without choosing a slice.
// Reproduces the Pi 4 headless Core of 2026-10-14 with no device and no
// radio: a Local-role Core with one slice of its own (slice 0 on 20 m,
// made at start with no owner), an iPhone that signs in as the app does
// (sliceAccess 3, remoteTx, settings schema 0) and gets its saved slice 2
// back on 40 m at admission (ruling 5.2 step 2), then sends tx.tune with
// no tx.setTxSlice. The fake MOX is keyed; nothing reaches a radio.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-14: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-10-14: the phone and a desktop window see exactly one active
//               slice after admission (ruling 5.10). J.J. Boyd (KG4VCF),
//               with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include "core/DeviceLayoutStore.h"
#include "core/SliceOwnership.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/session/SliceAccessPolicy.h"
#include "core/session/StationClient.h"
#include "core/settings/SettingsProxy.h"

#include <QSignalSpy>

namespace {

// The features the iPhone app declares that bear on this case
// (ios/NereusKit/Sources/NereusLink/LinkFeatures.swift, LinkFeatures.app).
const QHash<QByteArray, int> kPhoneApp{{"deviceAuth", 1},
                                       {"sessionHolder", 1},
                                       {"remoteTx", 1},
                                       {"sliceAccess", 3}};

MirrorUpdate boolArg(const char* name, bool value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Bool, QVariant(value)};
}

QString hexShort(const QByteArray& id)
{
    if (id.isEmpty()) {
        return QStringLiteral("(none)");
    }
    if (id == SliceOwnership::stationDevice()) {
        return QStringLiteral("(station)");
    }
    return QString::fromLatin1(id.toHex().left(8));
}

// What the Core held at the moment the phone became the holder, read on
// TransmitHolder::changed after StationServer's own handler (which runs
// bindTransmitSliceForHolder) has returned.
struct AtTake {
    bool seen = false;
    bool held = false;
    QByteArray holder;
    QByteArray subject0;
    QByteArray subject2;
    int activeForPhone = -1;
    int chosenTxForPhone = -1;
    bool sharesSlices = false;
    bool phoneMayTransmitOnActive = false;
    bool frozen = false;
    int flagAfterBind = -1;
    bool moxAtTake = false;
    // bindForHolder asked again with the same state; it is idempotent here
    // (a bind to the slice already bound is a no-op that answers true).
    bool bindProbe = false;
};

} // namespace

class TstPhoneTakeTxSlice : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString profile =
            QStringLiteral("phone-take-tx-slice-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    void aPhoneTakingTransmitByTuneTransmitsOnItsActiveSlice()
    {
        Core core;
        allowTransmit(core);
        core.model->configureStreamPool(5, 5, 192000);
        // The Core's own slice: slice 0 on 20 m, made at start with no owner.
        SliceModel* coreSlice = core.model->sliceById(0);
        QVERIFY(coreSlice != nullptr);
        coreSlice->setFrequency(14225000.0);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QVERIFY(ownership->mark(0).owner.isEmpty());
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        QCOMPARE(arbiter->txBoundSliceId(), 0);

        // The phone's saved slice 2 on 40 m, restored at its admission
        // (ruling 5.2 step 2), as the Pi logged "Placement: slice 2
        // freq=7.1802 MHz prevStream=-1 ... -> NewStream stream=1".
        Device phone;
        core.pair(phone);
        const QByteArray phoneId = phone.key.fingerprint();
        const QString mac = core.model->currentRadioMac();
        QVERIFY(!mac.isEmpty());
        SavedSlice saved;
        saved.id = 2;
        saved.panKey = QStringLiteral("pan-0");
        saved.frequencyHz = 7180200.0;
        saved.dspMode = DSPMode::LSB;
        QVERIFY(DeviceLayoutStore::append(AppSettings::instance(), mac, phoneId, saved, 4));

        LoopbackTransport* app = core.signIn(phone, kPhoneApp);
        QVERIFY(admitted(app));
        SliceModel* phoneSlice = core.model->sliceById(2);
        QVERIFY2(phoneSlice != nullptr, "the phone's saved slice 2 was not restored");
        QCOMPARE(phoneSlice->frequency(), 7180200.0);

        // The phone tunes its slice to 7.1793 MHz, as it did 21:39:20-23.
        app->sendText(SessionMessages::encode(
            SessionMessages::propertyWrite("slice:2", {f64("frequency", 7179300.0)}, 901)));
        QTRY_VERIFY(!propertyResult(app, 901).isEmpty());
        QCOMPARE(phoneSlice->frequency(), 7179300.0);

        const QByteArray subject0Before = ownership->mark(0).subject();
        const QByteArray subject2Before = ownership->mark(2).subject();
        const int activeBefore = ownership->activeFor(phoneId);
        qInfo().noquote() << "Before the take: slice 0 subject" << hexShort(subject0Before)
                          << "slice 2 subject" << hexShort(subject2Before) << "activeFor(phone)"
                          << activeBefore << "flag on" << arbiter->txBoundSliceId()
                          << "phone id" << hexShort(phoneId);

        // Record the Core's view the moment the phone becomes the holder.
        // Connected after StationServer's handler, so it runs after
        // bindTransmitSliceForHolder.
        TransmitHolder* holder = core.server->transmitHolder();
        QVERIFY(!holder->holder().has_value());
        MoxController* mox = core.model->moxController();
        AtTake at;
        StationServer* server = core.server.get();
        QObject::connect(holder, &TransmitHolder::changed, server, [&]() {
            if (at.seen || !holder->isHeldBy(phoneId)) {
                return;
            }
            at.seen = true;
            at.held = holder->state() == TransmitHolder::State::Held;
            at.holder = holder->holder() ? holder->holder()->deviceId : QByteArray();
            at.subject0 = ownership->mark(0).subject();
            at.subject2 = ownership->mark(2).subject();
            at.activeForPhone = ownership->activeFor(phoneId);
            at.chosenTxForPhone = server->chosenTxSliceForTest(phoneId);
            // keyerSharesSlices(phone) is private: it is true for a live
            // session that declared sliceAccess (StationTransmitTake.cpp),
            // which the Core shows the device as sliceAccessVersion.
            at.sharesSlices = app->isOpen() && capability(
                app->received(), QStringLiteral("sliceAccessVersion")).value_or(0) > 0;
            at.phoneMayTransmitOnActive =
                SliceAccessPolicy::mayTransmitOn(*ownership, phoneId, at.activeForPhone);
            at.frozen = arbiter->isFrozen();
            at.flagAfterBind = arbiter->txBoundSliceId();
            at.moxAtTake = mox->isMox();
            at.bindProbe = arbiter->bindForHolder(phoneId, at.chosenTxForPhone);
        });
        QSignalSpy flagMoves(arbiter, &TxSliceArbiter::txBoundSliceChanged);

        // TUNE, with no tx.setTxSlice, from a phone that holds nothing.
        const QJsonObject tune = core.invoke(app, "tx.tune", {boolArg("on", true)});
        QVERIFY2(tune.value(QStringLiteral("accepted")).toBool(false),
                 QJsonDocument(tune).toJson().constData());
        QVERIFY(holder->isHeldBy(phoneId));
        QVERIFY(at.seen);
        QTRY_VERIFY(mox->isMox());
        const int keyedOn = arbiter->txBoundSliceId();

        qInfo().noquote() << "At the take: held" << at.held << "holder" << hexShort(at.holder)
                          << "slice 0 subject" << hexShort(at.subject0) << "slice 2 subject"
                          << hexShort(at.subject2) << "activeFor(phone)" << at.activeForPhone
                          << "chosen tx slice" << at.chosenTxForPhone << "keyerSharesSlices"
                          << at.sharesSlices << "mayTransmitOn(active)"
                          << at.phoneMayTransmitOnActive << "frozen" << at.frozen
                          << "MOX at take" << at.moxAtTake << "bindForHolder probe"
                          << at.bindProbe << "flag after bind"
                          << at.flagAfterBind << "flag moves" << flagMoves.count()
                          << "keyed on slice" << keyedOn;

        // Unkey before anything is asserted, so a failure never leaves the
        // fake MOX keyed.
        const QJsonObject untune = core.invoke(app, "tx.tune", {boolArg("on", false)});
        QVERIFY(untune.value(QStringLiteral("accepted")).toBool(false));
        QTRY_COMPARE(mox->state(), MoxState::Rx);

        // Ruling 5.2: the saved slice comes back first (step 2), then the
        // first device alone on the Core adopts every slice with no owner
        // (step 3), so the phone owns the Core's slice 0 as well as its own
        // slice 2. adoptUnowned makes slice 0, the active one among the
        // unowned, the phone's active slice; the restored slice 2 never
        // became active (RadioModel::addSliceImpl, the station had one).
        QCOMPARE(subject0Before, phoneId);
        QCOMPARE(subject2Before, phoneId);
        QCOMPARE(activeBefore, 0);
        QVERIFY(at.held);
        QCOMPARE(at.holder, phoneId);
        QCOMPARE(at.subject0, phoneId);
        QCOMPARE(at.subject2, phoneId);
        QCOMPARE(at.activeForPhone, 0);
        QCOMPARE(at.chosenTxForPhone, -1);
        QVERIFY(at.sharesSlices);
        QVERIFY(at.phoneMayTransmitOnActive);
        QVERIFY(!at.frozen);
        QVERIFY(!at.moxAtTake);
        QVERIFY(at.bindProbe);
        // Ruling 8.10: with no chosen transmit slice, the new holder's
        // transmit binds to its active slice, slice 0, where the flag
        // already was. TUNE goes out on 20 m, as the Pi did.
        QCOMPARE(at.flagAfterBind, 0);
        QCOMPARE(flagMoves.count(), 0);
        QCOMPARE(keyedOn, 0);
    }

    // The contrast: had the phone made slice 2 its active slice
    // (setActiveSliceById, as the app's activate() and show() send), the
    // same tx.tune binds transmit there (ruling 8.10's active-slice arm).
    void aPhoneWhoseActiveSliceIsItsOwnTunesThere()
    {
        Core core;
        allowTransmit(core);
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14225000.0);
        TxSliceArbiter* arbiter = core.model->txSliceArbiter();
        Device phone;
        core.pair(phone);
        const QByteArray phoneId = phone.key.fingerprint();
        SavedSlice saved;
        saved.id = 2;
        saved.panKey = QStringLiteral("pan-0");
        saved.frequencyHz = 7180200.0;
        saved.dspMode = DSPMode::LSB;
        QVERIFY(DeviceLayoutStore::append(AppSettings::instance(), core.model->currentRadioMac(),
                                          phoneId, saved, 4));
        LoopbackTransport* app = core.signIn(phone, kPhoneApp);
        QVERIFY(admitted(app));
        QVERIFY(core.model->sliceById(2) != nullptr);
        QCOMPARE(core.model->sliceOwnership()->activeFor(phoneId), 0);

        const QJsonObject activate = core.invoke(app, "setActiveSliceById", {int64("sliceId", 2)});
        QVERIFY(activate.value(QStringLiteral("accepted")).toBool(false));
        QCOMPARE(core.model->sliceOwnership()->activeFor(phoneId), 2);
        QCOMPARE(arbiter->txBoundSliceId(), 0);

        MoxController* mox = core.model->moxController();
        const QJsonObject tune = core.invoke(app, "tx.tune", {boolArg("on", true)});
        QVERIFY(tune.value(QStringLiteral("accepted")).toBool(false));
        QTRY_VERIFY(mox->isMox());
        const int keyedOn = arbiter->txBoundSliceId();
        const QJsonObject untune = core.invoke(app, "tx.tune", {boolArg("on", false)});
        QVERIFY(untune.value(QStringLiteral("accepted")).toBool(false));
        QTRY_COMPARE(mox->state(), MoxState::Rx);
        QCOMPARE(keyedOn, 2);
    }

    // Why the app tuned slice 2 while the Core's active slice for it was 0.
    // At admission slice 2 is restored while it is the phone's only slice,
    // so applyActiveSlices flags it active; adoptUnowned then gives the lone
    // phone slice 0 and makes 0 its active slice. Before the fix nothing
    // reran applyActiveSlices, so the phone's wire carried active=true on
    // both of its slices, and the app tunes the first own slice flagged
    // active in the Core's object order (BandSlicesModel.ownActiveSliceId),
    // which can be slice 2. Ruling 5.10: each device sees exactly one.
    void aPhoneSeesExactlyOneActiveSliceAfterAdmission()
    {
        Core core;
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14225000.0);
        Device phone;
        core.pair(phone);
        const QByteArray phoneId = phone.key.fingerprint();
        SavedSlice saved;
        saved.id = 2;
        saved.panKey = QStringLiteral("pan-0");
        saved.frequencyHz = 7180200.0;
        saved.dspMode = DSPMode::LSB;
        QVERIFY(DeviceLayoutStore::append(AppSettings::instance(), core.model->currentRadioMac(),
                                          phoneId, saved, 4));
        LoopbackTransport* app = core.signIn(phone, kPhoneApp);
        QVERIFY(admitted(app));
        QVERIFY(core.model->sliceById(2) != nullptr);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->activeFor(phoneId), 0);
        QVERIFY(ownership->isActive(0));
        QVERIFY(!ownership->isActive(2));

        // Each slice's `active` has reached the phone (the create carries
        // it) before its value is read, so a missing property never passes
        // as false.
        const auto wire = [app](const char* key) {
            return latest(app->received(), QString::fromLatin1(key), QStringLiteral("active"));
        };
        QTRY_VERIFY(wire("slice:0").isBool() && wire("slice:2").isBool());
        QTRY_VERIFY(wire("slice:0").toBool());
        QTRY_VERIFY(!wire("slice:2").toBool());
        QVERIFY(core.model->sliceById(0)->isActive());
        QVERIFY(!core.model->sliceById(2)->isActive());
    }

    // The desktop path: a Remote-role window behind StationClient, signed in
    // by its paired key and admitted the same way (saved slice 2 restored,
    // slice 0 adopted). It moves its active slice on each `active` true
    // edge (StationClient applyMirroredValue, applyStationActiveSlice), so
    // with two flags it lands on whichever arrives last. This case guards
    // the wire the window is sent: before the fix it was sent two flags but
    // happened to land on slice 0 anyway, from the order they arrived in, so
    // only the wire check failed without the fix. Where the window lands is
    // checked too, as the outcome the single flag gives.
    void aDesktopWindowLandsOnTheCoresActiveSlice()
    {
        Core core;
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14225000.0);
        QTemporaryDir keyDir;
        QVERIFY(keyDir.isValid());
        const auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        PairedDevice record;
        record.id = key->fingerprint();
        record.publicKeySpki = key->publicKeySpki();
        record.name = QStringLiteral("Shack MacBook");
        record.kind = QStringLiteral("computer");
        QVERIFY(core.server->deviceStore()->add(record));
        SavedSlice saved;
        saved.id = 2;
        saved.panKey = QStringLiteral("pan-0");
        saved.frequencyHz = 7180200.0;
        saved.dspMode = DSPMode::LSB;
        QVERIFY(DeviceLayoutStore::append(AppSettings::instance(), core.model->currentRadioMac(),
                                          record.id, saved, 4));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, record.name);
        auto* app = new LoopbackTransport(QStringLiteral("desktop"));
        auto* station = new LoopbackTransport(QStringLiteral("core"));
        station->setPeerAddress(QStringLiteral("192.0.2.30"));
        app->setPeerCertificateSha256(core.certSha256());
        station->linkTo(app);
        // StationClient takes `app` as its child (startSession), so the
        // harness's client list must not delete it too.
        core.server->acceptTransport(station);
        window.startSession(app, QString(), QString(),
                            core.server->stationIdentity().fingerprint());
        QTRY_VERIFY(window.isHandshakeComplete());
        QVERIFY(core.model->sliceById(2) != nullptr);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->activeFor(record.id), 0);
        QCOMPARE(ownership->mark(2).owner, record.id);

        QTRY_VERIFY(remote.sliceById(0) != nullptr && remote.sliceById(2) != nullptr);
        // What the Core sent the window: one slice marked active. Both
        // properties have arrived before their values are read.
        const auto wire = [app](const char* key) {
            return latest(app->received(), QString::fromLatin1(key), QStringLiteral("active"));
        };
        QTRY_VERIFY(wire("slice:0").isBool() && wire("slice:2").isBool());
        QTRY_VERIFY(wire("slice:0").toBool());
        QTRY_VERIFY(!wire("slice:2").toBool());
        QTRY_VERIFY(remote.activeSlice() != nullptr && remote.activeSlice()->sliceIndex() == 0);
        QVERIFY(remote.sliceById(0)->isActive());
        QVERIFY(!remote.sliceById(2)->isActive());
        // It stays there: a later change from the Core reaches the window,
        // and its active slice has not moved.
        core.model->sliceById(0)->setFrequency(14230000.0);
        QTRY_COMPARE(remote.sliceById(0)->frequency(), 14230000.0);
        QCOMPARE(remote.activeSlice()->sliceIndex(), 0);
        QVERIFY(!remote.sliceById(2)->isActive());
        window.disconnectFromStation(QStringLiteral("test done"));
    }
};

QTEST_MAIN(TstPhoneTakeTxSlice)
#include "tst_phone_take_tx_slice.moc"
