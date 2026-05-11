// SPDX-License-Identifier: GPL-3.0-or-later
//
// NereusSDR - SpotHub dialog (shell only): see SpotHubDialog.h for
// the full port-citation header and the F1 / F2 / F3 / F4 task
// breakdown.
//
// Ported from AetherSDR src/gui/DxClusterDialog.cpp [@0cd4559]
// (shell only; per-tab content lands in Phase 3J-2 Tasks F2-F4).
//
// Modification history (NereusSDR):
//   2026-05-11  J.J. Boyd / KG4VCF  Phase 3J-2 Task F1. Initial
//                                    shell port; see header for full
//                                    notes. AI tooling: Anthropic
//                                    Claude Code.

#include "SpotHubDialog.h"

#include <QLabel>
#include <QTabWidget>
#include <QVBoxLayout>

namespace NereusSDR {

// From AetherSDR src/gui/DxClusterDialog.cpp:230-273 [@0cd4559].
// NereusSDR divergence: AetherSDR upstream takes a trailing
// `RadioModel* radioModel` argument; NereusSDR replaces it with the
// TCI-keyed `SpotModel* spots` (Phase 3J-2 Task D1). Routing of
// `tuneRequested(double)` into the active RadioModel happens in
// MainWindow when the dialog is instantiated. Replaces upstream's
// HAVE_WEBSOCKETS-gated FreeDvClient with the always-built
// FreeDVReporterClient (NereusSDR Task B5). Adds a PSK Reporter
// tab between FreeDV and Spot List (NereusSDR-only). The pane
// stylesheet (panel border + tab colors) follows upstream verbatim.
SpotHubDialog::SpotHubDialog(DxClusterClient* clusterClient,
                             DxClusterClient* rbnClient,
                             WsjtxClient* wsjtxClient,
                             SpotCollectorClient* spotCollectorClient,
                             PotaClient* potaClient,
                             FreeDVReporterClient* freedvClient,
                             PskReporterClient* pskClient,
                             SpotModel* spotModel,
                             DxccColorProvider* dxccProvider,
                             QWidget* parent)
    : QDialog(parent),
      m_clusterClient(clusterClient),
      m_rbnClient(rbnClient),
      m_wsjtxClient(wsjtxClient),
      m_spotCollectorClient(spotCollectorClient),
      m_potaClient(potaClient),
      m_freedvClient(freedvClient),
      m_pskClient(pskClient),
      m_spotModel(spotModel),
      m_dxccProvider(dxccProvider)
{
    setWindowTitle("SpotHub");
    setMinimumSize(680, 560);
    resize(760, 640);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(0);
    root->setContentsMargins(4, 4, 4, 4);

    auto* tabs = new QTabWidget;
    tabs->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #203040; }"
        "QTabBar::tab { background: #1a1a2e; color: #808890; border: 1px solid #203040; "
        "  padding: 6px 16px; margin-right: 2px; }"
        "QTabBar::tab:selected { background: #0f0f1a; color: #00b4d8; border-bottom: none; }");

    // Tab order matches AetherSDR upstream
    // (src/gui/DxClusterDialog.cpp:262-271). NereusSDR adds the PSK
    // Reporter tab between FreeDV and Spot List (Task F2 wires its
    // content). FreeDV tab is unconditional in NereusSDR; upstream
    // gated it on HAVE_WEBSOCKETS.
    buildClusterTab(tabs);
    buildRbnTab(tabs);
    buildWsjtxTab(tabs);
    buildSpotCollectorTab(tabs);
    buildPotaTab(tabs);
    buildFreeDvTab(tabs);
    buildPskTab(tabs);
    buildSpotListTab(tabs);
    buildDisplayTab(tabs);

    root->addWidget(tabs);
}

// F1 stubs - each adds a placeholder QLabel so the QTabWidget has a
// child page to host. F2 (per-source tabs), F3 (Spot List), and F4
// (Display) replace each stub body.

void SpotHubDialog::buildClusterTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Cluster tab content lands in Task F2.", page));
    tabs->addTab(page, "Cluster");
}

void SpotHubDialog::buildRbnTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("RBN tab content lands in Task F2.", page));
    tabs->addTab(page, "RBN");
}

void SpotHubDialog::buildWsjtxTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("WSJT-X tab content lands in Task F2.", page));
    tabs->addTab(page, "WSJT-X");
}

void SpotHubDialog::buildSpotCollectorTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("SpotCollector tab content lands in Task F2.", page));
    tabs->addTab(page, "SpotCollector");
}

void SpotHubDialog::buildPotaTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("POTA tab content lands in Task F2.", page));
    tabs->addTab(page, "POTA");
}

void SpotHubDialog::buildFreeDvTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("FreeDV tab content lands in Task F2.", page));
    tabs->addTab(page, "FreeDV");
}

void SpotHubDialog::buildPskTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("PSK Reporter tab content lands in Task F2.", page));
    tabs->addTab(page, "PSK Reporter");
}

void SpotHubDialog::buildSpotListTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Spot List tab content lands in Task F3.", page));
    tabs->addTab(page, "Spot List");
}

void SpotHubDialog::buildDisplayTab(QTabWidget* tabs)
{
    auto* page = new QWidget;
    auto* lay = new QVBoxLayout(page);
    lay->addWidget(new QLabel("Display tab content lands in Task F4.", page));
    tabs->addTab(page, "Display");
}

} // namespace NereusSDR
