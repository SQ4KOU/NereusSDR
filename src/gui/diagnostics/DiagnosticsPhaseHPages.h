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

    void buildUI();
};

// Diagnostics → Settings Validation (Phase H, functional).
class SettingsValidationPage : public SetupPage {
    Q_OBJECT
public:
    explicit SettingsValidationPage(RadioModel* model = nullptr, QWidget* parent = nullptr);

private slots:
    void refresh();
    void onResetClicked();
    void onForgetClicked();

private:
    RadioModel*  m_model{nullptr};
    QListWidget* m_issueList{nullptr};
    QPushButton* m_resetBtn{nullptr};
    QPushButton* m_forgetBtn{nullptr};
    QPushButton* m_refreshBtn{nullptr};

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

// Diagnostics → Logs (Phase H placeholder — real qCWarning/qCDebug
// capture is deferred follow-up).
class LogsPage : public SetupPage {
    Q_OBJECT
public:
    explicit LogsPage(QWidget* parent = nullptr);

private:
    QPlainTextEdit* m_logView{nullptr};
    QPushButton*    m_clearBtn{nullptr};

    void buildUI();
};

} // namespace NereusSDR
