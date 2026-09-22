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

private:
    struct Private;
    std::unique_ptr<Private> d;

    void drainCallbacks();
    void stopInternal(bool notify);
};

} // namespace NereusSDR
