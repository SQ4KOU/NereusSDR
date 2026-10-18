// =================================================================
// src/gui/styles/WorkspaceLookStyle.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The workspace look's style object:
// what the look changes in the platform style's own drawing, for controls
// no stylesheet draws.
//
// The look is stylesheets (WorkspacePreview, WorkspaceComponents) on top
// of the Fusion style. A control no sheet styles is drawn by Fusion from
// the palette, and two of its marks cannot be reached through the palette
// or a sheet without changing the control's shape:
//
//   - the outline of a dialog's default button, and of a push button with
//     keyboard focus (Fusion: the palette Highlight darkened, #264c75 in
//     the look);
//   - the focus rectangle of the current cell in a list, table or tree
//     (Fusion: the same colour, translucent, over a translucent tint).
//
// JJ (2026-10-10, option 1): both are the selection rim, kGlassSelBorder,
// in the shape the platform style draws. LookStyle wraps Fusion, lets it
// draw, and draws the rim over Fusion's outline along the same path, so
// the button keeps its corner radius, size, fill and text and stays level
// with the Fusion-drawn buttons beside it.
//
// Buttons a stylesheet draws (the glass buttons of applets and panels)
// never reach this code: QStyleSheetStyle draws them itself.
//
// Everything branches on WorkspacePreview::enabled(): with the switch off
// install() does nothing, and an installed LookStyle draws exactly what
// Fusion draws.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-10 - Written by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include <QProxyStyle>

class QApplication;

namespace NereusSDR::Workspace {

class LookStyle : public QProxyStyle
{
    Q_OBJECT

public:
    // Wraps a new Fusion style, which it owns.
    LookStyle();

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter,
                       const QWidget* widget = nullptr) const override;

    // Makes a LookStyle the application's style, once. The application
    // owns it. Does nothing when the workspace preview switch is off.
    static void install(QApplication& app);
};

} // namespace NereusSDR::Workspace
