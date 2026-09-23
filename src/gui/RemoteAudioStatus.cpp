// =================================================================
// src/gui/RemoteAudioStatus.cpp  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  See RemoteAudioStatus.h.
// =================================================================

#include "gui/RemoteAudioStatus.h"
#include "gui/RemoteGeneration.h"

#include <QStringList>

namespace NereusSDR {

RemoteAudioStatus::State deriveRemoteAudioState(const RemoteAudioStatusInputs& in)
{
    using State = RemoteAudioStatus::State;
    if (!in.mediaSession) {
        return State::NotConnected;
    }
    if (in.muted) {
        return State::MutedHere;
    }
    if (in.problem) {
        return State::PlaybackProblem;
    }
    const std::optional<RemoteAudioOffReason> reason =
        in.context ? in.context->offReason : std::nullopt;
    // Core may say radio-offline before this GUI mirrors the radio. Once it
    // does, this GUI withdraws its own request and Core answers
    // client-disabled, so the mirror has to count as well as the reason.
    if (!in.radioConnected || reason == RemoteAudioOffReason::RadioOffline) {
        return State::RadioOffline;
    }
    if (in.context && !in.context->enabled
        && reason == RemoteAudioOffReason::EncoderUnavailable) {
        return State::CoreCouldNotStart;
    }
    if (in.restarting) {
        return State::Reconnecting;
    }
    if (!in.context || !in.context->enabled) {
        return State::WaitingForAudio;
    }
    if (!in.receiverRunning) {
        return State::WaitingForAudio;
    }
    if (in.playing) {
        return State::Playing;
    }
    return State::Starting;
}

QString remoteAudioHeadline(RemoteAudioStatus::State state)
{
    using State = RemoteAudioStatus::State;
    switch (state) {
    case State::NotConnected:
        return QStringLiteral("Not connected to a Core");
    case State::WaitingForAudio:
        return QStringLiteral("Waiting for audio from Core");
    case State::MutedHere:
        return QStringLiteral("Muted on this computer");
    case State::RadioOffline:
        return QStringLiteral("Radio offline at the station");
    case State::CoreCouldNotStart:
        return QStringLiteral("Core could not start audio");
    case State::Starting:
        return QStringLiteral("Starting audio");
    case State::Playing:
        return QStringLiteral("Playing");
    case State::Reconnecting:
        return QStringLiteral("Audio interrupted, reconnecting");
    case State::PlaybackProblem:
        return QStringLiteral("Playback problem on this computer");
    }
    return {};
}

QString remoteAudioBannerWord(RemoteAudioStatus::State state)
{
    using State = RemoteAudioStatus::State;
    switch (state) {
    case State::Playing:
        return QStringLiteral("Audio playing");
    case State::MutedHere:
        return QStringLiteral("Audio muted");
    case State::RadioOffline:
        return QStringLiteral("Radio offline");
    case State::CoreCouldNotStart:
    case State::PlaybackProblem:
        return QStringLiteral("Audio unavailable");
    case State::WaitingForAudio:
    case State::Starting:
    case State::Reconnecting:
        return QStringLiteral("Audio waiting");
    case State::NotConnected:
        return QStringLiteral("Audio stopped");
    }
    return {};
}

QString remoteAudioProblemText(RemoteAudioReceiver::Fault fault)
{
    using Fault = RemoteAudioReceiver::Fault;
    switch (fault) {
    case Fault::SpeakerOpenFailed:
        return QStringLiteral("The selected speaker device could not be opened.");
    case Fault::SpeakerTimingUnavailable:
        return QStringLiteral("The speaker device stopped reporting its timing.");
    case Fault::SpeakerCallbackTooLarge:
        return QStringLiteral("The speaker device buffer is larger than remote playback "
                              "supports. Choose a smaller buffer or another device.");
    case Fault::SpeakerStalled:
        return QStringLiteral("The speaker device stopped playing audio.");
    case Fault::SpeakerWriteFailed:
        return QStringLiteral("Audio could not be sent to the speaker device.");
    case Fault::DecoderUnavailable:
        return QStringLiteral("The audio decoder could not start on this computer.");
    case Fault::ArrivalBurst:
    case Fault::StreamGap:
    case Fault::NoPackets:
    case Fault::DecodeFailed:
    case Fault::ClockBuffer:
        // The receiver restarts itself after these; none of them persists.
        return QStringLiteral("Audio was interrupted.");
    }
    return {};
}

QString remoteAudioCodecText(const RemoteAudioStatus& status)
{
    if (!status.detailNegotiated) {
        return QStringLiteral("Not reported by this Core");
    }
    if (!status.encoder) {
        return QStringLiteral("Audio is off");
    }
    const OpusEncoderProfile& profile = *status.encoder;
    QString channels;
    if (profile.channels == 2) {
        channels = QStringLiteral("stereo");
    } else if (profile.channels == 1) {
        channels = QStringLiteral("mono");
    } else {
        channels = QStringLiteral("%1 channels").arg(profile.channels);
    }
    const qint64 packetMs = profile.sampleRate > 0
        ? qint64(profile.frameSamples) * 1000 / profile.sampleRate : 0;
    // A target, not measured traffic: constrained VBR spends less on quiet audio.
    return QStringLiteral("Opus %1, %2 kbit/s target, %3 ms packets, audio up to %4 kHz")
        .arg(channels)
        .arg(profile.targetBitrate / 1000)
        .arg(packetMs)
        .arg(profile.audioBandwidthHz / 1000);
}

QString formatRemoteAudioDetails(const RemoteAudioStatus& status,
                                 const RemoteAudioReceiverTelemetry& playback)
{
    using State = RemoteAudioStatus::State;
    QStringList lines;
    lines << QStringLiteral("Remote audio: %1").arg(remoteAudioHeadline(status.state));
    if (status.problem) {
        lines << QStringLiteral("Problem: %1").arg(remoteAudioProblemText(*status.problem));
    }
    lines << QStringLiteral("Codec: %1").arg(remoteAudioCodecText(status));
    lines << QStringLiteral("Output: %1 (selected)").arg(status.selectedOutput);

    const bool showHealth = status.state != State::NotConnected
        && status.state != State::MutedHere && status.state != State::RadioOffline;
    if (showHealth) {
        lines << (playback.arrivalJitterMs
            ? QStringLiteral("Arrival jitter: %1 ms").arg(qRound(*playback.arrivalJitterMs))
            : QStringLiteral("Arrival jitter: not measured yet"));
        lines << (playback.expectedPackets > 0
            ? QStringLiteral("Missing packets: %1 of %2")
                  .arg(playback.missingPackets).arg(playback.expectedPackets)
            : QStringLiteral("Missing packets: none received yet"));
        lines << QStringLiteral("Gaps filled: %1").arg(playback.concealedPackets);
        lines << (playback.speakerQueuedMs
            ? QStringLiteral("Speaker buffer: %1 ms on this computer")
                  .arg(qRound(*playback.speakerQueuedMs))
            : QStringLiteral("Speaker buffer: not measured yet"));
    }
    return lines.join(QLatin1Char('\n'));
}

bool remoteAudioFailureRecovered(const RemoteAudioFailure& failure, quint32 epoch,
                                 const QString& connectionId,
                                 const std::optional<RemoteAudioContextMessage>& context,
                                 const RemoteAudioReceiverTelemetry& playback)
{
    return failure.epoch == epoch && failure.connectionId == connectionId
        && context && isNewerGeneration(context->generation, failure.contextGeneration)
        && playback.running && playback.generation > failure.receiverGeneration
        && playback.deviceConsumedFrames > 0;
}

} // namespace NereusSDR
