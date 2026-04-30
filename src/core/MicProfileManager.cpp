// =================================================================
// src/core/MicProfileManager.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original file. The mic-profile-bank API is a port of the
// Thetis comboTXProfile / Setup → TX Profile editor flow:
//   setup.cs:9505-9543 [v2.10.3.13] — comboTXProfileName_SelectedIndexChanged
//   setup.cs:9545-9612 [v2.10.3.13] — btnTXProfileSave_Click
//   setup.cs:9615-9656 [v2.10.3.13] — btnTXProfileDelete_Click
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-28 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//                 Phase 3M-1c chunk F (F.1-F.6) — see header comment.
// =================================================================

// no-port-check: NereusSDR-original file; Thetis-derived handler logic
// is cited inline below.

#include "MicProfileManager.h"

#include "AppSettings.h"
#include "LogCategories.h"
#include "models/TransmitModel.h"

#include <QLoggingCategory>

#include <algorithm>

namespace NereusSDR {

namespace {

constexpr const char* kProfileSubpath = "/tx/profile/";

QString profilePathPrefix(const QString& mac)
{
    return QStringLiteral("hardware/%1/tx/profile/").arg(mac);
}

QString profileFieldKey(const QString& mac, const QString& name, const QString& field)
{
    return profilePathPrefix(mac) + name + QLatin1Char('/') + field;
}

QString manifestKey(const QString& mac)
{
    return profilePathPrefix(mac) + QStringLiteral("_names");
}

QString activeKey(const QString& mac)
{
    return profilePathPrefix(mac) + QStringLiteral("active");
}

// ---------------------------------------------------------------------------
// 23 live-field keys captured per profile.  Order matches the table in the
// chunk-F task spec (tools/script-friendly: stays sorted by group).
// ---------------------------------------------------------------------------
const QStringList& liveKeyList()
{
    static const QStringList kKeys = {
        // Mic / VOX / MON (15 of which 14 are persisted live by TransmitModel
        // L.2 — micMute is excluded from per-MAC TX persistence by the safety
        // rule, but IS captured here because a profile carries a snapshot
        // including the mic in/out state at save-time).  Plus Mic_Source.
        QStringLiteral("MicGain"),
        QStringLiteral("Mic_Input_Boost"),
        QStringLiteral("Mic_XLR"),
        QStringLiteral("Line_Input_On"),
        QStringLiteral("Line_Input_Level"),
        QStringLiteral("Mic_TipRing"),
        QStringLiteral("Mic_Bias"),
        QStringLiteral("Mic_PTT_Disabled"),
        QStringLiteral("Dexp_Threshold"),
        QStringLiteral("VOX_GainScalar"),
        QStringLiteral("VOX_HangTime"),
        QStringLiteral("AntiVox_Gain"),
        QStringLiteral("AntiVox_Source_VAX"),
        QStringLiteral("MonitorVolume"),
        QStringLiteral("Mic_Source"),
        // Two-tone (7 properties + 1 enum)
        QStringLiteral("TwoToneFreq1"),
        QStringLiteral("TwoToneFreq2"),
        QStringLiteral("TwoToneLevel"),
        QStringLiteral("TwoTonePower"),
        QStringLiteral("TwoToneFreq2Delay"),
        QStringLiteral("TwoToneInvert"),
        QStringLiteral("TwoTonePulsed"),
        QStringLiteral("TwoToneDrivePowerOrigin"),
    };
    return kKeys;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// MicProfileManager
// ---------------------------------------------------------------------------

MicProfileManager::MicProfileManager(QObject* parent)
    : QObject(parent)
{
}

MicProfileManager::~MicProfileManager() = default;

void MicProfileManager::setMacAddress(const QString& mac)
{
    m_mac = mac;
}

QStringList MicProfileManager::readManifest() const
{
    if (m_mac.isEmpty()) {
        return {};
    }
    const QString raw = AppSettings::instance().value(manifestKey(m_mac)).toString();
    if (raw.isEmpty()) {
        return {};
    }
    QStringList names = raw.split(QLatin1Char(','), Qt::SkipEmptyParts);
    names.removeDuplicates();
    return names;
}

void MicProfileManager::writeManifest(const QStringList& names)
{
    if (m_mac.isEmpty()) {
        return;
    }
    AppSettings::instance().setValue(manifestKey(m_mac),
                                     names.join(QLatin1Char(',')));
}

QString MicProfileManager::readActiveKey() const
{
    if (m_mac.isEmpty()) {
        return {};
    }
    return AppSettings::instance().value(activeKey(m_mac)).toString();
}

void MicProfileManager::writeActiveKey(const QString& name)
{
    if (m_mac.isEmpty()) {
        return;
    }
    AppSettings::instance().setValue(activeKey(m_mac), name);
}

void MicProfileManager::load()
{
    if (m_mac.isEmpty()) {
        return;
    }

    QStringList names = readManifest();
    if (names.isEmpty()) {
        // F.5: first-launch seed.  Insert "Default" with documented values.
        const QHash<QString, QVariant> defaults = defaultProfileValues();
        writeProfileKeys(QStringLiteral("Default"), defaults);
        names = QStringList{QStringLiteral("Default")};
        writeManifest(names);
        // Active defaults to "Default" on first launch.
        writeActiveKey(QStringLiteral("Default"));
        emit profileListChanged();
    }
    // No emit on subsequent loads — the manifest already exists.
}

QStringList MicProfileManager::profileNames() const
{
    QStringList names = readManifest();
    std::sort(names.begin(), names.end());
    return names;
}

QString MicProfileManager::activeProfileName() const
{
    const QString active = readActiveKey();
    if (active.isEmpty()) {
        return QStringLiteral("Default");
    }
    return active;
}

void MicProfileManager::writeProfileKeys(const QString& name,
                                         const QHash<QString, QVariant>& values)
{
    if (m_mac.isEmpty()) {
        return;
    }
    auto& s = AppSettings::instance();
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        s.setValue(profileFieldKey(m_mac, name, it.key()),
                   it.value().toString());
    }
}

QHash<QString, QVariant> MicProfileManager::readProfileKeys(const QString& name) const
{
    QHash<QString, QVariant> out;
    if (m_mac.isEmpty()) {
        return out;
    }
    auto& s = AppSettings::instance();
    for (const QString& key : liveKeyList()) {
        const QString full = profileFieldKey(m_mac, name, key);
        if (s.contains(full)) {
            out.insert(key, s.value(full));
        }
    }
    return out;
}

void MicProfileManager::removeProfileKeys(const QString& name)
{
    if (m_mac.isEmpty()) {
        return;
    }
    auto& s = AppSettings::instance();
    for (const QString& key : liveKeyList()) {
        s.remove(profileFieldKey(m_mac, name, key));
    }
}

bool MicProfileManager::saveProfile(const QString& rawName, const TransmitModel* tx)
{
    if (m_mac.isEmpty() || tx == nullptr) {
        return false;
    }

    // F.2 / Thetis precedent: setup.cs:9550-9552 [v2.10.3.13].
    // no more , in profile names, because it will make the tci tx profile messages look like they have multiple parts
    // existing ones will cause issues no doubt, but just not worth the effort to reparse the database
    QString name = rawName;
    name.replace(QLatin1Char(','), QLatin1Char('_'));

    if (name.isEmpty()) {
        return false;
    }

    const QHash<QString, QVariant> values = captureLiveValues(tx);
    writeProfileKeys(name, values);

    QStringList names = readManifest();
    const bool wasInList = names.contains(name);
    if (!wasInList) {
        names.append(name);
        writeManifest(names);
        emit profileListChanged();
    }
    return true;
}

bool MicProfileManager::deleteProfile(const QString& name)
{
    if (m_mac.isEmpty()) {
        return false;
    }

    QStringList names = readManifest();

    // F.3 / Thetis precedent: setup.cs:9617-9624 [v2.10.3.13]
    //   if (comboTXProfileName.Items.Count == 1) { MessageBox.Show("It is not
    //   possible to delete the last remaining TX profile."); return; }
    if (names.size() <= 1) {
        qCWarning(lcDsp) << "It is not possible to delete the last remaining TX profile";
        return false;
    }

    if (!names.contains(name)) {
        return false;
    }

    removeProfileKeys(name);
    names.removeAll(name);
    writeManifest(names);

    // Active fallback.  If we just deleted the active profile, fall back to
    // the first remaining (sorted lexicographically — same surface the UI
    // sees via profileNames()).
    const QString currentActive = readActiveKey();
    if (currentActive == name) {
        QStringList sorted = names;
        std::sort(sorted.begin(), sorted.end());
        const QString fallback = sorted.first();
        writeActiveKey(fallback);
        emit activeProfileChanged(fallback);
    }

    emit profileListChanged();
    return true;
}

bool MicProfileManager::setActiveProfile(const QString& name, TransmitModel* tx)
{
    if (m_mac.isEmpty() || tx == nullptr) {
        return false;
    }

    const QStringList names = readManifest();
    if (!names.contains(name)) {
        return false;
    }

    const QHash<QString, QVariant> values = readProfileKeys(name);
    applyValuesToModel(values, tx);

    writeActiveKey(name);
    emit activeProfileChanged(name);
    return true;
}

// ---------------------------------------------------------------------------
// Static helpers
// ---------------------------------------------------------------------------

QHash<QString, QVariant> MicProfileManager::defaultProfileValues()
{
    QHash<QString, QVariant> out;
    // Mic / VOX / MON (15)
    out.insert(QStringLiteral("MicGain"),              QStringLiteral("-6"));            // NereusSDR-original (plan §0 row 11)
    out.insert(QStringLiteral("Mic_Input_Boost"),      QStringLiteral("True"));          // console.cs:13237 [v2.10.3.13]
    out.insert(QStringLiteral("Mic_XLR"),              QStringLiteral("True"));          // console.cs:13249 [v2.10.3.13]
    out.insert(QStringLiteral("Line_Input_On"),        QStringLiteral("False"));         // console.cs:13213 [v2.10.3.13]
    out.insert(QStringLiteral("Line_Input_Level"),     QStringLiteral("0"));             // console.cs:13225 [v2.10.3.13]
    out.insert(QStringLiteral("Mic_TipRing"),          QStringLiteral("True"));          // setup.designer.cs:8683 [v2.10.3.13]
    out.insert(QStringLiteral("Mic_Bias"),             QStringLiteral("False"));         // setup.designer.cs:8779 [v2.10.3.13]
    out.insert(QStringLiteral("Mic_PTT_Disabled"),     QStringLiteral("False"));         // console.cs:19757 [v2.10.3.13]
    out.insert(QStringLiteral("Dexp_Threshold"),       QStringLiteral("-40"));           // NereusSDR-original (ptbVOX range -80..0)
    out.insert(QStringLiteral("VOX_GainScalar"),       QStringLiteral("1"));             // audio.cs:194 [v2.10.3.13]
    out.insert(QStringLiteral("VOX_HangTime"),         QStringLiteral("500"));           // setup.designer.cs:45020-45024 [v2.10.3.13]
    out.insert(QStringLiteral("AntiVox_Gain"),         QStringLiteral("0"));             // NereusSDR-original (Thetis udAntiVoxGain=1.0dB)
    out.insert(QStringLiteral("AntiVox_Source_VAX"),   QStringLiteral("False"));         // audio.cs:446 [v2.10.3.13]
    out.insert(QStringLiteral("MonitorVolume"),        QStringLiteral("0.5"));           // audio.cs:417 [v2.10.3.13]
    out.insert(QStringLiteral("Mic_Source"),           QStringLiteral("Pc"));            // NereusSDR-native (always-safe)

    // Two-tone (7) + drive-power source (1)
    out.insert(QStringLiteral("TwoToneFreq1"),         QStringLiteral("700"));           // setup.cs:34226 [v2.10.3.13]
    out.insert(QStringLiteral("TwoToneFreq2"),         QStringLiteral("1900"));          // setup.cs:34227 [v2.10.3.13]
    out.insert(QStringLiteral("TwoToneLevel"),         QStringLiteral("-6"));            // NereusSDR-original safer (Designer ships 0)
    out.insert(QStringLiteral("TwoTonePower"),         QStringLiteral("50"));            // NereusSDR-original (Designer ships 10)
    out.insert(QStringLiteral("TwoToneFreq2Delay"),    QStringLiteral("0"));             // setup.Designer.cs:61943-61947 [v2.10.3.13]
    out.insert(QStringLiteral("TwoToneInvert"),        QStringLiteral("True"));          // setup.Designer.cs:61963 [v2.10.3.13]
    out.insert(QStringLiteral("TwoTonePulsed"),        QStringLiteral("False"));         // setup.Designer.cs:61643-61653 [v2.10.3.13]
    out.insert(QStringLiteral("TwoToneDrivePowerOrigin"),
               QStringLiteral("DriveSlider"));                                            // console.cs:46553 [v2.10.3.13]
    return out;
}

QHash<QString, QVariant> MicProfileManager::captureLiveValues(const TransmitModel* tx)
{
    QHash<QString, QVariant> out;
    if (tx == nullptr) {
        return out;
    }

    // Mic / VOX / MON (15)
    out.insert(QStringLiteral("MicGain"),              QString::number(tx->micGainDb()));
    out.insert(QStringLiteral("Mic_Input_Boost"),      tx->micBoost()      ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Mic_XLR"),              tx->micXlr()        ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Line_Input_On"),        tx->lineIn()        ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Line_Input_Level"),     QString::number(tx->lineInBoost()));
    out.insert(QStringLiteral("Mic_TipRing"),          tx->micTipRing()    ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Mic_Bias"),             tx->micBias()       ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Mic_PTT_Disabled"),     tx->micPttDisabled() ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("Dexp_Threshold"),       QString::number(tx->voxThresholdDb()));
    out.insert(QStringLiteral("VOX_GainScalar"),       QString::number(static_cast<double>(tx->voxGainScalar())));
    out.insert(QStringLiteral("VOX_HangTime"),         QString::number(tx->voxHangTimeMs()));
    out.insert(QStringLiteral("AntiVox_Gain"),         QString::number(tx->antiVoxGainDb()));
    out.insert(QStringLiteral("AntiVox_Source_VAX"),   tx->antiVoxSourceVax() ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("MonitorVolume"),        QString::number(static_cast<double>(tx->monitorVolume())));
    out.insert(QStringLiteral("Mic_Source"),
               tx->micSource() == MicSource::Radio ? QStringLiteral("Radio") : QStringLiteral("Pc"));

    // Two-tone (7) + drive-power source (1)
    out.insert(QStringLiteral("TwoToneFreq1"),         QString::number(tx->twoToneFreq1()));
    out.insert(QStringLiteral("TwoToneFreq2"),         QString::number(tx->twoToneFreq2()));
    out.insert(QStringLiteral("TwoToneLevel"),         QString::number(tx->twoToneLevel()));
    out.insert(QStringLiteral("TwoTonePower"),         QString::number(tx->twoTonePower()));
    out.insert(QStringLiteral("TwoToneFreq2Delay"),    QString::number(tx->twoToneFreq2Delay()));
    out.insert(QStringLiteral("TwoToneInvert"),        tx->twoToneInvert() ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("TwoTonePulsed"),        tx->twoTonePulsed() ? QStringLiteral("True") : QStringLiteral("False"));
    out.insert(QStringLiteral("TwoToneDrivePowerOrigin"),
               drivePowerSourceToString(tx->twoToneDrivePowerSource()));
    return out;
}

void MicProfileManager::applyValuesToModel(const QHash<QString, QVariant>& values,
                                           TransmitModel* tx)
{
    if (tx == nullptr) {
        return;
    }

    auto take = [&](const QString& key, const QString& def) -> QString {
        const auto it = values.constFind(key);
        if (it == values.constEnd()) {
            return def;
        }
        return it.value().toString();
    };

    // Mic / VOX / MON
    tx->setMicGainDb(take(QStringLiteral("MicGain"), QStringLiteral("-6")).toInt());
    tx->setMicBoost(take(QStringLiteral("Mic_Input_Boost"), QStringLiteral("True")) == QLatin1String("True"));
    tx->setMicXlr(take(QStringLiteral("Mic_XLR"), QStringLiteral("True")) == QLatin1String("True"));
    tx->setLineIn(take(QStringLiteral("Line_Input_On"), QStringLiteral("False")) == QLatin1String("True"));
    tx->setLineInBoost(take(QStringLiteral("Line_Input_Level"), QStringLiteral("0")).toDouble());
    tx->setMicTipRing(take(QStringLiteral("Mic_TipRing"), QStringLiteral("True")) == QLatin1String("True"));
    tx->setMicBias(take(QStringLiteral("Mic_Bias"), QStringLiteral("False")) == QLatin1String("True"));
    tx->setMicPttDisabled(take(QStringLiteral("Mic_PTT_Disabled"), QStringLiteral("False")) == QLatin1String("True"));
    tx->setVoxThresholdDb(take(QStringLiteral("Dexp_Threshold"), QStringLiteral("-40")).toInt());
    tx->setVoxGainScalar(take(QStringLiteral("VOX_GainScalar"), QStringLiteral("1")).toFloat());
    tx->setVoxHangTimeMs(take(QStringLiteral("VOX_HangTime"), QStringLiteral("500")).toInt());
    tx->setAntiVoxGainDb(take(QStringLiteral("AntiVox_Gain"), QStringLiteral("0")).toInt());
    tx->setAntiVoxSourceVax(take(QStringLiteral("AntiVox_Source_VAX"), QStringLiteral("False")) == QLatin1String("True"));
    tx->setMonitorVolume(take(QStringLiteral("MonitorVolume"), QStringLiteral("0.5")).toFloat());

    const QString micSrc = take(QStringLiteral("Mic_Source"), QStringLiteral("Pc"));
    tx->setMicSource(micSrc == QLatin1String("Radio") ? MicSource::Radio : MicSource::Pc);

    // Two-tone
    tx->setTwoToneFreq1(take(QStringLiteral("TwoToneFreq1"), QStringLiteral("700")).toInt());
    tx->setTwoToneFreq2(take(QStringLiteral("TwoToneFreq2"), QStringLiteral("1900")).toInt());
    tx->setTwoToneLevel(take(QStringLiteral("TwoToneLevel"), QStringLiteral("-6")).toDouble());
    tx->setTwoTonePower(take(QStringLiteral("TwoTonePower"), QStringLiteral("50")).toInt());
    tx->setTwoToneFreq2Delay(take(QStringLiteral("TwoToneFreq2Delay"), QStringLiteral("0")).toInt());
    tx->setTwoToneInvert(take(QStringLiteral("TwoToneInvert"), QStringLiteral("True")) == QLatin1String("True"));
    tx->setTwoTonePulsed(take(QStringLiteral("TwoTonePulsed"), QStringLiteral("False")) == QLatin1String("True"));
    tx->setTwoToneDrivePowerSource(drivePowerSourceFromString(
        take(QStringLiteral("TwoToneDrivePowerOrigin"), QStringLiteral("DriveSlider"))));
}

} // namespace NereusSDR
