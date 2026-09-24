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

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/StepAttenuatorController.h"
#include "core/dsp/DspAssetService.h"
#include "core/meters/SliceMeterPump.h"
#include "core/security/TokenStore.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationServer.h"
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
    std::unique_ptr<StationServer> server;

    ~Station()
    {
        server.reset();
        if (model != nullptr && stepAtt) {
            model->setStepAttController(nullptr);
        }
    }
};

QString buildStation(const QJsonObject& setup, Station* station)
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
    station->server =
        std::make_unique<StationServer>(station->model, *station->settings, station->dir.path());
    if (setup.value(QStringLiteral("media")).toBool(false)) {
        station->server->setMediaEnabled(true);
    }

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
    void refusalsOfOutboundWritesArePlain();
    void alteredFixturesFailReadably();

private:
    QString run(const QString& id, const QJsonObject& fixture);
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

QString TstLinkConformanceSession::run(const QString& id, const QJsonObject& fixture)
{
    Station station;
    const QString problem =
        buildStation(fixture.value(QStringLiteral("stationSetup")).toObject(), &station);
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
    const QList<LinkFixtures::Entry> entries =
        LinkFixtures::entries(m_manifest, QStringLiteral("session"));
    QVERIFY(!entries.isEmpty());
    for (const LinkFixtures::Entry& entry : entries) {
        QTest::newRow(qPrintable(entry.id)) << entry.id << entry.file;
    }
}

void TstLinkConformanceSession::sessionFixtures()
{
    QFETCH(QString, id);
    QFETCH(QString, file);
    QString error;
    const QJsonObject o =
        LinkFixtures::readObject(QDir(LinkFixtures::dataDirectory()).filePath(file), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    const QString failure = run(id, o);
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
                for (const CommandArgumentSpec& argument : spec.arguments) {
                    declared.append(QString::fromUtf8(argument.name));
                }
                bool unknownName = false;
                for (const QString& name : names) {
                    unknownName = unknownName || !declared.contains(name);
                }
                if (unknownName) {
                    wrong.insert(verb);
                } else if (names == declared) {
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
