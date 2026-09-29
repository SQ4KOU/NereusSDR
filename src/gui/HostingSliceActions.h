#pragma once
// no-port-check: NereusSDR-original.
// =================================================================
// src/gui/HostingSliceActions.h  (NereusSDR)
// =================================================================
//
// The hosting desktop's slice requests (slice control and shared listening
// plan Task 10). A desktop that hosts a Core is the station device, and its
// close, select, add, listen, stop listening, take control, release and
// listening level run as that device through
// StationServer::invokeAsStationDevice(): the same dispatcher, checks,
// confirm step and slice access a remote device's requests take. Nothing
// here calls RadioModel's slice entry points directly.
//
// A request's answer arrives as finished(); a refusal is also refused(),
// worded for the operator. A request held for a question is pending(), and
// the question itself is question(), answered with proceed() or cancel().
// A notice meant for the station device is notice(); controlTaken is a
// refusal instead, as it is in a remote window (Task 5).
//
// Each request names the slice as it is now (its incarnation and control
// revision from SliceOwnership), so the Core refuses one that no longer
// matches.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-29: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), slice control and shared listening plan Task 10,
//               with AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/SessionMessages.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

namespace NereusSDR {

class RadioModel;
class StationServer;

class HostingSliceActions : public QObject {
    Q_OBJECT

public:
    HostingSliceActions(StationServer* server, RadioModel* model, QObject* parent = nullptr);
    ~HostingSliceActions() override;

    // The slice access verbs (Task 5's four, Task 14b's level).
    void listen(int sliceId);
    void stopListening(int sliceId);
    void takeControl(int sliceId);
    void release(int sliceId);
    void setListenLevel(int sliceId, double level, bool muted);

    /// The station device's active slice (setActiveSliceById).
    void select(int sliceId);
    /// A new slice on `panId` (addSliceOnPan): +RX, the layout's empty pans,
    /// the chooser's New slice. At full capacity this asks first.
    void addOnPan(const QString& panId);
    /// Close (removeSlice). A slice someone else listens to stays open for
    /// them (Q6).
    void close(int sliceId);

    /// Answer the question question() carried: `choice` is the picked
    /// receiver or slice for a take, -1 for none.
    void proceed(qint64 questionId, qint64 choice);
    void cancel(qint64 questionId);
    /// Take it back, on a notice that offers it.
    void takeBack(qint64 noticeId);

    /// True while a request is being run. A refusal RadioModel raises
    /// during it (sliceAddRejected) is also this request's refusal, so a
    /// window showing both can skip the model's.
    bool invoking() const { return m_invokeDepth > 0; }

signals:
    /// A request was refused; `reason` is the operator's words.
    void refused(const QString& reason);
    /// The Core asks first (confirm.request); `prompt.prompt` is the
    /// question, answered with proceed() or cancel().
    void question(const NereusSDR::SessionMessage& prompt);
    /// A request on `sliceId` (-1 for an add) waits for a question (true),
    /// or no longer does (false).
    void pending(int sliceId, bool waiting);
    /// A notice for the station device, controlTaken excepted.
    void notice(const NereusSDR::SessionMessage& notice);
    /// Every request's final answer.
    void finished(const QByteArray& verb, int sliceId, bool accepted, const QString& reason);

private:
    void run(const QByteArray& verb, int sliceId, const QList<MirrorUpdate>& arguments);
    void onAnswer(const QByteArray& verb, int sliceId, const SessionMessage& result);
    QList<MirrorUpdate> sliceArguments(int sliceId, bool withRevision) const;

    QPointer<StationServer> m_server;
    QPointer<RadioModel> m_model;
    quint32 m_nextCommandId = 1;
    int m_invokeDepth = 0;
    /// Requests answered "Waiting for you to confirm." and not yet finally.
    QSet<quint32> m_waiting;
};

} // namespace NereusSDR
