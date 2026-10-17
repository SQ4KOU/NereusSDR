#pragma once
// =================================================================
// src/gui/containers/WorkspaceProfiles.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original named workspace profiles for the
// workspace preview. It ports no code.
//
// Studied reference: Longpath src/gui/LayoutProfiles.{h,cpp} at
// 551576e30c42731c6d51ce005c5f23ec38be391c (named snapshots of the
// container state, switched from a rail). This class follows the unified
// workspace design where the two differ:
//   - profiles have stable ids that do not depend on their names;
//   - import creates a new profile (Longpath replaces the named one);
//   - the last profile cannot be removed;
//   - the last selected profile is remembered;
//   - window position, size and maximized state are never part of a
//     profile;
//   - one AppSettings key holds every profile and is written in one
//     transaction, validated first, and a stale revision is rejected.
//
// A profile stores arrangement only: the container workspace document
// (containers, their edge and order, header modes, visibility, locks,
// free and floating geometry, strip thickness and extents) plus the
// screen each floating container was on. Switching never touches a
// slice, panadapter, frequency, mode, filter, DSP, the connection or MOX.
//
// With the workspace preview switch off nothing constructs this class,
// so the WorkspaceProfiles key is never read or written.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Review fixes: settled suppression lift, checked
//                 restores, Stale reload, validated import; the lift runs on
//                 the manager. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "ContainerDocument.h"
#include "gui/NereusGuiExport.h"

#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace NereusSDR {

class AppSettings;
class ContainerManager;

struct WorkspaceProfile {
    QString id;
    QString name;
    QDateTime savedAt;
    // The container workspace document. Its revision is not meaningful
    // inside a profile and is stored as 0.
    WorkspaceDocument workspace;
    // Floating container id -> the name of the screen it was on.
    QJsonObject screens;
    bool operator==(const WorkspaceProfile&) const = default;
};

struct WorkspaceProfilesState {
    quint64 revision = 0;
    QString activeId;
    QVector<WorkspaceProfile> profiles;
    bool operator==(const WorkspaceProfilesState&) const = default;
};

class NEREUS_GUI_EXPORT WorkspaceProfiles : public QObject {
    Q_OBJECT

public:
    static constexpr auto kSettingsKey = "WorkspaceProfiles";
    static constexpr auto kExportFormat = "nereussdr.workspace-profile";
    static constexpr int kSchemaVersion = 1;

    enum class Status { Ok, Stale, Invalid, StorageError, NotFound, LastProfile, NoWorkspace };
    struct Result {
        Status status = Status::Ok;
        QString error;
        QString id;   // the profile created or acted on
        bool ok() const { return status == Status::Ok; }
    };

    // `manager` supplies the live workspace (its store and reconcile). It
    // may be null in codec tests; switching then reports NoWorkspace.
    WorkspaceProfiles(AppSettings& settings, ContainerManager* manager, QObject* parent = nullptr);

    // Reads the key. When it is absent, one profile "Default" is made from
    // the current arrangement and written. A corrupt key is reported and
    // left untouched; mutations are refused until it loads.
    Result load();
    QString loadError() const { return m_loadError; }

    const WorkspaceProfilesState& state() const { return m_state; }
    quint64 revision() const { return m_state.revision; }
    QString activeId() const { return m_state.activeId; }
    QStringList ids() const;
    const WorkspaceProfile* profile(const QString& id) const;
    QString name(const QString& id) const;
    // "Right: Main Panel · Top: Meters"; free and floating containers last.
    QString summary(const QString& id) const;
    static QString summaryFor(const WorkspaceDocument& workspace);

    // Switches: flushes the live arrangement into the active profile, then
    // applies `id`'s arrangement through ContainerManager. Restores the
    // previous arrangement on failure.
    Result switchTo(const QString& id);
    // Explicit checkpoint of the live arrangement into the active profile.
    Result save();
    // New profiles. Empty keeps the panadapters and the session and shows
    // no container (the Main Panel, which owns the singleton applets, stays
    // in the document hidden). Current is "save as": the new profile takes
    // the live arrangement and becomes active; the old one keeps what it
    // last saved.
    Result createEmpty(const QString& name);
    Result createFromCurrent(const QString& name);
    Result duplicate(const QString& id, const QString& name);
    Result rename(const QString& id, const QString& name);
    // The last profile cannot be removed. Removing the active profile
    // switches to its neighbour first.
    Result remove(const QString& id);

    QByteArray exportProfile(const QString& id) const;
    Result exportToFile(const QString& id, const QString& path) const;
    // Always a new profile with a new id; never replaces one.
    Result importProfile(const QByteArray& bytes);
    Result importFromFile(const QString& path);

    // The transaction every mutation goes through: validates `next`,
    // rejects a stale `expectedRevision` (or a key changed underneath),
    // and writes the key with revision + 1 in one AppSettings save.
    Result commit(WorkspaceProfilesState next, quint64 expectedRevision);

    // The clock for savedAt (the preview fixes it).
    void setClock(std::function<QDateTime()> clock);

    static QByteArray encode(const WorkspaceProfilesState& state);
    static bool decode(const QByteArray& bytes, WorkspaceProfilesState& state, QString* error);
    static QString validate(const WorkspaceProfilesState& state);

signals:
    void profilesChanged();
    void activeChanged(const QString& id);
    void profileError(const QString& error);

private:
    Result fail(Status status, const QString& error);
    bool captureLive(WorkspaceDocument& workspace, QJsonObject& screens, QString* error) const;
    Result applyLive(const WorkspaceProfile& target);
    // Puts a previous arrangement back after a failed commit; a failure
    // to do so is logged.
    void restoreLive(const WorkspaceProfile& previous);
    WorkspaceDocument emptyFrom(const WorkspaceDocument& current) const;
    QString newId() const;
    QDateTime now() const;
    int indexOf(const QString& id) const;

    AppSettings& m_settings;
    QPointer<ContainerManager> m_manager;
    WorkspaceProfilesState m_state;
    QByteArray m_loadedRaw;
    bool m_loadedPresent = false;
    QString m_loadError;
    std::function<QDateTime()> m_clock;
    // Identifies the latest apply, whose queued lift of the geometry
    // capture suppression is the one that runs.
    // Shared with the queued suppression lift, which outlives this object
    // when the profiles go first.
    std::shared_ptr<quint64> m_applyGeneration = std::make_shared<quint64>(0);
};

} // namespace NereusSDR
