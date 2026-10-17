#pragma once
// =================================================================
// src/gui/containers/WorkspaceProfilesDialog.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original manage dialog for workspace
// profiles. It ports no code.
//
// Lists every profile with its letter badge, name, a summary of where
// its containers sit ("Right: Main Panel · Top: Meters") and when it was
// last saved, and offers New (Empty / From Current Arrangement),
// Duplicate, Rename, Save, Export, Import and Remove. Double-clicking a
// row switches to it. Styled from the Glass and Depth palette
// (gui/styles/WorkspacePalette.h).
//
// The static actions are shared with the profile rail, so the rail and
// the dialog prompt, confirm and report errors the same way.
//
// Workspace preview only.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include <QDialog>
#include <QPointer>
#include <QString>

class QLabel;
class QListWidget;
class QMenu;
class QPushButton;
class QToolButton;

namespace NereusSDR {

class WorkspaceProfiles;

class WorkspaceProfilesDialog : public QDialog {
    Q_OBJECT

public:
    explicit WorkspaceProfilesDialog(WorkspaceProfiles* profiles, QWidget* parent = nullptr);

    // The selected profile id, or empty.
    QString selectedId() const;
    void selectId(const QString& id);
    QListWidget* list() const { return m_list; }
    QToolButton* newButton() const { return m_new; }

    // The New menu: "Empty" and "From Current Arrangement". Each action
    // asks for a name and creates the profile. The menu is a child of
    // `parent`; the caller shows it (exec or popup) and deletes it.
    static QMenu* createNewMenu(WorkspaceProfiles* profiles, QWidget* parent);

    // Shared actions. Each prompts or confirms where it must and reports
    // a failure in a message box. They return true when the profiles
    // changed.
    static bool newProfile(WorkspaceProfiles* profiles, QWidget* parent, bool fromCurrent);
    static bool renameProfile(WorkspaceProfiles* profiles, QWidget* parent, const QString& id);
    static bool duplicateProfile(WorkspaceProfiles* profiles, QWidget* parent, const QString& id);
    static bool removeProfile(WorkspaceProfiles* profiles, QWidget* parent, const QString& id);
    static bool saveProfile(WorkspaceProfiles* profiles, QWidget* parent);
    static bool exportProfile(WorkspaceProfiles* profiles, QWidget* parent, const QString& id);
    static bool importProfile(WorkspaceProfiles* profiles, QWidget* parent);

private:
    void rebuild();
    void updateButtons();
    QWidget* makeRow(const QString& id) const;

    QPointer<WorkspaceProfiles> m_profiles;
    QListWidget* m_list = nullptr;
    QToolButton* m_new = nullptr;
    QPushButton* m_duplicate = nullptr;
    QPushButton* m_rename = nullptr;
    QPushButton* m_save = nullptr;
    QPushButton* m_export = nullptr;
    QPushButton* m_import = nullptr;
    QPushButton* m_remove = nullptr;
    QLabel* m_hint = nullptr;
};

} // namespace NereusSDR
