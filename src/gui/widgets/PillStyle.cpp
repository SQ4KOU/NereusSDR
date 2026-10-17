// src/gui/widgets/PillStyle.cpp

// --- From src/gui/widgets/CommandBar.h ---
// =================================================================
// src/gui/widgets/CommandBar.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Die Kopfleiste ───────────────────────────────────────────────────
//
// Der größte Einzelunterschied zwischen Longpath und der Vorlage des
// Betreibers. In der Vorlage liegen MODE, FILTER, BAND, FAVORITES, STEP und
// FRONT-END als beschriftete Pillengruppen quer über das Fenster; hier
// stand bisher links eine Spalte senkrechter Knöpfe und der Rest
// verteilt in den Panels.
//
// Aufgabe #7 der Projektliste behauptete, so eine Leiste sei gebaut.
// Am 2026-08-15 nachgesehen: im Quellbaum existierte kein solches
// Widget. Der Eintrag war falsch.
//
// ── Was die Leiste ausmacht, und was nicht ───────────────────────────
//
// Nicht die Farben — die sind seit heute Nacht in Ordnung. Es ist die
// FORM:
//
//   · Über jeder Knopfreihe eine Versalzeile mit weiter Laufweite, in
//     Skalenfarbe. Ohne sie ist eine Reihe Pillen nur eine Reihe
//     Pillen; mit ihr ist sie eine benannte Gruppe.
//
//   · Drei sichtbare Einträge, dann ein „…". Nicht dreizehn
//     nebeneinander — das ist die Filterwand im RX-Panel, und die liest
//     niemand, der sie nicht auswendig kann.
//
//   · Genau einer je Gruppe leuchtet. Der Rest ist fast unsichtbar.
//
//   · Radius 6, Höhe 27. Radius 3 ist der Qt-Standardwert und lässt
//     alles nach Qt aussehen.
//
// ── Was bewusst fehlt ────────────────────────────────────────────────
//
// BAND, FILTER und FAVORITES sind nicht dabei. Nicht aus Faulheit: bei
// denen ist die Frage „welche drei stehen vorne" eine Entscheidung des
// Betreibers, keine des Programms. Die drei letzten Bänder? Die
// Favoriten? Die des aktuellen Antennenanschlusses?
//
// MODE und STEP haben diese Frage nicht — da gibt es eine natürliche
// Auswahl und einen vollständigen Rest im Menü. Also sind sie zuerst
// da, und die Leiste beweist ihre Form, bevor jemand über Bandlisten
// entscheiden muss.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-15 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
//   2026-09-28 -- the filter "…" reads FilterPresetStore (Thetis names +
//                 the operator's own presets), Martin Fischer, OE5SOS,
//                 AI-assisted via Anthropic Claude.
// =================================================================

// --- From src/gui/widgets/CommandBar.cpp ---
// =================================================================
// src/gui/widgets/CommandBar.cpp  (Longpath)
// =================================================================
//
// Longpath-original. See CommandBar.h for what this is and why the
// band and filter groups are deliberately absent.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-15 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
//   2026-09-28 -- the filter "…" reads FilterPresetStore (Thetis names +
//                 the operator's own presets), Martin Fischer, OE5SOS,
//                 AI-assisted via Anthropic Claude.
//   2026-09-28 -- the running-filter pill says the preset name when the
//                 filter is a preset (same rule as the RX applet).
// =================================================================

// =================================================================
// src/gui/widgets/PillStyle.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/CommandBar.h
//   src/gui/widgets/CommandBar.cpp (CommandBar::pillStyle() only)
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR. Style:: resolves to the
//   Longpath values in WorkspacePalette and WorkspaceStyle through
//   gui/instruments/InstrumentStyle.h, never to NereusSDR::Style in
//   gui/StyleConstants.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/widgets/PillStyle.h"

#include "gui/instruments/InstrumentStyle.h"

#include <QColor>

namespace NereusSDR {

// NereusSDR: the Longpath values, see gui/instruments/InstrumentStyle.h.
namespace Style = ::NereusSDR::InstrumentStyle;

/// Das Aussehen einer Pille. Über Style::-Konstanten, damit die
/// Theme-Datei sie erreicht. Oeffentlich, weil die untere Leiste
/// dieselbe Optik traegt.
// From Longpath src/gui/widgets/CommandBar.cpp:59-98 [@551576e3]
QString PillStyle::pillStyle()
{
    // Glas & Tiefe (2026-09-17, Stilblatt 3): die Pille ist ERHABEN —
    // Verlauf, Lichtkante oben, dunkle Unterkante — und die aktive
    // traegt den gedeckten Auswahlverlauf statt des flachen kBlueBg.
    // Das nimmt der Kopfleiste die Blau-Summe (fuenf leuchtende Knoepfe
    // in einer Reihe), ohne dass die Auswahl leiser wuerde.
    return QStringLiteral(
        "QPushButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "               stop:0 %1, stop:1 %10);"
        "  border: 1px solid %2; border-radius: %6px;"
        "  border-top-color: %11; border-bottom-color: %12;"
        "  color: %3; font-size: 11px; font-weight: 600;"
        "  padding: 0 11px; min-height: %5px; max-height: %5px;"
        "}"
        "QPushButton:hover { background: %4; }"
        // Genau einer je Gruppe leuchtet. Der Rest ist fast unsichtbar —
        // das ist der Unterschied zwischen einer Leiste und einer Wand.
        "QPushButton:checked {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "               stop:0 %7, stop:1 %13);"
        "  border: 1px solid %8; border-top-color: %14; color: %9;"
        "}")
        .arg(QString::fromLatin1(Style::kGlassBtnTop),
             QString::fromLatin1(Style::kBorder),
             QString::fromLatin1(Style::kTextSecondary),
             QString::fromLatin1(Style::kButtonHover))
        .arg(PillStyle::kPillHeight)
        .arg(PillStyle::kPillRadius)
        .arg(QString::fromLatin1(Style::kGlassSelTop),
             QString::fromLatin1(Style::kGlassSelBorder),
             QString::fromLatin1(Style::kGlassSelText))
        .arg(QString::fromLatin1(Style::kGlassBtnBot),
             Style::shiftL(QColor(Style::kBorder), 12),
             Style::shiftL(QColor(Style::kBorder), -14),
             QString::fromLatin1(Style::kGlassSelBot),
             Style::shiftL(QColor(Style::kGlassSelBorder), 16));
}

} // namespace NereusSDR
