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
//   2026-08-09  J.J. Boyd / KG4VCF  Remote daemon R2: the five
//                                    IStationLink verbs, the pending-
//                                    command map that routes a station
//                                    refusal to an operator-facing
//                                    signal, attach/detach against the
//                                    RadioModel, and the two inbound
//                                    paths re-pointed off removeSlice().
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-40: station telemetry carries
//                                    each receiver's processing load only
//                                    for a peer that negotiated minor 11
//                                    and stationTelemetryVersion 3.
//                                    AI-assisted implementation via
//                                    Anthropic Claude Code.
//                                    Later the same day: the runtime NNR
//                                    limit arrives as SliceModel nnrLimit,
//                                    and the operator's retry is sent as
//                                    nnr.tryAgain, both from minor 11.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-21 / R-R3-09: the window's
//                                    NotchModel mirrors a notchControlVersion
//                                    Core's list and sends notch.* requests.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-11: the window holds a
//                                    radioHardwareVersion Core's `stepAtt`
//                                    object; its edits pass an edit gate.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-46 / R-R3-21: why the window's
//                                    attenuator edits cannot reach the
//                                    Core, for the window's controls.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-23 - R-R3-46: the `alexAntennas` object at
//                 radioHardwareVersion 2, its edit gate and reason, and the
//                 requestIoBoardProbe verb. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-09-23 - R-R3-47 / R-R3-22: the Core's read-only `amplifier` and
//                 `rfkit` objects, applied as plain state, and whether they
//                 are live. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-09-23 - R-R3-46 fix wave (radioHardwareVersion 3): the read-only
//                 `ioBoard` object, one band's antenna at a time
//                 (setAlexRxAntenna), and the window's OC pin matrix copy
//                 reloaded when the Core's OC settings arrive. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-48: remoteRfKitControlVersion 2 (the
//                 configureRfKit, disconnectRfKit and setRfKitEnabled
//                 requests; rfKitEnabled applied as plain state), and
//                 stationTciVersion 1 (the `stationTci` object and the
//                 setStationTci request). J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: remotePgxlControlVersion 2, the
//                 configurePgxl, disconnectPgxl and setPgxlConnectionSettings
//                 requests. J.J. Boyd (KG4VCF), AI-assisted via Anthropic
//                 Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: accessoryDataVersion 1 (the
//                 `accessoryData` object and the setTxInterlockPolicy,
//                 setPgxlPowerCap and clearAccessoryFaults requests). J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22: remotePgxlControlVersion 3 and
//                 remoteTgxlControlVersion 1 (the `accessorySettings` object
//                 and the amp's and tuner's own settings requests); their
//                 refusals go to the Advanced pages, not the slice toast.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47 / R-R3-22 / R-R3-48: every accessory request's
//                 refusal (Power Genius, Tuner Genius, RF-Kit, interlock,
//                 fault history, station TCI, 4O3A switch) goes to
//                 accessoryRequestRefused, never the slice toast. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-09-24 - R-R3-47: remoteRfKitControlVersion 3 (the resetRfKitError
//                 request, the RF-Kit page's settings from a remote window);
//                 a Core setting's change is reported to the window's pages
//                 (stationSettingChanged).
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "core/session/StationClient.h"

#include "core/AppSettings.h"
#include "core/FaultLog.h"
#include "core/session/MirrorPolicy.h"
#include "core/session/ObjectRegistry.h"
#include "core/session/SessionTransport.h"
#include "core/settings/SettingsProxy.h"
#include "models/AmplifierModel.h"
#include "models/NotchModel.h"
#include "models/RfKitModel.h"
#include "models/StationTciModel.h"
#include "models/AccessoryDataModel.h"
#include "models/AccessorySettingsModel.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include "models/PanadapterModel.h"
#include "models/PureSignalSettings.h"
#include "core/dsp/DspAssetService.h"
#include "DspCommandValues.h"
#include "PureSignalSessionFacade.h"
#include "core/StepAttenuatorFacade.h"
#include "core/accessories/AlexAntennaFacade.h"
#include "core/IoBoardHl2Facade.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "models/TunerModel.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QSslCertificate>
#include <QSslError>
#include <QStringList>
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

/// R-R3-17. True when `host` is an address literal in a private or
/// link-local range: IPv4 10/8, 172.16/12, 192.168/16, 169.254/16; IPv6
/// fc00::/7, fe80::/10. A host NAME is never classified: resolving it here
/// would make a pure mapping depend on the network it is diagnosing.
bool isLocalNetworkAddress(const QString& host)
{
    QString literal = host.trimmed();
    if (literal.startsWith(QLatin1Char('[')) && literal.endsWith(QLatin1Char(']'))) {
        literal = literal.mid(1, literal.size() - 2);
    }
    const QHostAddress address(literal);
    if (address.isNull()) {
        return false;
    }
    static const QList<QPair<QHostAddress, int>> kLocalSubnets = {
        QHostAddress::parseSubnet(QStringLiteral("10.0.0.0/8")),
        QHostAddress::parseSubnet(QStringLiteral("172.16.0.0/12")),
        QHostAddress::parseSubnet(QStringLiteral("192.168.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("169.254.0.0/16")),
        QHostAddress::parseSubnet(QStringLiteral("fc00::/7")),
        QHostAddress::parseSubnet(QStringLiteral("fe80::/10")),
    };
    for (const QPair<QHostAddress, int>& subnet : kLocalSubnets) {
        if (address.isInSubnet(subnet)) {
            return true;
        }
    }
    return false;
}

} // namespace

QString StationClient::connectionFailureReason(QAbstractSocket::SocketError error,
                                               const QString& errorText,
                                               const QString& host,
                                               bool macOs)
{
    // Keyed on the socket error enum AND its text. Qt reports EHOSTUNREACH
    // as NetworkError with the text "Host unreachable"; some paths carry
    // the platform's own "No route to host". HostNotFoundError is a name
    // lookup failure, not this case, and "Network unreachable" (also a
    // NetworkError) means this Mac has no route at all, which Local
    // Network privacy does not produce.
    const bool hostUnreachable =
        error == QAbstractSocket::NetworkError
        && (errorText.contains(QLatin1String("Host unreachable"), Qt::CaseInsensitive)
            || errorText.contains(QLatin1String("No route to host"), Qt::CaseInsensitive));
    if (!macOs || !hostUnreachable || !isLocalNetworkAddress(host)) {
        return errorText;
    }
    return QStringLiteral(
               "Can't reach the Core at %1. If this Mac is on the same network as "
               "the Core, macOS may be blocking NereusSDR from your local network: "
               "allow it in System Settings, Privacy & Security, Local Network, "
               "then press Connect.")
        .arg(host);
}

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

    // The seam RadioModel routes its five slice-mutating entry points
    // through in Role::Remote (RadioModel.h, attachStation). Attached for
    // this object's whole life rather than per session -- see the
    // IStationLink block in this class's header for why that is the state
    // with fewer ways to be wrong. Harmless on a Role::Local model, which
    // never consults the link at all.
    if (radioModel != nullptr) {
        radioModel->attachStation(this);
    }

    m_outboundMirror = new StateMirror(this);
    connect(m_outboundMirror, &StateMirror::propertiesChanged, this,
            [this](const QByteArray& objectKey, const QList<MirrorUpdate>& updates) {
                // The echo guard, checked FIRST, before this handler asks
                // anything else about the change -- exactly where
                // StateMirror::onWatchedPropertyChanged checks its own.
                // Before the first snapshot is complete, local changes have
                // no authenticated station state to merge with.  A later
                // schema burst on an established session is different: keep
                // genuine operator edits coalesced while its snapshot owns
                // the wire, then flush them after SnapshotComplete.
                if (m_applyingInbound || (!m_forwardLocalChanges && !m_handshakeComplete)) {
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
                    if (propertyResultsAvailable()) {
                        m_propertyWriteIds[objectKey].insert(update.name, 0);
                    }
                }
            });

    m_heartbeatTimer = new QTimer(this);
    m_heartbeatTimer->setInterval(m_heartbeatIntervalMs);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &StationClient::onHeartbeatTick);

    m_writeFlushTimer = new QTimer(this);
    m_writeFlushTimer->setInterval(kDefaultWriteFlushMs);
    connect(m_writeFlushTimer, &QTimer::timeout, this, &StationClient::onWriteFlushTick);

    // R-R3-16/17: owned and single-shot like m_reconnectTimer, so an
    // operator Disconnect or a completed handshake can always stop it.
    m_handshakeDeadlineTimer = new QTimer(this);
    m_handshakeDeadlineTimer->setSingleShot(true);
    connect(m_handshakeDeadlineTimer, &QTimer::timeout,
            this, &StationClient::onHandshakeDeadline);

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

StationClient::~StationClient()
{
    // The model holds this pointer non-owningly and outlives this object
    // in the ordinary GUI teardown, so leaving it attached would leave
    // RadioModel routing operator clicks into freed memory. QPointer, so
    // the reverse order (model destroyed first) needs no special case.
    if (!m_radioModel.isNull()) {
        m_radioModel->detachStation();
    }
}

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
        emit connectionActivityChanged();
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
        emit connectionActivityChanged();
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
            [this, socket, transportGuard, host = url.host()](QAbstractSocket::SocketError error) {
                if (transportGuard.isNull() || transportGuard.data() != m_transport) {
                    return;
                }
                // First error wins -- see dialStation()'s clear above. The
                // operator reason may name a recovery action (R-R3-17); the
                // raw socket error still goes to the log below.
                if (m_lastError.isEmpty()) {
                    m_lastError = connectionFailureReason(error, socket->errorString(), host);
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
        m_lastError = QStringLiteral("Station certificate fingerprint does not match the saved pin.");
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

    // A directly adopted replacement link needs the same retirement as a
    // redial. Otherwise the previous authentication/snapshot flags remain
    // true until the new Hello arrives, admitting work into an unverified
    // session. Retire its media and mirror state, preserving the existing
    // contract that a deliberate redial emits no sessionEnded notification.
    if (m_sessionActive) {
        endSession(QStringLiteral("replaced by a newer session"), false, false);
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

    // These are optional, remote-only fields.  A fresh peer may predate
    // them, in which case its snapshot cannot overwrite a status received
    // from the previous station.  Do this at the attach boundary rather
    // than on a same-session re-seed, whose status remains authoritative.
    if (m_radioModel != nullptr) {
        m_radioModel->applyStationReceiveLayoutStatus("receiveLayoutRestoreState", {});
        m_radioModel->applyStationReceiveLayoutStatus("receiveLayoutRestoreMessage", {});
    }

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
    m_lastTelemetrySequence = 0;
    m_lastTelemetrySampleElapsedMs = -1;
    m_capabilities.remoteDisplayBudgetVersion = 0;
    m_capabilities.displayBudget.reset();
    m_capabilities.displayBudgetReason.reset();
    m_capabilities.remotePs3DisplaySubscribed = false;

    // These three describe THIS session. Carrying them across a reconnect
    // would let a difference the station has since fixed keep showing up
    // in a diagnostic Task 20's bench is meant to trust.
    m_schemaOnlyOnStation.clear();
    m_schemaOnlyLocal.clear();
    m_unapplied.clear();
    m_unheldDeltaKeys.clear();
    m_pendingStationSchemas.clear();

    const quint32 epoch = m_sessionEpoch;
    connect(transport, &SessionTransport::textReceived, this,
            [this, transport, epoch](const QByteArray& wire) {
        // Disconnecting does not cancel already queued deliveries. A delayed
        // observation or snapshot from a replaced transport cannot become part
        // of its successor, even if a later allocation reuses the address.
        if (m_transport == transport && m_sessionEpoch == epoch) {
            onTransportText(wire);
        }
    });
    connect(transport, &SessionTransport::pongReceived, this,
            [this]() { m_pingsAwaitingPong = 0; });
    connect(transport, &SessionTransport::closed, this, &StationClient::onTransportClosed);

    // The heartbeat deliberately does NOT start here. A wss dial can take
    // seconds, and a heartbeat counting missed pongs across a socket that
    // has not finished connecting reports a slow dial as a dead station.
    // It starts on the first inbound frame instead (onTransportText), which
    // is the station's own Hello and therefore proof the link carries
    // traffic in both directions.
    //
    // R-R3-16/17: which left the wait BEFORE that frame unbounded. The
    // 2026-09-23 incident sat there for 8.7 minutes: Core's event loop was
    // blocked, so the TLS and WebSocket upgrade and Core's Hello all waited
    // on it, and nothing on this side was counting. The handshake deadline
    // covers exactly that window and the rest of the connect sequence. It
    // is armed here rather than on QWebSocket::connected because the
    // upgrade itself is part of what stalls, and it runs until the
    // snapshot-complete marker, the point the session is usable.
    if (m_handshakeDeadlineMs > 0) {
        m_handshakeDeadlineTimer->start(m_handshakeDeadlineMs);
    } else {
        m_handshakeDeadlineTimer->stop();
    }
    emit connectionActivityChanged();
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
    // A cancelled backoff has no active session for endSession() to retire.
    emit connectionActivityChanged();
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
void StationClient::endSession(const QString& reason, bool attemptReconnect,
                               bool reportSessionEnd)
{
    if (!m_sessionActive) {
        return;  // already reported for this attach
    }
    m_sessionActive = false;

    m_handshakeComplete = false;
    m_authenticated = false;
    m_forwardLocalChanges = false;
    m_propertyWriteIds.clear();
    if (m_radioModel) {
        m_radioModel->dspAssets()->resetSession();
        m_radioModel->dspAssets()->setRemoteNr3ModelsSupported(false);
        m_radioModel->pureSignalFacade()->resetSession();
        // The window keeps the Core's last notch list; unanswered requests
        // and held edits belong to the retired session.
        if (m_radioModel->notchModel()) {
            m_radioModel->notchModel()->resetSession();
        }
    }
    m_linkUp = false;
    m_heartbeatTimer->stop();
    m_writeFlushTimer->stop();
    m_handshakeDeadlineTimer->stop();
    // Results from the retired session can no longer arrive. Keeping its
    // unanswered commands would suppress completions for fresh requests
    // after reconnect (including the 4O3A master and C-Tune controls).
    m_pendingCommands.clear();
    m_pendingPs3Display.reset();

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
        // Fixed tuner telemetry is an admission snapshot, unlike the
        // retained slice model.  Once a remote session is inactive it must
        // not appear to have a live TGXL.  Keep the endpoint as a reconnect
        // draft, but clear device identity, live state and meters through
        // TunerModel's observational station-state adapter; this never
        // opens a local socket or issues an RF command.
        if (m_radioModel->role() == RadioModel::Role::Remote) {
            // 4O3A state is station-owned admission state, not retained
            // client display state.  Do this before a replacement snapshot
            // can arrive, so a disconnected station is never presented as
            // still listening on this machine.
            m_radioModel->clearRemoteFourO3AState();
            if (TunerModel* const tuner = m_radioModel->tunerModel()) {
                TunerModel::StationConnectionState disconnected;
                disconnected.configuredHost = tuner->configuredHost();
                disconnected.configuredPort = static_cast<quint16>(
                    qBound(0, tuner->configuredPort(), 65535));
                disconnected.phase = TunerModel::ConnectionPhase::Disconnected;
                tuner->setStationConnectionState(disconnected);
            }
        }
        // The remote accessory controls derive their enabled state from
        // StationClient's negotiated session state.  ConnectionState alone
        // does not change for every close/retry path, so publish this
        // boundary explicitly after the availability predicates are false.
        m_radioModel->reportStationLinkStateChanged();
        m_radioModel->setStationConnectionState(ConnectionState::Disconnected);
        m_radioModel->clearStationFilterState();
        for (SliceModel* slice : m_radioModel->slices()) {
            slice->setStationAutoAgcNoiseFloor(slice->stationAutoAgcNoiseFloorDbm(), false,
                                              slice->stationAutoAgcNoiseFloorGeneration());
        }
    }
    emit mediaSessionEnded(m_sessionEpoch);
    emit telemetrySessionEnded(m_sessionEpoch);
    if (reportSessionEnd) {
        emit sessionEnded(reason);
    }

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
    emit connectionActivityChanged();
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

// ── Handshake deadline (R-R3-16/17) ─────────────────────────────────────

QString StationClient::handshakeDeadlineReason()
{
    return QStringLiteral("The station did not finish connecting.");
}

void StationClient::setHandshakeDeadlineMs(int ms)
{
    m_handshakeDeadlineMs = ms;
    if (ms < 1) {
        qCWarning(lcStationClient)
            << "Handshake deadline disabled. A station that accepts the connection "
               "but never finishes connecting will be waited on indefinitely.";
    }
}

void StationClient::onHandshakeDeadline()
{
    if (!m_sessionActive || m_handshakeComplete) {
        return;  // stopped too late to matter; nothing is waiting
    }
    qCWarning(lcStationClient) << "Station did not finish connecting within"
                               << m_handshakeDeadlineMs << "ms; closing the link";

    // Recorded as the reason before anything below can race a socket error
    // into m_lastError (first error wins for this attempt, see
    // dialStation()). It is what the link-lost toast and the Core
    // connection status show.
    const QString reason = handshakeDeadlineReason();
    m_lastError = reason;

    // A stalled station is the "no reason from the daemon, just silence"
    // case the heartbeat timeout already treats as retry-eligible, so the
    // next attempt follows the normal backoff. A handshake that stalls
    // every time therefore slows down step by step rather than hammering
    // a Core that is already struggling: nothing here resets the schedule.
    //
    // Hold the transport across the call: disconnectFromStation() ends the
    // session first and then closes the link, and a QWebSocket still in its
    // TLS or upgrade phase does not close on a close() request. Abort it so
    // Core sees the connection go and can retire whatever it built for it.
    const QPointer<SessionTransport> transport(m_transport);
    disconnectFromStation(reason, /*attemptReconnect=*/true);
    if (auto* ws = qobject_cast<WebSocketTransport*>(transport.data())) {
        if (QWebSocket* socket = ws->socket();
            socket != nullptr && socket->state() != QAbstractSocket::ConnectedState) {
            socket->abort();
        }
    }
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

namespace {
// scheduleReconnect()'s schedule, in units of m_reconnectBackoffUnitMs; see
// its comment for where the numbers come from. File scope so that
// reconnectBackoffExhausted() reads the same ceiling.
constexpr int kReconnectBackoffSteps[] = { 1, 2, 5, 10, 30, 60 };
constexpr int kReconnectBackoffStepCount =
    static_cast<int>(sizeof(kReconnectBackoffSteps) / sizeof(kReconnectBackoffSteps[0]));
} // namespace

bool StationClient::reconnectBackoffExhausted() const
{
    // m_reconnectAttempts counts the retries scheduled since the schedule
    // last started over; the last step is the ceiling, so once as many
    // retries as there are steps have been scheduled, one of them waited
    // the ceiling.
    return m_reconnectAttempts >= kReconnectBackoffStepCount;
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
        emit connectionActivityChanged();
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
    case SessionMessageKind::StationTelemetry:
        if (telemetryAvailable()
            && message.telemetry.sequence > m_lastTelemetrySequence
            && message.telemetry.sampledElapsedMs >= m_lastTelemetrySampleElapsedMs) {
            m_lastTelemetrySequence = message.telemetry.sequence;
            m_lastTelemetrySampleElapsedMs = message.telemetry.sampledElapsedMs;
            // Only a Core that negotiated host telemetry may supply it.
            if (m_agreedMinor < kCoreHostTelemetrySessionProtocolMinor
                || m_capabilities.stationTelemetryVersion < 2) {
                message.telemetry.host = {};
            }
            // Only a Core that negotiated receiver load may supply it.
            if (m_agreedMinor < kReceiverLoadSessionProtocolMinor
                || m_capabilities.stationTelemetryVersion < 3) {
                message.telemetry.receivers.reset();
            }
            emit telemetryReceived(message.telemetry, m_sessionEpoch);
        }
        break;
    case SessionMessageKind::MediaControl:
        if (mediaAvailable()) {
            emit mediaControlReceived(message.mediaPayload, m_sessionEpoch);
        }
        break;
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
    case SessionMessageKind::PropertyResult:
        if (propertyResultsAvailable()) {
            handlePropertyResult(message);
        }
        break;
    case SessionMessageKind::SnapshotComplete: {
        const bool firstSnapshot = !m_handshakeComplete;
        // BEFORE anything below, and in particular before
        // m_forwardLocalChanges goes true: this marker is the FIRST moment
        // the station's full object set is known, and it is the only
        // moment at which "the station did not name this slice" means
        // "the station does not have this slice". See
        // reconcileSlicesAgainstStation().
        reconcileSlicesAgainstStation();
        m_handshakeComplete = true;
        // R-R3-16/17: the connect sequence finished inside its deadline.
        m_handshakeDeadlineTimer->stop();
        if (m_radioModel) {
            m_radioModel->pureSignalFacade()->setRemoteCapabilities(
                m_agreedMinor >= kDspControlSessionProtocolMinor && m_capabilities.psAlgorithmVersion == 3,
                m_capabilities.txPermitted);
            m_radioModel->dspAssets()->setRemoteNr3ModelsSupported(remoteNr3ModelsAvailable());
        }
        if (m_radioModel) {
            m_radioModel->setStationFilterSnapshotReady();
            m_radioModel->reportStationLinkStateChanged();
        }
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
        //
        // R-R3-28: when media was negotiated, the handshake alone no longer
        // proves the session works. Media that fails after every good
        // handshake would otherwise retry at the first step forever (the
        // "Explicit remaining boundary" in the R3 media-recovery evidence).
        // The reset then waits for noteMediaEstablished(); without media it
        // happens here, as it always has.
        m_everConnected = true;
        if (!mediaAvailable()) {
            m_reconnectAttempts = 0;
        }
        // Only now: everything that moved before this point was the
        // station's own burst landing, and forwarding any of it would tell
        // the station its own state back.
        m_forwardLocalChanges = true;
        m_writeFlushTimer->start();
        if (firstSnapshot) {
            qCInfo(lcStationClient) << "Session established with" << m_capabilities.stationName;
            emit handshakeComplete();
        }
        emit stateSnapshotApplied();
        break;
    }
    case SessionMessageKind::CommandResult:
        handleCommandResult(message);
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
    const QPointer<StationClient> self(this);
    const quint32 epoch = m_sessionEpoch;
    const auto previousBudget = remoteDisplayBudgetLimits();
    const bool previousPs3 = remotePs3DisplaySubscribed();
    const DisplayBudgetReason previousReason = remoteDisplayBudgetReason();
    StationCapabilities incoming = StationCapabilities::fromUpdates(message.updates);
    // A complete descriptor is authoritative for this authenticated epoch.
    // Malformed, partial or stale updates cannot turn a known cap into the
    // legacy fallback. A fresh attach clears it before accepting a new peer.
    if (m_capabilities.displayBudget) {
        bool retain = !incoming.displayBudget;
        if (incoming.displayBudget) {
            const quint32 delta = incoming.displayBudget->generation
                - m_capabilities.displayBudget->generation;
            retain = delta >= 0x80000000u
                || (delta == 0 && *incoming.displayBudget != *m_capabilities.displayBudget);
        }
        if (retain) {
            incoming.remoteDisplayBudgetVersion = m_capabilities.remoteDisplayBudgetVersion;
            incoming.displayBudget = m_capabilities.displayBudget;
            incoming.remotePs3DisplaySubscribed = m_capabilities.remotePs3DisplaySubscribed;
            incoming.displayBudgetReason = m_capabilities.displayBudgetReason;
        }
    }
    m_capabilities = incoming;

    if (m_capabilities.effectiveMaxSlices < m_capabilities.boardMaxSlices) {
        qCInfo(lcStationClient)
            << "Station is limiting slices to" << m_capabilities.effectiveMaxSlices
            << "of the board's" << m_capabilities.boardMaxSlices
            << "-- a daemon capacity decision, not a radio limit";
    }

    if (m_radioModel.isNull()) {
        if (previousBudget != remoteDisplayBudgetLimits()
            || previousPs3 != remotePs3DisplaySubscribed()
            || previousReason != remoteDisplayBudgetReason()) {
            emit displayBudgetChanged();
        }
        return;
    }
    // The step that makes three earlier tasks mean anything: identity,
    // board capabilities, the EFFECTIVE slice limit, userDdcCount, and the
    // connection state, all through one production entry point.
    m_radioModel->applyStationCapabilities(m_capabilities);
    if (!self || m_sessionEpoch != epoch || !m_sessionActive || !m_radioModel) { return; }
    // A station may update its advertised optional capabilities after the
    // initial snapshot.  Once the session is established, this can change
    // whether the typed remote TGXL controls are available without a radio
    // connection-state transition.
    if (m_handshakeComplete) {
        m_radioModel->pureSignalFacade()->setRemoteCapabilities(
            m_agreedMinor >= kDspControlSessionProtocolMinor && m_capabilities.psAlgorithmVersion == 3,
            m_capabilities.txPermitted);
        if (!self || m_sessionEpoch != epoch || !m_sessionActive || !m_radioModel) { return; }
        m_radioModel->dspAssets()->setRemoteNr3ModelsSupported(remoteNr3ModelsAvailable());
        if (!self || m_sessionEpoch != epoch || !m_sessionActive || !m_radioModel) { return; }
        m_radioModel->reportStationLinkStateChanged();
        if (!self || m_sessionEpoch != epoch || !m_sessionActive || !m_radioModel) { return; }
    }

    // The singletons exist from RadioModel's own construction, so they can
    // be mapped and watched as soon as capabilities land, rather than
    // waiting for an object.create that will never come for them (the
    // daemon watches them directly; only slices have a lifecycle).
    const QByteArray radioKey(kRadioKey);
    m_objects.insert(radioKey, m_radioModel.data());
    watchForOutbound(radioKey, m_radioModel.data());

    m_objects.insert("pureSignal", m_radioModel->pureSignalFacade());
    m_objects.insert("dspAssets", m_radioModel->dspAssets());
    m_radioModel->pureSignalFacade()->setRemoteRequestHandler(
        [self](Ps3Action action, const QVariantMap& arguments) -> quint32 {
        if (!self || !self->propertyResultsAvailable() || self->m_capabilities.psAlgorithmVersion != 3) {
            return 0;
        }
        const auto values = dspCommandValues(arguments);
        return values ? self->invokeCommand(PureSignalSessionFacade::actionVerb(action), *values) : 0;
    });
    disconnect(m_radioModel->pureSignalFacade(), &PureSignalSessionFacade::displaySubscriptionRequested,
               this, nullptr);
    connect(m_radioModel->pureSignalFacade(), &PureSignalSessionFacade::displaySubscriptionRequested,
            this, [this](bool enabled) {
        if (remoteDisplayBudgetLimits()) { emit ps3DisplaySubscriptionRequested(enabled); }
        else { requestPs3DisplaySubscription(enabled); }
    });
    m_radioModel->dspAssets()->setRemoteRequestHandler(
        [self](const QByteArray& verb, const QVariantMap& arguments) -> quint32 {
        if (!self || !self->m_handshakeComplete
            || self->m_agreedMinor < kDspControlSessionProtocolMinor
            || self->m_capabilities.dspAssetVersion < 1 || !verb.startsWith("dspAssets.")) {
            return 0;
        }
        // R-R3-21: NR3 models need a dspAssetVersion 2 Core. An older Core
        // would refuse them anyway; not sending keeps its answer predictable.
        if (self->m_capabilities.dspAssetVersion < 2
            && (verb == "dspAssets.selectNr3Model"
                || (verb == "dspAssets.beginImport"
                    && arguments.value(QStringLiteral("kind")).toLongLong()
                           == static_cast<qlonglong>(DspAssetKind::Nr3Model)))) {
            return 0;
        }
        const auto values = dspCommandValues(arguments);
        return values ? self->invokeCommand(verb, *values) : 0;
    });
    m_objects.insert("pureSignalSettings", m_radioModel->pureSignalSettings());
    watchForOutbound("pureSignalSettings", m_radioModel->pureSignalSettings());

    // R-R3-21 / R-R3-09: a Core with notchControlVersion owns the notch
    // list. The window mirrors it and asks for every change; it never
    // writes or restores the Core's Notch* settings. Against an older Core
    // nothing changes: the key is not registered, so its object (if any)
    // is dropped, and the model keeps today's settings path.
    if (NotchModel* notches = m_radioModel->notchModel()) {
        const bool mirrored = m_agreedMinor >= kDspControlSessionProtocolMinor
            && m_capabilities.notchControlVersion >= 1;
        if (mirrored) {
            notches->setMirrorMode(true);
            notches->setRemoteRequestHandler(
                [self](const QByteArray& verb, const QVariantMap& arguments) -> quint32 {
                if (!self || !self->remoteNotchControlAvailable() || !verb.startsWith("notch.")) {
                    return 0;
                }
                const auto values = dspCommandValues(arguments);
                return values ? self->invokeCommand(verb, *values) : 0;
            });
            m_objects.insert("notches", notches);
            watchForOutbound("notches", notches);
        } else {
            m_objects.remove("notches");
            m_outboundMirror->unwatch("notches");
            notches->setRemoteRequestHandler({});
            notches->setMirrorMode(false);
        }
    }

    // R-R3-46 / R-R3-11: a Core with radioHardwareVersion 1 mirrors its step
    // attenuator and preamp as `stepAtt` and applies the window's edits
    // through its own controller. The window's edits pass only while that
    // holds; against an older Core the key is not registered, so its object
    // (if any) is dropped and nothing is sent.
    if (StepAttenuatorFacade* stepAtt = m_radioModel->stepAttFacade()) {
        stepAtt->setEditGate([self](QString* reason) {
            const bool allowed = self
                && (self->m_applyingInbound || self->remoteRadioHardwareAvailable());
            if (!allowed && reason) {
                *reason = self ? self->radioHardwareUnavailableReason()
                               : QStringLiteral("Connect to the Core to change the attenuator "
                                                "and preamp.");
            }
            return allowed;
        });
        if (m_agreedMinor >= kRadioIdentitySessionProtocolMinor
            && m_capabilities.radioHardwareVersion >= 1) {
            m_objects.insert("stepAtt", stepAtt);
            watchForOutbound("stepAtt", stepAtt);
        } else {
            m_objects.remove("stepAtt");
            m_outboundMirror->unwatch("stepAtt");
        }
    }

    // R-R3-46: a Core with radioHardwareVersion 2 mirrors its Alex antenna
    // settings as `alexAntennas` and applies the window's receive edits
    // through its own AlexController. Same gate shape as `stepAtt`.
    if (AlexAntennaFacade* alex = m_radioModel->alexAntennaFacade()) {
        alex->setEditGate([self](QString* reason) {
            const bool allowed = self
                && (self->m_applyingInbound || self->remoteHardwareConfigAvailable());
            if (!allowed && reason) {
                *reason = self ? self->hardwareConfigUnavailableReason()
                               : QStringLiteral("Connect to the Core to change the radio's "
                                                "hardware settings.");
            }
            return allowed;
        });
        if (m_agreedMinor >= kRadioIdentitySessionProtocolMinor
            && m_capabilities.radioHardwareVersion >= 2) {
            m_objects.insert("alexAntennas", alex);
            watchForOutbound("alexAntennas", alex);
        } else {
            m_objects.remove("alexAntennas");
            m_outboundMirror->unwatch("alexAntennas");
        }
        // R-R3-46 fix wave (radioHardwareVersion 3): one band's antenna at a
        // time, so a list built before the Core changed another band cannot
        // put that band back. A version 2 Core gets today's whole list.
        if (m_agreedMinor >= kRadioIdentitySessionProtocolMinor
            && m_capabilities.radioHardwareVersion >= 3) {
            alex->setBandEditSender([self](Band band, int antenna, bool rxOnly, QString* reason) {
                if (!self) {
                    return false;
                }
                const CommandOutcome outcome = self->requestAlexRxAntenna(band, antenna, rxOnly);
                if (!outcome.sent && reason) {
                    *reason = outcome.reason;
                }
                return outcome.sent;
            });
        } else {
            alex->setBandEditSender({});
        }
    }

    // R-R3-46 fix wave (radioHardwareVersion 3): the Core's HL2 I/O board,
    // read-only; its values go into the window's own board, which Setup's
    // HL2 I/O board tab shows.
    if (IoBoardHl2Facade* ioBoard = m_radioModel->ioBoardFacade()) {
        if (m_agreedMinor >= kRadioIdentitySessionProtocolMinor
            && m_capabilities.radioHardwareVersion >= 3) {
            m_objects.insert("ioBoard", ioBoard);
            watchForOutbound("ioBoard", ioBoard);
        } else {
            m_objects.remove("ioBoard");
            m_outboundMirror->unwatch("ioBoard");
            // Follow-up item 5: the window's board shows no Core's board
            // it cannot follow (a previous Core's readings would stay).
            ioBoard->clearRemoteValues();
        }
    }

    const QByteArray transmitKey(kTransmitKey);
    m_objects.insert(transmitKey, &m_radioModel->transmitModel());
    watchForOutbound(transmitKey, &m_radioModel->transmitModel());

    if (m_radioModel->tunerModel() != nullptr) {
        const QByteArray tunerKey(kTunerKey);
        m_objects.insert(tunerKey, m_radioModel->tunerModel());
        watchForOutbound(tunerKey, m_radioModel->tunerModel());
    }

    // R-R3-47 / R-R3-22: the Core's Power Genius and RF-Kit status, read
    // only. Registered against a Core that offers them; otherwise the key
    // is not held and an object for it is dropped as skew. Nothing on
    // either is ever written back.
    const struct {
        const char* key;
        QObject* object;
        bool offered;
    } accessories[] = {
        { "amplifier", m_radioModel->amplifierModel(),
          m_agreedMinor >= kRadioIdentitySessionProtocolMinor
              && m_capabilities.remotePgxlControlVersion >= 1 },
        { "rfkit", m_radioModel->rfKitModel(),
          m_agreedMinor >= kRadioIdentitySessionProtocolMinor
              && m_capabilities.remoteRfKitControlVersion >= 1 },
        // R-R3-48: the Core's station TCI server.
        { "stationTci", m_radioModel->stationTciModel(),
          m_agreedMinor >= kRadioIdentitySessionProtocolMinor
              && m_capabilities.stationTciVersion >= 1 },
        // R-R3-47 / R-R3-22: the Core's accessory records and settings.
        { "accessoryData", m_radioModel->accessoryDataModel(),
          m_agreedMinor >= kRadioIdentitySessionProtocolMinor
              && m_capabilities.accessoryDataVersion >= 1 },
        // R-R3-47 / R-R3-22: the amp's and tuner's own settings.
        { "accessorySettings", m_radioModel->accessorySettingsModel(),
          m_agreedMinor >= kRadioIdentitySessionProtocolMinor
              && (m_capabilities.remotePgxlControlVersion >= 3
                  || m_capabilities.remoteTgxlControlVersion >= 1) },
    };
    for (const auto& accessory : accessories) {
        const QByteArray key(accessory.key);
        if (accessory.object != nullptr && accessory.offered) {
            m_objects.insert(key, accessory.object);
            watchForOutbound(key, accessory.object);
        } else {
            m_objects.remove(key);
            m_outboundMirror->unwatch(key);
        }
    }

    const QList<PanadapterModel*> pans = m_radioModel->panadapters();
    for (int i = 0; i < pans.size(); ++i) {
        const QByteArray key = QByteArray(kPanKeyPrefix) + QByteArray::number(i);
        m_objects.insert(key, pans.at(i));
        watchForOutbound(key, pans.at(i));
    }
    if (previousBudget != remoteDisplayBudgetLimits()
        || previousPs3 != remotePs3DisplaySubscribed()
        || previousReason != remoteDisplayBudgetReason()) {
        emit displayBudgetChanged();
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
    // R-R3-46: the window's copy of the Core's OC pin matrix follows the
    // Core's settings (RadioModel::scheduleRemoteOcReload, coalesced).
    if (!m_radioModel.isNull()) {
        for (auto it = data.constBegin(); it != data.constEnd(); ++it) {
            m_radioModel->scheduleRemoteOcReload(it.key());
        }
        // Follow-up 6: pages showing the Core's settings re-read them.
        m_radioModel->reportStationSettingChanged(QString());
    }

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
        // R-R3-47: and the RF-Kit's. A Core that offers `accessoryData`
        // then sends its live lists, which replace these.
        if (m_radioModel->rfkitFaultLog() != nullptr) {
            m_radioModel->rfkitFaultLog()->reload();
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
    const QString key = QString::fromUtf8(message.objectKey);
    if (message.updates.isEmpty()) {
        m_settingsProxy->applyRemoteRemoval(key);
    } else {
        m_settingsProxy->applyRemoteValue(key, message.updates.first().value,
                                          message.originTag);
    }
    // R-R3-46: a changed cell of the Core's OC pin matrix reloads the
    // window's copy, so its next save cannot send a stale cell back.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteOcReload(key);
        // Follow-up 6: another window's (or the Core's) change reaches the
        // pages that show it.
        m_radioModel->reportStationSettingChanged(key);
    }
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
    // R-R3-46: a refused OC cell settles the window's copy on the Core's.
    if (!m_radioModel.isNull()) {
        m_radioModel->scheduleRemoteOcReload(QString::fromUtf8(message.objectKey));
    }
    if (!message.reason.isEmpty() && m_radioModel) {
        m_radioModel->reportStationSliceCommandRejected(message.reason);
    }
}

// ── The mirror, inbound ──────────────────────────────────────────────────

void StationClient::handleSchema(const SessionMessage& message)
{
    // Schema messages open a full snapshot burst, including a same-session
    // reseed after Core discovers its radio. Pause writes until its marker.
    m_forwardLocalChanges = false;
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

// Whole-branch review, Important 1.
//
// endSession() deliberately RETAINS RadioModel's slices across a link
// loss (design doc section 13: "the client retains last-known state"), and
// resolveOrCreate() adopts, on reattach, only the ids the station actually
// NAMES. Nothing anywhere closed the other half of that: a slice the
// station no longer has was never adopted, never watched, never mirrored,
// and never removed either -- it simply stayed on screen, and every edit
// the operator made to it went nowhere. m_forwardLocalChanges gates the
// forwarder on session state, not on membership, so the ghost did not even
// produce a refusal to log.
//
// Reachable by an ordinary restart, not a contrived one: slice_count is a
// real key in packaging/nereusd.conf.sample, DaemonConfig parses it, and
// DaemonApp::createConfiguredSlices clamps it to min(requested, the
// board's cap) and additionally stops early when the allocator refuses. A
// daemon that served two slices and comes back with one is a config edit
// or a smaller board away.
//
// WHEN this runs is the whole of the design. It cannot run per
// object.create (a burst that has delivered slice 0 but not yet slice 1
// would reap slice 1 for not having arrived), it cannot run on
// Capabilities (which precedes the burst entirely), and it must not run
// on link loss (the station may well come back with the same set, and
// section 13's retention is what lets a GUI keep its pointers). The
// snapshot-complete marker is the one point at which the station has
// finished naming everything it has, which is exactly the question this
// asks.
//
// Removal goes through the same RadioModel::removeSliceWithStationId()
// that handleObjectDestroy() uses, under the same inbound guard, so a
// reap is indistinguishable downstream from an explicit destroy. NOT
// removeSlice(), which on a Role::Remote model sends a removeSlice verb
// to the station (RadioModel.h) -- reaping locally must not ask the
// station to remove slices it has already told this client it does not
// have. Two of the removal body's own invariants carry through unchanged
// and are relied on here:
// it refuses to remove the last remaining slice (so a station reporting
// zero slices leaves the client with one rather than an empty model), and
// it hands TX off before removing a TX-bound victim.
void StationClient::reconcileSlicesAgainstStation()
{
    if (m_radioModel.isNull()) {
        return;
    }
    // A COPY: removeSlice() mutates the list this iterates.
    const QList<SliceModel*> held = m_radioModel->slices();
    QList<int> reaped;
    for (SliceModel* slice : held) {
        if (slice == nullptr) {
            continue;
        }
        const int sliceId = slice->sliceIndex();
        const QByteArray key = QByteArray(kSliceKeyPrefix) + QByteArray::number(sliceId);
        if (m_objects.contains(key)) {
            continue;
        }
        // Symmetric with handleObjectDestroy(): drop the wire registry
        // entry and the outbound watch first, then remove the object.
        // Both are no-ops for a slice that was never adopted, and both
        // are correct for one adopted by an EARLIER session whose
        // registry entry endSession() already cleared.
        m_objects.remove(key);
        m_outboundMirror->unwatch(key);
        {
            InboundGuard guard(m_applyingInbound);
            m_radioModel->removeSliceWithStationId(sliceId);
        }
        // Confirmed, not assumed. removeSlice() returns void and refuses
        // silently when the victim is the LAST remaining slice, so a
        // station that somehow reported none would otherwise be logged as
        // a reap that did not happen. Not reachable through configuration
        // (DaemonConfig refuses slice_count below 1), which is why this
        // is a check rather than a branch with behaviour behind it.
        if (m_radioModel->sliceById(sliceId) == nullptr) {
            reaped.append(sliceId);
        } else {
            qCWarning(lcStationClient)
                << "Station does not have slice" << sliceId
                << "but it could not be removed locally; RadioModel always keeps at"
                << "least one slice. It stays on screen, unmirrored.";
        }
    }
    if (!reaped.isEmpty()) {
        QStringList ids;
        ids.reserve(reaped.size());
        for (int id : reaped) {
            ids.append(QString::number(id));
        }
        qCInfo(lcStationClient)
            << "Station no longer has slice(s)" << ids.join(QLatin1String(", "))
            << "-- removed locally so no control is left pointing at a slice"
            << "the station cannot act on";
    }
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
    m_propertyWriteIds.remove(message.objectKey);
    m_outboundMirror->unwatch(message.objectKey);
    if (sliceId >= 0 && !m_radioModel.isNull()) {
        InboundGuard guard(m_applyingInbound);
        // removeSliceWithStationId, NOT removeSlice. On a Role::Remote
        // model removeSlice() now SENDS a removeSlice verb (RadioModel.h),
        // so calling it here would bounce the station's own destroy
        // straight back at the station as a fresh command.
        m_radioModel->removeSliceWithStationId(sliceId);
    }
}

void StationClient::handleDelta(const SessionMessage& message)
{
    QObject* target = m_objects.value(message.objectKey).data();
    if (target == nullptr) {
        // Once per object per session: a newer Core's object this client
        // does not hold (notches on an older app) changes often.
        if (!m_unheldDeltaKeys.contains(message.objectKey)) {
            m_unheldDeltaKeys.insert(message.objectKey);
            qCWarning(lcStationClient) << "Delta for an object this client does not hold:"
                                       << message.objectKey;
        }
        return;
    }
    QList<MirrorUpdate> current;
    const auto pending = m_propertyWriteIds.value(message.objectKey);
    for (const auto& value : message.updates) {
        if (!pending.contains(value.name)) {
            current.append(value);
        }
    }
    applyUpdates(target, message.objectKey, current);
}

void StationClient::handlePropertyResult(const SessionMessage& message)
{
    QObject* target = m_objects.value(message.objectKey).data();
    if (!target || message.writeId == 0) {
        return;
    }
    auto pending = m_propertyWriteIds.find(message.objectKey);
    if (pending == m_propertyWriteIds.end()) {
        return;
    }
    QList<MirrorUpdate> acceptedValues;
    QList<SessionPropertyResult> currentResults;
    for (const auto& result : message.propertyResults) {
        if (!pending->contains(result.property)
            || pending->value(result.property) != message.writeId) {
            continue;
        }
        pending->remove(result.property);
        if (result.hasValue) {
            acceptedValues.append(result.value);
        }
        currentResults.append(result);
    }
    if (pending->isEmpty()) {
        m_propertyWriteIds.erase(pending);
    }
    applyUpdates(target, message.objectKey, acceptedValues);
    for (const auto& result : currentResults) {
        if (auto* slice = qobject_cast<SliceModel*>(target);
            slice && (result.property.startsWith("nnr") || result.property == "activeNr")) {
            InboundGuard guard(m_applyingInbound);
            slice->reportNnrEditResult(result.reason);
        }
        emit propertyWriteCompleted(message.objectKey, result.property, message.writeId,
                                    result.accepted, result.reason);
    }
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
    // A lock prevents GUI-originated tuning, never an authoritative station
    // snapshot/delta. Apply without temporarily unlocking observable state.
    if (className == "SliceModel" && prop.name == "frequency") {
        auto* slice = qobject_cast<SliceModel*>(target);
        const QVariant frequency = MirrorSchema::decode(prop, update.value);
        return slice && frequency.isValid() && slice->applyStationFrequency(frequency.toDouble());
    }
    if (prop.isWritable) {
        return schema.write(prop, target, update.value);
    }

    const QVariant native = MirrorSchema::decode(prop, update.value);
    if (!native.isValid()) {
        return false;
    }

    // Radio connectivity can change while the authenticated station link
    // stays up. The handshake seeds this state from capabilities, but its
    // read-only property deltas need the same explicit remote-state writer.
    // RadioModel's generic inbound hook is deliberately a command boundary.
    if (target == m_radioModel.data() && className == QByteArrayLiteral("RadioModel")
        && prop.name == QByteArrayLiteral("connected")
        && m_radioModel->role() == RadioModel::Role::Remote) {
        m_capabilities.radioConnected = native.toBool();
        m_radioModel->setStationConnectionState(m_capabilities.radioConnected
            ? ConnectionState::Connected : ConnectionState::Disconnected);
        return true;
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
    // state apply: SliceModel's signal readings call plain telemetry
    // setters, including the separate R3 peak and average readings.
    // Adding a pair here means having read the hook
    // body and confirmed it writes state rather than sending a command.
    static const QSet<QByteArray> kClientStateApplyHooks = {
        // R-R3-22: RadioModel assigns these only in Role::Remote; it never
        // calls the listener-owning setter or persists a GUI-local setting.
        QByteArrayLiteral("RadioModel.fourO3AEnabled"),
        QByteArrayLiteral("RadioModel.fourO3AListening"),
        QByteArrayLiteral("RadioModel.fourO3AListenerError"),
        // R-R3-47: likewise the Core's RF-Kit switch.
        QByteArrayLiteral("RadioModel.rfKitEnabled"),
        QByteArrayLiteral("SliceModel.signalStrengthDbm"),
        QByteArrayLiteral("SliceModel.signalPeakDbm"),
        QByteArrayLiteral("SliceModel.signalAverageDbm"),
        QByteArrayLiteral("SliceModel.stationAutoAgcNoiseFloorDbm"),
        QByteArrayLiteral("SliceModel.stationAutoAgcNoiseFloorValid"),
        QByteArrayLiteral("SliceModel.stationAutoAgcNoiseFloorGeneration"),
        QByteArrayLiteral("SliceModel.streamCtunPinned"),
        QByteArrayLiteral("SliceModel.streamEpoch"),
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
    if (target == m_radioModel.data() && className == "RadioModel") {
        if (propertyName == "settingsSaveError") {
            m_radioModel->applyStationSettingsSaveError(native.toString());
            return true;
        }
        if (m_radioModel->applyStationReceiveLayoutStatus(propertyName, native.toString())) {
            return true;
        }
        return m_radioModel->applyStationFilterValue(propertyName, native);
    }
    if (className == "PureSignalSessionFacade") {
        auto* facade = qobject_cast<PureSignalSessionFacade*>(target);
        return facade && facade->applyRemoteProperty(propertyName, native);
    }
    if (className == "DspAssetService") {
        auto* assets = qobject_cast<DspAssetService*>(target);
        return assets && assets->applyRemoteProperty(propertyName, native);
    }
    if (className == "NotchModel") {
        auto* notches = qobject_cast<NotchModel*>(target);
        return notches && notches->applyRemoteProperty(propertyName, native);
    }
    if (className == "StepAttenuatorFacade") {
        auto* stepAtt = qobject_cast<StepAttenuatorFacade*>(target);
        return stepAtt && stepAtt->applyRemoteProperty(propertyName, native);
    }
    if (className == "AlexAntennaFacade") {
        auto* alex = qobject_cast<AlexAntennaFacade*>(target);
        return alex && alex->applyRemoteProperty(propertyName, native);
    }
    if (className == "IoBoardHl2Facade") {
        auto* ioBoard = qobject_cast<IoBoardHl2Facade*>(target);
        return ioBoard && ioBoard->applyRemoteProperty(propertyName, native);
    }
    if (className == "PureSignalSettings") {
        auto* settings = qobject_cast<PureSignalSettings*>(target);
        return settings && settings->applyStationDiagnostic(propertyName, native);
    }
    if (className == "TunerModel") {
        auto* tuner = qobject_cast<TunerModel*>(target);
        return tuner != nullptr && tuner->applyStationValue(propertyName, native);
    }
    // R-R3-47 / R-R3-22: plain state applies; never a command to an amp.
    if (className == "AmplifierModel") {
        auto* amp = qobject_cast<AmplifierModel*>(target);
        return amp != nullptr && amp->applyStationValue(propertyName, native);
    }
    if (className == "RfKitModel") {
        auto* rfKit = qobject_cast<RfKitModel*>(target);
        return rfKit != nullptr && rfKit->applyStationValue(propertyName, native);
    }
    // R-R3-48: a plain state apply; the switch changes only by command.
    if (className == "StationTciModel") {
        auto* tci = qobject_cast<StationTciModel*>(target);
        return tci != nullptr && tci->applyStationValue(propertyName, native);
    }
    // R-R3-47 / R-R3-22: a plain state apply; changes only by command.
    if (className == "AccessoryDataModel") {
        auto* data = qobject_cast<AccessoryDataModel*>(target);
        return data != nullptr && data->applyStationValue(propertyName, native);
    }
    // R-R3-47 / R-R3-22: a plain state apply; changes only by command.
    if (className == "AccessorySettingsModel") {
        auto* settings = qobject_cast<AccessorySettingsModel*>(target);
        return settings != nullptr && settings->applyStationValue(propertyName, native);
    }
    if (className != "SliceModel") {
        return false;
    }
    auto* slice = qobject_cast<SliceModel*>(target);
    if (slice == nullptr) {
        return false;
    }
    if (propertyName.startsWith("nnr")) {
        return slice->applyStationNnrDiagnostic(propertyName, native);
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
        // ...and move RadioModel's own m_activeSlice with it. The flag
        // alone was not enough: activeSlice() is repointed only by
        // RadioModel::setActiveSlice(), so before this, every
        // activeSlice()-reading surface on a remote GUI (the container
        // S-meter, the RX applet, the DSP menu, the band buttons) stayed
        // stranded on whichever slice was created first, no matter which
        // one the station reported active.
        //
        // Only on the TRUE edge. The false edge is the slice being stood
        // down, and its partner true edge -- which arrives in the same
        // batch, in either order -- is what does the repointing;
        // setActiveSlice() clears the outgoing slice's flag itself.
        if (native.toBool() && !m_radioModel.isNull()) {
            m_radioModel->applyStationActiveSlice(slice->sliceIndex());
        }
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
    if (auto* settings = qobject_cast<PureSignalSettings*>(object)) {
        QPointer<StationClient> self(this);
        settings->setEditGate([self](QString* reason) {
            const bool allowed = self && (self->m_applyingInbound
                || (self->propertyResultsAvailable() && self->m_capabilities.psAlgorithmVersion == 3));
            if (!allowed && reason) {
                *reason = QStringLiteral("The station does not support PS3 settings.");
            }
            return allowed;
        });
    }
    m_outboundMirror->watch(objectKey, object);
    if (auto* slice = qobject_cast<SliceModel*>(object)) {
        const QPointer<StationClient> owner(this);
        slice->setNnrSettingsApplier([owner](const NnrSettings& requested, QString* reason)
                                       -> std::optional<NnrSettings> {
            if (owner && (owner->m_applyingInbound || owner->nnrControlAvailable())) {
                return requested;
            }
            if (reason) { *reason = QStringLiteral("This station session does not support NNR controls."); }
            return std::nullopt;
        });
        slice->setNrSelectionApplier([owner](NrSlot requested, QString* reason) {
            if (owner && owner->m_applyingInbound) {
                return true;
            }
            if (requested == NrSlot::NNR && !(owner && owner->nnrControlAvailable())) {
                if (reason) { *reason = QStringLiteral("This station session does not support NNR."); }
                return false;
            }
            // Fix wave I3 (R-R3-21): the Core said it has no usable NR3
            // model (mirrored nr3Runnable), so NR3 cannot run there.
            DspAssetService* assets = owner && owner->m_radioModel
                ? owner->m_radioModel->dspAssets() : nullptr;
            if (requested == NrSlot::NR3 && assets && !assets->nr3Runnable()) {
                if (reason) {
                    *reason = assets->nr3ModelStatus().isEmpty()
                        ? QStringLiteral("NR3 cannot run on this Core: no NR3 model file was found.")
                        : assets->nr3ModelStatus();
                }
                return false;
            }
            return true;
        });
        // R-R3-40: the operator's retry goes to the station. A station
        // echo (choosing the model the station reports) is not a retry.
        disconnect(slice, &SliceModel::nnrRetryRequested, this, nullptr);
        connect(slice, &SliceModel::nnrRetryRequested, this, [this, slice]() {
            if (m_applyingInbound) {
                return;
            }
            const auto outcome = requestNnrRetry(slice->sliceIndex());
            if (!outcome.sent) {
                slice->reportNnrEditResult(outcome.reason);
            }
        });
        disconnect(slice, &SliceModel::nnrDiagnosticsRequested, this, nullptr);
        connect(slice, &SliceModel::nnrDiagnosticsRequested, this,
                [this, slice](int testMode, int outputMode) {
            const auto outcome = requestNnrDiagnostics(slice->sliceIndex(), testMode, outputMode);
            if (!outcome.sent) { slice->reportNnrEditResult(outcome.reason); }
        });
    }
}

void StationClient::onWriteFlushTick()
{
    // handleSchema() pauses the wire for a full snapshot burst.  Preserve
    // coalesced operator edits until SnapshotComplete reopens this gate.
    if (!m_forwardLocalChanges) {
        return;
    }
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
            quint32 writeId = 0;
            if (propertyResultsAvailable()) {
                writeId = m_nextPropertyWriteId++;
                if (m_nextPropertyWriteId == 0) {
                    ++m_nextPropertyWriteId;
                }
                for (const auto& update : resolved) {
                    m_propertyWriteIds[batch.first].insert(update.name, writeId);
                }
            }
            send(SessionMessages::propertyWrite(batch.first, resolved, writeId));
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
    if (m_nextCommandId == 0) {
        ++m_nextCommandId;
    }
    send(SessionMessages::commandInvoke(verb, id, arguments));
    return id;
}

// ── IStationLink: the operator's clicks leaving this process ─────────────
//
// Every one of the five is the same three steps: build the arguments
// SessionCommandDispatcher's handler for that verb reads by name, hand
// them to invokeCommand(), and remember what the command was about so its
// result can be reported to a human. Nothing here touches RadioModel:
// applying the change on the way out is precisely the defect this seam
// closes, and the daemon's answer arrives on the ordinary mirror path.

namespace {

// The argument shapes are SessionCommandDispatcher's, read by NAME out of
// the CommandInvoke's `arguments` list (which reuses MirrorUpdate as a
// generic {name, kind, value} triple -- SessionMessage::arguments' own doc
// comment). `ordinal` is not consulted by any handler, so 0 throughout.
MirrorUpdate intArgument(const QByteArray& name, int value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Int64,
                         QVariant(static_cast<qlonglong>(value)) };
}

MirrorUpdate stringArgument(const QByteArray& name, const QString& value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Utf8, QVariant(value) };
}

MirrorUpdate boolArgument(const QByteArray& name, bool value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Bool, QVariant(value) };
}

MirrorUpdate doubleArgument(const QByteArray& name, double value)
{
    return MirrorUpdate{ 0, name, MirrorWireKind::Float64, QVariant(value) };
}

// R-R3-47 / R-R3-22: the amp's and tuner's own settings verbs, whose
// refusals the Advanced pages show (RadioModel::accessoryRequestRefused).
// L1 (R-R3-47, R-R3-22, R-R3-48): what an accessory request the Core
// refused was about, for RadioModel::accessoryRequestRefused; empty for
// every other verb. "pgxl" and "tgxl" (the amp's and tuner's connection,
// output limit and own settings), "rfkit", "interlock", "tci" (the
// station TCI server), "4o3a" (the 4O3A switch) and, for a fault history,
// the device it names ("faults" for any other).
QString accessoryRefusalDevice(const QByteArray& verb, const QString& faultsDevice)
{
    if (verb == "setPgxlName" || verb == "setPgxlHardware" || verb == "setPgxlNetwork"
        || verb == "savePgxlSettings" || verb == "readPgxlSettings"
        || verb == "setPgxlPowerCap" || verb == "configurePgxl" || verb == "disconnectPgxl"
        || verb == "setPgxlConnectionSettings") {
        return QStringLiteral("pgxl");
    }
    if (verb == "setTgxlName" || verb == "setTgxlNetwork" || verb == "saveTgxlSettings"
        || verb == "readTgxlSettings" || verb == "configureTgxl" || verb == "disconnectTgxl") {
        return QStringLiteral("tgxl");
    }
    if (verb == "configureRfKit" || verb == "disconnectRfKit" || verb == "setRfKitEnabled"
        || verb == "resetRfKitError") {
        return QStringLiteral("rfkit");
    }
    if (verb == "setTxInterlockPolicy") {
        return QStringLiteral("interlock");
    }
    if (verb == "setStationTci") {
        return QStringLiteral("tci");
    }
    if (verb == "setFourO3AEnabled") {
        return QStringLiteral("4o3a");
    }
    if (verb == "clearAccessoryFaults") {
        if (faultsDevice == QLatin1String("pgxl") || faultsDevice == QLatin1String("tgxl")
            || faultsDevice == QLatin1String("rfkit")) {
            return faultsDevice;
        }
        return QStringLiteral("faults");
    }
    return {};
}

} // namespace

StationClient::CommandOutcome StationClient::sendCommand(const QByteArray& verb, int sliceId,
                                                         const QList<MirrorUpdate>& arguments,
                                                         const QString& action)
{
    const quint32 id = invokeCommand(verb, arguments);
    if (id == 0) {
        // invokeCommand()'s own two refusals: no transport, or a transport
        // that has not finished authenticating. Both are the same thing to
        // an operator -- the station is not reachable right now -- and
        // both are ordinary rather than exceptional, because this is the
        // state a remote GUI sits in before its first handshake and again
        // for the whole of a reconnect backoff.
        return CommandOutcome{
            false,
            QStringLiteral("The station session is not established, so %1 was not sent.")
                .arg(action)
        };
    }
    PendingCommand pending;
    pending.verb = verb;
    pending.sliceId = sliceId;
    if (verb == "clearAccessoryFaults") {
        for (const MirrorUpdate& argument : arguments) {
            if (argument.name == "device") {
                pending.faultsDevice = argument.value.toString();
            }
        }
    }
    if ((verb == "requestStreamCtunPinned" || verb == "requestStreamCentre")
        && !m_radioModel.isNull()) {
        if (SliceModel* slice = m_radioModel->sliceById(sliceId)) {
            pending.streamEpoch = slice->streamEpoch();
        }
        if (verb == "requestStreamCtunPinned") {
            for (const MirrorUpdate& argument : arguments) {
                if (argument.name == "pinned" && argument.value.typeId() == QMetaType::Bool) {
                    pending.requestedPin = argument.value.toBool();
                    break;
                }
            }
        }
    }
    m_pendingCommands.insert(id, pending);
    return CommandOutcome{ true, QString(), id };
}

StationClient::CommandOutcome StationClient::requestAddSlice(const QString& initialPanId)
{
    return sendCommand("addSlice", -1, { stringArgument("initialPanId", initialPanId) },
                       QStringLiteral("the request for a new slice"));
}

StationClient::CommandOutcome StationClient::requestAddSliceOnPan(const QString& panId)
{
    return sendCommand("addSliceOnPan", -1, { stringArgument("panId", panId) },
                       QStringLiteral("the request for a new slice"));
}

StationClient::CommandOutcome StationClient::requestRemoveSlice(int sliceId)
{
    return sendCommand("removeSlice", sliceId, { intArgument("sliceId", sliceId) },
                       QStringLiteral("the request to close this slice"));
}

StationClient::CommandOutcome StationClient::requestActiveSlice(int sliceId)
{
    return sendCommand("setActiveSliceById", sliceId, { intArgument("sliceId", sliceId) },
                       QStringLiteral("the request to make slice %1 active").arg(sliceId));
}

StationClient::CommandOutcome StationClient::requestSliceSampleRate(int sliceId, int rateHz)
{
    return sendCommand(
        "requestSliceSampleRate", sliceId,
        { intArgument("sliceId", sliceId), intArgument("rateHz", rateHz) },
        QStringLiteral("the sample-rate change to %1 kHz").arg(rateHz / 1000));
}

StationClient::CommandOutcome StationClient::requestStreamCtunPinned(int sliceId, bool pinned)
{
    if (!remoteCtunAvailable()) {
        return { false, QStringLiteral("The station does not support remote C-Tune.") };
    }
    return sendCommand("requestStreamCtunPinned", sliceId,
                       { intArgument("sliceId", sliceId), boolArgument("pinned", pinned) },
                       QStringLiteral("the C-Tune pin change"));
}

StationClient::CommandOutcome StationClient::requestStreamCentre(int sliceId, double centreHz)
{
    if (!remoteCtunAvailable()) {
        return { false, QStringLiteral("The station does not support remote C-Tune.") };
    }
    return sendCommand("requestStreamCentre", sliceId,
                       { intArgument("sliceId", sliceId), doubleArgument("centreHz", centreHz) },
                       QStringLiteral("the C-Tune centre change"));
}

StationClient::CommandOutcome StationClient::requestConfigureTgxl(const QString& host, quint16 port)
{
    if (!remoteTgxlConfigAvailable()) {
        return { false, QStringLiteral("The station does not support remote TGXL configuration.") };
    }
    return sendCommand("configureTgxl", -1,
                       { stringArgument("host", host), intArgument("port", port) },
                       QStringLiteral("the TGXL configuration"));
}

bool StationClient::propertyResultsAvailable() const
{
    return m_handshakeComplete && m_agreedMinor >= kDspControlSessionProtocolMinor
        && m_capabilities.propertyResultVersion > 0;
}

StationClient::CommandOutcome StationClient::requestApplyNnrModels(quint32 revision)
{
    if (!nnrControlAvailable() || m_capabilities.dspAssetVersion < 1) {
        return {false, QStringLiteral("The station does not support NNR model application.")};
    }
    return sendCommand("nnr.applyModelSelection", -1, {intArgument("revision", revision)},
        QStringLiteral("the NNR model reconnect"));
}

bool StationClient::remoteNr3ModelsAvailable() const
{
    return m_handshakeComplete && m_agreedMinor >= kDspControlSessionProtocolMinor
        && m_capabilities.dspAssetVersion >= 2;
}

bool StationClient::remoteNotchControlAvailable() const
{
    return m_handshakeComplete && m_agreedMinor >= kDspControlSessionProtocolMinor
        && m_capabilities.notchControlVersion >= 1;
}

bool StationClient::remoteRadioHardwareAvailable() const
{
    return propertyResultsAvailable() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.radioHardwareVersion >= 1;
}

QString StationClient::radioHardwareUnavailableReason() const
{
    if (remoteRadioHardwareAvailable()) {
        return {};
    }
    if (!m_handshakeComplete) {
        return QStringLiteral("Connect to the Core to change the attenuator and preamp.");
    }
    return QStringLiteral("This Core cannot change its radio's attenuator for this "
                          "app. Updating the Core may help.");
}

bool StationClient::remoteHardwareConfigAvailable() const
{
    return propertyResultsAvailable() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.radioHardwareVersion >= 2;
}

QString StationClient::hardwareConfigUnavailableReason() const
{
    if (remoteHardwareConfigAvailable()) {
        return {};
    }
    if (!m_handshakeComplete) {
        return QStringLiteral("Connect to the Core to change the radio's hardware settings.");
    }
    return QStringLiteral("This Core cannot change its radio's hardware settings for this "
                          "app. Updating the Core may help.");
}

StationClient::CommandOutcome StationClient::requestAlexRxAntenna(Band band, int antenna,
                                                                  bool rxOnly)
{
    if (!remoteHardwareConfigAvailable() || m_capabilities.radioHardwareVersion < 3) {
        return {false, hardwareConfigUnavailableReason()};
    }
    return sendCommand("setAlexRxAntenna", -1,
                       { intArgument("band", static_cast<int>(band)),
                         intArgument("antenna", antenna), boolArgument("rxOnly", rxOnly) },
                       QStringLiteral("the antenna change"));
}

StationClient::CommandOutcome StationClient::requestIoBoardProbe()
{
    if (!remoteHardwareConfigAvailable()) {
        return {false, hardwareConfigUnavailableReason()};
    }
    return sendCommand("requestIoBoardProbe", -1, {},
                       QStringLiteral("the I/O board probe"));
}

bool StationClient::nnrControlAvailable() const
{
    return propertyResultsAvailable() && m_capabilities.nnrVersion > 0;
}

StationClient::CommandOutcome StationClient::requestNnrDiagnostics(int sliceId, int testMode, int outputMode)
{
    if (!nnrControlAvailable()) {
        return {false, QStringLiteral("The station does not support NNR diagnostics.")};
    }
    return sendCommand("nnr.setDiagnostics", sliceId,
        {intArgument("sliceId", sliceId), intArgument("testMode", testMode), intArgument("outputMode", outputMode)},
        QStringLiteral("the NNR diagnostic change"));
}

bool StationClient::nnrRetryAvailable() const
{
    return nnrControlAvailable() && m_agreedMinor >= kNnrLimitSessionProtocolMinor;
}

StationClient::CommandOutcome StationClient::requestNnrRetry(int sliceId)
{
    if (!nnrRetryAvailable()) {
        return {false, QStringLiteral("This station cannot try noise reduction again. "
                                      "Update the station software.")};
    }
    return sendCommand("nnr.tryAgain", sliceId, {intArgument("sliceId", sliceId)},
        QStringLiteral("trying noise reduction again"));
}

StationClient::CommandOutcome StationClient::requestConfigurePgxl(const QString& host, quint16 port)
{
    if (!remotePgxlControlAvailable()) {
        return { false, QStringLiteral("The station does not support remote PGXL configuration.") };
    }
    return sendCommand("configurePgxl", -1,
                       { stringArgument("host", host), intArgument("port", port) },
                       QStringLiteral("the Power Genius address"));
}

StationClient::CommandOutcome StationClient::requestDisconnectPgxl()
{
    if (!remotePgxlControlAvailable()) {
        return { false, QStringLiteral("The station does not support remote PGXL configuration.") };
    }
    return sendCommand("disconnectPgxl", -1, {}, QStringLiteral("the Power Genius disconnect"));
}

StationClient::CommandOutcome StationClient::requestPgxlConnectionSettings(bool autoReconnect,
                                                                           int keepaliveSec,
                                                                           int pingSec)
{
    if (!remotePgxlControlAvailable()) {
        return { false, QStringLiteral("The station does not support remote PGXL configuration.") };
    }
    return sendCommand("setPgxlConnectionSettings", -1,
                       { boolArgument("autoReconnect", autoReconnect),
                         intArgument("keepaliveSec", keepaliveSec),
                         intArgument("pingSec", pingSec) },
                       QStringLiteral("the Power Genius connection settings"));
}

// R-R3-47 / R-R3-22 (remoteRfKitControlVersion 2): the Core's RF-Kit.
StationClient::CommandOutcome StationClient::requestConfigureRfKit(const QString& host, quint16 port)
{
    if (!remoteRfKitControlAvailable()) {
        return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") };
    }
    return sendCommand("configureRfKit", -1,
                       { stringArgument("host", host), intArgument("port", port) },
                       QStringLiteral("the RF-Kit amplifier address"));
}

bool StationClient::rfKitSettingsAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remoteRfKitControlVersion >= 3;
}

StationClient::CommandOutcome StationClient::requestResetRfKitError()
{
    if (!rfKitSettingsAvailable()) {
        return IStationLink::requestResetRfKitError();
    }
    return sendCommand("resetRfKitError", -1, {},
                       QStringLiteral("the RF-Kit amplifier's error reset"));
}

StationClient::CommandOutcome StationClient::requestDisconnectRfKit()
{
    if (!remoteRfKitControlAvailable()) {
        return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") };
    }
    return sendCommand("disconnectRfKit", -1, {}, QStringLiteral("the RF-Kit amplifier disconnect"));
}

StationClient::CommandOutcome StationClient::requestRfKitEnabled(bool enabled)
{
    if (!remoteRfKitControlAvailable()) {
        return { false, QStringLiteral("This Core does not offer RF-Kit amplifier setup to this app.") };
    }
    return sendCommand("setRfKitEnabled", -1, { boolArgument("enabled", enabled) },
                       QStringLiteral("the RF-Kit amplifier switch"));
}

// R-R3-48 (stationTciVersion 1): the one TCI switch and port.
StationClient::CommandOutcome StationClient::requestStationTci(bool enabled, quint16 port)
{
    if (!stationTciAvailable()) {
        return { false, QStringLiteral("This Core has no TCI server for the station.") };
    }
    return sendCommand("setStationTci", -1,
                       { boolArgument("enabled", enabled), intArgument("port", port) },
                       QStringLiteral("the station's TCI server switch"));
}

// R-R3-47 / R-R3-22 (accessoryDataVersion 1): the Core's accessory records
// and settings.
StationClient::CommandOutcome StationClient::requestTxInterlockPolicy(int mode, int graceMs,
                                                                      bool swrGateEnabled,
                                                                      double swrGateMax)
{
    if (!accessoryDataAvailable()) {
        return IStationLink::requestTxInterlockPolicy(mode, graceMs, swrGateEnabled, swrGateMax);
    }
    return sendCommand("setTxInterlockPolicy", -1,
                       { intArgument("mode", mode), intArgument("graceMs", graceMs),
                         boolArgument("swrGateEnabled", swrGateEnabled),
                         doubleArgument("swrGateMax", swrGateMax) },
                       QStringLiteral("the transmit interlock"));
}

StationClient::CommandOutcome StationClient::requestPgxlPowerCap(bool enabled, int watts)
{
    if (!accessoryDataAvailable()) {
        return IStationLink::requestPgxlPowerCap(enabled, watts);
    }
    return sendCommand("setPgxlPowerCap", -1,
                       { boolArgument("enabled", enabled), intArgument("watts", watts) },
                       QStringLiteral("the Power Genius output limit"));
}

StationClient::CommandOutcome StationClient::requestClearAccessoryFaults(const QString& device)
{
    if (!accessoryDataAvailable()) {
        return IStationLink::requestClearAccessoryFaults(device);
    }
    return sendCommand("clearAccessoryFaults", -1, { stringArgument("device", device) },
                       QStringLiteral("the fault history"));
}

// R-R3-47 / R-R3-22 (remotePgxlControlVersion 3, remoteTgxlControlVersion
// 1): the amp's and tuner's own settings. A Core that did not offer them is
// not asked; the window says why.
StationClient::CommandOutcome StationClient::requestPgxlName(const QString& name)
{
    if (!pgxlDeviceSettingsAvailable()) {
        return IStationLink::requestPgxlName(name);
    }
    return sendCommand("setPgxlName", -1, { stringArgument("name", name) },
                       QStringLiteral("the Power Genius name"));
}

StationClient::CommandOutcome StationClient::requestPgxlHardware(const QString& setting,
                                                                 const QString& value)
{
    if (!pgxlDeviceSettingsAvailable()) {
        return IStationLink::requestPgxlHardware(setting, value);
    }
    if (setting == QLatin1String("ledIntensity")) {
        return sendCommand("setPgxlHardware", -1, { intArgument("ledIntensity", value.toInt()) },
                           QStringLiteral("the Power Genius hardware setting"));
    }
    return sendCommand("setPgxlHardware", -1, { stringArgument(setting.toUtf8(), value) },
                       QStringLiteral("the Power Genius hardware setting"));
}

StationClient::CommandOutcome StationClient::requestPgxlNetwork(bool dhcp, const QString& address,
                                                                const QString& netmask,
                                                                const QString& gateway)
{
    if (!pgxlDeviceSettingsAvailable()) {
        return IStationLink::requestPgxlNetwork(dhcp, address, netmask, gateway);
    }
    return sendCommand("setPgxlNetwork", -1,
                       { boolArgument("dhcp", dhcp), stringArgument("address", address),
                         stringArgument("netmask", netmask),
                         stringArgument("gateway", gateway) },
                       QStringLiteral("the Power Genius network settings"));
}

StationClient::CommandOutcome StationClient::requestPgxlSaveAndRestart()
{
    if (!pgxlDeviceSettingsAvailable()) {
        return IStationLink::requestPgxlSaveAndRestart();
    }
    return sendCommand("savePgxlSettings", -1, {},
                       QStringLiteral("the Power Genius Save & Reboot"));
}

StationClient::CommandOutcome StationClient::requestPgxlReadSettings()
{
    if (!pgxlDeviceSettingsAvailable()) {
        return IStationLink::requestPgxlReadSettings();
    }
    return sendCommand("readPgxlSettings", -1, {},
                       QStringLiteral("the request for the Power Genius settings"));
}

StationClient::CommandOutcome StationClient::requestTgxlName(const QString& name)
{
    if (!tgxlDeviceSettingsAvailable()) {
        return IStationLink::requestTgxlName(name);
    }
    return sendCommand("setTgxlName", -1, { stringArgument("name", name) },
                       QStringLiteral("the Tuner Genius name"));
}

StationClient::CommandOutcome StationClient::requestTgxlNetwork(bool dhcp, const QString& address,
                                                                const QString& netmask,
                                                                const QString& gateway)
{
    if (!tgxlDeviceSettingsAvailable()) {
        return IStationLink::requestTgxlNetwork(dhcp, address, netmask, gateway);
    }
    return sendCommand("setTgxlNetwork", -1,
                       { boolArgument("dhcp", dhcp), stringArgument("address", address),
                         stringArgument("netmask", netmask),
                         stringArgument("gateway", gateway) },
                       QStringLiteral("the Tuner Genius network settings"));
}

StationClient::CommandOutcome StationClient::requestTgxlSaveAndRestart()
{
    if (!tgxlDeviceSettingsAvailable()) {
        return IStationLink::requestTgxlSaveAndRestart();
    }
    return sendCommand("saveTgxlSettings", -1, {},
                       QStringLiteral("the Tuner Genius Save & Reboot"));
}

StationClient::CommandOutcome StationClient::requestTgxlReadSettings()
{
    if (!tgxlDeviceSettingsAvailable()) {
        return IStationLink::requestTgxlReadSettings();
    }
    return sendCommand("readTgxlSettings", -1, {},
                       QStringLiteral("the request for the Tuner Genius settings"));
}

StationClient::CommandOutcome StationClient::requestDisconnectTgxl()
{
    if (!remoteTgxlConfigAvailable()) {
        return { false, QStringLiteral("The station does not support remote TGXL configuration.") };
    }
    return sendCommand("disconnectTgxl", -1, {}, QStringLiteral("the TGXL disconnect"));
}

StationClient::CommandOutcome StationClient::requestFourO3AEnabled(bool enabled)
{
    if (!remoteFourO3AControlAvailable()) {
        return { false, QStringLiteral("The station does not support remote 4O3A control.") };
    }
    return sendCommand("setFourO3AEnabled", -1, { boolArgument("enabled", enabled) },
                       QStringLiteral("the 4O3A master change"));
}

void StationClient::handleCommandResult(const SessionMessage& message)
{
    // R-R3-21: the app shows a refusal in user words (OperatorReasonText),
    // so the Core's own text is kept here, each time, as it arrived.
    if (!message.accepted) {
        qCInfo(lcStationClient).noquote()
            << "Station refused" << QString::fromUtf8(message.commandVerb)
            << "command" << message.commandId << ":" << message.reason;
    }
    if (message.commandVerb == "ps3.subscribeDisplay" && m_pendingPs3Display
        && m_pendingPs3Display->first == message.commandId) {
        const bool enabled = m_pendingPs3Display->second;
        m_pendingPs3Display.reset();
        const QPointer<StationClient> self(this);
        const quint32 epoch = m_sessionEpoch;
        emit ps3DisplaySubscriptionFinished(message.commandId, enabled,
                                             message.accepted, message.reason);
        if (!self || m_sessionEpoch != epoch || !m_sessionActive) { return; }
    }
    // Taken, not read: an id is answered exactly once, and leaving the
    // entry behind would grow this map for the life of the session.
    // A result for an id this client does not hold is not an error worth
    // refusing -- it is what a second CommandResult, or a result for a
    // command sent through the generic invokeCommand() rather than the
    // five typed verbs, looks like -- so the signal below still fires and
    // only the operator-facing routing is skipped.
    const PendingCommand pending = m_pendingCommands.take(message.commandId);

    const bool isFourO3ACommand = pending.verb == "setFourO3AEnabled";
    bool newerFourO3ACommand = false;
    if (isFourO3ACommand) {
        for (auto it = m_pendingCommands.cbegin(); it != m_pendingCommands.cend(); ++it) {
            if (it.value().verb == pending.verb) {
                newerFourO3ACommand = true;
                break;
            }
        }
        if (!newerFourO3ACommand && !m_radioModel.isNull()) {
            m_radioModel->reportStationFourO3ACommandFinished(message.accepted, message.reason);
        }
    }

    // notch.* refusals are shown by NotchModel itself (notchAddRejected /
    // notchRequestRefused), in the words the window uses for a local one.
    if (!message.accepted && !m_radioModel.isNull()
        && !message.commandVerb.startsWith("ps3.") && !message.commandVerb.startsWith("dspAssets.")
        && !message.commandVerb.startsWith("notch.")) {
        // The station's OWN reason, relayed verbatim. Wording a refusal
        // here instead would put this client's guess in front of an
        // operator for a decision the daemon made -- and on a bench that
        // is indistinguishable from the click having silently done
        // nothing, which is the shape of the defect this round closes.
        const QString reason = message.reason.isEmpty()
            ? QStringLiteral("The station refused the request without giving a reason.")
            : message.reason;
        // requestSliceSampleRate is the one verb whose refusal already has
        // a slice-scoped signal locally (sliceRetuneRejected, which
        // MainWindow toasts for 6 s because it names a frequency), so it
        // keeps using it. `pending.verb` rather than message.commandVerb
        // so an unrecognised or absent echo cannot misroute; the two agree
        // on every path SessionCommandDispatcher produces.
        if (pending.verb.startsWith("nnr.")) {
            if (auto* slice = m_radioModel->sliceById(pending.sliceId)) {
                slice->reportNnrEditResult(reason);
            }
        } else if (pending.verb == "requestSliceSampleRate") {
            m_radioModel->reportStationRetuneRejected(pending.sliceId, reason);
        } else if (const QString device = accessoryRefusalDevice(pending.verb,
                                                                 pending.faultsDevice);
                   !device.isEmpty()) {
            // L1 (R-R3-47, R-R3-22, R-R3-48): an accessory refusal has its
            // own route (the pages that sent it show it; MainWindow says
            // it), never the slice one.
            m_radioModel->reportStationAccessoryRefusal(device, reason, message.commandId);
        } else {
            m_radioModel->reportStationSliceCommandRejected(reason);
        }
        // R-R3-46 fix wave: a refused band antenna leaves the window's
        // values as the Core's; the Setup tab that showed the click re-reads.
        if (pending.verb == "setAlexRxAntenna") {
            if (AlexAntennaFacade* alex = m_radioModel->alexAntennaFacade()) {
                alex->reportBandEditRefused();
            }
        }
    }

    // Follow-up 3: an accepted accessory request no page needs to claim.
    if (message.accepted && !m_radioModel.isNull()
        && !accessoryRefusalDevice(pending.verb, pending.faultsDevice).isEmpty()) {
        m_radioModel->forgetAccessoryRequest(message.commandId);
    }

    const bool isCtunCommand = pending.verb == "requestStreamCtunPinned"
        || pending.verb == "requestStreamCentre";
    bool newerCtunCommand = false;
    if (isCtunCommand) {
        for (auto it = m_pendingCommands.cbegin(); it != m_pendingCommands.cend(); ++it) {
            const PendingCommand& candidate = it.value();
            if (candidate.verb == pending.verb && candidate.sliceId == pending.sliceId
                && candidate.streamEpoch == pending.streamEpoch) {
                newerCtunCommand = true;
                break;
            }
        }
        if (!newerCtunCommand) {
            if (pending.verb == "requestStreamCtunPinned") {
                emit streamCtunPinFinished(pending.sliceId, pending.streamEpoch,
                                           pending.requestedPin, message.accepted);
            } else {
                emit streamCentreFinished(pending.sliceId, pending.streamEpoch,
                                          message.accepted);
            }
        }
    }

    if (message.commandVerb.startsWith("notch.") && m_radioModel && m_radioModel->notchModel()) {
        const auto values = dspCommandValues(message.updates);
        m_radioModel->notchModel()->receiveRemoteResult(message.commandId, message.commandVerb,
            message.accepted, message.reason, values.value_or(QVariantMap{}));
    }
    if (message.commandVerb.startsWith("dspAssets.") && m_radioModel) {
        if (const auto values = dspCommandValues(message.updates)) {
            m_radioModel->dspAssets()->receiveRemoteResult(message.commandId, message.commandVerb,
                message.accepted, message.reason, *values);
        }
    }
    if (message.commandVerb.startsWith("ps3.") && m_radioModel) {
        if (const auto values = dspCommandValues(message.updates)) {
            const QString state = values->value("phase").toString();
            Ps3ActionPhase phase = Ps3ActionPhase::Failed;
            if (message.accepted && state == "pending") {
                phase = Ps3ActionPhase::Pending;
            } else if (message.accepted && state == "completed") {
                phase = Ps3ActionPhase::Completed;
            } else if (message.accepted && state == "accepted") {
                phase = Ps3ActionPhase::Accepted;
            }
            m_radioModel->pureSignalFacade()->receiveRemoteActionResult(message.commandId,
                message.commandVerb, phase, message.reason, *values);
        }
    }
    emit commandResponse(message);
    emit commandResult(message.commandId, message.accepted, message.reason);
}

void StationClient::send(const SessionMessage& message)
{
    if (m_transport == nullptr) {
        return;
    }
    m_transport->sendText(SessionMessages::encode(message));
}

bool StationClient::remoteCtunAvailable() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen()
        && m_agreedMinor >= kRemoteCtunSessionProtocolMinor
        && m_capabilities.remoteCtunVersion >= 1;
}

bool StationClient::remoteTgxlConfigAvailable() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen()
        && m_agreedMinor >= kRemoteTgxlConfigSessionProtocolMinor
        && m_capabilities.remoteTgxlConfigVersion >= 1;
}

bool StationClient::stationLinkReady() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen();
}

bool StationClient::remoteAmplifierStatusAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remotePgxlControlVersion >= 1;
}

bool StationClient::remotePgxlControlAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remotePgxlControlVersion >= 2;
}

bool StationClient::remoteRfKitStatusAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remoteRfKitControlVersion >= 1;
}

bool StationClient::remoteRfKitControlAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remoteRfKitControlVersion >= 2;
}

bool StationClient::accessoryDataAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.accessoryDataVersion >= 1;
}

bool StationClient::pgxlDeviceSettingsAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remotePgxlControlVersion >= 3;
}

bool StationClient::tgxlDeviceSettingsAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.remoteTgxlControlVersion >= 1;
}

bool StationClient::stationTciAvailable() const
{
    return stationLinkReady() && m_agreedMinor >= kRadioIdentitySessionProtocolMinor
        && m_capabilities.stationTciVersion >= 1;
}

bool StationClient::coreServesTciOnThisComputer() const
{
    // What the Core last said it offers, kept while the link is down: the
    // Core keeps its server running whether or not this window is there.
    if (m_agreedMinor < kRadioIdentitySessionProtocolMinor
        || m_capabilities.stationTciVersion < 1) {
        return false;
    }
    if (m_coreOnThisComputerForTest >= 0) {
        return m_coreOnThisComputerForTest == 1;
    }
    const QString host = m_lastUrl.host();
    if (host.isEmpty()) {
        return false;
    }
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0) {
        return true;
    }
    const QHostAddress address(host);
    if (address.isNull()) {
        return false;
    }
    if (address.isLoopback()) {
        return true;
    }
    const auto local = QNetworkInterface::allAddresses();
    for (const QHostAddress& mine : local) {
        if (mine.isEqual(address, QHostAddress::ConvertV4MappedToIPv4)) {
            return true;
        }
    }
    return false;
}

bool StationClient::remoteFourO3AControlAvailable() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen()
        && m_agreedMinor >= kRemoteFourO3AControlSessionProtocolMinor
        && m_capabilities.remoteFourO3AControlVersion >= 1;
}

bool StationClient::telemetryAvailable() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen()
        && m_agreedMinor >= kStationTelemetrySessionProtocolMinor
        && m_capabilities.stationTelemetryVersion >= 1;
}

std::optional<SessionTransportTelemetry> StationClient::transportTelemetry() const
{
    if (!m_sessionActive || !m_handshakeComplete || !m_transport) { return std::nullopt; }
    return m_transport->telemetry();
}

bool StationClient::mediaAvailable() const
{
    return m_sessionActive && m_authenticated && m_handshakeComplete
        && m_transport && m_transport->isOpen()
        && m_agreedMinor >= kMediaSessionProtocolMinor
        && m_capabilities.remoteMediaVersion >= 1;
}

bool StationClient::remoteWidebandAvailable() const
{
    return mediaAvailable() && m_agreedMinor >= kRemoteWidebandSessionProtocolMinor
        && m_capabilities.remoteWidebandDisplayVersion >= 1;
}

bool StationClient::remoteAudioStatusAvailable() const
{
    return mediaAvailable() && m_agreedMinor >= kRemoteAudioStatusSessionProtocolMinor
        && m_capabilities.remoteAudioStatusVersion >= 1;
}

bool StationClient::spectrumGrantAvailable() const
{
    return mediaAvailable() && m_agreedMinor >= kRemoteSpectrumGrantSessionProtocolMinor
        && m_capabilities.spectrumGrantVersion >= 1;
}

std::optional<DisplayBudgetLimits> StationClient::remoteDisplayBudgetLimits() const
{
    if (!mediaAvailable() || m_agreedMinor < kRemoteDisplayBudgetSessionProtocolMinor
        || m_capabilities.remoteDisplayBudgetVersion < 1) { return std::nullopt; }
    return m_capabilities.displayBudget;
}

DisplayBudgetReason StationClient::remoteDisplayBudgetReason() const
{
    if (!remoteDisplayBudgetLimits() || m_agreedMinor < kDisplayBudgetReasonSessionProtocolMinor
        || !m_capabilities.displayBudgetReason) {
        return DisplayBudgetReason::None;
    }
    return *m_capabilities.displayBudgetReason;
}

bool StationClient::remotePs3DisplaySubscribed() const
{
    return remoteDisplayBudgetLimits() && m_capabilities.remotePs3DisplaySubscribed;
}

quint32 StationClient::requestPs3DisplaySubscription(bool enabled)
{
    if (!mediaAvailable() || m_capabilities.psDisplayVersion < 1) { return 0; }
    if (!remoteDisplayBudgetLimits()) {
        return invokeCommand("ps3.subscribeDisplay", {{0, "enabled", MirrorWireKind::Bool, enabled}});
    }
    // One acknowledged transition at a time. Arm its identity before either
    // notification or transport can reenter; no guessed release on timeout.
    if (m_pendingPs3Display) { return 0; }
    const quint32 id = m_nextCommandId++;
    if (m_nextCommandId == 0) { ++m_nextCommandId; }
    m_pendingPs3Display = qMakePair(id, enabled);
    const QPointer<StationClient> self(this);
    const quint32 epoch = m_sessionEpoch;
    emit ps3DisplaySubscriptionStarted(id, enabled);
    if (!self || m_sessionEpoch != epoch || !mediaAvailable()
        || !m_pendingPs3Display || m_pendingPs3Display->first != id) { return 0; }
    send(SessionMessages::commandInvoke("ps3.subscribeDisplay", id,
        {{0, "enabled", MirrorWireKind::Bool, enabled}}));
    return id;
}

void StationClient::noteMediaEstablished(quint32 expectedEpoch)
{
    // R-R3-28. The deferred half of the SnapshotComplete reset: with media
    // negotiated, this is the proven success. Epoch-scoped so a ready from
    // a retired peer cannot reset a newer session's schedule.
    if (expectedEpoch == 0 || expectedEpoch != m_sessionEpoch || !mediaAvailable()) {
        return;
    }
    m_reconnectAttempts = 0;
}

bool StationClient::sendMediaControl(const QJsonObject& payload, quint32 expectedEpoch)
{
    if (!mediaAvailable() || expectedEpoch != m_sessionEpoch) {
        return false;
    }
    SessionMessage message;
    message.kind = SessionMessageKind::MediaControl;
    message.mediaPayload = payload;
    const QByteArray wire = SessionMessages::encode(message);
    if (wire.isEmpty()) {
        return false;
    }
    m_transport->sendText(wire);
    return true;
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
