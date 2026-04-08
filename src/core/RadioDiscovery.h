#pragma once

#include <QObject>
#include <QHostAddress>
#include <QUdpSocket>
#include <QTimer>
#include <QMap>

namespace NereusSDR {

// Information about a discovered OpenHPSDR radio.
struct RadioInfo {
    QString name;               // e.g. "ANAN-7000DLE"
    QString model;              // board model string
    QString macAddress;         // MAC address (OpenHPSDR identifies by MAC)
    QHostAddress address;       // IP address
    quint16 port{1024};         // UDP port (always 1024 for OpenHPSDR)
    int protocol{1};            // 1 = Protocol 1, 2 = Protocol 2
    int boardType{0};           // 0=Metis, 1=Hermes, 2=Griffin, 4=Angelia, 5=Orion, 6=HermesLite, 10=OrionMkII
    int firmwareVersion{0};     // firmware version number
    int adcCount{1};            // number of ADCs
    bool inUse{false};          // whether the radio is currently in use by another client

    QString displayName() const;

    // Board type to human-readable name
    static QString boardTypeName(int type);
};

// Discovers OpenHPSDR radios on the local network.
// Protocol 1: UDP broadcast to port 1024
// Protocol 2: UDP broadcast/multicast to port 1024
class RadioDiscovery : public QObject {
    Q_OBJECT

public:
    explicit RadioDiscovery(QObject* parent = nullptr);
    ~RadioDiscovery() override;

    void startDiscovery();
    void stopDiscovery();

    QList<RadioInfo> discoveredRadios() const;

signals:
    void radioDiscovered(const RadioInfo& info);
    void radioUpdated(const RadioInfo& info);
    void radioLost(const QString& macAddress);

private slots:
    void onReadyRead();
    void onStaleCheck();
    void sendDiscoveryPacket();

private:
    static constexpr quint16 kDiscoveryPort = 1024;
    static constexpr int kStaleTimeoutMs = 10000;

    QUdpSocket* m_socket{nullptr};
    QTimer m_discoveryTimer;
    QTimer m_staleTimer;
    QMap<QString, RadioInfo> m_radios;  // keyed by MAC address
    QMap<QString, qint64> m_lastSeen;   // MAC → timestamp
};

} // namespace NereusSDR
