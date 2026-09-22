// =================================================================
// src/gui/RemoteAudioStatus.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  The GUI's persistent remote audio
// status (R-R3-23): the status value, the pure state derivation, the rule
// that clears a recorded playback failure, and the plain-English wording
// the Core connection panel and title bar show.  It owns no session,
// device, receiver or timer; RemoteMediaController feeds it.
// =================================================================

#pragma once

#include "core/session/media/OpusAudioCodec.h"
#include "core/session/media/RemoteAudioContext.h"
#include "core/session/media/RemoteAudioReceiver.h"

#include <QString>
#include <QtGlobal>

#include <optional>

namespace NereusSDR {

struct RemoteAudioStatus {
    enum class State {
        NotConnected,     // no remote media session
        WaitingForAudio,  // requested, awaiting Core or the media link
        MutedHere,        // master mute on this computer
        RadioOffline,     // station radio not connected
        CoreCouldNotStart,// Core reported encoder-unavailable
        Starting,         // Core sending, speaker progress not yet seen
        Playing,          // speaker consuming audio now
        Reconnecting,     // interruption, automatic retry scheduled
        PlaybackProblem,   // local output/decoder failure, persists
    };
    State state = State::NotConnected;
    bool detailNegotiated = false;               // Core reports codec detail
    std::optional<OpusEncoderProfile> encoder;   // current accepted context only
    QString selectedOutput;                      // selected speakers device name, "System default" when default
    std::optional<RemoteAudioReceiver::Fault> problem; // persistent local fault
    bool retryAvailable = false;
    friend bool operator==(const RemoteAudioStatus&, const RemoteAudioStatus&) = default;
};

struct RemoteAudioStatusInputs {
    bool mediaSession = false;
    bool muted = false;
    bool radioConnected = false;
    std::optional<RemoteAudioContextMessage> context;
    bool receiverRunning = false;
    bool playing = false;       // running, decoded > 0, device progress younger than 500 ms
    bool restarting = false;
    std::optional<RemoteAudioReceiver::Fault> problem;
};

/// First match wins: no media session, muted here, a persistent local
/// problem, the station radio offline (this GUI's mirror of it, or Core's
/// reason), Core's encoder unavailable, an automatic retry, no enabled
/// context yet, a receiver not yet running, the speaker consuming audio,
/// and otherwise starting.
RemoteAudioStatus::State deriveRemoteAudioState(const RemoteAudioStatusInputs& in);
QString remoteAudioHeadline(RemoteAudioStatus::State state);   // panel status words
QString remoteAudioBannerWord(RemoteAudioStatus::State state); // title bar words
/// The operator's wording for a playback fault. Only the local output and
/// decoder faults become persistent problems; every interruption the
/// receiver recovers from by itself reads "Audio was interrupted."
QString remoteAudioProblemText(RemoteAudioReceiver::Fault fault);
/// Core's reported encoder settings, labelled as a target; "Not reported by
/// this Core" when the detail was not negotiated, and "Audio is off" when it
/// was but the current context carries no encoder.
QString remoteAudioCodecText(const RemoteAudioStatus& status);

/// The identity a persistent playback failure is recorded against: the
/// session (epoch and connection), the accepted audio context it happened
/// in, and the receiver generation that was playing, or trying to play, it.
struct RemoteAudioFailure {
    RemoteAudioReceiver::Fault fault = RemoteAudioReceiver::Fault::SpeakerOpenFailed;
    quint32 epoch = 0;
    QString connectionId;
    quint32 contextGeneration = 0;
    quint64 receiverGeneration = 0;
};

/// Matching recovery, the only thing that clears a recorded failure: in the
/// same epoch and connection, an accepted context newer than the failed one
/// (wrap-aware), played by a running receiver of a later generation whose
/// speaker has consumed audio. Mute, unmute, a device change, a disabled
/// context, a restart or time alone never satisfy it.
bool remoteAudioFailureRecovered(const RemoteAudioFailure& failure, quint32 epoch,
                                 const QString& connectionId,
                                 const std::optional<RemoteAudioContextMessage>& context,
                                 const RemoteAudioReceiverTelemetry& playback);

} // namespace NereusSDR
