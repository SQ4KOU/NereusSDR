#pragma once
// =================================================================
// src/gui/RemoteRigctlServer.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. A Hamlib rigctld that runs in a remote
// window and acts as that window.
//
// The Core's own CAT acts as the station (CatModelAdapter, CatTxCoordinator):
// it cannot read or tune a slice a remote window controls, and its key would
// send the Core computer's audio. This server answers rigctld on this
// computer with the Core's RigctlProtocol parser over a RigctlTarget that:
//   - reads and tunes the slice this window transmits on, through the
//     window's slice setters, which already send to the Core;
//   - keys as this window's program, exactly as its TCI server's trx does
//     (TciServer::RemoteTransmit, R-R3-42), with the program's own audio;
//   - while that key is on, sends what programs play into the NereusSDR TX
//     device (VAX TX) on the window's microphone line in place of the
//     microphone (RemoteMediaController::pushProgramAudio).
// Nothing here adds a remote protocol: the Core sees the same slice changes
// and tx.key {trigger:"tci"} as from the window's own controls and TCI.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for WSJT-X CAT and VAX through a remote window.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/TciServer.h"

#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class QTcpSocket;

namespace NereusSDR {

class CatTcpTransport;
class RadioModel;
class RigctlProtocol;
class SliceAccessMirror;

class RemoteRigctlServer : public QObject {
    Q_OBJECT
public:
    explicit RemoteRigctlServer(RadioModel* model, QObject* parent = nullptr);
    ~RemoteRigctlServer() override;

    /// The window's slice access (which slices it controls); may be null.
    void setSliceAccess(SliceAccessMirror* access);
    /// The window's remote transmit (remoteTransmitForwarder); empty while
    /// the Core takes no keys from this window. Going empty forgets the key
    /// (the Core unkeys a device whose link drops).
    void setRemoteTransmit(TciServer::RemoteTransmit transmit);
    bool transmitAvailable() const;
    /// Whether the window's program-audio key is on now
    /// (RemoteTransmitClient::programAudioKey). The pump reads the VAX TX
    /// device only while it is: when it goes off (an unkey, the Core's stop,
    /// a refusal, the link), the window's own VAX feed reads it again, and
    /// the device takes one reader. Unset: not gated (tests).
    void setProgramAudioKey(std::function<bool()> on);

    bool start(const QHostAddress& address, quint16 port);
    void stop();
    bool isListening() const;
    quint16 port() const;
    QHostAddress address() const;
    QString errorString() const;
    int clientCount() const;

    /// The slice VFO A stands for: the slice this window transmits on (the
    /// Core's TX slice when this window controls it), else this window's
    /// active slice when it controls it, else the lowest slice it controls.
    /// -1 when it controls none.
    int vfoSliceId() const;
    bool mayChangeSlice(int sliceId) const;
    quint64 controlRevision(int sliceId) const;

    /// rigctld T and t for one client. A key is asked of the Core once for
    /// every client holding it, and released when the last lets go.
    bool requestKey(quint64 sessionId);
    void releaseKey(quint64 sessionId);
    /// The key is asked for or on.
    bool keyed() const { return m_keyPending || m_keyEpoch != 0; }

    /// The VAX TX audio pump's tick (public for tests).
    void pumpAudio();

signals:
    void listeningChanged(bool listening);
    void clientCountChanged(int count);
    void keyedChanged(bool keyed);

private:
    struct Client {
        std::shared_ptr<RigctlProtocol> protocol;
        QByteArray line;
        bool droppingLine{false};
    };
    void accept(QTcpSocket* socket);
    void receive(quint64 id, const QByteArray& bytes);
    void closeClient(quint64 id);
    void endKey();
    bool programAudioKeyOn() const;
    void setPumpRunning(bool running);

    QPointer<RadioModel> m_model;
    QPointer<SliceAccessMirror> m_access;
    TciServer::RemoteTransmit m_transmit;
    std::function<bool()> m_programAudioKey;
    std::unique_ptr<CatTcpTransport> m_tcp;
    QHash<quint64, Client> m_clients;
    quint64 m_nextClient{0};
    QSet<quint64> m_claims;
    bool m_keyPending{false};
    bool m_releaseWhilePending{false};
    quint32 m_keyEpoch{0};
    quint64 m_keyGeneration{0};
    QTimer m_pump;
    std::vector<float> m_audio;
};

} // namespace NereusSDR
