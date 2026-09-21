#pragma once
// no-port-check: NereusSDR-original. Remote daemon R3 receive display wiring.

#include "core/session/media/MediaPeer.h"
#include <QObject>
#include <memory>

namespace NereusSDR {
class StationClient;
class RadioModel;
class PanadapterStack;

/// Owns the GUI's media session and one bounded subscription per visible pan.
/// Display data never passes through the control/property mirror.
class RemoteMediaController final : public QObject {
    Q_OBJECT
public:
    RemoteMediaController(StationClient* client, RadioModel* model,
                          PanadapterStack* stack, QObject* parent = nullptr,
                          MediaPeer::TransportFactory factory = {});
    ~RemoteMediaController() override;

    quint64 receivedDisplayFrames() const;
    int activeEndpointCount() const;

signals:
    void errorOccurred(const QString& reason);
    void displayFrameReceived(quint32 endpointId);

private:
    struct Private;
    std::unique_ptr<Private> d;
    void start();
    void stop();
    void refreshSubscriptions();
    void receiveControl(const QJsonObject& payload, quint32 epoch);
    void receiveDisplay(const QByteArray& packet);
    void requestKeyframe(quint32 endpointId);
    void requestAudio();
    bool send(QJsonObject payload);
};
} // namespace NereusSDR
