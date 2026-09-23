#pragma once
// =================================================================
// tests/fakes/RemoteAudioSessionHarness.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original test fixture.
//
// A real Core and a real remote GUI joined over LoopbackTransport for the
// authenticated control plane. Both media peers use the default
// LibDataChannel transport, so RTP and Opus travel over real DTLS/SRTP.
// The station mixes two panned slices; the GUI plays into a PacedAudioBus
// the test drives.
//
// Moved unchanged out of tst_remote_audio_session.cpp (R-R3-23 Task 3) so
// tst_remote_media_controller can drive the same real session. Both files
// also share the readable printer for the GUI's remote audio state.
//
// R-R3-23 lossless: hideAudioProfile makes the Core look like one from
// before the lossless profile (its capabilities carry no
// audioProfileVersion), so tests can prove such a Core sees exactly the
// behaviour it did.
//
// =================================================================

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/HpsdrModel.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationClient.h"
#include "core/session/StationCapabilities.h"
#include "core/session/StationServer.h"
#include "core/settings/SettingsProxy.h"
#include "gui/RemoteAudioStatus.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "LoopbackTransport.h"
#include "PacedAudioBus.h"

#include <QJsonObject>
#include <QPointer>
#include <QTemporaryDir>
#include <QTest>
#include <QVector>

#include <cmath>
#include <functional>
#include <memory>
#include <optional>
#include <utility>

namespace NereusSDR {

// Readable QCOMPARE failures for the remote audio state (found by ADL).
inline char* toString(RemoteAudioStatus::State state)
{
    const char* name = "unknown";
    switch (state) {
    case RemoteAudioStatus::State::NotConnected: name = "NotConnected"; break;
    case RemoteAudioStatus::State::WaitingForAudio: name = "WaitingForAudio"; break;
    case RemoteAudioStatus::State::MutedHere: name = "MutedHere"; break;
    case RemoteAudioStatus::State::RadioOffline: name = "RadioOffline"; break;
    case RemoteAudioStatus::State::CoreCouldNotStart: name = "CoreCouldNotStart"; break;
    case RemoteAudioStatus::State::Starting: name = "Starting"; break;
    case RemoteAudioStatus::State::Playing: name = "Playing"; break;
    case RemoteAudioStatus::State::Reconnecting: name = "Reconnecting"; break;
    case RemoteAudioStatus::State::PlaybackProblem: name = "PlaybackProblem"; break;
    }
    return qstrdup(name);
}

} // namespace NereusSDR

namespace NereusSDR::Test {

// Test-only control link, so compatibility can be exercised without any
// production test hook. With a hello minor set on both ends, a current Core
// and a current GUI each believe the other speaks that minor. A one-shot
// forge puts a forged copy of the next audio context on the wire just
// ahead of the real one.
class RewritingTransport final : public LoopbackTransport {
public:
    using Forge = std::function<QJsonObject(const QJsonObject& real)>;

    RewritingTransport(const QString& description, std::optional<quint16> helloMinor)
        : LoopbackTransport(description), m_helloMinor(helloMinor) {}

    void sendText(const QByteArray& wire) override
    {
        const bool mayRewrite = (m_helloMinor && wire.contains("\"hello\""))
            || (forgeNextAudioContext && wire.contains("\"audio-context\""))
            || (hideAudioProfile && wire.contains("\"capabilities\""));
        SessionMessage message;
        if (mayRewrite && SessionMessages::decode(wire, &message)) {
            if (hideAudioProfile && message.kind == SessionMessageKind::Capabilities) {
                StationCapabilities capabilities = StationCapabilities::fromUpdates(message.updates);
                capabilities.audioProfileVersion = 0;
                ++hiddenAudioProfiles;
                LoopbackTransport::sendText(SessionMessages::encode(
                    SessionMessages::capabilities(capabilities.toUpdates())));
                return;
            }
            if (m_helloMinor && message.kind == SessionMessageKind::Hello) {
                ++rewrittenHellos;
                LoopbackTransport::sendText(SessionMessages::encode(SessionMessages::hello(
                    message.protocolMajor, *m_helloMinor, message.settingsSchemaVersion,
                    message.peerName)));
                return;
            }
            if (forgeNextAudioContext && message.kind == SessionMessageKind::MediaControl
                && message.mediaPayload.value(QStringLiteral("op"))
                    == QLatin1String("audio-context")) {
                SessionMessage forged = message;
                forged.mediaPayload = std::exchange(forgeNextAudioContext, {})(
                    message.mediaPayload);
                ++forgedContexts;
                LoopbackTransport::sendText(SessionMessages::encode(forged));
            }
        }
        LoopbackTransport::sendText(wire);
    }

    int rewrittenHellos = 0;
    int forgedContexts = 0;
    int hiddenAudioProfiles = 0;
    Forge forgeNextAudioContext;
    bool hideAudioProfile = false;

private:
    std::optional<quint16> m_helloMinor;
};

struct RemoteAudioSessionHarness {
    static constexpr int kFrames = 480;
    static constexpr double kPi = 3.14159265358979323846;

    QTemporaryDir directory;
    AppSettings settings;
    RadioModel station;
    StationServer server;
    RadioModel remote{RadioModel::Role::Remote};
    SettingsProxy settingsProxy;
    StationClient client{&remote, &settingsProxy};
    AudioEngine* stationAudio{nullptr};
    PacedAudioBus* remoteBus{nullptr};
    int sliceA{-1};
    int sliceB{-1};
    qint64 stationFrames{0};

    RemoteAudioSessionHarness()
        : settings(directory.filePath(QStringLiteral("station.settings")))
        , server(&station, settings, directory.path())
    {
        Q_ASSERT(directory.isValid());
        station.setBoardForTest(HPSDRHW::Saturn);
        station.configureStreamPool(/*userDdcCount=*/5, /*maxSlices=*/5,
                                    /*defaultRateHz=*/192000);
        station.setConnectionStateForTest(ConnectionState::Connected);
        stationAudio = station.audioEngine();
        Q_ASSERT(stationAudio != nullptr);
        stationAudio->masterMixForTest().setRampFrames(1);
        stationAudio->masterMixForTest().setSlewUpFrames(0);
        sliceA = station.addSlice();
        sliceB = station.addSlice();
        Q_ASSERT(sliceA >= 0 && sliceB >= 0);
        stationAudio->setSliceStreaming(sliceA, true);
        stationAudio->setSliceStreaming(sliceB, true);
        stationAudio->masterMixForTest().setSliceGain(sliceA, 0.60f, -0.95f);
        stationAudio->masterMixForTest().setSliceGain(sliceB, 0.45f, 0.95f);
        station.sliceById(sliceA)->setAudioPan(-0.95);
        station.sliceById(sliceB)->setAudioPan(0.95);
        server.setMediaEnabled(true);

        remote.setConnectionStateForTest(ConnectionState::Connected);
        auto bus = std::make_unique<PacedAudioBus>();
        remoteBus = bus.get();
        remote.audioEngine()->setSpeakersBusForTest(std::move(bus));
    }

    // helloMinor, when set, is what both ends announce, so both negotiate
    // down to it. forgeFirstContext, when set, puts one forged copy on the
    // wire ahead of Core's first audio context.
    void connectSession(std::optional<quint16> helloMinor = std::nullopt,
                        RewritingTransport::Forge forgeFirstContext = {})
    {
        auto* station = new RewritingTransport(QStringLiteral("station"), helloMinor);
        auto* clientEnd = new RewritingTransport(QStringLiteral("client"), helloMinor);
        station->forgeNextAudioContext = std::move(forgeFirstContext);
        station->hideAudioProfile = hideAudioProfile;
        stationLink = station;
        station->linkTo(clientEnd);
        client.startSession(clientEnd, server.token());
        server.acceptTransport(station);
        QTRY_VERIFY(server.mediaAvailable());
        if (helloMinor) {
            QCOMPARE(station->rewrittenHellos, 1);
            QCOMPARE(clientEnd->rewrittenHellos, 1);
            QCOMPARE(client.agreedMinor(), *helloMinor);
        }
        if (hideAudioProfile) {
            // On a reconnect the server can still report media from the
            // session being replaced; wait for this link's capabilities.
            QTRY_VERIFY(station->hiddenAudioProfiles >= 1 && client.isHandshakeComplete());
            QCOMPARE(client.capabilities().audioProfileVersion, 0);
        }
    }

    // Set before connectSession(): the Core appears to predate lossless.
    bool hideAudioProfile = false;

    QPointer<RewritingTransport> stationLink;

    void feedMixedTone()
    {
        QVector<float> a(kFrames * 2);
        QVector<float> b(kFrames * 2);
        for (int frame = 0; frame < kFrames; ++frame) {
            const double time = static_cast<double>(stationFrames + frame) / 48000.0;
            const float first = static_cast<float>(0.22 * std::sin(2.0 * kPi * 617.0 * time));
            const float second = static_cast<float>(0.19 * std::sin(2.0 * kPi * 1579.0 * time));
            a[frame * 2] = a[frame * 2 + 1] = first;
            b[frame * 2] = b[frame * 2 + 1] = second;
        }
        stationFrames += kFrames;
        stationAudio->rxBlockReady(sliceA, a.constData(), kFrames);
        stationAudio->rxBlockReady(sliceB, b.constData(), kFrames);
    }
};

} // namespace NereusSDR::Test
