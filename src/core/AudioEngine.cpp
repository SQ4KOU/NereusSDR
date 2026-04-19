// =================================================================
// src/core/AudioEngine.cpp  (NereusSDR)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   NereusSDR is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 QAudioSink feed-and-drain pattern ported from AetherSDR
//                 `src/core/AudioEngine.{h,cpp}` (48 kHz Int16 stereo,
//                 10 ms timer drain, 200 ms buffer cap).
//   2026-04-19 — Sub-Phase 4 refactor by J.J. Boyd (KG4VCF), AI-assisted
//                 via Anthropic Claude Code. Replaced QAudioSink direct-drain
//                 model with IAudioBus-based speakers / TX-input / VAX[1..4]
//                 ownership + MasterMixer per-slice accumulation. Inline
//                 synchronous flush on the DSP thread; no QTimer, no
//                 QAudioSink. (docs/architecture/2026-04-19-phase3o-vax-plan.md
//                 §Sub-Phase 4 Task 4.1.)
// =================================================================

#include "AudioEngine.h"

#include "LogCategories.h"
#include "audio/PortAudioBus.h"
#include "../models/RadioModel.h"
#include "../models/SliceModel.h"

#include <portaudio.h>

#include <algorithm>
#include <vector>

namespace NereusSDR {

namespace {

// Translate a NereusSDR AudioDeviceConfig into the IAudioBus AudioFormat
// contract. Float32 stereo is the canonical DSP format; channels is kept
// cfg-overridable but the single live path is stereo today.
AudioFormat toAudioFormat(const AudioDeviceConfig& cfg)
{
    AudioFormat f;
    f.sampleRate = cfg.sampleRate;
    f.channels   = cfg.channels;
    f.sample     = AudioFormat::Sample::Float32;
    return f;
}

} // namespace

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
    // Own the Pa_Initialize/Pa_Terminate pair so the static PortAudioBus
    // enumeration helpers (hostApis / outputDevicesFor / inputDevicesFor)
    // can be called at any time from the application. Tests that run
    // without a real audio subsystem will see Pa_Initialize fail; log and
    // continue — rxBlockReady / start() degrade to a safe no-op in that
    // case.
    const PaError err = Pa_Initialize();
    if (err != paNoError) {
        qCWarning(lcAudio) << "Pa_Initialize failed:" << Pa_GetErrorText(err)
                           << "— audio subsystem will be inert.";
        m_paInitialized = false;
    } else {
        m_paInitialized = true;
        qCInfo(lcAudio) << "PortAudio initialized:" << Pa_GetVersionText();
    }
}

AudioEngine::~AudioEngine()
{
    stop();
    if (m_paInitialized) {
        Pa_Terminate();
        m_paInitialized = false;
    }
}

void AudioEngine::setRadioModel(RadioModel* radio)
{
    m_radio = radio;
}

void AudioEngine::start()
{
    if (m_running) {
        return;
    }

    // Pre-register sliceId 0 with MasterMixer at startup, before any
    // audio-thread accumulate() can race against a main-thread
    // unordered_map insert+rehash on first-block (design-decision D6,
    // plan §Sub-Phase 4 Task 4.1). Multi-slice enrollment is a
    // Sub-Phase 9+ concern; slice-0 covers the single-RX live path.
    if (!m_slicePreregistered) {
        m_masterMix.setSliceGain(0, 1.0f, 0.0f);
        m_slicePreregistered = true;
    }

    ensureSpeakersOpen();
    m_running = true;

    qCInfo(lcAudio) << "AudioEngine started ("
                    << (m_speakersBus && m_speakersBus->isOpen()
                            ? "speakers bus open"
                            : "speakers bus NOT open")
                    << ")";
}

void AudioEngine::stop()
{
    if (!m_running) {
        return;
    }

    // Close every owned bus. unique_ptr::reset() invokes the bus dtor
    // which calls Pa_CloseStream where applicable. Safe on the main
    // thread because rxBlockReady() cannot run after m_running = false
    // — but note that rxBlockReady itself reads m_speakersBus directly;
    // the ordering contract is that the caller (RadioModel teardown)
    // stops the DSP worker thread before calling stop(). That contract
    // is preserved from the pre-refactor code.
    m_speakersBus.reset();
    m_txInputBus.reset();
    for (auto& bus : m_vaxBus) {
        bus.reset();
    }

    m_running = false;
    qCInfo(lcAudio) << "AudioEngine stopped";
}

std::unique_ptr<IAudioBus> AudioEngine::makeBus(const AudioDeviceConfig& cfg,
                                                bool capture)
{
    // TODO(phase3o-5/6): swap direct PortAudioBus construction for
    // AudioBusFactory::create() once CoreAudioHalBus / LinuxPipeBus land.
    auto bus = std::make_unique<PortAudioBus>();
    PortAudioConfig pcfg;
    pcfg.direction     = capture ? AudioDirection::Input
                                 : AudioDirection::Output;
    pcfg.hostApiIndex  = cfg.hostApiIndex;
    pcfg.deviceName    = cfg.deviceName;
    pcfg.bufferSamples = cfg.bufferSamples;
    pcfg.exclusiveMode = cfg.exclusiveMode;
    bus->setConfig(pcfg);

    const AudioFormat fmt = toAudioFormat(cfg);
    if (!bus->open(fmt)) {
        qCWarning(lcAudio) << "IAudioBus open failed:" << bus->errorString();
        return nullptr;
    }
    return bus;
}

void AudioEngine::ensureSpeakersOpen()
{
    if (m_speakersBus && m_speakersBus->isOpen()) {
        return;
    }
    if (!m_paInitialized) {
        return;
    }

    AudioDeviceConfig defaults;
    // defaults: empty deviceName (platform default), 48 kHz, stereo,
    // 256-frame buffer — matches what the DSP path produces.
    m_speakersBus = makeBus(defaults, /*capture=*/false);
    if (m_speakersBus) {
        m_speakersFormat = m_speakersBus->negotiatedFormat();
        qCInfo(lcAudio) << "Speakers bus opened @"
                        << m_speakersFormat.sampleRate << "Hz /"
                        << m_speakersFormat.channels << "ch"
                        << "[" << m_speakersBus->backendName() << "]";
    }
}

void AudioEngine::setSpeakersConfig(const AudioDeviceConfig& cfg)
{
    m_speakersBus.reset();
    if (!m_paInitialized) {
        return;
    }
    m_speakersBus = makeBus(cfg, /*capture=*/false);
    if (m_speakersBus) {
        m_speakersFormat = m_speakersBus->negotiatedFormat();
        qCInfo(lcAudio) << "Speakers bus reconfigured @"
                        << m_speakersFormat.sampleRate << "Hz /"
                        << m_speakersFormat.channels << "ch";
    }
}

void AudioEngine::setTxInputConfig(const AudioDeviceConfig& cfg)
{
    // TX pull() wiring lands in Phase 3M. We declare ownership + setter
    // per the Sub-Phase 4 plan so the bus is construction-ready; the bus
    // is inert until TxChannel starts pulling.
    m_txInputBus.reset();
    if (!m_paInitialized) {
        return;
    }
    m_txInputBus = makeBus(cfg, /*capture=*/true);
    if (m_txInputBus) {
        qCInfo(lcAudio) << "TX input bus opened"
                        << "[" << m_txInputBus->backendName() << "]";
    }
}

void AudioEngine::setVaxConfig(int channel, const AudioDeviceConfig& cfg)
{
    if (channel < 1 || channel > 4) {
        return;
    }
    const int idx = channel - 1;
    m_vaxBus[idx].reset();
    if (!m_paInitialized) {
        return;
    }
    m_vaxBus[idx] = makeBus(cfg, /*capture=*/false);
    if (m_vaxBus[idx]) {
        qCInfo(lcAudio) << "VAX" << channel << "bus opened"
                        << "[" << m_vaxBus[idx]->backendName() << "]";
    }
}

void AudioEngine::setVaxEnabled(int channel, bool on)
{
    if (channel < 1 || channel > 4) {
        return;
    }
    const int idx = channel - 1;
    if (!on) {
        m_vaxBus[idx].reset();
        return;
    }
    // Enable with defaults if no explicit setVaxConfig has been wired yet.
    // Real config lands via the VaxApplet / Setup→Audio→VAX UI in
    // Sub-Phase 7 / 8.
    if (!m_vaxBus[idx]) {
        AudioDeviceConfig defaults;
        m_vaxBus[idx] = makeBus(defaults, /*capture=*/false);
    }
}

void AudioEngine::rxBlockReady(int sliceId, const float* samples, int frames)
{
    if (!m_radio || samples == nullptr || frames <= 0) {
        return;
    }

    // SliceModel exposes muted() / setMuted() plus vaxChannel(). The
    // design spec uses audioMuted() as shorthand for the same property;
    // the alias is intentionally not introduced here (design-decision D5,
    // plan §Sub-Phase 4 Task 4.1). A formal rename (if chosen) happens in
    // Sub-Phase 9 alongside per-slice volume / pan control surfaces.
    SliceModel* slice = m_radio->sliceAt(sliceId);
    if (slice == nullptr) {
        return;
    }

    if (!slice->muted()) {
        m_masterMix.accumulate(sliceId, samples, frames);
    }

    // VAX tap receives raw demodulated audio — pre-MasterMixer gain/pan,
    // pre-master-volume — matching Thetis VAC behavior and the spec §3.4
    // pseudocode (`m_vaxBus[vaxCh-1]->push(samples, …)`, not the mixed
    // output). Per-channel rx gain for VAX is applied inside the bus
    // implementation (Sub-Phase 9+). See
    // docs/architecture/2026-04-19-vax-design.md.
    const int vaxCh = slice->vaxChannel();
    if (vaxCh >= 1 && vaxCh <= 4) {
        // Snapshot the bus pointer into a local so the isOpen() check and
        // the push() below observe the same IAudioBus instance. The
        // live-reconfig contract (AudioEngine.h) forbids setVaxConfig /
        // setVaxEnabled mid-block, but the snapshot eliminates any
        // torn-read window should a caller violate that contract.
        IAudioBus* vaxBus = m_vaxBus[vaxCh - 1].get();
        if (vaxBus != nullptr && vaxBus->isOpen()) {
            vaxBus->push(
                reinterpret_cast<const char*>(samples),
                static_cast<qint64>(frames) * 2 * sizeof(float));
        }
    }

    // Flush synchronously on the DSP thread. thread_local scratch so the
    // per-block vector reuse costs zero allocation after the first block
    // per thread. Channel count = 2 is intentionally hard-coded here:
    // the MasterMixer contract and the DSP pipeline both emit stereo.
    static thread_local std::vector<float> mix;
    const int stereoFloats = frames * 2;
    if (static_cast<int>(mix.size()) < stereoFloats) {
        mix.resize(static_cast<size_t>(stereoFloats));
    }
    m_masterMix.mixInto(mix.data(), frames);

    const float vol = m_masterVolume.load(std::memory_order_acquire);
    if (vol != 1.0f) {
        for (int i = 0; i < stereoFloats; ++i) {
            mix[i] *= vol;
        }
    }

    // Same snapshot idiom as the VAX tap: one load into a local so the
    // isOpen() guard and the push() observe the same IAudioBus.
    IAudioBus* speakersBus = m_speakersBus.get();
    if (speakersBus != nullptr && speakersBus->isOpen()) {
        speakersBus->push(
            reinterpret_cast<const char*>(mix.data()),
            static_cast<qint64>(stereoFloats) * sizeof(float));
    }
}

void AudioEngine::setVolume(float volume)
{
    volume = std::clamp(volume, 0.0f, 1.0f);
    // acq_rel pairs with the DSP-thread acquire load in rxBlockReady on
    // weak memory models (ARM / Apple Silicon); release alone would not
    // synchronize the read-side observation order here.
    const float prev = m_masterVolume.exchange(volume, std::memory_order_acq_rel);
    if (prev != volume) {
        emit volumeChanged(volume);
    }
}

} // namespace NereusSDR
