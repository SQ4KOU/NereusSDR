// =================================================================
// src/gui/styles/StylesheetColourTable.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The decision for every stylesheet
// colour literal in src/gui that the legacy table in WorkspacePalette
// does not cover: a row that names an existing palette role, or a
// place on the kept list with its reason.
//
// Rows are explicit. Each was checked against its use sites; there is
// no colour-distance rule. A row may name the property kind it was
// checked for ("background", "border" or "text"); the translator then
// applies it only there, so one literal can do different jobs in
// different properties. An empty kind applies to every property.
//
// The kept list names literals the translator leaves as written, with
// the reason. It wins over every row, so a literal on it is never
// rewritten. Its keys are the literal as the translator reports it:
// hex in lower case with its digits as written ("#555", "#ffffff"),
// rgb()/rgba() in lower case without spaces, colour names in lower
// case.
//
// Only reachable through the workspace preview switch: the translator
// runs only when WorkspacePreview::enabled().
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Written by J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include <QVector>

namespace NereusSDR::Workspace::StylesheetColours {

// A literal, as lower-case "#rrggbb", and the role it becomes for the
// given property kind (nullptr or "" means every property).
struct ColourRow {
    const char* hex;
    const char* kind;
    const char* role;
};

// A literal left as written, as the translator reports it, for the
// given property kind (nullptr or "" means every property).
struct KeptLiteral {
    const char* literal;
    const char* kind;
    const char* reason;
};

const QVector<ColourRow>& rows();
const QVector<KeptLiteral>& kept();

} // namespace NereusSDR::Workspace::StylesheetColours
