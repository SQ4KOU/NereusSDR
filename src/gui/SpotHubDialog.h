// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - SpotHub dialog: 9-tab strip aggregating six spot-ingest
// clients (Cluster / RBN / WSJT-X / SpotCollector / POTA / FreeDV /
// PSK Reporter), the cross-source Spot List table, and the Display
// preferences page.
//
// Ported from AetherSDR src/gui/DxClusterDialog.{h,cpp} [@0cd4559]
// (shell only; per-tab content lands in Phase 3J-2 Tasks F2-F4).
// AetherSDR is (C) its contributors and is licensed GPL-3.0-or-later
// (see https://github.com/ten9876/AetherSDR/blob/main/LICENSE).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task F1. Initial shell
//                                    port. Constructor wires the same
//                                    six client pointers AetherSDR
//                                    upstream takes, but the trailing
//                                    `RadioModel* radioModel` argument
//                                    is replaced with NereusSDR's
//                                    `SpotModel* spots` (the
//                                    TCI-keyed spot sink from Phase
//                                    3J-2 Task D1); routing of
//                                    tuneRequested() into RadioModel
//                                    happens in MainWindow when the
//                                    dialog is instantiated (Phase F
//                                    wire-up). Each build*Tab() is a
//                                    stub that adds a placeholder
//                                    QLabel; per-source tab content
//                                    lands in F2 (Cluster / RBN /
//                                    WSJT-X / SpotCollector / POTA /
//                                    FreeDV / PSK Reporter), F3 (Spot
//                                    List), and F4 (Display).
//                                    Replaces upstream's
//                                    HAVE_WEBSOCKETS-gated FreeDvClient
//                                    with the always-built
//                                    FreeDVReporterClient (NereusSDR
//                                    Task B5) - same Engine.IO /
//                                    Socket.IO contract, just no
//                                    compile-time gating. Adds a PSK
//                                    Reporter tab between FreeDV and
//                                    Spot List (NereusSDR has the
//                                    PskReporterClient from Phase
//                                    3J-2 Task B6; AetherSDR upstream
//                                    has no PSK Reporter tab).
//                                    AI tooling: Anthropic Claude
//                                    Code.

#pragma once

#include <QDialog>

class QTabWidget;

namespace NereusSDR {

class DxClusterClient;
class WsjtxClient;
class SpotCollectorClient;
class PotaClient;
class FreeDVReporterClient;
class PskReporterClient;
class SpotModel;
class DxccColorProvider;

// From AetherSDR src/gui/DxClusterDialog.h:79-215 [@0cd4559]
//
// SpotHubDialog: top-level QDialog containing a QTabWidget with
// nine tabs in upstream order (Cluster / RBN / WSJT-X /
// SpotCollector / POTA / FreeDV / PSK Reporter / Spot List /
// Display). The dialog is the modeless control surface for the
// six spot-ingest clients; each source has its own tab with
// connect/disconnect controls, a status LED, and a per-source
// console. The Spot List tab shows the merged 8-column table
// across all sources (NereusSDR SpotTableModel + BandFilterProxy
// from Phase 3J-2 Task D2). The Display tab holds DXCC
// color-coding configuration and global spot rendering toggles.
//
// F1 ships the SHELL only. Each `build<Source>Tab()` adds a
// placeholder QLabel. F2 (per-source tabs), F3 (Spot List),
// and F4 (Display) build the content.
class SpotHubDialog : public QDialog {
    Q_OBJECT

public:
    explicit SpotHubDialog(DxClusterClient* clusterClient,
                           DxClusterClient* rbnClient,
                           WsjtxClient* wsjtxClient,
                           SpotCollectorClient* spotCollectorClient,
                           PotaClient* potaClient,
                           FreeDVReporterClient* freedvClient,
                           PskReporterClient* pskClient,
                           SpotModel* spotModel,
                           DxccColorProvider* dxccProvider,
                           QWidget* parent = nullptr);

signals:
    // Forwarded from per-source tab controls in F2.
    void settingsChanged();
    void connectRequested(const QString& host, quint16 port, const QString& callsign);
    void disconnectRequested();
    void rbnConnectRequested(const QString& host, quint16 port, const QString& callsign);
    void rbnDisconnectRequested();
    void wsjtxStartRequested(const QString& address, quint16 port);
    void wsjtxStopRequested();
    void spotCollectorStartRequested(quint16 port);
    void spotCollectorStopRequested();
    void potaStartRequested(int intervalSec);
    void potaStopRequested();
    void freedvStartRequested();
    void freedvStopRequested();
    void pskStartRequested();
    void pskStopRequested();
    // Forwarded from the Spot List click-to-tune + the spot overlay.
    void tuneRequested(double freqMhz);
    // Forwarded from the Display tab's "Clear All Spots" button.
    void spotsClearedAll();

private:
    // Shell-only stubs in F1. F2-F4 fill these out.
    void buildClusterTab(QTabWidget* tabs);
    void buildRbnTab(QTabWidget* tabs);
    void buildWsjtxTab(QTabWidget* tabs);
    void buildSpotCollectorTab(QTabWidget* tabs);
    void buildPotaTab(QTabWidget* tabs);
    void buildFreeDvTab(QTabWidget* tabs);
    void buildPskTab(QTabWidget* tabs);
    void buildSpotListTab(QTabWidget* tabs);
    void buildDisplayTab(QTabWidget* tabs);

    // Held by pointer; ownership stays with the caller (RadioModel /
    // MainWindow in production, the test fixture in unit tests).
    DxClusterClient*      m_clusterClient{nullptr};
    DxClusterClient*      m_rbnClient{nullptr};
    WsjtxClient*          m_wsjtxClient{nullptr};
    SpotCollectorClient*  m_spotCollectorClient{nullptr};
    PotaClient*           m_potaClient{nullptr};
    FreeDVReporterClient* m_freedvClient{nullptr};
    PskReporterClient*    m_pskClient{nullptr};
    SpotModel*            m_spotModel{nullptr};
    DxccColorProvider*    m_dxccProvider{nullptr};
};

} // namespace NereusSDR
