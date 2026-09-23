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

    bool start(IMediaTransport::Role role, const QString& connectionId);
    void stop();

    bool acceptControl(const QJsonObject& control);
    bool sendDisplay(const QByteArray& message);
    IMediaTransport::DisplaySendResult submitDisplay(const QByteArray& message);
    bool displayBusy() const;
    bool sendRtp(const QByteArray& packet);

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
