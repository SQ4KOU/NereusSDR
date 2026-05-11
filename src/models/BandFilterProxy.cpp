// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - BandFilterProxy implementation. setBandVisible toggles
// QSet<QString> membership; filterAcceptsRow reads SpotTableModel::ColBand
// DisplayRole to compare against m_hiddenBands.
//
// Ported from AetherSDR src/gui/DxClusterDialog.cpp:208-226 [@0cd4559].
// AetherSDR is (C) its contributors and is licensed GPL-3.0-or-later
// (see https://github.com/ten9876/AetherSDR/blob/main/LICENSE).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task D2. Initial port.
//                                    AetherSDR's "AetherSDR" namespace
//                                    becomes "NereusSDR". Implementation
//                                    extracted from
//                                    DxClusterDialog.cpp:208-226 verbatim:
//                                    setBandVisible (toggles QSet
//                                    membership and reinvalidates the
//                                    filter), filterAcceptsRow (empty
//                                    m_hiddenBands -> always accept;
//                                    looks up the band string via
//                                    SpotTableModel::ColBand
//                                    DisplayRole; empty band always
//                                    shows; otherwise membership
//                                    check). AI tooling: Anthropic
//                                    Claude Code.

#include "BandFilterProxy.h"
#include "SpotTableModel.h"

namespace NereusSDR {

// From AetherSDR src/gui/DxClusterDialog.cpp:208-215 [@0cd4559]
void BandFilterProxy::setBandVisible(const QString& band, bool visible)
{
    if (visible)
        m_hiddenBands.remove(band);
    else
        m_hiddenBands.insert(band);
    invalidateFilter();
}

// From AetherSDR src/gui/DxClusterDialog.cpp:217-226 [@0cd4559]
bool BandFilterProxy::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    if (m_hiddenBands.isEmpty())
        return true;
    auto idx = sourceModel()->index(sourceRow, SpotTableModel::ColBand, sourceParent);
    QString band = sourceModel()->data(idx, Qt::DisplayRole).toString();
    if (band.isEmpty())
        return true;  // unknown band - always show
    return !m_hiddenBands.contains(band);
}

} // namespace NereusSDR
