// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#include "gui/containers/ContainerContentRegistry.h"
#include "gui/meters/WaveformMeterItem.h"
#include <QDir>
#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cmath>
using namespace NereusSDR;
namespace {
QImage render(WaveformMeterItem& face, int dpr)
{
    QImage im(500 * dpr, 300 * dpr, QImage::Format_ARGB32_Premultiplied);
    im.setDevicePixelRatio(dpr);
    im.fill(Qt::transparent);
    QPainter p(&im);
    face.paint(p, 500, 300);
    return im;
}
bool traceNear(const QImage& im, int x, int y, int radius = 4)
{
    const int dpr = int(im.devicePixelRatio());
    for (int j = (y - radius) * dpr; j <= (y + radius) * dpr; ++j) {
        for (int i = (x - radius) * dpr; i <= (x + radius) * dpr; ++i) {
            if (im.rect().contains(i, j) && im.pixelColor(i, j) == QColor("#7fae91")) {
                return true;
            }
        }
    }
    return false;
}
} // namespace
class TestWaveformMeter : public QObject
{
    Q_OBJECT
  private slots:
    void catalogAndAtomicConfiguration()
    {
        ContainerContentRegistry registry;
        const auto entry = registry.makeEntry("meter.radeMicWaveform");
        std::unique_ptr<MeterItem> item(
            registry.createMeterItem(entry, nullptr, ContentRenderMode::Validation));
        QVERIFY(item);
        auto* face = qobject_cast<WaveformMeterItem*>(item.get());
        QVERIFY(face);
        QCOMPARE(face->waveformKind(), WaveformKind::ConditionedMicrophone);
        QVERIFY(face->applyConfiguration({{"viewWindowMs", 100}, {"sourceStage", "RawMic"}, {"opaque", 42}}));
        QCOMPARE(face->viewWindowMs(), 100);
        QCOMPARE(face->waveformKind(), WaveformKind::RawMicrophone);
        auto prior = face->configuration();
        QVERIFY(!face->applyConfiguration({{"viewWindowMs", 400}, {"title", "bad"}}));
        QCOMPARE(face->configuration(), prior);
        QVERIFY(!face->applyConfiguration({{"sourceStage", "Fallback"}}));
        QCOMPARE(face->configuration(), prior);
        std::unique_ptr<MeterItem> restored(registry.createMeterItem(registry.captureMeterItem(*face),
                                                                     nullptr, ContentRenderMode::Validation));
        QVERIFY(restored);
        QCOMPARE(qobject_cast<WaveformMeterItem*>(restored.get())->configuration(), prior);
    }
    void fullRateImpulsesLastSampleAndDpr_data()
    {
        QTest::addColumn<int>("dpr");
        QTest::addColumn<int>("window");
        for (int dpr : {1, 2}) {
            for (int window : {5000, 1000, 100}) {
                QTest::newRow(qPrintable(QString("%1-%2").arg(dpr).arg(window))) << dpr << window;
            }
        }
    }
    void fullRateImpulsesLastSampleAndDpr()
    {
        QFETCH(int, dpr);
        QFETCH(int, window);
        WaveformMeterItem face;
        QVERIFY(face.applyConfiguration({{"viewWindowMs", window}, {"sourceStage", "RawMic"}}));
        auto s = std::make_shared<WaveformSnapshot>();
        s->kind = WaveformKind::RawMicrophone;
        s->sampleRate = 48000;
        s->asOfNs = qint64(window) * 1000000;
        s->availability = WaveformAvailability::Available;
        const int n = 48000 * window / 1000;
        s->samples.fill(0, n);
        s->samples[n / 2] = 1;
        s->samples[n / 2 + 1] = -1;
        s->samples[n - 1] = 1;
        s->segments = {{0, n, 0, 0}};
        face.setSnapshot(s);
        const auto im = render(face, dpr);
        QVERIFY(traceNear(im, 266, 39));
        QVERIFY(traceNear(im, 266, 258));
        QVERIFY(traceNear(im, 483, 39));
        const QString dir = qEnvironmentVariable("NEREUS_WAVEFORM_SHOTS");
        if (!dir.isEmpty()) {
            QDir().mkpath(dir);
            QVERIFY(im.save(dir + QString("/wide-%1ms-dpr%2.png").arg(window).arg(dpr)));
        }
    }
    void storedScalarAndMmioContextIsRetainedButNeverActivated()
    {
        ContainerContentRegistry registry;
        auto entry = registry.makeEntry("meter.radeMicWaveform");
        entry.context = {{"bindingId", 103},
                         {"mmioGuid", "01234567-89ab-cdef-0123-456789abcdef"},
                         {"mmioVariable", "audio"}};
        std::unique_ptr<MeterItem> item(
            registry.createMeterItem(entry, nullptr, ContentRenderMode::Validation));
        QVERIFY(item);
        QVERIFY(item->readingBindings().isEmpty());
        QVERIFY(!item->hasMmioBinding());
        const auto captured = registry.captureMeterItem(*item, entry);
        QCOMPARE(captured.context["bindingId"], entry.context["bindingId"]);
        QCOMPARE(captured.context["mmioGuid"], entry.context["mmioGuid"]);
        QCOMPARE(captured.context["mmioVariable"], entry.context["mmioVariable"]);
    }
    void closeZoomConnectsActualZigzagSamples()
    {
        WaveformMeterItem face;
        QVERIFY(face.applyConfiguration({{"viewWindowMs", 100}}));
        auto s = std::make_shared<WaveformSnapshot>();
        s->sampleRate = 16000;
        s->asOfNs = 100000000;
        s->availability = WaveformAvailability::Available;
        s->samples = {1, -1};
        s->segments = {{0, 2, 0, 50075000}};
        face.setSnapshot(s);
        auto im = render(face, 2);
        // Samples are separated by .545 physical pixels, so their connecting line crosses center.
        QVERIFY(traceNear(im, 266, 148, 1));
    }
    void fractionalCanvasWidthRetainsTheFinalImpulse()
    {
        WaveformMeterItem face;
        face.setRect(0, 0, 0.9984f, 1);
        QVERIFY(face.applyConfiguration({{"viewWindowMs", 100}, {"sourceStage", "RawMic"}}));
        auto snapshot = std::make_shared<WaveformSnapshot>();
        snapshot->kind = WaveformKind::RawMicrophone;
        snapshot->sampleRate = 48000;
        snapshot->asOfNs = 100000000;
        snapshot->availability = WaveformAvailability::Available;
        snapshot->samples.fill(0, 4800);
        snapshot->samples.last() = 1;
        snapshot->segments = {{0, 4800, 0, 0}};
        face.setSnapshot(snapshot);
        QVERIFY(traceNear(render(face, 1), 482, 39, 2));
    }
    void representativeOfflinePcmDetail()
    {
        WaveformMeterItem face;
        QVERIFY(face.applyConfiguration({{"viewWindowMs", 100}}));
        auto snapshot = std::make_shared<WaveformSnapshot>();
        snapshot->kind = WaveformKind::ConditionedMicrophone;
        snapshot->sampleRate = 16000;
        snapshot->asOfNs = 100000000;
        snapshot->availability = WaveformAvailability::Available;
        constexpr double kPi = 3.14159265358979323846;
        for (int i = 0; i < 1600; ++i) {
            const double time = double(i) / 16000;
            const double envelope = 0.55 + 0.2 * std::sin(2 * kPi * 15 * time);
            snapshot->samples.append(
                float(envelope * std::sin(2 * kPi * 110 * time) + 0.15 * std::sin(2 * kPi * 330 * time)));
        }
        snapshot->segments = {{0, 1600, 0, 0}};
        face.setSnapshot(snapshot);
        const QImage image = render(face, 2);
        QVERIFY(traceNear(image, 48, 148));
        WaveformMeterItem empty;
        QVERIFY(image != render(empty, 2));
        const QString directory = qEnvironmentVariable("NEREUS_WAVEFORM_SHOTS");
        if (!directory.isEmpty()) {
            QVERIFY(image.save(directory + "/conditioned-pcm-detail-100ms-dpr2.png"));
        }
    }
    void gapsAndUnavailableDifferFromSilence()
    {
        WaveformMeterItem face;
        auto s = std::make_shared<WaveformSnapshot>();
        s->sampleRate = 10;
        s->asOfNs = 5000000000LL;
        s->availability = WaveformAvailability::Available;
        s->samples = {0.8f, 0.8f, -0.8f, -0.8f};
        s->segments = {{0, 2, 0, 0}, {2, 2, 30, 3000000000LL}};
        face.setSnapshot(s);
        auto gap = render(face, 2);
        QVERIFY(!traceNear(gap, 200, 148, 20));
        s = std::make_shared<WaveformSnapshot>();
        s->sampleRate = 10;
        s->asOfNs = 5000000000LL;
        s->availability = WaveformAvailability::Available;
        s->samples.fill(0, 50);
        s->segments = {{0, 50, 0, 0}};
        face.setSnapshot(s);
        auto silence = render(face, 2);
        QVERIFY(traceNear(silence, 266, 148));
        auto unavailable = std::make_shared<WaveformSnapshot>();
        unavailable->availability = WaveformAvailability::Unavailable;
        unavailable->reason = "Conditioned input has not started. Select Raw microphone for setup.";
        face.setSnapshot(unavailable);
        auto empty = render(face, 2);
        QVERIFY(!traceNear(empty, 266, 148));
        QVERIFY(empty != silence);
        const QString dir = qEnvironmentVariable("NEREUS_WAVEFORM_SHOTS");
        if (!dir.isEmpty()) {
            QVERIFY(empty.save(dir + "/unavailable-dpr2.png"));
            QVERIFY(gap.save(dir + "/gaps-dpr2.png"));
        }
    }
};
QTEST_MAIN(TestWaveformMeter)
#include "tst_waveform_meter.moc"
