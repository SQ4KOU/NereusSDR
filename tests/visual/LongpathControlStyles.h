// tests/visual/LongpathControlStyles.h

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

// --- From src/gui/styles/AppTheme.h ---
// src/gui/styles/AppTheme.h  (Longpath)
//
// Application-wide dark-theme bootstrap. Installs a QPalette + minimal
// baseline QSS on the QApplication so every widget — even ones that
// don't carry their own stylesheet — picks up the Longpath dark theme
// instead of falling through to the host platform's Fusion defaults.
//
// Why this exists:
//   On Linux/Ubuntu (Yaru theme), bare Fusion produces light backgrounds
//   and an orange highlight color. Per-page stylesheets covered most of
//   the UI but missed popups, group-box titles, tooltips, and any
//   widget added without an explicit `setStyleSheet`. This file is the
//   single source of truth that fills those gaps.
//
// Usage (main.cpp, after `app.setStyle(QStyleFactory::create("Fusion"))`):
//
//     Longpath::applyDarkPalette(app);
//     Longpath::applyAppBaselineQss(app);
//
// Per-page stylesheets remain authoritative — the baseline only paints
// what they don't.

// --- From src/gui/styles/PopupMenuStyle.h ---
// src/gui/styles/PopupMenuStyle.h  (Longpath)
//
// Universal dark-palette QMenu stylesheet — applied to every antenna
// popup menu (VFO Flag, RxApplet, SpectrumOverlayPanel, meter items,
// future band/mode menus). Fixes issue #98 where Ubuntu 25.10's default
// theme rendered antenna menu items as dark-on-dark (only visible on
// hover). Use via:
//
//     QMenu menu(this);
//     menu.setStyleSheet(QString::fromLatin1(kPopupMenu));
//
// Every call site MUST set this stylesheet — enforced by
// tests/tst_popup_style_coverage.cpp.
//
// Phase 3P-I-a. Design: docs/architecture/antenna-routing-design.md §6.1 Rule 2.

// =================================================================
// tests/visual/LongpathControlStyles.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/StyleConstants.h (raisedFill, sunkenFill, buttonBaseStyle,
//   greenCheckedStyle, blueCheckedStyle, sliderHStyle, sliderVStyle,
//   kGroupBoxStyle, kComboStyle, kCheckBoxStyle, kRadioButtonStyle,
//   formFieldStyle, kSliderStyle, kButtonStyle, kTabStyle),
//   src/gui/styles/AppTheme.h (applyDarkPalette,
//   applyAppBaselineQss) and src/gui/styles/PopupMenuStyle.h
//   (kPopupMenu) at revision
//   551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   src/gui/StyleConstants.h carries the AetherSDR copyright and
//   GPLv3 notice reproduced above; src/gui/styles/AppTheme.h and
//   src/gui/styles/PopupMenuStyle.h have no copyright or GPL header.
//   Longpath's project LICENSE is GPLv3.
//
//   Used only by the visual preview tool's controls-sheet scene, to show
//   Longpath's own control stylesheets next to today's look and the
//   workspace preview. Nothing in the app includes it.
//
//   The colour constants resolve through NereusSDR's Workspace palette
//   (gui/styles/WorkspacePalette.h), which holds Longpath's values.
//   hexRole() is the identity and formInt() returns its fallback: that is
//   what Longpath's ThemeQss does with its built-in palette and no form
//   overrides. Style::themed() is NereusSDR's QssTranslator, the port of
//   Longpath's legacy table.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-08 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 Namespace Longpath::Style becomes
//                 NereusSDR::Preview::LongpathLook; shiftL() and
//                 kScrollBarStyle are the Workspace:: ports.
//   2026-10-09 - themed() keeps Longpath's own spin arrow images.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include <QApplication>
#include <QColor>
#include <QPalette>
#include <QString>
#include <QStringLiteral>

#include "gui/styles/QssTranslator.h"
#include "gui/styles/WorkspacePalette.h"
#include "gui/styles/WorkspaceStyle.h"

namespace NereusSDR::Preview::LongpathLook {

using namespace NereusSDR::Workspace;   // kAppBg, kBorder, ... and shiftL()

// Longpath's ThemeQss with its built-in palette: a role lookup returns the
// value itself, and a form setting returns its fallback.
// From Longpath src/gui/styles/ThemeQss.cpp:290-305 [@551576e3]
inline QString hexRole(const char* hex)
{
    return QString::fromLatin1(hex);
}

// From Longpath src/gui/styles/ThemeQss.cpp:307 [@551576e3]
inline int formInt(const char* /*name*/, int fallback)
{
    return fallback;
}

// Longpath's themed(): the legacy-hex table, ported as QssTranslator.
// NereusSDR: QssTranslator also points url(:/icons/spin-*.svg) at the
// workspace variants (spin-*-workspace.svg, the text role). Longpath's
// table leaves url() alone and draws its own #c8d8e8 arrows (Longpath
// resources/icons/spin-up.svg and spin-down.svg [@551576e3], the same
// files as NereusSDR's), so the reference keeps them.
inline QString themed(const QString& qss)
{
    QString out = QssTranslator::translate(qss).qss;
    out.replace(QStringLiteral(":/icons/spin-up-workspace.svg"), QStringLiteral(":/icons/spin-up.svg"));
    out.replace(QStringLiteral(":/icons/spin-down-workspace.svg"), QStringLiteral(":/icons/spin-down.svg"));
    return out;
}

/// AUFGESETZT: oben heller, unten dunkler — Licht von oben, die
/// Flaeche bekommt Dicke. Fuer Platten, Kopfleisten, Knoepfe.
// From Longpath src/gui/StyleConstants.h:705-719 [@551576e3]
inline QString raisedFill(const char* baseHex, int lift = -1, int drop = -1)
{
    // Tiefe aus dem Thema, sofern es eine Meinung hat. Die Aufrufer
    // duerfen weiter eigene Werte setzen — der aktive Knopf etwa ist
    // absichtlich etwas kraeftiger als eine ruhende Platte.
    if (lift < 0) { lift = formInt("relief", 16); }
    if (drop < 0) { drop = lift * 3 / 4; }
    const QColor base(hexRole(baseHex));
    return QStringLiteral(
        "qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        " stop:0 %1, stop:0.55 %2, stop:1 %3)")
        .arg(shiftL(base, lift), base.name(), shiftL(base, -drop));
}

/// VERSENKT: genau andersherum — oben dunkel, unten heller. Dasselbe
/// Mittel, umgedreht, und die Flaeche liegt drin statt drauf.
// From Longpath src/gui/StyleConstants.h:721-732 [@551576e3]
inline QString sunkenFill(const char* baseHex, int deepen = -1, int lift = -1)
{
    if (deepen < 0) { deepen = formInt("mulde", 10); }
    if (lift < 0)   { lift   = deepen * 6 / 5; }
    const QColor base(hexRole(baseHex));
    return QStringLiteral(
        "qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        " stop:0 %1, stop:1 %2)")
        .arg(shiftL(base, -deepen), shiftL(base, lift));
}

// From Longpath src/gui/StyleConstants.h:766-797 [@551576e3]
inline QString buttonBaseStyle()
{
    return QStringLiteral(
        "QPushButton {"
        // Polsterung 4/10 statt 2/4 und 7 statt 6 Punkt Eckenradius.
        // Klingt nach nichts und ist der halbe Eindruck von Ruhe: die
        // Knopfreihen in der Vorlage wirken nicht ruhiger, weil dort weniger
        // steht, sondern weil um jedes Ding mehr Luft ist.
        "  background: %1; border: 1px solid %2; border-radius: %6px;"
        // Glas & Tiefe (2026-09-17): ein erhabener Knopf hat oben eine
        // Lichtkante und unten eine dunkle Kante — der Rahmen ist
        // nicht rundum gleich, sondern das Licht kommt von oben.
        "  border-top-color: %9; border-bottom-color: %10;"
        "  color: %3; font-size: 11px; font-weight: bold;"
        "  padding: %7px %8px;"
        "}"
        "QPushButton:hover { background: %4; }"
        // Gedrueckt: der Verlauf kippt. Ein Knopf, der sich beim
        // Druecken nicht bewegt, fuehlt sich tot an — und das kostet
        // hier kein einziges Pixel Verschiebung.
        "QPushButton:pressed { background: %5; border-top-color: %10; border-bottom-color: %9; }"
    ).arg(raisedFill(kButtonBg),
          QLatin1String(kBorder),
          QLatin1String(kTextPrimary),
          raisedFill(kButtonAltHover, 20, 10),
          sunkenFill(kButtonBg))
     .arg(formInt("radius", 7))
     .arg(formInt("luft-v", 4))
     .arg(formInt("luft-h", 10))
     .arg(shiftL(QColor(hexRole(kBorder)), 12),
          shiftL(QColor(hexRole(kBorder)), -14));
}

// From Longpath src/gui/StyleConstants.h:799-804 [@551576e3]
inline QString greenCheckedStyle()
{
    return QStringLiteral(
        "QPushButton:checked { background: %1; color: %2; border: 1px solid %3; }"
    ).arg(kGreenBg, kGreenText, kGreenBorder);
}

// From Longpath src/gui/StyleConstants.h:806-824 [@551576e3]
inline QString blueCheckedStyle()
{
    // raisedFill statt einer flachen Fuellung: der aktive Knopf ist
    // dieselbe aufgesetzte Flaeche wie jeder andere, nur in der
    // Akzentfarbe. Zwei verschiedene Bauarten fuer denselben Knopf
    // waeren genau die Art Unstimmigkeit, die man als „unfertig" liest.
    // Glas & Tiefe (2026-09-17): der gedeckte Auswahlverlauf der
    // Kopfleisten-Pillen (kGlassSel*) statt des flachen kBlueBg — auf
    // den Applet-Blaettern leuchteten "Stereo", "Phone", "5.0k", "ON/RX/
    // TX" und die Scheibenmarke "A" in einem Blau, das die Kopfleiste
    // laengst nicht mehr trug. Eine Auswahl, eine Farbe.
    return QStringLiteral(
        "QPushButton:checked {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2);"
        "  color: %3; border: 1px solid %4; border-top-color: %5; }"
    ).arg(hexRole(kGlassSelTop), hexRole(kGlassSelBot),
          hexRole(kGlassSelText), hexRole(kGlassSelBorder),
          shiftL(QColor(hexRole(kGlassSelBorder)), 16));
}

// From Longpath src/gui/StyleConstants.h:897-920 [@551576e3]
inline QString sliderHStyle()
{
    // Glas & Tiefe (2026-09-17): die Rinne ist VERSENKT (schwarz, oben
    // die dunkle Kante, unten Licht), der Griff ERHABEN — eine Pille im
    // Knopfverlauf mit Akzentrand, nicht mehr die blaue Kugel von Qt.
    // Blau bleibt am Rand: der Griff ist anfassbar.
    return QStringLiteral(
        "QSlider::groove:horizontal {"
        "  height: 4px; background: %1; border: 1px solid %2;"
        "  border-top-color: %3; border-bottom-color: %4; border-radius: 3px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 10px; height: 14px; margin: -6px 0;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "               stop:0 %5, stop:1 %6);"
        "  border: 1px solid %7; border-top-color: %8; border-radius: 5px;"
        "}"
        "QSlider::handle:horizontal:hover { background: %9; }"
    ).arg(hexRole(kInsetBg), hexRole(kBorder),
          QLatin1String(kGlassShade), QLatin1String(kGlassLight),
          hexRole(kGlassBtnTop), hexRole(kGlassBtnBot),
          hexRole(kAccent), shiftL(QColor(hexRole(kAccent)), 18),
          hexRole(kGlassBtnTop));
}

// From Longpath src/gui/StyleConstants.h:922-943 [@551576e3]
inline QString sliderVStyle()
{
    // Wie sliderHStyle(), nur stehend: Rinne versenkt (Licht kommt von
    // oben, also Schattenkante oben), Griff eine liegende Pille.
    return QStringLiteral(
        "QSlider::groove:vertical {"
        "  width: 4px; background: %1; border: 1px solid %2;"
        "  border-top-color: %3; border-bottom-color: %4; border-radius: 3px;"
        "}"
        "QSlider::handle:vertical {"
        "  height: 10px; width: 14px; margin: 0 -6px;"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
        "               stop:0 %5, stop:1 %6);"
        "  border: 1px solid %7; border-top-color: %8; border-radius: 5px;"
        "}"
        "QSlider::handle:vertical:hover { background: %9; }"
    ).arg(hexRole(kInsetBg), hexRole(kBorder),
          QLatin1String(kGlassShade), QLatin1String(kGlassLight),
          hexRole(kGlassBtnTop), hexRole(kGlassBtnBot),
          hexRole(kAccent), shiftL(QColor(hexRole(kAccent)), 18),
          hexRole(kGlassBtnTop));
}

// From Longpath src/gui/StyleConstants.h:1075-1082 [@551576e3]
constexpr auto kGroupBoxStyle =
    // Eine Gruppe ist eine Platte im Panel: feiner Rahmen mit Licht-
    // kante, Radius 10 wie die Panels, Titel in Zweitfarbe — nicht
    // mehr blau und fett, das war die alte Setup-Palette.
    "QGroupBox { border: 1px solid #2c2c31; border-top-color: #38383e;"
    " border-radius: 10px; margin-top: 8px; padding-top: 12px;"
    " font-weight: 600; color: #a6a6ac; }"
    "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }";

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
// [Longpath kGlassEdgeLight / kGlassEdgeDark (StyleConstants.h:1103-1110)
//  are Workspace::kGlassEdgeLight / kGlassEdgeDark in WorkspacePalette.h]

// From Longpath src/gui/StyleConstants.h:1112-1133 [@551576e3]
constexpr auto kComboStyle =
    "QComboBox {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #222227, stop:1 #141417);"
    "  color: #dcdce1; border: 1px solid #2c2c31;"
    "  border-top-color: #38383e; border-bottom-color: #1f1f23;"
    "  border-radius: 7px; padding: 3px 8px; }"
    "QComboBox:hover {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a2a30, stop:1 #1a1a1e); }"
    "QComboBox:on {"   // aufgeklappt: gedrueckt, der Verlauf kippt
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #141417, stop:1 #222227);"
    "  border-top-color: #1f1f23; border-bottom-color: #38383e; }"
    "QComboBox:disabled { color: #6a6a70; border-color: #232327; }"
    "QComboBox::drop-down { border: none; width: 18px; subcontrol-origin: padding; subcontrol-position: center right; }"
    "QComboBox::down-arrow { image: url(:/icons/spin-down.svg); width: 10px; height: 10px; }"
    // Die Liste: dunkles Glas, gedeckte Auswahl (kGlassSelBot/Text) —
    // 2026-09-08 lernte das Haus, dass selection-color ohne
    // selection-background-color den Eintrag unsichtbar macht.
    "QComboBox QAbstractItemView {"
    "  background: #0e0e10; color: #dcdce1; border: 1px solid #2c2c31;"
    "  selection-background-color: #1e3d5f; selection-color: #dbe9f8; outline: none; }"
    "QComboBox QAbstractItemView::item { padding: 3px 6px; min-height: 18px; }"
    "QComboBox QAbstractItemView::item:selected { background: #1e3d5f; color: #dbe9f8; }";

// From Longpath src/gui/StyleConstants.h:1135-1146 [@551576e3]
constexpr auto kCheckBoxStyle =
    "QCheckBox { color: #dcdce1; spacing: 6px; min-height: 20px; }"   // min-height: drei Haekchen untereinander kollidierten
    "QCheckBox:disabled { color: #6a6a70; }"
    "QCheckBox::indicator { width: 14px; height: 14px;"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #000000, stop:1 #08080c);"
    "  border: 1px solid #2c2c31; border-top-color: rgba(0, 0, 0, 160);"
    "  border-bottom-color: rgba(255, 255, 255, 18); border-radius: 4px; }"
    "QCheckBox::indicator:hover { border-color: #4a7ba8; }"
    "QCheckBox::indicator:checked {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2f5f92, stop:1 #1e3d5f);"
    "  border: 1px solid #3d78b0; border-top-color: #4b88c1; }"
    "QCheckBox::indicator:checked:disabled { background: #232327; border-color: #232327; }";

// From Longpath src/gui/StyleConstants.h:1148-1158 [@551576e3]
constexpr auto kRadioButtonStyle =
    "QRadioButton { color: #dcdce1; spacing: 6px; min-height: 20px; }"
    "QRadioButton:disabled { color: #6a6a70; }"
    "QRadioButton::indicator { width: 14px; height: 14px;"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #000000, stop:1 #08080c);"
    "  border: 1px solid #2c2c31; border-top-color: rgba(0, 0, 0, 160);"
    "  border-bottom-color: rgba(255, 255, 255, 18); border-radius: 7px; }"
    "QRadioButton::indicator:hover { border-color: #4a7ba8; }"
    "QRadioButton::indicator:checked {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2f5f92, stop:1 #1e3d5f);"
    "  border: 1px solid #3d78b0; border-top-color: #4b88c1; }";

// ── Eingabefelder sind Mulden, keine Flaechen ────────────────────────
//
// Gemeldet 2026-08-16 auf Startup & Preferences: Callsign, Grid Square,
// Process Priority und FFTW Wisdom waren "fast unsichtbar -- man sieht
// nicht mehr, dass das bedienbare Felder sind".
//
// Ursache war nicht zu viel Daempfung, sondern die falsche RICHTUNG.
// Die Werte #1a2a3a (Grund) und #304050 (Rand) wurden von themed()
// korrekt auf #1a1a1e und #2c2c31 gezogen -- zwei fast gleiche
// Grautoene. Vorher trennte der Rand durch ein kraeftiges Blau von
// selbst; ohne das liegt ein Feld auf dem Panel wie eine Flaeche.
//
// HAUSSTIL beschreibt es als VERSENKT: "Versenkt (Glas) #000000 +
// inset 0 2px 8px #000". Ein Feld gehoert DUNKLER als seine Umgebung,
// nicht heller -- dann traegt der hellere Rand von selbst.
//
// Qt-Stylesheets koennen keinen inneren Schatten. Der Grund liegt
// deshalb auf kInsetBg (#08080a, dunkler als das Panel #0c0c0e) und der
// Rand auf kBorder (#2c2c31), der gegen fast Schwarz deutlich steht.
//
// Ein leeres Feld ist nicht inaktiv. Es wartet auf eine Eingabe.
// ── Eingabefelder sind VERSENKT ──────────────────────────────────────
//
// Der Betreiber, 2026-08-21, auf ein Bildschirmfoto zeigend: „hier
// siehst du gut die uebergaenge: filter, customs. im feld von 150 bis
// 2850." Gemeint sind die Eingabefelder und Rillen — weiche
// Verlaeufe, die eine Flaeche als eingelassen lesen lassen.
//
// Bei uns waren genau die flach: „background: #08080a", fest
// eingetippt. Und schlimmer — dieser Wert stand in KEINER
// Themen-Tabelle, die Felder folgten also gar keinem Thema. Im hellen
// Thema „Kreide" blieben sie schwarz.
//
// Jetzt sind es Funktionen: sunkenFill() ueber die Themenfarbe, Tiefe
// und Eckenradius aus den Form-Reglern. Die alten Konstanten sind
// ENTFERNT, nicht danebengestellt — sonst haette der Uebersetzer die
// vergessenen Aufrufstellen nicht gefunden, und die Haelfte der
// Felder waere flach geblieben.
/// Glasfeld mit Pfeilen: dieselbe versenkte Mulde wie glassFieldStyle(),
/// aber die Pfeilknoepfe bleiben (Setup-Seiten sind Formulare, dort
/// helfen sie; die Wertchips in den Applets kommen ohne aus).
// From Longpath src/gui/StyleConstants.h:1160-1224 [@551576e3]
inline QString formFieldStyle()
{
    return QStringLiteral(
        "QLineEdit, QSpinBox, QDoubleSpinBox, QPlainTextEdit, QTextEdit {"
        "  background: %1; border: 1px solid %2;"
        "  border-top-color: %3; border-bottom-color: %4;"
        "  border-radius: %5px; color: %6; padding: 3px 8px;"
        "  selection-background-color: %7; selection-color: %8;"
        "}"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus,"
        "QPlainTextEdit:focus, QTextEdit:focus { border: 1px solid %9; }"
        "QLineEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled {"
        "  color: %10; border-color: %11; }"
    ).arg(sunkenFill(kInsetBg, 8, 4),
          hexRole(kBorder),
          QLatin1String(kGlassShade),
          QLatin1String(kGlassLight),
          QString::number(kGlassChipRadius),
          hexRole(kTextPrimary),
          hexRole(kGlassSelBot),
          hexRole(kGlassSelText),
          hexRole(kAccent))
     .arg(hexRole(kDisabledText), hexRole(kDisabledBorder));
}

// From Longpath src/gui/StyleConstants.h:1230-1239 [@551576e3]
constexpr auto kSliderStyle =
    // Dieselbe Rinne wie sliderHStyle(), als Konstante fuer die Setup-
    // Seiten: versenkte Rinne, erhabener Griff mit Akzentrand.
    "QSlider::groove:horizontal { height: 4px; background: #050507;"
    " border: 1px solid #2c2c31; border-top-color: rgba(0, 0, 0, 160);"
    " border-bottom-color: rgba(255, 255, 255, 18); border-radius: 3px; }"
    "QSlider::handle:horizontal { width: 10px; height: 14px; margin: -6px 0;"
    " background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #222227, stop:1 #141417);"
    " border: 1px solid #4a7ba8; border-top-color: #5e8db8; border-radius: 5px; }"
    "QSlider::handle:horizontal:hover { background: #222227; }";

// From Longpath src/gui/StyleConstants.h:1241-1255 [@551576e3]
constexpr auto kButtonStyle =
    // Erhaben wie buttonBaseStyle(), als Konstante: Verlauf, Licht-
    // kante oben, dunkle Kante unten; gedrueckt kippt der Verlauf.
    "QPushButton {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #29292f, stop:0.55 #1a1a1e, stop:1 #0f0f11);"
    "  border: 1px solid #2c2c31; border-top-color: #38383e; border-bottom-color: #1f1f23;"
    "  border-radius: 7px; color: #dcdce1; padding: 4px 12px; }"
    "QPushButton:hover {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #42454e, stop:0.55 #2f3138, stop:1 #26282e); }"
    "QPushButton:pressed {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #0f0f11, stop:1 #29292f);"
    "  border-top-color: #1f1f23; border-bottom-color: #38383e; }"
    "QPushButton:disabled { color: #58585e; border-color: #232327;"
    "  background: #101013; }";   // flach: ein gesperrter Knopf hat keine Dicke

// Reiter (QTabWidget): die Platte darunter mit Lichtkante, die Reiter
// erhaben wie Knoepfe; der gewaehlte Reiter ist Teil der Platte —
// gleicher Grund, keine Unterkante, Text in Erstfarbe.
// From Longpath src/gui/StyleConstants.h:1256-1269 [@551576e3]
constexpr auto kTabStyle =
    "QTabWidget::pane { border: 1px solid #2c2c31; border-top-color: #38383e;"
    "  border-radius: 10px; background: #0e0e10; top: -1px; }"
    "QTabBar::tab {"
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #222227, stop:1 #141417);"
    "  color: #a6a6ac; border: 1px solid #2c2c31; border-top-color: #38383e; border-bottom: none;"
    "  border-top-left-radius: 7px; border-top-right-radius: 7px;"
    "  padding: 5px 14px; margin-right: 2px; }"
    "QTabBar::tab:hover { color: #dcdce1; }"
    "QTabBar::tab:selected { background: #0e0e10; color: #dcdce1; border-bottom: none;"
    "  margin-bottom: -1px; }";

// [Longpath kScrollBarStyle (StyleConstants.h:1271-1283) is
//  Workspace::kScrollBarStyle in gui/styles/WorkspaceStyle.h, already
//  ported with its comment.]

// From Longpath src/gui/styles/PopupMenuStyle.h:21-37 [@551576e3]
inline constexpr const char* kPopupMenu =
    "QMenu {"
    "  background: #1a2a3a;"
    "  color: #c8d8e8;"
    "  border: 1px solid #304050;"
    "}"
    "QMenu::item {"
    "  padding: 4px 20px;"
    "}"
    "QMenu::item:selected {"
    "  background: #2d5885;"
    "  color: #ffffff;"
    "}"
    "QMenu::item:disabled {"
    "  color: #607080;"
    "}";

// Build a dark QPalette from STYLEGUIDE.md constants and install it on
// the QApplication. Run AFTER QApplication::setStyle("Fusion") and
// BEFORE any widgets are constructed so children inherit the right
// defaults from creation.
// From Longpath src/gui/styles/AppTheme.h:36-97 [@551576e3]
inline void applyDarkPalette(QApplication& app)
{
    QPalette p = app.palette();

    // Durch das Theme: hexRole() liefert, was die Palette fuer diesen
    // Nereus-Wert will — ohne Theme den Wert selbst. Der Aufruf ist
    // also vor UND nach dem Laden einer Palette richtig; main.cpp ruft
    // ihn nach dem Laden noch einmal (2026-09-21).
    auto c = [](const char* hex) { return QColor(hexRole(hex)); };

    const QColor appBg        = c(kAppBg);          // #0f0f1a
    const QColor panelBg      = c(kPanelBg);        // #0a0a18
    const QColor buttonBg     = c(kButtonBg);       // #1a2a3a
    const QColor textPrimary  = c(kTextPrimary);    // #c8d8e8
    const QColor textInactive = c(kTextInactive);   // #405060
    const QColor accent       = c(kAccent);         // #00b4d8
    const QColor border       = c(kBorder);         // #205070
    const QColor disabledBg   = c(kDisabledBg);     // #1a1a2a
    const QColor disabledText = c(kDisabledText);   // #556070

    // Active and Inactive groups share colors so unfocused windows
    // still match the dark theme.
    for (auto group : { QPalette::Active, QPalette::Inactive }) {
        p.setColor(group, QPalette::Window,          appBg);
        p.setColor(group, QPalette::WindowText,      textPrimary);
        p.setColor(group, QPalette::Base,            buttonBg);
        p.setColor(group, QPalette::AlternateBase,   panelBg);
        p.setColor(group, QPalette::Text,            textPrimary);
        p.setColor(group, QPalette::Button,          buttonBg);
        p.setColor(group, QPalette::ButtonText,      textPrimary);
        p.setColor(group, QPalette::ToolTipBase,     buttonBg);
        p.setColor(group, QPalette::ToolTipText,     textPrimary);
        p.setColor(group, QPalette::Highlight,       accent);
        p.setColor(group, QPalette::HighlightedText, appBg);
        p.setColor(group, QPalette::Link,            accent);
        p.setColor(group, QPalette::LinkVisited,     accent);
        p.setColor(group, QPalette::PlaceholderText, textInactive);
        p.setColor(group, QPalette::BrightText,      QColor(Qt::white));
        // Frame shading roles — used by Fusion to draw bevels/grooves.
        p.setColor(group, QPalette::Light,           border);
        p.setColor(group, QPalette::Midlight,        border);
        p.setColor(group, QPalette::Dark,            panelBg);
        p.setColor(group, QPalette::Mid,             panelBg);
        p.setColor(group, QPalette::Shadow,          appBg);
    }

    // Disabled group — dim everything so disabled controls read as dim
    // against the dark surface instead of vanishing or flipping bright.
    p.setColor(QPalette::Disabled, QPalette::WindowText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Text,            disabledText);
    p.setColor(QPalette::Disabled, QPalette::ButtonText,      disabledText);
    p.setColor(QPalette::Disabled, QPalette::Button,          disabledBg);
    p.setColor(QPalette::Disabled, QPalette::Base,            disabledBg);
    p.setColor(QPalette::Disabled, QPalette::HighlightedText, disabledText);
    p.setColor(QPalette::Disabled, QPalette::PlaceholderText, disabledText);

    app.setPalette(p);
}

// Minimal app-wide QSS baseline for things QPalette can't fully cover:
//   - QGroupBox::title  — subcontrol; doesn't reliably inherit color from
//                         the parent QGroupBox rule on Fusion
//   - QToolTip          — Qt ignores the palette ToolTip roles unless
//                         a stylesheet is also present
//   - QMenu             — so any future menu added without the explicit
//                         kPopupMenu helper still renders dark, not
//                         dark-on-dark or white-on-light (issue #98 family)
//
// Per-page setStyleSheet calls override these whenever they want; this
// only fills the gap when nothing more specific applies.
// From Longpath src/gui/styles/AppTheme.h:99-164 [@551576e3]
inline void applyAppBaselineQss(QApplication& app)
{
    app.setStyleSheet(themed(QStringLiteral(
        "QToolTip {"
        "  color: %1; background: %2; border: 1px solid %3;"
        "  padding: 3px; border-radius: 6px;"
        "}"
        "QGroupBox::title { color: %4; }"
        "QMenu { background: %2; color: %1; border: 1px solid %3; }"
        "QMenu::item { padding: 4px 20px; }"
        "QMenu::item:selected { background: #2d5885; color: #ffffff; }"
        "QMenu::item:disabled { color: %5; }"
        // Spinbox arrows: Fusion paints these with palette.foreground()
        // darkened, which is unreadable against our dark surfaces. Point
        // QSS at the bundled SVG triangles (kTextPrimary fill) so every
        // spinbox in the app gets the same legible arrow regardless of
        // whether the parent page has its own QSpinBox styling. The
        // buttons are widened so the arrow image isn't clipped to
        // Fusion's narrow default subcontrol width.
        "QSpinBox::up-button, QDoubleSpinBox::up-button {"
        "  subcontrol-origin: border; subcontrol-position: top right;"
        "  width: 20px; border: none;"
        "}"
        "QSpinBox::down-button, QDoubleSpinBox::down-button {"
        "  subcontrol-origin: border; subcontrol-position: bottom right;"
        "  width: 20px; border: none;"
        "}"
        "QSpinBox::up-arrow, QDoubleSpinBox::up-arrow {"
        "  image: url(:/icons/spin-up.svg); width: 14px; height: 14px;"
        "}"
        "QSpinBox::down-arrow, QDoubleSpinBox::down-arrow {"
        "  image: url(:/icons/spin-down.svg); width: 14px; height: 14px;"
        "}"
    ).arg(QString::fromLatin1(kTextPrimary),
          QString::fromLatin1(kButtonBg),
          QString::fromLatin1(kBorder),
          QString::fromLatin1(kTitleText),
          QString::fromLatin1(kTextInactive))
        // ── Formularstil, Glas & Tiefe (2026-09-18) ─────────────────
        // Auswahlfelder, Haekchen, Wahlpunkte, Eingabefelder, Rinnen,
        // Gruppen und Knoepfe bekommen hier ihre Bauform, damit ein
        // Widget, das nirgends eigens gestylt wird (die halbe Setup-
        // Seite), trotzdem im Haus-Look erscheint. Seiten- und Widget-
        // Stylesheets duerfen weiter darueberlegen — sie sagen seit
        // heute dasselbe (Style::kComboStyle & Co.).
        + QLatin1String(kComboStyle)
        + QLatin1String(kCheckBoxStyle)
        + QLatin1String(kRadioButtonStyle)
        + QLatin1String(kGroupBoxStyle)
        + formFieldStyle()
        + sliderHStyle()
        + sliderVStyle()
        + QLatin1String(kButtonStyle)   // ohne font-size: Setup-Knoepfe erben ihre Schrift
        + QLatin1String(kScrollBarStyle)));   // themed(): Palettenrollen auch im Formularstil
}

} // namespace NereusSDR::Preview::LongpathLook
