// src/gui/widgets/BandwidthFilterPane.cpp

// --- From src/gui/widgets/BandwidthFilterPane.h ---
// =================================================================
// src/gui/widgets/BandwidthFilterPane.h  (Longpath)
// =================================================================
//
// Longpath-original, nach der Vorlage des Betreibers („BANDWIDTH
// FILTER" der Vorlage, Bildschirmfoto vom 2026-08-20). Die Regeln
// hinter den Zahlen kommen aus Thetis, siehe SliceModel::widthToEdges
// und ::constrainFilter.
//
// Der Durchlass EINES Empfaengers auf einer echten Frequenzachse.
//
// ── Warum nicht das vorhandene Widget ───────────────────────────────
//
// FilterPassbandWidget (RxApplet) zeichnet ein FESTES Trapez: die Form
// aendert sich nicht mit der Breite, die Kanten sitzen immer an
// derselben Stelle. Das ist ein Schema — es sagt „hier ist ein
// Filter", nicht „hier liegt er".
//
// Die Vorlage macht es anders und das ist der ganze Gewinn: die Skala
// liest 13.137 MHz, nicht „−300 Hz". Die Frage, die man wirklich hat,
// ist „liegt meine Flanke auf 13.139?" — und die beantwortet nur eine
// Achse, auf der die Frequenz steht.
//
// Beide bleiben vorerst nebeneinander stehen. Das vorhandene ist eine
// kleine Anzeige im RxApplet, dies hier eine Arbeitsflaeche. Sobald
// klar ist, dass die neue traegt, gehoert die alte zurueckgebaut —
// zwei Stellen, an denen man dasselbe zieht, sind eine zu viel.
//
// ── Drei Ziehbereiche ───────────────────────────────────────────────
//
//   Kante links / rechts   nur diese Kante, die Breite aendert sich
//   Mitte                  der ganze Durchlass, die Breite BLEIBT
//
// Der mittlere ist der Grund fuer die Flaeche: bei CW schiebt man den
// Durchlass ueber eine Stoerung, ohne die Frequenz zu verlieren.
//
// ── Was hier NICHT passiert ─────────────────────────────────────────
//
// Begrenzt wird nicht hier. Die Flaeche meldet Wunschwerte; die Regeln
// (Seitenband, Deckel, Breite-beim-Verschieben) sitzen in
// SliceModel::constrainFilter, damit sie auch fuer CAT und Tastatur
// gelten. Eine Flaeche, die selbst begrenzt, ist die zweite Rechnung,
// die beim naechsten Umbau auseinanderlaeuft.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-20 — Original fuer NereusSDR von Martin Fischer,
//                 KI-gestuetzt ueber Anthropic Claude (Cowork).
//   2026-09-17 — Neu gezeichnet in der Richtung "Glas & Tiefe"
//                 (Stilblatt 3), Achse auf runden Frequenzen.
// =================================================================

// --- From src/gui/widgets/BandwidthFilterPane.cpp ---
// =================================================================
// src/gui/widgets/BandwidthFilterPane.cpp  (Longpath)
// =================================================================
//
// Longpath-original. Begruendung steht im Header.
//
// =================================================================
// Modification history (Longpath):
//   2026-08-20 — Original fuer NereusSDR von Martin Fischer,
//                 KI-gestuetzt ueber Anthropic Claude (Cowork).
// =================================================================

// =================================================================
// src/gui/widgets/BandwidthFilterPane.cpp  (NereusSDR)
// =================================================================
//
// Ported from Longpath source:
//   src/gui/widgets/BandwidthFilterPane.h
//   src/gui/widgets/BandwidthFilterPane.cpp
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
//   2026-10-08 - Native cursors: open hand over the body and closed
//                 hand while dragging it, in place of Longpath's
//                 Qt::SizeAllCursor, which crashes Qt 6.11.0 on macOS.
//                 J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude
//                 Code.
//   2026-10-11 - Paint cost, nothing visible changed: the pane is marked
//                 opaque, a new trace repaints only the plot band, the
//                 role colors, pens, fonts and working lists are kept
//                 between paints, and the ground under the curve is
//                 stamped from a kept picture where that is exact.
//                 tst_bandwidth_filter_pane_paint holds every frame
//                 against a copy of the drawing as it was. J.J. Boyd
//                 (KG4VCF), AI-assisted via Anthropic Claude Code.
// =================================================================

// Modification history (NereusSDR):
//   2026-10-12 - Floor cached painter fonts only in the workspace look.
//                 J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.

#include "gui/styles/WorkspacePreview.h"
#include "gui/widgets/BandwidthFilterPane.h"

#include "gui/instruments/InstrumentStyle.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <array>
#include <cmath>

namespace NereusSDR {

// NereusSDR: the Longpath values, see gui/instruments/InstrumentStyle.h.
namespace Style = ::NereusSDR::InstrumentStyle;

namespace {

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:32 [@551576e3]
// Rand um die Zeichenflaeche. Oben mehr, weil dort die Beschriftungen
// der Kanten und die Breitenpille sitzen; unten fuer die Skala.
constexpr int kPadX      = 6;
constexpr int kPadTop    = 30;
constexpr int kPadBottom = 20;

// NereusSDR: how far under the plot a new trace can change a pixel. The
// curve is held between six rows under the plot's top and two over its
// last row (top and bot in paintEvent), and the widest halo stroke (8.0)
// reaches four rows beyond the curve, so two under the plot's last row.
// Six leaves room to spare. tst_bandwidth_filter_pane_paint measures the
// reach and fails if a trace ever changes a row outside the band.
constexpr int kTraceBandBelow = 6;

// NereusSDR: the largest whole pixel ratio the kept ground picture is
// made for, and the most device pixels it may hold (6 MB at four bytes
// each; a 620x105 pane on a Retina screen is a sixth of that). Beyond
// either the ground is drawn directly, as it always was.
constexpr int kBaseLayerMaxScale = 4;
constexpr qint64 kBaseLayerMaxPixels = 1500000;

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:462 [@551576e3]
// NereusSDR: the halo passes of paintEvent ("Hof, dann Linie"), widest
// and palest first. At file scope so the pens are made once
// (resolveColors) and not on every paint.
struct GlowPass { double width; int alpha; };
constexpr std::array<GlowPass, 3> kGlow = {{ {8.0, 14}, {4.0, 30}, {2.5, 60} }};

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:39 [@551576e3]
// Eine Zahl auf der Achse: Megahertz mit drei Nachkommastellen, wie in
// der Vorlage („13.137"). Kilohertz waeren eine Ziffer mehr und sagen
// dasselbe.
QString axisLabel(double hz)
{
    return QString::number(hz / 1.0e6, 'f', 3);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:55 [@551576e3]
// Eine Kantenangabe. Unter 1 kHz in Hertz, darueber in Kilohertz mit
// zwei Stellen — so macht es die Vorlage, und so bleibt die Zahl kurz
// genug, um ueber der Kante zu stehen.
//
// Betreiber 2026-09-03, mit Nachdruck: "minus darf nie!!!!!" — LSB legt
// beide Kanten unterhalb des Traegers, technisch also negativ, aber das
// gehoert nicht in die Anzeige. std::abs() schuetzte bisher nur die
// KILOHERTZ-JA-ODER-NEIN-Schwelle oben, nicht die ausgegebene Zahl
// selbst — "%1" mit dem rohen (moeglicherweise negativen) hz ergab
// "-150 Hz". Jetzt wird ueberall dieselbe Betragszahl verwendet wie die
// LOW/WIDTH/HIGH-Felder daneben (BandwidthFilterApplet::refreshNumbers).
QString cutLabel(int hz)
{
    const int mag = std::abs(hz);
    if (mag < 1000) {
        return QStringLiteral("%1 Hz").arg(mag);
    }
    return QStringLiteral("%1 kHz").arg(mag / 1000.0, 0, 'f', 2);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:64 [@551576e3]
QString widthLabel(int hz)
{
    // Same "minus darf nie" guard as cutLabel() above -- m_high - m_low
    // can go negative if the edges are inverted (a distinct, separately
    // fixed bug), and this label must never expose that as a literal
    // minus sign either.
    const int mag = std::abs(hz);
    if (mag < 1000) { return QStringLiteral("%1 Hz").arg(mag); }
    return QStringLiteral("%1 kHz").arg(mag / 1000.0, 0, 'f', 1);
}

} // namespace

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:77 [@551576e3]
BandwidthFilterPane::BandwidthFilterPane(QWidget* parent)
    : QWidget(parent)
    , m_accent(QColor(Style::role("accent", Style::kAccent)))
{
    // Klein genug fuer den Platz im RxApplet (dort rund 92 px), gross
    // genug, dass Beschriftungen und Achse hineinpassen.
    setMinimumHeight(84);
    setMinimumWidth(180);
    setMouseTracking(true);
    // NereusSDR: paintEvent fills the whole rect with the opaque inset
    // ground before anything else, in every state, so nothing behind the
    // pane needs repainting first. Set where the color is resolved.
    resolveColors();
}

// NereusSDR-original. The role colors, and everything made from them
// alone, looked up once and again when the widget's palette or style
// changes. The values and the arithmetic are those paintEvent used on
// every paint; the cites are on the drawing that uses them.
void BandwidthFilterPane::resolveColors()
{
    Colors& c = m_colors;
    c.insetBg = QColor(Style::role("inset-bg", Style::kInsetBg));
    c.grid = QColor(Style::role("spectrum-grid", Style::kSpectrumGrid));
    c.grid.setAlpha(28);
    c.gridLine = c.grid;
    c.gridLine.setAlpha(44);
    c.accent = QColor(Style::role("accent", Style::kAccent));
    c.passTop = c.accent;
    c.passTop.setAlpha(54);
    c.passBottom = c.accent;
    c.passBottom.setAlpha(24);
    c.border = QColor(Style::role("border", Style::kBorder));
    c.traceLine = QColor(Style::role("measured", Style::kAmberText));
    c.fillTop = c.traceLine;
    c.fillTop.setAlpha(96);
    c.fillBottom = c.traceLine;
    c.fillBottom.setAlpha(0);
    c.innerTop = c.traceLine;
    c.innerTop.setAlpha(150);
    c.innerBottom = c.traceLine;
    c.innerBottom.setAlpha(64);
    for (size_t i = 0; i < kGlow.size(); ++i) {
        QColor glow(c.traceLine);
        glow.setAlpha(kGlow[i].alpha);
        QPen glowPen(glow, kGlow[i].width);
        glowPen.setJoinStyle(Qt::RoundJoin);
        glowPen.setCapStyle(Qt::RoundCap);
        c.glowPens[i] = glowPen;
    }
    c.linePen = QPen(c.traceLine, 0.75);
    c.faint = QColor(Style::role("text-scale", Style::kTextScale));
    QColor avg(c.faint);
    avg.setAlpha(60);
    c.avgPen = QPen(avg, 1.0);
    c.edge = c.accent.lighter(140);
    c.edgePen = QPen(c.edge, 1);
    c.handlePen = QPen(c.edge, 1.0);
    c.handlePenActive = QPen(c.edge, 1.5);
    c.tick = c.edge;
    c.tick.setAlphaF(0.85f);
    c.handleTop = QColor(Style::kGlassBtnTop);
    c.handleBottom = QColor(Style::kGlassBtnBot);
    c.handleTopActive = QColor(Style::kGlassSelTop);
    c.handleBottomActive = QColor(Style::kGlassSelBot);
    c.ink = QColor(Style::role("text", Style::kTextPrimary));
    c.labelText = QColor(Style::role("text-secondary", Style::kTextSecondary));

    // The kept ground picture was drawn with the old colors.
    m_base = QImage();
    // Opaque only while the ground color has no transparency: otherwise
    // what is behind the pane would show and must be painted first.
    setAttribute(Qt::WA_OpaquePaintEvent, c.insetBg.isValid() && c.insetBg.alpha() == 255);
}

// NereusSDR-original. The three fonts and their metrics, made again only
// when the widget's font is another one.
void BandwidthFilterPane::resolveFonts()
{
    if (m_valueMetrics.has_value() && m_fontBase == font()
        && m_readableFonts == WorkspacePreview::enabled()) {
        return;
    }
    m_fontBase = font();
    m_readableFonts = WorkspacePreview::enabled();
    m_valueFont = WorkspacePreview::readableFont(Style::monoFont(m_fontBase, Style::kFontSmall));
    m_smallFont = WorkspacePreview::readableFont(Style::monoFont(m_fontBase, Style::kFontCaption));
    m_capsFont = WorkspacePreview::readableFont(Style::capsFont(m_fontBase, 8));
    m_valueMetrics.emplace(m_valueFont);
    m_capsMetrics.emplace(m_capsFont);
}

// NereusSDR-original.
void BandwidthFilterPane::changeEvent(QEvent* ev)
{
    if (ev->type() == QEvent::PaletteChange || ev->type() == QEvent::StyleChange) {
        resolveColors();
    }
    QWidget::changeEvent(ev);
}

// NereusSDR-original. The rows a new trace can change: the plot and a
// few rows under it (kTraceBandBelow). Nothing above the plot's first row
// and nothing lower than that depends on the trace or on the average
// line: the label strip, the chips and the axis numbers are drawn from
// the filter, the label, the VFO and the span alone.
QRect BandwidthFilterPane::traceBand() const
{
    const QRect r = plotRect();
    return QRect(0, r.top(), width(), r.height() + kTraceBandBelow).intersected(rect());
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:88 [@551576e3]
void BandwidthFilterPane::setLabel(const QString& text)
{
    if (m_label == text) { return; }
    m_label = text;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:95 [@551576e3]
void BandwidthFilterPane::setAccent(const QColor& c)
{
    if (m_accent == c) { return; }
    m_accent = c;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:102 [@551576e3]
void BandwidthFilterPane::setVfoFrequency(double hz)
{
    if (qFuzzyCompare(m_vfoHz, hz)) { return; }
    m_vfoHz = hz;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:109 [@551576e3]
void BandwidthFilterPane::setTrace(const QVector<float>& dbm)
{
    // ── Beruhigen, wie es OpenHPSDR zeigt ───────────────────────────
    //
    // Der Betreiber am 2026-08-23: "extrem unruhig, linie zu dick!
    // baue es genau wie bei openhpsdr."
    //
    // Die Rohwerte kommen mit 20 Bildern je Sekunde und sind
    // Spitzenwerte — jede Stuetzstelle springt bei jedem Bild. Bei
    // OpenHPSDR steht dort eine ruhige, duenne Kurve.
    //
    // Zwei Mittel, in dieser Reihenfolge:
    //
    //   1. ZEITLICH: exponentiell gleiten mit alpha = 0,22. Das ist
    //      dieselbe Familie wie die Glaettung im Panadapter
    //      (LogRecursive), nur ohne dessen Parameterwerk — hier geht
    //      es ums Augenmass beim Kantenziehen, nicht um Messtechnik.
    //      Ein Traeger steigt damit in rund einer Zehntelsekunde auf,
    //      das Zappeln verschwindet.
    //
    // ÖRTLICH wird NICHT geglaettet, und das ist eine Korrektur:
    //
    // Die erste Fassung mittelte ueber drei Stuetzstellen, mit dem
    // Kommentar, das verschlucke keinen echten Traeger — "der ist
    // breiter als drei Punkte". GEMESSEN war das falsch: ein Traeger
    // auf EINER Stuetzstelle, -60 dBm im -120er Rauschen, kam nach dem
    // Fenster bei -100 an. VIERZIG Dezibel weg, durch eine Zeile, die
    // ich fuer harmlos erklaert hatte.
    //
    // Zeitliches Gleiten allein genuegt: Rauschen ist von Bild zu Bild
    // zufaellig und mittelt sich damit ohnehin weg, ein Traeger steht
    // still und bleibt stehen.
    if (dbm.isEmpty()) {
        if (!m_trace.isEmpty()) { m_trace.clear(); update(); }
        return;
    }

    const QVector<float>& in = dbm;

    if (m_trace.size() != in.size()) {
        m_trace = in;                      // Groesse gewechselt: neu setzen
    } else {
        // From Longpath src/gui/widgets/BandwidthFilterPane.cpp:165 [@551576e3]
        // ── Schnell hoch, gemaechlich runter ────────────────────────
        //
        // Die erste Fassung glich in BEIDE Richtungen gleich schnell
        // (alpha 0,22). Der Betreiber sah daraufhin: "die form bleibt
        // auch immer leicht zu sehen, zeitversetzt" — ein blasses
        // Nachbild, das der Kurve hinterherlaeuft. Genau das macht
        // eine symmetrische Glaettung: sie verzoegert das Steigen
        // ebenso wie das Fallen.
        //
        // Richtig ist ungleich: STEIGEN sofort (ein Traeger, der
        // aufgeht, soll da sein, wenn er da ist), FALLEN gemaechlich
        // (dann beruhigt sich das Rauschen). Dieselbe Bauart wie ein
        // Spitzenwertzeiger mit Ruecklauf — und das ist auch, was
        // OpenHPSDR und Thetis an dieser Stelle zeigen.
        constexpr float aUp   = 0.75f;   // fast sofort
        constexpr float aDown = 0.16f;   // ruhiger Ruecklauf
        for (int i = 0; i < in.size(); ++i) {
            const float a = (in[i] > m_trace[i]) ? aUp : aDown;
            m_trace[i] = m_trace[i] * (1.0f - a) + in[i] * a;
        }
    }
    // NereusSDR: only the rows the curve can reach, where Longpath repaints
    // the whole pane (update()). Still one paint per trace, so the average
    // line, which moves a step on every paint, keeps its speed.
    update(traceBand());
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:175 [@551576e3]
void BandwidthFilterPane::setSpan(int hz)
{
    // Unter 2 kHz wird die Achse unlesbar, ueber 40 kHz verschwindet
    // der Durchlass zu einem Strich.
    const int clamped = std::clamp(hz, 2000, 40000);
    if (m_spanHz == clamped) { return; }
    m_spanHz = clamped;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:185 [@551576e3]
void BandwidthFilterPane::setFilter(int low, int high)
{
    if (m_low == low && m_high == high) { return; }
    m_low  = low;
    m_high = high;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:193 [@551576e3]
void BandwidthFilterPane::setHasFrequency(bool on)
{
    if (m_hasFrequency == on) { return; }
    m_hasFrequency = on;
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:200 [@551576e3]
QSize BandwidthFilterPane::sizeHint() const
{
    return QSize(360, 140);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:205 [@551576e3]
QRect BandwidthFilterPane::plotRect() const
{
    return QRect(kPadX, kPadTop,
                 std::max(1, width() - 2 * kPadX),
                 std::max(1, height() - kPadTop - kPadBottom));
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:212 [@551576e3]
int BandwidthFilterPane::hzToX(int hz) const
{
    const QRect r = plotRect();
    const double frac = (static_cast<double>(hz) + m_spanHz / 2.0) / m_spanHz;
    return r.left() + static_cast<int>(std::lround(frac * r.width()));
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:219 [@551576e3]
int BandwidthFilterPane::xToHz(int x) const
{
    const QRect r = plotRect();
    const double frac = static_cast<double>(x - r.left())
                      / std::max(1, r.width());
    return static_cast<int>(std::lround(frac * m_spanHz - m_spanHz / 2.0));
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:227 [@551576e3]
BandwidthFilterPane::Zone BandwidthFilterPane::zoneAt(int x) const
{
    const int xl = hzToX(m_low);
    const int xh = hzToX(m_high);

    // Bei beiden in Reichweite gewinnt die naehere — sonst laesst sich
    // ein sehr schmaler Durchlass nur an einer Seite anfassen.
    const bool nearLow  = std::abs(x - xl) <= kGrabPx;
    const bool nearHigh = std::abs(x - xh) <= kGrabPx;
    if (nearLow && nearHigh) {
        return (std::abs(x - xl) <= std::abs(x - xh)) ? Zone::LowEdge
                                                      : Zone::HighEdge;
    }
    if (nearLow)  { return Zone::LowEdge; }
    if (nearHigh) { return Zone::HighEdge; }
    if (x > xl && x < xh) { return Zone::Body; }
    return Zone::None;
}

// NereusSDR-original: native cursor shapes only. Open hand over the body,
// horizontal resize at the edges, arrow elsewhere. Qt::SizeAllCursor is
// image-drawn on macOS and crashes Qt 6.11.0 (comment at the title bar
// cursor in gui/containers/ContainerWidget.cpp; guarded by
// scripts/verify-no-image-cursors.py and tst_native_cursors).
Qt::CursorShape BandwidthFilterPane::hoverCursor(Zone z)
{
    if (z == Zone::LowEdge || z == Zone::HighEdge) { return Qt::SizeHorCursor; }
    if (z == Zone::Body) { return Qt::OpenHandCursor; }
    return Qt::ArrowCursor;
}

// ── Zeichnen ─────────────────────────────────────────────────────────
//
// Richtung „Glas & Tiefe", vom Betreiber am 2026-09-17 aus vier
// Stilblaettern gewaehlt (docs/design/2026-09-17-design-durchsicht.md).
// Die Flaeche ist VERSENKT — Schwarz, Innenschatten oben, Lichtkante —
// wie ein Instrument hinter Glas. Die Kurve traegt einen Hof, der
// Durchlass ist ein Glasstreifen, die Zahlen sitzen in Chips.
//
// Was aus den Tagen davor bleibt, weil es nichts mit dem Look zu tun
// hat, sondern mit dem Lesen:
//   * fester Pegelbereich, 40 dB ueber dem 10. Perzentil (2026-08-23,
//     "wo kein signal ist, ist die linie am boden");
//   * gerade Striche, keine Rundung (2026-08-23, die Vorlage hat
//     scharfe Ecken);
//   * schnell hoch, gemaechlich runter (setTrace);
//   * Kantenwerte ohne Vorzeichen (2026-09-03, "minus darf nie");
//   * Wortmarken tauschen bei LSB die Seite (2026-09-17);
//   * die Kurve bekommt das ganze Feld (2026-09-17, "schaut eher flach").
//
// Was weg ist: die Anteilszellen und die Feinskala im Durchlass. Das
// gewaehlte Blatt hatte beides nicht; die Zellen lagen als Kaesten
// ueber der gefuellten Flaeche, die Feinskala war ein zweiter Rahmen.

namespace {

// Die Mal-Helfer (paintInsetTop, paintGlassChip) liegen seit dem
// 2026-09-18 in StyleConstants.h — der Panadapter-Chrom braucht sie
// auch.
using Style::paintInsetTop;
using Style::paintGlassChip;

} // namespace

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:286 [@551576e3]
// NereusSDR: the first part of Longpath's paintEvent, everything under
// the curve: ground, grid, passband strip and VFO line. Its own function
// so that it can be drawn either straight onto the pane or once into the
// kept picture (stampBase). The drawing is Longpath's, line for line.
// [original inline comments from BandwidthFilterPane.cpp:286, :320-323
// and :340]
void BandwidthFilterPane::paintBase(QPainter& p, const QRect& r,
                                    const QVector<int>& marks, int xl, int xh) const
{
    const Colors& c = m_colors;

    // ── Grund: versenkt ─────────────────────────────────────────────
    p.fillRect(rect(), c.insetBg);
    paintInsetTop(p, rect());
    p.setPen(QColor(255, 255, 255, 6));
    p.drawLine(0, height() - 1, width() - 1, height() - 1);

    p.setPen(c.grid);
    for (int hz : marks) {
        const int x = hzToX(hz);
        p.drawLine(x, r.top() - 6, x, r.bottom());
    }

    // ── Der Durchlass als Glasstreifen ──────────────────────────────
    //
    // Von oben bis unten (Betreiber 2026-09-17), als Verlauf: oben
    // etwas dichter, unten leiser, mit einer Lichtkante am oberen
    // Rand. Er liegt UNTER der Kurve — die Kurve ist die Messung, der
    // Streifen nur das Fenster, durch das man sie hoert.
    {
        QLinearGradient pass(0, 0, 0, height());
        pass.setColorAt(0.0, c.passTop);
        pass.setColorAt(1.0, c.passBottom);
        p.fillRect(QRect(xl, 0, std::max(1, xh - xl), height()), pass);
        p.setPen(QColor(255, 255, 255, 26));
        p.drawLine(xl + 1, 0, xh - 1, 0);
    }

    // Die Nulllinie ist die VFO-Frequenz.
    {
        const int x0 = hzToX(0);
        p.setPen(c.border);
        p.drawLine(x0, r.top() - 6, x0, r.bottom());
    }
}

// NereusSDR-original. Whether the ground may be stamped from a kept
// picture on this painter, and at how many device pixels per Qt pixel;
// 0 means draw it directly.
//
// The stamp is used only where it is exact, not merely close: the painter
// maps Qt pixels to device pixels by a whole factor and a whole-pixel
// shift, nothing else (no fractional pixel ratio, no scaled or rotated
// preview), and it draws into an 8-bit image of the two formats Qt's
// window pictures use. The kept picture then has the same format and the
// same factor, so every pixel in it is computed exactly as it would be on
// the pane, and it is copied, not blended.
int BandwidthFilterPane::baseLayerScale(const QPainter& p, QImage::Format* format) const
{
    const QTransform t = p.deviceTransform();
    if (t.type() > QTransform::TxScale) {
        return 0;
    }
    const qreal s = t.m11();
    const int k = static_cast<int>(s);
    if (k < 1 || k > kBaseLayerMaxScale || static_cast<qreal>(k) != s || t.m22() != s) {
        return 0;
    }
    if (static_cast<qint64>(width()) * height() * k * k > kBaseLayerMaxPixels) {
        return 0;
    }
    if (t.dx() != std::floor(t.dx()) || t.dy() != std::floor(t.dy())) {
        return 0;
    }
    const QPaintEngine* engine = p.paintEngine();
    const QPaintDevice* device = (engine != nullptr) ? engine->paintDevice() : nullptr;
    if (device == nullptr || device->devType() != QInternal::Image) {
        return 0;
    }
    const QImage::Format f = static_cast<const QImage*>(device)->format();
    if (f != QImage::Format_RGB32 && f != QImage::Format_ARGB32_Premultiplied) {
        return 0;
    }
    *format = f;
    return k;
}

// NereusSDR-original. The ground from the kept picture, which is drawn
// again (by paintBase, the same code that draws it directly) whenever the
// size, the pixel factor, the format, the VFO, the span or a filter edge
// is another one; resolveColors drops it when the colors change.
void BandwidthFilterPane::stampBase(QPainter& p, int scale, QImage::Format format,
                                    const QRect& r, const QVector<int>& marks, int xl, int xh)
{
    const BaseKey key{size(), scale, format, m_vfoHz, m_spanHz, m_low, m_high};
    if (m_base.isNull() || !(key == m_baseKey)) {
        m_base = QImage(size() * scale, format);
        m_base.setDevicePixelRatio(scale);
        QPainter layer(&m_base);
        layer.setRenderHint(QPainter::Antialiasing, false);
        paintBase(layer, r, marks, xl, xh);
        layer.end();
        // Stamped one device pixel for one image pixel, below.
        m_base.setDevicePixelRatio(1.0);
        m_baseKey = key;
    }
    p.save();
    p.scale(1.0 / scale, 1.0 / scale);
    p.setCompositionMode(QPainter::CompositionMode_Source);
    p.drawImage(QPoint(0, 0), m_base);
    p.restore();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:279 [@551576e3]
void BandwidthFilterPane::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);

    const QRect r = plotRect();
    const Colors& c = m_colors;
    resolveFonts();

    // ── Die Marken der Achse: auf RUNDEN Frequenzen ─────────────────
    //
    // Bisher standen sie bei VFO ± k·Schritt. Steht der VFO auf
    // 7.192.500, liegen sie alle auf halben Kilohertz, und die
    // dreistellige Anzeige rundet mal auf, mal ab: 7.189 · 7.191 ·
    // 7.192 · 7.194 · 7.197 — gleiche Abstaende, ungleiche Zahlen, so
    // auf dem Foto vom 2026-09-17. Die Achse gehoert dem Band, nicht
    // dem VFO: Marken auf 7.188 · 7.190 · 7.192 …, die VFO-Linie steht
    // dazwischen, wo sie hingehoert. Gitter und Beschriftung nehmen
    // dieselbe Liste, damit die Zahlen etwas zum Festhalten haben.
    const int stepHz = (m_spanHz <= 6000) ? 1000
                     : (m_spanHz <= 24000) ? 2000 : 5000;
    // NereusSDR: the list is a member, emptied and filled again, so no
    // paint allocates one.
    QVector<int>& marks = m_marks;
    marks.resize(0);
    {
        const double first = std::ceil((m_vfoHz - m_spanHz / 2.0) / stepHz) * stepHz;
        for (double f = first; f <= m_vfoHz + m_spanHz / 2.0; f += stepHz) {
            marks << static_cast<int>(std::lround(f - m_vfoHz));
        }
    }

    const int xl = hzToX(m_low);
    const int xh = hzToX(m_high);

    // NereusSDR: ground, grid, strip and VFO line (paintBase). They change
    // only with the size, the VFO, the span and the filter, so where a
    // stamp is exact they come from a kept picture.
    QImage::Format baseFormat = QImage::Format_Invalid;
    const int baseScale = baseLayerScale(p, &baseFormat);
    if (baseScale > 0) {
        stampBase(p, baseScale, baseFormat, r, marks, xl, xh);
    } else {
        paintBase(p, r, marks, xl, xh);
    }

    // ── Das Signal ──────────────────────────────────────────────────
    //
    // Der Ausschnitt kommt vom Panadapter (SpectrumWidget::dbmOverRange)
    // — dieselbe Abbildung, dieselbe Kalibrierung. Boden ist das 10.
    // Perzentil (ein Ausreisser verschiebt sonst die Skala).
    //
    // Die Decke lag bis 2026-09-30 fest 40 dB darueber, und ein starkes
    // Signal stiess oben an — der Gipfel lief flach am Rand entlang, was
    // aussieht, als schluge die Kurve aus dem Rahmen. Betreiber am
    // 2026-09-30 an einem S9+12-Signal auf 40 m: "bandwith schlaegt ueber
    // den rahmen".
    //
    // Jetzt geht die Decke mit, sobald die Spitze sie erreicht: 40 dB
    // bleiben das MINDESTmass, damit ein ruhiges Band nicht zur flachen
    // Linie zusammenfaellt und die Skala zwischen zwei Baendern
    // vergleichbar bleibt — aber ein lautes Signal bekommt den Platz, den
    // es braucht, plus 3 dB Luft, damit der Gipfel nicht am Rahmen klebt.
    const int top = r.top() + 6;
    const int bot = r.bottom() - 2;
    if (m_trace.size() >= 2 && bot > top) {
        // From Longpath src/gui/widgets/BandwidthFilterPane.cpp:368 [@551576e3]
        // NereusSDR: Longpath sorts a copy of the whole trace to read two
        // values from it, the one a tenth of the way up and the largest.
        // Those two are found here without ordering the rest, in a list
        // kept between paints; they are the same two values.
        QVector<float>& sorted = m_sortScratch;
        sorted = m_trace;
        const auto tenth = sorted.begin() + sorted.size() / 10;
        std::nth_element(sorted.begin(), tenth, sorted.end());
        const float lo = *tenth;
        const float spitze = *std::max_element(tenth, sorted.end());
        const float hi = std::max(lo + 40.0f, spitze + 3.0f);
        const double yScale = (bot - top) / static_cast<double>(hi - lo);

        // NereusSDR: the point list is a member, emptied and filled again.
        QPolygonF& poly = m_poly;
        poly.resize(0);
        poly.reserve(m_trace.size() + 2);
        poly << QPointF(r.left(), bot);
        for (int i = 0; i < m_trace.size(); ++i) {
            const double x = r.left()
                + (r.width() - 1.0) * i / (m_trace.size() - 1.0);
            const double y = bot - (m_trace[i] - lo) * yScale;
            poly << QPointF(x, qBound<double>(top, y, bot));
        }
        poly << QPointF(r.right(), bot);
        QPainterPath tracePath;
        tracePath.addPolygon(poly);

        // ── Pegelraster mit dBm-Marke ───────────────────────────────
        //
        // Drei waagrechte Linien. Die Zahlen nur, wenn sie Platz
        // haben (2026-09-17: bei elf Punkten Abstand beruehrten sich
        // alle drei) — eine Linie ohne Zahl ist immer noch ein Raster.
        {
            p.setFont(m_smallFont);
            const QColor& gridLine = c.gridLine;
            const QColor& gridText = c.faint;
            const int spacing = (bot - top) / 4;
            const int textHeight = WorkspacePreview::enabled() ? qMax(12, p.fontMetrics().height()) : 12;
            const bool roomForAll = spacing >= qMax(14, textHeight);
            const bool roomForOne = spacing >= (WorkspacePreview::enabled() ? textHeight : 10);
            for (int k = 1; k <= 3; ++k) {
                const double frac = k / 4.0;
                const int y = static_cast<int>(bot - (bot - top) * frac);
                p.setPen(gridLine);
                p.drawLine(r.left() + 1, y, r.right() - 1, y);
                if (!roomForAll && !(roomForOne && k == 2)) { continue; }
                const int dbm = static_cast<int>(std::lround(lo + (hi - lo) * frac));
                p.setPen(gridText);
                p.drawText(QRect(r.right() - 46, y - (WorkspacePreview::enabled() ? textHeight / 2 : 7), 42, textHeight),
                           Qt::AlignRight | Qt::AlignVCenter,
                           QStringLiteral("%1").arg(dbm));
            }
        }

        const int xlF = qBound(r.left(), xl, r.right());
        const int xhF = qBound(r.left(), xh, r.right());

        // Kantenglaettung NUR fuer den Kurvenzug: Raster, Streifen und
        // Griffe sind waagrecht und senkrecht, Glaettung machte sie
        // nur unscharf.
        p.setRenderHint(QPainter::Antialiasing, true);

        // ── Verlauf unter der ganzen Kurve ──────────────────────────
        {
            QLinearGradient grad(0, top, 0, bot);
            grad.setColorAt(0.0, c.fillTop);
            grad.setColorAt(1.0, c.fillBottom);
            p.setPen(Qt::NoPen);
            p.setBrush(grad);
            p.drawPath(tracePath);
        }

        // ── Im Durchlass gefuellt ───────────────────────────────────
        //
        // Was HOERBAR ist, steht als Flaeche da (2026-09-17: "leider
        // ist da nur ein kleiner strich"); ausserhalb bleibt der zarte
        // Verlauf — man sieht auf einen Blick, was die Kante
        // abschneidet.
        {
            p.save();
            p.setClipRect(QRect(xlF, top, std::max(1, xhF - xlF), bot - top + 1));
            QLinearGradient inner(0, top, 0, bot);
            inner.setColorAt(0.0, c.innerTop);
            inner.setColorAt(1.0, c.innerBottom);
            p.setPen(Qt::NoPen);
            p.setBrush(inner);
            p.drawPath(tracePath);
            p.restore();
        }

        // ── Hof, dann Linie ─────────────────────────────────────────
        //
        // Der Hof ist es, was der Betreiber am OpenHPSDR-Bild als
        // "wirkt eher 3D" beschrieben hat (2026-08-23): kein breiter
        // Strich, sondern Durchgaenge von breit und blass nach schmal
        // und kraeftig. Drei, seit die Stuetzstellen dicht genug
        // liegen, dass keine Zacke zum Klumpen wird.
        {
            // NereusSDR: the widths and alphas are kGlow at the top of the
            // file; the three pens are made from them in resolveColors.
            p.setBrush(Qt::NoBrush);
            for (const QPen& glowPen : c.glowPens) {
                p.setPen(glowPen);
                p.drawPath(tracePath);
            }
        }
        p.setPen(c.linePen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(tracePath);

        // ── Die blasse Bezugslinie ──────────────────────────────────
        //
        // Der GEGLAETTETE Mittelwert, traege (alpha 0,02): sagt, wo
        // der Empfaenger im Mittel steht, und macht sichtbar, ob eine
        // Spitze wirklich heraussticht oder nur der Flur atmet.
        {
            if (m_avgLine.size() != m_trace.size()) {
                m_avgLine = m_trace;
            } else {
                // From Longpath src/gui/widgets/BandwidthFilterPane.cpp:488 [@551576e3]
                constexpr float aa = 0.02f;
                for (int i = 0; i < m_trace.size(); ++i) {
                    m_avgLine[i] = m_avgLine[i] * (1.0f - aa) + m_trace[i] * aa;
                }
            }
            p.setPen(c.avgPen);
            // NereusSDR: the point list is a member, emptied and filled again.
            QPolygonF& ap = m_avgPoly;
            ap.resize(0);
            ap.reserve(m_avgLine.size());
            for (int i = 0; i < m_avgLine.size(); ++i) {
                const double x = r.left()
                    + (r.width() - 1.0) * i / (m_avgLine.size() - 1.0);
                const double y = bot - (m_avgLine[i] - lo) * yScale;
                ap << QPointF(x, qBound<double>(top, y, bot));
            }
            p.drawPolyline(ap);
        }
        p.setRenderHint(QPainter::Antialiasing, false);
    }

    // ── Die Kanten ──────────────────────────────────────────────────
    //
    // Etwas heller als das Auswahlblau, damit sie auf dem Streifen
    // stehen; ueber die ganze Hoehe, weil die Zahlen oben und die
    // Achse unten zu dieser Saeule gehoeren.
    // NereusSDR: the edge color (accent.lighter(140), Longpath :514) and
    // its pens are made in resolveColors.
    p.setPen(c.edgePen);
    p.drawLine(xl, 0, xl, height());
    p.drawLine(xh, 0, xh, height());

    // ── Griffe: erhabene Pillen in halber Hoehe ─────────────────────
    //
    // Die ganze Kante als Griff waere groesser, sagt aber nicht, WO
    // man fassen soll. Erhaben: Verlauf, Lichtkante oben, dunkle
    // Unterkante — das Gegenstueck zur versenkten Flaeche.
    auto drawHandle = [&](int x, bool active) {
        const QRect h(x - 4, r.center().y() - 11, 9, 22);
        QLinearGradient g(0, h.top(), 0, h.bottom());
        g.setColorAt(0.0, active ? c.handleTopActive : c.handleTop);
        g.setColorAt(1.0, active ? c.handleBottomActive : c.handleBottom);
        p.save();
        p.setRenderHint(QPainter::Antialiasing, true);
        p.setPen(QColor(0, 0, 0, 140));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(h.translated(0, 1), 4, 4);        // Schatten
        p.setPen(active ? c.handlePenActive : c.handlePen);
        p.setBrush(g);
        p.drawRoundedRect(h, 4, 4);
        p.setPen(QColor(255, 255, 255, Style::kGlassLightAlpha + 8));
        p.drawLine(h.left() + 3, h.top() + 1, h.right() - 3, h.top() + 1);   // Lichtkante
        p.setPen(c.tick);
        p.drawLine(x, h.top() + 6, x, h.bottom() - 6);
        p.restore();
    };
    drawHandle(xl, m_hover == Zone::LowEdge || m_drag == Zone::LowEdge);
    drawHandle(xh, m_hover == Zone::HighEdge || m_drag == Zone::HighEdge);

    // ── Beschriftungen oben ─────────────────────────────────────────
    //
    // Zahlen Monospace (Hausstil Regel 4), auf der Schriftleiter: 11
    // fuer Werte, 9 fuer Achse und Marken. Wortmarken als Versalzeile.
    // NereusSDR: fonts from resolveFonts, colors from resolveColors.
    const QFont& value = m_valueFont;
    const QFont& small = m_smallFont;
    const QColor& faint = c.faint;
    const QColor& ink = c.ink;

    // Eng: zuerst faellt das WORT, dann die Zahl (2026-08-22). Gemessen
    // am Platz zwischen den Griffen, nicht an der Fensterbreite — und
    // fuer die Zahlen am tatsaechlichen Platz NEBEN dem Breitenchip:
    // Monospace ist breiter als die Textschrift von frueher, und auf
    // dem 620×105-Blatt schob sich "3.00 kHz" in den Chip. Wo die Zahl
    // nicht neben den Chip passt, faellt sie; LOW/HIGH stehen ohnehin
    // in der Bedienzeile darunter.
    const int labelRoom = xh - xl;
    const bool wordMarks = labelRoom >= 190;
    const QString widthText = widthLabel(m_high - m_low);
    const QFontMetrics& valueMetrics = *m_valueMetrics;
    const int chipW  = valueMetrics.horizontalAdvance(widthText) + 18;
    const int captionHeight = WorkspacePreview::enabled() ? qMax(10, m_capsMetrics->height()) : 10;
    const int valueHeight = WorkspacePreview::enabled() ? qMax(13, valueMetrics.height()) : 13;
    const int valueW = std::max(valueMetrics.horizontalAdvance(cutLabel(m_low)),
                                valueMetrics.horizontalAdvance(cutLabel(m_high)));
    const bool numbers = labelRoom >= chipW + 2 * (valueW + 14);

    if (wordMarks) {
        // Die Wortmarken folgen dem Seitenband: ohne Vorzeichen muss
        // das Wort sagen, was die Zahl ist. Bei LSB liegt die nahe
        // Kante rechts — links steht also der Audio-HOCHschnitt.
        p.setFont(m_capsFont);
        p.setPen(faint);
        const bool lowerSideband = (m_low < 0 && m_high <= 0);
        p.drawText(QRect(xl + 5, 1, 90, captionHeight),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   lowerSideband ? QStringLiteral("HIGH CUT")
                                 : QStringLiteral("LOW CUT"));
        p.drawText(QRect(xh - 95, 1, 90, captionHeight),
                   Qt::AlignRight | Qt::AlignVCenter,
                   lowerSideband ? QStringLiteral("LOW CUT")
                                 : QStringLiteral("HIGH CUT"));
    }

    if (numbers) {
        p.setFont(value);
        p.setPen(ink);
        const int yVal = wordMarks ? captionHeight + 2 : 4;
        p.drawText(QRect(xl + 5, yVal, 90, valueHeight),
                   Qt::AlignLeft | Qt::AlignVCenter, cutLabel(m_low));
        p.drawText(QRect(xh - 95, yVal, 90, valueHeight),
                   Qt::AlignRight | Qt::AlignVCenter, cutLabel(m_high));
    }

    // Die Breite in der Mitte, in einem Glaschip: sie ist die Zahl,
    // nach der man den Filter benennt.
    {
        const int cx = (xl + xh) / 2;
        const QRect box(cx - chipW / 2, 7, chipW, 19);
        paintGlassChip(p, box, Style::kGlassChipRadius);
        p.setFont(value);
        p.setPen(ink);
        p.drawText(box, Qt::AlignCenter, widthText);
    }

    // ── Die Beschriftung des Empfaengers ────────────────────────────
    //
    // Eine Kapsel, kein Knopf: grau, nicht blau — sie ist keine
    // Bedienung, und "Blau ist anfassbar".
    {
        const QFont& cap = m_capsFont;
        const QFontMetrics& fm = *m_capsMetrics;
        const int tw = fm.horizontalAdvance(m_label) + 14;
        const int labelHeight = WorkspacePreview::enabled() ? qMax(14, fm.height() + 2) : 14;
        const QRect box(4, 4, tw, labelHeight);
        paintGlassChip(p, box, 7);
        p.setFont(cap);
        p.setPen(c.labelText);
        p.drawText(box, Qt::AlignCenter, m_label);
    }

    // ── Die Achse ───────────────────────────────────────────────────
    //
    // Ohne Verbindung steht hier NICHTS. Eine erfundene Frequenz waere
    // eine Behauptung — dieselbe Regel wie beim Panadapter-Kopf.
    p.setFont(small);
    if (!m_hasFrequency) {
        p.setPen(faint);
        p.drawText(QRect(0, height() - kPadBottom, width(), kPadBottom),
                   Qt::AlignCenter, QStringLiteral("no radio"));
        return;
    }

    // Zahlen mittig unter ihrer Marke, am Rand hineingeschoben statt
    // angeschnitten (".111" statt "7.111" ist schlimmer als keine).
    //
    // Eng — die Flaeche im RxApplet ist 180 Punkte breit — stuenden
    // fuenf Zahlen ineinander ("14.22014.222…" auf dem Blatt vom
    // 2026-09-17). Dann bekommt nur jede zweite (dritte, …) Marke eine
    // Zahl; die Gitterlinien bleiben alle. Gemessen an der breitesten
    // Zahl gegen den Markenabstand, nicht geraten.
    int every = 1;
    if (marks.size() >= 2) {
        int widest = 0;
        for (int hz : marks) {
            widest = qMax(widest, p.fontMetrics().horizontalAdvance(axisLabel(m_vfoHz + hz)));
        }
        const int gap = qAbs(hzToX(marks[1]) - hzToX(marks[0]));
        if (gap > 0) {
            while (every * gap < widest + 10) { ++every; }
        }
    }
    // Das Hineinschieben am Rand kann eine Zahl auf ihre Nachbarin
    // schieben (268 px breit: "14.22014.222", Blatt vom 2026-09-18).
    // Darum merkt sich die Schleife den rechten Rand der zuletzt
    // gezeichneten Zahl; was darueber laege, entfaellt — eine Zahl
    // weniger ist besser als zwei ineinander.
    int lastRight = -1000;
    for (int i = 0; i < marks.size(); ++i) {
        if (i % every != 0) { continue; }
        const int hz = marks[i];
        const int x = hzToX(hz);
        const QString t = axisLabel(m_vfoHz + hz);
        const int tw = p.fontMetrics().horizontalAdvance(t) + 8;
        int left = x - tw / 2;
        if (left < 1) { left = 1; }
        if (left + tw > width() - 1) { left = width() - 1 - tw; }
        if (left < lastRight + 4) { continue; }
        lastRight = left + tw;
        p.setPen(faint);
        p.drawText(QRect(left, height() - kPadBottom + 2, tw,
                         WorkspacePreview::enabled() ? qMax(13, p.fontMetrics().height()) : 13),
                   Qt::AlignCenter, t);
    }
}

// ── Ziehen ───────────────────────────────────────────────────────────

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:680 [@551576e3]
void BandwidthFilterPane::mousePressEvent(QMouseEvent* ev)
{
    if (ev->button() != Qt::LeftButton) { QWidget::mousePressEvent(ev); return; }

    m_drag = zoneAt(ev->pos().x());
    if (m_drag == Zone::None) { return; }

    m_dragStartX    = ev->pos().x();
    m_dragStartLow  = m_low;
    m_dragStartHigh = m_high;
    // NereusSDR: closed hand while the body is dragged (native shape, see
    // hoverCursor). The edges keep the horizontal resize arrow.
    setCursor(m_drag == Zone::Body ? Qt::ClosedHandCursor : hoverCursor(m_drag));
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:693 [@551576e3]
void BandwidthFilterPane::mouseMoveEvent(QMouseEvent* ev)
{
    const int x = ev->pos().x();

    if (m_drag == Zone::None) {
        // Der Zeiger sagt VORHER, was ein Ziehen tun wird. Nur setzen,
        // wenn er sich aendert — jedes setCursor kostet auf macOS ein
        // CGImageCreate, und bei jeder Mausbewegung ist das zu viel.
        const Zone z = zoneAt(x);
        if (z != m_hover) {
            m_hover = z;
            // NereusSDR: the body shows the native open hand where Longpath
            // shows Qt::SizeAllCursor, which crashes Qt 6.11.0 on macOS
            // (2026-10-08, hover here). Root cause at the title bar cursor in
            // gui/containers/ContainerWidget.cpp; shapes from hoverCursor().
            const Qt::CursorShape want = hoverCursor(z);
            if (cursor().shape() != want) { setCursor(want); }
            update();
        }
        return;
    }

    const int deltaHz = ((xToHz(x) - xToHz(m_dragStartX)) / kStepHz) * kStepHz;

    if (m_drag == Zone::Body) {
        // Die Breite bleibt. Deshalb die MITTE melden und nicht zwei
        // Kanten: nur so kann das Modell mit filterShift begrenzen und
        // die Breite am Rand erhalten.
        const int centre = (m_dragStartLow + m_dragStartHigh) / 2 + deltaHz;
        emit filterCentreChanged(centre);
        return;
    }

    int low  = m_dragStartLow;
    int high = m_dragStartHigh;
    if (m_drag == Zone::LowEdge)  { low  = m_dragStartLow  + deltaHz; }
    if (m_drag == Zone::HighEdge) { high = m_dragStartHigh + deltaHz; }

    // Die Kanten duerfen einander nicht ueberholen. Alles Weitere —
    // Seitenband, Deckel — entscheidet das Modell.
    if (high - low < kStepHz) {
        if (m_drag == Zone::LowEdge) { low  = high - kStepHz; }
        else                          { high = low  + kStepHz; }
    }
    emit filterChanged(low, high);
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:739 [@551576e3]
void BandwidthFilterPane::mouseReleaseEvent(QMouseEvent* ev)
{
    if (m_drag == Zone::None) { return; }
    m_drag = Zone::None;
    // NereusSDR: leave the closed hand for the shape of whatever is now
    // under the pointer.
    m_hover = zoneAt(ev->pos().x());
    const Qt::CursorShape want = hoverCursor(m_hover);
    if (cursor().shape() != want) { setCursor(want); }
    update();
}

// From Longpath src/gui/widgets/BandwidthFilterPane.cpp:747 [@551576e3]
void BandwidthFilterPane::leaveEvent(QEvent*)
{
    if (m_hover == Zone::None) { return; }
    m_hover = Zone::None;
    unsetCursor();
    update();
}

} // namespace NereusSDR
