// src/gui/instruments/BarInstrument.h

// --- From src/gui/instruments/BarInstrument.h ---
// =================================================================
// src/gui/instruments/BarInstrument.h  (Longpath)
// =================================================================
//
// Longpath-original.
//
// ── Das Balkeninstrument ─────────────────────────────────────────────
//
// OE5SOS, 2026-08-17: „Dasselbe in der vorhandenen Balkenform als
// eigenes Widget. HGauge ist da; das Widget ist die Fassung drumherum
// mit Kopf und Fusszeile."
//
// ── Warum es nicht HGauge einbettet ──────────────────────────────────
//
// HGauge malt eine Mulde mit RANDLINIE und füllt sie in drei Zonen
// massiv aus (HGauge.cpp:60-115). Es kennt weder den Innenschatten
// noch den auslaufenden Verlauf noch die Glut — die drei Mittel, die
// dieses Widget mit dem Zeigerinstrument teilen soll.
//
// HGauge dafür umzubauen hiesse, zwanzig bestehende Gauges in zehn
// Applets auf einmal umzugestalten. Das ist eine sichtbare Änderung
// und gehört dem Betreiber, nicht diesem Auftrag. Also zeichnet dieses
// Widget über LinearSpine + InstrumentPainter — dieselbe Form wie
// HGauge, dieselbe Handschrift wie der Zeiger, und HGauge bleibt
// unangetastet.
//
// ── Eine Abweichung vom älteren Entwurf, bewusst ─────────────────────
//
// zeiger-verfeinert.html sagt im Fliesstext: „Der Balken bleibt, wie
// er ist" — also massive Füllung. Der Auftrag desselben Tages sagt
// darüber: „Gemeinsame Handschrift für alle drei — dieselben drei
// Mittel." Hier gilt der Auftrag, also auch beim Balken der
// auslaufende Verlauf. Genau so ein Punkt gehört auf den Schirm
// gesehen, bevor er stehen bleibt.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-17 — Created in C++20/Qt6 for NereusSDR by Martin Fischer,
//                 AI-assisted via Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/instruments/BarInstrument.h  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/instruments/BarInstrument.h
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
//   2026-10-12 - Shared accessory display readings and independent legal
//                 marker / display scale by J.J. Boyd (KG4VCF),
//                 AI-assisted via OpenAI Codex.
//   2026-10-07 - Ported in C++20/Qt6 for NereusSDR by J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-07 - Footer fit (resizeEvent, fullHeight, fitFooter): the
//                 footer no longer overlaps the scale numbers at the
//                 preferred and minimum sizes. J.J. Boyd (KG4VCF),
//                 AI-assisted via Anthropic Claude Code.
//   2026-10-08 - Adds hasSecondValue() and clearSecondValue().
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
//   2026-10-12 - A reading of the caller's own (setReading, setValue,
//                 value), the size of the compact form's number
//                 (setCompactValuePx), a small second reading after the
//                 compact form's name (setCompactNote) and the footer
//                 without its peak and limit text (setFooterDetailVisible),
//                 for the tuner and amplifier panels.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

#pragma once

#include "gui/instruments/PeakHold.h"
// NereusSDR: the instrument holds a ReadingDescriptor of its own, see
// setReading().
#include "gui/instruments/ReadingSource.h"

#include <QString>
#include <QWidget>

#include <optional>

namespace NereusSDR {

struct ReadingDescriptor;
class InstrumentFooter;

// From Longpath src/gui/instruments/BarInstrument.h:53 [@551576e3]
class BarInstrument : public QWidget {
    Q_OBJECT

public:
    explicit BarInstrument(QWidget* parent = nullptr);

    bool setPrimary(int bindingId);
    int  primary() const { return m_primary; }

    /// Die zweite Anzeige steht beim Balken als ZWEITE MULDE darunter,
    /// nicht als zweite Marke in derselben. Ein Balken hat keine
    /// Richtung, in die ein zweiter Zeiger ausweichen könnte.
    bool setSecondary(int bindingId);
    int  secondary() const { return m_secondary; }

    void onReading(int bindingId, double value);

    // NereusSDR: a reading of the caller's own, in place of one from the
    // meter binding table (setPrimary): the scale, limit and text rules
    // in `d`, and `name` where a bound reading shows its Thetis name. A
    // panel that owns its numbers (the tuner and amplifier panels) feeds
    // them with setValue(). A value already shown stays, so a change of
    // scale does not blank the row; the held peak stays with it.
    // setPrimary() goes back to the binding table.
    void setReading(const ReadingDescriptor& d, const QString& name);
    // Override a bound primary scale without changing its binding or value.
    void setPrimaryScale(const ReadingDescriptor& d);
    bool hasOwnReading() const { return m_ownReading.has_value(); }
    // NereusSDR: the primary reading as shown, the caller's own or the
    // bound one; nullptr when there is neither.
    const ReadingDescriptor* reading() const { return primaryReading(); }
    // NereusSDR: the name the primary reading is shown under.
    QString readingName() const;
    // NereusSDR: the primary reading's value, as onReading() sets it for
    // a bound reading. value() is the last one set; hasValue() says
    // whether it is shown or the instrument is in its no-reading state.
    void setValue(double value);
    double value() const { return m_value; }

    /// Zurück in den Zustand „keine Messung". Siehe
    /// NeedleInstrument::clearValue — dieselbe Regel, damit beide
    /// Formen dasselbe sagen.
    void clearValue();

    bool hasValue() const { return m_hasValue; }
    // NereusSDR: whether the secondary reading has a value, and dropping
    // only that value (a no-meter reading on the secondary binding).
    bool hasSecondValue() const { return m_hasSecond; }
    void clearSecondValue();

    /// Die Spitzenhaltung — dieselbe Klasse wie beim Zeigerinstrument,
    /// damit eine Einstellung aus dem Rechtsklickmenue beide Ansichten
    /// erreicht und nicht nur die gerade sichtbare.
    // ── Segmentierte Form ────────────────────────────────────────────
    //
    // Der Betreiber, 2026-08-20, mit einem Bildschirmfoto der Vorlage:
    // das S-Meter als Kette einzelner Felder. Der Balken war bis dahin
    // ein durchgehender Verlauf.
    //
    // Beides bleibt: Segmente liest man als Menge, ohne die Skala zu
    // lesen; der Verlauf zeigt kleine Aenderungen genauer. Die Wahl
    // steht im Rechtsklickmenue des Instruments und wird dort gemerkt.
    void setSegmented(bool on);
    bool isSegmented() const { return m_segmented; }

    // ── Die Roehre ───────────────────────────────────────────────────
    //
    // Der Betreiber am 2026-08-23, nach sechs Tiefenbehandlungen auf
    // einem Vergleichsblatt: "baue D5 einmal mit SWR und mit
    // Stehwelle. danach als option bei den einzelnen widget hinzu."
    //
    // D5 war die Roehre: die Mulde traegt einen Querverlauf und
    // woelbt sich dem Auge entgegen, statt sich einzugraben. Sie ist
    // UNABHAENGIG von der Segmentierung — beides laesst sich
    // kombinieren, und die Kette in der Roehre ist die Fassung, die
    // der Vorlage am naechsten kommt.
    //
    // Wie die Segmentierung steht die Wahl im Rechtsklickmenue und
    // wird dort gemerkt.
    void setTube(bool on);
    bool isTube() const { return m_tube; }

    // ── Knappe Fassung ───────────────────────────────────────────────
    //
    // Fuer Balken, die als ZUSATZZEILE unter etwas anderem stehen —
    // etwa im Frequenz-Widget. Dort ist die Zeile rund 30 Punkte hoch,
    // und die Fusszeile ("Spitze 5 · Grenze 100") legt sich dann ueber
    // die Skalenzahlen. Auf dem Bildschirmfoto des Betreibers vom
    // 2026-08-23 ist das gut zu sehen: "Grenze 100" steht quer ueber
    // der 30 und der 60.
    //
    // Knapp heisst: keine Fusszeile. Die Zahl selbst steht ohnehin
    // rechts, und Spitze und Grenze liest man in einer Zusatzzeile
    // nicht — dafuer gibt es die eigenstaendige Anzeige.
    void setCompact(bool on);
    bool isCompact() const { return m_compact; }

    // NereusSDR: the pixel size of the number in the compact form. 0
    // keeps the look's own size (Style::kFontCaption).
    void setCompactValuePx(int px);
    int  compactValuePx() const { return m_compactValuePx; }

    // NereusSDR: a small second reading after the compact form's name: a
    // hairline, `label` a size down from the name and `number` in the dim
    // reading color. Both empty takes it away. The full form does not
    // draw it.
    void setCompactNote(const QString& label, const QString& number);
    QString compactNoteLabel() const { return m_noteLabel; }
    QString compactNoteNumber() const { return m_noteNumber; }

    // NereusSDR: false leaves the footer's middle text ("PK ... LIM ...")
    // empty, for a width where it does not fit beside the number. The
    // marks on the bar still show the peak and the limit.
    void setFooterDetailVisible(bool on);
    bool footerDetailVisible() const { return m_footerDetail; }

    PeakHold&       peakHold()       { return m_peak; }
    const PeakHold& peakHold() const { return m_peak; }
    void resetPeak() { m_peak.reset(m_value); update(); }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // From Longpath src/gui/instruments/BarInstrument.h:139 [@551576e3]
    // Betreiber, 2026-08-30: das Frequenz-Fenster liess sich nicht so
    // weit ziehen, wie FrequencyInstrument allein erlaubt haette --
    // die dort eingebettete SWR-Zusatzzeile (ein gewoehnliches
    // BarInstrument, siehe FrequencyApplet.cpp) hatte mit 180 einen
    // hoeheren Bodenwert als die Ziffernzeile selbst und bestimmte
    // damit die tatsaechliche Grenze des ganzen Fensters. 90 ist die
    // kleinste Breite, bei der paintInto()s eigene Wache
    // (box.width() < 40.0) noch nicht zuschlaegt (90 - 24 Rand = 66).
    static constexpr int kMinWidth = 90;

public:
    /// Malt das Instrument fuer eine verlangte Groesse in einen fremden
    /// Maler. `bare` laesst die Fusszeile weg — fuer die Einblendung im
    /// Panadapter. Das Widget selbst wird dabei nicht angefasst.
    void paintInto(QPainter& painter, QSize forSize, bool bare);

protected:
    void paintEvent(QPaintEvent*) override;
    // NereusSDR: hides the footer when it does not fit, see fitFooter().
    void resizeEvent(QResizeEvent*) override;

private:
    void refreshFooter();
    // NereusSDR: the primary reading, the caller's own (setReading) or
    // the one the binding table has for m_primary; and the name a reading
    // is shown under, the caller's for its own reading and the Thetis
    // name for any other.
    const ReadingDescriptor* primaryReading() const;
    QString nameOf(const ReadingDescriptor& d) const;
    // NereusSDR: the height the trough, scale numbers and footer need
    // together, and the footer shown only when that height is there.
    int fullHeight() const;
    void fitFooter();
    void paintOne(class QPainter& p, const QRectF& area,
                  const ReadingDescriptor& d, double value,
                  bool withGlow, bool hasValue);

    InstrumentFooter* m_footer{nullptr};

    int    m_primary{-1};
    int    m_secondary{-1};
    double m_value{0.0};
    double m_secondValue{0.0};
    bool m_segmented{false};
    bool m_tube{false};
    bool m_compact{false};
    // NereusSDR: see setReading(), setCompactValuePx(), setCompactNote()
    // and setFooterDetailVisible().
    std::optional<ReadingDescriptor> m_ownReading;
    QString m_ownName;
    int     m_compactValuePx{0};
    QString m_noteLabel;
    QString m_noteNumber;
    bool    m_footerDetail{true};
    PeakHold m_peak;

    /// Ohne gültige Messung: kein Verlauf, keine Glut, keine Wertkante,
    /// Wert als Gedankenstrich. Mulde und Teilung bleiben. Begründung
    /// bei NeedleInstrument.
    bool m_hasValue{false};
    bool m_hasSecond{false};
};

} // namespace NereusSDR
