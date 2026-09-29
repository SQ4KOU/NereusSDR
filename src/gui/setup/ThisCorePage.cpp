// no-port-check: NereusSDR-original. Setup > This Core in a remote window.

// SPDX-License-Identifier: GPL-3.0-or-later
//
// =================================================================
// src/gui/setup/ThisCorePage.cpp  (NereusSDR)
// =================================================================
//
// NereusSDR-original; no upstream port. See ThisCorePage.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-26  J.J. Boyd / KG4VCF  Created (parity Task 21, R-IOS-18,
//                                    R-R3-38, R-R3-49). AI-assisted via
//                                    Anthropic Claude Code.
//   2026-09-26  J.J. Boyd / KG4VCF  iPhone app plan Task 78 (R-IOS-07,
//                                    R-IOS-02): the Connected now list.
//                                    AI-assisted via Anthropic Claude Code.
//   2026-09-29  J.J. Boyd / KG4VCF  The model choice is the Core's list for
//                                    the radio's board (stationRadios'
//                                    models); disabled with a reason on a
//                                    Core that does not send it. AI-assisted
//                                    via Anthropic Claude Code.
// =================================================================

#include "gui/setup/ThisCorePage.h"
#include "gui/multidevice/ConnectedDevicesList.h"

#include "core/HardwareProfile.h"
#include "core/HpsdrModel.h"
#include "core/session/IStationLink.h"
#include "core/station/StationRadios.h"
#include "models/RadioModel.h"

#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace NereusSDR {

namespace {

constexpr int kMacRole = Qt::UserRole + 1;
// The models the Core accepts for the radio (stationRadios' models), on
// column 2; empty when the Core did not send them.
constexpr int kModelsRole = Qt::UserRole + 2;

} // namespace

ThisCorePage::ThisCorePage(RadioModel* model, QWidget* parent)
    : SetupPage(QStringLiteral("This Core"), model, parent)
    , m_radioModel(model)
{
    QGroupBox* section = addSection(tr("Change radio"));
    auto* layout = qobject_cast<QVBoxLayout*>(section->layout());
    if (layout == nullptr) {
        layout = new QVBoxLayout(section);
    }

    auto* intro = new QLabel(
        tr("The radios this Core can see on its network. The Core runs one at a time; "
           "changing it restarts the Core's connection to its radio, and every window "
           "and device reconnects."),
        section);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    m_list = new QTreeWidget(section);
    m_list->setObjectName(QStringLiteral("thisCoreRadioList"));
    m_list->setRootIsDecorated(false);
    m_list->setColumnCount(5);
    m_list->setHeaderLabels({tr("Radio"), tr("Model"), tr("Address"), tr("MAC"), tr("")});
    m_list->header()->setStretchLastSection(false);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->setMinimumHeight(140);
    layout->addWidget(m_list);

    auto* buttons = new QHBoxLayout;
    m_useButton = new QPushButton(tr("Use this radio"), section);
    m_useButton->setObjectName(QStringLiteral("thisCoreUseRadio"));
    m_scanButton = new QPushButton(tr("Scan again"), section);
    m_scanButton->setObjectName(QStringLiteral("thisCoreScan"));
    m_forgetButton = new QPushButton(tr("Forget radio"), section);
    m_forgetButton->setObjectName(QStringLiteral("thisCoreForget"));
    buttons->addWidget(m_useButton);
    buttons->addWidget(m_scanButton);
    buttons->addStretch(1);
    buttons->addWidget(m_forgetButton);
    layout->addLayout(buttons);

    // Edit radio: the model the Core runs the selected radio as, the local
    // Connection panel's model override, applied at its next connect.
    auto* modelRow = new QHBoxLayout;
    auto* modelLabel = new QLabel(tr("Model:"), section);
    m_modelCombo = new QComboBox(section);
    m_modelCombo->setObjectName(QStringLiteral("thisCoreModel"));
    modelRow->addWidget(modelLabel);
    modelRow->addWidget(m_modelCombo, 1);
    layout->addLayout(modelRow);

    m_status = new QLabel(section);
    m_status->setObjectName(QStringLiteral("thisCoreStatus"));
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    connect(m_list, &QTreeWidget::currentItemChanged, this, [this]() { refreshControls(); });
    connect(m_useButton, &QPushButton::clicked, this, [this]() {
        send("station.selectRadio", selectedMac());
    });
    connect(m_scanButton, &QPushButton::clicked, this, [this]() {
        send("station.rescanRadios", QString());
    });
    connect(m_forgetButton, &QPushButton::clicked, this, [this]() {
        send("station.forgetRadio", selectedMac());
    });
    connect(m_modelCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_fillingModels || index < 0) {
            return;
        }
        send("station.setRadioModel", selectedMac(), m_modelCombo->itemData(index).toInt());
    });

    // iPhone app plan Task 78 (R-IOS-07; the several-devices design, section
    // 12 item 8): who is connected to the Core now, then the paired
    // devices, from the Core's connectedDevices and devices objects.
    QGroupBox* connectedSection = addSection(tr("Connected now"));
    auto* connectedLayout = qobject_cast<QVBoxLayout*>(connectedSection->layout());
    if (connectedLayout == nullptr) {
        connectedLayout = new QVBoxLayout(connectedSection);
    }
    m_connectedList = new ConnectedDevicesList(connectedSection);
    m_connectedList->setDevices(m_radioModel ? m_radioModel->stationDevices() : nullptr);
    connectedLayout->addWidget(m_connectedList);

    if (m_radioModel != nullptr) {
        connect(m_radioModel, &RadioModel::stationRadiosChanged, this,
                &ThisCorePage::rebuildList);
        connect(m_radioModel, &RadioModel::stationRadioRefused, this,
                [this](const QString& reason) {
            m_status->setText(reason);
            m_statusIsPage = false;
            rebuildList(); // a refused model change shows the Core's again
        });
        connect(m_radioModel, &RadioModel::stationLinkStateChanged, this,
                &ThisCorePage::refreshControls);
        connect(m_radioModel, &RadioModel::coreOnAirChanged, this,
                &ThisCorePage::refreshControls);
        // Fix wave (M2): the Core's own words for why it waits.
        connect(m_radioModel, &RadioModel::stationRadioWaitingChanged, this,
                &ThisCorePage::refreshControls);
    }
    rebuildList();
}

void ThisCorePage::setStationSettingsAvailable(bool available, const QString& reason)
{
    m_stationAvailable = available;
    m_stationReason = reason;
    refreshControls();
}

QString ThisCorePage::reconnectToChangeRadioReason()
{
    return tr("Reconnect this window to change the Core's radio.");
}

QString ThisCorePage::modelListUnavailableReason()
{
    return tr("Update the Core to change this radio's model from here.");
}

QString ThisCorePage::unavailableReason() const
{
    if (!m_stationAvailable) {
        return m_stationReason.isEmpty() ? tr("Connect to the Core to change these.")
                                         : m_stationReason;
    }
    IStationLink* link = m_radioModel != nullptr ? m_radioModel->stationLink() : nullptr;
    if (link == nullptr || !link->stationLinkReady()) {
        return tr("Connect to the Core to change these.");
    }
    if (!link->stationRadiosAvailable()) {
        return IStationLink::stationRadiosUnavailableReason();
    }
    // Fix wave (I5): the Core takes these only from a paired device.
    if (!link->signedInWithDeviceKey()) {
        // Follow-up N1: a sign-in that enrolled this computer's key is from
        // a paired device already; its next sign-in is by key.
        return link->enrolledDeviceKeyThisSession() ? reconnectToChangeRadioReason()
                                                    : StationRadios::pairedDeviceReason();
    }
    if (m_radioModel->isCoreOnAir()) {
        return RadioModel::onAirReason();
    }
    return {};
}

void ThisCorePage::selectCoreRadio()
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_list->topLevelItem(i);
        if (!item->text(4).isEmpty()) {
            m_list->setCurrentItem(item);
            return;
        }
    }
}

void ThisCorePage::rebuildList()
{
    const QString keep = selectedMac();
    m_list->clear();
    const QList<StationRadioEntry> radios =
        m_radioModel != nullptr ? m_radioModel->stationRadios() : QList<StationRadioEntry>{};
    QTreeWidgetItem* current = nullptr;
    for (const StationRadioEntry& radio : radios) {
        auto* item = new QTreeWidgetItem(m_list);
        item->setText(0, radio.name);
        item->setText(1, QString::fromLatin1(displayName(static_cast<HPSDRModel>(radio.model))));
        item->setText(2, radio.address);
        item->setText(3, radio.mac);
        item->setText(4, radio.inUse ? tr("The Core's radio") : QString());
        item->setData(0, kMacRole, radio.mac);
        item->setData(1, kMacRole, radio.model);
        QVariantList models;
        for (int m : radio.models) {
            models.append(m);
        }
        item->setData(2, kModelsRole, models);
        if (radio.mac == keep || (keep.isEmpty() && radio.inUse)) {
            current = item;
        }
    }
    if (current == nullptr && m_list->topLevelItemCount() > 0) {
        current = m_list->topLevelItem(0);
    }
    if (current != nullptr) {
        m_list->setCurrentItem(current);
    }
    for (int c = 1; c < m_list->columnCount(); ++c) {
        m_list->resizeColumnToContents(c);
    }
    refreshControls();
}

void ThisCorePage::refreshControls()
{
    const QString why = unavailableReason();
    const bool usable = why.isEmpty();
    const bool picked = !selectedMac().isEmpty();
    const bool coresRadio = selectedIsCoresRadio();

    // The model choice follows the selected radio: the models the Core
    // accepts for its board (station.setRadioModel's check), as the Core
    // sent them. The model alone does not name the board (Red Pitaya runs on
    // a Hermes or an Orion MkII board), so without the Core's list the
    // choice shows the model the Core runs it as and waits.
    m_fillingModels = true;
    m_modelCombo->clear();
    bool haveModelList = false;
    if (QTreeWidgetItem* item = m_list->currentItem()) {
        const int model = item->data(1, kMacRole).toInt();
        const QVariantList models = item->data(2, kModelsRole).toList();
        haveModelList = !models.isEmpty();
        if (haveModelList) {
            for (const QVariant& candidate : models) {
                m_modelCombo->addItem(
                    QString::fromLatin1(displayName(static_cast<HPSDRModel>(candidate.toInt()))),
                    candidate.toInt());
            }
        } else {
            m_modelCombo->addItem(QString::fromLatin1(displayName(static_cast<HPSDRModel>(model))),
                                  model);
        }
        m_modelCombo->setCurrentIndex(m_modelCombo->findData(model));
    }
    m_fillingModels = false;

    const auto gate = [](QWidget* w, bool enabled, const QString& reason,
                         const QString& tip) {
        w->setEnabled(enabled);
        w->setToolTip(enabled ? tip : reason);
    };
    const QString noRadio = tr("Choose a radio in the list first.");
    gate(m_scanButton, usable, why, tr("Look for radios on the Core's network again."));
    gate(m_useButton, usable && picked && !coresRadio,
         !usable ? why : !picked ? noRadio : tr("The Core is already using this radio."),
         tr("Make this the Core's radio."));
    gate(m_modelCombo, usable && picked && haveModelList,
         !usable ? why : !picked ? noRadio : modelListUnavailableReason(),
         tr("The model the Core runs this radio as, from its next connect."));
    gate(m_forgetButton, usable && picked && !coresRadio,
         !usable ? why : !picked ? noRadio : StationRadios::inUseReason(),
         tr("Remove this radio and its saved model from the Core."));
    gate(m_list, usable, why, QString());

    // The page's own line (why it cannot be used, or what the Core is
    // doing) until a request's answer replaces it.
    bool hasCore = false;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        hasCore = hasCore || !m_list->topLevelItem(i)->text(4).isEmpty();
    }
    if (!usable) {
        m_status->setText(why);
        m_statusIsPage = true;
    } else if (m_statusIsPage || m_status->text().isEmpty()) {
        // Fix wave (M2): the Core says why it waits (for a choice, for its
        // chosen radio to appear, for a radio another program holds); the
        // page's own words stand in for a Core that does not.
        const QString waiting =
            m_radioModel != nullptr ? m_radioModel->stationRadioWaiting() : QString();
        const QString line = !hasCore && !waiting.isEmpty() ? waiting
            : m_list->topLevelItemCount() == 0
                ? tr("The Core has not found a radio yet. Scan again.")
            : !hasCore ? tr("The Core is waiting for you to choose its radio.")
                       : QString();
        m_status->setText(line);
        m_statusIsPage = !line.isEmpty();
    }
}

QString ThisCorePage::selectedMac() const
{
    const QTreeWidgetItem* item = m_list != nullptr ? m_list->currentItem() : nullptr;
    return item != nullptr ? item->data(0, kMacRole).toString() : QString();
}

bool ThisCorePage::selectedIsCoresRadio() const
{
    const QTreeWidgetItem* item = m_list != nullptr ? m_list->currentItem() : nullptr;
    return item != nullptr && !item->text(4).isEmpty();
}

void ThisCorePage::send(const QByteArray& verb, const QString& mac, int model)
{
    IStationLink* link = m_radioModel != nullptr ? m_radioModel->stationLink() : nullptr;
    if (link == nullptr) {
        m_status->setText(tr("Connect to the Core to change these."));
        return;
    }
    m_status->clear();
    m_statusIsPage = false;
    const IStationLink::CommandOutcome outcome = link->requestStationRadio(verb, mac, model);
    if (!outcome.sent) {
        m_status->setText(outcome.reason);
    } else if (verb == "station.selectRadio") {
        m_status->setText(tr("The Core is changing its radio. This window reconnects when "
                             "it is ready."));
    } else if (verb == "station.rescanRadios") {
        m_status->setText(tr("The Core is looking for radios."));
    }
}

} // namespace NereusSDR
