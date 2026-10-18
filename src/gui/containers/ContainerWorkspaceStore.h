#pragma once
// no-port-check: NereusSDR-original transactional client presentation store.
// Modification history (NereusSDR):
//   2026-10-02 — Mixed container ownership, persistence and source routing by
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
//   2026-10-08 - Workspace preview only: workspaceKey(), the preview's own
//                 ContainerWorkspacePreview key. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
#include "ContainerDocument.h"
#include "gui/NereusGuiExport.h"
#include <QByteArray>
#include <QObject>

namespace NereusSDR {
class AppSettings;
class NEREUS_GUI_EXPORT ContainerWorkspaceStore : public QObject {
    Q_OBJECT
public:
    explicit ContainerWorkspaceStore(AppSettings& settings, QObject* parent = nullptr);
    DocumentResult load();
    WorkspaceDocument snapshot() const;
    QString loadError() const { return m_loadError; }
    CommitResult commit(const WorkspaceDocument& document, quint64 expectedRevision);
    // The AppSettings key the document lives in: ContainerWorkspace, or
    // ContainerWorkspacePreview with the workspace preview switch on.
    QString workspaceKey() const { return m_workspaceKey; }
signals:
    void committed(quint64 revision);
private:
    AppSettings& m_settings;
    QString m_workspaceKey;
    QString m_backupKey;
    WorkspaceDocument m_document;
    QByteArray m_loadedRaw;
    bool m_loadedPresent = false;
    QString m_loadError;
    bool m_committing = false;
};
} // namespace NereusSDR
