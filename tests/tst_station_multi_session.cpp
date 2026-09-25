// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_station_multi_session.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 71 (R-IOS-02; the several-devices design, sections
// 4.1 to 4.6, 10.1 and 10.3): several devices signed in to one Core at once,
// over the in-process loopback. The integration harness later tasks (72 to
// 78) extend.
//
// Refusals first (authorisation):
//   - a fifth device, and an older window, meet a full Core after
//     auth.result: session.end "The Core already has four devices
//     connected.", retryable, no code; no session of the four is touched;
//   - a device away within its 180 s holds its place against a fifth; a
//     pairing connection is not counted;
//   - a sign-in by another device never ends a session (the red check for
//     the old preemption);
//   - session.leave from a peer without sessionHolderVersion 1 is refused as
//     an unknown verb is;
//   - `connectedDevices` and sessionHolderVersion reach only a view at
//     minor 11 whose hello declared sessionHolder 1 with deviceAuth 1.
// Then admission: four devices, each with its own connect sequence and the
// same four entries in admission order; the same device again ending its
// older connection with sameDevice and admitted with no question; away and
// its end over an injected clock; leaving on purpose; revoking an away
// device; names and short names; durations measured at send, never from the
// wall clock; the Core's hello declaring sessionHolder 1.
//
// Keys are made at run time in scratch directories. Mirror traffic and
// media on a second device are not asserted: Tasks 72 and 76 own them.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 71 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <atomic>
#include <memory>

#include "core/AppSettings.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"
#include "core/session/DeviceSessionRegistry.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationServer.h"
#include "models/RadioModel.h"

#include "OperatorWording.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

const QString kCoreFull = QStringLiteral("The Core already has four devices connected.");
const QString kSameDevice = QStringLiteral("This device connected again.");
const QString kUnknownVerb =
    QStringLiteral("The Core does not know this request. Updating the Core may help.");

const QHash<QByteArray, int> kHolder{{"deviceAuth", 1}, {"sessionHolder", 1}};

// A device key made at run time, with the words it signs in with.
struct Device {
    QTemporaryDir dir;
    StationIdentity key = StationIdentity::loadOrCreate(dir.path());
    QString name = QStringLiteral("iPhone");
    QString kind = QStringLiteral("phone");
    QString shortName;

    Device() = default;
    Device(const QString& n, const QString& k, const QString& s = QString())
        : name(n), kind(k), shortName(s)
    {
    }

    PairedDevice record() const
    {
        PairedDevice device;
        device.id = key.fingerprint();
        device.publicKeySpki = key.publicKeySpki();
        device.name = name;
        device.kind = kind;
        return device;
    }

    QString id() const { return StationIdentity::toBase64Url(key.fingerprint()); }

    SessionDeviceBlock block(const QByteArray& challenge, const QByteArray& certSha256,
                             const QByteArray& stationSpki) const
    {
        return SessionDeviceBlock{
            id(), StationIdentity::toBase64Url(key.publicKeySpki()), name, kind,
            StationIdentity::toBase64Url(key.sign(DeviceAuthenticator::transcript(
                challenge, certSha256, stationSpki, key.publicKeySpki()))),
            shortName};
    }
};

QList<QJsonObject> ofType(const QList<QByteArray>& received, const QString& type)
{
    QList<QJsonObject> out;
    for (const QByteArray& wire : received) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("type")).toString() == type) {
            out.append(o);
        }
    }
    return out;
}

QJsonObject firstOfType(const QList<QByteArray>& received, const QString& type)
{
    const QList<QJsonObject> all = ofType(received, type);
    return all.isEmpty() ? QJsonObject{} : all.first();
}

// The latest value of `property` on the object `key`, from its create or a
// later delta; invalid when none arrived.
QJsonValue latest(const QList<QByteArray>& received, const QString& key, const QString& property)
{
    QJsonValue value;
    for (const QByteArray& wire : received) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        const QString type = o.value(QStringLiteral("type")).toString();
        if ((type != QStringLiteral("object.create") && type != QStringLiteral("delta"))
            || o.value(QStringLiteral("key")).toString() != key) {
            continue;
        }
        for (const QJsonValue& p : o.value(QStringLiteral("properties")).toArray()) {
            if (p.toObject().value(QStringLiteral("name")).toString() == property) {
                value = p.toObject().value(QStringLiteral("value"));
            }
        }
    }
    return value;
}

QJsonArray connectedList(const LoopbackTransport* app)
{
    return QJsonDocument::fromJson(
               latest(app->received(), QStringLiteral("connectedDevices"),
                      QStringLiteral("listJson"))
                   .toString()
                   .toUtf8())
        .array();
}

QJsonArray devicesList(const LoopbackTransport* app)
{
    return QJsonDocument::fromJson(
               latest(app->received(), QStringLiteral("devices"), QStringLiteral("listJson"))
                   .toString()
                   .toUtf8())
        .array();
}

QJsonObject entryFor(const QJsonArray& list, const QString& deviceId)
{
    for (const QJsonValue& v : list) {
        if (v.toObject().value(QStringLiteral("deviceId")).toString() == deviceId
            || v.toObject().value(QStringLiteral("id")).toString() == deviceId) {
            return v.toObject();
        }
    }
    return {};
}

bool sentConnectedDevices(const QList<QByteArray>& received)
{
    for (const QJsonObject& o : ofType(received, QStringLiteral("object.create"))) {
        if (o.value(QStringLiteral("key")).toString() == QStringLiteral("connectedDevices")) {
            return true;
        }
    }
    for (const QJsonObject& o : ofType(received, QStringLiteral("schema"))) {
        if (o.value(QStringLiteral("class")).toString()
            == QStringLiteral("ConnectedDevicesFacade")) {
            return true;
        }
    }
    return false;
}

std::optional<qint64> capability(const QList<QByteArray>& received, const QString& name)
{
    const QJsonObject caps = firstOfType(received, QStringLiteral("capabilities"));
    for (const QJsonValue& p : caps.value(QStringLiteral("properties")).toArray()) {
        if (p.toObject().value(QStringLiteral("name")).toString() == name) {
            return p.toObject().value(QStringLiteral("value")).toInteger();
        }
    }
    return std::nullopt;
}

// One Core over the loopback, in scratch directories, with an injected
// monotonic clock for its sessions.
struct Core {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;
    QList<LoopbackTransport*> clients;
    quint32 nextCommandId = 1;
    qint64 now = 0;

    explicit Core(bool upgradedWithToken = false)
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        model = std::make_unique<RadioModel>();
        model->setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:01");
        info.name = QStringLiteral("Bench HL2");
        info.boardType = HPSDRHW::HermesLite;
        model->setLastRadioInfoForTest(info);
        model->setConnectionStateForTest(ConnectionState::Connected);
        model->addSlice(QStringLiteral("pan-0"));
        const QString dir = upgradedWithToken
                                ? NereusSDR::Test::seedUpgradedCoreToken(securityDir.path())
                                : NereusSDR::Test::seedCoreIdentity(securityDir.path());
        server = std::make_unique<StationServer>(model.get(), *settings, dir);
        server->setHeartbeatIntervalMs(0);
        server->deviceSessions()->setClock([this]() { return now; });
    }

    ~Core()
    {
        server.reset();
        qDeleteAll(clients);
    }

    DeviceSessionRegistry& sessions() const { return *server->deviceSessions(); }

    void pair(const Device& device) const
    {
        QVERIFY(server->deviceStore()->add(device.record()));
    }

    QByteArray certSha256() const
    {
        QString pin = server->certificateFingerprint();
        pin.remove(QLatin1Char(':'));
        return QByteArray::fromHex(pin.toLatin1());
    }

    LoopbackTransport* open(const QString& address = QStringLiteral("192.0.2.7"))
    {
        auto* app = new LoopbackTransport(QStringLiteral("app"));
        auto* station = new LoopbackTransport(QStringLiteral("station"));
        station->setPeerAddress(address);
        station->linkTo(app);
        clients.append(app);
        server->acceptTransport(station);
        const bool greeted = QTest::qWaitFor([app]() { return !app->received().isEmpty(); }, 5000);
        Q_UNUSED(greeted);
        return app;
    }

    // Hello at `minor` declaring `features`, then `auth`; waits for the end
    // of the connect sequence or the close.
    static void send(LoopbackTransport* app, const SessionMessage& auth, quint16 minor,
                     const QHash<QByteArray, int>& features)
    {
        app->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, minor, 0, QStringLiteral("NereusSDR iPhone"),
            {kSessionProtocolMajor}, features)));
        app->sendText(SessionMessages::encode(auth));
        const bool settled = QTest::qWaitFor(
            [app]() {
                return app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"))
                    || !app->isOpen();
            },
            5000);
        Q_UNUSED(settled);
    }

    SessionMessage deviceAuth(LoopbackTransport* app, const Device& device) const
    {
        const QJsonObject hello = firstOfType(app->received(), QStringLiteral("hello"));
        const QByteArray challenge =
            StationIdentity::fromBase64Url(hello.value(QStringLiteral("challenge")).toString());
        return SessionMessages::authRequest(
            QString(), device.block(challenge, certSha256(), server->stationIdentity().publicKeySpki()));
    }

    // A device's sign-in, however it ends: the client end.
    LoopbackTransport* signIn(const Device& device,
                              const QHash<QByteArray, int>& features = kHolder,
                              quint16 minor = kSessionProtocolMinor)
    {
        LoopbackTransport* app = open();
        send(app, deviceAuth(app, device), minor, features);
        return app;
    }

    LoopbackTransport* tokenSignIn(const QHash<QByteArray, int>& features = {},
                                   const QString& address = QStringLiteral("192.0.2.20"))
    {
        LoopbackTransport* app = open(address);
        send(app, SessionMessages::authRequest(server->token()), kSessionProtocolMinor, features);
        return app;
    }

    QJsonObject invoke(LoopbackTransport* app, const QByteArray& verb,
                       const QList<MirrorUpdate>& arguments = {})
    {
        const quint32 id = nextCommandId++;
        app->sendText(SessionMessages::encode(SessionMessages::commandInvoke(verb, id, arguments)));
        const auto find = [app, id]() {
            for (const QJsonObject& o : ofType(app->received(), QStringLiteral("command.result"))) {
                if (o.value(QStringLiteral("id")).toInteger() == id) {
                    return o;
                }
            }
            return QJsonObject{};
        };
        const bool answered = QTest::qWaitFor([&find]() { return !find().isEmpty(); }, 5000);
        Q_UNUSED(answered);
        return find();
    }
};

bool admitted(const LoopbackTransport* app)
{
    return app != nullptr && app->isOpen()
        && app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete"));
}

QJsonObject endOf(LoopbackTransport* app)
{
    const bool closed = QTest::qWaitFor([app]() { return !app->isOpen(); }, 5000);
    Q_UNUSED(closed);
    return firstOfType(app->received(), QStringLiteral("session.end"));
}

void verifyCoreFull(LoopbackTransport* app)
{
    const QJsonObject result = firstOfType(app->received(), QStringLiteral("auth.result"));
    QCOMPARE(result.value(QStringLiteral("accepted")).toBool(false), true);
    const QJsonObject end = endOf(app);
    QVERIFY(!app->isOpen());
    QCOMPARE(end.value(QStringLiteral("reason")).toString(), kCoreFull);
    QCOMPARE(end.value(QStringLiteral("retryable")).toBool(false), true);
    QVERIFY(!end.contains(QStringLiteral("code")));
    // Nothing of a session reached it.
    QVERIFY(!app->receivedKinds().contains(QByteArrayLiteral("capabilities")));
    QVERIFY(!app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete")));
}

// Waits until `app`'s latest connectedDevices list satisfies `test`.
bool waitForList(const LoopbackTransport* app, const std::function<bool(const QJsonArray&)>& test)
{
    return QTest::qWaitFor([app, &test]() { return test(connectedList(app)); }, 5000);
}

QStringList idsOf(const QJsonArray& list)
{
    QStringList ids;
    for (const QJsonValue& v : list) {
        ids.append(v.toObject().value(QStringLiteral("deviceId")).toString());
    }
    return ids;
}

MirrorUpdate utf8(const char* name, const QString& value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Utf8, QVariant(value)};
}

} // namespace

class TstStationMultiSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        const QString profile =
            QStringLiteral("station-multi-session-%1").arg(QCoreApplication::applicationPid());
        AppSettings::setProfileOverride(profile);
        AppSettings::instance().clear();
    }

    void cleanupTestCase()
    {
        const QString path = AppSettings::instance().filePath();
        QFile::remove(path);
        QFile::remove(path + QStringLiteral(".bak"));
    }

    // ── Refusals ─────────────────────────────────────────────────────────

    void aFifthDeviceMeetsAFullCoreAndNobodyIsEnded()
    {
        Core core;
        Device devices[5];
        for (Device& d : devices) {
            core.pair(d);
        }
        QList<LoopbackTransport*> four;
        for (int i = 0; i < 4; ++i) {
            four.append(core.signIn(devices[i]));
            QVERIFY2(admitted(four.last()), qPrintable(QString::number(i)));
        }
        LoopbackTransport* fifth = core.signIn(devices[4]);
        verifyCoreFull(fifth);
        QVERIFY(OperatorWording::isPlain(kCoreFull));
        for (LoopbackTransport* app : four) {
            QVERIFY(app->isOpen());
            QVERIFY(ofType(app->received(), QStringLiteral("session.end")).isEmpty());
        }
        QCOMPARE(core.sessions().placesTaken(), 4);
    }

    void anOlderWindowMeetsAFullCoreRetryable()
    {
        Core core(/*upgradedWithToken=*/true);
        Device devices[4];
        for (int i = 0; i < 4; ++i) {
            core.pair(devices[i]);
            QVERIFY(admitted(core.signIn(devices[i])));
        }
        // Today's desktop: the token, no features.
        verifyCoreFull(core.tokenSignIn());
        // A window that signs in by key but predates the feature: the same.
        Device window(QStringLiteral("Shack Mac"), QStringLiteral("computer"));
        core.pair(window);
        verifyCoreFull(core.signIn(window, {{"deviceAuth", 1}}));
    }

    void anAwayDeviceHoldsItsPlaceAgainstAFifth()
    {
        Core core;
        Device devices[5];
        QList<LoopbackTransport*> apps;
        for (Device& d : devices) {
            core.pair(d);
        }
        for (int i = 0; i < 4; ++i) {
            apps.append(core.signIn(devices[i]));
            QVERIFY(admitted(apps.last()));
        }
        apps.first()->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(core.sessions().entry(devices[0].key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        core.now = DeviceSessionRegistry::kGraceMs - 1000;
        verifyCoreFull(core.signIn(devices[4]));
    }

    void aPairingConnectionIsNotCounted()
    {
        Core core;
        Device devices[4];
        for (Device& d : devices) {
            core.pair(d);
        }
        // A device pairing by code, held while the code is being hashed.
        std::atomic<bool> release{false};
        core.server->setPairingHasherForTest([&release](const QString&) {
            while (!release.load()) {
                QThread::msleep(5);
            }
            return QByteArray();
        });
        // Released however the test ends, so the Core's destructor can
        // wait for the hash.
        const auto releaseHash = qScopeGuard([&release]() { release = true; });
        core.server->pairingWindow()->reopen();
        Device newcomer(QStringLiteral("New iPad"), QStringLiteral("tablet"));
        LoopbackTransport* pairing = core.open();
        pairing->sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("NereusSDR iPad"),
            {kSessionProtocolMajor}, {{"deviceAuth", 1}})));
        pairing->sendText(SessionMessages::encode(SessionMessages::pairStart(
            QStringLiteral("code"),
            SessionPairDevice{StationIdentity::toBase64Url(newcomer.key.publicKeySpki()),
                              newcomer.name, newcomer.kind})));
        QTRY_VERIFY(core.server->isHashingPairingCodeForTest());
        for (Device& d : devices) {
            QVERIFY(admitted(core.signIn(d)));
        }
        QVERIFY(pairing->isOpen());
        QCOMPARE(core.sessions().placesTaken(), 4);
        release = true;
        QTRY_VERIFY(!core.server->isHashingPairingCodeForTest());
    }

    void anotherDevicesSignInNeverEndsASession()
    {
        // The red check for the old preemption: B signing in leaves A up.
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* first = core.signIn(a, {{"deviceAuth", 1}});
        QVERIFY(admitted(first));
        LoopbackTransport* second = core.signIn(b, {{"deviceAuth", 1}});
        QVERIFY(admitted(second));
        QTest::qWait(50);
        QVERIFY(first->isOpen());
        QVERIFY(ofType(first->received(), QStringLiteral("session.end")).isEmpty());
        // A token window neither.
        Core upgraded(true);
        Device c;
        upgraded.pair(c);
        LoopbackTransport* device = upgraded.signIn(c);
        QVERIFY(admitted(device));
        QVERIFY(admitted(upgraded.tokenSignIn()));
        QVERIFY(device->isOpen());
        QVERIFY(ofType(device->received(), QStringLiteral("session.end")).isEmpty());
    }

    void sessionLeaveFromAnOlderPeerIsAnUnknownVerb()
    {
        Core core;
        Device a;
        core.pair(a);
        LoopbackTransport* app = core.signIn(a, {{"deviceAuth", 1}});
        QVERIFY(admitted(app));
        const QJsonObject result = core.invoke(app, "session.leave");
        QCOMPARE(result.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(result.value(QStringLiteral("reason")).toString(), kUnknownVerb);
        QTest::qWait(50);
        QVERIFY(app->isOpen());
        QCOMPARE(core.sessions().placesTaken(), 1);
        QCOMPARE(core.sessions().entry(a.key.fingerprint())->state,
                 DeviceSessionRegistry::State::Listening);
    }

    void onlyADeclaringDeviceAtMinor11SeesWhoIsOnTheCore()
    {
        Core core(true);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(b);
        core.pair(c);
        // sessionHolder without deviceAuth is not declared.
        LoopbackTransport* noAuth = core.tokenSignIn({{"sessionHolder", 1}});
        QVERIFY(admitted(noAuth));
        QVERIFY(!sentConnectedDevices(noAuth->received()));
        QVERIFY(!capability(noAuth->received(), QStringLiteral("sessionHolderVersion")));
        // Minor 11 without the feature: today's wire.
        LoopbackTransport* plain = core.signIn(a, {{"deviceAuth", 1}});
        QVERIFY(admitted(plain));
        QVERIFY(!sentConnectedDevices(plain->received()));
        QVERIFY(!capability(plain->received(), QStringLiteral("sessionHolderVersion")));
        // An older peer that declares it.
        LoopbackTransport* older =
            core.signIn(b, kHolder, quint16(kRadioIdentitySessionProtocolMinor - 1));
        QVERIFY(admitted(older));
        QVERIFY(!sentConnectedDevices(older->received()));
        QVERIFY(!capability(older->received(), QStringLiteral("sessionHolderVersion")));
        // A device that declares it, with deviceAuth, at minor 11.
        noAuth->closeLink(QStringLiteral("done"));
        QTRY_COMPARE(core.sessions().placesTaken(), 2);
        LoopbackTransport* holder = core.signIn(c);
        QVERIFY(admitted(holder));
        QVERIFY(sentConnectedDevices(holder->received()));
        QCOMPARE(capability(holder->received(), QStringLiteral("sessionHolderVersion")),
                 std::optional<qint64>(1));
        QCOMPARE(latest(holder->received(), QStringLiteral("connectedDevices"),
                        QStringLiteral("deviceLimit"))
                     .toInteger(),
                 qint64(4));
        // Nothing later reaches the others either.
        QTest::qWait(100);
        QVERIFY(!sentConnectedDevices(plain->received()));
        QVERIFY(!sentConnectedDevices(older->received()));
    }

    // ── Admission ────────────────────────────────────────────────────────

    void theCoresHelloDeclaresSessionHolder()
    {
        Core core;
        LoopbackTransport* app = core.open();
        const QJsonObject hello = firstOfType(app->received(), QStringLiteral("hello"));
        QCOMPARE(hello.value(QStringLiteral("features")).toObject()
                     .value(QStringLiteral("sessionHolder")).toInt(),
                 1);
    }

    void fourDevicesAreAdmittedEachWithItsOwnConnectSequence()
    {
        Core core;
        Device devices[4] = {{QStringLiteral("iPhone"), QStringLiteral("phone")},
                             {QStringLiteral("iPad"), QStringLiteral("tablet")},
                             {QStringLiteral("Mac"), QStringLiteral("computer")},
                             {QStringLiteral("Car iPhone"), QStringLiteral("phone")}};
        QList<LoopbackTransport*> apps;
        for (Device& d : devices) {
            core.pair(d);
        }
        for (Device& d : devices) {
            core.now += 1000;
            apps.append(core.signIn(d));
            QVERIFY(admitted(apps.last()));
        }
        QStringList order;
        for (Device& d : devices) {
            order.append(d.id());
        }
        for (LoopbackTransport* app : apps) {
            // Each its own connect sequence, in the link's order.
            const QList<QByteArray> kinds = app->receivedKinds();
            QVERIFY(kinds.indexOf("auth.result") < kinds.indexOf("capabilities"));
            QVERIFY(kinds.indexOf("capabilities") < kinds.indexOf("settings.snapshot"));
            QVERIFY(kinds.indexOf("settings.snapshot") < kinds.indexOf("snapshot.complete"));
            QCOMPARE(kinds.count("snapshot.complete"), 1);
            QVERIFY(ofType(app->received(), QStringLiteral("session.end")).isEmpty());
            QVERIFY(waitForList(app, [&order](const QJsonArray& list) {
                return idsOf(list) == order;
            }));
        }
        const QJsonArray list = connectedList(apps.first());
        for (const QJsonValue& v : list) {
            const QJsonObject e = v.toObject();
            QCOMPARE(e.value(QStringLiteral("state")).toString(), QStringLiteral("listening"));
            QCOMPARE(e.value(QStringLiteral("paired")).toBool(), true);
            QCOMPARE(e.value(QStringLiteral("hostsCore")).toBool(), false);
            QCOMPARE(e.value(QStringLiteral("revocable")).toBool(), true);
            QCOMPARE(e.value(QStringLiteral("holdsTransmit")).toBool(), false);
            QCOMPARE(e.value(QStringLiteral("transmittingForSeconds")).toInt(-1), 0);
            QCOMPARE(e.value(QStringLiteral("awayForSeconds")).toInt(-1), 0);
            QCOMPARE(e.value(QStringLiteral("listeningOn")).toArray().size(), 0);
            QVERIFY(!e.contains(QStringLiteral("transmittingOn")));
        }
        QCOMPARE(entryFor(list, devices[1].id()).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("tablet"));
        // The fourth, admitted last, read the list when it was sent to it.
        QCOMPARE(entryFor(connectedList(apps.last()), devices[0].id())
                     .value(QStringLiteral("connectedForSeconds"))
                     .toInt(),
                 3);
    }

    void theSameDeviceAgainEndsOnlyItsOlderConnection()
    {
        Core core;
        Device a(QStringLiteral("iPhone"), QStringLiteral("phone"), QStringLiteral("Old"));
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        Device d(QStringLiteral("Car"), QStringLiteral("phone"));
        for (Device* x : {&a, &b, &c, &d}) {
            core.pair(*x);
        }
        LoopbackTransport* older = core.signIn(a);
        LoopbackTransport* other = core.signIn(b);
        QVERIFY(admitted(core.signIn(c)));
        QVERIFY(admitted(core.signIn(d)));
        QVERIFY(admitted(older));
        QVERIFY(admitted(other));
        // Full, and yet the same device is let in at once, asked nothing.
        a.shortName = QStringLiteral("New");
        LoopbackTransport* newer = core.signIn(a);
        QVERIFY(admitted(newer));
        const QJsonObject end = endOf(older);
        QCOMPARE(end.value(QStringLiteral("reason")).toString(), kSameDevice);
        QCOMPARE(end.value(QStringLiteral("retryable")).toBool(true), false);
        QCOMPARE(end.value(QStringLiteral("code")).toString(), QStringLiteral("sameDevice"));
        QVERIFY(OperatorWording::isPlain(kSameDevice));
        QVERIFY(other->isOpen());
        QVERIFY(ofType(other->received(), QStringLiteral("session.end")).isEmpty());
        QCOMPARE(core.sessions().placesTaken(), 4);
        // It kept its place, first in the list, and its new short name.
        QVERIFY(waitForList(other, [&a](const QJsonArray& list) {
            return idsOf(list).value(0) == a.id()
                && list.at(0).toObject().value(QStringLiteral("shortName")).toString()
                       == QStringLiteral("New");
        }));
    }

    void anAwayDeviceIsKeptThenFreedAt180Seconds()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        for (Device* x : {&a, &b, &c}) {
            core.pair(*x);
        }
        LoopbackTransport* first = core.signIn(a);
        LoopbackTransport* watcher = core.signIn(b);
        QVERIFY(admitted(first));
        QVERIFY(admitted(watcher));
        core.now = 10000;
        first->closeLink(QStringLiteral("the link dropped"));
        QVERIFY(waitForList(watcher, [&a](const QJsonArray& list) {
            return entryFor(list, a.id()).value(QStringLiteral("state")).toString()
                == QStringLiteral("away");
        }));
        QCOMPARE(core.sessions().placesTaken(), 2);
        // Counting: a device that joins 30 s later is told how long.
        core.now = 40000;
        LoopbackTransport* late = core.signIn(c);
        QVERIFY(admitted(late));
        QCOMPARE(entryFor(connectedList(late), a.id()).value(QStringLiteral("awayForSeconds"))
                     .toInt(),
                 30);
        // Back at 179 s: admitted with no question, its own place.
        core.now = 10000 + 179000;
        LoopbackTransport* back = core.signIn(a);
        QVERIFY(admitted(back));
        QVERIFY(waitForList(watcher, [&a](const QJsonArray& list) {
            return idsOf(list).value(0) == a.id()
                && list.at(0).toObject().value(QStringLiteral("state")).toString()
                       == QStringLiteral("listening");
        }));

        // Away again; at 180 s its place frees and the list drops it.
        core.now = 300000;
        back->closeLink(QStringLiteral("the link dropped again"));
        QTRY_COMPARE(core.sessions().entry(a.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        core.now = 300000 + 179999;
        QVERIFY(core.sessions().expireAway().isEmpty());
        QCOMPARE(core.sessions().placesTaken(), 3);
        core.now = 300000 + 180000;
        QCOMPARE(core.sessions().expireAway().size(), 1);
        QCOMPARE(core.sessions().placesTaken(), 2);
        QVERIFY(core.sessions().timeRanOutAtMs(a.key.fingerprint()));
        QVERIFY(waitForList(watcher, [&a](const QJsonArray& list) {
            return list.size() == 2 && entryFor(list, a.id()).isEmpty();
        }));
    }

    void aTokenWindowsDropFreesItsPlaceAtOnce()
    {
        Core core(true);
        Device a;
        core.pair(a);
        LoopbackTransport* watcher = core.signIn(a);
        LoopbackTransport* window = core.tokenSignIn();
        QVERIFY(admitted(watcher));
        QVERIFY(admitted(window));
        QVERIFY(waitForList(watcher, [](const QJsonArray& list) { return list.size() == 2; }));
        window->closeLink(QStringLiteral("closed"));
        QVERIFY(waitForList(watcher, [](const QJsonArray& list) { return list.size() == 1; }));
        QCOMPARE(core.sessions().placesTaken(), 1);
    }

    void revokingAnAwayDeviceFreesItsPlaceAtOnce()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* first = core.signIn(a);
        LoopbackTransport* watcher = core.signIn(b);
        QVERIFY(admitted(first));
        QVERIFY(admitted(watcher));
        first->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(core.sessions().entry(a.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        const QJsonObject result = core.invoke(watcher, "devices.revoke", {utf8("id", a.id())});
        QVERIFY2(result.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(result.value(QStringLiteral("reason")).toString()));
        QCOMPARE(core.sessions().placesTaken(), 1);
        QVERIFY(!core.sessions().timeRanOutAtMs(a.key.fingerprint()));
        QVERIFY(waitForList(watcher, [](const QJsonArray& list) { return list.size() == 1; }));
    }

    void leavingOnPurposeFreesThePlaceAtOnce()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* leaver = core.signIn(a);
        LoopbackTransport* watcher = core.signIn(b);
        QVERIFY(admitted(leaver));
        QVERIFY(admitted(watcher));
        const QJsonObject result = core.invoke(leaver, "session.leave");
        QVERIFY2(result.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(result.value(QStringLiteral("reason")).toString()));
        QCOMPARE(result.value(QStringLiteral("reason")).toString(), QString());
        // No away state: the place is free at once and the list drops it.
        QCOMPARE(core.sessions().placesTaken(), 1);
        QVERIFY(!core.sessions().entry(a.key.fingerprint()));
        QTRY_VERIFY(!leaver->isOpen());
        QVERIFY(waitForList(watcher, [&a](const QJsonArray& list) {
            return list.size() == 1 && entryFor(list, a.id()).isEmpty();
        }));
    }

    // ── The count before connecting (ruling 10.4) ────────────────────────

    void discoveryCountsThePlacesTakenAndAnUnclaimedCoreSendsNone()
    {
        Core unclaimed;
        QVERIFY(!unclaimed.server->deviceStore()->isClaimed());
        QCOMPARE(unclaimed.server->devicesConnectedForDiscovery(), 0);

        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        QCOMPARE(core.server->devicesConnectedForDiscovery(), 0);
        LoopbackTransport* first = core.signIn(a);
        QVERIFY(admitted(first));
        QVERIFY(admitted(core.signIn(b)));
        QCOMPARE(core.server->devicesConnectedForDiscovery(), 2);
        // Away still holds its place, so it still counts.
        first->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(core.sessions().entry(a.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        QCOMPARE(core.server->devicesConnectedForDiscovery(), 2);
        // A hosting desktop's own window takes a place too.
        core.sessions().registerHostingDevice(QByteArray(32, 'h'), QStringLiteral("Shack Mac"),
                                              QStringLiteral("Mac"));
        QCOMPARE(core.server->devicesConnectedForDiscovery(), 3);
    }

    // ── Names and short names ────────────────────────────────────────────

    void namesAndShortNamesAreNumberedTheSameOnBothLists()
    {
        Core core(true);
        Device first(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device second(QStringLiteral("iPhone"), QStringLiteral("phone"));
        Device grant(QStringLiteral("Grant's iPhone"), QStringLiteral("phone"),
                     QStringLiteral("Grant's"));
        core.pair(first);
        core.pair(second);
        core.pair(grant);
        // The later-paired one signs in first; the number still goes to it.
        LoopbackTransport* b = core.signIn(second);
        LoopbackTransport* a = core.signIn(first);
        LoopbackTransport* g = core.signIn(grant);
        LoopbackTransport* window = core.tokenSignIn({}, QStringLiteral("192.168.1.20"));
        for (LoopbackTransport* app : {a, b, g, window}) {
            QVERIFY(admitted(app));
        }
        QVERIFY(waitForList(g, [](const QJsonArray& list) { return list.size() == 4; }));
        const QJsonArray connected = connectedList(g);
        QCOMPARE(entryFor(connected, first.id()).value(QStringLiteral("name")).toString(),
                 QStringLiteral("iPhone"));
        QCOMPARE(entryFor(connected, second.id()).value(QStringLiteral("name")).toString(),
                 QStringLiteral("iPhone 2"));
        QCOMPARE(entryFor(connected, first.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Phone"));
        QCOMPARE(entryFor(connected, second.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Phone 2"));
        QCOMPARE(entryFor(connected, grant.id()).value(QStringLiteral("name")).toString(),
                 QStringLiteral("Grant's iPhone"));
        QCOMPARE(entryFor(connected, grant.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Grant's"));
        const QJsonObject token = connected.at(3).toObject();
        QCOMPARE(token.value(QStringLiteral("deviceId")).toString(), QStringLiteral("token:1"));
        QCOMPARE(token.value(QStringLiteral("name")).toString(),
                 QStringLiteral("Computer at 192.168.1.20"));
        QCOMPARE(token.value(QStringLiteral("shortName")).toString(), QStringLiteral("Computer"));
        QCOMPARE(token.value(QStringLiteral("paired")).toBool(true), false);
        QCOMPARE(token.value(QStringLiteral("revocable")).toBool(true), false);
        QCOMPARE(token.value(QStringLiteral("kind")).toString(), QStringLiteral("computer"));

        const QJsonArray paired = devicesList(g);
        QCOMPARE(entryFor(paired, first.id()).value(QStringLiteral("name")).toString(),
                 QStringLiteral("iPhone"));
        QCOMPARE(entryFor(paired, second.id()).value(QStringLiteral("name")).toString(),
                 QStringLiteral("iPhone 2"));
        QCOMPARE(entryFor(paired, second.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Phone 2"));
        QCOMPARE(entryFor(paired, grant.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Grant's"));

        // A new short name at sign-in replaces the stored one everywhere.
        second.shortName = QStringLiteral("Pocket");
        LoopbackTransport* again = core.signIn(second);
        QVERIFY(admitted(again));
        QVERIFY(waitForList(g, [&second](const QJsonArray& list) {
            return entryFor(list, second.id()).value(QStringLiteral("shortName")).toString()
                == QStringLiteral("Pocket");
        }));
        QTRY_COMPARE(entryFor(devicesList(g), second.id())
                         .value(QStringLiteral("shortName"))
                         .toString(),
                     QStringLiteral("Pocket"));
        QCOMPARE(core.server->deviceStore()->find(second.key.fingerprint())->shortName,
                 QStringLiteral("Pocket"));
    }

    void anUnusableShortNameIsTheKindsWord()
    {
        Core core;
        Device tooLong(QStringLiteral("Long iPad"), QStringLiteral("tablet"),
                       QString(33, QLatin1Char('x')));
        Device control(QStringLiteral("Control iPhone"), QStringLiteral("phone"),
                       QStringLiteral("Pocket\x01"));
        Device blank(QStringLiteral("Blank Mac"), QStringLiteral("computer"),
                     QStringLiteral("   "));
        for (Device* d : {&tooLong, &control, &blank}) {
            core.pair(*d);
        }
        LoopbackTransport* watcher = core.signIn(tooLong);
        QVERIFY(admitted(watcher));
        QVERIFY(admitted(core.signIn(control)));
        QVERIFY(admitted(core.signIn(blank)));
        QVERIFY(waitForList(watcher, [](const QJsonArray& list) { return list.size() == 3; }));
        const QJsonArray list = connectedList(watcher);
        QCOMPARE(entryFor(list, tooLong.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Tablet"));
        QCOMPARE(entryFor(list, control.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Phone"));
        QCOMPARE(entryFor(list, blank.id()).value(QStringLiteral("shortName")).toString(),
                 QStringLiteral("Computer"));
    }

    // ── Durations ────────────────────────────────────────────────────────

    void durationsAreMeasuredAtSendAndNeverFromTheWallClock()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        for (Device* x : {&a, &b, &c}) {
            core.pair(*x);
        }
        core.now = 0;
        LoopbackTransport* first = core.signIn(a);
        QVERIFY(admitted(first));
        // The wall clock jumps by a day; the Core's monotonic clock does not.
        core.server->deviceStore()->setClock(
            []() { return QDateTime::currentDateTimeUtc().addDays(1); });
        core.now = 5000;
        LoopbackTransport* second = core.signIn(b);
        QVERIFY(admitted(second));
        QCOMPARE(entryFor(connectedList(second), a.id())
                     .value(QStringLiteral("connectedForSeconds")).toInt(),
                 5);

        // Heartbeats do not move lastActivitySeconds; a command does, at
        // most once a minute.
        const int before = ofType(second->received(), QStringLiteral("delta")).size();
        core.server->setHeartbeatIntervalMs(10);
        QTest::qWait(60);
        core.server->setHeartbeatIntervalMs(0);
        core.now = 70000;
        LoopbackTransport* third = core.signIn(c);
        QVERIFY(admitted(third));
        QCOMPARE(entryFor(connectedList(third), a.id())
                     .value(QStringLiteral("lastActivitySeconds")).toInt(),
                 70);
        core.invoke(first, "station.rename", {utf8("label", QStringLiteral("KG4VCF"))});
        QVERIFY(waitForList(second, [&a](const QJsonArray& list) {
            return entryFor(list, a.id()).value(QStringLiteral("lastActivitySeconds")).toInt()
                == 0;
        }));
        const int afterFirst = ofType(second->received(), QStringLiteral("delta")).size();
        QVERIFY(afterFirst > before);
        // A second command within the minute re-sends nothing about it.
        core.now = 80000;
        const QByteArray revisionBefore =
            QJsonDocument(QJsonArray{latest(second->received(), QStringLiteral("connectedDevices"),
                                            QStringLiteral("revision"))})
                .toJson();
        core.invoke(first, "station.rename", {utf8("label", QStringLiteral("KG4VCF/shack"))});
        QTest::qWait(120);
        const QByteArray revisionAfter =
            QJsonDocument(QJsonArray{latest(second->received(), QStringLiteral("connectedDevices"),
                                            QStringLiteral("revision"))})
                .toJson();
        QCOMPARE(revisionAfter, revisionBefore);
    }
};

QTEST_MAIN(TstStationMultiSession)
#include "tst_station_multi_session.moc"
