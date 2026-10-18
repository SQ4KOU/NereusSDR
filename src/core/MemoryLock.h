// =================================================================
// src/core/MemoryLock.h  (NereusSDR-native)
// =================================================================
// 2026-05-26  J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude.
// 2026-10-15  J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude:
//             Windows raises the minimum working set before VirtualLock.
//
// Pin memory regions so the OS can not compress or page them out.
// Used on hot buffers (waterfall texture, overlay texture, FFT
// scratch, audio ring) that get touched at high frequency and whose
// stalls show up as jitter / underrun under build-load memory
// pressure.
//
// Backed by mlock(2) on macOS / Linux and VirtualLock on Windows.
// All three OSes enforce a per-process / per-user lockable byte
// limit (macOS RLIMIT_MEMLOCK ~ 96 MiB default; Linux similar but
// often capped lower without CAP_IPC_LOCK; Windows working-set
// minimum / SetProcessWorkingSetSize).  We log warnings on failure
// but continue -- locked memory is an optimization, not a
// correctness requirement.
//
// Windows: "The maximum number of pages that a process can lock is
// equal to the number of pages in its minimum working set minus a
// small overhead", and a process starts with a 50-page (200 KiB)
// minimum (Microsoft Learn: VirtualLock remarks,
// SetProcessWorkingSetSizeEx parameters).  So every lock first raises
// both working-set bounds by the locked size, and the unlock lowers
// them by the same amount.  Without that, every FFT scratch lock past
// the first 200 KiB failed with ERROR_WORKING_SET_QUOTA (1453).
//
// Caller responsibilities:
//
//   * Pass a stable pointer (the start of an allocation that won't
//     move).  std::vector::data() after resize is fine until the
//     next resize; QImage::bits() is fine until the QImage is
//     reassigned or resized.
//
//   * Unlock before resize / reallocation / destruction so the kernel
//     doesn't carry a stale lock past the lifetime of the pages.
//
//   * Each lock() must be paired with exactly one unlock() of the
//     same (addr, bytes) pair.
//
// Stats are aggregated globally so the perf overlay (or a future
// "/perf memlock" report) can show how much we have pinned.
// =================================================================
#pragma once

#include <cstddef>

namespace NereusSDR {

/// Pin the given memory region.  Internally aligns to the kernel's
/// page size (round addr down, length up) so callers can pass any
/// pointer / length pair without thinking about pages.
///
/// Returns true on success; logs a warning + returns false on OS
/// refusal (RLIMIT_MEMLOCK exceeded, region not resident, EPERM).
/// On failure the region is still usable -- just pageable.
bool lockMemory(const void* addr, std::size_t bytes, const char* tag = "");

/// Release a previously-locked region.  Same addr / bytes contract
/// as lock; aligns internally.  Safe to call with (nullptr, 0).
///
/// A no-op -- including for the stats -- when this region is not
/// currently locked.  Callers routinely unlock unconditionally in
/// destructors and realloc paths without checking whether the matching
/// lockMemory() succeeded, which is the common case on Linux once
/// RLIMIT_MEMLOCK is exhausted.  Internally only successfully-locked
/// ranges are tracked, so bytesLocked / regionsLocked can never
/// underflow.
///
/// If the region was locked with a different length than the one
/// passed here (buffer resized between lock and unlock), the range
/// that was actually locked is released.
void unlockMemory(const void* addr, std::size_t bytes);

/// Aggregate stats across all currently-locked regions.  Read-only;
/// used by the perf overlay / diagnostics.
struct MemoryLockStats {
    std::size_t bytesLocked{0};   // sum of currently-held lock byte counts
    int regionsLocked{0};         // currently-held lock count
    int lockFailuresTotal{0};     // cumulative lock() returns false
};
MemoryLockStats memoryLockStats();

namespace detail {

/// A process's minimum and maximum working-set sizes, in bytes.  Pure
/// arithmetic, built on every platform so the tests run everywhere;
/// only the Windows lock path calls these.
struct WorkingSetLimits {
    std::size_t minimum{0};
    std::size_t maximum{0};
};

/// Limits to ask for before locking `bytes` more: both bounds grow by
/// `bytes`, saturating instead of wrapping.
WorkingSetLimits workingSetForLock(WorkingSetLimits current, std::size_t bytes);

/// Limits to ask for after unlocking `bytes` that workingSetForLock()
/// added: both bounds shrink by `bytes`, stopping at zero, and the
/// maximum never drops below the minimum.
WorkingSetLimits workingSetForUnlock(WorkingSetLimits current, std::size_t bytes);

} // namespace detail

} // namespace NereusSDR
