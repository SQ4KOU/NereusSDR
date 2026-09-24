// no-port-check: NereusSDR-original. See StationAccessoryData.h.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/core/StationAccessoryData.cpp  (NereusSDR)
// =================================================================
//
// Modification history (NereusSDR):
//   2026-09-24  J.J. Boyd / KG4VCF  Created (R-R3-47, R-R3-22). AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include "core/StationAccessoryData.h"

#include "core/AppSettings.h"
#include "core/ConnectionDiagnostics.h"
#include "core/FaultLog.h"
#include "core/TuneMemoryStore.h"
#include "core/TxInterlockPolicy.h"
#include "models/AccessoryDataModel.h"

#include <QLoggingCategory>
#include <QStringList>

#include <cmath>

Q_LOGGING_CATEGORY(lcStationAccessoryData, "nereus.station.accessorydata")

namespace NereusSDR {

namespace {

bool isInterlockKey(const QString& key)
{
    return key == QLatin1String("PGXL_TxInterlockMode")
        || key == QLatin1String("PGXL_TxInterlockGraceMs")
        || key == QLatin1String("PGXL_TxSwrGate")
        || key == QLatin1String("PGXL_TxSwrGateMax");
}

} // namespace

StationAccessoryData::StationAccessoryData(AccessoryDataModel* model, const Sources& sources,
                                           QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_sources(sources)
{
    for (FaultLog* log : { m_sources.pgxlFaults, m_sources.tgxlFaults, m_sources.rfkitFaults }) {
        if (log != nullptr) {
            connect(log, &FaultLog::changed, this, &StationAccessoryData::publishFaults);
        }
    }
    if (m_sources.pgxlDiagnostics != nullptr) {
        connect(m_sources.pgxlDiagnostics, &ConnectionDiagnostics::changed, this, [this] {
            if (m_model) {
                m_model->setPgxlDiagnostics(m_sources.pgxlDiagnostics->counters());
            }
        });
    }
    if (m_sources.tgxlDiagnostics != nullptr) {
        connect(m_sources.tgxlDiagnostics, &ConnectionDiagnostics::changed, this, [this] {
            if (m_model) {
                m_model->setTgxlDiagnostics(m_sources.tgxlDiagnostics->counters());
            }
        });
    }
    if (m_sources.interlock != nullptr) {
        connect(m_sources.interlock, &TxInterlockPolicy::changed,
                this, &StationAccessoryData::publishInterlock);
    }
    if (m_sources.tuneMemory != nullptr) {
        connect(m_sources.tuneMemory, &TuneMemoryStore::changed,
                this, &StationAccessoryData::publishTuneMemory);
    }
    publishAll();
}

// static
QString StationAccessoryData::powerCapAlertText(int forwardW, int limitW)
{
    return QStringLiteral("Power Genius output %1 W is above the %2 W limit.")
        .arg(forwardW).arg(limitW);
}

void StationAccessoryData::publishAll()
{
    publishFaults();
    if (m_model && m_sources.pgxlDiagnostics) {
        m_model->setPgxlDiagnostics(m_sources.pgxlDiagnostics->counters());
    }
    if (m_model && m_sources.tgxlDiagnostics) {
        m_model->setTgxlDiagnostics(m_sources.tgxlDiagnostics->counters());
    }
    publishInterlock();
    publishPowerCapSettings();
    publishTuneMemory();
    publishLabels();
}

void StationAccessoryData::publishFaults()
{
    if (!m_model) {
        return;
    }
    const auto json = [](FaultLog* log) {
        return log != nullptr ? log->toJson() : QStringLiteral("[]");
    };
    m_model->setFaults(json(m_sources.pgxlFaults), json(m_sources.tgxlFaults),
                       json(m_sources.rfkitFaults));
}

void StationAccessoryData::publishInterlock()
{
    if (!m_model || !m_sources.interlock) {
        return;
    }
    const TxInterlockPolicy* p = m_sources.interlock;
    m_model->setInterlock(static_cast<AccessoryDataModel::InterlockMode>(static_cast<int>(p->mode())),
                          p->graceMs(), p->swrGateEnabled(),
                          static_cast<double>(p->swrGateMax()));
}

void StationAccessoryData::publishPowerCapSettings()
{
    if (!m_model) {
        return;
    }
    const auto& s = AppSettings::instance();
    AccessoryDataModel::PowerCap cap = m_model->powerCap();
    cap.enabled = s.value(QStringLiteral("PGXL_PowerCapEnabled"), QStringLiteral("False"))
                      .toString() == QStringLiteral("True");
    cap.watts = s.value(QStringLiteral("PGXL_PowerCapW"), 1500).toInt();
    if (!cap.enabled) {
        // Keep the re-arm state sane when the limit is switched off.
        cap.exceeded = false;
        m_alertArmed = true;
    }
    m_model->setPowerCap(cap);
}

void StationAccessoryData::publishTuneMemory()
{
    if (!m_model) {
        return;
    }
    const bool autoRecall = AppSettings::instance()
        .value(QStringLiteral("TGXL_AutoTuneMemoryRecall"), QStringLiteral("False"))
        .toString() == QStringLiteral("True");
    const QString json = m_sources.tuneMemory != nullptr
        ? TuneMemoryStore::toJson(m_sources.tuneMemory->listAll())
        : QStringLiteral("[]");
    m_model->setTuneMemory(json, autoRecall);
}

void StationAccessoryData::publishLabels()
{
    if (!m_model) {
        return;
    }
    const auto& s = AppSettings::instance();
    QStringList tgxl;
    for (int i = 1; i <= AccessoryDataModel::kTgxlAntennas; ++i) {
        tgxl.append(s.value(QStringLiteral("TGXL_Ant%1_Label").arg(i)).toString().trimmed());
    }
    QStringList rfkit;
    for (int i = 1; i <= AccessoryDataModel::kRfKitAntennas; ++i) {
        rfkit.append(s.value(QStringLiteral("RfKit_Ant%1_Label").arg(i)).toString().trimmed());
    }
    m_model->setLabels(tgxl, rfkit);
}

void StationAccessoryData::onForwardPower(double watts)
{
    if (!m_model) {
        return;
    }
    // The limit is read each time, as MainWindow read it: a Setup page in a
    // local window writes it straight to the settings.
    publishPowerCapSettings();
    AccessoryDataModel::PowerCap cap = m_model->powerCap();
    if (!cap.enabled) {
        return;
    }
    if (watts <= cap.watts) {
        // Re-arm: the output is back at or below the limit.
        m_alertArmed = true;
        cap.exceeded = false;
        m_model->setPowerCap(cap);
        return;
    }
    if (!m_alertArmed) {
        return;   // one alert per time over the limit
    }
    m_alertArmed = false;
    cap.exceeded = true;
    cap.alertText = powerCapAlertText(static_cast<int>(watts), cap.watts);
    ++cap.alertCount;
    m_model->setPowerCap(cap);
    qCInfo(lcStationAccessoryData) << cap.alertText;
}

void StationAccessoryData::applySetting(const QString& key)
{
    if (isInterlockKey(key)) {
        if (m_sources.interlock) {
            m_sources.interlock->reloadFromSettings();
        }
        return;
    }
    if (key.startsWith(QLatin1String("PGXL_PowerCap"))) {
        publishPowerCapSettings();
        return;
    }
    if (key.startsWith(QLatin1String("TGXL_TuneMemory_"))
        || key == QLatin1String("TGXL_AutoTuneMemoryRecall")) {
        publishTuneMemory();
        if (m_sources.tuneMemory) {
            m_sources.tuneMemory->notifyChanged();
        }
        return;
    }
    if ((key.startsWith(QLatin1String("TGXL_Ant")) || key.startsWith(QLatin1String("RfKit_Ant")))
        && key.endsWith(QLatin1String("_Label"))) {
        publishLabels();
        return;
    }
    // An app that clears a history by writing its key (every app before
    // the clearAccessoryFaults command): the Core's log follows it.
    const struct { const char* key; FaultLog* log; } logs[] = {
        { "PGXL_FaultHistory", m_sources.pgxlFaults },
        { "TGXL_FaultHistory", m_sources.tgxlFaults },
        { "RfKit_FaultHistory", m_sources.rfkitFaults },
    };
    for (const auto& entry : logs) {
        if (key == QLatin1String(entry.key) && entry.log != nullptr) {
            entry.log->reload();
            return;
        }
    }
}

bool StationAccessoryData::setInterlockPolicy(int mode, int graceMs, bool swrGateEnabled,
                                              double swrGateMax, QString* reason)
{
    if (!m_sources.interlock) {
        if (reason) {
            *reason = QStringLiteral("This Core has no transmit interlock.");
        }
        return false;
    }
    if (mode < 0 || mode > static_cast<int>(TxInterlockPolicy::Block)
        || graceMs < TxInterlockPolicy::kGraceMsMin || graceMs > TxInterlockPolicy::kGraceMsMax
        || !std::isfinite(swrGateMax) || swrGateMax < TxInterlockPolicy::kSwrGateMaxMin
        || swrGateMax > TxInterlockPolicy::kSwrGateMaxMax) {
        if (reason) {
            *reason = QStringLiteral("Choose an interlock mode, a grace period of 0 to 30000 ms "
                                     "and an SWR limit from 1.0 to 10.0.");
        }
        return false;
    }
    // Each setter saves its own setting and announces the change; the
    // policy's enforcement (MoxController) reads the new values from the
    // next transmit request on. Nothing here keys anything.
    TxInterlockPolicy* p = m_sources.interlock;
    p->setMode(static_cast<TxInterlockPolicy::Mode>(mode));
    p->setGraceMs(graceMs);
    p->setSwrGateEnabled(swrGateEnabled);
    p->setSwrGateMax(static_cast<float>(swrGateMax));
    publishInterlock();
    return true;
}

bool StationAccessoryData::setPowerCap(bool enabled, int watts, QString* reason)
{
    if (watts < kPowerCapMinW || watts > kPowerCapMaxW) {
        if (reason) {
            *reason = QStringLiteral("Choose an output limit from 100 to 2000 W.");
        }
        return false;
    }
    auto& s = AppSettings::instance();
    s.setValue(QStringLiteral("PGXL_PowerCapEnabled"),
               enabled ? QStringLiteral("True") : QStringLiteral("False"));
    s.setValue(QStringLiteral("PGXL_PowerCapW"), watts);
    publishPowerCapSettings();
    return true;
}

bool StationAccessoryData::clearFaults(const QString& device, QString* reason)
{
    FaultLog* log = nullptr;
    if (device == QLatin1String("pgxl")) {
        log = m_sources.pgxlFaults;
    } else if (device == QLatin1String("tgxl")) {
        log = m_sources.tgxlFaults;
    } else if (device == QLatin1String("rfkit")) {
        log = m_sources.rfkitFaults;
    }
    if (log == nullptr) {
        if (reason) {
            *reason = QStringLiteral("The request to clear the fault history was not understood.");
        }
        return false;
    }
    log->clear();
    return true;
}

} // namespace NereusSDR
