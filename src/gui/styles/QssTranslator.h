// =================================================================
// src/gui/styles/QssTranslator.h  (NereusSDR)
// =================================================================
//
// no-port-check: NereusSDR-original. The restricted compatibility
// translator from the Longpath theme design (LT "Theme interfaces and
// migration"). Its legacy-hex table comes from WorkspacePalette, which
// carries the Longpath attribution; the algorithm here is NereusSDR's.
//
// Rewrites legacy colour literals in a retained stylesheet into the
// Glass and Depth palette:
//   - every colour form a stylesheet can hold is read through one
//     lookup: '#' with 3, 4, 6 or 8 hex digits (Qt's order: the 4- and
//     8-digit forms carry the alpha first, #ARGB and #AARRGGBB),
//     rgb() and rgba() with 0-255 or percent components, and colour
//     names in a colour property. Each is looked up by its "#rrggbb";
//   - alpha in the source survives: an 8- or 4-digit token becomes
//     "#AARRGGBB" with the source alpha, rgba() keeps its alpha
//     argument exactly as written; opaque forms become the role's
//     "#rrggbb". "transparent" and "none" stay as written;
//   - a token is '#' followed by a run of [0-9A-Za-z_-]; a run that is
//     not 3, 4, 6 or 8 hex digits ("#2050701") is not a colour at all;
//   - text inside url(...), quoted strings and /* comments */ is copied
//     unchanged;
//   - when the sheet has braces, only declarations inside { } are
//     rewritten, so selectors (including "#objectName") are preserved;
//     a sheet without braces is a list of declarations;
//   - the lookup, in order: Options::explicitRoles; the kept list (left
//     as written); the #203040 context rule; a row for the property's
//     kind (background, border or text); a row for every property;
//   - colour literals without a row stay as they are and are reported,
//     hex in lower case as written, rgb()/rgba() in lower case without
//     spaces, names in lower case;
//   - one left-to-right pass, at most one rewrite per token.
//
// Difference from Longpath's Style::themed(): Longpath adds a row for
// every current value so that themed(themed(s)) == themed(s). NereusSDR
// does not; a target value may also be a source key, so translating an
// already-translated sheet can move a colour twice. Always translate
// the retained original, never widget->styleSheet() after theming
// (WorkspacePreview::themeSubtree keeps the original for this).
//
// #203040 has no table row. Callers name its role through
// Options::explicitRoles, or ask for the context rule
// (Options::resolveAmbiguousByContext): a slider groove or page gets
// "groove", a :hover/:pressed rule's background gets "button-hover",
// everything else "border-subtle".
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Written for the workspace preview by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Every colour form through one lookup, rows by
//                 property kind, and the kept list.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Optional font-size floor, J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
// =================================================================

#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace NereusSDR::QssTranslator {

// One mapping: a lower-case "#rrggbb" literal and the palette role it
// becomes. `kind` is empty for every property, or the property kind
// the row applies to: "background", "border" or "text".
struct Rule {
    QString legacyHex;
    QString role;
    QString kind;
};
using Table = QVector<Rule>;

// The table built from WorkspacePalette::legacyRows() followed by
// Workspace::StylesheetColours::rows().
const Table& defaultTable();

// A literal left as written, as the translator reports it, for one
// property kind or (empty) every property.
struct Keep {
    QString literal;
    QString kind;
};
using KeptList = QVector<Keep>;

// Built from Workspace::StylesheetColours::kept().
const KeptList& defaultKept();

// "background", "border", "text" or empty for a property name.
QString propertyKind(const QString& property);

// The legacy colour with no blanket row.
inline constexpr auto kAmbiguousHex = "#203040";

struct Options {
    // Opt-in logical-pixel floor for unquoted font-size px/pt values.
    // Zero keeps typography unchanged. Point values use the caller's
    // logical font DPI (device pixel ratio is not a font-size unit).
    bool translateColours = true;
    int minimumTextPixelSize = 0;
    qreal textLogicalDpi = 96.0;
    // Table to use; nullptr means defaultTable().
    const Table* table = nullptr;
    // Kept list to use; nullptr means defaultKept().
    const KeptList* kept = nullptr;
    // Per call site: lower-case legacy hex to role. Wins over the table.
    QHash<QString, QString> explicitRoles;
    // Resolve kAmbiguousHex by the selector and property it sits in.
    bool resolveAmbiguousByContext = false;
};

struct Result {
    QString qss;
    // Colour literals left as they were, normalised as described
    // above, one entry per occurrence, in source order.
    QStringList unknownLiterals;
    int rewritten = 0;
};

Result translate(const QString& original, const Options& options = {});

// Shorthand: translate with one explicit role for #203040.
Result translateWithAmbiguousRole(const QString& original, const QString& roleFor203040);

} // namespace NereusSDR::QssTranslator
