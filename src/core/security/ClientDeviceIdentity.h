#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/core/security/ClientDeviceIdentity.h  (NereusSDR)
// =================================================================
//
// This computer's own device key, as the desktop's remote window holds it
// (iPhone app plan Task 18, R-IOS-08; the pairing design,
// docs/architecture/2026-08-02-remote-station-identity-and-pairing-
// design.md sections 3.1 and 7: "The client generates its own key pair for
// the same reason, so a device is identified by what it holds rather than
// by what it claims").
//
// One ECDSA P-256 key pair, created the first time it is needed and kept
// in `device-identity.pem` (PKCS#8, PEM) in this profile's directory, mode
// 0600, written atomically; the same handling as the Core's own
// `station-identity.pem` (StationIdentity), in a file of its own, so a
// computer that also runs a Core never mixes the two. A key file that
// exists but cannot be read is never replaced: a new key is a new device,
// and every Core it was paired with would have to pair it again.
//
// The Core lists this computer as kind `computer`, named after the
// machine (machineName()), so nothing is typed.
//
// The private key never leaves the profile directory and is never logged.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/security/StationIdentity.h"

#include <QByteArray>
#include <QString>

#include <memory>

namespace NereusSDR {

class ClientDeviceIdentity {
public:
    /// The key file's name inside the profile directory.
    static constexpr const char* kKeyFileName = "device-identity.pem";
    /// The kind a desktop pairs and signs in as (the link document,
    /// sections 3.5 and 3.6).
    static constexpr const char* kKind = "computer";
    /// The longest device name the Core stores (DeviceStore), in UTF-8
    /// bytes.
    static constexpr int kMaxNameBytes = 64;

    /// An invalid identity (no key).
    ClientDeviceIdentity() = default;

    /// Loads `profileDir`/device-identity.pem, or creates it (mode 0600,
    /// atomically) when it does not exist. A file that exists but does not
    /// hold a P-256 private key gives an invalid identity whose lastError()
    /// says why; the file is left as it is.
    static ClientDeviceIdentity loadOrCreate(const QString& profileDir);

    /// This profile's key (AppSettings::resolveConfigDir of the profile
    /// in use), loaded or created once per process and shared after.
    static std::shared_ptr<const ClientDeviceIdentity> forThisProfile();

    bool isValid() const { return m_key.isValid(); }
    QString lastError() const { return m_key.lastError(); }
    QString keyPath() const { return m_key.keyPath(); }
    bool wasCreatedThisRun() const { return m_key.wasCreatedThisRun(); }

    /// SubjectPublicKeyInfo DER (91 bytes). Empty when invalid.
    QByteArray publicKeySpki() const { return m_key.publicKeySpki(); }
    /// SHA-256 of publicKeySpki(): the id a Core knows this computer by.
    QByteArray fingerprint() const { return m_key.fingerprint(); }
    /// ECDSA P-256 over SHA-256 of `message`, raw r || s (64 bytes).
    QByteArray sign(const QByteArray& message) const { return m_key.sign(message); }

    /// The name a Core lists this computer by: the machine's host name
    /// (deviceNameFrom(QSysInfo::machineHostName())).
    static QString machineName();
    /// `hostName` as a device name: a trailing ".local" dropped, the
    /// characters the Core refuses in a name dropped, trimmed, and cut to
    /// kMaxNameBytes UTF-8 bytes at a character boundary; "Computer" when
    /// nothing is left.
    static QString deviceNameFrom(const QString& hostName);

private:
    StationIdentity m_key;
};

} // namespace NereusSDR
