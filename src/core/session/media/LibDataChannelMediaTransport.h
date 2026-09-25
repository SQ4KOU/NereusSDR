#pragma once
// =================================================================
// src/core/session/media/LibDataChannelMediaTransport.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// libdatachannel v0.24.5 adapter for one direct DTLS/SCTP and SRTP peer.
// The implementation hides all rtc types so the dependency stays private
// to NereusCore.
//
// =================================================================

#include "core/session/media/IMediaTransport.h"

#include <memory>
#include <optional>

namespace NereusSDR {

/// Applies the IMediaTransport::kSctp* buffer sizes to libdatachannel's
/// process-wide SCTP settings. Only the first call in a process applies them
/// and returns true. LibDataChannelMediaTransport::start() calls it before it
/// creates any peer, so the daemon and the GUI both get the limits before
/// their first media peer; calling it earlier is harmless.
bool applyMediaSctpSettingsOnce();

/// What applyMediaSctpSettingsOnce() did in this process: how many times the
/// settings were applied (0 or 1) and how many peers existed at that moment.
struct MediaSctpSettingsRecord {
    int applications = 0;
    quint64 peersCreatedBeforeApplication = 0;
    quint64 peersCreated = 0;
};
MediaSctpSettingsRecord mediaSctpSettingsRecord();

/// The Opus a=fmtp parameters a send-only offer carries for an encoder
/// running at `targetBitrate` bit/s (R-R3-23). Under RFC 7587 section 6.1
/// only sprop-stereo describes what the offer's author sends (stereo);
/// stereo, useinbandfec, maxaveragebitrate and minptime describe what the
/// author prefers to receive. The Core receives no audio, so those mirror
/// its encoder rather than claim more: stereo, 10 ms minimum packet time,
/// useinbandfec left at its default of 0 (the encoder has FEC off) and an
/// average bitrate equal to the configured target. The encoder is reported
/// by the minor-8 audio context. Revisit when the m-line becomes sendrecv
/// (TX audio, R4).
QString opusOfferFormatParameters(int targetBitrate);

/// iPhone app plan Task 36 (R-IOS-13): the Opus a=fmtp parameters of the
/// microphone line, which the Core receives: mono 48 kHz, in-band FEC, a
/// 24 kbit/s average, 10 ms minimum packet time (RFC 7587 section 6.1: the
/// Core's receive preferences, matching the app's microphone encoder).
QString micLineOpusFormatParameters();

class LibDataChannelMediaTransport final : public IMediaTransport {
    Q_OBJECT

public:
    enum class CandidatePolicy {
        HostOnly,
        AnyIceType,
    };

    explicit LibDataChannelMediaTransport(
        QObject* parent = nullptr,
        CandidatePolicy candidatePolicy = CandidatePolicy::HostOnly);
    ~LibDataChannelMediaTransport() override;

    bool start(const StartOptions& options) override;
    void stop() override;

    bool acceptDescription(const QString& sdp, const QString& type) override;
    bool acceptCandidate(const QString& candidate, const QString& mid) override;

    bool sendDisplay(const QByteArray& message) override;
    DisplaySendResult submitDisplay(const QByteArray& message) override;
    bool displayBusy() const override;
    bool sendRtp(const QByteArray& packet) override;
    bool sendMicRtp(const QByteArray& packet) override;

    bool isReady() const override;
    bool losslessAudioNegotiated() const override;
    bool micLosslessNegotiated() const override;
    std::optional<MediaTransportTelemetry> telemetry() const override;

    /// Test seam: while stalled, the library thread that delivers received
    /// display messages waits instead of handing them over, so SCTP stops
    /// reading and the peer's send side fills. stop() always releases it.
    void setDisplayReceiveStalledForTest(bool stalled);

private:
    struct Private;
    std::unique_ptr<Private> d;

    void drainCallbacks();
    void stopInternal(bool notify);
};

} // namespace NereusSDR
