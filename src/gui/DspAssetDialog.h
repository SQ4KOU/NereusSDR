// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original bounded station DSP asset manager.
#pragma once

#include "core/dsp/DspAssetValidation.h"

#include <QDialog>
#include <QList>
#include <QPointer>
#include <QVariantMap>

#include <memory>

class QCloseEvent;
class QComboBox;
class QCryptographicHash;
class QFile;
class QGroupBox;
class QLabel;
class QPushButton;
class QSaveFile;
class QTableWidget;

namespace NereusSDR {

class DspAssetService;
class RadioModel;

// Modeless manager for station-owned NNR models or PureSignal v2 correction
// files. All station operations cross DspAssetService's typed request boundary;
// paths and file handles stay in this GUI process.
class DspAssetDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit DspAssetDialog(RadioModel* radio, DspAssetKind kind,
                            QWidget* parent = nullptr);

    // Narrow test/host seam. Production callers use the constructor above and
    // therefore always consume RadioModel::dspAssets().
    DspAssetDialog(RadioModel* radio, DspAssetService* service,
                   DspAssetKind kind, QWidget* parent = nullptr);
    ~DspAssetDialog() override;

    // These enter the same asynchronous request state machine as the file
    // picker actions. They let hosts and tests supply an already chosen path.
    bool importFile(const QString& path, const QString& label = {});
    bool exportAssetToFile(const QString& assetId, const QString& path);
    QString selectedAssetId() const;

signals:
    void restoreCorrectionRequested(QString assetId);
    void operationFinished(bool accepted, QString reason);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    enum class Operation {
        Idle,
        List,
        BeginImport,
        ImportChunk,
        FinishImport,
        CancelImport,
        SelectModel,
        ExportChunk
    };

    struct AssetRow {
        QString id;
        QString hash;
        QString label;
        QString format;
        QString encoding;
        QString compatibility;
        QString radioIdentity;
        qint64 size{0};
        DspAssetKind kind{DspAssetKind::NnrModel};
        bool valid{false};
        QString validationError;
        int version{0};
    };

    void buildUi();
    void requestList();
    bool beginRequest(Operation operation, const QByteArray& verb,
                      const QVariantMap& args);
    void requestNextImportChunk();
    void requestNextExportChunk();
    void onRequestCompleted(quint32 id, bool accepted, const QString& reason,
                            const QVariantMap& values);
    void handleListReply(const QVariantMap& values);
    void handleSelectionReply(bool accepted, const QString& reason,
                              const QVariantMap& values);
    void populateNnrSelectors();
    void updateSelectionSummary();
    void updateButtons();
    void restoreUiState();
    void saveUiState() const;
    QString settingsPrefix() const;
    void showError(const QString& error);
    void finishOperation(bool accepted, const QString& reason);
    void abortOperation(bool requestCancellation);
    void retireForSessionChange();
    void chooseImportFile();
    void chooseExportFile();
    void applyNnrModels();
    void restoreSelectedCorrection();
    void selectNnrModel(int slot, int comboIndex);
    qint64 kindSizeLimit() const;
    QString currentRadioIdentity() const;

    QPointer<RadioModel> m_radio;
    QPointer<DspAssetService> m_service;
    DspAssetKind m_kind{DspAssetKind::NnrModel};
    Operation m_operation{Operation::Idle};
    quint32 m_requestId{0};
    bool m_closing{false};
    bool m_populating{false};

    QList<AssetRow> m_assets;
    QTableWidget* m_table{nullptr};
    QComboBox* m_slotSelectors[2]{nullptr, nullptr};
    QLabel* m_actualLabels[2]{nullptr, nullptr};
    QLabel* m_selectionStatus{nullptr};
    QLabel* m_operationStatus{nullptr};
    QGroupBox* m_details{nullptr};
    QLabel* m_detailsText{nullptr};
    QPushButton* m_importButton{nullptr};
    QPushButton* m_exportButton{nullptr};
    QPushButton* m_refreshButton{nullptr};
    QPushButton* m_applyButton{nullptr};
    QPushButton* m_restoreButton{nullptr};

    std::unique_ptr<QFile> m_importFile;
    QString m_importTransferId;
    QString m_importLabel;
    QString m_importHash;
    qint64 m_importSize{0};
    qint64 m_importOffset{0};
    qsizetype m_lastImportChunkSize{0};

    std::unique_ptr<QSaveFile> m_exportFile;
    std::unique_ptr<QCryptographicHash> m_exportHasher;
    QString m_exportAssetId;
    QString m_exportExpectedHash;
    qint64 m_exportExpectedSize{-1};
    qint64 m_exportOffset{0};

    int m_selectingSlot{-1};
    QString m_lastAcceptedSelection[2];
};

} // namespace NereusSDR
