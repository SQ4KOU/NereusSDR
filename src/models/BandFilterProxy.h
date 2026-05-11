// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - BandFilterProxy: QSortFilterProxyModel that hides spots
// whose band (as reported by SpotTableModel's ColBand DisplayRole) is in
// the m_hiddenBands set. setBandVisible(band, visible) toggles set
// membership and reinvalidates the filter. Empty / unknown band always
// shows.
//
// Ported from AetherSDR src/gui/DxClusterDialog.h:62-75 [@0cd4559].
// AetherSDR is (C) its contributors and is licensed GPL-3.0-or-later
// (see https://github.com/ten9876/AetherSDR/blob/main/LICENSE).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task D2. Initial port.
//                                    AetherSDR's "AetherSDR" namespace
//                                    becomes "NereusSDR". Class lived
//                                    inline in AetherSDR's
//                                    DxClusterDialog.h:62-75 (with
//                                    setBandVisible / filterAcceptsRow
//                                    in DxClusterDialog.cpp:208-226);
//                                    extracted to standalone src/models/
//                                    files so the SpotHubDialog (Phase
//                                    F) can reuse it. Public surface
//                                    (setBandVisible, isBandVisible)
//                                    and protected filterAcceptsRow
//                                    override preserved verbatim.
//                                    Filter looks up the band string
//                                    via SpotTableModel::ColBand
//                                    DisplayRole; empty band string
//                                    always shows. AI tooling:
//                                    Anthropic Claude Code.

#pragma once

#include <QSortFilterProxyModel>
#include <QSet>
#include <QString>

namespace NereusSDR {

// From AetherSDR src/gui/DxClusterDialog.h:62-75 [@0cd4559]
class BandFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    explicit BandFilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void setBandVisible(const QString& band, bool visible);
    bool isBandVisible(const QString& band) const { return !m_hiddenBands.contains(band); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QSet<QString> m_hiddenBands;
};

} // namespace NereusSDR
