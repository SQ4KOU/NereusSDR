// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationDevicesFacade.cpp  (NereusSDR)
// =================================================================
// See StationDevicesFacade.h.
// =================================================================
// Modification history (NereusSDR):
//   2026-09-24: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), with AI-assisted implementation via Anthropic
//               Claude Code.
// =================================================================

#include "core/session/StationDevicesFacade.h"

#include "core/AppSettings.h"
#include "core/security/DeviceStore.h"
#include "core/security/StationIdentity.h"
#include "core/security/StationLabel.h"
#include "core/security/TokenStore.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <optional>

namespace NereusSDR {

namespace {
Q_LOGGING_CATEGORY(lcDevices, "nereus.station.devices")

QString wireTime(const QDateTime& time)
{
    return time.isValid() ? time.toUTC().toString(Qt::ISODate) : QString();
}

// Sets `key` to `value` and saves; on a failed save the old value is put
// back, so the setting and the file never disagree.
bool storeSetting(AppSettings& settings, const QString& key, const QString& value)
{
    const bool had = settings.contains(key);
    const QVariant previous = settings.value(key);
    settings.setValue(key, value);
    QString error;
    if (settings.save(&error)) {
        return true;
    }
    qCWarning(lcDevices) << "Could not save" << key << ":" << error;
    if (had) {
        settings.setValue(key, previous);
    } else {
        settings.remove(key);
    }
    return false;
}
} // namespace

StationDevicesFacade::StationDevicesFacade(DeviceStore& devices, TokenStore& tokens,
                                           const StationIdentity& identity,
                                           AppSettings& settings, QObject* parent)
    : QObject(parent)
    , m_devices(devices)
    , m_tokens(tokens)
    , m_identity(identity)
    , m_settings(settings)
{
    m_state = compute();
    connect(&m_devices, &DeviceStore::devicesChanged, this, &StationDevicesFacade::refresh);
}

StationDevicesFacade::State StationDevicesFacade::compute() const
{
    State state;
    QJsonArray list;
    for (const PairedDevice& device : m_devices.list()) {
        list.append(QJsonObject{
            {QStringLiteral("id"), StationIdentity::toBase64Url(device.id)},
            {QStringLiteral("name"), device.name},
            {QStringLiteral("kind"), device.kind},
            {QStringLiteral("pairedAt"), wireTime(device.pairedAt)},
            {QStringLiteral("lastSeen"), wireTime(device.lastSeen)},
            {QStringLiteral("connected"), m_connected.contains(device.id)},
        });
    }
    state.listJson = QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact));
    const std::optional<StationLabel> label = StationLabel::current(m_settings);
    state.stationLabel = label ? label->display() : QString();
    state.claimed = m_devices.isClaimed();
    state.tokenActive = m_tokens.isActive();
    if (m_identity.isValid()) {
        const QString acknowledged =
            m_settings.value(QLatin1String(kKeyBackupSettingsKey), QString()).toString();
        state.keyBackupAcknowledged =
            acknowledged == StationIdentity::toBase64Url(m_identity.fingerprint());
        state.keyPath = m_identity.keyPath();
    }
    return state;
}

void StationDevicesFacade::refresh()
{
    if (m_hold > 0) {
        m_refreshWanted = true;
        return;
    }
    const State next = compute();
    if (next == m_state) {
        return;
    }
    const bool labelChanged = next.stationLabel != m_state.stationLabel;
    m_state = next;
    ++m_revision;  // wraps past 2^32; readers compare by serial number
    emit devicesStateChanged();
    if (labelChanged) {
        emit stationLabelChanged(m_state.stationLabel);
    }
}

void StationDevicesFacade::holdRefresh()
{
    ++m_hold;
}

void StationDevicesFacade::resumeRefresh()
{
    if (m_hold > 0) {
        --m_hold;
    }
    if (m_hold == 0 && m_refreshWanted) {
        m_refreshWanted = false;
        refresh();
    }
}

void StationDevicesFacade::setConnectedDevices(const QSet<QByteArray>& ids)
{
    if (ids == m_connected) {
        return;
    }
    m_connected = ids;
    refresh();
}

DeviceAdminResult StationDevicesFacade::revoke(const QString& id)
{
    if (!m_devices.isValid()) {
        return {false, QStringLiteral("The Core cannot read its list of paired devices, so it "
                                      "cannot remove one.")};
    }
    bool ok = false;
    const QByteArray raw = StationIdentity::fromBase64Url(id, &ok);
    if (!ok || !m_devices.find(raw)) {
        return {false, QStringLiteral("That device is not paired with this Core.")};
    }
    // DeviceStore emits deviceRemoved (StationServer ends that device's
    // connection) and devicesChanged (refresh) on success.
    if (!m_devices.remove(raw)) {
        return {false, QStringLiteral("The Core could not remove that device. Try again.")};
    }
    qCInfo(lcDevices) << "A paired device was removed";
    return {true, QString()};
}

DeviceAdminResult StationDevicesFacade::rename(const QString& label)
{
    const std::optional<StationLabel> parsed = StationLabel::parse(label);
    if (!parsed) {
        return {false, StationLabel::ruleText()};
    }
    if (!storeSetting(m_settings, QLatin1String(StationLabel::kSettingsKey),
                      parsed->display())) {
        return {false, QStringLiteral("The Core could not save its new name. Try again.")};
    }
    refresh();
    return {true, QString()};
}

DeviceAdminResult StationDevicesFacade::acknowledgeKeyBackup()
{
    if (!m_identity.isValid()) {
        return {false, QStringLiteral("The Core's own key is unavailable, so there is nothing "
                                      "to back up.")};
    }
    if (!storeSetting(m_settings, QLatin1String(kKeyBackupSettingsKey),
                      StationIdentity::toBase64Url(m_identity.fingerprint()))) {
        return {false, QStringLiteral("The Core could not save this. Try again.")};
    }
    refresh();
    return {true, QString()};
}

DeviceAdminResult StationDevicesFacade::retireToken()
{
    // So the owner cannot lock everyone out: the token stays until a
    // device can sign in with its own key.
    if (!m_devices.isValid() || m_devices.list().isEmpty()) {
        return {false, QStringLiteral("Pair a device with this Core first, so a device can "
                                      "still sign in once the pairing token stops working.")};
    }
    if (!m_tokens.isActive()) {
        // Nothing to retire; a second request changes nothing.
        return {true, QString()};
    }
    if (!m_tokens.retire()) {
        return {false, QStringLiteral("The Core could not stop accepting its pairing token. "
                                      "Try again.")};
    }
    qCInfo(lcDevices) << "The pairing token was retired";
    refresh();
    emit tokenRetired();
    return {true, QString()};
}

} // namespace NereusSDR
