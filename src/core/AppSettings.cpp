// =================================================================
// src/core/AppSettings.cpp  (NereusSDR)
// =================================================================
//
// Ported from Thetis sources:
//   Project Files/Source/Console/database.cs, original licence from Thetis source is included below
//   AetherSDR src/core/AppSettings.{h,cpp} — AetherSDR has no per-file headers; project-level GPLv3 and contributor list per About dialog per https://github.com/ten9876/AetherSDR
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-18 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
//                 AppSettings XML persistence: key/value semantics (PascalCase keys, True/False string booleans, per-StationName nesting) port Thetis database.cs SaveVarsDictionary/RestoreVarsDictionary pattern; QXmlStream file I/O skeleton follows AetherSDR `src/core/AppSettings.{h,cpp}`.
// =================================================================

//=================================================================
// database.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2012  FlexRadio Systems
// Copyright (C) 2010-2020  Doug Wigley
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: gpl@flexradio.com.
// Paper mail may be sent to:
//    FlexRadio Systems
//    4616 W. Howard Lane  Suite 1-150
//    Austin, TX 78728
//    USA
//=================================================================
// Modifications to the database import function to allow using files created with earlier versions.
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines.
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

// Upstream source 'AetherSDR src/core/AppSettings.{h,cpp}' has no top-of-file header — project-level LICENSE applies.

#include "AppSettings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QDebug>

namespace NereusSDR {

AppSettings& AppSettings::instance()
{
    static AppSettings s;
    return s;
}

AppSettings::AppSettings()
{
    initFilePath();
}

AppSettings::AppSettings(const QString& filePath)
    : m_filePath(filePath)
{
    // Used by tests — no automatic load(); caller controls load/save lifecycle.
}

void AppSettings::initFilePath()
{
#ifdef Q_OS_MAC
    m_filePath = QDir::homePath() + "/Library/Preferences/NereusSDR/NereusSDR.settings";
#else
    m_filePath = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                 + "/NereusSDR/NereusSDR.settings";
#endif
}

// ---------------------------------------------------------------------------
// XML key encoding helpers
//
// XML element names cannot contain '/' or ':'.  Keys like
// "radios/aa:bb:cc/name" are encoded for storage and decoded on load.
//   '/' → "__s__"   (slash)
//   ':' → "__c__"   (colon)
// Existing simple keys (PascalCase, no special chars) are unaffected.
// ---------------------------------------------------------------------------

static QString encodeXmlKey(const QString& key)
{
    QString out = key;
    out.replace(QLatin1String(":"),  QLatin1String("__c__"));
    out.replace(QLatin1String("/"),  QLatin1String("__s__"));
    out.replace(QLatin1String("["),  QLatin1String("__lb__"));
    out.replace(QLatin1String("]"),  QLatin1String("__rb__"));
    return out;
}

static QString decodeXmlKey(const QString& tag)
{
    QString out = tag;
    out.replace(QLatin1String("__c__"),  QLatin1String(":"));
    out.replace(QLatin1String("__s__"),  QLatin1String("/"));
    out.replace(QLatin1String("__lb__"), QLatin1String("["));
    out.replace(QLatin1String("__rb__"), QLatin1String("]"));
    return out;
}

void AppSettings::load()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "No settings file found at" << m_filePath << "— using defaults";
        return;
    }

    QXmlStreamReader xml(&file);
    QString currentStation;
    bool inStation = false;

    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const QString tag = xml.name().toString();
            if (tag == "NereusSDR") {
                continue;
            }
            // Check if this is a station section
            if (!inStation && xml.attributes().hasAttribute(QStringLiteral("type"))
                && xml.attributes().value(QStringLiteral("type")) == QStringLiteral("station")) {
                inStation = true;
                currentStation = tag;
                m_stationName = tag;
                continue;
            }
            // Read element text as value; decode key back to internal format
            const QString value = xml.readElementText();
            const QString key   = decodeXmlKey(tag);
            if (inStation) {
                m_stationSettings.insert(key, value);
            } else {
                m_settings.insert(key, value);
            }
        } else if (xml.isEndElement() && inStation) {
            if (xml.name().toString() == currentStation) {
                inStation = false;
            }
        }
    }

    if (xml.hasError()) {
        qWarning() << "XML parse error in settings:" << xml.errorString();
    }

    qDebug() << "Loaded" << m_settings.size() << "settings,"
             << m_stationSettings.size() << "station settings";
}

void AppSettings::save()
{
    // Ensure directory exists
    QDir().mkpath(QFileInfo(m_filePath).absolutePath());

    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning() << "Could not save settings to" << m_filePath;
        return;
    }

    QXmlStreamWriter xml(&file);
    xml.setAutoFormatting(true);
    xml.writeStartDocument();
    xml.writeStartElement("NereusSDR");

    // Write top-level settings (encode keys so XML element names are valid)
    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it) {
        xml.writeTextElement(encodeXmlKey(it.key()), it.value());
    }

    // Write station settings
    if (!m_stationSettings.isEmpty()) {
        xml.writeStartElement(m_stationName);
        xml.writeAttribute("type", "station");
        for (auto it = m_stationSettings.constBegin(); it != m_stationSettings.constEnd(); ++it) {
            xml.writeTextElement(encodeXmlKey(it.key()), it.value());
        }
        xml.writeEndElement();
    }

    xml.writeEndElement(); // NereusSDR
    xml.writeEndDocument();

    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

QVariant AppSettings::value(const QString& key, const QVariant& defaultValue) const
{
    auto it = m_settings.constFind(key);
    if (it != m_settings.constEnd()) {
        return QVariant(it.value());
    }
    return defaultValue;
}

void AppSettings::setValue(const QString& key, const QVariant& val)
{
    m_settings.insert(key, val.toString());
}

void AppSettings::remove(const QString& key)
{
    m_settings.remove(key);
}

bool AppSettings::contains(const QString& key) const
{
    return m_settings.contains(key);
}

QStringList AppSettings::allKeys() const
{
    return m_settings.keys();
}

QVariant AppSettings::stationValue(const QString& key, const QVariant& defaultValue) const
{
    auto it = m_stationSettings.constFind(key);
    if (it != m_stationSettings.constEnd()) {
        return QVariant(it.value());
    }
    return defaultValue;
}

void AppSettings::setStationValue(const QString& key, const QVariant& val)
{
    m_stationSettings.insert(key, val.toString());
}

QString AppSettings::stationName() const
{
    return m_stationName;
}

void AppSettings::setStationName(const QString& name)
{
    m_stationName = name;
}

// ---------------------------------------------------------------------------
// Saved-radio helpers (Phase 3I Task 15)
// ---------------------------------------------------------------------------

// static
QString AppSettings::radioKeyPrefix(const QString& macKey)
{
    return QStringLiteral("radios/%1/").arg(macKey);
}

// static
QString AppSettings::macKeyFromSettingsKey(const QString& settingsKey)
{
    // Settings keys for per-radio fields look like: "radios/<macKey>/<field>"
    // This returns the <macKey> portion, or empty if the key doesn't match.
    static const QString kPrefix = QStringLiteral("radios/");
    if (!settingsKey.startsWith(kPrefix)) {
        return {};
    }
    const QString rest = settingsKey.mid(kPrefix.size()); // "<macKey>/<field>"
    const int slashIdx = rest.indexOf(QLatin1Char('/'));
    if (slashIdx < 0) {
        return {}; // "radios/lastConnected" etc. — top-level, not per-radio
    }
    return rest.left(slashIdx);
}

void AppSettings::saveRadio(const RadioInfo& info, bool pinToMac, bool autoConnect)
{
    const QString macKey = info.macAddress.isEmpty()
        ? QStringLiteral("manual-%1-%2")
              .arg(info.address.toString())
              .arg(info.port)
        : info.macAddress;

    const QString prefix = radioKeyPrefix(macKey);
    m_settings.insert(prefix + QStringLiteral("name"),            info.name);
    m_settings.insert(prefix + QStringLiteral("ipAddress"),       info.address.toString());
    m_settings.insert(prefix + QStringLiteral("port"),            QString::number(info.port));
    m_settings.insert(prefix + QStringLiteral("macAddress"),      info.macAddress);
    m_settings.insert(prefix + QStringLiteral("boardType"),
                      QString::number(static_cast<int>(info.boardType)));
    m_settings.insert(prefix + QStringLiteral("protocol"),
                      QString::number(static_cast<int>(info.protocol)));
    m_settings.insert(prefix + QStringLiteral("firmwareVersion"), QString::number(info.firmwareVersion));
    m_settings.insert(prefix + QStringLiteral("pinToMac"),        pinToMac   ? QStringLiteral("True") : QStringLiteral("False"));
    m_settings.insert(prefix + QStringLiteral("autoConnect"),     autoConnect ? QStringLiteral("True") : QStringLiteral("False"));
    m_settings.insert(prefix + QStringLiteral("lastSeen"),
                      QDateTime::currentDateTimeUtc().toString(Qt::ISODate));

    // Model override (Phase 3I-RP). FIRST = no override.
    if (info.modelOverride != HPSDRModel::FIRST) {
        m_settings.insert(prefix + QStringLiteral("modelOverride"),
                          QString::number(static_cast<int>(info.modelOverride)));
    }
}

void AppSettings::forgetRadio(const QString& macKey)
{
    const QString prefix = radioKeyPrefix(macKey);
    // Remove all keys with this prefix
    const QStringList keys = m_settings.keys();
    for (const QString& k : keys) {
        if (k.startsWith(prefix)) {
            m_settings.remove(k);
        }
    }
}

void AppSettings::clearSavedRadios()
{
    // Remove all radios/<key>/<field> entries (but preserve lastConnected, discoveryProfile)
    const QStringList keys = m_settings.keys();
    for (const QString& k : keys) {
        if (!k.startsWith(QStringLiteral("radios/"))) {
            continue;
        }
        // Only remove keys that have a per-radio sub-path (3 segments: radios/<mac>/<field>)
        const QString rest = k.mid(7); // strip "radios/"
        if (rest.contains(QLatin1Char('/'))) {
            m_settings.remove(k);
        }
    }
}

QList<SavedRadio> AppSettings::savedRadios() const
{
    // Collect all distinct macKeys
    QSet<QString> macKeys;
    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it) {
        const QString mk = macKeyFromSettingsKey(it.key());
        if (!mk.isEmpty()) {
            macKeys.insert(mk);
        }
    }

    QList<SavedRadio> result;
    result.reserve(macKeys.size());
    for (const QString& mk : std::as_const(macKeys)) {
        if (auto sr = savedRadio(mk)) {
            result.append(*sr);
        }
    }
    return result;
}

std::optional<SavedRadio> AppSettings::savedRadio(const QString& macKey) const
{
    const QString prefix = radioKeyPrefix(macKey);
    const QString nameKey = prefix + QStringLiteral("name");
    if (!m_settings.contains(nameKey)) {
        return std::nullopt;
    }

    SavedRadio sr;

    // RadioInfo fields
    sr.info.name            = m_settings.value(prefix + QStringLiteral("name"));
    sr.info.address         = QHostAddress(m_settings.value(prefix + QStringLiteral("ipAddress")));
    sr.info.port            = static_cast<quint16>(
                                m_settings.value(prefix + QStringLiteral("port"),
                                                 QStringLiteral("1024")).toUInt());
    sr.info.macAddress      = m_settings.value(prefix + QStringLiteral("macAddress"));
    sr.info.boardType       = static_cast<HPSDRHW>(
                                m_settings.value(prefix + QStringLiteral("boardType"),
                                                 QStringLiteral("999")).toInt());
    sr.info.protocol        = static_cast<ProtocolVersion>(
                                m_settings.value(prefix + QStringLiteral("protocol"),
                                                 QStringLiteral("1")).toInt());
    sr.info.firmwareVersion = m_settings.value(prefix + QStringLiteral("firmwareVersion"),
                                               QStringLiteral("0")).toInt();

    // Saved-only flags
    sr.pinToMac    = (m_settings.value(prefix + QStringLiteral("pinToMac"),
                                       QStringLiteral("False")) == QStringLiteral("True"));
    sr.autoConnect = (m_settings.value(prefix + QStringLiteral("autoConnect"),
                                       QStringLiteral("False")) == QStringLiteral("True"));

    const QString lastSeenStr = m_settings.value(prefix + QStringLiteral("lastSeen"));
    if (!lastSeenStr.isEmpty()) {
        sr.lastSeen = QDateTime::fromString(lastSeenStr, Qt::ISODate);
    }

    // Model override (Phase 3I-RP)
    const QString moStr = m_settings.value(prefix + QStringLiteral("modelOverride"),
                                            QStringLiteral("-1"));
    int moInt = moStr.toInt();
    if (moInt > static_cast<int>(HPSDRModel::FIRST) &&
        moInt < static_cast<int>(HPSDRModel::LAST)) {
        sr.info.modelOverride = static_cast<HPSDRModel>(moInt);
    }

    return sr;
}

QString AppSettings::lastConnected() const
{
    return m_settings.value(QStringLiteral("radios/lastConnected"));
}

void AppSettings::setLastConnected(const QString& macKey)
{
    if (macKey.isEmpty()) {
        m_settings.remove(QStringLiteral("radios/lastConnected"));
    } else {
        m_settings.insert(QStringLiteral("radios/lastConnected"), macKey);
    }
}

DiscoveryProfile AppSettings::discoveryProfile() const
{
    // Default to SafeDefault (4)
    const int v = m_settings.value(QStringLiteral("radios/discoveryProfile"),
                                   QStringLiteral("4")).toInt();
    return static_cast<DiscoveryProfile>(v);
}

void AppSettings::setDiscoveryProfile(DiscoveryProfile p)
{
    m_settings.insert(QStringLiteral("radios/discoveryProfile"),
                      QString::number(static_cast<int>(p)));
}

// ---------------------------------------------------------------------------
// Hardware tab persistence (Phase 3I Task 21)
// ---------------------------------------------------------------------------

void AppSettings::setHardwareValue(const QString& mac, const QString& key, const QVariant& value)
{
    const QString fullKey = QStringLiteral("hardware/%1/%2").arg(mac, key);
    m_settings.insert(fullKey, value.toString());
}

QVariant AppSettings::hardwareValue(const QString& mac, const QString& key,
                                     const QVariant& defaultValue) const
{
    const QString fullKey = QStringLiteral("hardware/%1/%2").arg(mac, key);
    auto it = m_settings.constFind(fullKey);
    if (it != m_settings.constEnd()) {
        return QVariant(it.value());
    }
    return defaultValue;
}

QMap<QString, QVariant> AppSettings::hardwareValues(const QString& mac) const
{
    const QString prefix = QStringLiteral("hardware/%1/").arg(mac);
    QMap<QString, QVariant> result;
    for (auto it = m_settings.constBegin(); it != m_settings.constEnd(); ++it) {
        if (it.key().startsWith(prefix)) {
            const QString bareKey = it.key().mid(prefix.size());
            result.insert(bareKey, QVariant(it.value()));
        }
    }
    return result;
}

void AppSettings::clearHardwareValues(const QString& mac)
{
    const QString prefix = QStringLiteral("hardware/%1/").arg(mac);
    const QStringList keys = m_settings.keys();
    for (const QString& k : keys) {
        if (k.startsWith(prefix)) {
            m_settings.remove(k);
        }
    }
}

// ---------------------------------------------------------------------------
// Model override (Phase 3I-RP)
// ---------------------------------------------------------------------------

HPSDRModel AppSettings::modelOverride(const QString& macKey) const
{
    const QString prefix = radioKeyPrefix(macKey);
    const QString val = m_settings.value(prefix + QStringLiteral("modelOverride"),
                                          QStringLiteral("-1"));
    int v = val.toInt();
    if (v > static_cast<int>(HPSDRModel::FIRST) &&
        v < static_cast<int>(HPSDRModel::LAST)) {
        return static_cast<HPSDRModel>(v);
    }
    return HPSDRModel::FIRST;
}

void AppSettings::setModelOverride(const QString& macKey, HPSDRModel model)
{
    const QString prefix = radioKeyPrefix(macKey);
    m_settings.insert(prefix + QStringLiteral("modelOverride"),
                      QString::number(static_cast<int>(model)));
}

} // namespace NereusSDR
