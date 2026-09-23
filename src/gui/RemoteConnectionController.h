#pragma once
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
#include "core/ConnectionState.h"
#include "core/session/RemoteStationOptions.h"
#include <QObject>
#include <QPointer>
#include <QDialog>

#include <functional>

class QTimer;

namespace NereusSDR {
class RadioModel;
class StationClient;
class RemoteMediaController;

// Uses session state, never radio connectivity, to decide whether an
// operator can connect or cancel. The same controller backs all surfaces.
class RemoteConnectionController final : public QObject {
    Q_OBJECT
public:
    RemoteConnectionController(StationClient* client, RadioModel* model,
                               RemoteStationOptions options, QObject* parent = nullptr);
    QString endpointText() const;
    QString statusText() const;
    QString radioText() const;
    QString detailText() const;
    ConnectionState state() const;
    bool canConnect() const;
    bool canDisconnect() const;
public slots:
    void connectToStation();
    void disconnectFromStation();
    void recoverMediaSession(quint32 expectedEpoch, const QString& reason);
signals:
    void changed();
    // R-R3-16 / R-R3-38: the operator disconnected (or cancelled a pending
    // retry) through disconnectFromStation(). Every operator Disconnect
    // surface calls that slot, so this is the one signal the window uses
    // to open Connections. Link loss, a Core that reports its radio
    // offline and every disconnect the app starts itself (a Core refusal,
    // preemption, shutdown) never emit it.
    void operatorDisconnected();
private:
    QPointer<StationClient> m_client;
    QPointer<RadioModel> m_model;
    RemoteStationOptions m_options;
    bool m_operatorDisconnected = false;
    quint32 m_pendingMediaRecoveryEpoch = 0;
    int m_retryAttempt = 0;
    int m_retryDelayMs = 0;
};

// A small modeless view of the configured Core. Full station selection and
// pairing remain a separate surface; this view always describes the live client.
// With a media controller, it also shows a "Remote audio" section (current
// status, audio quality, codec, output and health), the Opus or Lossless
// choice and a Retry button; without one the panel is unchanged.
class RemoteConnectionPanel final : public QDialog {
    Q_OBJECT
public:
    explicit RemoteConnectionPanel(RemoteConnectionController* controller,
                                   QWidget* parent = nullptr,
                                   RemoteMediaController* media = nullptr);
protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
private:
    // Height follows the wrapped text at the current width.
    void fitHeightToContent();
    // Polls the audio health once a second, only while the panel is shown.
    QTimer* m_audioTimer = nullptr;
    std::function<void()> m_refreshAudio;
};
} // namespace NereusSDR
