// BandwidthMonitorTab.cpp
//
// Source: ChannelMaster/bandwidth_monitor.h
//   - bandwidth_monitor_reset() (line 47): resets accumulated byte counters
//   - bandwidth_monitor_in(bytes) / bandwidth_monitor_out(bytes) (lines 48-49):
//       per-packet byte accounting for inbound/outbound streams
//
// The .h exposes a low-level C byte-accounting API. NereusSDR Phase 3I wires
// only the static controls (throttle threshold, auto-pause toggle) and
// provides stub live labels (default text "— Mbps" / "0 events"). The
// real-time feed from P1RadioConnection's bandwidth monitor is deferred
// to Phase 3L / Task 21.

#include "BandwidthMonitorTab.h"

#include "core/BoardCapabilities.h"
#include "core/RadioDiscovery.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>

namespace NereusSDR {

BandwidthMonitorTab::BandwidthMonitorTab(RadioModel* model, QWidget* parent)
    : QWidget(parent), m_model(model)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    outerLayout->setSpacing(8);

    // ── Live status group ─────────────────────────────────────────────────────
    // Source: bandwidth_monitor.h bandwidth_monitor_in/out (lines 48-49) —
    //   Phase 3I shows placeholder text; Task 21 / Phase 3L connects the feed.
    auto* statusGroup = new QGroupBox(tr("Live Status"), this);
    auto* statusForm  = new QFormLayout(statusGroup);
    statusForm->setLabelAlignment(Qt::AlignRight);

    m_currentRateLabel = new QLabel(QStringLiteral("— Mbps"), statusGroup);
    m_currentRateLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusForm->addRow(tr("LAN PHY rate:"), m_currentRateLabel);

    m_throttleEventLabel = new QLabel(QStringLiteral("0 events"), statusGroup);
    m_throttleEventLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    statusForm->addRow(tr("Throttle-triggered pauses:"), m_throttleEventLabel);

    outerLayout->addWidget(statusGroup);

    // ── Throttle settings group ───────────────────────────────────────────────
    auto* threshGroup = new QGroupBox(tr("Throttle Settings"), this);
    auto* threshForm  = new QFormLayout(threshGroup);
    threshForm->setLabelAlignment(Qt::AlignRight);

    // Throttle threshold spinbox 10..1000 Mbps, default 80 Mbps
    m_throttleThresholdSpin = new QSpinBox(threshGroup);
    m_throttleThresholdSpin->setRange(10, 1000);
    m_throttleThresholdSpin->setValue(80);
    m_throttleThresholdSpin->setSuffix(tr(" Mbps"));
    threshForm->addRow(tr("Throttle threshold:"), m_throttleThresholdSpin);

    // Auto-pause EP2 on throttle, default checked
    m_autoPauseCheck = new QCheckBox(tr("Auto-pause EP2 on throttle"), threshGroup);
    m_autoPauseCheck->setChecked(true);
    threshForm->addRow(m_autoPauseCheck);

    outerLayout->addWidget(threshGroup);
    outerLayout->addStretch();

    // ── Connections ───────────────────────────────────────────────────────────
    connect(m_throttleThresholdSpin,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int value) {
        emit settingChanged(QStringLiteral("bandwidthMonitor/throttleThresholdMbps"), value);
    });

    connect(m_autoPauseCheck, &QCheckBox::toggled, this, [this](bool checked) {
        emit settingChanged(QStringLiteral("bandwidthMonitor/autoPauseEp2"), checked);
    });
}

// ── populate ──────────────────────────────────────────────────────────────────

void BandwidthMonitorTab::populate(const RadioInfo& /*info*/, const BoardCapabilities& /*caps*/)
{
    // No board-specific gating required; live label updates come via
    // updateLiveStats() once Phase 3L / Task 21 wires the feed.
}

// ── updateLiveStats ───────────────────────────────────────────────────────────

void BandwidthMonitorTab::updateLiveStats(int currentMbps, int throttleEventCount)
{
    m_currentRateLabel->setText(
        QStringLiteral("%1 Mbps").arg(currentMbps));
    m_throttleEventLabel->setText(
        tr("%1 events").arg(throttleEventCount));
}

// ── restoreSettings ───────────────────────────────────────────────────────────

void BandwidthMonitorTab::restoreSettings(const QMap<QString, QVariant>& settings)
{
    auto it = settings.constFind(QStringLiteral("throttleThresholdMbps"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_throttleThresholdSpin);
        m_throttleThresholdSpin->setValue(it.value().toInt());
    }

    it = settings.constFind(QStringLiteral("autoPauseEp2"));
    if (it != settings.constEnd()) {
        QSignalBlocker blocker(m_autoPauseCheck);
        m_autoPauseCheck->setChecked(it.value().toBool());
    }
}

} // namespace NereusSDR
