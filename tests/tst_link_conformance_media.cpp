// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_link_conformance_media.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 3 (R-IOS-01): the station runs the link's media
// vectors (tests/data/link/v1/media/*.bin with *.expect.json). Each vector
// is bytes the station's own encoder wrote (tst_link_conformance_regen) and
// the values they decode to. The station decodes the bytes and compares
// them with the expectation, then encodes the expectation again and
// compares the result with the bytes, so a change to either the decoder or
// the encoder shows here. The app decodes the same bytes with its own
// decoders.
//
//   codec "nrsc1"   the LAN announcement datagram (link document section
//                   14), decoded by decodeStationLanAnnouncement
//   codec "ps3d"    one PureSignal display chunk, assembled by a fresh
//                   Ps3DisplayAssembler for the expectation's
//                   sessionGeneration; values are exact (tolerance 0)
//
//   cmake --build build --target tst_link_conformance_media
//   QT_QPA_PLATFORM=offscreen ctest --test-dir build \
//       -R '^tst_link_conformance_media$' --output-on-failure
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 3 (R-IOS-01): media
//                                    conformance runner. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonObject>

#include <optional>

#include "core/dsp/Ps3Snapshot.h"
#include "core/session/Ps3DisplayCodec.h"
#include "core/session/StationLanAnnouncement.h"

#include "LinkFixtures.h"

using namespace NereusSDR;
using NereusSDR::Test::LinkFixtures;
using NereusSDR::Test::LinkMediaVectors;

namespace {

QString checkAnnouncement(const QByteArray& bytes, const QJsonObject& expect)
{
    QString error;
    const std::optional<StationLanAnnouncement> decoded =
        decodeStationLanAnnouncement(bytes, &error);
    if (!decoded) {
        return QStringLiteral("the station's decoder refused the announcement: %1").arg(error);
    }
    LinkFixtures::Captures none;
    const QString difference =
        LinkFixtures::match(expect, LinkMediaVectors::toJson(*decoded), &none);
    if (!difference.isEmpty()) {
        return QStringLiteral("the decoded announcement differs at %1").arg(difference);
    }
    StationLanAnnouncement value;
    if (!LinkMediaVectors::fromJson(expect, &value, &error)) {
        return error;
    }
    if (encodeStationLanAnnouncement(value, &error) != bytes) {
        return QStringLiteral("the station's encoder no longer writes these bytes for this "
                              "announcement %1")
            .arg(error);
    }
    return QString();
}

QString checkPs3d(const QByteArray& bytes, QJsonObject expect)
{
    if (expect.value(QStringLiteral("tolerance")).toObject()
        != QJsonObject{{QStringLiteral("absolute"), 0}}) {
        return QStringLiteral("a PS3D vector carries exact values: tolerance must be "
                              "{\"absolute\": 0}");
    }
    expect.remove(QStringLiteral("tolerance"));
    QString error;
    Ps3Snapshot expected;
    if (!LinkMediaVectors::fromJson(expect, &expected, &error)) {
        return error;
    }
    Ps3DisplayAssembler assembler(expected.sessionGeneration);
    const std::optional<Ps3Snapshot> decoded = assembler.accept(bytes, &error);
    if (!decoded) {
        return QStringLiteral("the station's assembler refused the chunk: %1").arg(error);
    }
    LinkFixtures::Captures none;
    const QString difference =
        LinkFixtures::match(expect, LinkMediaVectors::toJson(*decoded), &none);
    if (!difference.isEmpty()) {
        return QStringLiteral("the decoded frame differs at %1").arg(difference);
    }
    const QList<QByteArray> chunks = Ps3DisplayCodec::encode(expected, &error);
    if (chunks.size() != 1 || chunks.first() != bytes) {
        return QStringLiteral("the station's encoder no longer writes these bytes for this "
                              "frame %1")
            .arg(error);
    }
    return QString();
}

QString checkMedia(const QByteArray& bytes, const QJsonObject& expectation)
{
    for (auto it = expectation.constBegin(); it != expectation.constEnd(); ++it) {
        if (it.key() != QStringLiteral("codec") && it.key() != QStringLiteral("expect")) {
            return QStringLiteral("media expectation: unknown field \"%1\"").arg(it.key());
        }
    }
    const QString codec = expectation.value(QStringLiteral("codec")).toString();
    const QJsonObject expect = expectation.value(QStringLiteral("expect")).toObject();
    if (codec == QStringLiteral("nrsc1")) {
        return checkAnnouncement(bytes, expect);
    }
    if (codec == QStringLiteral("ps3d")) {
        return checkPs3d(bytes, expect);
    }
    return QStringLiteral("media expectation: unknown codec \"%1\"").arg(codec);
}

} // namespace

class TstLinkConformanceMedia : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void mediaVectors_data();
    void mediaVectors();
    void alteredVectorsFailReadably();

private:
    bool load(const QString& id, QByteArray* bytes, QJsonObject* expectation);
    QJsonObject m_manifest;
};

void TstLinkConformanceMedia::initTestCase()
{
    QString error;
    m_manifest = LinkFixtures::readObject(
        QDir(LinkFixtures::dataDirectory()).filePath(QStringLiteral("manifest.json")), &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

bool TstLinkConformanceMedia::load(const QString& id, QByteArray* bytes, QJsonObject* expectation)
{
    for (const LinkFixtures::Entry& entry :
         LinkFixtures::entries(m_manifest, QStringLiteral("media"))) {
        if (entry.id != id) {
            continue;
        }
        const QDir root(LinkFixtures::dataDirectory());
        QFile file(root.filePath(entry.file));
        if (!file.open(QIODevice::ReadOnly)) {
            return false;
        }
        *bytes = file.readAll();
        QString error;
        *expectation = LinkFixtures::readObject(
            root.filePath(entry.file.chopped(4) + QStringLiteral(".expect.json")), &error);
        return error.isEmpty();
    }
    return false;
}

void TstLinkConformanceMedia::mediaVectors_data()
{
    QTest::addColumn<QString>("id");
    const QList<LinkFixtures::Entry> entries =
        LinkFixtures::entries(m_manifest, QStringLiteral("media"));
    QVERIFY(!entries.isEmpty());
    for (const LinkFixtures::Entry& entry : entries) {
        QTest::newRow(qPrintable(entry.id)) << entry.id;
    }
}

void TstLinkConformanceMedia::mediaVectors()
{
    QFETCH(QString, id);
    QByteArray bytes;
    QJsonObject expectation;
    QVERIFY2(load(id, &bytes, &expectation), qPrintable(id));
    const QString failure = checkMedia(bytes, expectation);
    QVERIFY2(failure.isEmpty(), qPrintable(failure));
}

void TstLinkConformanceMedia::alteredVectorsFailReadably()
{
    // One byte of the announcement's core name changed: the decoded name
    // differs, and the failure names the field.
    QByteArray bytes;
    QJsonObject expectation;
    QVERIFY(load(QStringLiteral("media-lan-announcement"), &bytes, &expectation));
    const qsizetype at = bytes.indexOf(QByteArrayLiteral("Shack Core"));
    QVERIFY(at >= 0);
    bytes[at] = 'W';
    QString failure = checkMedia(bytes, expectation);
    QVERIFY2(failure.contains(QStringLiteral("$.coreName")), qPrintable(failure));

    // One expected PS3D value changed: the failure names the element.
    QVERIFY(load(QStringLiteral("media-ps3d-frame"), &bytes, &expectation));
    QJsonObject expect = expectation.value(QStringLiteral("expect")).toObject();
    QJsonArray x = expect.value(QStringLiteral("x")).toArray();
    x.replace(2, 99.0);
    expect.insert(QStringLiteral("x"), x);
    expectation.insert(QStringLiteral("expect"), expect);
    failure = checkMedia(bytes, expectation);
    QVERIFY2(failure.contains(QStringLiteral("$.x[2]")), qPrintable(failure));
}

QTEST_MAIN(TstLinkConformanceMedia)
#include "tst_link_conformance_media.moc"
