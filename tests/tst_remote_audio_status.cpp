// =================================================================
// tests/tst_remote_audio_status.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-23: the remote audio status
// derivation table, the failure-clearing rule, and the exact wording the
// Core connection panel and title bar show.
//
// =================================================================

#include <QtTest>

#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include "gui/RemoteAudioStatus.h"

#include <QRegularExpression>

#include <limits>
#include <optional>

using namespace NereusSDR;

namespace NereusSDR {

// Readable QCOMPARE failures for the state (found by ADL).
char* toString(RemoteAudioStatus::State state)
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

namespace {

using State = RemoteAudioStatus::State;
using Fault = RemoteAudioReceiver::Fault;

// What a default Core announces: 48 kHz stereo, 40 ms, 24 kbit/s, 8 kHz.
OpusEncoderProfile defaultProfile()
{
    OpusEncoderProfile profile;
    profile.sampleRate = 48'000;
    profile.channels = 2;
    profile.frameSamples = 1'920;
    profile.targetBitrate = 24'000;
    profile.audioBandwidthHz = 8'000;
    return profile;
}

enum class ContextKind {
    None,             // nothing accepted yet
    Enabled,          // minor 8, with the encoder
    EnabledLegacy,    // minor 7, no encoder
    DisabledLegacy,   // minor 7, no reason
    ClientDisabled,
    MediaNotReady,
    RadioOffline,
    EncoderUnavailable,
};

std::optional<RemoteAudioContextMessage> contextOf(ContextKind kind)
{
    if (kind == ContextKind::None) {
        return std::nullopt;
    }
    RemoteAudioContextMessage context;
    context.connectionId = QStringLiteral("00000000-0000-4000-8000-000000000001");
    context.revision = 3;
    context.generation = 7;
    context.ssrc = 0x1234'5678;
    context.enabled = kind == ContextKind::Enabled || kind == ContextKind::EnabledLegacy;
    if (kind == ContextKind::Enabled) {
        context.encoder = defaultProfile();
    }
    switch (kind) {
    case ContextKind::ClientDisabled:
        context.offReason = RemoteAudioOffReason::ClientDisabled;
        break;
    case ContextKind::MediaNotReady:
        context.offReason = RemoteAudioOffReason::MediaNotReady;
        break;
    case ContextKind::RadioOffline:
        context.offReason = RemoteAudioOffReason::RadioOffline;
        break;
    case ContextKind::EncoderUnavailable:
        context.offReason = RemoteAudioOffReason::EncoderUnavailable;
        break;
    default:
        break;
    }
    return context;
}

// The receiver faults that only restart playback, never persist.
const QList<Fault> kInterruptionFaults{Fault::ArrivalBurst, Fault::StreamGap, Fault::NoPackets,
                                       Fault::DecodeFailed, Fault::ClockBuffer};

const QList<State> kAllStates{State::NotConnected, State::WaitingForAudio, State::MutedHere,
                              State::RadioOffline, State::CoreCouldNotStart, State::Starting,
                              State::Playing, State::Reconnecting, State::PlaybackProblem};

const QList<Fault> kAllFaults{Fault::SpeakerOpenFailed, Fault::SpeakerTimingUnavailable,
                              Fault::SpeakerCallbackTooLarge, Fault::SpeakerStalled,
                              Fault::SpeakerWriteFailed, Fault::DecoderUnavailable,
                              Fault::ArrivalBurst, Fault::StreamGap, Fault::NoPackets,
                              Fault::DecodeFailed, Fault::ClockBuffer};

// A failure recorded in session epoch 4, connection A, context 10, while
// receiver generation 5 was playing.
RemoteAudioFailure recordedFailure()
{
    RemoteAudioFailure failure;
    failure.fault = Fault::SpeakerStalled;
    failure.epoch = 4;
    failure.connectionId = QStringLiteral("00000000-0000-4000-8000-00000000000a");
    failure.contextGeneration = 10;
    failure.receiverGeneration = 5;
    return failure;
}

// Everything matching recovery needs: a newer enabled context of the same
// session played by a newer running receiver whose speaker consumed audio.
struct Recovery {
    quint32 epoch = 4;
    QString connectionId = QStringLiteral("00000000-0000-4000-8000-00000000000a");
    std::optional<RemoteAudioContextMessage> context;
    RemoteAudioReceiverTelemetry playback;

    Recovery()
    {
        RemoteAudioContextMessage accepted;
        accepted.connectionId = connectionId;
        accepted.revision = 9;
        accepted.generation = 11;
        accepted.enabled = true;
        accepted.ssrc = 0x1234'5678;
        accepted.encoder = defaultProfile();
        context = accepted;
        playback.generation = 6;
        playback.running = true;
        playback.decodedPackets = 3;
        playback.deviceConsumedFrames = 480;
    }

    bool recovered(const RemoteAudioFailure& failure = recordedFailure()) const
    {
        return remoteAudioFailureRecovered(failure, epoch, connectionId, context, playback);
    }
};

} // namespace

class TstRemoteAudioStatus final : public QObject {
    Q_OBJECT

private slots:
    void derivationFollowsThePrecedenceTable_data()
    {
        QTest::addColumn<bool>("mediaSession");
        QTest::addColumn<bool>("muted");
        QTest::addColumn<bool>("radioConnected");
        QTest::addColumn<ContextKind>("context");
        QTest::addColumn<bool>("receiverRunning");
        QTest::addColumn<bool>("playing");
        QTest::addColumn<bool>("restarting");
        QTest::addColumn<bool>("problem");
        QTest::addColumn<State>("expected");

        // mediaSession, muted, radioConnected, context, running, playing,
        // restarting, problem -> state
        QTest::newRow("no media session, idle")
            << false << false << false << ContextKind::None
            << false << false << false << false << State::NotConnected;
        QTest::newRow("no media session outranks every other input")
            << false << true << false << ContextKind::Enabled
            << true << true << true << true << State::NotConnected;
        QTest::newRow("muted outranks a problem and the radio offline")
            << true << true << false << ContextKind::RadioOffline
            << false << false << true << true << State::MutedHere;
        QTest::newRow("muted while playing")
            << true << true << true << ContextKind::Enabled
            << true << true << false << false << State::MutedHere;
        QTest::newRow("problem outranks the radio offline")
            << true << false << false << ContextKind::RadioOffline
            << false << false << false << true << State::PlaybackProblem;
        QTest::newRow("problem outranks encoder unavailable and a restart")
            << true << false << true << ContextKind::EncoderUnavailable
            << false << false << true << true << State::PlaybackProblem;
        QTest::newRow("problem persists while a newer context plays")
            << true << false << true << ContextKind::Enabled
            << true << true << false << true << State::PlaybackProblem;
        QTest::newRow("radio not connected in this GUI")
            << true << false << false << ContextKind::None
            << false << false << false << false << State::RadioOffline;
        QTest::newRow("Core reports radio-offline before this GUI's mirror")
            << true << false << true << ContextKind::RadioOffline
            << false << false << false << false << State::RadioOffline;
        QTest::newRow("client-disabled reply once this GUI's mirror is offline")
            << true << false << false << ContextKind::ClientDisabled
            << false << false << false << false << State::RadioOffline;
        QTest::newRow("radio offline outranks encoder unavailable and a restart")
            << true << false << false << ContextKind::EncoderUnavailable
            << false << false << true << false << State::RadioOffline;
        QTest::newRow("encoder unavailable")
            << true << false << true << ContextKind::EncoderUnavailable
            << false << false << false << false << State::CoreCouldNotStart;
        QTest::newRow("encoder unavailable outranks a restart")
            << true << false << true << ContextKind::EncoderUnavailable
            << false << false << true << false << State::CoreCouldNotStart;
        QTest::newRow("restart scheduled")
            << true << false << true << ContextKind::Enabled
            << false << false << true << false << State::Reconnecting;
        QTest::newRow("restart outranks a disabled context")
            << true << false << true << ContextKind::ClientDisabled
            << false << false << true << false << State::Reconnecting;
        QTest::newRow("restart outranks no context")
            << true << false << true << ContextKind::None
            << false << false << true << false << State::Reconnecting;
        QTest::newRow("no context yet")
            << true << false << true << ContextKind::None
            << false << false << false << false << State::WaitingForAudio;
        QTest::newRow("client-disabled context")
            << true << false << true << ContextKind::ClientDisabled
            << false << false << false << false << State::WaitingForAudio;
        QTest::newRow("media-not-ready context")
            << true << false << true << ContextKind::MediaNotReady
            << false << false << false << false << State::WaitingForAudio;
        QTest::newRow("minor-7 disabled context without a reason")
            << true << false << true << ContextKind::DisabledLegacy
            << false << false << false << false << State::WaitingForAudio;
        QTest::newRow("enabled context, receiver not running")
            << true << false << true << ContextKind::Enabled
            << false << false << false << false << State::WaitingForAudio;
        QTest::newRow("enabled context, playing flag without a running receiver")
            << true << false << true << ContextKind::Enabled
            << false << true << false << false << State::WaitingForAudio;
        QTest::newRow("playing")
            << true << false << true << ContextKind::Enabled
            << true << true << false << false << State::Playing;
        QTest::newRow("minor-7 context playing")
            << true << false << true << ContextKind::EnabledLegacy
            << true << true << false << false << State::Playing;
        QTest::newRow("running, speaker progress not yet seen")
            << true << false << true << ContextKind::Enabled
            << true << false << false << false << State::Starting;
    }

    void derivationFollowsThePrecedenceTable()
    {
        QFETCH(bool, mediaSession);
        QFETCH(bool, muted);
        QFETCH(bool, radioConnected);
        QFETCH(ContextKind, context);
        QFETCH(bool, receiverRunning);
        QFETCH(bool, playing);
        QFETCH(bool, restarting);
        QFETCH(bool, problem);
        QFETCH(State, expected);

        RemoteAudioStatusInputs inputs;
        inputs.mediaSession = mediaSession;
        inputs.muted = muted;
        inputs.radioConnected = radioConnected;
        inputs.context = contextOf(context);
        inputs.receiverRunning = receiverRunning;
        inputs.playing = playing;
        inputs.restarting = restarting;
        if (problem) {
            inputs.problem = Fault::SpeakerStalled;
        }
        QCOMPARE(deriveRemoteAudioState(inputs), expected);
    }

    void headlinesAndBannerWordsAreExact()
    {
        const QList<std::tuple<State, QString, QString>> expected{
            {State::NotConnected, QStringLiteral("Not connected to a Core"),
             QStringLiteral("Audio stopped")},
            {State::WaitingForAudio, QStringLiteral("Waiting for audio from Core"),
             QStringLiteral("Audio waiting")},
            {State::MutedHere, QStringLiteral("Muted on this computer"),
             QStringLiteral("Audio muted")},
            {State::RadioOffline, QStringLiteral("Radio offline at the station"),
             QStringLiteral("Radio offline")},
            {State::CoreCouldNotStart, QStringLiteral("Core could not start audio"),
             QStringLiteral("Audio unavailable")},
            {State::Starting, QStringLiteral("Starting audio"),
             QStringLiteral("Audio waiting")},
            {State::Playing, QStringLiteral("Playing"), QStringLiteral("Audio playing")},
            {State::Reconnecting, QStringLiteral("Audio interrupted, reconnecting"),
             QStringLiteral("Audio waiting")},
            {State::PlaybackProblem, QStringLiteral("Playback problem on this computer"),
             QStringLiteral("Audio unavailable")},
        };
        QCOMPARE(expected.size(), kAllStates.size());
        for (const auto& [state, headline, banner] : expected) {
            QCOMPARE(remoteAudioHeadline(state), headline);
            QCOMPARE(remoteAudioBannerWord(state), banner);
        }
    }

    void problemTextsArePlainEnglish()
    {
        QCOMPARE(remoteAudioProblemText(Fault::SpeakerOpenFailed),
                 QStringLiteral("The selected speaker device could not be opened."));
        QCOMPARE(remoteAudioProblemText(Fault::SpeakerTimingUnavailable),
                 QStringLiteral("The speaker device stopped reporting its timing."));
        QCOMPARE(remoteAudioProblemText(Fault::SpeakerCallbackTooLarge),
                 QStringLiteral("The speaker device buffer is larger than remote playback "
                                "supports. Choose a smaller buffer or another device."));
        QCOMPARE(remoteAudioProblemText(Fault::SpeakerStalled),
                 QStringLiteral("The speaker device stopped playing audio."));
        QCOMPARE(remoteAudioProblemText(Fault::SpeakerWriteFailed),
                 QStringLiteral("Audio could not be sent to the speaker device."));
        QCOMPARE(remoteAudioProblemText(Fault::DecoderUnavailable),
                 QStringLiteral("The audio decoder could not start on this computer."));
        // The receiver recovers from these by itself; they never persist.
        for (Fault fault : kInterruptionFaults) {
            QCOMPARE(remoteAudioProblemText(fault), QStringLiteral("Audio was interrupted."));
        }
    }

    void codecTextNamesWhatCoreReported()
    {
        RemoteAudioStatus status;
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Not reported by this Core"));
        status.detailNegotiated = true;
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Audio is off"));

        status.encoder = defaultProfile();
        QCOMPARE(remoteAudioCodecText(status),
                 QStringLiteral("Opus stereo, 24 kbit/s target, 40 ms packets, audio up to 8 kHz"));

        // Every number comes from the reported profile, never an assumed one.
        OpusEncoderProfile other = defaultProfile();
        other.targetBitrate = 48'000;
        other.audioBandwidthHz = 12'000;
        other.frameSamples = 960;
        status.encoder = other;
        QCOMPARE(remoteAudioCodecText(status),
                 QStringLiteral("Opus stereo, 48 kbit/s target, 20 ms packets, audio up to 12 kHz"));
        other = defaultProfile();
        other.channels = 1;
        other.audioBandwidthHz = 20'000;
        status.encoder = other;
        QCOMPARE(remoteAudioCodecText(status),
                 QStringLiteral("Opus mono, 24 kbit/s target, 40 ms packets, audio up to 20 kHz"));

        // A minor-7 Core cannot report one, whatever the value holds.
        status.detailNegotiated = false;
        QCOMPARE(remoteAudioCodecText(status), QStringLiteral("Not reported by this Core"));
    }

    void operatorWordingCarriesNoProtocolTerms()
    {
        static const QRegularExpression forbidden(
            QStringLiteral("\\[|\\]|\\bRTP\\b|SSRC|generation|epoch|revision|context|R-R3|"
                           "\\bphase\\b|\\bminor\\b"),
            QRegularExpression::CaseInsensitiveOption);
        QStringList words;
        for (State state : kAllStates) {
            words << remoteAudioHeadline(state) << remoteAudioBannerWord(state);
        }
        for (Fault fault : kAllFaults) {
            words << remoteAudioProblemText(fault);
        }
        RemoteAudioStatus status;
        words << remoteAudioCodecText(status);
        status.detailNegotiated = true;
        words << remoteAudioCodecText(status);
        status.encoder = defaultProfile();
        words << remoteAudioCodecText(status);
        for (const QString& text : words) {
            QVERIFY2(!text.isEmpty(), "every state and fault has operator wording");
            QVERIFY2(!forbidden.match(text).hasMatch(), qPrintable(text));
        }
    }

    void statusValueComparesEveryField()
    {
        const RemoteAudioStatus base;
        QVERIFY(base == RemoteAudioStatus{});
        RemoteAudioStatus changed = base;
        changed.state = State::Playing;
        QVERIFY(!(changed == base));
        changed = base;
        changed.detailNegotiated = true;
        QVERIFY(!(changed == base));
        changed = base;
        changed.encoder = defaultProfile();
        QVERIFY(!(changed == base));
        changed = base;
        changed.selectedOutput = QStringLiteral("System default");
        QVERIFY(!(changed == base));
        changed = base;
        changed.problem = Fault::SpeakerStalled;
        QVERIFY(!(changed == base));
        changed = base;
        changed.retryAvailable = true;
        QVERIFY(!(changed == base));
    }

    void failureClearsOnlyOnMatchingRecovery()
    {
        // Every condition met: cleared.
        QVERIFY(Recovery().recovered());

        // Another session never clears it.
        Recovery otherEpoch;
        otherEpoch.epoch = 5;
        QVERIFY(!otherEpoch.recovered());
        Recovery otherConnection;
        otherConnection.connectionId = QStringLiteral("00000000-0000-4000-8000-00000000000b");
        QVERIFY(!otherConnection.recovered());

        // No accepted context, the failed context itself, or an older one.
        Recovery noContext;
        noContext.context.reset();
        QVERIFY(!noContext.recovered());
        Recovery sameContext;
        sameContext.context->generation = 10;
        QVERIFY(!sameContext.recovered());
        Recovery olderContext;
        olderContext.context->generation = 9;
        QVERIFY(!olderContext.recovered());

        // Time alone, or a receiver that is not running: not cleared.
        Recovery notRunning;
        notRunning.playback.running = false;
        QVERIFY(!notRunning.recovered());

        // The failed receiver generation, or an older one, cannot clear it.
        Recovery sameReceiver;
        sameReceiver.playback.generation = 5;
        QVERIFY(!sameReceiver.recovered());
        Recovery olderReceiver;
        olderReceiver.playback.generation = 4;
        QVERIFY(!olderReceiver.recovered());

        // A newer context and receiver whose speaker has consumed nothing.
        Recovery noProgress;
        noProgress.playback.deviceConsumedFrames = 0;
        QVERIFY(!noProgress.recovered());
    }

    void failureClearingIsWrapAware()
    {
        RemoteAudioFailure nearWrap = recordedFailure();
        nearWrap.contextGeneration = std::numeric_limits<quint32>::max();
        Recovery wrapped;
        wrapped.context->generation = 1; // two contexts later, past the wrap
        QVERIFY(wrapped.recovered(nearWrap));

        // Half the ring back is older, not newer, however large it looks.
        RemoteAudioFailure early = recordedFailure();
        early.contextGeneration = 10;
        Recovery farAhead;
        farAhead.context->generation = 10u + 0x8000'0000u;
        QVERIFY(!farAhead.recovered(early));
        farAhead.context->generation = 10u + 0x7FFF'FFFFu;
        QVERIFY(farAhead.recovered(early));
    }
};

QTEST_GUILESS_MAIN(TstRemoteAudioStatus)
#include "tst_remote_audio_status.moc"
