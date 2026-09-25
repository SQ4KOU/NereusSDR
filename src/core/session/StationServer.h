#pragma once
// =================================================================
// src/core/session/StationServer.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 18.
//
// The daemon half of the wss session. Everything tasks 7 through 17 built
// converges here: this is the class that puts StateMirror, ObjectRegistry,
// SessionCommandDispatcher and SettingsProxyServer on a socket, behind
// task 17's TLS certificate and task 18's own TokenStore.
//
// ── THE CONNECT SEQUENCE (parent design section 7.0) ─────────────────────
//
//   TLS establish
//     -> protocol hello carrying a semantic version from both ends
//     -> authentication
//     -> capability exchange
//     -> state snapshot
//     -> snapshot-complete marker
//
// Concretely, per accepted connection, the daemon:
//
//   1. sends Hello (its own major.minor plus its AppSettings schema
//      version) the moment the socket is up, so a client can refuse
//      without ever revealing that it holds a token;
//   2. waits for the client's Hello, and REFUSES on a major mismatch with
//      a reason naming BOTH versions (section 7.0: "the connection is
//      refused with a message naming both versions rather than failing
//      obscurely"). Equal major with a differing minor NEGOTIATES DOWN to
//      the lower of the two and records it as agreedMinor();
//   3. waits for AuthRequest and runs it past TokenStore, which is
//      rate-limited (section 7.1);
//   4. sends AuthResult, then Capabilities;
//   5. sends the settings snapshot (SettingsProxyServer::buildSnapshot);
//   6. attaches the state mirror, which sends a schema per class, an
//      object.create per live object, and the snapshot-complete marker
//      LAST (StateMirror::attachSession).
//
// ── UP TO FOUR DEVICES, ONE SHARED MIRROR FOR NOW (topology) ────────────
//
// Task 18 chose one shared StateMirror because the remote design's section
// 7.1 then allowed one session at a time, with a newcomer preempting it.
// iPhone app Task 71 replaces that: up to four devices (kMaxDeviceSessions)
// hold sessions at once, each a device (a paired device, a window signed in
// with the older token, or a hosting desktop's own window), and the
// operator's control is the transmit holder, not the one session (the
// several-devices design, docs/architecture/2026-09-24-several-devices-on-
// one-core-design.md, sections 2 and 4). No sign-in ever ends another
// device's session. DeviceSessionRegistry decides who is let in after every
// accepted sign-in: a device that already holds a place (live, or away in
// its 180 s) replaces its own older connection with sameDevice; with a
// place free the device is admitted; a full Core refuses, retryable.
//
// The mirror is still one, shared, until Task 72 gives each device a view.
// StateMirror::hasAttachedSession() is a single bool and attachSession()
// clears the outbound coalescer, so a newcomer's attach would discard
// deltas the others still had pending. promoteToSession() therefore flushes
// the coalescer to every admitted session first, and the attach's schema,
// object.create and snapshot.complete messages go to the newcomer alone
// (m_burstTarget); only deltas go to everyone. Echo suppression is still
// the shared mirror's: a device's write reaches the others only through
// side effects the mirror forwards, which Task 72's per-writer echo fixes.
// Media and telemetry stay with one session (m_mediaSession, the first
// admitted while none holds it) until Task 76 gives each device a media
// controller: another admitted session is told no media is available
// (remoteMediaVersion 0 in its capabilities) and its media control is
// ignored, so media never goes to two sessions at once.
//
// A connection that has NOT yet authenticated does not touch the mirror at
// all -- it holds nothing but its own handshake state -- so a peer
// mid-handshake cannot disturb a live session. That is what keeps a failed
// or hostile connection attempt from being a denial of service against the
// operator's own sessions. Two bounds keep the mid-handshake population
// from becoming its own problem: kMaxConcurrentPeers caps how many sockets
// can exist at once, and kDefaultAuthDeadlineMs drops any that has not
// finished connecting in time (a peer that opens a socket and answers pings
// but never authenticates would otherwise live forever).
//
// ── THREADING ────────────────────────────────────────────────────────────
//
// This object, its QWebSocketServer, every transport it accepts, the
// StateMirror, the ObjectRegistry, the SessionCommandDispatcher, the
// SettingsProxyServer, the daemon's AppSettings and the RadioModel with
// every SliceModel under it ALL live on ONE thread: RadioModel's.
//
// That is not a convenience. StateMirror.h's attachSession() precondition
// spells out what breaks otherwise, and it breaks SILENTLY: every
// connection StateMirror::watch() makes is Qt::AutoConnection, which
// resolves to a direct call only while sender and receiver share a thread.
// Give the session its own thread and construct the mirror there, and
// onWatchedPropertyChanged() starts running AFTER applyInbound() has
// returned and cleared its m_applying guard, so every echo of a remote
// peer's own write leaks straight back to it -- with no test failing,
// because every test in this suite constructs on one thread.
// SessionCommandDispatcher.h states the same requirement for dispatch(),
// and SettingsProxyServer.h states it for AppSettings, which has no
// internal locking at all.
//
// So: the session read loop runs on the RadioModel thread. There is no I/O
// thread. Qt's WebSocket stack is event-loop driven, so this costs nothing
// a headless daemon notices; what it buys is that three separate
// documented invariants stay true by construction rather than by review.
//
// ── THE HEARTBEAT (task 18 step 2a) ──────────────────────────────────────
//
// **A TCP connection that dies silently never produces a close.** A laptop
// lid, a cell handoff, a NAT timeout: in all three the peer simply stops
// existing as far as the wire is concerned, and nothing about the socket
// says so. Without a heartbeat the daemon sits believing a dead client is
// alive, which is the state parent section 12.1's TX watchdog exists to
// make impossible.
//
// TciServer.cpp's 20 s QTimer + QWebSocket::ping is the in-tree precedent
// and this class copies its SHAPE. It deliberately does NOT copy its
// DETECTION MODEL. That precedent's own comment says it plainly:
//
//     "we don't expect a Pong back within any timeout -- we use the ping
//     itself to surface a dead socket via Qt's automatic write-error path"
//
// A write error is not a timely signal. A silently dead TCP connection can
// take minutes of retransmit backoff to produce one, and on a path where
// the far side vanished mid-NAT-mapping it may never produce one at all.
// So this class TRACKS PONGS: SessionTransport::pongReceived() resets a
// per-peer miss counter, and a peer that lets kDefaultMaxMissedPongs
// consecutive pings go unanswered is declared dead and closed.
//
// **Only a pong counts, deliberately.** An arbitrary inbound frame proves
// the peer's send path works; a pong proves the ROUND TRIP works, because
// the peer's WebSocket layer only emits one in response to a ping it
// actually received. The asymmetric case -- a client happily sending
// commands whose replies never reach it -- is exactly the case where the
// operator most needs the session torn down, and counting any inbound
// traffic as liveness would keep it alive indefinitely.
//
// **The numbers.** 20 s interval, 2 missed pongs, so a peer is declared
// dead between 40 s and 60 s after it actually went silent. The interval
// is TciServer's, which is this tree's own precedent and is itself ported
// from Thetis. The miss count comes from the only shipping configuration
// on real internet links anyone has measured for us: piHPSDR runs a 15 s
// heartbeat against a 30 s receive timeout (server_thread.c:925-934
// [@4aa95c5], verified 2026-08-08 by the maintainer), a ratio of two.
// One miss (20 s) would kill a session on a single dropped ping over a
// lossy mobile link, which is a false positive on precisely the links this
// feature exists for; three (60-80 s) is longer than a session-liveness
// signal needs to be.
//
// **What this is NOT: section 12.1's keyed-state deadline.** That section
// requires "a deadline in the low hundreds of milliseconds while MOX is
// asserted", which is two orders of magnitude tighter than the idle
// deadline above. R2 has no remote TX at all (design addendum section 2:
// "no MOX; TX is R4 in its entirety"), so there is no keyed state here to
// hang that deadline on and building one would be untestable speculation.
// The mechanism is the reusable part: setHeartbeatIntervalMs() and
// setMaxMissedPongs() are live-settable, so whichever task brings remote
// TX tightens an existing, soaked mechanism rather than introducing a
// second one at the moment it first becomes safety-critical. Pulling the
// heartbeat forward into R2 was a maintainer directive with exactly that
// soak time as its stated purpose.
//
// ── SCOPE ────────────────────────────────────────────────────────────────
//
// No ICE, no codecs, no spectrum, no audio. R2's demo is a blank
// panadapter, a blank waterfall and silent speakers, on purpose.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 18: the daemon
//                                    half of the wss session. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-23  J.J. Boyd / KG4VCF  R-R3-08/37/40: displayBudgetLimits()
//                                    is the budget in force for the
//                                    session; a computed one reaches only
//                                    minor-11 peers. AI-assisted
//                                    implementation via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 4 (R-IOS-01): link
//                                    majors both ways (the hello's
//                                    `majors`, the plain-words refusal),
//                                    the peer's declared features and an
//                                    explicit TLS 1.2 minimum. AI-assisted
//                                    transformation via Anthropic Claude
//                                    Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-47 / R-R3-22: pgxlControlVersion
//                                    3 and tgxlControlVersion 1, the amp's
//                                    and tuner's own settings. AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  R-R3-49 / R-R3-47: remoteTgxlControlVersion 2.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 12 (R-IOS-08,
//                                    R-IOS-02): the Core's identity key,
//                                    paired devices and device sign-in;
//                                    token enrolment; the first-run banner
//                                    names the identity key. AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 13 (R-IOS-08): the
//                                    `devices` object for a device that
//                                    declares deviceAuth, its verbs, and a
//                                    connection ended when its device is
//                                    removed or the token it signed in
//                                    with is retired. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 14 (R-IOS-08, D37):
//                                    the pairing window, pairing by one
//                                    tap and by code (SPAKE2+EE), the
//                                    hello's features.pairing,
//                                    pairingVersion 1, pairing.open and
//                                    pairing.close, and the code sent only
//                                    to a connection signed in with a
//                                    paired device's key. AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-24: Part C fix wave: the pairing code is never printed
//               to standard output (the journal on a packaged Core). J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
//   2026-09-24: Part C fix wave (security Minors R1-M1, M2, M4,
//               M5): the confirm-step recheck, the step 1 point check, the
//               per-address handshake cap and 0600 on load. J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic Claude
//               Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 19 (R-IOS-06): the
//                                    `catalog` object and
//                                    stationCatalogVersion 1. AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 20 (R-IOS-27):
//                                    displayExtrasVersion 1. AI-assisted
//                                    via Anthropic Claude Code.
//   2026-09-25  J.J. Boyd / KG4VCF  iPhone app Task 71 (R-IOS-02): up to
//                                    four device sessions, admission and
//                                    the same-device rule in place of
//                                    preemption, the away state,
//                                    session.leave, sessionHolder 1 and
//                                    the `connectedDevices` object; 24
//                                    sockets. AI-assisted via Anthropic
//                                    Claude Code.
// =================================================================

#include <QHash>
#include <QHostAddress>
#include <QPair>
#include <QObject>
#include <QPointer>
#include <QSslConfiguration>
#include <QString>

#include <memory>
#include <functional>
#include <optional>
#include <utility>

#include "core/session/LinkVersion.h"
#include "core/session/SessionMessages.h"
#include "core/session/StationCapabilities.h"

QT_BEGIN_NAMESPACE
class QThread;
class QTimer;
class QWebSocketServer;
QT_END_NAMESPACE

namespace NereusSDR {

class AppSettings;
class CertificateStore;
class DeviceAuthenticator;
class DeviceStore;
class PairingWindow;
class SpakeExchange;
class StationIdentity;
class ObjectRegistry;
class ConnectedDevicesFacade;
class DeviceSessionRegistry;
class RadioModel;
class SessionCommandDispatcher;
class SessionTransport;
class SettingsProxyServer;
class StateMirror;
class StationCatalog;
class StationDevicesFacade;
class TokenStore;

class StationServer : public QObject {
    Q_OBJECT

public:
    /// See the class comment's heartbeat section for both numbers and the
    /// reasoning behind each.
    static constexpr int kDefaultHeartbeatIntervalMs = 20000;
    static constexpr int kDefaultMaxMissedPongs = 2;

    /// How often the outbound delta coalescer is drained. StateMirror's
    /// coalescer is latest-wins and re-resolves against the live model at
    /// flush time, so this is a rate limit rather than a delay budget: one
    /// band-button press runs 75 setters on each of up to five slices
    /// (design addendum section 7), and without a flush cadence that is
    /// roughly 250 messages for one keypress.
    static constexpr int kDefaultDeltaFlushMs = 50;

    /// How long a connection has to complete the section 7.0 handshake
    /// before it is dropped. Without it, a peer that opens a socket and
    /// answers pings but never authenticates lives forever, holding a slot
    /// and a file descriptor, which is a cheap way to sit on a station.
    /// Generous on purpose: the only work between accept and authenticate
    /// is two small messages, so 30 s is far more than a real client needs
    /// even on a bad link, and short enough that a stuck peer clears
    /// without operator action.
    ///
    /// R-R3-16/17: the value is kStationHandshakeDeadlineMs, shared with
    /// StationClient's own deadline, and it now runs until the peer's
    /// snapshot has been sent rather than until it authenticates.
    static constexpr int kDefaultAuthDeadlineMs = kStationHandshakeDeadlineMs;

    /// iPhone app Task 71 (ruling 4.5): a cap on sockets of every kind,
    /// signed in or not, pairing or connecting. Devices hold at most
    /// kMaxDeviceSessions places; while a device reconnects it can hold its
    /// old socket (not yet noticed dead) and four racing attempts (direct
    /// over IPv6 and IPv4, the rendezvous path and the relay), so five
    /// sockets for each of four devices is 20, and a fifth device's four
    /// attempts make 24. At kMaxIncomingMessageBytes that is 24 MiB of
    /// exposure before sign-in, which a Pi 4 does not notice. The next
    /// socket is refused before any hello, retryable.
    static constexpr int kMaxConcurrentPeers = 24;
    /// iPhone app Task 71 (D44, ruling 4.4): devices that hold a place at
    /// once (DeviceSessionRegistry::kMaxDeviceSessions): admitted sessions,
    /// devices away in their grace period and a hosting desktop's own
    /// window. Not connections still connecting, pairing connections, or
    /// the station device of a Core with no desktop.
    static constexpr int kMaxDeviceSessions = 4;
    /// Part C fix wave (R1-M4): connections from one address that are
    /// still connecting (their snapshot not yet sent), so one host cannot
    /// hold every kMaxConcurrentPeers slot by redialling within the
    /// handshake deadline. IPv6 addresses are counted per /64 (see
    /// addressKey()). A connection with no address of its own (the
    /// relay, later) is not counted here.
    static constexpr int kMaxHandshakesPerAddress = 2;

    /// Largest inbound WebSocket message, and frame, on an ACCEPTED
    /// socket. Applied by WebSocketTransport's constructor.
    ///
    /// This bound is PRE-AUTHENTICATION and that is the whole reason it
    /// exists. Qt's default, measured on Qt 6.11 by reading it back off an
    /// accepted socket, is 2147483646 bytes: just under 2 GiB, per message,
    /// per socket. Qt buffers a complete message before it emits
    /// textMessageReceived, so kDefaultAuthDeadlineMs bounds how LONG an
    /// unauthenticated peer may sit here but bounds no BYTES at all.
    /// Uncapped, kMaxConcurrentPeers of them come to roughly 16 GiB on a
    /// daemon whose stated hardware floor is a Pi 4.
    ///
    /// Sized against the largest message a legitimate client can send,
    /// which is not a guess:
    ///
    ///   - Hello, AuthRequest, SettingsRemove: hundreds of bytes. The
    ///     token is 43 base64url characters (TokenStore.h).
    ///   - CommandInvoke: at most two named scalar arguments across the
    ///     four known verbs (SessionMessage::commandVerb).
    ///   - PropertyWrite: one object's coalesced dirty set, bounded by
    ///     that class's whole property table. SliceModel is the largest
    ///     mirrored class at over a hundred Q_PROPERTY declarations, and
    ///     its only QString-valued mirrored properties are antenna names,
    ///     panKey and lastRadeRxCallsign. At a generous 128 bytes per JSON
    ///     entry that is under 16 KiB.
    ///   - SettingsWrite: one key plus one value. The longest values this
    ///     tree stores under a Station-classified key are persisted JSON
    ///     blobs, and the largest of those is bounded by construction:
    ///     FaultLog is a 10-entry ring of six short fields
    ///     (FaultLog.cpp:19, kMaxEvents = 10).
    ///
    /// So roughly 16 KiB is the real ceiling, and 1 MiB is about 64 times
    /// that. It leaves 8 MiB of total pre-auth exposure across every peer
    /// slot, which is a number a Pi 4 does not notice. TciServer.cpp:1470
    /// makes the same call at the same magnitude for a socket that is
    /// loopback-only.
    static constexpr quint64 kMaxIncomingMessageBytes = 1024ULL * 1024ULL;

    /// `radioModel` and `settings` are NOT owned and must outlive this
    /// object; both must live on this object's thread (see the class
    /// comment's threading section, which is where the consequences of
    /// getting that wrong are spelled out).
    ///
    /// `securityDirectory` is where the TLS certificate and the auth token
    /// live. Empty means the daemon profile's own config directory, which
    /// is the production answer; tests pass a scratch directory so a run
    /// never reads back or overwrites a real station's identity.
    ///
    /// `supportedMajors` (iPhone app Task 4) is the link majors this
    /// station advertises and accepts, oldest first. The default is the
    /// build's own (kSupportedSessionMajors); tests inject theirs, and a
    /// debug build of nereusd takes --test-link-majors.
    explicit StationServer(RadioModel* radioModel, AppSettings& settings,
                           const QString& securityDirectory = QString(),
                           QObject* parent = nullptr,
                           const QList<quint16>& supportedMajors =
                               LinkVersion::supportedMajors());
    ~StationServer() override;

    StationServer(const StationServer&) = delete;
    StationServer& operator=(const StationServer&) = delete;

    /// Binds a wss listener. False (with lastError() set) when TLS is
    /// unavailable, the certificate could not be provisioned, or the bind
    /// failed. Port 0 asks the OS for a free one; read it back with
    /// serverPort().
    bool listen(const QHostAddress& address, quint16 port);

    void close();
    bool isListening() const;
    quint16 serverPort() const;
    QHostAddress serverAddress() const;
    QString lastError() const { return m_lastError; }

    /// The listener's TLS configuration as it is in force, read back from
    /// the listener (default-constructed before the first listen()). Its
    /// protocol() is QSsl::TlsV1_2OrLater: the minimum is set explicitly
    /// rather than left to Qt's default.
    QSslConfiguration tlsConfiguration() const;

    /// iPhone app Task 4 (R-IOS-01): the link majors this station accepts,
    /// oldest first.
    QList<quint16> supportedMajors() const { return m_supportedMajors; }

    /// The major `peer`'s session runs at: the one its hello chose from
    /// this station's list. 0 before that hello was accepted, or for a
    /// transport that is not a peer.
    quint16 peerAgreedMajor(SessionTransport* peer) const;

    /// True when `peer`'s hello declared `feature` at `minVersion` or
    /// later. What the station must know before capabilities are sent
    /// (device authentication, pairing, the takeover question, Setup
    /// descriptions) is asked here. An older app declares nothing.
    bool peerDeclares(SessionTransport* peer, const QByteArray& feature, int minVersion) const;

    /// The pairing token of a Core upgraded from before paired devices,
    /// until it is retired; empty on a new Core (iPhone app Task 12: none
    /// is generated any more). And the TLS fingerprint a client pins.
    QString token() const;
    QString certificateFingerprint() const;

    /// iPhone app Task 12 (R-IOS-08): the Core's paired devices and its
    /// identity key. Never null / always present; the identity may be
    /// invalid (StationIdentity::isValid()) when its file is damaged, and
    /// then listen() refuses.
    DeviceStore* deviceStore() const;
    const StationIdentity& stationIdentity() const;
    /// iPhone app Task 13 (R-IOS-08): the mirrored `devices` object and the
    /// device administration verbs behind it.
    StationDevicesFacade* devicesFacade() const;
    /// 1 when the Core sends `devices` and takes its verbs (its identity
    /// key is usable), else 0.
    int deviceAdminVersion() const;
    /// iPhone app Task 19 (R-IOS-06): the mirrored `catalog` object, the
    /// values the Core owns and an app draws its controls from. Never null.
    StationCatalog* catalog() const;
    /// 1: the Core sends `catalog` to a peer at minor 11.
    int stationCatalogVersion() const;
    /// iPhone app Task 20 (R-IOS-27): 1 while media is enabled; a peer at
    /// minor 11 may then ask a spectrum subscription for display extras.
    int displayExtrasVersion() const;

    /// The first-run block, exactly as the operator is shown it: the TLS
    /// pin and the identity key's path with the prompt to back it up.
    ///
    /// Pure and public for two reasons. It keeps the one place the block
    /// is FORMATTED separate from the one place it is WRITTEN, so the
    /// write side can be a single stdout call with no formatting logic in
    /// it; and it lets a test assert on the exact text without capturing a
    /// stream. See writePairingBanner() in the .cpp for why the banner
    /// does not go through qCInfo() like every other line in this class.
    static QString formatFirstRunBanner(const QString& fingerprint,
                                        const QString& identityKeyPath);

    /// Adopt an already-connected transport as a new peer. This is what
    /// the QWebSocketServer's newConnection handler calls, and it is also
    /// how a test drives the real handshake over an in-process pipe: ONE
    /// code path, no test-only branch (SessionTransport.h explains why the
    /// seam exists at all). Takes ownership by reparenting.
    void acceptTransport(SessionTransport* transport);

    /// Parent design section 4.5's EFFECTIVE slice limit: what this daemon
    /// can sustain, which on the Pi 4 floor may be fewer than the radio
    /// supports. Defaults to the board's own maxSlices, i.e. no narrowing,
    /// because R2 builds no PerfMonitor to compute anything better. Values
    /// below 1 are ignored.
    void setSustainableSliceLimit(int slices);
    int sustainableSliceLimit() const { return m_sustainableSliceLimit; }

    /// See the class comment's heartbeat section. Applied to the running
    /// timer immediately. An interval of 0 or less STOPS the heartbeat
    /// entirely, which exists for a bench session an operator is
    /// deliberately holding open through a laptop suspend; it is not a
    /// supported production configuration and is logged as a warning.
    void setHeartbeatIntervalMs(int ms);
    int heartbeatIntervalMs() const { return m_heartbeatIntervalMs; }

    void setMaxMissedPongs(int misses);
    int maxMissedPongs() const { return m_maxMissedPongs; }

    /// Consecutive failed authentications tolerated before the station
    /// stops answering, and for how long. Forwards to TokenStore; see its
    /// header for the semantics and for why RateLimited is a distinct
    /// outcome from Rejected. Defaults are TokenStore's own.
    void setAuthRateLimit(int maxFailures, int lockoutMs);

    /// iPhone app Task 12: nereusd.conf's `pairing_lan_click = allow|deny`
    /// (default allow). Whether a device on this Core's own network may
    /// pair with one tap while the Core is unclaimed; the pairing window
    /// (Task 14) reads it. Deny forces the code for every pairing.
    void setPairingLanClickAllowed(bool allowed) { m_pairingLanClickAllowed = allowed; }
    bool pairingLanClickAllowed() const { return m_pairingLanClickAllowed; }

    /// iPhone app Task 14 (R-IOS-08): the Core's pairing window. Never null.
    /// Its state is the Core's, not any connection's: open with no timer
    /// while the Core is unclaimed, reopened from the console (reopen())
    /// or a paired device (`pairing.open`).
    PairingWindow* pairingWindow() const;
    /// 1 when the Core pairs devices (its identity key is usable): the
    /// hello declares features.pairing 1 and capabilities carry
    /// pairingVersion 1. 0 otherwise.
    int pairingVersion() const;

    /// The pairing code is never printed or logged (Part C fix wave: on a
    /// packaged Core standard output lands in the journal). The console's
    /// `nereusd pairing show` gives it on request, over the owner-only
    /// control socket, and so does the status page while unclaimed.
    /// Writes `text` to standard output and flushes it: the Core's console,
    /// as the first-run banner is written.
    static void printToConsole(const QString& text);
    /// Part C fix wave (R1-M4): `address` as the per-address handshake
    /// count keys it: IPv4, and an IPv4-mapped IPv6 address, as the full
    /// IPv4 address; any other IPv6 address as its /64 prefix
    /// ("2001:db8:1:2::/64"), no scope id. "" for "".
    static QString addressKey(const QString& address);
    /// Whether `address` (a connection's peer address) is on one of this
    /// machine's directly connected networks: a loopback address, or one
    /// inside the subnet of an address of a running interface. Empty (a
    /// relayed connection) is not. One tap pairs only from such an address.
    static bool isOnDirectNetwork(const QString& address);

#ifdef NEREUS_BUILD_TESTS
    /// Replaces the code's hash (SpakeExchange::storedData) so a test can
    /// hold it on the worker and watch the event loop keep serving. Null
    /// restores the real one.
    void setPairingHasherForTest(std::function<QByteArray(const QString&)> hasher);
    bool isHashingPairingCodeForTest() const;
#endif

    /// See kDefaultAuthDeadlineMs. Values below 1 disable the deadline,
    /// which is logged as a warning rather than silently accepted.
    void setAuthDeadlineMs(int ms);
    int authDeadlineMs() const { return m_authDeadlineMs; }

    /// Every peer currently attached, authenticated or not.
    int peerCount() const { return static_cast<int>(m_peers.size()); }

    /// At least one device's session is admitted and live (up to
    /// kMaxDeviceSessions may be; see the topology note above).
    bool hasAuthenticatedSession() const;
    /// Live admitted sessions, 0 to kMaxDeviceSessions.
    int authenticatedSessionCount() const;

    /// Configure before accepting sessions. Old peers remain control-only.
    void setMediaEnabled(bool enabled);
    bool mediaAvailable() const;
    bool remoteWidebandAvailable() const;
    /// The session agreed minor 8 or later: audio contexts carry the encoder
    /// profile or the off reason. Minor-7 peers keep the eight-key context.
    bool remoteAudioStatusAvailable() const;
    /// The session agreed minor 9 or later: spectrum contexts report the
    /// grant Core made. Minor-8 peers keep the 19-key (20 with wideband) context.
    bool spectrumGrantAvailable() const;
    /// The session agreed minor 11 and the Core advertised
    /// displayExtrasVersion 1: a subscription may carry the display extras
    /// fields (iPhone app Task 20, display extras v1).
    bool displayExtrasAvailable() const;
    /// Installs newer limits (a later generation) and why they are below the
    /// Core's ceiling (R-R3-08, R-R3-37). A new reason needs a new
    /// generation; the same limits with the same reason are accepted as-is.
    bool setDisplayBudgetLimits(const DisplayBudgetLimits& limits,
                                DisplayBudgetReason reason = DisplayBudgetReason::None);
    /// The budget in force for the current session: the limits last set,
    /// except that with setDisplayBudgetForReasonPeersOnly(true) a peer
    /// below kDisplayBudgetReasonSessionProtocolMinor (or no peer) has none
    /// and keeps legacy mode.
    std::optional<DisplayBudgetLimits> displayBudgetLimits() const;
    /// The limits last set, whichever peer is attached.
    std::optional<DisplayBudgetLimits> configuredDisplayBudgetLimits() const
    {
        return m_displayBudget;
    }
    /// True for a budget nereusd computed rather than read from its
    /// configuration (display_adaptive on, no limits configured): only an
    /// app that understands the budget reason is put in budget mode.
    void setDisplayBudgetForReasonPeersOnly(bool reasonPeersOnly);
    DisplayBudgetReason displayBudgetReason() const { return m_displayBudgetReason; }
    void setDisplayBudgetEnforcementEnabled(bool enabled);
    bool displayBudgetAvailable() const;
    void publishDisplayBudgetCapabilities();
    using Ps3DisplayAdmissionHandler = std::function<bool(bool, QString*)>;
    void setPs3DisplayAdmissionHandler(Ps3DisplayAdmissionHandler handler);
    quint64 mediaSessionEpoch() const { return m_mediaSessionEpoch; }
    /// expectedEpoch is captured by the producer when its session starts;
    /// late work must never target a replacement session.
    bool sendMediaControl(const QJsonObject& payload, quint64 expectedEpoch);

    /// Observations are independently available even in a control-only session.
    /// Configure before accepting a client; never change negotiated support live.
    void setTelemetryEnabled(bool enabled);
    bool telemetryAvailable() const;
    quint64 sessionEpoch() const { return m_mediaSessionEpoch; }
    bool sendTelemetry(const StationTelemetrySnapshot& snapshot, quint64 expectedEpoch);

    /// The capability descriptor this daemon would advertise right now.
    /// Public so a caller (and this task's tests) can inspect what a
    /// client is about to be told without standing up a client.
    StationCapabilities buildCapabilities() const;
    /// R-R3-46: what this Core offers a window of its radio's hardware:
    /// 0 nothing, 1 the `stepAtt` object, 2 also `alexAntennas`, the
    /// hardware apply step and the I/O board probe.
    int radioHardwareVersion() const;
    // R-R3-47 / R-R3-22: 1 when this Core owns its accessories and mirrors
    // the `amplifier` and `rfkit` objects (remotePgxlControlVersion and
    // remoteRfKitControlVersion); 0 otherwise.
    int accessoryStatusVersion() const;
    // R-R3-47: remotePgxlControlVersion. 3 on a Core that owns its
    // accessories (the `amplifier` object, the configurePgxl,
    // disconnectPgxl and setPgxlConnectionSettings verbs, and the amp's own
    // settings on `accessorySettings` with their verbs); 0 otherwise.
    int pgxlControlVersion() const;
    // R-R3-47 / R-R3-22: remoteTgxlControlVersion. 2 on a Core that owns its
    // accessories (the tuner's own settings on `accessorySettings` and the
    // setTgxlName, setTgxlNetwork, saveTgxlSettings and readTgxlSettings
    // verbs; from 2, R-R3-49, setTgxlAntenna, setTgxlOperate and
    // setTgxlBypass); 0 otherwise.
    int tgxlControlVersion() const;
    // R-R3-47: remoteRfKitControlVersion. 3 on a Core that owns its
    // accessories (the `rfkit` object with its interface, antenna, tuner
    // and band-follow rows, the configureRfKit, disconnectRfKit and
    // setRfKitEnabled verbs, and from 3 the resetRfKitError verb and a
    // window's auto-reconnect and poll interval applied at once); 0
    // otherwise.
    int rfKitControlVersion() const;
    // R-R3-48: stationTciVersion. 1 on a Core that runs its own station
    // TCI server (the `stationTci` object and the setStationTci verb).
    int stationTciVersion() const;
    // R-R3-47 / R-R3-22: accessoryDataVersion. 1 on a Core that owns its
    // accessories (the `accessoryData` object and the setTxInterlockPolicy,
    // setPgxlPowerCap and clearAccessoryFaults verbs); 0 otherwise.
    int accessoryDataVersion() const;

    /// iPhone app Task 71 (R-IOS-02): who holds a place on the Core, and
    /// the mirrored `connectedDevices` object. Never null. Task 48
    /// registers a hosting desktop's own window on the registry; the LAN
    /// announcement and the Bonjour record count its placesTaken().
    DeviceSessionRegistry* deviceSessions() const { return m_deviceSessions.get(); }
    ConnectedDevicesFacade* connectedDevices() const { return m_connectedDevices.get(); }
    /// 1: the Core admits up to four devices and sends `connectedDevices`
    /// to a peer that declared the hello feature `sessionHolder` 1 with
    /// deviceAuth 1, at minor 11 (the design's ruling 10.1), and takes
    /// session.leave.
    int sessionHolderVersion() const { return 1; }
    /// Places taken as the LAN announcement and the Bonjour record count
    /// them: DeviceSessionRegistry::placesTaken(), or 0 on a Core no device
    /// has claimed (ruling 10.4).
    int devicesConnectedForDiscovery() const;

    // ---- Subsystem accessors, non-owning, for tests and diagnostics ----
    StateMirror* stateMirror() const { return m_mirror; }
    ObjectRegistry* objectRegistry() const { return m_registry; }
    SettingsProxyServer* settingsServer() const { return m_settingsServer; }

signals:
    void listeningChanged(bool listening);
    void displayBudgetChanged();
    void telemetrySessionStarted(quint64 epoch);
    void telemetrySessionEnded(quint64 epoch);
    void mediaSessionStarted(quint64 epoch);
    void mediaSessionEnded(quint64 epoch);
    void mediaControlReceived(const QJsonObject& payload, quint64 epoch);
    /// A peer completed the full section 7.0 sequence and is now one of the
    /// admitted sessions.
    void clientAuthenticated(const QString& peer);

    /// A peer went away, for any reason, with the reason. Fires for
    /// unauthenticated peers too.
    void peerDisconnected(const QString& peer, const QString& reason);

    // iPhone app Task 71: sessionPreempted is gone with preemption. No
    // sign-in ever ends another device's session; a device's own newer
    // connection ends its older one with sameDevice (ruling 4.8).

    /// The heartbeat declared a peer dead: it stopped answering pings
    /// without closing. Distinct from peerDisconnected's ordinary path
    /// because this is the case that has no TCP close behind it at all.
    void peerHeartbeatTimeout(const QString& peer);

private:
    /// Per-connection state. Deliberately small: everything that is not
    /// per-CONNECTION (the mirror, the registry, the dispatcher, the
    /// settings server) is shared among the admitted sessions until Tasks
    /// 72 and 76 give each device its own view and media (see the topology
    /// note). Who holds a place is DeviceSessionRegistry's.
    /// iPhone app Task 14: one connection's pairing (StationServer.cpp).
    struct PairingAttempt;

    struct Peer {
        SessionTransport* transport = nullptr;
        QString description;
        bool helloReceived = false;
        bool authenticated = false;
        quint16 agreedMinor = 0;
        /// iPhone app Task 4: the major the peer's hello chose (0 until
        /// accepted) and what that hello declared.
        quint16 agreedMajor = 0;
        QHash<QByteArray, int> features;
        bool snapshotComplete = false;
        /// iPhone app Task 12: this connection's device sign-in challenge
        /// (32 bytes, sent in the hello) and, once authenticated, the
        /// paired device it signed in as (empty for a token sign-in that
        /// enrolled nothing).
        QByteArray challenge;
        QByteArray deviceId;
        /// iPhone app Task 13: signed in with the pairing token (whether or
        /// not it also enrolled its device key). Retiring the token ends it.
        bool signedInWithToken = false;
        /// iPhone app Task 14: this connection's pairing, from pair.start
        /// to its end. Shared, not owned alone, only because Peer is copied.
        std::shared_ptr<PairingAttempt> pairing;
        /// iPhone app Task 71: the device this connection's session is for
        /// (DeviceSessionRegistry's id: a paired device's raw id, or
        /// "token:<n>"), set once it is admitted; empty before, and for a
        /// connection turned away from a full Core.
        QByteArray sessionDeviceId;
        /// How this admitted session's end reaches the registry: a
        /// connection replaced by its own device's newer one (sameDevice),
        /// or one whose device left on purpose or was revoked, frees or
        /// keeps its place by itself and must not be marked away.
        bool placeSettled = false;
        bool leaving = false;

        /// Pings sent since the last pong. Reset to 0 by every pong; the
        /// heartbeat tick declares death when it reaches maxMissedPongs().
        int pingsAwaitingPong = 0;

        /// Owned single-shot finish-the-handshake-or-drop timer, parented
        /// to the transport so it dies with it. Stopped once the peer's
        /// snapshot has been sent (R-R3-16/17). An OWNED timer rather than static
        /// QTimer::singleShot deliberately: the plan's own section 13 note
        /// records that PgxlConnection and TgxlConnection get that wrong,
        /// and cancellability matters more here because this subsystem
        /// gates a transmitter.
        QTimer* authDeadline = nullptr;
    };

    void onNewWebSocketConnection();
    void onTransportText(SessionTransport* transport, const QByteArray& wire);
    void onTransportClosed(SessionTransport* transport);
    void onHeartbeatTick();

    // Every handler takes the TRANSPORT and looks its Peer up itself,
    // never a Peer& held across a call. dropPeer() erases from m_peers,
    // and Qt6's QHash does not promise a reference into it survives an
    // unrelated erase -- promoteToSession() drops the INCUMBENT session
    // while holding the newcomer's entry, which is exactly the shape that
    // would go wrong.
    void handleHello(SessionTransport* transport, const SessionMessage& message);
    void handleAuthRequest(SessionTransport* transport, const SessionMessage& message);
    void handlePropertyWrite(SessionTransport* transport, const SessionMessage& message);
    void handleSettingsWrite(SessionTransport* transport, const SessionMessage& message);
    void handleSettingsRemove(SessionTransport* transport, const SessionMessage& message);
    // iPhone app Task 14 (R-IOS-08): pairing, before any sign-in.
    void handlePairStart(SessionTransport* transport, const SessionMessage& message);
    void handlePairSpake(SessionTransport* transport, const SessionMessage& message);
    void handlePairConfirm(SessionTransport* transport, const SessionMessage& message);
    void handlePairFailFromDevice(SessionTransport* transport);
    /// Sends pair.fail with `reason` and `retryAfterMs`, then ends the
    /// connection. A code the connection had taken is burned by dropPeer.
    void sendPairFail(SessionTransport* transport, const QString& reason, qint64 retryAfterMs);
    /// Hashes the window's current code (one Argon2id hash per code) on a
    /// worker thread, never on this event loop; finishPairingHash() takes
    /// the result back here, keeps it while the code is current, and sends
    /// step 0 to the connections waiting for it.
    void startPairingHash();
    void finishPairingHash(quint64 serial, const QByteArray& stored);
    /// Sends step 0 from the kept hash.
    void beginCodeExchange(SessionTransport* transport);
    /// Signed in with a paired device's own key (not the old token): the
    /// only connections the pairing code is sent to.
    bool peerSeesPairingCode(SessionTransport* transport) const;
    /// `message` as `transport` may see it: the pairing code blanked on the
    /// `devices` object and in pairing.open's result for any other
    /// connection.
    SessionMessage withPairingCodeFor(SessionTransport* transport,
                                      const SessionMessage& message) const;

    /// iPhone app Task 71: after an accepted sign-in, asks the registry
    /// who is let in (ruling 4.4) and ends the device's own older
    /// connection (sameDevice), admits, or turns a full Core's newcomer
    /// away.
    void admit(SessionTransport* transport, const QString& name, const QString& shortName,
               const QString& kind);

    /// Completes the session: capability exchange, settings snapshot,
    /// mirror attach, snapshot-complete marker. Never ends another session.
    void promoteToSession(SessionTransport* transport);

    /// Sends the identity-dependent portion of the session state. Returns
    /// false if `transport` is no longer an admitted session after either
    /// send. The initial handshake uses this before the mirror attach; a
    /// later radio identity event uses it on each admitted session without
    /// replaying the mirror snapshot or restarting media.
    bool sendCapabilitiesAndSettingsSnapshot(SessionTransport* transport);

    /// `retryable` rides out on the SessionEnd and tells the client's
    /// reconnect policy whether the condition that produced this drop
    /// clears on its own. Required, not defaulted, so a new refusal cannot
    /// be added without someone deciding which kind it is. See
    /// SessionMessage::retryable; the classification for each call site is
    /// argued at the site.
    /// `endCode` (iPhone app Task 12) is the SessionEndCode the
    /// session.end carries; empty for an end that has none.
    void dropPeer(SessionTransport* transport, const QString& reason,
                  bool sendSessionEnd, bool retryable,
                  const QString& endCode = QString());
    void send(SessionTransport* transport, const SessionMessage& message);
    /// iPhone app Task 71: `message` to every admitted session (each fitted
    /// to what that peer negotiated), or during an attach's burst the
    /// burst's own messages to the attaching session alone.
    void sendToSession(const SessionMessage& message);
    /// One mirror or control message to one admitted peer, fitted to it.
    void sendToPeer(SessionTransport* transport, const SessionMessage& message);
    /// The capability descriptor `transport` is told.
    StationCapabilities buildCapabilitiesFor(SessionTransport* transport) const;
    /// sessionHolder 1 in `transport`'s hello, with deviceAuth 1 (ruling
    /// 10.1: the one without the other is not declared).
    bool peerHoldsSessions(SessionTransport* transport) const;
    /// iPhone app Task 71: sessionHolderVersion 1 reached `transport`
    /// (minor 11 and the feature declared).
    bool peerHasSessionHolderVersion(SessionTransport* transport) const;
    /// A command, property write or settings write from `transport`'s
    /// device (never a heartbeat).
    void noteActivity(SessionTransport* transport);
    /// Re-arms the grace timer for the next away device's end.
    void scheduleGraceCheck();

    /// iPhone app Task 13 (R-IOS-08): ends every authenticated connection
    /// `matches` picks with session.end, not retryable, `reason` and
    /// `endCode`. The connection whose command is being dispatched right
    /// now is ended just after its result has gone out.
    void endAuthenticatedPeers(const std::function<bool(const Peer&)>& matches,
                               const QString& reason, const char* endCode);
    /// Tells the devices object which paired devices are connected.
    void publishConnectedDevices();

    /// Watches the five singleton mirrored models plus every slice
    /// RadioModel already holds. Idempotent.
    void buildMirror();

    QPointer<RadioModel> m_radioModel;
    AppSettings& m_settings;
    QString m_securityDirectory;
    QString m_lastError;

    /// iPhone app Task 4: what this station's hello advertises. Task 12
    /// declares deviceAuth 1 (when the identity key is usable); the tasks
    /// that add pairing, the takeover question and Setup descriptions add
    /// theirs here.
    QList<quint16> m_supportedMajors;
    QHash<QByteArray, int> m_declaredFeatures;

    std::unique_ptr<CertificateStore> m_certificates;
    std::unique_ptr<TokenStore> m_tokens;
    // iPhone app Task 12 (R-IOS-08): the Core's identity key, its paired
    // devices, device sign-in, the certificate's SHA-256 (what a device
    // signs) and the identity's signature over it (sent in every hello).
    std::unique_ptr<StationIdentity> m_identity;
    std::unique_ptr<DeviceStore> m_devices;
    std::unique_ptr<DeviceAuthenticator> m_deviceAuth;
    // iPhone app Task 13: after the three it reads, so it goes first.
    std::unique_ptr<StationDevicesFacade> m_devicesFacade;
    // iPhone app Task 19: the Core's catalogue.
    std::unique_ptr<StationCatalog> m_catalog;
    // iPhone app Task 71: who holds a place on the Core.
    std::unique_ptr<DeviceSessionRegistry> m_deviceSessions;
    // The connection whose command.invoke is being dispatched, and the end
    // it is owed once its result has been sent (a self-revoke, or a token
    // session retiring the token).
    SessionTransport* m_dispatchingTransport = nullptr;
    std::optional<std::pair<QString, QString>> m_pendingEnd;
    QByteArray m_certSha256;
    QByteArray m_certBinding;
    bool m_pairingLanClickAllowed = true;
    // iPhone app Task 14: the pairing window, the stored data for its
    // current code (wiped when the code changes), and the console.
    std::unique_ptr<PairingWindow> m_pairingWindow;
    QByteArray m_pairingStored;
    quint64 m_pairingStoredSerial = 0;
    // The hash worker (one at a time), the code serial it hashes, and the
    // hash itself (SpakeExchange::storedData; a test may hold it).
    std::unique_ptr<QThread> m_pairingHashThread;
    quint64 m_pairingHashSerial = 0;
    std::function<QByteArray(const QString&)> m_pairingHasher;

    QWebSocketServer* m_wsServer = nullptr;

    StateMirror* m_mirror = nullptr;
    ObjectRegistry* m_registry = nullptr;
    SessionCommandDispatcher* m_dispatcher = nullptr;
    SettingsProxyServer* m_settingsServer = nullptr;
    bool m_mirrorBuilt = false;

    QHash<SessionTransport*, Peer> m_peers;
    /// iPhone app Task 71: the one admitted session media and telemetry go
    /// to until Task 76 (see the topology note); null when none holds them.
    SessionTransport* m_mediaSession = nullptr;
    /// During promoteToSession()'s attach: the session its burst is for.
    SessionTransport* m_burstTarget = nullptr;
    /// Command results owed to a session other than the one being
    /// dispatched now (a result that arrives on a later turn), by verb and
    /// id.
    QHash<QPair<QByteArray, quint32>, QPointer<SessionTransport>> m_resultRoutes;
    bool m_resultSentInDispatch = false;
    std::unique_ptr<ConnectedDevicesFacade> m_connectedDevices;
    QTimer* m_graceTimer = nullptr;
    bool m_mediaEnabled = false;
    bool m_displayBudgetEnforcementEnabled = false;
    std::optional<DisplayBudgetLimits> m_displayBudget;
    bool m_displayBudgetForReasonPeersOnly = false;
    DisplayBudgetReason m_displayBudgetReason = DisplayBudgetReason::None;
    bool m_telemetryEnabled = false;
    quint64 m_mediaSessionEpoch = 0;

    QTimer* m_heartbeatTimer = nullptr;
    QTimer* m_deltaFlushTimer = nullptr;

    int m_authDeadlineMs = kDefaultAuthDeadlineMs;
    int m_heartbeatIntervalMs = kDefaultHeartbeatIntervalMs;
    int m_maxMissedPongs = kDefaultMaxMissedPongs;
    int m_sustainableSliceLimit = 0;
};

} // namespace NereusSDR
