// =================================================================
// src/gui/diagnostics/RadioStatusPage.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. Diagnostics → Radio Status dashboard.
// Thetis surfaces these readouts piecemeal across Front Console /
// PA Settings / main meter / etc. NereusSDR consolidates them into
// a single tab backed by Phase 3P-H Task 1 models (RadioStatus,
// SettingsHygiene) + Phase 3P-E HermesLiteBandwidthMonitor.
//
// No direct Thetis port at this layer; data shapes ported in Task 1.
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-20 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include "gui/SetupPage.h"

#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QFrame>
#include <QListWidget>
#include <QTimer>
#include <QElapsedTimer>

namespace NereusSDR {

class RadioStatus;
class SettingsHygiene;
class HermesLiteBandwidthMonitor;

// RadioStatusPage — the Diagnostics → Radio Status dashboard.
//
// Layout: top status bar (4 columns) + 5 cards in a 3+2 grid:
//   Row 1: PA Status | Forward/Reflected/SWR | PTT Source
//   Row 2: Connection Quality summary | Settings Hygiene warnings
//
// The page holds a non-owning RadioModel* so it can access
// radioStatus(), bwMonitor(), and settingsHygiene() accessors.
// It wires signals to update UI in real time.
class RadioStatusPage : public SetupPage {
    Q_OBJECT

public:
    explicit RadioStatusPage(RadioModel* model = nullptr, QWidget* parent = nullptr);

private slots:
    void onPaTemperatureChanged(double celsius);
    void onPaCurrentChanged(double amps);
    void onPowerChanged(double forward, double reflected, double swr);
    void onPttChanged();
    void onIssuesChanged();
    void onUptimeTick();
    void onBwPollTick();

private:
    void buildStatusBar(QFrame* bar);
    void buildPaStatusCard(QFrame* card);
    void buildPowerCard(QFrame* card);
    void buildPttCard(QFrame* card);
    void buildConnectionCard(QFrame* card);
    void buildHygieneCard(QFrame* card);

    void refreshPttPills();
    void refreshHygieneRows();

    RadioModel* m_model{nullptr};

    // ── Status bar labels ─────────────────────────────────────────────────
    QLabel* m_radioLabel{nullptr};
    QLabel* m_uptimeLabel{nullptr};
    QLabel* m_firmwareLabel{nullptr};
    QLabel* m_modeLabel{nullptr};

    // ── PA Status card ────────────────────────────────────────────────────
    QLabel*       m_paTemperatureLabel{nullptr};
    QLabel*       m_paCurrentLabel{nullptr};
    QLabel*       m_paVoltageLabel{nullptr};
    QProgressBar* m_paTempBar{nullptr};
    QProgressBar* m_paCurrentBar{nullptr};

    // ── Power card ────────────────────────────────────────────────────────
    QLabel*       m_forwardLabel{nullptr};
    QLabel*       m_reflectedLabel{nullptr};
    QLabel*       m_swrLabel{nullptr};
    QProgressBar* m_forwardBar{nullptr};
    QProgressBar* m_swrBar{nullptr};

    // ── PTT card ──────────────────────────────────────────────────────────
    QLabel*      m_pttActiveLabel{nullptr};
    QListWidget* m_pttHistoryList{nullptr};
    // PTT source pill buttons (not checkable — visual only)
    QPushButton* m_pttPills[7]{};

    // ── Connection quality card ───────────────────────────────────────────
    QLabel* m_bwEp6Label{nullptr};
    QLabel* m_bwEp2Label{nullptr};
    QLabel* m_bwThrottleLabel{nullptr};
    QLabel* m_bwSeqGapLabel{nullptr};

    // ── Settings hygiene card ─────────────────────────────────────────────
    QListWidget*  m_issueList{nullptr};
    QPushButton*  m_resetBtn{nullptr};
    QPushButton*  m_forgetBtn{nullptr};

    // ── Timers ────────────────────────────────────────────────────────────
    QTimer        m_uptimeTimer;
    QTimer        m_bwPollTimer;
    QElapsedTimer m_connectClock;
};

} // namespace NereusSDR
