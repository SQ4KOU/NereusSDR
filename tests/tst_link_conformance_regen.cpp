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
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 3 (R-IOS-01): NSDC and
//                                    Opus vectors, with "after" for the
//                                    stateful decoders. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Part A (R-R3-03, R-IOS-01):
//                                    three NSDC vectors both malformed and
//                                    refused. AI-assisted transformation
//                                    via Anthropic Claude Code.
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
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/OpusAudioCodec.h"

#include "LinkFixtures.h"

using namespace NereusSDR;
using namespace NereusSDR::Test;

class TstLinkConformanceRegen : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void writeLanAnnouncement();
    void writePs3dFrame();
    void writeNsdcFrames();
    void writeOpusPackets();

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

void TstLinkConformanceRegen::writeNsdcFrames()
{
    // One encoder sends frames 1 to 4; frame 3 is lost on the way, so the
    // sender is asked for a keyframe and sends frame 4 as one. Each
    // expectation is what the station's decoder makes of the packet after
    // the packets its "after" names.
    const QList<DisplayCodecFrame> frames = LinkMediaVectors::nsdcFrames();
    QCOMPARE(frames.size(), 4);
    DisplayCodecEncoder encoder;
    const QByteArray full = encoder.encode(frames.at(0));
    const QByteArray delta = encoder.encode(frames.at(1));
    const QByteArray lost = encoder.encode(frames.at(2));
    const QByteArray keyframe = encoder.encode(frames.at(3), /*requestKeyframe=*/true);
    QVERIFY(!full.isEmpty() && !delta.isEmpty() && !lost.isEmpty() && !keyframe.isEmpty());

    const auto expectation = [](const QList<QByteArray>& before, const QByteArray& packet,
                                const QStringList& after) {
        DisplayCodecDecoder decoder;
        for (const QByteArray& earlier : before) {
            decoder.decode(earlier);
        }
        const DisplayCodecDecodeResult result = decoder.decode(packet);
        QJsonObject expect = LinkMediaVectors::toJson(result);
        expect.insert(QStringLiteral("keyframe"), (packet.at(5) & 0x01) != 0);
        if (!after.isEmpty()) {
            expect.insert(QStringLiteral("after"), QJsonArray::fromStringList(after));
        }
        if (result.disposition == DisplayCodecDisposition::Accepted) {
            expect.insert(QStringLiteral("tolerance"),
                          QJsonObject{{QStringLiteral("dbm"), 0.01}});
        }
        return QJsonObject{{QStringLiteral("codec"), QStringLiteral("nsdc1")},
                           {QStringLiteral("expect"), expect}};
    };
    const QStringList afterFull{QStringLiteral("media-nsdc1-full")};
    QVERIFY(write(QStringLiteral("nsdc1-full"), full, expectation({}, full, {})));
    QVERIFY(write(QStringLiteral("nsdc1-delta"), delta, expectation({full}, delta, afterFull)));
    QVERIFY(write(QStringLiteral("nsdc1-delta-after-loss"), lost,
                  expectation({full}, lost, afterFull)));
    QVERIFY(write(QStringLiteral("nsdc1-keyframe-after-loss"), keyframe,
                  expectation({full}, keyframe, afterFull)));

    // Datagrams both malformed and refused: the structure is checked first
    // (display codec document, "State and recovery"), so each rejects as
    // malformed whatever the decoder's state would have refused it for.
    QVERIFY(write(QStringLiteral("nsdc1-malformed-delta"),
                  LinkMediaVectors::nsdcBadPlaneDelta(delta),
                  expectation({}, LinkMediaVectors::nsdcBadPlaneDelta(delta), {})));
    QVERIFY(write(QStringLiteral("nsdc1-malformed-stale-delta"),
                  LinkMediaVectors::nsdcStaleTruncatedDelta(delta),
                  expectation({full}, LinkMediaVectors::nsdcStaleTruncatedDelta(delta),
                              afterFull)));
    QVERIFY(write(QStringLiteral("nsdc1-malformed-keyframe"),
                  LinkMediaVectors::nsdcBadBlockCountKeyframe(keyframe),
                  expectation({full, lost}, LinkMediaVectors::nsdcBadBlockCountKeyframe(keyframe),
                              {QStringLiteral("media-nsdc1-full"),
                               QStringLiteral("media-nsdc1-delta-after-loss")})));
}

void TstLinkConformanceRegen::writeOpusPackets()
{
    // Four consecutive packets from one encoder at the station's settings
    // (OpusAudioCodecConfig: 48 kHz stereo, 1920 samples, 24000 bit/s,
    // wideband), each as the RTP packet the station sends. The reference
    // PCM is the station's decoder's output decoding them in order.
    OpusAudioEncoder encoder;
    QVERIFY(encoder.isReady());
    OpusAudioDecoder decoder;
    QVERIFY(decoder.isReady());
    QStringList before;
    for (int i = 0; i < LinkMediaVectors::kOpusPackets; ++i) {
        const OpusRtpEncodeResult encoded = encoder.encode(
            LinkMediaVectors::opusInput(i),
            static_cast<quint16>(LinkMediaVectors::kOpusFirstSequence + i),
            LinkMediaVectors::kOpusFirstTimestamp
                + static_cast<quint32>(i * OpusAudioCodecConfig::kFrameSamples),
            LinkMediaVectors::kOpusSsrc);
        QCOMPARE(encoded.status, OpusAudioCodecStatus::Accepted);
        const OpusRtpDecodeResult decoded =
            decoder.decodeRtp(encoded.packet, LinkMediaVectors::kOpusSsrc);
        QCOMPARE(decoded.status, OpusAudioCodecStatus::Accepted);
        QJsonObject expect = LinkMediaVectors::toJson(decoded);
        expect.insert(QStringLiteral("ssrc"), static_cast<qint64>(LinkMediaVectors::kOpusSsrc));
        expect.insert(QStringLiteral("tolerance"), QJsonObject{{QStringLiteral("minSnrDb"), 60}});
        if (!before.isEmpty()) {
            expect.insert(QStringLiteral("after"), QJsonArray::fromStringList(before));
        }
        const QString name = QStringLiteral("opus-%1").arg(i + 1);
        QVERIFY(write(name, encoded.packet,
                      QJsonObject{{QStringLiteral("codec"), QStringLiteral("opus")},
                                  {QStringLiteral("expect"), expect}}));
        before.append(QStringLiteral("media-") + name);
    }
}

QTEST_MAIN(TstLinkConformanceRegen)
#include "tst_link_conformance_regen.moc"
