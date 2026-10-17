// src/gui/instruments/InstrumentFooter.cpp

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

// --- From src/gui/instruments/InstrumentFooter.cpp ---
// =================================================================
// src/gui/instruments/InstrumentFooter.cpp  (Longpath)
// =================================================================
// Siehe InstrumentFooter.h — eine Fusszeile für beide Instrumente.
// =================================================================

// =================================================================
// src/gui/instruments/InstrumentFooter.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/instruments/InstrumentFooter.h
//   src/gui/instruments/InstrumentFooter.cpp
//   at revision 551576e30c42731c6d51ce005c5f23ec38be391c,
//   https://github.com/oe5sos/Longpath, primary author Martin Fischer
//   (OE5SOS). The upstream banners are copied above.
//
//   Upstream source has no top-of-file copyright or GPL header; the
//   project-level LICENSE (GPLv3) applies.
//
//   Namespace Longpath becomes NereusSDR.
//   Style:: resolves to the Longpath values in WorkspacePalette and
//   WorkspaceStyle through gui/instruments/InstrumentStyle.h, never to
//   NereusSDR::Style in gui/StyleConstants.h.
//
// =================================================================
// Modification history (NereusSDR):
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - Each label's style sheet carries the pixel size its
//                 QFont already has, so a host panel's own
//                 `QLabel { font-size: ... }` rule no longer resizes the
//                 footer; setDetailVisible() leaves the middle text empty.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#include "gui/instruments/InstrumentFooter.h"

#include "gui/instruments/InstrumentStyle.h"
#include "gui/instruments/InstrumentPainter.h"

#include <QHBoxLayout>
#include <QLabel>

namespace NereusSDR {

// NereusSDR: the Longpath values, see gui/instruments/InstrumentStyle.h.
namespace Style = ::NereusSDR::InstrumentStyle;

namespace {
// NereusSDR: a label's color sheet with the pixel size its QFont was given
// (capsFont and monoFont set one). A style sheet rule on an ancestor,
// such as an applet's `QLabel { font-size: 11px; }`, otherwise replaces
// the QFont's size and the footer changes with its host.
QString labelSheet(const QLabel* label, const QString& color)
{
    const int px = label->font().pixelSize();
    if (px <= 0) {
        return QStringLiteral("QLabel { color: %1; background: transparent; }")
            .arg(color);
    }
    return QStringLiteral("QLabel { color: %1; background: transparent; font-size: %2px; }")
        .arg(color).arg(px);
}
} // namespace

// From Longpath src/gui/instruments/InstrumentFooter.cpp:18 [@551576e3]
InstrumentFooter::InstrumentFooter(QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 7, 0, 0);
    lay->setSpacing(14);

    // Versalzeile, Laufweite .18em — HAUSSTIL.md §Die acht Regeln,
    // Regel 1. Qt-Stylesheets kennen kein letter-spacing und verwerfen
    // es kommentarlos; die Laufweite muss über QFont gesetzt werden
    // (Vorlage: makeGroupHead() in SpectrumOverlayPanel.cpp).
    m_caption = new QLabel(this);
    // Versalzeile ueber Style::capsFont — Groesse, Halbfett,
    // Grossbuchstaben und Laufweite an einer Stelle. Sie stand hier als
    // 8 px mit von Hand gerechneter Laufweite; 9 ist die benannte
    // Stufe und liegt weiterhin im Bereich, den HAUSSTIL.md nennt.
    m_caption->setFont(Style::capsFont(m_caption->font()));
    // NereusSDR: labelSheet() adds the font's own size, see above.
    m_caption->setStyleSheet(Style::themed(
        labelSheet(m_caption, QString::fromLatin1(Style::kTextScale))));
    lay->addWidget(m_caption, 0, Qt::AlignBaseline);

    m_middle = new QLabel(this);
    // Zahlen, die sich aendern -> Monospace (HAUSSTIL Regel 2).
    m_middle->setFont(Style::monoFont(m_middle->font(), Style::kFontSmall));
    // NereusSDR: labelSheet() adds the font's own size, see above.
    m_middle->setStyleSheet(Style::themed(
        labelSheet(m_middle, Instrument::measuredDim().name())));
    lay->addWidget(m_middle, 1, Qt::AlignBaseline);

    m_value = new QLabel(this);
    m_value->setFont(Style::monoFont(m_value->font(), Style::kFontReading,
                                     QFont::DemiBold));
    setValueColour(QColor(Style::role("measured", Style::kAmberText)));
    m_value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lay->addWidget(m_value, 0, Qt::AlignBaseline);
}

// From Longpath src/gui/instruments/InstrumentFooter.cpp:56 [@551576e3]
void InstrumentFooter::setCaption(const QString& text)
{
    m_caption->setText(text);
}

// From Longpath src/gui/instruments/InstrumentFooter.cpp:61 [@551576e3]
void InstrumentFooter::setValueText(const QString& text)
{
    m_value->setText(text);
    // Die Breite waechst nur (2026-09-27): "S7" nach "S9+20", "-99 dBm"
    // nach "-105 dBm" schob sonst die ganze Zeile hin und her, weil
    // niemand Platz reservierte.
    const int w = m_value->fontMetrics().horizontalAdvance(text);
    if (w > m_valueWidth) {
        m_valueWidth = w;
        m_value->setMinimumWidth(w);
    }
}

// From Longpath src/gui/instruments/InstrumentFooter.cpp:74 [@551576e3]
void InstrumentFooter::setValueColour(const QColor& c)
{
    // NereusSDR: labelSheet() adds the font's own size, see above.
    m_value->setStyleSheet(labelSheet(m_value, c.name()));
}

void InstrumentFooter::setPeakAndLimit(const QString& peakText,
                                       const QString& limitText)
{
    QStringList parts;
    // PK und LIM statt "Spitze" und "Grenze": die Ueberschrift darueber
    // ist Thetis-woertlich englisch ("RX: Signal Avg"), und "SIGNAL AVE
    // · Spitze S9" in einer Zeile war zwei Sprachen in einem Feld (Foto
    // vom 2026-09-17). Die Kuerzel sind die des Hausstils (Pegel: "MIC"
    // oben, "PK" darunter) und in beiden Sprachen lesbar.
    if (!peakText.isEmpty())  { parts << QStringLiteral("PK %1").arg(peakText); }
    if (!limitText.isEmpty()) { parts << QStringLiteral("LIM %1").arg(limitText); }
    // NereusSDR: empty when the detail is switched off, see setDetailVisible().
    m_middle->setText(m_detailVisible ? parts.join(QStringLiteral(" · ")) : QString());
}

// NereusSDR: the middle label stays in the layout (its stretch keeps the
// value at the right edge) and only its text goes.
void InstrumentFooter::setDetailVisible(bool on)
{
    m_detailVisible = on;
    if (!on) { m_middle->clear(); }
}

int InstrumentFooter::valueMinimumWidthForTest() const { return m_value->minimumWidth(); }

} // namespace NereusSDR
