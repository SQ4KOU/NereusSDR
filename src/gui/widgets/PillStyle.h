// src/gui/widgets/PillStyle.h

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

// =================================================================
// src/gui/widgets/PillStyle.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/CommandBar.h (pillStyle, kPillHeight, kPillRadius only)
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banner is copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR. CommandBar itself is not
//   ported: only the pill look the profile rail shares with it, as the
//   free functions and constants of namespace PillStyle.
//   CommandBar::pillStyle() becomes PillStyle::pillStyle().
//   Workspace preview only.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QString>

namespace NereusSDR::PillStyle {

// Die Pillen-Optik der oberen Leiste — oeffentlich, seit die UNTERE
// Leiste sie auch benutzt (2026-08-19, auf Ansage des Betreibers:
// „die Taskleiste unten sollte auch wie die Taskleiste oben
// aussehen, gleiches Design").
//
// Eine Funktion statt zweier Stylesheets: zwei Leisten, die
// „gleich" aussehen sollen und ihre Farben getrennt fuehren, sehen
// nach dem naechsten Feinschliff wieder verschieden aus.
// From Longpath src/gui/widgets/CommandBar.h:167-175 [@551576e3]
QString pillStyle();

// From Longpath src/gui/widgets/CommandBar.h:182 [@551576e3]
inline constexpr int kPillHeight = 27;
// From Longpath src/gui/widgets/CommandBar.h:183 [@551576e3]
inline constexpr int kPillRadius = 6;

} // namespace NereusSDR::PillStyle
