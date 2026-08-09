// =================================================================
// src/core/session/StationClient.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
// See StationClient.h for the connect sequence, the two directions of the
// property mirror, the echo guard, and the caller's ordering
// preconditions.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the GUI half
//                                    of the wss session. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 19: link loss,
//                                    daemon restart and reconnect. AI-
//                                    assisted transformation via Anthropic
//                                    Claude Code.
//   2026-08-09  J.J. Boyd / KG4VCF  Whole-branch review, Important 4:
//                                    an entry-less settings.value is a
//                                    removal, not an empty string.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
// =================================================================

#include "core/session/StationClient.h"

#include "core/AppSettings.h"
#include "core/FaultLog.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionTransport.h"
#include "core/settings/SettingsProxy.h"
#include "models/PanadapterModel.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"

#include <QCryptographicHash>
#include <QLoggingCategory>
#include <QSslCertificate>
#include <QSslError>
#include <QTimer>
#include <QWebSocket>

#include <algorithm>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcStationClient, "nereus.stationclient")

constexpr const char* kRadioKey = "radio";
constexpr const char* kTransmitKey = "transmit";
constexpr const char* kTunerKey = "tuner";
constexpr const char* kPanKeyPrefix = "pan:";
constexpr const char* kSliceKeyPrefix = "slice:";

QString peerNameForThisProcess()
{
    return QStringLiteral("NereusSDR GUI");
}

// Named readLocal... rather than localSettingsSchemaVersion() deliberately:
// StationClient has a member accessor of that exact name, which would
// silently win name lookup inside every member function and turn the
// initialisation below into "m_localSettingsSchema = m_localSettingsSchema".
// The compiler caught it as an unused-function warning; the behaviour it
// was hiding was a schema comparison against a permanent zero.
qint32 readLocalSettingsSchemaVersion()
{
    // BY NAME: both ends read the value stored under this literal key in
    // their OWN store. AppSettings::ensureSettingsAtVersion() is what
    // writes it, and it classifies OperatorLocal (SettingsScope.cpp has no
    // rule for it, and the default is OperatorLocal), so it never travels
    // in the settings snapshot and each side really does read its own.
    return static_cast<qint32>(
        AppSettings::instance()
            .value(QStringLiteral("SettingsSchemaVersion"), QStringLiteral("0"))
            .toString()
            .toInt());
}

/// Colon-separated uppercase SHA-256 hex pairs, byte-for-byte the form
/// CertificateStore::fingerprintSha256() prints. Computed here from
/// QSslCertificate::digest() rather than OpenSSL's X509_digest: both hash
/// the same DER encoding, so the strings match, and the client half has no
/// reason to link libcrypto.
QString formatFingerprint(const QByteArray& sha256Digest)
{
    QString out;
    out.reserve(sha256Digest.size() * 3);
    for (int i = 0; i < sha256Digest.size(); ++i) {
        if (i > 0) {
            out.append(QLatin1Char(':'));
        }
        out.append(QString::number(static_cast<quint8>(sha256Digest[i]), 16)
                       .rightJustified(2, QLatin1Char('0'))
                       .toUpper());
    }
    return out;
}

int idFromKey(const QByteArray& objectKey, const char* prefix)
{
    const QByteArray p(prefix);
    if (!objectKey.startsWith(p)) {
        return -1;
    }
    bool ok = false;
    const int id = objectKey.mid(p.size()).toInt(&ok);
    return ok ? id : -1;
}

QByteArray skewKey(const QByteArray& className, const QByteArray& property)
{
    return className + '.' + property;
}

/// RAII for m_applyingInbound. Save/restore rather than hardcoding false
/// on exit, for the same reason StateMirror::ApplyingGuard does: an apply
/// can reach a setter whose side effects lead back into another apply, and
/// an inner call clearing the flag would leak the outer call's remaining
/// notifies straight back to the station as echoes.
class InboundGuard {
public:
    explicit InboundGuard(bool& flag) : m_flag(flag), m_previous(flag) { m_flag = true; }
    ~InboundGuard() { m_flag = m_previous; }
    InboundGuard(const InboundGuard&) = delete;
    InboundGuard& operator=(const InboundGuard&) = delete;

private:
    bool& m_flag;
    bool m_previous;
};

} // namespace

StationClient::StationClient(RadioModel* radioModel, SettingsProxy* settingsProxy,
                             QObject* parent)
    : QObject(parent)
    , m_radioModel(radioModel)
    , m_settingsProxy(settingsProxy)
{
    m_localSettingsSchema = readLocalSettingsSchemaVersion();
    if (m_localSettingsSchema == 0) {
        // The observable trace of CoreInit::initialize() not having run
        // yet. See StationClient.h's ordering preconditions: installing
        // the settings backend before the migrations means this machine's
        // own migrated keys get pushed up to the station.
        qCWarning(lcStationClient)
            << "SettingsSchemaVersion is unset. StationClient is being constructed"
            << "before CoreInit::initialize() ran its settings migrations, which is"
            << "the ordering that leaks local migrated keys to the station.";
    }

    if (radioModel != nullptr && radioModel->role() != RadioModel::Role::Remote) {
        qCWarning(lcStationClient)
            << "StationClient was given a Role::Local RadioModel. Its capability"
            << "apply and connection-state entry points refuse on a local model,"
            << "so this session will connect and then drive nothing.";
    }

    m_outboundMirror = new StateMirror(this);
    connect(m_outboundMirror, &StateMirror::propertiesChanged, this,
            [this](const QByteArray& objectKey, const QList<MirrorUpdate>& updates) {
                // The echo guard, checked FIRST, before this handler asks
                // anything else about the change -- exactly where
                // StateMirror::onWatchedPropertyChanged checks its own.
                if (m_applyingInbound || !m_forwardLocalChanges) {
                    return;
                }
                QObject* object = m_objects.value(objectKey).data();
                if (object == nullptr) {
                    return;
                }
                const QByteArray className =
                    MirrorSchema::shortClassName(object->metaObject()->className());
                for (const MirrorUpdate& update : updates) {
                    // OUTBOUND is MirrorPolicy-gated: this is the exact
                    // direction that table describes, so a property the
                    // station would refuse is dropped here rather than
                    // sent and argued about.
                    if (!MirrorPolicy::inboundAllowed(className, update.name)) {
                        continue;
                    }
                    m_outboundCoalescer.update(objectKey, update);
                }
            });

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &StationClient::onHeartbeatTick);

    m_writeFlushTimer = new QTimer(this);
    m_writeFlushTimer->setInterval(kDefaultWriteFlushMs);
    connect(m_writeFlushTimer, &QTimer::timeout, this, &StationClient::onWriteFlushTick);

    // Task 19: the automatic-reconnect timer. Owned (parented to this,
    // dies with it), single-shot (armed fresh by scheduleReconnect() for
    // each attempt rather than ticking repeatedly), and stoppable from
    // anywhere that holds `this` -- never static QTimer::singleShot. See
    // the class comment's link-loss section.
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &StationClient::onReconnectTimeout);

    // ── The settings proxy's OUTBOUND half ───────────────────────────────
    //
    // SettingsProxy.h's own contract says it emits these "for a live
    // session (Task 18) to relay over the wire", and until this connect
    // existed neither signal had a consumer anywhere in src/. The
    // operator-visible shape of that gap: a remote GUI's Setup change
    // updates the optimistic cache, appears to take, never reaches the
    // station, and is silently reverted by the next snapshot.
    //
    // Origin tag read at emit time rather than captured, because
    // handleSettingsSnapshot() assigns it after the handshake and these
    // connects are made in the constructor.
    if (m_settingsProxy != nullptr) {
        connect(m_settingsProxy, &SettingsProxy::outboundWriteRequested, this,
                [this](const QString& key, const QVariant& value) {
                    if (m_settingsProxy.isNull()) {
                        return;
                    }
                    send(SessionMessages::settingsWrite(key, value.toString(),
                                                        m_settingsProxy->localOriginTag()));
                });
        connect(m_settingsProxy, &SettingsProxy::outboundRemoveRequested, this,
                [this](const QString& key) {
                    send(SessionMessages::settingsRemove(key));
                });
    }
}

StationClient::~StationClient() = default;

// ── Connecting ───────────────────────────────────────────────────────────

void StationClient::connectToStation(const QUrl& url, const QString& token,
                                     const QString& expectedFingerprint,
                                     bool allowUnpinned)
{
    // Fix round 1, Important 2. Every OTHER entry into connectToStation()
    // cancels a pending retry as a side effect of reaching attachTransport()
    // (which does this too, unconditionally, for the case where a caller
    // reconnects by hand while a backoff wait is still counting down). The
    // empty-fingerprint refusal below returns BEFORE ever reaching
    // dialStation() and therefore attachTransport(), so without this
    // explicit stop here, a retry armed by an earlier failed dial survived
    // an unrelated refused attempt at a DIFFERENT station untouched, and
    // would go on to silently redial the FIRST station once its backoff
    // elapsed -- an operator who tried station B and was told the
    // connection was refused would, moments later, find the GUI connected
    // to station A instead, unprompted. Stopped here, unconditionally,
    // before any other logic runs, so it also covers the refusal path.
    m_reconnectTimer->stop();

    // FIRST error wins for the rest of this connection attempt. A pinning
    // refusal is followed immediately by the socket errors it causes
    // ("the host name did not match", "remote host closed"), and reporting
    // the last of those to the operator would name a consequence instead
    // of the cause. Cleared here so a later attempt starts clean.
    m_lastError.clear();

    if (expectedFingerprint.isEmpty() && !allowUnpinned) {
        m_lastError = QStringLiteral(
            "No station certificate fingerprint to pin. Refusing to connect: an "
            "unpinned self-signed certificate authenticates nothing.");
        qCWarning(lcStationClient) << m_lastError;
        emit sessionEnded(m_lastError);
        return;
    }

    // A DELIBERATE, fresh attempt that is actually going to DIAL -- the
    // very first connect, or an operator manually reconnecting after
    // giving up -- always starts the backoff over. Moved below the
    // refusal check (fix round 1, Important 2): resetting it for a call
    // that never reaches dialStation() would silently reset the schedule
    // of a DIFFERENT, still-relevant retry sequence that happens to be
    // between attempts (timer briefly inactive, mid-redial) at the exact
    // moment an unrelated refusal runs. dialStation()'s own redial entry,
    // onReconnectTimeout(), deliberately does not reset this at all.
    m_reconnectAttempts = 0;

    dialStation(url, token, expectedFingerprint, allowUnpinned);
}

void StationClient::dialStation(const QUrl& url, const QString& token,
                                const QString& expectedFingerprint, bool allowUnpinned)
{
    // First error wins for THIS attempt -- see connectToStation()'s own
    // clear. A redial from onReconnectTimeout() is a new attempt and gets
    // its own clean slate too.
    m_lastError.clear();

    // A PIN THAT IS CONFIGURED IS A PIN THAT MUST BE CHECKED, and a scheme
    // with no TLS under it cannot check one. RemoteStationOptions::
    // isValidStationUrl accepts ws:// for a loopback bench run and its
    // rejection message advertises it, while connectToStation() above
    // refuses only an EMPTY fingerprint. So an operator who had pinned a
    // fingerprint correctly and typed ws:// got no TLS, therefore no
    // sslErrors, therefore no pin comparison anywhere, and then
    // handleHello() put the shared pre-shared token on the wire in
    // cleartext. Checked BEFORE the latch below so a refused URL is not
    // left behind for onReconnectTimeout() to redial.
    if (url.scheme().compare(QLatin1String("wss"), Qt::CaseInsensitive) != 0
        && !expectedFingerprint.isEmpty()) {
        m_lastError =
            QStringLiteral("Refusing to connect to %1: a station certificate "
                           "fingerprint is pinned, but \"%2\" carries no TLS, so "
                           "there is nothing to compare the fingerprint against "
                           "and the pairing token would travel in cleartext. Use "
                           "wss://.")
                .arg(url.toString(QUrl::RemovePassword), url.scheme());
        qCWarning(lcStationClient) << m_lastError;
        // Emitted directly rather than through endSession(), matching
        // connectToStation()'s own empty-fingerprint refusal: no session
        // has been attached yet, so endSession()'s m_sessionActive guard
        // would swallow this and the caller would hear nothing at all.
        emit sessionEnded(m_lastError);
        return;
    }

    // Whether this attempt owes a certificate comparison before it may
    // send the token. The conjunction is the SAME one the sslErrors bypass
    // below has always used: opting out requires both an explicit
    // allowUnpinned and no configured pin. attachTransport() turns this
    // into the per-attach m_pinSatisfied.
    m_pinRequired = !(expectedFingerprint.isEmpty() && allowUnpinned);

    // Latched (Task 19) so a later automatic retry can redial identically.
    // Parent design section 13: "onReconnectTimeout slot with latched host
    // and port."
    m_lastUrl = url;
    m_lastFingerprint = expectedFingerprint;
    m_lastAllowUnpinned = allowUnpinned;

    auto* socket = new QWebSocket();
    // Capped before the socket is ever opened. A pinned certificate proves
    // WHO the station is, not that it will behave; see
    // kMaxIncomingMessageBytes for how the number was derived from the
    // connect-time settings snapshot.
    auto* transport = new WebSocketTransport(socket, kMaxIncomingMessageBytes);
    // Task 19 fix: captured so the two lambdas below can tell a STALE
    // socket's asynchronous signal apart from the current one's. See the
    // class comment's link-loss section for the defect this closes --
    // the Task 18 review found it and graded it Minor only because
    // nothing called connectToStation() (and now dialStation()) twice on
    // one client before this task's automatic reconnect existed to do
    // exactly that.
    const QPointer<SessionTransport> transportGuard(transport);
    const QString pinned = expectedFingerprint.toUpper();

    connect(socket, &QWebSocket::sslErrors, this,
            [this, socket, pinned, allowUnpinned, transportGuard](const QList<QSslError>& errors) {
                // A stale socket's error must not act on whatever session
                // is CURRENT by the time it arrives -- the same guard
                // onTransportClosed() uses, applied here because these
                // two lambdas are connected to the QWebSocket, not the
                // SessionTransport wrapping it, so attachTransport()'s
                // disconnect(stale, ...) release does not reach them.
                if (transportGuard.isNull() || transportGuard.data() != m_transport) {
                    return;
                }
                if (pinned.isEmpty() && allowUnpinned) {
                    socket->ignoreSslErrors(errors);
                    return;
                }
                // The comparison itself now lives in ensurePinSatisfied(),
                // which this path shares with the connected() handler
                // below and with handleHello()'s gate. Keeping it here as
                // well as there is what makes a MISMATCH surface at the
                // earliest possible moment -- mid-handshake, before the
                // socket is even usable -- while the other two callers are
                // what make the comparison happen AT ALL on a handshake
                // that produced no errors to report.
                if (!ensurePinSatisfied()) {
                    return;
                }
                // The pinned fingerprint IS the identity check (parent
                // design section 10.5), so chain-trust errors that are
                // wholly explained by self-signing are ignored. Validity
                // dates are NOT: an expired certificate on a pinned
                // fingerprint still means somebody's clock is wrong, and
                // silently accepting it hides that.
                QList<QSslError> ignorable;
                for (const QSslError& error : errors) {
                    if (error.error() == QSslError::CertificateExpired
                        || error.error() == QSslError::CertificateNotYetValid) {
                        continue;
                    }
                    ignorable.append(error);
                }
                socket->ignoreSslErrors(ignorable);
            });

    // THE fix for the second reachable instance of the pinning gap.
    // QWebSocket::sslErrors fires ONLY when the handshake produced errors,
    // so a handshake the client's own trust store already accepts -- a
    // corporate or antivirus MITM root, or a genuine DV certificate issued
    // for a dynamic-DNS station name -- never reached the comparison at
    // all. That is precisely the case pinning exists for. connected()
    // fires on every successful handshake, error-free ones included, and
    // by then sslConfiguration().peerCertificate() is populated.
    //
    // This is belt to handleHello()'s braces rather than a replacement for
    // it: Qt emits connected() before it delivers any frame on the same
    // socket, so this normally runs first, but the token is gated on
    // m_pinSatisfied at the one place it is actually sent, so the ordering
    // does not have to be relied upon.
    connect(socket, &QWebSocket::connected, this, [this, transportGuard]() {
        if (transportGuard.isNull() || transportGuard.data() != m_transport) {
            return;
        }
        ensurePinSatisfied();
    });

    connect(socket, &QWebSocket::errorOccurred, this,
            [this, socket, transportGuard](QAbstractSocket::SocketError) {
                if (transportGuard.isNull() || transportGuard.data() != m_transport) {
                    return;
                }
                // First error wins -- see dialStation()'s clear above.
                if (m_lastError.isEmpty()) {
                    m_lastError = socket->errorString();
                }
                qCWarning(lcStationClient)
                    << "Station connection error:" << socket->errorString();
                // A refused or unreachable station emits this and may never
                // emit disconnected at all, so waiting for a close would
                // leave the caller with no signal whatsoever. endSession()
                // is idempotent per attach, so a socket error DURING a live
                // session that is followed by a real close still reports
                // once. A socket-level error is retry-eligible (Task 19):
                // this is the "station is down, or restarting" case
                // automatic reconnect exists for.
                endSession(m_lastError, /*attemptReconnect=*/true);
            });

    attachTransport(transport, token);
    socket->open(url);
}

// The one place the pinned fingerprint is compared, called from three:
// the sslErrors handler (earliest possible refusal on a handshake that
// reported problems), the connected() handler (every OTHER handshake,
// which is the case the old code never checked at all), and handleHello()
// immediately before the token would go out (the gate that makes the
// property hold regardless of which of the other two ran, or whether
// either did).
//
// Idempotent and cheap after the first success: m_pinSatisfied latches.
//
// A refusal here is a SECURITY refusal, not a transient link failure, so
// it is never retry-eligible. Redialing the same latched station that is
// presenting the same wrong certificate cannot converge, and retrying
// forever against an active MITM is worse than stopping.
bool StationClient::ensurePinSatisfied()
{
    if (m_pinSatisfied) {
        return true;
    }

    auto* wsTransport = qobject_cast<WebSocketTransport*>(m_transport);
    QWebSocket* socket = wsTransport != nullptr ? wsTransport->socket() : nullptr;
    const QSslCertificate peer = socket != nullptr
                                     ? socket->sslConfiguration().peerCertificate()
                                     : QSslCertificate();

    if (peer.isNull()) {
        // A pin is configured and the link cannot produce a certificate to
        // check it against. dialStation() refuses a non-TLS scheme up
        // front, so reaching this means something stranger: a transport
        // that is not a WebSocketTransport at all, or a wss socket whose
        // peer certificate is somehow absent. Refuse rather than fall
        // through, because falling through is what sends the token.
        m_lastError = QStringLiteral(
            "Refusing to authenticate: a station certificate fingerprint is "
            "pinned, but this link presented no certificate to compare it "
            "against.");
        qCWarning(lcStationClient) << m_lastError;
        // endSession FIRST, abort second, for the same reason
        // disconnectFromStation() documents for closeLink(): abort()
        // emits errorOccurred SYNCHRONOUSLY, that handler calls
        // endSession(attemptReconnect = true), and endSession is
        // once-per-attach -- so aborting first let the socket error win
        // the race and schedule a reconnect for a refusal this function
        // has just declared non-retryable. Observed in the log as
        // "Scheduling reconnect attempt 1" immediately after a fingerprint
        // mismatch.
        endSession(m_lastError, /*attemptReconnect=*/false);
        if (socket != nullptr) {
            socket->abort();
        }
        return false;
    }

    const QString pinned = m_lastFingerprint.toUpper();
    const QString actual = formatFingerprint(peer.digest(QCryptographicHash::Sha256));
    if (actual != pinned) {
        m_lastError = QStringLiteral("Station certificate fingerprint does not match. "
                                     "Expected %1, got %2.")
                          .arg(pinned, actual);
        qCWarning(lcStationClient) << m_lastError;
        // See the ordering note above: endSession, then abort.
        endSession(m_lastError, /*attemptReconnect=*/false);
        socket->abort();
        return false;
    }

    m_pinSatisfied = true;
    return true;
}

void StationClient::startSession(SessionTransport* transport, const QString& token,
                                 const QString& expectedFingerprint)
{
    // A pin this caller states is a pin this session owes, exactly as on
    // the dial path. With no fingerprint (the default, and every adopted
    // transport in the tree today) there is nothing to compare and nothing
    // to require. Set BEFORE attachTransport(), which is what turns it
    // into this attach's m_pinSatisfied, and latched into
    // m_lastFingerprint because ensurePinSatisfied() reads it from there.
    m_pinRequired = !expectedFingerprint.isEmpty();

    // Fix round 1, Minor 3. Without this, a client that once dialed via
    // connectToStation() (latching m_lastUrl to something real) and LATER
    // runs a startSession()-based seam session -- this suite's own pattern
    // for the manual-reconnect half of the link-loss narrative, and a
    // legitimate production sequence too if a caller ever mixes the two
    // entry points -- would keep the STALE latch from the earlier real
    // dial. A retry-eligible close of the SEAM session would then
    // scheduleReconnect() find m_lastUrl still valid and silently redial
    // the earlier, unrelated target. Invalidating it here is what makes
    // the class comment's claim -- "nothing to redial" for a
    // startSession()-based session -- actually true rather than true only
    // for a client that has never dialed at all.
    //
    // m_lastFingerprint is the exception among the three: it is not a
    // redial target, it is what ensurePinSatisfied() compares against, so
    // it takes this call's own value rather than being blanked. With the
    // default empty argument that is byte-identical to the clear this
    // replaced.
    m_lastUrl.clear();
    m_lastFingerprint = expectedFingerprint;
    m_lastAllowUnpinned = false;
    attachTransport(transport, token);
}

void StationClient::attachTransport(SessionTransport* transport, const QString& token)
{
    if (transport == nullptr) {
        return;
    }

    // RELEASE THE OLD LINK FIRST. Overwriting m_transport without this was
    // a reconnect defect waiting for Task 19: WebSocketTransport::closeLink
    // closes ASYNCHRONOUSLY, so "heartbeat timeout, reconnect from the
    // slot, the old socket's disconnected arrives a moment later" drove the
    // BRAND NEW session to Disconnected, stopped both timers and called
    // setReady(false). Every reconnect also leaked a WebSocketTransport and
    // its QWebSocket, still connected to onTransportText.
    //
    // Disconnecting every signal from the old transport to this object
    // FIRST is what makes the subsequent closeLink() safe: the close it
    // provokes can no longer reach onTransportClosed(). onTransportClosed()
    // additionally ignores anything that is not the current transport (see
    // there), so the two protections are independent -- the same
    // erase-then-look-up discipline StationServer::dropPeer already uses.
    if (m_transport != nullptr && m_transport != transport) {
        SessionTransport* stale = m_transport;
        m_transport = nullptr;
        disconnect(stale, nullptr, this, nullptr);
        stale->closeLink(QStringLiteral("replaced by a newer session"));
        stale->deleteLater();
    }

    // Task 19: a fresh attach -- whether from a manual reconnect or from
    // onReconnectTimeout()'s own redial -- supersedes any retry this
    // client might independently have pending (e.g. a caller reconnecting
    // by hand, via startSession(), while a connectToStation()-originated
    // backoff wait was still counting down). Stopping here, unconditionally,
    // is what keeps that stray timer from firing a redial on top of a
    // session that is already re-establishing.
    m_reconnectTimer->stop();

    transport->setParent(this);
    m_transport = transport;
    m_token = token;
    m_pingsAwaitingPong = 0;
    m_linkUp = false;
    m_sessionActive = true;

    // Per-attach, never carried across one. A reconnect re-dials and gets
    // a fresh TLS handshake, possibly against a different certificate, so
    // a pin satisfied by the PREVIOUS session says nothing about this one.
    // The caller (dialStation or startSession) has already set
    // m_pinRequired for this attempt.
    m_pinSatisfied = !m_pinRequired;

    // Task 19: a new epoch for every attach, including the first (so the
    // first session is epoch 1; 0 means "never attached"). See
    // sessionEpoch()'s doc comment.
    ++m_sessionEpoch;

    // These three describe THIS session. Carrying them across a reconnect
    // would let a difference the station has since fixed keep showing up
    // in a diagnostic Task 20's bench is meant to trust.
    m_schemaOnlyOnStation.clear();
    m_schemaOnlyLocal.clear();
    m_unapplied.clear();
    m_pendingStationSchemas.clear();

    connect(transport, &SessionTransport::textReceived, this,
            &StationClient::onTransportText);
    connect(transport, &SessionTransport::pongReceived, this,
            [this]() { m_pingsAwaitingPong = 0; });
    connect(transport, &SessionTransport::closed, this, &StationClient::onTransportClosed);

    // The heartbeat deliberately does NOT start here. A wss dial can take
    // seconds, and a heartbeat counting missed pongs across a socket that
    // has not finished connecting reports a slow dial as a dead station.
    // It starts on the first inbound frame instead (onTransportText), which
    // is the station's own Hello and therefore proof the link carries
    // traffic in both directions.
}

void StationClient::disconnectFromStation(const QString& reason, bool attemptReconnect)
{
    // Task 19: cancel a PENDING retry even when no session is active at
    // all -- the backoff-wait state has m_sessionActive already false
    // (the session it was about already ended), so endSession()'s own
    // guard below would skip the block that normally stops this timer.
    // This is the other half of what makes the timer genuinely
    // cancellable rather than merely stoppable-from-inside-a-live-session:
    // parent design section 13, "ICE restart and operator-initiated
    // disconnect both need [cancellability]."
    if (m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }

    // endSession FIRST, closeLink second. A transport can deliver its
    // closed() signal synchronously (the in-process one does, and nothing
    // forbids it), and endSession() is once-per-attach, so closing first
    // let the close handler's generic "link closed" win the race and the
    // caller was told that instead of "heartbeat timeout" -- the specific
    // reason, thrown away by ordering alone. endSession() touches no
    // transport, so running it first is safe.
    endSession(reason, attemptReconnect);
    if (m_transport != nullptr) {
        m_transport->closeLink(reason);
    }
}

void StationClient::onTransportClosed()
{
    // Ignore a close from a transport this client has already moved on
    // from. sender() is null when this is called directly rather than
    // through the signal, which is a legitimate internal path.
    if (sender() != nullptr && sender() != m_transport) {
        return;
    }
    // Task 19: a plain transport close with no station-sent reason is
    // exactly the case automatic reconnect exists for -- "kill the
    // daemon" (a clean TCP close) is the bench scenario the parent task
    // brief opens with, and it looks exactly like this: no SessionEnd
    // message (nothing was left alive to send one), just the socket going
    // away. Retry-eligible.
    endSession(m_lastError.isEmpty() ? QStringLiteral("link closed") : m_lastError,
              /*attemptReconnect=*/true);
}

// The single place a session ends, so sessionEnded() fires AT MOST ONCE
// per attach no matter which of the six paths got here (peer close, socket
// error, heartbeat timeout, station SessionEnd, version refusal, auth
// refusal). Not "exactly once" (fix round 1, Minor 7): an attach that is
// SUPERSEDED by a fresh attachTransport() before its own endSession() ever
// runs is released silently, with no sessionEnded for it at all -- see
// the header's own note on this method for the covering test.
//
// The previous shape emitted only `if (m_handshakeComplete)`, which swallowed
// the two failures that matter most:
//
//   - A failed INITIAL connect (station down, wrong port, TLS refused)
//     produced no signal at all, for a full 40 to 60 second heartbeat
//     window. v0.5.1 shipped "connection state stuck Connected on failed
//     initial connect"; this is the same bug class, so it gets a named
//     mechanism rather than a gate that happens to be true on the paths
//     someone tested.
//   - The client's own heartbeat timeout, because disconnectFromStation()
//     cleared m_handshakeComplete before the close handler read it.
//
// Task 19 extends this with `attemptReconnect` and the mirror teardown
// below. See the class comment's link-loss section for the full
// contract this enforces: the mirror registry does not survive a link
// loss, RadioModel's own state does (retained, not reset -- design doc
// section 13), and only a closure that WANTS a retry re-arms the
// automatic reconnect timer.
void StationClient::endSession(const QString& reason, bool attemptReconnect)
{
    if (!m_sessionActive) {
        return;  // already reported for this attach
    }
    m_sessionActive = false;

    m_handshakeComplete = false;
    m_authenticated = false;
    m_forwardLocalChanges = false;
    m_linkUp = false;
    m_heartbeatTimer->stop();
    m_writeFlushTimer->stop();

    // Fix round 1, Important 1: disconnect the dead transport's signals to
    // this object. Without this, m_transport stays fully wired
    // (onTransportText, onTransportClosed, the pongReceived lambda) for
    // the entire stale window even though the session it belonged to has
    // just ended -- onTransportText() has no m_sessionActive gate of its
    // own (only the heartbeat-start check does), so a frame arriving late
    // on this SAME transport is dispatched in full, and a buffered
    // SnapshotComplete would silently re-set m_handshakeComplete /
    // m_everConnected / m_forwardLocalChanges with no sessionEnded ever
    // firing for the attach that just ended. Exactly the case this
    // subsystem exists to prevent: a link that resumes after a heartbeat
    // timeout, delivering a frame that was already in flight when the
    // timeout was declared. Mirrors attachTransport()'s identical
    // disconnect for a superseded transport. Deliberately does NOT null
    // m_transport: disconnectFromStation()'s subsequent closeLink() call
    // is a direct call on the object itself, not a signal delivery, and is
    // unaffected by severing its signals TO this object; the pointer stays
    // valid until the next attachTransport() releases it the same way a
    // superseded transport is released.
    if (m_transport != nullptr) {
        disconnect(m_transport, nullptr, this, nullptr);
    }

    // Task 19 step 2: mirror teardown. All three describe THIS session and
    // none may survive it:
    //
    //   - m_objects (mirroredObjectKeys()/mirroredObject()) is the wire-key
    //     -> live-object registry. Its own doc comment says "Task 19 tears
    //     this down on link loss." Clearing it is what makes handleDelta()
    //     drop every further inbound frame for an unknown key (there is
    //     nothing to route it to) rather than silently keep applying
    //     traffic from a session that no longer exists, and what makes
    //     mirroredObjectKeys() correctly read empty while disconnected.
    //   - m_outboundMirror->unwatchAll() stops the outbound watcher.
    //     m_forwardLocalChanges (already false above) already prevents any
    //     local change from being forwarded while disconnected, so this is
    //     not independently load-bearing for "drops writes" -- it is
    //     hygiene: a long disconnect should not leave stale QMetaObject
    //     connections to objects that may be destroyed by some other path
    //     before reconnect, and it is the symmetric counterpart to the
    //     re-watching handleCapabilities()/resolveOrCreate() do on the
    //     next attach.
    //   - m_outboundCoalescer.clear() IS load-bearing: without it, a local
    //     edit that was marked dirty but never reached a flush before the
    //     link died would sit pending, and once the NEXT session's write
    //     flush timer resumes, onWriteFlushTick() would re-resolve it
    //     against the (by then reconnected) live model and send it as a
    //     property.write -- telling the fresh station its own
    //     just-applied value back, and doing so under the guise of a
    //     "fresh snapshot" that is supposed to have no leftover cruft from
    //     the session before it.
    //
    // RadioModel's own state -- SliceModel::frequency() and the rest -- is
    // deliberately NOT touched here. No removeSlice() call, no reset to a
    // default. Section 13: "the client retains last-known state". A
    // reconnect ADOPTS the retained SliceModel objects under the station's
    // ids (resolveOrCreate(), unchanged by this task), which is what lets
    // a GUI holding a raw pointer to one survive a reconnect unchanged.
    m_objects.clear();
    m_outboundMirror->unwatchAll();
    m_outboundCoalescer.clear();

    if (!m_settingsProxy.isNull()) {
        m_settingsProxy->setReady(false);
    }
    if (!m_radioModel.isNull()) {
        m_radioModel->setStationConnectionState(ConnectionState::Disconnected);
    }
    emit sessionEnded(reason);

    // Task 19 step 3: automatic reconnect. Only for a closure that WANTS
    // one, and only when there is something to redial -- a
    // startSession()-based session (every non-TLS test in this suite, and
    // the production protocol-seam path) never latches a URL, so
    // scheduleReconnect() is simply never reached for it. See the class
    // comment for why "the daemon spoke with an explicit reason" (version
    // refusal, auth refusal, preemption, peer-limit refusal --
    // attemptReconnect false on all of those call sites) is deliberately
    // NOT retried.
    if (attemptReconnect && m_lastUrl.isValid()) {
        scheduleReconnect();
    }
}

// ── Heartbeat ────────────────────────────────────────────────────────────

void StationClient::setHeartbeatIntervalMs(int ms)
{
    m_heartbeatIntervalMs = ms;
    if (ms <= 0) {
        qCWarning(lcStationClient)
            << "Heartbeat disabled. A station that dies without closing the TCP "
               "connection will not be detected.";
        m_heartbeatTimer->stop();
        return;
    }
    m_heartbeatTimer->setInterval(ms);
    // m_linkUp, not m_transport: see attachTransport() for why the
    // heartbeat waits for the first inbound frame.
    if (m_linkUp) {
        m_heartbeatTimer->start();
    }
}

void StationClient::setMaxMissedPongs(int misses)
{
    m_maxMissedPongs = misses < 1 ? 1 : misses;
}

void StationClient::onHeartbeatTick()
{
    if (m_transport == nullptr) {
        m_heartbeatTimer->stop();
        return;
    }
    if (m_pingsAwaitingPong >= m_maxMissedPongs) {
        qCWarning(lcStationClient)
            << "Station missed" << m_pingsAwaitingPong
            << "consecutive pongs; declaring the link dead";
        emit stationHeartbeatTimeout();
        // Task 19 step 3a: the case that motivated pulling the heartbeat
        // into R2 at all -- a peer that stops responding WITHOUT closing.
        // Retry-eligible: there is no daemon-sent reason here, just
        // silence, the same as a plain transport close.
        disconnectFromStation(QStringLiteral("heartbeat timeout"), /*attemptReconnect=*/true);
        return;
    }
    ++m_pingsAwaitingPong;
    m_transport->ping();
}

// ── Reconnect (Task 19) ──────────────────────────────────────────────────

void StationClient::setReconnectBackoffUnitMs(int ms)
{
    m_reconnectBackoffUnitMs = ms > 0 ? ms : 1;
}

bool StationClient::isReconnectPending() const
{
    return m_reconnectTimer->isActive();
}

void StationClient::scheduleReconnect()
{
    // Same schedule as PgxlConnection.cpp:30's kBackoffSec and
    // TgxlConnection.cpp:30's kTgxlBackoffSec ({1, 2, 5, 10, 30, 60} in
    // both, verified against this tree), reused for consistency with an
    // already-shipped, human-reviewed choice. NOT reused: that class's
    // static QTimer::singleShot mechanism -- see the class comment's
    // link-loss section for why parent design section 13 calls that out
    // by name as the thing not to copy. Scaled by m_reconnectBackoffUnitMs
    // (production default 1000, i.e. real seconds) rather than exposed as
    // a raw ms table, so a test can shrink the whole schedule
    // proportionally with one setter instead of duplicating six numbers.
    static constexpr int kReconnectBackoffSteps[] = { 1, 2, 5, 10, 30, 60 };
    static constexpr int kReconnectBackoffStepCount =
        static_cast<int>(sizeof(kReconnectBackoffSteps) / sizeof(kReconnectBackoffSteps[0]));

    const int idx = std::min(m_reconnectAttempts, kReconnectBackoffStepCount - 1);
    const int delayMs = kReconnectBackoffSteps[idx] * m_reconnectBackoffUnitMs;
    ++m_reconnectAttempts;

    qCInfo(lcStationClient) << "Scheduling reconnect attempt" << m_reconnectAttempts
                            << "in" << delayMs << "ms";
    // Fix round 1, Minor 8: start BEFORE emitting. A directly-connected
    // slot that reacts to reconnectScheduled() by cancelling (e.g. an
    // operator's own "stop retrying" control) must see an ALREADY-ARMED
    // timer to cancel; emitting first would let such a slot's
    // isReconnectPending() read false and its own stop() call be
    // overridden a moment later by the start() below.
    m_reconnectTimer->start(delayMs);
    emit reconnectScheduled(m_reconnectAttempts, delayMs);
}

void StationClient::onReconnectTimeout()
{
    if (!m_lastUrl.isValid()) {
        // Defensive: disconnectFromStation() and attachTransport() both
        // stop this timer unconditionally, so a fired-with-nothing-to-
        // redial timeout should be unreachable. Not treated as a bug if
        // it somehow happens -- just nothing to do.
        return;
    }
    dialStation(m_lastUrl, m_token, m_lastFingerprint, m_lastAllowUnpinned);
}

// ── Inbound dispatch ─────────────────────────────────────────────────────

void StationClient::onTransportText(const QByteArray& wire)
{
    // First frame from the station is proof the link carries traffic in
    // both directions, which is the point at which a missed-pong count
    // starts meaning something. See attachTransport().
    if (!m_linkUp) {
        m_linkUp = true;
        if (m_heartbeatIntervalMs > 0 && m_sessionActive) {
            m_heartbeatTimer->start();
        }
    }

    SessionMessage message;
    if (!SessionMessages::decode(wire, &message)) {
        qCWarning(lcStationClient) << "Undecodable message from station; ignoring";
        return;
    }

    switch (message.kind) {
    case SessionMessageKind::Hello:
        handleHello(message);
        break;
    case SessionMessageKind::AuthResult:
        handleAuthResult(message);
        break;
    case SessionMessageKind::Capabilities:
        handleCapabilities(message);
        break;
    case SessionMessageKind::SettingsSnapshot:
        handleSettingsSnapshot(message);
        break;
    case SessionMessageKind::Schema:
        handleSchema(message);
        break;
    case SessionMessageKind::ObjectCreate:
        handleObjectCreate(message);
        break;
    case SessionMessageKind::ObjectDestroy:
        handleObjectDestroy(message);
        break;
    case SessionMessageKind::Delta:
        handleDelta(message);
        break;
    case SessionMessageKind::SnapshotComplete:
        m_handshakeComplete = true;
        // Task 19: this is a PROVEN success, the moment isStale() (once it
        // has ever been true) goes false again, and the only place that
        // resets the reconnect backoff on the strength of an actually
        // working session rather than merely a deliberate new attempt
        // (connectToStation() resets it too, but for a DIFFERENT reason --
        // see its own comment). Without this reset, a session that
        // survived for hours after a rocky initial connect would have its
        // NEXT, unrelated drop start retrying at whatever the ORIGINAL
        // struggle's backoff had climbed to, possibly the 60 s ceiling,
        // rather than at the first, fast step.
        m_everConnected = true;
        m_reconnectAttempts = 0;
        // Only now: everything that moved before this point was the
        // station's own burst landing, and forwarding any of it would tell
        // the station its own state back.
        m_forwardLocalChanges = true;
        m_writeFlushTimer->start();
        qCInfo(lcStationClient) << "Session established with" << m_capabilities.stationName;
        emit handshakeComplete();
        break;
    case SessionMessageKind::CommandResult:
        emit commandResult(message.commandId, message.accepted, message.reason);
        break;
    case SessionMessageKind::SettingsValue:
        handleSettingsValue(message);
        break;
    case SessionMessageKind::SettingsReject:
        handleSettingsReject(message);
        break;
    case SessionMessageKind::SessionEnd:
        qCWarning(lcStationClient) << "Station ended the session:" << message.reason
                                   << (message.retryable ? "(retryable)" : "(permanent)");
        m_lastError = message.reason;
        // The station's own classification, not this end's guess at one
        // and not a match against its English prose. Every station-sent
        // refusal used to take disconnectFromStation()'s default of false,
        // so "Station is at its concurrent-connection limit" -- a cap the
        // header explicitly sizes to be hit BY a reconnecting client --
        // permanently disarmed automatic reconnect. See
        // SessionMessage::retryable and the classification argued at each
        // StationServer call site.
        disconnectFromStation(message.reason, message.retryable);
        break;
    default:
        qCWarning(lcStationClient) << "Ignoring station message of client-only kind:"
                                   << SessionMessages::kindName(message.kind);
        break;
    }
}

void StationClient::handleHello(const SessionMessage& message)
{
    // Parent design section 7.0's version policy, applied from this side
    // too rather than trusting the station to have applied it: a station
    // several majors ahead may not even recognise this client's Hello.
    if (message.protocolMajor != kSessionProtocolMajor) {
        m_lastError = QStringLiteral(
                          "Protocol major version mismatch: this client speaks %1.%2, "
                          "the station speaks %3.%4. A differing major means an "
                          "incompatible wire contract.")
                          .arg(kSessionProtocolMajor)
                          .arg(kSessionProtocolMinor)
                          .arg(message.protocolMajor)
                          .arg(message.protocolMinor);
        qCWarning(lcStationClient) << m_lastError;
        disconnectFromStation(m_lastError);
        return;
    }

    m_agreedMinor = std::min(kSessionProtocolMinor, message.protocolMinor);

    // Schema version skew, caught BY NAME at handshake -- see
    // readLocalSettingsSchemaVersion() for what "by name" means. Reported
    // rather than refused: this version governs the shape of each side's
    // OWN local settings file, not the wire contract, and section 7.0's
    // refusal rule is about the protocol major alone.
    m_stationSettingsSchema = message.settingsSchemaVersion;
    m_settingsSchemaSkew = m_stationSettingsSchema != m_localSettingsSchema;
    if (m_settingsSchemaSkew) {
        qCWarning(lcStationClient)
            << "Settings schema skew: this client is at" << m_localSettingsSchema
            << "the station is at" << m_stationSettingsSchema
            << "-- station settings may not round-trip as expected";
    }

    // THE GATE. This is the one line on which the pre-shared token leaves
    // this process, so this is where the pin has to have been checked --
    // not in whichever handler happened to fire, and not only on a
    // handshake that reported errors. ensurePinSatisfied() has normally
    // already latched by now (connected() precedes any inbound frame), so
    // in the ordinary case this costs one bool test; when it has not, it
    // does the comparison here rather than letting the secret out.
    if (!ensurePinSatisfied()) {
        return;
    }

    send(SessionMessages::hello(kSessionProtocolMajor, kSessionProtocolMinor,
                                m_localSettingsSchema, peerNameForThisProcess()));
    send(SessionMessages::authRequest(m_token));
}

void StationClient::handleAuthResult(const SessionMessage& message)
{
    if (!message.accepted) {
        m_lastError = message.reason;
        qCWarning(lcStationClient) << "Station refused authentication:" << message.reason;
        // A WRONG TOKEN stays permanent; a RATE-LIMITED refusal does not.
        // The station's rate limiter is global rather than per-peer, so
        // somebody else's five bad guesses inside 60 s refuse the operator
        // too (TokenStore.h:44-48). Treated as permanent, that was a
        // stranger being able to lock the operator out of their own
        // station until they noticed and reconnected by hand. Retrying is
        // safe here precisely BECAUSE the wrong-token case is not retried:
        // this client only comes back when the station said the condition
        // was temporary.
        disconnectFromStation(message.reason, message.retryable);
        return;
    }
    m_authenticated = true;
}

void StationClient::handleCapabilities(const SessionMessage& message)
{
    m_capabilities = StationCapabilities::fromUpdates(message.updates);

    if (m_capabilities.effectiveMaxSlices < m_capabilities.boardMaxSlices) {
        qCInfo(lcStationClient)
            << "Station is limiting slices to" << m_capabilities.effectiveMaxSlices
            << "of the board's" << m_capabilities.boardMaxSlices
            << "-- a daemon capacity decision, not a radio limit";
    }

    if (m_radioModel.isNull()) {
        return;
    }
    // The step that makes three earlier tasks mean anything: identity,
    // board capabilities, the EFFECTIVE slice limit, userDdcCount, and the
    // connection state, all through one production entry point.
    m_radioModel->applyStationCapabilities(m_capabilities);

    // The singletons exist from RadioModel's own construction, so they can
    // be mapped and watched as soon as capabilities land, rather than
    // waiting for an object.create that will never come for them (the
    // daemon watches them directly; only slices have a lifecycle).
    const QByteArray radioKey(kRadioKey);
    m_objects.insert(radioKey, m_radioModel.data());
    watchForOutbound(radioKey, m_radioModel.data());

    const QByteArray transmitKey(kTransmitKey);
    m_objects.insert(transmitKey, &m_radioModel->transmitModel());
    watchForOutbound(transmitKey, &m_radioModel->transmitModel());

    if (m_radioModel->tunerModel() != nullptr) {
        const QByteArray tunerKey(kTunerKey);
        m_objects.insert(tunerKey, m_radioModel->tunerModel());
        watchForOutbound(tunerKey, m_radioModel->tunerModel());
    }

    const QList<PanadapterModel*> pans = m_radioModel->panadapters();
    for (int i = 0; i < pans.size(); ++i) {
        const QByteArray key = QByteArray(kPanKeyPrefix) + QByteArray::number(i);
        m_objects.insert(key, pans.at(i));
        watchForOutbound(key, pans.at(i));
    }
}

void StationClient::handleSettingsSnapshot(const SessionMessage& message)
{
    if (m_settingsProxy.isNull()) {
        return;
    }
    QMap<QString, QString> data;
    for (const MirrorUpdate& entry : message.updates) {
        data.insert(QString::fromUtf8(entry.name), entry.value.toString());
    }

    const bool firstSnapshot = !m_settingsProxy->hasReceivedSnapshot();
    m_settingsProxy->applySnapshot(data);

    // Ready only NOW, never earlier. SliceModel, NotchModel,
    // FilterPresetStore and TciServer all do contains()-then-seed against
    // Station-classified prefixes in their constructors, and the only
    // thing stopping them writing ship defaults into the STATION's store
    // is that writes are dropped while not ready (SettingsProxy.h's own
    // "record, not fix" section). Those constructors ran when the
    // RadioModel this class was handed was built, which is necessarily
    // before now.
    m_settingsProxy->setReady(true);
    m_settingsProxy->setLocalOriginTag(
        QStringLiteral("client-%1").arg(reinterpret_cast<quintptr>(this), 0, 16));

    if (firstSnapshot && !m_radioModel.isNull()) {
        // Task 15 handoff: FaultLog loads its ring buffer in its
        // constructor, which on a remote client runs long before any
        // station settings exist, so both of RadioModel's instances come
        // up empty and stay that way forever. reload() was made public for
        // exactly this call and had zero callers until now. Noticing this
        // by hand needs PGXL or TGXL hardware, which is why it is wired
        // here rather than left to a bench to find.
        if (m_radioModel->pgxlFaultLog() != nullptr) {
            m_radioModel->pgxlFaultLog()->reload();
        }
        if (m_radioModel->tgxlFaultLog() != nullptr) {
            m_radioModel->tgxlFaultLog()->reload();
        }
    }
}

void StationClient::handleSettingsValue(const SessionMessage& message)
{
    if (m_settingsProxy.isNull()) {
        return;
    }
    // An EMPTY entry list means the key is GONE from the station's store,
    // the same absence convention handleSettingsReject below already
    // reads (whole-branch review, Important 4). Before this, the daemon
    // sent a real entry holding "" for a removal and this method cached
    // it, so the client reported contains() true and value(key, default)
    // "" for a key the station did not have.
    if (message.updates.isEmpty()) {
        m_settingsProxy->applyRemoteRemoval(QString::fromUtf8(message.objectKey));
        return;
    }
    m_settingsProxy->applyRemoteValue(QString::fromUtf8(message.objectKey),
                                      message.updates.first().value, message.originTag);
}

void StationClient::handleSettingsReject(const SessionMessage& message)
{
    if (m_settingsProxy.isNull()) {
        return;
    }
    // An EMPTY entry list means the station has nothing for this key
    // either: proven-unset, which SettingsProxy::applyRejection()
    // distinguishes from a restored empty string via an invalid QVariant.
    const QVariant restored =
        message.updates.isEmpty() ? QVariant() : message.updates.first().value;
    m_settingsProxy->applyRejection(QString::fromUtf8(message.objectKey), restored);
}

// ── The mirror, inbound ──────────────────────────────────────────────────

void StationClient::handleSchema(const SessionMessage& message)
{
    // Task 18 step 8: schema skew caught by NAME comparison at handshake.
    // MirrorSchema's ordinals are dense and per-class, so two builds that
    // declare different property sets assign DIFFERENT ordinals to the
    // same names -- which is why every MirrorUpdate carries its name as
    // well, and why comparing names is the check that actually means
    // something. A property present on one side and absent on the other is
    // reported, not fatal: the overlap still round-trips correctly, and
    // refusing the whole session over one unknown property would make
    // every future property addition a flag day.
    const QMetaObject* mo = nullptr;
    QSet<QByteArray> stationNames;
    for (const SessionSchemaField& field : message.fields) {
        stationNames.insert(field.name);
    }

    // Resolve the class through an object of it we already hold, if any:
    // MirrorSchema is keyed on QMetaObject and this class has no static
    // name-to-metaobject table (and should not grow one -- the mirrored
    // allowlist already lives in MirrorSchema).
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        QObject* object = it.value().data();
        if (object == nullptr) {
            continue;
        }
        if (MirrorSchema::shortClassName(object->metaObject()->className())
            == message.className) {
            mo = object->metaObject();
            break;
        }
    }
    if (mo == nullptr) {
        // Nothing of this class exists here YET. A slice's schema always
        // arrives before its first object.create, so recording every
        // station name as skew here would report the entire SliceModel
        // property table as missing on a client that in fact declares all
        // of it. Defer instead, and run the real comparison the moment an
        // instance exists (handleObjectCreate).
        m_pendingStationSchemas.insert(message.className, stationNames);
        return;
    }

    compareSchema(message.className, stationNames, mo);
}

void StationClient::compareSchema(const QByteArray& className,
                                  const QSet<QByteArray>& stationNames,
                                  const QMetaObject* mo)
{
    const MirrorSchema& local = MirrorSchema::forMetaObject(mo);
    QSet<QByteArray> localNames;
    for (const MirrorProperty& prop : local.properties()) {
        localNames.insert(prop.name);
    }

    int onlyStation = 0;
    int onlyLocal = 0;
    for (const QByteArray& name : stationNames) {
        if (!localNames.contains(name)) {
            m_schemaOnlyOnStation.insert(skewKey(className, name));
            ++onlyStation;
        }
    }
    for (const QByteArray& name : localNames) {
        if (!stationNames.contains(name)) {
            m_schemaOnlyLocal.insert(skewKey(className, name));
            ++onlyLocal;
        }
    }
    // THIS class's counts, not the accumulated set sizes. Logging the set
    // totals made every class after the first look like it had inherited
    // the previous one's differences.
    if (onlyStation > 0 || onlyLocal > 0) {
        qCWarning(lcStationClient)
            << "Mirror schema skew for" << className << "-- station-only:" << onlyStation
            << "local-only:" << onlyLocal;
    }
}

QObject* StationClient::resolveOrCreate(const QByteArray& objectKey,
                                        const QByteArray& className)
{
    if (QObject* existing = m_objects.value(objectKey).data()) {
        return existing;
    }
    if (m_radioModel.isNull()) {
        return nullptr;
    }

    const int sliceId = idFromKey(objectKey, kSliceKeyPrefix);
    if (sliceId < 0) {
        qCWarning(lcStationClient)
            << "Station named an object this client cannot construct:" << objectKey
            << className;
        return nullptr;
    }

    // A slice this client already holds under the station's id is ADOPTED,
    // not refused and not duplicated. This is the ordinary case rather than
    // an edge: RadioModel's own construction path can leave a slice behind
    // (connectToRadio seeds Slice A locally, and a reconnect finds whatever
    // the previous session built), and the id space is the STATION's, so an
    // existing local slice under that id is the same slice by definition.
    SliceModel* slice = m_radioModel->sliceById(sliceId);
    if (slice == nullptr) {
        // Ids are the station's, not minted here -- see
        // RadioModel::addSliceWithStationId for why a locally minted id
        // drifts from the station's after any mid-list removal, and what
        // that costs.
        if (m_radioModel->addSliceWithStationId(sliceId) != sliceId) {
            return nullptr;
        }
        slice = m_radioModel->sliceById(sliceId);
    }
    if (slice == nullptr) {
        return nullptr;
    }
    m_objects.insert(objectKey, slice);
    watchForOutbound(objectKey, slice);
    return slice;
}

void StationClient::handleObjectCreate(const SessionMessage& message)
{
    QObject* target = resolveOrCreate(message.objectKey, message.className);
    if (target == nullptr) {
        return;
    }

    // The deferred half of the schema comparison: this is the first
    // instance of a class whose schema arrived before anything of that
    // class existed here. See handleSchema().
    const auto pending = m_pendingStationSchemas.find(message.className);
    if (pending != m_pendingStationSchemas.end()) {
        compareSchema(message.className, pending.value(), target->metaObject());
        m_pendingStationSchemas.erase(pending);
    }

    applyUpdates(target, message.objectKey, message.updates);
}

void StationClient::handleObjectDestroy(const SessionMessage& message)
{
    const int sliceId = idFromKey(message.objectKey, kSliceKeyPrefix);
    m_objects.remove(message.objectKey);
    m_outboundMirror->unwatch(message.objectKey);
    if (sliceId >= 0 && !m_radioModel.isNull()) {
        InboundGuard guard(m_applyingInbound);
        m_radioModel->removeSlice(sliceId);
    }
}

void StationClient::handleDelta(const SessionMessage& message)
{
    QObject* target = m_objects.value(message.objectKey).data();
    if (target == nullptr) {
        qCWarning(lcStationClient) << "Delta for an object this client does not hold:"
                                   << message.objectKey;
        return;
    }
    applyUpdates(target, message.objectKey, message.updates);
}

void StationClient::applyUpdates(QObject* target, const QByteArray& objectKey,
                                 const QList<MirrorUpdate>& updates)
{
    Q_UNUSED(objectKey)
    const MirrorSchema& schema = MirrorSchema::forObject(target);
    const QByteArray className =
        MirrorSchema::shortClassName(target->metaObject()->className());

    // ONE guard around the whole batch, not one per property: a single
    // setter can move several properties as a side effect (SliceModel::
    // setDspMode rewrites both filter edges), and those have to be
    // suppressed too.
    InboundGuard guard(m_applyingInbound);

    for (const MirrorUpdate& update : updates) {
        const MirrorProperty* prop = schema.byName(update.name);
        if (prop == nullptr) {
            // A property this build does not declare. Already recorded by
            // handleSchema when the schema arrived; recorded again here
            // because a slice's schema is compared before any instance of
            // it exists.
            m_schemaOnlyOnStation.insert(skewKey(className, update.name));
            continue;
        }
        if (!applyOne(target, *prop, update)) {
            const QByteArray key = skewKey(className, update.name);
            if (!m_unapplied.contains(key)) {
                m_unapplied.insert(key);
                // ONCE per (class, property), not per delta: an S-meter
                // property that cannot land would otherwise produce a log
                // line ten times a second.
                qCWarning(lcStationClient)
                    << "No way to apply station value for" << key
                    << "-- this property will read stale on this client";
            }
        }
    }
}

bool StationClient::applyOne(QObject* target, const MirrorProperty& prop,
                             const MirrorUpdate& update)
{
    const MirrorSchema& schema = MirrorSchema::forObject(target);
    const QByteArray className =
        MirrorSchema::shortClassName(target->metaObject()->className());

    // CONSTANT properties are object identity, not state. sliceIndex is
    // the case that matters, and it was already consumed: it is what
    // resolveOrCreate() minted the slice under.
    if (prop.isConstant) {
        return true;
    }

    // Strategy 1: a real Q_PROPERTY WRITE.
    if (prop.isWritable) {
        return schema.write(prop, target, update.value);
    }

    const QVariant native = MirrorSchema::decode(prop, update.value);
    if (!native.isValid()) {
        return false;
    }

    // Strategy 2: the model's own inbound hook -- but ONLY where that hook
    // is a genuine STATE APPLY, never where it is a COMMAND SENDER.
    //
    // This distinction is not fussiness, it is a direction error the
    // allowlist exists to make structurally impossible. applyMirroredValue
    // is the DAEMON's inbound path: "a remote peer is asking this model to
    // do something." Inbound on a CLIENT the same message means the
    // opposite: "the station reports this is now true." Feeding a state
    // report into a command sender inverts the link.
    //
    // TunerModel is the live case. Its hook answers isOperate / isBypass /
    // antennaA by calling setOperate() / setBypass() / setAntennaA(), each
    // of which forwards a command to a bound TgxlConnection. Two things
    // went wrong before this allowlist:
    //
    //   - Those three setters no-op when no tuner is bound and the hook
    //     still returns success, so on a client isOperate and isBypass
    //     reported as APPLIED, changed nothing, read stale, and never
    //     entered m_unapplied -- defeating the accessor Task 20's bench is
    //     meant to trust.
    //   - It was inert only because a remote client has no TgxlConnection.
    //     Bind one and every inbound tuner delta from the station becomes
    //     an outbound tuner COMMAND from the client.
    //
    // Fixed HERE rather than in TunerModel::applyMirroredValue, which was
    // the other option the review offered. That hook's accept-with-no-tuner
    // behaviour is deliberate and tested: tst_mirror_inbound's
    // tunerOperateAndBypassRouteThroughTheHookToTheRealCommandSlots pins it
    // with the rationale that the mirror is a REMOTE CLICK and must not
    // diverge from what a local TunerApplet click does, which is also a
    // silent no-op with no tuner attached. Changing it would overturn a
    // documented Task 8 decision to fix a problem that only exists on the
    // client, so the client is where it is fixed.
    //
    // So the client consults the hook only for pairs proven to be a plain
    // state apply. Exactly one today: SliceModel::signalStrengthDbm, whose
    // hook calls setSignalStrengthDbm(), a plain setter task 12 added for
    // precisely this path. Adding a pair here means having read the hook
    // body and confirmed it writes state rather than sending a command.
    static const QSet<QByteArray> kClientStateApplyHooks = {
        QByteArrayLiteral("SliceModel.signalStrengthDbm"),
    };
    if (kClientStateApplyHooks.contains(skewKey(className, prop.name))) {
        QString hookReason;
        const bool invoked = QMetaObject::invokeMethod(
            target, "applyMirroredValue", Qt::DirectConnection,
            Q_RETURN_ARG(QString, hookReason), Q_ARG(QByteArray, prop.name),
            Q_ARG(QVariant, native));
        if (invoked && hookReason.isEmpty()) {
            return true;
        }
    }

    // Strategy 3: the client-side adapter, for properties whose hook
    // refusal is correct on the daemon and wrong here.
    return applyClientOnlyProperty(target, className, prop.name, native);
}

bool StationClient::applyClientOnlyProperty(QObject* target, const QByteArray& className,
                                            const QByteArray& propertyName,
                                            const QVariant& native)
{
    if (className != "SliceModel") {
        return false;
    }
    auto* slice = qobject_cast<SliceModel*>(target);
    if (slice == nullptr) {
        return false;
    }

    // SliceModel::active and ::txSlice have no WRITE, and their
    // applyMirroredValue correctly REFUSES on the daemon: a remote peer
    // must go through the setActiveSliceById verb and through
    // TxSliceArbiter respectively, or it would bypass the exclusivity
    // those two exist to maintain. Inbound on a client the direction is
    // reversed -- the arbiter has already spoken and this is its answer
    // arriving -- so refusing here would leave a remote operator unable to
    // see which slice is active or which one transmits, both of which the
    // R2 demo names explicitly (design addendum section 2).
    if (propertyName == "active") {
        slice->setActive(native.toBool());
        return true;
    }
    if (propertyName == "txSlice") {
        slice->setTxSlice(native.toBool());
        return true;
    }
    if (propertyName == "band") {
        // Derived from frequency by SliceModel itself, on this client
        // exactly as on the daemon, so the frequency delta in the same
        // batch already produced it. Accepted rather than counted as a
        // gap, because nothing is actually missing.
        return true;
    }
    return false;
}

// ── The mirror, outbound ─────────────────────────────────────────────────

void StationClient::watchForOutbound(const QByteArray& objectKey, QObject* object)
{
    if (object == nullptr) {
        return;
    }
    m_outboundMirror->watch(objectKey, object);
}

void StationClient::onWriteFlushTick()
{
    const QList<QPair<QByteArray, QList<MirrorUpdate>>> pending = m_outboundCoalescer.flush();
    for (const auto& batch : pending) {
        QObject* object = m_objects.value(batch.first).data();
        if (object == nullptr) {
            // Unwatched or destroyed since it was marked dirty. Dropped
            // rather than sent, the same call StateMirror::
            // flushCoalescedDeltas makes for the same situation.
            continue;
        }
        const MirrorSchema& schema = MirrorSchema::forObject(object);
        QList<MirrorUpdate> resolved;
        resolved.reserve(batch.second.size());
        for (const MirrorUpdate& pendingUpdate : batch.second) {
            // Re-read the LIVE value rather than sending what the
            // coalescer stored: an inbound apply can have moved the
            // property again since it was marked dirty (the guard
            // suppresses the observer, not the change), and sending the
            // stale one would tell the station to undo its own value.
            const MirrorProperty* prop = schema.byName(pendingUpdate.name);
            if (prop == nullptr) {
                continue;
            }
            const QVariant live = schema.read(*prop, object);
            if (!live.isValid()) {
                continue;
            }
            resolved.append(MirrorUpdate{ prop->ordinal, prop->name, prop->kind, live });
        }
        if (!resolved.isEmpty()) {
            send(SessionMessages::propertyWrite(batch.first, resolved));
        }
    }
}

// ── Commands and send ────────────────────────────────────────────────────

quint32 StationClient::invokeCommand(const QByteArray& verb,
                                     const QList<MirrorUpdate>& arguments)
{
    if (m_transport == nullptr || !m_authenticated) {
        return 0;
    }
    const quint32 id = m_nextCommandId++;
    send(SessionMessages::commandInvoke(verb, id, arguments));
    return id;
}

void StationClient::send(const SessionMessage& message)
{
    if (m_transport == nullptr) {
        return;
    }
    m_transport->sendText(SessionMessages::encode(message));
}

QList<QByteArray> StationClient::mirroredObjectKeys() const
{
    QList<QByteArray> keys;
    keys.reserve(m_objects.size());
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!it.value().isNull()) {
            keys.append(it.key());
        }
    }
    std::sort(keys.begin(), keys.end());
    return keys;
}

QObject* StationClient::mirroredObject(const QByteArray& objectKey) const
{
    return m_objects.value(objectKey).data();
}

} // namespace NereusSDR
