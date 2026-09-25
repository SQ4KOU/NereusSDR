#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// tests/fakes/UpgradedCoreToken.h  (NereusSDR)
// =================================================================
//
// iPhone app plan Task 12: a new Core creates no pairing token; only a Core
// upgraded from before paired devices still has one (TokenStore.h). Tests
// whose window signs in with the token stand up exactly that Core: this
// writes a `station-token` into the security directory, the way the
// earlier Core left it (43 base64url characters of 32 random bytes from
// the operating system's generator, mode 0600), unless one is there
// already, and returns the directory so it can wrap the constructor
// argument. It also makes the Core's identity key (seedCoreIdentity), as
// the upgraded Core's first start did. Keys and tokens are made at run
// time; none is ever stored in the tree.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QRandomGenerator>
#include <QString>

#include "core/security/StationIdentity.h"

namespace NereusSDR::Test {

// The Core's identity key, made before the StationServer starts, so the
// server loads it and does not print its first-run banner into the test's
// output. Returns the directory.
inline QString seedCoreIdentity(const QString& directory)
{
    QDir().mkpath(directory);
    NereusSDR::StationIdentity::loadOrCreate(directory);
    return directory;
}

inline QString seedUpgradedCoreToken(const QString& directory)
{
    seedCoreIdentity(directory);
    const QString path = QDir(directory).filePath(QStringLiteral("station-token"));
    if (QFile::exists(path)) {
        return directory;
    }
    quint32 words[8]{};
    QRandomGenerator::system()->fillRange(words);
    const QByteArray token =
        QByteArray(reinterpret_cast<const char*>(words), sizeof(words))
            .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.write(token);
    }
    return directory;
}

} // namespace NereusSDR::Test
