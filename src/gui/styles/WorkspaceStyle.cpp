// src/gui/styles/WorkspaceStyle.cpp

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
// src/gui/styles/WorkspaceStyle.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/StyleConstants.h and src/gui/styles/ThemeQss.cpp at
//   revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   src/gui/StyleConstants.h carries the AetherSDR copyright and
//   GPLv3 notice reproduced above; src/gui/styles/ThemeQss.cpp has
//   no copyright or GPL header. Longpath's project LICENSE is GPLv3.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//                 hexRole(kX) becomes role("name", kX) against the
//                 built-in WorkspacePalette.
//   2026-10-08 - Upstream rationale and its section title restored above
//                 shiftL(); the licence note corrected.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-08 - scrollBarStyle(): kScrollBarStyle at a given thickness.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Floor font factories at JJ's approved 11 px minimum;
//                 original tracking retained. J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
// =================================================================

#include "WorkspaceStyle.h"
#include "WorkspacePreview.h"

#include "WorkspacePalette.h"

#include <QLatin1String>
#include <QPainter>
#include <QRect>
#include <QStringLiteral>

#include <algorithm>

namespace NereusSDR::Workspace {

// From Longpath src/gui/styles/ThemeQss.cpp:284-288 [@551576e3]
QString role(const char* roleName, const char* fallback)
{
    return WorkspacePalette::glassAndDepth().qss(QString::fromLatin1(roleName),
                                                 QString::fromLatin1(fallback));
}

// From Longpath src/gui/StyleConstants.h:1350-1358 [@551576e3]
QFont capsFont(const QFont& base, int px)
{
    QFont f = base;
    f.setPixelSize(px);
    f.setWeight(QFont::DemiBold);
    f.setCapitalization(QFont::AllUppercase);
    f.setLetterSpacing(QFont::AbsoluteSpacing, px * kCapsTracking);
    return WorkspacePreview::readableFont(f);
}

// From Longpath src/gui/StyleConstants.h:1383-1401 [@551576e3]
QFont monoFont(const QFont& base, int px, QFont::Weight w)
{
    QFont f = base;
    f.setPixelSize(px);
    f.setWeight(w);
    // setFamilies statt setFamily: die ganze Rueckfallkette, dieselbe wie
    // LP_MONO_QSS. Mit setFamily() stand hier nur "Menlo" -- auf einem
    // Rechner ohne Menlo nahm Qt die naechstbeste Schrift, und das war
    // nicht zwingend eine dicktengleiche.
    f.setFamilies({QStringLiteral("Menlo"),
                   QStringLiteral("DejaVu Sans Mono"),
                   QStringLiteral("monospace")});
    // Menlo gibt es nur auf dem Mac. Ohne diesen Hinweis nahm Qt unter
    // Windows/Linux eine proportionale Schrift, und die Stellen tanzten
    // doch (2026-09-27).
    f.setStyleHint(QFont::TypeWriter);
    f.setFixedPitch(true);
    return WorkspacePreview::readableFont(f);
}

// From Longpath src/gui/StyleConstants.h:544-553 [@551576e3]
void paintInsetTop(QPainter& p, const QRect& r)
{
    static const int kShade[] = {kGlassShadeAlpha, 70, 40, 18};
    for (int i = 0; i < 4; ++i) {
        p.setPen(QColor(0, 0, 0, kShade[i]));
        p.drawLine(r.left(), r.top() + 1 + i, r.right(), r.top() + 1 + i);
    }
    p.setPen(QColor(255, 255, 255, kGlassLightAlpha));
    p.drawLine(r.left(), r.top(), r.right(), r.top());
}

// From Longpath src/gui/StyleConstants.h:575-589 [@551576e3]
void paintGlassChip(QPainter& p, const QRect& box, int radius)
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QColor(role("border", kBorder)));
    p.setBrush(QColor(0, 0, 0));
    p.drawRoundedRect(box, radius, radius);
    p.setClipRect(box.adjusted(1, 1, -1, -1));
    p.setRenderHint(QPainter::Antialiasing, false);
    p.setPen(QColor(255, 255, 255, kGlassLightAlpha));
    p.drawLine(box.left() + 1, box.top() + 1, box.right() - 1, box.top() + 1);
    p.setPen(QColor(0, 0, 0, 90));
    p.drawLine(box.left() + 1, box.top() + 2, box.right() - 1, box.top() + 2);
    p.restore();
}

// Shared Stylesheet Fragments
// ── Flaechen mit Verlauf statt flacher Fuellung ──────────────────────
//
// Der Betreiber, 2026-08-21, mit einem Bildschirmfoto der Vorlage: „die
// uebergaenge im hintergrund bei den widget wirken sehr gut" — und
// nach dem Entwurfsblatt: „sehe keinen unterschied beim den pdf.
// mache es wie bei [der Vorlage]."
//
// Der Unterschied ist benennbar: in der Vorlage ist keine Flaeche eine
// Farbe, sondern ein Verlauf von oben nach unten, mit einer helleren
// Oberkante. Das Auge liest das als Licht von oben und damit als
// Flaeche mit DICKE — ein Stueck Blech statt eines Rechtecks. Bei uns
// war alles flach.
//
// Zwei Regeln, mehr steckt nicht dahinter:
//
//   AUFGESETZT (Platte, Kopfleiste, Knopf): oben heller, unten dunkler,
//   eine helle Linie an der Oberkante.
//
//   VERSENKT (Mulde, Eingabefeld, Rille): genau ANDERSHERUM — oben
//   dunkel, unten heller. Dasselbe Mittel, umgedreht, und die Flaeche
//   liegt drin statt drauf.
//
// Kein neuer Farbton: alle Stufen kommen aus der Familie, die die
// Palette ohnehin fuehrt. Und alles an EINER Stelle — wer hier etwas
// aendert, aendert das ganze Programm mit.
//
// Ein senkrechter Verlauf in Qt-Stylesheets:
//   qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 …, stop:1 …)
// Die Stufen werden aus der Grundfarbe GERECHNET, nicht getippt.
// Grund: die Themenverwaltung tauscht Farben ueber ihren Hexwert aus
// (ThemeQss). Ein von Hand gesetztes "#22222a" kennt sie nicht — im
// hellen Thema „Kreide" waere der Knopf dunkel geblieben. Also: die
// Grundfarbe durch hexRole() durchs Thema schicken und die helle und
// dunkle Stufe daraus ableiten. Dann stimmt jedes Thema von selbst,
// auch die, die es noch nicht gibt.
// Die Stufen werden AUS der Grundfarbe gerechnet, nicht getippt.
//
// Zwei Gruende. Erstens die Themenverwaltung: sie tauscht Farben ueber
// ihren Hexwert aus (ThemeQss). Ein von Hand gesetztes "#22222a" kennt
// sie nicht — im hellen Thema „Kreide" waere der Knopf dunkel
// geblieben. Also geht die Grundfarbe durch hexRole() und die Stufen
// werden daraus abgeleitet; dann stimmt jedes Thema von selbst, auch
// die, die es noch nicht gibt.
//
// Zweitens die Rechenart. Der erste Versuch nahm QColor::lighter(118),
// und das ist eine MULTIPLIKATION der Helligkeit. Auf unserem fast
// schwarzen Grund (#1a1a1e, Helligkeit 26) sind 18 Prozent davon
// gerade fuenf Stufen — der Testknopf kam auf einen Abstand von 6,
// und genau das war der Fehler, den der Betreiber am Entwurfsblatt
// gesehen hat: „sehe keinen unterschied". Auf Dunkel muss ADDIERT
// werden. shiftL() legt einen festen Betrag drauf, unabhaengig davon,
// wie dunkel der Grund ist.
// From Longpath src/gui/StyleConstants.h:697-703 [@551576e3]
QString shiftL(const QColor& base, int deltaL)
{
    QColor c = base.toHsl();
    const int l = std::clamp(c.lightness() + deltaL, 0, 255);
    c.setHsl(c.hslHue(), c.hslSaturation(), l, c.alpha());
    return c.name();
}

// From Longpath src/gui/StyleConstants.h:949-954 [@551576e3]
QString glassPanelFill()
{
    return QStringLiteral(
        "qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 %1, stop:1 %2)")
        .arg(role("panel-top", kGlassPanelTop), role("panel-bottom", kGlassPanelBot));
}

// From Longpath src/gui/StyleConstants.h:1052-1066 [@551576e3]
QString titleBarStyle()
{
    // Die Kopfleiste soll AUFLIEGEN, nicht eingelassen sein: etwas
    // kraeftiger als die Platte darunter, und eine dunkle Unterkante,
    // die sie von ihr abhebt. Die Toene stehen weiter in der Palette
    // (kTitleGrad*) — hier ist nur der Schluss dunkler geworden.
    // Glas & Tiefe (2026-09-17): dazu eine Lichtkante am oberen Rand,
    // damit der Kopf eine Oberflaeche hat und nicht nur einen Verlauf.
    return QStringLiteral(
        "background: qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        " stop:0 %1, stop:0.5 %2, stop:1 %3);"
        " border-bottom: 1px solid %4; border-top: 1px solid %5;"
    ).arg(QLatin1String(kTitleGradTop), QLatin1String(kTitleGradMid),
          QLatin1String(kTitleGradBot), QLatin1String(kTitleBorder),
          QLatin1String(kGlassLight));
}

QString scrollBarStyle(int thicknessPx)
{
    // [original comment from StyleConstants.h:1271-1272 is at kScrollBarStyle in WorkspaceStyle.h]
    // From Longpath src/gui/StyleConstants.h:1273-1283 [@551576e3]
    QString bars = QString::fromLatin1(kScrollBarStyle);
    bars.replace(QLatin1String("width: 10px; margin: 0;"),
                 QStringLiteral("width: %1px; margin: 0;").arg(thicknessPx));
    bars.replace(QLatin1String("height: 10px; margin: 0;"),
                 QStringLiteral("height: %1px; margin: 0;").arg(thicknessPx));
    return bars;
}

} // namespace NereusSDR::Workspace
