// =================================================================
// src/gui/diagnostics/DiagnosticsPhaseHPages.h  (NereusSDR)
// =================================================================
//
// NereusSDR-original. Four sibling Diagnostics sub-tabs added in
// Phase 3P-H per spec §13:
//   - Connection Quality   (60 s history of latency/seq-gap/throttle)
//   - Settings Validation  (full audit list backed by SettingsHygiene)
//   - Export / Import      (per-MAC + global AppSettings XML round-trip)
//   - Logs                 (recent qCWarning/qCDebug viewer)
//
// SettingsValidation and ExportImportConfig are functional in this
// commit; ConnectionQuality and Logs render as placeholders pending
// follow-up wire-up tasks.
//
// =================================================================
//
// Modification history (NereusSDR):
//   2026-04-20 — Original implementation for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-23 - R-R3-21 / R-R3-10: Settings Validation's Reset and Forget change
//                 the radio's settings, which a remote window holds on
//                 the Core; they are disabled while it does not have them.
//                 J.J. Boyd (KG4VCF), with AI-assisted implementation via
//                 Anthropic Claude Code.
//   2026-09-26 - R-R3-32 (remote-window parity Task 14): Connection
//                 Quality's Live Counters title says "from the Core" in a
//                 remote window. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include "gui/SetupPage.h"

#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

namespace NereusSDR {

class RadioModel;

// Diagnostics → Connection Quality (Phase H placeholder).
class ConnectionQualityPage : public SetupPage {
    Q_OBJECT
public:
    explicit ConnectionQualityPage(RadioModel* model = nullptr, QWidget* parent = nullptr);

private slots:
    void onTick();

private:
    RadioModel* m_model{nullptr};
    QLabel*     m_ep6BytesLabel{nullptr};
    QLabel*     m_ep2BytesLabel{nullptr};
    QLabel*     m_throttleLabel{nullptr};
    QLabel*     m_seqGapLabel{nullptr};
    QLabel*     m_historyPlaceholder{nullptr};
    // R-R3-32 (parity Task 14): says "from the Core" in a remote window.
    QGroupBox*  m_liveGroup{nullptr};

    void buildUI();
};

// Diagnostics → Settings Validation (Phase H, functional).
class SettingsValidationPage : public SetupPage {
    Q_OBJECT
public:
    explicit SettingsValidationPage(RadioModel* model = nullptr, QWidget* parent = nullptr);

    // R-R3-21 / R-R3-10: Reset and Forget change the radio's settings, which are
    // the Core's in a remote window, so they are disabled while the Core's
    // settings are unavailable. Refresh only reads.
    void setStationSettingsAvailable(bool available, const QString& reason) override;

private slots:
    void refresh();
    void onRevalidateClicked();
    void onResetClicked();
    void onForgetClicked();

private:
    RadioModel*  m_model{nullptr};
    QListWidget* m_issueList{nullptr};
    QPushButton* m_resetBtn{nullptr};
    QPushButton* m_forgetBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    // The last availability pushed by setStationSettingsAvailable(). Read
    // again after a confirmation returns: the link can drop while it is
    // open (R3 Setup fix wave, final review M2).
    bool         m_stationSettingsAvailable{true};

    void buildUI();
};

// Diagnostics → Export / Import Config (Phase H, functional).
class ExportImportConfigPage : public SetupPage {
    Q_OBJECT
public:
    explicit ExportImportConfigPage(RadioModel* model = nullptr, QWidget* parent = nullptr);

private slots:
    void onExportAllClicked();
    void onImportAllClicked();
    void onExportRadioClicked();

private:
    RadioModel*  m_model{nullptr};
    QLabel*      m_settingsPathLabel{nullptr};
    QLabel*      m_radioSummaryLabel{nullptr};
    QPushButton* m_exportAllBtn{nullptr};
    QPushButton* m_importAllBtn{nullptr};
    QPushButton* m_exportRadioBtn{nullptr};

    void buildUI();
};

// Diagnostics → Logs. R-R3-21: shows the log file's recent lines, the
// ones Help > Support's log viewer shows (SupportDialog::logTailText),
// read again each time the page is shown or Refresh is pressed.
class LogsPage : public SetupPage {
    Q_OBJECT
public:
    explicit LogsPage(QWidget* parent = nullptr);

    void refresh();

protected:
    void showEvent(QShowEvent* event) override;

private:
    QPlainTextEdit* m_logView{nullptr};
    QPushButton*    m_refreshBtn{nullptr};
    QPushButton*    m_clearBtn{nullptr};

    void buildUI();
};

} // namespace NereusSDR
