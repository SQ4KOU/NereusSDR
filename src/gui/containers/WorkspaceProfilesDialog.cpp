// =================================================================
// src/gui/containers/WorkspaceProfilesDialog.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original manage dialog for workspace
// profiles. It ports no code. See WorkspaceProfilesDialog.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Letter badges in the rail's pill style; long summaries
//                 elided with the full text in a tooltip. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "WorkspaceProfilesDialog.h"

#include "WorkspaceProfiles.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/widgets/PillStyle.h"
#include "gui/widgets/ProfileRail.h"

#include <QDir>
#include <QFontMetrics>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace NereusSDR {

namespace {

// Room either side of the letter in a badge, as the rail sizes its pills
// (ProfileRail::setzeMasse: horizontalAdvance + 20).
constexpr int kBadgeTextPad = 20;
// The badge column's width, so the names line up whatever the letter.
constexpr int kBadgeSlot = 32;
constexpr int kRowHeight = 52;
constexpr int kMinWidth = 600;
constexpr int kMinHeight = 380;

QString latin(const char* s)
{
    return QString::fromLatin1(s);
}

QString dialogStyle()
{
    using namespace Workspace;
    return QStringLiteral(
        "QDialog { background: %1; }"
        "QLabel { color: %2; background: transparent; }"
        "QListWidget { background: %3; border: 1px solid %4; border-radius: 6px;"
        "  outline: none; padding: 2px; }"
        "QListWidget::item { border-bottom: 1px solid %5; }"
        "QListWidget::item:selected { background: %6; }"
        "QPushButton, QToolButton { background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "  stop:0 %7, stop:1 %8); color: %2; border: 1px solid %4;"
        "  border-radius: 6px; padding: 4px 12px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover, QToolButton:hover { background: %9; }"
        "QPushButton:disabled, QToolButton:disabled { color: %10; }"
        "QToolButton::menu-indicator { subcontrol-origin: padding;"
        "  subcontrol-position: right center; right: 4px; }"
        "QToolButton { padding-right: 18px; }")
        .arg(latin(kPanelBg), latin(kTextPrimary), latin(kInsetBg), latin(kBorder),
             latin(kBorderSubtle), latin(kTileActiveBg), latin(kGlassBtnTop),
             latin(kGlassBtnBot), latin(kButtonHover))
        .arg(latin(kTextInactive));
}

// A one-line label that elides its text with "…" to the width it
// gets and shows the full text as a tooltip when it does.
class ElidedLabel : public QLabel {
public:
    ElidedLabel(const QString& full, QWidget* parent) : QLabel(parent), m_full(full)
    {
        setMinimumWidth(0);
        setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        setText(full);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QLabel::resizeEvent(event);
        const QString shown = fontMetrics().elidedText(m_full, Qt::ElideRight, contentsRect().width());
        if (shown != text()) {
            setText(shown);
        }
        setToolTip(shown == m_full ? QString() : m_full);
    }

private:
    QString m_full;
};

void reportFailure(QWidget* parent, const QString& what, const WorkspaceProfiles::Result& r)
{
    if (r.ok()) {
        return;
    }
    QMessageBox::warning(parent, QStringLiteral("Workspace Profiles"),
                         QStringLiteral("%1 failed.\n\n%2").arg(what, r.error));
}

QString askName(QWidget* parent, const QString& title, const QString& initial, bool* ok)
{
    const QString text = QInputDialog::getText(parent, title, QStringLiteral("Profile name:"),
                                               QLineEdit::Normal, initial, ok);
    return text.trimmed();
}

QString exportDir()
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    return desktop.isEmpty() ? QDir::homePath() : desktop;
}

} // namespace

WorkspaceProfilesDialog::WorkspaceProfilesDialog(WorkspaceProfiles* profiles, QWidget* parent)
    : QDialog(parent)
    , m_profiles(profiles)
{
    setObjectName(QStringLiteral("WorkspaceProfilesDialog"));
    setWindowTitle(QStringLiteral("Workspace Profiles"));
    setMinimumSize(kMinWidth, kMinHeight);
    setStyleSheet(dialogStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 12);
    root->setSpacing(10);

    auto* intro = new QLabel(
        QStringLiteral("A profile holds where your containers sit. Switching never changes "
                       "a slice, frequency, mode or filter."),
        this);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }")
                             .arg(latin(Workspace::kTextSecondary)));
    root->addWidget(intro);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("workspaceProfilesList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(true);
    root->addWidget(m_list, 1);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(6);
    m_new = new QToolButton(this);
    m_new->setText(QStringLiteral("New"));
    m_new->setPopupMode(QToolButton::InstantPopup);
    m_new->setMenu(createNewMenu(m_profiles, this));
    buttons->addWidget(m_new);
    auto addButton = [this, buttons](const QString& text) {
        auto* b = new QPushButton(text, this);
        b->setAutoDefault(false);
        buttons->addWidget(b);
        return b;
    };
    m_duplicate = addButton(QStringLiteral("Duplicate"));
    m_rename = addButton(QStringLiteral("Rename"));
    m_save = addButton(QStringLiteral("Save"));
    m_export = addButton(QStringLiteral("Export"));
    m_import = addButton(QStringLiteral("Import"));
    buttons->addStretch(1);
    m_remove = addButton(QStringLiteral("Remove"));
    root->addLayout(buttons);

    auto* bottom = new QHBoxLayout;
    m_hint = new QLabel(QStringLiteral("Double-click a profile to switch to it."), this);
    m_hint->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }")
                              .arg(latin(Workspace::kTextScale)));
    bottom->addWidget(m_hint, 1);
    auto* close = new QPushButton(QStringLiteral("Close"), this);
    close->setAutoDefault(false);
    bottom->addWidget(close);
    root->addLayout(bottom);

    connect(close, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_list, &QListWidget::currentRowChanged, this, [this](int) { updateButtons(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!m_profiles || !item) {
            return;
        }
        reportFailure(this, QStringLiteral("Switching"),
                      m_profiles->switchTo(item->data(Qt::UserRole).toString()));
    });
    connect(m_duplicate, &QPushButton::clicked, this,
            [this]() { duplicateProfile(m_profiles, this, selectedId()); });
    connect(m_rename, &QPushButton::clicked, this,
            [this]() { renameProfile(m_profiles, this, selectedId()); });
    connect(m_save, &QPushButton::clicked, this, [this]() { saveProfile(m_profiles, this); });
    connect(m_export, &QPushButton::clicked, this,
            [this]() { exportProfile(m_profiles, this, selectedId()); });
    connect(m_import, &QPushButton::clicked, this, [this]() { importProfile(m_profiles, this); });
    connect(m_remove, &QPushButton::clicked, this,
            [this]() { removeProfile(m_profiles, this, selectedId()); });

    if (m_profiles) {
        connect(m_profiles, &WorkspaceProfiles::profilesChanged, this,
                &WorkspaceProfilesDialog::rebuild);
        connect(m_profiles, &WorkspaceProfiles::activeChanged, this,
                [this](const QString&) { rebuild(); });
    }
    rebuild();
    if (m_profiles) {
        selectId(m_profiles->activeId());
    }
}

QString WorkspaceProfilesDialog::selectedId() const
{
    const QListWidgetItem* item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void WorkspaceProfilesDialog::selectId(const QString& id)
{
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->data(Qt::UserRole).toString() == id) {
            m_list->setCurrentRow(i);
            return;
        }
    }
}

QWidget* WorkspaceProfilesDialog::makeRow(const QString& id) const
{
    using namespace Workspace;
    const WorkspaceProfile* p = m_profiles->profile(id);
    const bool active = (id == m_profiles->activeId());

    auto* row = new QWidget;
    row->setAttribute(Qt::WA_TranslucentBackground);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(8, 6, 10, 6);
    h->setSpacing(10);

    // The same pill as the rail's badge: lit (checked) for the active
    // profile. It only shows; the row takes the clicks.
    auto* badge = new QPushButton(ProfileRail::initialFor(p->name), row);
    badge->setObjectName(QStringLiteral("workspaceProfilesBadge"));
    badge->setCheckable(true);
    badge->setChecked(active);
    badge->setFocusPolicy(Qt::NoFocus);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    badge->setStyleSheet(PillStyle::pillStyle());
    badge->setFixedWidth(std::max(kBadgeSlot,
                                  badge->fontMetrics().horizontalAdvance(badge->text()) + kBadgeTextPad));
    h->addWidget(badge, 0, Qt::AlignVCenter);

    auto* text = new QVBoxLayout;
    text->setSpacing(2);
    auto* name = new QLabel(active ? QStringLiteral("%1  <span style=\"color:%2; font-size:9px;"
                                                    " font-weight:600;\">ACTIVE</span>")
                                         .arg(p->name.toHtmlEscaped(), latin(kGlassSelText))
                                   : p->name.toHtmlEscaped(),
                            row);
    name->setTextFormat(Qt::RichText);
    name->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 13px; font-weight: 600; }")
                            .arg(latin(kTextPrimary)));
    text->addWidget(name);
    auto* summary = new ElidedLabel(m_profiles->summary(id), row);
    summary->setObjectName(QStringLiteral("workspaceProfilesSummary"));
    summary->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }")
                               .arg(latin(kTextSecondary)));
    text->addWidget(summary);
    h->addLayout(text, 1);

    const QString saved = p->savedAt.isValid()
        ? QStringLiteral("Saved %1").arg(p->savedAt.toUTC().toString(
              QStringLiteral("yyyy-MM-dd HH:mm 'UTC'")))
        : QStringLiteral("Not saved");
    auto* when = new QLabel(saved, row);
    when->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 11px; }")
                            .arg(latin(kTextScale)));
    h->addWidget(when, 0, Qt::AlignRight | Qt::AlignVCenter);
    return row;
}

void WorkspaceProfilesDialog::rebuild()
{
    const QString keep = selectedId();
    m_list->clear();
    if (!m_profiles) {
        updateButtons();
        return;
    }
    for (const QString& id : m_profiles->ids()) {
        auto* item = new QListWidgetItem(m_list);
        item->setData(Qt::UserRole, id);
        item->setSizeHint(QSize(0, kRowHeight));
        m_list->setItemWidget(item, makeRow(id));
    }
    selectId(keep.isEmpty() || !m_profiles->profile(keep) ? m_profiles->activeId() : keep);
    updateButtons();
}

void WorkspaceProfilesDialog::updateButtons()
{
    const QString id = selectedId();
    const bool have = m_profiles && !id.isEmpty();
    const bool usable = m_profiles && m_profiles->loadError().isEmpty();
    m_new->setEnabled(usable);
    m_duplicate->setEnabled(usable && have);
    m_rename->setEnabled(usable && have);
    // Save checkpoints the active arrangement; it is offered on the
    // active profile only.
    m_save->setEnabled(usable && have && id == m_profiles->activeId());
    m_export->setEnabled(have);
    m_import->setEnabled(usable);
    m_remove->setEnabled(usable && have && m_profiles->ids().size() > 1);
}

QMenu* WorkspaceProfilesDialog::createNewMenu(WorkspaceProfiles* profiles, QWidget* parent)
{
    auto* menu = new QMenu(parent);
    menu->setObjectName(QStringLiteral("workspaceProfilesNewMenu"));
    QAction* empty = menu->addAction(QStringLiteral("Empty"));
    empty->setToolTip(QStringLiteral("A profile with no containers shown."));
    QAction* current = menu->addAction(QStringLiteral("From Current Arrangement"));
    current->setToolTip(QStringLiteral("A copy of the arrangement on screen now."));
    const QPointer<WorkspaceProfiles> guard(profiles);
    QObject::connect(empty, &QAction::triggered, menu,
                     [guard, parent]() { newProfile(guard, parent, false); });
    QObject::connect(current, &QAction::triggered, menu,
                     [guard, parent]() { newProfile(guard, parent, true); });
    return menu;
}

bool WorkspaceProfilesDialog::newProfile(WorkspaceProfiles* profiles, QWidget* parent,
                                         bool fromCurrent)
{
    if (!profiles) {
        return false;
    }
    bool ok = false;
    const QString name = askName(parent, QStringLiteral("New Profile"),
                                 QStringLiteral("Profile %1").arg(profiles->ids().size() + 1), &ok);
    if (!ok || name.isEmpty()) {
        return false;
    }
    const auto r = fromCurrent ? profiles->createFromCurrent(name) : profiles->createEmpty(name);
    reportFailure(parent, QStringLiteral("Creating the profile"), r);
    return r.ok();
}

bool WorkspaceProfilesDialog::renameProfile(WorkspaceProfiles* profiles, QWidget* parent,
                                            const QString& id)
{
    if (!profiles || !profiles->profile(id)) {
        return false;
    }
    bool ok = false;
    const QString name = askName(parent, QStringLiteral("Rename Profile"), profiles->name(id), &ok);
    if (!ok || name.isEmpty() || name == profiles->name(id)) {
        return false;
    }
    const auto r = profiles->rename(id, name);
    reportFailure(parent, QStringLiteral("Renaming the profile"), r);
    return r.ok();
}

bool WorkspaceProfilesDialog::duplicateProfile(WorkspaceProfiles* profiles, QWidget* parent,
                                               const QString& id)
{
    if (!profiles || !profiles->profile(id)) {
        return false;
    }
    bool ok = false;
    const QString name = askName(parent, QStringLiteral("Duplicate Profile"),
                                 QStringLiteral("%1 copy").arg(profiles->name(id)), &ok);
    if (!ok || name.isEmpty()) {
        return false;
    }
    const auto r = profiles->duplicate(id, name);
    reportFailure(parent, QStringLiteral("Duplicating the profile"), r);
    return r.ok();
}

bool WorkspaceProfilesDialog::removeProfile(WorkspaceProfiles* profiles, QWidget* parent,
                                            const QString& id)
{
    if (!profiles || !profiles->profile(id)) {
        return false;
    }
    if (profiles->ids().size() <= 1) {
        QMessageBox::information(parent, QStringLiteral("Workspace Profiles"),
                                 QStringLiteral("The last profile cannot be removed."));
        return false;
    }
    const auto answer = QMessageBox::question(
        parent, QStringLiteral("Remove Profile"),
        QStringLiteral("Remove the profile “%1”? This cannot be undone.")
            .arg(profiles->name(id)),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return false;
    }
    const auto r = profiles->remove(id);
    reportFailure(parent, QStringLiteral("Removing the profile"), r);
    return r.ok();
}

bool WorkspaceProfilesDialog::saveProfile(WorkspaceProfiles* profiles, QWidget* parent)
{
    if (!profiles) {
        return false;
    }
    const auto r = profiles->save();
    reportFailure(parent, QStringLiteral("Saving the profile"), r);
    return r.ok();
}

bool WorkspaceProfilesDialog::exportProfile(WorkspaceProfiles* profiles, QWidget* parent,
                                            const QString& id)
{
    if (!profiles || !profiles->profile(id)) {
        return false;
    }
    const QString suggested = QDir(exportDir()).filePath(
        QStringLiteral("%1.nereus-profile.json").arg(profiles->name(id)));
    const QString path = QFileDialog::getSaveFileName(
        parent, QStringLiteral("Export Profile"), suggested,
        QStringLiteral("Workspace profile (*.json)"));
    if (path.isEmpty()) {
        return false;
    }
    const auto r = profiles->exportToFile(id, path);
    reportFailure(parent, QStringLiteral("Exporting the profile"), r);
    return false;
}

bool WorkspaceProfilesDialog::importProfile(WorkspaceProfiles* profiles, QWidget* parent)
{
    if (!profiles) {
        return false;
    }
    const QString path = QFileDialog::getOpenFileName(
        parent, QStringLiteral("Import Profile"), exportDir(),
        QStringLiteral("Workspace profile (*.json)"));
    if (path.isEmpty()) {
        return false;
    }
    const auto r = profiles->importFromFile(path);
    reportFailure(parent, QStringLiteral("Importing the profile"), r);
    return r.ok();
}

} // namespace NereusSDR
