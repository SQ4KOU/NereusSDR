// =================================================================
// src/gui/applets/AmpApplet.cpp  (NereusSDR)
// =================================================================
//
// Source attribution (AetherSDR, GPLv3):
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       per https://github.com/ten9876/AetherSDR (GPLv3)
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 section 5 requirements.
// =================================================================
// Modification history (NereusSDR):
//   2026-05-18  Ported in C++20/Qt6 for NereusSDR by J.J. Boyd (KG4VCF),
//                 with AI-assisted transformation via Anthropic Claude Code.
//                 Layout from AetherSDR src/gui/AmpApplet.{h,cpp} [@0cd4559].
//                 Changes from upstream: AppletWidget base; RadioModel* ctor;
//                 NereusSDR HGauge setter API replaces positional constructor.
//   2026-09-23  R-R3-21: on a remote-station model OPERATE and the
//                 Disconnect/Reconnect action are disabled with a plain
//                 reason; they act on this computer's own PGXL socket.
//                 The reason is public so the RF-Kit applet shares it.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-23  R-R3-47 / R-R3-22: the gauges, labels and OPERATE state
//                 read the RadioModel's AmplifierModel, which carries the
//                 one Power Genius conversion (formerly MainWindow's), in
//                 local and remote windows; a remote window shows a stale
//                 line when it loses the Core. J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
//   2026-09-24  R-R3-48: the band-follow line (paired with the radio or
//                 not). J.J. Boyd (KG4VCF), with AI-assisted implementation
//                 via Anthropic Claude Code.
//   2026-09-24  R-R3-22 / R-R3-47: a remote window's Disconnect and
//                 Reconnect ask the Core, which owns the Power Genius
//                 (disconnectPgxl, configurePgxl, remotePgxlControlVersion
//                 2); a status line shows the Core's connection as it
//                 changes and the plain reason for a refusal; an older
//                 Core leaves the item off with the reason. OPERATE stays
//                 with remote transmit. J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "AmpApplet.h"
#include "core/session/IStationLink.h"
#include "gui/OperatorReasonText.h"
#include "gui/HGauge.h"
#include "models/AmplifierModel.h"
#include "models/RadioModel.h"

#include <QContextMenuEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QVBoxLayout>

namespace NereusSDR {

// From AetherSDR src/gui/AmpApplet.cpp:11-77 [@0cd4559]
AmpApplet::AmpApplet(RadioModel* model, QWidget* parent)
    : AppletWidget(model, parent)
{
    auto* root = new QWidget(this);
    auto* topLayout = new QVBoxLayout(this);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);
    // Do NOT add appletTitleBar() here — AppletPanelWidget::wrapWithTitleBar
    // (AppletPanelWidget.cpp:155) already prepends a host-side title bar from
    // appletTitle(). Adding our own here results in a double header. Same fix
    // applied to PureSignalApplet (PureSignalApplet.cpp:156-160 comment).
    topLayout->addWidget(root);

    auto* vbox = new QVBoxLayout(root);
    // From AetherSDR src/gui/AmpApplet.cpp:14-16 [@0cd4559]
    vbox->setContentsMargins(4, 2, 4, 2);
    vbox->setSpacing(2);

    // Fwd Power gauge: 0-2000W
    // From AetherSDR src/gui/AmpApplet.cpp:18-22 [@0cd4559]
    m_fwdGauge = new HGauge(this);
    m_fwdGauge->setRange(0.0, 2000.0);
    m_fwdGauge->setYellowStart(1000.0);
    m_fwdGauge->setRedStart(1500.0);
    m_fwdGauge->setTitle(QStringLiteral("Fwd Pwr"));
    m_fwdGauge->setTickLabels({
        QStringLiteral("0"),
        QStringLiteral("500"),
        QStringLiteral("1000"),
        QStringLiteral("1.5k"),
        QStringLiteral("2k")
    });
    vbox->addWidget(m_fwdGauge);

    // SWR gauge: 1-3
    // From AetherSDR src/gui/AmpApplet.cpp:24-28 [@0cd4559]
    m_swrGauge = new HGauge(this);
    m_swrGauge->setRange(1.0, 3.0);
    m_swrGauge->setYellowStart(2.0);
    m_swrGauge->setRedStart(2.5);
    m_swrGauge->setTitle(QStringLiteral("SWR"));
    m_swrGauge->setValue(1.0);  // start at minimum (1:1)
    m_swrGauge->setTickLabels({
        QStringLiteral("1"),
        QStringLiteral("1.5"),
        QStringLiteral("2"),
        QStringLiteral("2.5"),
        QStringLiteral("3")
    });
    vbox->addWidget(m_swrGauge);

    // Temp gauge: 0-100 deg C
    // From AetherSDR src/gui/AmpApplet.cpp:30-34 [@0cd4559]
    m_tempGauge = new HGauge(this);
    m_tempGauge->setRange(0.0, 100.0);
    m_tempGauge->setYellowStart(55.0);
    m_tempGauge->setRedStart(80.0);
    m_tempGauge->setTitle(QStringLiteral("Temp"));
    m_tempGauge->setUnit(QStringLiteral("°C"));
    m_tempGauge->setTickLabels({
        QStringLiteral("0"),
        QStringLiteral("30"),
        QStringLiteral("55"),
        QStringLiteral("80"),
        QStringLiteral("100")
    });
    vbox->addWidget(m_tempGauge);

    // From AetherSDR src/gui/AmpApplet.cpp:36 [@0cd4559]
    vbox->addSpacing(8);

    // PGXL direct telemetry -- stacked labels + OPERATE button
    // From AetherSDR src/gui/AmpApplet.cpp:37-39 [@0cd4559]
    static const char* kLabelStyle =
        "QLabel { color: #c8d8e8; font-size: 10px; }";

    // From AetherSDR src/gui/AmpApplet.cpp:41-76 [@0cd4559]
    auto* telRow = new QHBoxLayout;
    telRow->setSpacing(4);

    auto* telStack = new QVBoxLayout;
    telStack->setSpacing(0);
    telStack->setContentsMargins(0, 0, 0, 0);

    m_powerLabel = new QLabel(root);
    m_powerLabel->setStyleSheet(QLatin1String(kLabelStyle));
    m_powerLabel->hide();
    telStack->addWidget(m_powerLabel);

    m_meffLabel = new QLabel(root);
    m_meffLabel->setStyleSheet(QLatin1String(kLabelStyle));
    m_meffLabel->hide();
    telStack->addWidget(m_meffLabel);

    telRow->addLayout(telStack);

    // From AetherSDR src/gui/AmpApplet.cpp:60 [@0cd4559]
    telRow->addSpacing(30);

    // From AetherSDR src/gui/AmpApplet.cpp:62-74 [@0cd4559]
    m_operateBtn = new QPushButton(QStringLiteral("OPERATE"), root);
    m_operateBtn->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_operateBtn->setStyleSheet(
        QStringLiteral(
            "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
            "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
            "QPushButton:hover { background: #204060; }"));
    m_operateBtn->hide();
    connect(m_operateBtn, &QPushButton::clicked, this, [this]() {
        // Toggle: if currently in OPERATE state -> request standby, else -> request operate.
        // From AetherSDR src/gui/AmpApplet.cpp:70-72 [@0cd4559]
        bool isOp = (m_operateBtn->text() == QStringLiteral("OPERATE"));
        emit operateToggled(!isOp);
    });
    telRow->addWidget(m_operateBtn, 1);

    vbox->addLayout(telRow);

    // R-R3-48: whether the amp follows the radio's band (it does once it
    // is paired with the radio), in local and remote windows alike.
    m_bandFollowLabel = new QLabel(root);
    m_bandFollowLabel->setObjectName(QStringLiteral("ampBandFollowLabel"));
    m_bandFollowLabel->setTextFormat(Qt::PlainText);
    m_bandFollowLabel->setWordWrap(true);
    m_bandFollowLabel->setStyleSheet(QStringLiteral("color: #9aa5b1; font-size: 10px;"));
    vbox->addWidget(m_bandFollowLabel);

    // R-R3-47: a remote window's readings come from the Core; this line
    // says when they are not live.
    m_staleLabel = new QLabel(root);
    m_staleLabel->setTextFormat(Qt::PlainText);
    m_staleLabel->setWordWrap(true);
    m_staleLabel->setStyleSheet(QStringLiteral("color: #d9a441; font-size: 10px;"));
    m_staleLabel->setVisible(false);
    vbox->addWidget(m_staleLabel);

    // R-R3-22: a remote window's line for the Core's connection to the amp
    // and the reason a Disconnect or Reconnect was not taken.
    m_connectionLabel = new QLabel(root);
    m_connectionLabel->setObjectName(QStringLiteral("ampConnectionLabel"));
    m_connectionLabel->setTextFormat(Qt::PlainText);
    m_connectionLabel->setWordWrap(true);
    m_connectionLabel->setStyleSheet(QStringLiteral("color: #9aa5b1; font-size: 10px;"));
    m_connectionLabel->setVisible(false);
    vbox->addWidget(m_connectionLabel);

    // R-R3-47: the gauges follow the RadioModel's AmplifierModel: the
    // Core's `amplifier` object in a remote window, the same object fed
    // by this computer's own PgxlConnection in a local one.
    if (m_model) {
        m_amp = m_model->amplifierModel();
        if (m_amp) {
            connect(m_amp, &AmplifierModel::statusChanged,
                    this, &AmpApplet::syncFromAmplifier);
            connect(m_amp, &AmplifierModel::bandFollowChanged,
                    this, &AmpApplet::syncFromAmplifier);
            connect(m_amp, &AmplifierModel::stationConnectionChanged,
                    this, &AmpApplet::updateConnectionLine);
            m_lastPhase = m_amp->connectionPhase();
        }
        connect(m_model, &RadioModel::stationLinkStateChanged,
                this, &AmpApplet::updateStationState);
        connect(m_model, &RadioModel::accessoryRequestRefused,
                this, &AmpApplet::onAccessoryRequestRefused);
        syncFromAmplifier();
        updateStationState();
    }

    // R-R3-21: OPERATE drives this computer's own PgxlConnection
    // (MainWindow's operateToggled handler), which a remote window never
    // connects: the amplifier sits at the station.
    if (m_model && !m_model->ownsLocalDsp()) {
        m_operateBtn->setEnabled(false);
        m_operateBtn->setToolTip(remoteUnavailableReason());
    }
}

QString AmpApplet::remoteUnavailableReason()
{
    return tr("Amplifier control is not available from a remote window yet.");
}

// R-R3-22 / R-R3-47: the words the 4O3A page's remote Power Genius tab
// uses for the same phases.
QString AmpApplet::stationConnectionText(TunerModel::ConnectionPhase phase,
                                         const QString& error)
{
    using Phase = TunerModel::ConnectionPhase;
    const QString reason = error.isEmpty() ? QString() : OperatorReasonText::forDisplay(error);
    switch (phase) {
    case Phase::Disabled:     return tr("Disabled at station");
    case Phase::Disconnected: return tr("Disconnected");
    case Phase::Discovering:  return tr("Discovering at station");
    case Phase::Connecting:   return tr("Connecting at station");
    case Phase::Identifying:  return tr("Identifying device");
    case Phase::Retrying:
        return reason.isEmpty() ? tr("Retrying at station")
                                : tr("Retrying at station: %1").arg(reason);
    case Phase::Connected:    return tr("Connected");
    case Phase::Error:
        return reason.isEmpty() ? tr("Stopped at station")
                                : tr("Error: %1").arg(reason);
    }
    return QString();
}

bool AmpApplet::stationConnectionActive(TunerModel::ConnectionPhase phase)
{
    using Phase = TunerModel::ConnectionPhase;
    return phase == Phase::Connected || phase == Phase::Discovering
        || phase == Phase::Connecting || phase == Phase::Identifying
        || phase == Phase::Retrying;
}

bool AmpApplet::isRemoteWindow() const
{
    return m_model && !m_model->ownsLocalDsp();
}

// R-R3-22: the Core's phase, or the reason the applet's own request was
// not taken, while the Core reports its amp; the stale line covers the rest.
void AmpApplet::updateConnectionLine()
{
    if (!m_connectionLabel) {
        return;
    }
    if (m_amp && m_amp->connectionPhase() != m_lastPhase) {
        // The Core moved: a request that was waiting has been taken.
        m_lastPhase = m_amp->connectionPhase();
        m_requestPending = false;
        m_requestReason.clear();
    }
    const IStationLink* link = isRemoteWindow() ? m_model->stationLink() : nullptr;
    if (!m_amp || !link || !link->stationLinkReady()
        || !link->remoteAmplifierStatusAvailable()) {
        m_connectionLabel->clear();
        m_connectionLabel->setVisible(false);
        return;
    }
    m_connectionLabel->setText(m_requestReason.isEmpty()
        ? stationConnectionText(m_amp->connectionPhase(), m_amp->connectionError())
        : m_requestReason);
    m_connectionLabel->setVisible(true);
}

// R-R3-22: the Core refused the applet's own Disconnect or Reconnect.
// Other Power Genius requests (the Setup pages') show where they were sent.
void AmpApplet::onAccessoryRequestRefused(const QString& device, const QString& reason)
{
    if (device != QLatin1String("pgxl") || !m_requestPending) {
        return;
    }
    m_requestPending = false;
    m_requestReason = OperatorReasonText::forDisplay(reason);
    updateConnectionLine();
}

void AmpApplet::requestRemoteConnectionToggle()
{
    IStationLink* link = m_model ? m_model->stationLink() : nullptr;
    if (!link || !m_amp || !link->remotePgxlControlAvailable()) {
        return;
    }
    const auto outcome = stationConnectionActive(m_amp->connectionPhase())
        ? link->requestDisconnectPgxl()
        : link->requestConfigurePgxl(m_amp->configuredHost(),
                                     static_cast<quint16>(m_amp->configuredPort()));
    m_requestPending = outcome.sent;
    m_requestReason = outcome.sent ? QString() : OperatorReasonText::forDisplay(outcome.reason);
    updateConnectionLine();
}

QString AmpApplet::connectionLineTextForTesting() const
{
    return m_connectionLabel && !m_connectionLabel->isHidden() ? m_connectionLabel->text()
                                                               : QString();
}

// R-R3-47: every reading the AmplifierModel holds, each time it changes.
// Nothing is shown before the amp's first reading, as before; after it the
// values are shown as they are, zeros and STANDBY included.
void AmpApplet::syncFromAmplifier()
{
    if (m_amp && m_bandFollowLabel) {
        m_bandFollowLabel->setText(m_amp->bandFollowText());
    }
    if (!m_amp || !m_amp->present()) {
        return;
    }
    setTemp(static_cast<float>(m_amp->temperatureC()));
    setDrainCurrent(static_cast<float>(m_amp->drainCurrentA()));
    setMainsVoltage(qRound(m_amp->mainsVoltageV()));
    if (!m_amp->deviceState().isEmpty()) {
        setState(m_amp->deviceState());
    }
    if (!m_amp->efficiencyText().isEmpty()) {
        setMeff(m_amp->efficiencyText());
    }
    setFwdPower(static_cast<float>(m_amp->forwardPowerW()));
    setSwr(static_cast<float>(m_amp->swr()));
}

void AmpApplet::updateStationState()
{
    if (!m_staleLabel) {
        return;
    }
    if (!m_model || m_model->ownsLocalDsp()) {
        m_staleLabel->setVisible(false);
        return;
    }
    const IStationLink* link = m_model->stationLink();
    if (!link || !link->stationLinkReady()) {
        m_staleLabel->setText(tr("Core disconnected. Power Genius readings are stale."));
        m_staleLabel->setVisible(true);
    } else if (!link->remoteAmplifierStatusAvailable()) {
        m_staleLabel->setText(tr("This Core does not report its Power Genius to this app. "
                                 "Updating the Core may help."));
        m_staleLabel->setVisible(true);
    } else {
        m_staleLabel->setVisible(false);
    }
    updateConnectionLine();
}

double AmpApplet::fwdGaugeValueForTesting() const { return m_fwdGauge->value(); }
double AmpApplet::swrGaugeValueForTesting() const { return m_swrGauge->value(); }
double AmpApplet::tempGaugeValueForTesting() const { return m_tempGauge->value(); }
QString AmpApplet::powerLabelTextForTesting() const { return m_powerLabel->text(); }
QString AmpApplet::meffLabelTextForTesting() const { return m_meffLabel->text(); }
QString AmpApplet::operateButtonTextForTesting() const { return m_operateBtn->text(); }
bool AmpApplet::operateButtonShownForTesting() const { return !m_operateBtn->isHidden(); }
bool AmpApplet::staleIndicatorVisibleForTesting() const
{
    return m_staleLabel && !m_staleLabel->isHidden();
}
QString AmpApplet::bandFollowTextForTesting() const
{
    return m_bandFollowLabel ? m_bandFollowLabel->text() : QString();
}

QString AmpApplet::staleIndicatorTextForTesting() const
{
    return m_staleLabel ? m_staleLabel->text() : QString();
}

// From AetherSDR src/gui/AmpApplet.cpp:79-82 [@0cd4559]
void AmpApplet::setFwdPower(float watts)
{
    m_fwdGauge->setValue(static_cast<double>(watts));
}

// From AetherSDR src/gui/AmpApplet.cpp:84-87 [@0cd4559]
void AmpApplet::setSwr(float swr)
{
    m_swrGauge->setValue(static_cast<double>(swr));
}

// From AetherSDR src/gui/AmpApplet.cpp:89-92 [@0cd4559]
void AmpApplet::setTemp(float degC)
{
    m_tempGauge->setValue(static_cast<double>(degC));
}

// From AetherSDR src/gui/AmpApplet.cpp:94-98 [@0cd4559]
void AmpApplet::setDrainCurrent(float amps)
{
    m_drainAmps = amps;
    updatePowerLabel();
}

// From AetherSDR src/gui/AmpApplet.cpp:100-104 [@0cd4559]
void AmpApplet::setMainsVoltage(int volts)
{
    m_mainsVolts = volts;
    updatePowerLabel();
}

// From AetherSDR src/gui/AmpApplet.cpp:106-113 [@0cd4559]
void AmpApplet::updatePowerLabel()
{
    m_powerLabel->setText(
        QStringLiteral("Volts: %1V  Amps: %2A")
            .arg(m_mainsVolts)
            .arg(static_cast<double>(m_drainAmps), 0, 'f', 1));
    m_powerLabel->show();
}

// From AetherSDR src/gui/AmpApplet.cpp:115-135 [@0cd4559]
void AmpApplet::setState(const QString& state)
{
    // Match TGXL OPERATE button style: green when operating, default when standby.
    // PGXL states: IDLE (ready), TRANSMIT_A/TRANSMIT_B (keyed), OPERATE, STANDBY, POWERUP, FAULT
    bool operating = (state == QStringLiteral("IDLE")
                      || state == QStringLiteral("OPERATE")
                      || state.startsWith(QStringLiteral("TRANSMIT")));

    // 2026-05-22 bench fix: cache the keyed sub-state for
    // isTransmitting() so the PGXL status handler in MainWindow can
    // gate the latched peakfwd / swr writes.
    m_isTransmitting = state.startsWith(QStringLiteral("TRANSMIT"));
    if (operating) {
        m_operateBtn->setText(QStringLiteral("OPERATE"));
        m_operateBtn->setStyleSheet(
            QStringLiteral(
                "QPushButton { background: #006030; border: 1px solid #008040; "
                "border-radius: 3px; color: #ffffff; font-size: 10px; font-weight: bold; }"
                "QPushButton:hover { background: #007040; }"));
    } else {
        m_operateBtn->setText(QStringLiteral("STANDBY"));
        m_operateBtn->setStyleSheet(
            QStringLiteral(
                "QPushButton { background: #1a3a5a; border: 1px solid #205070; "
                "border-radius: 3px; color: #c8d8e8; font-size: 10px; font-weight: bold; }"
                "QPushButton:hover { background: #204060; }"));
    }
    m_operateBtn->show();
}

// From AetherSDR src/gui/AmpApplet.cpp:137-141 [@0cd4559]
void AmpApplet::setMeff(const QString& meff)
{
    m_meffLabel->setText(QStringLiteral("MEffA:   %1").arg(meff));
    m_meffLabel->show();
}


// Phase 3P-II Phase 4 Task 88: update PGXL connected flag for context menu.
void AmpApplet::setPgxlConnected(bool connected)
{
    m_pgxlConnected = connected;
}

// Phase 3P-II Phase 4 Task 88: right-click context menu.
// Menu structure per design doc ss5.9:
//   Open PGXL Advanced...        -> navigationRequested("pgxlAdvanced")
//   (separator)
//   Disconnect / Reconnect        -> connectionToggleRequested()
//   Copy diagnostics to clipboard -> diagnosticsCopyRequested()
void AmpApplet::contextMenuEvent(QContextMenuEvent* ev)
{
    QMenu* menu = buildContextMenu(this);
    menu->exec(ev->globalPos());
    menu->deleteLater();
}

QMenu* AmpApplet::buildContextMenu(QObject* menuParent)
{
    auto* menu = new QMenu(qobject_cast<QWidget*>(menuParent));

    // Open PGXL Advanced...
    auto* openAdvancedAction = menu->addAction(QStringLiteral("Open PGXL Advanced..."));
    connect(openAdvancedAction, &QAction::triggered, this, [this]() {
        emit navigationRequested(QStringLiteral("pgxlAdvanced"));
    });

    menu->addSeparator();

    // Disconnect or Reconnect depending on current state
    if (!isRemoteWindow()) {
        const QString toggleLabel = m_pgxlConnected
            ? QStringLiteral("Disconnect")
            : QStringLiteral("Reconnect");
        auto* toggleAction = menu->addAction(toggleLabel);
        connect(toggleAction, &QAction::triggered, this, [this]() {
            emit connectionToggleRequested();
        });
    } else {
        // R-R3-22 / R-R3-47: the Core owns the amp's connection. Disconnect
        // (or cancel an attempt) and Reconnect to its saved address go to
        // the Core; nothing here opens a connection of this computer's own.
        const bool active = m_amp && stationConnectionActive(m_amp->connectionPhase());
        auto* toggleAction = menu->addAction(active ? QStringLiteral("Disconnect")
                                                    : QStringLiteral("Reconnect"));
        const IStationLink* link = m_model->stationLink();
        QString unavailable;
        if (link && !link->stationLinkReady()) {
            unavailable = tr("Waiting for the Core to connect.");
        } else if (!link || !link->remotePgxlControlAvailable()) {
            unavailable = tr("This Core does not offer Power Genius XL control to this app.");
        } else if (!active && (!m_amp || m_amp->configuredHost().isEmpty()
                               || m_amp->configuredPort() <= 0)) {
            unavailable = tr("Enter the Power Genius address in Setup first.");
        }
        if (!unavailable.isEmpty()) {
            toggleAction->setEnabled(false);
            toggleAction->setToolTip(unavailable);
            menu->setToolTipsVisible(true);
        }
        connect(toggleAction, &QAction::triggered,
                this, &AmpApplet::requestRemoteConnectionToggle);
    }

    // Copy diagnostics to clipboard
    auto* copyDiagAction = menu->addAction(QStringLiteral("Copy diagnostics to clipboard"));
    connect(copyDiagAction, &QAction::triggered, this, [this]() {
        emit diagnosticsCopyRequested();
    });

    return menu;
}
} // namespace NereusSDR
