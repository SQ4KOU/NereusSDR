// src/gui/instruments/InstrumentFooter.h

// --- From src/gui/instruments/InstrumentFooter.h ---
// =================================================================
// src/gui/instruments/InstrumentFooter.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Eine Fusszeile für beide Instrumente ─────────────────────────────
//
// OE5SOS, 2026-08-17: „Die Fusszeile ist in beiden Instrumenten gleich
// aufgebaut, damit der Blick beim Wechsel nicht springt."
//
// Deshalb ein eigenes Widget und keine zweimal gebaute Zeile. Zwei
// Aufbauten, die gleich AUSSEHEN SOLLEN, sind zwei Aufbauten, die
// auseinanderlaufen werden — und der Sprung, den der Betreiber
// vermeiden will, ist genau ein Pixel Unterschied in der Grundlinie.
//
// Aufbau, aus dem Entwurf (zeiger-verfeinert.html, .foot):
//   links   die Grösse, Versalzeile in Skalenfarbe
//   mittig  Spitze und Grenze, klein und matt
//   rechts  der Wert, gross und in Messwertfarbe
//
// =================================================================
// Modification history (Longpath):
//   2026-08-17 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/instruments/InstrumentFooter.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/instruments/InstrumentFooter.h
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - setDetailVisible(): the footer without its peak and
//                 limit text. J.J. Boyd (KG4VCF), AI-assisted via
//                 Anthropic Claude Code.
// =================================================================

#pragma once

#include <QWidget>

class QLabel;

namespace NereusSDR {

// From Longpath src/gui/instruments/InstrumentFooter.h:36 [@551576e3]
class InstrumentFooter : public QWidget {
    Q_OBJECT

public:
    int valueMinimumWidthForTest() const;
    explicit InstrumentFooter(QWidget* parent = nullptr);

    /// Die Grösse links, z. B. „Stehwelle".
    void setCaption(const QString& text);

    /// Der Wert rechts, schon formatiert.
    void setValueText(const QString& text);

    /// Die Farbe des Werts — sie wechselt mit Zeiger, Sektor und Glut
    /// auf danger, damit die Zahl nicht ruhig bleibt, während das
    /// Instrument warnt.
    void setValueColour(const QColor& c);

    /// Die mittlere Zeile. Leere Zeichenketten lassen den jeweiligen
    /// Teil weg — es gibt Grössen ohne Grenze.
    void setPeakAndLimit(const QString& peakText, const QString& limitText);

    // NereusSDR: false leaves the middle line empty whatever
    // setPeakAndLimit() is given, for a width where it does not fit
    // beside the value. The next setPeakAndLimit() after true fills it.
    void setDetailVisible(bool on);
    bool detailVisible() const { return m_detailVisible; }

private:
    QLabel* m_caption{nullptr};
    QLabel* m_middle{nullptr};
    QLabel* m_value{nullptr};
    bool    m_detailVisible{true};   // NereusSDR: see setDetailVisible()
    int     m_valueWidth{0};   // groesste bisher gezeigte Breite (2026-09-27)
};

} // namespace NereusSDR
