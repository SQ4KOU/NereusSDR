// SPDX-License-Identifier: GPL-2.0-or-later
//
// NereusSDR - FreeDVReporterDialog: standalone window listing every
// connected qso.freedv.org station in a sortable 14-column table.
//
// Ported from freedv-gui src/gui/dialogs/freedv_reporter.{h,cpp}
// [@77e793a] - Qt6-native rewrite. NereusSDR re-uses the COLUMN
// DEFINITIONS, COLUMN ORDER, COLUMN ALIGNMENT, ROW HIGHLIGHT BEHAVIOR,
// and SNR / FREQUENCY / TIMESTAMP FORMATTING from upstream and
// implements them with QTableView + a private QAbstractTableModel
// subclass; the wxWidgets-specific bits (wxDataViewCtrl, ReportData
// per-row pointers in fnQueue_) do not carry over because Qt's model /
// view contract takes the place of those.
//
// G1 is the shell + 14-column table only. The filter / QSY / message
// bar (G2) and the menu bar with Show / Filter / Idle-longer-than menus
// (G3) come in later tasks. Placeholder bottom-controls QHBoxLayout
// and QMenuBar are added in G1 so G2 and G3 can drop content into the
// existing shell without restructuring.
//
// License (upstream): freedv-gui carries an LGPLv2.1+ root license
// (`freedv-gui/COPYING`); the specific `freedv_reporter.h` /
// `freedv_reporter.cpp` files each carry a per-file Copyright header
// (Mooneer Salem, GPLv2.1+) reproduced verbatim below. LGPL is upgrade-
// compatible to GPLv2-or-later when linked into a GPL work (LGPL §3
// conversion clause), which is the model NereusSDR uses.
//
// --- From freedv-gui src/gui/dialogs/freedv_reporter.h:1-21 (verbatim header) ---
//
// ==========================================================================
// Name:            freedv_reporter.h
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
//                                    port. Constructor wires the
//                                    FreeDVStationModel pointer that
//                                    FreeDVReporterClient + Task D3
//                                    already feed; the FreeDVReporter
//                                    Client* second arg is reserved
//                                    for the G2 QSY-button work and
//                                    is unused in G1. The 14 column
//                                    labels / widths / alignment come
//                                    from upstream createColumn_
//                                    (freedv_reporter.cpp:75-182
//                                    [@77e793a]). The per-row TX-red /
//                                    RX-green highlight tones come
//                                    from upstream updateHighlights
//                                    (freedv_reporter.cpp:1289-1322
//                                    [@77e793a]); NereusSDR drops the
//                                    250ms timer / lastRxDate threshold
//                                    sweep in favor of a per-row QTimer
//                                    that clears the highlight 6 s
//                                    after the TX / RX event, matching
//                                    the freedv_reporter_tx_rx_highlight
//                                    _time radio setting at its default
//                                    (G3 surfaces this as a user-visible
//                                    knob). NereusSDR-architectural
//                                    divergences vs freedv-gui:
//                                    (1) SNR -99 renders as " - " and
//                                    other SNR values render as a
//                                    signed integer "+12" / "-3"
//                                    (upstream renders ToString(snr, 1)
//                                    -> "12.0"). (2) Frequency renders
//                                    in MHz with 4 decimals; the kHz /
//                                    MHz toggle from the upstream
//                                    reportingFrequencyAsKhz setting
//                                    is deferred to G3. AI tooling:
//                                    Anthropic Claude Code.
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task G2. Filter / QSY /
//                                    message bar landed. New widgets:
//                                    band-filter combo + Track-frequency
//                                    radio pair + QSY freq edit + Send
//                                    QSY button + Open Website / OK
//                                    buttons (top row), MRU dropdown +
//                                    message edit + Send / Save / Clear
//                                    (bottom row). New signals
//                                    qsyRequested / messageSendRequested
//                                    / tuneRequested. Band filtering is
//                                    a QSortFilterProxyModel that maps
//                                    each row's frequency through
//                                    Band::bandFromFrequency() and
//                                    compares to the combo selection.
//                                    NereusSDR-architectural divergences
//                                    vs freedv-gui:
//                                    (1) NereusSDR exposes a 13-entry
//                                        band filter (All + 12 ham
//                                        bands), while upstream's
//                                        wxComboBox is also 13 entries
//                                        but rolls 6m+up into ">= 6 m"
//                                        and adds a separate "Other"
//                                        slot (freedv_reporter.cpp
//                                        :324-338 [@77e793a]). The
//                                        13-count is preserved.
//                                    (2) QSY frequency is typed by the
//                                        user in MHz (task spec); the
//                                        upstream button always uses
//                                        the radio's own current
//                                        reportingFrequency
//                                        (freedv_reporter.cpp:1085
//                                        [@77e793a]). H2 routes this
//                                        signal externally.
//                                    AI tooling: Anthropic Claude Code.

#pragma once

#include <QColor>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QWidget>

class QComboBox;
class QHBoxLayout;
class QItemSelection;
class QLineEdit;
class QMenuBar;
class QPushButton;
class QRadioButton;
class QSortFilterProxyModel;
class QTableView;
class QTimer;
class QVBoxLayout;

namespace NereusSDR {

class FreeDVReporterClient;
class FreeDVReporterTableModel;
class FreeDVReporterBandFilterProxy;
class FreeDVReporterRowHighlightDelegate;
class FreeDVStationModel;
struct FreeDVStation;

// Column index constants ported verbatim from
//   freedv-gui src/gui/dialogs/freedv_reporter.cpp:47-65 [@77e793a].
// Upstream uses #define; NereusSDR scopes them as constexpr inside an
// enum so the column count is a single source of truth.
enum FreeDVReporterColumn : int {
    kCallsignCol      = 0,
    kGridSquareCol    = 1,
    kDistanceCol      = 2,
    kHeadingCol       = 3,
    kVersionCol       = 4,
    kFrequencyCol     = 5,
    kTxModeCol        = 6,
    kStatusCol        = 7,
    kUserMessageCol   = 8,
    kLastTxDateCol    = 9,
    kLastRxCallsignCol = 10,
    kLastRxModeCol    = 11,
    kSnrCol           = 12,
    kLastUpdateDateCol = 13,
    kFreeDVReporterColumnCount = 14,
};

// FreeDV Reporter dialog window.
//
// Modeless. MainWindow keeps a single instance pointer and toggles
// visibility; `setAttribute(Qt::WA_DeleteOnClose, false)` in the
// constructor enforces that.
class FreeDVReporterDialog : public QWidget {
    Q_OBJECT

public:
    explicit FreeDVReporterDialog(FreeDVStationModel* stationModel,
                                  FreeDVReporterClient* client = nullptr,
                                  QWidget* parent = nullptr);
    ~FreeDVReporterDialog() override;

    // Test seam: drop the highlight-clear interval below the production
    // 6 s default so the unit test does not block on wall-clock time.
    void setHighlightClearMsForTest(int ms);

    // Test seam: read back the row highlight color for a given sid.
    // Returns an invalid QColor when no highlight is active. Mirrors
    // the upstream `reportData->backgroundColor` cell after
    // updateHighlights() in freedv_reporter.cpp:1304-1339 [@77e793a].
    QColor rowHighlightColorForTest(const QString& sid) const;

    // G2: test seam for the Open Website button. Replaces actually
    // calling QDesktopServices::openUrl from inside a unit test.
    // Production button click invokes openWebsite() which delegates
    // to websiteUrl() + QDesktopServices::openUrl.
    QUrl websiteUrl() const;

signals:
    // G2: QSY request - connected externally to FreeDVReporterClient
    //   ::requestQSY (wire shape from freedv-gui FreeDVReporter.cpp
    //   requestQSY at :104-119 [@77e793a]). MainWindow does the
    //   connect in H2.
    void qsyRequested(const QString& targetSid, quint64 freqHz,
                      const QString& message);

    // G2: Status message - connected externally to FreeDVReporterClient
    //   ::updateMessage (wire shape from freedv-gui FreeDVReporter.cpp
    //   updateMessage at :122-130 [@77e793a]). Empty text clears the
    //   remote message per upstream.
    void messageSendRequested(const QString& text);

    // G2: Optional - emitted when user double-clicks a row or hits
    //   Send QSY so MainWindow can tune the active slice to the
    //   target freq. Same shape as SpotHubDialog::tuneRequested.
    void tuneRequested(quint64 freqHz);

private slots:
    // FreeDVStationModel signal handlers. Each forwards into the
    // private QAbstractTableModel adapter and into applyHighlightFor
    // Station so the per-row TX-red / RX-green tint tracks the model.
    void onStationAdded(const QString& sid, const NereusSDR::FreeDVStation& info);
    void onStationUpdated(const QString& sid, const NereusSDR::FreeDVStation& info);
    void onStationRemoved(const QString& sid);
    void onCleared();

    // G2: bottom-bar action handlers.
    void onBandFilterChanged(int index);
    void onTableSelectionChanged(const QItemSelection& selected,
                                 const QItemSelection& deselected);
    void onQsySendClicked();
    void onOpenWebsiteClicked();
    void onMessageSendClicked();
    void onMessageSaveClicked();
    void onMessageClearClicked();
    void onMessageDropdownActivated(int index);

private:
    void buildUi();
    void buildBottomBar();
    void applyHighlightForStation(const QString& sid,
                                  const NereusSDR::FreeDVStation& info);
    void setHighlight(const QString& sid, const QColor& bg);
    void refreshDelegateSidOrder();

    // G2: AppSettings serialization helpers for the MRU message list.
    // Stored under "FreeDvReporter/SavedMessages" as a newline-joined
    // string per the task-spec convention.
    QStringList loadSavedMessages() const;
    void        saveSavedMessages(const QStringList& msgs) const;
    void        refreshMessageDropdown(const QStringList& msgs);

    // G2: resolve the sid of the currently selected table row, mapped
    // back through the band-filter proxy. Returns empty if no row is
    // selected.
    QString currentSelectedSid() const;

    FreeDVStationModel*               m_stationModel{nullptr};
    QPointer<FreeDVReporterClient>    m_client;  // reserved for H2 QSY wiring

    QMenuBar*                         m_menuBar{nullptr};       // G3 placeholder
    QTableView*                       m_table{nullptr};
    FreeDVReporterTableModel*         m_tableModel{nullptr};
    FreeDVReporterBandFilterProxy*    m_bandProxy{nullptr};
    FreeDVReporterRowHighlightDelegate* m_rowDelegate{nullptr};

    // G2 bottom-bar widgets.
    QWidget*      m_bottomBarHost{nullptr};
    QComboBox*    m_bandFilter{nullptr};
    QRadioButton* m_trackBandRadio{nullptr};
    QRadioButton* m_trackFreqRadio{nullptr};
    QLineEdit*    m_qsyFreq{nullptr};
    QPushButton*  m_qsySendButton{nullptr};
    QPushButton*  m_openWebsiteButton{nullptr};
    QPushButton*  m_closeButton{nullptr};
    QComboBox*    m_msgDropdown{nullptr};
    QLineEdit*    m_msgEdit{nullptr};
    QPushButton*  m_msgSendButton{nullptr};
    QPushButton*  m_msgSaveButton{nullptr};
    QPushButton*  m_msgClearButton{nullptr};

    // Per-sid clear timers. Each fires once m_highlightClearMs ms
    // after the TX / RX event, clearing the row tint. Replaces
    // upstream's 250 ms global timer + per-row time-threshold scan
    // (freedv_reporter.cpp:1289-1322 [@77e793a]).
    QHash<QString, QTimer*>           m_clearTimers;
    int                               m_highlightClearMs;
};

}  // namespace NereusSDR
