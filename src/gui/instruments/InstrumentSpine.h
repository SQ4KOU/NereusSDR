// src/gui/instruments/InstrumentSpine.h

// --- From src/gui/instruments/InstrumentSpine.h ---
// =================================================================
// src/gui/instruments/InstrumentSpine.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Die Geometrie zweimal, die Mittel einmal ─────────────────────────
//
// OE5SOS, 2026-08-17: „sag, welchen gemeinsamen Zeichenweg du für
// Mulde, Verlauf und Glut vorsiehst — die drei Mittel sollen an einer
// Stelle stehen, nicht dreimal."
//
// Die drei Mittel stehen in InstrumentPainter, genau einmal. Sie
// müssen aber zwei Formen bedienen: den Bogen (Zeigerinstrument,
// VFO B) und die Gerade (Balken, VFO A). Der Betreiber hat es selbst
// so beschrieben: „die Rille der Zeiger, gerade gezogen".
//
// Also trennt diese Datei, was verschieden ist, von dem, was gleich
// ist. Eine Spine beantwortet, WO etwas liegt; der Painter weiss, WIE
// es aussieht. Fünf Ansichten fallen daraus:
//
//   Zeigerinstrument   ArcSpine
//   Balkeninstrument   LinearSpine
//   VFO A Bandstreifen LinearSpine, breit und flach
//   VFO B flacher Bogen ArcSpine, grosser Radius, kleiner Sweep
//   VFO C nur die Zahl  keine Spine — nur die Glut
//
// ── Anteile, keine Werte ─────────────────────────────────────────────
//
// Eine Spine kennt keine Messgrössen. Sie rechnet mit Anteilen 0..1;
// die Umrechnung Wert → Anteil macht ReadingDescriptor::fraction(),
// weil dort die Kennlinie steht (die Empfangsskala ist nicht linear).
// Eine Geometrie, die Skalen kennt, wäre die dritte Stelle, an der
// eine S-Stauchung stehen könnte.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-17 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/instruments/InstrumentSpine.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/instruments/InstrumentSpine.h
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
// =================================================================

#pragma once

#include <QBrush>
#include <QLineF>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

namespace NereusSDR {

// From Longpath src/gui/instruments/InstrumentSpine.h:69 [@551576e3]
// ── Der Massstab, in dem die Entwürfe notiert sind ───────────────────
//
// ~/Downloads/zeiger-verfeinert.html zeichnet in ein festes Feld von
// 520 × 190 mit Radius 148 und Rillenbreite 13. Der Browser skaliert
// dieses Feld als GANZES: Strichstärken, Nabe und Teilstriche wachsen
// dort selbstverständlich mit dem Bogen mit.
//
// Der erste Port hat nur Radius und Drehpunkt als Verhältnis gerechnet
// und Zeigerbreite, Nabenradius, Tick-Längen und Beschriftungsabstand
// in absoluten Pixeln stehen lassen. Bei genau 520 Pixeln Breite stimmt
// das; bei jeder anderen nicht — und weil die Instrumente in der
// Applet-Spalte schmaler stehen, war der Zeiger relativ zu dünn. Der
// Betreiber hat es als „dünne gerade Striche" gesehen, 2026-08-18:
// „Das ist keine Gestaltung, das ist ein Proportionsfehler."
//
// Darum: alle Masse der Entwürfe stehen im Code als Verhältnis zu
// dieser Zahl und werden mit Spine::unit() wieder in Pixel gerechnet.
inline constexpr qreal kDesignUnit = 148.0;

// From Longpath src/gui/instruments/InstrumentSpine.h:75 [@551576e3]
/// Die Rillenbreite des Entwurfs, als Anteil von kDesignUnit. Die Rille
/// ist in beiden Entwürfen das Mass, an dem die Strichstärken hängen —
/// nicht der Radius, denn der flache VFO-Bogen hat einen sehr grossen
/// Radius bei schmaler Rille und bekäme sonst fingerdicke Striche.
inline constexpr qreal kTroughOfUnit = 13.0 / kDesignUnit;

// From Longpath src/gui/instruments/InstrumentSpine.h:77 [@551576e3]
class Spine {
public:
    virtual ~Spine() = default;

    /// Die Bezugslänge dieser Geometrie in Pixeln. Ein Mass aus dem
    /// Entwurf wird zu `x / kDesignUnit * s.unit()`.
    virtual qreal unit() const = 0;

    // ── Die Mulde ────────────────────────────────────────────────────

    /// Der Pfad der Rille, zu STREICHEN mit troughWidth().
    virtual QPainterPath troughPath() const = 0;
    virtual qreal        troughWidth() const = 0;

    /// Die dunkle Kante an der oberen bzw. äusseren Begrenzung der
    /// Mulde. Ebenfalls zu streichen, mit shadowWidth().
    virtual QPainterPath shadowPath() const = 0;
    virtual qreal        shadowWidth() const = 0;

    /// Der Abschnitt der Rille von Anteil a bis b — für den roten
    /// Bereich oberhalb der Schwelle. Ebenfalls zu streichen.
    virtual QPainterPath troughSpan(qreal a, qreal b) const = 0;

    // ── Der auslaufende Verlauf ──────────────────────────────────────

    /// Die Fläche, die der Verlauf bis zum Anteil f bedeckt — zu
    /// FÜLLEN mit fadeBrush().
    ///
    /// Was das ist, entscheidet die Geometrie, und die beiden Entwürfe
    /// meinen Verschiedenes: beim Bogen ist es der Sektor vom
    /// Drehpunkt bis unter die Rille („er trägt die Fläche, ohne sie
    /// zu füllen"), bei der Geraden das Innere der Mulde selbst.
    virtual QPainterPath fillArea(qreal f) const = 0;

    /// Der Pinsel dazu: innen null, aussen etwa ein Viertel.
    ///
    /// Die ACHSE ist geometrieabhängig, und das ist keine Nachlässigkeit
    /// — sie steht in den Entwürfen des Betreibers so. Beim Bogen läuft
    /// der Verlauf radial vom Drehpunkt nach aussen, bei der Geraden
    /// längs der Werteachse. „Aussen" heisst in beiden Formen „fern vom
    /// Anfang", nur liegt der Anfang einmal im Drehpunkt und einmal am
    /// linken Rand.
    virtual QBrush fadeBrush(const QColor& c) const = 0;

    // ── Die Glut ─────────────────────────────────────────────────────

    /// Die Fläche, über die die Glut verläuft.
    virtual QRectF glowBounds() const = 0;

    /// Beschneidung der Glut, oder ein leerer Pfad für „nicht
    /// beschneiden". Beim Bogen ist das die halbe Scheibe über dem
    /// Drehpunkt — die Glut soll „über gut die halbe Höhe reichen und
    /// dort verschwinden", nicht als voller Kreis unter das Instrument
    /// laufen.
    virtual QPainterPath glowClip() const = 0;

    // ── Teilung und Marken ───────────────────────────────────────────

    /// Teilstrich bei Anteil f, `len` lang, von der Rille nach innen.
    virtual QLineF tick(qreal f, qreal len) const = 0;

    /// Ankerpunkt für eine Beschriftung bei Anteil f, `off` von der
    /// Rille entfernt. Der Text wird darum zentriert.
    virtual QPointF labelAnchor(qreal f, qreal off) const = 0;

    /// Linie quer durch die Rille bei Anteil f — für die helle Kante
    /// am Wert, die Bandkanten und die Schwellenmarke. `outward` und
    /// `inward` verlängern über die Mulde hinaus.
    virtual QLineF crossAt(qreal f, qreal outward, qreal inward) const = 0;
};

// ── Bogen ────────────────────────────────────────────────────────────

// From Longpath src/gui/instruments/InstrumentSpine.h:150 [@551576e3]
class ArcSpine : public Spine {
public:
    /// `centre` ist der Drehpunkt, `radius` die MITTE der Rille,
    /// `width` ihre Dicke. `deg0` gehört zu Anteil 0, `deg1` zu 1;
    /// gemessen wie in der Mathematik (0° = rechts, gegen den
    /// Uhrzeigersinn), also im Entwurf 168° → 12°.
    ArcSpine(QPointF centre, qreal radius, qreal width,
             qreal deg0, qreal deg1);

    /// Innenradius des Sektors. Vorgabe wie im Entwurf (12 von 148),
    /// mitwachsend; beim sehr flachen VFO-Bogen setzt der Aufrufer ihn
    /// dicht unter die Rille, sonst liefe der Sektor über das halbe
    /// Fenster.
    void setSectorInnerRadius(qreal r) { m_sectorInner = r; }

    /// Wie weit die Glut vom Drehpunkt reicht, als Anteil des Radius.
    void setGlowExtent(qreal f) { m_glowExtent = f; }

    QPointF centre() const { return m_c; }
    qreal   radius() const { return m_r; }

    /// Der Winkel zu einem Anteil — die Zeiger brauchen ihn direkt.
    qreal degreeAt(qreal f) const;
    /// Der Punkt bei Radius r und Winkel deg.
    QPointF pointAt(qreal r, qreal deg) const;

    qreal        unit() const override { return m_w / kTroughOfUnit; }
    QPainterPath troughPath() const override;
    qreal        troughWidth() const override { return m_w; }
    QPainterPath shadowPath() const override;
    qreal        shadowWidth() const override;
    QPainterPath troughSpan(qreal a, qreal b) const override;
    QPainterPath fillArea(qreal f) const override;
    QBrush       fadeBrush(const QColor& c) const override;
    QRectF       glowBounds() const override;
    QPainterPath glowClip() const override;
    QLineF       tick(qreal f, qreal len) const override;
    QPointF      labelAnchor(qreal f, qreal off) const override;
    QLineF       crossAt(qreal f, qreal outward, qreal inward) const override;

private:
    QPainterPath arcPath(qreal r, qreal d0, qreal d1) const;
    QPainterPath sectorPath(qreal rOut, qreal rIn, qreal d0, qreal d1) const;

    QPointF m_c;
    qreal   m_r{0};
    qreal   m_w{0};
    qreal   m_deg0{0};
    qreal   m_deg1{0};
    qreal   m_sectorInner{12.0};
    qreal   m_glowExtent{0.62};
};

// ── Gerade ───────────────────────────────────────────────────────────

// From Longpath src/gui/instruments/InstrumentSpine.h:205 [@551576e3]
class LinearSpine : public Spine {
public:
    /// `trough` ist das Rechteck der Mulde selbst.
    LinearSpine(QRectF trough, qreal cornerRadius = 3.0);

    QRectF troughRect() const { return m_r; }
    /// Die x-Stelle zu einem Anteil.
    qreal xAt(qreal f) const;

    /// Fest, nicht mitwachsend — und das ist Absicht. Der Entwurf der
    /// Geraden (~/Downloads/frequenz-im-zeigerstil.html) ist gegen den
    /// Code noch nicht gegengeprüft; ihn hier mitwachsen zu lassen wäre
    /// eine sichtbare Änderung an Balken und Frequenzstreifen, die
    /// niemand angesehen hat. Wenn die Gerade drankommt, steht die
    /// Stelle schon.
    qreal        unit() const override { return kDesignUnit; }
    QPainterPath troughPath() const override;
    qreal        troughWidth() const override { return m_r.height(); }
    QPainterPath shadowPath() const override;
    qreal        shadowWidth() const override { return 2.0; }
    QPainterPath troughSpan(qreal a, qreal b) const override;
    QPainterPath fillArea(qreal f) const override;
    QBrush       fadeBrush(const QColor& c) const override;
    QRectF       glowBounds() const override;
    QPainterPath glowClip() const override;
    QLineF       tick(qreal f, qreal len) const override;
    QPointF      labelAnchor(qreal f, qreal off) const override;
    QLineF       crossAt(qreal f, qreal outward, qreal inward) const override;

private:
    QRectF m_r;
    qreal  m_rad{3.0};
};

} // namespace NereusSDR
