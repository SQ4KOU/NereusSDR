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
// Then, from Task 72 (sections 5.5, rulings 5.6 to 5.8), what each device
// receives: A's change to a blanker two slices share reaches B and not A,
// whose property.result carries the readback; a newcomer's attach leaves
// another device's pending deltas in place and carries current values;
// command.result, property.result and settings.reject go only to the device
// that asked, settings.value to every device with its writer's origin; a
// DSP-asset job ends with its own device's session and no other.
//
// Then, from Task 73 (sections 5.1 to 5.9, rulings 5.1 to 5.14), whose each
// slice is. Refusals first (authorisation): a write to another device's slice
// or to any marker, and removeSlice, setActiveSliceById, nnr.* and notch.add
// naming another device's slice, are refused with the plain reason naming
// its owner, and nothing changes (the red check: take the owner filter out
// and this fails). Then: each device receives its own slices and a marker
// for every other slice, an older window its own slices and no marker; the
// first device alone adopts the slices nobody owns, a later one gets a first
// slice on the station-level active slice's receiver, costing no receiver,
// or none with the slice cap full; each device has its own active slice and
// the station-level one follows the most recent choice, the FreeDV Reporter
// frequency with it; a device away keeps its slices for its 180 s, then they
// close and are saved (restored under its old letter when free, else the
// lowest free, with their settings), or pass to the station device held for
// it when it was the last device, never adopted by another and its own again
// when it signs in; revoking closes its slices, held ones included, and
// forgets its layout; connectedDevices lists what each device listens on.
//
// Then, from Task 76 (the several-devices design, rulings 9.1 to 9.4),
// media and capacity per device: each admitted device gets its own media
// controller (DaemonMediaHub) and its own share of the display budget in
// its own capabilities (sharedConnection, or sharedProcessing under the
// governor's cut, only while another device is admitted; coreBusy or none
// to a device that did not declare sessionHolder); A's media control
// reaches A's controller only; each device hears only its own slices
// (distinct tones per slice, read back from each device's lossless audio);
// displays and receiver streams only for its own slices; two devices
// watching one receiver share one FFT; B leaving ends only B's media; the
// Core's local output plays only the station device's slices; telemetry
// reaches every session that negotiated it.
//
// Keys are made at run time in scratch directories.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 71 (R-IOS-02), with
//               AI-assisted implementation via Anthropic Claude Code.
//   2026-09-25: iPhone app plan Task 72 (R-IOS-02): a mirror view per
//               device, echo per writer and routing per session. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-25: iPhone app plan Task 73 (R-IOS-02): slice ownership,
//               markers, refusals for another device's slice, the active
//               slice per device, held slices and saved layouts. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-25: iPhone app plan Task 74 (R-IOS-02): the anchor passes
//               with the C-Tune pin. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-25: iPhone app plan Task 76 (R-IOS-31): media and capacity
//               per device. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QCryptographicHash>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <QtEndian>

#include <atomic>
#include <memory>

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/DeviceLayoutStore.h"
#include "core/SliceOwnership.h"
#include "core/security/DeviceAuthenticator.h"
#include "core/security/DeviceStore.h"
#include "core/security/PairingWindow.h"
#include "core/security/StationIdentity.h"
#include "core/session/DeviceSessionRegistry.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationServer.h"
#include "core/WdspTypes.h"
#include "core/dsp/DspAssetService.h"
#include "core/daemon/DaemonTelemetryController.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "models/NotchModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

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

MirrorUpdate int64(const char* name, qint64 value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Int64, QVariant(value)};
}

// Every value `property` of `key` carried in a delta from message `from` on.
QList<QJsonValue> deltaValues(const QList<QByteArray>& received, int from, const QString& key,
                              const QString& property)
{
    QList<QJsonValue> values;
    for (int i = std::max(0, from); i < received.size(); ++i) {
        const QJsonObject o = QJsonDocument::fromJson(received.at(i)).object();
        if (o.value(QStringLiteral("type")).toString() != QStringLiteral("delta")
            || o.value(QStringLiteral("key")).toString() != key) {
            continue;
        }
        for (const QJsonValue& p : o.value(QStringLiteral("properties")).toArray()) {
            if (p.toObject().value(QStringLiteral("name")).toString() == property) {
                values.append(p.toObject().value(QStringLiteral("value")));
            }
        }
    }
    return values;
}

int countOfType(const QList<QByteArray>& received, int from, const QString& type)
{
    int count = 0;
    for (int i = std::max(0, from); i < received.size(); ++i) {
        if (QJsonDocument::fromJson(received.at(i)).object().value(QStringLiteral("type")).toString()
            == type) {
            ++count;
        }
    }
    return count;
}

// Task 73: whether `key` was created on `app` and not destroyed since.
bool holds(const LoopbackTransport* app, const QString& key)
{
    bool held = false;
    for (const QByteArray& wire : app->received()) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        if (o.value(QStringLiteral("key")).toString() != key) {
            continue;
        }
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("object.create")) {
            held = true;
        } else if (type == QStringLiteral("object.destroy")) {
            held = false;
        }
    }
    return held;
}

// Every key with `prefix` `app` holds now.
QStringList heldKeys(const LoopbackTransport* app, const QString& prefix)
{
    QStringList keys;
    for (const QByteArray& wire : app->received()) {
        const QString key = QJsonDocument::fromJson(wire).object().value(QStringLiteral("key")).toString();
        if (key.startsWith(prefix) && !keys.contains(key) && holds(app, key)) {
            keys.append(key);
        }
    }
    keys.sort();
    return keys;
}

// Whether any object message of `app` (create, delta, destroy) carried
// `key`: a property.result names the key it answers and is not one.
bool everSaw(const LoopbackTransport* app, const QString& key)
{
    for (const QByteArray& wire : app->received()) {
        const QJsonObject o = QJsonDocument::fromJson(wire).object();
        const QString type = o.value(QStringLiteral("type")).toString();
        if ((type == QStringLiteral("object.create") || type == QStringLiteral("delta")
             || type == QStringLiteral("object.destroy"))
            && o.value(QStringLiteral("key")).toString() == key) {
            return true;
        }
    }
    return false;
}

// Every value of the property.result for `writeId` on `app`.
QJsonObject propertyResult(const LoopbackTransport* app, qint64 writeId)
{
    for (const QJsonObject& o : ofType(app->received(), QStringLiteral("property.result"))) {
        if (o.value(QStringLiteral("writeId")).toInteger() == writeId) {
            return o;
        }
    }
    return {};
}

// The plain refusal for another device's slice (ruling 5.9).
QString ownedElsewhere(const QString& ownerName)
{
    return QStringLiteral("That slice belongs to %1. It can be changed only there.").arg(ownerName);
}

MirrorUpdate f64(const char* name, double value)
{
    return MirrorUpdate{0, QByteArray(name), MirrorWireKind::Float64, QVariant(value)};
}

// Which slice ids hold at least one receiver, by receiver.
int receiversInUse(const RadioModel& model)
{
    QSet<int> streams;
    for (const SliceModel* slice : model.slices()) {
        if (slice->streamIndex() >= 0) {
            streams.insert(slice->streamIndex());
        }
    }
    return streams.size();
}

// A second slice 10 kHz from slice 0, so the two share one receiver and
// its one noise blanker (tst_mirror_inbound's co-hosting).
int addCoHostedSlice(RadioModel& model)
{
    model.configureStreamPool(5, 5, 192000);
    const int second = model.addSlice(QStringLiteral("pan-0"));
    model.sliceById(0)->setFrequency(14200000.0);
    model.sliceById(second)->setFrequency(14210000.0);
    return second;
}


// ── Task 76: media per device ───────────────────────────────────────────

// A media transport that records what it is given; ready on demand.
class MediaFake final : public IMediaTransport {
public:
    explicit MediaFake(QObject* parent = nullptr) : IMediaTransport(parent) {}
    bool start(const StartOptions& options) override
    {
        startOptions = options;
        started = true;
        return true;
    }
    void stop() override { started = readyState = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& bytes) override
    {
        if (readyState) { displays.append(bytes); }
        return readyState;
    }
    bool sendRtp(const QByteArray& packet) override
    {
        if (readyState) { rtpPackets.append(packet); }
        return readyState;
    }
    bool isReady() const override { return readyState; }
    bool losslessAudioNegotiated() const override { return true; }
    void becomeReady() { readyState = true; emit ready(); }

    bool started{false};
    bool readyState{false};
    StartOptions startOptions{Role::Answerer, 0};
    QList<QByteArray> displays;
    QList<QByteArray> rtpPackets;
};

constexpr char kMediaConnection[] = "11111111-2222-4333-8444-555555555555";

void sendMedia(LoopbackTransport* app, const QJsonObject& payload)
{
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = payload;
    app->sendText(SessionMessages::encode(message));
}

// Every media control `op` the Core sent `app`, in arrival order.
QList<QJsonObject> mediaOps(const LoopbackTransport* app, const QString& op)
{
    QList<QJsonObject> out;
    for (const QByteArray& wire : app->received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::MediaControl
            && message.mediaPayload.value(QStringLiteral("op")).toString() == op) {
            out.append(message.mediaPayload);
        }
    }
    return out;
}

// The last value of capability `name` the Core told `app`.
QJsonValue latestCapability(const QList<QByteArray>& received, const QString& name)
{
    QJsonValue value;
    for (const QJsonObject& caps : ofType(received, QStringLiteral("capabilities"))) {
        for (const QJsonValue& p : caps.value(QStringLiteral("properties")).toArray()) {
            if (p.toObject().value(QStringLiteral("name")).toString() == name) {
                value = p.toObject().value(QStringLiteral("value"));
            }
        }
    }
    return value;
}

// The media start of an app that understands audio profiles and receiver
// streams.
QJsonObject mediaStart()
{
    return {{QStringLiteral("op"), QStringLiteral("start")},
            {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
            {QStringLiteral("audioProfileVersion"), 1},
            {QStringLiteral("receiverAudioVersion"), 1}};
}

QJsonObject spectrumPlane()
{
    return {{QStringLiteral("detector"), 0},
            {QStringLiteral("averageMode"), -1},
            {QStringLiteral("averageAlpha"), 0.0}};
}

QJsonObject displayRequest(quint32 endpointId, int sliceId, double centreHz)
{
    return {{QStringLiteral("op"), QStringLiteral("subscribe")},
            {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
            {QStringLiteral("endpointId"), static_cast<qint64>(endpointId)},
            {QStringLiteral("revision"), 1},
            {QStringLiteral("sliceId"), sliceId},
            {QStringLiteral("tier"), QStringLiteral("wide")},
            {QStringLiteral("fftSize"), 1024},
            {QStringLiteral("windowType"), 0},
            {QStringLiteral("centreHz"), centreHz},
            {QStringLiteral("spanHz"), 48000.0},
            {QStringLiteral("pixels"), 128},
            {QStringLiteral("fps"), 10},
            {QStringLiteral("framesPerLine"), 1},
            {QStringLiteral("trace"), spectrumPlane()},
            {QStringLiteral("waterfall"), spectrumPlane()},
            {QStringLiteral("minDbm"), -180.0},
            {QStringLiteral("maxDbm"), 0.0},
            {QStringLiteral("wideSpanFactor"), 0.0}};
}

// The left samples of an L16 packet (RTP header of 12 bytes, big-endian
// 16-bit stereo), each as the float it was.
QList<float> l16Left(const QByteArray& packet)
{
    QList<float> left;
    for (qsizetype at = 12; at + 4 <= packet.size(); at += 4) {
        left.append(static_cast<float>(qFromBigEndian<qint16>(packet.constData() + at))
                    / 32768.0f);
    }
    return left;
}

// Two devices on one Core with media on and a hub making their media
// controllers; A holds slice 0, B its own first slice.
struct MediaCore {
    Core core{/*upgradedWithToken=*/true};
    Device a{QStringLiteral("iPhone"), QStringLiteral("phone")};
    Device b{QStringLiteral("iPad"), QStringLiteral("tablet")};
    QList<QPointer<MediaFake>> transports;
    std::unique_ptr<DaemonMediaHub> hub;
    LoopbackTransport* appA = nullptr;
    LoopbackTransport* appB = nullptr;

    explicit MediaCore(std::optional<DisplayBudgetLimits> budget = std::nullopt)
    {
        // Receivers for the slices, so displays have a stream to ride.
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        core.server->setMediaEnabled(true);
        core.server->setTelemetryEnabled(true);
        if (budget) {
            core.server->setDisplayBudgetLimits(*budget);
        }
        hub = std::make_unique<DaemonMediaHub>(
            core.server.get(), core.model.get(), nullptr,
            [this](QObject* parent) -> IMediaTransport* {
                auto* transport = new MediaFake(parent);
                transports.append(transport);
                return transport;
            });
        core.pair(a);
        core.pair(b);
    }

    ~MediaCore()
    {
        hub.reset();
    }

    void signInBoth()
    {
        appA = core.signIn(a);
        QVERIFY(admitted(appA));
        appB = core.signIn(b);
        QVERIFY(admitted(appB));
        QTRY_COMPARE(hub->controllerCount(), 2);
    }

    quint64 epochOf(int index) const { return core.server->mediaSessionEpochs().at(index); }

    int sliceOf(quint64 epoch) const
    {
        for (const SliceModel* slice : core.model->slices()) {
            if (core.server->mediaSessionOwnsSlice(epoch, slice->sliceIndex())) {
                return slice->sliceIndex();
            }
        }
        return -1;
    }

    // Starts `app`'s media and makes its transport (the next one made)
    // ready.
    MediaFake* startMedia(LoopbackTransport* app)
    {
        const qsizetype before = transports.size();
        sendMedia(app, mediaStart());
        if (!QTest::qWaitFor([this, before]() { return transports.size() > before; }, 5000)) {
            return nullptr;
        }
        MediaFake* transport = transports.last();
        transport->becomeReady();
        return transport;
    }
};

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
            // Task 73: the first adopted slice 0, each later one got a slice.
            QCOMPARE(e.value(QStringLiteral("listeningOn")).toArray().size(), 1);
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

    // ── A mirror view per device (Task 72) ───────────────────────────────

    void aSharedBlankerChangeReachesTheOtherDeviceNotTheWriter()
    {
        Core core;
        const int second = addCoHostedSlice(*core.model);
        QVERIFY(core.model->sliceById(0)->streamIndex() >= 0);
        QCOMPARE(core.model->sliceById(second)->streamIndex(),
                 core.model->sliceById(0)->streamIndex());
        QCOMPARE(core.model->sliceById(second)->nbMode(), NbMode::Off);
        const QString sharedKey = QStringLiteral("slice:%1").arg(second);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        const QStringList bKeys = heldKeys(appB, QStringLiteral("slice:"));
        QCOMPARE(bKeys.size(), 1);
        const QString bKey = bKeys.first();
        const int bSlice = bKey.mid(6).toInt();
        const int fromA = appA->received().size();
        const int fromB = appB->received().size();

        appA->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:0",
            {MirrorUpdate{0, "nbMode", MirrorWireKind::Enum,
                          QVariant(static_cast<int>(NbMode::NB))}},
            77)));

        // A: the blanker is the shared receiver's, so B's slice there makes
        // it a change that affects B (ruling 6.1, iPhone app Task 75): held
        // and asked. Nothing changes until A goes ahead.
        QVERIFY(QTest::qWaitFor(
            [appA]() { return !firstOfType(appA->received(), QStringLiteral("confirm.request")).isEmpty(); },
            5000));
        const QJsonObject held = firstOfType(appA->received(), QStringLiteral("property.result"));
        QCOMPARE(held.value(QStringLiteral("writeId")).toInteger(), 77);
        QCOMPARE(held.value(QStringLiteral("results")).toArray().first().toObject()
                     .value(QStringLiteral("reason")).toString(),
                 QStringLiteral("Waiting for you to confirm."));
        QCOMPARE(core.model->sliceById(second)->nbMode(), NbMode::Off);
        const QJsonObject question = firstOfType(appA->received(), QStringLiteral("confirm.request"));
        QCOMPARE(question.value(QStringLiteral("kind")).toString(), QStringLiteral("sharedSetting"));
        const QJsonObject proceeded = core.invoke(
            appA, "confirm.proceed",
            {int64("id", question.value(QStringLiteral("id")).toInteger()), int64("choice", -1)});
        QCOMPARE(proceeded.value(QStringLiteral("accepted")).toBool(false), true);
        // Its readback, with the value the Core kept (ruling 7.4a).
        bool readBack = false;
        for (const QJsonValue& v : proceeded.value(QStringLiteral("values")).toArray()) {
            if (v.toObject().value(QStringLiteral("name")).toString() == QStringLiteral("nbMode")) {
                QCOMPARE(v.toObject().value(QStringLiteral("value")).toInt(),
                         static_cast<int>(NbMode::NB));
                readBack = true;
            }
        }
        QVERIFY(readBack);
        QCOMPARE(core.model->sliceById(second)->nbMode(), NbMode::NB);

        // B: A's two slices are A's (Task 73), so B never sees them; the
        // blanker change reaches B on its own slice, which joined the same
        // receiver when B was admitted.
        QCOMPARE(core.model->sliceById(bSlice)->streamIndex(),
                 core.model->sliceById(0)->streamIndex());
        QCOMPARE(core.model->sliceById(bSlice)->nbMode(), NbMode::NB);
        QVERIFY(QTest::qWaitFor(
            [&]() {
                return !deltaValues(appB->received(), fromB, bKey, QStringLiteral("nbMode")).isEmpty();
            },
            5000));
        QCOMPARE(deltaValues(appB->received(), fromB, bKey, QStringLiteral("nbMode")).last().toInt(),
                 static_cast<int>(NbMode::NB));
        QVERIFY(!everSaw(appB, QStringLiteral("slice:0")));
        QVERIFY(!everSaw(appB, sharedKey));
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("property.result")), 0);
        // B is told who changed what.
        QTRY_COMPARE(countOfType(appB->received(), fromB, QStringLiteral("notice")), 1);

        // A: no echo of its own write, on either slice, over several flushes.
        QTest::qWait(3 * StationServer::kDefaultDeltaFlushMs);
        QVERIFY(deltaValues(appA->received(), fromA, QStringLiteral("slice:0"),
                            QStringLiteral("nbMode")).isEmpty());
        QVERIFY(deltaValues(appA->received(), fromA, sharedKey, QStringLiteral("nbMode")).isEmpty());
    }

    void aNewcomerLeavesAnotherDevicesDeltasInPlace()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        const int fromB = appB->received().size();

        // Pending for B when A attaches (the flush tick has not run).
        core.model->sliceById(0)->setFrequency(7151000.0);
        core.model->sliceById(0)->setAfGain(31);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));

        // A's burst holds the current values: slice 0 is B's (Task 73), so
        // A holds it as a marker.
        QCOMPARE(latest(appA->received(), QStringLiteral("marker:0"), QStringLiteral("frequency"))
                     .toDouble(),
                 7151000.0);
        QVERIFY(!everSaw(appA, QStringLiteral("slice:0")));
        // Every one of B's arrives, and none of A's burst.
        QVERIFY(QTest::qWaitFor(
            [&]() {
                return !deltaValues(appB->received(), fromB, QStringLiteral("slice:0"),
                                    QStringLiteral("frequency")).isEmpty()
                    && !deltaValues(appB->received(), fromB, QStringLiteral("slice:0"),
                                    QStringLiteral("afGain")).isEmpty();
            },
            5000));
        QCOMPARE(deltaValues(appB->received(), fromB, QStringLiteral("slice:0"),
                             QStringLiteral("frequency")).last().toDouble(),
                 7151000.0);
        QCOMPARE(deltaValues(appB->received(), fromB, QStringLiteral("slice:0"),
                             QStringLiteral("afGain")).last().toInt(),
                 31);
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("schema")), 0);
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("snapshot.complete")), 0);
    }

    void resultsGoOnlyToTheDeviceThatAsked()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        const int fromB = appB->received().size();

        // command.result
        const QJsonObject listed = core.invoke(appA, "dspAssets.list");
        QCOMPARE(listed.value(QStringLiteral("accepted")).toBool(false), true);
        // property.result
        appA->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:0", {f64("frequency", 14123000.0)}, 91)));
        QVERIFY(QTest::qWaitFor(
            [appA]() { return !firstOfType(appA->received(), QStringLiteral("property.result")).isEmpty(); },
            5000));
        // settings.value: to both, with A's origin.
        appA->sendText(SessionMessages::encode(SessionMessages::settingsWrite(
            QStringLiteral("DisplaySpectrumFps"), QStringLiteral("30"), QStringLiteral("origin-a"))));
        const auto sawValue = [](LoopbackTransport* app) {
            for (const QJsonObject& o : ofType(app->received(), QStringLiteral("settings.value"))) {
                if (o.value(QStringLiteral("key")).toString() == QStringLiteral("DisplaySpectrumFps")) {
                    return o;
                }
            }
            return QJsonObject{};
        };
        QVERIFY(QTest::qWaitFor([&]() { return !sawValue(appA).isEmpty() && !sawValue(appB).isEmpty(); },
                                5000));
        QCOMPARE(sawValue(appA).value(QStringLiteral("origin")).toString(), QStringLiteral("origin-a"));
        QCOMPARE(sawValue(appB).value(QStringLiteral("origin")).toString(), QStringLiteral("origin-a"));
        // settings.reject: an operator-local key, refused to A only.
        appA->sendText(SessionMessages::encode(SessionMessages::settingsWrite(
            QStringLiteral("RxOnly"), QStringLiteral("True"), QStringLiteral("origin-a"))));
        QVERIFY(QTest::qWaitFor(
            [appA]() { return !firstOfType(appA->received(), QStringLiteral("settings.reject")).isEmpty(); },
            5000));
        // B also receives A's retune as a delta (A's write, B's change), on
        // the marker it holds for A's slice (Task 73).
        QVERIFY(QTest::qWaitFor(
            [&]() {
                return !deltaValues(appB->received(), fromB, QStringLiteral("marker:0"),
                                    QStringLiteral("frequency")).isEmpty();
            },
            5000));
        QTest::qWait(2 * StationServer::kDefaultDeltaFlushMs);
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("command.result")), 0);
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("property.result")), 0);
        QCOMPARE(countOfType(appB->received(), fromB, QStringLiteral("settings.reject")), 0);
    }

    // ── Slice ownership (Task 73) ────────────────────────────────────────
    //
    // Refusals first: who may change which slice.

    void aWriteOrVerbOnAnotherDevicesSliceIsRefusedAndNothingChanges()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        SliceModel* hers = core.model->sliceById(0);
        const double frequency = hers->frequency();
        const int afGain = hers->afGain();
        const QString reason = ownedElsewhere(QStringLiteral("iPhone"));
        QVERIFY(OperatorWording::isPlain(reason));

        // A property write to A's slice from B: every property refused, no
        // value of A's slice sent back, nothing changed.
        appB->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "slice:0", {f64("frequency", 7074000.0), int64("afGain", 5)}, 501)));
        QTRY_VERIFY(!propertyResult(appB, 501).isEmpty());
        const QJsonArray results = propertyResult(appB, 501).value(QStringLiteral("results")).toArray();
        QCOMPARE(results.size(), 2);
        for (const QJsonValue& r : results) {
            QCOMPARE(r.toObject().value(QStringLiteral("accepted")).toBool(true), false);
            QCOMPARE(r.toObject().value(QStringLiteral("reason")).toString(), reason);
            QCOMPARE(r.toObject().value(QStringLiteral("hasValue")).toBool(true), false);
        }
        QCOMPARE(hers->frequency(), frequency);
        QCOMPARE(hers->afGain(), afGain);

        // A write to any marker is refused the same way.
        appB->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "marker:0", {f64("frequency", 7074000.0)}, 502)));
        QTRY_VERIFY(!propertyResult(appB, 502).isEmpty());
        const QJsonObject marker =
            propertyResult(appB, 502).value(QStringLiteral("results")).toArray().first().toObject();
        QCOMPARE(marker.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(marker.value(QStringLiteral("reason")).toString(), reason);
        QCOMPARE(hers->frequency(), frequency);

        // The verbs that name a slice.
        const QList<QPair<QByteArray, QList<MirrorUpdate>>> verbs{
            {"removeSlice", {int64("sliceId", 0)}},
            {"setActiveSliceById", {int64("sliceId", 0)}},
            {"nnr.resetTuning", {int64("sliceId", 0)}},
            {"nnr.tryAgain", {int64("sliceId", 0)}},
            {"nnr.setDiagnostics", {int64("sliceId", 0), int64("testMode", 1), int64("outputMode", 1)}},
            {"notch.add", {int64("sliceId", 0), f64("centreHz", frequency + 500.0), f64("widthHz", 100.0)}},
        };
        const int notches = static_cast<int>(core.model->notchModel()->notches().size());
        for (const auto& verb : verbs) {
            const QJsonObject refused = core.invoke(appB, verb.first, verb.second);
            QVERIFY2(!refused.value(QStringLiteral("accepted")).toBool(true), verb.first.constData());
            QCOMPARE(refused.value(QStringLiteral("reason")).toString(), reason);
        }
        QVERIFY(core.model->sliceById(0) == hers);
        QVERIFY(hers->isActive());
        QCOMPARE(static_cast<int>(core.model->notchModel()->notches().size()), notches);
        QCOMPARE(core.model->activeSlice(), hers);

        // Its own slice it may address.
        const int own = heldKeys(appB, QStringLiteral("slice:")).first().mid(6).toInt();
        QCOMPARE(core.invoke(appB, "setActiveSliceById", {int64("sliceId", own)})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        // And never saw A's.
        QVERIFY(!everSaw(appB, QStringLiteral("slice:0")));
    }

    // Each device receives its own slices and a marker for every other.

    void eachDeviceReceivesItsOwnSlicesAndTheOthersMarkers()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"), QStringLiteral("Tab"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        // A, admitted alone, adopts the slice nobody owned.
        QCOMPARE(heldKeys(appA, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:0")});
        QVERIFY(heldKeys(appA, QStringLiteral("marker:")).isEmpty());

        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        // B gets a first slice of its own, and a marker for A's.
        QCOMPARE(core.model->slices().size(), 2);
        QCOMPARE(heldKeys(appB, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:1")});
        QCOMPARE(heldKeys(appB, QStringLiteral("marker:")), QStringList{QStringLiteral("marker:0")});
        QVERIFY(!everSaw(appB, QStringLiteral("slice:0")));
        const SliceModel* hers = core.model->sliceById(0);
        const auto markerB = [&](const char* property) {
            return latest(appB->received(), QStringLiteral("marker:0"), QString::fromLatin1(property));
        };
        QCOMPARE(markerB("sliceId").toInt(), 0);
        QCOMPARE(markerB("ownerDeviceId").toString(), a.id());
        QCOMPARE(markerB("ownerName").toString(), QStringLiteral("iPhone"));
        QCOMPARE(markerB("ownerShortName").toString(), QStringLiteral("Phone"));
        QCOMPARE(markerB("ownerKind").toString(), QStringLiteral("phone"));
        QCOMPARE(markerB("ownerAway").toBool(true), false);
        QCOMPARE(markerB("frequency").toDouble(), hers->frequency());
        QCOMPARE(markerB("dspMode").toInt(), static_cast<int>(hers->dspMode()));
        QCOMPARE(markerB("filterLow").toInt(), hers->filterLow());
        QCOMPARE(markerB("filterHigh").toInt(), hers->filterHigh());
        QCOMPARE(markerB("band").toInt(), static_cast<int>(hers->band()));
        QCOMPARE(markerB("streamIndex").toInt(), hers->streamIndex());
        QCOMPARE(markerB("psPaused").toBool(true), hers->psPaused());
        QCOMPARE(markerB("txSlice").toBool(), hers->isTxSlice());

        // A: B's marker, its schema first, and never B's slice.
        QTRY_VERIFY(holds(appA, QStringLiteral("marker:1")));
        QVERIFY(!everSaw(appA, QStringLiteral("slice:1")));
        QCOMPARE(latest(appA->received(), QStringLiteral("marker:1"), QStringLiteral("ownerShortName"))
                     .toString(),
                 QStringLiteral("Tab"));
        QCOMPARE(latest(appA->received(), QStringLiteral("marker:1"), QStringLiteral("ownerKind"))
                     .toString(),
                 QStringLiteral("tablet"));
        int schemaAt = -1;
        int createAt = -1;
        for (int i = 0; i < appA->received().size(); ++i) {
            const QJsonObject o = QJsonDocument::fromJson(appA->received().at(i)).object();
            if (schemaAt < 0 && o.value(QStringLiteral("type")).toString() == QStringLiteral("schema")
                && o.value(QStringLiteral("class")).toString() == QStringLiteral("SliceMarker")) {
                schemaAt = i;
            }
            if (createAt < 0 && o.value(QStringLiteral("type")).toString() == QStringLiteral("object.create")
                && o.value(QStringLiteral("key")).toString() == QStringLiteral("marker:1")) {
                createAt = i;
            }
        }
        QVERIFY(schemaAt >= 0);
        QVERIFY(schemaAt < createAt);

        // A's retune reaches B as its marker's frequency, and only so.
        const int fromB = appB->received().size();
        core.model->sliceById(0)->setFrequency(14100000.0);
        QVERIFY(QTest::qWaitFor(
            [&]() {
                const QList<QJsonValue> v = deltaValues(appB->received(), fromB,
                                                        QStringLiteral("marker:0"),
                                                        QStringLiteral("frequency"));
                return !v.isEmpty() && v.last().toDouble() == 14100000.0;
            },
            5000));
        QVERIFY(!everSaw(appB, QStringLiteral("slice:0")));
    }

    void anOlderWindowReceivesOnlyItsOwnSlicesAndNoMarkers()
    {
        Core core;
        Device a;
        Device older(QStringLiteral("Mac"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(older);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        // An older window: signs in by key, predates several devices.
        LoopbackTransport* window = core.signIn(older, {{"deviceAuth", 1}});
        QVERIFY(admitted(window));
        QCOMPARE(heldKeys(window, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:1")});
        core.model->sliceById(0)->setFrequency(14150000.0);
        QTest::qWait(3 * StationServer::kDefaultDeltaFlushMs);
        for (const QByteArray& wire : window->received()) {
            const QJsonObject o = QJsonDocument::fromJson(wire).object();
            QVERIFY(!o.value(QStringLiteral("key")).toString().startsWith(QStringLiteral("marker:")));
            QVERIFY(o.value(QStringLiteral("class")).toString() != QStringLiteral("SliceMarker"));
        }
        QVERIFY(!everSaw(window, QStringLiteral("slice:0")));
        // The device with the feature sees the window's slice as a marker.
        QTRY_VERIFY(holds(appA, QStringLiteral("marker:1")));
    }

    // The active slice (rulings 5.10, 5.11).

    void eachDeviceHasItsOwnActiveSliceAndTheStationFollowsTheLatest()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        // A adds a second slice: A's, a marker to B.
        const QJsonObject added = core.invoke(appA, "addSlice", {utf8("initialPanId", QString())});
        QVERIFY2(added.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(added.value(QStringLiteral("reason")).toString()));
        QCOMPARE(core.model->sliceOwnership()->mark(2).owner, a.key.fingerprint());
        QTRY_VERIFY(holds(appA, QStringLiteral("slice:2")));
        QTRY_VERIFY(holds(appB, QStringLiteral("marker:2")));
        QVERIFY(!everSaw(appB, QStringLiteral("slice:2")));

        const auto activeOwn = [](const LoopbackTransport* app) {
            int count = 0;
            for (const QString& key : heldKeys(app, QStringLiteral("slice:"))) {
                if (latest(app->received(), key, QStringLiteral("active")).toBool()) {
                    ++count;
                }
            }
            return count;
        };

        // A chooses slice 2: B's active slice is unchanged.
        QCOMPARE(core.invoke(appA, "setActiveSliceById", {int64("sliceId", 2)})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QVERIFY(core.model->sliceById(2)->isActive());
        QVERIFY(!core.model->sliceById(0)->isActive());
        QVERIFY(core.model->sliceById(1)->isActive());
        QCOMPARE(core.model->activeSlice()->sliceIndex(), 2);
        QTRY_VERIFY(latest(appA->received(), QStringLiteral("slice:2"), QStringLiteral("active")).toBool());
        QTRY_COMPARE(activeOwn(appA), 1);
        QCOMPARE(activeOwn(appB), 1);

        // B chooses its own: the station-level slice follows, and the FreeDV
        // Reporter frequency with it.
        core.model->sliceById(1)->setFrequency(7074000.0);
        QCOMPARE(core.invoke(appB, "setActiveSliceById", {int64("sliceId", 1)})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QCOMPARE(core.model->activeSlice()->sliceIndex(), 1);
        QVERIFY(core.model->sliceById(2)->isActive());
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(7074000));
        // A tuning its own slice does not move it; B tuning the station's does.
        core.model->sliceById(2)->setFrequency(21074000.0);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(7074000));
        core.model->sliceById(1)->setFrequency(7076000.0);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(7076000));
        QCOMPARE(activeOwn(appA), 1);
        QCOMPARE(activeOwn(appB), 1);
    }

    // Where a device's slices come from, and where they go (ruling 5.2,
    // rulings 4.11 and 4.12).

    void aDeviceWithNoSliceGetsOneOnTheStationReceiverOrNoneWithTheCapFull()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        QVERIFY(core.model->sliceById(0)->streamIndex() >= 0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("Mac"), QStringLiteral("computer"));
        for (Device* x : {&a, &b, &c}) {
            core.pair(*x);
        }
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        const int receivers = receiversInUse(*core.model);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        QCOMPARE(core.model->slices().size(), 2);
        const SliceModel* bs = core.model->sliceById(1);
        QVERIFY(bs != nullptr);
        QCOMPARE(core.model->sliceOwnership()->mark(1).owner, b.key.fingerprint());
        QCOMPARE(bs->frequency(), 14200000.0);
        QCOMPARE(bs->streamIndex(), core.model->sliceById(0)->streamIndex());
        QCOMPARE(receiversInUse(*core.model), receivers);

        // A fills the slice cap.
        for (int i = 0; i < 8; ++i) {
            if (!core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})
                     .value(QStringLiteral("accepted")).toBool()) {
                break;
            }
        }
        const int full = core.model->slices().size();
        QVERIFY(full > 2);
        LoopbackTransport* appC = core.signIn(c);
        QVERIFY(admitted(appC));
        QCOMPARE(core.model->slices().size(), full);
        QVERIFY(heldKeys(appC, QStringLiteral("slice:")).isEmpty());
        QCOMPARE(heldKeys(appC, QStringLiteral("marker:")).size(), full);
    }

    void theLastDeviceLeavingPassesItsSlicesToTheStationHeldForIt()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        QVERIFY(core.invoke(appA, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(!appA->isOpen());
        const SliceOwnership* own = core.model->sliceOwnership();
        // Still running, the station device's, held for A.
        QVERIFY(core.model->sliceById(0) != nullptr);
        QCOMPARE(own->mark(0).owner, SliceOwnership::stationDevice());
        QCOMPARE(own->mark(0).heldFor, a.key.fingerprint());
        // Ruling 5.14: VAX on the Core's computer carries the station
        // device's slices, and no device's.
        const auto vaxCarries = [&core](int sliceId) {
            return ((core.model->audioEngine()->vaxSliceMask() >> sliceId) & 1u) != 0;
        };
        QVERIFY(vaxCarries(0));

        // B, admitted meanwhile, does not adopt it.
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        QCOMPARE(own->mark(0).heldFor, a.key.fingerprint());
        QCOMPARE(heldKeys(appB, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:1")});
        QVERIFY(holds(appB, QStringLiteral("marker:0")));
        QCOMPARE(latest(appB->received(), QStringLiteral("marker:0"), QStringLiteral("ownerDeviceId"))
                     .toString(),
                 a.id());
        QCOMPARE(latest(appB->received(), QStringLiteral("marker:0"), QStringLiteral("ownerAway"))
                     .toBool(false),
                 true);

        QVERIFY(!vaxCarries(1));
        // A signs in again: its slice is its own again.
        LoopbackTransport* back = core.signIn(a);
        QVERIFY(admitted(back));
        QVERIFY(!vaxCarries(0));
        QCOMPARE(own->mark(0).owner, a.key.fingerprint());
        QVERIFY(!own->mark(0).isHeld());
        QCOMPARE(heldKeys(back, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:0")});
        QVERIFY(holds(back, QStringLiteral("marker:1")));
        QTRY_COMPARE(latest(appB->received(), QStringLiteral("marker:0"), QStringLiteral("ownerAway"))
                         .toBool(true),
                     false);
        QCOMPARE(core.model->slices().size(), 2);
    }

    void anAwayDevicesSlicesAreKeptThenClosedAndSavedAtTheEndOf180Seconds()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        core.model->sliceById(1)->setFrequency(7074000.0);
        core.model->sliceById(1)->setAfGain(17);
        core.now = 1000;
        appB->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(core.sessions().entry(b.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        // Within the 180 s: kept, still B's, its marker away.
        QVERIFY(core.model->sliceById(1) != nullptr);
        QCOMPARE(core.model->sliceOwnership()->mark(1).owner, b.key.fingerprint());
        QTRY_COMPARE(latest(appA->received(), QStringLiteral("marker:1"), QStringLiteral("ownerAway"))
                         .toBool(false),
                     true);

        // At their end, with A still here: closed and saved.
        core.now = 1000 + DeviceSessionRegistry::kGraceMs;
        QCOMPARE(core.sessions().expireAway().size(), 1);
        QVERIFY(core.model->sliceById(1) == nullptr);
        QTRY_VERIFY(!holds(appA, QStringLiteral("marker:1")));
        const QString mac = core.model->currentRadioMac();
        const QList<SavedSlice> saved =
            DeviceLayoutStore::load(AppSettings::instance(), mac, b.key.fingerprint());
        QCOMPARE(saved.size(), 1);
        QCOMPARE(saved.first().id, 1);
        QCOMPARE(saved.first().frequencyHz, 7074000.0);
        QCOMPARE(saved.first().settings.value(QStringLiteral("Slice/AfGain")), QStringLiteral("17"));
        // Its letter's keys were cleared: a new slice there starts fresh.
        QVERIFY(DeviceLayoutStore::captureSliceSettings(AppSettings::instance(), mac, 1).isEmpty());

        // Back: restored under its old letter, with its settings.
        LoopbackTransport* back = core.signIn(b);
        QVERIFY(admitted(back));
        QCOMPARE(heldKeys(back, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:1")});
        QVERIFY(core.model->sliceById(1) != nullptr);
        QCOMPARE(core.model->sliceById(1)->frequency(), 7074000.0);
        QCOMPARE(core.model->sliceById(1)->afGain(), 17);
        QCOMPARE(core.model->sliceOwnership()->mark(1).owner, b.key.fingerprint());
        QVERIFY(DeviceLayoutStore::load(AppSettings::instance(), mac, b.key.fingerprint()).isEmpty());
    }

    void aRestoredSliceTakesTheLowestFreeLetterWhenItsOwnIsTaken()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        core.model->sliceById(1)->setAfGain(23);
        core.model->sliceById(1)->setFrequency(3573000.0);
        QVERIFY(core.invoke(appB, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(core.model->sliceById(1) == nullptr);

        // A takes letter B meanwhile, and does not get B's settings.
        QVERIFY(core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})
                    .value(QStringLiteral("accepted")).toBool());
        QVERIFY(core.model->sliceById(1) != nullptr);
        QCOMPARE(core.model->sliceOwnership()->mark(1).owner, a.key.fingerprint());
        QVERIFY(core.model->sliceById(1)->afGain() != 23);

        // B back: its slice on the lowest free letter, with its settings.
        LoopbackTransport* back = core.signIn(b);
        QVERIFY(admitted(back));
        QCOMPARE(heldKeys(back, QStringLiteral("slice:")), QStringList{QStringLiteral("slice:2")});
        QCOMPARE(core.model->sliceById(2)->afGain(), 23);
        QCOMPARE(core.model->sliceById(2)->frequency(), 3573000.0);
        QCOMPARE(core.model->sliceOwnership()->mark(2).owner, b.key.fingerprint());
    }

    void theLastDevicesSlicesAreHeldForItWhenIts180SecondsEnd()
    {
        Core core;
        Device a;
        core.pair(a);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        appA->closeLink(QStringLiteral("lost"));
        QTRY_COMPARE(core.sessions().entry(a.key.fingerprint())->state,
                     DeviceSessionRegistry::State::Away);
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        core.now = DeviceSessionRegistry::kGraceMs;
        QCOMPARE(core.sessions().expireAway().size(), 1);
        QVERIFY(core.model->sliceById(0) != nullptr);
        QCOMPARE(core.model->sliceOwnership()->mark(0).heldFor, a.key.fingerprint());
    }

    void revokingADeviceClosesItsSlicesHeldOnesIncludedAndForgetsItsLayout()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        QVERIFY(core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})
                    .value(QStringLiteral("accepted")).toBool());
        QVERIFY(core.invoke(appA, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(!appA->isOpen());
        QCOMPARE(core.model->sliceOwnership()->heldFor(a.key.fingerprint()), (QList<int>{0, 1}));
        const QString mac = core.model->currentRadioMac();
        SavedSlice earlier;
        earlier.id = 3;
        earlier.frequencyHz = 10136000.0;
        QVERIFY(DeviceLayoutStore::append(AppSettings::instance(), mac, a.key.fingerprint(), earlier, 5));

        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        QTRY_VERIFY(holds(appB, QStringLiteral("marker:0")));
        const QJsonObject revoked = core.invoke(appB, "devices.revoke", {utf8("id", a.id())});
        QVERIFY2(revoked.value(QStringLiteral("accepted")).toBool(),
                 qPrintable(revoked.value(QStringLiteral("reason")).toString()));
        QVERIFY(core.model->sliceById(0) == nullptr);
        QVERIFY(core.model->sliceById(1) == nullptr);
        QVERIFY(core.model->sliceById(2) != nullptr);
        QTRY_VERIFY(!holds(appB, QStringLiteral("marker:0")));
        QTRY_VERIFY(!holds(appB, QStringLiteral("marker:1")));
        QVERIFY(DeviceLayoutStore::load(AppSettings::instance(), mac, a.key.fingerprint()).isEmpty());
    }

    // Ruling 5.2 step 3 with ruling 5.3: a manifest from before owners
    // restores its slices with no owner; the first device admitted alone
    // adopts them all, a later one none.
    void aManifestFromBeforeOwnersIsAdoptedByTheFirstDeviceAlone()
    {
        Core core;
        const QString mac = AppSettings::normalizedRadioMac(core.model->currentRadioMac());
        AppSettings::instance().setHardwareValue(
            mac, QStringLiteral("receiveLayout"),
            QStringLiteral(R"({"version":1,"slices":[{"id":0,"panKey":"pan-0","frequencyHz":7074000,"dspMode":1},{"id":1,"panKey":"pan-0","frequencyHz":7075000,"dspMode":1}]})"));
        const auto forget = qScopeGuard([&mac] {
            AppSettings::instance().remove(QStringLiteral("hardware/%1/receiveLayout").arg(mac));
        });
        core.model->prepareReceiveLayout(mac);
        QCOMPARE(core.model->slices().size(), 2);
        const SliceOwnership* ownership = core.model->sliceOwnership();
        QCOMPARE(ownership->unowned(), (QList<int>{0, 1}));
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        QCOMPARE(ownership->ownedBy(a.key.fingerprint()), (QList<int>{0, 1}));
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        QCOMPARE(ownership->ownedBy(a.key.fingerprint()), (QList<int>{0, 1}));
        QCOMPARE(ownership->ownedBy(b.key.fingerprint()), (QList<int>{2}));
    }

    // Ruling 5.2 step 2: a saved slice that does not fit is recorded for
    // the notice and kept, not dropped.
    void aSavedSliceThatDoesNotFitIsRecordedNotDropped()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        core.model->sliceById(1)->setFrequency(10136000.0);
        QVERIFY(core.invoke(appB, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(core.model->sliceById(1) == nullptr);
        // A fills every slice while B is gone.
        for (int i = 0; i < 8; ++i) {
            if (!core.invoke(appA, "addSlice", {utf8("initialPanId", QString())})
                     .value(QStringLiteral("accepted")).toBool()) {
                break;
            }
        }
        const int full = core.model->slices().size();
        LoopbackTransport* back = core.signIn(b);
        QVERIFY(admitted(back));
        QCOMPARE(core.model->slices().size(), full);
        QVERIFY(heldKeys(back, QStringLiteral("slice:")).isEmpty());
        const QList<SavedSlice> missing = core.server->slicesNotRestored(b.key.fingerprint());
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing.first().frequencyHz, 10136000.0);
        const QList<SavedSlice> kept = DeviceLayoutStore::load(
            AppSettings::instance(), core.model->currentRadioMac(), b.key.fingerprint());
        QCOMPARE(kept.size(), 1);
        QCOMPARE(kept.first().frequencyHz, 10136000.0);
    }

    // A slice the Core makes for nobody (a radio that arrives after the
    // device, the Core's own top-up) goes to a device alone on the Core; with
    // several on it, it stays nobody's, a marker to each.
    void aSliceTheCoreMakesGoesToADeviceAloneAndToNobodyWithSeveral()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        const int alone = core.model->addSlice(QString());
        QVERIFY(alone > 0);
        QTRY_COMPARE(core.model->sliceOwnership()->mark(alone).owner, a.key.fingerprint());
        QTRY_VERIFY(holds(appA, QStringLiteral("slice:%1").arg(alone)));

        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        const int shared = core.model->addSlice(QString());
        QVERIFY(shared > 0);
        QTest::qWait(3 * StationServer::kDefaultDeltaFlushMs);
        QVERIFY(core.model->sliceOwnership()->mark(shared).owner.isEmpty());
        QTRY_VERIFY(holds(appA, QStringLiteral("marker:%1").arg(shared)));
        QTRY_VERIFY(holds(appB, QStringLiteral("marker:%1").arg(shared)));
        QVERIFY(!everSaw(appA, QStringLiteral("slice:%1").arg(shared)));
    }

    void connectedDevicesListsWhatEachDeviceListensOn()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        const SliceModel* bs = core.model->sliceById(1);
        const QJsonObject expected{{QStringLiteral("sliceId"), 1},
                                   {QStringLiteral("letter"), QStringLiteral("B")},
                                   {QStringLiteral("band"), static_cast<int>(bs->band())},
                                   {QStringLiteral("mode"), static_cast<int>(bs->dspMode())}};
        QVERIFY(waitForList(appA, [&](const QJsonArray& list) {
            return entryFor(list, b.id()).value(QStringLiteral("listeningOn")).toArray()
                == QJsonArray{expected};
        }));
        const QJsonArray mine =
            entryFor(connectedList(appA), a.id()).value(QStringLiteral("listeningOn")).toArray();
        QCOMPARE(mine.size(), 1);
        QCOMPARE(mine.first().toObject().value(QStringLiteral("letter")).toString(), QStringLiteral("A"));
        // An away device's slices are still listed.
        appB->closeLink(QStringLiteral("lost"));
        QVERIFY(waitForList(appA, [&](const QJsonArray& list) {
            const QJsonObject entry = entryFor(list, b.id());
            return entry.value(QStringLiteral("state")).toString() == QStringLiteral("away")
                && entry.value(QStringLiteral("listeningOn")).toArray() == QJsonArray{expected};
        }));
    }

    // iPhone app Task 74 (ruling 6.2): the anchor passes to the device whose
    // slice has been on the receiver longest when the anchor's last slice
    // leaves it, and with it the C-Tune pin (ruling 6.3); nobody is asked
    // or told.
    void theAnchorPassesWithThePinWhenItsLastSliceLeaves()
    {
        Core core;
        core.model->configureStreamPool(2, 5, 192000);
        core.model->sliceById(0)->setFrequency(7074000.0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));
        const int receiver = core.model->sliceById(0)->streamIndex();
        QCOMPARE(core.model->sliceById(1)->streamIndex(), receiver);
        QCOMPARE(core.model->sliceOwnership()->anchorOf(receiver), a.key.fingerprint());
        const MirrorUpdate pin{0, "pinned", MirrorWireKind::Bool, true};
        QCOMPARE(core.invoke(appB, "requestStreamCtunPinned", {int64("sliceId", 1), pin})
                     .value(QStringLiteral("accepted")).toBool(true),
                 false);
        // A's slice leaves for 20 m, a receiver of its own.
        core.model->sliceById(0)->setFrequency(14074000.0);
        QVERIFY(core.model->sliceById(0)->streamIndex() != receiver);
        QCOMPARE(core.model->sliceOwnership()->anchorOf(receiver), b.key.fingerprint());
        QCOMPARE(core.invoke(appB, "requestStreamCtunPinned", {int64("sliceId", 1), pin})
                     .value(QStringLiteral("accepted")).toBool(false),
                 true);
        QVERIFY(!appA->receivedKinds().contains(QByteArrayLiteral("confirm.request")));
        QVERIFY(!appB->receivedKinds().contains(QByteArrayLiteral("notice")));
    }

    void aDspAssetJobEndsWithItsOwnDeviceOnly()
    {
        Core core;
        DspAssetStore* store = core.model->dspAssets()->store();
        QVERIFY(store != nullptr);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appA));
        QVERIFY(admitted(appB));

        const QByteArray payload(64, 'n');
        const QString hash =
            QString::fromLatin1(QCryptographicHash::hash(payload, QCryptographicHash::Sha256).toHex());
        const auto begin = [&](LoopbackTransport* app, const QString& label) {
            const QJsonObject r = core.invoke(app, "dspAssets.beginImport",
                                              {int64("kind", 0), utf8("label", label),
                                               int64("size", payload.size()), utf8("hash", hash),
                                               utf8("radioIdentity", QString())});
            [&]() { QCOMPARE(r.value(QStringLiteral("accepted")).toBool(false), true); }();
            for (const QJsonValue& v : r.value(QStringLiteral("values")).toArray()) {
                if (v.toObject().value(QStringLiteral("name")).toString()
                    == QStringLiteral("transferId")) {
                    return v.toObject().value(QStringLiteral("value")).toString();
                }
            }
            return QString();
        };
        const QString jobA = begin(appA, QStringLiteral("from A"));
        const QString jobB = begin(appB, QStringLiteral("from B"));
        QVERIFY(!jobA.isEmpty());
        QVERIFY(!jobB.isEmpty());
        QVERIFY(jobA != jobB);

        // B leaves: its own job is cancelled, and A's goes on.
        appB->closeLink(QStringLiteral("test"));
        QVERIFY(QTest::qWaitFor([&core]() { return core.server->authenticatedSessionCount() == 1; },
                                5000));
        QVERIFY(!store->appendImport(jobB, QByteArray("x")));
        const QJsonObject chunk = core.invoke(
            appA, "dspAssets.chunk",
            {utf8("transferId", jobA), int64("offset", 0),
             utf8("data", QString::fromLatin1(payload.left(16).toBase64()))});
        QVERIFY2(chunk.value(QStringLiteral("accepted")).toBool(false),
                 qPrintable(chunk.value(QStringLiteral("reason")).toString()));

        // A leaves: now its job is cancelled too.
        appA->closeLink(QStringLiteral("test"));
        QVERIFY(QTest::qWaitFor([&core]() { return core.server->authenticatedSessionCount() == 0; },
                                5000));
        QVERIFY(!store->appendImport(jobA, QByteArray("x")));
    }

    // ── Task 76: media and capacity per device ─────────────────────────

    // Each device's capabilities carry its own share of the Core's display
    // budget, its own generation and its own reason: sharedConnection
    // while another device is admitted, sharedProcessing under the
    // governor's cut, and alone again the total's own reason. A device that
    // did not declare sessionHolder hears coreBusy or none instead.
    void eachDeviceGetsItsOwnShareOfTheDisplayBudget()
    {
        MediaCore m(DisplayBudgetLimits{1'000'000, 100'000, 5});
        m.appA = m.core.signIn(m.a);
        QVERIFY(admitted(m.appA));
        const QList<QByteArray> aloneA = m.appA->received();
        QCOMPARE(latestCapability(aloneA, QStringLiteral("remoteMediaVersion")).toInteger(), 1);
        QCOMPARE(latestCapability(aloneA, QStringLiteral("displayApplicationBytesPerSecond"))
                     .toInteger(), 1'000'000);
        QCOMPARE(latestCapability(aloneA, QStringLiteral("displayBudgetGeneration")).toInteger(), 5);
        QCOMPARE(latestCapability(aloneA, QStringLiteral("displayBudgetReason")).toString(),
                 QStringLiteral("none"));

        m.appB = m.core.signIn(m.b);
        QVERIFY(admitted(m.appB));
        QTRY_COMPARE(m.hub->controllerCount(), 2);
        // B's first capabilities: half, shared.
        QCOMPARE(latestCapability(m.appB->received(),
                                  QStringLiteral("displayApplicationBytesPerSecond")).toInteger(),
                 500'000);
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("remoteMediaVersion"))
                     .toInteger(), 1);
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("sharedConnection"));
        // A is told its new share with a new generation.
        QTRY_COMPARE(latestCapability(m.appA->received(),
                                      QStringLiteral("displayApplicationBytesPerSecond"))
                         .toInteger(), 500'000);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("spectrumSampleUnitsPerSecond"))
                     .toInteger(), 50'000);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetGeneration"))
                     .toInteger(), 6);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("sharedConnection"));

        // The governor cuts the total: sharedProcessing.
        QVERIFY(m.core.server->setDisplayBudgetLimits(DisplayBudgetLimits{500'000, 50'000, 7},
                                                      DisplayBudgetReason::CoreBusy));
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedProcessing"));
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("sharedProcessing"));
        QCOMPARE(latestCapability(m.appB->received(),
                                  QStringLiteral("displayApplicationBytesPerSecond")).toInteger(),
                 250'000);

        // B leaves: A alone again, the whole cut total and the total's own
        // reason, with a newer generation.
        const qint64 generationBefore =
            latestCapability(m.appA->received(), QStringLiteral("displayBudgetGeneration"))
                .toInteger();
        QVERIFY(m.core.invoke(m.appB, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE(m.hub->controllerCount(), 1);
        QTRY_COMPARE(latestCapability(m.appA->received(),
                                      QStringLiteral("displayApplicationBytesPerSecond"))
                         .toInteger(), 500'000);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("coreBusy"));
        QVERIFY(latestCapability(m.appA->received(), QStringLiteral("displayBudgetGeneration"))
                    .toInteger() > generationBefore);

        // An older window beside A: its share is shared, but it hears the
        // reason it knows (coreBusy under the cut), while A hears
        // sharedProcessing.
        LoopbackTransport* older = m.core.tokenSignIn();
        QVERIFY(admitted(older));
        QCOMPARE(latestCapability(older->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("coreBusy"));
        QCOMPARE(latestCapability(older->received(),
                                  QStringLiteral("displayApplicationBytesPerSecond")).toInteger(),
                 250'000);
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedProcessing"));
    }

    // A's media control reaches A's controller only; B leaving ends only
    // B's media.
    void eachDevicesMediaControlReachesItsOwnControllerOnly()
    {
        MediaCore m;
        m.signInBoth();
        MediaFake* transportA = m.startMedia(m.appA);
        QVERIFY(transportA);
        QCOMPARE(m.transports.size(), 1);
        // Only A's controller made a media connection; B was sent nothing.
        QVERIFY(!m.appB->receivedKinds().contains(QByteArrayLiteral("media.control")));
        QCOMPARE(m.hub->controllerFor(m.epochOf(0))->sessionEpoch(), m.epochOf(0));

        MediaFake* transportB = m.startMedia(m.appB);
        QVERIFY(transportB);
        QVERIFY(transportA != transportB);
        QVERIFY(transportA->started && transportB->started);

        // B leaves: its media stops, A's goes on.
        m.appB->closeLink(QStringLiteral("test"));
        QTRY_COMPARE(m.hub->controllerCount(), 1);
        QTRY_VERIFY(!transportB || !transportB->started);
        QVERIFY(transportA->started);
        QVERIFY(transportA->readyState);
    }

    // Each device hears only its own slices: distinct tones per slice, read
    // back from each device's lossless audio. The Core's local output
    // plays neither (both slices are devices', none is the station's).
    void eachDeviceHearsOnlyItsOwnSlices()
    {
        MediaCore m;
        m.signInBoth();
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        QVERIFY(sliceA >= 0 && sliceB >= 0 && sliceA != sliceB);
        AudioEngine* engine = m.core.model->audioEngine();
        engine->masterMixForTest().setRampFrames(1);
        engine->masterMixForTest().setSlewUpFrames(0);
        engine->setSliceStreaming(sliceA, true);
        engine->setSliceStreaming(sliceB, true);
        // The Core's own speakers carry neither device's slice.
        QCOMPARE(engine->localOutputSliceMask() & ((1u << sliceA) | (1u << sliceB)), 0u);

        MediaFake* transportA = m.startMedia(m.appA);
        MediaFake* transportB = m.startMedia(m.appB);
        QVERIFY(transportA && transportB);
        for (LoopbackTransport* app : {m.appA, m.appB}) {
            sendMedia(app, {{QStringLiteral("op"), QStringLiteral("audio")},
                            {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
                            {QStringLiteral("revision"), 1},
                            {QStringLiteral("enabled"), true},
                            {QStringLiteral("profile"), QStringLiteral("lossless")}});
        }
        QTRY_VERIFY(!mediaOps(m.appA, QStringLiteral("audio-context")).isEmpty()
                    && !mediaOps(m.appB, QStringLiteral("audio-context")).isEmpty());
        QVERIFY(mediaOps(m.appA, QStringLiteral("audio-context")).last()
                    .value(QStringLiteral("enabled")).toBool());

        // A's slice plays 0.5, B's 0.25 on the left, several capture blocks.
        const QVector<float> toneA(64 * 2, 0.5f);
        const QVector<float> toneB(64 * 2, 0.25f);
        const auto feed = [&](int blocks) {
            for (int frame = 0; frame < blocks * 1920; frame += 64) {
                engine->rxBlockReady(sliceA, toneA.constData(), 64);
                engine->rxBlockReady(sliceB, toneB.constData(), 64);
            }
        };
        feed(3);
        QTRY_VERIFY(transportA->rtpPackets.size() >= 10 && transportB->rtpPackets.size() >= 10);
        const QList<float> heardA = l16Left(transportA->rtpPackets.last());
        const QList<float> heardB = l16Left(transportB->rtpPackets.last());
        QVERIFY(!heardA.isEmpty() && !heardB.isEmpty());
        // Each hears its own tone at its own slice gain, never the other's:
        // the two levels stand in the ratio of the tones fed.
        const float levelA = heardA.last();
        const float levelB = heardB.last();
        QVERIFY(levelA > 0.0f && levelB > 0.0f);
        QVERIFY2(std::abs(levelA / levelB - 2.0f) < 0.01f,
                 qPrintable(QStringLiteral("A %1, B %2").arg(levelA).arg(levelB)));
        for (float sample : heardA) {
            QVERIFY(std::abs(sample - levelA) < 1.0f / 16384.0f);
        }

        // B's slice alone: A's mix falls silent, B's carries on.
        const QVector<float> silence(64 * 2, 0.0f);
        const qsizetype packetsA = transportA->rtpPackets.size();
        const qsizetype packetsB = transportB->rtpPackets.size();
        for (int frame = 0; frame < 3 * 1920; frame += 64) {
            engine->rxBlockReady(sliceA, silence.constData(), 64);
            engine->rxBlockReady(sliceB, toneB.constData(), 64);
        }
        QTRY_VERIFY(transportA->rtpPackets.size() >= packetsA + 10
                    && transportB->rtpPackets.size() >= packetsB + 10);
        QCOMPARE(l16Left(transportA->rtpPackets.last()).last(), 0.0f);
        QVERIFY(std::abs(l16Left(transportB->rtpPackets.last()).last() - levelB) < 1.0f / 16384.0f);

        // A slice A makes joins A's mix at once, and not B's.
        const QJsonObject added =
            m.core.invoke(m.appA, "addSlice", {utf8("initialPanId", QString())});
        QVERIFY2(added.value(QStringLiteral("accepted")).toBool(false),
                 qPrintable(added.value(QStringLiteral("reason")).toString()));
        int sliceA2 = -1;
        for (const SliceModel* slice : m.core.model->slices()) {
            if (slice->sliceIndex() != sliceA && slice->sliceIndex() != sliceB) {
                sliceA2 = slice->sliceIndex();
            }
        }
        QVERIFY(sliceA2 >= 0);
        const int mixA = m.hub->controllerFor(m.epochOf(0))->ownerMixSlot();
        const int mixB = m.hub->controllerFor(m.epochOf(1))->ownerMixSlot();
        QTRY_COMPARE(engine->ownerMixSliceMask(mixA), (1u << sliceA) | (1u << sliceA2));
        QCOMPARE(engine->ownerMixSliceMask(mixB), 1u << sliceB);
    }

    // A device subscribes displays and receiver streams only for its own
    // slices; two devices watching one receiver share one FFT, and it keeps
    // running for A when B leaves.
    void displaysOnlyForOwnSlicesAndOneFftPerReceiver()
    {
        MediaCore m(DisplayBudgetLimits{10'000'000, 1'000'000, 1});
        m.signInBoth();
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        SliceModel* a = m.core.model->sliceById(sliceA);
        SliceModel* b = m.core.model->sliceById(sliceB);
        QVERIFY(a && b);
        QCOMPARE(a->streamIndex(), b->streamIndex());
        const double centre = m.core.model->streamCentreHz(a->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));

        // A asks for B's slice: refused, nothing set up.
        sendMedia(m.appA, displayRequest(1, sliceB, centre));
        QTRY_VERIFY(!mediaOps(m.appA, QStringLiteral("allocation-result")).isEmpty());
        QJsonObject refused = mediaOps(m.appA, QStringLiteral("allocation-result")).last();
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QCOMPARE(refused.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("That slice belongs to another device."));
        QVERIFY(OperatorWording::isPlain(refused.value(QStringLiteral("reason")).toString()));
        QCOMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 0);

        // Each asks for its own: both accepted, on one engine.
        sendMedia(m.appA, displayRequest(2, sliceA, centre));
        sendMedia(m.appB, displayRequest(1, sliceB, centre));
        QTRY_COMPARE(mediaOps(m.appA, QStringLiteral("allocation-result")).size(), 2);
        QTRY_COMPARE(mediaOps(m.appB, QStringLiteral("allocation-result")).size(), 1);
        QVERIFY(mediaOps(m.appA, QStringLiteral("allocation-result")).last()
                    .value(QStringLiteral("accepted")).toBool());
        QVERIFY(mediaOps(m.appB, QStringLiteral("allocation-result")).last()
                    .value(QStringLiteral("accepted")).toBool());
        QCOMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 1);
        QCOMPARE(m.hub->controllerFor(m.epochOf(1))->activeEndpointCount(), 1);
        QCOMPARE(m.hub->sharedSpectrum()->source().activeSources().size(), 1);

        // A's receiver stream for B's slice is refused as a slice not there.
        sendMedia(m.appA, {{QStringLiteral("op"), QStringLiteral("receiver-audio")},
                           {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
                           {QStringLiteral("sliceId"), sliceB},
                           {QStringLiteral("revision"), 1},
                           {QStringLiteral("enabled"), true},
                           {QStringLiteral("profile"), QStringLiteral("opus")}});
        QTRY_VERIFY(!mediaOps(m.appA, QStringLiteral("receiver-audio-context")).isEmpty());
        const QJsonObject stream =
            mediaOps(m.appA, QStringLiteral("receiver-audio-context")).last();
        QCOMPARE(stream.value(QStringLiteral("enabled")).toBool(true), false);
        QCOMPARE(stream.value(QStringLiteral("reason")).toString(),
                 QStringLiteral("slice-removed"));

        // B leaves: A's pan and the engine it rides go on.
        m.appB->closeLink(QStringLiteral("test"));
        QTRY_COMPARE(m.hub->controllerCount(), 1);
        QCOMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 1);
        QCOMPARE(m.hub->sharedSpectrum()->source().activeSources().size(), 1);
    }

    // The governor sees every device's display charge, PureSignal's once.
    void theGovernorSeesEveryDevicesCharge()
    {
        MediaCore m(DisplayBudgetLimits{10'000'000, 1'000'000, 1});
        m.signInBoth();
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centre = m.core.model->streamCentreHz(
            m.core.model->sliceById(sliceA)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));
        sendMedia(m.appA, displayRequest(1, sliceA, centre));
        sendMedia(m.appB, displayRequest(1, sliceB, centre));
        QTRY_COMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 1);
        QTRY_COMPARE(m.hub->controllerFor(m.epochOf(1))->activeEndpointCount(), 1);
        const DisplayBudgetCharge one = m.hub->controllerFor(m.epochOf(0))->ownDisplayCharge();
        QVERIFY(one.applicationBytesPerSecond > 0);
        QCOMPARE(m.hub->acceptedDisplayCharge(), *sumDisplayCharges({one, one}));
    }

    // Telemetry reaches every session that negotiated it, each on its own
    // sequence.
    void telemetryReachesEverySessionThatNegotiatedIt()
    {
        MediaCore m;
        m.signInBoth();
        std::vector<std::unique_ptr<DaemonTelemetryController>> collectors;
        for (quint64 epoch : m.core.server->mediaSessionEpochs()) {
            QVERIFY(m.core.server->telemetryAvailable(epoch));
            auto collector = std::make_unique<DaemonTelemetryController>(
                m.core.server.get(), m.core.model.get(), nullptr);
            collector->disableAutomaticSamplingForTest();
            collector->bindToSession(epoch);
            QCOMPARE(collector->sessionEpoch(), epoch);
            collectors.push_back(std::move(collector));
        }
        for (auto& collector : collectors) {
            collector->sampleNow();
        }
        QTRY_COMPARE(ofType(m.appA->received(), QStringLiteral("station.metrics.v1")).size(), 1);
        QTRY_COMPARE(ofType(m.appB->received(), QStringLiteral("station.metrics.v1")).size(), 1);
        // A new session's own collector never takes another's.
        QCOMPARE(collectors.front()->sessionEpoch(), m.epochOf(0));
        QCOMPARE(collectors.back()->sessionEpoch(), m.epochOf(1));
        collectors.clear();
    }
};

QTEST_MAIN(TstStationMultiSession)
#include "tst_station_multi_session.moc"
