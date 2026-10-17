// no-port-check: NereusSDR-original visual preview tool; controls comparison sheet.
// =================================================================
// tests/visual/ControlsSheet.h  (NereusSDR)
// =================================================================
//
// The controls-sheet scene: the same real Qt controls, at the same sizes,
// in three looks side by side. Today is the shipped look (switch off).
// Preview now is the workspace preview as it ships (QssTranslator and
// Workspace::Components over today's stylesheets). Longpath is Longpath's
// own control stylesheets (LongpathControlStyles.h). Real NereusSDR panel
// slider rows are cropped from the main window and the TX EQ dialog in
// each look. Writes controls-sheet and controls-sheet--zoom.
//
// Modification history (NereusSDR):
//   2026-10-08 - Written for the workspace visual review. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - measurePanelFit and appendPanelFitCrop for the strip-fit
//                 scenes (applet minimum width against the Main Panel
//                 viewport). J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - sceneIndicatorChoices, sceneLitButtons and
//                 sceneControlSizes: candidate looks for JJ to pick from.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
//   2026-10-08 - measureAppletFit for every container holding applets.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
//   2026-10-08 - sceneStatusColourChoices and sceneDataColourChoices: the
//                 status and data colour choices, rendered from real
//                 widgets through WorkspacePreview's test seams. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - The decided colours: sceneStatusColoursDecided
//                 (decided--status, with the pulse's two steps) and
//                 sceneDataColoursDecided (decided--data, with a user
//                 container background and user item colours) replace the
//                 choice sheets. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
//   2026-10-08 - sceneControlsDecided (decided--controls + --zoom). J.J.
//                 Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

class QWidget;

namespace NereusSDR::Preview {

struct SceneContext;

// Runs after the two-band window pose (runWindowScene). Leaves the app in
// today's look with the workspace preview switch off.
void sceneControlsSheet(SceneContext& ctx);

// Every scroll area under the Main Panel container, and every applet in
// it: width, minimum width and how much of it falls outside the scroll
// viewport, in logical px. `look` names the look in the result.
QJsonObject measurePanelFit(SceneContext& ctx, const QString& look);

// Every visible container (docked, edge strip or floating) that holds
// applets: its applet count, viewport width and the applet px that fall
// outside the viewport or exceed it by minimum width.
QJsonArray measureAppletFit(QWidget* window);

// Appends `<scene>--panel`: the Main Panel container cut from the last
// window image, with `fit` (from measurePanelFit) in its sidecar, and
// stores `fit` on the window image too.
void appendPanelFitCrop(SceneContext& ctx, const QString& scene, const QJsonObject& fit);

// Choice sheets: one row per candidate look, real widgets in every cell.
// Each leaves the app in today's look with the preview switch off.
// controls-sheet--indicators (+ --zoom): checkbox and radio off, on,
// disabled off and disabled on at 16 px, five candidates, on the Main
// Panel and Setup page backgrounds.
void sceneIndicatorChoices(SceneContext& ctx);
// controls-sheet--lit-buttons (+ --zoom): checked push buttons, today's
// three meanings, Longpath's tones for the same three, and Longpath's one
// blue; resting and hovered.
void sceneLitButtons(SceneContext& ctx);
// controls-sheet--lit-green (+ --zoom): Longpath's flat green and three
// stronger raised greens built from palette roles, each next to the
// Longpath blue and amber and one unlit button; resting.
void sceneLitGreenChoices(SceneContext& ctx);
// controls-sheet--scrollbars (+ --zoom): Longpath's scroll bar at 8, 10
// and 14 px on a scrolled area the size of the Main Panel's; resting and
// handle hovered. `liveBars` (the window's and Setup's real bars, measured
// by the caller) goes into the sidecar.
void sceneScrollBarChoices(SceneContext& ctx, const QJsonObject& liveBars);
// controls-sheet--sizes: combo, line edit, spin box, tabs and push button
// at Longpath's sizes, and at today's with Longpath shading (today's and
// height-scaled corners).
void sceneControlSizes(SceneContext& ctx);

// The status and data colours JJ decided, with the workspace preview on.
// decided--status (+ --zoom, 4x): the title bar status area in four
// states, the dot's two pulse steps for Connected and Link lost, the status
// badges, the TCI Server page's status and Network Diagnostics' Connection
// rows.
void sceneStatusColoursDecided(SceneContext& ctx);
// decided--data (+ --crops): the posed window (both pans, the top and left
// meter strips, the Longpath needle, the Main Panel's dial and filter
// passband), a time-series graph and the parametric EQ; then a user
// background on the Levels container, and user item colours on SIGNALAVG
// inside a Meters container with a user background. Call after the window
// is posed with the Meters, Levels and Needle containers.
void sceneDataColoursDecided(SceneContext& ctx);
// decided--controls (+ --zoom): JJ's decided control look through the
// product path with the preview on: sliders (resting, hover, pressed,
// disabled), check boxes and radios (off, on, disabled off, disabled on),
// the seven lit buttons resting and hovered, combo, line edit, spin box,
// tabs and push button at today's sizes, and the app sheet's scroll bar.
// Cells have the approved choice cells' sizes and backgrounds.
void sceneControlsDecided(SceneContext& ctx);

} // namespace NereusSDR::Preview
