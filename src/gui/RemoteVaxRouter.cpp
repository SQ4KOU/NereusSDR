// =================================================================
// src/gui/RemoteVaxRouter.cpp  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. R-R3-44; see RemoteVaxRouter.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-23: Written for NereusSDR by J.J. Boyd (KG4VCF), with
//                 AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "gui/RemoteVaxRouter.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "core/LogCategories.h"
#include "core/audio/RemoteVaxFeeder.h"
#include "core/session/RemoteStationOptions.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QCryptographicHash>
#include <QTimer>
#include <QUrl>

#include <optional>

namespace NereusSDR {

namespace {

// The receiver stop the link itself reports while it is not up; the
// window already shows the link's state, so it is never raised here.
constexpr auto kMediaNotReady = "media-not-ready";

} // namespace

RemoteVaxRouter::RemoteVaxRouter(RadioModel* model, AudioEngine* engine, QString coreKey,
                                 QObject* parent)
    : RemoteVaxRouter(model, engine, std::move(coreKey),
                      [engine](int channel) {
                          return std::make_unique<RemoteVaxFeeder>(
                              channel, VaxOutputPort::forEngine(engine, channel));
                      },
                      /*startWorkers=*/true, parent)
{
}

RemoteVaxRouter::RemoteVaxRouter(RadioModel* model, AudioEngine* engine, QString coreKey,
                                 FeederFactory factory, bool startWorkers, QObject* parent)
    : QObject(parent)
    , m_model(model)
    , m_engine(engine)
    , m_coreKey(std::move(coreKey))
    , m_startWorkers(startWorkers)
{
    for (int i = 0; i < kChannels; ++i) {
        m_feeders[static_cast<std::size_t>(i)] = factory ? factory(i + 1) : nullptr;
    }
    if (m_model) {
        // Every slice of the remote model keeps its channel here, not in
        // the Core's Slice<N>/VaxChannel.
        m_model->setRemoteVaxChannelStore([this](int sliceId, int channel) {
            storeChannel(sliceId, channel);
        });
        connect(m_model, &RadioModel::sliceAdded, this, [this](int sliceId) {
            adoptSlice(sliceId);
            refresh();
        });
        connect(m_model, &RadioModel::sliceRemoved, this, [this](int) { refresh(); });
        for (SliceModel* slice : m_model->slices()) {
            if (slice) { adoptSlice(slice->sliceIndex()); }
        }
    }
    m_readerTimer = new QTimer(this);
    m_readerTimer->setInterval(kReaderPollMs);
    connect(m_readerTimer, &QTimer::timeout, this, &RemoteVaxRouter::refresh);
    m_readerTimer->start();
}

RemoteVaxRouter::~RemoteVaxRouter()
{
    if (m_model) {
        m_model->setRemoteVaxChannelStore({});
    }
    for (int i = 0; i < kChannels; ++i) {
        releaseChannel(i);
    }
    // The feeders' workers stop as they are destroyed.
}

QString RemoteVaxRouter::coreKeyFor(const RemoteStationOptions& station)
{
    QString identity;
    if (!station.fingerprint.isEmpty()) {
        identity = QStringLiteral("fingerprint:") + station.fingerprint.toUpper()
            .remove(QLatin1Char(':'));
    } else {
        const QUrl url(station.url);
        identity = QStringLiteral("address:") + url.host().toLower() + QLatin1Char(':')
            + QString::number(url.port());
    }
    // Hashed so neither the address nor the fingerprint is spelled out in
    // the settings file's key names.
    return QString::fromLatin1(
        QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256)
            .toHex().left(16));
}

QString RemoteVaxRouter::settingsKey(const QString& coreKey, int sliceId)
{
    return QStringLiteral("RemoteVax/%1/Slice%2/Channel").arg(coreKey).arg(sliceId);
}

void RemoteVaxRouter::setReceiverAudio(ReceiverAudio source)
{
    for (int i = 0; i < kChannels; ++i) {
        releaseChannel(i);
    }
    m_source = std::move(source);
    refresh();
}

RemoteVaxFeeder* RemoteVaxRouter::feeder(int channel) const
{
    if (channel < 1 || channel > kChannels) { return nullptr; }
    return m_feeders[static_cast<std::size_t>(channel - 1)].get();
}

int RemoteVaxRouter::requestedSlice(int channel) const
{
    if (channel < 1 || channel > kChannels) { return -1; }
    return m_requested[static_cast<std::size_t>(channel - 1)];
}

int RemoteVaxRouter::storedChannel(int sliceId) const
{
    const int channel = AppSettings::instance()
        .value(settingsKey(m_coreKey, sliceId), QStringLiteral("0")).toString().toInt();
    return (channel >= 1 && channel <= kChannels) ? channel : 0;
}

void RemoteVaxRouter::storeChannel(int sliceId, int channel)
{
    auto& settings = AppSettings::instance();
    settings.setValue(settingsKey(m_coreKey, sliceId), QString::number(channel));
    settings.save();
    refresh();
}

void RemoteVaxRouter::adoptSlice(int sliceId)
{
    if (!m_model) { return; }
    SliceModel* slice = m_model->sliceById(sliceId);
    if (!slice) { return; }
    connect(slice, &SliceModel::vaxChannelChanged, this, &RemoteVaxRouter::refresh,
            Qt::UniqueConnection);
    // Restore the channel this computer kept for the Core's slice. The
    // store writes the same value back, which changes nothing.
    slice->setVaxChannel(storedChannel(sliceId));
}

void RemoteVaxRouter::releaseChannel(int index)
{
    const auto i = static_cast<std::size_t>(index);
    RemoteVaxFeeder* feeder = m_feeders[i].get();
    const int slice = m_requested[i];
    m_requested[i] = -1;
    if (!feeder) { return; }
    if (slice >= 0 && m_source.release) {
        // After this returns the feeder is not called for that slice again.
        m_source.release(slice, feeder);
    }
    feeder->stopWorker();
    feeder->setSourceSlice(-1);
    m_noticed[i].clear();
}

void RemoteVaxRouter::refresh()
{
    for (int index = 0; index < kChannels; ++index) {
        const auto i = static_cast<std::size_t>(index);
        const int channel = index + 1;
        RemoteVaxFeeder* feeder = m_feeders[i].get();
        if (!feeder) { continue; }

        int wanted = -1;
        if (m_model) {
            for (SliceModel* slice : m_model->slices()) {
                if (slice && slice->vaxChannel() == channel
                    && (wanted < 0 || slice->sliceIndex() < wanted)) {
                    wanted = slice->sliceIndex();
                }
            }
        }
        if (wanted >= 0) {
            const bool outputOpen = m_engine && m_engine->isVaxBusOpen(channel);
            // Only a platform that reports readers can say there is none.
            const std::optional<bool> reader =
                m_engine ? m_engine->vaxOutputHasReader(channel) : std::nullopt;
            if (!outputOpen || reader == std::optional<bool>(false) || !m_source.request) {
                wanted = -1;
            }
        }

        if (wanted != m_requested[i]) {
            releaseChannel(index);
            if (wanted >= 0) {
                qCInfo(lcAudio) << "Remote VAX" << channel << "carries slice" << wanted;
                feeder->setSourceSlice(wanted);
                m_requested[i] = wanted;
                if (m_startWorkers) { feeder->startWorker(); }
                m_source.request(wanted, feeder);
            } else {
                qCInfo(lcAudio) << "Remote VAX" << channel << "carries no slice";
            }
        }

        if (m_requested[i] >= 0) {
            const QString reason = feeder->lastStopReason();
            if (reason != m_noticed[i]) {
                m_noticed[i] = reason;
                if (!reason.isEmpty() && reason != QLatin1String(kMediaNotReady)) {
                    emit notice(channel, reason);
                }
            }
        }
    }
}

} // namespace NereusSDR
