// no-port-check: NereusSDR-original.
// =================================================================
// src/core/session/StationVaxFacade.cpp  (NereusSDR)
// =================================================================
//
// See StationVaxFacade.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-09-28: original implementation for NereusSDR by J.J. Boyd
//               (KG4VCF), iPhone app plan Task 25 (R-IOS-18), with
//               AI-assisted implementation via Anthropic Claude Code.
// =================================================================

#include "core/session/StationVaxFacade.h"

#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"

#include <QVariant>

#include <algorithm>
#include <cmath>

namespace NereusSDR {

QString StationVax::rxGainKey(int channel)
{
    // VaxApplet.cpp kRxGainKey.
    return QStringLiteral("audio/Vax%1/RxGain").arg(channel);
}

QString StationVax::mutedKey(int channel)
{
    // VaxApplet.cpp kMutedKey.
    return QStringLiteral("audio/Vax%1/Muted").arg(channel);
}

QString StationVax::txGainKey()
{
    // VaxApplet.cpp kTxGainKey.
    return QStringLiteral("audio/TxGain");
}

QString StationVax::deviceName(int channel)
{
    // What VaxApplet::deviceLabelFor shows for the channel.
#ifdef Q_OS_WIN
    const QString name = AppSettings::instance()
        .value(QStringLiteral("audio/Vax%1/DeviceName").arg(channel))
        .toString();
    return name.isEmpty() ? QStringLiteral("(no device)") : name;
#else
    // macOS CoreAudioHalBus and Linux LinuxPipeBus register the virtual
    // device under this name (AudioEngine::makeVaxBus).
    return QStringLiteral("NereusSDR VAX %1").arg(channel);
#endif
}

bool StationVax::isTransmitProperty(const QByteArray& name)
{
    return name == "txGain";
}

QString StationVax::levelRefusal(const QByteArray& name, const QVariant& value)
{
    if (!name.endsWith("RxGain") && name != "txGain") {
        return {};
    }
    const double level = value.toDouble();
    if (!std::isfinite(level) || level < 0.0 || level > 1.0) {
        return QStringLiteral("A VAX level goes from 0 to 1.");
    }
    return {};
}

StationVax::StationVax(QObject* parent)
    : QObject(parent)
{
}

StationVax::~StationVax() = default;

void StationVax::bind(RadioModel* model, AudioEngine* audio, AppSettings* settings)
{
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
        for (SliceModel* slice : m_model->slices()) {
            if (slice != nullptr) {
                disconnect(slice, nullptr, this, nullptr);
            }
        }
    }
    if (m_audio) {
        disconnect(m_audio, nullptr, this, nullptr);
    }
    m_model = model;
    m_audio = audio;
    m_settings = settings;
    if (audio != nullptr) {
        // A change made anywhere (the station computer's applet, a device)
        // reaches every device.
        connect(audio, &AudioEngine::vaxRxGainChanged, this, [this](int, float) {
            emit gainsChanged();
        });
        connect(audio, &AudioEngine::vaxTxGainChanged, this, [this](float) {
            emit gainsChanged();
        });
        connect(audio, &AudioEngine::vaxMutedChanged, this, [this](int, bool) {
            emit mutesChanged();
        });
    }
    if (model != nullptr) {
        // As VaxApplet::connectSliceTagsTracking follows them.
        connect(model, &RadioModel::sliceAdded, this, [this](int index) {
            if (m_model) {
                watchSlice(m_model->sliceById(index));
            }
            refreshSlices();
        });
        connect(model, &RadioModel::sliceRemoved, this, [this](int) { refreshSlices(); });
        for (SliceModel* slice : model->slices()) {
            watchSlice(slice);
        }
    }
    refreshSlices();
    emit gainsChanged();
    emit mutesChanged();
}

void StationVax::watchSlice(QObject* object)
{
    auto* slice = qobject_cast<SliceModel*>(object);
    if (slice == nullptr) {
        return;
    }
    connect(slice, &SliceModel::vaxChannelChanged, this, [this]() { refreshSlices(); });
    connect(slice, &SliceModel::txSliceChanged, this, [this]() { refreshSlices(); });
}

void StationVax::refreshSlices()
{
    // VaxApplet::updateTagsLabels: each channel's slices, and the transmit
    // slice, by the slice's letter.
    QString slices[kChannels];
    QString tx;
    if (m_model) {
        for (SliceModel* slice : m_model->slices()) {
            if (slice == nullptr) {
                continue;
            }
            const QChar letter = slice->sliceLetter();
            const int channel = slice->vaxChannel();
            if (channel >= 1 && channel <= kChannels) {
                slices[channel - 1].append(letter);
            }
            if (slice->isTxSlice()) {
                tx = QString(letter);
            }
        }
    }
    bool changed = tx != m_txSlice;
    for (int i = 0; i < kChannels; ++i) {
        changed = changed || slices[i] != m_slices[i];
        m_slices[i] = slices[i];
    }
    m_txSlice = tx;
    if (changed) {
        emit slicesChanged();
    }
}

double StationVax::rxGain(int channel) const
{
    return m_audio ? static_cast<double>(m_audio->vaxRxGain(channel)) : 1.0;
}

bool StationVax::muted(int channel) const
{
    return m_audio ? m_audio->vaxMuted(channel) : false;
}

double StationVax::txGain() const
{
    return m_audio ? static_cast<double>(m_audio->vaxTxGain()) : 1.0;
}

void StationVax::save(const QString& key, const QString& value)
{
    if (m_settings == nullptr) {
        return;
    }
    m_settings->setValue(key, value);
    m_settings->save();
}

void StationVax::setRxGain(int channel, double gain)
{
    if (channel < 1 || channel > kChannels || !m_audio) {
        return;
    }
    const float level = std::clamp(static_cast<float>(gain), 0.0f, 1.0f);
    // As the applet's slider does: the engine, then the saved key.
    m_audio->setVaxRxGain(channel, level);
    save(rxGainKey(channel), QString::number(level, 'f', 3));
}

void StationVax::setMuted(int channel, bool on)
{
    if (channel < 1 || channel > kChannels || !m_audio) {
        return;
    }
    // As the applet's Mute button does.
    m_audio->setVaxMuted(channel, on);
    save(mutedKey(channel), on ? QStringLiteral("True") : QStringLiteral("False"));
}

void StationVax::setTxGain(double gain)
{
    if (!m_audio) {
        return;
    }
    const float level = std::clamp(static_cast<float>(gain), 0.0f, 1.0f);
    // As the applet's TX slider does.
    m_audio->setVaxTxGain(level);
    save(txGainKey(), QString::number(level, 'f', 3));
}

} // namespace NereusSDR
