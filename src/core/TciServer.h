// no-port-check: AetherSDR-derived NereusSDR file.  Transport lifecycle
// (start/stop/onNewConnection/onClientDisconnected) is adapted from
// AetherSDR src/core/TciServer.{h,cpp} [@0cd4559]; NereusSDR diverges in
// bind address, double-start contract, signal set, and client table type.
// Registered in docs/attribution/aethersdr-reconciliation.md.

// src/core/TciServer.h  (NereusSDR)
// NereusSDR-original — TCI WebSocket server.
//
// Transport pattern ported from AetherSDR src/core/TciServer.{h,cpp} [@0cd4559].
// Per-client field set condensed from Thetis TCIServer.cs:684-790 [v2.10.3.13]
// (49 Thetis fields → 14 NereusSDR fields; see TciClientSession.h for the
// detailed divergence rationale).
//
// Key NereusSDR divergences from AetherSDR:
//   - Bind address: QHostAddress::LocalHost (AetherSDR binds to Any).
//     Per design doc Q7 lock-in — TCI is a local-process IPC bus in NereusSDR.
//   - double-start contract: returns false + logs warning (AetherSDR returns
//     true, treating double-start as idempotent).
//   - Signals: finer-grained clientConnected / clientDisconnected carrying
//     QWebSocket* (AetherSDR emits clientCountChanged(int) only).
//   - Client table: QHash<QWebSocket*, shared_ptr<TciClientSession>> instead of
//     QList<ClientState> — O(1) lookup by socket pointer in disconnect handler.
//
// Modification history (NereusSDR):
//   2026-05-10 — Phase 3J-1 Task 2.1 by J.J. Boyd (KG4VCF);
//                AI-assisted transformation via Anthropic Claude Code.

#pragma once
#ifdef HAVE_WEBSOCKETS

#include <QHash>
#include <QObject>
#include <memory>

#include "TciClientSession.h"

class QWebSocketServer;
class QWebSocket;
class QTimer;

namespace NereusSDR {

class RadioModel;

// TCI WebSocket server — exposes radio state over the ExpertSDR3 TCI protocol.
//
// Lifecycle:
//   start(port)  — bind QWebSocketServer to LocalHost:port; 0 = ephemeral.
//   stop()       — close all client sockets + the server socket.
//   isRunning()  — true between a successful start() and the next stop().
//
// Threading: all methods must be called from the thread that owns this object
// (the main GUI thread in the current NereusSDR architecture).  QWebSocket
// callbacks fire on the same thread via the Qt event loop.
class TciServer : public QObject {
    Q_OBJECT

public:
    explicit TciServer(RadioModel* model, QObject* parent = nullptr);
    ~TciServer() override;

    // Start listening on the given port.  Pass 0 to let the OS assign a port.
    // Returns true if the server started listening; false on failure or if
    // the server is already running (double-start is rejected).
    bool start(quint16 port = 50001);

    // Stop the server and disconnect all clients.
    void stop();

    bool    isRunning()   const;
    quint16 port()        const;
    int     clientCount() const { return m_clients.size(); }

    // Override the ping interval (milliseconds) for testability.
    // Default 20000ms matches Thetis TCIServer.cs:2650 [v2.10.3.13] (1000 * 20).
    // Call before or after start(); if the timer is already running the new
    // interval takes effect immediately.
    void setPingIntervalMs(int ms);

signals:
    // Emitted after the server begins listening.  port is the actual bound port
    // (useful when start() was called with port=0).
    void serverStarted(quint16 port);

    // Emitted after stop() completes and all clients have been disconnected.
    void serverStopped();

    // Emitted when a new TCI client connects.
    void clientConnected(QWebSocket* socket);

    // Emitted when a TCI client disconnects (cleanly or on error).
    void clientDisconnected(QWebSocket* socket);

    // Emitted when the server fails to bind.
    void errorOccurred(const QString& errStr);

private slots:
    // From AetherSDR src/core/TciServer.cpp:247-273 [@0cd4559] — accept loop
    void onNewConnection();

    // From AetherSDR src/core/TciServer.cpp:275+ [@0cd4559] — cleanup on disconnect
    void onClientDisconnected();

    // Text-frame handler — Phase 3 Task 3.2 wires TciProtocol dispatch here.
    void onTextMessageReceived(const QString& msg);

    // Binary-frame handler — Phase 17 wires TX audio + IQ inbound here.
    void onBinaryMessageReceived(const QByteArray& data);

private:
    RadioModel*        m_model;
    QWebSocketServer*  m_server{nullptr};
    QHash<QWebSocket*, std::shared_ptr<TciClientSession>> m_clients;

    QTimer* m_pingTimer{nullptr};

    // From Thetis TCIServer.cs:2650 [v2.10.3.13] — 1000 * 20 = 20000ms.
    // Thetis comment: "per websock spec ping frames are every 20 seconds."
    int m_pingIntervalMs{20000};
};

} // namespace NereusSDR

#endif // HAVE_WEBSOCKETS
