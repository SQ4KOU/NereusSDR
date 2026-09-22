// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original bounded station DSP asset manager.

#include "DspAssetDialog.h"

#include "core/AppSettings.h"
#include "core/dsp/DspAssetService.h"
#include "core/session/PureSignalSessionFacade.h"
#include "gui/StyleConstants.h"
#include "models/RadioModel.h"

#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariantMap>
#include <QVBoxLayout>

#include <array>
#include <limits>
#include <utility>

namespace NereusSDR {

namespace {

constexpr qsizetype kMaximumListJsonBytes = 256 * 1024;
constexpr int kMaximumRows = 128;
constexpr auto kGeometrySuffix = "/Geometry";
constexpr auto kDetailsSuffix = "/DetailsExpanded";

QString humanSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024) {
        return QObject::tr("%1 MiB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 2);
    }
    if (bytes >= 1024) {
        return QObject::tr("%1 KiB").arg(double(bytes) / 1024.0, 0, 'f', 1);
    }
    return QObject::tr("%1 bytes").arg(bytes);
}

QString shortId(const QString& id)
{
    if (id.startsWith(QStringLiteral("sha256:")) && id.size() > 23) {
        return id.left(23) + QChar(0x2026);
    }
    return id;
}

bool isExactInteger(const QVariant& value, qint64* result)
{
    qint64 converted = 0;
    switch (value.metaType().id()) {
    case QMetaType::Int: converted = value.toInt(); break;
    case QMetaType::UInt: converted = value.toUInt(); break;
    case QMetaType::LongLong: converted = value.toLongLong(); break;
    case QMetaType::ULongLong: {
        const qulonglong input = value.toULongLong();
        if (input > static_cast<qulonglong>(std::numeric_limits<qint64>::max())) {
            return false;
        }
        converted = static_cast<qint64>(input);
        break;
    }
    default: return false;
    }
    if (result) {
        *result = converted;
    }
    return true;
}

bool isExactString(const QVariant& value, QString* result)
{
    if (value.metaType().id() != QMetaType::QString) {
        return false;
    }
    if (result) {
        *result = value.toString();
    }
    return true;
}

bool isExactBool(const QVariant& value, bool* result)
{
    if (value.metaType().id() != QMetaType::Bool) {
        return false;
    }
    if (result) {
        *result = value.toBool();
    }
    return true;
}

} // namespace

DspAssetDialog::DspAssetDialog(RadioModel* radio, DspAssetKind kind, QWidget* parent)
    : DspAssetDialog(radio, radio ? radio->dspAssets() : nullptr, kind, parent)
{
}

DspAssetDialog::DspAssetDialog(RadioModel* radio, DspAssetService* service,
                               DspAssetKind kind, QWidget* parent)
    : QDialog(parent), m_radio(radio), m_service(service), m_kind(kind)
{
    setObjectName(QStringLiteral("dspAssetDialog"));
    setWindowTitle(kind == DspAssetKind::NnrModel
                       ? tr("NNR Model Assets") : tr("PureSignal Correction Assets"));
    setModal(false);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setMinimumSize(560, 420);
    buildUi();
    restoreUiState();
    connect(m_details, &QGroupBox::toggled, this, [this](bool expanded) {
        m_detailsText->setVisible(expanded);
        AppSettings::instance().setValue(
            settingsPrefix() + QString::fromLatin1(kDetailsSuffix),
            expanded ? QStringLiteral("True") : QStringLiteral("False"));
        adjustSize();
    });

    if (m_service) {
        connect(m_service, &DspAssetService::requestCompleted, this,
                &DspAssetDialog::onRequestCompleted);
        connect(m_service, &DspAssetService::selectionChanged, this, [this] {
            updateSelectionSummary();
        });
        connect(m_service, &QObject::destroyed, this, [this] {
            abortOperation(false);
            showError(tr("The station asset service is no longer available."));
        });
    }
    if (m_radio) {
        connect(m_radio->pureSignalFacade(), &PureSignalSessionFacade::statusChanged,
                this, &DspAssetDialog::updateButtons);
        connect(m_radio, &RadioModel::stationLinkStateChanged, this,
                &DspAssetDialog::retireForSessionChange);
        connect(m_radio, &RadioModel::connectionStateChanged, this,
                [this](ConnectionState) { retireForSessionChange(); });
    }

    if (!m_service) {
        showError(tr("DSP asset management is not available for this station."));
        updateButtons();
        return;
    }
    requestList();
}

DspAssetDialog::~DspAssetDialog()
{
    saveUiState();
    abortOperation(true);
}

void DspAssetDialog::buildUi()
{
    Style::applyDarkPageStyle(this);
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    if (m_kind == DspAssetKind::NnrModel) {
        auto* selectionGroup = new QGroupBox(tr("Model selection"), this);
        selectionGroup->setObjectName(QStringLiteral("nnrAssetSelectionGroup"));
        auto* form = new QFormLayout(selectionGroup);
        const std::array<QString, 2> names{tr("Standard slot"), tr("Premium slot")};
        for (int slot = 0; slot < 2; ++slot) {
            auto* row = new QWidget(selectionGroup);
            auto* rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            m_slotSelectors[slot] = new QComboBox(row);
            m_slotSelectors[slot]->setObjectName(
                slot == 0 ? QStringLiteral("nnrStandardAssetCombo")
                          : QStringLiteral("nnrPremiumAssetCombo"));
            m_actualLabels[slot] = new QLabel(row);
            m_actualLabels[slot]->setObjectName(
                slot == 0 ? QStringLiteral("nnrStandardActualLabel")
                          : QStringLiteral("nnrPremiumActualLabel"));
            m_actualLabels[slot]->setMinimumWidth(135);
            rowLayout->addWidget(m_slotSelectors[slot], 1);
            rowLayout->addWidget(m_actualLabels[slot]);
            form->addRow(names[slot], row);
            connect(m_slotSelectors[slot], qOverload<int>(&QComboBox::activated), this,
                    [this, slot](int index) { selectNnrModel(slot, index); });
        }
        m_selectionStatus = new QLabel(selectionGroup);
        m_selectionStatus->setObjectName(QStringLiteral("nnrAssetSelectionStatus"));
        m_selectionStatus->setWordWrap(true);
        form->addRow(tr("Status"), m_selectionStatus);
        root->addWidget(selectionGroup);
    }

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("dspAssetTable"));
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels(
        {tr("Label"), tr("Format"), tr("Encoding"), tr("Size"), tr("Identity"),
         tr("Compatibility")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setAlternatingRowColors(true);
    m_table->verticalHeader()->hide();
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Stretch);
    for (int column = 1; column < 5; ++column) {
        m_table->horizontalHeader()->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    }
    root->addWidget(m_table, 1);

    m_details = new QGroupBox(tr("Details"), this);
    m_details->setObjectName(QStringLiteral("dspAssetDetails"));
    m_details->setCheckable(true);
    m_details->setChecked(false);
    auto* detailsLayout = new QVBoxLayout(m_details);
    m_detailsText = new QLabel(tr("Select an asset to inspect its metadata."), m_details);
    m_detailsText->setObjectName(QStringLiteral("dspAssetDetailsText"));
    m_detailsText->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detailsText->setWordWrap(true);
    detailsLayout->addWidget(m_detailsText);
    root->addWidget(m_details);

    m_operationStatus = new QLabel(this);
    m_operationStatus->setObjectName(QStringLiteral("dspAssetOperationStatus"));
    m_operationStatus->setWordWrap(true);
    root->addWidget(m_operationStatus);

    auto* buttons = new QHBoxLayout();
    m_importButton = new QPushButton(tr("Import\u2026"), this);
    m_importButton->setObjectName(QStringLiteral("dspAssetImportButton"));
    m_exportButton = new QPushButton(tr("Export\u2026"), this);
    m_exportButton->setObjectName(QStringLiteral("dspAssetExportButton"));
    m_refreshButton = new QPushButton(tr("Refresh assets"), this);
    m_refreshButton->setObjectName(QStringLiteral("dspAssetRefreshButton"));
    buttons->addWidget(m_importButton);
    buttons->addWidget(m_exportButton);
    buttons->addWidget(m_refreshButton);
    buttons->addStretch(1);
    if (m_kind == DspAssetKind::NnrModel) {
        m_applyButton = new QPushButton(tr("Apply models and reconnect"), this);
        m_applyButton->setObjectName(QStringLiteral("applyNnrAssetsButton"));
        buttons->addWidget(m_applyButton);
        connect(m_applyButton, &QPushButton::clicked, this, &DspAssetDialog::applyNnrModels);
    } else {
        m_restoreButton = new QPushButton(tr("Restore selected"), this);
        m_restoreButton->setObjectName(QStringLiteral("restoreCorrectionAssetButton"));
        buttons->addWidget(m_restoreButton);
        connect(m_restoreButton, &QPushButton::clicked, this,
                &DspAssetDialog::restoreSelectedCorrection);
    }
    auto* closeButton = new QPushButton(tr("Close"), this);
    closeButton->setObjectName(QStringLiteral("dspAssetCloseButton"));
    buttons->addWidget(closeButton);
    root->addLayout(buttons);

    connect(m_importButton, &QPushButton::clicked, this, &DspAssetDialog::chooseImportFile);
    connect(m_exportButton, &QPushButton::clicked, this, &DspAssetDialog::chooseExportFile);
    connect(m_refreshButton, &QPushButton::clicked, this, &DspAssetDialog::requestList);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const int row = m_table->currentRow();
        if (row >= 0 && row < m_assets.size()) {
            const AssetRow& asset = m_assets.at(row);
            QString text = tr("ID: %1\nSHA-256: %2\nFormat: %3 v%4\nEncoding: %5\nSize: %6")
                               .arg(asset.id, asset.hash, asset.format)
                               .arg(m_table->item(row, 1)->data(Qt::UserRole).toInt())
                               .arg(asset.encoding, humanSize(asset.size));
            if (!asset.radioIdentity.isEmpty()) {
                text += tr("\nRadio: %1").arg(asset.radioIdentity);
            }
            if (!asset.compatibility.isEmpty()) {
                text += tr("\nCompatibility: %1").arg(asset.compatibility);
            }
            if (!asset.validationError.isEmpty()) {
                text += tr("\nValidation: %1").arg(asset.validationError);
            }
            m_detailsText->setText(text);
        } else {
            m_detailsText->setText(tr("Select an asset to inspect its metadata."));
        }
        updateButtons();
    });
    updateButtons();
}

QString DspAssetDialog::settingsPrefix() const
{
    return m_kind == DspAssetKind::NnrModel
               ? QStringLiteral("DspAssetDialog/NnrModel")
               : QStringLiteral("DspAssetDialog/Ps3Correction");
}

void DspAssetDialog::restoreUiState()
{
    AppSettings& settings = AppSettings::instance();
    const QString prefix = settingsPrefix();
    const QByteArray savedGeometry = QByteArray::fromBase64(
        settings.value(prefix + QString::fromLatin1(kGeometrySuffix)).toString().toLatin1());
    bool restored = false;
    if (!savedGeometry.isEmpty()) {
        restored = restoreGeometry(savedGeometry);
    }
    if (!restored) {
        resize(760, 520);
    }

    QScreen* destination = nullptr;
    qint64 largestIntersection = 0;
    for (QScreen* screen : QGuiApplication::screens()) {
        if (!screen) {
            continue;
        }
        const QRect intersection = screen->availableGeometry().intersected(frameGeometry());
        const qint64 area = qint64(intersection.width()) * intersection.height();
        if (area > largestIntersection) {
            destination = screen;
            largestIntersection = area;
        }
    }
    if (!destination) {
        destination = parentWidget() && parentWidget()->screen()
                          ? parentWidget()->screen() : QGuiApplication::primaryScreen();
    }
    if (destination) {
        const QRect available = destination->availableGeometry();
        const int safeWidth = qMin(qMax(minimumWidth(), width()), available.width());
        const int safeHeight = qMin(qMax(minimumHeight(), height()), available.height());
        resize(safeWidth, safeHeight);
        const int x = qBound(available.left(), frameGeometry().left(),
                             qMax(available.left(), available.right() - frameGeometry().width() + 1));
        const int y = qBound(available.top(), frameGeometry().top(),
                             qMax(available.top(), available.bottom() - frameGeometry().height() + 1));
        move(x + this->geometry().left() - frameGeometry().left(),
             y + this->geometry().top() - frameGeometry().top());
    }

    const bool detailsExpanded =
        settings.value(prefix + QString::fromLatin1(kDetailsSuffix),
                       QStringLiteral("False")).toString() == QStringLiteral("True");
    {
        const QSignalBlocker blocker(m_details);
        m_details->setChecked(detailsExpanded);
    }
    m_detailsText->setVisible(detailsExpanded);
}

void DspAssetDialog::saveUiState() const
{
    AppSettings::instance().setValue(
        settingsPrefix() + QString::fromLatin1(kGeometrySuffix),
        QString::fromLatin1(saveGeometry().toBase64()));
}

qint64 DspAssetDialog::kindSizeLimit() const
{
    return m_kind == DspAssetKind::NnrModel ? DspAssetValidation::kMaxNnrModelBytes
                                             : DspAssetValidation::kMaxPs3CorrectionBytes;
}

QString DspAssetDialog::currentRadioIdentity() const
{
    return m_radio ? AppSettings::normalizedRadioMac(m_radio->currentRadioMac()) : QString();
}

bool DspAssetDialog::beginRequest(Operation operation, const QByteArray& verb,
                                  const QVariantMap& args)
{
    if (!m_service || m_requestId != 0) {
        return false;
    }
    m_operation = operation;
    m_requestId = m_service->request(verb, args);
    if (m_requestId == 0) {
        m_operation = Operation::Idle;
        showError(tr("The station could not start the DSP asset request."));
        emit operationFinished(false, m_operationStatus->text());
        updateButtons();
        return false;
    }
    updateButtons();
    return true;
}

void DspAssetDialog::requestList()
{
    if (m_requestId != 0 || !m_service) {
        return;
    }
    m_operationStatus->setText(tr("Refreshing station assets\u2026"));
    beginRequest(Operation::List, "dspAssets.list", {});
}

void DspAssetDialog::handleListReply(const QVariantMap& values)
{
    QString json;
    if (!isExactString(values.value(QStringLiteral("assets")), &json)
        || json.toUtf8().size() > kMaximumListJsonBytes) {
        finishOperation(false, tr("The station returned an invalid or oversized asset list."));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(json.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isArray()
        || document.array().size() > kMaximumRows) {
        finishOperation(false, tr("The station returned an invalid asset list."));
        return;
    }

    QList<AssetRow> parsed;
    const QString radioIdentity = currentRadioIdentity();
    for (const QJsonValue& value : document.array()) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        const int kindValue = object.value(QStringLiteral("kind")).toInt(-1);
        if (kindValue != static_cast<int>(m_kind)) {
            continue;
        }
        AssetRow row;
        row.kind = static_cast<DspAssetKind>(kindValue);
        row.id = object.value(QStringLiteral("id")).toString();
        row.hash = object.value(QStringLiteral("hash")).toString();
        row.label = object.value(QStringLiteral("label")).toString();
        row.format = object.value(QStringLiteral("format")).toString();
        row.encoding = object.value(QStringLiteral("numericEncoding")).toString();
        row.compatibility = object.value(QStringLiteral("compatibility")).toString();
        row.radioIdentity = object.value(QStringLiteral("radioIdentity")).toString();
        row.size = qint64(object.value(QStringLiteral("size")).toDouble(-1));
        row.valid = object.value(QStringLiteral("valid")).toBool(false);
        row.validationError = object.value(QStringLiteral("validationError")).toString();
        row.version = object.value(QStringLiteral("version")).toInt();
        if (row.id.isEmpty() || row.id.size() > 128 || row.size <= 0
            || row.size > kindSizeLimit()) {
            continue;
        }
        if (m_kind == DspAssetKind::Ps3Correction) {
            if (!row.valid) {
                continue;
            }
            if (!radioIdentity.isEmpty()
                && AppSettings::normalizedRadioMac(row.radioIdentity) != radioIdentity) {
                continue;
            }
        }
        parsed.append(std::move(row));
    }
    m_assets = std::move(parsed);

    m_table->setRowCount(m_assets.size());
    for (int rowIndex = 0; rowIndex < m_assets.size(); ++rowIndex) {
        const AssetRow& asset = m_assets.at(rowIndex);
        const QStringList text{asset.label.isEmpty() ? tr("Unnamed asset") : asset.label,
                               asset.format,
                               asset.encoding,
                               humanSize(asset.size),
                               shortId(asset.id),
                               asset.valid ? asset.compatibility : asset.validationError};
        for (int column = 0; column < text.size(); ++column) {
            auto* item = new QTableWidgetItem(text.at(column));
            item->setToolTip(text.at(column));
            if (column == 1) {
                item->setData(Qt::UserRole, asset.version);
            }
            m_table->setItem(rowIndex, column, item);
        }
        m_table->item(rowIndex, 0)->setData(Qt::UserRole, asset.id);
    }
    if (!m_assets.isEmpty()) {
        m_table->selectRow(0);
    }
    populateNnrSelectors();
    finishOperation(true, tr("Station assets refreshed."));
}

void DspAssetDialog::populateNnrSelectors()
{
    if (m_kind != DspAssetKind::NnrModel || !m_service) {
        return;
    }
    m_populating = true;
    const std::array<QString, 2> desired = m_service->desiredNnrModelAssets();
    for (int slot = 0; slot < 2; ++slot) {
        QSignalBlocker blocker(m_slotSelectors[slot]);
        m_slotSelectors[slot]->clear();
        const QString bundled = QStringLiteral("bundled:%1").arg(slot);
        m_slotSelectors[slot]->addItem(slot == 0 ? tr("Bundled standard model")
                                                : tr("Bundled premium model"),
                                          bundled);
        for (const AssetRow& asset : std::as_const(m_assets)) {
            if (!asset.valid || asset.kind != DspAssetKind::NnrModel) {
                continue;
            }
            m_slotSelectors[slot]->addItem(
                asset.label.isEmpty() ? shortId(asset.id) : asset.label, asset.id);
        }
        int selected = m_slotSelectors[slot]->findData(desired[slot]);
        if (selected < 0) {
            m_slotSelectors[slot]->addItem(
                tr("Missing \u2014 %1").arg(shortId(desired[slot])), desired[slot]);
            selected = m_slotSelectors[slot]->count() - 1;
            m_slotSelectors[slot]->setItemData(selected,
                                               tr("This desired asset is missing or invalid."),
                                               Qt::ToolTipRole);
        }
        m_slotSelectors[slot]->setCurrentIndex(selected);
        m_lastAcceptedSelection[slot] = desired[slot];
    }
    m_populating = false;
    updateSelectionSummary();
}

void DspAssetDialog::updateSelectionSummary()
{
    if (m_kind != DspAssetKind::NnrModel || !m_service || !m_selectionStatus) {
        return;
    }
    const auto active = m_service->activeNnrModelAssets();
    for (int slot = 0; slot < 2; ++slot) {
        m_actualLabels[slot]->setText(tr("Active: %1").arg(shortId(active[slot])));
    }
    QString status = m_service->nnrModelStatus();
    if (m_service->nnrModelSelectionPending()) {
        status = tr("Pending reconnect. %1").arg(status);
    }
    m_selectionStatus->setText(status);
    updateButtons();
}

QString DspAssetDialog::selectedAssetId() const
{
    if (!m_table) {
        return {};
    }
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_assets.size()) {
        return {};
    }
    return m_assets.at(row).id;
}

void DspAssetDialog::updateButtons()
{
    const bool available = m_service && m_operation == Operation::Idle && m_requestId == 0;
    const bool hasSelection = !selectedAssetId().isEmpty();
    if (m_importButton) {
        m_importButton->setEnabled(available);
    }
    if (m_exportButton) {
        m_exportButton->setEnabled(available && hasSelection);
    }
    if (m_refreshButton) {
        m_refreshButton->setEnabled(available);
    }
    if (m_applyButton) {
        m_applyButton->setEnabled(available && m_radio);
    }
    if (m_restoreButton) {
        const bool canRestore = m_radio && m_radio->pureSignalFacade()->canActuate();
        m_restoreButton->setEnabled(available && hasSelection && canRestore);
        m_restoreButton->setToolTip(canRestore ? QString()
            : tr("Restore applies a correction and requires transmit authorization. "
                 "Remote operation becomes available with R4; import and export remain available."));
    }
    for (QComboBox* selector : m_slotSelectors) {
        if (selector) {
            selector->setEnabled(available);
        }
    }
}

void DspAssetDialog::showError(const QString& error)
{
    if (m_operationStatus) {
        m_operationStatus->setText(error);
    }
}

void DspAssetDialog::finishOperation(bool accepted, const QString& reason)
{
    m_requestId = 0;
    m_operation = Operation::Idle;
    if (!reason.isEmpty()) {
        m_operationStatus->setText(reason);
    }
    updateButtons();
    emit operationFinished(accepted, reason);
}

bool DspAssetDialog::importFile(const QString& path, const QString& label)
{
    if (!m_service || m_operation != Operation::Idle || m_requestId != 0) {
        return false;
    }
    auto input = std::make_unique<QFile>(path);
    if (!input->open(QIODevice::ReadOnly)) {
        showError(tr("Could not open the selected file for reading."));
        return false;
    }
    const qint64 size = input->size();
    if (size <= 0 || size > kindSizeLimit()) {
        showError(tr("The selected file is empty or exceeds the %1 format limit.")
                      .arg(humanSize(kindSizeLimit())));
        return false;
    }
    QString boundedLabel = label.trimmed();
    if (boundedLabel.isEmpty()) {
        boundedLabel = QFileInfo(path).completeBaseName().trimmed();
    }
    if (boundedLabel.isEmpty()) {
        boundedLabel = QFileInfo(path).fileName();
    }
    if (boundedLabel.size() > 128) {
        showError(tr("The asset label may contain at most 128 characters."));
        return false;
    }
    const QString radioIdentity = currentRadioIdentity();
    if (m_kind == DspAssetKind::Ps3Correction && radioIdentity.isEmpty()) {
        showError(tr("Connect to the correction file's radio before importing it."));
        return false;
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!input->atEnd()) {
        const QByteArray chunk = input->read(DspAssetStore::kTransferChunkBytes);
        if (chunk.isEmpty() && input->error() != QFile::NoError) {
            showError(tr("Could not read the selected file."));
            return false;
        }
        hash.addData(chunk);
    }
    if (!input->seek(0)) {
        showError(tr("Could not rewind the selected file."));
        return false;
    }

    m_importFile = std::move(input);
    m_importSize = size;
    m_importOffset = 0;
    m_importLabel = boundedLabel;
    m_importHash = QString::fromLatin1(hash.result().toHex());
    m_operationStatus->setText(tr("Starting bounded import\u2026"));
    const QVariantMap args{{QStringLiteral("kind"), static_cast<int>(m_kind)},
                           {QStringLiteral("label"), m_importLabel},
                           {QStringLiteral("size"), m_importSize},
                           {QStringLiteral("hash"), m_importHash},
                           {QStringLiteral("radioIdentity"), radioIdentity}};
    if (!beginRequest(Operation::BeginImport, "dspAssets.beginImport", args)) {
        m_importFile.reset();
        return false;
    }
    return true;
}

void DspAssetDialog::requestNextImportChunk()
{
    if (!m_importFile || m_importTransferId.isEmpty()) {
        abortOperation(true);
        finishOperation(false, tr("The import transfer was lost."));
        return;
    }
    if (m_importOffset == m_importSize) {
        if (!beginRequest(Operation::FinishImport, "dspAssets.finishImport",
                          {{QStringLiteral("transferId"), m_importTransferId}})) {
            abortOperation(true);
        }
        return;
    }
    const QByteArray chunk = m_importFile->read(DspAssetStore::kTransferChunkBytes);
    if (chunk.isEmpty() || chunk.size() > DspAssetStore::kTransferChunkBytes
        || m_importOffset + chunk.size() > m_importSize) {
        const QString reason = tr("Reading the import file failed before its advertised size.");
        abortOperation(true);
        finishOperation(false, reason);
        return;
    }
    m_lastImportChunkSize = chunk.size();
    m_operationStatus->setText(tr("Importing %1 of %2\u2026")
                                   .arg(humanSize(m_importOffset), humanSize(m_importSize)));
    if (!beginRequest(Operation::ImportChunk, "dspAssets.chunk",
                      {{QStringLiteral("transferId"), m_importTransferId},
                       {QStringLiteral("offset"), m_importOffset},
                       {QStringLiteral("data"), QString::fromLatin1(chunk.toBase64())}})) {
        abortOperation(true);
    }
}

bool DspAssetDialog::exportAssetToFile(const QString& assetId, const QString& path)
{
    if (!m_service || m_operation != Operation::Idle || m_requestId != 0
        || assetId.isEmpty() || assetId.size() > 128)
        return false;
    auto output = std::make_unique<QSaveFile>(path);
    if (!output->open(QIODevice::WriteOnly)) {
        showError(tr("Could not open the export destination."));
        return false;
    }
    m_exportFile = std::move(output);
    m_exportHasher = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_exportAssetId = assetId;
    m_exportExpectedHash.clear();
    m_exportExpectedSize = -1;
    m_exportOffset = 0;
    m_operationStatus->setText(tr("Starting bounded export\u2026"));
    requestNextExportChunk();
    return m_requestId != 0;
}

void DspAssetDialog::requestNextExportChunk()
{
    if (!m_exportFile || !m_exportHasher || m_exportAssetId.isEmpty()) {
        abortOperation(false);
        finishOperation(false, tr("The export transfer was lost."));
        return;
    }
    if (!beginRequest(Operation::ExportChunk, "dspAssets.export",
                      {{QStringLiteral("id"), m_exportAssetId},
                       {QStringLiteral("offset"), m_exportOffset}})) {
        abortOperation(false);
    }
}

void DspAssetDialog::onRequestCompleted(quint32 id, bool accepted,
                                        const QString& reason,
                                        const QVariantMap& values)
{
    if (id == 0 || id != m_requestId || m_closing) {
        return;
    }
    const Operation completed = m_operation;
    m_requestId = 0;

    if (completed == Operation::SelectModel) {
        handleSelectionReply(accepted, reason, values);
        return;
    }
    if (!accepted) {
        const QString message = reason.isEmpty() ? tr("The station refused the DSP asset request.")
                                                  : reason;
        const bool importActive = completed == Operation::BeginImport
                                  || completed == Operation::ImportChunk
                                  || completed == Operation::FinishImport;
        abortOperation(importActive);
        finishOperation(false, message);
        return;
    }

    switch (completed) {
    case Operation::List:
        handleListReply(values);
        break;
    case Operation::BeginImport: {
        QString token;
        if (!isExactString(values.value(QStringLiteral("transferId")), &token)
            || token.isEmpty() || token.size() > 128) {
            abortOperation(false);
            finishOperation(false, tr("The station returned an invalid import transfer ID."));
            return;
        }
        m_importTransferId = token;
        requestNextImportChunk();
        break;
    }
    case Operation::ImportChunk: {
        qint64 offset = -1;
        const qint64 expected = m_importOffset + m_lastImportChunkSize;
        if (!isExactInteger(values.value(QStringLiteral("offset")), &offset)
            || offset != expected || offset > m_importSize) {
            abortOperation(true);
            finishOperation(false, tr("The station acknowledged an unexpected import offset."));
            return;
        }
        m_importOffset = offset;
        requestNextImportChunk();
        break;
    }
    case Operation::FinishImport: {
        m_importFile.reset();
        m_importTransferId.clear();
        finishOperation(true, tr("Asset imported and validated by the station."));
        requestList();
        break;
    }
    case Operation::ExportChunk: {
        qint64 offset = -1;
        qint64 size = -1;
        QString hash;
        QString encoded;
        bool eof = false;
        static const QRegularExpression hashPattern(QStringLiteral("^[0-9a-f]{64}$"));
        if (!isExactInteger(values.value(QStringLiteral("offset")), &offset)
            || !isExactInteger(values.value(QStringLiteral("size")), &size)
            || !isExactString(values.value(QStringLiteral("hash")), &hash)
            || !isExactString(values.value(QStringLiteral("data")), &encoded)
            || !isExactBool(values.value(QStringLiteral("eof")), &eof)
            || offset != m_exportOffset || size <= 0 || size > kindSizeLimit()
            || !hashPattern.match(hash).hasMatch()) {
            abortOperation(false);
            finishOperation(false, tr("The station returned invalid export metadata."));
            return;
        }
        if (m_exportExpectedSize < 0) {
            m_exportExpectedSize = size;
            m_exportExpectedHash = hash;
        } else if (size != m_exportExpectedSize || hash != m_exportExpectedHash) {
            abortOperation(false);
            finishOperation(false, tr("The export identity changed during transfer."));
            return;
        }
        const QByteArray ascii = encoded.toLatin1();
        const auto decoded = QByteArray::fromBase64Encoding(
            ascii, QByteArray::AbortOnBase64DecodingErrors);
        if (QString::fromLatin1(ascii) != encoded || !decoded
            || decoded.decoded.toBase64() != ascii
            || decoded.decoded.size() > DspAssetStore::kTransferChunkBytes
            || m_exportOffset + decoded.decoded.size() > m_exportExpectedSize
            || (!eof && decoded.decoded.isEmpty())) {
            abortOperation(false);
            finishOperation(false, tr("The station returned invalid bounded export data."));
            return;
        }
        if (m_exportFile->write(decoded.decoded) != decoded.decoded.size()) {
            abortOperation(false);
            finishOperation(false, tr("Writing the export destination failed."));
            return;
        }
        m_exportHasher->addData(decoded.decoded);
        m_exportOffset += decoded.decoded.size();
        if (!eof) {
            if (m_exportOffset >= m_exportExpectedSize) {
                abortOperation(false);
                finishOperation(false, tr("The station omitted the export end marker."));
                return;
            }
            m_operationStatus->setText(tr("Exporting %1 of %2\u2026")
                                           .arg(humanSize(m_exportOffset),
                                                humanSize(m_exportExpectedSize)));
            requestNextExportChunk();
            return;
        }
        const QString actualHash = QString::fromLatin1(m_exportHasher->result().toHex());
        if (m_exportOffset != m_exportExpectedSize || actualHash != m_exportExpectedHash
            || !m_exportFile->commit()) {
            abortOperation(false);
            finishOperation(false, tr("The export was incomplete or failed SHA-256 verification."));
            return;
        }
        m_exportFile.reset();
        m_exportHasher.reset();
        finishOperation(true, tr("Asset exported after size and SHA-256 verification."));
        break;
    }
    case Operation::CancelImport:
        finishOperation(true, {});
        break;
    case Operation::Idle:
    case Operation::SelectModel:
        break;
    }
}

void DspAssetDialog::selectNnrModel(int slot, int comboIndex)
{
    if (m_populating || slot < 0 || slot > 1 || !m_slotSelectors[slot]
        || m_operation != Operation::Idle || m_requestId != 0)
        return;
    const QString id = m_slotSelectors[slot]->itemData(comboIndex).toString();
    if (id.isEmpty() || id == m_lastAcceptedSelection[slot]) {
        return;
    }
    m_selectingSlot = slot;
    m_operationStatus->setText(tr("Saving the desired model selection\u2026"));
    if (!beginRequest(Operation::SelectModel, "dspAssets.selectNnrModel",
                      {{QStringLiteral("slot"), slot}, {QStringLiteral("id"), id}})) {
        populateNnrSelectors();
        m_selectingSlot = -1;
    }
}

void DspAssetDialog::handleSelectionReply(bool accepted, const QString& reason,
                                          const QVariantMap&)
{
    if (!accepted) {
        populateNnrSelectors();
        m_selectingSlot = -1;
        finishOperation(false, reason.isEmpty() ? tr("The station refused the model selection.")
                                                : reason);
        return;
    }
    if (m_selectingSlot >= 0 && m_selectingSlot < 2) {
        m_lastAcceptedSelection[m_selectingSlot] =
            m_slotSelectors[m_selectingSlot]->currentData().toString();
    }
    m_selectingSlot = -1;
    populateNnrSelectors();
    finishOperation(true, tr("Desired model saved. Reconnect explicitly to apply it."));
}

void DspAssetDialog::chooseImportFile()
{
    const QString filter = m_kind == DspAssetKind::NnrModel
                               ? tr("WDSP neural models (*.bin *.nn);;All files (*)")
                               : tr("PureSignal v2 corrections (*.txt *.ps3);;All files (*)");
    const QString path = QFileDialog::getOpenFileName(this, tr("Import DSP asset"), {}, filter);
    if (path.isEmpty()) {
        return;
    }
    bool accepted = false;
    QString label = QInputDialog::getText(this, tr("Asset label"), tr("Label"),
                                          QLineEdit::Normal,
                                          QFileInfo(path).completeBaseName(), &accepted);
    if (!accepted) {
        return;
    }
    if (label.size() > 128) {
        label.truncate(128);
    }
    importFile(path, label);
}

void DspAssetDialog::chooseExportFile()
{
    const QString id = selectedAssetId();
    if (id.isEmpty()) {
        return;
    }
    const QString suffix = m_kind == DspAssetKind::NnrModel ? QStringLiteral(".bin")
                                                             : QStringLiteral(".txt");
    QString suggested;
    const int row = m_table->currentRow();
    if (row >= 0 && row < m_assets.size()) {
        suggested = m_assets.at(row).label;
    }
    if (suggested.isEmpty()) {
        suggested = QStringLiteral("dsp-asset");
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Export DSP asset"),
                                                       suggested + suffix);
    if (!path.isEmpty()) {
        exportAssetToFile(id, path);
    }
}

void DspAssetDialog::applyNnrModels()
{
    if (m_kind != DspAssetKind::NnrModel || !m_radio || !m_service
        || m_operation != Operation::Idle)
        return;
    QString reason;
    if (!m_radio->applyNnrModelSelection(m_service->selectionRevision(), &reason)) {
        showError(reason.isEmpty() ? tr("The station refused the reconnect request.") : reason);
        return;
    }
    m_operationStatus->setText(reason.isEmpty()
                                   ? tr("Reconnect requested. The station will report when models are active.")
                                   : reason);
}

void DspAssetDialog::restoreSelectedCorrection()
{
    if (m_kind != DspAssetKind::Ps3Correction || m_operation != Operation::Idle
        || m_requestId != 0 || !m_radio || !m_radio->pureSignalFacade()->canActuate()) {
        return;
    }
    const QString id = selectedAssetId();
    if (!id.isEmpty()) {
        emit restoreCorrectionRequested(id);
    }
}

void DspAssetDialog::abortOperation(bool requestCancellation)
{
    const QString token = m_importTransferId;
    m_requestId = 0;
    m_operation = Operation::Idle;
    if (m_importFile) {
        m_importFile->close();
    }
    m_importFile.reset();
    m_importTransferId.clear();
    m_importLabel.clear();
    m_importHash.clear();
    m_importSize = 0;
    m_importOffset = 0;
    m_lastImportChunkSize = 0;
    if (m_exportFile) {
        m_exportFile->cancelWriting();
    }
    m_exportFile.reset();
    m_exportHasher.reset();
    m_exportAssetId.clear();
    m_exportExpectedHash.clear();
    m_exportExpectedSize = -1;
    m_exportOffset = 0;
    if (requestCancellation && m_service && !token.isEmpty()) {
        // Fire and forget: this dialog has retired the transfer and deliberately
        // ignores the cancellation reply. The service owns the station request.
        m_service->request("dspAssets.cancelImport",
                           {{QStringLiteral("transferId"), token}});
    }
    updateButtons();
}

void DspAssetDialog::retireForSessionChange()
{
    if (m_closing) {
        return;
    }
    abortOperation(true);
    showError(tr("The station session changed. Reopen the asset manager to refresh its state."));
    hide();
}

void DspAssetDialog::closeEvent(QCloseEvent* event)
{
    m_closing = true;
    abortOperation(true);
    saveUiState();
    m_closing = false;
    event->accept();
}

} // namespace NereusSDR
