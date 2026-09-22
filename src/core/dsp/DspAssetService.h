// SPDX-License-Identifier: GPL-2.0-or-later
// NereusSDR-original local/remote station DSP asset contract.
#pragma once

#include "DspAssetStore.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QVariantMap>

#include <array>
#include <functional>
#include <memory>

class QCryptographicHash;

namespace NereusSDR {

class AppSettings;

struct DspAssetServiceResult {
    bool accepted{false};
    QString reason;
    QVariantMap values;
};

class DspAssetService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString nnrStandardAsset READ nnrStandardAsset NOTIFY selectionChanged)
    Q_PROPERTY(QString nnrPremiumAsset READ nnrPremiumAsset NOTIFY selectionChanged)
    Q_PROPERTY(bool nnrModelSelectionPending READ nnrModelSelectionPending NOTIFY selectionChanged)
    Q_PROPERTY(QString nnrModelStatus READ nnrModelStatus NOTIFY selectionChanged)
    Q_PROPERTY(quint32 selectionRevision READ selectionRevision NOTIFY selectionChanged)

public:
    using RemoteRequestHandler =
        std::function<quint32(const QByteArray&, const QVariantMap&)>;

    explicit DspAssetService(AppSettings& settings, bool local,
                             QObject* parent = nullptr);
    ~DspAssetService() override;

    bool isLocal() const noexcept { return m_local; }
    DspAssetStore* store() const noexcept { return m_store.get(); }

    QString nnrStandardAsset() const { return m_selected[0]; }
    QString nnrPremiumAsset() const { return m_selected[1]; }
    std::array<QString, 2> desiredNnrModelAssets() const { return m_selected; }
    std::array<QString, 2> activeNnrModelAssets() const { return m_active; }
    bool nnrModelSelectionPending() const;
    QString nnrModelStatus() const { return m_status; }
    quint32 selectionRevision() const noexcept { return m_revision; }

    void setRadioIdentity(const QString& mac);
    DspAssetServiceResult execute(const QByteArray& verb, const QVariantMap& args,
                                  const QString& owner);
    quint32 request(const QByteArray& verb, const QVariantMap& args);
    void setRemoteRequestHandler(RemoteRequestHandler handler);
    bool receiveRemoteResult(quint32 id, const QByteArray& verb, bool accepted,
                             const QString& reason, const QVariantMap& values);
    void resetSession();
    void cancelOwner(const QString& owner);

    std::array<QString, 2> resolveNnrModelPaths(QString* reason = nullptr);
    void markNnrModelsApplied();
    bool applyRemoteProperty(const QByteArray& name, const QVariant& value);

signals:
    void requestCompleted(quint32 id, bool accepted, QString reason, QVariantMap values);
    void selectionChanged();
    void configurationChanged();

private:
    struct ActiveImport;
    struct PendingRequest {
        QByteArray verb;
    };

    quint32 allocateRequestId();
    void refreshSelectionStatus();
    bool setSelection(int slot, const QString& id, QString* reason);
    DspAssetServiceResult reject(const QString& reason) const;

    AppSettings& m_settings;
    bool m_local{false};
    std::unique_ptr<DspAssetStore> m_store;
    RemoteRequestHandler m_remoteRequest;
    QHash<QString, std::shared_ptr<ActiveImport>> m_imports;
    QHash<quint32, PendingRequest> m_pendingRequests;
    quint32 m_nextRequestId{1};
    std::array<QString, 2> m_selected;
    std::array<QString, 2> m_active;
    std::array<QString, 2> m_lastResolved;
    quint32 m_revision{1};
    QString m_status;
    QString m_radioIdentity;
    bool m_remotePending{false};
};

} // namespace NereusSDR
