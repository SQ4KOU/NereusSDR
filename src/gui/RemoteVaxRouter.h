// =================================================================
// src/gui/RemoteVaxRouter.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-44 (R3 receiver audio plan,
// Task 5): VAX in a remote window.
//
// Owns the four VAX channels' feeders (core/audio/RemoteVaxFeeder) and
// decides, on the GUI thread, which of the Core's receivers each one asks
// for:
//
//   - A slice's VAX channel is kept on this computer, per Core and slice
//     (settingsKey()), never in the Core's Slice<N>/VaxChannel. The router
//     installs itself as the remote model's VAX channel store and restores
//     each slice's channel as the slice appears.
//   - VAX N carries the lowest-numbered slice assigned to it.
//   - Its stream is asked for while the channel has a slice and an open
//     output, and, where the platform reports whether an app is reading
//     the output (macOS, PipeWire), only while one is. Elsewhere it runs
//     while the channel is assigned.
//   - Request and release run here, on the GUI thread, never from the
//     feeders' block callbacks (RemoteMediaController's contract).
//
// A reason the Core's stream stopped for is raised once as notice(), in
// its wire or sentence form (OperatorReasonText::forDisplay words it);
// media-not-ready is not raised, being the link's own state.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <array>
#include <functional>
#include <memory>

class QTimer;

namespace NereusSDR {

class AudioEngine;
class IReceiverPcmSink;
class RadioModel;
class RemoteVaxFeeder;
struct RemoteStationOptions;

class RemoteVaxRouter final : public QObject {
    Q_OBJECT
public:
    static constexpr int kChannels = 4;
    static constexpr int kReaderPollMs = 500;

    /// How the router asks the Core for a receiver's audio (MainWindow
    /// wires RemoteMediaController::requestReceiverAudio / release).
    struct ReceiverAudio {
        std::function<void(int sliceId, IReceiverPcmSink* sink)> request;
        std::function<void(int sliceId, IReceiverPcmSink* sink)> release;
    };

    /// Builds channel N's feeder (1..4). Tests pass their own output and
    /// clock; production uses the engine's VAX outputs.
    using FeederFactory = std::function<std::unique_ptr<RemoteVaxFeeder>(int channel)>;

    /// `coreKey` names the Core the assignments belong to (coreKeyFor()).
    /// With startWorkers false the feeders' workers are never started (a
    /// test pumps them itself).
    RemoteVaxRouter(RadioModel* model, AudioEngine* engine, QString coreKey,
                    QObject* parent = nullptr);
    RemoteVaxRouter(RadioModel* model, AudioEngine* engine, QString coreKey,
                    FeederFactory factory, bool startWorkers, QObject* parent = nullptr);
    ~RemoteVaxRouter() override;

    /// A short, stable name for the Core a remote window connects to: its
    /// pinned certificate when there is one, else its address.
    static QString coreKeyFor(const RemoteStationOptions& station);
    /// This computer's setting for a slice's VAX channel with that Core.
    static QString settingsKey(const QString& coreKey, int sliceId);

    void setReceiverAudio(ReceiverAudio source);

    RemoteVaxFeeder* feeder(int channel) const;
    /// The slice channel N's stream is asked for, -1 for none.
    int requestedSlice(int channel) const;
    /// The channel stored on this computer for a slice (0 when none).
    int storedChannel(int sliceId) const;

    /// Recomputes what each channel asks for. Runs on every assignment
    /// change and on a timer (the reader check).
    void refresh();

signals:
    /// A channel's stream stopped for `reason` (show it through
    /// OperatorReasonText::forDisplay).
    void notice(int channel, const QString& reason);

private:
    void storeChannel(int sliceId, int channel);
    void adoptSlice(int sliceId);
    void releaseChannel(int index);

    QPointer<RadioModel> m_model;
    AudioEngine* m_engine{nullptr};
    QString m_coreKey;
    ReceiverAudio m_source;
    bool m_startWorkers{true};
    std::array<std::unique_ptr<RemoteVaxFeeder>, kChannels> m_feeders;
    std::array<int, kChannels> m_requested{{-1, -1, -1, -1}};
    std::array<QString, kChannels> m_noticed;
    QTimer* m_readerTimer{nullptr};
};

} // namespace NereusSDR
