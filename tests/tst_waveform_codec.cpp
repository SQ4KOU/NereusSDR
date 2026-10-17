// no-port-check: NereusSDR-original wire validation tests.
#include "core/session/media/WaveformCodec.h"
#include <QtTest>
#include <bit>
#include <cmath>
#include <limits>
using namespace NereusSDR;
class TestWaveformCodec : public QObject {
    Q_OBJECT
private slots:
    void fullRateExact() {
        for (WaveformKind kind : {WaveformKind::RawMicrophone,
             WaveformKind::ConditionedMicrophone, WaveformKind::DecodedSpeech}) {
            WaveformPacket p;
            p.sliceId = 5; p.sourceGeneration = UINT64_MAX; p.deliveryGeneration = UINT64_MAX - 1;
            p.batch.kind = kind; p.batch.sampleRate = WaveformCodec::sampleRate(kind);
            p.batch.serial = UINT64_MAX; p.batch.droppedFrames = UINT64_MAX;
            p.batch.samples = {0.0f, -0.0f, 0.1234567f, -2.0f, std::numeric_limits<float>::denorm_min()};
            p.batch.segments = {{0, 5, UINT64_MAX - 5, 123456789}};
            const auto bytes = WaveformCodec::encode(p); QVERIFY(bytes);
            QCOMPARE(bytes->size(), WaveformCodec::kHeaderBytes + 20);
            const auto decoded = WaveformCodec::decode(*bytes); QVERIFY(decoded);
            QCOMPARE(decoded->sourceGeneration, p.sourceGeneration);
            QCOMPARE(decoded->deliveryGeneration, p.deliveryGeneration);
            QCOMPARE(decoded->batch.serial, p.batch.serial);
            for (size_t i = 0; i < p.batch.samples.size(); ++i) {
                QCOMPARE(std::bit_cast<quint32>(decoded->batch.samples[i]), std::bit_cast<quint32>(p.batch.samples[i]));
            }
        }
    }
    void strictMalformed() {
        WaveformPacket p;
        p.sliceId = 0; p.sourceGeneration = 1; p.deliveryGeneration = 1;
        p.batch.kind = WaveformKind::RawMicrophone; p.batch.sampleRate = 48000;
        p.batch.serial = 1; p.batch.samples = {0.5f}; p.batch.segments = {{0,1,0,0}};
        const auto encoded = WaveformCodec::encode(p); QVERIFY(encoded);
        for (int offset : {0,4,5,6,7,12,56,60}) {
            QByteArray broken = *encoded; broken[offset] = char(0xff);
            QVERIFY2(!WaveformCodec::decode(broken), qPrintable(QString::number(offset)));
        }
        QVERIFY(!WaveformCodec::decode(encoded->left(encoded->size()-1)));
        QByteArray nonfinite = *encoded;
        nonfinite[72]=char(0x7f); nonfinite[73]=char(0x80); nonfinite[74]=0; nonfinite[75]=0;
        QVERIFY(!WaveformCodec::decode(nonfinite));
        p.batch.samples[0] = std::numeric_limits<float>::infinity(); QVERIFY(!WaveformCodec::encode(p));
        QVERIFY(!WaveformCodec::decimalIdentity(QJsonValue(42)));
        QVERIFY(!WaveformCodec::decimalIdentity(QJsonValue("01")));
        QVERIFY(!WaveformCodec::decimalIdentity(QJsonValue("18446744073709551616")));
        QCOMPARE(WaveformCodec::decimalIdentity(QJsonValue("18446744073709551615")), std::optional<quint64>(UINT64_MAX));
    }
    void chunksKeepGapCoordinates() {
        WaveformBatch batch; batch.kind=WaveformKind::RawMicrophone; batch.sampleRate=48000;
        batch.samples.resize(2500, 0.125f);
        batch.segments={{0,2000,100,900000000},{2000,500,4000,1900000000}};
        const auto first = WaveformCodec::chunk(0,1,2,1,batch,0,0); QVERIFY(first);
        QCOMPARE(first->batch.samples.size(),size_t(1024));
        const auto next = WaveformCodec::chunk(0,1,2,2,batch,0,1024); QVERIFY(next);
        QCOMPARE(next->batch.samples.size(),size_t(976));
        QCOMPARE(next->batch.segments[0].firstSequence,quint64(1124));
        QCOMPARE(next->batch.segments[0].startNs,qint64(921333333));
        const auto gap = WaveformCodec::chunk(0,1,2,3,batch,1,0); QVERIFY(gap);
        QCOMPARE(gap->batch.segments[0].firstSequence,quint64(4000));
        QCOMPARE(gap->batch.segments[0].startNs,qint64(1900000000));
        QVERIFY(!WaveformCodec::chunk(0,1,2,3,batch,0,2000));
    }
};
QTEST_GUILESS_MAIN(TestWaveformCodec)
#include "tst_waveform_codec.moc"
