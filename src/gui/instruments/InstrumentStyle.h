// =================================================================
// src/gui/instruments/InstrumentStyle.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original style shim for the ported Longpath
// instruments. It ports nothing; the values it exposes are the Task 2
// WorkspacePalette and WorkspaceStyle ports, which carry the Longpath
// attribution.
//
// The Longpath instruments write Style::kAmberText, Style::monoFont,
// Style::role and Style::themed. NereusSDR has its own NereusSDR::Style
// (gui/StyleConstants.h) with several same-named symbols at different
// values. Each ported instrument .cpp declares
//
//     namespace Style = ::NereusSDR::InstrumentStyle;
//
// inside namespace NereusSDR, so every Style:: name there resolves to
// the Longpath values below. If StyleConstants.h ever reaches one of
// those translation units the alias stops compiling instead of
// silently picking the other values.
//
// themed() is the identity. Longpath's Style::themed() maps legacy
// colour literals to the active theme; under Longpath's built-in theme
// the instruments' literals are already the theme's values, so the
// identity is the faithful equivalent. The workspace QssTranslator is
// not used here: it would remap the Longpath values a second time.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace visual review by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspaceStyle.h"

#include <QString>

namespace NereusSDR::InstrumentStyle {

using namespace ::NereusSDR::Workspace;

// Identity: see the file header.
inline QString themed(const QString& qss)
{
    return qss;
}

} // namespace NereusSDR::InstrumentStyle
