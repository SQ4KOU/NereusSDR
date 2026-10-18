// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
#pragma once
// no-port-check: NereusSDR-original main-thread observer history.
#include "WaveformSnapshot.h"
#include "core/NereusCoreExport.h"
namespace NereusSDR {
class NEREUS_CORE_EXPORT WaveformHistory
{
  public:
    void append(const WaveformBatch& batch);
    void clear();
    WaveformSnapshotPtr snapshot(qint64 asOfNs);

  private:
    WaveformSnapshot m_data;
    WaveformSnapshotPtr m_cached;
    void evict(qint64 beforeNs);
};
} // namespace NereusSDR
