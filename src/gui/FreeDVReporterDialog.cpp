// SPDX-License-Identifier: GPL-2.0-or-later
//
// NereusSDR - FreeDVReporterDialog: standalone window listing every
// connected qso.freedv.org station in a sortable 14-column table.
// Implementation.
//
// Ported from freedv-gui src/gui/dialogs/freedv_reporter.{h,cpp}
// [@77e793a] - Qt6-native rewrite. Per-port cites inline next to each
// function body.
//
// License (upstream): freedv-gui carries an LGPLv2.1+ root license
// (`freedv-gui/COPYING`); the specific `freedv_reporter.cpp` file
// carries a per-file Copyright header (Mooneer Salem, GPLv2.1+)
// reproduced verbatim below. LGPL is upgrade-compatible to GPLv2-or-
// later (LGPL §3 conversion clause).
//
// --- From freedv-gui src/gui/dialogs/freedv_reporter.cpp:1-21 (verbatim header) ---
//
// ==========================================================================
// Name:            freedv_reporter.cpp
// Purpose:         Dialog that displays current FreeDV Reporter spots
// Created:         Jun 12, 2023
// Authors:         Mooneer Salem
//
// License:
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2.1,
//  as published by the Free Software Foundation.  This program is
//  distributed in the hope that it will be useful, but WITHOUT ANY
//  WARRANTY; without even the implied warranty of MERCHANTABILITY or
//  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
//  License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <http://www.gnu.org/licenses/>.
//
// ==========================================================================
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task G1. Initial shell
//                                    port. Implementation. AI tooling:
//                                    Anthropic Claude Code.

#include "FreeDVReporterDialog.h"

#include "core/FreeDVReporterClient.h"
#include "core/FreeDVStation.h"
#include "models/FreeDVStationModel.h"

#include <QAbstractTableModel>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMenuBar>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace NereusSDR {

// =====================================================================
// Highlight color defaults
//
// TX background: #fc4500 ported verbatim from freedv-gui
//   src/config/ReportingConfiguration.cpp:91 [@77e793a]
//   (default value of freedvReporterTxRowBackgroundColor).
// Msg background: #E58BE5 ported verbatim from freedv-gui
//   src/config/ReportingConfiguration.cpp:95 [@77e793a]
//   (default value of freedvReporterMsgRowBackgroundColor).
// RX background: NereusSDR-architectural divergence. Upstream uses
//   #379baf (cyan-blue, more blue than green) as the default
//   value of freedvReporterRxRowBackgroundColor
//   (ReportingConfiguration.cpp:93 [@77e793a]). NereusSDR picks
//   a green-dominant tone here so the dialog visually distinguishes
//   "actively decoding a callsign right now" (green) from
//   "transmitting" (red); G3 will surface the upstream color knobs
//   to AppSettings and let the user restore upstream cyan if they
//   prefer.
// =====================================================================
static const QColor kTxBackgroundColor(0xfc, 0x45, 0x00);  // red-orange
static const QColor kRxBackgroundColor(0x3f, 0xaf, 0x55);  // green
static const QColor kMsgBackgroundColor(0xe5, 0x8b, 0xe5); // pink

// Default highlight-clear interval. Production matches the task spec's
// hard-coded 6 s; the test seam drops this to 50 ms.
// TODO: Phase 3J-2 G3 makes this configurable via AppSettings, matching
// upstream's freedv_reporter_tx_rx_highlight_time setting.
static constexpr int kDefaultHighlightClearMs = 6000;

// =====================================================================
// FreeDVReporterTableModel
//
// QAbstractTableModel adapter over FreeDVStationModel. Each row is a
// FreeDVStation snapshot; the column count is fixed at 14 (see
// FreeDVReporterColumn enum in the header).
//
// The mapping rules (header text, alignment, cell text formatting) are
// drawn from freedv-gui's createColumn_ switch (freedv_reporter.cpp
// :75-182 [@77e793a]) and the corresponding GetValue() switch
// (freedv_reporter.cpp:3087-3146 [@77e793a]); the QAbstractTableModel
// shape itself is NereusSDR-native since wxDataViewModel is wx-specific.
// =====================================================================
class FreeDVReporterTableModel : public QAbstractTableModel {
public:
    explicit FreeDVReporterTableModel(QObject* parent = nullptr)
        : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : m_rows.size();
    }
    int columnCount(const QModelIndex& parent = QModelIndex()) const override {
        return parent.isValid() ? 0 : kFreeDVReporterColumnCount;
    }

    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override {
        if (orientation != Qt::Horizontal || section < 0 || section >= kFreeDVReporterColumnCount) {
            return {};
        }
        if (role == Qt::DisplayRole) {
            // From freedv-gui freedv_reporter.cpp:85-153 [@77e793a]
            // (createColumn_ switch column-name table). G1 hard-codes
            // MHz for col 5; the kHz/MHz toggle from the upstream
            // reportingFrequencyAsKhz setting lands in G3.
            switch (section) {
                case kCallsignCol:       return QStringLiteral("Callsign");
                case kGridSquareCol:     return QStringLiteral("Locator");
                case kDistanceCol:       return QStringLiteral("km");
                case kHeadingCol:        return QStringLiteral("Hdg");
                case kVersionCol:        return QStringLiteral("Version");
                case kFrequencyCol:      return QStringLiteral("MHz");
                case kTxModeCol:         return QStringLiteral("Mode");
                case kStatusCol:         return QStringLiteral("Status");
                case kUserMessageCol:    return QStringLiteral("Msg");
                case kLastTxDateCol:     return QStringLiteral("Last TX");
                case kLastRxCallsignCol: return QStringLiteral("RX Call");
                case kLastRxModeCol:     return QStringLiteral("Mode (RX)");
                case kSnrCol:            return QStringLiteral("SNR");
                case kLastUpdateDateCol: return QStringLiteral("Last Update");
                default: return {};
            }
        }
        if (role == Qt::TextAlignmentRole) {
            // From freedv-gui freedv_reporter.cpp:79+93-153 [@77e793a]
            // (renderer SetAlignment per column). Most columns default
            // to wxALIGN_LEFT; DISTANCE / HEADING / FREQUENCY override
            // to wxALIGN_RIGHT; USER_MESSAGE builds with wxALIGN_CENTER.
            switch (section) {
                case kDistanceCol:
                case kHeadingCol:
                case kFrequencyCol:
                    return int(Qt::AlignRight | Qt::AlignVCenter);
                case kUserMessageCol:
                    return int(Qt::AlignHCenter | Qt::AlignVCenter);
                default:
                    return int(Qt::AlignLeft | Qt::AlignVCenter);
            }
        }
        return {};
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
            return {};
        }
        const FreeDVStation& s = m_rows.at(index.row());

        if (role == Qt::DisplayRole) {
            // From freedv-gui freedv_reporter.cpp:3087-3146 [@77e793a]
            // (GetValue switch). NereusSDR formats the per-cell strings
            // up front here instead of caching them on the row struct
            // the way upstream does (freedv-gui refreshes `freqString`
            // / `snr` / `heading` on the ReporterData record itself).
            switch (index.column()) {
                case kCallsignCol:       return s.callsign;
                case kGridSquareCol:     return s.gridSquare;
                case kDistanceCol:       return formatDistance(s.distanceKm);
                case kHeadingCol:        return formatHeading(s.headingDeg, s.headingCardinal);
                case kVersionCol:        return s.version;
                case kFrequencyCol:      return formatFrequencyMhz(s.frequencyHz);
                case kTxModeCol:         return s.txMode;
                case kStatusCol:         return s.status;
                case kUserMessageCol:    return s.userMessage;
                case kLastTxDateCol:     return formatTimestamp(s.lastTxDate);
                case kLastRxCallsignCol: return s.lastRxCallsign;
                case kLastRxModeCol:     return s.lastRxMode;
                case kSnrCol:            return formatSnr(s.snrVal);
                case kLastUpdateDateCol: return formatTimestamp(s.lastUpdate);
                default: return {};
            }
        }
        if (role == Qt::TextAlignmentRole) {
            // Per-cell alignment mirrors per-header alignment.
            return headerData(index.column(), Qt::Horizontal, Qt::TextAlignmentRole);
        }
        return {};
    }

    // FreeDVStationModel adapters. The dialog connects these to the
    // model's signals so the table stays in sync.
    void onStationAdded(const QString& sid, const FreeDVStation& info) {
        if (m_indexBySid.contains(sid)) {
            onStationUpdated(sid, info);
            return;
        }
        const int newRow = m_rows.size();
        beginInsertRows(QModelIndex(), newRow, newRow);
        m_rows.append(info);
        m_indexBySid.insert(sid, newRow);
        endInsertRows();
    }

    void onStationUpdated(const QString& sid, const FreeDVStation& info) {
        auto it = m_indexBySid.constFind(sid);
        if (it == m_indexBySid.constEnd()) {
            onStationAdded(sid, info);
            return;
        }
        const int row = it.value();
        m_rows[row] = info;
        emit dataChanged(index(row, 0), index(row, kFreeDVReporterColumnCount - 1));
    }

    void onStationRemoved(const QString& sid) {
        auto it = m_indexBySid.constFind(sid);
        if (it == m_indexBySid.constEnd()) {
            return;
        }
        const int row = it.value();
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        m_indexBySid.remove(sid);
        // Reindex the trailing rows.
        for (auto i = m_indexBySid.begin(); i != m_indexBySid.end(); ++i) {
            if (i.value() > row) {
                i.value() -= 1;
            }
        }
        endRemoveRows();
    }

    void onCleared() {
        beginResetModel();
        m_rows.clear();
        m_indexBySid.clear();
        endResetModel();
    }

    int rowForSid(const QString& sid) const {
        return m_indexBySid.value(sid, -1);
    }

private:
    // From freedv-gui freedv_reporter.cpp:3180-3194 [@77e793a].
    // Upstream uses wxNumberFormatter::ToString(freqHz/1e6, 4); the
    // QLocale-based formatter matches that 4-decimal rendering.
    static QString formatFrequencyMhz(quint64 frequencyHz) {
        if (frequencyHz == 0) {
            return QStringLiteral(" - ");
        }
        const double mhz = static_cast<double>(frequencyHz) / 1.0e6;
        return QLocale::system().toString(mhz, 'f', 4);
    }

    // SNR rendering. NereusSDR-architectural divergence from upstream:
    // upstream calls wxNumberFormatter::ToString(snr, 1) which yields
    // "12.0" / "-99.0" / etc (freedv_reporter.cpp:3692 [@77e793a]);
    // NereusSDR renders the integer with a forced sign prefix and
    // shows " - " for the unknown-SNR sentinel (-99) so the column
    // stays narrow at 60 px minimum width. G3 will surface this as a
    // per-radio formatter setting if upstream parity is requested.
    static QString formatSnr(int snrVal) {
        if (snrVal == -99) {
            return QStringLiteral(" - ");
        }
        if (snrVal >= 0) {
            return QStringLiteral("+%1").arg(snrVal);
        }
        return QString::number(snrVal);
    }

    // From freedv-gui freedv_reporter.cpp:3358-3363 [@77e793a]
    // (refreshAllRows distance/heading string assembly). Upstream uses
    // wxNumberFormatter::ToString(distance, 0); NereusSDR matches that.
    static QString formatDistance(double distanceKm) {
        if (distanceKm <= 0.0) {
            return QStringLiteral(" - ");
        }
        return QString::number(static_cast<int>(distanceKm + 0.5));
    }

    // From freedv-gui freedv_reporter.cpp:3196-3210 [@77e793a]
    // (refreshAllRows heading string). Upstream renders either
    // GetCardinalDirection_(headingVal) or ToString(headingVal, 0) per
    // user setting reportingDirectionAsCardinal. NereusSDR pre-stamps
    // the cardinal in FreeDVStationModel (Task D3 followup) and renders
    // "045 NE" combining both forms; when no grid is available the
    // upstream `parent_->UNKNOWN_STR` path produces " - ".
    static QString formatHeading(double headingDeg, const QString& cardinal) {
        if (headingDeg <= 0.0 && cardinal.isEmpty()) {
            return QStringLiteral(" - ");
        }
        const QString deg = QStringLiteral("%1").arg(static_cast<int>(headingDeg + 0.5),
                                                     3, 10, QLatin1Char('0'));
        if (cardinal.isEmpty()) {
            return deg + QStringLiteral("°");
        }
        return deg + QStringLiteral("° ") + cardinal;
    }

    // From freedv-gui freedv_reporter.cpp:2308 [@77e793a] (`return
    // parent_->UNKNOWN_STR`) - upstream formats datestamps via the
    // makeValidTime_() helper, which calls wxDateTime::Format("%x %X")
    // by default; NereusSDR renders local time with HH:mm:ss per the
    // G1 task spec ("upstream wxDateTime::Format("%H:%M:%S")"). The
    // wider %x %X format lands in G3 once the kHz/MHz toggle exposes
    // the format setting.
    static QString formatTimestamp(const QDateTime& dt) {
        if (!dt.isValid()) {
            return QStringLiteral(" - ");
        }
        return dt.toLocalTime().toString(QStringLiteral("HH:mm:ss"));
    }

    QList<FreeDVStation>     m_rows;
    QHash<QString, int>      m_indexBySid;
};

// =====================================================================
// FreeDVReporterRowHighlightDelegate
//
// Paints row backgrounds when a sid has an active TX / RX / Msg
// highlight. The highlight map is owned by the dialog (so a per-sid
// QTimer can clear it after kDefaultHighlightClearMs) and queried by
// the delegate at paint time.
//
// The delegate is a NereusSDR-architectural divergence from upstream's
// approach (upstream stamps `backgroundColor` directly onto the
// `ReporterData` row struct and the wxDataViewCtrl renderer reads it
// off the row; freedv_reporter.cpp:3026 [@77e793a]). Keeping the
// highlight state in a separate view-side QHash keeps the QAbstract
// TableModel data layer free of view bookkeeping.
// =====================================================================
class FreeDVReporterRowHighlightDelegate : public QStyledItemDelegate {
public:
    explicit FreeDVReporterRowHighlightDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);
        // Resolve the sid for this row, then check the highlight map.
        const QString sid = m_sidByRow.value(index.row());
        const QColor bg = m_highlight.value(sid);
        if (bg.isValid()) {
            painter->save();
            painter->fillRect(opt.rect, bg);
            painter->restore();
        }
        QStyledItemDelegate::paint(painter, option, index);
    }

    // Highlight map mutators - the dialog drives these on stationUpdated.
    void setRowHighlight(const QString& sid, const QColor& bg) {
        if (bg.isValid()) {
            m_highlight.insert(sid, bg);
        } else {
            m_highlight.remove(sid);
        }
    }
    void setSidOrder(const QList<QString>& sids) {
        m_sidByRow.clear();
        for (int r = 0; r < sids.size(); ++r) {
            m_sidByRow.insert(r, sids.at(r));
        }
    }
    QColor highlightFor(const QString& sid) const {
        return m_highlight.value(sid);
    }

private:
    QHash<QString, QColor>       m_highlight;
    QHash<int, QString>          m_sidByRow;
};

// =====================================================================
// FreeDVReporterDialog
// =====================================================================

FreeDVReporterDialog::FreeDVReporterDialog(FreeDVStationModel* stationModel,
                                           FreeDVReporterClient* client,
                                           QWidget* parent)
    : QWidget(parent, Qt::Window),
      m_stationModel(stationModel),
      m_client(client),
      m_highlightClearMs(kDefaultHighlightClearMs) {
    setAttribute(Qt::WA_DeleteOnClose, false);
    setWindowTitle(QStringLiteral("FreeDV Reporter"));
    buildUi();

    if (m_stationModel) {
        connect(m_stationModel, &FreeDVStationModel::stationAdded,
                this, &FreeDVReporterDialog::onStationAdded);
        connect(m_stationModel, &FreeDVStationModel::stationUpdated,
                this, &FreeDVReporterDialog::onStationUpdated);
        connect(m_stationModel, &FreeDVStationModel::stationRemoved,
                this, &FreeDVReporterDialog::onStationRemoved);
        connect(m_stationModel, &FreeDVStationModel::cleared,
                this, &FreeDVReporterDialog::onCleared);
        // Seed any stations the model already holds.
        const auto initial = m_stationModel->stations();
        for (auto it = initial.constBegin(); it != initial.constEnd(); ++it) {
            onStationAdded(it.key(), it.value());
        }
    }
}

FreeDVReporterDialog::~FreeDVReporterDialog() {
    // m_clearTimers and m_table / delegates are parented to the dialog;
    // Qt's parent ownership reclaims them automatically.
}

void FreeDVReporterDialog::buildUi() {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // G3 placeholder. The Show / Filter / Idle-longer-than menus wire
    // in here. Built now so MainWindow's single-instance pointer can
    // findChild<QMenuBar*>() in tests.
    m_menuBar = new QMenuBar(this);
    outer->setMenuBar(m_menuBar);

    m_tableModel = new FreeDVReporterTableModel(this);
    m_rowDelegate = new FreeDVReporterRowHighlightDelegate(this);

    m_table = new QTableView(this);
    m_table->setModel(m_tableModel);
    m_table->setItemDelegate(m_rowDelegate);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSortingEnabled(true);
    m_table->setAlternatingRowColors(false);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionsMovable(true);  // G3 surfaces this
    m_table->horizontalHeader()->setStretchLastSection(true);

    // Per-column minimum widths from freedv-gui createColumn_
    //   (freedv_reporter.cpp:75-182 [@77e793a]). Upstream calls
    //   colObj->SetMinWidth(minWidth); Qt does the equivalent via
    //   the header's setMinimumSectionSize per-column.
    static constexpr int kMinWidths[kFreeDVReporterColumnCount] = {
        70,  // CALLSIGN_COL
        65,  // GRID_SQUARE_COL
        60,  // DISTANCE_COL
        60,  // HEADING_COL
        70,  // VERSION_COL
        60,  // FREQUENCY_COL
        65,  // TX_MODE_COL
        60,  // STATUS_COL
        130, // USER_MESSAGE_COL
        60,  // LAST_TX_DATE_COL
        65,  // LAST_RX_CALLSIGN_COL
        60,  // LAST_RX_MODE_COL
        60,  // SNR_COL
        100, // LAST_UPDATE_DATE_COL
    };
    auto* header = m_table->horizontalHeader();
    for (int c = 0; c < kFreeDVReporterColumnCount; ++c) {
        header->resizeSection(c, kMinWidths[c]);
    }

    outer->addWidget(m_table, 1);

    // G2 placeholder. The filter / QSY / message bar wires in here.
    m_bottomControls = new QHBoxLayout;
    m_bottomControls->setContentsMargins(4, 4, 4, 4);
    auto* bottomWrap = new QWidget(this);
    bottomWrap->setLayout(m_bottomControls);
    outer->addWidget(bottomWrap);
}

void FreeDVReporterDialog::onStationAdded(const QString& sid, const FreeDVStation& info) {
    m_tableModel->onStationAdded(sid, info);
    refreshDelegateSidOrder();
    // From freedv-gui freedv_reporter.cpp:1308-1322 [@77e793a]
    // (priority chain: msg > tx > rx). Apply on add too in case the
    // first event we receive already carries TX-active state.
    applyHighlightForStation(sid, info);
}

void FreeDVReporterDialog::onStationUpdated(const QString& sid, const FreeDVStation& info) {
    m_tableModel->onStationUpdated(sid, info);
    refreshDelegateSidOrder();
    // From freedv-gui freedv_reporter.cpp:1308-1322 [@77e793a]
    applyHighlightForStation(sid, info);
}

void FreeDVReporterDialog::onStationRemoved(const QString& sid) {
    if (auto* t = m_clearTimers.take(sid)) {
        t->stop();
        t->deleteLater();
    }
    m_rowDelegate->setRowHighlight(sid, QColor());
    m_tableModel->onStationRemoved(sid);
    refreshDelegateSidOrder();
}

void FreeDVReporterDialog::onCleared() {
    for (auto it = m_clearTimers.begin(); it != m_clearTimers.end(); ++it) {
        it.value()->stop();
        it.value()->deleteLater();
    }
    m_clearTimers.clear();
    m_tableModel->onCleared();
    refreshDelegateSidOrder();
}

void FreeDVReporterDialog::applyHighlightForStation(const QString& sid,
                                                    const FreeDVStation& info) {
    // From freedv-gui freedv_reporter.cpp:1289-1322 [@77e793a]
    // (priority chain: messaging > transmitting > receiving valid
    // callsign > receiving without valid callsign > default). NereusSDR
    // simplifies the four upstream branches to three (TX, RX, Msg) and
    // drops the "lastRxCallsign empty -> short timeout" subcase since
    // G1 does not yet receive that distinction over the wire (a freq
    // change clears lastRxCallsign upstream; we'll wire that explicitly
    // in G2 once the FreeDVReporterClient gains the freq_change ->
    // station-update relay).
    const bool isTransmitting = info.transmitting;
    const bool isReceiving =
        info.lastRxDate.isValid()
        && !info.lastRxCallsign.isEmpty()
        && info.lastRxDate.msecsTo(QDateTime::currentDateTime()) < m_highlightClearMs;
    const bool isMessaging =
        !info.userMessage.isEmpty()
        && info.lastUpdate.isValid()
        && info.lastUpdate.msecsTo(QDateTime::currentDateTime()) < m_highlightClearMs;

    QColor bg;
    if (isMessaging) {
        bg = kMsgBackgroundColor;
    } else if (isTransmitting) {
        bg = kTxBackgroundColor;
    } else if (isReceiving) {
        bg = kRxBackgroundColor;
    }
    setHighlight(sid, bg);
}

void FreeDVReporterDialog::setHighlight(const QString& sid, const QColor& bg) {
    if (bg.isValid()) {
        m_rowDelegate->setRowHighlight(sid, bg);
        // Schedule the clear timer. Replaces any prior clear timer for
        // this sid so a fresh TX event extends the highlight window.
        if (auto* prev = m_clearTimers.take(sid)) {
            prev->stop();
            prev->deleteLater();
        }
        auto* timer = new QTimer(this);
        timer->setSingleShot(true);
        connect(timer, &QTimer::timeout, this, [this, sid] {
            m_rowDelegate->setRowHighlight(sid, QColor());
            if (auto* t = m_clearTimers.take(sid)) {
                t->deleteLater();
            }
            if (m_table) {
                m_table->viewport()->update();
            }
        });
        m_clearTimers.insert(sid, timer);
        timer->start(m_highlightClearMs);
    } else {
        m_rowDelegate->setRowHighlight(sid, QColor());
        if (auto* prev = m_clearTimers.take(sid)) {
            prev->stop();
            prev->deleteLater();
        }
    }
    if (m_table) {
        m_table->viewport()->update();
    }
}

void FreeDVReporterDialog::refreshDelegateSidOrder() {
    // Walk the table model's current row order and tell the delegate
    // which sid lives at each row.
    QList<QString> sids;
    sids.reserve(m_tableModel->rowCount());
    for (int r = 0; r < m_tableModel->rowCount(); ++r) {
        const QString call = m_tableModel->data(m_tableModel->index(r, kCallsignCol),
                                                Qt::DisplayRole).toString();
        // Look up sid by walking the station model's hash; cheap for
        // the < 200-station typical population. G2 will replace this
        // with a sid-keyed iteration once we surface that field.
        QString sidForRow;
        if (m_stationModel) {
            const auto map = m_stationModel->stations();
            for (auto it = map.constBegin(); it != map.constEnd(); ++it) {
                if (it.value().callsign == call) {
                    sidForRow = it.key();
                    break;
                }
            }
        }
        sids.append(sidForRow);
    }
    m_rowDelegate->setSidOrder(sids);
}

void FreeDVReporterDialog::setHighlightClearMsForTest(int ms) {
    m_highlightClearMs = ms;
    // Restart any active clear timers so the test seam takes effect
    // immediately rather than waiting for the next TX / RX event.
    for (auto it = m_clearTimers.begin(); it != m_clearTimers.end(); ++it) {
        it.value()->stop();
        it.value()->start(m_highlightClearMs);
    }
}

QColor FreeDVReporterDialog::rowHighlightColorForTest(const QString& sid) const {
    if (!m_rowDelegate) {
        return {};
    }
    return m_rowDelegate->highlightFor(sid);
}

}  // namespace NereusSDR
