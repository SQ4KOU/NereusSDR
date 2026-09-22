// no-port-check: NereusSDR-original. Observational Core/GUI telemetry adapter.
#include "RemoteTelemetryController.h"
#include "gui/RemoteMediaController.h"
#include "core/session/StationClient.h"
#include <QStringList>

namespace NereusSDR {
namespace {
using Metric = TelemetryHistory::Metric;
constexpr qint64 kStationFreshMs = 3000;
// Session heartbeat pings are 20 seconds apart. A metrics tick is not a pong.
constexpr qint64 kRttFreshMs = 60000;
std::size_t index(Metric metric) { return static_cast<std::size_t>(metric); }
TelemetryHistory::MetricMask mask(Metric first, Metric last)
{
    TelemetryHistory::MetricMask result;
    for (auto i = index(first); i <= index(last); ++i) { result.set(i); }
    return result;
}
void breakRange(TelemetryHistory& history, Metric first, Metric last)
{
    for (auto i = index(first); i <= index(last); ++i) {
        history.breakMetric(static_cast<Metric>(i));
    }
}
std::optional<double> rate(quint64 current, quint64 previous, qint64 elapsed)
{
    if (elapsed <= 0 || current < previous) { return std::nullopt; }
    return double(current - previous) * 1000.0 / double(elapsed);
}
QString number(std::optional<double> value, int decimals = 1)
{
    return value ? QString::number(*value, 'f', decimals) : QStringLiteral("—");
}
}

RemoteTelemetryController::RemoteTelemetryController(
    StationClient* client, RemoteMediaController* media, QObject* parent,
    Clock clock, PlaybackObserver playback)
    : QObject(parent), m_client(client), m_media(media),
      m_now(std::move(clock)), m_playback(std::move(playback))
{
    m_clock.start();
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &RemoteTelemetryController::sampleNow);
    if (client) {
        connect(client, &StationClient::telemetryReceived,
                this, &RemoteTelemetryController::receiveStation);
        connect(client, &StationClient::telemetrySessionEnded, this, [this](quint32 epoch) {
            if (epoch == m_epoch) { clearSession(); }
        });
        connect(client, &StationClient::handshakeComplete, this, [this] {
            clearSession();
            sampleNow();
        });
        connect(client, &StationClient::connectionActivityChanged, this, [this] {
            if (!m_client || !m_client->isHandshakeComplete()) { clearSession(); }
        });
    }
    sampleNow();
    m_timer.start();
}

qint64 RemoteTelemetryController::nowMs() const
{
    return m_now ? m_now() : m_clock.elapsed();
}

void RemoteTelemetryController::clearSession()
{
    breakRange(m_history, Metric::RadioRxMbps, Metric::PlaybackPacketAgeMs);
    m_station.reset();
    m_transportBaseline.reset();
    m_playbackBaseline.reset();
    m_playbackEventsBaseline.reset();
    m_lastTickMs = -1;
    m_epoch = 0;
    m_stationWasStale = false;
    m_view = {};
    emit changed();
}

void RemoteTelemetryController::receiveStation(
    const StationTelemetrySnapshot& sample, quint32 epoch)
{
    if (!m_client || !m_client->telemetryAvailable()
        || epoch != m_client->sessionEpoch()) { return; }
    if (m_epoch != epoch) { clearSession(); m_epoch = epoch; }
    if (m_station && (sample.sequence <= m_station->sequence
                     || sample.sampledElapsedMs < m_station->sampledElapsedMs)) { return; }
    if (m_station && sample.audio.contextGeneration != m_station->audio.contextGeneration) {
        breakRange(m_history, Metric::AudioSourceFramesPerSecond, Metric::AudioSourceDropsPerSecond);
    }
    m_station = sample;
    m_stationReceivedMs = nowMs();
    m_stationWasStale = false;
    TelemetryHistory::Sample observation{m_stationReceivedMs, epoch, {}};
    auto& values = observation.values;
    values[index(Metric::RadioRxMbps)] = sample.radio.rxMbps;
    values[index(Metric::RadioTxMbps)] = sample.radio.txMbps;
    if (sample.radio.rttMs && sample.radio.rttAgeMs && *sample.radio.rttAgeMs <= kRttFreshMs) {
        values[index(Metric::RadioRttMs)] = double(*sample.radio.rttMs);
    }
    values[index(Metric::AudioSourceFramesPerSecond)] = sample.audio.sourceFramesPerSecond;
    values[index(Metric::AudioEncodedPacketsPerSecond)] = sample.audio.encodedPacketsPerSecond;
    values[index(Metric::AudioSendAcceptedPerSecond)] = sample.audio.sendAcceptedPerSecond;
    values[index(Metric::AudioSendRejectedPerSecond)] = sample.audio.sendRejectedPerSecond;
    values[index(Metric::AudioSourceDropsPerSecond)] = sample.audio.sourceDropsPerSecond;
    m_history.append(observation, mask(Metric::RadioRxMbps, Metric::RadioRttMs)
        | mask(Metric::AudioSourceFramesPerSecond, Metric::AudioSourceDropsPerSecond));
    refreshCurrent(m_stationReceivedMs);
    emit changed();
}

void RemoteTelemetryController::refreshCurrent(qint64 now)
{
    m_view.stationAgeMs.reset();
    m_view.radio = {};
    m_view.coreAudio = {};
    if (!m_client || !m_client->isHandshakeComplete()) {
        m_view = {};
        return;
    }
    if (!m_client->telemetryAvailable()) { m_view.state = RemoteTelemetryView::State::Unsupported; }
    else if (!m_station) { m_view.state = RemoteTelemetryView::State::Waiting; }
    else {
        const qint64 age = qMax<qint64>(0, now - m_stationReceivedMs);
        m_view.stationAgeMs = age;
        if (age > kStationFreshMs) {
            m_view.state = RemoteTelemetryView::State::Stale;
            if (!m_stationWasStale) {
                breakRange(m_history, Metric::RadioRxMbps, Metric::RadioRttMs);
                breakRange(m_history, Metric::AudioSourceFramesPerSecond, Metric::AudioSourceDropsPerSecond);
                m_stationWasStale = true;
            }
        } else {
            m_view.state = RemoteTelemetryView::State::Current;
            m_view.radio = m_station->radio;
            m_view.coreAudio = m_station->audio;
            if (m_view.radio.rttAgeMs) {
                *m_view.radio.rttAgeMs += age;
                if (*m_view.radio.rttAgeMs > kRttFreshMs) {
                    m_view.radio.rttAgeMs.reset(); m_view.radio.rttMs.reset();
                }
            }
        }
    }
}

void RemoteTelemetryController::sampleNow()
{
    const qint64 now = nowMs();
    if (!m_client || !m_client->isHandshakeComplete()) {
        if (m_epoch || m_view.state != RemoteTelemetryView::State::Disconnected) { clearSession(); }
        // Prune retained history even during an extended disconnection.
        m_history.append({now, 0, {}});
        return;
    }
    if (m_epoch != m_client->sessionEpoch()) { clearSession(); m_epoch = m_client->sessionEpoch(); }
    const qint64 elapsed = m_lastTickMs < 0 ? 0 : now - m_lastTickMs;
    TelemetryHistory::Sample observation{now, m_epoch, {}};
    auto& values = observation.values;
    m_view.controlRxKbps.reset(); m_view.controlTxKbps.reset();
    m_view.coreRttMs.reset(); m_view.coreRttAgeMs.reset();
    const auto transport = m_client->transportTelemetry();
    if (transport) {
        if (m_transportBaseline && elapsed > 0) {
            auto rx = rate(transport->receivedPayloadBytes, m_transportBaseline->receivedPayloadBytes, elapsed);
            auto tx = rate(transport->acceptedPayloadBytes, m_transportBaseline->acceptedPayloadBytes, elapsed);
            if (rx) { m_view.controlRxKbps = *rx * 8.0 / 1000.0; }
            if (tx) { m_view.controlTxKbps = *tx * 8.0 / 1000.0; }
        }
        if (transport->pongRttMs && transport->pongAgeMs && *transport->pongAgeMs <= kRttFreshMs) {
            m_view.coreRttMs = transport->pongRttMs;
            m_view.coreRttAgeMs = transport->pongAgeMs;
            values[index(Metric::SessionRttMs)] = double(*transport->pongRttMs);
        }
    }
    m_transportBaseline = transport;
    values[index(Metric::SessionPayloadRxKbps)] = m_view.controlRxKbps;
    values[index(Metric::SessionPayloadTxKbps)] = m_view.controlTxKbps;

    const auto playback = m_playback ? m_playback()
        : m_media ? m_media->audioTelemetry() : RemoteAudioReceiverTelemetry{};
    m_view.playback = playback;
    m_view.playbackActive = playback.running && playback.decodedPackets > 0
        && playback.lastDeviceProgressAgeMs && *playback.lastDeviceProgressAgeMs < 500;
    if (m_playbackBaseline && playback.generation != m_playbackBaseline->generation) {
        breakRange(m_history, Metric::PlaybackDecodedPacketsPerSecond, Metric::PlaybackPacketAgeMs);
    }
    if (playback.running && m_playbackBaseline && m_playbackBaseline->running
        && playback.generation == m_playbackBaseline->generation && elapsed > 0) {
        const auto& previous = *m_playbackBaseline;
        values[index(Metric::PlaybackDecodedPacketsPerSecond)] = rate(playback.decodedPackets, previous.decodedPackets, elapsed);
        values[index(Metric::PlaybackConcealedPacketsPerSecond)] = rate(playback.concealedPackets, previous.concealedPackets, elapsed);
        values[index(Metric::PlaybackLatePacketsPerSecond)] = rate(playback.latePackets, previous.latePackets, elapsed);
    }
    // A restart-causing interruption can happen entirely between timer ticks.
    // Lifetime event totals survive receiver contexts, including stopped ones;
    // an unavailable transition snapshot must not erase their previous baseline.
    if (playback.lifetimeUnderflows && playback.lifetimeOverflows) {
        const PlaybackEvents events{*playback.lifetimeUnderflows,
                                    *playback.lifetimeOverflows, now};
        if (m_playbackEventsBaseline) {
            const auto& previous = *m_playbackEventsBaseline;
            const auto underflows = rate(events.underflows, previous.underflows, now - previous.sampledMs);
            const auto overflows = rate(events.overflows, previous.overflows, now - previous.sampledMs);
            // A stable stopped context has no ongoing playback event rate.
            // Preserve a final nonzero event even when it stopped the receiver.
            if (playback.running || (underflows && *underflows > 0) || (overflows && *overflows > 0)) {
                values[index(Metric::PlaybackUnderflowsPerSecond)] = underflows;
                values[index(Metric::PlaybackOverflowsPerSecond)] = overflows;
            }
        }
        m_playbackEventsBaseline = events;
    }
    if (playback.running && playback.lastAdmittedPacketAgeMs) {
        values[index(Metric::PlaybackPacketAgeMs)] = double(*playback.lastAdmittedPacketAgeMs);
    }
    m_playbackBaseline = playback;
    m_lastTickMs = now;
    m_history.append(observation, mask(Metric::SessionPayloadRxKbps, Metric::SessionRttMs)
        | mask(Metric::PlaybackDecodedPacketsPerSecond, Metric::PlaybackPacketAgeMs));
    refreshCurrent(now);
    emit changed();
}

QString RemoteTelemetryController::bannerText() const
{
    QStringList parts;
    switch (m_view.state) {
    case RemoteTelemetryView::State::Disconnected: return {};
    case RemoteTelemetryView::State::Unsupported: parts << tr("telemetry unsupported"); break;
    case RemoteTelemetryView::State::Waiting: parts << tr("waiting for telemetry"); break;
    case RemoteTelemetryView::State::Stale: parts << tr("telemetry stale"); break;
    case RemoteTelemetryView::State::Current:
        parts << (m_view.radio.connected
            ? tr("Radio ↓%1 ↑%2 Mbps").arg(number(m_view.radio.rxMbps), number(m_view.radio.txMbps))
            : tr("Radio offline"));
        break;
    }
    parts << tr("Core %1 ms").arg(m_view.coreRttMs ? QString::number(*m_view.coreRttMs) : QStringLiteral("—"));
    parts << (m_view.playbackActive ? tr("Audio playing")
        : m_view.playback.running ? tr("Audio waiting") : tr("Audio stopped"));
    return parts.join(QStringLiteral("  ·  "));
}

QString RemoteTelemetryController::detailText() const
{
    if (m_view.state == RemoteTelemetryView::State::Disconnected) { return tr("Current telemetry unavailable while disconnected."); }
    QStringList text{bannerText()};
    if (m_view.stationAgeMs) { text << tr("Core measurements received %1 ms ago.").arg(*m_view.stationAgeMs); }
    text << tr("Radio rates: Core ↔ radio, in Mbps. Control payload: GUI ↔ Core, excluding media and transport overhead.");
    text << tr("Control payload RX %1 / TX %2 kbit/s").arg(number(m_view.controlRxKbps), number(m_view.controlTxKbps));
    text << (m_view.coreRttAgeMs ? tr("Core RTT: WebSocket round trip, measured %1 ms ago.").arg(*m_view.coreRttAgeMs)
        : tr("Core RTT: no recent pong measurement."));
    text << (m_view.radio.rttMs && m_view.radio.rttAgeMs
        ? tr("Radio RTT: %1 ms, measured %2 ms ago.").arg(*m_view.radio.rttMs).arg(*m_view.radio.rttAgeMs)
        : tr("Radio RTT: unavailable."));
    text << tr("RTT graphs hold the last measurement between pings; age advances independently and stale values disappear.");
    text << tr("Core audio: %1 frames/s; encoded %2, transport accepted %3, refused %4 packets/s; source drops %5 events/s.")
        .arg(number(m_view.coreAudio.sourceFramesPerSecond, 0), number(m_view.coreAudio.encodedPacketsPerSecond),
             number(m_view.coreAudio.sendAcceptedPerSecond), number(m_view.coreAudio.sendRejectedPerSecond),
             number(m_view.coreAudio.sourceDropsPerSecond));
    const auto& p = m_view.playback;
    text << tr("Playback context %1: admitted %2, decoded %3, concealed %4, late %5, invalid %6, duplicate %7, rejected headers %8.")
        .arg(p.generation).arg(p.acceptedPackets).arg(p.decodedPackets).arg(p.concealedPackets)
        .arg(p.latePackets).arg(p.invalidPackets).arg(p.duplicatePackets).arg(p.rejectedHeaders);
    text << tr("Playback underflows %1 / overflows %2; device consumed %3 frames; last admitted packet %4 ms ago.")
        .arg(p.underflows).arg(p.overflows).arg(p.deviceConsumedFrames)
        .arg(p.lastAdmittedPacketAgeMs ? QString::number(*p.lastAdmittedPacketAgeMs) : QStringLiteral("—"));
    if (p.lifetimeUnderflows && p.lifetimeOverflows) {
        text << tr("Playback interruptions this GUI run: %1 underflows / %2 overflows, including retired audio contexts.")
            .arg(*p.lifetimeUnderflows).arg(*p.lifetimeOverflows);
    }
    text << tr("Transport acceptance does not prove delivery. Concealment and source drops are events, not a packet-loss percentage.");
    return text.join(QLatin1Char('\n'));
}
} // namespace NereusSDR
