#pragma once
// no-port-check: NereusSDR-original. Observational Core/GUI telemetry adapter.
#include "gui/TelemetryHistory.h"
#include "core/session/StationTelemetry.h"
#include "core/session/SessionTransport.h"
#include "core/session/media/RemoteAudioReceiver.h"
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <functional>

namespace NereusSDR {
class StationClient;
class RemoteMediaController;

struct RemoteTelemetryView {
    enum class State { Disconnected, Unsupported, Waiting, Current, Stale };
    State state = State::Disconnected;
    std::optional<qint64> stationAgeMs;
    StationRadioTelemetry radio;
    StationAudioTelemetry coreAudio;
    std::optional<double> controlRxKbps, controlTxKbps;
    std::optional<quint64> coreRttMs;
    std::optional<qint64> coreRttAgeMs;
    RemoteAudioReceiverTelemetry playback;
    bool playbackActive = false;
};

// All methods run on the GUI thread. Collection continues while the dialog is
// closed; no spectrum callbacks, settings writes or connection policy live here.
class RemoteTelemetryController final : public QObject {
    Q_OBJECT
public:
    using Clock = std::function<qint64()>;
    using PlaybackObserver = std::function<RemoteAudioReceiverTelemetry()>;
    RemoteTelemetryController(StationClient* client, RemoteMediaController* media,
                              QObject* parent = nullptr,
                              Clock clock = {}, PlaybackObserver playback = {});
    const RemoteTelemetryView& current() const { return m_view; }
    const TelemetryHistory& history() const { return m_history; }
    qint64 nowMs() const;
    QString bannerText() const;
    QString detailText() const;
    // Also used by deterministic lifecycle tests with a monotonic clock.
    void sampleNow();
signals:
    void changed();
private:
    void receiveStation(const StationTelemetrySnapshot& sample, quint32 epoch);
    void clearSession();
    void refreshCurrent(qint64 now);
    QPointer<StationClient> m_client;
    QPointer<RemoteMediaController> m_media;
    QElapsedTimer m_clock;
    Clock m_now;
    PlaybackObserver m_playback;
    QTimer m_timer;
    TelemetryHistory m_history;
    RemoteTelemetryView m_view;
    std::optional<StationTelemetrySnapshot> m_station;
    qint64 m_stationReceivedMs = 0;
    quint32 m_epoch = 0;
    bool m_stationWasStale = false;
    std::optional<SessionTransportTelemetry> m_transportBaseline;
    std::optional<RemoteAudioReceiverTelemetry> m_playbackBaseline;
    struct PlaybackEvents {
        quint64 underflows = 0, overflows = 0;
        qint64 sampledMs = 0;
    };
    std::optional<PlaybackEvents> m_playbackEventsBaseline;
    qint64 m_lastTickMs = -1;
};
} // namespace NereusSDR
