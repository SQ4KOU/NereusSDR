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
    bool sendRtp(const QByteArray& packet) override;

    bool isReady() const override;
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
