// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_link_conformance_regen.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan, Task 3 (R-IOS-01): writes the link's media vectors,
// tests/data/link/v1/media/*.bin with their *.expect.json, from the
// station's own encoders. Into NEREUS_LINK_REGEN_OUT (the v1 directory;
// files land in its media/ folder) when it is set, and otherwise into a
// temporary directory, so ctest runs it without touching the tree (the
// precedent is tst_link_surface_manifest_regen). Not a regression check;
// tst_link_conformance_media is.
//
//   cmake --build build --target tst_link_conformance_regen
//   NEREUS_LINK_REGEN_OUT=tests/data/link/v1 QT_QPA_PLATFORM=offscreen \
//       ./build/tests/tst_link_conformance_regen
//
// Every input is fixed, so a second run writes the same bytes. Read the
// resulting diff whole before committing it.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 3 (R-IOS-01): media
//                                    vector writer. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "core/dsp/Ps3Snapshot.h"
#include "core/session/Ps3DisplayCodec.h"
#include "core/session/StationLanAnnouncement.h"

#include "LinkFixtures.h"

using namespace NereusSDR;
using namespace NereusSDR::Test;

class TstLinkConformanceRegen : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void writeLanAnnouncement();
    void writePs3dFrame();

private:
    bool write(const QString& name, const QByteArray& bytes, const QJsonObject& expectation);
    QTemporaryDir m_scratch;
    QString m_media;
};

void TstLinkConformanceRegen::initTestCase()
{
    const QByteArray requested = qgetenv("NEREUS_LINK_REGEN_OUT");
    const QString root =
        requested.isEmpty() ? m_scratch.path() : QString::fromLocal8Bit(requested);
    QVERIFY2(!root.isEmpty(), "no output directory");
    m_media = QDir(root).filePath(QStringLiteral("media"));
    QVERIFY(QDir().mkpath(m_media));
}

bool TstLinkConformanceRegen::write(const QString& name, const QByteArray& bytes,
                                    const QJsonObject& expectation)
{
    QFile bin(QDir(m_media).filePath(name + QStringLiteral(".bin")));
    QFile expect(QDir(m_media).filePath(name + QStringLiteral(".expect.json")));
    if (!bin.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || !expect.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray json = QJsonDocument(expectation).toJson(QJsonDocument::Indented);
    const bool ok = bin.write(bytes) == bytes.size() && expect.write(json) == json.size();
    if (ok && !qEnvironmentVariableIsEmpty("NEREUS_LINK_REGEN_OUT")) {
        qInfo().noquote() << "wrote" << bin.fileName() << "and" << expect.fileName();
    }
    return ok;
}

void TstLinkConformanceRegen::writeLanAnnouncement()
{
    const StationLanAnnouncement value = LinkMediaVectors::lanAnnouncement();
    QString error;
    const QByteArray bytes = encodeStationLanAnnouncement(value, &error);
    QVERIFY2(!bytes.isEmpty(), qPrintable(error));
    QVERIFY(write(QStringLiteral("lan-announcement"), bytes,
                  QJsonObject{{QStringLiteral("codec"), QStringLiteral("nrsc1")},
                              {QStringLiteral("expect"), LinkMediaVectors::toJson(value)}}));
}

void TstLinkConformanceRegen::writePs3dFrame()
{
    const Ps3Snapshot snapshot = LinkMediaVectors::ps3Snapshot();
    QString error;
    const QList<QByteArray> chunks = Ps3DisplayCodec::encode(snapshot, &error);
    QVERIFY2(chunks.size() == 1, qPrintable(error));
    QJsonObject expect = LinkMediaVectors::toJson(snapshot);
    expect.insert(QStringLiteral("tolerance"), QJsonObject{{QStringLiteral("absolute"), 0}});
    QVERIFY(write(QStringLiteral("ps3d-frame"), chunks.first(),
                  QJsonObject{{QStringLiteral("codec"), QStringLiteral("ps3d")},
                              {QStringLiteral("expect"), expect}}));
}

QTEST_MAIN(TstLinkConformanceRegen)
#include "tst_link_conformance_regen.moc"
