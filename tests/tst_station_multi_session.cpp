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
//   2026-09-25: fix wave after the group review of Tasks 71 to 76: the
//               harness comes from MultiDeviceHarness.h, one copy for every
//               several-devices test. J.J. Boyd (KG4VCF), with AI-assisted
//               implementation via Anthropic Claude Code.
//   2026-09-25: checkpoint join (R-IOS-02, R-IOS-27): slice.selectBand
//               and notch.addAtSlice refused for another device's slice.
//               J.J. Boyd (KG4VCF), with AI-assisted implementation via
//               Anthropic Claude Code.
// =================================================================

#include "MultiDeviceHarness.h"

#include <QtEndian>

#include "core/daemon/DaemonTelemetryController.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/session/media/DisplayBudget.h"
#include "core/session/media/DisplayLoadGovernor.h"
#include "core/session/media/IMediaTransport.h"
#include "core/session/media/SpectrumEndpoint.h"
#include "models/Band.h"

namespace {

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

QJsonObject displayRequest(quint32 endpointId, int sliceId, double centreHz);

// displayRequest at `fps` frames a second (128 pixels, no wide plane).
QJsonObject displayRequestAt(quint32 endpointId, int sliceId, double centreHz, int fps,
                             quint32 revision = 1)
{
    QJsonObject request = displayRequest(endpointId, sliceId, centreHz);
    request.insert(QStringLiteral("fps"), fps);
    request.insert(QStringLiteral("revision"), static_cast<qint64>(revision));
    return request;
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
        // Today's desktop: the token, no features. Told to update or try
        // later, since it cannot answer the fifth-device question.
        QVERIFY(OperatorWording::isPlain(kOlderWindowCoreFull));
        verifyCoreFull(core.tokenSignIn(), kOlderWindowCoreFull);
        // A window that signs in by key but predates the feature: the same.
        Device window(QStringLiteral("Shack Mac"), QStringLiteral("computer"));
        core.pair(window);
        verifyCoreFull(core.signIn(window, {{"deviceAuth", 1}}), kOlderWindowCoreFull);
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

    // Fix wave I1: every client counts its command ids from 1, so two
    // devices use the same id at once. A result that arrives on a later
    // turn (the sample rate is the dispatcher's asynchronous verb) goes to
    // the session that asked, once, never to the other.
    void aLaterResultGoesToItsOwnDeviceWhenTwoUseTheSameCommandId()
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
        const int aSlice = core.model->sliceOwnership()->ownedBy(a.key.fingerprint()).first();
        const int bSlice = core.model->sliceOwnership()->ownedBy(b.key.fingerprint()).first();
        const quint32 sameId = 4242;
        // Both sent before the Core's event loop runs either rate change.
        appA->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "requestSliceSampleRate", sameId, {int64("sliceId", aSlice), int64("rateHz", 96000)})));
        appB->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
            "requestSliceSampleRate", sameId, {int64("sliceId", bSlice), int64("rateHz", 96000)})));
        const auto resultsFor = [sameId](const LoopbackTransport* app) {
            QList<QJsonObject> out;
            for (const QJsonObject& o : ofType(app->received(), QStringLiteral("command.result"))) {
                if (o.value(QStringLiteral("id")).toInteger() == sameId) {
                    out.append(o);
                }
            }
            return out;
        };
        QTRY_VERIFY(!resultsFor(appA).isEmpty() && !resultsFor(appB).isEmpty());
        QTest::qWait(2 * StationServer::kDefaultDeltaFlushMs);
        QCOMPARE(resultsFor(appA).size(), 1);
        QCOMPARE(resultsFor(appB).size(), 1);
        QVERIFY2(resultsFor(appA).first().value(QStringLiteral("accepted")).toBool(false),
                 QJsonDocument(resultsFor(appA).first()).toJson().constData());
        QVERIFY(resultsFor(appB).first().value(QStringLiteral("accepted")).toBool(false));
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
            // Fix wave C1 (ruling 5.9): the three verbs that are routes on
            // a device's own slice are refusals on another's, with no
            // receiver in use yet.
            {"requestSliceSampleRate", {int64("sliceId", 0), int64("rateHz", 96000)}},
            {"requestStreamCentre", {int64("sliceId", 0), f64("centreHz", frequency + 1000.0)}},
            {"requestStreamCtunPinned",
             {int64("sliceId", 0), MirrorUpdate{0, "pinned", MirrorWireKind::Bool, true}}},
            // The checkpoint join: the band buttons and +TNF on a slice.
            {"slice.selectBand",
             {int64("sliceId", 0),
              int64("band", static_cast<int>(hers->band() == Band::Band40m ? Band::Band20m
                                                                          : Band::Band40m))}},
            {"notch.addAtSlice", {int64("sliceId", 0)}},
        };
        const Band band = hers->band();
        const int notches = static_cast<int>(core.model->notchModel()->notches().size());
        for (const auto& verb : verbs) {
            const QJsonObject refused = core.invoke(appB, verb.first, verb.second);
            QVERIFY2(!refused.value(QStringLiteral("accepted")).toBool(true), verb.first.constData());
            QCOMPARE(refused.value(QStringLiteral("reason")).toString(), reason);
        }
        QVERIFY(core.model->sliceById(0) == hers);
        QVERIFY(hers->isActive());
        QCOMPARE(hers->band(), band);
        QCOMPARE(hers->frequency(), frequency);
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

    // Fix wave I3: a slice's own settings keys (Slice<N>/...) are written
    // or removed only by slice N's owner; anyone else is refused with the
    // foreign-slice reason and nothing is stored. (Its NNR keys under
    // hardware/<mac>/slices/<N>/ are model-owned, refused to everyone.)
    void aSlicesSettingsKeysAreWrittenOrRemovedOnlyByItsOwner()
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
        const int own = core.model->sliceOwnership()->ownedBy(b.key.fingerprint()).first();
        const QString sliceKey = QStringLiteral("Slice0/AfGain");
        core.settings->setValue(sliceKey, QStringLiteral("17"));
        const QString reason = ownedElsewhere(QStringLiteral("iPhone"));
        const auto rejectFor = [appB](const QString& key) {
            for (const QJsonObject& o : ofType(appB->received(), QStringLiteral("settings.reject"))) {
                if (o.value(QStringLiteral("key")).toString() == key) {
                    return o;
                }
            }
            return QJsonObject{};
        };

        appB->sendText(SessionMessages::encode(
            SessionMessages::settingsWrite(sliceKey, QStringLiteral("5"), QStringLiteral("b-1"))));
        QTRY_VERIFY(!rejectFor(sliceKey).isEmpty());
        QCOMPARE(rejectFor(sliceKey).value(QStringLiteral("reason")).toString(), reason);
        QCOMPARE(core.settings->value(sliceKey).toString(), QStringLiteral("17"));
        const int rejectsBefore = static_cast<int>(ofType(appB->received(),
                                                          QStringLiteral("settings.reject")).size());
        appB->sendText(SessionMessages::encode(SessionMessages::settingsRemove(sliceKey)));
        QTRY_VERIFY(ofType(appB->received(), QStringLiteral("settings.reject")).size() > rejectsBefore);
        QCOMPARE(ofType(appB->received(), QStringLiteral("settings.reject")).last()
                     .value(QStringLiteral("reason")).toString(),
                 reason);
        QCOMPARE(core.settings->value(sliceKey).toString(), QStringLiteral("17"));
        // A slice that is not live has no owner: its keys, which would
        // seed the next slice under that id, are nobody's to write.
        const QString unusedKey = QStringLiteral("Slice4/AfGain");
        QVERIFY(!core.model->sliceOwnership()->isLive(4));
        appB->sendText(SessionMessages::encode(
            SessionMessages::settingsWrite(unusedKey, QStringLiteral("3"), QStringLiteral("b-3"))));
        QTRY_VERIFY(!rejectFor(unusedKey).isEmpty());
        QVERIFY(!core.settings->contains(unusedKey));

        // Its own slice's keys, and A its own, are written as before.
        const QString ownKey = QStringLiteral("Slice%1/AfGain").arg(own);
        appB->sendText(SessionMessages::encode(
            SessionMessages::settingsWrite(ownKey, QStringLiteral("9"), QStringLiteral("b-2"))));
        QTRY_COMPARE(core.settings->value(ownKey).toString(), QStringLiteral("9"));
        appA->sendText(SessionMessages::encode(
            SessionMessages::settingsWrite(sliceKey, QStringLiteral("21"), QStringLiteral("a-1"))));
        QTRY_COMPARE(core.settings->value(sliceKey).toString(), QStringLiteral("21"));
        QVERIFY(rejectFor(ownKey).isEmpty());
    }

    // Fix wave C1 (ruling 5.9): a sample rate, a C-Tune centre or pin
    // naming another device's slice, a slice nobody owns, or a slice held
    // for a device, is refused with the foreign-slice reason whatever the
    // receivers are, and neither the rate nor the window moves.
    void rateCentreAndPinOnASliceNotTheRequestersAreRefused()
    {
        Core core;
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        Device c(QStringLiteral("MacBook"), QStringLiteral("computer"));
        core.pair(a);
        core.pair(b);
        core.pair(c);
        core.model->configureStreamPool(5, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        // A signs in and leaves: its slice is held for it.
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        QVERIFY(core.invoke(appA, "session.leave").value(QStringLiteral("accepted")).toBool());
        QTRY_VERIFY(!appA->isOpen());
        QCOMPARE(core.model->sliceOwnership()->mark(0).heldFor, a.key.fingerprint());
        LoopbackTransport* appB = core.signIn(b);
        LoopbackTransport* appC = core.signIn(c);
        QVERIFY(admitted(appB));
        QVERIFY(admitted(appC));
        const int bSlice = core.model->sliceOwnership()->ownedBy(b.key.fingerprint()).first();
        // A slice the Core makes with several devices on it is nobody's.
        const int nobodys = core.model->addSlice(QStringLiteral("pan-0"));
        QVERIFY(nobodys >= 0);
        QVERIFY(core.model->sliceOwnership()->mark(nobodys).owner.isEmpty());
        QVERIFY(core.model->streamAllocator().streamCount() > 0);

        const QList<QPair<int, QString>> targets{
            {bSlice, ownedElsewhere(QStringLiteral("iPad"))},
            {0, ownedElsewhere(QStringLiteral("iPhone"))},
            {nobodys, QStringLiteral("That slice belongs to the Core. It can be changed only there.")},
        };
        for (const auto& target : targets) {
            SliceModel* slice = core.model->sliceById(target.first);
            QVERIFY(slice != nullptr);
            const int stream = slice->streamIndex();
            const int rate = slice->sampleRateHz();
            const double centre = stream >= 0 ? core.model->streamAllocator().streamCentreHz(stream)
                                              : 0.0;
            const QList<QPair<QByteArray, QList<MirrorUpdate>>> verbs{
                {"requestSliceSampleRate", {int64("sliceId", target.first), int64("rateHz", 96000)}},
                {"requestStreamCentre",
                 {int64("sliceId", target.first), f64("centreHz", slice->frequency() + 1000.0)}},
                {"requestStreamCtunPinned",
                 {int64("sliceId", target.first),
                  MirrorUpdate{0, "pinned", MirrorWireKind::Bool, true}}},
            };
            for (const auto& verb : verbs) {
                const QJsonObject refused = core.invoke(appC, verb.first, verb.second);
                QVERIFY2(!refused.value(QStringLiteral("accepted")).toBool(true),
                         verb.first.constData());
                QCOMPARE(refused.value(QStringLiteral("reason")).toString(), target.second);
            }
            // Nothing a later turn of the event loop could apply either.
            QTest::qWait(50);
            QCOMPARE(slice->sampleRateHz(), rate);
            if (stream >= 0) {
                QCOMPARE(core.model->streamAllocator().streamCentreHz(stream), centre);
            }
        }
    }

    // Fix wave 2 (re-review Minor 1): C1's refusal with no receivers in
    // use: the rate, the C-Tune centre and the pin on another device's
    // slice are refused with the foreign-slice reason, and nothing moves.
    void rateCentreAndPinOnAnotherDevicesSliceAreRefusedWithNoReceiversInUse()
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
        QVERIFY(core.model->streamAllocator().streamCount() <= 0);
        QCOMPARE(core.model->sliceOwnership()->mark(0).owner, a.key.fingerprint());
        SliceModel* slice = core.model->sliceById(0);
        QVERIFY(slice != nullptr);
        const int rate = slice->sampleRateHz();
        const QList<QPair<QByteArray, QList<MirrorUpdate>>> verbs{
            {"requestSliceSampleRate", {int64("sliceId", 0), int64("rateHz", 96000)}},
            {"requestStreamCentre", {int64("sliceId", 0), f64("centreHz", slice->frequency() + 1000.0)}},
            {"requestStreamCtunPinned",
             {int64("sliceId", 0), MirrorUpdate{0, "pinned", MirrorWireKind::Bool, true}}},
        };
        for (const auto& verb : verbs) {
            const QJsonObject refused = core.invoke(appB, verb.first, verb.second);
            QVERIFY2(!refused.value(QStringLiteral("accepted")).toBool(true), verb.first.constData());
            QCOMPARE(refused.value(QStringLiteral("reason")).toString(),
                     ownedElsewhere(QStringLiteral("iPhone")));
        }
        QTest::qWait(50);
        QCOMPARE(slice->sampleRateHz(), rate);
        QVERIFY(!slice->streamCtunPinned());
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

        // Fix wave (ruling 5.11): a slice in RADE mode is the one the FreeDV
        // Reporter lists, whoever's active slice leads; retuning it moves
        // the listing even though it is not the active one, and the
        // station-level slice leads again once no slice is in RADE.
        core.model->sliceById(2)->setDspMode(DSPMode::RADE_U);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(21074000));
        core.model->sliceById(2)->setFrequency(14236000.0);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(14236000));
        core.model->sliceById(1)->setFrequency(7078000.0);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(14236000));
        core.model->sliceById(2)->setDspMode(DSPMode::USB);
        QCOMPARE(core.model->freedvWantedFrequencyHzForTest(), quint64(7078000));
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

    // Fix wave I4 (ruling 5.12): a pan is a device plus a pan key. B's
    // first slice on a pan key A also uses ("pan-x") opens B's own pan and
    // claims a receiver of its own; B's second slice there is on B's pan,
    // and costs nothing more.
    void aPanIsADeviceAndAPanKey()
    {
        Core core;
        core.model->configureStreamPool(4, 5, 192000);
        core.model->sliceById(0)->setFrequency(14200000.0);
        Device a;
        Device b(QStringLiteral("iPad"), QStringLiteral("tablet"));
        core.pair(a);
        core.pair(b);
        LoopbackTransport* appA = core.signIn(a);
        QVERIFY(admitted(appA));
        QVERIFY(core.invoke(appA, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-x"))})
                    .value(QStringLiteral("accepted")).toBool());
        LoopbackTransport* appB = core.signIn(b);
        QVERIFY(admitted(appB));
        const int before = receiversInUse(*core.model);
        QVERIFY(!core.model->slicesOnPan(QStringLiteral("pan-x")).isEmpty());

        const QJsonObject opened =
            core.invoke(appB, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-x"))});
        QVERIFY2(opened.value(QStringLiteral("accepted")).toBool(false),
                 qPrintable(opened.value(QStringLiteral("reason")).toString()));
        const int bPan = opened.value(QStringLiteral("affected")).toArray().first().toString()
                             .mid(6).toInt();
        QCOMPARE(core.model->sliceOwnership()->mark(bPan).owner, b.key.fingerprint());
        const int bStream = core.model->sliceById(bPan)->streamIndex();
        QVERIFY(bStream >= 0);
        // A receiver of its own: no other slice is on it.
        QCOMPARE(core.model->slicesOnStream(bStream), QVector<int>{bPan});
        QCOMPARE(receiversInUse(*core.model), before + 1);

        // B's second slice on its own pan-x joins B's pan, no new receiver.
        const QJsonObject joined =
            core.invoke(appB, "addSliceOnPan", {utf8("panId", QStringLiteral("pan-x"))});
        QVERIFY(joined.value(QStringLiteral("accepted")).toBool(false));
        QCOMPARE(receiversInUse(*core.model), before + 1);
    }

    // Fix wave I4 (ruling 5.12): the Core mirrors no pans. Its model holds
    // no PanadapterModel (only RadioModel::addPanadapter makes one, and no
    // Core or window path calls it), so no device is sent a `pan:<i>` and a
    // write to one changes nothing.
    void theCoreMirrorsNoPansAndAPanWriteChangesNothing()
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
        QVERIFY(core.model->panadapters().isEmpty());
        appB->sendText(SessionMessages::encode(SessionMessages::propertyWrite(
            "pan:0", {f64("centerFrequency", 7074000.0)}, 611)));
        QTRY_VERIFY(!propertyResult(appB, 611).isEmpty());
        const QJsonObject refused =
            propertyResult(appB, 611).value(QStringLiteral("results")).toArray().first().toObject();
        QCOMPARE(refused.value(QStringLiteral("accepted")).toBool(true), false);
        QVERIFY(core.model->panadapters().isEmpty());
        QTest::qWait(2 * StationServer::kDefaultDeltaFlushMs);
        QVERIFY(heldKeys(appA, QStringLiteral("pan:")).isEmpty());
        QVERIFY(heldKeys(appB, QStringLiteral("pan:")).isEmpty());
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

    // Fix wave (ruling 4.8 keeps a device's pans): a device's C-Tune pin
    // survives its link dropping, its media ending with it, and its coming
    // back as the same device; it ends when the device leaves for good.
    void aDevicesPinSurvivesItsReturnAndEndsWhenItLeaves()
    {
        MediaCore m;
        m.appA = m.core.signIn(m.a);
        QVERIFY(admitted(m.appA));
        QTRY_COMPARE(m.hub->controllerCount(), 1);
        QVERIFY(m.startMedia(m.appA) != nullptr);
        QTRY_COMPARE(m.core.server->mediaSessionEpochs().size(), 1);
        const SliceModel* slice = m.core.model->sliceById(0);
        QVERIFY(slice->streamIndex() >= 0);
        QCOMPARE(m.core.model->sliceOwnership()->anchorOf(slice->streamIndex()),
                 m.a.key.fingerprint());
        const MirrorUpdate pin{0, "pinned", MirrorWireKind::Bool, true};
        QVERIFY(m.core.invoke(m.appA, "requestStreamCtunPinned", {int64("sliceId", 0), pin})
                    .value(QStringLiteral("accepted")).toBool(false));
        QVERIFY(slice->streamCtunPinned());

        // The link drops: A is away, its media has ended, the pin stays.
        m.appA->closeLink(QStringLiteral("the link dropped"));
        QTRY_VERIFY(m.core.server->mediaSessionEpochs().isEmpty());
        QVERIFY(m.core.model->sliceById(0)->streamCtunPinned());

        // A back as the same device: its pin is as it left it.
        LoopbackTransport* back = m.core.signIn(m.a);
        QVERIFY(admitted(back));
        QVERIFY(m.core.model->sliceById(0)->streamCtunPinned());

        // A leaves for good: the pin ends.
        m.core.invoke(back, "session.leave");
        QTRY_VERIFY(!back->isOpen());
        QVERIFY(!m.core.model->sliceById(0)->streamCtunPinned());
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
    // Task 76 (ruling 9.3, design ruling 9.3a) and fix wave 2 (Important
    // 3): each device asks for what it wants (here a pan wider than the
    // Core grants, so each is admitted while its request stays above half
    // the samples). While a device's share is below its request and another
    // device is admitted, its reason is sharedConnection, or
    // sharedProcessing under the governor's cut; alone it hears none or
    // coreBusy.
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
        // Each asks for 512 pixels at 60 frames a second over a window its
        // 4096-bin engine gives 1024 bins, so all 512 are carried (fix wave
        // 3: a request counts only the pixels its window can carry; at the
        // 1024-bin engine's 256 bins it would ask for half): 61'440 samples
        // a second, more
        // than half the total's 100'000. The first is admitted whole; the
        // second, over its half, is refused for the budget and still asked
        // for (its request is held while it may ask again).
        const auto wants = [&m](LoopbackTransport* app, quint64 epoch, bool fits = true) {
            const int slice = m.sliceOf(epoch);
            const double centre =
                m.core.model->streamCentreHz(m.core.model->sliceById(slice)->streamIndex());
            QJsonObject request = displayRequestAt(1, slice, centre, 60);
            request.insert(QStringLiteral("fftSize"), 4096);
            request.insert(QStringLiteral("pixels"), 512);
            sendMedia(app, request);
            QTRY_COMPARE(mediaOps(app, QStringLiteral("allocation-result")).size(), 1);
            const QJsonObject result = mediaOps(app, QStringLiteral("allocation-result")).last();
            QCOMPARE(result.value(QStringLiteral("accepted")).toBool(!fits), fits);
            if (!fits) {
                QCOMPARE(result.value(QStringLiteral("reason")).toString(),
                         QString::fromLatin1(kDisplayBudgetRefusalReason));
            }
        };
        QVERIFY(m.startMedia(m.appA));
        wants(m.appA, m.epochOf(0));
        // Alone it has the whole total and the total's own reason.
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("none"));
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetGeneration"))
                     .toInteger(), 5);

        m.appB = m.core.signIn(m.b);
        QVERIFY(admitted(m.appB));
        QTRY_COMPARE(m.hub->controllerCount(), 2);
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("remoteMediaVersion"))
                     .toInteger(), 1);
        QVERIFY(m.startMedia(m.appB));
        wants(m.appB, m.epochOf(1), false);
        // Both ask for more than half: equal halves, each told the
        // connection is shared.
        for (LoopbackTransport* app : {m.appA, m.appB}) {
            QTRY_COMPARE(latestCapability(app->received(),
                                          QStringLiteral("spectrumSampleUnitsPerSecond"))
                             .toInteger(), 50'000);
            QCOMPARE(latestCapability(app->received(),
                                      QStringLiteral("displayApplicationBytesPerSecond"))
                         .toInteger(), 500'000);
            QCOMPARE(latestCapability(app->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedConnection"));
        }
        // A is told its new share with a new generation.
        QVERIFY(latestCapability(m.appA->received(), QStringLiteral("displayBudgetGeneration"))
                    .toInteger() > 5);

        // The governor cuts the total: sharedProcessing, each half of it.
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

        // An older window beside A: A, short of its request, hears
        // sharedProcessing; the older window, which asks for no display yet
        // and has its one useful pan, hears the reason it knows (coreBusy
        // under the cut).
        LoopbackTransport* older = m.core.tokenSignIn();
        QVERIFY(admitted(older));
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedProcessing"));
        QCOMPARE(latestCapability(older->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("coreBusy"));
        const DisplayBudgetCharge floorPan = DisplayLoadGovernor::floorPanCharge();
        QVERIFY(static_cast<quint64>(latestCapability(older->received(),
                                                      QStringLiteral("spectrumSampleUnitsPerSecond"))
                                         .toInteger())
                >= floorPan.spectrumSampleUnitsPerSecond);
        QTRY_COMPARE(latestCapability(m.appA->received(),
                                      QStringLiteral("spectrumSampleUnitsPerSecond"))
                         .toInteger(),
                     static_cast<qint64>(50'000 - floorPan.spectrumSampleUnitsPerSecond));
    }

    // Fix wave I5 (ruling 9.3): each device's request is its demand, the
    // charges of its displays as subscribed (a display refused for the
    // budget included), not what it was granted, and (fix wave 2, Critical
    // 1) at least one useful pan. A sound-only device and a one-pan device
    // beside a four-pan device each get what they ask for, and the rest
    // goes max-min fair to the four-pan device. The link cannot tell the
    // sound-only device from one that has not subscribed yet, so it keeps
    // the floor of one useful pan.
    // Fix wave 3 (the re-review's third out-of-scope item): a device's
    // request counts the pixels its window can carry (the source bins the
    // grant clamps to), not more. B asks for 1024 pixels over a window
    // whose bins are far fewer; it is fully served, hears none, and A has
    // the rest.
    void aDevicesRequestCountsOnlyThePixelsItsWindowCanCarry()
    {
        const DisplayBudgetCharge p = spectrumDisplayCost(128, 60, false)->charge;
        const DisplayBudgetCharge f = DisplayLoadGovernor::floorPanCharge();
        const DisplayBudgetCharge wide = spectrumDisplayCost(1024, 60, false)->charge;
        const DisplayBudgetLimits total{f.applicationBytesPerSecond + 5 * p.applicationBytesPerSecond,
                                        f.spectrumSampleUnitsPerSecond
                                            + 5 * p.spectrumSampleUnitsPerSecond,
                                        1};
        // Asked for as 1024 pixels, B's pan would want more than half.
        QVERIFY(2 * wide.spectrumSampleUnitsPerSecond > total.spectrumSampleUnitsPerSecond);
        MediaCore m(total);
        m.signInBoth();
        QTRY_COMPARE(m.hub->controllerCount(), 2);
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centre =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceA)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));

        QJsonObject many = displayRequestAt(1, sliceB, centre, 60);
        many.insert(QStringLiteral("pixels"), 1024);
        sendMedia(m.appB, many);
        QTRY_COMPARE(mediaOps(m.appB, QStringLiteral("allocation-result")).size(), 1);
        QVERIFY(mediaOps(m.appB, QStringLiteral("allocation-result")).last()
                    .value(QStringLiteral("accepted")).toBool());
        for (quint32 endpoint = 1; endpoint <= 4; ++endpoint) {
            sendMedia(m.appA, displayRequestAt(endpoint, sliceA, centre, 60));
        }
        QTRY_COMPARE(mediaOps(m.appA, QStringLiteral("allocation-result")).size(), 4);

        const auto share = [](const LoopbackTransport* app, const char* name) {
            return static_cast<quint64>(
                latestCapability(app->received(), QString::fromLatin1(name)).toInteger());
        };
        // B is given what its window can carry, below half the total, and
        // is told nothing is short.
        QTRY_COMPARE(latestCapability(m.appB->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("none"));
        const quint64 bSamples = share(m.appB, "spectrumSampleUnitsPerSecond");
        QVERIFY2(bSamples < wide.spectrumSampleUnitsPerSecond, qPrintable(QString::number(bSamples)));
        QVERIFY(2 * bSamples < total.spectrumSampleUnitsPerSecond);
        // A, short of its four pans, has everything B leaves.
        QTRY_COMPARE(share(m.appA, "spectrumSampleUnitsPerSecond"),
                     total.spectrumSampleUnitsPerSecond - bSamples);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("sharedConnection"));
    }

    // Fix wave 3 (the re-review's Important 2, ruling 9.3), with a holder
    // injected into the split input (Task 34's holder at the merge). A and
    // B settle at half the total. A becomes the present holder: its share
    // stays its request (half) until it asks for more, and then it has its
    // whole request. B, cut back beside the holder, is left short when the
    // holder lets go until it asks again, and then the two are equal.
    void aHolderChangeGivesTheRulesOnlyToDevicesThatAskAgain()
    {
        const DisplayBudgetCharge p = spectrumDisplayCost(256, 30, false)->charge;
        // Samples are the limit that binds (bytes have ample room: a pan's
        // endpoint charge carries more bytes than the bare spectrum cost),
        // at 30 frames a second so four pans stay inside the sender's
        // message rate.
        const DisplayBudgetLimits total{40 * p.applicationBytesPerSecond,
                                        4 * p.spectrumSampleUnitsPerSecond, 1};
        MediaCore m(total);
        m.signInBoth();
        QTRY_COMPARE(m.hub->controllerCount(), 2);
        const quint64 epochA = m.epochOf(0);
        const int sliceA = m.sliceOf(epochA);
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centre =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceA)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));
        // One pan: 256 pixels (all its window's bins) at 30 frames a second,
        // above the one-useful-pan floor.
        QVERIFY(p.spectrumSampleUnitsPerSecond
                > DisplayLoadGovernor::floorPanCharge().spectrumSampleUnitsPerSecond);
        const auto pan = [&centre](quint32 endpoint, int slice, quint32 revision = 1) {
            QJsonObject request = displayRequestAt(endpoint, slice, centre, 30, revision);
            request.insert(QStringLiteral("pixels"), 256);
            return request;
        };
        const auto samples = [](const LoopbackTransport* app) {
            return static_cast<quint64>(
                latestCapability(app->received(), QStringLiteral("spectrumSampleUnitsPerSecond"))
                    .toInteger());
        };
        const auto accepted = [](const LoopbackTransport* app, quint32 endpoint) {
            for (const QJsonObject& r : mediaOps(app, QStringLiteral("allocation-result"))) {
                if (r.value(QStringLiteral("endpointId")).toInteger() == endpoint
                    && r.value(QStringLiteral("accepted")).toBool()) {
                    return true;
                }
            }
            return false;
        };
        for (quint32 endpoint = 1; endpoint <= 2; ++endpoint) {
            sendMedia(m.appA, pan(endpoint, sliceA));
            sendMedia(m.appB, pan(endpoint, sliceB));
        }
        QTRY_VERIFY(accepted(m.appA, 2) && accepted(m.appB, 2));
        QTRY_COMPARE(samples(m.appA), 2 * p.spectrumSampleUnitsPerSecond);
        QTRY_COMPARE(samples(m.appB), 2 * p.spectrumSampleUnitsPerSecond);

        // A becomes the present holder: rule 1 gives it its request, which
        // is still half.
        m.core.server->setDisplayBudgetHolderForTest(DisplayBudgetHolderKind::Device, epochA);
        QTest::qWait(50);
        QCOMPARE(samples(m.appA), 2 * p.spectrumSampleUnitsPerSecond);
        // A asks again for what its operator wants (four pans): all of it.
        for (quint32 endpoint = 3; endpoint <= 4; ++endpoint) {
            sendMedia(m.appA, pan(endpoint, sliceA));
        }
        QTRY_VERIFY(accepted(m.appA, 3) && accepted(m.appA, 4));
        QTRY_COMPARE(samples(m.appA), 4 * p.spectrumSampleUnitsPerSecond);

        // B, cut back beside the holder, drops a pan.
        QJsonObject drop{{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                         {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
                         {QStringLiteral("endpointId"), 2},
                         {QStringLiteral("revision"), 2}};
        sendMedia(m.appB, drop);
        // The holder lets go: B is left with the one pan it still asks for.
        m.core.server->setDisplayBudgetHolderForTest(DisplayBudgetHolderKind::Unheld);
        QTRY_COMPARE(samples(m.appB), p.spectrumSampleUnitsPerSecond);
        QCOMPARE(samples(m.appA), 3 * p.spectrumSampleUnitsPerSecond);
        // B asks again for its two pans (a closed display's id is not used
        // again): equal halves (rule 3).
        sendMedia(m.appB, pan(3, sliceB));
        QTRY_COMPARE(samples(m.appB), 2 * p.spectrumSampleUnitsPerSecond);
        QTRY_COMPARE(samples(m.appA), 2 * p.spectrumSampleUnitsPerSecond);
    }

    void eachDevicesRequestIsWhatItsDisplaysAskFor()
    {
        const auto pan = spectrumDisplayCost(128, 60, false);
        QVERIFY(pan.has_value());
        const DisplayBudgetCharge p = pan->charge;
        const DisplayBudgetCharge f = DisplayLoadGovernor::floorPanCharge();
        QVERIFY(p.applicationBytesPerSecond > f.applicationBytesPerSecond);
        QVERIFY(p.spectrumSampleUnitsPerSecond > f.spectrumSampleUnitsPerSecond);
        const auto total = [&](quint64 pans) {
            return DisplayBudgetLimits{f.applicationBytesPerSecond + pans * p.applicationBytesPerSecond,
                                       f.spectrumSampleUnitsPerSecond
                                           + pans * p.spectrumSampleUnitsPerSecond,
                                       1};
        };
        MediaCore m(total(3));
        Device soundOnly(QStringLiteral("Shack speaker"), QStringLiteral("phone"));
        m.core.pair(soundOnly);
        m.signInBoth();
        LoopbackTransport* appC = m.core.signIn(soundOnly);
        QVERIFY(admitted(appC));
        QTRY_COMPARE(m.hub->controllerCount(), 3);
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centre =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceA)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));

        // B: one pan.
        sendMedia(m.appB, displayRequestAt(1, sliceB, centre, 60));
        QTRY_COMPARE(mediaOps(m.appB, QStringLiteral("allocation-result")).size(), 1);
        QVERIFY(mediaOps(m.appB, QStringLiteral("allocation-result")).last()
                    .value(QStringLiteral("accepted")).toBool());
        // A: four pans; what does not fit its share is refused, and still
        // asked for.
        for (quint32 endpoint = 1; endpoint <= 4; ++endpoint) {
            sendMedia(m.appA, displayRequestAt(endpoint, sliceA, centre, 60));
        }
        QTRY_COMPARE(mediaOps(m.appA, QStringLiteral("allocation-result")).size(), 4);

        const auto share = [](const LoopbackTransport* app, const char* name) {
            return static_cast<quint64>(
                latestCapability(app->received(), QString::fromLatin1(name)).toInteger());
        };
        // The sound-only device: one useful pan, its floor.
        QTRY_COMPARE(share(appC, "displayApplicationBytesPerSecond"), f.applicationBytesPerSecond);
        QCOMPARE(share(appC, "spectrumSampleUnitsPerSecond"), f.spectrumSampleUnitsPerSecond);
        QCOMPARE(latestCapability(appC->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("none"));
        // The one-pan device gets its one pan.
        QTRY_COMPARE(share(m.appB, "displayApplicationBytesPerSecond"),
                     p.applicationBytesPerSecond);
        QCOMPARE(share(m.appB, "spectrumSampleUnitsPerSecond"), p.spectrumSampleUnitsPerSecond);
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("none"));
        // The four-pan device the rest, below its request, told the
        // connection is shared.
        QTRY_COMPARE(share(m.appA, "displayApplicationBytesPerSecond"),
                     2 * p.applicationBytesPerSecond);
        QCOMPARE(share(m.appA, "spectrumSampleUnitsPerSecond"), 2 * p.spectrumSampleUnitsPerSecond);
        QCOMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("sharedConnection"));
        int acceptedA = 0;
        for (const QJsonObject& r : mediaOps(m.appA, QStringLiteral("allocation-result"))) {
            acceptedA += r.value(QStringLiteral("accepted")).toBool() ? 1 : 0;
        }
        QCOMPARE(acceptedA, 2);

        // Under the governor's cut the device short of its request hears
        // sharedProcessing; the one given its request the total's reason.
        DisplayBudgetLimits cut = total(2);
        cut.generation = 2;
        QVERIFY(m.core.server->setDisplayBudgetLimits(cut, DisplayBudgetReason::CoreBusy));
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedProcessing"));
        QCOMPARE(latestCapability(m.appB->received(), QStringLiteral("displayBudgetReason"))
                     .toString(), QStringLiteral("coreBusy"));
        QCOMPARE(share(m.appB, "displayApplicationBytesPerSecond"), p.applicationBytesPerSecond);
        DisplayBudgetLimits restored = total(3);
        restored.generation = 3;
        QVERIFY(m.core.server->setDisplayBudgetLimits(restored, DisplayBudgetReason::None));
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedConnection"));

        // B closes its pan: its demand goes (to the floor), and A grows into
        // what B no longer asks for.
        QJsonObject stop{{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                         {QStringLiteral("connectionId"), QLatin1String(kMediaConnection)},
                         {QStringLiteral("endpointId"), 1},
                         {QStringLiteral("revision"), 2}};
        sendMedia(m.appB, stop);
        QTRY_COMPARE(share(m.appA, "displayApplicationBytesPerSecond"),
                     restored.applicationBytesPerSecond - 2 * f.applicationBytesPerSecond);
        QCOMPARE(share(m.appB, "displayApplicationBytesPerSecond"), f.applicationBytesPerSecond);
    }

    // Fix wave 2 (Critical 1, the re-review's case): A alone subscribes
    // displays costing the whole total. B is admitted: before it asks for
    // anything it has one useful pan, not a share of 1. Once B asks for
    // the whole total too, it has at least half.
    void aDeviceThatJoinsSecondIsNotStarved()
    {
        const auto pan = spectrumDisplayCost(128, 60, false);
        QVERIFY(pan.has_value());
        const DisplayBudgetCharge p = pan->charge;
        const DisplayBudgetLimits total{2 * p.applicationBytesPerSecond,
                                        2 * p.spectrumSampleUnitsPerSecond, 1};
        MediaCore m(total);
        m.appA = m.core.signIn(m.a);
        QVERIFY(admitted(m.appA));
        QTRY_COMPARE(m.hub->controllerCount(), 1);
        const int sliceA = m.sliceOf(m.epochOf(0));
        const double centre =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceA)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        for (quint32 endpoint = 1; endpoint <= 2; ++endpoint) {
            sendMedia(m.appA, displayRequestAt(endpoint, sliceA, centre, 60));
        }
        QTRY_COMPARE(mediaOps(m.appA, QStringLiteral("allocation-result")).size(), 2);
        for (const QJsonObject& r : mediaOps(m.appA, QStringLiteral("allocation-result"))) {
            QVERIFY(r.value(QStringLiteral("accepted")).toBool());
        }

        m.appB = m.core.signIn(m.b);
        QVERIFY(admitted(m.appB));
        QTRY_COMPARE(m.hub->controllerCount(), 2);
        const auto share = [](const LoopbackTransport* app, const char* name) {
            return static_cast<quint64>(
                latestCapability(app->received(), QString::fromLatin1(name)).toInteger());
        };
        // The newcomer, before it asks: one useful pan.
        const DisplayBudgetCharge f = DisplayLoadGovernor::floorPanCharge();
        QTRY_VERIFY(share(m.appB, "displayApplicationBytesPerSecond")
                    >= f.applicationBytesPerSecond);
        QVERIFY(share(m.appB, "spectrumSampleUnitsPerSecond") >= f.spectrumSampleUnitsPerSecond);
        // A, below what it asks for beside B: the connection is shared.
        QTRY_COMPARE(latestCapability(m.appA->received(), QStringLiteral("displayBudgetReason"))
                         .toString(), QStringLiteral("sharedConnection"));

        // B asks for the whole total: at least half of it.
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centreB =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceB)->streamIndex());
        QVERIFY(m.startMedia(m.appB));
        for (quint32 endpoint = 1; endpoint <= 2; ++endpoint) {
            sendMedia(m.appB, displayRequestAt(endpoint, sliceB, centreB, 60));
        }
        QTRY_COMPARE(mediaOps(m.appB, QStringLiteral("allocation-result")).size(), 2);
        QTRY_VERIFY(share(m.appB, "displayApplicationBytesPerSecond")
                    >= total.applicationBytesPerSecond / 2);
        QVERIFY(share(m.appB, "spectrumSampleUnitsPerSecond")
                >= total.spectrumSampleUnitsPerSecond / 2);
        QCOMPARE(share(m.appA, "displayApplicationBytesPerSecond"),
                 total.applicationBytesPerSecond / 2);
    }

    // Fix wave 2 (Important 2): a display refused for the budget counts in
    // its device's request only until the client asks for it again or
    // closes it, or the hold after its refusal runs out. B's refused pan,
    // dropped without an unsubscribe, stops cutting A once the hold ends;
    // refused again and then closed, it stops at once.
    void aRefusedDisplaysRequestEndsWhenItIsNotAskedForAgain()
    {
        const auto pan = spectrumDisplayCost(128, 60, false);
        const auto wide = spectrumDisplayCost(256, 60, false);
        QVERIFY(pan.has_value() && wide.has_value());
        const DisplayBudgetCharge p = pan->charge;
        MediaCore m(DisplayBudgetLimits{4 * p.applicationBytesPerSecond,
                                        4 * p.spectrumSampleUnitsPerSecond, 1});
        m.signInBoth();
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        const double centreA =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceA)->streamIndex());
        const double centreB =
            m.core.model->streamCentreHz(m.core.model->sliceById(sliceB)->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));
        const auto share = [](const LoopbackTransport* app) {
            return static_cast<quint64>(
                latestCapability(app->received(), QStringLiteral("displayApplicationBytesPerSecond"))
                    .toInteger());
        };
        const auto results = [](const LoopbackTransport* app) {
            return mediaOps(app, QStringLiteral("allocation-result"));
        };

        // A: three pans, all admitted.
        for (quint32 endpoint = 1; endpoint <= 3; ++endpoint) {
            sendMedia(m.appA, displayRequestAt(endpoint, sliceA, centreA, 60));
        }
        QTRY_COMPARE(results(m.appA).size(), 3);
        for (const QJsonObject& r : results(m.appA)) {
            QVERIFY(r.value(QStringLiteral("accepted")).toBool());
        }
        // B: one pan, admitted; A keeps its three.
        sendMedia(m.appB, displayRequestAt(1, sliceB, centreB, 60));
        QTRY_COMPARE(results(m.appB).size(), 1);
        QVERIFY(results(m.appB).last().value(QStringLiteral("accepted")).toBool());
        QTRY_COMPARE(share(m.appA), 3 * p.applicationBytesPerSecond);

        // B asks for a wider pan that does not fit: refused, and while its
        // request stands A is cut to half.
        DaemonMediaController* controllerB = m.hub->controllerFor(m.epochOf(1));
        QVERIFY(controllerB);
        controllerB->setRefusedDisplayDemandHoldMsForTest(300);
        QJsonObject wider = displayRequestAt(2, sliceB, centreB, 60);
        wider.insert(QStringLiteral("pixels"), 256);
        sendMedia(m.appB, wider);
        QTRY_COMPARE(results(m.appB).size(), 2);
        QVERIFY(!results(m.appB).last().value(QStringLiteral("accepted")).toBool(true));
        QCOMPARE(results(m.appB).last().value(QStringLiteral("reason")).toString(),
                 QLatin1String(kDisplayBudgetRefusalReason));
        QTRY_COMPARE(share(m.appA), 2 * p.applicationBytesPerSecond);
        // B drops it without a word: once the hold runs out, A has its
        // three pans' room back.
        QTRY_COMPARE(share(m.appA), 3 * p.applicationBytesPerSecond);

        // Refused again, then closed: A has its room back at once, long
        // before any hold would end.
        controllerB->setRefusedDisplayDemandHoldMsForTest(60'000);
        wider.insert(QStringLiteral("revision"), 2);
        sendMedia(m.appB, wider);
        QTRY_COMPARE(results(m.appB).size(), 3);
        QVERIFY(!results(m.appB).last().value(QStringLiteral("accepted")).toBool(true));
        QTRY_COMPARE(share(m.appA), 2 * p.applicationBytesPerSecond);
        sendMedia(m.appB, QJsonObject{{QStringLiteral("op"), QStringLiteral("unsubscribe")},
                                      {QStringLiteral("connectionId"),
                                       QLatin1String(kMediaConnection)},
                                      {QStringLiteral("endpointId"), 2},
                                      {QStringLiteral("revision"), 3}});
        QTRY_COMPARE(share(m.appA), 3 * p.applicationBytesPerSecond);
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
        // Fix wave: the tone fed first is still queued ahead of the silence
        // (three blocks fed, about one taken), so A falls silent once it has
        // drained; a fixed packet count raced the drain under load.
        QTRY_COMPARE(l16Left(transportA->rtpPackets.last()).last(), 0.0f);
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

    // Fix wave (ruling 9.1): a display on a slice that passes to another
    // owner retires as a removed slice's does; the new owner's goes on.
    void aDisplayRetiresWhenItsSlicePassesToAnotherOwner()
    {
        MediaCore m(DisplayBudgetLimits{10'000'000, 1'000'000, 1});
        m.signInBoth();
        const int sliceA = m.sliceOf(m.epochOf(0));
        const int sliceB = m.sliceOf(m.epochOf(1));
        SliceModel* a = m.core.model->sliceById(sliceA);
        QVERIFY(a != nullptr && sliceB >= 0);
        const double centre = m.core.model->streamCentreHz(a->streamIndex());
        QVERIFY(m.startMedia(m.appA));
        QVERIFY(m.startMedia(m.appB));
        sendMedia(m.appA, displayRequest(2, sliceA, centre));
        sendMedia(m.appB, displayRequest(1, sliceB, centre));
        QTRY_COMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 1);
        QTRY_COMPARE(m.hub->controllerFor(m.epochOf(1))->activeEndpointCount(), 1);

        m.core.model->sliceOwnership()->setOwner(sliceA, m.b.key.fingerprint());
        QTRY_COMPARE(m.hub->controllerFor(m.epochOf(0))->activeEndpointCount(), 0);
        const auto retired = [&m]() {
            for (const QString& op : {QStringLiteral("allocation-result"), QStringLiteral("rejected")}) {
                for (const QJsonObject& o : mediaOps(m.appA, op)) {
                    if (o.value(QStringLiteral("endpointId")).toInteger() == 2
                        && o.value(QStringLiteral("reason")).toString()
                            == QLatin1String(kRetireReasonSliceRemoved)) {
                        return true;
                    }
                }
            }
            return false;
        };
        QTRY_VERIFY(retired());
        QCOMPARE(m.hub->controllerFor(m.epochOf(1))->activeEndpointCount(), 1);
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
