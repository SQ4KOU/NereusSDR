// =================================================================
// src/core/audio/PortAudioBus.cpp  (NereusSDR)
// =================================================================
// See PortAudioBus.h for contract. NereusSDR-original.
// =================================================================

#include "PortAudioBus.h"

#include <portaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace NereusSDR {

PortAudioBus::PortAudioBus() {
    // 1 second stereo float ring at the nominal default rate. The push
    // path wraps modulo ring size, so a longer stream than 1 s simply
    // overwrites the oldest unread samples (underruns surface as silence
    // in paCallback, not overruns).
    m_ring.resize(48000 * 2);
}

PortAudioBus::~PortAudioBus() {
    close();
}

void PortAudioBus::setConfig(const PortAudioConfig& cfg) {
    m_cfg = cfg;
}

bool PortAudioBus::open(const AudioFormat& format) {
    if (m_stream) {
        close();
    }

    PaStreamParameters params;
    PaError err = paNoError;
    const PaDeviceInfo* di = nullptr;

    if (m_cfg.direction == AudioDirection::Output) {
        params.device = Pa_GetDefaultOutputDevice();
        if (params.device == paNoDevice) {
            m_err = QStringLiteral("No default output device");
            return false;
        }
        di = Pa_GetDeviceInfo(params.device);
        if (di == nullptr) {
            m_err = QStringLiteral("Pa_GetDeviceInfo returned null for default device");
            return false;
        }
        params.channelCount              = format.channels;
        params.sampleFormat              = paFloat32;
        params.suggestedLatency          = di->defaultLowOutputLatency;
        params.hostApiSpecificStreamInfo = nullptr;

        err = Pa_OpenStream(
            &m_stream, nullptr, &params,
            format.sampleRate, m_cfg.bufferSamples,
            paClipOff, &PortAudioBus::paCallback, this);
    } else {
        params.device = Pa_GetDefaultInputDevice();
        if (params.device == paNoDevice) {
            m_err = QStringLiteral("No default input device");
            return false;
        }
        di = Pa_GetDeviceInfo(params.device);
        if (di == nullptr) {
            m_err = QStringLiteral("Pa_GetDeviceInfo returned null for default device");
            return false;
        }
        params.channelCount              = format.channels;
        params.sampleFormat              = paFloat32;
        params.suggestedLatency          = di->defaultLowInputLatency;
        params.hostApiSpecificStreamInfo = nullptr;

        err = Pa_OpenStream(
            &m_stream, &params, nullptr,
            format.sampleRate, m_cfg.bufferSamples,
            paClipOff, &PortAudioBus::paCallback, this);
    }

    if (err != paNoError) {
        m_err = QString::fromUtf8(Pa_GetErrorText(err));
        m_stream = nullptr;
        m_negFormat = {};
        m_backendName.clear();
        return false;
    }

    Pa_StartStream(m_stream);
    m_negFormat = format;

    // Defensive null-check on host-API lookup. With a device handed back
    // by Pa_GetDefault{Output,Input}Device this should never be null, but
    // keep the backend name well-defined if it ever is.
    const PaHostApiInfo* hai = Pa_GetHostApiInfo(di->hostApi);
    if (hai != nullptr && hai->name != nullptr) {
        m_backendName = QString::fromUtf8(hai->name);
    } else {
        m_backendName.clear();
    }
    return true;
}

void PortAudioBus::close() {
    if (!m_stream) {
        return;
    }
    Pa_StopStream(m_stream);
    Pa_CloseStream(m_stream);
    m_stream = nullptr;
}

qint64 PortAudioBus::push(const char* data, qint64 bytes) {
    if (!m_stream) { return 0; }
    if (m_cfg.direction != AudioDirection::Output) { return 0; }
    const int floatCount = static_cast<int>(bytes / sizeof(float));
    const qint64 ringSize = static_cast<qint64>(m_ring.size());
    qint64 w = m_ringWrite.load(std::memory_order_relaxed);
    const float* in = reinterpret_cast<const float*>(data);
    float peak = 0.0f;
    for (int i = 0; i < floatCount; ++i) {
        m_ring[w % ringSize] = in[i];
        w++;
        peak = std::max(peak, std::abs(in[i]));
    }
    m_ringWrite.store(w, std::memory_order_release);
    m_rxLevel.store(peak, std::memory_order_release);
    return bytes;
}

qint64 PortAudioBus::pull(char* data, qint64 maxBytes) {
    if (!m_stream) { return 0; }
    if (m_cfg.direction != AudioDirection::Input) { return 0; }
    const int maxFloats = static_cast<int>(maxBytes / sizeof(float));
    const qint64 ringSize = static_cast<qint64>(m_ring.size());
    qint64 r = m_ringRead.load(std::memory_order_relaxed);
    const qint64 w = m_ringWrite.load(std::memory_order_acquire);
    float* dst = reinterpret_cast<float*>(data);
    int count = 0;
    while (count < maxFloats && r < w) {
        dst[count] = m_ring[r % ringSize];
        ++count;
        ++r;
    }
    m_ringRead.store(r, std::memory_order_release);
    return static_cast<qint64>(count) * static_cast<qint64>(sizeof(float));
}

int PortAudioBus::paCallback(const void* in, void* out,
                             unsigned long frames,
                             const PaStreamCallbackTimeInfo* /*timeInfo*/,
                             unsigned long /*flags*/,
                             void* userData) {
    PortAudioBus* self = static_cast<PortAudioBus*>(userData);
    const qint64 ringSize = static_cast<qint64>(self->m_ring.size());

    if (self->m_cfg.direction == AudioDirection::Output) {
        float* o = static_cast<float*>(out);
        const int want = static_cast<int>(frames) * self->m_negFormat.channels;

        qint64 r = self->m_ringRead.load(std::memory_order_relaxed);
        const qint64 w = self->m_ringWrite.load(std::memory_order_acquire);

        for (int i = 0; i < want; ++i) {
            if (r < w) {
                o[i] = self->m_ring[r % ringSize];
                r++;
            } else {
                o[i] = 0.0f;  // underrun -> silence
            }
        }
        self->m_ringRead.store(r, std::memory_order_release);
    } else {
        // Input mode: read captured samples from `in`, write to ring,
        // update m_txLevel (the audio here is destined for transmit).
        const float* i_in = static_cast<const float*>(in);
        const int have = static_cast<int>(frames) * self->m_negFormat.channels;

        qint64 w = self->m_ringWrite.load(std::memory_order_relaxed);
        float peak = 0.0f;
        if (i_in != nullptr) {
            for (int i = 0; i < have; ++i) {
                self->m_ring[w % ringSize] = i_in[i];
                w++;
                peak = std::max(peak, std::abs(i_in[i]));
            }
        }
        self->m_ringWrite.store(w, std::memory_order_release);
        self->m_txLevel.store(peak, std::memory_order_release);
    }
    return paContinue;
}

QVector<PortAudioBus::HostApiInfo> PortAudioBus::hostApis() {
    QVector<HostApiInfo> out;
    const int n = Pa_GetHostApiCount();
    for (int i = 0; i < n; ++i) {
        const PaHostApiInfo* h = Pa_GetHostApiInfo(i);
        if (h) { out.push_back({i, QString::fromUtf8(h->name)}); }
    }
    return out;
}

QVector<PortAudioBus::DeviceInfo> PortAudioBus::outputDevicesFor(int hostApiIndex) {
    QVector<DeviceInfo> out;
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (!d || d->hostApi != hostApiIndex || d->maxOutputChannels <= 0) { continue; }
        // d->defaultSampleRate is double; all PortAudio host APIs report integer rates.
        out.push_back({
            i, QString::fromUtf8(d->name),
            d->maxOutputChannels, d->maxInputChannels,
            static_cast<int>(d->defaultSampleRate), d->hostApi
        });
    }
    return out;
}

QVector<PortAudioBus::DeviceInfo> PortAudioBus::inputDevicesFor(int hostApiIndex) {
    QVector<DeviceInfo> out;
    const int n = Pa_GetDeviceCount();
    for (int i = 0; i < n; ++i) {
        const PaDeviceInfo* d = Pa_GetDeviceInfo(i);
        if (!d || d->hostApi != hostApiIndex || d->maxInputChannels <= 0) { continue; }
        out.push_back({
            i, QString::fromUtf8(d->name),
            d->maxOutputChannels, d->maxInputChannels,
            static_cast<int>(d->defaultSampleRate), d->hostApi
        });
    }
    return out;
}

} // namespace NereusSDR
