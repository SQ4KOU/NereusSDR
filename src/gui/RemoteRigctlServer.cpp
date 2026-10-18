// =================================================================
// src/gui/RemoteRigctlServer.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. See the header. The key's lifecycle
// follows the remote window's TCI server (TciServer::handleRemoteTrx and its
// client-gone release), which this server keys through.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-15 - Created for WSJT-X CAT and VAX through a remote window.
//   2026-10-15 - The pump starts after keyForProgram and stops before the
//                unkey, so it is the VAX TX device's only reader while the
//                window's VAX feed (PR 520) is stopped for the key.
//   2026-10-15 - Review fixes: T 1 after a pending T 0 keeps the key; a key
//                refused inside key() answers RPRT -9; a changed forwarder
//                releases the key, as TciServer does.
//   2026-10-15 - The pump reads only while the program-audio key is on, and
//                the Core ending the key ends it here, as in TciServer.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-16 - The pump keeps the VAX TX device's backlog (up to the
//                Core's deepest transmit buffer): read on a 10 ms GUI timer,
//                the device's 16 ms guard dropped a tenth of WSJT-X's audio
//                and the Core ran dry every second of a key.
//                J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/RemoteRigctlServer.h"

#include "core/AudioEngine.h"
#include "core/LogCategories.h"
#include "core/cat/CatTcpTransport.h"
#include "core/cat/RigctlProtocol.h"
#include "core/cat/RigctlTarget.h"
#include "core/session/SliceAccessMirror.h"
#include "core/session/media/RemoteMicReceiver.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QTcpSocket>

#include <algorithm>
#include <utility>

namespace NereusSDR {

namespace {

// The rigctld line limit the Core's CAT sessions keep (CatSession::feedLines).
constexpr qsizetype kMaximumLineBytes = 4096;
// The VAX TX device is 48 kHz (AudioEngine::pullVaxTxMic), as is the
// window's microphone line.
constexpr int kVaxTxRateHz = 48000;
// One pump tick takes up to this much: 100 ms, well over a 10 ms tick, so a
// stalled timer catches up in one tick.
constexpr int kPumpFrames = 4800;
constexpr int kPumpIntervalMs = 10;

// The slice properties the Core freezes on the slice it transmits on while
// it is on the air (CatModelAdapter::mayChange, StationSliceFreeze).
bool frozenWhileOnAir(const QByteArray& property)
{
    static const QList<QByteArray> kFrozen{"frequency", "dspMode", "filterLow", "filterHigh",
                                           "txAntenna", "band", "xitEnabled", "xitHz"};
    return kFrozen.contains(property);
}

/// One rigctld client's view of the server: this window, on vfoSliceId().
/// VFO B stands for nothing; split asks are refused as the Core's CAT
/// refuses a channel bound to one slice.
class RemoteRigctlTarget final : public RigctlTarget {
public:
    RemoteRigctlTarget(RemoteRigctlServer* server, RadioModel* model, quint64 sessionId)
        : m_server(server), m_model(model), m_sessionId(sessionId)
    {
    }

    bool live() const override { return m_server && m_model; }

    int sliceIdOf(CatVfo vfo) const override
    {
        if (!live() || vfo != CatVfo::Primary) {
            return -1;
        }
        return m_server->vfoSliceId();
    }

    SliceModel* resolveSlice(CatVfo vfo) const override
    {
        const int id = sliceIdOf(vfo);
        return id >= 0 ? m_model->sliceById(id) : nullptr;
    }

    bool mayRead(CatVfo vfo) const override { return resolveSlice(vfo) != nullptr; }

    bool mayChange(CatVfo vfo, const QByteArray& property) const override
    {
        const SliceModel* slice = resolveSlice(vfo);
        if (slice == nullptr || !m_server->mayChangeSlice(slice->sliceIndex())) {
            return false;
        }
        return !refusedOnAir(vfo) || !frozenWhileOnAir(property);
    }

    bool refusedOnAir(CatVfo vfo) const override
    {
        // This window's own key is on the slice VFO A stands for.
        return resolveSlice(vfo) != nullptr && m_server->keyed();
    }

    CatWriteToken prepareWrite(CatVfo vfo, const QByteArray& property) const override
    {
        CatWriteToken token{CatBinding{}, vfo, property};
        token.admitted = mayChange(vfo, property);
        if (const SliceModel* slice = resolveSlice(vfo)) {
            token.binding.primarySliceId = slice->sliceIndex();
            token.controlRevision = m_server->controlRevision(slice->sliceIndex());
        }
        return token;
    }

    bool revalidateWrite(const CatWriteToken& token) const override
    {
        const SliceModel* slice = resolveSlice(token.vfo);
        return token.admitted && slice != nullptr
            && slice->sliceIndex() == token.binding.primarySliceId
            && m_server->controlRevision(slice->sliceIndex()) == token.controlRevision
            && mayChange(token.vfo, token.property);
    }

    // This window's key lands on its own slice (StationTransmitTake's
    // landing for a device that shares slices), which VFO A stands for.
    int txSliceId() const override { return sliceIdOf(CatVfo::Primary); }
    bool transmitting() const override { return live() && m_server->keyed(); }

    bool requestTxSelection(int sliceId) override
    {
        return sliceId >= 0 && sliceId == sliceIdOf(CatVfo::Primary);
    }

    bool requestPtt(int sliceId) override
    {
        return live() && sliceId == sliceIdOf(CatVfo::Primary)
            && m_server->requestKey(m_sessionId);
    }

    void releasePtt() override
    {
        if (m_server) {
            m_server->releaseKey(m_sessionId);
        }
    }

private:
    QPointer<RemoteRigctlServer> m_server;
    QPointer<RadioModel> m_model;
    quint64 m_sessionId;
};

} // namespace

RemoteRigctlServer::RemoteRigctlServer(RadioModel* model, QObject* parent)
    : QObject(parent), m_model(model), m_tcp(std::make_unique<CatTcpTransport>())
{
    m_audio.resize(kPumpFrames);
    m_pump.setInterval(kPumpIntervalMs);
    connect(&m_pump, &QTimer::timeout, this, &RemoteRigctlServer::pumpAudio);
    connect(m_tcp.get(), &CatTcpTransport::clientAccepted, this, &RemoteRigctlServer::accept);
    connect(m_tcp.get(), &CatTcpTransport::bytesReceived, this, &RemoteRigctlServer::receive);
    connect(m_tcp.get(), &CatTcpTransport::closeRequested, this, &RemoteRigctlServer::closeClient);
    connect(m_tcp.get(), &CatTcpTransport::clientCountChanged, this,
            &RemoteRigctlServer::clientCountChanged);
    // As TciServer::hookGlobalBroadcasts: the Core ending this window's key
    // on its own (a safety stop, a take) ends it here too, with no unkey.
    if (m_model) {
        connect(m_model, &RadioModel::transmittingChanged, this, [this](bool on) {
            if (!on && m_keyEpoch != 0) {
                qCInfo(lcCat) << "Remote rigctld: the Core ended this window's key";
                m_keyEpoch = 0;
                endKey();
            }
        });
    }
}

void RemoteRigctlServer::setProgramAudioKey(std::function<bool()> on)
{
    m_programAudioKey = std::move(on);
}

bool RemoteRigctlServer::programAudioKeyOn() const
{
    return !m_programAudioKey || m_programAudioKey();
}

RemoteRigctlServer::~RemoteRigctlServer()
{
    stop();
}

void RemoteRigctlServer::setSliceAccess(SliceAccessMirror* access)
{
    m_access = access;
}

void RemoteRigctlServer::setRemoteTransmit(TciServer::RemoteTransmit transmit)
{
    // As TciServer::setRemoteTransmit: a key this window holds through the
    // old forwarder is released there (remote transmit can go while the
    // link stays up), and an answer still on its way finds a newer
    // generation.
    if (keyed()) {
        qCInfo(lcCat) << "Remote rigctld: remote transmit changed; the key is released";
        const quint32 epoch = m_keyEpoch;
        ++m_keyGeneration;
        m_keyPending = false;
        m_releaseWhilePending = false;
        m_keyEpoch = 0;
        endKey();
        if (epoch != 0 && m_transmit.unkey) {
            m_transmit.unkey(epoch);
        }
    }
    m_transmit = std::move(transmit);
}

bool RemoteRigctlServer::transmitAvailable() const
{
    return static_cast<bool>(m_transmit.key) && static_cast<bool>(m_transmit.unkey);
}

bool RemoteRigctlServer::start(const QHostAddress& address, quint16 port)
{
    if (m_tcp->isListening()) {
        return true;
    }
    if (!m_tcp->start(address, port)) {
        qCWarning(lcCat) << "Remote rigctld: could not listen on" << address.toString() << port
                         << ":" << m_tcp->errorString();
        return false;
    }
    qCInfo(lcCat) << "Remote rigctld listening on" << address.toString() << m_tcp->boundPort();
    emit listeningChanged(true);
    return true;
}

void RemoteRigctlServer::stop()
{
    const QList<quint64> ids = m_clients.keys();
    for (quint64 id : ids) {
        closeClient(id);
    }
    if (m_tcp->isListening()) {
        m_tcp->stop();
        qCInfo(lcCat) << "Remote rigctld stopped";
        emit listeningChanged(false);
    }
}

bool RemoteRigctlServer::isListening() const
{
    return m_tcp->isListening();
}

quint16 RemoteRigctlServer::port() const
{
    return m_tcp->boundPort();
}

QHostAddress RemoteRigctlServer::address() const
{
    return m_tcp->boundAddress();
}

QString RemoteRigctlServer::errorString() const
{
    return m_tcp->errorString();
}

int RemoteRigctlServer::clientCount() const
{
    return m_tcp->clientCount();
}

int RemoteRigctlServer::vfoSliceId() const
{
    if (!m_model) {
        return -1;
    }
    const auto controlled = [this](const SliceModel* slice) {
        if (slice == nullptr || slice->isReadOnlyListener()) {
            return false;
        }
        // A Core that sends no slice access: every slice the window may
        // change is its own, as the window's controls take it.
        return !m_access || m_access->entries().isEmpty()
            || m_access->controlledHere(slice->sliceIndex());
    };
    // The Core's TX slice arrives as the slice's flag (StationClient).
    for (const SliceModel* slice : m_model->slices()) {
        if (slice != nullptr && slice->isTxSlice() && controlled(slice)) {
            return slice->sliceIndex();
        }
    }
    if (controlled(m_model->activeSlice())) {
        return m_model->activeSlice()->sliceIndex();
    }
    int lowest = -1;
    for (const SliceModel* slice : m_model->slices()) {
        if (controlled(slice) && (lowest < 0 || slice->sliceIndex() < lowest)) {
            lowest = slice->sliceIndex();
        }
    }
    return lowest;
}

bool RemoteRigctlServer::mayChangeSlice(int sliceId) const
{
    const SliceModel* slice = m_model ? m_model->sliceById(sliceId) : nullptr;
    // A locked slice is changed as the Core's CAT changes it: its setters
    // hold the value, and the readback answers RPRT -9.
    return slice != nullptr && !slice->isReadOnlyListener();
}

quint64 RemoteRigctlServer::controlRevision(int sliceId) const
{
    if (m_access) {
        if (const auto entry = m_access->entry(sliceId)) {
            return entry->controlRevision;
        }
    }
    return 0;
}

bool RemoteRigctlServer::requestKey(quint64 sessionId)
{
    if (keyed()) {
        m_claims.insert(sessionId);
        // T 1 after a T 0 that came while the Core had not answered yet:
        // the key is wanted again.
        m_releaseWhilePending = false;
        return true;
    }
    if (!transmitAvailable()) {
        qCInfo(lcCat) << "Remote rigctld: PTT refused; the Core takes no keys from this window now";
        return false;
    }
    m_claims.insert(sessionId);
    m_keyPending = true;
    m_releaseWhilePending = false;
    const quint64 generation = ++m_keyGeneration;
    emit keyedChanged(true);
    qCInfo(lcCat) << "Remote rigctld: PTT forwarded to the Core as a program's key";
    const QPointer<RemoteRigctlServer> self(this);
    m_transmit.key(true, [self, generation](const TciServer::RemoteKeyAnswer& answer) {
        if (!self) {
            return;
        }
        if (generation != self->m_keyGeneration || !self->m_keyPending) {
            // A stale answer: an accepted key it brings is released at once.
            if (answer.accepted && answer.epoch != 0 && self->m_transmit.unkey) {
                self->m_transmit.unkey(answer.epoch);
            }
            return;
        }
        self->m_keyPending = false;
        if (!answer.accepted) {
            qCInfo(lcCat) << "Remote rigctld: the Core refused the key:" << answer.reason;
            self->m_claims.clear();
            self->endKey();
            return;
        }
        if (self->m_releaseWhilePending || self->m_claims.isEmpty()) {
            // The program let go before the Core answered.
            self->m_releaseWhilePending = false;
            self->endKey();
            self->m_transmit.unkey(answer.epoch);
            return;
        }
        self->m_keyEpoch = answer.epoch;
        qCInfo(lcCat) << "Remote rigctld: the Core keyed this window, epoch" << answer.epoch;
    });
    // The VAX TX device takes one reader. keyForProgram has now stopped the
    // window's own VAX feed (RemoteMediaController), so the pump starts only
    // after it, and only if the key is still on (a refusal can come back
    // inside key()).
    if (keyed() && generation == m_keyGeneration && programAudioKeyOn()) {
        // Whatever a program played before this key is not this key's audio.
        if (AudioEngine* engine = m_model ? m_model->localAudioDevices() : nullptr) {
            while (engine->pullVaxTxMic(m_audio.data(), kPumpFrames) == kPumpFrames) {
            }
        }
        setPumpRunning(true);
    }
    // RemoteTransmitClient refuses some keys inside key() (the radio's
    // microphone required, another program's key): RPRT -9, not RPRT 0.
    return keyed();
}

void RemoteRigctlServer::releaseKey(quint64 sessionId)
{
    if (!m_claims.remove(sessionId) || !m_claims.isEmpty()) {
        return;
    }
    if (m_keyPending) {
        m_releaseWhilePending = true;
        return;
    }
    if (m_keyEpoch != 0) {
        qCInfo(lcCat) << "Remote rigctld: PTT off unkeys this window's key, epoch" << m_keyEpoch;
        const quint32 epoch = m_keyEpoch;
        m_keyEpoch = 0;
        // The pump stops before the unkey, which lets the window's VAX feed
        // read the device again: never two readers.
        endKey();
        if (m_transmit.unkey) {
            m_transmit.unkey(epoch);
        }
    }
}

void RemoteRigctlServer::endKey()
{
    m_claims.clear();
    setPumpRunning(false);
    emit keyedChanged(false);
}

void RemoteRigctlServer::setPumpRunning(bool running)
{
    if (running && !m_pump.isActive()) {
        m_pump.start();
    } else if (!running) {
        m_pump.stop();
    }
}

void RemoteRigctlServer::pumpAudio()
{
    AudioEngine* engine = m_model ? m_model->localAudioDevices() : nullptr;
    if (engine == nullptr || !keyed() || !m_transmit.audio || !programAudioKeyOn()) {
        return;
    }
    // A late tick finds more than 16 ms waiting: kept, up to what the
    // Core's transmit buffer holds.
    const int got = engine->pullVaxTxMicKeepingBacklog(m_audio.data(), kPumpFrames,
                                                       RemoteMicConfig::kMaxDepthFrames);
    if (got > 0) {
        m_transmit.audio(m_audio.data(), got, 1, kVaxTxRateHz);
    }
}

void RemoteRigctlServer::accept(QTcpSocket* socket)
{
    const quint64 id = ++m_nextClient;
    Client client;
    client.protocol = std::make_shared<RigctlProtocol>(
        std::make_shared<RemoteRigctlTarget>(this, m_model.data(), id));
    m_clients.insert(id, std::move(client));
    if (!m_tcp->attachSession(id, socket)) {
        m_clients.remove(id);
    }
}

void RemoteRigctlServer::receive(quint64 id, const QByteArray& bytes)
{
    const QPointer<RemoteRigctlServer> self(this);
    for (char byte : bytes) {
        auto it = m_clients.find(id);
        if (it == m_clients.end()) {
            return;
        }
        if (byte != '\n') {
            if (!it->droppingLine) {
                if (it->line.size() == kMaximumLineBytes) {
                    it->line.clear();
                    it->droppingLine = true;
                } else {
                    it->line.append(byte);
                }
            }
            continue;
        }
        const bool dropped = it->droppingLine;
        const QByteArray line = it->line + byte;
        it->line.clear();
        it->droppingLine = false;
        const std::shared_ptr<RigctlProtocol> protocol = it->protocol;
        const QByteArray answer = dropped
            ? QByteArray("RPRT -1\n")
            : protocol->handleLine(QString::fromLatin1(line)).toLatin1();
        if (!self) {
            return;
        }
        it = m_clients.find(id);
        if (it == m_clients.end()) {
            return;
        }
        if (!answer.isEmpty() && !m_tcp->writeBytes(id, answer)) {
            closeClient(id);
            return;
        }
        // Hamlib rigctld closes the client after answering q.
        if (protocol->takeQuit()) {
            m_tcp->finishSession(id);
            closeClient(id);
            return;
        }
    }
}

void RemoteRigctlServer::closeClient(quint64 id)
{
    if (!m_clients.contains(id)) {
        return;
    }
    // The client that keyed is gone: nothing would send its PTT off.
    releaseKey(id);
    m_clients.remove(id);
    m_tcp->closeSession(id);
}

} // namespace NereusSDR
