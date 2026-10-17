// src/gui/styles/WorkspacePalette.h

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
// src/gui/styles/WorkspacePalette.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/StyleConstants.h (palette values) and
//   src/gui/styles/ThemeQss.cpp (role names and the legacy-colour table)
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   src/gui/StyleConstants.h carries the AetherSDR copyright and
//   GPLv3 notice reproduced above; src/gui/styles/ThemeQss.cpp has
//   no copyright or GPL header. Longpath's project LICENSE is GPLv3.
//
//   The built-in "Glass and Depth" palette: named presentation roles
//   with the Longpath values, looked up as QColor or as a stylesheet
//   string. It lives in NereusSDR::Workspace so that the symbols whose
//   names match NereusSDR::Style (kBorder, kAccent, ...) but whose values
//   differ never collide. NereusSDR's StyleConstants.h is unchanged and
//   remains the facade for existing callers.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 Used only behind the workspace preview switch.
//   2026-10-08 - Upstream comments restored on the ported constants; the
//                 licence note corrected.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QColor>
#include <QHash>
#include <QString>
#include <QVector>

namespace NereusSDR::Workspace {

// ── Surfaces ─────────────────────────────────────────────────────────
// ── Entblaut, 2026-08-15 ──────────────────────────────────────────────
//
// „nach [der Vorlage] sieht das aber nicht aus" — OE5SOS, mit einem Screenshot,
// auf dem dreißig Knöpfe einen leuchtend blauen Rahmen tragen: 5K, 4.4K,
// SQL, RIT, XIT, TUNE, MOX, VOX, MON, LEV, EQ, CFC, 2-Tone, PS-A. In
// der Vorlage ist ein inaktiver Knopf fast unsichtbar; sichtbar ist der eine,
// der an ist.
//
// Die Ursache war kBorder = #205070 — ein kräftiges Blau als Rahmen für
// ALLES. Dazu der durchgehende Blaustich in den Flächen: #0f0f1a,
// #0a0a18, #203040, #1a2a3a haben alle mehr Blau als Rot.
//
// Die alten Werte stehen daneben, weil das der Umbauzustand ist: was
// noch als Hex-Literal in einem Widget steht, folgt hier NICHT mit und
// bleibt alt. Diese Ungleichheit ist beabsichtigt und nützlich — sie
// zeigt genau, welche Stelle noch Arbeit braucht. Siehe
// docs/design/HAUSSTIL.md.
// From Longpath src/gui/StyleConstants.h:90 [@551576e3]
constexpr auto kAppBg           = "#08080a";   // war #0f0f1a
// From Longpath src/gui/StyleConstants.h:91 [@551576e3]
constexpr auto kPanelBg         = "#0c0c0e";   // war #0a0a18
// Hintergrund einer aktiv ausgewaehlten Kachel (z.B. VFO-Kachel) --
// eine Stufe heller als kPanelBg, aber kein eigenes Knopf-Hover.
constexpr auto kTileActiveBg    = "#16161c";   // From Longpath src/gui/StyleConstants.h:94 [@551576e3]
// Grundflaeche des Panadapters, unter Bild und Raster.
//
// Grau-Blau statt des fast schwarzen #0a0a12: der Betreiber hat am
// 2026-08-20 gesagt, der Standardhintergrund solle genau so aussehen
// wie auf den Logo-Vorschlaegen. Dort steht der Verlauf
// #1a2733 -> #0e151b; dies ist sein Mittelwert als Flaeche.
//
// Eine gespeicherte eigene Farbe (PanadapterBackgroundFill) hat
// Vorrang — dieser Wert ist nur die Vorgabe fuer alle, die keine
// gewaehlt haben.
constexpr auto kPanadapterBg    = "#141e27";   // From Longpath src/gui/StyleConstants.h:219 [@551576e3]
constexpr auto kStatusBarBg     = "#0a0a0c";   // From Longpath src/gui/StyleConstants.h:640 [@551576e3]
constexpr auto kStatusBarBorder = "#1f1f23";   // From Longpath src/gui/StyleConstants.h:641 [@551576e3]
constexpr auto kStatusSep       = "#2c2c31";   // From Longpath src/gui/StyleConstants.h:642 [@551576e3]

// ── Text ─────────────────────────────────────────────────────────────
// ── Die Textleiter, 2026-08-20 angehoben ─────────────────────────────
//
// Der Betreiber: „generell ist die schrift sehr dunkel. heller waere
// besser. zahlen und zeiger aufhellen."
//
// Angehoben wurde die GANZE Leiter, nicht einzelne Werte. Die Abstaende
// zwischen den Stufen tragen die Bedeutung (Fliesstext vor Nebentext
// vor Skala vor Inaktiv); haette man nur die dunkelste angehoben, waere
// sie ihrer Nachbarin zu nah gekommen und der Unterschied zwischen
// „leise" und „aus" verloren.
//
//   text          #c4c4c9 -> #dcdce1
//   title-text    #a8a8ae -> #c4c4ca
//   secondary     #8e8e93 -> #a6a6ac
//   tertiary      #76767a -> #909096
//   scale         #5c5c60 -> #7e7e85     Skalen und Gradzahlen
//   inactive      #3d3d41 -> #58585e
//   measured      #c2924f -> #d8a55f     Zeiger und Messwerte
//
// Dieselben Werte stehen ein zweites Mal in themes/oe5sos.json: die
// Palette bedient den Malcode, die Themendatei die Stylesheets. Wer
// eine der beiden aendert, muss die andere mitziehen.
// From Longpath src/gui/StyleConstants.h:117 [@551576e3]
constexpr auto kTextPrimary     = "#dcdce1";   // war #c8d8e8, dann #c4c4c9
constexpr auto kTextSecondary   = "#a6a6ac";   // From Longpath src/gui/StyleConstants.h:118 [@551576e3]
constexpr auto kTextTertiary    = "#909096";   // From Longpath src/gui/StyleConstants.h:119 [@551576e3]
constexpr auto kTextScale       = "#7e7e85";   // From Longpath src/gui/StyleConstants.h:182 [@551576e3]
constexpr auto kTextInactive    = "#58585e";   // From Longpath src/gui/StyleConstants.h:183 [@551576e3]
// Longpath-original — used in 5+ places for AGC-T / pan / similar labels;
// sits between kTextSecondary (#8090a0) and kTextScale (#607080).
constexpr auto kLabelMid        = "#828288";   // From Longpath src/gui/StyleConstants.h:186 [@551576e3]
// Versalzeilen-Text ueber Gruppen (z.B. Spektrum-Overlay-Ueberschriften)
// und Fallback fuer unbekannte Zustaende -- dunkler als kTextTertiary,
// heller als kTextInactive.
constexpr auto kTextGroupHead   = "#5c5c60";   // From Longpath src/gui/StyleConstants.h:190 [@551576e3]
constexpr auto kAccent          = "#4a7ba8";   // From Longpath src/gui/StyleConstants.h:191 [@551576e3]
constexpr auto kTitleText       = "#c4c4ca";   // From Longpath src/gui/StyleConstants.h:192 [@551576e3]

// ── Buttons and borders ──────────────────────────────────────────────
// Borders & Surfaces
//
// kBorder ist der wichtigste Wert hier: er umrandet jeden Knopf im
// Programm. #205070 machte daraus dreißig blaue Rechtecke. #2c2c31 ist
// gerade noch da und tritt nicht mehr nach vorne — der ausgewählte
// Knopf soll auffallen, nicht alle anderen.
// From Longpath src/gui/StyleConstants.h:200 [@551576e3]
constexpr auto kButtonBg        = "#1a1a1e";   // war #1a2a3a
// From Longpath src/gui/StyleConstants.h:201 [@551576e3]
constexpr auto kButtonHover     = "#26262b";   // war #203040
// From Longpath src/gui/StyleConstants.h:202 [@551576e3]
constexpr auto kButtonAltHover  = "#2f3138";   // war #204060
// From Longpath src/gui/StyleConstants.h:203 [@551576e3]
constexpr auto kBorder          = "#2c2c31";   // war #205070
// From Longpath src/gui/StyleConstants.h:204 [@551576e3]
constexpr auto kBorderSubtle    = "#1f1f23";   // war #203040
// Gedaempft/aus -- eine Stufe dunkler als kBorder. Gegenstueck zu
// kGreenBorder bei LED-artigen An/Aus-Zustaenden, auch fuer blassen
// Knopftext auf dunklem Grund.
constexpr auto kBorderMuted     = "#3d3d41";   // From Longpath src/gui/StyleConstants.h:208 [@551576e3]

// ── Recessed fields and grooves ──────────────────────────────────────
// ── Eigener Wert, nicht derselbe wie der Seitengrund ────────────────
//
// kInsetBg trug bis zum 2026-08-21 EXAKT denselben Wert wie kAppBg
// (#08080a). Das ist nicht nur Geschmack, es macht die Rolle
// unbrauchbar: die Themen-Umsetzung schluesselt ueber den FARBWERT,
// und wer denselben Wert zweimal vergibt, bekommt nur die erste Rolle
// — app-bg steht frueher in der Tabelle. Ein Thema konnte die
// Eingabefelder deshalb nicht anfassen, ohne den ganzen Seitengrund
// mitzunehmen.
//
// Dieselbe Krankheit wie „#203040 war vier Konstanten auf einmal"
// weiter unten in ThemeQss.cpp, nur unbemerkt geblieben, weil beide
// Werte gleich AUSSEHEN.
//
// Gefunden von tst_theme_form_knobs: die Pruefung verlangte, dass ein
// Thema die Feldfarbe aendert, und das ging nicht.
//
// Der neue Wert ist zwei Stufen tiefer als der Seitengrund — was fuer
// eine VERSENKTE Flaeche ohnehin richtig ist: ein Feld liegt im Grund,
// es liegt nicht darauf.
// From Longpath src/gui/StyleConstants.h:245 [@551576e3]
constexpr auto kInsetBg         = "#050507";   // war #08080a (= kAppBg!)
// Verlaufsanfang einer Mulde -- bewusst eine Stufe heller als kInsetBg,
// nicht derselbe Wert: der Verlauf braucht beide Enden.
constexpr auto kInsetBgTop      = "#101014";   // From Longpath src/gui/StyleConstants.h:248 [@551576e3]
// From Longpath src/gui/StyleConstants.h:249 [@551576e3]
constexpr auto kInsetBorder     = "#232329";   // war #1e2e3e
// From Longpath src/gui/StyleConstants.h:250 [@551576e3]
constexpr auto kGroove          = "#1f1f23";   // war #203040

// ── Title bar gradient ───────────────────────────────────────────────
// Title Bar Gradient
constexpr auto kTitleGradTop    = "#26262b";   // From Longpath src/gui/StyleConstants.h:253 [@551576e3]
constexpr auto kTitleGradMid    = "#1c1c20";   // From Longpath src/gui/StyleConstants.h:254 [@551576e3]
constexpr auto kTitleGradBot    = "#141417";   // From Longpath src/gui/StyleConstants.h:255 [@551576e3]
constexpr auto kTitleBorder     = "#0d0d0f";   // From Longpath src/gui/StyleConstants.h:256 [@551576e3]

// ── Confirmed state (green) ──────────────────────────────────────────
// Active/Checked Button States
constexpr auto kGreenBg         = "#1c3a2a";   // From Longpath src/gui/StyleConstants.h:259 [@551576e3]
constexpr auto kGreenText       = "#6fa384";   // From Longpath src/gui/StyleConstants.h:260 [@551576e3]
constexpr auto kGreenBorder     = "#2c5c44";   // From Longpath src/gui/StyleConstants.h:261 [@551576e3]
// "Verbunden/live" -- kraeftiger und gesaettigter als das gedaempfte
// kGreenText, fuer einen echten Live-Zustand statt einer ruhigen
// Erfolgsmeldung (z.B. Spracherkennung "hoert", KiwiSDR verbunden).
constexpr auto kLiveGreen       = "#4caf6a";   // From Longpath src/gui/StyleConstants.h:265 [@551576e3]

// ── Selection (blue) ─────────────────────────────────────────────────
// ── Der aktive Zustand ───────────────────────────────────────────────
//
// Der Betreiber, 2026-08-21, zur Vorlage: „das design hat teilweise mehr
// stil ... ich glaube die farblichen verlaeufe machen es."
//
// Die Verlaeufe waren es nicht — die stehen seit zwei Tagen drin und
// sind nachgemessen. Gemessen wurde stattdessen die SAETTIGUNG des
// aktiven Knopfes: bei uns 65 Prozent, in der Vorlage 100. Das alte #254a72
// hatte 39 Prozent Saettigung im Farbwert selbst; es sagte nicht „das
// hier ist an", sondern „das hier ist irgendwie anders".
//
// Der neue Wert ist gesaettigt und liegt im selben Farbton (rund
// 215 Grad), damit nichts anderes in der Palette dagegen steht.
//
// Fuer helle Themen aendert sich dadurch NICHTS: „Kreide" fuehrt
// sel-bg und sel-border selbst (#e0d6ee / #a889cc). Ein gesaettigtes
// Blau auf hellem Grund waere laut — das ist der Grund, warum die
// Rolle dort ueberschrieben wird und nicht hier.
// From Longpath src/gui/StyleConstants.h:293 [@551576e3]
constexpr auto kBlueBg          = "#3576e0";   // war #254a72 (S 39 %)
// From Longpath src/gui/StyleConstants.h:294 [@551576e3]
constexpr auto kBlueText        = "#ffffff";   // war #cfe2f5
// From Longpath src/gui/StyleConstants.h:295 [@551576e3]
constexpr auto kBlueBorder      = "#2a5fbe";   // war #2f5c86
constexpr auto kBlueHover       = "#2d5885";   // From Longpath src/gui/StyleConstants.h:296 [@551576e3]

// ── Reading and warning (amber) ──────────────────────────────────────
constexpr auto kAmberBg         = "#33280f";   // From Longpath src/gui/StyleConstants.h:297 [@551576e3]
constexpr auto kAmberText       = "#d8a55f";   // From Longpath src/gui/StyleConstants.h:298 [@551576e3]
// Die gedaempfte Messwertstufe. Sie stand bis 2026-08-17 dreimal als
// nacktes #6b5630 im Baum -- dateilokal in HGauge.cpp:13, noch einmal
// in InstrumentPainter.cpp und als Literal in VfoWidget. Eine Farbe mit
// drei Wohnorten und keinem Namen ist genau die, die beim naechsten
// Feinschliff an zwei Stellen mitgezogen wird und an einer nicht.
constexpr auto kAmberDim        = "#6b5630";   // From Longpath src/gui/StyleConstants.h:304 [@551576e3]
constexpr auto kAmberBorder     = "#6b5426";   // From Longpath src/gui/StyleConstants.h:305 [@551576e3]
constexpr auto kAmberWarn       = "#a8853f";   // From Longpath src/gui/StyleConstants.h:306 [@551576e3]

// ── Status badges ────────────────────────────────────────────────────
// ── Abzeichen: Paare aus Grund und Text ─────────────────────────────
//
// OE5SOS, 2026-08-17: „keine errechneten Deckkraftwerte, sondern
// benannte Paare aus Grund und Text — wie danger-bg mit Rot-Text."
//
// StatusBadge legte bis dahin eine Deckkraft von 10-20 % ueber den
// Untergrund: rgba(255,96,96,51) und Geschwister. Zwei Gruende, warum
// das weg ist:
//
//   Eine Deckkraft ueber einem UNBEKANNTEN Grund ergibt je nach
//   Untergrund etwas anderes. Dasselbe Abzeichen sah in der
//   Statuszeile anders aus als im RxDashboard, ohne dass irgendwo
//   stand, welcher Ton herauskommen sollte.
//
//   Ein Paar haelt Grund und Text zusammen. Genau das war beim
//   Abzeichen auseinandergelaufen (siehe StatusBadge::applyStyle).
//
// ── Woher die Werte kommen ──────────────────────────────────────────
//
// Ausgerechnet, nicht geschaetzt: fuer jede Variante der sichtbare
// Abstand ihrer bisherigen Wasche vom Panelgrund (#0c0c0e), gemessen
// in CIE-Lab wie in tools/colour_audit.py, und dann die Deckkraft
// gesucht, die mit der HEUTIGEN Schriftfarbe denselben Abstand ergibt.
//
// Der Unterschied ist noetig, weil die alten Waschen aus den alten
// HELLEN Farben gerechnet waren -- Gold #ffd700 unter einer Schrift,
// die seit dem Hausstil Bernstein #c2924f ist, und Signalrot #ff6060
// unter #c25a5c. Dieselbe Deckkraft mit einer gedaempften Farbe
// verschwindet; derselbe ABSTAND bleibt sichtbar.
//
//   Info  #141c27 -> #161e27   (dE 10,0)
//   On    #14251b -> #212b27   (dE 14,9)
//   Off   #15171b -> #18181a   (dE  5,0)
//   Warn  #29240c -> #372c1d   (dE 19,7)
//   Tx    #3d1d1e -> #3f2224   (dE 20,5)
//
// Eigene Abstufung neben kGreenBg/kAmberBg/kRedBg: die sind die
// Fuellung eines Knopfes (TgxlAdvancedPage:648-664), ein Abzeichen ist
// eine Andeutung. Zwei Staerken, zwei Namensfamilien.
constexpr auto kBadgeInfoBg     = "#161e27";   // From Longpath src/gui/StyleConstants.h:350 [@551576e3]
constexpr auto kBadgeOkBg       = "#212b27";   // From Longpath src/gui/StyleConstants.h:351 [@551576e3]
constexpr auto kBadgeOffBg      = "#18181a";   // From Longpath src/gui/StyleConstants.h:352 [@551576e3]
constexpr auto kBadgeWarnBg     = "#372c1d";   // From Longpath src/gui/StyleConstants.h:353 [@551576e3]
constexpr auto kBadgeTxBg       = "#3f2224";   // From Longpath src/gui/StyleConstants.h:354 [@551576e3]

// ── TX and danger (red) ──────────────────────────────────────────────
// Das kraeftigere Sende-Rot. HAUSSTIL.md §Bedeutung: „Sendet / Gefahr
// #a86b6d — nur MOX/TX darf kraeftiger, #c25a5c". Stand als Literal in
// StatusBadge und hatte keinen Namen.
constexpr auto kTxRed           = "#c25a5c";   // From Longpath src/gui/StyleConstants.h:359 [@551576e3]
constexpr auto kRedBg           = "#7a2c2e";   // From Longpath src/gui/StyleConstants.h:361 [@551576e3]
constexpr auto kRedText         = "#f0dcdc";   // From Longpath src/gui/StyleConstants.h:362 [@551576e3]
constexpr auto kRedBorder       = "#a86b6d";   // From Longpath src/gui/StyleConstants.h:363 [@551576e3]
constexpr auto kGaugeDanger     = "#a86b6d";   // From Longpath src/gui/StyleConstants.h:506 [@551576e3]

// ── Spectrum chrome ──────────────────────────────────────────────────
// From Longpath src/gui/StyleConstants.h:461 [@551576e3]
constexpr auto kSpectrumGrid    = "#8a8f96";   // Gitterlinien (Alpha im Code)

// ── Glass and Depth ──────────────────────────────────────────────────
// ── Glas & Tiefe ─────────────────────────────────────────────────────
//
// Die Richtung, die der Betreiber am 2026-09-17 aus vier Stilblaettern
// gewaehlt hat (docs/design/2026-09-17-design-durchsicht.md, Blatt 3):
// Panels mit Verlauf und Schatten, Zeichenflaechen VERSENKT (Schwarz,
// Innenschatten oben, Lichtkante), Knoepfe ERHABEN (Verlauf, Lichtkante,
// dunkle Unterkante), Zahlen in schwarzen Glaschips, Kurven mit Hof.
// Die Grundregeln des Hausstils bleiben (zwei Prozent Farbe, Blau ist
// anfassbar, Warm ist gemessen); das hier ist ihre Ausformung.
//
// Licht und Schatten sind ALPHA-Werte auf Weiss bzw. Schwarz, keine
// Farben: dieselbe Kante liegt auf jedem Grund richtig.
// From Longpath src/gui/StyleConstants.h:521 [@551576e3]
constexpr auto kGlassPanelTop   = "#17171b";   // Panelverlauf oben
// From Longpath src/gui/StyleConstants.h:522 [@551576e3]
constexpr auto kGlassPanelBot   = "#0e0e10";   // Panelverlauf unten
// From Longpath src/gui/StyleConstants.h:523 [@551576e3]
constexpr auto kGlassBtnTop     = "#222227";   // erhabener Knopf, oben
// From Longpath src/gui/StyleConstants.h:524 [@551576e3]
constexpr auto kGlassBtnBot     = "#141417";   // erhabener Knopf, unten
// From Longpath src/gui/StyleConstants.h:525 [@551576e3]
constexpr auto kGlassSelTop     = "#2f5f92";   // Auswahl, oben
// From Longpath src/gui/StyleConstants.h:526 [@551576e3]
constexpr auto kGlassSelBot     = "#1e3d5f";   // Auswahl, unten
constexpr auto kGlassSelBorder  = "#3d78b0";   // From Longpath src/gui/StyleConstants.h:527 [@551576e3]
constexpr auto kGlassSelText    = "#dbe9f8";   // From Longpath src/gui/StyleConstants.h:528 [@551576e3]
// From Longpath src/gui/StyleConstants.h:529 [@551576e3]
constexpr int  kGlassLightAlpha  = 18;         // Lichtkante: Weiss mit diesem Alpha
// From Longpath src/gui/StyleConstants.h:530 [@551576e3]
constexpr int  kGlassShadeAlpha  = 110;        // Innenschatten: Schwarz, erste Zeile
// From Longpath src/gui/StyleConstants.h:531 [@551576e3]
constexpr auto kGlassLight       = "rgba(255, 255, 255, 18)";   // dieselbe Kante fuer Stylesheets
constexpr auto kGlassShade       = "rgba(0, 0, 0, 160)";        // From Longpath src/gui/StyleConstants.h:532 [@551576e3]
constexpr int  kGlassChipRadius  = 7;          // From Longpath src/gui/StyleConstants.h:533 [@551576e3]
constexpr int  kGlassPanelRadius = 10;         // From Longpath src/gui/StyleConstants.h:534 [@551576e3]

// ── Formularstil: Glas & Tiefe (2026-09-18) ───────────────────────────
//
// Roadmap C/F, Betreiber am 17.09.: "kannst ruhig weiter designen".
// Ein Stil fuer alle Formularteile, in Applets wie im Setup-Dialog:
//
//   Auswahlfelder (QComboBox) sind ERHABEN wie ein Knopf — Verlauf,
//   Lichtkante oben, dunkle Kante unten, Radius 7 — denn sie oeffnen
//   ein Menue, das UEBER der Oberflaeche liegt. Die Liste selbst ist
//   ein dunkles Glas mit dem gedeckten Auswahlverlauf der Kopfleiste.
//
//   Haekchen und Wahlpunkte sind VERSENKT (schwarz, Schattenkante
//   oben, Lichtkante unten); angehakt fuellt sie derselbe gedeckte
//   Blauverlauf wie jede andere Auswahl. Radius 4 statt 2 — die
//   Regel "nie 3" gilt auch fuer Kleines.
//
//   Eingabefelder sind Glasfelder (glassFieldStyle), Rinnen versenkt
//   (sliderHStyle/sliderVStyle), Knoepfe erhaben (buttonBaseStyle).
//
// Feste Hexwerte statt hexRole(): die Konstanten muessen constexpr
// bleiben, weil ~200 Aufrufstellen sie als const char* erwarten; die
// Glasfarben (kGlass*) haben ohnehin keine Themenrolle. Werte:
// Kante hell = shiftL(kBorder, +12), Kante dunkel = shiftL(kBorder, -14).
// From Longpath src/gui/StyleConstants.h:1109 [@551576e3]
constexpr auto kGlassEdgeLight  = "#38383e";   // Lichtkante auf kBorder
// From Longpath src/gui/StyleConstants.h:1110 [@551576e3]
constexpr auto kGlassEdgeDark   = "#1f1f23";   // Schattenkante auf kBorder

// ── Disabled ─────────────────────────────────────────────────────────
// Disabled
constexpr auto kDisabledBg      = "#141417";   // From Longpath src/gui/StyleConstants.h:618 [@551576e3]
constexpr auto kDisabledText    = "#4e4e53";   // From Longpath src/gui/StyleConstants.h:619 [@551576e3]
constexpr auto kDisabledBorder  = "#232327";   // From Longpath src/gui/StyleConstants.h:620 [@551576e3]

// ── Overlay and DSP toggle ───────────────────────────────────────────
constexpr auto kOverlayBorder   = "#2c2c31";   // From Longpath src/gui/StyleConstants.h:626 [@551576e3]
// DSP toggle — brighter green than the kGreenBg action buttons.
// Used by VfoWidget DSP tab and SpectrumOverlayPanel DSP toggles.
// Source: Longpath-original. Distinct semantic from action-button
// "checked" state because DSP toggles communicate "feature on" not
// "action engaged."
constexpr auto kDspToggleBg     = "#1b3527";   // From Longpath src/gui/StyleConstants.h:886 [@551576e3]
constexpr auto kDspToggleBorder = "#33684c";   // From Longpath src/gui/StyleConstants.h:887 [@551576e3]
constexpr auto kDspToggleText   = "#7fae91";   // From Longpath src/gui/StyleConstants.h:888 [@551576e3]

// A named-role palette. Role names follow Longpath's ThemeQss role
// vocabulary (kebab-case, for example "border-subtle"), plus the roles
// the later instrument ports look up by name (design-facts C6).
class WorkspacePalette
{
public:
    struct Entry {
        const char* role;
        const char* hex;   // "#rrggbb", lower case
    };

    // One row of the legacy-colour table: a hex value NereusSDR's
    // retained stylesheets write, and the role it stands for.
    struct LegacyRow {
        const char* legacyHex;   // "#rrggbb", lower case
        const char* role;
    };

    explicit WorkspacePalette(QVector<Entry> entries);

    // The built-in Glass and Depth palette.
    static const WorkspacePalette& glassAndDepth();

    // Legacy hex to role, built from Longpath ThemeQss.cpp's table and
    // design-facts B10. #203040 has no row on purpose: it stood for a
    // hover, a groove, a subtle border and the status-bar border, and a
    // single row cannot recover which. Callers name its role.
    static const QVector<LegacyRow>& legacyRows();

    bool contains(const QString& role) const;
    // Invalid QColor when the role is unknown.
    QColor color(const QString& role) const;
    QColor color(const QString& role, const QColor& fallback) const;
    // "#rrggbb" for a stylesheet; empty when the role is unknown.
    QString qss(const QString& role) const;
    QString qss(const QString& role, const QString& fallback) const;

    const QVector<Entry>& entries() const { return m_entries; }

private:
    QVector<Entry> m_entries;
    QHash<QString, QString> m_byRole;
};

} // namespace NereusSDR::Workspace
