#pragma once
// =================================================================
// src/core/session/RemoteStationOptions.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. Remote-daemon R2 Task 20.
//
// Where a GUI process gets its station address and token from, and the
// one rule for what counts as a usable station URL.
//
// Deliberately a plain value struct with no AppSettings dependency and no
// key literals. The keys live in the Setup page that owns the field group
// (src/gui/setup/CatNetworkSetupPages.cpp) and nowhere else, because
// tests/tst_settings_scope.cpp's completeness sweep asserts that every
// AppSettings key literal appearing in src/core or src/models classifies
// Station -- and the station URL and its token are the two keys in this
// tree that MUST NOT: they are the client's own address book, and writing
// them into the shared station store would hand one operator's
// credentials to every other client of the same daemon. Keeping the
// literals out of src/core keeps that from ever becoming a question.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-08-08  J.J. Boyd / KG4VCF  Remote daemon R2 Task 20: --station
//                                    and --token, and the remote-mode GUI
//                                    gate. AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-24  J.J. Boyd / KG4VCF  iPhone app Task 18 (R-IOS-08): the
//                                    paired Core's identity fingerprint.
//                                    AI-assisted transformation via
//                                    Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  iPhone app plan Task 27 (R-IOS-16):
//                                    the Core's last good addresses.
//                                    AI-assisted via Anthropic Claude
//                                    Code.
//   2026-09-26  J.J. Boyd / KG4VCF  iPhone app plan Task 28 fix wave
//                                    (R-IOS-16): the Core's recorded
//                                    controlChannelVersion. AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace NereusSDR {

/// The station a GUI process should drive, or an empty url for local
/// direct mode (the default, and the only mode before R2).
struct RemoteStationOptions {
    /// `wss://host:port` (or `ws://` for a loopback bench run). Empty
    /// means local direct mode: construct a Role::Local RadioModel and
    /// behave exactly as every release before R2 did.
    QString url;

    /// The daemon's shared token, as printed by `nereusd` on first run.
    /// Never generated here and never sent anywhere but the station named
    /// by `url`.
    QString token;

    /// SHA-256 fingerprint of the certificate to pin, or empty.
    QString fingerprint;

    /// Accept the station's self-signed certificate without a pinned
    /// fingerprint. Bench convenience; see StationClient::connectToStation.
    bool allowUnpinned = false;

    /// iPhone app Task 18 (R-IOS-08): the fingerprint (SHA-256 of the
    /// SubjectPublicKeyInfo DER, 32 bytes) of the Core identity key this
    /// computer paired with, or empty for a Core it has not. When set, the
    /// Core is trusted by that key: its hello must show it, the certificate
    /// binding must verify for the certificate this connection presents,
    /// and this computer signs in with its own device key. The pin and the
    /// token are then not used (the link document, sections 3.4 and 3.5).
    QByteArray identityFingerprint;

    /// iPhone app plan Task 27 (R-IOS-16; the pairing design, section 5.3):
    /// where this computer last reached the Core (station URLs, the most
    /// recent first, at most kMaxCachedAddresses), tried before `url` so a
    /// reconnect never needs the remote access service.
    static constexpr int kMaxCachedAddresses = 4;
    QStringList cachedAddresses;

    /// iPhone app plan Task 28 fix wave (R-IOS-16; the safety review's
    /// Important 5): the controlChannelVersion the Core sent at this
    /// computer's last sign-in, or -1 before any (a Core paired through a
    /// mailbox has had no session yet). Connecting from anywhere is offered
    /// to a Core that sent 1 or has none recorded, and refused with
    /// kUpdateCoreForServiceReason to one that sent 0
    /// (serviceConnectRefusal()).
    int controlChannelVersion = -1;
    static constexpr const char* kUpdateCoreForServiceReason =
        "Update the Core to reach it from anywhere.";
    /// Why connecting through the remote access service is not offered for
    /// this Core, in plain words, or empty when it is: a Core this
    /// computer has not paired with (it is introduced by its identity key),
    /// or one whose last session declared no control channel.
    QString serviceConnectRefusal() const
    {
        if (identityFingerprint.isEmpty()) {
            return QStringLiteral("Pair with the Core to reach it from anywhere.");
        }
        if (controlChannelVersion == 0) {
            return QString::fromLatin1(kUpdateCoreForServiceReason);
        }
        return QString();
    }

    /// True when this process should run as a remote client.
    bool isRemote() const { return !url.isEmpty(); }

    /// Whether `candidate` is a station URL this build can dial.
    ///
    /// Accepts ws:// and wss:// with a non-empty host. Rejects everything
    /// else, including http/https, which is the mistake worth catching by
    /// name: a QWebSocket handed an http:// URL fails at connect time with
    /// a message that does not say why.
    ///
    /// `whyNot`, when non-null, receives a one-line operator-facing reason
    /// on failure and is left untouched on success.
    static bool isValidStationUrl(const QString& candidate, QString* whyNot = nullptr);
};

} // namespace NereusSDR
