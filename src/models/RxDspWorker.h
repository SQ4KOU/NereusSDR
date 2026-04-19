#pragma once

// =================================================================
// src/models/RxDspWorker.h  (NereusSDR)
// =================================================================
//
// Ported from Thetis source:
//   Project Files/Source/Console/console.cs, original licence from Thetis source is included below
//
// =================================================================
// Modification history (NereusSDR):
//   2026-04-17 — Reimplemented in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), with AI-assisted transformation via Anthropic
//                 Claude Code.
// =================================================================

//=================================================================
// console.cs
//=================================================================
// Thetis is a C# implementation of a Software Defined Radio.
// Copyright (C) 2004-2009  FlexRadio Systems 
// Copyright (C) 2010-2020  Doug Wigley
// Credit is given to Sizenko Alexander of Style-7 (http://www.styleseven.com/) for the Digital-7 font.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//
// You may contact us via email at: sales@flex-radio.com.
// Paper mail may be sent to: 
//    FlexRadio Systems
//    8900 Marybank Dr.
//    Austin, TX 78750
//    USA
//
//=================================================================
// Modifications to support the Behringer Midi controllers
// by Chris Codella, W2PA, May 2017.  Indicated by //-W2PA comment lines. 
// Modifications for using the new database import function.  W2PA, 29 May 2017
// Support QSK, possible with Protocol-2 firmware v1.7 (Orion-MkI and Orion-MkII), and later.  W2PA, 5 April 2019 
// Modfied heavily - Copyright (C) 2019-2026 Richard Samphire (MW0LGE)
//
//============================================================================================//
// Dual-Licensing Statement (Applies Only to Author's Contributions, Richard Samphire MW0LGE) //
// ------------------------------------------------------------------------------------------ //
// For any code originally written by Richard Samphire MW0LGE, or for any modifications       //
// made by him, the copyright holder for those portions (Richard Samphire) reserves the       //
// right to use, license, and distribute such code under different terms, including           //
// closed-source and proprietary licences, in addition to the GNU General Public License      //
// granted above. Nothing in this statement restricts any rights granted to recipients under  //
// the GNU GPL. Code contributed by others (not Richard Samphire) remains licensed under      //
// its original terms and is not affected by this dual-licensing statement in any way.        //
// Richard Samphire can be reached by email at :  mw0lge@grange-lane.co.uk                    //
//============================================================================================//

// Migrated to VS2026 - 18/12/25 MW0LGE v2.10.3.12

#include <atomic>

#include <QObject>
#include <QVector>

namespace NereusSDR {

class WdspEngine;
class AudioEngine;

// RxDspWorker runs the per-receiver I/Q → WDSP → audio processing step
// on a dedicated DSP thread, off the GUI main thread.
//
// Why: WDSP's fexchange2() (called via RxChannel::processIq) is opened
// with bfo=1, which makes it block on Sem_OutReady whenever the WDSP
// channel's internal DSP loop hasn't replenished its output ring. When
// that call ran on the GUI main thread it produced a deterministic
// two-way deadlock: fexchange2 waited on Sem_OutReady, the WDSP worker
// (wdspmain) waited on Sem_BuffReady, and because the main thread was
// blocked the Qt event loop stopped delivering more I/Q events to feed
// fexchange2 — so wdspmain never received another input batch and never
// signalled Sem_OutReady. Moving fexchange2 to its own thread keeps the
// blocking semantics local to that thread and lets the GUI event loop
// keep dispatching I/Q events.
//
// The worker is owned by RadioModel. It is constructed on the main
// thread, given non-owning WdspEngine/AudioEngine pointers via
// setEngines(), moved to RadioModel::m_dspThread, then driven by a
// Qt::QueuedConnection from ReceiverManager::iqDataForReceiver.
class RxDspWorker : public QObject {
    Q_OBJECT

public:
    // Default buffer sizes match the historical hardcoded values that
    // were correct for a 768 kHz wire rate (Thetis formula
    // in_size = 64 * rate / 48000 → 1024 at 768 kHz, out_size always 64
    // because WDSP decimates input_rate → 48000 internally). Real call
    // sites override these via setBufferSizes() once the wire rate is
    // known; the defaults exist so test fixtures and code paths that
    // predate setBufferSizes() keep working.
    static constexpr int kDefaultInSize  = 1024;
    static constexpr int kDefaultOutSize = 64;

    explicit RxDspWorker(QObject* parent = nullptr);
    ~RxDspWorker() override;

    // Set non-owning engine pointers. Must be called on the main
    // thread before moveToThread(). The engines must outlive this
    // worker — RadioModel guarantees this by tearing the worker down
    // before destroying the engines.
    void setEngines(WdspEngine* wdsp, AudioEngine* audio);

    // Configure the per-rate accumulator drain size. Must match the
    // in_size and out_size that RadioModel passed to
    // WdspEngine::createRxChannel for the same connect cycle, otherwise
    // fexchange2 receives the wrong number of samples per call and
    // produces glitchy / jittery audio output. The Thetis formula is
    // in_size = 64 * rate / 48000; out_size is always 64 in the current
    // RX path (input_rate → 48000 decimation). Safe to call from any
    // thread — m_inSize/m_outSize are std::atomic<int>, and
    // processIqBatch snapshots both at batch start so a concurrent
    // setBufferSizes() takes effect no earlier than the next batch.
    void setBufferSizes(int inSize, int outSize);

    int inSize() const { return m_inSize.load(std::memory_order_relaxed); }
    int outSize() const { return m_outSize.load(std::memory_order_relaxed); }

public slots:
    // Receive a batch of interleaved I/Q from ReceiverManager. Runs
    // on m_dspThread via Qt::QueuedConnection. Accumulates samples
    // into in_size chunks, hands each chunk to RxChannel::processIq,
    // and forwards the decoded audio to AudioEngine.
    void processIqBatch(int receiverIndex,
                        const QVector<float>& interleavedIQ);

    // Drop any partial accumulator state. Called from RadioModel
    // teardown via Qt::BlockingQueuedConnection so it executes on
    // the worker thread before the WDSP channel is destroyed.
    void resetAccumulator();

signals:
    // Emitted at the end of every processIqBatch invocation, on the
    // DSP thread. Used by tests to observe that work happens off the
    // main thread without requiring a real WDSP build.
    void batchProcessed();

    // Emitted once per drained chunk, regardless of whether the WDSP
    // engine is wired. Carries the chunk's sample count so tests can
    // verify the drain size matches setBufferSizes(). Production code
    // does not need to listen to this — it exists for the regression
    // test that pins the per-rate accumulator-drain contract.
    void chunkDrained(int sampleCount);

private:
    WdspEngine*      m_wdspEngine{nullptr};
    AudioEngine*     m_audioEngine{nullptr};
    QVector<float>   m_iqAccumI;
    QVector<float>   m_iqAccumQ;
    // Reusable interleaved stereo scratch handed to AudioEngine::rxBlockReady.
    // Sized to outSize*2 on first use and reused in-place per batch so the
    // DSP thread never allocates in the hot path after warmup.
    QVector<float>   m_interleavedOut;
    // Written by setBufferSizes() (typically on the main thread when the
    // wire rate changes) and read by processIqBatch() on the DSP thread.
    // std::atomic<int> prevents the C++ data race that plain int reads
    // would exhibit. Relaxed ordering is sufficient: no other state is
    // published alongside these values.
    std::atomic<int> m_inSize{kDefaultInSize};
    std::atomic<int> m_outSize{kDefaultOutSize};
};

} // namespace NereusSDR
