#pragma once
// no-port-check: NereusSDR-original client-local workspace draft transaction.
// Modification history (NereusSDR):
//   2026-10-11 - Apply can commit through a caller's function (the
//                 container manager's commit) instead of the store. J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
#include "ContainerWorkspaceStore.h"
#include <QStringList>
#include <functional>
namespace NereusSDR {
class ContainerEditSession {
public:
    // What apply() commits the merged document with. Empty: the store.
    using Commit = std::function<CommitResult(const WorkspaceDocument&, quint64)>;
    explicit ContainerEditSession(ContainerWorkspaceStore& store, Commit commit = {});
    const WorkspaceDocument& draft() const { return m_draft; }
    void setDraft(const WorkspaceDocument& document) { m_draft = document; m_casConflicts.clear(); }
    CommitResult apply();
    void cancel();
    bool hasPendingChanges() const {return m_draft!=m_base;}
    QStringList conflictingContainers() const;
    void reloadContainers(const QStringList& ids);
private:
    ContainerWorkspaceStore& m_store;
    Commit m_commit;
    WorkspaceDocument m_base, m_draft;
    QStringList m_casConflicts;
    QStringList conflicts(const WorkspaceDocument& live) const;
};
}
