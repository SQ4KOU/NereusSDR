#pragma once
// =================================================================
// src/core/session/media/MediaPeer.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
//
// Strict session signalling bridge for one authenticated media connection.
// The controller owns authentication and session epochs. This class carries
// only bounded SDP/candidate control and delegates media to IMediaTransport.
//
// =================================================================

#include "core/session/media/IMediaTransport.h"

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace NereusSDR {

struct MediaPeerTelemetry {
    quint64 generation = 0;
    MediaTransportTelemetry traffic;
};

class MediaPeer final : public QObject {
    Q_OBJECT

public:
    using TransportFactory = std::function<IMediaTransport*(QObject* parent)>;

    explicit MediaPeer(QObject* parent = nullptr,
                       TransportFactory factory = {});
    ~MediaPeer() override;

    // audioTargetBitrate is the Opus target this side sends at; an offerer's
    // audio description never advertises more (R-R3-23). An answerer sends
    // no audio and keeps the default.
    bool start(IMediaTransport::Role role, const QString& connectionId,
               int audioTargetBitrate = IMediaTransport::kDefaultAudioTargetBitrate);
    void stop();

    bool acceptControl(const QJsonObject& control);
    bool sendDisplay(const QByteArray& message);
    IMediaTransport::DisplaySendResult submitDisplay(const QByteArray& message);
    bool displayBusy() const;
    bool sendRtp(const QByteArray& packet);

    /// Why the last start() returned false (R-R3-28, amended 2026-09-23).
    /// Only TransportConstructionFailed is transient and worth a retry;
    /// every other refusal is permanent.
    enum class StartRefusal {
        /// The last start() succeeded, or none has run.
        None,
        /// This peer is already started, or the connection id is not
        /// canonical.
        Precondition,
        /// The factory returned no transport, or one on another thread.
        InvalidTransport,
        /// The transport refused without reporting an error: a
        /// precondition of its own, such as an SSRC of zero.
        TransportRefused,
        /// The factory threw, or the transport reported an error and
        /// refused because it could not build its peer.
        TransportConstructionFailed,
    };
    StartRefusal lastStartRefusal() const;

    bool isReady() const;
    QString connectionId() const;
    quint32 audioSsrc() const;
    std::optional<MediaPeerTelemetry> telemetry() const;

signals:
    void controlReady(const QJsonObject& control);
    void displayReceived(const QByteArray& message);
    void rtpReceived(const QByteArray& packet);
    void ready();
    void closed();
    void connectionFailed(const QString& message);
    void errorOccurred(const QString& message);
    void displayWritable();
    void displayErrorOccurred(const QString& message);

private:
    struct Private;
    Private* d;

    bool isCurrent(const IMediaTransport* transport, quint64 generation) const;
    void stopInternal(bool notify);
};

} // namespace NereusSDR
