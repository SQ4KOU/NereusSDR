// =================================================================
// src/gui/setup/FourO3APage.cpp  (NereusSDR)
// =================================================================
//
// See FourO3APage.h for the design overview.  Implementation notes:
//
//   - The General tab is hand-built (master toggle + status row +
//     embedded pages).  The other 3 tabs simply addTab() an existing
//     QWidget subclass; the master toggle gates whether they're
//     interactive via QTabWidget::setTabEnabled.
//
//   - FlexAPI status refresh runs on a 1 Hz timer so the listening
//     status reflects live changes (e.g. after the master toggle
//     starts/stops the listener).
//
//   - PeripheralsPage and PgxlInterlockPage already manage their own
//     state through AppSettings + RadioModel signals.  They drop in
//     unchanged.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-05-21 -- Created in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

#include "FourO3APage.h"

#include "CatNetworkSetupPages.h"   // PeripheralsPage
#include "PgxlInterlockPage.h"
#include "PgxlAdvancedPage.h"
#include "TgxlAdvancedPage.h"

#include "core/SmartSdrApiListener.h"
#include "core/session/IStationLink.h"
#include "models/RadioModel.h"

#include <QCheckBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace NereusSDR {

FourO3APage::FourO3APage(RadioModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    m_tabs = new QTabWidget(this);
    root->addWidget(m_tabs);

    // Tab 1: General.  Hand-built composite that hosts the master
    // toggle, FlexAPI status row, and embedded Peripherals / PGXL
    // interlock pages.
    m_tabs->addTab(buildGeneralTab(), tr("General"));

    // Tab 2: PowerGenius XL.  Embed the existing PgxlAdvancedPage as-is;
    // its content is already structured per the mockup (identity,
    // operation, telemetry, fault history).  Page constructor takes
    // a RadioModel pointer so it can wire to PgxlConnection signals.
    if (m_model && m_model->role() == RadioModel::Role::Remote) {
        // Advanced pages bind local accessory sockets. Keep their places
        // visibly unavailable until remote administration is implemented.
        m_tabs->addTab(new QWidget(m_tabs), tr("PowerGenius XL"));
        m_tabs->addTab(new QWidget(m_tabs), tr("Tuner Genius XL"));
        for (int i = 1; i < m_tabs->count(); ++i) {
            m_tabs->setTabToolTip(i, tr("Remote accessory administration is not available yet."));
        }
    } else {
        m_pgxlAdvancedPage = new PgxlAdvancedPage(m_model);
        m_tabs->addTab(m_pgxlAdvancedPage, tr("PowerGenius XL"));

        // Tab 3: Tuner Genius XL.  Same pattern as Tab 2.
        m_tgxlAdvancedPage = new TgxlAdvancedPage(m_model);
        m_tabs->addTab(m_tgxlAdvancedPage, tr("Tuner Genius XL"));
    }

    // 2026-05-22 menu cleanup: Diagnostics tab removed. Connection
    // State duplicated the General tab's FlexAPI status row and the
    // per-peer labels on PGXL/TGXL tabs; Disconnect/Reconnect Log
    // duplicated the rolling NereusSDR log file. Both removed for
    // bench-driven simplification (operator can read the log file or
    // PGXL/TGXL detail tabs for the same data).

    // Apply current master-gate state to the detail tabs at
    // construction time so a cold-open with FourO3A_Enabled=False
    // shows the tabs greyed out.
    const bool initialEnabled = m_model && m_model->fourO3AEnabled();
    applyMasterGateToTabs(initialEnabled);

    // Per-radio peripherals refactor (2026-05-26): repaint the banner
    // and refresh the master toggle each time the connection state
    // changes, so the page always reflects the connected radio's
    // per-MAC scope.
    if (m_model) {
        connect(m_model, &RadioModel::connectionStateChanged,
                this, &FourO3APage::refreshConnectionBanner);
        connect(m_model, &RadioModel::stationLinkStateChanged,
                this, &FourO3APage::refreshConnectionBanner);
        connect(m_model, &RadioModel::fourO3AStatusChanged,
                this, [this]() {
                    refreshConnectionBanner();
                    refreshFlexApiStatus();
                });
        connect(m_model, &RadioModel::stationFourO3ACommandFinished,
                this, &FourO3APage::onStationFourO3ACommandFinished);
    }
    refreshConnectionBanner();  // initial paint

    // Periodic FlexAPI status refresh (1 Hz).  Captures live state
    // changes including post-toggle start/stop and any external
    // listener errors.
    auto* statusTimer = new QTimer(this);
    statusTimer->setInterval(1000);
    connect(statusTimer, &QTimer::timeout,
            this, &FourO3APage::refreshFlexApiStatus);
    statusTimer->start();
    refreshFlexApiStatus();  // initial paint
}

QWidget* FourO3APage::buildGeneralTab()
{
    auto* tab = new QWidget(this);
    auto* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(0, 8, 0, 0);
    layout->setSpacing(12);

    // Per-radio peripherals refactor (2026-05-26): banner row at the top
    // announces whose peripheral scope is being edited.  Filled by
    // refreshConnectionBanner().
    m_connectionBanner = new QLabel(tab);
    m_connectionBanner->setWordWrap(true);
    m_connectionBanner->setStyleSheet(
        QStringLiteral("color:#ffcc66; font-size:11px; font-weight:bold;"));
    layout->addWidget(m_connectionBanner);

    // ── Master Switch ─────────────────────────────────────────────
    auto* masterBox = new QGroupBox(tr("Master Switch"), tab);
    auto* masterLayout = new QVBoxLayout(masterBox);
    m_masterToggle = new QCheckBox(tr("Enable 4O3A integration"), masterBox);
    m_masterToggle->setObjectName(QStringLiteral("fourO3AMasterToggle"));
    m_masterToggle->setToolTip(
        tr("Gates the FlexAPI listener on TCP 4992 and the PGXL / TGXL "
           "auto-connect paths.  Off by default; turn on only when you "
           "want NereusSDR to expose itself to 4O3A amps and tuners on "
           "your local network."));
    m_masterToggle->setChecked(m_model && m_model->fourO3AEnabled());
    connect(m_masterToggle, &QCheckBox::toggled,
            this, &FourO3APage::onMasterToggled);
    masterLayout->addWidget(m_masterToggle);

    auto* masterHelp = new QLabel(
        tr("When enabled: FlexAPI listener binds TCP 4992; PowerGenius XL "
           "and Tuner Genius XL pages become interactive; PGXL/TGXL "
           "auto-connect runs at startup.  When disabled: no port is "
           "bound, no outbound connection attempts, and the detail tabs "
           "below are greyed out."),
        masterBox);
    masterHelp->setWordWrap(true);
    masterHelp->setStyleSheet(QStringLiteral("color: #888; font-size: 11px;"));
    masterLayout->addWidget(masterHelp);
    layout->addWidget(masterBox);

    // ── FlexAPI Listener Status ───────────────────────────────────
    auto* statusBox = new QGroupBox(tr("FlexAPI Listener"), tab);
    auto* statusLayout = new QHBoxLayout(statusBox);
    m_flexApiStatusLabel = new QLabel(tr("Status: \xE2\x97\x8B Idle"), statusBox);
    m_flexApiStatusLabel->setObjectName(QStringLiteral("fourO3AListenerStatus"));
    m_flexApiStatusLabel->setToolTip(
        tr("Live state of the TCP 4992 SmartSDR API listener.  Reflects "
           "the master toggle above plus any external bind errors."));
    statusLayout->addWidget(m_flexApiStatusLabel);
    statusLayout->addStretch();
    layout->addWidget(statusBox);

    // ── PGXL / TGXL Connection ────────────────────────────────────
    // PeripheralsPage already implements the per-row IP / port /
    // Connect button pattern; embedding it preserves its existing
    // AppSettings wire-up and PgxlConnection / TgxlConnection signal
    // bindings without duplication.
    m_peripheralsPage = new PeripheralsPage(m_model, tab);
    layout->addWidget(m_peripheralsPage);

    // ── PGXL Interlock ────────────────────────────────────────────
    if (!m_model || m_model->role() != RadioModel::Role::Remote) {
        m_pgxlInterlockPage = new PgxlInterlockPage(m_model, tab);
        layout->addWidget(m_pgxlInterlockPage);
    }

    layout->addStretch();
    return tab;
}

void FourO3APage::onMasterToggled(bool checked)
{
    if (!m_model) {
        return;
    }
    if (m_model->role() == RadioModel::Role::Remote) {
        auto* const link = m_model->stationLink();
        const bool available = link && link->remoteFourO3AControlAvailable()
            && !m_model->currentRadioMac().isEmpty();
        if (!available) {
            m_remoteMasterResultIsError = true;
            m_remoteMasterResult = tr("Core 4O3A control is unavailable for this session.");
            refreshConnectionBanner();
            refreshFlexApiStatus();
            return;
        }
        const IStationLink::CommandOutcome outcome = link->requestFourO3AEnabled(checked);
        if (outcome.sent) {
            m_remoteMasterPending = true;
            m_remoteMasterResultIsError = false;
            m_remoteMasterResult.clear();
        } else {
            m_remoteMasterPending = false;
            m_remoteMasterResultIsError = true;
            m_remoteMasterResult = outcome.reason;
        }
        // The QCheckBox has already followed the click. Restore the Core
        // snapshot immediately: no local listener or preference is changed
        // while a request is in flight.
        refreshConnectionBanner();
        refreshFlexApiStatus();
        return;
    }
    m_model->setFourO3AEnabled(checked);
    applyMasterGateToTabs(checked);
    refreshFlexApiStatus();  // immediate paint so the dot reflects new state

    // Greying behaviour also applies to the embedded sections on the
    // General tab; the master toggle stays enabled but everything
    // below it disables when the gate is off.  Keeps the master
    // toggle reachable so the operator can flip it back on.
    if (m_peripheralsPage) { m_peripheralsPage->setEnabled(checked); }
    if (m_pgxlInterlockPage) { m_pgxlInterlockPage->setEnabled(checked); }
}

void FourO3APage::onStationFourO3ACommandFinished(bool accepted, const QString& reason)
{
    m_remoteMasterPending = false;
    m_remoteMasterResultIsError = !accepted;
    m_remoteMasterResult = accepted
        ? tr("Core accepted the request; waiting for its listener status.")
        : (reason.isEmpty()
               ? tr("Core refused the 4O3A request.")
               : reason);
    refreshConnectionBanner();
    refreshFlexApiStatus();
}

void FourO3APage::applyMasterGateToTabs(bool enabled)
{
    if (!m_tabs) { return; }
    if (m_model && m_model->role() == RadioModel::Role::Remote) {
        for (int i = 1; i < m_tabs->count(); ++i) { m_tabs->setTabEnabled(i, false); }
        // Core refuses configure when its master is off. The row still needs
        // to show that reason and let an operator cancel existing work.
        if (m_peripheralsPage) { m_peripheralsPage->setEnabled(true); }
        return;
    }
    // Tab 0 (General) stays enabled so the master toggle is always
    // reachable; tabs 1, 2, 3 (PowerGenius XL / Tuner Genius XL /
    // Diagnostics) gate on the master state.
    for (int i = 1; i < m_tabs->count(); ++i) {
        m_tabs->setTabEnabled(i, enabled);
    }
    // Also propagate to General-tab embedded sections so the IP/port
    // grid + interlock controls grey out (kept visible for context).
    if (m_peripheralsPage) { m_peripheralsPage->setEnabled(enabled); }
    if (m_pgxlInterlockPage) { m_pgxlInterlockPage->setEnabled(enabled); }
}

void FourO3APage::refreshConnectionBanner()
{
    if (!m_connectionBanner) {
        return;
    }
    if (m_model && m_model->role() == RadioModel::Role::Remote) {
        const auto* const link = m_model->stationLink();
        const bool ready = link && link->remoteFourO3AControlAvailable();
        const bool haveMac = !m_model->currentRadioMac().isEmpty();
        if (!ready || !haveMac) {
            // A result from the retired session will never arrive. Do not
            // carry its pending latch into the next authenticated session.
            m_remoteMasterPending = false;
            m_remoteMasterResult.clear();
            m_remoteMasterResultIsError = false;
        }
        m_connectionBanner->setText(ready && haveMac
            ? tr("4O3A integration is managed by Core for %1.").arg(m_model->currentRadioMac())
            : tr("4O3A integration is managed by Core; wait for a ready station session."));
        if (m_masterToggle) {
            const QSignalBlocker blocker(m_masterToggle);
            m_masterToggle->setChecked(m_model->fourO3AEnabled());
            m_masterToggle->setEnabled(ready && haveMac && !m_remoteMasterPending);
            if (m_remoteMasterPending) {
                m_masterToggle->setText(tr("Enable 4O3A integration (request pending)"));
                m_masterToggle->setToolTip(tr("Waiting for Core to acknowledge and mirror the 4O3A state."));
            } else {
                m_masterToggle->setText(tr("Enable 4O3A integration"));
                m_masterToggle->setToolTip(ready && haveMac
                    ? tr("Ask Core to change its 4O3A master switch. The check state follows Core's snapshot.")
                    : tr("Core 4O3A control requires a ready session and connected station radio."));
            }
        }
        applyMasterGateToTabs(false);
        return;
    }
    // Use the per-MAC scope as the "connected" gate so unit tests that
    // pin a MAC via setLastRadioInfoForTest + setConnectionStateForTest
    // (without a live RadioConnection object) still drive the right
    // banner state.
    const QString mac     = m_model ? m_model->currentRadioMac() : QString{};
    const bool    haveMac = !mac.isEmpty();
    if (haveMac) {
        const QString name = m_model->name();
        m_connectionBanner->setText(
            tr("Editing peripherals for %1 (%2)").arg(name, mac));
        m_connectionBanner->setStyleSheet(
            QStringLiteral("color:#7ec850; font-size:11px; font-weight:bold;"));
    } else {
        m_connectionBanner->setText(
            tr("Connect to a radio to edit its peripherals."));
        m_connectionBanner->setStyleSheet(
            QStringLiteral("color:#ffcc66; font-size:11px; font-weight:bold;"));
    }
    // Master toggle reflects the per-MAC FourO3A_Enabled flag.  Block
    // signals during the refresh so the in-progress assignment doesn't
    // re-enter setFourO3AEnabled and inadvertently flip the listener
    // state.  Also gate enabled-state on the per-MAC scope availability.
    if (m_masterToggle) {
        m_masterToggle->setEnabled(haveMac);
        m_masterToggle->blockSignals(true);
        m_masterToggle->setChecked(m_model && m_model->fourO3AEnabled());
        m_masterToggle->blockSignals(false);
    }
    // Detail tabs follow the (now possibly-changed) master flag, but
    // also grey out when no radio is connected so the operator can't
    // edit a per-MAC scope that doesn't exist yet.
    applyMasterGateToTabs(m_model && m_model->fourO3AEnabled());
    if (m_peripheralsPage) {
        m_peripheralsPage->setEnabled(haveMac
                                      && m_model->fourO3AEnabled());
    }
    if (m_pgxlInterlockPage) {
        m_pgxlInterlockPage->setEnabled(haveMac
                                        && m_model->fourO3AEnabled());
    }
}

void FourO3APage::refreshFlexApiStatus()
{
    if (!m_flexApiStatusLabel) { return; }
    if (m_model && m_model->role() == RadioModel::Role::Remote) {
        const auto* const link = m_model->stationLink();
        const bool ready = link && link->remoteFourO3AControlAvailable()
            && !m_model->currentRadioMac().isEmpty();
        if (!ready) {
            m_flexApiStatusLabel->setText(tr("Status: \xE2\x97\x8B Core session unavailable"));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #888;"));
        } else if (m_model->fourO3AListening()) {
            m_flexApiStatusLabel->setText(tr("Status: \xE2\x97\x8F Core listening on TCP 4992"));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #4CAF50;"));
        } else if (!m_model->fourO3AListenerError().isEmpty()) {
            m_flexApiStatusLabel->setText(
                tr("Status: \xE2\x97\x8F Core listener error: %1")
                    .arg(m_model->fourO3AListenerError()));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #E53935;"));
        } else if (m_remoteMasterResultIsError && !m_remoteMasterResult.isEmpty()) {
            m_flexApiStatusLabel->setText(tr("Status: %1").arg(m_remoteMasterResult));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #E53935;"));
        } else if (m_model->fourO3AEnabled()) {
            m_flexApiStatusLabel->setText(tr("Status: \xE2\x97\x8B Core listener starting"));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #ffcc66;"));
        } else {
            m_flexApiStatusLabel->setText(tr("Status: \xE2\x97\x8B Disabled at Core"));
            m_flexApiStatusLabel->setStyleSheet(QStringLiteral("color: #888;"));
        }
        m_flexApiStatusLabel->setToolTip(
            tr("Core owns the TCP 4992 listener. This remote GUI only displays its mirrored status."));
        return;
    }
    SmartSdrApiListener* listener =
        m_model ? m_model->smartSdrListener() : nullptr;
    const bool listening = listener && listener->isListening();
    if (listening) {
        m_flexApiStatusLabel->setText(
            tr("Status: \xE2\x97\x8F Listening on TCP 4992"));
        m_flexApiStatusLabel->setStyleSheet(
            QStringLiteral("color: #4CAF50;"));  // green
    } else {
        const bool gateOn = m_model && m_model->fourO3AEnabled();
        if (gateOn) {
            // Master is ON but the listener isn't bound -- bind failure
            // (e.g. port in use).  Flag it red so the operator notices.
            m_flexApiStatusLabel->setText(
                tr("Status: \xE2\x97\x8F TCP 4992 bind failed"));
            m_flexApiStatusLabel->setStyleSheet(
                QStringLiteral("color: #E53935;"));  // red
        } else {
            m_flexApiStatusLabel->setText(
                tr("Status: \xE2\x97\x8B Disabled "
                   "(toggle 'Enable 4O3A integration' above to start)"));
            m_flexApiStatusLabel->setStyleSheet(
                QStringLiteral("color: #888;"));   // grey
        }
    }
}

}  // namespace NereusSDR
