#pragma once

#include "RadioDiscovery.h"

#include <QObject>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QTimer>

namespace NereusSDR {

// Connection state for a radio.
enum class ConnectionState {
    Disconnected,
    Connecting,
    Connected,
    Error
};

// Manages the connection to an OpenHPSDR radio.
// Protocol 1: UDP-only communication via Metis frames on port 1024.
// Protocol 2: TCP command channel + UDP data channels.
class RadioConnection : public QObject {
    Q_OBJECT

public:
    explicit RadioConnection(QObject* parent = nullptr);
    ~RadioConnection() override;

    void connectToRadio(const RadioInfo& info);
    void disconnect();

    ConnectionState state() const { return m_state; }
    bool isConnected() const { return m_state == ConnectionState::Connected; }

    const RadioInfo& radioInfo() const { return m_radioInfo; }

    // Send raw data to the radio
    void sendUdpData(const QByteArray& data);
    void sendCommand(const QByteArray& command);  // Protocol 2 TCP command

signals:
    void connectionStateChanged(ConnectionState state);
    void dataReceived(const QByteArray& data);
    void commandResponse(const QByteArray& response);
    void errorOccurred(const QString& message);

private slots:
    void onUdpReadyRead();
    void onTcpConnected();
    void onTcpReadyRead();
    void onTcpError(QAbstractSocket::SocketError error);

private:
    void setState(ConnectionState newState);
    void connectProtocol1();
    void connectProtocol2();

    ConnectionState m_state{ConnectionState::Disconnected};
    RadioInfo m_radioInfo;

    // Protocol 1 uses UDP only
    QUdpSocket* m_udpSocket{nullptr};

    // Protocol 2 additionally uses TCP for commands
    QTcpSocket* m_tcpSocket{nullptr};

    QTimer m_reconnectTimer;
    bool m_intentionalDisconnect{false};
};

} // namespace NereusSDR
