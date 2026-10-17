// =================================================================
// src/gui/containers/WorkspaceProfiles.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original named workspace profiles for the
// workspace preview. See WorkspaceProfiles.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-11 - captureLive's note: the snapshot holds remembered places.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Review fixes: settled suppression lift, checked
//                 restores, Stale reload, validated import; the lift runs on
//                 the manager. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "WorkspaceProfiles.h"

#include "ContainerContentRegistry.h"
#include "ContainerDocumentCodec.h"
#include "ContainerManager.h"
#include "ContainerWidget.h"
#include "ContainerWorkspaceStore.h"
#include "core/AppSettings.h"
#include "core/LogCategories.h"

#include <QCoreApplication>
#include <QEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QScreen>
#include <QSet>
#include <QUuid>

#include <limits>

namespace NereusSDR {

namespace {

constexpr auto kStateFormat = "nereussdr.workspace-profiles";
// Layout passes run synchronously after an apply so the strips settle
// before geometry capture resumes.
constexpr int kApplySettlePasses = 3;

QString edgeTitle(DockEdge edge)
{
    switch (edge) {
    case DockEdge::Left:
        return QStringLiteral("Left");
    case DockEdge::Right:
        return QStringLiteral("Right");
    case DockEdge::Top:
        return QStringLiteral("Top");
    case DockEdge::Bottom:
        return QStringLiteral("Bottom");
    }
    return QStringLiteral("Right");
}

QJsonObject profileJson(const WorkspaceProfile& p, bool withId)
{
    WorkspaceDocument workspace = p.workspace;
    workspace.revision = 0;
    QJsonObject o{
        {QStringLiteral("name"), p.name},
        {QStringLiteral("savedAt"), p.savedAt.toUTC().toString(Qt::ISODate)},
        {QStringLiteral("workspace"),
         QJsonDocument::fromJson(ContainerDocumentCodec::encode(workspace)).object()},
        {QStringLiteral("screens"), p.screens},
    };
    if (withId) {
        o.insert(QStringLiteral("id"), p.id);
    }
    return o;
}

bool profileFromJson(const QJsonObject& o, WorkspaceProfile& p, QString* error)
{
    p.id = o.value(QStringLiteral("id")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.savedAt = QDateTime::fromString(o.value(QStringLiteral("savedAt")).toString(), Qt::ISODate);
    if (!o.value(QStringLiteral("workspace")).isObject()) {
        *error = QStringLiteral("Profile %1 has no workspace").arg(p.name);
        return false;
    }
    const DocumentResult decoded = ContainerDocumentCodec::decode(
        QJsonDocument(o.value(QStringLiteral("workspace")).toObject()).toJson(QJsonDocument::Compact));
    if (!decoded.ok) {
        *error = QStringLiteral("Profile %1: %2").arg(p.name, decoded.error);
        return false;
    }
    p.workspace = decoded.document;
    p.workspace.revision = 0;
    p.screens = o.value(QStringLiteral("screens")).toObject();
    return true;
}

} // namespace

WorkspaceProfiles::WorkspaceProfiles(AppSettings& settings, ContainerManager* manager, QObject* parent)
    : QObject(parent), m_settings(settings), m_manager(manager)
{
}

QByteArray WorkspaceProfiles::encode(const WorkspaceProfilesState& state)
{
    QJsonArray profiles;
    for (const WorkspaceProfile& p : state.profiles) {
        profiles.append(profileJson(p, true));
    }
    const QJsonObject root{
        {QStringLiteral("format"), QString::fromLatin1(kStateFormat)},
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        // A decimal string keeps all 64 bits, as the workspace codec does.
        {QStringLiteral("revision"), QString::number(state.revision)},
        {QStringLiteral("activeId"), state.activeId},
        {QStringLiteral("profiles"), profiles},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

bool WorkspaceProfiles::decode(const QByteArray& bytes, WorkspaceProfilesState& state, QString* error)
{
    QString scratch;
    QString* err = error != nullptr ? error : &scratch;
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *err = QStringLiteral("Workspace profiles are not valid JSON");
        return false;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kStateFormat)
        || root.value(QStringLiteral("schemaVersion")).toInt() != kSchemaVersion) {
        *err = QStringLiteral("Unknown workspace profiles format or version");
        return false;
    }
    bool ok = false;
    WorkspaceProfilesState decoded;
    decoded.revision = root.value(QStringLiteral("revision")).toString().toULongLong(&ok);
    if (!ok) {
        *err = QStringLiteral("Workspace profiles revision is invalid");
        return false;
    }
    decoded.activeId = root.value(QStringLiteral("activeId")).toString();
    for (const QJsonValue& v : root.value(QStringLiteral("profiles")).toArray()) {
        WorkspaceProfile p;
        if (!v.isObject() || !profileFromJson(v.toObject(), p, err)) {
            if (err->isEmpty()) {
                *err = QStringLiteral("A workspace profile is not an object");
            }
            return false;
        }
        decoded.profiles.append(p);
    }
    const QString invalid = validate(decoded);
    if (!invalid.isEmpty()) {
        *err = invalid;
        return false;
    }
    state = decoded;
    return true;
}

QString WorkspaceProfiles::validate(const WorkspaceProfilesState& state)
{
    if (state.profiles.isEmpty()) {
        return QStringLiteral("There must be at least one workspace profile");
    }
    QSet<QString> ids;
    for (const WorkspaceProfile& p : state.profiles) {
        if (p.id.trimmed().isEmpty() || ids.contains(p.id)) {
            return QStringLiteral("Profile ids must be nonempty and unique: %1").arg(p.id);
        }
        ids.insert(p.id);
        if (p.name.trimmed().isEmpty()) {
            return QStringLiteral("Profile names must be nonempty");
        }
        const QString workspace = ContainerDocumentCodec::validate(p.workspace);
        if (!workspace.isEmpty()) {
            return QStringLiteral("Profile %1: %2").arg(p.name, workspace);
        }
    }
    if (!ids.contains(state.activeId)) {
        return QStringLiteral("The active profile does not exist");
    }
    return {};
}

WorkspaceProfiles::Result WorkspaceProfiles::fail(Status status, const QString& error)
{
    qCWarning(lcContainer) << "Workspace profiles:" << error;
    emit profileError(error);
    return {status, error, {}};
}

WorkspaceProfiles::Result WorkspaceProfiles::load()
{
    const QString key = QString::fromLatin1(kSettingsKey);
    const bool present = m_settings.contains(key);
    if (present) {
        const QByteArray raw = m_settings.value(key).toString().toUtf8();
        WorkspaceProfilesState decoded;
        QString error;
        if (!decode(raw, decoded, &error)) {
            // Keep the stored bytes untouched; nothing may overwrite them.
            m_loadError = error;
            return fail(Status::Invalid, error);
        }
        m_state = decoded;
        m_loadedRaw = raw;
        m_loadedPresent = true;
        m_loadError.clear();
        emit profilesChanged();
        return {};
    }
    m_state = {};
    m_loadedRaw.clear();
    m_loadedPresent = false;
    m_loadError.clear();
    WorkspaceProfile first;
    first.id = newId();
    first.name = QStringLiteral("Default");
    first.savedAt = now();
    QString error;
    if (m_manager && m_manager->workspaceStore()) {
        if (!captureLive(first.workspace, first.screens, &error)) {
            return fail(Status::NoWorkspace, error);
        }
    }
    WorkspaceProfilesState next;
    next.activeId = first.id;
    next.profiles.append(first);
    const Result result = commit(next, m_state.revision);
    if (result.ok()) {
        emit profilesChanged();
        emit activeChanged(m_state.activeId);
    }
    return result;
}

QStringList WorkspaceProfiles::ids() const
{
    QStringList result;
    for (const WorkspaceProfile& p : m_state.profiles) {
        result.append(p.id);
    }
    return result;
}

int WorkspaceProfiles::indexOf(const QString& id) const
{
    for (int i = 0; i < m_state.profiles.size(); ++i) {
        if (m_state.profiles.at(i).id == id) {
            return i;
        }
    }
    return -1;
}

const WorkspaceProfile* WorkspaceProfiles::profile(const QString& id) const
{
    const int i = indexOf(id);
    return i >= 0 ? &m_state.profiles.at(i) : nullptr;
}

QString WorkspaceProfiles::name(const QString& id) const
{
    const WorkspaceProfile* p = profile(id);
    return p != nullptr ? p->name : QString();
}

QString WorkspaceProfiles::summaryFor(const WorkspaceDocument& workspace)
{
    const DockEdge order[] = {DockEdge::Right, DockEdge::Left, DockEdge::Top, DockEdge::Bottom};
    QStringList parts;
    const auto title = [](const ContainerDocument& d) {
        return d.name.isEmpty() ? d.id : d.name;
    };
    for (DockEdge edge : order) {
        QStringList names;
        for (const ContainerDocument& d : workspace.containers) {
            if (d.visible && d.dockMode == DockMode::PanelDocked && d.edge == edge) {
                names.append(title(d));
            }
        }
        if (!names.isEmpty()) {
            parts.append(QStringLiteral("%1: %2").arg(edgeTitle(edge), names.join(QStringLiteral(", "))));
        }
    }
    const QPair<DockMode, QString> loose[] = {{DockMode::OverlayDocked, QStringLiteral("Free")},
                                              {DockMode::Floating, QStringLiteral("Floating")}};
    for (const auto& [mode, label] : loose) {
        QStringList names;
        for (const ContainerDocument& d : workspace.containers) {
            if (d.visible && d.dockMode == mode) {
                names.append(title(d));
            }
        }
        if (!names.isEmpty()) {
            parts.append(QStringLiteral("%1: %2").arg(label, names.join(QStringLiteral(", "))));
        }
    }
    if (parts.isEmpty()) {
        return QStringLiteral("No containers");
    }
    return parts.join(QStringLiteral(" · "));
}

QString WorkspaceProfiles::summary(const QString& id) const
{
    const WorkspaceProfile* p = profile(id);
    return p != nullptr ? summaryFor(p->workspace) : QString();
}

void WorkspaceProfiles::setClock(std::function<QDateTime()> clock)
{
    m_clock = std::move(clock);
}

QDateTime WorkspaceProfiles::now() const
{
    return m_clock ? m_clock() : QDateTime::currentDateTimeUtc();
}

QString WorkspaceProfiles::newId() const
{
    QString id;
    do {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } while (indexOf(id) >= 0);
    return id;
}

bool WorkspaceProfiles::captureLive(WorkspaceDocument& workspace, QJsonObject& screens, QString* error) const
{
    ContainerWorkspaceStore* store = m_manager ? m_manager->workspaceStore() : nullptr;
    if (store == nullptr) {
        *error = QStringLiteral("No container workspace");
        return false;
    }
    if (!store->loadError().isEmpty()) {
        *error = store->loadError();
        return false;
    }
    // Commit pending operator moves first, so the snapshot holds every
    // container's remembered place.
    m_manager->saveState();
    workspace = store->snapshot();
    workspace.revision = 0;
    screens = {};
    for (const ContainerDocument& d : workspace.containers) {
        if (d.dockMode != DockMode::Floating) {
            continue;
        }
        if (ContainerWidget* c = m_manager->container(d.id)) {
            if (QScreen* screen = c->window()->screen()) {
                screens.insert(d.id, screen->name());
            }
        }
    }
    return true;
}

WorkspaceProfiles::Result WorkspaceProfiles::applyLive(const WorkspaceProfile& target)
{
    ContainerWorkspaceStore* store = m_manager ? m_manager->workspaceStore() : nullptr;
    if (store == nullptr) {
        return fail(Status::NoWorkspace, QStringLiteral("No container workspace"));
    }
    const WorkspaceDocument previous = store->snapshot();
    WorkspaceDocument next = target.workspace;
    next.revision = previous.revision;
    // Geometry capture is suppressed while the target settles, so the
    // layout passes of the apply are not written back as user edits.
    m_manager->setGeometryCaptureSuppressed(true);
    const CommitResult result = m_manager->commitWorkspace(next, previous.revision);
    for (int pass = 0; pass < kApplySettlePasses; ++pass) {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    }
    // Moves and resizes the passes posted are delivered before capture
    // resumes: the lift is queued behind them. A later apply supersedes
    // an earlier apply's lift. The manager is the context: the lift runs
    // even when these profiles are destroyed first.
    const quint64 generation = ++*m_applyGeneration;
    ContainerManager* manager = m_manager.data();
    QMetaObject::invokeMethod(manager, [manager, generation, current = m_applyGeneration] {
        if (generation == *current) {
            manager->setGeometryCaptureSuppressed(false);
        }
    }, Qt::QueuedConnection);
    if (result.status != CommitStatus::Saved) {
        // The store is unchanged; put the widgets back on it.
        m_manager->reconcileWorkspace(store->snapshot());
        return fail(Status::Invalid, QStringLiteral("Could not apply profile %1: %2").arg(target.name, result.error));
    }
    return {};
}

void WorkspaceProfiles::restoreLive(const WorkspaceProfile& previous)
{
    if (const Result restored = applyLive(previous); !restored.ok()) {
        qCWarning(lcContainer) << "Workspace profiles: could not restore" << previous.name << ":"
                               << restored.error;
    }
}

WorkspaceDocument WorkspaceProfiles::emptyFrom(const WorkspaceDocument& current) const
{
    WorkspaceDocument empty;
    empty.schemaVersion = current.schemaVersion;
    empty.mainContainerId = current.mainContainerId;
    empty.arrangement.gridAlignment = current.arrangement.gridAlignment;
    for (const ContainerDocument& d : current.containers) {
        // The Main Panel owns the singleton applets, so it stays in the
        // document, hidden; every other container goes.
        if (d.id == current.mainContainerId) {
            ContainerDocument main = d;
            main.visible = false;
            empty.containers.append(main);
        }
    }
    return empty;
}

WorkspaceProfiles::Result WorkspaceProfiles::commit(WorkspaceProfilesState next, quint64 expectedRevision)
{
    if (!m_loadError.isEmpty()) {
        return fail(Status::Invalid, m_loadError);
    }
    if (expectedRevision != m_state.revision) {
        return fail(Status::Stale, QStringLiteral("Workspace profiles revision changed"));
    }
    const QString key = QString::fromLatin1(kSettingsKey);
    const bool present = m_settings.contains(key);
    const QByteArray raw = m_settings.value(key).toString().toUtf8();
    if (present != m_loadedPresent || (present && raw != m_loadedRaw)) {
        // Another writer changed the key since it was loaded. Nothing is
        // retried: the key is read once more and the rail and dialog show
        // what it holds, so the operator's next action works on it.
        // load() itself emits profilesChanged (and activeChanged when it
        // has to recreate the key).
        const Result stale = fail(Status::Stale, QStringLiteral("Workspace profiles changed; reload before editing"));
        const QString before = m_state.activeId;
        if (load().ok() && present && m_state.activeId != before) {
            emit activeChanged(m_state.activeId);
        }
        return stale;
    }
    const QString invalid = validate(next);
    if (!invalid.isEmpty()) {
        return fail(Status::Invalid, invalid);
    }
    if (m_state.revision == std::numeric_limits<quint64>::max()) {
        return fail(Status::Invalid, QStringLiteral("Workspace profiles revision is exhausted"));
    }
    next.revision = m_state.revision + 1;
    const QByteArray encoded = encode(next);
    const QVariant old = m_settings.value(key);
    m_settings.setValue(key, QString::fromUtf8(encoded));
    QString error;
    if (!m_settings.save(&error)) {
        if (present) {
            m_settings.setValue(key, old);
        } else {
            m_settings.remove(key);
        }
        return fail(Status::StorageError, error);
    }
    m_state = next;
    m_loadedRaw = encoded;
    m_loadedPresent = true;
    return {};
}

WorkspaceProfiles::Result WorkspaceProfiles::switchTo(const QString& id)
{
    const int target = indexOf(id);
    if (target < 0) {
        return fail(Status::NotFound, QStringLiteral("No such profile"));
    }
    if (id == m_state.activeId) {
        return {Status::Ok, {}, id};
    }
    WorkspaceProfilesState next = m_state;
    const int active = indexOf(m_state.activeId);
    QString error;
    if (active >= 0) {
        WorkspaceProfile& current = next.profiles[active];
        if (!captureLive(current.workspace, current.screens, &error)) {
            return fail(Status::NoWorkspace, error);
        }
        current.savedAt = now();
    }
    if (Result applied = applyLive(next.profiles.at(target)); !applied.ok()) {
        return applied;
    }
    next.activeId = id;
    Result result = commit(next, m_state.revision);
    if (!result.ok()) {
        // Restore the arrangement that was live before the switch.
        if (active >= 0) {
            restoreLive(next.profiles.at(active));
        }
        return result;
    }
    result.id = id;
    emit profilesChanged();
    emit activeChanged(id);
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::save()
{
    const int active = indexOf(m_state.activeId);
    if (active < 0) {
        return fail(Status::NotFound, QStringLiteral("No active profile"));
    }
    WorkspaceProfilesState next = m_state;
    QString error;
    WorkspaceProfile& current = next.profiles[active];
    if (!captureLive(current.workspace, current.screens, &error)) {
        return fail(Status::NoWorkspace, error);
    }
    current.savedAt = now();
    Result result = commit(next, m_state.revision);
    if (result.ok()) {
        result.id = m_state.activeId;
        emit profilesChanged();
    }
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::createEmpty(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        return fail(Status::Invalid, QStringLiteral("A profile needs a name"));
    }
    WorkspaceProfilesState next = m_state;
    const int active = indexOf(m_state.activeId);
    WorkspaceProfile created;
    QString error;
    WorkspaceDocument live;
    QJsonObject screens;
    if (!captureLive(live, screens, &error)) {
        return fail(Status::NoWorkspace, error);
    }
    if (active >= 0) {
        // Leaving the active profile: it keeps what is live now, as a switch.
        next.profiles[active].workspace = live;
        next.profiles[active].screens = screens;
        next.profiles[active].savedAt = now();
    }
    created.id = newId();
    created.name = name.trimmed();
    created.savedAt = now();
    created.workspace = emptyFrom(live);
    if (Result applied = applyLive(created); !applied.ok()) {
        return applied;
    }
    next.profiles.append(created);
    next.activeId = created.id;
    Result result = commit(next, m_state.revision);
    if (!result.ok()) {
        if (active >= 0) {
            restoreLive(next.profiles.at(active));
        }
        return result;
    }
    result.id = created.id;
    emit profilesChanged();
    emit activeChanged(created.id);
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::createFromCurrent(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        return fail(Status::Invalid, QStringLiteral("A profile needs a name"));
    }
    WorkspaceProfile created;
    QString error;
    if (!captureLive(created.workspace, created.screens, &error)) {
        return fail(Status::NoWorkspace, error);
    }
    created.id = newId();
    created.name = name.trimmed();
    created.savedAt = now();
    WorkspaceProfilesState next = m_state;
    next.profiles.append(created);
    next.activeId = created.id;
    Result result = commit(next, m_state.revision);
    if (result.ok()) {
        result.id = created.id;
        emit profilesChanged();
        emit activeChanged(created.id);
    }
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::duplicate(const QString& id, const QString& name)
{
    const int source = indexOf(id);
    if (source < 0) {
        return fail(Status::NotFound, QStringLiteral("No such profile"));
    }
    if (name.trimmed().isEmpty()) {
        return fail(Status::Invalid, QStringLiteral("A profile needs a name"));
    }
    WorkspaceProfile copy = m_state.profiles.at(source);
    if (id == m_state.activeId) {
        // The active profile's arrangement is what is live.
        QString error;
        if (!captureLive(copy.workspace, copy.screens, &error)) {
            return fail(Status::NoWorkspace, error);
        }
    }
    copy.id = newId();
    copy.name = name.trimmed();
    copy.savedAt = now();
    WorkspaceProfilesState next = m_state;
    next.profiles.insert(source + 1, copy);
    Result result = commit(next, m_state.revision);
    if (result.ok()) {
        result.id = copy.id;
        emit profilesChanged();
    }
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::rename(const QString& id, const QString& name)
{
    const int index = indexOf(id);
    if (index < 0) {
        return fail(Status::NotFound, QStringLiteral("No such profile"));
    }
    if (name.trimmed().isEmpty()) {
        return fail(Status::Invalid, QStringLiteral("A profile needs a name"));
    }
    WorkspaceProfilesState next = m_state;
    next.profiles[index].name = name.trimmed();
    Result result = commit(next, m_state.revision);
    if (result.ok()) {
        result.id = id;
        emit profilesChanged();
    }
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::remove(const QString& id)
{
    const int index = indexOf(id);
    if (index < 0) {
        return fail(Status::NotFound, QStringLiteral("No such profile"));
    }
    if (m_state.profiles.size() <= 1) {
        return fail(Status::LastProfile, QStringLiteral("The last profile cannot be removed"));
    }
    WorkspaceProfilesState next = m_state;
    const bool wasActive = id == m_state.activeId;
    WorkspaceProfile removed = next.profiles.at(index);
    if (wasActive) {
        // The neighbour after it, or before it when it is last.
        const int neighbour = index + 1 < next.profiles.size() ? index + 1 : index - 1;
        QString error;
        if (!captureLive(removed.workspace, removed.screens, &error)) {
            return fail(Status::NoWorkspace, error);
        }
        if (Result applied = applyLive(next.profiles.at(neighbour)); !applied.ok()) {
            return applied;
        }
        next.activeId = next.profiles.at(neighbour).id;
    }
    next.profiles.removeAt(index);
    Result result = commit(next, m_state.revision);
    if (!result.ok()) {
        if (wasActive) {
            restoreLive(removed);
        }
        return result;
    }
    result.id = id;
    emit profilesChanged();
    if (wasActive) {
        emit activeChanged(m_state.activeId);
    }
    return result;
}

QByteArray WorkspaceProfiles::exportProfile(const QString& id) const
{
    const WorkspaceProfile* p = profile(id);
    if (p == nullptr) {
        return {};
    }
    WorkspaceProfile exported = *p;
    if (id == m_state.activeId && m_manager && m_manager->workspaceStore()) {
        // The active profile exports what is live.
        exported.workspace = m_manager->workspaceStore()->snapshot();
        exported.workspace.revision = 0;
    }
    const QJsonObject root{
        {QStringLiteral("format"), QString::fromLatin1(kExportFormat)},
        {QStringLiteral("schemaVersion"), kSchemaVersion},
        {QStringLiteral("profile"), profileJson(exported, false)},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

WorkspaceProfiles::Result WorkspaceProfiles::exportToFile(const QString& id, const QString& path) const
{
    const QByteArray bytes = exportProfile(id);
    if (bytes.isEmpty()) {
        return {Status::NotFound, QStringLiteral("No such profile"), id};
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(bytes) != bytes.size()) {
        qCWarning(lcContainer) << "Workspace profile export failed:" << file.errorString();
        return {Status::StorageError, file.errorString(), id};
    }
    return {Status::Ok, {}, id};
}

WorkspaceProfiles::Result WorkspaceProfiles::importProfile(const QByteArray& bytes)
{
    QJsonParseError parse;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        return fail(Status::Invalid, QStringLiteral("The file is not a workspace profile"));
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(kExportFormat)
        || root.value(QStringLiteral("schemaVersion")).toInt() != kSchemaVersion
        || !root.value(QStringLiteral("profile")).isObject()) {
        return fail(Status::Invalid, QStringLiteral("Unknown workspace profile format or version"));
    }
    WorkspaceProfile imported;
    QString error;
    if (!profileFromJson(root.value(QStringLiteral("profile")).toObject(), imported, &error)) {
        return fail(Status::Invalid, error);
    }
    // Every entry passes the same validation as a portable or settings
    // import before anything is written.
    ContainerContentRegistry fallback;
    const ContainerContentRegistry* registry =
        m_manager && m_manager->contentRegistry() ? m_manager->contentRegistry() : &fallback;
    for (const ContainerDocument& container : imported.workspace.containers) {
        for (const ContentEntry& entry : container.contents) {
            if (const QString invalid = registry->validateEntry(entry); !invalid.isEmpty()) {
                return fail(Status::Invalid, invalid);
            }
        }
    }
    // Import always creates a profile with a new id.
    imported.id = newId();
    if (imported.name.trimmed().isEmpty()) {
        imported.name = QStringLiteral("Imported");
    }
    if (!imported.savedAt.isValid()) {
        imported.savedAt = now();
    }
    WorkspaceProfilesState next = m_state;
    next.profiles.append(imported);
    Result result = commit(next, m_state.revision);
    if (result.ok()) {
        result.id = imported.id;
        emit profilesChanged();
    }
    return result;
}

WorkspaceProfiles::Result WorkspaceProfiles::importFromFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return fail(Status::StorageError, file.errorString());
    }
    return importProfile(file.readAll());
}

} // namespace NereusSDR
