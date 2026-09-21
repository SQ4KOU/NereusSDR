#pragma once
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
#include "core/ConnectionState.h"
#include "core/session/RemoteStationOptions.h"
#include <QObject>
#include <QPointer>
#include <QDialog>

namespace NereusSDR {
class RadioModel;
class StationClient;

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
signals:
    void changed();
private:
    QPointer<StationClient> m_client;
    QPointer<RadioModel> m_model;
    RemoteStationOptions m_options;
    bool m_operatorDisconnected = false;
    int m_retryAttempt = 0;
    int m_retryDelayMs = 0;
};

// A small modeless view of the configured Core. Full station selection and
// pairing remain a separate surface; this view always describes the live client.
class RemoteConnectionPanel final : public QDialog {
    Q_OBJECT
public:
    explicit RemoteConnectionPanel(RemoteConnectionController* controller,
                                   QWidget* parent = nullptr);
};
} // namespace NereusSDR
