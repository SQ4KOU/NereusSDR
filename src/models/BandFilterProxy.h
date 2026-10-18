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
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task F3. Adds source
//                                    filtering as a NereusSDR-native
//                                    extension (AetherSDR upstream
//                                    proxies bands only). New public
//                                    surface: setSourceVisible(source,
//                                    visible) and isSourceVisible(source)
//                                    mirror the band API. The proxy
//                                    looks up source via
//                                    SpotTableModel::ColSource
//                                    DisplayRole and applies both
//                                    band and source predicates in
//                                    filterAcceptsRow (AND semantics:
//                                    a row must pass both filters to
//                                    be visible). Empty source string
//                                    always shows (matches band
//                                    convention). The Spot List tab
//                                    drives this from 7 source-pill
//                                    QPushButtons (DX / RBN / JT / COL
//                                    / POT / FDR / PSK). AI tooling:
//                                    Anthropic Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  Derives from OrderKeepingProxyModel,
//                                    so the Spot List's accessibility-safe
//                                    refresh can reset it in the order it
//                                    shows. AI-assisted via Anthropic
//                                    Claude Code.
//   2026-10-15  J.J. Boyd (KG4VCF)  invalidateFilterCompat flushes the
//                                    view's pending accessibility reset
//                                    first. AI-assisted via Anthropic
//                                    Claude Code.

#pragma once

#include "models/OrderKeepingProxyModel.h"

#include <QSortFilterProxyModel>
#include <QSet>
#include <QString>
#include <QtGlobal>

namespace NereusSDR {

// From AetherSDR src/gui/DxClusterDialog.h:62-75 [@0cd4559]
class BandFilterProxy : public OrderKeepingProxyModel {
    Q_OBJECT
public:
    explicit BandFilterProxy(QObject* parent = nullptr) : OrderKeepingProxyModel(parent) {}

    void setBandVisible(const QString& band, bool visible);
    bool isBandVisible(const QString& band) const { return !m_hiddenBands.contains(band); }

    // NereusSDR Task F3 extension. AetherSDR upstream filters bands
    // only; the SpotHub Spot List tab additionally pills by source
    // (Cluster / RBN / WSJT-X / SpotCollector / POTA / FreeDV / PSK).
    void setSourceVisible(const QString& source, bool visible);
    bool isSourceVisible(const QString& source) const { return !m_hiddenSources.contains(source); }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

    // Qt 6.13 deprecated invalidateFilter() in favor of
    // beginFilterChange() / endFilterChange(Direction::Rows). Both APIs
    // exist in Qt 6.10+. NereusSDR has no explicit Qt minimum in
    // CMakeLists.txt, so we keep a fallback to invalidateFilter on
    // older Qt builds. NereusSDR-only compat shim; not an upstream port.
    // The view's accessibility flush runs first (a no-op once the setters
    // flushed before changing the filter).
    void invalidateFilterCompat() {
        prepareForRowChange();
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
        beginFilterChange();
        endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
        invalidateFilter();
#endif
    }

private:
    QSet<QString> m_hiddenBands;
    QSet<QString> m_hiddenSources;
};

} // namespace NereusSDR
