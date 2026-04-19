#pragma once

// =================================================================
// src/core/AudioEngine.h  (NereusSDR)
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

#include "AudioDeviceConfig.h"
#include "IAudioBus.h"
#include "audio/MasterMixer.h"

#include <QObject>
#include <QString>

#include <array>
#include <atomic>
#include <memory>

namespace NereusSDR {

class RadioModel;
class SliceModel;

// Audio engine for NereusSDR (Phase 3O VAX).
//
// Owns one IAudioBus per routable endpoint:
//   - m_speakersBus: the master mix goes here.
//   - m_txInputBus:  TX mic capture source (pull() wiring lands in 3M).
//   - m_vaxBus[0..3]: the four VAX slots.
//
// rxBlockReady(sliceId, samples, frames) is the single RX-audio entry
// point. Called from the DSP worker thread (RxDspWorker) with one
// interleaved stereo block per WDSP fexchange2 drain. Accumulates into
// MasterMixer, taps the slice's VAX channel if selected, and flushes the
// mixed master to m_speakersBus synchronously on the DSP thread. No
// QTimer, no QAudioSink, no m_rxBuffer, no mutex — RT-safety rests on
// PortAudioBus's lock-free SPSC ring.
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Non-owning back-pointer so rxBlockReady can look up the active
    // SliceModel to read mute / VAX-channel state. Null is safe (unit
    // tests that construct AudioEngine without a RadioModel): rxBlockReady
    // becomes a no-op.
    void setRadioModel(RadioModel* radio);

    // Legacy start/stop retained for the single-entry-point symmetry the
    // rest of the codebase expects. Speakers bus is opened lazily the
    // first time setSpeakersConfig() runs.
    void start();
    void stop();
    bool isRunning() const { return m_running; }

    // Phase 3O: per-endpoint IAudioBus ownership.
    //
    // Live-reconfig contract: each of these setters destroys and
    // reconstructs the underlying IAudioBus. That is NOT safe while the
    // DSP thread is inside rxBlockReady(). Caller must ensure one of:
    //   - AudioEngine::isRunning() == false (before start() or after
    //     stop()), or
    //   - the DSP thread feeding rxBlockReady() is quiesced for the
    //     duration of the call.
    // Live device switching while audio is streaming is the
    // responsibility of a higher layer (Phase 3O Sub-Phase 8, Setup →
    // Audio → Devices) and is not provided by this class.
    void setSpeakersConfig(const AudioDeviceConfig& cfg);
    void setTxInputConfig(const AudioDeviceConfig& cfg);
    void setVaxConfig(int channel, const AudioDeviceConfig& cfg);  // 1..4
    void setVaxEnabled(int channel, bool on);

    // Called by RxDspWorker when a slice produces an RX audio block.
    // samples is interleaved stereo float32, length = frames * 2.
    void rxBlockReady(int sliceId, const float* samples, int frames);

    // Master volume (0.0–1.0). Read on the DSP thread, written on the
    // main thread. Preserves the existing AF-gain wiring in
    // RadioModel::wireSliceSignals.
    void setVolume(float volume);
    float volume() const { return m_masterVolume.load(std::memory_order_acquire); }

signals:
    void volumeChanged(float volume);

private:
    // Translate AudioDeviceConfig → AudioFormat + PortAudioConfig and
    // open the given bus slot. Direct PortAudioBus construction today;
    // TODO(phase3o-5/6): swap for AudioBusFactory::create() once
    // CoreAudioHalBus and LinuxPipeBus land.
    std::unique_ptr<IAudioBus> makeBus(const AudioDeviceConfig& cfg,
                                       bool capture);

    // Open m_speakersBus with a sensible platform default if nothing
    // has been wired by the time start() runs. Keeps the live RX path
    // audible without a Setup→Audio→Devices UI in Sub-Phase 4.
    void ensureSpeakersOpen();

    RadioModel* m_radio{nullptr};

    std::unique_ptr<IAudioBus> m_speakersBus;
    std::unique_ptr<IAudioBus> m_txInputBus;
    std::array<std::unique_ptr<IAudioBus>, 4> m_vaxBus;
    MasterMixer m_masterMix;

    // Speakers format last negotiated. frames passed to rxBlockReady may
    // vary per block; the bus handles that internally via its ring. Kept
    // for diagnostics.
    AudioFormat m_speakersFormat;

    // Written by setVolume() on the UI thread, read by rxBlockReady() on
    // the DSP thread. Atomic for the cross-thread handshake per
    // CLAUDE.md C++ style guide.
    std::atomic<float> m_masterVolume{0.5f};

    // Was Pa_Initialize() actually successful? Guards the matching
    // Pa_Terminate() in the destructor so unit tests that construct
    // AudioEngine without a real audio subsystem don't hit a spurious
    // terminate.
    bool m_paInitialized{false};
    bool m_running{false};
    // Was sliceId 0 registered with MasterMixer? startup-only invariant
    // per design-decision D6 (plan); prevents a main-thread insert/rehash
    // race against the audio thread's lock-free find().
    bool m_slicePreregistered{false};
};

} // namespace NereusSDR
