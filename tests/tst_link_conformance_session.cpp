// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_link_conformance_session.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 3 (R-IOS-01): the station runs the link's session
// fixtures (tests/data/link/v1/sessions/*.json). Each describes a station
// (stationSetup) and a script of client and station messages, virtual time
// and closes; LinkFixtures::runSession plays the client's half against a
// real StationServer over the in-process loopback and checks the station's
// half. The app runs the same scripts the other way round.
//
// stationSetup, as this runner builds it:
//   radio            "static": a RadioModel reporting an HL2 connected
//                    (MAC AA:BB:CC:DD:EE:01), with no radio behind it and its
//                    slice meter pump stopped, so nothing changes on its own;
//                    "connectable": ConnectableRadioModel, a RadioModel
//                    connected to the P1 fake radio, WDSP channels and all
//   slices           slices the model holds before the client connects (1)
//   panadapters      panadapters it holds (0)
//   coreAccessories  the Core owns its accessories (the amplifier, RF-Kit
//                    and tuner objects and their verbs) (false)
//   stationTci       the Core runs a station TCI server; it stays off (false)
//   stepAttenuator   a step attenuator controller is bound (false)
//   media            the station's media is enabled (false)
//   priorFailedAuthentications
//                    other clients that each sent a wrong token before this
//                    one connects (0); the lockout is the station's, not a
//                    peer's
//   otherConnections other clients connected and still connecting, which
//                    send nothing (0); with 8 the station is at its limit
//   clientAnswersPings, preemptingClient   read by the player itself
//
// NEREUS_LINK_TRACE_DIR, when set, receives every message the station sent
// in each fixture (<id>.jsonl), for writing a fixture to what the code does.
//
//   cmake --build build --target tst_link_conformance_session
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build \
//       -R '^tst_link_conformance_session$' --output-on-failure
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 3 (R-IOS-01): session
//                                    conformance runner. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): runs once per link major in
//                                    the manifest, against a station that
//                                    offers that major.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4b (R-IOS-01): the
//                                    connectable station waits for
//                                    PureSignal's readiness to settle.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01):
//                                    PureSignal's readiness follows the
//                                    receive-only station at once; only
//                                    the slices' readings are waited for.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A fix wave (R-IOS-01):
//                                    fixtures say which ends run them and
//                                    each client step's role; placeholders
//                                    in client messages.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  Lane B takes integration (R-IOS-01,
//                                    R-R3-47): a verb's optional
//                                    arguments may be left out of either
//                                    leg's check (setPgxlHardware).
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>

#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTemporaryDir>

#include <functional>
#include <memory>
#include <vector>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/StepAttenuatorController.h"
#include "core/dsp/DspAssetService.h"
#include "core/meters/SliceMeterPump.h"
#include "core/security/TokenStore.h"
#include "core/session/LinkVersion.h"
#include "core/session/PureSignalSessionFacade.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsScope.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "LinkFixtures.h"
#include "OperatorWording.h"
#include "fakes/ConnectableRadioModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;
using NereusSDR::Test::ConnectableRadioModel;
using NereusSDR::Test::LinkFixtures;
using NereusSDR::Test::LoopbackTransport;

namespace {

const QStringList kSetupKeys{
    QStringLiteral("radio"),           QStringLiteral("slices"),
    QStringLiteral("panadapters"),     QStringLiteral("coreAccessories"),
    QStringLiteral("stationTci"),      QStringLiteral("stepAttenuator"),
    QStringLiteral("media"),           QStringLiteral("priorFailedAuthentications"),
    QStringLiteral("clientAnswersPings"), QStringLiteral("preemptingClient"),
    QStringLiteral("otherConnections"),
};

// The station a fixture's stationSetup describes. Members are declared in
// the order that makes destruction safe: the server goes first, then the
// model, then the controller the model points at.
struct Station {
    QTemporaryDir dir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<StepAttenuatorController> stepAtt;
    std::unique_ptr<ConnectableRadioModel> harness;
    std::unique_ptr<RadioModel> ownModel;
    RadioModel* model = nullptr;
    // otherConnections: the far ends of connections still connecting,
    // kept open until the fixture ends (the server goes first).
    std::vector<std::unique_ptr<LoopbackTransport>> others;
    std::unique_ptr<StationServer> server;

    ~Station()
    {
        server.reset();
        if (model != nullptr && stepAtt) {
            model->setStepAttController(nullptr);
        }
    }
};

QString buildStation(const QJsonObject& setup, Station* station, quint16 major)
{
    for (auto it = setup.constBegin(); it != setup.constEnd(); ++it) {
        if (!kSetupKeys.contains(it.key())) {
            return QStringLiteral("stationSetup: unknown field \"%1\"").arg(it.key());
        }
    }
    if (!station->dir.isValid()) {
        return QStringLiteral("no scratch directory");
    }
    // RadioModel keeps state in AppSettings::instance() (notches, accessory
    // choices, DSP asset selections). Each fixture starts from an empty
    // profile, so one fixture's writes never reach the next.
    AppSettings::instance().clear();
    station->settings = std::make_unique<AppSettings>(
        station->dir.filePath(QStringLiteral("NereusSDR.settings")));

    const QString radio = setup.value(QStringLiteral("radio")).toString(QStringLiteral("static"));
    if (radio == QStringLiteral("connectable")) {
        station->harness = ConnectableRadioModel::create();
        if (!station->harness) {
            return QStringLiteral("the connectable radio model did not connect");
        }
        station->model = &station->harness->model();
    } else if (radio == QStringLiteral("static")) {
        station->ownModel = std::make_unique<RadioModel>();
        station->model = station->ownModel.get();
        station->model->setBoardForTest(HPSDRHW::HermesLite);
        RadioInfo info;
        info.macAddress = QStringLiteral("AA:BB:CC:DD:EE:01");
        info.name = QStringLiteral("Bench HL2");
        info.boardType = HPSDRHW::HermesLite;
        station->model->setLastRadioInfoForTest(info);
        station->model->setConnectionStateForTest(ConnectionState::Connected);
        // No WDSP channels, so the pump would write its no-reading value to
        // every slice on each poll, on real time. Stopped, nothing changes
        // on its own and every delta in a fixture is one the script caused.
        if (SliceMeterPump* pump = station->model->sliceMeterPump()) {
            pump->stop();
        }
    } else {
        return QStringLiteral("stationSetup.radio must be \"static\" or \"connectable\"");
    }

    RadioModel& model = *station->model;
    if (setup.value(QStringLiteral("coreAccessories")).toBool(false)) {
        model.enableStationAccessoryIdentity();
    }
    if (setup.value(QStringLiteral("stationTci")).toBool(false)) {
        model.enableStationTci(QStringLiteral("127.0.0.1"));
    }
    if (setup.value(QStringLiteral("stepAttenuator")).toBool(false)) {
        station->stepAtt = std::make_unique<StepAttenuatorController>();
        station->stepAtt->setTickTimerEnabled(false);
        model.setStepAttController(station->stepAtt.get());
    }
    const int slices = setup.value(QStringLiteral("slices")).toInt(1);
    while (model.slices().size() < slices) {
        model.addSlice(QStringLiteral("pan-0"));
    }
    const int pans = setup.value(QStringLiteral("panadapters")).toInt(0);
    for (int i = 0; i < pans; ++i) {
        model.addPanadapter();
    }
    if (radio == QStringLiteral("static")) {
        if (SliceMeterPump* pump = model.sliceMeterPump()) {
            pump->stop();
        }
    }

    // Provision the throwaway token first, so the server loads it rather
    // than generating one and printing its first-run pairing banner.
    { TokenStore provision(station->dir.path()); }
    // A station that offers exactly the link major this pass covers.
    station->server = std::make_unique<StationServer>(station->model, *station->settings,
                                                      station->dir.path(), nullptr,
                                                      QList<quint16>{major});
    if (setup.value(QStringLiteral("media")).toBool(false)) {
        station->server->setMediaEnabled(true);
    }
    if (station->harness) {
        // A StationServer makes its radio receive-only
        // (setReceiveOnlyStationPolicy), which turns PureSignal's readiness
        // (canActuate) off; the pureSignal object follows on that call
        // (RadioModel::receiveOnlyStationPolicyChanged), so it is settled
        // here already.
        PureSignalSessionFacade* const facade = station->model->pureSignalFacade();
        if (facade == nullptr || facade->canActuate()) {
            return QStringLiteral("PureSignal's readiness did not follow the receive-only "
                                  "station");
        }
        // Each slice's signal readings: the meter pump writes the
        // no-reading value until the receiver's meter has one.
        const bool readings = QTest::qWaitFor([station]() {
            for (SliceModel* slice : station->model->slices()) {
                if (slice->signalStrengthDbm() <= SliceMeterPump::kNoReadingDbm
                    || slice->signalPeakDbm() <= SliceMeterPump::kNoReadingDbm
                    || slice->signalAverageDbm() <= SliceMeterPump::kNoReadingDbm) {
                    return false;
                }
            }
            return true;
        }, 5000);
        if (!readings) {
            return QStringLiteral("the slices' signal readings did not settle");
        }
    }

    // Other clients connected and still connecting (they send nothing), so
    // the fixture's client is the next one to arrive.
    const int others = setup.value(QStringLiteral("otherConnections")).toInt(0);
    for (int i = 0; i < others; ++i) {
        auto other = std::make_unique<LoopbackTransport>(QStringLiteral("conformance-waiting-client"));
        auto* otherEnd =
            new LoopbackTransport(QStringLiteral("conformance-waiting"), station->server.get());
        otherEnd->linkTo(other.get());
        station->server->acceptTransport(otherEnd);
        station->others.push_back(std::move(other));
    }
    QCoreApplication::processEvents();

    const int failures = setup.value(QStringLiteral("priorFailedAuthentications")).toInt(0);
    for (int i = 0; i < failures; ++i) {
        LoopbackTransport other(QStringLiteral("conformance-other-client"));
        auto* otherEnd =
            new LoopbackTransport(QStringLiteral("conformance-other"), station->server.get());
        otherEnd->linkTo(&other);
        station->server->acceptTransport(otherEnd);
        other.sendText(SessionMessages::encode(SessionMessages::hello(
            kSessionProtocolMajor, kSessionProtocolMinor, 0, QStringLiteral("conformance-other"))));
        other.sendText(SessionMessages::encode(
            SessionMessages::authRequest(QStringLiteral("not-the-station-token"))));
        // Event processing only: the refusal and the close are queued work.
        const QDeadlineTimer deadline(5000);
        while (other.isOpen() && !deadline.hasExpired()) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (other.isOpen()) {
            return QStringLiteral("a prior client's wrong token was not refused");
        }
    }
    return QString();
}

// A placeholder text of the link document's section 16.3.
bool isPlaceholderText(const QJsonValue& value)
{
    return value.isString() && value.toString().startsWith(QLatin1Char('$'));
}

// Every string value at any depth under `value`, with its path.
void collectStrings(const QJsonValue& value, const QString& path,
                    QList<QPair<QString, QString>>* out)
{
    if (value.isString()) {
        out->append({path, value.toString()});
    } else if (value.isObject()) {
        const QJsonObject o = value.toObject();
        for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
            collectStrings(it.value(), path + QLatin1Char('.') + it.key(), out);
        }
    } else if (value.isArray()) {
        const QJsonArray a = value.toArray();
        for (int i = 0; i < a.size(); ++i) {
            collectStrings(a.at(i), QStringLiteral("%1[%2]").arg(path).arg(i), out);
        }
    }
}

// Why a fixture marked for the app holds something a conformant client
// could not send, or an app runner could not play (section 16.3); empty
// when it holds none. `surface` is surface.json.
QStringList appConformanceProblems(const QString& id, const QJsonObject& fixture,
                                   const QJsonObject& surface)
{
    QStringList problems;
    const auto fail = [&problems, &id](int step, const QString& why) {
        problems.append(QStringLiteral("%1 step %2: %3").arg(id).arg(step).arg(why));
    };
    QHash<QString, QJsonObject> verbs;
    for (const QJsonValue& v : surface.value(QStringLiteral("commands")).toArray()) {
        verbs.insert(v.toObject().value(QStringLiteral("verb")).toString(), v.toObject());
    }
    const QJsonObject classes = surface.value(QStringLiteral("mirrorClasses")).toObject();
    const QSet<QString> clientKinds{
        QStringLiteral("hello"),          QStringLiteral("auth.request"),
        QStringLiteral("command.invoke"), QStringLiteral("media.control"),
        QStringLiteral("property.write"), QStringLiteral("settings.write"),
        QStringLiteral("settings.remove")};
    QHash<QString, QString> classOfKey;
    QHash<QString, double> capabilities;
    int agreedMinor = -1;
    const QJsonArray steps = fixture.value(QStringLiteral("steps")).toArray();
    for (int index = 0; index < steps.size(); ++index) {
        const QJsonObject step = steps.at(index).toObject();
        const QJsonObject message = step.value(QStringLiteral("message")).toObject();
        const QString type = message.value(QStringLiteral("type")).toString();
        const QString from = step.value(QStringLiteral("from")).toString();
        if (from == QStringLiteral("station")) {
            // What an app runner sends its client: nothing it cannot fill.
            QList<QPair<QString, QString>> strings;
            collectStrings(message, QStringLiteral("$"), &strings);
            for (const auto& [path, text] : strings) {
                const bool summarised =
                    text == QStringLiteral("$any")
                    && ((type == QStringLiteral("schema") && path == QStringLiteral("$.fields"))
                        || (type == QStringLiteral("object.create")
                            && path == QStringLiteral("$.properties")));
                if (text.startsWith(QStringLiteral("$capture:"))
                    || text.startsWith(QStringLiteral("$string:"))
                    || text.startsWith(QStringLiteral("$int:"))
                    || (text == QStringLiteral("$any") && !summarised)
                    || text == QStringLiteral("$object") || text == QStringLiteral("$majors")) {
                    fail(index, QStringLiteral("%1 is %2, which an app runner cannot send")
                                    .arg(path, text));
                }
            }
            if (type == QStringLiteral("object.create")) {
                classOfKey.insert(message.value(QStringLiteral("key")).toString(),
                                  message.value(QStringLiteral("class")).toString());
            }
            if ((type == QStringLiteral("schema") || type == QStringLiteral("object.create"))
                && !classes.contains(message.value(QStringLiteral("class")).toString())) {
                fail(index, QStringLiteral("class not in mirrorClasses, so no stand-in"));
            }
            if (type == QStringLiteral("capabilities")) {
                capabilities.clear();
                for (const QJsonValue& p : message.value(QStringLiteral("properties")).toArray()) {
                    capabilities.insert(p.toObject().value(QStringLiteral("name")).toString(),
                                        p.toObject().value(QStringLiteral("value")).toDouble());
                }
            }
            continue;
        }
        if (from != QStringLiteral("client")) {
            continue;
        }
        const QString role = step.value(QStringLiteral("role")).toString();
        if ((type == QStringLiteral("hello") || type == QStringLiteral("auth.request"))
            && role != QStringLiteral("behaviour")) {
            fail(index, QStringLiteral("the client makes its own %1; it cannot be scripted")
                            .arg(type));
        }
        if (role != QStringLiteral("behaviour")) {
            // A scripted message is filled alike by both runners only when
            // it names nothing: no $string:<name> or $int:<name> (so only
            // the station's runner ever advances the counter), and its own
            // ids are literals from 1000 up, clear of the client's.
            QList<QPair<QString, QString>> strings;
            collectStrings(message, QStringLiteral("$"), &strings);
            for (const auto& [path, text] : strings) {
                if (text.startsWith(QLatin1Char('$')) && text != QStringLiteral("$ref:token")) {
                    fail(index, QStringLiteral("a scripted message holds %1 at %2").arg(text, path));
                }
            }
            for (const QString& key : {QStringLiteral("id"), QStringLiteral("writeId")}) {
                if ((type == QStringLiteral("command.invoke") || type == QStringLiteral("property.write"))
                    && message.contains(key) && message.value(key).toDouble() < 1000.0) {
                    fail(index, QStringLiteral("a scripted %1 is a literal from 1000 up").arg(key));
                }
            }
            continue;
        }
        if (!clientKinds.contains(type)) {
            fail(index, QStringLiteral("a client never sends %1").arg(type));
            continue;
        }
        if (type == QStringLiteral("hello")) {
            // An app lists every major it supports (its own and the one
            // before, section 6.1), so a fixture cannot pin the list.
            if (message.value(QStringLiteral("majors")) != QJsonValue(QStringLiteral("$majors"))) {
                fail(index, QStringLiteral("hello must carry majors as \"$majors\""));
            }
            if (!message.contains(QStringLiteral("features"))) {
                fail(index, QStringLiteral("hello must carry features"));
            }
            if (message.value(QStringLiteral("peer")) != QJsonValue(QStringLiteral("$string"))
                || message.value(QStringLiteral("settingsSchema"))
                       != QJsonValue(QStringLiteral("$int"))) {
                fail(index, QStringLiteral("peer and settingsSchema are the app's own: "
                                           "$string and $int"));
            }
            agreedMinor = message.value(QStringLiteral("minor")).toInt();
        } else if (type == QStringLiteral("auth.request")) {
            const QString token = message.value(QStringLiteral("token")).toString();
            if (token != QStringLiteral("$ref:token") && token != QStringLiteral("$string")) {
                fail(index, QStringLiteral("token must be $ref:token or $string"));
            }
        } else if (type == QStringLiteral("command.invoke")) {
            const QString verb = message.value(QStringLiteral("verb")).toString();
            // Section 9.1: an id is a whole number the client chooses, from
            // 0 to 4294967295, and from 1 for the nnr, ps3 and dspAssets
            // families. The placeholder holds the client to that range.
            const bool fromOne = verb.startsWith(QStringLiteral("nnr."))
                || verb.startsWith(QStringLiteral("ps3."))
                || verb.startsWith(QStringLiteral("dspAssets."));
            const QString id = message.value(QStringLiteral("id")).toString();
            const QString range = fromOne ? QStringLiteral(":1:4294967295")
                                          : QStringLiteral(":0:4294967295");
            if (!id.startsWith(QStringLiteral("$int:")) || !id.endsWith(range)
                || id.count(QLatin1Char(':')) != 3) {
                fail(index, QStringLiteral("id is the app's own: $int:<name>%1").arg(range));
            }
            QList<QPair<QString, QString>> argStrings;
            collectStrings(message.value(QStringLiteral("args")), QStringLiteral("$.args"),
                           &argStrings);
            for (const auto& [path, text] : argStrings) {
                if (text.startsWith(QLatin1Char('$'))) {
                    fail(index, QStringLiteral("%1 holds %2: arguments are what the app is told "
                                               "to send, never placeholders")
                                    .arg(path, text));
                }
            }
            if (!verbs.contains(verb)) {
                fail(index, QStringLiteral("%1 is not a verb").arg(verb));
                continue;
            }
            const QJsonObject spec = verbs.value(verb);
            const QJsonArray declared = spec.value(QStringLiteral("arguments")).toArray();
            const QJsonArray args = message.value(QStringLiteral("args")).toArray();
            // The declared arguments in order, each with its kind; an
            // optional one may be left out (setPgxlHardware takes one of
            // its three), a required one may not.
            bool fits = true;
            int next = 0;
            for (int d = 0; fits && d < declared.size(); ++d) {
                const QJsonObject want = declared.at(d).toObject();
                const bool present = next < args.size()
                    && args.at(next).toObject().value(QStringLiteral("name"))
                           == want.value(QStringLiteral("name"));
                if (present) {
                    fits = args.at(next).toObject().value(QStringLiteral("kind"))
                        == want.value(QStringLiteral("kind"));
                    ++next;
                } else {
                    fits = want.value(QStringLiteral("optional")).toBool();
                }
            }
            fits = fits && next == args.size() && (args.isEmpty() == declared.isEmpty());
            if (!fits) {
                fail(index, QStringLiteral("%1's arguments are not the ones it takes").arg(verb));
            }
            const QString capability = spec.value(QStringLiteral("capability")).toString();
            const double version = spec.value(QStringLiteral("capabilityVersion")).toDouble();
            // The PureSignal action verbs need psAlgorithmVersion equal to
            // their version, not at least (section 6.2).
            const bool exact = capability == QStringLiteral("psAlgorithmVersion");
            const double advertised = capabilities.value(capability, 0.0);
            if (!capability.isEmpty() && (exact ? advertised != version : advertised < version)) {
                fail(index, QStringLiteral("%1 was not advertised (%2 %3 needed)")
                                .arg(verb, capability).arg(version));
            }
            if (agreedMinor < spec.value(QStringLiteral("minMinor")).toInt()) {
                fail(index, QStringLiteral("%1 needs a newer minor").arg(verb));
            }
        } else if (type == QStringLiteral("property.write")) {
            const QString writeId = message.value(QStringLiteral("writeId")).toString();
            if (!writeId.startsWith(QStringLiteral("$int:"))
                || !writeId.endsWith(QStringLiteral(":1:4294967295"))
                || writeId.count(QLatin1Char(':')) != 3) {
                fail(index, QStringLiteral("writeId is the app's own: $int:<name>:1:4294967295"));
            }
            const QString cls = classOfKey.value(message.value(QStringLiteral("key")).toString());
            QHash<QString, QJsonObject> fields;
            for (const QJsonValue& f : classes.value(cls).toObject()
                                            .value(QStringLiteral("properties")).toArray()) {
                fields.insert(f.toObject().value(QStringLiteral("name")).toString(), f.toObject());
            }
            for (const QJsonValue& p : message.value(QStringLiteral("properties")).toArray()) {
                const QJsonObject entry = p.toObject();
                const QJsonObject field =
                    fields.value(entry.value(QStringLiteral("name")).toString());
                if (field.isEmpty()
                    || field.value(QStringLiteral("direction")) == QJsonValue(QStringLiteral("outbound"))
                    || field.value(QStringLiteral("kind")) != entry.value(QStringLiteral("kind"))
                    || field.value(QStringLiteral("ordinal")) != entry.value(QStringLiteral("ordinal"))) {
                    fail(index, QStringLiteral("%1 is not a property a client may write")
                                    .arg(entry.value(QStringLiteral("name")).toString()));
                }
            }
        } else if (type == QStringLiteral("settings.write")
                   || type == QStringLiteral("settings.remove")) {
            const QString key = message.value(QStringLiteral("key")).toString();
            if (classifySettingsKey(key) != SettingsScope::Station) {
                fail(index, QStringLiteral("%1 is not a Core setting").arg(key));
            }
            if (type == QStringLiteral("settings.write")
                && !message.value(QStringLiteral("origin")).toString().startsWith(
                    QStringLiteral("$string:"))) {
                fail(index, QStringLiteral("origin is the app's own: $string:<name>"));
            }
        }
    }
    return problems;
}

void writeTrace(const QString& id, const LoopbackTransport& client)
{
    const QByteArray directory = qgetenv("NEREUS_LINK_TRACE_DIR");
    if (directory.isEmpty()) {
        return;
    }
    QDir().mkpath(QString::fromLocal8Bit(directory));
    QFile file(QDir(QString::fromLocal8Bit(directory)).filePath(id + QStringLiteral(".jsonl")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    for (const QByteArray& wire : client.received()) {
        file.write(wire);
        file.write("\n");
    }
}

} // namespace

class TstLinkConformanceSession : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void sessionFixtures_data();
    void sessionFixtures();
    void everyVerbIsInvokedRightAndWrong();
    void rightAndWrongLegsGetDifferentAnswers();
    void everyFixtureRunsOnTheStation();
    void appFixturesHoldOnlyWhatAConformantClientSends();
    void theConformanceCheckCatchesWhatAnAppCannotSend();
    void refusalsOfOutboundWritesArePlain();
    void alteredFixturesFailReadably();

private:
    QString run(const QString& id, const QJsonObject& fixture,
                quint16 major = kSessionProtocolMajor);
    QJsonObject fixture(const QString& id);
    QJsonObject m_manifest;
};

void TstLinkConformanceSession::initTestCase()
{
    // RadioModel reads AppSettings::instance(); keep this process's copy
    // private, as tst_station_session does.
    const QString profile =
        QStringLiteral("link-conformance-session-%1").arg(QCoreApplication::applicationPid());
    AppSettings::setProfileOverride(profile);
    QCOMPARE(AppSettings::instance().filePath(), AppSettings::resolveSettingsPath(profile));
    AppSettings::instance().clear();
    // Whether this machine's build carries the bundled NR3 model files
    // decides what the dspAssets object says about them. Fixtures are the
    // same on every machine, so none is found here.
    DspAssetService::setBundledNr3ModelPathsForTest([](const QString&) { return QString(); });

    QString error;
    m_manifest = LinkFixtures::readObject(
        QDir(LinkFixtures::dataDirectory()).filePath(QStringLiteral("manifest.json")), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void TstLinkConformanceSession::cleanupTestCase()
{
    DspAssetService::setBundledNr3ModelPathsForTest({});
    const QString path = AppSettings::instance().filePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

QJsonObject TstLinkConformanceSession::fixture(const QString& id)
{
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("session"))) {
        if (entry.id == id) {
            QString error;
            const QJsonObject o = LinkFixtures::readObject(
                QDir(LinkFixtures::dataDirectory()).filePath(entry.file), &error);
            return error.isEmpty() ? o : QJsonObject{};
        }
    }
    return {};
}

QString TstLinkConformanceSession::run(const QString& id, const QJsonObject& fixture,
                                       quint16 major)
{
    Station station;
    const QString problem =
        buildStation(fixture.value(QStringLiteral("stationSetup")).toObject(), &station, major);
    if (!problem.isEmpty()) {
        return problem;
    }
    // Declared after the station, so it goes first; the server owns the
    // station end once it accepts it.
    LoopbackTransport client(QStringLiteral("conformance-client"));
    auto* stationEnd = new LoopbackTransport(QStringLiteral("conformance"), station.server.get());
    stationEnd->linkTo(&client);
    station.server->acceptTransport(stationEnd);
    const QString failure = LinkFixtures::runSession(fixture, *station.server, client);
    writeTrace(id, client);
    return failure;
}

void TstLinkConformanceSession::sessionFixtures_data()
{
    QTest::addColumn<QString>("id");
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("major");
    const QList<LinkFixtures::Entry> entries =
        LinkFixtures::entries(m_manifest, QStringLiteral("session"));
    QVERIFY(!entries.isEmpty());
    // Once per link major the suite covers (manifest linkMajors).
    for (const quint16 major : LinkFixtures::linkMajors(m_manifest)) {
        for (const LinkFixtures::Entry& entry : entries) {
            QTest::newRow(qPrintable(QStringLiteral("%1 link %2").arg(entry.id).arg(major)))
                << entry.id << entry.file << int(major);
        }
    }
}

void TstLinkConformanceSession::sessionFixtures()
{
    QFETCH(QString, id);
    QFETCH(QString, file);
    QFETCH(int, major);
    QVERIFY2(LinkVersion::supportedMajors().contains(quint16(major)),
             qPrintable(QStringLiteral("this station does not offer link major %1").arg(major)));
    QString error;
    const QJsonObject o =
        LinkFixtures::readObject(QDir(LinkFixtures::dataDirectory()).filePath(file), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    if (o.value(QStringLiteral("stationSetup")).toObject()
            .value(QStringLiteral("otherConnections")).toInt(0) > 0) {
        // The station logs the connection it turns away; that is the case.
        QTest::ignoreMessage(QtWarningMsg,
                             QRegularExpression(QStringLiteral("^Refusing connection from")));
    }
    const QString failure = run(id, o, quint16(major));
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

void TstLinkConformanceSession::everyVerbIsInvokedRightAndWrong()
{
    // Each verb in verbSpecs() is invoked by some session fixture with its
    // own argument names, and again with an argument name it does not take.
    QSet<QString> right;
    QSet<QString> wrong;
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("session"))) {
        const QJsonObject o = fixture(entry.id);
        for (const QJsonValue& step : o.value(QStringLiteral("steps")).toArray()) {
            const QJsonObject s = step.toObject();
            const QJsonObject message = s.value(QStringLiteral("message")).toObject();
            if (s.value(QStringLiteral("from")).toString() != QStringLiteral("client")
                || message.value(QStringLiteral("type")).toString()
                       != QStringLiteral("command.invoke")) {
                continue;
            }
            const QString verb = message.value(QStringLiteral("verb")).toString();
            QStringList names;
            for (const QJsonValue& arg : message.value(QStringLiteral("args")).toArray()) {
                names.append(arg.toObject().value(QStringLiteral("name")).toString());
            }
            for (const CommandVerbSpec& spec : SessionCommandDispatcher::verbSpecs()) {
                if (QString::fromUtf8(spec.verb) != verb) {
                    continue;
                }
                QStringList declared;
                QStringList required;
                for (const CommandArgumentSpec& argument : spec.arguments) {
                    declared.append(QString::fromUtf8(argument.name));
                    if (!argument.optional) {
                        required.append(QString::fromUtf8(argument.name));
                    }
                }
                bool unknownName = false;
                for (const QString& name : names) {
                    unknownName = unknownName || !declared.contains(name);
                }
                // Its own arguments: every required one, optional ones as
                // the verb takes them, in the declared order without
                // repeats (setPgxlHardware takes one of its three). With
                // no optional argument this is names == declared.
                bool own = !unknownName && !names.isEmpty() == !declared.isEmpty();
                qsizetype at = -1;
                for (const QString& name : names) {
                    const qsizetype found = declared.indexOf(name);
                    own = own && found > at;
                    at = found;
                }
                for (const QString& name : required) {
                    own = own && names.contains(name);
                }
                if (unknownName) {
                    wrong.insert(verb);
                } else if (own) {
                    right.insert(verb);
                }
            }
        }
    }
    for (const CommandVerbSpec& spec : SessionCommandDispatcher::verbSpecs()) {
        const QString verb = QString::fromUtf8(spec.verb);
        QVERIFY2(right.contains(verb),
                 qPrintable(verb + QStringLiteral(" is never invoked with its own arguments")));
        if (!spec.arguments.isEmpty()) {
            QVERIFY2(wrong.contains(verb),
                     qPrintable(verb + QStringLiteral(" is never invoked with a wrong name")));
        }
    }
}

void TstLinkConformanceSession::rightAndWrongLegsGetDifferentAnswers()
{
    // A verb invoked with its own arguments and again with one renamed
    // must get two different answers, or the fixture does not show the
    // station read the arguments at all (a refusal made before reading
    // them answers both alike). The answer compared is every
    // command.result for the invoke's id (PureSignal answers twice), each
    // as accepted and reason.
    QStringList same;
    int compared = 0;
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("session"))) {
        const QJsonArray steps = fixture(entry.id).value(QStringLiteral("steps")).toArray();
        QHash<QString, QString> right;
        QHash<QString, QString> wrong;
        QHash<QString, QJsonArray> rightArgs;
        QHash<QString, QJsonArray> wrongArgs;
        for (int i = 0; i < steps.size(); ++i) {
            const QJsonObject step = steps.at(i).toObject();
            const QJsonObject message = step.value(QStringLiteral("message")).toObject();
            if (step.value(QStringLiteral("from")).toString() != QStringLiteral("client")
                || message.value(QStringLiteral("type")).toString()
                       != QStringLiteral("command.invoke")
                || message.value(QStringLiteral("args")).toArray().isEmpty()) {
                continue;
            }
            const QJsonValue id = message.value(QStringLiteral("id"));
            const QJsonValue refersTo = id.isString()
                ? QJsonValue(id.toString().replace(QStringLiteral("$int:"), QStringLiteral("$ref:")))
                : id;
            QStringList answers;
            for (int j = i + 1; j < steps.size(); ++j) {
                const QJsonObject reply = steps.at(j).toObject().value(QStringLiteral("message")).toObject();
                if (reply.value(QStringLiteral("type")).toString() == QStringLiteral("command.result")
                    && reply.value(QStringLiteral("id")) == refersTo) {
                    answers.append(QStringLiteral("%1 %2")
                                       .arg(reply.value(QStringLiteral("accepted")).toBool())
                                       .arg(reply.value(QStringLiteral("reason")).toString()));
                }
            }
            const QString answer = answers.join(QStringLiteral(" / "));
            bool renamed = false;
            for (const QJsonValue& arg : message.value(QStringLiteral("args")).toArray()) {
                renamed = renamed
                    || arg.toObject().value(QStringLiteral("name")).toString()
                           == QStringLiteral("conformanceWrongName");
            }
            const QString verb = message.value(QStringLiteral("verb")).toString();
            (renamed ? wrong : right).insert(verb, answer);
            (renamed ? wrongArgs : rightArgs).insert(verb, message.value(QStringLiteral("args")).toArray());
        }
        for (auto it = wrong.constBegin(); it != wrong.constEnd(); ++it) {
            if (!right.contains(it.key())) {
                continue;
            }
            ++compared;
            // The legs differ by the one renamed argument name and nothing
            // else: the same arguments in order, kinds and values alike.
            const QJsonArray a = rightArgs.value(it.key());
            const QJsonArray b = wrongArgs.value(it.key());
            int renamedCount = 0;
            bool otherwiseAlike = a.size() == b.size();
            for (int k = 0; otherwiseAlike && k < a.size(); ++k) {
                QJsonObject x = a.at(k).toObject();
                QJsonObject y = b.at(k).toObject();
                if (x.value(QStringLiteral("name")) != y.value(QStringLiteral("name"))) {
                    ++renamedCount;
                    otherwiseAlike = y.value(QStringLiteral("name")).toString()
                        == QStringLiteral("conformanceWrongName");
                    x.remove(QStringLiteral("name"));
                    y.remove(QStringLiteral("name"));
                }
                otherwiseAlike = otherwiseAlike && x == y;
            }
            if (!otherwiseAlike || renamedCount != 1) {
                same.append(QStringLiteral("%1 %2: the legs differ by more than one renamed "
                                           "argument name")
                                .arg(entry.id, it.key()));
            }
            if (right.value(it.key()) == it.value()) {
                same.append(QStringLiteral("%1 %2: both legs answered \"%3\"")
                                .arg(entry.id, it.key(), it.value()));
            }
        }
    }
    QVERIFY2(compared >= 25, qPrintable(QString::number(compared)));
    QVERIFY2(same.isEmpty(), qPrintable(same.join(QLatin1Char('\n'))));

    // nnr.applyModelSelection names the selection it applies by the
    // revision the snapshot gave (dspAssets' selectionRevision), as an app
    // does; any other revision is refused before the arguments matter.
    const QJsonArray steps =
        fixture(QStringLiteral("session-verbs-nnr")).value(QStringLiteral("steps")).toArray();
    double snapshotRevision = -1.0;
    double invokedRevision = -2.0;
    for (const QJsonValue& value : steps) {
        const QJsonObject step = value.toObject();
        const QJsonObject message = step.value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("object.create")
            && message.value(QStringLiteral("key")).toString() == QStringLiteral("dspAssets")) {
            for (const QJsonValue& p : message.value(QStringLiteral("properties")).toArray()) {
                if (p.toObject().value(QStringLiteral("name")).toString()
                    == QStringLiteral("selectionRevision")) {
                    snapshotRevision = p.toObject().value(QStringLiteral("value")).toDouble();
                }
            }
        }
        if (step.value(QStringLiteral("role")).toString() == QStringLiteral("behaviour")
            && message.value(QStringLiteral("verb")).toString()
                   == QStringLiteral("nnr.applyModelSelection")) {
            invokedRevision = message.value(QStringLiteral("args")).toArray().at(0).toObject()
                                  .value(QStringLiteral("value")).toDouble();
        }
    }
    QCOMPARE(invokedRevision, snapshotRevision);
}

void TstLinkConformanceSession::everyFixtureRunsOnTheStation()
{
    // The station's runner plays every fixture (section 16.3), and each
    // fixture's shape is the format's.
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("session"))) {
        const QJsonObject o = fixture(entry.id);
        const QString problem = LinkFixtures::checkSessionFormat(o);
        QVERIFY2(problem.isEmpty(), qPrintable(entry.id + QStringLiteral(": ") + problem));
        QVERIFY2(LinkFixtures::runsOn(o, QStringLiteral("station")), qPrintable(entry.id));
    }
}

void TstLinkConformanceSession::appFixturesHoldOnlyWhatAConformantClientSends()
{
    QString error;
    const QJsonObject surface = LinkFixtures::readObject(
        QDir(LinkFixtures::dataDirectory()).filePath(QStringLiteral("surface.json")), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QStringList problems;
    int forTheApp = 0;
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("session"))) {
        const QJsonObject o = fixture(entry.id);
        if (!LinkFixtures::runsOn(o, QStringLiteral("app"))) {
            continue;
        }
        ++forTheApp;
        problems.append(appConformanceProblems(entry.id, o, surface));
    }
    QVERIFY2(forTheApp >= 20, qPrintable(QString::number(forTheApp)));
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TstLinkConformanceSession::theConformanceCheckCatchesWhatAnAppCannotSend()
{
    QString error;
    const QJsonObject surface = LinkFixtures::readObject(
        QDir(LinkFixtures::dataDirectory()).filePath(QStringLiteral("surface.json")), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QJsonObject base = fixture(QStringLiteral("session-property-write"));
    QVERIFY(!base.isEmpty());
    QVERIFY(appConformanceProblems(QStringLiteral("base"), base, surface).isEmpty());

    // Each alteration an app could not produce is named.
    const auto altered = [&base](const std::function<void(QJsonObject&)>& change, int* at) {
        QJsonObject o = base;
        QJsonArray steps = o.value(QStringLiteral("steps")).toArray();
        for (int i = 0; i < steps.size(); ++i) {
            QJsonObject step = steps.at(i).toObject();
            if (step.value(QStringLiteral("role")).toString() != QStringLiteral("behaviour")) {
                continue;
            }
            QJsonObject message = step.value(QStringLiteral("message")).toObject();
            const QJsonObject before = message;
            change(message);
            if (message != before) {
                step.insert(QStringLiteral("message"), message);
                steps.replace(i, step);
                *at = i;
                break;
            }
        }
        o.insert(QStringLiteral("steps"), steps);
        return o;
    };
    int at = -1;
    // An older app's hello, without majors or features.
    QStringList p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("hello")) {
            m.remove(QStringLiteral("majors"));
            m.remove(QStringLiteral("features"));
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("majors")), qPrintable(p.join('|')));
    // A pinned list of majors.
    p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("hello")) {
            m.insert(QStringLiteral("majors"), QJsonArray{1});
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("$majors")), qPrintable(p.join('|')));
    // A pinned peer name.
    p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("hello")) {
            m.insert(QStringLiteral("peer"), QStringLiteral("NereusSDR iPhone"));
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("peer")), qPrintable(p.join('|')));
    // A literal write id.
    p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("property.write")) {
            m.insert(QStringLiteral("writeId"), 2);
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("writeId")), qPrintable(p.join('|')));
    // A write to a property the Core sets itself.
    p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("property.write")) {
            m.insert(QStringLiteral("properties"),
                     QJsonArray{QJsonObject{{QStringLiteral("ordinal"), 15},
                                            {QStringLiteral("name"), QStringLiteral("signalStrengthDbm")},
                                            {QStringLiteral("kind"), QStringLiteral("f64")},
                                            {QStringLiteral("value"), -50.0}}});
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("signalStrengthDbm")),
             qPrintable(p.join('|')));
    // A message kind a client never sends, as behaviour.
    p = appConformanceProblems(QStringLiteral("x"), altered([](QJsonObject& m) {
        if (m.value(QStringLiteral("type")).toString() == QStringLiteral("auth.request")) {
            m = QJsonObject{{QStringLiteral("type"), QStringLiteral("conformance.unknown")}};
        }
    }, &at), surface);
    QVERIFY2(p.join(QLatin1Char('|')).contains(QStringLiteral("never sends")),
             qPrintable(p.join('|')));
    // The rest of the rules, each planted once on a copy of a fixture:
    // step `index` of `id`, changed by `change`, must be named by `expect`.
    const auto planted = [&surface, this](const QString& id, int index,
                                          const std::function<void(QJsonObject&)>& change) {
        QJsonObject o = fixture(id);
        QJsonArray steps = o.value(QStringLiteral("steps")).toArray();
        QJsonObject step = steps.at(index).toObject();
        change(step);
        steps.replace(index, step);
        o.insert(QStringLiteral("steps"), steps);
        return appConformanceProblems(id, o, surface).join(QLatin1Char('|'));
    };
    const auto setIn = [](QJsonObject& step, const QString& key, const QJsonValue& value) {
        QJsonObject message = step.value(QStringLiteral("message")).toObject();
        message.insert(key, value);
        step.insert(QStringLiteral("message"), message);
    };
    const QString write = QStringLiteral("session-property-write");
    const QString ps3 = QStringLiteral("session-verbs-ps3");
    // A scripted hello or token in a fixture for the app.
    QString found = planted(write, 1, [](QJsonObject& step) {
        step.insert(QStringLiteral("role"), QStringLiteral("scripted"));
    });
    QVERIFY2(found.contains(QStringLiteral("cannot be scripted")), qPrintable(found));
    found = planted(write, 2, [](QJsonObject& step) {
        step.insert(QStringLiteral("role"), QStringLiteral("scripted"));
    });
    QVERIFY2(found.contains(QStringLiteral("own auth.request")), qPrintable(found));
    // Station messages an app's runner cannot fill: $capture, a stray
    // $any, a named $int.
    found = planted(write, 3, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("reason"), QStringLiteral("$capture:why"));
    });
    QVERIFY2(found.contains(QStringLiteral("$capture:why")), qPrintable(found));
    found = planted(write, 3, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("accepted"), QStringLiteral("$any"));
    });
    QVERIFY2(found.contains(QStringLiteral("$.accepted is $any")), qPrintable(found));
    found = planted(write, 3, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("retryable"), QStringLiteral("$int:n"));
    });
    QVERIFY2(found.contains(QStringLiteral("$int:n")), qPrintable(found));
    // A verb with arguments it does not take, and one not advertised
    // (PureSignal's gate is psAlgorithmVersion equal to 3; 4 fails it).
    found = planted(ps3, 36, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("args"),
              QJsonArray{QJsonObject{{QStringLiteral("ordinal"), 0},
                                     {QStringLiteral("name"), QStringLiteral("enabled")},
                                     {QStringLiteral("kind"), QStringLiteral("i64")},
                                     {QStringLiteral("value"), 0}}});
    });
    QVERIFY2(found.contains(QStringLiteral("ps3.twoTone's arguments")), qPrintable(found));
    found = planted(ps3, 4, [](QJsonObject& step) {
        QJsonObject message = step.value(QStringLiteral("message")).toObject();
        QJsonArray properties = message.value(QStringLiteral("properties")).toArray();
        for (int i = 0; i < properties.size(); ++i) {
            QJsonObject entry = properties.at(i).toObject();
            if (entry.value(QStringLiteral("name")).toString() == QStringLiteral("psAlgorithmVersion")) {
                entry.insert(QStringLiteral("value"), 4);
                properties.replace(i, entry);
            }
        }
        message.insert(QStringLiteral("properties"), properties);
        step.insert(QStringLiteral("message"), message);
    });
    QVERIFY2(found.contains(QStringLiteral("ps3.off was not advertised")), qPrintable(found));
    // A placeholder among a behaviour step's arguments.
    found = planted(ps3, 43, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("args"),
              QJsonArray{QJsonObject{{QStringLiteral("ordinal"), 0},
                                     {QStringLiteral("name"), QStringLiteral("label")},
                                     {QStringLiteral("kind"), QStringLiteral("utf8")},
                                     {QStringLiteral("value"), QStringLiteral("$string")}}});
    });
    QVERIFY2(found.contains(QStringLiteral("never placeholders")), qPrintable(found));
    // An id without the link's range, or from 0 where 1 is the least.
    found = planted(ps3, 23, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("id"), QStringLiteral("$int:invoke23"));
    });
    QVERIFY2(found.contains(QStringLiteral(":1:4294967295")), qPrintable(found));
    found = planted(ps3, 23, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("id"), QStringLiteral("$int:invoke23:0:4294967295"));
    });
    QVERIFY2(found.contains(QStringLiteral(":1:4294967295")), qPrintable(found));
    // A scripted id below 1000, and a scripted message naming a value.
    found = planted(ps3, 40, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("id"), 167);
    });
    QVERIFY2(found.contains(QStringLiteral("from 1000 up")), qPrintable(found));
    found = planted(ps3, 40, [&setIn](QJsonObject& step) {
        setIn(step, QStringLiteral("id"), QStringLiteral("$int:scripted"));
    });
    QVERIFY2(found.contains(QStringLiteral("a scripted message holds $int:scripted")),
             qPrintable(found));
}

void TstLinkConformanceSession::refusalsOfOutboundWritesArePlain()
{
    // The property-write fixture pins the station's two refusals of an
    // outbound write, and an operator may read both.
    const QJsonObject o = fixture(QStringLiteral("session-property-write"));
    QVERIFY(!o.isEmpty());
    QSet<QString> reasons;
    for (const QJsonValue& step : o.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject message = step.toObject().value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("type")).toString() != QStringLiteral("property.result")) {
            continue;
        }
        for (const QJsonValue& result : message.value(QStringLiteral("results")).toArray()) {
            const QJsonObject r = result.toObject();
            const QString property = r.value(QStringLiteral("property")).toString();
            if (property == QStringLiteral("active")
                || property == QStringLiteral("signalStrengthDbm")) {
                reasons.insert(property + QLatin1Char('=')
                               + r.value(QStringLiteral("reason")).toString());
            }
        }
    }
    const QString active = SliceModel::activeWriteReason();
    QVERIFY2(reasons.contains(QStringLiteral("active=") + active), qPrintable(active));
    QVERIFY2(OperatorWording::isPlain(active), qPrintable(active));
    bool signal = false;
    for (const QString& entry : reasons) {
        if (entry.startsWith(QStringLiteral("signalStrengthDbm="))) {
            signal = true;
            const QString reason = entry.section(QLatin1Char('='), 1);
            QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
        }
    }
    QVERIFY(signal);
}

void TstLinkConformanceSession::alteredFixturesFailReadably()
{
    // A number the DSP measures matches within its stated tolerance only.
    LinkFixtures::Captures none;
    QVERIFY(LinkFixtures::match(QStringLiteral("$within:1:-399.02"), -399.5, &none).isEmpty());
    const QString outside = LinkFixtures::match(QStringLiteral("$within:1:-399.02"), -401.0, &none);
    QVERIFY2(outside.contains(QStringLiteral("within 1 of -399.02")), qPrintable(outside));
    QVERIFY(!LinkFixtures::match(QStringLiteral("$within:1:-399.02"), QStringLiteral("x"), &none)
                 .isEmpty());
    // Both numbers are JSON numbers: forms a number parser might take but
    // JSON does not ("+1", "inf", ".5", "1.", "0x10", a space) are refused.
    for (const char* bad : {"$within:+1:5", "$within:1:inf", "$within:.5:5", "$within:1.:5",
                            "$within:1:0x10", "$within: 1:5", "$within:1:-nan"}) {
        const QString refused = LinkFixtures::match(QString::fromLatin1(bad), 5.0, &none);
        QVERIFY2(refused.contains(QStringLiteral("is not $within")), bad);
    }
    QVERIFY(LinkFixtures::match(QStringLiteral("$within:1e-1:5.05"), 5.0, &none).isEmpty());
    // "$majors": an app supporting [1, 2] and choosing 1 passes; a list out
    // of order, or one without the chosen major, does not.
    const QJsonObject hello{{QStringLiteral("major"), 1},
                            {QStringLiteral("majors"), QStringLiteral("$majors")}};
    const auto withMajors = [](const QJsonArray& majors) {
        return QJsonObject{{QStringLiteral("major"), 1}, {QStringLiteral("majors"), majors}};
    };
    QVERIFY(LinkFixtures::match(hello, withMajors({1, 2}), &none).isEmpty());
    QVERIFY(LinkFixtures::match(hello, withMajors({1}), &none).isEmpty());
    QVERIFY(!LinkFixtures::match(hello, withMajors({2, 1}), &none).isEmpty());
    QVERIFY(!LinkFixtures::match(hello, withMajors({2, 3}), &none).isEmpty());
    QVERIFY(!LinkFixtures::match(hello, withMajors({}), &none).isEmpty());
    // connect-connectable's signal readings never admit the meter pump's
    // no-reading value: a reading of -400 fails the fixture.
    int readings = 0;
    for (const QJsonValue& step :
         fixture(QStringLiteral("session-connect-connectable")).value(QStringLiteral("steps")).toArray()) {
        const QJsonObject message = step.toObject().value(QStringLiteral("message")).toObject();
        if (!message.value(QStringLiteral("properties")).isArray()) {
            continue;
        }
        for (const QJsonValue& p : message.value(QStringLiteral("properties")).toArray()) {
            const QJsonValue value = p.toObject().value(QStringLiteral("value"));
            if (value.toString().startsWith(QStringLiteral("$within:"))) {
                ++readings;
                QVERIFY2(!LinkFixtures::match(value, SliceMeterPump::kNoReadingDbm, &none).isEmpty(),
                         qPrintable(value.toString()));
            }
        }
    }
    QCOMPARE(readings, 3);

    // A value the station sends, changed: the failure names the step, the
    // path inside the message and what the station really sent.
    QJsonObject changed = fixture(QStringLiteral("session-wrong-token"));
    QVERIFY(!changed.isEmpty());
    QJsonArray steps = changed.value(QStringLiteral("steps")).toArray();
    int index = -1;
    for (int i = 0; i < steps.size(); ++i) {
        const QJsonObject message = steps.at(i).toObject().value(QStringLiteral("message")).toObject();
        if (message.value(QStringLiteral("type")).toString() == QStringLiteral("auth.result")) {
            index = i;
        }
    }
    QVERIFY(index >= 0);
    QJsonObject step = steps.at(index).toObject();
    QJsonObject message = step.value(QStringLiteral("message")).toObject();
    message.insert(QStringLiteral("reason"), QStringLiteral("Something else"));
    step.insert(QStringLiteral("message"), message);
    steps.replace(index, step);
    changed.insert(QStringLiteral("steps"), steps);
    QString failure = run(QStringLiteral("altered-wrong-token"), changed);
    QVERIFY2(failure.contains(QStringLiteral("step %1").arg(index))
                 && failure.contains(QStringLiteral("$.reason"))
                 && failure.contains(QStringLiteral("the station sent")),
             qPrintable(failure));

    // Too little virtual time: the connect deadline has not come, so the
    // station is still waiting and never sends what the fixture expects.
    QJsonObject early = fixture(QStringLiteral("session-connect-deadline"));
    QVERIFY(!early.isEmpty());
    steps = early.value(QStringLiteral("steps")).toArray();
    for (int i = 0; i < steps.size(); ++i) {
        if (steps.at(i).toObject().contains(QStringLiteral("advanceMs"))) {
            steps.replace(i, QJsonObject{{QStringLiteral("advanceMs"), 29000}});
        }
    }
    early.insert(QStringLiteral("steps"), steps);
    failure = run(QStringLiteral("altered-connect-deadline"), early);
    QVERIFY2(failure.contains(QStringLiteral("the station sent nothing more")),
             qPrintable(failure));
}

QTEST_MAIN(TstLinkConformanceSession)
#include "tst_link_conformance_session.moc"
