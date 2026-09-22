// =================================================================
// src/gui/ConnectionSelector.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R3 Core session presentation and actions.
// =================================================================

#include "gui/ConnectionSelector.h"

#include <QAbstractItemView>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

namespace NereusSDR {
namespace {

constexpr int kKeyRole = Qt::UserRole;

void configurePlainTextLabel(QLabel* label)
{
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
}

} // namespace

ConnectionSelector::ConnectionSelector(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Connections"));
    setModal(false);
    setWindowModality(Qt::NonModal);
    setObjectName(QStringLiteral("connectionSelector"));

    auto* layout = new QVBoxLayout(this);

    m_noticeLabel = new QLabel(this);
    m_noticeLabel->setObjectName(QStringLiteral("connectionSelectorNotice"));
    configurePlainTextLabel(m_noticeLabel);
    m_noticeLabel->setVisible(false);
    layout->addWidget(m_noticeLabel);

    m_discoveryStatusLabel = new QLabel(this);
    m_discoveryStatusLabel->setObjectName(QStringLiteral("connectionSelectorDiscoveryStatus"));
    configurePlainTextLabel(m_discoveryStatusLabel);
    layout->addWidget(m_discoveryStatusLabel);

    m_targetTree = new QTreeWidget(this);
    m_targetTree->setObjectName(QStringLiteral("connectionSelectorTargets"));
    m_targetTree->setColumnCount(4);
    m_targetTree->setHeaderLabels({tr("Name"), tr("Radio"), tr("Address"), tr("Status")});
    m_targetTree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_targetTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_targetTree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_targetTree->setRootIsDecorated(false);
    m_targetTree->setUniformRowHeights(true);
    m_targetTree->header()->setStretchLastSection(true);
    m_targetTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_targetTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_targetTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_targetTree->setMinimumHeight(240);
    layout->addWidget(m_targetTree, 1);

    auto* currentGroup = new QGroupBox(tr("Current connection"), this);
    auto* currentLayout = new QVBoxLayout(currentGroup);
    m_currentSummaryLabel = new QLabel(currentGroup);
    m_currentSummaryLabel->setObjectName(QStringLiteral("connectionSelectorCurrentSummary"));
    configurePlainTextLabel(m_currentSummaryLabel);
    m_currentDetailsLabel = new QLabel(currentGroup);
    m_currentDetailsLabel->setObjectName(QStringLiteral("connectionSelectorCurrentDetails"));
    configurePlainTextLabel(m_currentDetailsLabel);
    m_currentDetailsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    currentLayout->addWidget(m_currentSummaryLabel);
    currentLayout->addWidget(m_currentDetailsLabel);
    layout->addWidget(currentGroup);

    auto* actionLayout = new QHBoxLayout();
    m_addCoreButton = makeButton(tr("Add Core…"), QStringLiteral("connectionSelectorAddCore"));
    m_addRadioButton = makeButton(tr("Add Radio…"), QStringLiteral("connectionSelectorAddRadio"));
    m_scanButton = makeButton(tr("Scan"), QStringLiteral("connectionSelectorScan"));
    m_editButton = makeButton(tr("Edit…"), QStringLiteral("connectionSelectorEdit"));
    m_forgetButton = makeButton(tr("Forget…"), QStringLiteral("connectionSelectorForget"));
    m_detailsButton = makeButton(tr("Details"), QStringLiteral("connectionSelectorDetails"));
    m_disconnectButton = makeButton(tr("Disconnect"), QStringLiteral("connectionSelectorDisconnect"));
    m_connectButton = makeButton(tr("Connect"), QStringLiteral("connectionSelectorConnect"));
    auto* closeButton = makeButton(tr("Close"), QStringLiteral("connectionSelectorClose"));

    actionLayout->addWidget(m_addCoreButton);
    actionLayout->addWidget(m_addRadioButton);
    actionLayout->addWidget(m_scanButton);
    actionLayout->addWidget(m_editButton);
    actionLayout->addWidget(m_forgetButton);
    actionLayout->addStretch();
    actionLayout->addWidget(m_detailsButton);
    actionLayout->addWidget(m_disconnectButton);
    actionLayout->addWidget(m_connectButton);
    actionLayout->addWidget(closeButton);
    layout->addLayout(actionLayout);

    connect(m_targetTree, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem*, QTreeWidgetItem*) { updateActions(); });
    connect(m_addCoreButton, &QPushButton::clicked, this, &ConnectionSelector::addCoreRequested);
    connect(m_addRadioButton, &QPushButton::clicked, this, &ConnectionSelector::addRadioRequested);
    connect(m_scanButton, &QPushButton::clicked, this, &ConnectionSelector::scanRequested);
    connect(m_editButton, &QPushButton::clicked, this, [this] {
        if (const ConnectionTargetRow* target = selectedTarget(); target != nullptr) {
            emit editRequested(target->key);
        }
    });
    connect(m_forgetButton, &QPushButton::clicked, this, [this] {
        if (const ConnectionTargetRow* target = selectedTarget(); target != nullptr) {
            emit forgetRequested(target->key);
        }
    });
    connect(m_detailsButton, &QPushButton::clicked, this, [this] {
        if (const ConnectionTargetRow* target = selectedTarget(); target != nullptr) {
            emit detailsRequested(target->key);
        }
    });
    connect(m_disconnectButton, &QPushButton::clicked, this,
            &ConnectionSelector::disconnectRequested);
    connect(m_connectButton, &QPushButton::clicked, this, [this] {
        if (const ConnectionTargetRow* target = selectedTarget();
            target != nullptr && target->connectable) {
            emit connectRequested(target->key);
        }
    });
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);

    setTargets({});
    setCurrentConnection({}, {}, false, false);
    resize(820, 540);
}

void ConnectionSelector::setTargets(const QList<ConnectionTargetRow>& targets)
{
    const QString previousKey = selectedKey();
    m_targets = targets;

    QList<ConnectionTargetRow> localRadios;
    QList<ConnectionTargetRow> lanCores;
    QList<ConnectionTargetRow> savedCores;
    for (const ConnectionTargetRow& target : m_targets) {
        switch (target.kind) {
        case ConnectionTargetKind::LocalRadio: localRadios.append(target); break;
        case ConnectionTargetKind::LanCore: lanCores.append(target); break;
        case ConnectionTargetKind::SavedCore: savedCores.append(target); break;
        }
    }

    m_targetTree->clear();
    addGroup(tr("Radios on this network"), ConnectionTargetKind::LocalRadio,
             tr("No radios found on this network."), localRadios);
    addGroup(tr("Stations on this network"), ConnectionTargetKind::LanCore,
             tr("No stations found on this network."), lanCores);
    addGroup(tr("Your stations"), ConnectionTargetKind::SavedCore,
             tr("No saved stations."), savedCores);
    setSelectedKey(previousKey);
    updateActions();
}

void ConnectionSelector::setCurrentConnection(const QString& summary, const QString& details,
                                               bool canDisconnect, bool retrying)
{
    m_currentSummaryLabel->setText(summary);
    m_currentDetailsLabel->setText(details);
    m_canDisconnect = canDisconnect;
    m_retrying = retrying;
    updateActions();
}

void ConnectionSelector::setDiscoveryStatus(const QString& text)
{
    m_discoveryStatusLabel->setText(text);
}

void ConnectionSelector::setNotice(const QString& notice)
{
    m_noticeLabel->setText(notice);
    m_noticeLabel->setVisible(!notice.isEmpty());
}

QString ConnectionSelector::selectedKey() const
{
    const QTreeWidgetItem* item = m_targetTree->currentItem();
    return item == nullptr ? QString{} : item->data(0, kKeyRole).toString();
}

void ConnectionSelector::setSelectedKey(const QString& key)
{
    if (key.isEmpty()) {
        m_targetTree->setCurrentItem(nullptr);
        return;
    }

    for (int groupIndex = 0; groupIndex < m_targetTree->topLevelItemCount(); ++groupIndex) {
        QTreeWidgetItem* group = m_targetTree->topLevelItem(groupIndex);
        for (int rowIndex = 0; rowIndex < group->childCount(); ++rowIndex) {
            QTreeWidgetItem* row = group->child(rowIndex);
            if (row->data(0, kKeyRole).toString() == key) {
                m_targetTree->setCurrentItem(row);
                return;
            }
        }
    }
    m_targetTree->setCurrentItem(nullptr);
}

void ConnectionSelector::addGroup(const QString& title, ConnectionTargetKind kind,
                                  const QString& emptyText,
                                  const QList<ConnectionTargetRow>& targets)
{
    auto* group = new QTreeWidgetItem(m_targetTree, {title});
    group->setFirstColumnSpanned(true);
    group->setFlags(Qt::ItemIsEnabled);
    group->setExpanded(true);

    if (targets.isEmpty()) {
        auto* empty = new QTreeWidgetItem(group, {emptyText});
        empty->setFirstColumnSpanned(true);
        empty->setFlags(Qt::ItemIsEnabled);
        return;
    }

    for (const ConnectionTargetRow& target : targets) {
        auto* row = new QTreeWidgetItem(group,
                                        {target.name, target.radioText,
                                         target.address, target.state});
        row->setData(0, kKeyRole, target.key);
        row->setData(0, kKeyRole + 1, static_cast<int>(kind));
        row->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }
}

const ConnectionTargetRow* ConnectionSelector::selectedTarget() const
{
    const QString key = selectedKey();
    if (key.isEmpty()) {
        return nullptr;
    }
    for (const ConnectionTargetRow& target : m_targets) {
        if (target.key == key) {
            return &target;
        }
    }
    return nullptr;
}

void ConnectionSelector::updateActions()
{
    const ConnectionTargetRow* target = selectedTarget();
    const bool hasTarget = target != nullptr;
    const bool canConnect = hasTarget && target->connectable;
    const bool canEdit = hasTarget && target->editable;
    const bool canForget = hasTarget && target->forgettable;
    m_connectButton->setVisible(canConnect);
    m_connectButton->setEnabled(canConnect);
    m_editButton->setVisible(canEdit);
    m_editButton->setEnabled(canEdit);
    m_forgetButton->setVisible(canForget);
    m_forgetButton->setEnabled(canForget);
    m_detailsButton->setVisible(hasTarget);
    m_detailsButton->setEnabled(hasTarget);
    m_disconnectButton->setText(m_retrying ? tr("Cancel retry") : tr("Disconnect"));
    const bool canDisconnect = m_canDisconnect || m_retrying;
    m_disconnectButton->setVisible(canDisconnect);
    m_disconnectButton->setEnabled(canDisconnect);
}

QPushButton* ConnectionSelector::makeButton(const QString& text, const QString& objectName)
{
    auto* button = new QPushButton(text, this);
    button->setObjectName(objectName);
    button->setAutoDefault(false);
    return button;
}

} // namespace NereusSDR
