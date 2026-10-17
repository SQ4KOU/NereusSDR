// =================================================================
// src/core/audio/PortAudioLibrary.h  (NereusSDR)
// =================================================================
// no-port-check: NereusSDR-original.  One lock and one reference count
// for the PortAudio library in this process (R-AUD-06).
//
// Every PortAudio library call made outside an audio callback holds
// mutex(): Pa_Initialize and Pa_Terminate, the device and host API
// queries, and opening, starting, stopping and closing a stream.  It is
// never taken inside a callback.  Pa_StopStream waits for the callback
// to return while the lock is held; the callback takes no lock, so that
// wait always ends.  The lock is recursive: an open holds it and calls
// the query helpers, which take it again.
//
// acquire() and release() wrap Pa_Initialize and Pa_Terminate and count
// this process's references.  reinitialize() is Rescan: with no stream
// open it terminates PortAudio as many times as it is referenced and
// initialises it as many times again, all under the lock, so PortAudio
// lists the devices present now and no other thread ever sees it half
// way.  With a stream open it does nothing and says so (Pa_Terminate
// would close that stream under its owner).
//
// A driver that never returns must not hang the window.  The catalogue's
// thread takes the lock with no bound (LongHold: Rescan and the device
// listing) and marks since when it holds it.  Every other caller, the
// main thread above all, takes it through lockBounded(): it waits at most
// kBoundedWaitMs, and gives up at once when a LongHold has already held
// it that long (a driver call that has not returned).  A caller that
// gives up does nothing to PortAudio: release() leaves the reference and
// PortAudio running (never Pa_Terminate under a call still inside a
// driver), an open fails and is retried later, and the queries return
// the lists they last read.  A thread stuck inside a driver stays there
// until that driver returns or the process exits.
//
// Modification history (NereusSDR):
//   2026-10-09: native audio plan Task 7 fix (R-AUD-06). J.J. Boyd
//               (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-09: final review fix (R-AUD-03, R-AUD-06): bounded waits
//               (lockBounded, LongHold); the counts are atomics read
//               without the lock. J.J. Boyd (KG4VCF), AI-assisted via
//               Anthropic Claude Code.
//   2026-10-12: PortAudio starts on first need, not at launch
//               (ensureStarted, addUser, removeUser). J.J. Boyd (KG4VCF),
//               AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <functional>
#include <mutex>

namespace NereusSDR::PortAudioLibrary {

// The longest a bounded caller waits for the lock; the same 3 s as the
// catalogue's stop wait (AudioDeviceCatalog::kStopWaitMs).
constexpr int kBoundedWaitMs = 3000;

std::recursive_mutex& mutex();

// The lock; or, not owned (owns_lock() false), after kBoundedWaitMs, or at
// once when a LongHold has held it for kBoundedWaitMs already.
std::unique_lock<std::recursive_mutex> lockBounded();

// The lock with no bound, for the catalogue's thread (Rescan and the
// device listing).  Marks the lock held from now until it is released.
class LongHold {
public:
    LongHold();
    ~LongHold();
    LongHold(const LongHold&) = delete;
    LongHold& operator=(const LongHold&) = delete;

private:
    std::lock_guard<std::recursive_mutex> m_lock;
    bool m_marked = false;
};

// Pa_Initialize under the lock; true when it succeeded (one reference).
// False also when the lock is not had within the bound.
bool acquire();
// Pa_Terminate under the lock for one reference; nothing with none, and
// nothing (the reference and PortAudio left running) when the lock is
// not had within the bound.
void release();
// This process's references now.
int references();

// PortAudio starts on first need, never at launch: Pa_Initialize walks
// every audio device, which holds the caller for many seconds on a system
// that is slow to list them.  ensureStarted() is that first need (a stream
// on the older drivers, or their device list): it starts PortAudio once
// and keeps one reference for the process; later calls do nothing.  True
// when PortAudio is running afterwards, however it was started.  False in
// a test run (no PortAudio call), and when the lock is not had within the
// bound and PortAudio was not started on demand before.
bool ensureStarted();
// True once ensureStarted() has started PortAudio and until its reference
// is given back.
bool startedOnDemand();
// The engines that may need PortAudio.  The last one to leave gives back
// ensureStarted()'s reference; with none, the reference lasts until the
// process ends.
void addUser();
void removeUser();

// Pa_Initialize and Pa_Terminate calls made through this file.
int initializeCalls();
int terminateCalls();
#ifdef NEREUS_BUILD_TESTS
// Stand-ins for Pa_Initialize and Pa_Terminate (each returns a PaError),
// so a test counts the starts without a call into PortAudio.  Empty
// functions restore PortAudio's own.
void setDriverForTest(std::function<int()> initialize, std::function<int()> terminate);
#endif

// Rescan: true when PortAudio was started again, false when a stream is
// open or there is no reference (the next acquire() lists afresh).
bool reinitialize();

// A PortAudioBus stream opened or closed (the caller holds the lock).
void streamOpened();
void streamClosed();
int openStreams();

} // namespace NereusSDR::PortAudioLibrary
