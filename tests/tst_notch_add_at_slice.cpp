// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_notch_add_at_slice.cpp  (NereusSDR)
// =================================================================
//
// R-IOS-27, R-IOS-06: notch.addAtSlice, the desktop's +TNF for a device.
// The Core composes the centre and width the desktop's +TNF button does
// (RadioModel::addTnfForSlice, which MainWindow::onAddTnfClicked calls) on
// its own slice and adds through notch.add's path, so the add rules and
// refusals are notch.add's. Gated by notchControlVersion 2.
//
// Loopback link, no RF and no hardware: nothing here keys a radio and no
// audio device is opened.
//
//   cmake --build build --target tst_notch_add_at_slice
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build -R '^tst_notch_add_at_slice$' \
//       --output-on-failure
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-25  J.J. Boyd / KG4VCF  R-IOS-27, R-IOS-06: created.
//                                    AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <memory>

#include "core/AppSettings.h"
#include "core/ConnectionState.h"
#include "core/HardwareProfile.h"
#include "core/session/SessionCommandDispatcher.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "fakes/LoopbackTransport.h"
#include "fakes/UpgradedCoreToken.h"
#include "models/NotchModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include "OperatorWording.h"

using namespace NereusSDR;
using NereusSDR::Test::LoopbackTransport;

namespace {

const QString kMac = QStringLiteral("AA:BB:CC:DD:EE:01");
const QString kNotUnderstood = QStringLiteral("This notch change is not one this Core understands.");
const QString kNoReceiver = QStringLiteral("That receiver is not on this Core");
const QString kOlderApp = QStringLiteral("Update this app to change notches on this Core.");

std::unique_ptr<RadioModel> makeStationRadioModel()
{
    auto model = std::make_unique<RadioModel>();
    model->setBoardForTest(HPSDRHW::HermesLite);
    model->setHpsdrModelForTest(HPSDRModel::HERMESLITE);
    RadioInfo info;
    info.macAddress = kMac;
    info.name = QStringLiteral("Bench HL2");
    info.boardType = HPSDRHW::HermesLite;
    model->setLastRadioInfoForTest(info);
    model->setConnectionStateForTest(ConnectionState::Connected);
    model->addSlice(QStringLiteral("pan-0"));
    return model;
}

// A receive-only Core and one window, handshake complete.
struct Session {
    explicit Session(const QString& securityDir, QObject* parent)
        : settings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")))
    {
        core = makeStationRadioModel();
        server = std::make_unique<StationServer>(
            core.get(), settings, NereusSDR::Test::seedUpgradedCoreToken(securityDir));
        client = std::make_unique<StationClient>(&window, &proxy);
        coreEnd = new LoopbackTransport(QStringLiteral("station-end"), parent);
        windowEnd = new LoopbackTransport(QStringLiteral("client-end"), parent);
        coreEnd->linkTo(windowEnd);
    }
    bool connect()
    {
        QSignalSpy completed(client.get(), &StationClient::handshakeComplete);
        client->startSession(windowEnd, server->token());
        server->acceptTransport(coreEnd);
        return completed.wait(5000) || completed.count() == 1;
    }
    SessionMessage invoke(const QByteArray& verb, const QList<MirrorUpdate>& arguments)
    {
        const quint32 id = ++nextId;
        windowEnd->sendText(SessionMessages::encode(
            SessionMessages::commandInvoke(verb, id, arguments)));
        SessionMessage found;
        found.reason = QStringLiteral("no command.result arrived");
        (void)QTest::qWaitFor([&] {
            for (const QByteArray& wire : windowEnd->received()) {
                SessionMessage message;
                if (SessionMessages::decode(wire, &message)
                    && message.kind == SessionMessageKind::CommandResult
                    && message.commandId == id) {
                    found = message;
                    return true;
                }
            }
            return false;
        }, 3000);
        return found;
    }
    SessionMessage addAtSlice(int sliceId)
    {
        return invoke("notch.addAtSlice",
                      {MirrorUpdate{0, "sliceId", MirrorWireKind::Int64,
                                    QVariant(qlonglong(sliceId))}});
    }
    SessionMessage add(int sliceId, double centreHz, double widthHz)
    {
        return invoke("notch.add",
                      {MirrorUpdate{0, "sliceId", MirrorWireKind::Int64, QVariant(qlonglong(sliceId))},
                       MirrorUpdate{0, "centreHz", MirrorWireKind::Float64, QVariant(centreHz)},
                       MirrorUpdate{0, "widthHz", MirrorWireKind::Float64, QVariant(widthHz)}});
    }
    SliceModel* slice() const { return core->slices().first(); }
    NotchModel* notches() const { return core->notchModel(); }

    QTemporaryDir settingsDir;
    AppSettings settings;
    std::unique_ptr<RadioModel> core;
    std::unique_ptr<StationServer> server;
    RadioModel window{RadioModel::Role::Remote};
    SettingsProxy proxy;
    std::unique_ptr<StationClient> client;
    LoopbackTransport* coreEnd = nullptr;
    LoopbackTransport* windowEnd = nullptr;
    quint32 nextId = 96000;
};

int addedIdOf(const SessionMessage& result)
{
    for (const MirrorUpdate& value : result.updates) {
        if (value.name == QByteArrayLiteral("id")) {
            return value.value.toInt();
        }
    }
    return -1;
}

}  // namespace

class TstNotchAddAtSlice : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();

    void theVerbIsDeclaredWithItsCapability();
    void coreOffersNotchControlVersion2();
    void landsTheDesktopsTnfNotch_data();
    void landsTheDesktopsTnfNotch();
    void anUnknownReceiverIsRefused();
    void aRepeatInsideTheDedupeWindowIsRefusedAsTheDesktopRefusesIt();
    void aBusySettingsPageIsRefusedAsNotchAddRefusesIt();
    void wrongArgumentsAreNotUnderstood();
    void anOlderAppIsToldToUpdate();
    void refusalsArePlainWords();

private:
    QTemporaryDir m_securityDir;
};

void TstNotchAddAtSlice::initTestCase()
{
    QVERIFY(m_securityDir.isValid());
    const QString profile = QStringLiteral("notch-add-at-slice-%1")
                                .arg(QCoreApplication::applicationPid());
    AppSettings::setProfileOverride(profile);
}

void TstNotchAddAtSlice::init()
{
    AppSettings::instance().clear();
    AppSettings::instance().setValue(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("7"));
}

void TstNotchAddAtSlice::cleanupTestCase()
{
    const QString path = AppSettings::instance().filePath();
    QFile::remove(path);
    QFile::remove(path + QStringLiteral(".bak"));
}

void TstNotchAddAtSlice::theVerbIsDeclaredWithItsCapability()
{
    bool found = false;
    for (const CommandVerbSpec& spec : SessionCommandDispatcher::verbSpecs()) {
        if (spec.verb == QByteArrayLiteral("notch.addAtSlice")) {
            found = true;
            QCOMPARE(spec.arguments.size(), 1);
            QCOMPARE(spec.arguments.at(0).name, QByteArrayLiteral("sliceId"));
            QCOMPARE(spec.arguments.at(0).kind, MirrorWireKind::Int64);
            QVERIFY(!spec.arguments.at(0).optional);
            QCOMPARE(spec.capability, QByteArrayLiteral("notchControlVersion"));
            QCOMPARE(spec.capabilityVersion, 2);
            QCOMPARE(int(spec.minMinor), int(kDspControlSessionProtocolMinor));
        } else if (spec.verb.startsWith("notch.")) {
            // notch.add, notch.move, notch.setActive and notch.delete stay at 1.
            QCOMPARE(spec.capability, QByteArrayLiteral("notchControlVersion"));
            QCOMPARE(spec.capabilityVersion, 1);
        }
    }
    QVERIFY2(found, "notch.addAtSlice is not a verb");
}

void TstNotchAddAtSlice::coreOffersNotchControlVersion2()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    QCOMPARE(s.server->buildCapabilities().notchControlVersion, 2);
    QCOMPARE(s.client->capabilities().notchControlVersion, 2);
}

void TstNotchAddAtSlice::landsTheDesktopsTnfNotch_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<int>("filterLow");
    QTest::addColumn<int>("filterHigh");
    QTest::newRow("USB") << static_cast<int>(DSPMode::USB) << 100 << 2900;
    QTest::newRow("LSB") << static_cast<int>(DSPMode::LSB) << -2900 << -100;
    QTest::newRow("DIGU") << static_cast<int>(DSPMode::DIGU) << 200 << 2800;
    QTest::newRow("DIGL") << static_cast<int>(DSPMode::DIGL) << -2800 << -200;
}

// The verb and the desktop's +TNF, driven on the same Core and slice, land
// the same notch; and that notch is TNFAdd's centre from the slice's
// demodulated frequency (the DIG click-tune offset included) and filter.
void TstNotchAddAtSlice::landsTheDesktopsTnfNotch()
{
    QFETCH(int, mode);
    QFETCH(int, filterLow);
    QFETCH(int, filterHigh);
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    SliceModel* const slice = s.slice();
    slice->setFrequency(14100000.0);
    slice->setDspMode(static_cast<DSPMode>(mode));
    slice->setFilter(filterLow, filterHigh);
    slice->setDiguOffsetHz(1500);
    slice->setDiglOffsetHz(2210);

    // The desktop's +TNF.
    const int desktopId = s.core->addTnfForSlice(slice);
    QVERIFY(desktopId >= 0);
    const Notch desktop = *s.notches()->notchById(desktopId);
    s.notches()->clear();
    QCOMPARE(s.notches()->notches().size(), 0);

    // The device's.
    const SessionMessage result = s.addAtSlice(slice->sliceIndex());
    QVERIFY2(result.accepted, qPrintable(result.reason));
    QVERIFY(result.reason.isEmpty());
    QCOMPARE(result.affectedKeys, (QList<QByteArray>{QByteArrayLiteral("notches")}));
    const int verbId = addedIdOf(result);
    QVERIFY(verbId >= 0);
    QCOMPARE(s.notches()->notches().size(), 1);
    const Notch byVerb = *s.notches()->notchById(verbId);

    QCOMPARE(byVerb.centerHz, desktop.centerHz);
    QCOMPARE(byVerb.widthHz, desktop.widthHz);

    // Independently of the shared code: TNFAdd's centre and width.
    const int middle = filterLow + (filterHigh - filterLow) / 2;
    double demodulated = 14100000.0;
    if (static_cast<DSPMode>(mode) == DSPMode::DIGU) { demodulated += 1500.0; }
    if (static_cast<DSPMode>(mode) == DSPMode::DIGL) { demodulated += 2210.0; }
    QCOMPARE(byVerb.centerHz, demodulated + middle);
    QCOMPARE(byVerb.widthHz, NotchModel::kDefaultNotchWidthHz);
}

void TstNotchAddAtSlice::anUnknownReceiverIsRefused()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    const SessionMessage result = s.addAtSlice(99);
    QVERIFY(!result.accepted);
    QCOMPARE(result.reason, kNoReceiver);
    // notch.add says the same for the same receiver.
    QCOMPARE(s.add(99, 14100000.0, 200.0).reason, kNoReceiver);
    QCOMPARE(s.notches()->notches().size(), 0);
}

// A second +TNF on the same signal lands inside the 10 Hz dedupe window
// (console.cs:40259-40260 [v2.10.3.15]); the desktop's button is refused
// and so is the verb, with the model's own reason.
void TstNotchAddAtSlice::aRepeatInsideTheDedupeWindowIsRefusedAsTheDesktopRefusesIt()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    SliceModel* const slice = s.slice();
    slice->setFrequency(14100000.0);
    slice->setDspMode(DSPMode::USB);
    slice->setFilter(100, 2900);

    const SessionMessage first = s.addAtSlice(slice->sliceIndex());
    QVERIFY2(first.accepted, qPrintable(first.reason));

    QSignalSpy rejected(s.notches(), &NotchModel::notchAddRejected);
    QCOMPARE(s.core->addTnfForSlice(slice), -1);
    QCOMPARE(rejected.count(), 1);
    const QString desktopReason = rejected.at(0).at(0).toString();

    const SessionMessage repeat = s.addAtSlice(slice->sliceIndex());
    QVERIFY(!repeat.accepted);
    QCOMPARE(repeat.reason, desktopReason);
    QCOMPARE(repeat.reason, QStringLiteral("A notch already exists within 10 Hz"));
    QVERIFY(repeat.affectedKeys.isEmpty());
    QCOMPARE(s.notches()->notches().size(), 1);
}

void TstNotchAddAtSlice::aBusySettingsPageIsRefusedAsNotchAddRefusesIt()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    SliceModel* const slice = s.slice();
    slice->setFrequency(14100000.0);
    s.notches()->setAdminBusy(true);
    const SessionMessage viaAdd = s.add(slice->sliceIndex(), 14101000.0, 200.0);
    const SessionMessage result = s.addAtSlice(slice->sliceIndex());
    s.notches()->setAdminBusy(false);
    QVERIFY(!viaAdd.accepted);
    QVERIFY(!result.accepted);
    QCOMPARE(result.reason, viaAdd.reason);
    QCOMPARE(s.notches()->notches().size(), 0);
}

void TstNotchAddAtSlice::wrongArgumentsAreNotUnderstood()
{
    Session s(m_securityDir.path(), this);
    QVERIFY(s.connect());
    const qlonglong id = s.slice()->sliceIndex();
    const QList<QList<MirrorUpdate>> wrong{
        // A renamed argument.
        {MirrorUpdate{0, "conformanceWrongName", MirrorWireKind::Int64, QVariant(id)}},
        // None.
        {},
        // One extra.
        {MirrorUpdate{0, "sliceId", MirrorWireKind::Int64, QVariant(id)},
         MirrorUpdate{0, "centreHz", MirrorWireKind::Float64, QVariant(14100000.0)}},
        // The receiver as a real number.
        {MirrorUpdate{0, "sliceId", MirrorWireKind::Float64, QVariant(double(id))}},
        // The receiver as text.
        {MirrorUpdate{0, "sliceId", MirrorWireKind::Utf8, QVariant(QStringLiteral("0"))}},
    };
    for (const QList<MirrorUpdate>& arguments : wrong) {
        const SessionMessage result = s.invoke("notch.addAtSlice", arguments);
        QVERIFY(!result.accepted);
        QCOMPARE(result.reason, kNotUnderstood);
    }
    QCOMPARE(s.notches()->notches().size(), 0);
}

// A peer below the notch verbs' minor is refused before the Core reads it,
// as every notch verb is.
void TstNotchAddAtSlice::anOlderAppIsToldToUpdate()
{
    QTemporaryDir settingsDir;
    AppSettings settings(settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
    std::unique_ptr<RadioModel> core = makeStationRadioModel();
    StationServer server(core.get(), settings,
                         NereusSDR::Test::seedUpgradedCoreToken(m_securityDir.path()));
    auto* app = new LoopbackTransport(QStringLiteral("app"), this);
    auto* station = new LoopbackTransport(QStringLiteral("station"), &server);
    station->linkTo(app);
    server.acceptTransport(station);
    QVERIFY(QTest::qWaitFor([app]() { return !app->received().isEmpty(); }, 5000));
    app->sendText(SessionMessages::encode(SessionMessages::hello(
        kSessionProtocolMajor, quint16(kDspControlSessionProtocolMinor - 1), 0,
        QStringLiteral("NereusSDR iPhone"))));
    app->sendText(SessionMessages::encode(SessionMessages::authRequest(server.token())));
    QVERIFY(QTest::qWaitFor(
        [app]() { return app->receivedKinds().contains(QByteArrayLiteral("snapshot.complete")); },
        5000));
    app->sendText(SessionMessages::encode(SessionMessages::commandInvoke(
        "notch.addAtSlice", 7101,
        {MirrorUpdate{0, "sliceId", MirrorWireKind::Int64, QVariant(qlonglong(0))}})));
    SessionMessage found;
    QVERIFY(QTest::qWaitFor([&] {
        for (const QByteArray& wire : app->received()) {
            SessionMessage message;
            if (SessionMessages::decode(wire, &message)
                && message.kind == SessionMessageKind::CommandResult && message.commandId == 7101) {
                found = message;
                return true;
            }
        }
        return false;
    }, 3000));
    QVERIFY(!found.accepted);
    QCOMPARE(found.reason, kOlderApp);
    QCOMPARE(core->notchModel()->notches().size(), 0);
}

void TstNotchAddAtSlice::refusalsArePlainWords()
{
    for (const QString& reason :
         {kNotUnderstood, kNoReceiver, kOlderApp,
          QStringLiteral("A notch already exists within 10 Hz")}) {
        QVERIFY2(OperatorWording::isPlain(reason), qPrintable(reason));
        QVERIFY2(OperatorWording::coreCalledStationIn(reason).isEmpty(), qPrintable(reason));
    }
}

QTEST_MAIN(TstNotchAddAtSlice)
#include "tst_notch_add_at_slice.moc"
