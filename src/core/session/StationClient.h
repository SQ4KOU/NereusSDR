#pragma once
// =================================================================
// src/core/session/StationClient.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
//
// The GUI half of the wss session: the object that makes a RadioModel
// which never calls connectToRadio() behave as though it had.
//
// ── WHAT IT DOES, IN ORDER ───────────────────────────────────────────────
//
//   1. Opens wss:// to the station and PINS the certificate fingerprint it
//      was given out of band (parent design section 10.5: "the daemon
//      generates a self-signed certificate on first run and the client
//      pins its fingerprint, displayed at pairing time alongside the
//      token"). A self-signed certificate produces SSL errors by
//      definition; this class ignores exactly the errors that are
//      explained by self-signing, and only after the fingerprint matches.
//   2. Reads the station's Hello and applies section 7.0's version policy
//      from its own side: refuse on major mismatch naming BOTH versions,
//      negotiate down on minor.
//   3. Compares the station's AppSettings schema version against its own,
//      BY NAME -- both sides read the value stored under the literal key
//      "SettingsSchemaVersion" in their own store. Skew is reported, not
//      refused: that version governs the shape of each side's own local
//      settings file, not the wire contract.
//   4. Sends its Hello and then its token.
//   5. On Capabilities, writes station identity, the EFFECTIVE slice limit
//      and userDdcCount into RadioModel and drives it to Connected
//      (RadioModel::applyStationCapabilities). This is the step that makes
//      task 3's storage-backed isConnected() true, unpins maxSlices() from
//      its disconnected default of 1, and wakes the GUI.
//   6. On the settings snapshot, feeds SettingsProxy and marks it ready.
//   7. On the mirror burst, builds client-side objects and applies their
//      state; on the snapshot-complete marker, starts forwarding local
//      changes back.
//
// ── BOTH DIRECTIONS OF THE PROPERTY MIRROR, AND WHY THEY DIFFER ──────────
//
// INBOUND (station to here) is unconditional. The daemon owns the radio,
// so whatever it reports is true by definition and must land, including on
// properties with no Q_PROPERTY WRITE. MirrorPolicy is deliberately NOT
// consulted on this path: that table answers "may a remote GUI write this
// to the daemon", which is the opposite question. Applying an inbound
// value goes, in order:
//
//   1. MirrorSchema::write() when the property has a WRITE accessor. This
//      is the overwhelming majority -- 24 of 148 mirrored properties lack
//      one (design addendum section 3).
//   2. The model's own Q_INVOKABLE applyMirroredValue hook, but ONLY for
//      an explicit allowlist of (class, property) pairs whose hook is a
//      genuine STATE APPLY. Exactly one today: SliceModel::
//      signalStrengthDbm, whose hook calls a plain setter task 12 added
//      for precisely this path.
//
//      The allowlist exists because that hook is the DAEMON's inbound
//      path ("a peer is asking this model to do something"), and some
//      implementations of it are COMMAND SENDERS. TunerModel answers
//      isOperate / isBypass / antennaA by forwarding to a bound
//      TgxlConnection. Consulting it here fed a station STATE REPORT into
//      a command sender, which inverts the link; it was inert only
//      because a remote client has no TgxlConnection, and binding one
//      would have turned every inbound tuner delta into an outbound
//      tuner command. Those setters also no-op with no connection while
//      the hook still reports success, so the properties reported as
//      applied, changed nothing, and never reached unappliedProperties().
//      The hook itself is deliberately left alone -- see the .cpp for why
//      that behaviour is Task 8's tested, documented choice.
//   3. A small client-side adapter for the handful of properties whose
//      only legitimate CLIENT-side writer is this class, but whose only
//      legitimate DAEMON-side writer is an arbiter that must not be
//      bypassed: SliceModel::active and SliceModel::txSlice. Their
//      applyMirroredValue refusals are correct on the daemon (a remote
//      peer must go through setActiveSliceById / TxSliceArbiter) and
//      wrong here, where the arbiter has already spoken and this is
//      simply its answer arriving.
//
// Anything none of the three can apply is counted and logged ONCE per
// (class, property) rather than per delta, so a bench session gets one
// line naming a real gap instead of a flood. unappliedProperties() below
// exposes the set.
//
// OUTBOUND (here to station) IS MirrorPolicy-gated, because it is the
// direction MirrorPolicy describes: only Bidirectional properties are
// forwarded, and everything else the operator's own GUI happens to move
// locally is dropped rather than argued about with the station.
//
// ── THE ECHO GUARD ───────────────────────────────────────────────────────
//
// Applying an inbound value calls a real setter, which emits a real
// NOTIFY, which the outbound watcher would forward straight back. The
// daemon has StateMirror::m_applying for the mirror-image problem; this
// class has m_applyingInbound, checked at the top of the outbound
// observer, before it asks anything else. It works for the same reason the
// daemon's does and no other: observer and applier are on ONE thread, so
// the NOTIFY is delivered synchronously, inside the guarded region. See
// the threading note below.
//
// ── THREADING ────────────────────────────────────────────────────────────
//
// This object, its transport, its StateMirror, the RadioModel it drives
// and every SliceModel under it live on ONE thread. Same invariant, same
// reasons, as StationServer's (see its header). The echo guard above is
// one of the three things that silently stops working if that is ever
// violated.
//
// ── ORDERING PRECONDITIONS THE CALLER OWNS ───────────────────────────────
//
// Two, both inherited from task 15 and both invisible from inside this
// class, so the constructor checks what it can and this comment records
// the rest:
//
//   - **Construct this AFTER CoreInit::initialize().** AppSettings::load()
//     bulk-populates and cannot leak, but the schema migrations
//     (AppSettings.cpp) go through setValue(), so installing the remote
//     backend before them would push this machine's own migrated keys up
//     to the station. The constructor warns if the schema-version key is
//     absent, which is the observable trace of migrations not having run.
//   - **SettingsProxy::ready() must not be true before the models are
//     constructed.** SliceModel, NotchModel, FilterPresetStore and
//     TciServer all do contains()-then-seed against Station-classified
//     prefixes in their constructors, and the ONLY thing stopping them
//     writing ship defaults into the station's store is that writes are
//     dropped while not ready. This class never sets ready() before the
//     handshake completes, which is necessarily after the RadioModel it
//     was handed already existed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the GUI half
//                                    of the wss session. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
// =================================================================

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QUrl>

#include "core/session/MirrorSchema.h"
#include "core/session/SessionMessages.h"
#include "core/session/StateMirror.h"
#include "core/session/StationCapabilities.h"

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

namespace NereusSDR {

class RadioModel;
class SessionTransport;
class SettingsProxy;

class StationClient : public QObject {
    Q_OBJECT

public:
    /// Matches StationServer's, and for the same reasons -- see that
    /// class's heartbeat section. Both ends heartbeat independently: a
    /// silently dead link has to be detected from whichever side is still
    /// alive, and which side that is is not knowable in advance.
    static constexpr int kDefaultHeartbeatIntervalMs = 20000;
    static constexpr int kDefaultMaxMissedPongs = 2;

    /// How often locally-observed property changes are drained toward the
    /// station. See StationServer::kDefaultDeltaFlushMs for the same
    /// reasoning in the other direction.
    static constexpr int kDefaultWriteFlushMs = 50;

    /// `radioModel` must be Role::Remote and is NOT owned. `settingsProxy`
    /// is the backend a remote-mode GUI installs via
    /// AppSettings::setRemoteBackend(); also not owned. Both must live on
    /// this object's thread.
    explicit StationClient(RadioModel* radioModel, SettingsProxy* settingsProxy,
                           QObject* parent = nullptr);
    ~StationClient() override;

    StationClient(const StationClient&) = delete;
    StationClient& operator=(const StationClient&) = delete;

    /// Open a wss connection. `expectedFingerprint` is the station's
    /// SHA-256 in CertificateStore::fingerprintSha256()'s colon-separated
    /// uppercase form; an empty one means "do not pin", which is refused
    /// unless allowUnpinned is true, because silently not pinning is the
    /// failure mode that makes the whole certificate model decorative.
    void connectToStation(const QUrl& url, const QString& token,
                          const QString& expectedFingerprint,
                          bool allowUnpinned = false);

    /// Drive the session over an already-open transport instead of dialing
    /// one. Same code path from the first message onward; this is how the
    /// protocol half is exercised without TLS (SessionTransport.h explains
    /// why that matters). Takes ownership by reparenting.
    void startSession(SessionTransport* transport, const QString& token);

    void disconnectFromStation(const QString& reason);

    bool isHandshakeComplete() const { return m_handshakeComplete; }
    QString lastError() const { return m_lastError; }

    /// The station's descriptor as applied. Default-constructed before the
    /// capability exchange.
    const StationCapabilities& capabilities() const { return m_capabilities; }

    /// The minor version both ends agreed on (section 7.0: negotiate down
    /// to the lower). Meaningful once the station's Hello has arrived.
    quint16 agreedMinor() const { return m_agreedMinor; }

    /// Non-zero when the station's AppSettings schema version differs from
    /// this build's. Reported, never a refusal -- see the class comment.
    bool hasSettingsSchemaSkew() const { return m_settingsSchemaSkew; }
    qint32 stationSettingsSchemaVersion() const { return m_stationSettingsSchema; }
    qint32 localSettingsSchemaVersion() const { return m_localSettingsSchema; }

    /// Mirrored property names the station sent that this build's own
    /// MirrorSchema does not carry, and vice versa: schema skew caught by
    /// NAME comparison at handshake, per task 18 step 8. Keyed
    /// "ClassName.propertyName". Empty when the two schemas agree.
    QSet<QByteArray> schemaNamesOnlyOnStation() const { return m_schemaOnlyOnStation; }
    QSet<QByteArray> schemaNamesOnlyLocal() const { return m_schemaOnlyLocal; }

    /// "ClassName.propertyName" for every inbound property none of the
    /// three apply strategies could land. See the class comment.
    QSet<QByteArray> unappliedProperties() const { return m_unapplied; }

    /// The client-side object registry: wire key to live object. Task 19
    /// tears this down on link loss.
    QList<QByteArray> mirroredObjectKeys() const;
    QObject* mirroredObject(const QByteArray& objectKey) const;

    /// Send a command verb (SessionCommandDispatcher's five) to the
    /// station. Returns the commandId the result will echo, or 0 when
    /// there is no session.
    quint32 invokeCommand(const QByteArray& verb, const QList<MirrorUpdate>& arguments);

    void setHeartbeatIntervalMs(int ms);
    int heartbeatIntervalMs() const { return m_heartbeatIntervalMs; }
    void setMaxMissedPongs(int misses);
    int maxMissedPongs() const { return m_maxMissedPongs; }

signals:
    /// The full section 7.0 sequence completed, snapshot-complete marker
    /// included. Parent section 12.2 gates TX on exactly this point.
    void handshakeComplete();

    /// The session ended, with the station's own reason where it gave one
    /// (a version refusal, a failed authentication, or being displaced by
    /// a newer connection).
    void sessionEnded(const QString& reason);

    /// A CommandResult came back. `commandId` matches invokeCommand()'s
    /// return value.
    void commandResult(quint32 commandId, bool accepted, const QString& reason);

    /// The heartbeat declared the station dead: it stopped answering pings
    /// without closing.
    void stationHeartbeatTimeout();

private:
    void attachTransport(SessionTransport* transport, const QString& token);
    void onTransportText(const QByteArray& wire);
    void onTransportClosed();

    /// The single place a session ends. Emits sessionEnded() EXACTLY ONCE
    /// per attachTransport(), whichever of the six paths reached it (peer
    /// close, socket error, heartbeat timeout, the station's own
    /// SessionEnd, a version refusal, an auth refusal). Idempotent: a
    /// second call for the same attach returns without emitting.
    void endSession(const QString& reason);
    void onHeartbeatTick();
    void onWriteFlushTick();

    void handleHello(const SessionMessage& message);
    void handleAuthResult(const SessionMessage& message);
    void handleCapabilities(const SessionMessage& message);
    void handleSettingsSnapshot(const SessionMessage& message);
    void handleSchema(const SessionMessage& message);

    /// The NAME comparison itself, shared by handleSchema() (when an
    /// instance of the class already exists) and handleObjectCreate()
    /// (when the schema arrived first, which is always the case for
    /// slices).
    void compareSchema(const QByteArray& className, const QSet<QByteArray>& stationNames,
                       const QMetaObject* mo);
    void handleObjectCreate(const SessionMessage& message);
    void handleObjectDestroy(const SessionMessage& message);
    void handleDelta(const SessionMessage& message);
    void handleSettingsValue(const SessionMessage& message);
    void handleSettingsReject(const SessionMessage& message);

    /// Inbound apply for one object, under the echo guard. See the class
    /// comment's three-strategy list.
    void applyUpdates(QObject* target, const QByteArray& objectKey,
                      const QList<MirrorUpdate>& updates);
    bool applyOne(QObject* target, const MirrorProperty& prop, const MirrorUpdate& update);

    /// Client-side adapter for daemon-to-client-only properties whose
    /// applyMirroredValue hook correctly refuses on the daemon side.
    /// Returns false when this class has no adapter for the property.
    bool applyClientOnlyProperty(QObject* target, const QByteArray& className,
                                 const QByteArray& propertyName, const QVariant& native);

    /// Resolve a wire object key onto a live client-side object, creating
    /// a slice when the key names one this client does not hold yet.
    QObject* resolveOrCreate(const QByteArray& objectKey, const QByteArray& className);

    void send(const SessionMessage& message);
    void watchForOutbound(const QByteArray& objectKey, QObject* object);

    QPointer<RadioModel> m_radioModel;
    QPointer<SettingsProxy> m_settingsProxy;
    SessionTransport* m_transport = nullptr;

    QString m_token;
    QString m_lastError;
    bool m_handshakeComplete = false;
    bool m_authenticated = false;
    quint16 m_agreedMinor = 0;

    StationCapabilities m_capabilities;

    qint32 m_localSettingsSchema = 0;
    qint32 m_stationSettingsSchema = 0;
    bool m_settingsSchemaSkew = false;

    /// Station schemas that arrived before any instance of their class
    /// existed here, waiting for one. See handleSchema().
    QHash<QByteArray, QSet<QByteArray>> m_pendingStationSchemas;

    QSet<QByteArray> m_schemaOnlyOnStation;
    QSet<QByteArray> m_schemaOnlyLocal;
    QSet<QByteArray> m_unapplied;

    QHash<QByteArray, QPointer<QObject>> m_objects;

    /// The outbound half. StateMirror is reused rather than reimplemented:
    /// its propertiesChanged() signal (tasks 7-8) fires for every watched
    /// object regardless of whether a session was ever attached, which is
    /// exactly the observer this direction needs. attachSession() is never
    /// called on it -- that is the DAEMON's connect-time burst, and a
    /// client has nothing to burst.
    StateMirror* m_outboundMirror = nullptr;
    MirrorCoalescer m_outboundCoalescer;

    /// True for the duration of one inbound apply. See the class comment's
    /// echo-guard section.
    bool m_applyingInbound = false;

    /// True once the snapshot-complete marker has arrived. Until then no
    /// local change is forwarded: everything moving is the station's own
    /// burst landing, and forwarding any of it would tell the station its
    /// own state back.
    bool m_forwardLocalChanges = false;

    /// True from attachTransport() until endSession() reports. What makes
    /// "exactly one sessionEnded per attach" true, and what makes a FAILED
    /// INITIAL CONNECT reportable at all: the previous shape gated the
    /// emit on m_handshakeComplete, so a station that was simply down
    /// produced no signal whatsoever.
    bool m_sessionActive = false;

    /// True once a frame has actually arrived from the station. Gates the
    /// heartbeat: a wss dial can take seconds, and counting missed pongs
    /// across a socket that has not finished connecting reports a slow
    /// dial as a dead station.
    bool m_linkUp = false;

    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_writeFlushTimer = nullptr;
    int m_heartbeatIntervalMs = kDefaultHeartbeatIntervalMs;
    int m_maxMissedPongs = kDefaultMaxMissedPongs;
    int m_pingsAwaitingPong = 0;

    quint32 m_nextCommandId = 1;
};

} // namespace NereusSDR
