// SPDX-License-Identifier: GPL-3.0-or-later
// Modification history (NereusSDR):
// 2026-10-11  J.J. Boyd / KG4VCF; original waveform observation. AI tooling: OpenAI Codex.
// 2026-10-11  J.J. Boyd / KG4VCF; the colors come from the workspace palette roles
//             instead of copied literals. Same values. AI tooling: Anthropic Claude Code.
#pragma once
// no-port-check: Original semantic adapter. It ports nothing: each color is
// read by role from WorkspacePalette (gui/styles), which carries the Longpath
// attribution, with the palette constant as the fallback.
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspaceStyle.h"

#include <QColor>
namespace NereusSDR::WaveformMeterStyle {
inline QColor panel()
{
    return QColor(Workspace::role("panel", Workspace::kPanelBg));
}
inline QColor inset()
{
    return QColor(Workspace::role("inset", Workspace::kInsetBg));
}
inline QColor border()
{
    return QColor(Workspace::role("inset-border", Workspace::kInsetBorder));
}
inline QColor text()
{
    return QColor(Workspace::role("text", Workspace::kTextPrimary));
}
inline QColor secondary()
{
    return QColor(Workspace::role("text-secondary", Workspace::kTextSecondary));
}
inline QColor scale()
{
    return QColor(Workspace::role("text-scale", Workspace::kTextScale));
}
// The palette's grid color at the waveform face's own opacity.
constexpr int kGridAlpha = 48;
inline QColor grid()
{
    QColor color(Workspace::role("spectrum-grid", Workspace::kSpectrumGrid));
    color.setAlpha(kGridAlpha);
    return color;
}
// Accepted original waveform mockup trace color.
inline QColor trace()
{
    return QColor("#7fae91");
}
} // namespace NereusSDR::WaveformMeterStyle
