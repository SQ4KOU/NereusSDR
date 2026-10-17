// src/gui/styles/WorkspaceStyle.h

// --- From src/gui/StyleConstants.h ---
// =================================================================
// src/gui/StyleConstants.h  (Longpath)
// =================================================================
//
// Source attribution (AetherSDR — GPLv3):
//
//   Copyright (C) 2024-2026  Jeremy (KK7GWY) / AetherSDR contributors
//       — per https://github.com/ten9876/AetherSDR (GPLv3; see LICENSE
//       and About dialog for the live contributor list)
//
//   This file is a port or structural derivative of AetherSDR source.
//   AetherSDR is licensed under the GNU General Public License v3.
//   Longpath is also GPLv3. Attribution follows GPLv3 §5 requirements.
//
// =================================================================
// Modification history (Longpath):
//   2026-04-16 — Ported/adapted in C++20/Qt6 for NereusSDR by
//                 J.J. Boyd (KG4VCF), with AI-assisted transformation
//                 via Anthropic Claude Code.
//                 Theme palette imported from AetherSDR `src/gui/ComboStyle.h` /
//                 `HGauge.h` / `SliceColors.h` and inline QColor calls
//                 in `MainWindow.cpp` / `VfoWidget.cpp`.
// =================================================================

// --- From src/gui/styles/ThemeQss.cpp ---
// =================================================================
// src/gui/styles/ThemeQss.cpp  (Longpath)
// =================================================================
//
// Longpath-original. See ThemeQss.h for why this exists and Theme.h
// for the layer it belongs to.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-15 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
//   2026-08-15 — Rollen ergänzt; die Tabelle fragt jetzt Theme, damit
//                 die Palette aus einer Datei außerhalb des Repos
//                 kommen kann.
// =================================================================

// =================================================================
// src/gui/styles/WorkspaceStyle.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/StyleConstants.h (font ladder, capsFont, monoFont,
//   paintGlassChip, paintInsetTop, glassPanelFill, shiftL,
//   titleBarStyle, kScrollBarStyle) and src/gui/styles/ThemeQss.cpp
//   (role) at revision
//   551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   src/gui/StyleConstants.h carries the AetherSDR copyright and
//   GPLv3 notice reproduced above; src/gui/styles/ThemeQss.cpp has
//   no copyright or GPL header. Longpath's project LICENSE is GPLv3.
//
//   Longpath's LP_MONO_QSS #define is not ported; kMonoQssFamilies is
//   the constexpr equivalent.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 role() looks up WorkspacePalette instead of
//                 Longpath's runtime Theme.
//   2026-10-08 - Upstream comments restored on the font ladder, caps
//                 tracking and mono families; the licence note corrected;
//                 the shiftL() block cite covers its section title.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - scrollBarStyle(): kScrollBarStyle at a given thickness.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QColor>
#include <QFont>
#include <QString>

class QPainter;
class QRect;

namespace NereusSDR::Workspace {

// ── Font size ladder (pixels) ────────────────────────────────────────
// ── Sechs Schriftstufen ─────────────────────────────────────────────
//
// OE5SOS, 2026-08-18: „sechs Stufen, QFont::setLetterSpacing, benannte
// Konstanten, vorher zaehlen was es schon gibt."
//
// GEZAEHLT am 2026-08-18: 641 Vorkommen in 18 verschiedenen Groessen
// (60 x setPixelSize, 581 x font-size im Stylesheet). Die haeufigsten
// sind 11 (232), 10 (186), 9 (69), 12 (72) — vier Stufen, die kaum
// auseinanderliegen und offensichtlich nicht entschieden, sondern
// gewachsen sind.
//
// Die Leiter unten deckte am 2026-08-18 zunaechst nur die drei
// Instrumente ab, absichtlich NICHT den ganzen Baum: 641 Stellen blind
// zu ersetzen waere derselbe Fehler wie die Palettenumstellung ohne
// Abbildungstabelle. Wer sie ausweitet, tut es Datei fuer Datei.
//
// UPDATE 2026-08-21: genau diese Datei-fuer-Datei-Ausweitung ist seither
// passiert — der ganze Baum wurde auf die Leiter zurueckgeholt (zwoelf
// abweichende Schriftgroessen auf null). scripts/verify-style-drift.py
// haelt seitdem den GANZEN Baum auf 0 abweichende Groessen fest (Decke
// in docs/design/style-drift-baseline.json), nicht nur die drei
// Instrumente. Wachsen dort neue Abweichungen (z.B. 2026-09-02: 20 neue
// Stellen in KiwiSDR-/Chrome-/Applet-Dateien, seither wieder auf die
// Leiter geholt), sind das keine neuen "erlaubten" Stellen ausserhalb
// der Leiter, sondern Drift, die dieselbe Datei-fuer-Datei-Pruefung
// braucht wie die urspruengliche Migration — kein blindes Ersetzen.
//
// Die Zahlen sind ein Vorschlag mit einer Stelle, an der man ihn
// aendert. Herkunft:
//   Micro    7  — die kleinsten Panadapter-Beschriftungen (dBm-Skala,
//                 Wasserfall-"LIVE"-Chip, Wasserfall-Zeitachse):
//                 setPointSize(7) am 2026-09-24 auf setPixelSize(7)
//                 umgestellt (Betreiber: die Schrift des Panadapters
//                 soll pixelfest sein, nicht DPI-abhaengig), damit
//                 sichtbar auf die Leiter genommen statt als
//                 Ausnahme durchgerutscht
//   Caption  9  — HAUSSTIL.md §Die acht Regeln, Regel 1: 8-9 px
//   Small   11  — die haeufigste Groesse im Baum
//   Body    13  — die VFO-Zeile des Frequenz-Widgets
//   Sub     16  — bisher duenn belegt (8 Vorkommen), fuellt die Luecke
//   Reading 22  — der abgelesene Wert in der Instrumenten-Fusszeile
//   Display 38  — die Frequenz selbst
//   Alarm   48  — die "HIGH SWR"-Warnung auf dem Panadapter: dieselbe
//                 2026-09-24-Umstellung, derselbe Grund
constexpr int kFontMicro   = 7;    // From Longpath src/gui/StyleConstants.h:164 [@551576e3]
constexpr int kFontCaption = 9;    // From Longpath src/gui/StyleConstants.h:165 [@551576e3]
constexpr int kFontSmall   = 11;   // From Longpath src/gui/StyleConstants.h:166 [@551576e3]
constexpr int kFontBody    = 13;   // From Longpath src/gui/StyleConstants.h:167 [@551576e3]
constexpr int kFontSub     = 16;   // From Longpath src/gui/StyleConstants.h:168 [@551576e3]
constexpr int kFontReading = 22;   // From Longpath src/gui/StyleConstants.h:169 [@551576e3]
constexpr int kFontDisplay = 38;   // From Longpath src/gui/StyleConstants.h:170 [@551576e3]
constexpr int kFontAlarm   = 48;   // From Longpath src/gui/StyleConstants.h:171 [@551576e3]

/// Laufweite der Versalzeile, als Anteil der Schriftgroesse.
/// HAUSSTIL.md §Die acht Regeln: „.18em".
///
/// Sie MUSS ueber QFont::setLetterSpacing gesetzt werden.
/// Qt-Stylesheets kennen kein letter-spacing und verwerfen es
/// kommentarlos — das steht so in der Uebergabe vom 2026-08-15 und hat
/// dort schon einmal eine Stunde gekostet.
constexpr double kCapsTracking = 0.18;   // From Longpath src/gui/StyleConstants.h:180 [@551576e3]

/// Dieselbe Schriftwahl wie monoFont(), aber fuer Stilvorlagen (QSS).
///
/// Warum es das geben muss: am 2026-10-06 stand im Protokoll
///
///     Populating font family aliases took N ms. Replace uses of missing
///     font family "SF Mono" with one that exists.
///
/// Nachgezaehlt gab es dann ACHT verschiedene Schreibweisen fuer
/// dieselbe Absicht, und auf dem Mac des Betreibers kamen dabei VIER
/// verschiedene Schriften heraus:
///
///     'SF Mono', Menlo, monospace        12x  -> Menlo
///     monospace                          12x  -> Qt-Vorgabe
///     'ui-monospace','Menlo','Consolas'   3x  -> Menlo
///     Menlo                               2x  -> Menlo
///     Menlo, monospace                    1x  -> Menlo
///     Menlo, Consolas, monospace          1x  -> Menlo
///     Consolas, 'Courier New', monospace  1x  -> COURIER NEW
///     'Monaco','Menlo',monospace          1x  -> MONACO
///     QFont("SF Mono") ohne Rueckfall     1x  -> gar nicht Monospace
///     QFont("Consolas") ohne Rueckfall    1x  -> gar nicht Monospace
///
/// SF Mono ist auf dem Mac NICHT als Familie ansprechbar (geprueft:
/// 0 Treffer in system_profiler, Menlo 26). Consolas ist Windows. Wer
/// sie ohne Rueckfall nennt, bekommt die proportionale Vorgabeschrift --
/// und genau das sah der Betreiber.
///
/// Als Makro, damit es sich an Zeichenkettenliterale anfuegen laesst:
///     "QLabel { font-family: " LP_MONO_QSS "; }"
// The monospace fallback chain for stylesheets, the same families as
// monoFont(). From Longpath src/gui/StyleConstants.h:70 [@551576e3]
constexpr auto kMonoQssFamilies = "Menlo, 'DejaVu Sans Mono', monospace";

// Bildlaufleisten: schmal, dunkel, ohne Pfeile — Qt/macOS malt sonst
// eine helle Systemleiste in jede Setup-Seite.
// From Longpath src/gui/StyleConstants.h:1271-1283 [@551576e3]
constexpr auto kScrollBarStyle =
    "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
    "QScrollBar::handle:vertical { background: #2c2c31; border-radius: 4px; min-height: 24px; margin: 2px; }"
    "QScrollBar::handle:vertical:hover { background: #3a3a41; }"
    "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; border: none; background: none; }"
    "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    "QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }"
    "QScrollBar::handle:horizontal { background: #2c2c31; border-radius: 4px; min-width: 24px; margin: 2px; }"
    "QScrollBar::handle:horizontal:hover { background: #3a3a41; }"
    "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; border: none; background: none; }"
    "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }";

// Role lookup into the built-in WorkspacePalette: the role's "#rrggbb",
// or `fallback` when the palette has no such role.
// From Longpath src/gui/styles/ThemeQss.cpp:284-288 [@551576e3]
QString role(const char* roleName, const char* fallback);

/// Eine Versalzeile: Groesse, Halbfett, Grossbuchstaben und die
/// Laufweite aus kCapsTracking — an einer Stelle, damit sie nicht je
/// Aufrufstelle neu gerechnet wird und dabei auseinanderlaeuft.
// From Longpath src/gui/StyleConstants.h:1347-1358 [@551576e3]
QFont capsFont(const QFont& base, int px = kFontCaption);

/// Eine Zahl, die sich aendert: Monospace, damit Stellen untereinander
/// stehen. HAUSSTIL.md §Die acht Regeln, Regel 2.
///
/// Haelt dieselbe Familie wie LP_MONO_QSS. Wer eine aendert, aendert
/// beide -- tst_schriftfamilien faengt das Auseinanderlaufen.
// From Longpath src/gui/StyleConstants.h:1377-1401 [@551576e3]
QFont monoFont(const QFont& base, int px, QFont::Weight w = QFont::Normal);

/// Innenschatten oben plus Lichtkante — fuer eine VERSENKTE Flaeche.
// From Longpath src/gui/StyleConstants.h:543-553 [@551576e3]
void paintInsetTop(QPainter& p, const QRect& r);

/// Ein Glaschip: schwarz, feiner Rahmen, Lichtkante oben innen, ein
/// Hauch Schatten darunter. Fuer Zahlen, die man liest, nicht drueckt.
// From Longpath src/gui/StyleConstants.h:573-589 [@551576e3]
void paintGlassChip(QPainter& p, const QRect& box, int radius);

// [original comment block from StyleConstants.h:644-696 sits above
//  shiftL() in WorkspaceStyle.cpp]
// From Longpath src/gui/StyleConstants.h:697-703 [@551576e3]
QString shiftL(const QColor& base, int deltaL);

/// Die Platte eines Panels (Glas & Tiefe, 2026-09-17): ein Verlauf von
/// oben hell nach unten dunkel und ein feiner Rahmen. Fuer schwebende
/// Fenster und gedockte Zellen dasselbe — die Applets darin sind
/// durchsichtig, damit die Platte durchscheint.
// From Longpath src/gui/StyleConstants.h:945-954 [@551576e3]
QString glassPanelFill();

// From Longpath src/gui/StyleConstants.h:1052-1066 [@551576e3]
QString titleBarStyle();

// kScrollBarStyle at `thicknessPx` instead of Longpath's 10 px: the
// workspace look keeps the platform style's bar thickness (JJ's choice,
// 2026-10-08), so no scroll area changes width.
QString scrollBarStyle(int thicknessPx);

} // namespace NereusSDR::Workspace
