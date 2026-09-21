#pragma once
// =================================================================
// src/core/session/media/IMediaTransport.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// One authenticated session's direct encrypted media peer. Authentication
// and model ownership remain above this interface. Implementations carry
// already-encoded display messages and raw RTP packets only.
//
// =================================================================

#include <QByteArray>
#include <QObject>
#include <QString>

namespace NereusSDR {

class IMediaTransport : public QObject {
    Q_OBJECT

public:
    enum class Role {
        Offerer,
        Answerer,
    };
    Q_ENUM(Role)

    struct StartOptions {
        Role role;
        // Per-session RTP routing identity. DTLS authenticates the peer;
        // this value is not an authentication token.
        quint32 localAudioSsrc;
    };

    static constexpr qsizetype kMaxDescriptionBytes = 64 * 1024;
    static constexpr qsizetype kMaxCandidateBytes = 4 * 1024;
    static constexpr qsizetype kMaxCandidateMidBytes = 256;
    static constexpr qsizetype kMaxDisplayMessageBytes = 64 * 1024;
    static constexpr qsizetype kMaxRawRtpBytes = 940;
    static constexpr qsizetype kMinRawRtpBytes = 12;
    static constexpr int kMaxRemoteCandidates = 64;
    static constexpr int kConfiguredMtuBytes = 1000;

    explicit IMediaTransport(QObject* parent = nullptr) : QObject(parent) {}
    ~IMediaTransport() override = default;

    virtual bool start(const StartOptions& options) = 0;
    virtual void stop() = 0;

    virtual bool acceptDescription(const QString& sdp, const QString& type) = 0;
    virtual bool acceptCandidate(const QString& candidate, const QString& mid) = 0;

    /// Send without waiting for transport backpressure. The adapter adds no
    /// application-side queue. False can also mean libdatachannel buffered a
    /// display message, so callers must not retry the same bytes solely from
    /// the return value.
    virtual bool sendDisplay(const QByteArray& message) = 0;
    virtual bool sendRtp(const QByteArray& packet) = 0;

    virtual bool isReady() const = 0;

signals:
    void localDescription(const QString& sdp, const QString& type);
    void localCandidate(const QString& candidate, const QString& mid);
    void displayReceived(const QByteArray& message);
    void rtpReceived(const QByteArray& packet);
    void ready();
    void closed();
    void errorOccurred(const QString& message);
};

} // namespace NereusSDR
