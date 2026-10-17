// no-port-check: NereusSDR-original tests of the radio's second firmware
// number over the station link (firmwareBeta 1).
//
// A radio's firmware release is two numbers (22 and 14 read "22.14"), and
// the link's firmwareVersion is the first alone. These tests pin the entry
// that carries the second: its place and form on the wire, that a Core sends
// it only to an app whose hello declared it, and that a remote window words
// the Core's radio as a window running that radio itself does.
#include "MultiDeviceHarness.h"

#include <QLabel>

#include "core/security/ClientDeviceIdentity.h"
#include "core/session/StationClient.h"
#include "core/settings/SettingsProxy.h"
#include "gui/diagnostics/RadioStatusPage.h"

namespace {
// Setup > Diagnostics > Radio Status' Firmware readout, by its setup id.
QString firmwareReadout(const QWidget& page)
{
    for (const QLabel* label : page.findChildren<QLabel*>()) {
        if (label->property("nereusSetupId").toString()
            == QStringLiteral("diagnostics.radioStatus.firmware")) {
            return label->text();
        }
    }
    return QStringLiteral("(no firmware readout)");
}

QList<MirrorUpdate> lastCapabilityEntries(const LoopbackTransport* app)
{
    QList<MirrorUpdate> result;
    for (const QByteArray& wire : app->received()) {
        SessionMessage message;
        if (SessionMessages::decode(wire, &message)
            && message.kind == SessionMessageKind::Capabilities) {
            result = message.updates;
        }
    }
    return result;
}

QList<QByteArray> namesOf(const QList<MirrorUpdate>& entries)
{
    QList<QByteArray> names;
    for (const MirrorUpdate& entry : entries) {
        names.append(entry.name);
    }
    return names;
}

MirrorUpdate betaEntry(const QVariant& value, MirrorWireKind kind = MirrorWireKind::Int64)
{
    return {0, QByteArrayLiteral("firmwareBeta"), kind, value};
}

// The Core's radio as a successful connect leaves it: the stored record and
// the version string, which is the record's first number.
void giveRadio(Core& core, int first, int second, const QString& version)
{
    RadioInfo info = core.model->currentRadioInfo();
    info.firmwareVersion = first;
    info.firmwareBetaVersion = second;
    core.model->setLastRadioInfoForTest(info);
    core.model->setVersionForTest(version);
}

StationCapabilities radioCaps(HPSDRHW board, const QString& first)
{
    StationCapabilities caps;
    caps.stationName = QStringLiteral("Bench");
    caps.macAddress = QStringLiteral("AA:BB:CC:DD:EE:14");
    caps.board = board;
    caps.firmwareVersion = first;
    return caps;
}
} // namespace

class TstFirmwareBetaLink : public QObject {
    Q_OBJECT
private slots:
    void entrySitsAfterThePictureVersionAndBeforeTheBuildInfo();
    void entryIsSentOnlyWithItsFlag();
    void badEntriesReadAsAbsent();
    void coreSendsTheNumberOnlyToAnAppThatDeclaresIt();
    void coreSendsNoNumberForARecordOfAnotherRadio();
    void remoteWindowWordsTheCoresRadio();
    void radioStatusPageShowsTheCoresRelease();
    void openRadioStatusPageFollowsFirmwareChanges();
    void windowOnACoreReadsBothNumbers();
};

void TstFirmwareBetaLink::entrySitsAfterThePictureVersionAndBeforeTheBuildInfo()
{
    StationCapabilities caps = radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22"));
    caps.radioIdentityEntries = true;
    caps.spectrumPictureVersion = 1;
    caps.firmwareBetaEntry = true;
    caps.firmwareBetaVersion = 14;
    caps.coreBuildInfo = CoreBuildInfo{QStringLiteral("2026.10.0"), QStringLiteral("test@1")};

    const QList<MirrorUpdate> entries = caps.toUpdates();
    const QList<QByteArray> names = namesOf(entries);
    const qsizetype at = names.indexOf(QByteArrayLiteral("firmwareBeta"));
    QVERIFY(at >= 0);
    QCOMPARE(names.count(QByteArrayLiteral("firmwareBeta")), 1);
    QCOMPARE(names.at(at - 1), QByteArrayLiteral("spectrumPictureVersion"));
    QCOMPARE(names.at(at + 1), QByteArrayLiteral("coreBuildInfo"));
    QCOMPARE(names.last(), QByteArrayLiteral("coreBuildInfo"));
    QCOMPARE(entries.at(at).ordinal, 0);
    QVERIFY(entries.at(at).kind == MirrorWireKind::Int64);
    QVERIFY(entries.at(at).value.typeId() == QMetaType::LongLong);
    QCOMPARE(entries.at(at).value.toLongLong(), 14LL);

    const StationCapabilities read = StationCapabilities::fromUpdates(entries);
    QVERIFY(read.firmwareBetaEntry);
    QCOMPARE(read.firmwareBetaVersion, 14);
    QCOMPARE(read.firmwareVersion, QStringLiteral("22"));
    QCOMPARE(read.spectrumPictureVersion, 1);
    QVERIFY(read.coreBuildInfo);

    // With no picture version before it, the entry still precedes the build
    // info.
    caps.spectrumPictureVersion = 0;
    const QList<QByteArray> withoutPicture = namesOf(caps.toUpdates());
    QCOMPARE(withoutPicture.at(withoutPicture.size() - 2), QByteArrayLiteral("firmwareBeta"));
    QCOMPARE(withoutPicture.last(), QByteArrayLiteral("coreBuildInfo"));
}

void TstFirmwareBetaLink::entryIsSentOnlyWithItsFlag()
{
    // A number without the flag (a peer that did not declare the feature) is
    // not sent.
    StationCapabilities caps = radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22"));
    caps.radioIdentityEntries = true;
    caps.firmwareBetaVersion = 14;
    QVERIFY(!namesOf(caps.toUpdates()).contains(QByteArrayLiteral("firmwareBeta")));
    const StationCapabilities older = StationCapabilities::fromUpdates(caps.toUpdates());
    QVERIFY(!older.firmwareBetaEntry);
    QCOMPARE(older.firmwareBetaVersion, 0);

    // The flag with no number (a radio that reported none) sends a zero, so a
    // window that had another radio's number drops it.
    caps.firmwareBetaEntry = true;
    caps.firmwareBetaVersion = 0;
    const QList<MirrorUpdate> entries = caps.toUpdates();
    const qsizetype at = namesOf(entries).indexOf(QByteArrayLiteral("firmwareBeta"));
    QVERIFY(at >= 0);
    QCOMPARE(entries.at(at).value.toLongLong(), 0LL);
    const StationCapabilities none = StationCapabilities::fromUpdates(entries);
    QVERIFY(none.firmwareBetaEntry);
    QCOMPARE(none.firmwareBetaVersion, 0);
}

void TstFirmwareBetaLink::badEntriesReadAsAbsent()
{
    const auto read = [](const QList<MirrorUpdate>& entries) {
        return StationCapabilities::fromUpdates(entries);
    };
    // The byte's whole range is taken.
    QVERIFY(read({betaEntry(0LL)}).firmwareBetaEntry);
    QCOMPARE(read({betaEntry(0LL)}).firmwareBetaVersion, 0);
    QCOMPARE(read({betaEntry(1LL)}).firmwareBetaVersion, 1);
    QVERIFY(read({betaEntry(255LL)}).firmwareBetaEntry);
    QCOMPARE(read({betaEntry(255LL)}).firmwareBetaVersion, 255);

    // Outside one byte, the wrong kind, or an ordinal: no entry, no number.
    MirrorUpdate ordinal = betaEntry(14LL);
    ordinal.ordinal = 1;
    const QList<MirrorUpdate> bad{
        betaEntry(256LL), betaEntry(-1LL), betaEntry(70000LL),
        betaEntry(QStringLiteral("14"), MirrorWireKind::Utf8),
        betaEntry(true, MirrorWireKind::Bool),
        betaEntry(QStringLiteral("14")),   // an i64 entry carrying text
        ordinal};
    for (const MirrorUpdate& entry : bad) {
        const StationCapabilities caps = read({entry});
        QVERIFY2(!caps.firmwareBetaEntry, entry.value.toString().toUtf8().constData());
        QCOMPARE(caps.firmwareBetaVersion, 0);
    }

    // A bad entry costs nothing else, and the first of two is the one read.
    const StationCapabilities beside = read(
        {{0, QByteArrayLiteral("firmwareVersion"), MirrorWireKind::Utf8, QStringLiteral("22")},
         betaEntry(256LL)});
    QCOMPARE(beside.firmwareVersion, QStringLiteral("22"));
    QVERIFY(!beside.firmwareBetaEntry);
    const StationCapabilities twice = read({betaEntry(14LL), betaEntry(15LL)});
    QVERIFY(twice.firmwareBetaEntry);
    QCOMPARE(twice.firmwareBetaVersion, 14);
}

void TstFirmwareBetaLink::coreSendsTheNumberOnlyToAnAppThatDeclaresIt()
{
    Core core;
    giveRadio(core, 22, 14, QStringLiteral("22"));
    Device device(QStringLiteral("Desktop"), QStringLiteral("computer"));
    core.pair(device);

    LoopbackTransport* declared = core.signIn(device, {{"deviceAuth", 1}, {"firmwareBeta", 1}});
    QVERIFY(admitted(declared));
    const QList<MirrorUpdate> sent = lastCapabilityEntries(declared);
    QVERIFY(namesOf(sent).contains(QByteArrayLiteral("firmwareBeta")));
    const StationCapabilities caps = StationCapabilities::fromUpdates(sent);
    QCOMPARE(caps.firmwareVersion, QStringLiteral("22"));
    QVERIFY(caps.firmwareBetaEntry);
    QCOMPARE(caps.firmwareBetaVersion, 14);

    // An app built before the entry is sent what it was sent before.
    LoopbackTransport* plain = core.signIn(device, {{"deviceAuth", 1}});
    QVERIFY(admitted(plain));
    const QList<MirrorUpdate> plainSent = lastCapabilityEntries(plain);
    QVERIFY(!plainSent.isEmpty());
    QVERIFY(!namesOf(plainSent).contains(QByteArrayLiteral("firmwareBeta")));
    QCOMPARE(StationCapabilities::fromUpdates(plainSent).firmwareVersion, QStringLiteral("22"));

    // So is one below the minor the newer entries need, whatever it declares.
    LoopbackTransport* old = core.signIn(device, {{"deviceAuth", 1}, {"firmwareBeta", 1}},
                                        quint16(kRadioIdentitySessionProtocolMinor - 1));
    QVERIFY(admitted(old));
    QVERIFY(!lastCapabilityEntries(old).isEmpty());
    QVERIFY(!namesOf(lastCapabilityEntries(old)).contains(QByteArrayLiteral("firmwareBeta")));

    // The number goes as the radio reported it: an ANAN-G2's is its program's
    // version, which each end leaves out of the text.
    giveRadio(core, 27, 39, QStringLiteral("27"));
    LoopbackTransport* g2 = core.signIn(device, {{"deviceAuth", 1}, {"firmwareBeta", 1}});
    QVERIFY(admitted(g2));
    QCOMPARE(StationCapabilities::fromUpdates(lastCapabilityEntries(g2)).firmwareBetaVersion, 39);
}

void TstFirmwareBetaLink::coreSendsNoNumberForARecordOfAnotherRadio()
{
    // The record is written when a connect starts and the version when one
    // succeeds: after a failed try at a second radio the record is that
    // radio's, and its second number is not the first radio's.
    Core core;
    giveRadio(core, 21, 9, QStringLiteral("22"));
    QCOMPARE(core.model->firmwareBetaVersion(), 0);
    QCOMPARE(core.model->firmwareText(), QStringLiteral("22"));
    Device device(QStringLiteral("Desktop"), QStringLiteral("computer"));
    core.pair(device);
    LoopbackTransport* app = core.signIn(device, {{"deviceAuth", 1}, {"firmwareBeta", 1}});
    QVERIFY(admitted(app));
    const StationCapabilities caps = StationCapabilities::fromUpdates(lastCapabilityEntries(app));
    QCOMPARE(caps.firmwareVersion, QStringLiteral("22"));
    QVERIFY(caps.firmwareBetaEntry);
    QCOMPARE(caps.firmwareBetaVersion, 0);

    // The same record and version agreeing: the number counts.
    giveRadio(core, 22, 14, QStringLiteral("22"));
    QCOMPARE(core.model->firmwareBetaVersion(), 14);
    QCOMPARE(core.model->firmwareText(), QStringLiteral("22.14"));

    // No radio yet: no number and no text.
    giveRadio(core, 0, 14, QString());
    QCOMPARE(core.model->firmwareBetaVersion(), 0);
    QCOMPARE(core.model->firmwareText(), QString());
}

void TstFirmwareBetaLink::remoteWindowWordsTheCoresRadio()
{
    RadioModel remote(RadioModel::Role::Remote);

    StationCapabilities caps = radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22"));
    caps.firmwareBetaEntry = true;
    caps.firmwareBetaVersion = 14;
    remote.applyStationCapabilities(caps);
    QCOMPARE(remote.version(), QStringLiteral("22"));
    QCOMPARE(remote.currentRadioInfo().firmwareVersion, 22);
    QCOMPARE(remote.currentRadioInfo().firmwareBetaVersion, 14);
    QCOMPARE(remote.firmwareBetaVersion(), 14);
    QCOMPARE(remote.firmwareText(), QStringLiteral("22.14"));

    // An older Core sends the first number alone: the window reads it alone,
    // and the last Core's second number is not left beside it.
    remote.applyStationCapabilities(radioCaps(HPSDRHW::OrionMKII, QStringLiteral("21")));
    QCOMPARE(remote.currentRadioInfo().firmwareBetaVersion, 0);
    QCOMPARE(remote.firmwareText(), QStringLiteral("21"));

    // An ANAN-G2's second number is its program's version: carried, kept and
    // left out of the text, as a window running that radio leaves it out.
    StationCapabilities g2 = radioCaps(HPSDRHW::Saturn, QStringLiteral("27"));
    g2.firmwareBetaEntry = true;
    g2.firmwareBetaVersion = 39;
    remote.applyStationCapabilities(g2);
    QCOMPARE(remote.currentRadioInfo().firmwareBetaVersion, 39);
    QCOMPARE(remote.firmwareText(), QStringLiteral("27"));

    // A Core with no radio: nothing to word, whatever number came with it.
    StationCapabilities none = radioCaps(HPSDRHW::Unknown, QString());
    none.firmwareBetaEntry = true;
    none.firmwareBetaVersion = 14;
    remote.applyStationCapabilities(none);
    QCOMPARE(remote.currentRadioInfo().firmwareBetaVersion, 0);
    QCOMPARE(remote.firmwareBetaVersion(), 0);
    QCOMPARE(remote.firmwareText(), QString());
}

void TstFirmwareBetaLink::radioStatusPageShowsTheCoresRelease()
{
    // A window on a Core that sends the second number.
    RadioModel remote(RadioModel::Role::Remote);
    StationCapabilities caps = radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22"));
    caps.firmwareBetaEntry = true;
    caps.firmwareBetaVersion = 14;
    remote.applyStationCapabilities(caps);
    const RadioStatusPage page(&remote);
    QCOMPARE(firmwareReadout(page), QStringLiteral("22.14"));

    // A window on an older Core reads the first number alone, as before.
    RadioModel older(RadioModel::Role::Remote);
    older.applyStationCapabilities(radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22")));
    const RadioStatusPage olderPage(&older);
    QCOMPARE(firmwareReadout(olderPage), QStringLiteral("22"));
}

void TstFirmwareBetaLink::openRadioStatusPageFollowsFirmwareChanges()
{
    RadioModel remote(RadioModel::Role::Remote);
    StationCapabilities caps = radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22"));
    remote.applyStationCapabilities(caps);
    const RadioStatusPage page(&remote);
    QCOMPARE(firmwareReadout(page), QStringLiteral("22"));

    // An already-open page follows the Core's second number, including a
    // change that leaves the first number and connection state alone.
    caps.firmwareBetaEntry = true;
    caps.firmwareBetaVersion = 14;
    remote.applyStationCapabilities(caps);
    QCOMPARE(firmwareReadout(page), QStringLiteral("22.14"));

    caps.firmwareBetaVersion = 15;
    remote.applyStationCapabilities(caps);
    QCOMPARE(firmwareReadout(page), QStringLiteral("22.15"));

    // An older Core's descriptor clears the second number on the same page.
    remote.applyStationCapabilities(radioCaps(HPSDRHW::OrionMKII, QStringLiteral("22")));
    QCOMPARE(firmwareReadout(page), QStringLiteral("22"));
}

void TstFirmwareBetaLink::windowOnACoreReadsBothNumbers()
{
    Core core;
    giveRadio(core, 22, 14, QStringLiteral("22"));
    QTemporaryDir keyDir;
    auto key = std::make_shared<const ClientDeviceIdentity>(
        ClientDeviceIdentity::loadOrCreate(keyDir.path()));
    QVERIFY(key->isValid());
    PairedDevice record;
    record.id = key->fingerprint();
    record.publicKeySpki = key->publicKeySpki();
    record.name = QStringLiteral("Desktop");
    record.kind = QStringLiteral("computer");
    QVERIFY(core.server->deviceStore()->add(record));

    const auto connectTo = [&core, &key](StationClient& client) {
        client.setDeviceIdentity(key, QStringLiteral("Desktop"));
        auto* app = new LoopbackTransport(QStringLiteral("window"));
        auto* station = new LoopbackTransport(QStringLiteral("core"));
        app->setPeerCertificateSha256(core.certSha256());
        station->linkTo(app);
        client.startSession(app, {}, {}, core.server->stationIdentity().fingerprint());
        core.server->acceptTransport(station);
    };

    // Today's window declares the entry and words the Core's radio in full.
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        connectTo(client);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_COMPARE(remote.version(), QStringLiteral("22"));
        QVERIFY(client.capabilities().firmwareBetaEntry);
        QCOMPARE(client.capabilities().firmwareBetaVersion, 14);
        QCOMPARE(remote.firmwareText(), QStringLiteral("22.14"));
        client.disconnectFromStation(QStringLiteral("done"));
    }

    // A window built before the entry is sent the first number alone.
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        client.withholdFeatureForTest(QByteArrayLiteral("firmwareBeta"));
        connectTo(client);
        QTRY_VERIFY(client.isHandshakeComplete());
        QTRY_COMPARE(remote.version(), QStringLiteral("22"));
        QVERIFY(!client.capabilities().firmwareBetaEntry);
        QCOMPARE(remote.firmwareText(), QStringLiteral("22"));
        client.disconnectFromStation(QStringLiteral("done"));
    }
}

QTEST_MAIN(TstFirmwareBetaLink)
#include "tst_firmware_beta_link.moc"
