// no-port-check: NereusSDR-original. Authenticated GUI subscription lifecycle.
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <algorithm>
#include <cmath>
#include "core/AppSettings.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "core/session/media/DisplayCodec.h"
#include "core/session/media/DaemonMediaController.h"
#include "core/FFTEngine.h"
#include "core/StepAttenuatorController.h"
#include "gui/RemoteMediaController.h"
#include "gui/PanadapterStack.h"
#include "gui/PanadapterApplet.h"
#include "gui/SpectrumWidget.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "fakes/LoopbackTransport.h"

using namespace NereusSDR;

class DisplayTransport final : public IMediaTransport {
public:
    explicit DisplayTransport(QObject* parent) : IMediaTransport(parent) {}
    bool start(const StartOptions&) override { return true; }
    void stop() override { active = false; }
    bool acceptDescription(const QString&, const QString&) override { return true; }
    bool acceptCandidate(const QString&, const QString&) override { return true; }
    bool sendDisplay(const QByteArray& packet) override {
        if (!active) { return false; }
        if (other) { other->deliver(packet); }
        return true;
    }
    bool sendRtp(const QByteArray&) override { return active; }
    bool isReady() const override { return active; }
    void activate() { active = true; emit ready(); }
    void deliver(const QByteArray& packet) { emit displayReceived(packet); }
    bool active = false;
    QPointer<DisplayTransport> other;
};

namespace {
QJsonObject lastControl(const QSignalSpy& spy, const QString& op)
{
    for (auto it = spy.crbegin(); it != spy.crend(); ++it) {
        const QJsonObject control = it->at(0).toJsonObject();
        if (control.value(QStringLiteral("op")) == op) { return control; }
    }
    return {};
}
int countControl(const QSignalSpy& spy, const QString& op)
{
    int count = 0;
    for (const auto& call : spy) {
        if (call.at(0).toJsonObject().value(QStringLiteral("op")) == op) { ++count; }
    }
    return count;
}
} // namespace

class TestRemoteMediaController : public QObject {
    Q_OBJECT
private slots:
    void sharedWindowChangeAndRadioReconnectResumeBothPanes()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        auto& appSettings = AppSettings::instance();
        const QVariant savedWindow = appSettings.value(QStringLiteral("DisplayFftWindow"));
        const QVariant savedFft = appSettings.value(QStringLiteral("DisplayFftSize"));
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::Hann)));
        appSettings.setValue(QStringLiteral("DisplayFftSize"), QStringLiteral("4096"));
        const auto restore = qScopeGuard([&] {
            appSettings.setValue(QStringLiteral("DisplayFftWindow"), savedWindow);
            appSettings.setValue(QStringLiteral("DisplayFftSize"), savedFft);
        });
        RadioModel station;
        station.setBoardForTest(HPSDRHW::Saturn);
        StepAttenuatorController stationAttenuator(&station);
        stationAttenuator.setStepAttEnabled(true);
        stationAttenuator.setAttenuation(0);
        station.setStepAttController(&stationAttenuator);
        const auto detachStationAttenuator = qScopeGuard([&] {
            station.setStepAttController(nullptr);
        });
        station.configureStreamPool(5, 5, 192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        const int sliceId = station.addSlice();
        auto* slice = station.sliceById(sliceId);
        QVERIFY(slice);
        const int stream = slice->streamIndex();
        QVERIFY(stream >= 0);
        const double centre = station.streamCentreHz(stream);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        QPointer<DisplayTransport> sourceMedia;
        DaemonMediaController daemon(&server, &station, nullptr,
            [&sourceMedia](QObject* owner) -> IMediaTransport* {
                sourceMedia = new DisplayTransport(owner);
                return sourceMedia;
            });
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* first = stack.addPanadapter(QStringLiteral("first"));
        auto* second = stack.addPanadapter(QStringLiteral("second"));
        for (auto* applet : {first, second}) {
            applet->setActiveSliceIndex(sliceId);
            applet->spectrumWidget()->setDisplayWindowPreservingHistory(centre, 48000);
        }
        stack.resize(600, 700);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> sinkMedia;
        RemoteMediaController gui(&client, &remote, &stack, nullptr,
            [&sinkMedia](QObject* owner) -> IMediaTransport* {
                sinkMedia = new DisplayTransport(owner);
                return sinkMedia;
            });
        QSignalSpy outbound(&server, &StationServer::mediaControlReceived);
        QSignalSpy inbound(&client, &StationClient::mediaControlReceived);
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(sourceMedia && sinkMedia);
        sourceMedia->other = sinkMedia;
        sourceMedia->activate();
        sinkMedia->activate();
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        // Feed real tagged ingress in bounded radio-sized chunks. Repeated
        // calls also wait for the asynchronous source configuration to finish.
        QVector<float> iq(2048);
        for (int i = 0; i < iq.size(); i += 2) {
            // Keep the station-calibration matrix well below the codec's
            // 0 dBm ceiling; otherwise a positive offset can hide a shift.
            iq[i] = 0.01f * std::cos(double(i) * 0.17);
            iq[i + 1] = 0.01f * std::sin(double(i) * 0.17);
        }
        const auto bothHaveFrames = [&] {
            QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
            return !first->spectrumWidget()->renderedPixels().isEmpty()
                && !second->spectrumWidget()->renderedPixels().isEmpty();
        };
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        // DaemonMediaController applies the station offset before reducing
        // and encoding. Exercise all authoritative receiver paths using the
        // same tagged I/Q, rather than relying on the remote client's local
        // DisplayCalOffset preference.
        const auto traceLevels = [&] {
            QVector<float> bins = first->spectrumWidget()->renderedPixels();
            std::sort(bins.begin(), bins.end());
            return qMakePair(bins.last(), bins.at(bins.size() / 2));
        };
        const auto feedAndAwaitOffset = [&](double expectedOffsetDb,
                                            const QPair<float, float>& referenceDbm) {
            return QTest::qWaitFor([&] {
                QMetaObject::invokeMethod(&station, "rawIqDataForStream", Qt::DirectConnection,
                    Q_ARG(int, stream), Q_ARG(QVector<float>, iq));
                const float offset = static_cast<float>(expectedOffsetDb);
                const auto levels = traceLevels();
                return std::abs(levels.first - (referenceDbm.first + offset)) < 1.0f
                    && std::abs(levels.second - (referenceDbm.second + offset)) < 1.0f;
            }, 5000);
        };
        const auto baseTraceDbm = traceLevels();
        const double baseOffsetDb = station.rxMeterOffsetDb();
        stationAttenuator.setAttenuation(10);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto stepAttTraceDbm = traceLevels();
        stationAttenuator.setStepAttEnabled(false);
        stationAttenuator.setPreampMode(PreampMode::Off);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOffTraceDbm = traceLevels();
        stationAttenuator.setPreampMode(PreampMode::On);
        QVERIFY(feedAndAwaitOffset(station.rxMeterOffsetDb() - baseOffsetDb, baseTraceDbm));
        const auto preampOnTraceDbm = traceLevels();
        QVERIFY(std::abs(stepAttTraceDbm.first - baseTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOffTraceDbm.first - stepAttTraceDbm.first) > 1.0f);
        QVERIFY(std::abs(preampOnTraceDbm.first - preampOffTraceDbm.first) > 1.0f);
        outbound.clear();
        inbound.clear();
        appSettings.setValue(QStringLiteral("DisplayFftWindow"), QString::number(int(WindowFunction::BlackmanHarris4)));
        QTRY_COMPARE(countControl(outbound, QStringLiteral("subscribe")), 2);
        QCOMPARE(countControl(outbound, QStringLiteral("unsubscribe")), 2);
        // Both old users of the source must retire before either replacement.
        QStringList operations;
        for (const auto& call : outbound) {
            const QString op = call.at(0).toJsonObject().value(QStringLiteral("op")).toString();
            if (op == QStringLiteral("subscribe") || op == QStringLiteral("unsubscribe")) {
                operations.append(op);
            }
        }
        QCOMPARE(operations, QStringList({QStringLiteral("unsubscribe"), QStringLiteral("unsubscribe"),
                                         QStringLiteral("subscribe"), QStringLiteral("subscribe")}));
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        QTRY_COMPARE(countControl(inbound, QStringLiteral("context")), 2);
        QCOMPARE(countControl(inbound, QStringLiteral("rejected")), 0);

        station.setConnectionStateForTest(ConnectionState::Disconnected);
        QTRY_COMPARE(gui.activeEndpointCount(), 0);
        QVERIFY(first->spectrumWidget()->renderedPixels().isEmpty());
        QVERIFY(second->spectrumWidget()->renderedPixels().isEmpty());
        station.setConnectionStateForTest(ConnectionState::Connected);
        QTRY_COMPARE(daemon.activeEndpointCount(), 2);
        QTRY_VERIFY_WITH_TIMEOUT(bothHaveFrames(), 5000);
        client.disconnectFromStation(QStringLiteral("test complete"));
    }

    void contextMediaAndRetirementStayInAuthenticatedSession()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("station.settings")));
        RadioModel station;
        station.setBoardForTest(HPSDRHW::HermesLite);
        station.setConnectionStateForTest(ConnectionState::Connected);
        station.addSlice(QStringLiteral("pan-0"));
        QVERIFY(!station.slices().isEmpty());
        SliceModel* stationSlice = station.slices().first();
        stationSlice->setStreamIndex(0);
        stationSlice->setFrequency(14225000);
        StationServer server(&station, settings, dir.path());
        server.setMediaEnabled(true);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient client(&remote, &proxy);
        PanadapterStack stack;
        auto* applet = stack.addPanadapter(QStringLiteral("pan-0"));
        applet->setActiveSliceIndex(stationSlice->sliceIndex());
        auto* widget = applet->spectrumWidget();
        widget->setDisplayWindowPreservingHistory(14225000, 24000);
        stack.resize(600, 400);
        stack.show();
        QVERIFY(QTest::qWaitForWindowExposed(&stack));
        QPointer<DisplayTransport> media;
        RemoteMediaController controller(&client, &remote, &stack, nullptr,
            [&media](QObject* owner) -> IMediaTransport* {
                media = new DisplayTransport(owner);
                return media;
            });
        QSignalSpy controls(&server, &StationServer::mediaControlReceived);
        QSignalSpy frames(&controller, &RemoteMediaController::displayFrameReceived);
        QVERIFY(!media); // No pre-authentication peer or subscription.
        auto* stationLink = new Test::LoopbackTransport(QStringLiteral("station"));
        auto* clientLink = new Test::LoopbackTransport(QStringLiteral("client"));
        stationLink->linkTo(clientLink);
        client.startSession(clientLink, server.token());
        server.acceptTransport(stationLink);
        QTRY_VERIFY(client.mediaAvailable());
        QTRY_VERIFY(media);
        QTRY_COMPARE(countControl(controls, QStringLiteral("start")), 1);
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), 0);
        media->activate();
        QTRY_COMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        const QJsonObject subscription = lastControl(controls, QStringLiteral("subscribe"));
        const quint32 id = quint32(subscription.value(QStringLiteral("endpointId")).toDouble());
        DisplayCodecFrame frame;
        frame.context = {id, 1, -180, 0, 128, 128, 0};
        frame.traceDbm = QVector<float>(128, -75);
        frame.waterfallDbm = QVector<float>(128, -125);
        frame.waterfallAdvance = true;
        DisplayCodecEncoder encoder;
        const QByteArray packet = encoder.encode(frame);
        QVERIFY(!packet.isEmpty());
        media->deliver(packet); // Media may race its reliable context.
        QCOMPARE(frames.count(), 0);
        QJsonObject context{
            {QStringLiteral("op"), QStringLiteral("context")},
            {QStringLiteral("connectionId"), subscription.value(QStringLiteral("connectionId"))},
            {QStringLiteral("endpointId"), double(id)},
            {QStringLiteral("revision"), subscription.value(QStringLiteral("revision"))},
            {QStringLiteral("contextGeneration"), 1}, {QStringLiteral("sourceStream"), 0},
            {QStringLiteral("sourceCentreHz"), 14225000}, {QStringLiteral("sampleRateHz"), 192000},
            {QStringLiteral("centreHz"), 14225023.4375}, {QStringLiteral("spanHz"), 24046.875},
            {QStringLiteral("wideCentreHz"), 0}, {QStringLiteral("wideSpanHz"), 0},
            {QStringLiteral("traceSamples"), 128}, {QStringLiteral("waterfallSamples"), 128},
            {QStringLiteral("wideSamples"), 0}, {QStringLiteral("minDbm"), -180},
            {QStringLiteral("maxDbm"), 0}, {QStringLiteral("fps"), 30},
            {QStringLiteral("framesPerLine"), 1}};
        QJsonObject invalid = context;
        invalid.insert(QStringLiteral("traceSamples"), 128.5);
        QVERIFY(server.sendMediaControl(invalid, server.mediaSessionEpoch()));
        QTest::qWait(30);
        media->deliver(packet);
        QCOMPARE(frames.count(), 0);
        QVERIFY(server.sendMediaControl(context, server.mediaSessionEpoch()));
        QTRY_COMPARE(countControl(controls, QStringLiteral("keyframe")), 1);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        QCOMPARE(widget->renderedPixels().size(), 128);
        QVERIFY(std::abs(widget->renderedPixels().first() + 75) < 0.4);
        QVERIFY(std::abs(widget->wfRenderedPixels().first() + 125) < 0.4);
        QTest::qWait(250);
        // Bin-aligned accepted geometry must not create a resubscribe loop.
        QCOMPARE(countControl(controls, QStringLiteral("subscribe")), 1);
        applet->hide();
        QTRY_COMPARE(countControl(controls, QStringLiteral("unsubscribe")), 1);
        QCOMPARE(controller.activeEndpointCount(), 0);
        media->deliver(packet);
        QCOMPARE(frames.count(), 1);
        client.disconnectFromStation(QStringLiteral("test complete"));
        QVERIFY(!media || !media->active);
        QVERIFY(widget->renderedPixels().isEmpty());
    }
};
QTEST_MAIN(TestRemoteMediaController)
#include "tst_remote_media_controller.moc"
