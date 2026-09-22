#pragma once
// no-port-check: NereusSDR-original session-neutral PS3 presentation boundary.
#include "core/dsp/Ps3Snapshot.h"
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantMap>
#include <functional>
#include <optional>

namespace NereusSDR {
class RadioModel;
class PureSignal;
class PureSignalSettings;

enum class Ps3Action {
    OffReset, Single, StartAutomatic, ApplyCurrentCorrection, SetTwoTone,
    SaveCorrection, RestoreCorrection
};
enum class Ps3ActionPhase { Accepted, Pending, Completed, Failed };
struct Ps3ActionResult {
    quint32 operationId{0};
    Ps3ActionPhase phase{Ps3ActionPhase::Failed};
    QString reason;
    QVariantMap values;
};

class PureSignalSessionFacade final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY statusChanged)
    Q_PROPERTY(bool canActuate READ canActuate NOTIFY statusChanged)
    Q_PROPERTY(bool twoToneOn READ twoToneOn NOTIFY statusChanged)
    Q_PROPERTY(QString statusJson READ statusJson NOTIFY statusChanged)
    Q_PROPERTY(QString lastActionError READ lastActionError NOTIFY statusChanged)
    Q_PROPERTY(quint64 displayGeneration READ displayGeneration NOTIFY statusChanged)
public:
    using RemoteRequestHandler = std::function<quint32(Ps3Action, const QVariantMap&)>;
    explicit PureSignalSessionFacade(RadioModel* radio, PureSignal* coordinator = nullptr,
                                     QObject* parent = nullptr);
    ~PureSignalSessionFacade() override;
    PureSignalSettings* settings() const;
    bool available() const { return m_available; }
    bool canActuate() const { return m_canActuate; }
    bool twoToneOn() const { return m_twoToneOn; }
    QString statusJson() const { return m_statusJson; }
    QString lastActionError() const { return m_lastError; }
    quint64 displayGeneration() const { return m_displayGeneration; }
    Ps3StatusSnapshot statusSnapshot() const { return m_status; }
    std::optional<Ps3Snapshot> displaySnapshot() const { return m_display; }

    quint32 requestAction(Ps3Action action, const QVariantMap& arguments = {});
    Ps3ActionResult executeAction(Ps3Action action, const QVariantMap& arguments,
                                  quint32 operationId);
    void setCoordinator(PureSignal* coordinator);
    void setRemoteRequestHandler(RemoteRequestHandler handler);
    void setRemoteCapabilities(bool available, bool canActuate);
    void receiveRemoteActionResult(quint32 operationId, const QByteArray& verb, Ps3ActionPhase phase,
                                    const QString& reason, const QVariantMap& values);
    bool applyRemoteProperty(const QByteArray& property, const QVariant& value);
    void resetSession();

    void setAmpViewSubscribed(bool subscribed);
    void setRemoteAmpViewSubscribed(bool subscribed);
    bool ampViewSubscribed() const { return m_ampViewSubscribed; }
    bool remoteAmpViewSubscribed() const { return m_remoteAmpViewSubscribed; }
    void receiveDisplaySnapshot(const Ps3Snapshot& snapshot);
    static QByteArray actionVerb(Ps3Action action);
    static std::optional<Ps3Action> actionForVerb(const QByteArray& verb);

signals:
    void statusChanged();
    void actionResult(quint32 operationId, NereusSDR::Ps3ActionPhase phase,
                      QString reason, QVariantMap values);
    void displaySnapshotReady(const NereusSDR::Ps3Snapshot& snapshot);
    void displayInvalidated();
    void displaySubscriptionRequested(bool subscribed);

private:
    struct PendingOperation {
        PendingOperation() = default;
        PendingOperation(Ps3Action requestedAction, quint64 sessionGeneration)
            : action(requestedAction), generation(sessionGeneration) {}
        Ps3Action action{Ps3Action::OffReset};
        quint64 generation{0};
        int startAttempts{0};
        int startSuccesses{0};
        QString path;
        QString label;
        std::optional<Ps3FileOperationToken> file;
    };
    void refreshStatus();
    void acquireDisplay();
    void reconcileDisplayTimer();
    void finishFile(int kind, int result, quint64 generation, quint64 completion);
    void retainRetiredSave(const PendingOperation& pending);
    void finishOperation(quint32 operationId, Ps3ActionPhase phase,
                          const QString& reason, const QVariantMap& values = {});
    bool remote() const;

    QPointer<RadioModel> m_radio;
    QPointer<PureSignal> m_coordinator;
    RemoteRequestHandler m_remoteRequest;
    QHash<quint32, PendingOperation> m_pending;
    QSet<quint32> m_queued;
    struct RetiredSave {
        QPointer<PureSignal> owner;
        PendingOperation operation;
    };
    QList<RetiredSave> m_retiredSaves;
    QTimer m_displayTimer;
    quint32 m_nextOperationId{1};
    quint64 m_displayGeneration{1};
    quint64 m_displaySequence{0};
    quint64 m_actionGeneration{1};
    bool m_available{false};
    bool m_remoteCapabilityAvailable{false};
    bool m_remoteTxPermitted{false};
    bool m_remoteRuntimeAvailable{false};
    bool m_remoteRuntimeCanActuate{false};
    bool m_canActuate{false};
    bool m_twoToneOn{false};
    bool m_ampViewSubscribed{false};
    bool m_remoteAmpViewSubscribed{false};
    QString m_statusJson;
    QString m_lastError;
    Ps3StatusSnapshot m_status;
    std::optional<Ps3Snapshot> m_display;
};
} // namespace NereusSDR
Q_DECLARE_METATYPE(NereusSDR::Ps3ActionPhase)
Q_DECLARE_METATYPE(NereusSDR::Ps3Snapshot)
