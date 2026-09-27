// no-port-check: NereusSDR-original.
// =================================================================
// tests/tst_rendezvous_client.cpp  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 27 (R-IOS-08, R-IOS-16): reaching the Core through
// the remote access service (the rendezvous,
// docs/architecture/2026-09-23-rendezvous-v1.md).
//
// The conformance suite, rendezvous/conformance/v1/ (section 10):
//   - every crypto vector (strict base64url, the P-256 key check, the
//     rendezvous id, the registration and introduction signatures, the
//     TURN credentials);
//   - every control fixture the Core and the desktop receive or send, each
//     decoded (or refused) and encoded again;
//   - the Core's runner (section 10.4): this test plays the service towards
//     a RendezvousClient in the station role for every fixture whose runs
//     include "core", and the desktop's runner does the same towards one in
//     the client role for every fixture that includes "app".
//
// Then the Core against the real Python service (rendezvous/server), run
// on this computer with a small STUN and TURN fake beside it
// (tests/tools/fake_turn_server.py); nothing leaves this computer:
//   - a Core registers, a paired device's introduction arrives, and an ICE
//     connection completes with the service's STUN server in use;
//   - with direct paths blocked in the test (only relay candidates pass),
//     it completes through TURN over UDP;
//   - an introduction from a device the Core never paired, and from one it
//     revoked, is dropped without a reply and counted;
//   - pairing by code works through a mailbox: a recording relay between
//     the ends and the service sees every body forwarded unchanged and
//     never the code, and the service's log shows the mailbox;
//   - with the service stopped, a session that was running continues;
//   - IPv6 is preferred where both ends have it.
//
// And the desktop (Step 2): a saved Core's last good addresses are tried
// first, so with the service stopped a client with a cached address still
// connects; an address that does not answer, that another computer
// answers, or that never opens gives way to the next; each attempt is
// recorded path by path in plain words; the saved Core keeps its last good
// addresses, most recent first.
//
// Keys, codes, nonces and secrets are made at run time; nothing secret is
// printed.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
//   2026-09-26: iPhone app plan Task 28 (R-IOS-16): a session through the
//               service and its relay given back, an answer with no
//               credentials and its retirement, the plain words of a
//               service connection; the Task 27 ICE tests answer
//               introductions themselves. J.J. Boyd (KG4VCF), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageAuthenticationCode>
#include <QNetworkInterface>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QWebSocket>
#include <QWebSocketServer>

#include <functional>
#include <memory>
#include <optional>

#ifdef Q_OS_UNIX
#include <pwd.h>
#include <unistd.h>
#endif

#include "core/AppSettings.h"
#include "core/security/ClientDeviceIdentity.h"
#include "core/security/DeviceStore.h"
#include "core/security/PairingCode.h"
#include "core/security/PairingWindow.h"
#include "core/security/SpakeExchange.h"
#include "core/security/StationIdentity.h"
#include "core/session/DataChannelTransport.h"
#include "core/session/RendezvousDialer.h"
#include "core/session/StationDevicesFacade.h"
#include "core/session/IceConfiguration.h"
#include "core/session/RendezvousClient.h"
#include "core/session/RendezvousMailboxTransport.h"
#include "core/session/RendezvousWire.h"
#include "core/session/StationPairingClient.h"
#include "core/session/StationRendezvous.h"
#include "core/session/StationClient.h"
#include "core/session/StationServer.h"
#include "core/session/media/LibDataChannelMediaTransport.h"
#include "core/settings/SettingsProxy.h"
#include "gui/CoreTargetStore.h"
#include "models/RadioModel.h"

#include "OperatorWording.h"
#include "fakes/UpgradedCoreToken.h"

#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

using namespace NereusSDR;
namespace Wire = NereusSDR::RendezvousWire;

namespace {

const QString kSuite = QStringLiteral(NEREUS_SOURCE_DIR "/rendezvous/conformance/v1");

QJsonObject readJson(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

QString readText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QByteArray randomBytes(int count)
{
    QByteArray bytes(count, Qt::Uninitialized);
    for (int index = 0; index < count; ++index) {
        bytes[index] = static_cast<char>(QRandomGenerator::system()->bounded(256));
    }
    return bytes;
}

QString b64(const QByteArray& bytes)
{
    return StationIdentity::toBase64Url(bytes);
}

QByteArray unb64(const QString& text)
{
    return StationIdentity::fromBase64Url(text);
}

// A P-256 key made at run time in a directory of its own.
struct TestKey {
    QTemporaryDir dir;
    StationIdentity identity = StationIdentity::loadOrCreate(dir.path());

    QByteArray spki() const { return identity.publicKeySpki(); }
    QByteArray sign(const QByteArray& message) const { return identity.sign(message); }
};

std::shared_ptr<TestKey> makeKey()
{
    return std::make_shared<TestKey>();
}

QString turnPassword(const QByteArray& secret, const QString& username)
{
    return QString::fromLatin1(
        QMessageAuthenticationCode::hash(username.toUtf8(), secret, QCryptographicHash::Sha1)
            .toBase64());
}

// The Python the service runs on, with this user's own packages: ctest
// gives every test a home of its own (tests/CMakeLists.txt), and Python
// finds a user's packages under the home directory, so the service is
// started with the account's real one.
QProcessEnvironment pythonEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_UNIX
    if (const passwd* account = getpwuid(getuid()); account != nullptr && account->pw_dir != nullptr) {
        env.insert(QStringLiteral("HOME"), QString::fromLocal8Bit(account->pw_dir));
    }
#endif
    return env;
}

quint16 freeTcpPort()
{
    QTcpServer probe;
    probe.listen(QHostAddress::LocalHost, 0);
    return probe.serverPort();
}

// ── The service played by a runner (section 10.4) ──────────────────────

class ServicePlayer : public QObject {
public:
    ServicePlayer() : m_server(QStringLiteral("rendezvous-runner"), QWebSocketServer::NonSecureMode)
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QWebSocket* socket = m_server.nextPendingConnection();
                socket->setParent(this);
                hostHeaders.append(QString::fromLatin1(socket->request().rawHeader("Host")));
                auto* queue = new QStringList;
                m_inbox.insert(socket, queue);
                QObject::connect(socket, &QWebSocket::textMessageReceived, this,
                                 [queue](const QString& text) { queue->append(text); });
                QObject::connect(socket, &QWebSocket::disconnected, this,
                                 [this, socket] { m_closed.insert(socket); });
                m_pending.append(socket);
            }
        });
    }

    ~ServicePlayer() override
    {
        // The sockets are children, destroyed after the members: silence
        // them first, so a close during teardown touches nothing gone.
        for (auto it = m_inbox.cbegin(); it != m_inbox.cend(); ++it) {
            QObject::disconnect(it.key(), nullptr, this, nullptr);
        }
        qDeleteAll(m_inbox);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(m_server.serverPort()));
    }

    QWebSocket* waitForConnection(int timeoutMs = 5000)
    {
        QDeadlineTimer deadline(timeoutMs);
        while (m_pending.isEmpty() && !deadline.hasExpired()) {
            QTest::qWait(5);
        }
        return m_pending.isEmpty() ? nullptr : m_pending.takeFirst();
    }

    std::optional<QString> waitForMessage(QWebSocket* socket, int timeoutMs = 5000)
    {
        QStringList* queue = m_inbox.value(socket);
        if (queue == nullptr) {
            return std::nullopt;
        }
        QDeadlineTimer deadline(timeoutMs);
        while (queue->isEmpty() && !deadline.hasExpired()) {
            QTest::qWait(5);
        }
        if (queue->isEmpty()) {
            return std::nullopt;
        }
        return queue->takeFirst();
    }

    /// The Host header of each connection's opening request.
    QStringList hostHeaders;

    quint16 port() const { return m_server.serverPort(); }

    bool silentFor(QWebSocket* socket, int ms)
    {
        QTest::qWait(ms);
        QStringList* queue = m_inbox.value(socket);
        return queue == nullptr || queue->isEmpty();
    }

private:
    QWebSocketServer m_server;
    QList<QWebSocket*> m_pending;
    QHash<QWebSocket*, QStringList*> m_inbox;
    QSet<QWebSocket*> m_closed;
};

// ── Placeholders (section 10.4, and link section 16.1) ─────────────────

struct Context {
    bool coreMode = true;
    QHash<QString, QJsonValue> recorded;
    // Station keys by name: runner-made (a private key), or learned from
    // the Core (its public key only).
    QHash<QString, std::shared_ptr<TestKey>> stationKeys;
    QHash<QString, QByteArray> stationSpki;
    QHash<QString, std::shared_ptr<TestKey>> devices;
    QByteArray turnSecret = randomBytes(24).toHex();
    qint64 wallClock = 1800000000;
    qint64 advancedMs = 0;
    qint64 turnTtlSeconds = 86400;
    QStringList stunUrls{QStringLiteral("stun:rv6.conformance.invalid:3478"),
                         QStringLiteral("stun:rv4.conformance.invalid:3478")};
    QStringList turnUrls{QStringLiteral("turn:rv6.conformance.invalid:3478?transport=udp"),
                         QStringLiteral("turn:rv4.conformance.invalid:3478?transport=udp")};
    QString offerSdp = readText(kSuite + QStringLiteral("/sdp/offer.sdp"));
    QString answerSdp = readText(kSuite + QStringLiteral("/sdp/answer.sdp"));

    void setup(const QJsonObject& serverSetup)
    {
        if (serverSetup.contains(QStringLiteral("stunUrls"))) {
            stunUrls.clear();
            for (const QJsonValue& url : serverSetup.value(QStringLiteral("stunUrls")).toArray()) {
                stunUrls.append(url.toString());
            }
        }
        if (serverSetup.contains(QStringLiteral("turnUrls"))) {
            turnUrls.clear();
            for (const QJsonValue& url : serverSetup.value(QStringLiteral("turnUrls")).toArray()) {
                turnUrls.append(url.toString());
            }
        }
        if (serverSetup.contains(QStringLiteral("turnTtlSeconds"))) {
            turnTtlSeconds = serverSetup.value(QStringLiteral("turnTtlSeconds")).toInteger();
        }
        if (serverSetup.contains(QStringLiteral("wallClock"))) {
            wallClock = serverSetup.value(QStringLiteral("wallClock")).toInteger();
        }
    }

    QByteArray spkiOf(const QString& name)
    {
        if (stationSpki.contains(name)) {
            return stationSpki.value(name);
        }
        auto key = makeKey();
        stationKeys.insert(name, key);
        stationSpki.insert(name, key->spki());
        return key->spki();
    }

    std::shared_ptr<TestKey> device(const QString& name)
    {
        if (!devices.contains(name)) {
            devices.insert(name, makeKey());
        }
        return devices.value(name);
    }

    QByteArray recordedBytes(const QString& name) const
    {
        return unb64(recorded.value(name).toString());
    }

    QJsonObject turnObject(const QString& station)
    {
        const qint64 expires = wallClock + advancedMs / 1000 + turnTtlSeconds;
        const QString username = QStringLiteral("%1:%2").arg(expires).arg(
            Wire::rendezvousId(spkiOf(station)));
        return QJsonObject{
            {QStringLiteral("username"), username},
            {QStringLiteral("password"), turnPassword(turnSecret, username)},
            {QStringLiteral("expires"), static_cast<double>(expires)},
            {QStringLiteral("urls"), QJsonArray::fromStringList(turnUrls)},
        };
    }

    QByteArray signatureCase(const std::shared_ptr<TestKey>& key, QByteArray transcript,
                             const QString& which, const QByteArray& otherTranscript)
    {
        if (which == QLatin1String("otherNonce")) {
            transcript = otherTranscript;
        }
        QByteArray signature = key ? key->sign(transcript) : QByteArray();
        if (which == QLatin1String("flippedBit") && !signature.isEmpty()) {
            signature[signature.size() - 1] = static_cast<char>(signature.back() ^ 0x01);
        }
        return signature;
    }

    QJsonValue fill(const QJsonValue& value)
    {
        if (value.isObject()) {
            QJsonObject object = value.toObject();
            for (auto it = object.begin(); it != object.end(); ++it) {
                it.value() = fill(it.value());
            }
            return object;
        }
        if (value.isArray()) {
            QJsonArray array;
            for (const QJsonValue& entry : value.toArray()) {
                array.append(fill(entry));
            }
            return array;
        }
        if (!value.isString() || !value.toString().startsWith(QLatin1Char('$'))) {
            return value;
        }
        const QString text = value.toString();
        const QStringList parts = text.split(QLatin1Char(':'));
        const QString kind = parts.value(0);
        if (kind == QLatin1String("$b64")) {
            const QString filled = b64(randomBytes(parts.value(1).toInt()));
            recorded.insert(parts.value(2), filled);
            return filled;
        }
        if (kind == QLatin1String("$ref")) {
            return recorded.value(parts.value(1));
        }
        if (kind == QLatin1String("$key")) {
            const QByteArray spki = spkiOf(parts.value(1));
            return parts.value(2) == QLatin1String("id") ? QJsonValue(Wire::rendezvousId(spki))
                                                         : QJsonValue(b64(spki));
        }
        if (kind == QLatin1String("$device")) {
            return b64(StationIdentity::fingerprintOf(device(parts.value(1))->spki()));
        }
        if (kind == QLatin1String("$introduce")) {
            const QString id = Wire::rendezvousId(spkiOf(parts.value(2)));
            const QByteArray signature = signatureCase(
                device(parts.value(1)),
                Wire::introduceTranscript(id, recordedBytes(parts.value(3))), parts.value(4),
                Wire::introduceTranscript(id, randomBytes(32)));
            recorded.insert(text, b64(signature));
            return b64(signature);
        }
        if (kind == QLatin1String("$register")) {
            spkiOf(parts.value(1));
            const QByteArray signature = signatureCase(
                stationKeys.value(parts.value(1)),
                Wire::registerTranscript(recordedBytes(parts.value(2))), parts.value(3),
                Wire::registerTranscript(randomBytes(32)));
            recorded.insert(text, b64(signature));
            return b64(signature);
        }
        if (kind == QLatin1String("$sdp")) {
            const QString sdp = parts.value(1) == QLatin1String("offer") ? offerSdp : answerSdp;
            recorded.insert(parts.value(2), sdp);
            return sdp;
        }
        if (kind == QLatin1String("$candidate")) {
            const QString candidate =
                QStringLiteral("candidate:1 1 UDP 2122317823 127.0.0.1 50000 typ host");
            recorded.insert(parts.value(1), candidate);
            return candidate;
        }
        if (kind == QLatin1String("$turn")) {
            const QJsonObject turn = turnObject(parts.value(1));
            recorded.insert(parts.value(2), turn);
            return turn;
        }
        return value;
    }

    bool match(const QJsonValue& expected, const QJsonValue& actual, QString* why)
    {
        const auto fail = [why](const QString& text) {
            *why = text;
            return false;
        };
        if (expected.isObject()) {
            if (!actual.isObject()) {
                return fail(QStringLiteral("not an object"));
            }
            const QJsonObject want = expected.toObject();
            const QJsonObject got = actual.toObject();
            QStringList wantKeys = want.keys();
            QStringList gotKeys = got.keys();
            wantKeys.sort();
            gotKeys.sort();
            if (wantKeys != gotKeys) {
                return fail(QStringLiteral("keys %1, expected %2")
                                .arg(gotKeys.join(QLatin1Char(',')),
                                     wantKeys.join(QLatin1Char(','))));
            }
            // A public key before the id derived from it.
            std::stable_sort(wantKeys.begin(), wantKeys.end(), [&want](const QString& a,
                                                                      const QString& b) {
                const bool aKey = want.value(a).toString().endsWith(QLatin1String(":publicKey"));
                const bool bKey = want.value(b).toString().endsWith(QLatin1String(":publicKey"));
                return aKey && !bKey;
            });
            for (const QString& key : std::as_const(wantKeys)) {
                QString inner;
                if (!match(want.value(key), got.value(key), &inner)) {
                    return fail(key + QStringLiteral(": ") + inner);
                }
            }
            return true;
        }
        if (expected.isArray()) {
            const QJsonArray want = expected.toArray();
            const QJsonArray got = actual.toArray();
            if (!actual.isArray() || want.size() != got.size()) {
                return fail(QStringLiteral("array differs"));
            }
            for (qsizetype index = 0; index < want.size(); ++index) {
                if (!match(want.at(index), got.at(index), why)) {
                    return false;
                }
            }
            return true;
        }
        if (!expected.isString() || !expected.toString().startsWith(QLatin1Char('$'))) {
            return expected == actual ? true : fail(QStringLiteral("value differs"));
        }
        const QString text = expected.toString();
        const QStringList parts = text.split(QLatin1Char(':'));
        const QString kind = parts.value(0);
        if (kind == QLatin1String("$any")) {
            return true;
        }
        if (kind == QLatin1String("$string") || kind == QLatin1String("$capture")) {
            if (kind == QLatin1String("$string") && !actual.isString()) {
                return fail(QStringLiteral("not a string"));
            }
            if (parts.size() > 1) {
                recorded.insert(parts.value(1), actual);
            }
            return true;
        }
        if (kind == QLatin1String("$int")) {
            if (!actual.isDouble() || actual.toDouble() != std::floor(actual.toDouble())) {
                return fail(QStringLiteral("not a whole number"));
            }
            if (parts.size() > 1) {
                recorded.insert(parts.value(1), actual);
            }
            return true;
        }
        if (kind == QLatin1String("$b64")) {
            bool ok = false;
            const QByteArray bytes = StationIdentity::fromBase64Url(actual.toString(), &ok);
            if (!actual.isString() || !ok || bytes.size() != parts.value(1).toInt()) {
                return fail(QStringLiteral("not base64url of the right length"));
            }
            recorded.insert(parts.value(2), actual);
            return true;
        }
        if (kind == QLatin1String("$ref")) {
            return recorded.value(parts.value(1)) == actual ? true
                                                           : fail(QStringLiteral("not the recorded value"));
        }
        if (kind == QLatin1String("$key")) {
            const QString name = parts.value(1);
            if (parts.value(2) == QLatin1String("publicKey")) {
                bool ok = false;
                const QByteArray spki = StationIdentity::fromBase64Url(actual.toString(), &ok);
                if (!ok || !StationIdentity::isP256Spki(spki)) {
                    return fail(QStringLiteral("not a canonical P-256 key"));
                }
                if (coreMode && !stationSpki.contains(name)) {
                    stationSpki.insert(name, spki);
                    return true;
                }
                return spki == spkiOf(name) ? true : fail(QStringLiteral("another key"));
            }
            return actual.toString() == Wire::rendezvousId(spkiOf(name))
                       ? true
                       : fail(QStringLiteral("not the id of the key"));
        }
        if (kind == QLatin1String("$device")) {
            return actual.toString() == b64(StationIdentity::fingerprintOf(device(parts.value(1))->spki()))
                       ? true
                       : fail(QStringLiteral("not the device's id"));
        }
        if (kind == QLatin1String("$register")) {
            const QByteArray signature = unb64(actual.toString());
            return StationIdentity::verify(spkiOf(parts.value(1)),
                                           Wire::registerTranscript(recordedBytes(parts.value(2))),
                                           signature)
                       ? true
                       : fail(QStringLiteral("the registration signature does not verify"));
        }
        if (kind == QLatin1String("$introduce")) {
            const QByteArray signature = unb64(actual.toString());
            const QString id = Wire::rendezvousId(spkiOf(parts.value(2)));
            return StationIdentity::verify(device(parts.value(1))->spki(),
                                           Wire::introduceTranscript(id, recordedBytes(parts.value(3))),
                                           signature)
                       ? true
                       : fail(QStringLiteral("the introduction signature does not verify"));
        }
        if (kind == QLatin1String("$sdp")) {
            if (!actual.isString() || actual.toString().isEmpty()) {
                return fail(QStringLiteral("not a description"));
            }
            recorded.insert(parts.value(2), actual);
            return true;
        }
        if (kind == QLatin1String("$candidate")) {
            if (!actual.isString() || !Wire::isCandidate(actual.toString())) {
                return fail(QStringLiteral("not a candidate"));
            }
            recorded.insert(parts.value(1), actual);
            return true;
        }
        if (kind == QLatin1String("$turn")) {
            const QJsonObject turn = turnObject(parts.value(1));
            recorded.insert(parts.value(2), turn);
            return QJsonValue(turn) == actual ? true : fail(QStringLiteral("other credentials"));
        }
        return fail(QStringLiteral("unknown placeholder ") + kind);
    }
};

QString compact(const QJsonValue& value)
{
    return QString::fromUtf8(QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact));
}

// Section 5.3's listed keys, for the control fixtures' comparison.
QStringList listedKeys(Wire::Direction direction, Wire::Kind kind)
{
    using D = Wire::Direction;
    using K = Wire::Kind;
    const bool fromStation = direction == D::StationToService;
    const bool toStation = direction == D::ServiceToStation;
    switch (kind) {
    case K::Hello: return {"type", "version", "nonce", "stun"};
    case K::Register: return {"type", "id", "publicKey"};
    case K::Challenge: return {"type", "nonce"};
    case K::Prove: return {"type", "signature"};
    case K::Registered: return {"type", "id"};
    case K::Introduce: return {"type", "id", "device", "deviceSignature", "offer"};
    case K::Introduction: return {"type", "from", "device", "deviceSignature", "offer", "nonce"};
    case K::Answer:
        return fromStation ? QStringList{"type", "to", "answer", "turn"}
                           : QStringList{"type", "answer", "turn"};
    case K::Credentials: return {"type", "from", "turn"};
    case K::Candidate:
        if (fromStation) {
            return {"type", "to", "candidate"};
        }
        return toStation ? QStringList{"type", "from", "candidate"}
                         : QStringList{"type", "candidate"};
    case K::IntroductionEnd:
        return toStation ? QStringList{"type", "from", "code"} : QStringList{"type", "code"};
    case K::Nameplate:
    case K::MailboxOpen:
    case K::MailboxOpened:
        return {"type", "nameplate"};
    case K::Mailbox: return {"type", "body"};
    case K::MailboxClosed: return {"type", "code"};
    case K::Error: return {"type", "code", "reason", "retryAfterMs"};
    default: return {"type"};
    }
}

QJsonObject restricted(const QJsonObject& object, const QStringList& keys)
{
    QJsonObject result;
    for (const QString& key : keys) {
        if (!object.contains(key)) {
            continue;
        }
        QJsonValue value = object.value(key);
        if (key == QLatin1String("turn") && value.isObject()) {
            value = restricted(value.toObject(), {"username", "password", "expires", "urls"});
        }
        result.insert(key, value);
    }
    return result;
}

// ── The Python service and the STUN and TURN fake, on this computer ────

class LocalService {
public:
    /// `stun`: the service's hello names the fake's STUN server. `relay`:
    /// the service mints relay credentials for the fake's TURN server.
    explicit LocalService(bool stun = true, bool relay = true) : m_stun(stun), m_relay(relay) {}

    /// The fake relay refuses every allocation with 486 (Allocation Quota
    /// Reached), as a full relay does. Before start().
    void setRelayFull(bool full) { m_relayFull = full; }

    ~LocalService() { stop(); stopTurn(); }

    bool start()
    {
        if ((m_stun || m_relay) && !startTurn()) {
            return false;
        }
        m_port = freeTcpPort();
        QFile secret(m_dir.filePath(QStringLiteral("turn-secret")));
        if (!secret.open(QIODevice::WriteOnly)) {
            return false;
        }
        secret.write(m_secret);
        secret.close();
        QFile config(m_dir.filePath(QStringLiteral("rendezvous.conf")));
        if (!config.open(QIODevice::WriteOnly)) {
            return false;
        }
        const QString stun = m_stun ? QStringLiteral("stun:127.0.0.1:%1").arg(m_turnPort)
                                    : QString();
        const QString turn = m_relay
            ? QStringLiteral("turn:127.0.0.1:%1?transport=udp").arg(m_turnPort)
            : QString();
        config.write(QStringLiteral("[rendezvous]\n"
                                    "listen = 127.0.0.1:%1\n"
                                    "stun_urls = %2\n"
                                    "turn_urls = %3\n"
                                    "turn_secret_file = %4\n"
                                    "log_level = info\n")
                         .arg(m_port)
                         .arg(stun, turn,
                              m_relay ? secret.fileName() : QString())
                         .toUtf8());
        config.close();
        return launch();
    }

    // Starts the service again on the same port, as after a restart.
    bool launch()
    {
        m_process = std::make_unique<QProcess>();
        QProcessEnvironment env = pythonEnvironment();
        env.insert(QStringLiteral("PYTHONPATH"),
                   QStringLiteral(NEREUS_SOURCE_DIR "/rendezvous/server"));
        m_process->setProcessEnvironment(env);
        m_process->setWorkingDirectory(m_dir.path());
        m_process->start(QStringLiteral("python3"),
                         {QStringLiteral("-m"), QStringLiteral("nereus_rendezvous"),
                          QStringLiteral("--config"),
                          m_dir.filePath(QStringLiteral("rendezvous.conf"))});
        if (!m_process->waitForStarted(10000)) {
            return false;
        }
        QDeadlineTimer deadline(15000);
        while (!deadline.hasExpired()) {
            QTcpSocket probe;
            probe.connectToHost(QHostAddress::LocalHost, m_port);
            if (probe.waitForConnected(200)) {
                return true;
            }
            QTest::qWait(50);
        }
        return false;
    }

    void stop()
    {
        if (m_process) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_process->terminate();
            if (!m_process->waitForFinished(5000)) {
                m_process->kill();
                m_process->waitForFinished(2000);
            }
            m_log += QString::fromUtf8(m_process->readAllStandardError());
            m_process.reset();
        }
    }

    QString log()
    {
        if (m_process) {
            m_log += QString::fromUtf8(m_process->readAllStandardError());
        }
        return m_log;
    }

    QString turnOutput()
    {
        if (m_turn) {
            m_turnLog += QString::fromUtf8(m_turn->readAllStandardOutput());
        }
        return m_turnLog;
    }

    QUrl url() const { return QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(m_port)); }
    quint16 port() const { return m_port; }

private:
    bool startTurn()
    {
        QFile secret(m_dir.filePath(QStringLiteral("turn-secret")));
        if (!secret.open(QIODevice::WriteOnly)) {
            return false;
        }
        secret.write(m_secret);
        secret.close();
        const QString portFile = m_dir.filePath(QStringLiteral("turn-port"));
        m_turn = std::make_unique<QProcess>();
        m_turn->setProcessEnvironment(pythonEnvironment());
        QStringList arguments{QStringLiteral(NEREUS_SOURCE_DIR "/tests/tools/fake_turn_server.py"),
                              QStringLiteral("--secret-file"), secret.fileName(),
                              QStringLiteral("--port-file"), portFile};
        if (m_relayFull) {
            arguments.append(QStringLiteral("--quota-full"));
        }
        m_turn->start(QStringLiteral("python3"), arguments);
        if (!m_turn->waitForStarted(10000)) {
            return false;
        }
        QDeadlineTimer deadline(10000);
        while (!QFile::exists(portFile) && !deadline.hasExpired()) {
            QTest::qWait(20);
        }
        m_turnPort = static_cast<quint16>(readText(portFile).toInt());
        return m_turnPort != 0;
    }

    void stopTurn()
    {
        if (m_turn) {
            m_turn->terminate();
            if (!m_turn->waitForFinished(3000)) {
                m_turn->kill();
                m_turn->waitForFinished(2000);
            }
            m_turn.reset();
        }
    }

    bool m_stun = true;
    bool m_relay = true;
    bool m_relayFull = false;
    QTemporaryDir m_dir;
    QByteArray m_secret = randomBytes(24).toHex();
    quint16 m_port = 0;
    quint16 m_turnPort = 0;
    std::unique_ptr<QProcess> m_process;
    std::unique_ptr<QProcess> m_turn;
    QString m_log;
    QString m_turnLog;
};

// A relay between the ends and the service that records every message
// both ways, so a test can see what the service was given.
class RecordingRelay : public QObject {
public:
    explicit RecordingRelay(const QUrl& service)
        : m_service(service)
        , m_server(QStringLiteral("recording-relay"), QWebSocketServer::NonSecureMode)
    {
        m_server.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&m_server, &QWebSocketServer::newConnection, this, [this] {
            while (m_server.hasPendingConnections()) {
                QWebSocket* inner = m_server.nextPendingConnection();
                inner->setParent(this);
                auto* outer = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
                auto* backlog = new QStringList;
                m_backlogs.append(backlog);
                QObject::connect(inner, &QWebSocket::textMessageReceived, this,
                                 [this, outer, backlog](const QString& text) {
                    toService.append(text);
                    if (outer->state() == QAbstractSocket::ConnectedState) {
                        outer->sendTextMessage(text);
                    } else {
                        backlog->append(text);
                    }
                });
                QObject::connect(outer, &QWebSocket::connected, this, [outer, backlog] {
                    for (const QString& text : std::as_const(*backlog)) {
                        outer->sendTextMessage(text);
                    }
                    backlog->clear();
                });
                QObject::connect(outer, &QWebSocket::textMessageReceived, this,
                                 [this, inner](const QString& text) {
                    fromService.append(text);
                    inner->sendTextMessage(text);
                });
                QObject::connect(inner, &QWebSocket::disconnected, outer, [outer] { outer->close(); });
                QObject::connect(outer, &QWebSocket::disconnected, inner, [inner] { inner->close(); });
                outer->open(m_service);
            }
        });
    }

    ~RecordingRelay() override { qDeleteAll(m_backlogs); }

    QUrl url() const
    {
        return QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(m_server.serverPort()));
    }

    QStringList toService;
    QStringList fromService;

private:
    QUrl m_service;
    QWebSocketServer m_server;
    QList<QStringList*> m_backlogs;
};

// One Core with its StationServer, as the pairing tests stand it up.
struct Core {
    QTemporaryDir settingsDir;
    QTemporaryDir securityDir;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<RadioModel> model;
    std::unique_ptr<StationServer> server;

    // `upgraded`: a Core from before paired devices, which still signs a
    // window in by its token (a new Core has none). `tokenLike` names
    // another upgraded Core's security directory whose token this one
    // shares, so a test can show the token was never sent to it.
    explicit Core(bool upgraded = false, const QString& tokenLike = QString())
    {
        settings = std::make_unique<AppSettings>(
            settingsDir.filePath(QStringLiteral("NereusSDR.settings")));
        settings->setValue(QStringLiteral("StationCallsign"), QStringLiteral("KG4VCF"));
        model = std::make_unique<RadioModel>();
        const QString security = NereusSDR::Test::seedCoreIdentity(securityDir.path());
        if (!tokenLike.isEmpty()) {
            QFile::copy(QDir(tokenLike).filePath(QStringLiteral("station-token")),
                        QDir(security).filePath(QStringLiteral("station-token")));
        }
        if (upgraded) {
            NereusSDR::Test::seedUpgradedCoreToken(security);
        }
        server = std::make_unique<StationServer>(model.get(), *settings, security);
        server->setHeartbeatIntervalMs(0);
    }

    ~Core() { server.reset(); }

    // This computer, paired as a device the way a pairing leaves it.
    bool pairComputer(const ClientDeviceIdentity& key)
    {
        PairedDevice device;
        device.id = key.fingerprint();
        device.publicKeySpki = key.publicKeySpki();
        device.name = QStringLiteral("Shack MacBook");
        device.kind = QStringLiteral("computer");
        return server->deviceStore()->add(device);
    }

    QUrl url() const
    {
        return QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(server->serverPort()));
    }

    bool pair(const TestKey& key, const QString& name = QStringLiteral("Test phone"))
    {
        PairedDevice device;
        device.id = StationIdentity::fingerprintOf(key.spki());
        device.publicKeySpki = key.spki();
        device.name = name;
        device.kind = QStringLiteral("phone");
        return server->deviceStore()->add(device);
    }
};

// Both ends of one connection through the service: the client's offer and
// the Core's answer each on a LibDataChannelMediaTransport started with the
// service's ICE settings, candidates trickled through the service. With
// `relayOnly`, only relay candidates are passed on in either direction:
// the test's stand-in for a network where direct UDP is blocked.
class IcePair : public QObject {
public:
    IcePair(RendezvousClient* station, RendezvousClient* client, bool relayOnly)
        : m_station(station), m_client(client), m_relayOnly(relayOnly)
    {
        QObject::connect(station, &RendezvousClient::introduced, this,
                         [this](const RendezvousIntroduction& introduction) {
            onIntroduced(introduction);
        });
        QObject::connect(station, &RendezvousClient::credentialsReceived, this,
                         [this](const QByteArray&, bool offered, const Wire::Turn& turn) {
            IceConfiguration ice = *m_stationIce;
            ice.setRelay(offered ? std::optional<Wire::Turn>(turn) : std::nullopt, 1);
            stationRelays = ice.relayServers().size();
            m_answerer->gatherCandidates(ice.relayServers());
        });
        QObject::connect(station, &RendezvousClient::candidateReceived, this,
                         [this](const QByteArray&, const QString& candidate) {
            ++stationHeard;
            if (!candidate.isEmpty() && m_answerer && passes(candidate)) {
                m_answerer->acceptCandidate(candidate, QString());
            }
        });
        QObject::connect(client, &RendezvousClient::answerReceived, this,
                         [this](const QString& sdp, bool offered, const Wire::Turn& turn) {
            QVERIFY(m_offerer->acceptDescription(sdp, QStringLiteral("answer")));
            IceConfiguration ice = *m_clientIce;
            ice.setRelay(offered ? std::optional<Wire::Turn>(turn) : std::nullopt, 1);
            clientRelays = ice.relayServers().size();
            m_offerer->gatherCandidates(ice.relayServers());
        });
        QObject::connect(client, &RendezvousClient::candidateReceived, this,
                         [this](const QByteArray&, const QString& candidate) {
            if (!candidate.isEmpty() && passes(candidate)) {
                m_offerer->acceptCandidate(candidate, QString());
            }
        });
    }

    // Connects the client to the service, then makes its offer with the
    // service's STUN server and introduces it to the Core as `device`.
    void start(const QString& stationId, std::shared_ptr<TestKey> device)
    {
        m_device = std::move(device);
        QObject::connect(m_client, &RendezvousClient::connected, this, [this, stationId] {
            if (m_offerer) {
                return;
            }
            // Nothing known about either family on the loopback: the service's
            // first entry, one relay.
            m_clientIce = IceConfiguration::throughRendezvous(m_client->stunUrls(), true,
                                                              AddressFamilies{}, HostFamilies{});
            m_offerer = std::make_unique<LibDataChannelMediaTransport>();
            QObject::connect(m_offerer.get(), &IMediaTransport::localDescription, this,
                             [this, stationId](const QString& sdp, const QString&) {
                const std::shared_ptr<TestKey> key = m_device;
                m_client->introduce(stationId, key->spki(),
                                    [key](const QByteArray& message) { return key->sign(message); },
                                    sdp);
            });
            QObject::connect(m_offerer.get(), &IMediaTransport::localCandidate, this,
                             [this](const QString& candidate, const QString&) {
                clientCandidates.append(candidate);
                m_client->sendCandidate(candidate);
            });
            QObject::connect(m_offerer.get(), &IMediaTransport::gatheringComplete, this, [this] {
                clientGathered = true;
                m_client->sendCandidate(QString());
            });
            QObject::connect(m_offerer.get(), &IMediaTransport::ready, this,
                             [this] { clientReady = true; });
            QObject::connect(m_offerer.get(), &IMediaTransport::displayReceived, this,
                             [this](const QByteArray& message) { clientReceived.append(message); });
            IMediaTransport::StartOptions options{IMediaTransport::Role::Offerer, 0x1111};
            options.ice = m_clientIce;
            QVERIFY(m_offerer->start(options));
        });
        m_client->connectToService();
    }

    LibDataChannelMediaTransport* offerer() const { return m_offerer.get(); }
    LibDataChannelMediaTransport* answerer() const { return m_answerer.get(); }

    bool clientReady = false;
    bool stationReady = false;
    bool clientGathered = false;
    bool stationGathered = false;
    int stationHeard = 0;
    qsizetype stationRelays = -1;
    qsizetype clientRelays = -1;
    QStringList clientCandidates;
    QStringList stationCandidates;
    QList<QByteArray> clientReceived;
    QList<QByteArray> stationReceived;

private:
    bool passes(const QString& candidate) const
    {
        return !m_relayOnly || IceConfiguration::candidateType(candidate) == QLatin1String("relay");
    }

    void onIntroduced(const RendezvousIntroduction& introduction)
    {
        const QByteArray id = introduction.id;
        m_stationIce = IceConfiguration::throughRendezvous(
            m_station->stunUrls(), m_station->relayAllowed(), AddressFamilies{}, HostFamilies{});
        m_answerer = std::make_unique<LibDataChannelMediaTransport>();
        QObject::connect(m_answerer.get(), &IMediaTransport::localDescription, this,
                         [this, id](const QString& sdp, const QString&) {
            QVERIFY(m_station->answer(id, sdp));
            // With the relay denied no credentials follow: gather now.
            if (!m_station->relayAllowed()) {
                m_answerer->gatherCandidates({});
            }
        });
        QObject::connect(m_answerer.get(), &IMediaTransport::localCandidate, this,
                         [this, id](const QString& candidate, const QString&) {
            stationCandidates.append(candidate);
            m_station->sendCandidate(id, candidate);
        });
        QObject::connect(m_answerer.get(), &IMediaTransport::gatheringComplete, this, [this, id] {
            stationGathered = true;
            m_station->sendCandidate(id, QString());
        });
        QObject::connect(m_answerer.get(), &IMediaTransport::ready, this,
                         [this] { stationReady = true; });
        QObject::connect(m_answerer.get(), &IMediaTransport::displayReceived, this,
                         [this](const QByteArray& message) { stationReceived.append(message); });
        IMediaTransport::StartOptions options{IMediaTransport::Role::Answerer, 0x2222};
        options.ice = m_stationIce;
        QVERIFY(m_answerer->start(options));
        QVERIFY(m_answerer->acceptDescription(introduction.offer, QStringLiteral("offer")));
    }

    RendezvousClient* m_station;
    RendezvousClient* m_client;
    bool m_relayOnly;
    std::shared_ptr<TestKey> m_device;
    std::optional<IceConfiguration> m_stationIce;
    std::optional<IceConfiguration> m_clientIce;
    std::unique_ptr<LibDataChannelMediaTransport> m_offerer;
    std::unique_ptr<LibDataChannelMediaTransport> m_answerer;
};

bool hasUsableIpv6()
{
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (address.protocol() == QAbstractSocket::IPv6Protocol && !address.isLinkLocal()
                && !address.isLoopback()) {
                return true;
            }
        }
    }
    return false;
}

// A certificate for 127.0.0.1 (in its subjectAltName) and its key, made at
// run time and written where a Core's CertificateStore loads them, so a
// test can add the certificate to the trust store and have a handshake to
// that Core raise no TLS error at all. RSA, as CertificateStore uses.
QSslCertificate writeLoopbackCertificate(const QString& directory)
{
    using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
    using CertPtr = std::unique_ptr<X509, decltype(&X509_free)>;
    using BioPtr = std::unique_ptr<BIO, decltype(&BIO_free)>;
    KeyPtr key(EVP_RSA_gen(2048), &EVP_PKEY_free);
    CertPtr cert(X509_new(), &X509_free);
    if (!key || !cert || X509_set_version(cert.get(), 2) != 1
        || ASN1_INTEGER_set_int64(X509_get_serialNumber(cert.get()),
                                  QRandomGenerator::system()->bounded(1, 1 << 30))
               != 1
        || !X509_gmtime_adj(X509_getm_notBefore(cert.get()), -60)
        || !X509_gmtime_adj(X509_getm_notAfter(cert.get()), 24 * 60 * 60)
        || X509_set_pubkey(cert.get(), key.get()) != 1) {
        return QSslCertificate();
    }
    X509_NAME* name = X509_get_subject_name(cert.get());
    if (X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                                   reinterpret_cast<const unsigned char*>("127.0.0.1"), -1, -1, 0)
            != 1
        || X509_set_issuer_name(cert.get(), name) != 1) {
        return QSslCertificate();
    }
    X509V3_CTX context;
    X509V3_set_ctx(&context, cert.get(), cert.get(), nullptr, nullptr, 0);
    const std::pair<int, const char*> extensions[] = {
        {NID_basic_constraints, "critical,CA:TRUE"},
        {NID_key_usage, "critical,digitalSignature,keyEncipherment,keyCertSign"},
        {NID_ext_key_usage, "serverAuth"},
        {NID_subject_alt_name, "IP:127.0.0.1"},
    };
    for (const auto& [nid, value] : extensions) {
        X509_EXTENSION* extension = X509V3_EXT_conf_nid(nullptr, &context, nid, value);
        const bool added = extension && X509_add_ext(cert.get(), extension, -1) == 1;
        X509_EXTENSION_free(extension);
        if (!added) {
            return QSslCertificate();
        }
    }
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) == 0) {
        return QSslCertificate();
    }
    BioPtr certBio(BIO_new(BIO_s_mem()), &BIO_free);
    BioPtr keyBio(BIO_new(BIO_s_mem()), &BIO_free);
    if (!certBio || !keyBio || PEM_write_bio_X509(certBio.get(), cert.get()) != 1
        || PEM_write_bio_PrivateKey(keyBio.get(), key.get(), nullptr, nullptr, 0, nullptr,
                                    nullptr)
               != 1) {
        return QSslCertificate();
    }
    const auto contents = [](BIO* bio) {
        char* data = nullptr;
        const long length = BIO_get_mem_data(bio, &data);
        return QByteArray(data, static_cast<qsizetype>(length));
    };
    const QByteArray certPem = contents(certBio.get());
    const std::pair<QString, QByteArray> files[] = {
        {QStringLiteral("tls-cert.pem"), certPem},
        {QStringLiteral("tls-key.pem"), contents(keyBio.get())},
    };
    for (const auto& [file, pem] : files) {
        QFile out(QDir(directory).filePath(file));
        if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(pem) != pem.size()
            || !out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            return QSslCertificate();
        }
    }
    return QSslCertificate(certPem, QSsl::Pem);
}

// Adds a certificate authority to the trust store for as long as it lives.
class TrustedAuthority {
public:
    explicit TrustedAuthority(const QSslCertificate& authority)
        : m_saved(QSslConfiguration::defaultConfiguration())
    {
        QSslConfiguration configuration = m_saved;
        configuration.addCaCertificate(authority);
        QSslConfiguration::setDefaultConfiguration(configuration);
    }
    ~TrustedAuthority() { QSslConfiguration::setDefaultConfiguration(m_saved); }
    TrustedAuthority(const TrustedAuthority&) = delete;
    TrustedAuthority& operator=(const TrustedAuthority&) = delete;

private:
    QSslConfiguration m_saved;
};

} // namespace

class TstRendezvousClient : public QObject {
    Q_OBJECT

private:
    void runCoreFixture(const QString& file);
    void runAppFixture(const QString& file);

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RendezvousIntroduction>();
        qRegisterMetaType<Wire::Turn>();
        QVERIFY2(QFile::exists(kSuite + QStringLiteral("/manifest.json")), qPrintable(kSuite));
    }

    // ── Crypto vectors (section 10.2) ───────────────────────────────────

    void base64urlVectors()
    {
        const QJsonArray cases =
            readJson(kSuite + QStringLiteral("/crypto/base64url.json")).value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            bool ok = false;
            const QByteArray bytes =
                StationIdentity::fromBase64Url(c.value(QStringLiteral("text")).toString(), &ok);
            QCOMPARE(ok, c.value(QStringLiteral("valid")).toBool());
            if (ok) {
                QCOMPARE(bytes.toHex(), c.value(QStringLiteral("bytesHex")).toString().toLatin1());
            }
        }
    }

    void p256KeyVectors()
    {
        const QJsonArray cases =
            readJson(kSuite + QStringLiteral("/crypto/p256-spki.json")).value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            const QByteArray spki =
                QByteArray::fromHex(c.value(QStringLiteral("spkiHex")).toString().toLatin1());
            QVERIFY2(StationIdentity::isP256Spki(spki) == c.value(QStringLiteral("valid")).toBool(),
                     qPrintable(c.value(QStringLiteral("name")).toString()));
        }
    }

    void rendezvousIdVectors()
    {
        const QJsonObject file = readJson(kSuite + QStringLiteral("/crypto/rendezvous-id.json"));
        QCOMPARE(QByteArray("NereusSDR rendezvous id v1\n").toHex(),
                 file.value(QStringLiteral("prefixHex")).toString().toLatin1());
        const QJsonArray cases = file.value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            const QByteArray spki = unb64(c.value(QStringLiteral("publicKey")).toString());
            QCOMPARE(QCryptographicHash::hash(QByteArray("NereusSDR rendezvous id v1\n") + spki,
                                              QCryptographicHash::Sha256)
                         .toHex(),
                     c.value(QStringLiteral("digestHex")).toString().toLatin1());
            const QString id = Wire::rendezvousId(spki);
            QCOMPARE(id, c.value(QStringLiteral("id")).toString());
            QVERIFY(Wire::isRendezvousId(id));
        }
        QVERIFY(!Wire::isRendezvousId(QStringLiteral("LWHU2KYJNRFDWVPXVDCKAO3LB7")));
        QVERIFY(!Wire::isRendezvousId(QStringLiteral("lwhu2kyjnrfdwvpxvdckao3lb")));
        QVERIFY(Wire::rendezvousId(QByteArray(90, 'x')).isEmpty());
    }

    void registerProofVectors()
    {
        const QJsonObject file = readJson(kSuite + QStringLiteral("/crypto/register-proof.json"));
        const QJsonObject key = file.value(QStringLiteral("key")).toObject();
        const QByteArray spki = unb64(key.value(QStringLiteral("publicKey")).toString());
        QCOMPARE(Wire::rendezvousId(spki), key.value(QStringLiteral("id")).toString());
        const QByteArray transcript =
            Wire::registerTranscript(unb64(file.value(QStringLiteral("nonce")).toString()));
        QCOMPARE(transcript.size(), 65);
        QCOMPARE(transcript.toHex(), file.value(QStringLiteral("transcriptHex")).toString().toLatin1());
        const QJsonArray cases = file.value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            bool keyOk = false;
            bool signatureOk = false;
            const QByteArray caseKey =
                StationIdentity::fromBase64Url(c.value(QStringLiteral("publicKey")).toString(), &keyOk);
            const QByteArray signature =
                StationIdentity::fromBase64Url(c.value(QStringLiteral("signature")).toString(), &signatureOk);
            const bool verified = keyOk && signatureOk && StationIdentity::isP256Spki(caseKey)
                                  && StationIdentity::verify(caseKey, transcript, signature);
            QVERIFY2(verified == c.value(QStringLiteral("valid")).toBool(),
                     qPrintable(c.value(QStringLiteral("name")).toString()));
        }
    }

    void introduceSignatureVectors()
    {
        const QJsonObject file = readJson(kSuite + QStringLiteral("/crypto/introduce-signature.json"));
        const QJsonObject device = file.value(QStringLiteral("device")).toObject();
        const QByteArray spki = unb64(device.value(QStringLiteral("publicKey")).toString());
        QCOMPARE(b64(StationIdentity::fingerprintOf(spki)), device.value(QStringLiteral("id")).toString());
        const QByteArray transcript =
            Wire::introduceTranscript(file.value(QStringLiteral("stationId")).toString(),
                                      unb64(file.value(QStringLiteral("nonce")).toString()));
        QCOMPARE(transcript.size(), 81);
        QCOMPARE(transcript.toHex(), file.value(QStringLiteral("transcriptHex")).toString().toLatin1());
        const QJsonArray cases = file.value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            bool ok = false;
            const QByteArray signature =
                StationIdentity::fromBase64Url(c.value(QStringLiteral("signature")).toString(), &ok);
            const bool verified = ok && StationIdentity::verify(spki, transcript, signature);
            QVERIFY2(verified == c.value(QStringLiteral("valid")).toBool(),
                     qPrintable(c.value(QStringLiteral("name")).toString()));
        }
    }

    void turnCredentialVectors()
    {
        const QJsonArray cases =
            readJson(kSuite + QStringLiteral("/crypto/turn-credentials.json")).value(QStringLiteral("cases")).toArray();
        QVERIFY(!cases.isEmpty());
        for (const QJsonValue& value : cases) {
            const QJsonObject c = value.toObject();
            const QString username = QStringLiteral("%1:%2")
                                         .arg(c.value(QStringLiteral("expires")).toInteger())
                                         .arg(c.value(QStringLiteral("stationId")).toString());
            QCOMPARE(username, c.value(QStringLiteral("username")).toString());
            QCOMPARE(turnPassword(c.value(QStringLiteral("secret")).toString().toUtf8(), username),
                     c.value(QStringLiteral("password")).toString());
        }
    }

    // ── Control fixtures (section 10.3) ─────────────────────────────────

    void controlFixtures()
    {
        QDir dir(kSuite + QStringLiteral("/control"));
        const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        QVERIFY(files.size() > 40);
        int station = 0;
        int client = 0;
        for (const QString& name : files) {
            const QJsonObject fixture = readJson(dir.filePath(name));
            const QString from = fixture.value(QStringLiteral("from")).toString();
            const QString to = fixture.value(QStringLiteral("to")).toString();
            Wire::Direction direction;
            if (from == QLatin1String("station")) {
                direction = Wire::Direction::StationToService;
                ++station;
            } else if (from == QLatin1String("client")) {
                direction = Wire::Direction::ClientToService;
                ++client;
            } else if (to == QLatin1String("station")) {
                direction = Wire::Direction::ServiceToStation;
                ++station;
            } else {
                direction = Wire::Direction::ServiceToClient;
                ++client;
            }
            const QJsonObject wire = fixture.value(QStringLiteral("wire")).toObject();
            const QByteArray text = QJsonDocument(wire).toJson(QJsonDocument::Compact);
            Wire::Message message;
            QString why;
            const bool decoded = Wire::decode(direction, text, &message, &why);
            QVERIFY2(decoded == fixture.value(QStringLiteral("decodes")).toBool(),
                     qPrintable(name + QStringLiteral(": ") + why));
            if (!decoded) {
                continue;
            }
            const QByteArray again = Wire::encode(direction, message);
            QVERIFY2(!again.isEmpty(), qPrintable(name));
            const QStringList keys = listedKeys(direction, message.kind);
            const QJsonObject ours = QJsonDocument::fromJson(again).object();
            QStringList ourKeys = ours.keys();
            QStringList wantKeys = keys;
            ourKeys.sort();
            wantKeys.sort();
            QVERIFY2(ourKeys == wantKeys, qPrintable(name));
            QVERIFY2(restricted(ours, keys) == restricted(wire, keys), qPrintable(name));
        }
        QVERIFY(station > 0);
        QVERIFY(client > 0);
    }

    // A service that replays an accepted introduction under new ids gets
    // at most kMaxLiveIntroductions of them reported; the rest are dropped
    // and counted.
    void theCoreHoldsABoundedNumberOfIntroductions()
    {
        ServicePlayer player;
        auto coreKey = makeKey();
        auto device = makeKey();
        const QByteArray deviceId = StationIdentity::fingerprintOf(device->spki());
        RendezvousClient core;
        core.setServers({player.url()});
        QSignalSpy registered(&core, &RendezvousClient::registered);
        QSignalSpy introduced(&core, &RendezvousClient::introduced);
        core.registerStation(coreKey->spki(),
                             [coreKey](const QByteArray& m) { return coreKey->sign(m); },
                             [device, deviceId](const QByteArray& id) {
                                 return id == deviceId ? device->spki() : QByteArray();
                             });
        QWebSocket* station = player.waitForConnection();
        QVERIFY(station != nullptr);
        const auto send = [station](const QJsonObject& message) {
            station->sendTextMessage(compact(message));
        };
        send({{"type", "hello"}, {"version", 1}, {"nonce", b64(randomBytes(32))},
              {"stun", QJsonArray()}});
        QVERIFY(player.waitForMessage(station).has_value());
        const QByteArray challenge = randomBytes(32);
        send({{"type", "challenge"}, {"nonce", b64(challenge)}});
        QVERIFY(player.waitForMessage(station).has_value());
        const QString id = Wire::rendezvousId(coreKey->spki());
        send({{"type", "registered"}, {"id", id}});
        QTRY_COMPARE(registered.size(), 1);
        const QByteArray nonce = randomBytes(32);
        const QString signature = b64(device->sign(Wire::introduceTranscript(id, nonce)));
        const QString offer = readText(kSuite + QStringLiteral("/sdp/offer.sdp"));
        const int sent = RendezvousClient::kMaxLiveIntroductions + 4;
        for (int index = 0; index < sent; ++index) {
            send({{"type", "introduction"}, {"from", b64(randomBytes(16))},
                  {"device", b64(deviceId)}, {"deviceSignature", signature}, {"offer", offer},
                  {"nonce", b64(nonce)}});
        }
        QTRY_COMPARE(introduced.size() + static_cast<qsizetype>(core.droppedIntroductions()),
                     static_cast<qsizetype>(sent));
        QCOMPARE(introduced.size(), static_cast<qsizetype>(RendezvousClient::kMaxLiveIntroductions));
        QCOMPARE(core.droppedIntroductions(), quint64(4));
        core.stop();
    }

    // Fix wave I3: a service that sends its hello and then never registers
    // the Core is left after the hello time, and the reconnect runs; the
    // time is shortened through the injectable value.
    void aServiceThatNeverRegistersTheCoreIsLeft()
    {
        ServicePlayer player;
        auto coreKey = makeKey();
        RendezvousClient core;
        core.setServers({player.url()});
        QCOMPARE(core.helloTimeoutMs(), RendezvousClient::kHelloTimeoutMs);
        core.setHelloTimeoutMs(200);
        core.setReconnectDelaysMs({50});
        QSignalSpy lost(&core, &RendezvousClient::connectionLost);
        core.registerStation(coreKey->spki(),
                             [coreKey](const QByteArray& m) { return coreKey->sign(m); },
                             [](const QByteArray&) { return QByteArray(); });
        QWebSocket* first = player.waitForConnection();
        QVERIFY(first != nullptr);
        first->sendTextMessage(compact(QJsonObject{{"type", "hello"}, {"version", 1},
                                        {"nonce", b64(randomBytes(32))}, {"stun", QJsonArray()}}));
        const std::optional<QString> registration = player.waitForMessage(first);
        QVERIFY(registration.has_value());
        QVERIFY(registration->contains(QLatin1String("\"register\"")));
        // Then nothing: no challenge, no registered.
        QTRY_COMPARE_WITH_TIMEOUT(lost.size(), 1, 5000);
        QVERIFY(!core.isRegistered());
        QWebSocket* second = player.waitForConnection();
        QVERIFY(second != nullptr);
        core.stop();
    }

    // Fix wave: `replaced` keeps the backoff climbing across the next
    // registration, so two Cores that share a key do not flap at the first
    // rung; an ordinary loss still starts again from the first rung once
    // the Core is registered.
    void aReplacedCoreKeepsBackingOff()
    {
        ServicePlayer player;
        auto coreKey = makeKey();
        RendezvousClient core;
        core.setServers({player.url()});
        core.setReconnectDelaysMs({20, 40, 60, 80});
        QSignalSpy registered(&core, &RendezvousClient::registered);
        core.registerStation(coreKey->spki(),
                             [coreKey](const QByteArray& m) { return coreKey->sign(m); },
                             [](const QByteArray&) { return QByteArray(); });
        const QString id = Wire::rendezvousId(coreKey->spki());
        const auto registerOn = [&](QWebSocket* socket) {
            socket->sendTextMessage(compact(QJsonObject{{"type", "hello"}, {"version", 1},
                                             {"nonce", b64(randomBytes(32))},
                                             {"stun", QJsonArray()}}));
            QVERIFY(player.waitForMessage(socket).has_value());
            socket->sendTextMessage(compact(QJsonObject{{"type", "challenge"}, {"nonce", b64(randomBytes(32))}}));
            QVERIFY(player.waitForMessage(socket).has_value());
            socket->sendTextMessage(compact(QJsonObject{{"type", "registered"}, {"id", id}}));
        };
        const auto replace = [](QWebSocket* socket) {
            socket->sendTextMessage(compact(QJsonObject{{"type", "error"}, {"code", "replaced"},
                                             {"reason", "The Core registered again on another "
                                                        "connection, so this one was closed."},
                                             {"retryAfterMs", 0}}));
            socket->close();
        };

        QWebSocket* socket = player.waitForConnection();
        QVERIFY(socket != nullptr);
        registerOn(socket);
        QTRY_COMPARE(registered.size(), 1);
        QCOMPARE(core.reconnectAttempts(), 0);
        for (int round = 1; round <= 2; ++round) {
            replace(socket);
            socket = player.waitForConnection();
            QVERIFY(socket != nullptr);
            registerOn(socket);
            QTRY_COMPARE(registered.size(), 1 + round);
            QCOMPARE(core.reconnectAttempts(), round);
        }
        // An ordinary loss: registered again, back to the first rung.
        socket->close();
        socket = player.waitForConnection();
        QVERIFY(socket != nullptr);
        registerOn(socket);
        QTRY_COMPARE(registered.size(), 4);
        QCOMPARE(core.reconnectAttempts(), 0);
        core.stop();
    }

    // Fix wave: a `nameplate` the Core did not claim, and a second
    // `mailbox.opened` while one is open, are ignored.
    void unaskedNameplatesAndSecondMailboxesAreIgnored()
    {
        ServicePlayer player;
        auto coreKey = makeKey();
        RendezvousClient core;
        core.setServers({player.url()});
        QSignalSpy registered(&core, &RendezvousClient::registered);
        QSignalSpy claimed(&core, &RendezvousClient::nameplateClaimed);
        QSignalSpy opened(&core, &RendezvousClient::mailboxOpened);
        QSignalSpy received(&core, &RendezvousClient::mailboxReceived);
        core.registerStation(coreKey->spki(),
                             [coreKey](const QByteArray& m) { return coreKey->sign(m); },
                             [](const QByteArray&) { return QByteArray(); });
        QWebSocket* socket = player.waitForConnection();
        QVERIFY(socket != nullptr);
        const auto send = [socket](const QJsonObject& message) {
            socket->sendTextMessage(compact(message));
        };
        send({{"type", "hello"}, {"version", 1}, {"nonce", b64(randomBytes(32))},
              {"stun", QJsonArray()}});
        QVERIFY(player.waitForMessage(socket).has_value());
        send({{"type", "challenge"}, {"nonce", b64(randomBytes(32))}});
        QVERIFY(player.waitForMessage(socket).has_value());
        send({{"type", "registered"}, {"id", Wire::rendezvousId(coreKey->spki())}});
        QTRY_COMPARE(registered.size(), 1);

        // Messages are handled in order, so once the mailbox opens the
        // nameplate before it has been dealt with.
        send({{"type", "nameplate"}, {"nameplate", 7}});
        send({{"type", "mailbox.opened"}, {"nameplate", 7}});
        QTRY_COMPARE(opened.size(), 1);
        QCOMPARE(claimed.size(), 0);
        send({{"type", "mailbox.opened"}, {"nameplate", 8}});
        send({{"type", "mailbox"}, {"body", "one"}});
        QTRY_COMPARE(received.size(), 1);
        QCOMPARE(opened.size(), 1);

        // A claim this Core made is answered as before.
        core.claimNameplate();
        const std::optional<QString> claim = player.waitForMessage(socket);
        QVERIFY(claim.has_value());
        QVERIFY(claim->contains(QLatin1String("nameplate.claim")));
        send({{"type", "nameplate"}, {"nameplate", 9}});
        QTRY_COMPARE(claimed.size(), 1);
        QCOMPARE(claimed.first().first().toInt(), 9);
        core.stop();
    }

    void theSenderKeepsToTheWire()
    {
        // Section 2: a message over the cap is not sent, whatever its kind.
        Wire::Message mailbox;
        mailbox.kind = Wire::Kind::Mailbox;
        mailbox.body = QString(Wire::kMaxBodyBytes, QLatin1Char('b'));
        QVERIFY(!Wire::encode(Wire::Direction::ClientToService, mailbox).isEmpty());
        mailbox.body.append(QLatin1Char('b'));
        QVERIFY(Wire::encode(Wire::Direction::ClientToService, mailbox).isEmpty());
        // Control characters escape to six bytes each: the field fits, the
        // message does not.
        mailbox.body = QString(Wire::kMaxBodyBytes, QChar(0x01));
        QVERIFY(Wire::encode(Wire::Direction::ClientToService, mailbox).size() == 0);
        // A lone surrogate is never sent.
        mailbox.body = QString(QChar(0xD800));
        QVERIFY(Wire::encode(Wire::Direction::ClientToService, mailbox).isEmpty());
        // An `a=` is taken off a candidate, and one that still does not
        // start `candidate:` is refused.
        QCOMPARE(Wire::wireCandidate(QStringLiteral("a=candidate:1 1 UDP 1 ::1 5 typ host")),
                 QStringLiteral("candidate:1 1 UDP 1 ::1 5 typ host"));
        Wire::Message candidate;
        candidate.kind = Wire::Kind::Candidate;
        candidate.candidate = QStringLiteral("a=candidate:1 1 UDP 1 ::1 5 typ host");
        QVERIFY(Wire::encode(Wire::Direction::ClientToService, candidate).isEmpty());
        // A station never sends a client's kinds, nor a client a station's.
        Wire::Message claim;
        claim.kind = Wire::Kind::NameplateClaim;
        QVERIFY(Wire::encode(Wire::Direction::ClientToService, claim).isEmpty());
        QVERIFY(!Wire::encode(Wire::Direction::StationToService, claim).isEmpty());
    }

    void serverAddressesAreRead()
    {
        QStringList rejected;
        const QList<QUrl> urls = RendezvousClient::serverUrls(
            {QStringLiteral("rv.nereussdr.com"), QStringLiteral("rv.example.net:8443"),
             QStringLiteral("[2001:db8::5]:443"), QStringLiteral("wss://rv.example.org/path"),
             QStringLiteral("ws://127.0.0.1:8710"), QStringLiteral("ws://rv.example.net"),
             QStringLiteral("http://rv.example.net"), QStringLiteral("rv.nereussdr.com")},
            &rejected);
        QCOMPARE(urls,
                 QList<QUrl>({QUrl(QStringLiteral("wss://rv.nereussdr.com/")),
                              QUrl(QStringLiteral("wss://rv.example.net:8443/")),
                              QUrl(QStringLiteral("wss://[2001:db8::5]:443/")),
                              QUrl(QStringLiteral("wss://rv.example.org/path")),
                              QUrl(QStringLiteral("ws://127.0.0.1:8710/"))}));
        QCOMPARE(rejected, QStringList({QStringLiteral("ws://rv.example.net"),
                                        QStringLiteral("http://rv.example.net")}));
    }

    // Every test binary runs in test mode (tests/TestSandboxInit.cpp): a
    // Core started with the default server list, as tst_daemon_app starts
    // one, never reaches rv.nereussdr.com from a test.
    void aTestRunNeverLeavesThisComputer()
    {
        QVERIFY(QStandardPaths::isTestModeEnabled());
        RendezvousClient client;
        client.setServers(RendezvousClient::serverUrls({QString::fromLatin1(RendezvousClient::kDefaultServer)}));
        QCOMPARE(client.servers(), QList<QUrl>({QUrl(QStringLiteral("wss://rv.nereussdr.com/"))}));
        QSignalSpy unreachable(&client, &RendezvousClient::unreachable);
        QSignalSpy connected(&client, &RendezvousClient::connected);
        client.connectToService();
        QTRY_COMPARE(unreachable.size(), 1);
        QCOMPARE(connected.size(), 0);
        QCOMPARE(client.findChildren<QWebSocket*>().size(), 0);
    }

    // The service is reached by host name through a web server (Caddy on
    // rv.nereussdr.com), so the opening request is an HTTP/1.1 Upgrade
    // (QWebSocket's only kind; never HTTP/2's extended CONNECT) carrying the
    // service's name, not an address it resolved to.
    void theServiceIsAskedForByName()
    {
        ServicePlayer player;
        RendezvousClient client;
        client.setServers(RendezvousClient::serverUrls(
            {QStringLiteral("ws://localhost:%1").arg(player.port())}));
        client.connectToService();
        QVERIFY(player.waitForConnection() != nullptr);
        QCOMPARE(player.hostHeaders,
                 QStringList({QStringLiteral("localhost:%1").arg(player.port())}));
        client.stop();
    }

    // ── The Core's and the desktop's runners (section 10.4) ────────────

    void coreRunner_data()
    {
        QTest::addColumn<QString>("file");
        const QJsonArray fixtures =
            readJson(kSuite + QStringLiteral("/manifest.json")).value(QStringLiteral("fixtures")).toArray();
        int count = 0;
        for (const QJsonValue& value : fixtures) {
            const QString file = value.toObject().value(QStringLiteral("file")).toString();
            if (value.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("session")) {
                continue;
            }
            const QJsonArray runs = readJson(kSuite + QLatin1Char('/') + file).value(QStringLiteral("runs")).toArray();
            if (runs.contains(QJsonValue(QStringLiteral("core")))) {
                QTest::newRow(qPrintable(file)) << file;
                ++count;
            }
        }
        QVERIFY(count == 10);
    }

    void coreRunner()
    {
        QFETCH(QString, file);
        runCoreFixture(file);
    }

    void appRunner_data()
    {
        QTest::addColumn<QString>("file");
        const QJsonArray fixtures =
            readJson(kSuite + QStringLiteral("/manifest.json")).value(QStringLiteral("fixtures")).toArray();
        for (const QJsonValue& value : fixtures) {
            const QString file = value.toObject().value(QStringLiteral("file")).toString();
            if (value.toObject().value(QStringLiteral("kind")).toString() != QLatin1String("session")) {
                continue;
            }
            const QJsonArray runs = readJson(kSuite + QLatin1Char('/') + file).value(QStringLiteral("runs")).toArray();
            if (runs.contains(QJsonValue(QStringLiteral("app")))) {
                QTest::newRow(qPrintable(file)) << file;
            }
        }
    }

    void appRunner()
    {
        QFETCH(QString, file);
        runAppFixture(file);
    }

    // ── Against the Python service on this computer ────────────────────

    void aPairedDeviceConnectsOverIceUsingStun()
    {
        // STUN only: the service has no relay, so the connection is direct.
        LocalService service(/*stun=*/true, /*relay=*/false);
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        // The id comes from the station identity key, never the TLS
        // certificate's.
        QCOMPARE(rendezvous.client()->stationId(),
                 Wire::rendezvousId(core.server->stationIdentity().publicKeySpki()));

        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, /*relayOnly=*/false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);
        const auto path = pair.offerer()->selectedPath();
        QVERIFY(path.has_value());
        QVERIFY(!path->relayed());
        // The service's STUN server was used: both ends gathered a
        // server-reflexive candidate from it.
        const auto gatheredThroughServer = [](const QStringList& candidates) {
            for (const QString& candidate : candidates) {
                if (IceConfiguration::candidateType(candidate) == QLatin1String("srflx")) {
                    return true;
                }
            }
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(gatheredThroughServer(pair.clientCandidates), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(gatheredThroughServer(pair.stationCandidates), 10000);
        QCOMPARE(rendezvous.client()->droppedIntroductions(), quint64(0));
    }

    // With the relay on offer too, a working direct path is the one used.
    void aDirectPathIsPreferredOverTheRelay()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, /*relayOnly=*/false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);
        QCOMPARE(pair.stationRelays, 1);
        QCOMPARE(pair.clientRelays, 1);
        QTRY_VERIFY_WITH_TIMEOUT(pair.offerer()->selectedPath().has_value()
                                     && !pair.offerer()->selectedPath()->relayed(),
                                 15000);
    }

    // A full relay (TURN 486, Allocation Quota Reached) is an ordinary
    // outcome: gathering finishes without it on both ends and the
    // connection goes on directly.
    void aFullRelayIsNotFatal()
    {
        LocalService service;
        service.setRelayFull(true);
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientGathered && pair.stationGathered, 30000);
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("QUOTA 486")), 5000);
        QVERIFY(!service.turnOutput().contains(QLatin1String("ALLOCATED")));
        for (const QString& candidate : pair.clientCandidates + pair.stationCandidates) {
            QVERIFY(IceConfiguration::candidateType(candidate) != QLatin1String("relay"));
        }
        QVERIFY(!pair.offerer()->selectedPath()->relayed());
    }

    // Only relay candidates are passed on, both ways, and the connection
    // works through the relay. This asserts what a loopback test can prove
    // (the follow-up to the Task 27 re-review): each end allocated on the
    // relay, the relay carried checks both ways (at least one relay pair's
    // checks crossed it), and the message arrived. It does not assert the
    // relayed pair was the one chosen: on the loopback nothing blocks a
    // direct packet, and libjuice pairs a peer-reflexive candidate learned
    // from a relayed check with the host socket, so a direct pair can form
    // and win. "Direct blocked means relayed" is the traversal harness's
    // udp-direct-blocked scenario, where packets really are dropped.
    void withOnlyRelayCandidatesTheRelayCarriesTheConnection()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);

        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, /*relayOnly=*/true);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 60000);
        QCOMPARE(pair.stationRelays, 1);
        QCOMPARE(pair.clientRelays, 1);
        QVERIFY(pair.offerer()->selectedPath().has_value());
        // Both ends allocated, and the relay carried checks both ways.
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("ALLOCATED 2")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("RELAYED OUT 1")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("RELAYED IN 1")), 5000);
        // The message arrives.
        QTRY_VERIFY_WITH_TIMEOUT(pair.offerer()->sendDisplay(QByteArrayLiteral("through the relay")),
                                 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pair.stationReceived.contains(QByteArrayLiteral("through the relay")),
                                 5000);
    }

    void relayDeniedAsksForNoCredentials()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/false);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QSignalSpy credentials(rendezvous.client(), &RendezvousClient::credentialsReceived);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        client.setServers({service.url()});
        QSignalSpy answers(&client, &RendezvousClient::answerReceived);
        IcePair pair(rendezvous.client(), &client, false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);
        QCOMPARE(credentials.size(), 0);
        QCOMPARE(answers.size(), 1);
        QCOMPARE(answers.at(0).at(1).toBool(), false);
        QVERIFY(!pair.offerer()->selectedPath()->relayed());
    }

    void unpairedAndRevokedDevicesGetNoAnswerAndAreCounted()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto stranger = makeKey();
        auto revoked = makeKey();
        QVERIFY(core.pair(*revoked));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QSignalSpy introduced(rendezvous.client(), &RendezvousClient::introduced);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        QVERIFY(core.server->deviceStore()->remove(StationIdentity::fingerprintOf(revoked->spki())));

        const QString offer = readText(kSuite + QStringLiteral("/sdp/offer.sdp"));
        for (const auto& key : {stranger, revoked}) {
            RendezvousClient client;
            client.setServers({service.url()});
            QSignalSpy answers(&client, &RendezvousClient::answerReceived);
            QSignalSpy errors(&client, &RendezvousClient::serviceError);
            const quint64 droppedBefore = rendezvous.client()->droppedIntroductions();
            client.introduce(rendezvous.client()->stationId(), key->spki(),
                             [key](const QByteArray& message) { return key->sign(message); },
                             offer);
            // The Core has decided once it counts the drop; an answer would
            // have been sent before that, and the service forwards in order.
            QTRY_COMPARE_WITH_TIMEOUT(rendezvous.client()->droppedIntroductions(),
                                      droppedBefore + 1, 10000);
            QCOMPARE(answers.size(), 0);
            // Silence, not `offline`: the service cannot tell whether a
            // device is paired (section 6.4).
            QCOMPARE(errors.size(), 0);
        }
        QCOMPARE(introduced.size(), 0);
        QCOMPARE(rendezvous.client()->droppedIntroductions(), quint64(2));
    }

    void pairingByCodeGoesThroughTheMailboxWithoutTheCode()
    {
        QVERIFY(SpakeExchange::isAvailable());
        LocalService service;
        QVERIFY(service.start());
        RecordingRelay relay(service.url());
        Core core;
        QVERIFY(core.server->pairingWindow()->isOpen());
        StationRendezvous rendezvous(core.server.get(), {relay.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy nameplates(rendezvous.client(), &RendezvousClient::nameplateClaimed);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(nameplates.size(), 1, 10000);
        // The code the Core shows carries the nameplate the service gave it.
        const int nameplate = nameplates.at(0).at(0).toInt();
        QTRY_VERIFY(core.server->pairingWindow()->currentCode().startsWith(
            QString::number(nameplate) + QLatin1Char('-')));
        const QString code = core.server->pairingWindow()->currentCode();

        QTemporaryDir keyDir;
        auto identity = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        StationPairingClient pairing(identity, QStringLiteral("Shack MacBook"));
        QSignalSpy paired(&pairing, &StationPairingClient::paired);
        QSignalSpy failed(&pairing, &StationPairingClient::failed);
        pairing.pairByCodeFromAnywhere(code, {relay.url()});
        QTRY_VERIFY_WITH_TIMEOUT(!paired.isEmpty() || !failed.isEmpty(), 60000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed.at(0).at(0).toString()));
        const PairedStationRecord record = paired.at(0).at(0).value<PairedStationRecord>();
        QCOMPARE(record.identityKey, core.server->stationIdentity().publicKeySpki());
        QVERIFY(core.server->deviceStore()->find(identity->fingerprint()).has_value());
        // The first pairing claims the Core and closes its window; the
        // nameplate goes back.
        QTRY_VERIFY(!core.server->pairingWindow()->isOpen());

        // What the service was given: pairing messages, forwarded
        // unchanged, and never the code.
        QStringList sent;
        QStringList delivered;
        for (const QString& text : std::as_const(relay.toService)) {
            const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
            if (message.value(QStringLiteral("type")).toString() == QLatin1String("mailbox")) {
                sent.append(message.value(QStringLiteral("body")).toString());
            }
        }
        for (const QString& text : std::as_const(relay.fromService)) {
            const QJsonObject message = QJsonDocument::fromJson(text.toUtf8()).object();
            if (message.value(QStringLiteral("type")).toString() == QLatin1String("mailbox")) {
                delivered.append(message.value(QStringLiteral("body")).toString());
            }
        }
        QVERIFY(sent.size() >= 5);
        QCOMPARE(delivered, sent);
        for (const QString& body : std::as_const(sent)) {
            QVERIFY(QJsonDocument::fromJson(body.toUtf8())
                        .object()
                        .value(QStringLiteral("type"))
                        .toString()
                        .startsWith(QLatin1String("pair.")));
        }
        const QStringList words = code.split(QLatin1Char('-')).mid(1);
        QCOMPARE(words.size(), 2);
        for (const QString& text : relay.toService + relay.fromService) {
            QVERIFY(!text.contains(code));
            for (const QString& word : words) {
                QVERIFY(!text.contains(word, Qt::CaseInsensitive));
            }
        }
        // The service's own log records the mailbox (never its bodies).
        QTRY_VERIFY_WITH_TIMEOUT(service.log().contains(QLatin1String("mailbox opened")), 5000);
        QVERIFY(!service.log().contains(code));

        // Task 28 fix wave (privacy): no device name and no Core label in
        // the clear. The plain pair.start names the computer only
        // "Computer"; its own name went sealed, and that is the name the
        // Core recorded. The Core's label goes only in its sealed box.
        const QString label = core.server->devicesFacade()->stationLabel();
        QStringList secrets{QStringLiteral("Shack MacBook"), QStringLiteral("KG4VCF")};
        if (!label.isEmpty()) {
            secrets.append(label);
        }
        for (const QString& text : relay.toService + relay.fromService) {
            for (const QString& secret : std::as_const(secrets)) {
                QVERIFY2(!text.contains(secret, Qt::CaseInsensitive), qPrintable(secret));
            }
        }
        bool sawStart = false;
        for (const QString& body : std::as_const(sent)) {
            const QJsonObject message = QJsonDocument::fromJson(body.toUtf8()).object();
            if (message.value(QStringLiteral("type")).toString() == QLatin1String("pair.start")) {
                sawStart = true;
                QCOMPARE(message.value(QStringLiteral("device")).toObject()
                             .value(QStringLiteral("name")).toString(),
                         QStringLiteral("Computer"));
            }
            QVERIFY(message.value(QStringLiteral("type")).toString()
                    != QLatin1String("pair.accept"));
        }
        QVERIFY(sawStart);
        QCOMPARE(core.server->deviceStore()->find(identity->fingerprint())->name,
                 QStringLiteral("Shack MacBook"));
    }

    void aSessionOutlivesTheService()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        rendezvous.client()->setReconnectDelaysMs({200});
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QSignalSpy lost(rendezvous.client(), &RendezvousClient::connectionLost);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);

        service.stop();
        QTRY_VERIFY_WITH_TIMEOUT(lost.size() >= 1, 10000);
        // The connection the service introduced carries on both ways.
        QTRY_VERIFY_WITH_TIMEOUT(pair.offerer()->sendDisplay(QByteArrayLiteral("after the stop")), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pair.stationReceived.contains(QByteArrayLiteral("after the stop")),
                                 5000);
        QVERIFY(pair.answerer()->sendDisplay(QByteArrayLiteral("and back")));
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReceived.contains(QByteArrayLiteral("and back")), 5000);
        QVERIFY(pair.offerer()->isReady());
        QVERIFY(pair.answerer()->isReady());

        // The Core registers again once the service is back.
        QVERIFY(service.launch());
        QTRY_VERIFY_WITH_TIMEOUT(registered.size() >= 2, 20000);
        QVERIFY(pair.offerer()->isReady());
    }

    void ipv6IsPreferredWhenBothEndsHaveIt()
    {
        if (!hasUsableIpv6()) {
            QSKIP("This computer has no IPv6 address beyond link-local and loopback.");
        }
        LocalService service;
        QVERIFY(service.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        client.setServers({service.url()});
        IcePair pair(rendezvous.client(), &client, false);
        pair.start(rendezvous.client()->stationId(), phone);
        QTRY_VERIFY_WITH_TIMEOUT(pair.clientReady && pair.stationReady, 30000);
        const auto hasFamily = [](const QStringList& candidates, QAbstractSocket::NetworkLayerProtocol family) {
            for (const QString& candidate : candidates) {
                const QStringList fields = candidate.split(QLatin1Char(' '));
                if (QHostAddress(fields.value(4)).protocol() == family) {
                    return true;
                }
            }
            return false;
        };
        // Both ends gathered IPv6 host candidates (and IPv4 ones).
        QVERIFY(hasFamily(pair.clientCandidates, QAbstractSocket::IPv6Protocol));
        QVERIFY(hasFamily(pair.stationCandidates, QAbstractSocket::IPv6Protocol));
        QVERIFY(hasFamily(pair.clientCandidates, QAbstractSocket::IPv4Protocol));
        // Nomination settles on the highest priority pair; wait for it.
        QTRY_VERIFY_WITH_TIMEOUT(pair.offerer()->selectedPath().has_value()
                                     && QHostAddress(pair.offerer()->selectedPath()->localAddress)
                                            .protocol() == QAbstractSocket::IPv6Protocol,
                                 10000);
        QCOMPARE(QHostAddress(pair.offerer()->selectedPath()->remoteAddress).protocol(),
                 QAbstractSocket::IPv6Protocol);
    }

    // ── The desktop: cached addresses and the attempt record ───────────

    // The pairing design, section 5.3: the cached address first, so a
    // reconnect never needs the remote access service; none is running
    // here at all.
    void aCachedAddressConnectsWithoutTheService()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("No TLS backend");
        }
        Core core;
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        window.setCachedAddresses({core.url()});
        // The saved address is one nothing answers at: it is never needed.
        const QUrl saved(QStringLiteral("wss://127.0.0.1:%1").arg(freeTcpPort()));
        window.connectToStation(saved, QString(), QString(), false,
                                core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QCOMPARE(window.connectedUrl(), core.url());
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 1);
        QCOMPARE(attempt.tries.at(0).path, StationConnectionAttempt::Path::ThisNetwork);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::Connected);
        QVERIFY(attempt.connected());
        QCOMPARE(attempt.summary(),
                 QStringLiteral("Tried this network (127.0.0.1:%1): connected.")
                     .arg(core.server->serverPort()));
        window.disconnectFromStation(QStringLiteral("test done"));
    }

    // A cached address nothing answers at, and one another computer
    // answers at, each give way to the next address at once.
    void aDeadOrForeignCachedAddressGivesWay()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("No TLS backend");
        }
        Core core;
        Core other;
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QVERIFY(other.server->listen(QHostAddress::LocalHost, 0));
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        const QUrl dead(QStringLiteral("wss://127.0.0.1:%1").arg(freeTcpPort()));
        window.setCachedAddresses({dead, other.url()});
        window.connectToStation(core.url(), QString(), QString(), false,
                                core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QCOMPARE(window.connectedUrl(), core.url());
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 3);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::NoAnswer);
        QCOMPARE(attempt.tries.at(1).outcome, StationConnectionAttempt::Outcome::NotThisCore);
        QCOMPARE(attempt.tries.at(2).outcome, StationConnectionAttempt::Outcome::Connected);
        QVERIFY(attempt.summary().contains(QLatin1String("no answer")));
        QVERIFY(attempt.summary().contains(QLatin1String("another computer answered")));
        // The other Core was told nothing: this computer sent no sign-in.
        QVERIFY(!other.server->hasAuthenticatedSession());
        QCOMPARE(window.lastEndReport().kind, StationEndReport::Kind::None);
        window.disconnectFromStation(QStringLiteral("test done"));
    }

    // A cached address that takes the connection and then never opens
    // gives way after the short open time.
    void aCachedAddressThatNeverOpensGivesWay()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("No TLS backend");
        }
        Core core;
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QTcpServer silent;
        QVERIFY(silent.listen(QHostAddress::LocalHost, 0));
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        window.setCachedAddresses({QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(silent.serverPort()))});
        QCOMPARE(window.cachedAddressOpenTimeoutMs(), StationClient::kCachedAddressOpenTimeoutMs);
        // The open time shortened through the injectable value, as the
        // handshake deadline tests do; nothing here waits the real 4 s.
        window.setCachedAddressOpenTimeoutMs(250);
        QElapsedTimer clock;
        clock.start();
        window.connectToStation(core.url(), QString(), QString(), false,
                                core.server->stationIdentity().fingerprint());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QVERIFY(clock.elapsed() >= 250 - 50);
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 2);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::TimedOut);
        QCOMPARE(attempt.tries.at(1).outcome, StationConnectionAttempt::Outcome::Connected);
        window.disconnectFromStation(QStringLiteral("test done"));
    }

    // Fix wave I1 (b): a Core saved with "connect without a certificate
    // fingerprint" has nothing that proves who answers, so its token never
    // goes to an address the operator did not type: no cached address is
    // dialled at all.
    void anUnpinnedCoreNeverDialsACachedAddress()
    {
        QTcpServer trap;
        QVERIFY(trap.listen(QHostAddress::LocalHost, 0));
        QSignalSpy trapped(&trap, &QTcpServer::newConnection);
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setCachedAddresses({QUrl(QStringLiteral("wss://127.0.0.1:%1").arg(trap.serverPort()))});
        QSignalSpy ended(&window, &StationClient::sessionEnded);
        const QUrl typed(QStringLiteral("wss://127.0.0.1:%1").arg(freeTcpPort()));
        window.connectToStation(typed, QStringLiteral("secret-token"), QString(),
                                /*allowUnpinned=*/true);
        QCOMPARE(window.connectionAttempt().tries.size(), 1);
        QCOMPARE(window.connectionAttempt().tries.at(0).address,
                 QStringLiteral("127.0.0.1:%1").arg(typed.port()));
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 10000);
        QCOMPARE(window.connectionAttempt().tries.size(), 1);
        QCOMPARE(trapped.size(), 0);
        window.disconnectFromStation(QStringLiteral("test done"));
    }

    // Fix wave I1 (c): a pinned Core whose cached address now reaches
    // another computer (another certificate) gives way to the next address
    // with the token unsent, as a paired Core does on identityChanged; the
    // other computer shares the token here, so a sent token would have
    // signed it in.
    void aPinMismatchAtACachedAddressGivesWayWithTheTokenUnsent()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("No TLS backend");
        }
        Core core(/*upgraded=*/true);
        Core other(/*upgraded=*/true, core.securityDir.path());
        QVERIFY(!core.server->token().isEmpty());
        QCOMPARE(other.server->token(), core.server->token());
        QVERIFY(core.server->certificateFingerprint() != other.server->certificateFingerprint());
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QVERIFY(other.server->listen(QHostAddress::LocalHost, 0));

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setCachedAddresses({other.url()});
        QSignalSpy endedEarly(&window, &StationClient::sessionEnded);
        window.connectToStation(core.url(), core.server->token(),
                                core.server->certificateFingerprint(), false);
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QCOMPARE(window.connectedUrl(), core.url());
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 2);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::NotThisCore);
        QCOMPARE(attempt.tries.at(1).outcome, StationConnectionAttempt::Outcome::Connected);
        QVERIFY(!other.server->hasAuthenticatedSession());
        QCOMPARE(window.lastEndReport().kind, StationEndReport::Kind::None);
        // Giving way is not an end: nothing told the window it stopped.
        QCOMPARE(endedEarly.size(), 0);
        window.disconnectFromStation(QStringLiteral("test done"));

        // At the last address the plan has, a mismatch still ends the
        // attempt, with no retry, as before.
        RadioModel remoteAlone(RadioModel::Role::Remote);
        SettingsProxy proxyAlone;
        StationClient alone(&remoteAlone, &proxyAlone);
        QSignalSpy ended(&alone, &StationClient::sessionEnded);
        alone.connectToStation(other.url(), core.server->token(),
                               core.server->certificateFingerprint(), false);
        QTRY_VERIFY_WITH_TIMEOUT(!ended.isEmpty(), 10000);
        QCOMPARE(ended.first().first().toString(),
                 QStringLiteral("Station certificate fingerprint does not match the saved pin."));
        QVERIFY(!alone.isReconnectPending());
        QVERIFY(!other.server->hasAuthenticatedSession());
    }

    // The follow-up to the Task 27 re-review (new Minor 3): the other path
    // to the same refusal. The other computer's certificate is one the
    // trust store accepts (a certificate for 127.0.0.1 from an authority
    // added to it), so the handshake raises no TLS error and the mismatch
    // is caught when the socket connects (StationClient's connected()
    // handler), not in the sslErrors handler. It still gives way to the
    // next address with the token unsent.
    void aPinMismatchOnATrustedCertificateGivesWayWithTheTokenUnsent()
    {
        if (!QSslSocket::supportsSsl()) {
            QSKIP("No TLS backend");
        }
        Core core(/*upgraded=*/true);
        Core other(/*upgraded=*/true, core.securityDir.path());
        other.server.reset();
        const QSslCertificate trusted = writeLoopbackCertificate(other.securityDir.path());
        QVERIFY(!trusted.isNull());
        other.server = std::make_unique<StationServer>(other.model.get(), *other.settings,
                                                       other.securityDir.path());
        other.server->setHeartbeatIntervalMs(0);
        QCOMPARE(other.server->token(), core.server->token());
        QVERIFY(core.server->certificateFingerprint() != other.server->certificateFingerprint());
        QVERIFY(core.server->listen(QHostAddress::LocalHost, 0));
        QVERIFY(other.server->listen(QHostAddress::LocalHost, 0));
        const TrustedAuthority authority(trusted);

        // The trust store accepts the other computer: a handshake to it
        // raises no TLS error.
        {
            QWebSocket probe;
            QSignalSpy errors(&probe, &QWebSocket::sslErrors);
            QSignalSpy opened(&probe, &QWebSocket::connected);
            probe.open(other.url());
            QTRY_VERIFY_WITH_TIMEOUT(!opened.isEmpty(), 10000);
            QCOMPARE(errors.size(), 0);
            probe.abort();
        }

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setCachedAddresses({other.url()});
        QSignalSpy endedEarly(&window, &StationClient::sessionEnded);
        window.connectToStation(core.url(), core.server->token(),
                                core.server->certificateFingerprint(), false);
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 20000);
        QCOMPARE(window.connectedUrl(), core.url());
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 2);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::NotThisCore);
        QCOMPARE(attempt.tries.at(1).outcome, StationConnectionAttempt::Outcome::Connected);
        QVERIFY(!other.server->hasAuthenticatedSession());
        QCOMPARE(endedEarly.size(), 0);
        window.disconnectFromStation(QStringLiteral("test done"));
    }

    void pathsAreNamedForTheOperator()
    {
        QCOMPARE(StationConnectionAttempt::pathFor(QUrl(QStringLiteral("wss://127.0.0.1:47910"))),
                 StationConnectionAttempt::Path::ThisNetwork);
        QCOMPARE(StationConnectionAttempt::pathFor(QUrl(QStringLiteral("wss://shack.local:47910"))),
                 StationConnectionAttempt::Path::ThisNetwork);
        QCOMPARE(StationConnectionAttempt::pathFor(QUrl(QStringLiteral("wss://shack.example.net:47910"))),
                 StationConnectionAttempt::Path::Direct);
        QCOMPARE(StationConnectionAttempt::pathFor(QUrl(QStringLiteral("wss://[2001:db8::7]:47910"))),
                 StationConnectionAttempt::Path::Direct);
        StationConnectionAttempt attempt;
        QCOMPARE(attempt.summary(), QString());
        attempt.tries.append({StationConnectionAttempt::Path::ThisNetwork,
                              QStringLiteral("192.168.1.20:47910"),
                              StationConnectionAttempt::Outcome::NoAnswer});
        attempt.tries.append({StationConnectionAttempt::Path::Direct,
                              QStringLiteral("shack.example.net:47910"),
                              StationConnectionAttempt::Outcome::TimedOut});
        attempt.tries.append({StationConnectionAttempt::Path::Relay, QStringLiteral("rv.nereussdr.com"),
                              StationConnectionAttempt::Outcome::Connected});
        QCOMPARE(attempt.summary(),
                 QStringLiteral("Tried this network (192.168.1.20:47910): no answer; direct "
                                "(shack.example.net:47910): no answer in time; relay "
                                "(rv.nereussdr.com): connected."));
    }

    // The saved Core keeps where it was reached, most recent first, at
    // most four, and a record from before them still loads.
    void theSavedCoreKeepsItsLastGoodAddresses()
    {
        QTemporaryDir dir;
        AppSettings settings(dir.filePath(QStringLiteral("NereusSDR.settings")));
        CoreTargetStore store(settings);
        QVERIFY(store.load());
        SavedCoreTarget target;
        target.id = CoreTargetStore::createId();
        target.label = QStringLiteral("KG4VCF/shack");
        target.connection.url = QStringLiteral("wss://shack.example.net:47910");
        target.connection.identityFingerprint = QByteArray(32, '\x07');
        QVERIFY(store.upsert(target));
        QVERIFY(!settings.value(QStringLiteral("ConnectionTargets/V2")).toString().contains(
            QLatin1String("lastAddresses")));

        for (int port = 1; port <= 5; ++port) {
            QVERIFY(store.rememberAddress(target.id,
                                          QStringLiteral("wss://192.168.1.%1:47910").arg(port)));
        }
        QVERIFY(store.rememberAddress(target.id, QStringLiteral("wss://192.168.1.3:47910")));
        QVERIFY(!store.rememberAddress(target.id, QStringLiteral("http://192.168.1.9")));
        const QStringList expected{QStringLiteral("wss://192.168.1.3:47910"),
                                   QStringLiteral("wss://192.168.1.5:47910"),
                                   QStringLiteral("wss://192.168.1.4:47910"),
                                   QStringLiteral("wss://192.168.1.2:47910")};
        QCOMPARE(store.target(target.id)->connection.cachedAddresses, expected);

        CoreTargetStore reloaded(settings);
        QVERIFY(reloaded.load());
        QCOMPARE(reloaded.target(target.id)->connection.cachedAddresses, expected);
        QCOMPARE(reloaded.target(target.id)->connection.url, target.connection.url);
    }

    // ── Task 28: the control session through the service ─────────────

    // A paired computer reaches the Core through the service: the Core
    // answers the introduction with a control connection presenting its own
    // certificate, the whole session runs over it, the introduction is
    // retired as the connection opens, and when the session ends both ends
    // give their relay allocations back.
    void aSessionRunsThroughTheServiceAndGivesTheRelayBack()
    {
        LocalService service;
        QVERIFY(service.start());
        Core core;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        QVERIFY(core.pairComputer(*key));
        StationRendezvous rendezvous(core.server.get(), {service.url()}, /*relayAllowed=*/true);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QSignalSpy introduced(rendezvous.client(), &RendezvousClient::introduced);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);

        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        window.setDeviceIdentity(key, QStringLiteral("Shack MacBook"));
        QSignalSpy ended(&window, &StationClient::sessionEnded);
        window.connectThroughService({service.url()}, rendezvous.client()->stationId(),
                                     core.server->stationIdentity().fingerprint());
        QVERIFY(window.isConnectionActive());
        QTRY_VERIFY_WITH_TIMEOUT(window.isHandshakeComplete(), 60000);
        QCOMPARE(ended.size(), 0);
        QCOMPARE(introduced.size(), 1);
        QVERIFY(core.server->hasAuthenticatedSession());
        // Retired here, not at the service's 120 s.
        QCOMPARE(rendezvous.client()->liveIntroductions(), 0);
        QCOMPARE(rendezvous.pendingAnswers(), 0);
        // What the attempt record shows, and the settings media reuses.
        const StationConnectionAttempt attempt = window.connectionAttempt();
        QCOMPARE(attempt.tries.size(), 1);
        QCOMPARE(attempt.tries.at(0).outcome, StationConnectionAttempt::Outcome::Connected);
        const std::optional<IceConfiguration> ice = window.sessionIceConfiguration();
        QVERIFY(ice.has_value());
        QVERIFY(ice->relayKnown());
        QCOMPARE(ice->relayServers().size(), 1);
        const quint64 epoch = core.server->mediaSessionEpoch();
        QVERIFY(core.server->sessionIceConfiguration(epoch).has_value());
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("ALLOCATED 2")),
                                 10000);

        window.disconnectFromStation(QStringLiteral("test done"));
        QTRY_VERIFY_WITH_TIMEOUT(!core.server->hasAuthenticatedSession(), 10000);
        // Both allocations, the Core's and the computer's, given back at
        // once rather than held for their lifetime.
        QTRY_VERIFY_WITH_TIMEOUT(service.turnOutput().contains(QLatin1String("RELEASED 2")),
                                 15000);
    }

    // The Core answered with the relay allowed and the credentials never
    // came: after the bound it gathers without the relay, and an answer
    // whose connection never opens is retired at its deadline.
    void anAnswerWithoutCredentialsGathersAndIsRetired()
    {
        ServicePlayer player;
        Core core;
        auto device = makeKey();
        QVERIFY(core.pair(*device));
        StationRendezvous rendezvous(core.server.get(), {player.url()}, /*relayAllowed=*/true);
        rendezvous.setCredentialsTimeoutMs(200);
        rendezvous.setAnswerDeadlineMs(1500);
        QVERIFY(rendezvous.start());
        QWebSocket* station = player.waitForConnection();
        QVERIFY(station != nullptr);
        const auto send = [station](const QJsonObject& message) {
            station->sendTextMessage(compact(message));
        };
        send({{"type", "hello"}, {"version", 1}, {"nonce", b64(randomBytes(32))},
              {"stun", QJsonArray()}});
        QVERIFY(player.waitForMessage(station).has_value());  // register
        send({{"type", "challenge"}, {"nonce", b64(randomBytes(32))}});
        QVERIFY(player.waitForMessage(station).has_value());  // prove
        const QString id = Wire::rendezvousId(core.server->stationIdentity().publicKeySpki());
        send({{"type", "registered"}, {"id", id}});
        QTRY_VERIFY(rendezvous.client()->isRegistered());

        // A real control offer, from a device's end.
        DataChannelTransport offerer;
        QSignalSpy offered(&offerer, &DataChannelTransport::localDescription);
        DataChannelTransport::Options options;
        options.role = DataChannelTransport::Role::Offerer;
        options.maxIncomingBytes = StationClient::kMaxIncomingMessageBytes;
        options.ice = IceConfiguration::throughRendezvous({}, true, AddressFamilies{},
                                                          HostFamilies{});
        QVERIFY(offerer.start(options));
        QTRY_COMPARE(offered.size(), 1);
        const QByteArray nonce = randomBytes(32);
        const QByteArray intro = randomBytes(16);
        send({{"type", "introduction"}, {"from", b64(intro)},
              {"device", b64(StationIdentity::fingerprintOf(device->spki()))},
              {"deviceSignature", b64(device->sign(Wire::introduceTranscript(id, nonce)))},
              {"offer", offered.at(0).at(0).toString()}, {"nonce", b64(nonce)}});

        const std::optional<QString> answer = player.waitForMessage(station);
        QVERIFY(answer.has_value());
        const QJsonObject answerObject = QJsonDocument::fromJson(answer->toUtf8()).object();
        QCOMPARE(answerObject.value("type").toString(), QStringLiteral("answer"));
        QCOMPARE(answerObject.value("turn").toBool(), true);
        QCOMPARE(rendezvous.pendingAnswers(), 1);
        // No credentials: the Core gathers anyway and ends its candidates.
        bool ended = false;
        QDeadlineTimer deadline(5000);
        while (!ended && !deadline.hasExpired()) {
            const std::optional<QString> next = player.waitForMessage(station, 200);
            if (next) {
                const QJsonObject message = QJsonDocument::fromJson(next->toUtf8()).object();
                ended = message.value("type").toString() == QStringLiteral("candidate")
                    && message.value("candidate").toString().isEmpty();
            }
        }
        QVERIFY2(ended, "the Core never ended its candidates without the credentials");
        // Nothing connects (the device's end never hears the answer): the
        // introduction is retired at the deadline and its place is free.
        QTRY_COMPARE_WITH_TIMEOUT(rendezvous.pendingAnswers(), 0, 5000);
        QCOMPARE(rendezvous.client()->liveIntroductions(), 0);
        // The same introduction handed back is dropped.
        QSignalSpy introducedAgain(rendezvous.client(), &RendezvousClient::introduced);
        send({{"type", "introduction"}, {"from", b64(intro)},
              {"device", b64(StationIdentity::fingerprintOf(device->spki()))},
              {"deviceSignature", b64(device->sign(Wire::introduceTranscript(id, nonce)))},
              {"offer", offered.at(0).at(0).toString()}, {"nonce", b64(nonce)}});
        QVERIFY(player.silentFor(station, 300));
        QCOMPARE(introducedAgain.size(), 0);
        rendezvous.client()->stop();
    }

    // Task 28 fix wave (review Minor 1): the Core leaving the service, or
    // this computer losing its connection to it, ends a dial at once
    // rather than at kDialDeadlineMs (79 s).
    void aDialEndsAtOnceWhenItsIntroductionEnds_data()
    {
        QTest::addColumn<bool>("connectionLost");
        QTest::newRow("stationLeft") << false;
        QTest::newRow("connectionLost") << true;
    }

    void aDialEndsAtOnceWhenItsIntroductionEnds()
    {
        QFETCH(bool, connectionLost);
        ServicePlayer player;
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        RendezvousDialer dialer;
        QSignalSpy failed(&dialer, &RendezvousDialer::failed);
        dialer.dial({player.url()}, QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaa"), key);
        QWebSocket* service = player.waitForConnection();
        QVERIFY(service != nullptr);
        service->sendTextMessage(compact(QJsonObject{{"type", "hello"}, {"version", 1},
                                                     {"nonce", b64(randomBytes(32))},
                                                     {"stun", QJsonArray()}}));
        const std::optional<QString> introduce = player.waitForMessage(service);
        QVERIFY(introduce.has_value());
        QCOMPARE(QJsonDocument::fromJson(introduce->toUtf8()).object().value("type").toString(),
                 QStringLiteral("introduce"));
        QElapsedTimer elapsed;
        elapsed.start();
        if (connectionLost) {
            service->close();
        } else {
            service->sendTextMessage(
                compact(QJsonObject{{"type", "introduction.end"}, {"code", "stationLeft"}}));
        }
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QVERIFY(elapsed.elapsed() < 5000);
        QCOMPARE(failed.at(0).at(0).toString(),
                 QStringLiteral("The Core could not be reached from here."));
        QVERIFY(NereusSDR::OperatorWording::isPlain(failed.at(0).at(0).toString()));
    }

    // Task 28 fix wave (review Minor 1): the device leaving before its
    // connection opened frees the Core's answer at once (its peer
    // connection and any relay allocation), not at kAnswerDeadlineMs.
    void anUnopenedAnswerIsFreedWhenTheDeviceLeaves()
    {
        ServicePlayer player;
        Core core;
        auto device = makeKey();
        QVERIFY(core.pair(*device));
        StationRendezvous rendezvous(core.server.get(), {player.url()}, /*relayAllowed=*/false);
        QVERIFY(rendezvous.start());
        QWebSocket* station = player.waitForConnection();
        QVERIFY(station != nullptr);
        const auto send = [station](const QJsonObject& message) {
            station->sendTextMessage(compact(message));
        };
        send({{"type", "hello"}, {"version", 1}, {"nonce", b64(randomBytes(32))},
              {"stun", QJsonArray()}});
        QVERIFY(player.waitForMessage(station).has_value());  // register
        send({{"type", "challenge"}, {"nonce", b64(randomBytes(32))}});
        QVERIFY(player.waitForMessage(station).has_value());  // prove
        const QString id = Wire::rendezvousId(core.server->stationIdentity().publicKeySpki());
        send({{"type", "registered"}, {"id", id}});
        QTRY_VERIFY(rendezvous.client()->isRegistered());

        DataChannelTransport offerer;
        QSignalSpy offered(&offerer, &DataChannelTransport::localDescription);
        DataChannelTransport::Options options;
        options.role = DataChannelTransport::Role::Offerer;
        options.maxIncomingBytes = StationClient::kMaxIncomingMessageBytes;
        options.ice = IceConfiguration::throughRendezvous({}, false, AddressFamilies{},
                                                          HostFamilies{});
        QVERIFY(offerer.start(options));
        QTRY_COMPARE(offered.size(), 1);
        const QByteArray nonce = randomBytes(32);
        const QByteArray intro = randomBytes(16);
        send({{"type", "introduction"}, {"from", b64(intro)},
              {"device", b64(StationIdentity::fingerprintOf(device->spki()))},
              {"deviceSignature", b64(device->sign(Wire::introduceTranscript(id, nonce)))},
              {"offer", offered.at(0).at(0).toString()}, {"nonce", b64(nonce)}});
        const std::optional<QString> answer = player.waitForMessage(station);
        QVERIFY(answer.has_value());
        QCOMPARE(QJsonDocument::fromJson(answer->toUtf8()).object().value("type").toString(),
                 QStringLiteral("answer"));
        QCOMPARE(rendezvous.pendingAnswers(), 1);

        send({{"type", "introduction.end"}, {"from", b64(intro)}, {"code", "clientLeft"}});
        QTRY_COMPARE_WITH_TIMEOUT(rendezvous.pendingAnswers(), 0, 2000);
        QCOMPARE(rendezvous.client()->liveIntroductions(), 0);
        rendezvous.client()->stop();
    }

    // A client with no paired Core, or no key, is told plainly and nothing
    // is dialled.
    void connectingThroughTheServiceNeedsAPairedCore()
    {
        RadioModel remote(RadioModel::Role::Remote);
        SettingsProxy proxy;
        StationClient window(&remote, &proxy);
        QSignalSpy ended(&window, &StationClient::sessionEnded);
        window.connectThroughService({QUrl(QStringLiteral("ws://127.0.0.1:9/"))},
                                     QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaa"),
                                     QByteArray(32, 'x'));
        QCOMPARE(ended.size(), 1);
        QVERIFY(!window.isConnectionActive());
        QVERIFY(NereusSDR::OperatorWording::isPlain(ended.at(0).at(0).toString()));
        // The dialer's own words, and a service that is not there.
        RendezvousDialer noKey;
        QSignalSpy noKeyFailed(&noKey, &RendezvousDialer::failed);
        noKey.dial({QUrl(QStringLiteral("ws://127.0.0.1:9/"))},
                   QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaa"), nullptr);
        QTRY_COMPARE(noKeyFailed.size(), 1);
        QVERIFY(NereusSDR::OperatorWording::isPlain(noKeyFailed.at(0).at(0).toString()));
        QTemporaryDir keyDir;
        auto key = std::make_shared<const ClientDeviceIdentity>(
            ClientDeviceIdentity::loadOrCreate(keyDir.path()));
        RendezvousDialer nowhere;
        QSignalSpy nowhereFailed(&nowhere, &RendezvousDialer::failed);
        nowhere.dial({QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(freeTcpPort()))},
                     QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaa"), key);
        QTRY_COMPARE_WITH_TIMEOUT(nowhereFailed.size(), 1, 15000);
        QVERIFY(NereusSDR::OperatorWording::isPlain(nowhereFailed.at(0).at(0).toString()));
    }

    void aClientTriesTheNextServerWhenTheCoreIsNotOnTheFirst()
    {
        LocalService first(/*stun=*/false, /*relay=*/false);
        LocalService second(/*stun=*/false, /*relay=*/false);
        QVERIFY(first.start());
        QVERIFY(second.start());
        Core core;
        auto phone = makeKey();
        QVERIFY(core.pair(*phone));
        // The Core is registered only with the second server; the first
        // answers `offline`, and the client moves on down its list.
        StationRendezvous rendezvous(core.server.get(), {second.url()}, true);
        // Task 27's ICE tests answer the introduction themselves (IcePair).
        rendezvous.setAnswersIntroductionsForTest(false);
        QSignalSpy registered(rendezvous.client(), &RendezvousClient::registered);
        QSignalSpy introduced(rendezvous.client(), &RendezvousClient::introduced);
        QVERIFY(rendezvous.start());
        QTRY_COMPARE_WITH_TIMEOUT(registered.size(), 1, 10000);
        RendezvousClient client;
        // A server that is not running at all comes first of all.
        client.setServers({QUrl(QStringLiteral("ws://127.0.0.1:%1/").arg(freeTcpPort())),
                           first.url(), second.url()});
        client.introduce(rendezvous.client()->stationId(), phone->spki(),
                         [phone](const QByteArray& message) { return phone->sign(message); },
                         readText(kSuite + QStringLiteral("/sdp/offer.sdp")));
        QTRY_COMPARE_WITH_TIMEOUT(introduced.size(), 1, 20000);
        QCOMPARE(client.currentServer(), second.url());

        // Nowhere to be found: the client says so in plain words.
        RendezvousClient lonely;
        lonely.setServers({first.url()});
        QSignalSpy unreachable(&lonely, &RendezvousClient::unreachable);
        lonely.introduce(Wire::rendezvousId(makeKey()->spki()), phone->spki(),
                         [phone](const QByteArray& message) { return phone->sign(message); },
                         readText(kSuite + QStringLiteral("/sdp/offer.sdp")));
        QTRY_COMPARE_WITH_TIMEOUT(unreachable.size(), 1, 10000);
        QCOMPARE(unreachable.at(0).at(0).toString(),
                 QStringLiteral("The Core is not reachable right now. Check that it is running and "
                                "connected to the internet."));
    }
};

void TstRendezvousClient::runCoreFixture(const QString& file)
{
    const QJsonObject fixture = readJson(kSuite + QLatin1Char('/') + file);
    Context ctx;
    ctx.coreMode = true;
    ctx.setup(fixture.value(QStringLiteral("serverSetup")).toObject());
    ServicePlayer player;

    // The Core: its station identity key and the devices it paired before
    // the fixture starts, all made now.
    auto coreKey = makeKey();
    QHash<QByteArray, QByteArray> paired;
    for (const QJsonValue& name : fixture.value(QStringLiteral("pairedDevices")).toArray()) {
        const QByteArray spki = ctx.device(name.toString())->spki();
        paired.insert(StationIdentity::fingerprintOf(spki), spki);
    }
    RendezvousClient core;
    core.setServers({player.url()});
    QList<RendezvousIntroduction> introductions;
    connect(&core, &RendezvousClient::introduced, this,
            [&introductions](const RendezvousIntroduction& introduction) {
        introductions.append(introduction);
    });
    QWebSocket* station = nullptr;

    for (const QJsonValue& value : fixture.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject step = value.toObject();
        const QString where = file + QStringLiteral(": ") + compact(step).left(120);
        if (step.contains(QStringLiteral("connect"))) {
            if (step.value(QStringLiteral("connect")).toString() == QLatin1String("station")) {
                core.registerStation(coreKey->spki(),
                                     [coreKey](const QByteArray& message) { return coreKey->sign(message); },
                                     [&paired](const QByteArray& id) { return paired.value(id); });
                station = player.waitForConnection();
                QVERIFY2(station != nullptr, qPrintable(where));
            }
            continue;
        }
        if (step.contains(QStringLiteral("advanceMs"))) {
            ctx.advancedMs += step.value(QStringLiteral("advanceMs")).toInteger();
            continue;
        }
        if (step.contains(QStringLiteral("disconnect"))) {
            if (step.value(QStringLiteral("disconnect")).toString() == QLatin1String("station")) {
                core.stop();
            }
            continue;
        }
        if (step.contains(QStringLiteral("expectSilent"))) {
            QVERIFY2(station == nullptr || player.silentFor(station, 1000), qPrintable(where));
            continue;
        }
        if (step.contains(QStringLiteral("expectClosed"))) {
            if (step.value(QStringLiteral("expectClosed")).toString() == QLatin1String("station")
                && station != nullptr) {
                station->close(static_cast<QWebSocketProtocol::CloseCode>(
                    step.value(QStringLiteral("code")).toInt()));
                QVERIFY2(player.silentFor(station, 300), qPrintable(where));
            }
            continue;
        }
        const QString from = step.value(QStringLiteral("from")).toString();
        const QJsonObject message = step.value(QStringLiteral("message")).toObject();
        if (from == QLatin1String("server")) {
            const QJsonValue filled = ctx.fill(message);
            if (step.value(QStringLiteral("to")).toString() == QLatin1String("station")) {
                QVERIFY2(station != nullptr, qPrintable(where));
                station->sendTextMessage(compact(filled));
            }
            continue;
        }
        if (from != QLatin1String("station")
            || step.value(QStringLiteral("role")).toString() != QLatin1String("behaviour")) {
            // Another connection's message, or one a conformant Core never
            // sends: its placeholders are recorded only.
            ctx.fill(message);
            continue;
        }
        // Drive the Core to this behaviour, then match what it sends.
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("answer")) {
            QTRY_VERIFY2(!introductions.isEmpty(), qPrintable(where));
            QVERIFY2(core.answer(introductions.last().id, ctx.answerSdp), qPrintable(where));
        } else if (type == QLatin1String("candidate")) {
            QVERIFY2(!introductions.isEmpty(), qPrintable(where));
            QVERIFY2(core.sendCandidate(introductions.last().id,
                                        ctx.fill(message.value(QStringLiteral("candidate"))).toString()),
                     qPrintable(where));
        } else if (type == QLatin1String("nameplate.claim")) {
            core.claimNameplate();
        } else if (type == QLatin1String("nameplate.release")) {
            core.releaseNameplate();
        } else if (type == QLatin1String("mailbox")) {
            QTRY_VERIFY2(core.isMailboxOpen(), qPrintable(where));
            QVERIFY2(core.sendMailbox(message.value(QStringLiteral("body")).toString()), qPrintable(where));
        } else if (type == QLatin1String("mailbox.close")) {
            core.closeMailbox();
        }
        const std::optional<QString> sent = player.waitForMessage(station);
        QVERIFY2(sent.has_value(), qPrintable(where + QStringLiteral(": nothing sent")));
        QString why;
        const QJsonObject actual = QJsonDocument::fromJson(sent->toUtf8()).object();
        QVERIFY2(ctx.match(message, actual, &why), qPrintable(where + QStringLiteral(": ") + why));
    }
    // Nothing more from the Core.
    if (station != nullptr) {
        QVERIFY2(player.silentFor(station, 200), qPrintable(file));
    }
    core.stop();
}

void TstRendezvousClient::runAppFixture(const QString& file)
{
    const QJsonObject fixture = readJson(kSuite + QLatin1Char('/') + file);
    Context ctx;
    ctx.coreMode = false;
    ctx.setup(fixture.value(QStringLiteral("serverSetup")).toObject());
    ServicePlayer player;
    RendezvousClient client;
    client.setServers({player.url()});
    QSignalSpy connected(&client, &RendezvousClient::connected);
    QSignalSpy answers(&client, &RendezvousClient::answerReceived);
    QSignalSpy mailboxes(&client, &RendezvousClient::mailboxOpened);
    QSignalSpy closedMailboxes(&client, &RendezvousClient::mailboxClosed);
    QSignalSpy ends(&client, &RendezvousClient::introductionEnded);
    QSignalSpy errors(&client, &RendezvousClient::serviceError);
    QWebSocket* socket = nullptr;
    int expectedAnswers = 0;
    int expectedOpened = 0;
    int expectedClosed = 0;
    int expectedEnds = 0;
    int expectedErrors = 0;

    for (const QJsonValue& value : fixture.value(QStringLiteral("steps")).toArray()) {
        const QJsonObject step = value.toObject();
        const QString where = file + QStringLiteral(": ") + compact(step).left(120);
        if (step.contains(QStringLiteral("connect"))) {
            if (step.value(QStringLiteral("connect")).toString() == QLatin1String("client")) {
                client.connectToService();
                socket = player.waitForConnection();
                QVERIFY2(socket != nullptr, qPrintable(where));
            }
            continue;
        }
        if (step.contains(QStringLiteral("advanceMs"))) {
            ctx.advancedMs += step.value(QStringLiteral("advanceMs")).toInteger();
            continue;
        }
        if (step.contains(QStringLiteral("disconnect"))) {
            if (step.value(QStringLiteral("disconnect")).toString() == QLatin1String("client")) {
                client.stop();
            }
            continue;
        }
        if (step.contains(QStringLiteral("expectSilent"))) {
            QVERIFY2(socket == nullptr || player.silentFor(socket, 1000), qPrintable(where));
            continue;
        }
        if (step.contains(QStringLiteral("expectClosed"))) {
            if (step.value(QStringLiteral("expectClosed")).toString() == QLatin1String("client")
                && socket != nullptr) {
                socket->close(static_cast<QWebSocketProtocol::CloseCode>(
                    step.value(QStringLiteral("code")).toInt()));
                QVERIFY2(player.silentFor(socket, 300), qPrintable(where));
            }
            continue;
        }
        const QString from = step.value(QStringLiteral("from")).toString();
        const QJsonObject message = step.value(QStringLiteral("message")).toObject();
        if (from == QLatin1String("server")) {
            const QJsonValue filled = ctx.fill(message);
            if (step.value(QStringLiteral("to")).toString() == QLatin1String("client")) {
                QVERIFY2(socket != nullptr, qPrintable(where));
                socket->sendTextMessage(compact(filled));
                const QString type = message.value(QStringLiteral("type")).toString();
                if (type == QLatin1String("hello")) {
                    QTRY_VERIFY2(!connected.isEmpty(), qPrintable(where));
                } else if (type == QLatin1String("answer")) {
                    ++expectedAnswers;
                } else if (type == QLatin1String("mailbox.opened")) {
                    ++expectedOpened;
                } else if (type == QLatin1String("mailbox.closed")) {
                    ++expectedClosed;
                } else if (type == QLatin1String("introduction.end")) {
                    ++expectedEnds;
                } else if (type == QLatin1String("error")) {
                    ++expectedErrors;
                }
            }
            continue;
        }
        if (from != QLatin1String("client")
            || step.value(QStringLiteral("role")).toString() != QLatin1String("behaviour")) {
            ctx.fill(message);
            continue;
        }
        const QString type = message.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("introduce")) {
            // The Core it paired with, and its own device key.
            const QString station = message.value(QStringLiteral("id")).toString().split(QLatin1Char(':')).value(1);
            const QString deviceName =
                message.value(QStringLiteral("device")).toString().split(QLatin1Char(':')).value(1);
            const std::shared_ptr<TestKey> device = ctx.device(deviceName);
            client.introduce(Wire::rendezvousId(ctx.spkiOf(station)), device->spki(),
                             [device](const QByteArray& m) { return device->sign(m); }, ctx.offerSdp);
        } else if (type == QLatin1String("mailbox.open")) {
            client.openMailbox(message.value(QStringLiteral("nameplate")).toInt());
        } else if (type == QLatin1String("mailbox")) {
            QTRY_VERIFY2(client.isMailboxOpen(), qPrintable(where));
            QVERIFY2(client.sendMailbox(message.value(QStringLiteral("body")).toString()), qPrintable(where));
        } else if (type == QLatin1String("mailbox.close")) {
            client.closeMailbox();
        } else if (type == QLatin1String("candidate")) {
            QVERIFY2(client.sendCandidate(ctx.fill(message.value(QStringLiteral("candidate"))).toString()),
                     qPrintable(where));
        }
        const std::optional<QString> sent = player.waitForMessage(socket);
        QVERIFY2(sent.has_value(), qPrintable(where + QStringLiteral(": nothing sent")));
        QString why;
        const QJsonObject actual = QJsonDocument::fromJson(sent->toUtf8()).object();
        QVERIFY2(ctx.match(message, actual, &why), qPrintable(where + QStringLiteral(": ") + why));
    }
    // The client read every message the service sent it.
    QTRY_COMPARE(answers.size(), expectedAnswers);
    QTRY_COMPARE(mailboxes.size(), expectedOpened);
    QTRY_COMPARE(closedMailboxes.size(), expectedClosed);
    QTRY_COMPARE(ends.size(), expectedEnds);
    QTRY_COMPARE(errors.size(), expectedErrors);
    if (socket != nullptr) {
        QVERIFY2(player.silentFor(socket, 200), qPrintable(file));
    }
    client.stop();
}

QTEST_GUILESS_MAIN(TstRendezvousClient)
#include "tst_rendezvous_client.moc"
